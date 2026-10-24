# persist/ provenance

`scfa_persist.c/.h` persist and restore a Lua 5.1 universe (tables with cycles and metatables, closures with shared
upvalues, protos, coroutines suspended anywhere, typed userdata through callbacks, permanents). It is derived from
**Pluto 2.4**, the heavy duty persistence library for Lua 5.1 by Ben Sunshine-Hill.

| | |
|---|---|
| Upstream | https://github.com/hoelzro/pluto (the maintained mirror of Pluto; "Transfer of ownership" note: now maintained by Stefan Reich of LuaOS, http://luaos.net) |
| Commit | `bf1ab52e4c04a402ec37e6dbfb9cc27832fd101b` (2012-11-20), Pluto 2.4 |
| Original home | luaforge (pluto), Pluto 1.x was for Lua 5.0, 2.x for 5.1 |
| License | public domain, copyright 2004 Ben Sunshine-Hill ("released into the public domain", with the usual MIT-style warranty disclaimer; the author asks politely for an email about applications). Text in `LICENSE-PLUTO.txt`. Compatible with this repo's MIT. |
| Files kept unmodified | `upstream/` (pluto.c, pluto.h, README, CHANGELOG, FILEFORMAT, THANKS) so `git diff --no-index upstream/pluto.c scfa_persist.c` shows what changed |
| Not taken | `pdep/` (copies of Lua 5.1.3 internal headers: the core's own headers are used), the makefiles, `pluto.vc.c`, `lzio.c` (the core has its own), tests |

Why Pluto and not a rewrite: it is the only maintained 5.1 library that persists functions and **threads** (Lua closures,
upvalues, coroutine stacks with CallInfo and saved pc). The other candidates (serpent, binser, lua-marshal, Lua `string.dump`
based code) cannot persist coroutines or live closures.

## What was changed (all in scfa_persist.c)

Dialect / core:
* Built against the core's own headers (`lstate.h`, `lfunc.h`, ...). Protos are written as the raw `Instruction` array, so
  the SCFA opcode set (OP_XOR, shifts, `continue` jumps) round-trips untouched; the stream header pins the build
  (`scfa_persist_build_tag`: Lua version, `lua_Number` size and kind, `Instruction` size, pointer size, `NUM_OPCODES` and an FNV hash
  of `luaP_opnames`), so a save from another opcode table is refused instead of crashing.
* `lua_Number` is written as its raw bytes (float32 build: 4 bytes, bit exact, NaN payloads included).
* The GPG userdata type slot (`Udata.rtype`, `lua_getuserdatatype`) selects the typed path; payloads go through callbacks
  (`scfa_persist_opts.udata_save/udata_load`), the metatable through the library.
* Works with both builds (C with longjmp errors, C++ with `-DSCFA_CPP_ERRORS`); tested with float32 and double numbers.

Fixes and hardening against Pluto 2.4:
* A C frame of a suspended thread (the `coroutine.yield` frame) has no saved pc; Pluto read `ci_func(ci)->l.p->code` through the C function pointer.
* Stack growth bypasses `lua_checkstack` (which caps a C function at `LUAI_MAXCSTACK` = 8000 slots) so deep structures work; recursion is bounded by `max_depth`.
* Fixed-width little-endian records instead of raw `int`/`size_t`; `ci->tailcalls`, the thread's own globals table and `errfunc` are saved.
* Open upvalues of a thread are re-pointed in one fix-up pass at the end (Pluto unlinked each from the collector list: O(n) per upvalue).
* No `__persist` metafunctions. Permanents only for tables, functions, userdata and threads (not strings and numbers).
  Optional `SCFA_PERSIST_PERMTABLE_CONTENT`: a permanent table is written by identity AND content, merged into the live table on load (the engine's named tables keep their contents in the 5.0 saves).
* The default environment of a function is written as "the loader's globals", not as a copy.
* `scfa_unpersist` is protected: truncated or corrupt data, wrong build, unknown permanents are Lua errors with a message, the collector is restored (stays stopped if the caller had stopped it), half built objects are safe for the collector.
* Tables are created with their array/hash size (iteration order after a load is NOT guaranteed to equal the order before).

Tests: `scfa-test/persist/run.sh` (C float32, C++ float32, C double; ~5100 checks incl. every-step coroutine round trip, GC stress, 300 random corruptions, 60000-deep structures in a big-stack thread).
