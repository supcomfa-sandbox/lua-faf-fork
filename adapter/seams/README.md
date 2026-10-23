# seams: the machine-readable map of every Lua entry the exe calls

A **seam** is a function inside the Lua VM / LuaPlus cluster of the game exe that is called (or has its address stored)
from *outside* that cluster: engine code, or FAF's binary patches. Those are the places where the adapter can cut in:
detour the exe's entry and forward the call, through `ILua5_0`, to a provider (original 5.0, or a Lua 5.1 fork).
Anything that touches Lua data *without* going through such a function (inline struct reads, baked-in constants, function
pointers stored by static init) is listed separately as **direct access**; those cannot be detoured and need a different fix.

Files

| file | what |
|---|---|
| `seams.schema.json` | JSON Schema (draft 2020-12). Validate with `ctrl.py re lua-seams validate`. |
| `faf-forgedalliance.seams.json` | the map for the FAF `ForgedAlliance.exe` build (source of truth, committed). Also carries the Steam `SupremeCommander.exe` addresses, see below. |
| `../include/ilua50_seams.h` | generated X-macro header for the hook DLL (`gen-header`), committed so the hook needs no tool run. |

Why one file with two address columns instead of one file per build: the census scanned the FAF exe (callers, functions,
subsystems, patch sites). The Steam build was only mapped function-by-function by code signature (167 of 168 mapped, 13 of them
"twin-ambiguous": the FAF code is byte-identical to another function, so the Steam address is the best guess by position)
and re-counted for call sites. There is no independent Steam caller analysis, so a second file would repeat the FAF data. If a Steam
census is ever done, add `steam-supremecommander.seams.json` with the same schema (`exe.faf` then names the Steam file).

## Schema, field by field

Top level: `schema_version` (1), `exe` (`faf`/`steam`: the file names the addresses refer to), `generated` (tool + note),
`seams[]`, `direct_access[]`.

`seams[]` item (example: `lua_pushnumber`):

```json
{
  "name": "lua_pushnumber",
  "layer": "lapi",
  "signature": "void lua_pushnumber(lua_State *, float)",
  "calling_convention": "cdecl", "calling_convention_source": "disasm", "calling_convention_evidence": "plain ret, ecx never read",
  "addr": {"faf": "0x90cd40", "steam": "0x978490", "steam_confidence": "unique"},
  "callers": {"sites": 270, "functions": 221, "patch_sites": 29, "steam_sites": 269},
  "subsystems": [{"name": "sim AI brain / platoon", "sites": 59}],
  "category": "push/to conversions",
  "ilua50": "lua_pushnumber",
  "break_51": {"type": "semantics", "detail": "lua_Number is float32 in GPG; ...", "fix_hint": "Detour the exe entry and forward ..."},
  "detourable": {"value": true, "level": "function-level"},
  "gpg_addition": false,
  "status": {"state": "in_interface", "provided_by": ["lua50_passthrough"]}
}
```

| field | meaning |
|---|---|
| `name` | unique. Lua API name, or `Class::method(argtypes)` for LuaPlus (overloads are told apart by the argument list). |
| `layer` | `lapi` Lua C API; `lauxlib` auxiliary lib (`luaL_*`); `luaplus` the LuaPlus C++ wrapper (`LuaObject::`, `LuaState::`); `gpg_ext` functions GPG *added* to the C API; `vm_internal` VM-internal functions (`luaV_`, `luaH_`, `luaD_`...) reached by FAF patch asm; `serializer` the six save/load callbacks stored as function pointers. |
| `signature` | C/C++ prototype from the FAF patch headers (`FA-Binary-Patches/include/LuaAPI.h`), parameter names often missing; `unknown` when no declaration exists. A hidden return pointer (e.g. `lua_touserdata(RRef *ret, ...)`) is shown as the header declares it. Note: the exe's `lua_Number` is float32; `ilua50.h` defaults to `double` (host override `ILUA50_NUMBER`). |
| `calling_convention` | `cdecl`, `thiscall` (this in ECX, MSVC), `stdcall`, `unknown`. `calling_convention_source`: `disasm` = classified from the function's own bytes (ECX read before write = thiscall; `ret N` vs `ret`); `inferred` = by name (a `Class::method` that never reads ECX is still thiscall; a `lua*` function that tail-jumps is cdecl). `calling_convention_evidence` says what was seen. |
| `addr` | `faf`/`steam`: preferred-base virtual addresses, lowercase `0x` hex (`steam` null if unmapped). `steam_confidence`: `unique`, `twin-ambiguous (identical code xN in FAF)`, `none`. |
| `callers` | `sites` engine call sites incl. tail jmps; `functions` distinct engine functions; `patch_sites` calls from FAF patch code; plus `tail_jmp_sites`, `eh_unwind_sites` (MSVC EH funclets, not in `sites`), `fnptr_store_sites`, `bound_c_function_callers`, `steam_sites`. |
| `subsystems` | top engine subsystems calling it (by address band of the callers), biggest first. |
| `category` | functional group from the census rules (`stack ops`, `table get/set`, `LuaPlus wrapper`, ...). |
| `ilua50` | the `ILua5_0` entry (`../include/ilua50.h`) that carries this seam, or `null`. Entries are matched by name; `validate --ilua50-h` checks every header entry has exactly one seam. |
| `break_51` | what changes on Lua 5.1. `type`: `none`, `semantics` (same signature, behaviour differs), `renamed` (replaced by another API), `removed`, `layout` (struct layout the caller or patch depends on). `detail` says how, `fix_hint` what the provider/shim has to do. |
| `detourable` | `value` false when the function cannot be cut at its entry (called from patch asm, or stored as a pointer); `reason` then says why. `level` is the census' detour class (`function-level`, `function-level (reimplement)`, `function-level (layout-dependent)`, `function-level (needs 5.1 core patch)`, ...). |
| `gpg_addition` | true for GPG-specific API (see below). |
| `status` | `state`: `unmapped` (no ILua5_0 entry yet), `in_interface` (entry exists), `detoured` (the hook detours it), `provided` (a non-passthrough provider implements it). `provided_by`: provider names from the `.toml` map `[providers]`. Rule: `ilua50` set <=> state is not `unmapped`. |
| `notes` | free text (call-site patterns, FAF patches that redirect callers). |

