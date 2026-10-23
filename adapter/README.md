> **Moved.** The adapter (ILua5_0, middleware, lua50 passthrough, lua51 provider, seams JSON, tests) now lives in the base repo under
> `lua-upgrade/adapter` (https://github.com/supcomfa-sandbox/supreme-commander-forged-alliance, branch `issue-26-lua-ai-optimizations/207-lua-full-provider`;
> hook source in `lua-upgrade/hook`). This fork keeps only the Lua VM (5.0/5.1 dialect cores, port-adapter layout, GPG C API additions).
> Do not add to this `adapter/` directory; it is kept only until the base repo copy is merged (copied from `adapter/slice3` @ 5934b63).

# adapter: ILua5_0 + provider map (story #207, epic #26)

An adapter between the SupCom/FAF exe and its Lua scripts. It exposes the Lua 5.0 API the engine
already calls, takes those calls, and forwards them through one interface to a provider chosen by a
`.toml` map. Today every entry maps to a passthrough provider that calls the ORIGINAL 5.0 code
(scripts unchanged). Later an entry (or a script function) can be switched to a Lua 5.1 provider
(5.1 fork state + re-implemented scripts) by editing the map, with no code change.

```
  exe (engine code)
     | calls lua_call / lua_pcall / ...            (seam detours, addresses from the census)
     v
  LuaAdapter  (middleware, src/lua_adapter.c)      resolves target from the .toml map, counts calls
     | only ever speaks ILua5_0 - never knows which Lua is behind it
     v
  ILua5_0     (include/ilua50.h)  THE provider interface (no separate ILuaProvider)
     |------------------------------\
     v                              v
  lua50_passthrough provider       lua51 provider (later: 5.1 fork state + 5.1 scripts)
     | table of ORIGINAL 5.0 fns
     v
  exe's own 5.0 functions (in game)  /  harness runtime DLL (tests)
```

## Interface

`ILua5_0` is a struct of function pointers, `self` first (so several instances of one implementation can
coexist), then the exact Lua 5.0 signature. The single source of truth is the X-macro `ILUA50_FUNCS` in
`include/ilua50.h`; the struct, the middleware and the passthrough are all generated from it. `Lua50Funcs`
is the same list without `self` (the "original function" table).

Current set (23): `lua_open lua_close lua_newthread lua_gettop lua_settop lua_pushvalue lua_gettable
lua_settable lua_rawget lua_rawset lua_pushnumber lua_pushstring lua_pushboolean lua_pushnil lua_tonumber
lua_tostring lua_toboolean lua_type lua_next lua_call lua_pcall lua_resume lua_setgcthreshold`.

The census of every Lua entry the FAF exe calls is `seams/faf-forgedalliance.seams.json` (168 seams, schema-validated; see
[seams/README.md](seams/README.md)). Pick the next entries from it. Adding an entry = one line in `ILUA50_FUNCS` plus the
`Lua50Funcs` member, then set that seam's `ilua50`/`status` in the JSON and regenerate `include/ilua50_seams.h`
(`ctrl.py re lua-seams gen-header`), which the hook uses for its detour addresses.

`LuaProvider { name, ILua5_0 *api, init, shutdown, priv }`; `shutdown` releases the provider.

## Provider map (`.toml`)

Parser: tomlc99 (MIT), vendored in `third_party/tomlc99` (same copy and local patch as the base repo's
`custom_supcom_controller`, which already parses its controller `.toml` with it).

```toml
[providers]                # name -> implementation; `runtime` is an opaque string given to the impl factory
lua50 = { impl = "lua50_passthrough" }
lua51 = { impl = "lua51", runtime = "51faffloat" }

[default]
provider = "lua50"

[api]                      # per ILua5_0 function (seam level), optional
lua_open = "lua51"

[script]                   # per script function, optional ("chunk:function" or "chunk:*")
"/lua/sim/Projectile.lua:OnCollisionCheck" = "lua51"
```

