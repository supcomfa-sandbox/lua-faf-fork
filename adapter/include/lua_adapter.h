/* lua_adapter.h - the middleware between the engine's Lua seams and the providers.
 *
 *   seam detours -> LuaAdapter (this: resolve target from the .toml map, count calls)
 *                -> ONE interface, ILua5_0 -> a provider (5.0 passthrough, or a 5.1 provider)
 *
 * The middleware implements ILua5_0 itself (lua_adapter_api) and only ever talks to providers
 * through ILua5_0, so it never knows which Lua is behind a provider. Which provider answers is
 * policy and lives in the .toml map:
 *
 *   [providers]
 *   lua50 = { impl = "lua50_passthrough" }
 *   lua51 = { impl = "lua51", runtime = "51faffloat" }   # runtime: opaque string for the impl factory
 *   [default]
 *   provider = "lua50"
 *   [api]                  # per ILua5_0 function (seam level), optional
 *   lua_open = "lua51"
 *   [script]               # per script function, optional
 *   "/lua/sim/Projectile.lua:OnCollisionCheck" = "lua51"
 *   "/lua/sim/Unit.lua:*" = "lua51"
 *
 * Resolution for an API call: [api].<fn>, else the provider that OWNS the lua_State (the one whose
 * lua_open/lua_newthread produced it; states cannot move between providers), else [default].
 * Script keys: exact `chunk:function`, then `chunk:*`, then default. Chunk compare is case-insensitive,
 * backslash == slash, leading '@' ignored. (Script-level routing needs a call context the C API does
 * not carry; the lookup is provided now, wiring it into dispatch is a later slice.)
 */
#ifndef LUA_ADAPTER_H
#define LUA_ADAPTER_H
#include <stddef.h>
#include "ilua50.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LuaAdapter LuaAdapter;

/* Builds a provider for `[providers].<name> = { impl = ..., runtime = ... }` (runtime may be NULL).
 * On failure returns NULL and fills err. The adapter calls provider->init then, at destroy, ->shutdown
 * (shutdown releases the provider). */
typedef LuaProvider *(*LuaImplFactory)(const char *provider_name, const char *runtime, void *ctx,
                                       char *err, size_t errlen);

LuaAdapter *lua_adapter_create(void);
void lua_adapter_destroy(LuaAdapter *a);
int  lua_adapter_register_impl(LuaAdapter *a, const char *impl, LuaImplFactory f, void *ctx); /* 0 ok */
/* Parse the map and instantiate providers. 0 ok, else -1 with err filled (adapter left empty). */
int  lua_adapter_load_toml(LuaAdapter *a, const char *toml_text, char *err, size_t errlen);

ILua5_0 *lua_adapter_api(LuaAdapter *a);               /* what the seam detours call */
LuaProvider *lua_adapter_provider_named(const LuaAdapter *a, const char *name);
LuaProvider *lua_adapter_provider_for_api(const LuaAdapter *a, const char *func);   /* [api] else default */
LuaProvider *lua_adapter_provider_for_script(const LuaAdapter *a, const char *chunkname, const char *function_name);
unsigned long lua_adapter_calls(const LuaAdapter *a, const char *func);   /* per ILua5_0 entry, "" = total */

#ifdef __cplusplus
}
#endif
#endif
