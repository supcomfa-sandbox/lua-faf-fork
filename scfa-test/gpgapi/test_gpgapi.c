/*
** Tests of the GPG C API additions on the 5.1 core (port "gpgapi"). Plain C source, built by
** scfa-test/gpgapi/run.sh against the C core, and (as C++) against the C++ core.
*/
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static lua_State *newL (void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  return L;
}

/* ---- global / state user data ---- */
static void test_userdata_slots (void) {
  int a = 1, b = 2, c = 3;
  lua_State *L = newL(), *T;
  CHECK(lua_getglobaluserdata(L) == NULL);
  CHECK(lua_getstateuserdata(L) == NULL);
  lua_setglobaluserdata(L, &a);
  lua_setstateuserdata(L, &b);
  CHECK(lua_getglobaluserdata(L) == &a);
  CHECK(lua_getstateuserdata(L) == &b);
  T = lua_newthread(L);
  /* the global slot is shared by every thread of the state */
  CHECK(lua_getglobaluserdata(T) == &a);
  /* the per-state slot is NOT inherited (exe 0x924300: the new thread's L+0x44 = 0) */
  CHECK(lua_getstateuserdata(T) == NULL);
  lua_setstateuserdata(T, &c);
  CHECK(lua_getstateuserdata(T) == &c);
  CHECK(lua_getstateuserdata(L) == &b);
  /* a thread made by a script starts with NULL too */
  CHECK(luaL_dostring(L, "co = coroutine.create(function() end)") == 0);
  lua_getglobal(L, "co");
  CHECK(lua_getstateuserdata(lua_tothread(L, -1)) == NULL);
  lua_close(L);
}

/* ---- user GC hook ---- */
static int hook_calls, hook_main, hook_other;
static lua_State *mainL, *hook_seen_thread;
static void hook (lua_State *L) {
  hook_calls++;
  if (L == mainL) hook_main++; else { hook_other++; hook_seen_thread = L; }
}

static void test_gc_hook (void) {
  lua_State *L = newL(), *T;
  mainL = L; hook_calls = hook_main = hook_other = 0; hook_seen_thread = NULL;
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(hook_calls == 0);              /* no hook installed */
  lua_setusergcfunction(L, hook);
  T = lua_newthread(L);                /* anchored on L's stack */
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(hook_main >= 1);               /* the main thread was traversed */
  CHECK(hook_other >= 1);              /* the live coroutine was traversed */
  CHECK(hook_seen_thread == T);
  lua_setusergcfunction(L, NULL);
  hook_calls = 0;
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(hook_calls == 0);              /* removed */
  lua_close(L);
}

/* the hook keeps alive a value that nothing else references (weak table entry) */
static int weakref;
static void markhook (lua_State *L) {
  if (L != mainL) return;
  lua_rawgeti(L, LUA_REGISTRYINDEX, weakref);  /* the weak table */
  lua_rawgeti(L, -1, 1);                       /* its only (weakly held) value */
  lua_markvalue(L, -1);
  lua_pop(L, 2);
}

static int weak_entry_is_nil (lua_State *L) {
  int r;
  lua_rawgeti(L, LUA_REGISTRYINDEX, weakref);
  lua_rawgeti(L, -1, 1);
  r = lua_isnil(L, -1);
  lua_pop(L, 2);
  return r;
}

static void fill_weak_entry (lua_State *L) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, weakref);
  lua_newtable(L);
  lua_rawseti(L, -2, 1);
  lua_pop(L, 1);
}

static void test_gc_hook_mark (void) {
  lua_State *L = newL();
  mainL = L;
  (void)luaL_dostring(L, "return setmetatable({}, {__mode = 'v'})");
  weakref = luaL_ref(L, LUA_REGISTRYINDEX);
  fill_weak_entry(L);
  lua_gc(L, LUA_GCCOLLECT, 0);                 /* no hook: collected */
  CHECK(weak_entry_is_nil(L));
  fill_weak_entry(L);
  lua_setusergcfunction(L, markhook);
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(!weak_entry_is_nil(L));                /* kept alive by the hook */
  lua_setusergcfunction(L, NULL);
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(weak_entry_is_nil(L));                 /* hook gone: collected */
  lua_close(L);
}