Resolution of an API call: `[api].<fn>`, else the provider that OWNS the `lua_State` (the one whose
`lua_open`/`lua_newthread` made it; a state cannot move between providers), else `[default]`. So `[default]`
chooses where new states are created, and everything on that state follows. Mixing providers per call on one
state needs a state bridge (not built). Script keys: exact, then `chunk:*`, then default; chunk compare is
case-insensitive, `\` == `/`, leading `@` ignored. The script lookup (`lua_adapter_provider_for_script`)
exists and is tested; wiring it into dispatch needs a call context the plain C API does not carry (later slice).
Validation errors: unknown impl, provider, or `[api]` function name, missing `[default]`, bad toml.

Impl factories are registered by the host (`lua_adapter_register_impl`); the map only names them. Policy is in
the toml; the code has no addresses or provider choices.

## Filling the original-function table in game

`Lua50Funcs` is filled with trampolines to the exe's own entries. For each entry, detour the exe function so
the engine lands in `LuaAdapter`'s `ILua5_0` face (`lua_adapter_api`), and keep the detour's trampoline as the
`orig` pointer for the passthrough provider. Known addresses so far:

| entry | Steam | FAF |
|---|---|---|
| lua_call | 0x978b50 | census |
| lua_pcall | 0x978b80 | census |
| lua_resume | 0x97fd60 | census |
| lua_open | 0x98fc70 | census |
| lua_setgcthreshold | 0x978dc0 | 0x90d670 |

The rest come from the census; the addresses go in the hook's ini/toml (hook DLL is a generic engine).
Calling convention and the float32 `lua_Number` (`ILUA50_NUMBER`) must match the exe's functions: check with
`tools/disasm.py` per entry before detouring (the engine may use LuaPlus thiscall/register conventions that a
plain cdecl pointer type does not describe; the trampoline stubs in the hook, not this interface, absorb that).

## Known limits (what detours cannot cover)

- Engine code reading `lua_State` / `global_State` / `TObject` fields directly instead of calling the API.
- LuaPlus inlined accesses (stack/table reads compiled into the exe).
- The save serializer (walks VM structures directly), so a 5.1 provider's state is invisible to it.
- A provider other than 5.0 cannot share a `lua_State` with engine code that pokes its internals.

The seam map (every Lua entry the exe calls, FAF + Steam addresses, 5.1 break types, GPG additions the 5.1 provider must supply): [seams/README.md](seams/README.md); census note in the research book (`book.py find lua-seams`).

## Tests (offline steel thread)

`tests/`, 32-bit mingw (`PATH` must start with `C:\msys64\mingw32\bin`), `make test`:

- `test_adapter_map` - toml parsing, `[api]`/`[script]`/default precedence, owner routing, error cases; stub originals, no DLL.
- `test_adapter_steel <map.toml> <variant> <serving provider>` - engine-shaped calls (`add`; the
  OnCollisionCheck-shaped script, 5 cases; a coroutine) through middleware -> ILua5_0 -> passthrough -> a harness
  runtime DLL (`rt_shim.c` adapts `rt_abi.h` to plain 5.0 signatures). The same test runs three times by swapping
  only the toml: `lua50.toml` (rt `50`), `lua51.toml` (rt `51float`), `mixed.toml` (only `lua_open` -> lua51).
  The 5.1 side is a passthrough over a 5.1 runtime DLL, a stand-in for the real 5.1 provider.

Env: `LUA_UPGRADE_BIN` (runtime DLL dir, default the base repo `out/lua-upgrade/bin`), `LUA_UPGRADE_SCRIPTS`
(harness `tests/scripts`). `rt_abi.h` in `tests/` is a copy of the harness ABI v1.

## Slice 2 plan (in game, not done)

- Hook DLL `supcom_luaadapter`, built by the base repo's hook build (`dllFromC/hooks/supcom_luaadapter`, stamped
  per `tools/hooks`). Base repo consumes this fork as a submodule `git-submodules/lua-faf-fork` (proposed over a
  vendored copy: the adapter and the 5.1 fork state evolve together, and the hook build just compiles `adapter/`).
- Detours only `lua_call` first (Steam 0x978b50; FAF address from the census); the passthrough `orig` is the trampoline.
- Ini is the controller: a `[lua_adapter]` ini section (enabled by presence) gives the path of the provider-map
  `.toml` (and the detour address/enable list); all provider policy stays in that toml.
- Acceptance: game runs; sim checksums identical with and without the adapter; per-entry call counts
  (`lua_adapter_calls`) logged to the trace. Then add entries one slice at a time (lua_pcall, lua_resume, ...).
