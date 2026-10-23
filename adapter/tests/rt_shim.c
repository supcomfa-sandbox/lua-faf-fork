/* rt_shim.c - test host glue: turns a harness runtime DLL (rt_abi.h, lua<variant>_rt.dll) into the
 * plain Lua 5.0 function table (Lua50Funcs) that the passthrough provider wraps.
 * Plain 5.0 signatures carry no context, so each concurrently loaded runtime gets its own slot of
 * shim functions (macro-instantiated). Approximations (test glue only): rawget/rawset == get/set,
 * gettable/settable support string and number keys, lua_call is pcall that ignores errors. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_abi.h"
#include "lua_adapter.h"
#include "lua50_passthrough.h"
#include "rt_shim.h"

typedef struct {
#define X(R, N, P) R (*N) P;
  RT_FUNCS(X)
#undef X
} Rt;

#define MAXSLOT 2
static Rt slot_rt[MAXSLOT];
static char slot_variant_name[MAXSLOT][64];
static int slots_used;

#define MAXSTATES 64
static struct { lua_State *L; int slot; } states[MAXSTATES];
static int nstates;
static void remember(lua_State *L, int slot) { if (L && nstates < MAXSTATES) { states[nstates].L = L; states[nstates].slot = slot; nstates++; } }
static int slot_of(lua_State *L) { for (int i = 0; i < nstates; i++) if (states[i].L == L) return states[i].slot; return -1; }

static int rt_type_to_50(int t) {
  switch (t) {
    case LT_NIL: return LUA_TNIL; case LT_BOOL: return LUA_TBOOLEAN; case LT_NUMBER: return LUA_TNUMBER;
    case LT_STRING: return LUA_TSTRING; case LT_TABLE: return LUA_TTABLE; case LT_FUNCTION: return LUA_TFUNCTION;
    case LT_USERDATA: return LUA_TUSERDATA; case LT_THREAD: return LUA_TTHREAD; case LT_LIGHTUD: return LUA_TLIGHTUSERDATA;
    default: return LUA_TNONE;
  }
}
static int status_to_50(int s) { switch (s) { case RT_OK: return 0; case RT_ERRRUN: return 1; case RT_ERRFILE: return 2;
  case RT_ERRSYNTAX: return 3; case RT_ERRMEM: return 4; default: return 5; } }

#define SHIMS(N) \
static lua_State *s##N##_open(void) { lua_State *L = (lua_State *)slot_rt[N].rt_new_state(); remember(L, N); return L; } \
static void s##N##_close(lua_State *L) { slot_rt[N].rt_close(L); } \
static lua_State *s##N##_newthread(lua_State *L) { lua_State *T = (lua_State *)slot_rt[N].rt_new_thread(L); remember(T, N); return T; } \
static int s##N##_gettop(lua_State *L) { return slot_rt[N].rt_gettop(L); } \
static void s##N##_settop(lua_State *L, int i) { slot_rt[N].rt_settop(L, i); } \
static void s##N##_pushvalue(lua_State *L, int i) { slot_rt[N].rt_push_value(L, i); } \
static void s##N##_gettable(lua_State *L, int idx) { Rt *r = &slot_rt[N]; \
  if (r->rt_type(L, -1) == LT_STRING) { char k[256]; strncpy(k, r->rt_to_string(L, -1), 255); k[255] = 0; r->rt_settop(L, -2); \
    if (idx == LUA_GLOBALSINDEX) r->rt_getglobal(L, k); else r->rt_getfield(L, idx < 0 ? idx + 1 : idx, k); } \
  else { int n = (int)r->rt_to_number(L, -1); r->rt_settop(L, -2); r->rt_geti(L, idx < 0 ? idx + 1 : idx, n); } } \
static void s##N##_settable(lua_State *L, int idx) { Rt *r = &slot_rt[N]; \
  if (r->rt_type(L, -2) == LT_STRING) { char k[256]; strncpy(k, r->rt_to_string(L, -2), 255); k[255] = 0; \
    if (idx == LUA_GLOBALSINDEX) r->rt_setglobal(L, k); else r->rt_setfield(L, idx, k); } \
  else { int n = (int)r->rt_to_number(L, -2); r->rt_seti(L, idx, n); } \
  r->rt_settop(L, -2); } \
static void s##N##_pushnumber(lua_State *L, lua_Number n) { slot_rt[N].rt_push_number(L, n); } \
static void s##N##_pushstring(lua_State *L, const char *s) { slot_rt[N].rt_push_string(L, s); } \
static void s##N##_pushboolean(lua_State *L, int b) { slot_rt[N].rt_push_bool(L, b); } \
static void s##N##_pushnil(lua_State *L) { slot_rt[N].rt_push_nil(L); } \
static lua_Number s##N##_tonumber(lua_State *L, int i) { return slot_rt[N].rt_to_number(L, i); } \
static const char *s##N##_tostring(lua_State *L, int i) { return slot_rt[N].rt_to_string(L, i); } \
static int s##N##_toboolean(lua_State *L, int i) { return slot_rt[N].rt_to_bool(L, i); } \
static int s##N##_type(lua_State *L, int i) { return rt_type_to_50(slot_rt[N].rt_type(L, i)); } \
static int s##N##_next(lua_State *L, int i) { return slot_rt[N].rt_next(L, i); } \
static void s##N##_call(lua_State *L, int a, int r) { slot_rt[N].rt_call(L, a, r); } \
static int s##N##_pcall(lua_State *L, int a, int r, int e) { (void)e; return status_to_50(slot_rt[N].rt_call(L, a, r)); } \
static int s##N##_resume(lua_State *L, int a) { int s = slot_rt[N].rt_resume(L, a); return s == RT_YIELDED ? LUA_YIELD : status_to_50(s); } \
static void s##N##_setgc(lua_State *L, int t) { if (t == 0) slot_rt[N].rt_collect(L); } \
static void fill##N(Lua50Funcs *f) { \
  f->lua_open = s##N##_open; f->lua_close = s##N##_close; f->lua_newthread = s##N##_newthread; f->lua_gettop = s##N##_gettop; \
  f->lua_settop = s##N##_settop; f->lua_pushvalue = s##N##_pushvalue; f->lua_gettable = s##N##_gettable; f->lua_settable = s##N##_settable; \
  f->lua_rawget = s##N##_gettable; f->lua_rawset = s##N##_settable; f->lua_pushnumber = s##N##_pushnumber; f->lua_pushstring = s##N##_pushstring; \
  f->lua_pushboolean = s##N##_pushboolean; f->lua_pushnil = s##N##_pushnil; f->lua_tonumber = s##N##_tonumber; f->lua_tostring = s##N##_tostring; \
  f->lua_toboolean = s##N##_toboolean; f->lua_type = s##N##_type; f->lua_next = s##N##_next; f->lua_call = s##N##_call; \
  f->lua_pcall = s##N##_pcall; f->lua_resume = s##N##_resume; f->lua_setgcthreshold = s##N##_setgc; }

SHIMS(0)
SHIMS(1)

static int load_rt(Rt *rt, const char *variant, char *err, size_t n) {
  const char *dir = getenv("LUA_UPGRADE_BIN");
  char path[600];
  snprintf(path, sizeof path, "%s\\lua%s_rt.dll", dir ? dir : "C:\\Users\\admin\\Desktop\\supreme-commander-forged-alliance\\out\\lua-upgrade\\bin", variant);
  HMODULE h = LoadLibraryA(path);
  if (!h) { snprintf(err, n, "cannot load %s", path); return -1; }
#define X(R, N, P) if (!(rt->N = (R (*) P)GetProcAddress(h, #N))) { snprintf(err, n, "%s: missing %s", path, #N); return -1; }
  RT_FUNCS(X)
#undef X
  return 0;
}

LuaProvider *rt_shim_factory(const char *name, const char *runtime, void *ctx, char *err, size_t errlen) {
  const char *variant = runtime ? runtime : (const char *)ctx;
  if (slots_used >= MAXSLOT) { snprintf(err, errlen, "rt_shim: out of slots"); return NULL; }
  int s = slots_used;
  if (load_rt(&slot_rt[s], variant, err, errlen)) return NULL;
  snprintf(slot_variant_name[s], sizeof slot_variant_name[s], "%s", slot_rt[s].rt_variant());
  Lua50Funcs f; memset(&f, 0, sizeof f);
  if (s == 0) fill0(&f); else fill1(&f);
  slots_used++;
  return lua50_passthrough_create(name, &f);
}

int rt_shim_dostring(lua_State *L, const char *code) {
  int s = slot_of(L); if (s < 0) return -1;
  int rc = slot_rt[s].rt_dostring(L, code, "=test");
  if (rc) fprintf(stderr, "dostring: %s\n", slot_rt[s].rt_last_error(L));
  slot_rt[s].rt_settop(L, 0);
  return rc;
}
const char *rt_shim_variant(lua_State *L) { int s = slot_of(L); return s < 0 ? "?" : slot_variant_name[s]; }
