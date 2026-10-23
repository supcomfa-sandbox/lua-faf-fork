/*
** SCFA dialect port "gpgapi", adapter for the Lua 5.1 core: the C API GPG added to Lua 5.0.1
** (contract and exe evidence: lua5_0-api/gpgapi/scfa_gpgapi.h).
** Included by lobject.h, lstate.h, lstate.c, lstring.c, ltable.c, ldo.c, lgc.c, lapi.c, each with its
** SCFA_IN_<FILE> macro defined; only the section of the including file compiles. The core keeps
** only the hook macros below; the behaviour is in the lapi/lgc sections at the bottom.
**
** Differences from the exe (inherent to stock 5.1, listed in README.md "Not ported / known gaps"):
**  - the global/state user pointers live in new fields, not at G+0x148 / L+0x44 (the engine only
**    reaches them through the accessor functions, census D04/D05);
**  - the default metatables for tables/userdata are empty (NULL) until set; the exe creates an empty
**    table per type at lua_open, so every table there has a metatable;
**  - for the other basic types the default metatable is the 5.1 per-type metatable (the exe uses the
**    table itself as the handler of every event, luaT_gettmbyobj 0x928450);
**  - the user GC hook receives the lua_State that was traversed, not the exe's GCState.
*/

#include "scfa_gpgapi.h"


/* ------------------------------------------------------------ lobject.h */
#if defined(SCFA_IN_LOBJECT_H)

/* Udata: C++ type slot (exe Udata+0xc) */
#define SCFA_UDATA_FIELDS	const void *rtype;

#endif


/* ------------------------------------------------------------- lstate.h */
#if defined(SCFA_IN_LSTATE_H)

/* global_State: user pointer (exe G+0x148), GC hook (G+0x14c), default metatables of tables / userdata */
#define SCFA_GLOBAL_FIELDS \
  void *scfa_globalud; \
  lua_UserGCFunction scfa_usergc; \
  struct Table *scfa_defmt_table; \
  struct Table *scfa_defmt_udata;
#define SCFA_GLOBAL_INIT(g) \
  ((g)->scfa_globalud = NULL, (g)->scfa_usergc = NULL, \
   (g)->scfa_defmt_table = NULL, (g)->scfa_defmt_udata = NULL)

/* lua_State: per-state user pointer (exe L+0x44) */
#define SCFA_STATE_FIELDS	void *scfa_stateud;
#define SCFA_STATE_INIT(L)	((L)->scfa_stateud = NULL)	/* also for new threads: not inherited */

#endif


/* ---------------------------------------- lstring.c / ltable.c / lgc.c / ldo.c hooks */
#if defined(SCFA_IN_LSTRING_C)
#define SCFA_UDATA_INIT(L, u) \
  ((u)->uv.rtype = NULL, (u)->uv.metatable = G(L)->scfa_defmt_udata)
#endif

#if defined(SCFA_IN_LTABLE_C)
#define SCFA_NEWTABLE_META(L)	(G(L)->scfa_defmt_table)
#endif

#if defined(SCFA_IN_LGC_C)
/* roots: the default metatables (called from markmt, i.e. at the start and again in the atomic step) */
#define SCFA_MARK_ROOTS(g) \
  { if ((g)->scfa_defmt_table) markobject(g, (g)->scfa_defmt_table); \
    if ((g)->scfa_defmt_udata) markobject(g, (g)->scfa_defmt_udata); }
/* a thread was traversed (exe 0x915750, case tt 9) */
#define SCFA_THREAD_TRAVERSED(g, th)	{ if ((g)->scfa_usergc) (g)->scfa_usergc(th); }
#endif

#if defined(SCFA_IN_LDO_C)
#if defined(__cplusplus) && defined(SCFA_CPP_ERRORS)
/* unprotected error: the exe throws a C++ exception; the stack is reset, the error object is on top */
#define SCFA_UNPROTECTED_ERROR(L, errcode, reset) \
  { reset(L, errcode); scfa_LuaError e_; e_.L = L; e_.status = errcode; throw e_; }
#else
#define SCFA_UNPROTECTED_ERROR(L, errcode, reset)	((void)0)
#endif
#endif


/* ------------------------------------------------------- lgc.c: lua_markvalue */
#if defined(SCFA_IN_LGC_C_API)

