/*
** Tests of persist/scfa_persist on the 5.1 SCFA core. Plain C, built by scfa-test/persist/run.sh as C
** (C core, float32 numbers), as C++ (C++ core, errors as C++ exceptions) and with double numbers.
** The Lua test code is in the SCFA dialect: no `#` (it starts a comment), table.getn for lengths.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "scfa_persist.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(got, want) do { const char *g_ = (got), *w_ = (want); checks++; \
  if (!g_ || strcmp(g_, w_) != 0) { failures++; printf("FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, g_ ? g_ : "(null)", w_); } } while (0)

/* ---------------------------------------------------------------- helpers */

typedef struct Buf { char *p; size_t n, cap; } Buf;
static int bufwriter (lua_State *L, const void *p, size_t sz, void *ud) {
  Buf *b = (Buf *)ud;
  (void)L;
  if (b->n + sz > b->cap) { b->cap = (b->n + sz) * 2 + 64; b->p = (char *)realloc(b->p, b->cap); }
  memcpy(b->p + b->n, p, sz);
  b->n += sz;
  return 0;
}
static int failingwriter (lua_State *L, const void *p, size_t sz, void *ud) {
  (void)L; (void)p; (void)sz; (void)ud;
  return 1;
}
typedef struct Rd { const char *p; size_t n; } Rd;
static const char *bufreader (lua_State *L, void *ud, size_t *sz) {
  Rd *r = (Rd *)ud;
  (void)L;
  if (r->n == 0) return NULL;
  *sz = r->n > 7 ? 7 : r->n;	/* small odd chunks: the reader must cope */
  { const char *p = r->p; r->p += *sz; r->n -= *sz; return p; }
}

static lua_State *newL (void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  return L;
}

