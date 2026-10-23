// Tests of LuaPlus-on-5.1 (LuaObject handle, LuaState wrapper) against the 5.1 SCFA core compiled as C++.
// Built by adapter/luaplus51/run.sh.

#include <cstdio>
#include <cstring>
#include "luaplus51.h"

using namespace LuaPlus;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool weak_entry_alive (LuaState &S) {
  S.DoString("alive = (W[1] ~= nil)");
  LuaObject a = S.GetGlobal("alive");
  return a.GetBoolean();
}

// ---- layout: the engine inline-reads these
static void test_layout () {
  CHECK(offsetof(LuaState, m_state) == 0);
  if (sizeof(void *) == 4) {
    CHECK(sizeof(LuaObject) == 0x14);
    CHECK(sizeof(LuaState) == 0x34);
  }
  LuaState S;
  CHECK(*reinterpret_cast<lua_State **>(&S) == S.m_state);  // [LuaState+0] is the lua_State
  CHECK(LuaState::FromCState(S.m_state) == &S);
  // m_object.tt at +0xc holds the engine type number, inline
  LuaObject t = S.CreateTable();
  CHECK(*reinterpret_cast<int *>(reinterpret_cast<char *>(&t) + 0xc) == 5);
  CHECK(*reinterpret_cast<LuaState **>(reinterpret_cast<char *>(&t) + 0x8) == &S);
  LuaObject n; n.AssignNumber(&S, 3.5f);
  CHECK(*reinterpret_cast<int *>(reinterpret_cast<char *>(&n) + 0xc) == 3);
  if (sizeof(lua_Number) == 4) CHECK(*reinterpret_cast<float *>(reinterpret_cast<char *>(&n) + 0x10) == 3.5f);
}

// ---- types and values
static void test_values () {
  LuaState S;
  LuaObject nil;
  CHECK(nil.IsNil() && nil.Type() == 0 && !nil.IsValid());
  LuaObject nilOnState(&S);
  CHECK(nilOnState.IsNil() && nilOnState.IsValid());

  LuaObject b; b.AssignBoolean(&S, true);
  CHECK(b.IsBoolean() && b.GetBoolean() == true);
  LuaObject num; num.AssignNumber(&S, 42);
  CHECK(num.IsNumber() && num.IsInteger() && num.GetInteger() == 42 && num.GetNumber() == 42);
  LuaObject frac; frac.AssignNumber(&S, 0.5f);
  CHECK(frac.IsNumber() && !frac.IsInteger() && frac.GetNumber() == 0.5f);
  LuaObject str; str.AssignString(&S, "hello");
  CHECK(str.IsString() && std::strcmp(str.GetString(), "hello") == 0 && str.IsConvertibleToString());
  LuaObject tbl = S.CreateTable();
  CHECK(tbl.IsTable() && tbl.Type() == 5 && !tbl.IsFunction());

  S.DoString("function lf() end");
  LuaObject lf = S.GetGlobal("lf");
  CHECK(lf.IsFunction() && lf.Type() == 7 && !lf.IsCFunction());
  LuaObject pf = S.GetGlobal("print");
  CHECK(pf.IsFunction() && pf.IsCFunction() && pf.Type() == 6);
  lua_newuserdata(S.m_state, 4);
  LuaObject ud(&S, -1);
  lua_pop(S.m_state, 1);
  CHECK(ud.IsUserData() && ud.Type() == 8);
  lua_newthread(S.m_state);
  LuaObject th(&S, -1);
  lua_pop(S.m_state, 1);
  CHECK(th.IsThread() && th.Type() == 9);
  CHECK(std::strcmp(tbl.TypeName(), "table") == 0 && std::strcmp(th.TypeName(), "thread") == 0);
  CHECK(lua_gettop(S.m_state) == 0);  // no stack leaks
}

