#ifndef RT_SHIM_H
#define RT_SHIM_H
#include "lua_adapter.h"
/* LuaImplFactory over a harness runtime DLL. ctx = default runtime variant ("50", "51float", ...) used
 * when the toml gives no `runtime`. DLL dir: env LUA_UPGRADE_BIN (default: base repo out/lua-upgrade/bin). */
LuaProvider *rt_shim_factory(const char *name, const char *runtime, void *ctx, char *err, size_t errlen);
int rt_shim_dostring(lua_State *L, const char *code);   /* setup only: bypasses the adapter; 0 ok */
const char *rt_shim_variant(lua_State *L);              /* rt_variant() of the runtime that owns L */
#endif
