# gpgapi, adapter for the Lua 5.0 core: engine-only

The port `lua5_0-api/gpgapi` (the C API GPG added to Lua 5.0.1: `lua_get/setglobaluserdata`,
`lua_get/setstateuserdata`, `lua_setusergcfunction`, `lua_setdefaultmetatable`, the RType slot of userdata and
the `RRef` signatures of `lua_touserdata` / `lua_newuserdata`) is **not implemented by the 5.0 core in
`lua-5.0/`**: that core is the FAForever lua-lang imitation of the *syntax* changes only (checked with
`rg -i 'globaluserdata|stateuserdata|usergc|RType'` over `lua-5.0/`: no hit; it has the stock 5.0.2
`defaultmeta`, a single table).

They only matter for the engine-facing build. In the game the functions are the exe's own
(`ForgedAlliance.exe` 0x924050 .. 0x9240b0, 0x90ad00, 0x90cbb0, 0x90d7e0), so the 5.0 side needs nothing:
the adapter's passthrough provider calls the exe. The 5.1 core implements the port
(`lua-5_1-adapters/gpgapi`) so a 5.1 provider can stand in for them.

Nothing from this port is compiled into the 5.0 core; `build-win32-50.sh` does not add the port to its include path.
