# Lua 5.1.5 with the SCFA dialect

Lua 5.1.5 modified to understand the dialect of Lua that Supreme Commander: Forged Alliance
(GPG's modified Lua 5.0.1 / LuaPlus) runs, so game scripts can run unchanged on a 5.1 VM.
It is the 5.1 counterpart of the `lua-5.0` branch of
[FAForever/lua-lang](https://github.com/FAForever/lua-lang) (MIT); the feature list and the
operator semantics follow that fork. Everything else is stock 5.1.5 (the first commit of the
branch is the unmodified `lua-5.1.5.tar.gz`, sha256 `2640fc56...5333`).

License: MIT, see `COPYRIGHT` (Lua, Tecgraf PUC-Rio). The dialect ideas come from FAForever/lua-lang (MIT).

## Features (one commit each)

| Feature | Origin | Notes |
|---|---|---|
| `#` starts a line comment | GPG dialect (lua-lang) | The 5.1 length operator `#t` is therefore NOT available; use `table.getn(t)` |
| `!=` as a synonym of `~=` | GPG dialect | |
| `continue` statement | GPG dialect | Real jump: while / repeat / numeric and generic for, nested loops, closes upvalues of crossed blocks. Must be the last statement of its block. `no loop to continue` outside loops. In `repeat ... until c`, a `continue` that skips a local declared later is an error only if `c` reads that local |
| `& \| << >>` | GPG dialect | Precedence 8 (tighter than `*` `/`), left associative. Operands must both be numbers (no string coercion, no metamethods): ``"`&' operands must be numbers"``. Computed as unsigned 32-bit; shift counts are taken `& 31` |
| `^` is xor, pow operator removed | GPG dialect | Priority 10 left / 9 right. If both operands are numbers: 32-bit xor; otherwise string coercion, then the `__pow` metamethod. Power is `math.pow`. Opcode `OP_XOR`, `TM_XOR` |
| Hex literals | stock 5.1 already does this | tested only |
| UTF-8 BOM skipped | FAF files carry one | In the lexer (any chunk) and in `luaL_loadfile`; the first-line `#!` skip is stock 5.1 |
| `string.lualex` | GPG (stub) | Registered, returns 1, like lua-lang |
| Table constructor size hints `{&hash &array ...}` | LuaPlus | Parsed (optional `,`/`;` after the hints) and used as NEWTABLE size hints; no visible effect |
| `table.getsize`, `table.empty`, `table.unpack(t,i,j)`, `table.clone` | FA executable | `src/lscfa_tablefuncs.c`, on by `LUA_SCFA_TABLEFUNCS` (luaconf.h). `clone` is deep and cycle-safe, metatables shared |
| `for k,v in table do` | Lua 5.0 / game scripts | A table in place of the iterator function iterates with `next` (OP_TFORLOOP) |
| `table.setn` / `table.getn` | Lua 5.0 | `setn(t,n)` stores field `n`; `getn` returns a numeric `n` field, else the 5.1 border. (insert/remove/sort keep 5.1 behaviour) |
| `LUA_NUMBER_FLOAT32` | the game uses single-precision numbers | `-DLUA_NUMBER_FLOAT32`: `lua_Number` is `float`, printed with `%.7g` |

Kept from 5.1: `%`, `select`, per-type metatables and the string metatable (strings have methods),
`LUA_COMPAT_*` as configured in `luaconf.h` (implicit `arg` in `(...)` functions, no `arg` in the main chunk,
`f\n(x)` is a syntax error), 5.1 bytecode with extra opcodes (not compatible with stock 5.1 bytecode).

## Port/adapter layout (branch `port-adapter-layout`)

The dialect is expressed as two ports with one adapter per core, so the Lua cores only carry small hook calls and
X-macro lists (the 5.1 core differs from stock 5.1.5 by about 120 lines in `src/`):

    lua5_0-api/keywords/scfa_keywords.h   syntax port: token/operator/opcode lists (X-macros), priorities, hook contracts
    lua5_0-api/types/scfa_types.h         types port: number representation (-DLUA_NUMBER_FLOAT32), u32 bit ops
    lua-5_0-adapters/{keywords,types}/    the lua-lang 5.0 changes behind the ports   (core: lua-5.0/)
    lua-5_1-adapters/{keywords,types}/    the 5.1 work behind the same ports          (core: src/)

An adapter provides `scfa_keywords_impl.h` / `scfa_types_impl.h` (same names in both adapter dirs); the build picks the
adapter with `-I`: `build-win32.sh` (5.1) and `build-win32-50.sh` (5.0). The adapter headers are included by the core
files that call the hooks, each with an `SCFA_IN_<FILE>` macro, so hooks can be `static`. 5.1 only: the Lua 5.0 behaviours
kept on the 5.1 VM (`for k,v in table do`, `table.setn/getn`), the optional table functions, BOM in `luaL_loadfile`.
5.0 only (as in lua-lang): `luaL_loadfile` replacement that skips BOM and `#` line, hex literals in the lexer,
per-type metatables backported from 5.1 so strings get methods.

## Lua 5.0 perf switches (branch `lua-5.0-perf`)

`lua-5.0/src/lvm.c` has two compile switches for the in-place speed-up of the 5.0 VM (base repo story #284). With
neither defined the file compiles to the same object code as before (checked with objdump, x87 and SSE flags).

| switch | what |
|---|---|
| `SCFA_VM_FASTPATH` | `OP_GETTABLE` / `OP_SELF` / `OP_GETGLOBAL` with a string key on a table: inlined `luaH_getstr`, and a miss walks up to 8 levels of `__index` tables inline. An `__index` function, a deeper chain or a non-table operand takes the stock path from the start (lookups have no side effects, so results, errors and the `loop in gettable` limit are unchanged). |
| `SCFA_VM_JUMPTABLE` | computed-goto dispatch (gcc labels as values); each handler fetches and dispatches the next instruction. Unknown opcodes are ignored, as by the stock switch. |

The base repo builds them as harness runtimes `50faffloat-perf` (SSE float flags, the game's msvcr80 CRT math),
`50faffloat-perf-fast` (+ fast path) and `50faffloat-perf-goto` (+ computed goto), checked against the 50faffloat goldens.

## Not ported / known gaps

- `__band`, `__bor`, `__bshl`, `__bshr` metamethods (the exe names them; lua-lang does not implement them either).
- GPG removed `__gc` for tables (5.1 keeps it for userdata only, as stock 5.1).
- Engine behaviour (C++ exceptions instead of longjmp, LuaPlus object API) is out of scope.
- `proto`/`upval` type names exist as in stock 5.1.

## Build (32-bit, Windows, msys2 mingw32)

    ./build-win32.sh build                              # build/lua.exe, build/luac.exe (double numbers)
    ./build-win32.sh buildf -DLUA_NUMBER_FLOAT32        # float32 numbers

`build-win32.sh` puts `C:\msys64\mingw32\bin` on PATH. Any C compiler works: compile `src/*.c` except
`luac.c`/`print.c` (those two only for `luac`) with the files listed in the script. `lscfa_tablefuncs.c` is
part of the library.

## Tests

    build/lua.exe scfa-test/scfa.lua

Plain assertions (`PASS`/`FAIL`, non-zero exit on failure) covering every feature above and the stock-5.1
behaviour that is kept. The tests were written before the implementation (they fail on stock 5.1.5).

## Differences from stock 5.1.5

`src/llex.[ch]`, `lparser.[ch]`, `lcode.[ch]`, `lopcodes.[ch]`, `lvm.c`, `ltm.h`, `lauxlib.c`, `lstrlib.c`,
`ltablib.c`, `lualib.h`, `luaconf.h`, `print.c`, new `lscfa_tablefuncs.c`. `git diff d3e144e` shows all of it.
