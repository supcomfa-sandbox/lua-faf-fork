/* lua50_passthrough.h - provider that forwards every ILua5_0 call to the ORIGINAL 5.0 functions.
 * In-game the host fills `orig` with trampolines to the exe's 5.0 entries; in tests with shims
 * over a harness runtime DLL. The table is copied. A NULL entry makes that call a no-op returning 0. */
#ifndef LUA50_PASSTHROUGH_H
#define LUA50_PASSTHROUGH_H
#include "ilua50.h"

#ifdef __cplusplus
extern "C" {
#endif

LuaProvider *lua50_passthrough_create(const char *name, const Lua50Funcs *orig);
void lua50_passthrough_destroy(LuaProvider *p);
/* Calls forwarded through this instance: total, or per entry name (e.g. "lua_call"). */
unsigned long lua50_passthrough_calls(const LuaProvider *p);
unsigned long lua50_passthrough_calls_of(const LuaProvider *p, const char *func);

#ifdef __cplusplus
}
#endif
#endif