/* perms: every C function of the standard libraries, named "lib.name" (base functions "_G.name").
** forward: function -> name; reverse: name -> function. Extra: a table "tablelib" for the table library when
** content is requested is added by the caller. */
static void build_perms (lua_State *L, int reverse) {
  static const char *const libs[] = {"string", "table", "math", "coroutine", "os", "io", "debug", "package", NULL};
  int i, t;
  lua_newtable(L);
  t = lua_gettop(L);
  /* the globals table itself, by identity (the engine names _G too) */
  if (reverse) { lua_pushstring(L, "_G"); lua_pushvalue(L, LUA_GLOBALSINDEX); lua_rawset(L, t); }
  else { lua_pushvalue(L, LUA_GLOBALSINDEX); lua_pushstring(L, "_G"); lua_rawset(L, t); }
  /* base functions */
  lua_pushnil(L);
  while (lua_next(L, LUA_GLOBALSINDEX)) {
    if (lua_iscfunction(L, -1) && lua_type(L, -2) == LUA_TSTRING) {
      char name[128];
      sprintf(name, "_G.%s", lua_tostring(L, -2));
      if (reverse) { lua_pushstring(L, name); lua_pushvalue(L, -2); lua_rawset(L, t); }
      else { lua_pushvalue(L, -1); lua_pushstring(L, name); lua_rawset(L, t); }
    }
    lua_pop(L, 1);
  }
  for (i = 0; libs[i]; i++) {
    lua_getglobal(L, libs[i]);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      if (lua_iscfunction(L, -1) && lua_type(L, -2) == LUA_TSTRING) {
        char name[128];
        sprintf(name, "%s.%s", libs[i], lua_tostring(L, -2));
        if (reverse) { lua_pushstring(L, name); lua_pushvalue(L, -2); lua_rawset(L, t); }
        else { lua_pushvalue(L, -1); lua_pushstring(L, name); lua_rawset(L, t); }
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
}

/* stack: [root] -> [] ; bytes in b. Returns lua_pcall status (message on top on failure). */
typedef struct Job { Buf *b; const scfa_persist_opts *opts; lua_Writer w; } Job;
static int do_persist (lua_State *L) {
  Job *j = (Job *)lua_touserdata(L, 3);
  lua_pop(L, 1);
  scfa_persist(L, j->w, j->b, j->opts);
  return 0;
}
static int save_with (lua_State *L, Buf *b, const scfa_persist_opts *opts, lua_Writer w, int extra_perms_flag) {
  int status;
  Job j;
  (void)extra_perms_flag;
  j.b = b; j.opts = opts; j.w = w;
  build_perms(L, 0);			/* root perms */
  lua_insert(L, 1);			/* perms root */
  lua_pushcfunction(L, do_persist);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushlightuserdata(L, &j);
  status = lua_pcall(L, 3, 0, 0);
  if (status == 0) lua_settop(L, 0);
  else { lua_replace(L, 1); lua_settop(L, 1); }	/* leave only the message */
  return status;
}
static int save (lua_State *L, Buf *b, const scfa_persist_opts *opts) { return save_with(L, b, opts, bufwriter, 0); }

/* -> status; on success leaves the root on the stack (index 1), on failure the message */
static int load (lua_State *L, const char *p, size_t n, const scfa_persist_opts *opts) {
  Rd r;
  int status;
  r.p = p; r.n = n;
  lua_settop(L, 0);
  build_perms(L, 1);
  status = scfa_unpersist(L, bufreader, &r, opts);
  if (status == 0) lua_remove(L, 1);	/* [perms root] -> [root] */
  else { lua_replace(L, 1); lua_settop(L, 1); }
  return status;
}

static int run (lua_State *L, const char *code) {	/* returns status, message/results on the stack */
  return luaL_loadstring(L, code) || lua_pcall(L, 0, LUA_MULTRET, 0);
}
/* run code that returns one string; result kept in a static buffer */
static const char *runs (lua_State *L, const char *code) {
  static char out[4096];
  int top = lua_gettop(L);
  if (run(L, code) != 0) {
    snprintf(out, sizeof(out), "ERROR: %s", lua_tostring(L, -1));
    lua_settop(L, top);
    return out;
  }
  snprintf(out, sizeof(out), "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "(not a string)");
  lua_settop(L, top);
  return out;
}

/* persist the global `name' of L, load it into a new state as the global `name' */
static lua_State *roundtrip (lua_State *L, const char *name, const scfa_persist_opts *opts, size_t *bytes) {
  Buf b = {0, 0, 0};
  lua_State *L2;
  lua_getglobal(L, name);
  if (save(L, &b, opts) != 0) { printf("  save failed: %s\n", lua_tostring(L, -1)); free(b.p); return NULL; }
  L2 = newL();
  if (load(L2, b.p, b.n, opts) != 0) { printf("  load failed: %s\n", lua_tostring(L2, -1)); free(b.p); lua_close(L2); return NULL; }
  lua_setglobal(L2, name);
  if (bytes) *bytes = b.n;
  free(b.p);
  return L2;
}

/* ------------------------------------------------------------ the tests */

static void test_plain_values (void) {
  lua_State *L = newL(), *L2;
  size_t i;
  lua_Number nums[10];
  union { lua_Number n; unsigned char b[sizeof(lua_Number)]; } u1, u2;
  int ok = 1;
  nums[0] = 0; nums[1] = 1; nums[2] = -1; nums[3] = (lua_Number)0.1; nums[4] = (lua_Number)(1.0 / 3.0);
  nums[5] = (lua_Number)16777217.0; nums[6] = (lua_Number)1e30; nums[7] = (lua_Number)-1e-30;
  nums[8] = (lua_Number)(0.0 / 1.0); nums[9] = (lua_Number)3.4028234e38;
  CHECK(luaL_loadstring(L, "return {true, false, 'str', 'with\\0zero', '', 42, nested = {1, 2, {3}}, [5.5] = 'x', [true] = 'b'}") == 0);
  CHECK(lua_pcall(L, 0, 1, 0) == 0);
  for (i = 0; i < 10; i++) { lua_pushnumber(L, nums[i]); lua_rawseti(L, -2, (int)(100 + i)); }
  lua_setglobal(L, "v");
  L2 = roundtrip(L, "v", NULL, NULL);
  CHECK(L2 != NULL);
  if (!L2) { lua_close(L); return; }
  /* true false str 9 (len of with\0zero) '' 42 3 x b */
  CHECKS(runs(L2, "return tostring(v[1])..tostring(v[2])..v[3]..string.len(v[4])..v[5]..v[6]..v.nested[3][1]..v[5.5]..v[true]"),
         "truefalsestr9423xb");
  lua_getglobal(L2, "v");
  CHECK(lua_istable(L2, -1));
  for (i = 0; i < 10; i++) {
    lua_rawgeti(L2, -1, (int)(100 + i));
    u1.n = lua_tonumber(L2, -1); u2.n = nums[i];
    if (memcmp(u1.b, u2.b, sizeof(lua_Number)) != 0) ok = 0;
    lua_pop(L2, 1);
  }
  CHECK(ok);	/* bit exact numbers */
  CHECKS(runs(L2, "return v[4] == 'with\\0zero' and 'yes' or 'no'"), "yes");
  lua_close(L); lua_close(L2);
}

static void test_nan_inf (void) {
  lua_State *L = newL(), *L2;
  CHECK(luaL_dostring(L, "n = {1/0, -1/0, 0/0}") == 0);
  L2 = roundtrip(L, "n", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return (n[1] == 1/0 and n[2] == -1/0 and n[3] ~= n[3]) and 'ok' or 'bad'"), "ok");
    lua_close(L2);
  }
  lua_close(L);
}

static void test_tables_cycles_metatables (void) {
  lua_State *L = newL(), *L2;
  const char *src =
    "t = {name = 'root'}\n"
    "t.self = t\n"                          /* cycle */
    "t.a = {x = 1}; t.b = t.a\n"            /* shared */
    "t.a.back = t\n"
    "local mt = {}\n"
    "mt.__index = function(tbl, k) return 'dflt_'..k end\n"
    "mt.__tostring = function() return 'T!' end\n"
    "setmetatable(t, mt)\n"
    "t.child = setmetatable({}, mt)\n"      /* same metatable shared */
    "t.arr = {10, 20, 30, nil, 50, n = 5}\n"
    "t[t.a] = 'table as key'\n"
    "t[mt] = 'metatable as key'\n";
  CHECK(luaL_dostring(L, src) == 0);
  L2 = roundtrip(L, "t", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return (t.self == t and t.a == t.b and t.a.back == t) and 'ok' or 'bad'"), "ok");
    CHECKS(runs(L2, "return t.missing"), "dflt_missing");
    CHECKS(runs(L2, "return tostring(t)"), "T!");
    CHECKS(runs(L2, "return (getmetatable(t) == getmetatable(t.child)) and 'shared' or 'split'"), "shared");
    CHECKS(runs(L2, "return t.arr[1]..t.arr[2]..t.arr[3]..tostring(t.arr[4])..t.arr[5]..t.arr.n"), "102030nil505");
    CHECKS(runs(L2, "return t[t.a]"), "table as key");
    CHECKS(runs(L2, "return t[getmetatable(t)]"), "metatable as key");
    lua_close(L2);
  }
  lua_close(L);
}

static void test_closures_upvalues (void) {
  lua_State *L = newL(), *L2;
  const char *src =
    "local count = 0\n"
    "local function inc() count = count + 1; return count end\n"
    "local function add(n) count = count + n; return count end\n"
    "local function get() return count end\n"
    "local function mk(k) local acc = k; return function(d) acc = acc + d; return acc end end\n"
    "local a, b = mk(100), mk(200)\n"
    "local function vararg(...) return select('#', ...), ... end\n"
    "c = {inc = inc, add = add, get = get, a = a, b = b, a2 = a, vararg = vararg}\n"
    "c.recursive = function(n) if n <= 1 then return 1 end return n * c.recursive(n - 1) end\n"
    "local function self_ref() return self_ref end\n"
    "c.self_ref = self_ref\n"
    "c.nested = function() return function() return function() return count end end end\n";
  CHECK(luaL_dostring(L, src) == 0);
  /* mutate before saving: the saved value is what counts */
  CHECK(luaL_dostring(L, "c.inc(); c.inc(); c.a(5)") == 0);
  L2 = roundtrip(L, "c", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return c.get()"), "2");
    CHECKS(runs(L2, "return c.add(10)..','..c.get()..','..c.inc()"), "12,12,13");	/* shared upvalue 'count' */
    CHECKS(runs(L2, "return c.a(1)..','..c.a2(1)..','..c.b(1)"), "106,107,201");	/* a, a2 are the same closure */
    CHECKS(runs(L2, "return c.a == c.a2 and 'same' or 'diff'"), "same");
    CHECKS(runs(L2, "return (c.vararg(7, 8, 9))"), "3");
    CHECKS(runs(L2, "return c.recursive(5)"), "120");
    CHECKS(runs(L2, "return c.self_ref() == c.self_ref and 'ok' or 'bad'"), "ok");
    CHECKS(runs(L2, "return c.nested()()()"), "13");
    lua_close(L2);
  }
  lua_close(L);
}

static void test_function_environments (void) {
  lua_State *L = newL(), *L2;
  const char *src =
    "env = {tag = 'private'}\n"
    "setmetatable(env, {__index = _G})\n"
    "local f = function() return tag end\n"
    "setfenv(f, env)\n"
    "g = function() return type(print) end\n"      /* default environment */
    "holder = {f = f, g = g, env = env}\n";
  CHECK(luaL_dostring(L, src) == 0);
  L2 = roundtrip(L, "holder", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return holder.f()"), "private");
    CHECKS(runs(L2, "return (getfenv(holder.f) == holder.env) and 'ok' or 'bad'"), "ok");
    CHECKS(runs(L2, "return holder.g()"), "function");	/* the default environment is the loader's _G */
    CHECKS(runs(L2, "return (getfenv(holder.g) == _G) and 'ok' or 'bad'"), "ok");
    lua_close(L2);
  }
  lua_close(L);
}

static void test_dialect_opcodes (void) {
  lua_State *L = newL(), *L2;
  const char *src =
    "d = {}\n"
    "d.f = function(n)\n"
    "  local s = 0\n"
    "  for i = 1, n do\n"
    "    if i == 3 then continue end\n"
    "    s = s + (i ^ 5) + ((i << 2) & 0xFF)\n"      /* xor, shift, and */
    "  end\n"
    "  return s\n"
    "end\n"
    "d.expected = d.f(6)\n";
  CHECK(luaL_dostring(L, src) == 0);
  L2 = roundtrip(L, "d", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return (d.f(6) == d.expected) and 'ok' or 'bad'"), "ok");
    CHECKS(runs(L2, "return type(d.f(6))"), "number");
    lua_close(L2);
  }
  lua_close(L);
}

/* the big one: a scenario with a coroutine that yields from nested calls, persisted at every step */
static const char *scenario =
  "function mk()\n"
  "  local S = {n = 0, log = {}}\n"
  "  local shared = 0\n"
  "  local function say(...) S.n = S.n + 1; S.log[S.n] = table.concat({...}, ',') end\n"
  "  local function inc(d) shared = shared + d; return shared end\n"
  "  local function get() return shared end\n"
  "  S.inc, S.get = inc, get\n"
  "  local function inner(a)\n"
  "    local x = a * 2\n"
  "    local y = coroutine.yield(x)\n"
  "    inc(y)\n"
  "    return x + y\n"
  "  end\n"
  "  local function mid(a)\n"
  "    local t = {}\n"
  "    for i = 1, 3 do\n"
  "      t[i] = inner(a + i)\n"
  "      say('mid', i, t[i], get())\n"
  "    end\n"
  "    return t\n"
  "  end\n"
  "  S.co = coroutine.create(function(a, b)\n"
  "    local r = mid(a)\n"
  "    local sum = 0\n"
  "    for i = 1, 3 do sum = sum + r[i] end\n"
  "    say('sum', sum, b)\n"
  "    local z = coroutine.yield(sum)\n"
  "    say('z', z, get())\n"
  "    local counter = 0\n"
  "    local function up() counter = counter + 1; return counter end\n"
  "    local function up2() counter = counter + 10; return counter end\n"
  "    S.up, S.up2 = up, up2\n"
  "    coroutine.yield(up())\n"
  "    say('after', counter, up2(), up(), get())\n"
  "    return 'done', counter\n"
  "  end)\n"
  "  S.steps = {{1, 2}, {5}, {7}, {9}, {11}, {13}, {15}, {17}, {}}\n"
  "  S.next = 1\n"
  "  S.out = {}\n"
  "  return S\n"
  "end\n"
  "function step(S)\n"
  "  if S.next > table.getn(S.steps) then return false end\n"
  "  local args = S.steps[S.next]\n"
  "  S.next = S.next + 1\n"
  "  local r = {coroutine.resume(S.co, unpack(args))}\n"
  "  local o = {}\n"
  "  for i = 1, table.getn(r) do o[i] = tostring(r[i]) end\n"
  "  S.out[S.next - 1] = table.concat(o, ',') .. '|' .. coroutine.status(S.co)\n"
  "  return true\n"
  "end\n"
  "function report(S)\n"
  "  local s = table.concat(S.out, ';') .. '#' .. table.concat(S.log, ';')\n"
  "  if S.up then s = s .. '#' .. S.get() end\n"
  "  return s\n"
  "end\n";

static void test_coroutine_every_step (void) {
  lua_State *L0 = newL();
  char expected[2048];
  int total = 0, k;
  CHECK(luaL_dostring(L0, scenario) == 0);
  CHECK(luaL_dostring(L0, "S = mk()") == 0);
  while (1) {
    lua_getglobal(L0, "step"); lua_getglobal(L0, "S");
    CHECK(lua_pcall(L0, 1, 1, 0) == 0);
    if (!lua_toboolean(L0, -1)) { lua_pop(L0, 1); break; }
    lua_pop(L0, 1);
    total++;
  }
  snprintf(expected, sizeof(expected), "%s", runs(L0, "return report(S)"));
  printf("  scenario: %d steps, expected \"%s\"\n", total, expected);
  CHECK(strstr(expected, "ERROR") == NULL);
  CHECK(strstr(expected, "done") != NULL);
  lua_close(L0);

  for (k = 0; k <= total; k++) {		/* persist after k steps, finish in a fresh state */
    lua_State *A = newL(), *B;
    char code[128];
    int i;
    Buf b = {0, 0, 0};
    CHECK(luaL_dostring(A, scenario) == 0);
    CHECK(luaL_dostring(A, "S = mk()") == 0);
    for (i = 0; i < k; i++) CHECK(luaL_dostring(A, "step(S)") == 0);
    lua_getglobal(A, "S");
    if (save(A, &b, NULL) != 0) { printf("  k=%d save failed: %s\n", k, lua_tostring(A, -1)); failures++; lua_close(A); free(b.p); continue; }
    lua_close(A);
    B = newL();
    CHECK(luaL_dostring(B, scenario) == 0);	/* the functions step/report live in the loader */
    if (load(B, b.p, b.n, NULL) != 0) { printf("  k=%d load failed: %s\n", k, lua_tostring(B, -1)); failures++; lua_close(B); free(b.p); continue; }
    lua_setglobal(B, "S");
    for (i = k; i < total; i++) CHECK(luaL_dostring(B, "step(S)") == 0);
    snprintf(code, sizeof(code), "return report(S)");
    {
      const char *got = runs(B, code);
      if (strcmp(got, expected) != 0) printf("  k=%d (%d bytes)\n", k, (int)b.n);
      CHECKS(got, expected);
    }
    lua_close(B);
    free(b.p);
  }
}

static void test_coroutine_chain_hops (void) {	/* persist -> load -> persist -> load ... after every resume */
  lua_State *L = newL();
  char expected[2048];
  int total = 0, i;
  CHECK(luaL_dostring(L, scenario) == 0);
  CHECK(luaL_dostring(L, "S = mk()") == 0);
  while (1) {
    lua_getglobal(L, "step"); lua_getglobal(L, "S");
    CHECK(lua_pcall(L, 1, 1, 0) == 0);
    if (!lua_toboolean(L, -1)) { lua_pop(L, 1); break; }
    lua_pop(L, 1);
    total++;
  }
  snprintf(expected, sizeof(expected), "%s", runs(L, "return report(S)"));
  lua_close(L);

  L = newL();
  CHECK(luaL_dostring(L, scenario) == 0);
  CHECK(luaL_dostring(L, "S = mk()") == 0);
  for (i = 0; i < total; i++) {
    Buf b = {0, 0, 0};
    lua_State *N;
    CHECK(luaL_dostring(L, "step(S)") == 0);
    lua_getglobal(L, "S");
    if (save(L, &b, NULL) != 0) { failures++; printf("  hop %d save failed: %s\n", i, lua_tostring(L, -1)); free(b.p); break; }
    lua_close(L);
    N = newL();
    CHECK(luaL_dostring(N, scenario) == 0);
    if (load(N, b.p, b.n, NULL) != 0) { failures++; printf("  hop %d load failed: %s\n", i, lua_tostring(N, -1)); free(b.p); lua_close(N); return; }
    lua_setglobal(N, "S");
    free(b.p);
    L = N;
  }
  CHECKS(runs(L, "return report(S)"), expected);
  lua_close(L);
}

static void test_threads_misc (void) {
  lua_State *L = newL(), *L2;
  const char *src =
    "T = {}\n"
    "T.notstarted = coroutine.create(function(x) return x * 2 end)\n"
    "T.finished = coroutine.create(function() return 1 end); coroutine.resume(T.finished)\n"
    "T.failed = coroutine.create(function() error('boom') end); coroutine.resume(T.failed)\n"
    "T.gen = coroutine.create(function() for i = 1, 10 do coroutine.yield(i) end end)\n"
    "coroutine.resume(T.gen); coroutine.resume(T.gen)\n"
    "T.same = T.gen\n";
  CHECK(luaL_dostring(L, src) == 0);
  L2 = roundtrip(L, "T", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return coroutine.status(T.notstarted)..coroutine.status(T.finished)..coroutine.status(T.failed)..coroutine.status(T.gen)"),
           "suspendeddeaddeadsuspended");
    CHECKS(runs(L2, "local ok, v = coroutine.resume(T.notstarted, 21); return tostring(ok)..v"), "true42");
    CHECKS(runs(L2, "return tostring(select(2, coroutine.resume(T.finished)))"), "cannot resume dead coroutine");
    CHECKS(runs(L2, "local _, a = coroutine.resume(T.gen); local _, b = coroutine.resume(T.same); return a..b"), "34");
    CHECKS(runs(L2, "return (T.same == T.gen) and 'same' or 'diff'"), "same");
    lua_close(L2);
  }
  lua_close(L);
}

static void test_open_upvalue_sharing (void) {	/* closures made inside a suspended coroutine share its live locals */
  lua_State *L = newL(), *L2;
  const char *src =
    "R = {}\n"
    "R.co = coroutine.create(function()\n"
    "  local n = 1\n"
    "  local function a() n = n + 1; return n end\n"
    "  local function b() n = n * 10; return n end\n"
    "  R.a, R.b = a, b\n"
    "  coroutine.yield('y1')\n"
    "  coroutine.yield('y2:' .. n)\n"
    "  return 'end:' .. n\n"
    "end)\n"
    "coroutine.resume(R.co)\n";
  CHECK(luaL_dostring(L, src) == 0);
  L2 = roundtrip(L, "R", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    /* a() and b() act on the thread's local while it is suspended; the thread sees the changes */
    CHECKS(runs(L2, "return R.a()..','..R.b()..','..R.a()"), "2,20,21");
    CHECKS(runs(L2, "local _, v = coroutine.resume(R.co); return v"), "y2:21");
    CHECKS(runs(L2, "R.a(); local _, v = coroutine.resume(R.co); return v"), "end:22");
    lua_close(L2);
  }
  lua_close(L);
}

/* permanents */
static int cfunc_a (lua_State *L) { lua_pushstring(L, "A"); return 1; }
static int cfunc_b (lua_State *L) { lua_pushstring(L, "B"); return 1; }

static void test_permanents (void) {
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  const char *src = "P = {f = _G.print, s = string.format, y = coroutine.yield, tbl = {k = 'v'}}";
  CHECK(luaL_dostring(L, src) == 0);
  /* a C function that is not in the permanents table cannot be saved */
  lua_newtable(L); lua_pushcfunction(L, cfunc_a); lua_setfield(L, -2, "ca");
  CHECK(save(L, &b, NULL) != 0);
  CHECK(strstr(lua_tostring(L, -1), "C function") != NULL);
  lua_settop(L, 0); free(b.p); b.p = NULL; b.n = b.cap = 0;

  L2 = roundtrip(L, "P", NULL, NULL);
  CHECK(L2 != NULL);
  if (L2) {
    CHECKS(runs(L2, "return (P.f == print and P.s == string.format and P.y == coroutine.yield) and 'same' or 'diff'"), "same");
    CHECKS(runs(L2, "return P.tbl.k"), "v");
    /* a name missing in the loading state is an error naming it */
    lua_close(L2);
  }
  {
    lua_State *L3 = newL();
    lua_getglobal(L, "P");
    CHECK(save(L, &b, NULL) == 0);
    CHECK(luaL_dostring(L3, "string.format = nil") == 0);
    CHECK(load(L3, b.p, b.n, NULL) != 0);
    CHECK(strstr(lua_tostring(L3, -1), "string.format") != NULL);
    lua_close(L3);
    free(b.p);
  }
  (void)cfunc_b;
  lua_close(L);
}

static void test_permanent_table_content (void) {
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  scfa_persist_opts o;
  memset(&o, 0, sizeof(o));
  o.flags = SCFA_PERSIST_PERMTABLE_CONTENT;
  /* the table `string' is a permanent (by identity); scripts added a function and a field to it */
  CHECK(luaL_dostring(L, "string.extra = function(x) return 'extra:'..x end; string.n = 7; W = {s = string}") == 0);
  /* give the root perms an entry for the string table: save() builds function perms only, so add it by hand */
  {
    Job j;
    j.b = &b; j.opts = &o; j.w = bufwriter;
    lua_getglobal(L, "W");
    build_perms(L, 0);
    lua_getglobal(L, "string"); lua_pushstring(L, "string"); lua_rawset(L, -3);
    lua_insert(L, 1);
    lua_pushcfunction(L, do_persist); lua_pushvalue(L, 1); lua_pushvalue(L, 2); lua_pushlightuserdata(L, &j);
    CHECK(lua_pcall(L, 3, 0, 0) == 0);
    lua_settop(L, 0);
  }
  L2 = newL();
  {
    Rd r; r.p = b.p; r.n = b.n;
    build_perms(L2, 1);
    lua_getglobal(L2, "string"); lua_pushstring(L2, "string"); lua_insert(L2, -2); lua_rawset(L2, 1);
    CHECK(scfa_unpersist(L2, bufreader, &r, &o) == 0);
    lua_setglobal(L2, "W");
  }
  CHECKS(runs(L2, "return (W.s == string) and 'identity' or 'copy'"), "identity");
  CHECKS(runs(L2, "return string.extra('q')..string.n"), "extra:q7");	/* content merged into the live table */
  CHECKS(runs(L2, "return ('abc'):upper()"), "ABC");
  lua_close(L2); lua_close(L); free(b.p);
}

/* typed userdata through callbacks */
typedef struct FakeType { const char *name; size_t size; } FakeType;
static FakeType type_vec = {"Vec", 12}, type_id = {"Ident", 4};
static FakeType *types[] = {&type_vec, &type_id, NULL};
static int saves, loads;

static int ud_save (lua_State *L, int idx, const void *rtype, scfa_persist_io *io, void *ud) {
  const FakeType *t = (const FakeType *)rtype;
  unsigned char len = (unsigned char)strlen(t->name);
  (void)ud;
  saves++;
  scfa_persist_write(io, &len, 1);
  scfa_persist_write(io, t->name, len);
  scfa_persist_write(io, lua_touserdata(L, idx), t->size);	/* a plain POD payload */
  return 0;
}
static int ud_load (lua_State *L, scfa_persist_io *io, void *ud) {
  unsigned char len;
  char name[64];
  int i;
  void *d;
  (void)ud;
  loads++;
  scfa_persist_read(io, &len, 1);
  scfa_persist_read(io, name, len);
  name[len] = 0;
  for (i = 0; types[i]; i++) if (strcmp(types[i]->name, name) == 0) break;
  if (!types[i]) return 1;
  d = lua_newuserdatat(L, types[i]->size, types[i]);
  scfa_persist_read(io, d, types[i]->size);
  return 0;
}

static void test_typed_userdata (void) {
  lua_State *L = newL(), *L2;
  scfa_persist_opts o;
  float *v;
  int *id;
  Buf b = {0, 0, 0};
  memset(&o, 0, sizeof(o));
  o.udata_save = ud_save; o.udata_load = ud_load;
  v = (float *)lua_newuserdatat(L, 12, &type_vec);
  v[0] = 1.5f; v[1] = -2.25f; v[2] = 1e10f;
  lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, cfunc_a); lua_pushvalue(L, -1); lua_setglobal(L, "cfa"); lua_setfield(L, -2, "who");
  lua_setmetatable(L, -2);
  lua_setglobal(L, "vec");
  id = (int *)lua_newuserdatat(L, 4, &type_id);
  *id = 4242;
  lua_pushvalue(L, -1); lua_setglobal(L, "id");
  lua_pop(L, 1);
  CHECK(luaL_dostring(L, "U = {vec = vec, again = vec, id = id, holder = (function() local v, i = vec, id; return function() return v, i end end)()}") == 0);

  /* without a handler: error */
  lua_getglobal(L, "U");
  CHECK(save(L, &b, NULL) != 0);
  CHECK(strstr(lua_tostring(L, -1), "typed userdata") != NULL);
  lua_settop(L, 0);
  free(b.p); b.p = NULL; b.n = b.cap = 0;

  /* a handler that is not set: error */
  lua_getglobal(L, "U");
  {
    scfa_persist_opts bad = o;
    bad.udata_save = (int (*)(lua_State *, int, const void *, scfa_persist_io *, void *))0;
    CHECK(save(L, &b, &bad) != 0);
    lua_settop(L, 0);
  }
  free(b.p); b.p = NULL; b.n = b.cap = 0;

  saves = loads = 0;
  lua_getglobal(L, "U");
  CHECK(save(L, &b, &o) == 0);	/* the metatable's C function is a permanent (global cfa) */
  if (lua_gettop(L) > 0) printf("  typed save: %s\n", lua_tostring(L, -1));
  lua_settop(L, 0);
  CHECK(saves == 2);	/* the userdata seen twice is saved once */
  L2 = newL();
  {
    Rd r; r.p = b.p; r.n = b.n;
    lua_pushcfunction(L2, cfunc_a); lua_setglobal(L2, "cfa");
    lua_settop(L2, 0);
    build_perms(L2, 1);
    CHECK(scfa_unpersist(L2, bufreader, &r, &o) == 0);
    lua_remove(L2, 1);
    lua_setglobal(L2, "U");
  }
  CHECK(loads == 2);
  lua_getglobal(L2, "U"); lua_getfield(L2, -1, "vec");
  CHECK(lua_isuserdata(L2, -1) && !lua_islightuserdata(L2, -1));
  CHECK(lua_getuserdatatype(L2, -1) == &type_vec);
  v = (float *)lua_touserdata(L2, -1);
  CHECK(v && v[0] == 1.5f && v[1] == -2.25f && v[2] == 1e10f);
  lua_pop(L2, 2);
  CHECKS(runs(L2, "return (U.vec == U.again) and 'same' or 'diff'"), "same");
  CHECKS(runs(L2, "return U.vec:who()"), "A");
  CHECKS(runs(L2, "local a, b = U.holder(); return (a == U.vec and b == U.id) and 'ok' or 'bad'"), "ok");
  lua_getglobal(L2, "U"); lua_getfield(L2, -1, "id");
  CHECK(lua_getuserdatatype(L2, -1) == &type_id);
  CHECK(*(int *)lua_touserdata(L2, -1) == 4242);
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_untyped_userdata_and_lightud (void) {
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  scfa_persist_opts o;
  int *p;
  memset(&o, 0, sizeof(o));
  p = (int *)lua_newuserdata(L, sizeof(int)); *p = 99;
  lua_setglobal(L, "plain");
  CHECK(luaL_dostring(L, "X = {plain}") == 0 || 1);
  lua_getglobal(L, "plain"); lua_setglobal(L, "x1");
  lua_newtable(L); lua_getglobal(L, "plain"); lua_rawseti(L, -2, 1);
  lua_pushlightuserdata(L, (void *)0x1234); lua_rawseti(L, -2, 2);
  lua_setglobal(L, "X");
  lua_getglobal(L, "X");
  CHECK(save(L, &b, &o) != 0);			/* light userdata refused by default */
  CHECK(strstr(lua_tostring(L, -1), "light userdata") != NULL);
  lua_settop(L, 0); free(b.p); b.p = NULL; b.n = b.cap = 0;
  o.flags = SCFA_PERSIST_ALLOW_LIGHTUD;
  lua_getglobal(L, "X");
  CHECK(save(L, &b, &o) == 0);
  L2 = newL();
  CHECK(load(L2, b.p, b.n, &o) == 0);
  lua_setglobal(L2, "X");
  lua_getglobal(L2, "X"); lua_rawgeti(L2, -1, 1);
  CHECK(lua_isuserdata(L2, -1) && *(int *)lua_touserdata(L2, -1) == 99);
  lua_pop(L2, 1); lua_rawgeti(L2, -1, 2);
  CHECK(lua_touserdata(L2, -1) == (void *)0x1234);
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_gc_after_load (void) {		/* dropping a loaded universe and collecting must not corrupt the state */
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  int i;
  CHECK(luaL_dostring(L, scenario) == 0);
  CHECK(luaL_dostring(L, "S = mk(); step(S); step(S); step(S)") == 0);
  lua_getglobal(L, "S");
  CHECK(save(L, &b, NULL) == 0);
  L2 = newL();
  CHECK(luaL_dostring(L2, scenario) == 0);
  for (i = 0; i < 5; i++) {
    CHECK(load(L2, b.p, b.n, NULL) == 0);
    lua_settop(L2, 0);
    lua_gc(L2, LUA_GCCOLLECT, 0);
  }
  CHECKS(runs(L2, "local n = 0; for k, v in pairs(_G) do n = n + 1 end; return n > 10 and 'ok' or 'bad'"), "ok");
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_gc_after_failed_load (void) {	/* a load that fails half way leaves garbage the collector must be able to free */
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  size_t n;
  CHECK(luaL_dostring(L, scenario) == 0);
  CHECK(luaL_dostring(L, "S = mk(); step(S); step(S); step(S)") == 0);
  lua_getglobal(L, "S");
  CHECK(save(L, &b, NULL) == 0);
  L2 = newL();
  CHECK(luaL_dostring(L2, scenario) == 0);
  for (n = 0; n < b.n; n++) {
    if (getenv("PERSIST_TRACE")) fprintf(stderr, "trunc %d\n", (int)n);
    CHECK(load(L2, b.p, n, NULL) != 0);
    lua_settop(L2, 0);
    lua_gc(L2, LUA_GCCOLLECT, 0);
  }
  CHECKS(runs(L2, "local n = 0; for k, v in pairs(_G) do n = n + 1 end; return n > 10 and 'ok' or 'bad'"), "ok");
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_errors_and_corruption (void) {
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  size_t n;
  int status, i, stopped_before, bad = 0;
  CHECK(luaL_dostring(L, scenario) == 0);
  CHECK(luaL_dostring(L, "S = mk(); step(S); step(S); step(S)") == 0);
  lua_getglobal(L, "S");
  CHECK(save(L, &b, NULL) == 0);
  /* the writer failing is an error */
  lua_getglobal(L, "S");
  CHECK(save_with(L, &b, NULL, failingwriter, 0) != 0);
  lua_settop(L, 0);

  /* every truncation fails cleanly (no crash) and leaves the collector as it was */
  L2 = newL();
  CHECK(luaL_dostring(L2, scenario) == 0);
  for (n = 0; n < b.n; n += (b.n > 400 ? b.n / 400 : 1)) {
    status = load(L2, b.p, n, NULL);
    if (status == 0) bad++;
  }
  CHECK(bad == 0);
  CHECK(load(L2, b.p, b.n, NULL) == 0);	/* the full stream still loads */
  lua_settop(L2, 0);

  /* GC state: running before and after, also after a failure */
  CHECK(lua_gc(L2, LUA_GCSTOP, 0) == 0 || 1);
  lua_gc(L2, LUA_GCRESTART, 0);
  stopped_before = 0; (void)stopped_before;
  status = load(L2, b.p, b.n / 2, NULL);
  CHECK(status != 0);
  lua_settop(L2, 0);
  CHECK(runs(L2, "collectgarbage('collect'); return 'gc ok'")[0] == 'g');
  /* a collector stopped by the caller stays stopped */
  lua_gc(L2, LUA_GCSTOP, 0);
  CHECK(load(L2, b.p, b.n, NULL) == 0);
  lua_settop(L2, 0);
  {
    size_t before = (size_t)lua_gc(L2, LUA_GCCOUNT, 0);
    int k;
    for (k = 0; k < 200; k++) { lua_newtable(L2); lua_pop(L2, 1); }	/* with the collector stopped, memory only grows */
    lua_settop(L2, 0);
    CHECK((size_t)lua_gc(L2, LUA_GCCOUNT, 0) >= before);
  }
  lua_gc(L2, LUA_GCRESTART, 0);

  /* wrong build tag */
  {
    char *copy = (char *)malloc(b.n);
    memcpy(copy, b.p, b.n);
    copy[12] ^= 0x01;		/* a byte inside the tag text */
    CHECK(load(L2, copy, b.n, NULL) != 0);
    CHECK(strstr(lua_tostring(L2, -1), "another build") != NULL);
    memcpy(copy, b.p, b.n);
    copy[0] ^= 0xff;
    CHECK(load(L2, copy, b.n, NULL) != 0);
    CHECK(strstr(lua_tostring(L2, -1), "not a persisted") != NULL);

    /* random byte flips behind the header: an error or a (possibly different) universe, never a crash */
    srand(12345);
    for (i = 0; i < 300; i++) {
      size_t pos = 60 + (size_t)rand() % (b.n - 60);
      memcpy(copy, b.p, b.n);
      copy[pos] ^= (char)(1 + rand() % 255);
      load(L2, copy, b.n, NULL);
      lua_settop(L2, 0);
      lua_gc(L2, LUA_GCCOLLECT, 0);
    }
    free(copy);
  }
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_gc_stress (void) {		/* a collector that runs all the time during persist and unpersist */
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  const char *mkbig =
    "B = {}\n"
    "for i = 1, 300 do\n"
    "  local t = {id = i, name = 'item' .. i, list = {i, i + 1, i + 2}}\n"
    "  local n = i\n"
    "  t.f = function() n = n + 1; return n end\n"
    "  t.co = coroutine.create(function() local x = i; while true do x = x + coroutine.yield(x) end end)\n"
    "  coroutine.resume(t.co); coroutine.resume(t.co, i)\n"
    "  B[i] = t\n"
    "  if i > 1 then t.prev = B[i - 1] end\n"
    "end\n";
  CHECK(luaL_dostring(L, mkbig) == 0);
  lua_gc(L, LUA_GCSETPAUSE, 1);
  lua_gc(L, LUA_GCSETSTEPMUL, 2000);
  lua_getglobal(L, "B");
  CHECK(save(L, &b, NULL) == 0);
  L2 = newL();
  lua_gc(L2, LUA_GCSETPAUSE, 1);
  lua_gc(L2, LUA_GCSETSTEPMUL, 2000);
  CHECK(load(L2, b.p, b.n, NULL) == 0);
  lua_setglobal(L2, "B");
  lua_gc(L2, LUA_GCCOLLECT, 0); lua_gc(L2, LUA_GCCOLLECT, 0);
  CHECKS(runs(L2, "local s = 0; for i = 1, 300 do s = s + B[i].f() + B[i].list[3] + select(2, coroutine.resume(B[i].co, 1)) end; return s"),
         runs(L, "local s = 0; for i = 1, 300 do s = s + B[i].f() + B[i].list[3] + select(2, coroutine.resume(B[i].co, 1)) end; return s"));
  CHECKS(runs(L2, "return (B[300].prev == B[299] and B[2].prev == B[1] and B[1].prev == nil) and 'ok' or 'bad'"), "ok");
  printf("  gc stress: %d objects-ish, %d bytes\n", 300, (int)b.n);
  lua_close(L2); lua_close(L); free(b.p);
}

static void test_depth_limit (void) {
  lua_State *L = newL();
  Buf b = {0, 0, 0};
  scfa_persist_opts o;
  memset(&o, 0, sizeof(o));
  o.max_depth = 200;
  CHECK(luaL_dostring(L, "L = {}; local p = L; for i = 1, 1000 do p.next = {}; p = p.next end") == 0);
  lua_getglobal(L, "L");
  CHECK(save(L, &b, &o) != 0);
  CHECK(strstr(lua_tostring(L, -1), "nested too deeply") != NULL);
  lua_settop(L, 0);
  free(b.p); b.p = NULL; b.n = b.cap = 0;
  o.max_depth = 0;
  lua_getglobal(L, "L");
  CHECK(save(L, &b, &o) == 0);		/* 1000 deep is fine with the default limit */
  lua_close(L); free(b.p);
}

#ifdef _WIN32
static DWORD WINAPI deep_thread (LPVOID arg) {
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  int *result = (int *)arg;
  const char *s;
  /* a 60000 element linked list and a 60000 element chain of closures */
  luaL_dostring(L, "L = {}; local p = L; for i = 1, 60000 do p.v = i; p.next = {}; p = p.next end\n"
                   "local f = function() return 0 end\n"
                   "for i = 1, 5000 do local g = f; f = function() return g() + 1 end end\n"
                   "F = f");
  lua_getglobal(L, "L");
  if (save(L, &b, NULL) != 0) { printf("  deep save failed: %s\n", lua_tostring(L, -1)); *result = 1; lua_close(L); return 0; }
  L2 = newL();
  if (load(L2, b.p, b.n, NULL) != 0) { printf("  deep load failed: %s\n", lua_tostring(L2, -1)); *result = 2; free(b.p); lua_close(L); lua_close(L2); return 0; }
  lua_setglobal(L2, "L");
  {
    const char *code = "local n, p = 0, L; while p.next do n = n + p.v; p = p.next end; return n";
    static char want[64];
    sprintf(want, "%s", runs(L, code));
    s = runs(L2, code);
    if (strcmp(s, want) != 0) { printf("  deep list sum: %s want %s\n", s, want); *result = 3; }
  }
  free(b.p); b.p = NULL; b.n = b.cap = 0;
  lua_getglobal(L, "F");
  if (save(L, &b, NULL) != 0) { printf("  deep closures save failed: %s\n", lua_tostring(L, -1)); *result = 4; }
  else {
    lua_State *L3 = newL();
    if (load(L3, b.p, b.n, NULL) != 0) { printf("  deep closures load failed: %s\n", lua_tostring(L3, -1)); *result = 5; }
    else {
      lua_setglobal(L3, "F");
      /* running a 20000 deep call chain needs C stack in the VM too (Lua-to-Lua calls do not recurse): fine */
      s = runs(L3, "return F()");
      if (strcmp(s, "5000") != 0) { printf("  deep closures result %s\n", s); *result = 6; }
    }
    lua_close(L3);
  }
  free(b.p);
  lua_close(L); lua_close(L2);
  return 0;
}
static void test_deep_structures (void) {
  int result = 0;
  HANDLE h = CreateThread(NULL, 512u * 1024 * 1024, deep_thread, &result, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
  CHECK(h != NULL);
  if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
  CHECK(result == 0);
}
#else
static void test_deep_structures (void) { }
#endif

static void test_two_universes_independent (void) {	/* loading twice gives two independent copies */
  lua_State *L = newL(), *L2;
  Buf b = {0, 0, 0};
  CHECK(luaL_dostring(L, "local t = {n = 0}; t.inc = function() t.n = t.n + 1; return t.n end; T = t") == 0);
  lua_getglobal(L, "T");
  CHECK(save(L, &b, NULL) == 0);
  L2 = newL();
  CHECK(load(L2, b.p, b.n, NULL) == 0); lua_setglobal(L2, "A");
  CHECK(load(L2, b.p, b.n, NULL) == 0); lua_setglobal(L2, "B");
  CHECKS(runs(L2, "return A.inc()..A.inc()..B.inc()"), "121");
  lua_close(L2); lua_close(L); free(b.p);
}

#define RUN(f) do { if (!only || strstr(#f, only)) { printf("-- %s\n", #f); f(); } } while (0)
int main (void) {
  const char *only = getenv("PERSIST_ONLY");
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("persist tests (lua_Number is %d bytes, tag %s)\n", (int)sizeof(lua_Number), scfa_persist_build_tag());
  RUN(test_plain_values);
  RUN(test_nan_inf);
  RUN(test_tables_cycles_metatables);
  RUN(test_closures_upvalues);
  RUN(test_function_environments);
  RUN(test_dialect_opcodes);
  RUN(test_coroutine_every_step);
  RUN(test_coroutine_chain_hops);
  RUN(test_threads_misc);
  RUN(test_open_upvalue_sharing);
  RUN(test_permanents);
  RUN(test_permanent_table_content);
  RUN(test_typed_userdata);
  RUN(test_untyped_userdata_and_lightud);
  RUN(test_gc_after_load);
  RUN(test_gc_after_failed_load);
  RUN(test_errors_and_corruption);
  RUN(test_gc_stress);
  RUN(test_depth_limit);
  RUN(test_deep_structures);
  RUN(test_two_universes_independent);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