/* ---- default metatables ---- */
static void test_defaultmetatable (void) {
  lua_State *L = newL();
  (void)luaL_dostring(L, "old = {}");
  (void)luaL_dostring(L, "return {__index = function(t, k) return 'dflt:' .. k end}");
  lua_setdefaultmetatable(L, SCFA_GPG_TTABLE);
  CHECK(lua_gettop(L) == 0);                   /* popped */
  CHECK(luaL_dostring(L, "return ({}).foo") == 0);
  CHECK(strcmp(lua_tostring(L, -1), "dflt:foo") == 0);
  lua_pop(L, 1);
  CHECK(luaL_dostring(L, "return old.foo") == 0);  /* existing tables are not touched */
  CHECK(lua_isnil(L, -1));
  lua_pop(L, 1);
  /* a table that sets its own metatable replaces it */
  CHECK(luaL_dostring(L, "return setmetatable({}, {__index = {foo = 1}}).foo") == 0);
  CHECK(lua_tonumber(L, -1) == 1);
  lua_pop(L, 1);
  /* not a table: only popped, nothing changes */
  lua_pushnumber(L, 5);
  lua_setdefaultmetatable(L, SCFA_GPG_TTABLE);
  CHECK(lua_gettop(L) == 0);
  CHECK(luaL_dostring(L, "return ({}).foo") == 0);
  CHECK(strcmp(lua_tostring(L, -1), "dflt:foo") == 0);
  lua_pop(L, 1);
  /* userdata: new full userdata start with it */
  (void)luaL_dostring(L, "return {__index = function(u, k) return 'ud:' .. k end}");
  lua_setdefaultmetatable(L, SCFA_GPG_TUSERDATA);
  lua_newuserdata(L, 4);
  lua_setglobal(L, "u");
  CHECK(luaL_dostring(L, "return u.bar") == 0);
  CHECK(strcmp(lua_tostring(L, -1), "ud:bar") == 0);
  lua_pop(L, 1);
  /* other basic types: the per-type metatable (numbers get methods) */
  (void)luaL_dostring(L, "return {__index = {double = function(n) return n * 2 end}}");
  lua_setdefaultmetatable(L, SCFA_GPG_TNUMBER);
  CHECK(luaL_dostring(L, "return (21):double()") == 0);
  CHECK(lua_tonumber(L, -1) == 42);
  lua_pop(L, 1);
  /* function types share the 5.1 function slot */
  (void)luaL_dostring(L, "return {__index = {tag = 'fn'}}");
  lua_setdefaultmetatable(L, SCFA_GPG_TFUNCTION);
  CHECK(luaL_dostring(L, "return print.tag") == 0);
  CHECK(strcmp(lua_tostring(L, -1), "fn") == 0);
  lua_pop(L, 1);
  /* the default metatables survive full collections */
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(luaL_dostring(L, "return ({}).x") == 0);
  CHECK(strcmp(lua_tostring(L, -1), "dflt:x") == 0);
  lua_close(L);
}

/* ---- userdata type slot ---- */
static int rtype_a, rtype_b;
static void test_userdata_type (void) {
  lua_State *L = newL();
  scfa_RRef r;
  void *p, *q;
  p = lua_newuserdatat(L, 16, &rtype_a);
  CHECK(p != NULL);
  CHECK(lua_getuserdatatype(L, -1) == &rtype_a);
  lua_touserdata_rref(&r, L, -1);
  CHECK(r.d == p && r.t == &rtype_a);
  CHECK(lua_touserdata(L, -1) == p);           /* the stock signature still works */
  q = lua_newuserdata(L, 8);                   /* untyped */
  CHECK(lua_getuserdatatype(L, -1) == NULL);
  lua_touserdata_rref(&r, L, -1);
  CHECK(r.d == q && r.t == NULL);
  lua_pop(L, 1);
  lua_newuserdatat(L, 0, &rtype_b);
  CHECK(lua_getuserdatatype(L, -1) == &rtype_b);
  lua_pop(L, 1);
  /* not a full userdata: {NULL, NULL} */
  lua_pushlightuserdata(L, &rtype_a);
  lua_touserdata_rref(&r, L, -1);
  CHECK(r.d == NULL && r.t == NULL);
  CHECK(lua_getuserdatatype(L, -1) == NULL);
  lua_pop(L, 1);
  lua_pushnumber(L, 1);
  lua_touserdata_rref(&r, L, -1);
  CHECK(r.d == NULL && r.t == NULL);
  lua_pop(L, 1);
  lua_touserdata_rref(&r, L, 50);              /* bad index */
  CHECK(r.d == NULL && r.t == NULL);
  /* the payload is intact and survives a collection (anchored on the stack) */
  memset(p, 0xAB, 16);
  lua_gc(L, LUA_GCCOLLECT, 0);
  CHECK(((unsigned char *)p)[15] == 0xAB);
  CHECK(lua_getuserdatatype(L, 1) == &rtype_a);
  lua_close(L);
}

static void test_gpgtype (void) {
  lua_State *L = newL();
  lua_pushnil(L); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TNIL); lua_pop(L, 1);
  lua_pushboolean(L, 1); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TBOOLEAN); lua_pop(L, 1);
  lua_pushlightuserdata(L, L); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TLIGHTUSERDATA); lua_pop(L, 1);
  lua_pushnumber(L, 1); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TNUMBER); lua_pop(L, 1);
  lua_pushstring(L, "s"); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TSTRING); lua_pop(L, 1);
  lua_newtable(L); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TTABLE); lua_pop(L, 1);
  lua_getglobal(L, "print"); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TCFUNCTION); lua_pop(L, 1);
  (void)luaL_dostring(L, "return function() end"); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TFUNCTION); lua_pop(L, 1);
  lua_newuserdata(L, 1); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TUSERDATA); lua_pop(L, 1);
  lua_newthread(L); CHECK(lua_gpgtype(L, -1) == SCFA_GPG_TTHREAD); lua_pop(L, 1);
  CHECK(lua_gpgtype(L, 20) == -1);
  lua_close(L);
}

int main (void) {
  test_userdata_slots();
  test_gc_hook();
  test_gc_hook_mark();
  test_defaultmetatable();
  test_userdata_type();
  test_gpgtype();
  printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures != 0;
}