// ---- copy / assign / destroy and the registry references
static void test_handles () {
  LuaState S;
  S.DoString("W = setmetatable({}, {__mode = 'v'}); W[1] = {}");
  CHECK(S.UsedObjectCount() == 0);
  {
    LuaObject w = S.GetGlobal("W");
    LuaObject e = w[1];                       // operator[](int)
    CHECK(e.IsTable());
    S.DoString("collectgarbage()");
    CHECK(weak_entry_alive(S));               // the handle roots the table
    {
      LuaObject copy(e);                      // copy ctor: its own reference
      LuaObject assigned;
      assigned = e;                           // operator=
      e.Reset();
      S.DoString("collectgarbage()");
      CHECK(weak_entry_alive(S));             // the copies still root it
      CHECK(copy == assigned);
      CHECK(copy.IsTable() && assigned.IsTable());
      assigned = assigned;                    // self assignment is harmless
      CHECK(assigned.IsTable());
      copy.Reset();
      S.DoString("collectgarbage()");
      CHECK(weak_entry_alive(S));             // `assigned' alone
    }
    S.DoString("collectgarbage()");
    CHECK(!weak_entry_alive(S));              // all handles gone: collected
  }
  CHECK(S.UsedObjectCount() == 0);
  CHECK(lua_gettop(S.m_state) == 0);

  // reassigning to another value releases the old one
  S.DoString("W[2] = {}");
  {
    LuaObject h = S.GetGlobal("W")[2];
    S.DoString("collectgarbage()");
    h.AssignNumber(&S, 1);
    S.DoString("collectgarbage()");
    S.DoString("alive = (W[2] ~= nil)");
    CHECK(!S.GetGlobal("alive").GetBoolean());
    CHECK(h.IsNumber());
  }
}

// ---- Push
static void test_push () {
  LuaState S;
  LuaObject t = S.CreateTable();
  t.SetString("k", "v");
  t.PushStack(S.m_state);
  CHECK(lua_istable(S.m_state, -1));
  lua_getfield(S.m_state, -1, "k");
  CHECK(std::strcmp(lua_tostring(S.m_state, -1), "v") == 0);
  lua_pop(S.m_state, 1);
  LuaObject back(&S, -1);
  CHECK(back == t);                           // same table
  lua_pop(S.m_state, 1);
  LuaStackObject so = t.PushStack(&S);
  CHECK(so.m_stackIndex == 1 && so.m_state == &S);
  LuaObject viaStack(so);
  CHECK(viaStack == t);
  lua_settop(S.m_state, 0);
  LuaObject nums; nums.AssignNumber(&S, 7);
  nums.PushStack(S.m_state);
  CHECK(lua_tonumber(S.m_state, -1) == 7);
  LuaObject nil;
  nil.PushStack(S.m_state);
  CHECK(lua_isnil(S.m_state, -1));
  lua_settop(S.m_state, 0);
}

// ---- tables: GetByName / SetObject
static void test_tables () {
  LuaState S;
  LuaObject g = S.GetGlobals();
  CHECK(g.IsTable());
  LuaObject t = S.CreateTable();
  LuaObject v; v.AssignString(&S, "value");
  t.SetObject("name", v);
  t.SetObject(1, v);
  t.SetNumber("n", 5);
  t.SetInteger("i", 6);
  t.SetBoolean("flag", true);
  t.SetString("s", "str");
  g.SetObject("T", t);                        // publish to Lua
  S.DoString("r1 = T.name; r2 = T[1]; r3 = T.n; r4 = T.i; r5 = T.flag; r6 = T.s");
  CHECK(std::strcmp(S.GetGlobal("r1").GetString(), "value") == 0);
  CHECK(std::strcmp(S.GetGlobal("r2").GetString(), "value") == 0);
  CHECK(S.GetGlobal("r3").GetInteger() == 5 && S.GetGlobal("r4").GetInteger() == 6);
  CHECK(S.GetGlobal("r5").GetBoolean());
  CHECK(std::strcmp(S.GetGlobal("r6").GetString(), "str") == 0);
  CHECK(std::strcmp(t.GetByName("name").GetString(), "value") == 0);
  CHECK(std::strcmp(t["s"].GetString(), "str") == 0);
  CHECK(t.GetByName("missing").IsNil());
  CHECK(t.GetTableCount() == 1);
  t.SetNil("n");
  CHECK(t.GetByName("n").IsNil());
  // nested
  LuaObject inner = S.CreateTable();
  inner.SetString("x", "deep");
  t.SetObject("inner", inner);
  CHECK(std::strcmp(t["inner"]["x"].GetString(), "deep") == 0);
  // object keys
  LuaObject key; key.AssignString(&S, "objkey");
  t.SetObject(key, inner);
  CHECK(t.GetObject(key) == inner);
  // metatables and metamethods are honoured
  S.DoString("M = setmetatable({}, {__index = function(t, k) return k .. '!' end})");
  CHECK(std::strcmp(S.GetGlobal("M").GetByName("abc").GetString(), "abc!") == 0);
  LuaObject mt = S.GetGlobal("M").GetMetaTable();
  CHECK(mt.IsTable());
  LuaObject plain = S.CreateTable();
  CHECK(plain.GetMetaTable().IsNil());
  plain.SetMetaTable(mt);
  CHECK(std::strcmp(plain["zzz"].GetString(), "zzz!") == 0);
  // lookups on a handle created from a Lua global keep following changes
  S.DoString("G2 = {a = 1}");
  LuaObject g2 = S.GetGlobal("G2");
  S.DoString("G2.a = 2");
  CHECK(g2["a"].GetInteger() == 2);
  CHECK(lua_gettop(S.m_state) == 0);
}

