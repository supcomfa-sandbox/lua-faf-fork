// LuaPlus on Lua 5.1: the first pieces of the engine-facing LuaPlus layer (LuaObject, LuaState) over the
// 5.1 SCFA core, keeping the layouts the engine depends on (census D02 / D11 in
// adapter/seams/README.md of the seams branch; FA-Binary-Patches include/LuaAPI.h):
//
//   LuaState  0x34 bytes, `lua_State *m_state` at offset 0 (the engine reads [LuaState+0] inline to get
//             the lua_State for 86 % of its C API calls)
//   LuaObject 0x14 bytes {m_next@0, m_prev@4, m_state@8, m_object.tt@0xc, m_object.value@0x10}, embedded by
//             value in engine frames and members; the engine reads m_object.tt inline (engine type numbers,
//             scfa_gpgapi.h: 5 table, 6/7 function, 8 userdata, 9 thread)
//
// A LuaObject is a HANDLE: nil/boolean/number are stored inline in m_object.value; every other value is a
// reference in the registry (luaL_ref) owned by the object, so Lua never moves or frees what an object
// points to. m_next/m_prev keep the intrusive per-LuaState "used list" of the exe: a LuaState that goes away
// first detaches the objects that are left (they become nil, state-less).
//
// Build: the 5.1 core as C++ (-x c++ -DSCFA_CPP_ERRORS, define SCFA_LUA_IS_CPP when including this) so a Lua
// error raised by an operation here (e.g. GetNumber on a table, SetObject on nil) is a C++ exception
// (scfa_LuaError), like the exe; with the C core the same errors reach the panic function.
#pragma once

#include <cstddef>

#if defined(SCFA_LUA_IS_CPP)
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#else
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#endif

namespace LuaPlus {

class LuaState;
class LuaObject;

enum StandardLibraries { LIB_NONE, LIB_BASE, LIB_OSIO };

class LuaStackObject {
public:
  LuaStackObject() : m_state(0), m_stackIndex(0) {}
  LuaStackObject(LuaState *state, int index) : m_state(state), m_stackIndex(index) {}
  LuaState *m_state;
  int m_stackIndex;
};

class LuaObject {  // 0x14 bytes on x86
public:
  LuaObject();                                    // nil, no state
  explicit LuaObject(LuaState *state);            // nil, on a state
  LuaObject(LuaState *state, int stackIndex);     // a copy of the stack value (relative or absolute index)
  LuaObject(const LuaObject &obj);
  LuaObject(const LuaStackObject &stack);
  ~LuaObject();
  LuaObject &operator=(const LuaObject &obj);
  LuaObject &operator=(const LuaStackObject &stack);

  // --- type queries (engine type numbers) ---
  int Type() const { return m_object.tt; }
  const char *TypeName() const;
  bool IsNil() const { return m_object.tt == 0; }
  bool IsBoolean() const { return m_object.tt == 1; }
  bool IsLightUserData() const { return m_object.tt == 2; }
  bool IsNumber() const { return m_object.tt == 3; }
  bool IsInteger() const;
  bool IsString() const { return m_object.tt == 4; }
  bool IsConvertibleToString() const { return IsString() || IsNumber(); }
  bool IsTable() const { return m_object.tt == 5; }
  bool IsCFunction() const { return m_object.tt == 6; }
  bool IsFunction() const { return m_object.tt == 6 || m_object.tt == 7; }
  bool IsUserData() const { return m_object.tt == 8; }
  bool IsThread() const { return m_object.tt == 9; }
  LuaState *GetActiveState() const { return m_state; }
  lua_State *GetActiveCState() const;
  bool IsValid() const { return m_state != 0; }

  // --- values (a wrong type raises a Lua error: TypeError) ---
  bool GetBoolean() const;
  lua_Number GetNumber() const;
  lua_Number GetDouble() const { return GetNumber(); }
  int GetInteger() const;
  const char *GetString() const;                  // valid while this object (or another reference) is alive
  void *GetLightUserData() const;
  void TypeError(const char *op) const;

  // --- stack ---
  void PushStack(lua_State *L) const;             // pushes the value (nil when state-less)
  LuaStackObject PushStack(LuaState *state) const;

