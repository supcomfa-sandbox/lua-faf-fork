/* Unit test: .toml provider map + middleware routing, no Lua runtime needed (stub originals). */
#include <stdio.h>
#include <string.h>
#include "lua_adapter.h"
#include "lua50_passthrough.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int tagA, tagB;
static lua_State *open_a(void) { return (lua_State *)&tagA; }
static lua_State *open_b(void) { return (lua_State *)&tagB; }
static int top_a(lua_State *L) { (void)L; return 11; }
static int top_b(lua_State *L) { (void)L; return 22; }

static LuaProvider *fac(const char *name, const char *rt, void *ctx, char *err, size_t n) {
  Lua50Funcs f; memset(&f, 0, sizeof f);
  (void)rt; (void)err; (void)n;
  if (ctx == &tagA) { f.lua_open = open_a; f.lua_gettop = top_a; }
  else              { f.lua_open = open_b; f.lua_gettop = top_b; }
  return lua50_passthrough_create(name, &f);
}

static LuaAdapter *mk(void) {
  LuaAdapter *a = lua_adapter_create();
  lua_adapter_register_impl(a, "impl_a", fac, &tagA);
  lua_adapter_register_impl(a, "impl_b", fac, &tagB);
  return a;
}

static const char *GOOD =
  "[providers]\n"
  "a = { impl = \"impl_a\" }\n"
  "b = { impl = \"impl_b\", runtime = \"x\" }\n"
  "[default]\nprovider = \"a\"\n"
  "[api]\nlua_open = \"b\"\n"
  "[script]\n"
  "\"/lua/sim/Projectile.lua:OnCollisionCheck\" = \"b\"\n"
  "\"/lua/sim/Unit.lua:*\" = \"b\"\n";

static void expect_fail(const char *toml, const char *needle) {
  char err[256] = "";
  LuaAdapter *a = mk();
  int rc = lua_adapter_load_toml(a, toml, err, sizeof err);
  if (rc == 0 || !strstr(err, needle)) { printf("FAIL expected error containing '%s', rc=%d err='%s'\n", needle, rc, err); fails++; }
  lua_adapter_destroy(a);
}

int main(void) {
  char err[256] = "";
  LuaAdapter *a = mk();
  CHECK(lua_adapter_load_toml(a, GOOD, err, sizeof err) == 0);
  if (err[0]) printf("err: %s\n", err);
  LuaProvider *pa = lua_adapter_provider_named(a, "a"), *pb = lua_adapter_provider_named(a, "b");
  CHECK(pa && pb && pa != pb);

  /* api-level */
  CHECK(lua_adapter_provider_for_api(a, "lua_open") == pb);
  CHECK(lua_adapter_provider_for_api(a, "lua_call") == pa);
  /* script-level: exact, chunk wildcard, normalisation, default */
  CHECK(lua_adapter_provider_for_script(a, "/lua/sim/Projectile.lua", "OnCollisionCheck") == pb);
  CHECK(lua_adapter_provider_for_script(a, "@\\LUA\\SIM\\projectile.lua", "OnCollisionCheck") == pb);
  CHECK(lua_adapter_provider_for_script(a, "/lua/sim/Projectile.lua", "OnCreate") == pa);
  CHECK(lua_adapter_provider_for_script(a, "/lua/sim/Unit.lua", "Anything") == pb);
  CHECK(lua_adapter_provider_for_script(a, "/lua/other.lua", "f") == pa);

  /* middleware dispatch: lua_open -> b (api override), then state-bound calls follow the owner */
  ILua5_0 *api = lua_adapter_api(a);
  lua_State *L = api->lua_open(api);
  CHECK(L == (lua_State *)&tagB);
  CHECK(api->lua_gettop(api, L) == 22);                       /* owner b, no [api] entry */
  CHECK(api->lua_gettop(api, (lua_State *)&tagA) == 11);      /* unknown state -> default a */
  CHECK(lua_adapter_calls(a, "lua_gettop") == 2);
  CHECK(lua_adapter_calls(a, "lua_open") == 1);
  CHECK(lua_adapter_calls(a, "") == 3);
  CHECK(lua50_passthrough_calls_of(pb, "lua_gettop") == 1);
  CHECK(lua50_passthrough_calls_of(pa, "lua_gettop") == 1);
  CHECK(lua50_passthrough_calls_of(pb, "lua_open") == 1);
  api->lua_pushnil(api, L);                                   /* NULL original: harmless no-op, still counted */
  CHECK(lua50_passthrough_calls_of(pb, "lua_pushnil") == 1);
  lua_adapter_destroy(a);

  expect_fail("[providers]\na = { impl = \"impl_a\" }\n", "default");
  expect_fail("[providers]\na = { impl = \"nope\" }\n[default]\nprovider = \"a\"\n", "nope");
  expect_fail("[providers]\na = { impl = \"impl_a\" }\n[default]\nprovider = \"zz\"\n", "zz");
  expect_fail("[providers]\na = { impl = \"impl_a\" }\n[default]\nprovider = \"a\"\n[api]\nlua_bogus = \"a\"\n", "lua_bogus");
  expect_fail("[providers]\na = { impl = \"impl_a\" }\n[default]\nprovider = \"a\"\n[script]\n\"x.lua:f\" = \"q\"\n", "q");
  expect_fail("this is = = not toml", "toml");

  printf(fails ? "test_adapter_map: %d FAILED\n" : "test_adapter_map: all passed\n", fails);
  return fails != 0;
}
