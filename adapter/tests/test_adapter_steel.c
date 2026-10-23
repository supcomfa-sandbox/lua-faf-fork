/* Steel thread: engine-shaped calls -> LuaAdapter (middleware) -> ILua5_0 -> provider -> harness runtime DLL.
 * usage: test_adapter_steel <map.toml> <expected rt_variant of the default provider>
 * The same test runs against lua50 and lua51 by swapping only the .toml. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_shim.h"
#include "lua50_passthrough.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(n + 1); b[fread(b, 1, n, f)] = 0; fclose(f); return b;
}
static char *script(const char *name) {
  const char *d = getenv("LUA_UPGRADE_SCRIPTS");
  char p[700];
  snprintf(p, sizeof p, "%s\\%s", d ? d : "C:\\Users\\admin\\Desktop\\supreme-commander-forged-alliance\\.worktrees\\issue-40-dev-platform-cli-runner-router\\lua-upgrade\\tests\\scripts", name);
  return slurp(p);
}

/* Engine-shaped: global function call with numeric args. */
static double call_add(ILua5_0 *I, lua_State *L, double a, double b) {
  ilua_getglobal(I, L, "add");
  I->lua_pushnumber(I, L, a); I->lua_pushnumber(I, L, b);
  I->lua_call(I, L, 2, 1);
  double r = I->lua_tonumber(I, L, -1);
  I->lua_settop(I, L, 0);
  return r;
}
/* Engine-shaped: proj:OnCollisionCheck(other) with globals S, O set by the case. */
static int call_collision(ILua5_0 *I, lua_State *L, int *ok) {
  ilua_getglobal(I, L, "Projectile");
  I->lua_pushstring(I, L, "OnCollisionCheck");
  I->lua_gettable(I, L, -2);                       /* Projectile, fn */
  ilua_getglobal(I, L, "S"); ilua_getglobal(I, L, "O");
  int st = I->lua_pcall(I, L, 2, 1, 0);
  *ok = (st == 0);
  int r = I->lua_toboolean(I, L, -1);
  I->lua_settop(I, L, 0);
  return r;
}

static const char *SETUP =
  "function IsAlly(a, b) return a == b end\n"
  "local function bp(h) return { CategoriesHash = h } end\n"
  "local tgt = { Army = 2, Blueprint = bp{ANTITORPEDO=true}, }\n"
  "local self_t = { Army = 1, Blueprint = bp{TORPEDO=true}, }\n"
  "tgt.OriginalTarget = self_t\n"
  "cases = {\n"
  "  { { Army = 1, Blueprint = bp{} }, { Army = 1, Blueprint = bp{} } },\n"          /* same army: false */
  "  { self_t, tgt },\n"                                                              /* torpedo vs antitorpedo target: true */
  "  { { Army = 1, Blueprint = bp{TACTICAL=true} }, { Army = 2, Blueprint = bp{} } },\n" /* false */
  "  { { Army = 1, Blueprint = bp{}, CollideFriendly = true }, { Army = 2, Blueprint = bp{} } },\n" /* not IsAlly -> true */
  "  { { Army = 1, Blueprint = bp{} }, { Army = 2, Blueprint = bp{} } },\n"           /* true */
  "}\n";
static const int EXPECT[] = { 0, 1, 0, 1, 1 };

int main(int argc, char **argv) {
  if (argc < 3) { printf("usage: %s map.toml expected_variant\n", argv[0]); return 2; }
  char err[300] = "";
  char *toml = slurp(argv[1]);
  if (!toml) { printf("cannot read %s\n", argv[1]); return 2; }
  LuaAdapter *a = lua_adapter_create();
  lua_adapter_register_impl(a, "lua50_passthrough", rt_shim_factory, "50");
  lua_adapter_register_impl(a, "lua51", rt_shim_factory, "51float");
  if (lua_adapter_load_toml(a, toml, err, sizeof err)) { printf("load_toml: %s\n", err); return 2; }
  ILua5_0 *I = lua_adapter_api(a);

  lua_State *L = I->lua_open(I);
  CHECK(L != NULL);
  CHECK(strcmp(rt_shim_variant(L), argv[2]) == 0);
  if (strcmp(rt_shim_variant(L), argv[2])) printf("  variant=%s expected=%s\n", rt_shim_variant(L), argv[2]);

  char *add = script("add.lua"), *col = script("collision.lua");
  CHECK(add && col);
  CHECK(rt_shim_dostring(L, add) == 0);
  CHECK(rt_shim_dostring(L, col) == 0);
  CHECK(rt_shim_dostring(L, SETUP) == 0);

  CHECK(call_add(I, L, 2, 40) == 42);
  CHECK(call_add(I, L, -1.5, 0.5) == -1);
  for (int i = 0; i < 5; i++) {
    char code[80]; snprintf(code, sizeof code, "S, O = cases[%d][1], cases[%d][2]", i + 1, i + 1);
    CHECK(rt_shim_dostring(L, code) == 0);
    int ok = 0, r = call_collision(I, L, &ok);
    CHECK(ok);
    if (r != EXPECT[i] || !ok) printf("  case %d: got %d expected %d\n", i + 1, r, EXPECT[i]);
    CHECK(r == EXPECT[i]);
  }

  /* a coroutine goes through the same provider (owner follows the parent state) */
  lua_State *T = I->lua_newthread(I, L);
  CHECK(T != NULL && strcmp(rt_shim_variant(T), argv[2]) == 0);

  /* middleware saw everything, and the default provider's passthrough counted it */
  CHECK(lua_adapter_calls(a, "lua_call") == 2);
  CHECK(lua_adapter_calls(a, "lua_pcall") == 5);
  LuaProvider *def = lua_adapter_provider_for_api(a, "lua_call");
  CHECK(def && lua50_passthrough_calls_of(def, "lua_call") == 2);
  printf("variant=%s calls=%lu pcall=%lu\n", rt_shim_variant(L), lua_adapter_calls(a, ""), lua_adapter_calls(a, "lua_pcall"));
  I->lua_close(I, L);
  lua_adapter_destroy(a);
  printf(fails ? "test_adapter_steel: %d FAILED\n" : "test_adapter_steel: all passed\n", fails);
  return fails != 0;
}