  // --- tables (metamethods are honoured, like the engine's luaV_gettable/settable) ---
  LuaObject GetByName(const char *name) const;
  LuaObject operator[](const char *key) const { return GetByName(key); }
  LuaObject operator[](int key) const;
  LuaObject GetObject(const LuaObject &key) const;
  void SetObject(const char *key, const LuaObject &value) const;
  void SetObject(int key, const LuaObject &value) const;
  void SetObject(const LuaObject &key, const LuaObject &value) const;
  void SetNil(const char *key) const;
  void SetBoolean(const char *key, bool value) const;
  void SetNumber(const char *key, lua_Number value) const;
  void SetInteger(const char *key, int value) const;
  void SetString(const char *key, const char *value) const;
  int GetTableCount() const;                      // raw length (lua_objlen)
  LuaObject GetMetaTable() const;
  void SetMetaTable(const LuaObject &metatable) const;

  // --- (re)assignment on a state ---
  void AssignNil(LuaState *state);
  void AssignBoolean(LuaState *state, bool value);
  void AssignNumber(LuaState *state, lua_Number value);
  void AssignInteger(LuaState *state, int value);
  void AssignString(LuaState *state, const char *value);
  void AssignNewTable(LuaState *state, int narray = 0, int nhash = 0);
  void Reset();                                   // nil, detached from its state

  bool operator==(const LuaObject &o) const;      // raw equality
  bool operator!=(const LuaObject &o) const { return !(*this == o); }

  // --- layout (do not reorder: the engine embeds this by value and reads the fields inline) ---
  LuaObject *m_next;
  LuaObject *m_prev;
  LuaState *m_state;
  struct TObject {
    int tt;                                       // engine type number
    union Value {
      int ref;                                    // registry reference (strings, tables, functions, userdata, threads)
      int b;                                      // boolean
      float n;                                    // number, when lua_Number is float32
      void *p;
    } value;
  } m_object;

private:
  friend class LuaState;
  void Link(LuaState *state);
  void Unlink();
  void ReleaseValue();
  void TakeTop(LuaState *state);                  // pops the top of state's lua_State into *this (value part)
  void CopyValueFrom(const LuaObject &o);
  bool ByRef() const;
};

class LuaState {  // 0x34 bytes on x86
public:
  explicit LuaState(StandardLibraries libs = LIB_BASE);
  explicit LuaState(LuaState *parentState);       // a coroutine of parentState
  ~LuaState();

  lua_State *GetActiveCState() const { return m_state; }
  LuaState *GetActiveState() { return this; }
  static LuaState *FromCState(lua_State *L) { return static_cast<LuaState *>(lua_getstateuserdata(L)); }
  LuaObject GetGlobals();
  LuaObject GetGlobal(const char *name);
  LuaObject CreateTable(int narray = 0, int nhash = 0);
  int DoString(const char *chunk);                // protected; 0 or a Lua status, message left on the stack on error
  int UsedObjectCount() const;                    // objects currently in the used list (test aid)
  bool IsRoot() const { return m_rootState == this; }

  // --- layout ---
  lua_State *m_state;                             // offset 0
  void *ForMultipleThreads;
  bool m_ownState;
  LuaObject m_threadObj;
  LuaState *m_rootState;
  struct MiniLuaObject {
    LuaObject *m_next;
    LuaObject *m_prev;
  } m_headObject, m_tailObject;

private:
  friend class LuaObject;
  LuaState(const LuaState &);                     // not copyable
  LuaState &operator=(const LuaState &);
  LuaObject *HeadNode() { return reinterpret_cast<LuaObject *>(&m_headObject); }
  LuaObject *TailNode() { return reinterpret_cast<LuaObject *>(&m_tailObject); }
  void InitList();
};

// The engine's layout contract (32-bit build; the 64-bit build is only for tooling).
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define SCFA_LP_ASSERT_LAYOUT(cond) static_assert(cond, #cond)
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || sizeof(LuaObject) == 0x14);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || sizeof(LuaState) == 0x34);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaObject, m_next) == 0x0);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaObject, m_prev) == 0x4);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaObject, m_state) == 0x8);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaObject, m_object) == 0xc);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || sizeof(LuaObject::TObject) == 0x8);
SCFA_LP_ASSERT_LAYOUT(offsetof(LuaState, m_state) == 0);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaState, m_threadObj) == 0xc);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaState, m_rootState) == 0x20);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaState, m_headObject) == 0x24);
SCFA_LP_ASSERT_LAYOUT(sizeof(void *) != 4 || offsetof(LuaState, m_tailObject) == 0x2c);

}  // namespace LuaPlus
