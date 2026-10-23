/* lua_adapter.c - middleware: .toml provider map, per-call routing, call counting.
 * Talks to providers only through ILua5_0. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua_adapter.h"
#include "toml.h"

enum {
#define MW_IDX(R, N, P, A, S, LP) IDX_##N,
  ILUA50_FUNCS(MW_IDX)
#undef MW_IDX
  NFUNCS
};
static const char *const names[] = {
#define MW_NAME(R, N, P, A, S, LP) #N,
  ILUA50_FUNCS(MW_NAME)
#undef MW_NAME
};

#define MAXIMPL 16
#define MAXPROV 16
typedef struct { char *impl; LuaImplFactory f; void *ctx; } Impl;
typedef struct { char *chunk; char *func; LuaProvider *prov; } ScriptRoute;
typedef struct { lua_State *L; LuaProvider *prov; } Owner;

struct LuaAdapter {
  ILua5_0 api;                       /* first member: self casts back */
  Impl impls[MAXIMPL]; int nimpl;
  LuaProvider *provs[MAXPROV]; int nprov;
  LuaProvider *def;
  LuaProvider *apimap[NFUNCS];
  ScriptRoute *scripts; int nscripts;
  Owner *owners; int nowners, capowners;   /* linear, newest first; fine for a steel thread */
  unsigned long counts[NFUNCS];
};

/* ---- routing ---- */
static void remember_owner(LuaAdapter *a, lua_State *L, LuaProvider *p) {
  if (!L) return;
  if (a->nowners == a->capowners) {
    a->capowners = a->capowners ? a->capowners * 2 : 64;
    a->owners = (Owner *)realloc(a->owners, a->capowners * sizeof(Owner));
  }
  a->owners[a->nowners].L = L; a->owners[a->nowners].prov = p; a->nowners++;
}
static LuaProvider *owner_of(const LuaAdapter *a, lua_State *L) {
  for (int i = a->nowners - 1; i >= 0; i--) if (a->owners[i].L == L) return a->owners[i].prov;
  return NULL;
}
static void forget_owner(LuaAdapter *a, lua_State *L) {
  for (int i = a->nowners - 1; i >= 0; i--) if (a->owners[i].L == L) { a->owners[i] = a->owners[--a->nowners]; return; }
}
static LuaProvider *resolve(const LuaAdapter *a, int idx, lua_State *L) {
  if (a->apimap[idx]) return a->apimap[idx];
  if (L) { LuaProvider *o = owner_of(a, L); if (o) return o; }
  return a->def;
}

