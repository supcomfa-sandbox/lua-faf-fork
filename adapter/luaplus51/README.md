# luaplus51: LuaPlus on Lua 5.1 (first pieces)

The engine-facing LuaPlus layer (`LuaObject`, `LuaState`) over the 5.1 SCFA core, with the layouts the engine
depends on (census D02, D11, D14 in the seams map; `FA-Binary-Patches/include/LuaAPI.h`):

| | |
|---|---|
| `LuaState` | `static_assert`ed 0x34 bytes (x86), `lua_State *m_state` at offset 0 (the engine reads `[LuaState+0]` inline), same member order as the exe: `ForMultipleThreads`, `m_ownState`, `m_threadObj`, `m_rootState`, used-list head/tail sentinels |
| `LuaObject` | `static_assert`ed 0x14 bytes: `m_next@0 m_prev@4 m_state@8 m_object.tt@0xc m_object.value@0x10`; `tt` holds the ENGINE type number (5 table, 6 C function, 7 Lua function, 8 userdata, 9 thread), so inline `Type()` reads keep working |

A `LuaObject` is a handle: nil / boolean / float32 number inline in `m_object.value`; everything else is a registry
reference (`luaL_ref`) owned by the object (double-number builds also reference numbers). Copy = new reference,
assign = release + copy, destroy = `luaL_unref`. The intrusive used list (`m_next`/`m_prev`) is kept per `LuaState`;
a state that dies first turns the remaining objects into nil, state-less handles.

Implemented: constructors (nil, on a state, from a stack index, copy, from `LuaStackObject`), `=`, `Reset`, type queries,
`GetBoolean/Number/Integer/String/LightUserData`, `PushStack`, `GetByName`, `operator[]`, `GetObject`, `SetObject`
(string / int / object keys), `SetNil/Boolean/Number/Integer/String`, `GetTableCount`, `Get/SetMetaTable`, `Assign*`,
`==`; `LuaState`: root and coroutine constructors, `FromCState` (via `lua_get/setstateuserdata`), `GetGlobals`,
`GetGlobal`, `CreateTable`, `DoString`.

Errors: a wrong-type access (`GetNumber` on a table, `SetString` on nil, ...) is a Lua error. Build the core as C++
(`-x c++ -DSCFA_CPP_ERRORS`, define `SCFA_LUA_IS_CPP` when including `luaplus51.h`) and it is thrown as
`scfa_LuaError`, with the C++ frames' destructors run, like the exe; with the C core (`luaplus51.h` then wraps the
headers in `extern "C"`) it reaches the panic function.

Not done: the rest of the LuaPlus surface (`LuaObject::Register`, `CreateTable(key)`, `AssignNewUserData` with RType,
`Lookup`, `Insert`, `Clone/DeepCopy`, `LuaState::CheckString/ArgError/Error`, standard-library selection details),
the exe's calling conventions (thiscall; the detour thunks absorb that), a coroutine wrapper must be destroyed
before its root `LuaState`, the engine's `LuaState` free-list of `LuaObject`s (only the used list is modelled).

Tests: `adapter/luaplus51/run.sh [outdir] [cflags]` (from the repo root; `-DLUA_NUMBER_FLOAT32` for the game's numbers).
