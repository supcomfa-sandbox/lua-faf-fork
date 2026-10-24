/*
** scfa_persist: persistence of the Lua 5.1 universe for the SCFA 5.1 core.
** Derived from Pluto 2.4 (Ben Sunshine-Hill, public domain; maintained by hoelzro/Stefan Reich),
** see persist/PROVENANCE.md. Changes against Pluto, in short:
**   - built against the core's own headers (no pdep copies of 5.1.3 headers);
**   - fixed-width little-endian stream with a header that pins the build (number type, opcode set);
**   - no __persist metafunctions; typed userdata through callbacks (the engine's RType serializers);
**   - permanents only for tables/functions/userdata/threads, optional content merge for permanent tables;
**   - C frames of a suspended thread are written without a saved pc (Pluto dereferences the C function);
**   - open upvalues of a thread are re-pointed by a final fix-up pass (no O(n) unlink per upvalue);
**   - bounded recursion, stack growth past LUAI_MAXCSTACK, truncated/corrupt streams raise Lua errors,
**     the collector is restored on every exit of scfa_unpersist.
** See scfa_persist.h for the contract.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define lscfa_persist_c
#define LUA_CORE

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lgc.h"
#include "ldo.h"
#include "lmem.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"
#include "lopcodes.h"

#include "scfa_persist.h"

#ifndef setuvvalue2s
#define setuvvalue2s(L,obj,x) \
  { TValue *i_o=(obj); i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUPVAL; }
#endif

/* stream kinds */
enum { K_NIL = 0, K_BOOL = 1, K_NUM = 2, K_LUD = 3, K_REF = 4, K_NEW = 5, K_PERM = 6 };

#define MAGIC		0x54505346u	/* "FSPT" */
#define DEFAULT_DEPTH	100000
#define NOPC		0xFFFFFFFFu	/* no saved pc (C frame, base frame) */

struct scfa_persist_io {
  lua_State *L;
  lua_Writer w;
  void *wud;
  ZIO z;
  scfa_persist_readfn rf;	/* exact reader, or NULL: z */
  void *rud;
  int reading;
};

/* ------------------------------------------------------------------ io */

LUA_API lua_State *scfa_persist_state (scfa_persist_io *io) { return io->L; }

LUA_API void scfa_persist_write (scfa_persist_io *io, const void *p, size_t n) {
  if (n == 0) return;
  if (io->w(io->L, p, n, io->wud) != 0) luaL_error(io->L, "persist: the writer failed");
}

LUA_API void scfa_persist_read (scfa_persist_io *io, void *p, size_t n) {
  if (n == 0) return;
  if (io->rf) {
    if (io->rf(io->L, p, n, io->rud) != 0) luaL_error(io->L, "persist: truncated or corrupt data");
    return;
  }
  if (luaZ_read(&io->z, p, n) != 0) luaL_error(io->L, "persist: truncated or corrupt data");
}