`direct_access[]` item: `id` (D01...), `site` (`where`, `faf`, `steam`: addresses or counts as text), `touches` (what data: struct, offsets),
`owner` (who has to be fixed: `luaplus` the LuaPlus wrapper, `faf_patch` a FAF binary patch, `serializer` the save/load code,
`engine` engine code proper with no owner we control, so the shim must keep the structure valid), `access` (read/write/call ABI/...),
`sites`, `break_51`, `fix_hint`, `evidence`.

## GPG additions (FAF depends on them, the 5.1 provider must supply them)

GPG changed Lua 5.0 in ways stock 5.1 does not have, and the engine and FAF patches rely on every one. A 5.1 provider that does not
implement them breaks the game even if every stock `lua_*` call works. `gpg_addition: true` (5 seams in the census):

| seam | what | 5.1 gap |
|---|---|---|
| `lua_getglobaluserdata` (`gpg_ext`, 142 engine sites) | engine context pointer stored in `global_State+0x148`; bound C functions fetch the Sim/UI object through it | no such slot in 5.1: add to the core or keep a side table keyed by the state |
| `lua_setglobaluserdata` (`gpg_ext`) | sets it | same |
| `lua_traceback` (`gpg_ext`) | GPG-added traceback function (1 engine site) | implement over `lua_getstack`/`lua_getinfo` |
| `LuaState::FromCState(L+0x44)` (`luaplus`, 42 sites) | `lua_State` -> owning `LuaState*` read from `L+0x44` | add a per-state user slot, or a map |
| `lua_touserdata` (`lapi`, GPG-changed signature) | takes/returns an `RRef {void*, RType*}` through a hidden pointer; the C++ type is stored in the userdata header | provider must carry an RType per userdata (larger header or side table) |

Also GPG-specific but not called from the engine proper in the census (so not seams): `lua_setusergcfunction` (per-global GC hook
at `G+0x14c`), `lua_getstateuserdata`/`lua_setstateuserdata`, `lua_setdefaultmetatable`, `luaopen_serialize`; see `direct_access` D05.
Whole LuaPlus layer (88 seams: `LuaObject`, `LuaState`) is GPG C++ code too, but it is not `gpg_addition`: it is its own layer
(`luaplus`) that the adapter reimplements over the new VM while keeping the 0x14-byte `LuaObject` layout.

## Regenerate

```
# 1. re-run the census against the installed exes (minutes; needs the reverse-engineer submodule) and write CSVs + JSON
uv run tools/ctrl.py re lua-seams regen --ilua50-h <this repo>/adapter/include/ilua50.h --json adapter/seams/faf-forgedalliance.seams.json
#    --reuse keeps the scan intermediates (rules/notes-only change takes seconds); --evidence re-runs the direct-access scans
# 2. CSVs only -> JSON
uv run tools/ctrl.py re lua-seams export --from out/lua-upgrade/seams --out adapter/seams/faf-forgedalliance.seams.json --ilua50-h adapter/include/ilua50.h
# 3. validate (schema + cross-checks: unique names/addresses, every ILua5_0 entry has exactly one seam, no dangling ilua50 links)
uv run tools/ctrl.py re lua-seams validate adapter/seams/faf-forgedalliance.seams.json --ilua50-h adapter/include/ilua50.h
# 4. hook header
uv run tools/ctrl.py re lua-seams gen-header --json adapter/seams/faf-forgedalliance.seams.json --out adapter/include/ilua50_seams.h
```

`ctrl.py` lives in the base repo (`tools/`); the scan module is `tools/supcom/re/lua_seams/`. CSVs and scan intermediates are scratch
(`out/lua-upgrade/seams/`); the JSON in this directory is the committed source of truth. For a new exe build point
`LUA_SEAMS_FAF_EXE` / `LUA_SEAMS_STEAM_EXE` at it and re-run step 1.

## How the hook and ILua5_0 use it

* `ilua50_seams.h` defines `ILUA50_SEAMS(X)` with one row per `ILua5_0` entry: `X(entry, faf_addr, steam_addr, "entry")`. The hook builds
  its detour table from it (pick `faf` or `steam` by the running exe), so no address is hand-copied. `ILUA50_ALL_SEAMS(X)` lists every
  seam (C-safe id, addresses, real name) for tools, coverage counters and the next entries.
* Growing the interface = add the entry to `ILUA50_FUNCS` in `ilua50.h` (name equal to the seam's `name`), set that seam's `ilua50` and
  `status` in the JSON (or re-run `regen --ilua50-h`), re-run `gen-header`. `validate --ilua50-h` fails if the header and the JSON disagree.
* Pick the next entries by `callers.sites` and `break_51.type` (sort the JSON): the hot cheap ones first, `detourable.value == false` and
  `direct_access` rows need their own plan.
* Calling convention matters for the detour thunk: `thiscall` seams (all `luaplus`) need a trampoline that moves ECX; `cdecl` ones do not.
