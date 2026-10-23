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