// ---- wrapper: used list, threads, FromCState
static void test_state () {
  LuaObject survivor;
  {
    LuaState S;
    LuaObject a = S.CreateTable();
    survivor = S.CreateTable();
    CHECK(S.UsedObjectCount() == 2);
    CHECK(S.GetGlobals().IsTable());
  }
  // the state went away first: the object is nil and state-less, destroying it is safe
  CHECK(survivor.IsNil() && !survivor.IsValid());

  LuaState S;
  LuaState T(&S);                             // a coroutine wrapper
  CHECK(T.m_state != S.m_state);
  CHECK(LuaState::FromCState(T.m_state) == &T);
  CHECK(LuaState::FromCState(S.m_state) == &S);
  CHECK(!T.IsRoot() && S.IsRoot() && T.m_rootState == &S);
  CHECK(lua_gettop(S.m_state) == 0);          // the thread was moved off the stack into a reference
  // a script-created coroutine has no wrapper
  S.DoString("co = coroutine.create(function() end)");
  LuaObject co = S.GetGlobal("co");
  CHECK(co.IsThread());
  co.PushStack(S.m_state);
  CHECK(LuaState::FromCState(lua_tothread(S.m_state, -1)) == 0);
  lua_pop(S.m_state, 1);
  // run something on the thread wrapper; the global table is shared
  S.DoString("shared = 11");
  CHECK(T.GetGlobal("shared").GetInteger() == 11);
  LuaObject g = T.GetGlobals();
  g.SetNumber("fromT", 12);
  CHECK(S.GetGlobal("fromT").GetInteger() == 12);
}

// ---- errors are C++ exceptions across engine-shaped frames
static int dtors = 0;
struct Probe { ~Probe() { dtors++; } };

static void failing_get_number (LuaState &S) {
  Probe p;
  LuaObject t = S.CreateTable();
  t.GetNumber();                              // type error: throws
  CHECK(!"GetNumber on a table returned");
}

static void test_errors () {
  LuaState S;
  bool caught = false;
  try { failing_get_number(S); }
  catch (const scfa_LuaError &e) {
    caught = true;
    CHECK(e.L == S.m_state && e.status == LUA_ERRRUN);
    CHECK(std::strstr(lua_tostring(e.L, -1), "attempt to get as number a table value") != NULL);
  }
  CHECK(caught);
  CHECK(dtors == 1);                          // the frame's destructors ran
  CHECK(S.UsedObjectCount() == 0);            // and so did the LuaObject destructor (handle released)
  lua_settop(S.m_state, 0);

  // SetObject on a nil handle that has a state: indexing nil raises
  LuaObject nil(&S);
  caught = false;
  try { nil.SetString("k", "v"); }
  catch (const scfa_LuaError &e) { caught = true; CHECK(e.status == LUA_ERRRUN); }
  CHECK(caught);
  lua_settop(S.m_state, 0);

  // an error from a Lua function called through the wrapper surfaces as an exception too
  caught = false;
  try {
    S.DoString("function bad() error('lua failed') end");
    LuaObject f = S.GetGlobal("bad");
    f.PushStack(S.m_state);
    lua_call(S.m_state, 0, 0);
  }
  catch (const scfa_LuaError &e) {
    caught = true;
    CHECK(std::strstr(lua_tostring(e.L, -1), "lua failed") != NULL);
  }
  CHECK(caught);
  lua_settop(S.m_state, 0);
  // the state is fine afterwards
  S.DoString("ok = 1");
  CHECK(S.GetGlobal("ok").GetInteger() == 1);
  CHECK(S.DoString("error('protected')") == LUA_ERRRUN);  // DoString is protected: a status, no throw
}

int main () {
  test_layout();
  test_values();
  test_handles();
  test_push();
  test_tables();
  test_state();
  test_errors();
  std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures != 0;
}