void luaC_scfa_mark (global_State *g, const TValue *o) {
  if (iscollectable(o) && iswhite(gcvalue(o)))
    reallymarkobject(g, gcvalue(o));
}

#endif


/* --------------------------------------------------------------- lapi.c */
#if defined(SCFA_IN_LAPI_C)

/* lgc.c (SCFA_IN_LGC_C_API) */
extern void luaC_scfa_mark (global_State *g, const TValue *o);

LUA_API void lua_markvalue (lua_State *L, int idx) {
  luaC_scfa_mark(G(L), index2adr(L, idx));
}

LUA_API void lua_setglobaluserdata (lua_State *L, void *p) { G(L)->scfa_globalud = p; }
LUA_API void *lua_getglobaluserdata (lua_State *L) { return G(L)->scfa_globalud; }
LUA_API void lua_setstateuserdata (lua_State *L, void *p) { L->scfa_stateud = p; }
LUA_API void *lua_getstateuserdata (lua_State *L) { return L->scfa_stateud; }
LUA_API void lua_setusergcfunction (lua_State *L, lua_UserGCFunction f) { G(L)->scfa_usergc = f; }


/* engine type number -> 5.1 type tag (-1: no such type) */
static int scfa_gpg2tag (int t) {
  switch (t) {
    case SCFA_GPG_TNIL: return LUA_TNIL;
    case SCFA_GPG_TBOOLEAN: return LUA_TBOOLEAN;
    case SCFA_GPG_TLIGHTUSERDATA: return LUA_TLIGHTUSERDATA;
    case SCFA_GPG_TNUMBER: return LUA_TNUMBER;
    case SCFA_GPG_TSTRING: return LUA_TSTRING;
    case SCFA_GPG_TTABLE: return LUA_TTABLE;
    case SCFA_GPG_TCFUNCTION:
    case SCFA_GPG_TFUNCTION: return LUA_TFUNCTION;
    case SCFA_GPG_TUSERDATA: return LUA_TUSERDATA;
    case SCFA_GPG_TTHREAD: return LUA_TTHREAD;
    default: return -1;
  }
}


LUA_API void lua_setdefaultmetatable (lua_State *L, int gpgtype) {
  lua_lock(L);
  api_checknelems(L, 1);
  if (ttistable(L->top - 1)) {
    Table *mt = hvalue(L->top - 1);
    global_State *g = G(L);
    if (gpgtype == SCFA_GPG_TTABLE) g->scfa_defmt_table = mt;
    else if (gpgtype == SCFA_GPG_TUSERDATA) g->scfa_defmt_udata = mt;
    else {
      int tag = scfa_gpg2tag(gpgtype);
      if (tag >= 0) g->mt[tag] = mt;
    }
  }
  L->top--;
  lua_unlock(L);
}


LUA_API int lua_gpgtype (lua_State *L, int idx) {
  StkId o = index2adr(L, idx);
  if (o == luaO_nilobject) return -1;
  switch (ttype(o)) {
    case LUA_TFUNCTION: return clvalue(o)->c.isC ? SCFA_GPG_TCFUNCTION : SCFA_GPG_TFUNCTION;
    case LUA_TUSERDATA: return SCFA_GPG_TUSERDATA;
    case LUA_TTHREAD: return SCFA_GPG_TTHREAD;
    default: return ttype(o);  /* 0..5 coincide */
  }
}


LUA_API void *lua_newuserdatat (lua_State *L, size_t size, const void *rtype) {
  void *p = lua_newuserdata(L, size);
  (cast(Udata *, p) - 1)->uv.rtype = rtype;
  return p;
}


LUA_API const void *lua_getuserdatatype (lua_State *L, int idx) {
  StkId o = index2adr(L, idx);
  return (o != luaO_nilobject && ttisuserdata(o)) ? uvalue(o)->rtype : NULL;
}


LUA_API void lua_touserdata_rref (scfa_RRef *ret, lua_State *L, int idx) {
  StkId o = index2adr(L, idx);
  if (o != luaO_nilobject && ttisuserdata(o)) {
    ret->d = rawuvalue(o) + 1;
    ret->t = rawuvalue(o)->uv.rtype;
  }
  else {
    ret->d = NULL;
    ret->t = NULL;
  }
}

#endif