/* ---- the ILua5_0 face ---- */
#define SELF p->api
#define MW_FN(R, N, PARAMS, ARGS, SARGS, LP) \
  static R gen_##N PARAMS { \
    LuaAdapter *a = (LuaAdapter *)self; \
    LuaProvider *p = resolve(a, IDX_##N, LP); \
    a->counts[IDX_##N]++; \
    if (!p) return (R)0; \
    return p->api->N SARGS; \
  }
ILUA50_FUNCS(MW_FN)
#undef MW_FN

static lua_State *mw_open(ILua5_0 *self) {
  LuaAdapter *a = (LuaAdapter *)self;
  lua_State *L = gen_lua_open(self);
  remember_owner(a, L, resolve(a, IDX_lua_open, NULL));
  return L;
}
static lua_State *mw_newthread(ILua5_0 *self, lua_State *L) {
  LuaAdapter *a = (LuaAdapter *)self;
  LuaProvider *p = resolve(a, IDX_lua_newthread, L);
  lua_State *T = gen_lua_newthread(self, L);
  remember_owner(a, T, p);
  return T;
}
static void mw_close(ILua5_0 *self, lua_State *L) {
  LuaAdapter *a = (LuaAdapter *)self;
  gen_lua_close(self, L);
  forget_owner(a, L);
}

LuaAdapter *lua_adapter_create(void) {
  LuaAdapter *a = (LuaAdapter *)calloc(1, sizeof *a);
  if (!a) return NULL;
#define MW_SET(R, N, P, A, S, LP) a->api.N = gen_##N;
  ILUA50_FUNCS(MW_SET)
#undef MW_SET
  a->api.lua_open = mw_open; a->api.lua_newthread = mw_newthread; a->api.lua_close = mw_close;
  return a;
}

static void clear_loaded(LuaAdapter *a) {
  for (int i = 0; i < a->nprov; i++) if (a->provs[i]->shutdown) a->provs[i]->shutdown(a->provs[i]);
  a->nprov = 0; a->def = NULL;
  memset(a->apimap, 0, sizeof a->apimap);
  for (int i = 0; i < a->nscripts; i++) { free(a->scripts[i].chunk); free(a->scripts[i].func); }
  free(a->scripts); a->scripts = NULL; a->nscripts = 0;
  a->nowners = 0;
}

void lua_adapter_destroy(LuaAdapter *a) {
  if (!a) return;
  clear_loaded(a);
  for (int i = 0; i < a->nimpl; i++) free(a->impls[i].impl);
  free(a->owners);
  free(a);
}

int lua_adapter_register_impl(LuaAdapter *a, const char *impl, LuaImplFactory f, void *ctx) {
  if (a->nimpl >= MAXIMPL) return -1;
  a->impls[a->nimpl].impl = strdup(impl); a->impls[a->nimpl].f = f; a->impls[a->nimpl].ctx = ctx; a->nimpl++;
  return 0;
}

ILua5_0 *lua_adapter_api(LuaAdapter *a) { return &a->api; }

LuaProvider *lua_adapter_provider_named(const LuaAdapter *a, const char *name) {
  for (int i = 0; i < a->nprov; i++) if (!strcmp(a->provs[i]->name, name)) return a->provs[i];
  return NULL;
}

static int func_index(const char *f) {
  for (int i = 0; i < NFUNCS; i++) if (!strcmp(names[i], f)) return i;
  return -1;
}

LuaProvider *lua_adapter_provider_for_api(const LuaAdapter *a, const char *func) {
  int i = func_index(func);
  if (i < 0) return NULL;
  return a->apimap[i] ? a->apimap[i] : a->def;
}

unsigned long lua_adapter_calls(const LuaAdapter *a, const char *func) {
  if (!func || !*func) { unsigned long t = 0; for (int i = 0; i < NFUNCS; i++) t += a->counts[i]; return t; }
  int i = func_index(func);
  return i < 0 ? 0 : a->counts[i];
}

/* ---- script routing ---- */
static char *norm_chunk(const char *c) {
  if (*c == '@') c++;
  char *r = strdup(c);
  for (char *p = r; *p; p++) *p = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
  return r;
}

LuaProvider *lua_adapter_provider_for_script(const LuaAdapter *a, const char *chunkname, const char *function_name) {
  char *c = norm_chunk(chunkname);
  LuaProvider *wild = NULL, *exact = NULL;
  for (int i = 0; i < a->nscripts; i++) {
    if (strcmp(a->scripts[i].chunk, c)) continue;
    if (!strcmp(a->scripts[i].func, "*")) wild = a->scripts[i].prov;
    else if (function_name && !strcmp(a->scripts[i].func, function_name)) exact = a->scripts[i].prov;
  }
  free(c);
  return exact ? exact : wild ? wild : a->def;
}

/* ---- toml ---- */
#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

int lua_adapter_load_toml(LuaAdapter *a, const char *text, char *err, size_t errlen) {
  char perr[200] = "";
  char *copy = strdup(text);
  toml_table_t *root = toml_parse(copy, perr, sizeof perr);
  if (!root) { snprintf(err, errlen, "toml parse error: %s", perr); free(copy); return -1; }
  clear_loaded(a);

  toml_table_t *provs = toml_table_in(root, "providers");
  if (!provs) FAIL("missing [providers]");
  for (int i = 0; toml_key_in(provs, i); i++) {
    const char *pname = toml_key_in(provs, i);
    toml_table_t *pt = toml_table_in(provs, pname);
    if (!pt) FAIL("provider '%s' must be an inline table { impl = ... }", pname);
    toml_datum_t impl = toml_string_in(pt, "impl");
    if (!impl.ok) FAIL("provider '%s': missing impl", pname);
    toml_datum_t rt = toml_string_in(pt, "runtime");
    Impl *im = NULL;
    for (int k = 0; k < a->nimpl; k++) if (!strcmp(a->impls[k].impl, impl.u.s)) im = &a->impls[k];
    if (!im) { snprintf(err, errlen, "provider '%s': unknown impl '%s'", pname, impl.u.s); free(impl.u.s); if (rt.ok) free(rt.u.s); goto fail; }
    char ferr[200] = "factory failed";
    LuaProvider *p = a->nprov < MAXPROV ? im->f(pname, rt.ok ? rt.u.s : NULL, im->ctx, ferr, sizeof ferr) : NULL;
    free(impl.u.s); if (rt.ok) free(rt.u.s);
    if (!p) FAIL("provider '%s': %s", pname, ferr);
    if (p->init && p->init(p, NULL)) { if (p->shutdown) p->shutdown(p); FAIL("provider '%s': init failed", pname); }
    /* the map name is the toml key, whatever the impl called itself */
    a->provs[a->nprov++] = p;
    p->name = strdup(pname);   /* small leak per provider for the process lifetime; acceptable */
  }

  toml_table_t *def = toml_table_in(root, "default");
  toml_datum_t dn = def ? toml_string_in(def, "provider") : (toml_datum_t){0};
  if (!dn.ok) FAIL("missing [default] provider");
  a->def = lua_adapter_provider_named(a, dn.u.s);
  if (!a->def) { snprintf(err, errlen, "default provider '%s' is not in [providers]", dn.u.s); free(dn.u.s); goto fail; }
  free(dn.u.s);

  toml_table_t *api = toml_table_in(root, "api");
  for (int i = 0; api && toml_key_in(api, i); i++) {
    const char *fn = toml_key_in(api, i);
    int fi = func_index(fn);
    if (fi < 0) FAIL("[api] unknown ILua5_0 function '%s'", fn);
    toml_datum_t v = toml_string_in(api, fn);
    if (!v.ok) FAIL("[api] %s: expected a provider name", fn);
    LuaProvider *p = lua_adapter_provider_named(a, v.u.s);
    if (!p) { snprintf(err, errlen, "[api] %s: unknown provider '%s'", fn, v.u.s); free(v.u.s); goto fail; }
    free(v.u.s);
    a->apimap[fi] = p;
  }

  toml_table_t *sc = toml_table_in(root, "script");
  for (int i = 0; sc && toml_key_in(sc, i); i++) {
    const char *key = toml_key_in(sc, i);
    const char *colon = strrchr(key, ':');
    if (!colon) FAIL("[script] key '%s' must be \"chunk:function\"", key);
    toml_datum_t v = toml_string_in(sc, key);
    if (!v.ok) FAIL("[script] %s: expected a provider name", key);
    LuaProvider *p = lua_adapter_provider_named(a, v.u.s);
    if (!p) { snprintf(err, errlen, "[script] %s: unknown provider '%s'", key, v.u.s); free(v.u.s); goto fail; }
    free(v.u.s);
    a->scripts = (ScriptRoute *)realloc(a->scripts, (a->nscripts + 1) * sizeof(ScriptRoute));
    char *chunk = strndup(key, (size_t)(colon - key));
    a->scripts[a->nscripts].chunk = norm_chunk(chunk); free(chunk);
    a->scripts[a->nscripts].func = strdup(colon + 1);
    a->scripts[a->nscripts].prov = p;
    a->nscripts++;
  }
  toml_free(root); free(copy);
  return 0;
fail:
  toml_free(root); free(copy);
  clear_loaded(a);
  return -1;
}
