// Lua errors as C++ exceptions: the 5.1 core compiled as C++ with -DSCFA_CPP_ERRORS.
// The engine's VM throws C++ exceptions instead of longjmp, so destructors of C++ frames between
// the error and the handler run. Built by scfa-test/gpgapi/run.sh (step 3).
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int dtor_count = 0;
struct Sentinel { ~Sentinel() { dtor_count++; } };

static lua_State *newL () { lua_State *L = luaL_newstate(); luaL_openlibs(L); return L; }

// a bound C++ function with an RAII object in its frame that raises a Lua error
static int raises (lua_State *L) {
  Sentinel s;
  return luaL_error(L, "boom %d", 42);
}

// a C++ function (engine-shaped) that calls Lua without protection and lets the error escape
static int call_unprotected (lua_State *L) {
  Sentinel s;
  lua_pushvalue(L, 1);
  lua_call(L, 0, 0);
  return 0;
}

// 1. unprotected lua_call of a failing script: caught as a C++ exception in the engine-side frame
static void test_unprotected_call_throws () {
  lua_State *L = newL();
  int caught = 0, status = 0;
  const char *msg = NULL;
  try {
    luaL_loadstring(L, "error('from lua')");
    lua_call(L, 0, 0);
    CHECK(!"lua_call returned after an error");
  }
  catch (const scfa_LuaError &e) {
    caught = 1; status = e.status; msg = lua_tostring(e.L, -1);
    CHECK(e.L == L);
  }
  CHECK(caught);
  CHECK(status == LUA_ERRRUN);
  CHECK(msg && std::strstr(msg, "from lua") != NULL);
  // the state is usable afterwards: the stack was reset to the error object
  CHECK(lua_gettop(L) == 1);
  lua_settop(L, 0);
  CHECK(luaL_dostring(L, "return 1 + 1") == 0);
  CHECK(lua_tonumber(L, -1) == 2);
  lua_close(L);
}

// 2. non-string error objects arrive intact
static void test_error_object () {
  lua_State *L = newL();
  bool caught = false;
  try {
    luaL_loadstring(L, "error({code = 7})");
    lua_call(L, 0, 0);
  }
  catch (const scfa_LuaError &e) {
    caught = true;
    CHECK(lua_istable(e.L, -1));
    lua_getfield(e.L, -1, "code");
    CHECK(lua_tonumber(e.L, -1) == 7);
  }
  CHECK(caught);
  lua_close(L);
}

// 3. an error crosses a C++ frame with a destructor (protected: lua_pcall returns a status)
static void test_destructors_run_in_pcall () {
  lua_State *L = newL();
  dtor_count = 0;
  lua_pushcfunction(L, raises);
  int status = lua_pcall(L, 0, 0, 0);
  CHECK(status == LUA_ERRRUN);
  CHECK(dtor_count == 1);                      // longjmp would have skipped it
  CHECK(std::strstr(lua_tostring(L, -1), "boom 42") != NULL);
  // from a Lua script through pcall()
  lua_pushcfunction(L, raises);
  lua_setglobal(L, "raises");
  dtor_count = 0;
  CHECK(luaL_dostring(L, "local ok, e = pcall(raises); return ok, e") == 0);
  CHECK(dtor_count == 1);
  CHECK(lua_toboolean(L, -2) == 0);
  lua_close(L);
}

// 4. unprotected: the exception propagates through the Lua frames and a C++ frame with a destructor
static void test_destructors_run_unprotected () {
  lua_State *L = newL();
  dtor_count = 0;
  lua_pushcfunction(L, raises);
  lua_setglobal(L, "raises");
  bool caught = false;
  try {
    luaL_loadstring(L, "local function f() raises() end f()");
    lua_call(L, 0, 0);
  }
  catch (const scfa_LuaError &e) {
    caught = true;
    CHECK(e.status == LUA_ERRRUN);
  }
  CHECK(caught);
  CHECK(dtor_count == 1);
  // engine-shaped: C++ function -> lua_call -> Lua error -> escapes the C++ function
  dtor_count = 0;
  caught = false;
  lua_settop(L, 0);
  lua_pushcfunction(L, call_unprotected);
  luaL_loadstring(L, "error('x')");
  try { lua_call(L, 1, 0); }
  catch (const scfa_LuaError &) { caught = true; }
  CHECK(caught);
  CHECK(dtor_count == 1);
  lua_close(L);
}

// 5. a pcall inside protects the outer unprotected call: no exception escapes
static void test_pcall_still_returns_status () {
  lua_State *L = newL();
  bool escaped = false;
  try {
    CHECK(luaL_dostring(L, "return pcall(function() error('e') end)") == 0);
    CHECK(lua_toboolean(L, -2) == 0);
    luaL_loadstring(L, "error('outer')");
    CHECK(lua_pcall(L, 0, 0, 0) == LUA_ERRRUN);
  }
  catch (...) { escaped = true; }
  CHECK(!escaped);
  lua_close(L);
}

// 6. errors in coroutines stay inside resume
static void test_coroutine_error () {
  lua_State *L = newL();
  bool escaped = false;
  try {
    lua_State *T = lua_newthread(L);
    luaL_loadstring(T, "error('in coroutine')");
    CHECK(lua_resume(T, 0) == LUA_ERRRUN);
    CHECK(std::strstr(lua_tostring(T, -1), "in coroutine") != NULL);
  }
  catch (...) { escaped = true; }
  CHECK(!escaped);
  lua_close(L);
}

// 7. syntax errors from luaL_loadstring are statuses, not exceptions
static void test_syntax_error_is_status () {
  lua_State *L = newL();
  bool escaped = false;
  try { CHECK(luaL_loadstring(L, "x = = 1") == LUA_ERRSYNTAX); }
  catch (...) { escaped = true; }
  CHECK(!escaped);
  lua_close(L);
}

int main () {
  test_unprotected_call_throws();
  test_error_object();
  test_destructors_run_in_pcall();
  test_destructors_run_unprotected();
  test_pcall_still_returns_status();
  test_coroutine_error();
  test_syntax_error_is_status();
  std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures != 0;
}
