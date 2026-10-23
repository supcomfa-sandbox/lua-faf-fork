// LuaPlus on Lua 5.1: LuaObject (registry-reference handle) and LuaState. See luaplus51.h.
#include "luaplus51.h"

#include <cstring>

namespace LuaPlus {

namespace {

const bool kInlineNumber = sizeof(lua_Number) == sizeof(float);

// 5.1 type -> engine type number (scfa_gpgapi.h)
int EngineType(lua_State *L, int idx) { return lua_gpgtype(L, idx); }

inline lua_State *CState(LuaState *s) { return s->m_state; }

}  // namespace

// ----------------------------------------------------------------------------------- LuaObject

LuaObject::LuaObject() : m_next(0), m_prev(0), m_state(0) {
  m_object.tt = 0;
  m_object.value.ref = 0;
}

LuaObject::LuaObject(LuaState *state) : m_next(0), m_prev(0), m_state(0) {
  m_object.tt = 0;
  m_object.value.ref = 0;
  if (state) Link(state);
}

LuaObject::LuaObject(LuaState *state, int stackIndex) : m_next(0), m_prev(0), m_state(0) {
  m_object.tt = 0;
  m_object.value.ref = 0;
  if (state) {
    Link(state);
    lua_pushvalue(CState(state), stackIndex);
    TakeTop(state);
  }
}

LuaObject::LuaObject(const LuaObject &obj) : m_next(0), m_prev(0), m_state(0) {
  m_object.tt = 0;
  m_object.value.ref = 0;
  if (obj.m_state) {
    Link(obj.m_state);
    CopyValueFrom(obj);
  }
}

LuaObject::LuaObject(const LuaStackObject &stack) : m_next(0), m_prev(0), m_state(0) {
  m_object.tt = 0;
  m_object.value.ref = 0;
  if (stack.m_state) {
    Link(stack.m_state);
    lua_pushvalue(CState(stack.m_state), stack.m_stackIndex);
    TakeTop(stack.m_state);
  }
}

LuaObject::~LuaObject() { Reset(); }

LuaObject &LuaObject::operator=(const LuaObject &obj) {
  if (this == &obj) return *this;
  ReleaseValue();
  if (m_state != obj.m_state) {
    Unlink();
    if (obj.m_state) Link(obj.m_state);
  }
  if (obj.m_state) CopyValueFrom(obj);
  return *this;
}

LuaObject &LuaObject::operator=(const LuaStackObject &stack) {
  ReleaseValue();
  if (m_state != stack.m_state) {
    Unlink();
    if (stack.m_state) Link(stack.m_state);
  }
  if (stack.m_state) {
    lua_pushvalue(CState(stack.m_state), stack.m_stackIndex);
    TakeTop(stack.m_state);
  }
  return *this;
}

void LuaObject::Reset() {
  ReleaseValue();
  Unlink();
}

bool LuaObject::ByRef() const {
  switch (m_object.tt) {
    case 0: case 1: return false;
    case 3: return !kInlineNumber;
    default: return true;
  }
}

void LuaObject::ReleaseValue() {
  if (m_state && ByRef()) luaL_unref(CState(m_state), LUA_REGISTRYINDEX, m_object.value.ref);
  m_object.tt = 0;
  m_object.value.ref = 0;
}

void LuaObject::Link(LuaState *state) {
  // insert before the tail sentinel of the state's used list
  LuaObject *tail = state->TailNode();
  LuaObject *last = tail->m_prev;
  m_next = tail;
  m_prev = last;
  last->m_next = this;
  tail->m_prev = this;
  m_state = state;
}

void LuaObject::Unlink() {
  if (!m_state) return;
  m_prev->m_next = m_next;
  m_next->m_prev = m_prev;
  m_next = m_prev = 0;
  m_state = 0;
}

// pops the top of the state's stack into this object (which is linked and holds no value)
void LuaObject::TakeTop(LuaState *state) {
  lua_State *L = CState(state);
  int et = EngineType(L, -1);
  switch (et) {
    case 0:
      lua_pop(L, 1);
      break;
    case 1:
      m_object.tt = 1;
      m_object.value.b = lua_toboolean(L, -1);
      lua_pop(L, 1);
      break;
    case 3:
      if (kInlineNumber) {
        m_object.tt = 3;
        m_object.value.n = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        break;
      }
      // fall through: double numbers do not fit in the 4-byte value
    default:
      m_object.value.ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pops
      m_object.tt = et;
      break;
  }
}

void LuaObject::CopyValueFrom(const LuaObject &o) {
  if (!ByRef() && !o.ByRef()) {
    m_object = o.m_object;
    return;
  }
  lua_State *L = CState(m_state);
  o.PushStack(L);
  TakeTop(m_state);
}

lua_State *LuaObject::GetActiveCState() const { return m_state ? m_state->m_state : 0; }

void LuaObject::PushStack(lua_State *L) const {
  switch (m_object.tt) {
    case 0: lua_pushnil(L); break;
    case 1: lua_pushboolean(L, m_object.value.b); break;
    case 3:
      if (kInlineNumber) { lua_pushnumber(L, m_object.value.n); break; }
      // fall through
    default: lua_rawgeti(L, LUA_REGISTRYINDEX, m_object.value.ref); break;
  }
}

LuaStackObject LuaObject::PushStack(LuaState *state) const {
  PushStack(CState(state));
  return LuaStackObject(state, lua_gettop(CState(state)));
}

const char *LuaObject::TypeName() const {
  static const char *const names[] = {"nil",    "boolean",  "userdata", "number",   "string",   "table",
                                      "function", "function", "userdata", "thread"};
  return (m_object.tt >= 0 && m_object.tt <= 9) ? names[m_object.tt] : "?";
}

bool LuaObject::IsInteger() const {
  if (!IsNumber()) return false;
  lua_Number n = GetNumber();
  return n == static_cast<lua_Number>(static_cast<int>(n));
}

void LuaObject::TypeError(const char *op) const {
  lua_State *L = CState(m_state);
  if (!L) return;
  lua_pushfstring(L, "attempt to %s a %s value", op, TypeName());
  lua_error(L);  // throws scfa_LuaError in the C++ build
}

bool LuaObject::GetBoolean() const {
  if (!IsBoolean()) TypeError("get as boolean");
  return IsBoolean() && m_object.value.b != 0;
}

lua_Number LuaObject::GetNumber() const {
  if (!IsNumber()) { TypeError("get as number"); return 0; }
  if (kInlineNumber) return m_object.value.n;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_Number n = lua_tonumber(L, -1);
  lua_pop(L, 1);
  return n;
}

int LuaObject::GetInteger() const { return static_cast<int>(GetNumber()); }

const char *LuaObject::GetString() const {
  if (!IsString()) { TypeError("get as string"); return ""; }
  lua_State *L = CState(m_state);
  PushStack(L);
  const char *s = lua_tostring(L, -1);  // anchored by the registry reference: stays valid
  lua_pop(L, 1);
  return s;
}

void *LuaObject::GetLightUserData() const {
  if (!IsLightUserData()) { TypeError("get as lightuserdata"); return 0; }
  lua_State *L = CState(m_state);
  PushStack(L);
  void *p = lua_touserdata(L, -1);
  lua_pop(L, 1);
  return p;
}

// ---- tables

LuaObject LuaObject::GetByName(const char *name) const {
  if (!m_state) return LuaObject();
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_getfield(L, -1, name);  // errors (non-indexable) are Lua errors
  LuaObject r(m_state);
  r.TakeTop(m_state);
  lua_pop(L, 1);
  return r;
}

LuaObject LuaObject::operator[](int key) const {
  if (!m_state) return LuaObject();
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushinteger(L, key);
  lua_gettable(L, -2);
  LuaObject r(m_state);
  r.TakeTop(m_state);
  lua_pop(L, 1);
  return r;
}

LuaObject LuaObject::GetObject(const LuaObject &key) const {
  if (!m_state) return LuaObject();
  lua_State *L = CState(m_state);
  PushStack(L);
  key.PushStack(L);
  lua_gettable(L, -2);
  LuaObject r(m_state);
  r.TakeTop(m_state);
  lua_pop(L, 1);
  return r;
}

void LuaObject::SetObject(const char *key, const LuaObject &value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  value.PushStack(L);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
}

void LuaObject::SetObject(int key, const LuaObject &value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushinteger(L, key);
  value.PushStack(L);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

void LuaObject::SetObject(const LuaObject &key, const LuaObject &value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  key.PushStack(L);
  value.PushStack(L);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

void LuaObject::SetNil(const char *key) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushnil(L);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
}

void LuaObject::SetBoolean(const char *key, bool value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
}

void LuaObject::SetNumber(const char *key, lua_Number value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushnumber(L, value);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
}

void LuaObject::SetInteger(const char *key, int value) const { SetNumber(key, static_cast<lua_Number>(value)); }

void LuaObject::SetString(const char *key, const char *value) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  lua_pushstring(L, value);
  lua_setfield(L, -2, key);
  lua_pop(L, 1);
}

int LuaObject::GetTableCount() const {
  if (!IsTable()) { TypeError("get the size of"); return 0; }
  lua_State *L = CState(m_state);
  PushStack(L);
  int n = static_cast<int>(lua_objlen(L, -1));
  lua_pop(L, 1);
  return n;
}

LuaObject LuaObject::GetMetaTable() const {
  if (!m_state) return LuaObject();
  lua_State *L = CState(m_state);
  PushStack(L);
  LuaObject r(m_state);
  if (lua_getmetatable(L, -1)) r.TakeTop(m_state);
  lua_pop(L, 1);
  return r;
}

void LuaObject::SetMetaTable(const LuaObject &metatable) const {
  if (!m_state) return;
  lua_State *L = CState(m_state);
  PushStack(L);
  metatable.PushStack(L);
  lua_setmetatable(L, -2);
  lua_pop(L, 1);
}

// ---- assignment

void LuaObject::AssignNil(LuaState *state) {
  ReleaseValue();
  if (m_state != state) { Unlink(); if (state) Link(state); }
}

void LuaObject::AssignBoolean(LuaState *state, bool value) {
  AssignNil(state);
  if (!state) return;
  lua_pushboolean(CState(state), value);
  TakeTop(state);
}

void LuaObject::AssignNumber(LuaState *state, lua_Number value) {
  AssignNil(state);
  if (!state) return;
  lua_pushnumber(CState(state), value);
  TakeTop(state);
}

void LuaObject::AssignInteger(LuaState *state, int value) { AssignNumber(state, static_cast<lua_Number>(value)); }

void LuaObject::AssignString(LuaState *state, const char *value) {
  AssignNil(state);
  if (!state) return;
  lua_pushstring(CState(state), value);
  TakeTop(state);
}

void LuaObject::AssignNewTable(LuaState *state, int narray, int nhash) {
  AssignNil(state);
  if (!state) return;
  lua_createtable(CState(state), narray, nhash);
  TakeTop(state);
}

bool LuaObject::operator==(const LuaObject &o) const {
  if (!m_state || !o.m_state) return !m_state && !o.m_state;
  lua_State *L = CState(m_state);
  PushStack(L);
  o.PushStack(L);
  bool eq = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return eq;
}

// ------------------------------------------------------------------------------------ LuaState

void LuaState::InitList() {
  m_headObject.m_next = TailNode();
  m_headObject.m_prev = 0;
  m_tailObject.m_next = 0;
  m_tailObject.m_prev = HeadNode();
}

LuaState::LuaState(StandardLibraries libs)
    : m_state(0), ForMultipleThreads(0), m_ownState(true), m_threadObj(), m_rootState(this) {
  InitList();
  m_state = luaL_newstate();
  if (!m_state) return;
  lua_setstateuserdata(m_state, this);
  if (libs != LIB_NONE) {
    lua_pushcfunction(m_state, luaopen_base);   lua_pushstring(m_state, "");              lua_call(m_state, 1, 0);
    lua_pushcfunction(m_state, luaopen_table);  lua_pushstring(m_state, LUA_TABLIBNAME);  lua_call(m_state, 1, 0);
    lua_pushcfunction(m_state, luaopen_string); lua_pushstring(m_state, LUA_STRLIBNAME);  lua_call(m_state, 1, 0);
    lua_pushcfunction(m_state, luaopen_math);   lua_pushstring(m_state, LUA_MATHLIBNAME); lua_call(m_state, 1, 0);
    if (libs == LIB_OSIO) {
      lua_pushcfunction(m_state, luaopen_io); lua_pushstring(m_state, LUA_IOLIBNAME); lua_call(m_state, 1, 0);
      lua_pushcfunction(m_state, luaopen_os); lua_pushstring(m_state, LUA_OSLIBNAME); lua_call(m_state, 1, 0);
    }
  }
}

LuaState::LuaState(LuaState *parent)
    : m_state(0), ForMultipleThreads(0), m_ownState(false), m_threadObj(), m_rootState(parent->m_rootState) {
  InitList();
  lua_State *P = parent->m_state;
  m_state = lua_newthread(P);  // on parent's stack
  // anchor the thread in the registry through an object of the root state
  m_threadObj.Link(m_rootState);
  m_threadObj.TakeTop(parent);  // pops the new thread from P's stack into the reference
  lua_setstateuserdata(m_state, this);  // the exe leaves a new thread's pointer 0: the wrapper sets its own
}

LuaState::~LuaState() {
  // detach the objects that outlive us: they become nil and state-less
  for (LuaObject *o = m_headObject.m_next; o != TailNode();) {
    LuaObject *next = o->m_next;
    if (!m_ownState && o->ByRef() && m_state) luaL_unref(m_state, LUA_REGISTRYINDEX, o->m_object.value.ref);
    o->m_object.tt = 0;
    o->m_object.value.ref = 0;
    o->m_next = o->m_prev = 0;
    o->m_state = 0;
    o = next;
  }
  if (m_state) lua_setstateuserdata(m_state, 0);
  if (m_ownState && m_state) lua_close(m_state);
  m_state = 0;
}

LuaObject LuaState::GetGlobals() {
  lua_pushvalue(m_state, LUA_GLOBALSINDEX);
  LuaObject r(this);
  r.TakeTop(this);
  return r;
}

LuaObject LuaState::GetGlobal(const char *name) {
  lua_getglobal(m_state, name);
  LuaObject r(this);
  r.TakeTop(this);
  return r;
}

LuaObject LuaState::CreateTable(int narray, int nhash) {
  lua_createtable(m_state, narray, nhash);
  LuaObject r(this);
  r.TakeTop(this);
  return r;
}

int LuaState::DoString(const char *chunk) {
  int status = luaL_loadstring(m_state, chunk);
  if (status == 0) status = lua_pcall(m_state, 0, 0, 0);
  return status;
}

int LuaState::UsedObjectCount() const {
  int n = 0;
  LuaState *self = const_cast<LuaState *>(this);
  for (LuaObject *o = m_headObject.m_next; o != self->TailNode(); o = o->m_next) n++;
  return n;
}

}  // namespace LuaPlus