static void put8 (scfa_persist_io *io, unsigned v) {
  unsigned char b = (unsigned char)v;
  scfa_persist_write(io, &b, 1);
}
static void put32 (scfa_persist_io *io, uint32_t v) {
  unsigned char b[4];
  b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8);
  b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24);
  scfa_persist_write(io, b, 4);
}
static unsigned get8 (scfa_persist_io *io) {
  unsigned char b;
  scfa_persist_read(io, &b, 1);
  return b;
}
static uint32_t get32 (scfa_persist_io *io) {
  unsigned char b[4];
  scfa_persist_read(io, b, 4);
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* ------------------------------------------------------------- build tag */

static char tagbuf[96];

LUA_API const char *scfa_persist_build_tag (void) {
  uint32_t h = 2166136261u;
  int i;
  for (i = 0; i < NUM_OPCODES; i++) {
    const char *s = luaP_opnames[i];
    while (*s) { h = (h ^ (unsigned char)*s++) * 16777619u; }
    h = (h ^ 0xff) * 16777619u;
  }
  sprintf(tagbuf, "scfa-persist/%d %s num=%d%s instr=%d ptr=%d ops=%d/%08x",
          SCFA_PERSIST_VERSION, LUA_VERSION, (int)sizeof(lua_Number),
#if defined(LUA_NUMBER_FLOAT32)
          "f",
#else
          "d",
#endif
          (int)sizeof(Instruction), (int)sizeof(void *), NUM_OPCODES, (unsigned)h);
  return tagbuf;
}

/* ----------------------------------------------------------- common bits */

#define PERMS	1
#define REFT	2

/* make room for n slots even past LUAI_MAXCSTACK (that limit is for C functions; we may be deep) */
static void need (lua_State *L, int n) {
  luaD_checkstack(L, n);
  if (L->ci->top < L->top + n) L->ci->top = L->top + n;
}

static void pusho (lua_State *L, const TValue *o) {
  need(L, 1);
  setobj2s(L, L->top, o);
  L->top++;
}
static void pushproto (lua_State *L, Proto *p) {
  need(L, 1);
  setptvalue2s(L, L->top, p);
  L->top++;
}
static void pushupval (lua_State *L, UpVal *uv) {
  need(L, 1);
  setuvvalue2s(L, L->top, uv);
  L->top++;
}
static void pushtstring (lua_State *L, TString *s) {
  need(L, 1);
  setsvalue2s(L, L->top, s);
  L->top++;
}

/* ============================================================== persist */

typedef struct PI {
  lua_State *L;
  scfa_persist_io io;
  const scfa_persist_opts *opts;
  int flags;
  int counter;
  int depth, maxdepth;
} PI;

static void persist (PI *pi);

static TValue *topv (lua_State *L) { return L->top - 1; }

static void persisttable_body (PI *pi) {
  lua_State *L = pi->L;
  Table *h = hvalue(topv(L));
  int narr = h->sizearray, nrec = 0, i;
  for (i = 0; i < sizenode(h); i++) if (!ttisnil(gval(gnode(h, i)))) nrec++;
  put32(&pi->io, (uint32_t)narr);
  put32(&pi->io, (uint32_t)nrec);
  need(L, 6);
  if (!lua_getmetatable(L, -1)) lua_pushnil(L);
  persist(pi);
  lua_pop(L, 1);
  lua_pushnil(L);
  while (lua_next(L, -2)) {
    lua_pushvalue(L, -2);
    persist(pi);
    lua_pop(L, 1);
    persist(pi);
    lua_pop(L, 1);
  }
  lua_pushnil(L);
  persist(pi);	/* terminator */
  lua_pop(L, 1);
}

static void persistuserdata (PI *pi) {
  lua_State *L = pi->L;
  const void *rtype = lua_getuserdatatype(L, -1);
  need(L, 4);
  if (rtype) {
    if (!pi->opts || !pi->opts->udata_save)
      luaL_error(L, "persist: no handler for typed userdata");
    put8(&pi->io, 1);
    if (pi->opts->udata_save(L, lua_gettop(L), rtype, &pi->io, pi->opts->udata_ud) != 0)
      luaL_error(L, "persist: userdata cannot be persisted");
    /* the save callback may only use the stack above idx; make sure it left it as it found it */
  }
  else {
    size_t len = lua_objlen(L, -1);
    put8(&pi->io, 0);
    put32(&pi->io, (uint32_t)len);
    scfa_persist_write(&pi->io, lua_touserdata(L, -1), len);
  }
  if (!lua_getmetatable(L, -1)) lua_pushnil(L);
  persist(pi);
  lua_pop(L, 1);
}

static void persistfunction (PI *pi) {
  lua_State *L = pi->L;
  Closure *cl = clvalue(topv(L));
  int i;
  if (cl->c.isC)
    luaL_error(L, "persist: C function %p (%d upvalues) is not in the permanents table",
               (void *)cl->c.f, (int)cl->c.nupvalues);
  put8(&pi->io, cl->l.nupvalues);
  pushproto(L, cl->l.p);
  persist(pi);
  lua_pop(L, 1);
  for (i = 0; i < cl->l.nupvalues; i++) {
    pushupval(L, cl->l.upvals[i]);
    persist(pi);
    lua_pop(L, 1);
  }
  /* environment: the loading thread's globals when it is the globals of this thread */
  need(L, 3);
  lua_getfenv(L, -1);
  if (lua_rawequal(L, -1, LUA_GLOBALSINDEX)) { lua_pop(L, 1); lua_pushnil(L); }
  persist(pi);
  lua_pop(L, 1);
}

static void persistupval (PI *pi) {
  lua_State *L = pi->L;
  UpVal *uv = gco2uv(gcvalue(topv(L)));
  pusho(L, uv->v);
  persist(pi);
  lua_pop(L, 1);
}

static void persistproto (PI *pi) {
  lua_State *L = pi->L;
  Proto *p = gco2p(gcvalue(topv(L)));
  int i;
  put32(&pi->io, (uint32_t)p->sizek);
  for (i = 0; i < p->sizek; i++) { pusho(L, &p->k[i]); persist(pi); lua_pop(L, 1); }
  put32(&pi->io, (uint32_t)p->sizep);
  for (i = 0; i < p->sizep; i++) { pushproto(L, p->p[i]); persist(pi); lua_pop(L, 1); }
  put32(&pi->io, (uint32_t)p->sizecode);
  scfa_persist_write(&pi->io, p->code, sizeof(Instruction) * p->sizecode);
  put32(&pi->io, (uint32_t)p->sizeupvalues);
  for (i = 0; i < p->sizeupvalues; i++) { pushtstring(L, p->upvalues[i]); persist(pi); lua_pop(L, 1); }
  put32(&pi->io, (uint32_t)p->sizelocvars);
  for (i = 0; i < p->sizelocvars; i++) {
    pushtstring(L, p->locvars[i].varname); persist(pi); lua_pop(L, 1);
    put32(&pi->io, (uint32_t)p->locvars[i].startpc);
    put32(&pi->io, (uint32_t)p->locvars[i].endpc);
  }
  if (p->source) pushtstring(L, p->source); else lua_pushnil(L);
  persist(pi);
  lua_pop(L, 1);
  put32(&pi->io, (uint32_t)p->sizelineinfo);
  for (i = 0; i < p->sizelineinfo; i++) put32(&pi->io, (uint32_t)p->lineinfo[i]);
  put32(&pi->io, (uint32_t)p->linedefined);
  put32(&pi->io, (uint32_t)p->lastlinedefined);
  put8(&pi->io, p->nups);
  put8(&pi->io, p->numparams);
  put8(&pi->io, p->is_vararg);
  put8(&pi->io, p->maxstacksize);
}

static void persistthread (PI *pi) {
  lua_State *L = pi->L;
  lua_State *L2 = thvalue(topv(L));
  int i, nframes;
  UpVal *uv;
  GCObject *o;
  if (L2 == L) luaL_error(L, "persist: cannot persist the running thread");
  if (L2 == G(L)->mainthread) luaL_error(L, "persist: the main thread is not in the permanents table");
  /* a thread that is neither suspended in a yield nor at its start nor dead is in the middle of running
  ** (resumed another coroutine): its C frames cannot be restored */
  if (L2->status == 0 && L2->ci != L2->base_ci)
    luaL_error(L, "persist: cannot persist a thread that is running or has resumed another thread");
  put32(&pi->io, (uint32_t)(L2->stacksize - EXTRA_STACK));
  put32(&pi->io, (uint32_t)(L2->top - L2->stack));
  for (i = 0; i < L2->top - L2->stack; i++) { pusho(L, L2->stack + i); persist(pi); lua_pop(L, 1); }
  nframes = (int)(L2->ci - L2->base_ci) + 1;
  put32(&pi->io, (uint32_t)nframes);
  for (i = 0; i < nframes; i++) {
    CallInfo *ci = L2->base_ci + i;
    uint32_t pc = NOPC;
    if (isLua(ci)) pc = (uint32_t)(ci->savedpc - ci_func(ci)->l.p->code);
    put32(&pi->io, (uint32_t)(ci->base - L2->stack));
    put32(&pi->io, (uint32_t)(ci->func - L2->stack));
    put32(&pi->io, (uint32_t)(ci->top - L2->stack));
    put32(&pi->io, (uint32_t)ci->nresults);
    put32(&pi->io, pc);
    put32(&pi->io, (uint32_t)ci->tailcalls);
  }
  put8(&pi->io, L2->status);
  put32(&pi->io, (uint32_t)(L2->base - L2->stack));
  put32(&pi->io, (uint32_t)L2->errfunc);
  pusho(L, &L2->l_gt);
  persist(pi);
  lua_pop(L, 1);
  /* open upvalues, innermost first, each with its stack slot; the boxed values are shared by ref */
  for (o = L2->openupval; o != NULL; o = uv->next) {
    uv = gco2uv(o);
    pushupval(L, uv);
    persist(pi);
    lua_pop(L, 1);
    put32(&pi->io, (uint32_t)(uv->v - L2->stack));
  }
  lua_pushnil(L);
  persist(pi);
  lua_pop(L, 1);
}

static void persist (PI *pi) {
  lua_State *L = pi->L;
  int type = lua_type(L, -1);
  int ref;
  if (++pi->depth > pi->maxdepth) luaL_error(L, "persist: structure nested too deeply (%d)", pi->maxdepth);
  need(L, 10);
  switch (type) {
    case LUA_TNIL: put8(&pi->io, K_NIL); break;
    case LUA_TBOOLEAN: put8(&pi->io, K_BOOL); put8(&pi->io, lua_toboolean(L, -1) ? 1 : 0); break;
    case LUA_TNUMBER: {
      lua_Number n = lua_tonumber(L, -1);
      put8(&pi->io, K_NUM);
      scfa_persist_write(&pi->io, &n, sizeof(n));
      break;
    }
    case LUA_TLIGHTUSERDATA: {
      uint32_t lo, hi;
      uint64_t v = (uint64_t)(uintptr_t)lua_touserdata(L, -1);
      if (!(pi->flags & SCFA_PERSIST_ALLOW_LIGHTUD)) luaL_error(L, "persist: light userdata cannot be persisted");
      lo = (uint32_t)v; hi = (uint32_t)(v >> 32);
      put8(&pi->io, K_LUD); put32(&pi->io, lo); put32(&pi->io, hi);
      break;
    }
    default: {
      lua_pushvalue(L, -1);
      lua_rawget(L, REFT);
      if (!lua_isnil(L, -1)) {
        ref = (int)(intptr_t)lua_touserdata(L, -1);
        lua_pop(L, 1);
        put8(&pi->io, K_REF);
        put32(&pi->io, (uint32_t)ref);
        break;
      }
      lua_pop(L, 1);
      ref = ++pi->counter;
      lua_pushvalue(L, -1);
      lua_pushlightuserdata(L, (void *)(intptr_t)ref);
      lua_rawset(L, REFT);
      if (type == LUA_TTABLE || type == LUA_TFUNCTION || type == LUA_TUSERDATA || type == LUA_TTHREAD) {
        lua_pushvalue(L, -1);
        lua_rawget(L, PERMS);
        if (!lua_isnil(L, -1)) {
          put8(&pi->io, K_PERM);
          put32(&pi->io, (uint32_t)ref);
          persist(pi);	/* the key */
          lua_pop(L, 1);
          if (type == LUA_TTABLE && (pi->flags & SCFA_PERSIST_PERMTABLE_CONTENT)) persisttable_body(pi);
          break;
        }
        lua_pop(L, 1);
      }
      put8(&pi->io, K_NEW);
      put32(&pi->io, (uint32_t)ref);
      put8(&pi->io, type);
      switch (type) {
        case LUA_TSTRING: {
          size_t len = lua_objlen(L, -1);
          put32(&pi->io, (uint32_t)len);
          scfa_persist_write(&pi->io, lua_tostring(L, -1), len);
          break;
        }
        case LUA_TTABLE: persisttable_body(pi); break;
        case LUA_TFUNCTION: persistfunction(pi); break;
        case LUA_TUSERDATA: persistuserdata(pi); break;
        case LUA_TTHREAD: persistthread(pi); break;
        case LUA_TPROTO: persistproto(pi); break;
        case LUA_TUPVAL: persistupval(pi); break;
        default: luaL_error(L, "persist: unknown type %d", type);
      }
    }
  }
  pi->depth--;
}

LUA_API void scfa_persist (lua_State *L, lua_Writer writer, void *wud, const scfa_persist_opts *opts) {
  PI pi;
  const char *tag = scfa_persist_build_tag();
  luaL_checktype(L, 1, LUA_TTABLE);
  if (lua_gettop(L) != 2) luaL_error(L, "persist: expected [perms, root] on the stack");
  memset(&pi, 0, sizeof(pi));
  pi.L = L; pi.io.L = L; pi.io.w = writer; pi.io.wud = wud; pi.opts = opts;
  pi.flags = opts ? opts->flags : 0;
  pi.maxdepth = (opts && opts->max_depth > 0) ? opts->max_depth : DEFAULT_DEPTH;
  put32(&pi.io, MAGIC);
  put32(&pi.io, (uint32_t)strlen(tag));
  scfa_persist_write(&pi.io, tag, strlen(tag));
  put32(&pi.io, (uint32_t)pi.flags);
  lua_newtable(L);		/* perms root reft */
  lua_insert(L, 2);		/* perms reft root */
  persist(&pi);
  lua_remove(L, 2);		/* perms root */
}

/* ============================================================ unpersist */

typedef struct UP {
  lua_State *L;
  scfa_persist_io io;
  const scfa_persist_opts *opts;
  int flags;		/* flags of the stream */
  int reft;		/* stack index of the reference table */
  int depth, maxdepth;
  Proto *dummyp;
  UpVal *dummyuv;
  TString *emptystr;
  char *scratch;
  size_t scap;
  LClosure **closures;	/* closures with upvalues, for the open-upvalue fix-up */
  size_t nclosures, capclosures;
  UpVal **pairs;	/* (boxed, open) pairs */
  size_t npairs, cappairs;
  int ok;
} UP;

static void unpersist (UP *up);

static void *grow (void *p, size_t *cap, size_t n, size_t esz) {
  if (n < *cap) return p;
  *cap = *cap ? *cap * 2 : 256;
  p = realloc(p, *cap * esz);
  return p;
}

static void registerobject (UP *up, int ref) {
  lua_State *L = up->L;
  need(L, 3);
  lua_pushlightuserdata(L, (void *)(intptr_t)ref);
  lua_pushvalue(L, -2);
  lua_rawset(L, up->reft);
}

static void checkindex (UP *up, uint32_t v, uint32_t limit, const char *what) {
  if (v > limit) luaL_error(up->L, "persist: corrupt data (%s)", what);
}

static void unpersiststring (UP *up) {
  lua_State *L = up->L;
  char small[256];
  uint32_t len = get32(&up->io);
  if (len <= sizeof(small)) {
    scfa_persist_read(&up->io, small, len);
    lua_pushlstring(L, small, len);
  }
  else {
    if (len > 0x7fffffffu) luaL_error(L, "persist: corrupt data (string size)");
    if (up->scap < len) {
      char *n = (char *)realloc(up->scratch, len);
      if (!n) luaL_error(L, "persist: not enough memory");
      up->scratch = n; up->scap = len;
    }
    scfa_persist_read(&up->io, up->scratch, len);
    lua_pushlstring(L, up->scratch, len);
  }
}

static void setmeta_from_top (UP *up) {	/* [obj mt|nil] -> [obj]; nil clears */
  lua_State *L = up->L;
  if (!lua_istable(L, -1)) {
    if (!lua_isnil(L, -1)) luaL_error(L, "persist: corrupt data (metatable)");
    lua_pop(L, 1);
    lua_pushnil(L);
  }
  lua_setmetatable(L, -2);
}

static void unpersisttable_body (UP *up, int fresh, int ref) {
  lua_State *L = up->L;
  uint32_t narr = get32(&up->io), nrec = get32(&up->io);
  need(L, 6);
  if (fresh) {
    if (narr > (1u << 26)) narr = 1u << 26;
    if (nrec > (1u << 26)) nrec = 1u << 26;
    lua_createtable(L, (int)narr, (int)nrec);
    registerobject(up, ref);
  }
  unpersist(up);
  if (fresh) setmeta_from_top(up);
  else if (!lua_isnil(L, -1)) { /* merge into a permanent table: its own metatable stays */ lua_pop(L, 1); }
  else lua_pop(L, 1);
  for (;;) {
    unpersist(up);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
    unpersist(up);
    lua_rawset(L, -3);
  }
}

static LClosure *newbox (UP *up) {	/* closure with one closed upvalue holding nil: stands for an UpVal */
  lua_State *L = up->L;
  LClosure *box = &luaF_newLclosure(L, 1, hvalue(gt(L)))->l;
  need(L, 1);
  setclvalue(L, L->top, (Closure *)box);
  L->top++;
  box->p = up->dummyp;
  box->upvals[0] = luaF_newupval(L);
  return box;
}

static void unpersistfunction (UP *up, int ref) {
  lua_State *L = up->L;
  unsigned nups = get8(&up->io);
  unsigned i;
  LClosure *lcl = &luaF_newLclosure(L, (int)nups, hvalue(gt(L)))->l;
  need(L, 3);
  setclvalue(L, L->top, (Closure *)lcl);
  L->top++;
  lcl->p = up->dummyp;
  for (i = 0; i < nups; i++) lcl->upvals[i] = up->dummyuv;
  registerobject(up, ref);
  if (nups > 0) {
    up->closures = (LClosure **)grow(up->closures, &up->capclosures, up->nclosures, sizeof(LClosure *));
    if (!up->closures) luaL_error(L, "persist: not enough memory");
    up->closures[up->nclosures++] = lcl;
  }
  unpersist(up);
  if (lua_type(L, -1) != LUA_TPROTO) luaL_error(L, "persist: corrupt data (closure proto)");
  lcl->p = gco2p(gcvalue(topv(L)));
  if (lcl->p->nups != nups) luaL_error(L, "persist: corrupt data (upvalue count)");
  lua_pop(L, 1);
  for (i = 0; i < nups; i++) {
    unpersist(up);
    if (!lua_isfunction(L, -1) || clvalue(topv(L))->c.isC || clvalue(topv(L))->l.nupvalues != 1)
      luaL_error(L, "persist: corrupt data (upvalue)");
    lcl->upvals[i] = clvalue(topv(L))->l.upvals[0];
    lua_pop(L, 1);
  }
  unpersist(up);
  if (lua_istable(L, -1)) lcl->env = hvalue(topv(L));
  else if (!lua_isnil(L, -1)) luaL_error(L, "persist: corrupt data (function environment)");
  lua_pop(L, 1);
}

static void unpersistupval (UP *up, int ref) {
  lua_State *L = up->L;
  LClosure *box = newbox(up);
  registerobject(up, ref);
  unpersist(up);
  setobj(L, box->upvals[0]->v, topv(L));
  lua_pop(L, 1);
}

static void unpersistproto (UP *up) {
  lua_State *L = up->L;
  Proto *p = luaF_newproto(L);
  uint32_t n, i;
  pushproto(L, p);
  n = get32(&up->io);
  checkindex(up, n, 1u << 24, "constants");
  p->k = luaM_newvector(L, (int)n, TValue);
  for (i = 0; i < n; i++) setnilvalue(&p->k[i]);
  p->sizek = (int)n;
  for (i = 0; i < n; i++) { unpersist(up); setobj(L, &p->k[i], topv(L)); lua_pop(L, 1); }
  n = get32(&up->io);
  checkindex(up, n, 1u << 24, "protos");
  p->p = luaM_newvector(L, (int)n, Proto *);
  for (i = 0; i < n; i++) p->p[i] = up->dummyp;	/* placeholders keep the half built proto safe for the collector */
  p->sizep = (int)n;
  for (i = 0; i < n; i++) {
    unpersist(up);
    if (lua_type(L, -1) != LUA_TPROTO) luaL_error(L, "persist: corrupt data (sub proto)");
    p->p[i] = gco2p(gcvalue(topv(L)));
    lua_pop(L, 1);
  }
  n = get32(&up->io);
  checkindex(up, n, 1u << 26, "code");
  p->code = luaM_newvector(L, (int)n, Instruction);
  p->sizecode = (int)n;
  scfa_persist_read(&up->io, p->code, sizeof(Instruction) * n);
  n = get32(&up->io);
  checkindex(up, n, 1u << 12, "upvalue names");
  p->upvalues = luaM_newvector(L, (int)n, TString *);
  for (i = 0; i < n; i++) p->upvalues[i] = up->emptystr;
  p->sizeupvalues = (int)n;
  for (i = 0; i < n; i++) {
    unpersist(up);
    if (!lua_isstring(L, -1)) luaL_error(L, "persist: corrupt data (upvalue name)");
    p->upvalues[i] = rawtsvalue(topv(L));
    lua_pop(L, 1);
  }
  n = get32(&up->io);
  checkindex(up, n, 1u << 24, "locvars");
  p->locvars = luaM_newvector(L, (int)n, LocVar);
  p->sizelocvars = (int)n;
  for (i = 0; i < n; i++) {
    p->locvars[i].varname = up->emptystr;
    p->locvars[i].startpc = p->locvars[i].endpc = 0;
  }
  for (i = 0; i < n; i++) {
    unpersist(up);
    if (!lua_isstring(L, -1)) luaL_error(L, "persist: corrupt data (local name)");
    p->locvars[i].varname = rawtsvalue(topv(L));
    lua_pop(L, 1);
    p->locvars[i].startpc = (int)get32(&up->io);
    p->locvars[i].endpc = (int)get32(&up->io);
  }
  unpersist(up);
  if (lua_isstring(L, -1)) p->source = rawtsvalue(topv(L));
  lua_pop(L, 1);
  n = get32(&up->io);
  checkindex(up, n, 1u << 26, "lineinfo");
  p->lineinfo = luaM_newvector(L, (int)n, int);
  p->sizelineinfo = (int)n;
  for (i = 0; i < n; i++) p->lineinfo[i] = (int)get32(&up->io);
  p->linedefined = (int)get32(&up->io);
  p->lastlinedefined = (int)get32(&up->io);
  p->nups = (lu_byte)get8(&up->io);
  p->numparams = (lu_byte)get8(&up->io);
  p->is_vararg = (lu_byte)get8(&up->io);
  p->maxstacksize = (lu_byte)get8(&up->io);
}

static void unpersistthread (UP *up, int ref) {
  lua_State *L = up->L;
  lua_State *L2 = lua_newthread(L);
  uint32_t stacksize, top, nframes, i;
  need(L, 4);
  registerobject(up, ref);
  stacksize = get32(&up->io);
  top = get32(&up->io);
  checkindex(up, stacksize, 1u << 22, "thread stack size");
  checkindex(up, top, stacksize + EXTRA_STACK, "thread top");
  if ((int)stacksize > L2->stacksize - EXTRA_STACK) luaD_reallocstack(L2, (int)stacksize);
  for (i = 0; i < top; i++) {
    unpersist(up);
    setobj2s(L2, L2->stack + i, topv(L));
    lua_pop(L, 1);
  }
  L2->top = L2->stack + top;
  nframes = get32(&up->io);
  checkindex(up, nframes, 1u << 20, "frame count");
  if (nframes == 0) luaL_error(L, "persist: corrupt data (no frames)");
  if ((uint32_t)L2->size_ci < nframes + 4) luaD_reallocCI(L2, (int)nframes + 4);
  for (i = 0; i < nframes; i++) {
    CallInfo *ci = L2->base_ci + i;
    uint32_t base = get32(&up->io), func = get32(&up->io), ctop = get32(&up->io);
    uint32_t pc;
    ci->nresults = (int)get32(&up->io);
    pc = get32(&up->io);
    ci->tailcalls = (int)get32(&up->io);
    checkindex(up, base, stacksize + EXTRA_STACK, "frame base");
    checkindex(up, func, stacksize + EXTRA_STACK, "frame func");
    checkindex(up, ctop, stacksize + EXTRA_STACK, "frame top");
    ci->base = L2->stack + base;
    ci->func = L2->stack + func;
    ci->top = L2->stack + ctop;
    ci->savedpc = NULL;
    if (pc != NOPC) {
      if (!isLua(ci) || pc > (uint32_t)ci_func(ci)->l.p->sizecode) luaL_error(L, "persist: corrupt data (saved pc)");
      ci->savedpc = ci_func(ci)->l.p->code + pc;
    }
  }
  L2->ci = L2->base_ci + (nframes - 1);
  L2->savedpc = L2->ci->savedpc;
  L2->status = (lu_byte)get8(&up->io);
  {
    uint32_t base = get32(&up->io);
    checkindex(up, base, stacksize + EXTRA_STACK, "thread base");
    L2->base = L2->stack + base;
  }
  L2->errfunc = (ptrdiff_t)(int32_t)get32(&up->io);
  unpersist(up);
  if (!lua_istable(L, -1)) luaL_error(L, "persist: corrupt data (thread globals)");
  sethvalue(L2, gt(L2), hvalue(topv(L)));
  lua_pop(L, 1);
  for (;;) {
    UpVal *boxed, *open;
    uint32_t pos;
    unpersist(up);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
    if (!lua_isfunction(L, -1) || clvalue(topv(L))->c.isC || clvalue(topv(L))->l.nupvalues != 1)
      luaL_error(L, "persist: corrupt data (open upvalue)");
    boxed = clvalue(topv(L))->l.upvals[0];
    lua_pop(L, 1);
    pos = get32(&up->io);
    checkindex(up, pos, stacksize + EXTRA_STACK, "open upvalue slot");
    open = luaF_findupval(L2, L2->stack + pos);
    up->pairs = (UpVal **)grow(up->pairs, &up->cappairs, up->npairs * 2 + 1, sizeof(UpVal *));
    if (!up->pairs) luaL_error(L, "persist: not enough memory");
    up->pairs[up->npairs * 2] = boxed;
    up->pairs[up->npairs * 2 + 1] = open;
    up->npairs++;
  }
}

static void unpersistuserdata (UP *up, int ref) {
  lua_State *L = up->L;
  unsigned typed = get8(&up->io);
  need(L, 3);
  if (typed) {
    if (!up->opts || !up->opts->udata_load) luaL_error(L, "persist: no handler for typed userdata");
    if (up->opts->udata_load(L, &up->io, up->opts->udata_ud) != 0 || !lua_isuserdata(L, -1))
      luaL_error(L, "persist: userdata handler failed");
  }
  else {
    uint32_t len = get32(&up->io);
    void *d;
    checkindex(up, len, 1u << 28, "userdata size");
    d = lua_newuserdata(L, len);
    scfa_persist_read(&up->io, d, len);
  }
  registerobject(up, ref);
  unpersist(up);
  setmeta_from_top(up);
}

static void unpersistperm (UP *up, int ref) {
  lua_State *L = up->L;
  need(L, 3);
  unpersist(up);			/* key */
  lua_pushvalue(L, -1);
  lua_gettable(L, PERMS);
  if (lua_isnil(L, -1)) {
    if (lua_type(L, -2) == LUA_TSTRING) luaL_error(L, "persist: permanent object '%s' not found", lua_tostring(L, -2));
    luaL_error(L, "persist: permanent object not found");
  }
  lua_remove(L, -2);			/* [obj] */
  registerobject(up, ref);
  if (lua_istable(L, -1) && (up->flags & SCFA_PERSIST_PERMTABLE_CONTENT)) unpersisttable_body(up, 0, ref);
}

static void unpersist (UP *up) {
  lua_State *L = up->L;
  unsigned kind;
  if (++up->depth > up->maxdepth) luaL_error(L, "persist: structure nested too deeply");
  need(L, 10);
  kind = get8(&up->io);
  switch (kind) {
    case K_NIL: lua_pushnil(L); break;
    case K_BOOL: lua_pushboolean(L, get8(&up->io)); break;
    case K_NUM: {
      lua_Number n;
      scfa_persist_read(&up->io, &n, sizeof(n));
      lua_pushnumber(L, n);
      break;
    }
    case K_LUD: {
      uint32_t lo = get32(&up->io), hi = get32(&up->io);
      lua_pushlightuserdata(L, (void *)(uintptr_t)(((uint64_t)hi << 32) | lo));
      break;
    }
    case K_REF: {
      uint32_t ref = get32(&up->io);
      lua_pushlightuserdata(L, (void *)(intptr_t)ref);
      lua_rawget(L, up->reft);
      if (lua_isnil(L, -1)) luaL_error(L, "persist: corrupt data (dangling reference %d)", (int)ref);
      break;
    }
    case K_PERM: unpersistperm(up, (int)get32(&up->io)); break;
    case K_NEW: {
      int ref = (int)get32(&up->io);
      int type = (int)get8(&up->io);
      switch (type) {
        case LUA_TSTRING: unpersiststring(up); registerobject(up, ref); break;
        case LUA_TTABLE: unpersisttable_body(up, 1, ref); break;
        case LUA_TFUNCTION: unpersistfunction(up, ref); break;
        case LUA_TUSERDATA: unpersistuserdata(up, ref); break;
        case LUA_TTHREAD: unpersistthread(up, ref); break;
        case LUA_TPROTO: unpersistproto(up); registerobject(up, ref); break;
        case LUA_TUPVAL: unpersistupval(up, ref); break;
        default: luaL_error(L, "persist: corrupt data (type %d)", type);
      }
      break;
    }
    default: luaL_error(L, "persist: corrupt data (kind %d)", (int)kind);
  }
  up->depth--;
}

static int cmp_ptr (const void *a, const void *b) {
  UpVal *x = *(UpVal *const *)a, *y = *(UpVal *const *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

static void fixup_open_upvalues (UP *up) {
  size_t i;
  int j;
  if (up->npairs == 0) return;
  /* pairs are (boxed, open); sort by boxed pointer */
  qsort(up->pairs, up->npairs, 2 * sizeof(UpVal *), cmp_ptr);
  for (i = 0; i < up->nclosures; i++) {
    LClosure *c = up->closures[i];
    for (j = 0; j < c->nupvalues; j++) {
      UpVal **hit = (UpVal **)bsearch(&c->upvals[j], up->pairs, up->npairs, 2 * sizeof(UpVal *), cmp_ptr);
      if (hit) c->upvals[j] = hit[1];
    }
  }
}

static int do_unpersist (lua_State *L) {
  /* stack: [perms, up] */
  UP *up = (UP *)lua_touserdata(L, 2);
  uint32_t magic, len;
  char tag[200];
  const char *mine = scfa_persist_build_tag();
  lua_pop(L, 1);			/* [perms] */
  up->L = L;
  up->io.L = L;
  magic = get32(&up->io);
  if (magic != MAGIC) luaL_error(L, "persist: not a persisted universe");
  len = get32(&up->io);
  if (len >= sizeof(tag)) luaL_error(L, "persist: corrupt header");
  scfa_persist_read(&up->io, tag, len);
  tag[len] = 0;
  if (strcmp(tag, mine) != 0) luaL_error(L, "persist: stream was written by another build (%s, this is %s)", tag, mine);
  up->flags = (int)get32(&up->io);
  lua_newtable(L);			/* [perms reft] */
  up->reft = lua_gettop(L);
  up->dummyp = luaF_newproto(L);
  pushproto(L, up->dummyp);
  up->dummyuv = luaF_newupval(L);
  pushupval(L, up->dummyuv);
  up->emptystr = luaS_newlstr(L, "", 0);
  pushtstring(L, up->emptystr);		/* anchors: [perms reft dummyp dummyuv empty] */
  unpersist(up);			/* [perms reft dummyp dummyuv empty root] */
  fixup_open_upvalues(up);
  lua_replace(L, 2);			/* [perms root dummyp dummyuv empty] */
  lua_settop(L, 2);
  return 1;
}

static int unpersist_common (lua_State *L, UP *upp) {
  UP up = *upp;
  int status;
  int stopped_before;
  int perms = lua_gettop(L);
  up.io.reading = 1;
  stopped_before = (G(L)->GCthreshold == MAX_LUMEM);
  lua_gc(L, LUA_GCSTOP, 0);
  lua_pushcfunction(L, do_unpersist);
  lua_pushvalue(L, perms);
  lua_pushlightuserdata(L, &up);
  status = lua_pcall(L, 2, 1, 0);	/* -> [perms, root] or [perms, msg] */
  if (!stopped_before) lua_gc(L, LUA_GCRESTART, 0);
  free(up.scratch);
  free(up.closures);
  free(up.pairs);
  return status;
}

static void up_init (UP *up, const scfa_persist_opts *opts) {
  memset(up, 0, sizeof(*up));
  up->opts = opts;
  up->maxdepth = (opts && opts->max_depth > 0) ? opts->max_depth : DEFAULT_DEPTH;
}

LUA_API int scfa_unpersist (lua_State *L, lua_Reader reader, void *rud, const scfa_persist_opts *opts) {
  UP up;
  up_init(&up, opts);
  luaZ_init(L, &up.io.z, reader, rud);
  return unpersist_common(L, &up);
}

LUA_API int scfa_unpersist_exact (lua_State *L, scfa_persist_readfn read, void *rud, const scfa_persist_opts *opts) {
  UP up;
  up_init(&up, opts);
  up.io.rf = read;
  up.io.rud = rud;
  return unpersist_common(L, &up);
}

LUA_API void scfa_persist_reverse_perms (lua_State *L, int permsidx) {
  permsidx = (permsidx > 0 || permsidx <= LUA_REGISTRYINDEX) ? permsidx : lua_gettop(L) + permsidx + 1;
  luaL_checkstack(L, 4, "reverse perms");
  lua_newtable(L);
  lua_pushnil(L);
  while (lua_next(L, permsidx)) {	/* k v */
    lua_pushvalue(L, -1);		/* k v v */
    lua_pushvalue(L, -3);		/* k v v k */
    lua_rawset(L, -5);			/* k v ; rev[v] = k */
    lua_pop(L, 1);
  }
}
