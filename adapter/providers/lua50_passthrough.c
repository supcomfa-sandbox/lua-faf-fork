/* lua50_passthrough.c - ILua5_0 provider that forwards each call to a table of ORIGINAL 5.0 functions.
 * No Lua knowledge beyond the signatures; the host decides what the originals are. */
#include <stdlib.h>
#include <string.h>
#include "lua50_passthrough.h"

enum {
#define PT_IDX(R, N, P, A, S, LP) IDX_##N,
  ILUA50_FUNCS(PT_IDX)
#undef PT_IDX
  NFUNCS
};
static const char *const names[] = {
#define PT_NAME(R, N, P, A, S, LP) #N,
  ILUA50_FUNCS(PT_NAME)
#undef PT_NAME
};

typedef struct {
  ILua5_0 api;              /* first: self pointers cast back to Pt */
  LuaProvider prov;
  Lua50Funcs orig;
  char *name;
  unsigned long counts[NFUNCS];
} Pt;

#define PT_FN(R, N, PARAMS, ARGS, SARGS, LP) \
  static R pt_##N PARAMS { \
    Pt *pt = (Pt *)self; \
    pt->counts[IDX_##N]++; \
    if (!pt->orig.N) return (R)0; \
    return pt->orig.N ARGS; \
  }
ILUA50_FUNCS(PT_FN)
#undef PT_FN

static void pt_shutdown(LuaProvider *p) { lua50_passthrough_destroy(p); }

LuaProvider *lua50_passthrough_create(const char *name, const Lua50Funcs *orig) {
  Pt *pt = (Pt *)calloc(1, sizeof *pt);
  if (!pt) return NULL;
  pt->orig = *orig;
  pt->name = strdup(name ? name : "lua50_passthrough");
#define PT_SET(R, N, P, A, S, LP) pt->api.N = pt_##N;
  ILUA50_FUNCS(PT_SET)
#undef PT_SET
  pt->prov.name = pt->name;
  pt->prov.api = &pt->api;
  pt->prov.init = NULL;
  pt->prov.shutdown = pt_shutdown;
  pt->prov.priv = pt;
  return &pt->prov;
}

void lua50_passthrough_destroy(LuaProvider *p) {
  if (!p) return;
  Pt *pt = (Pt *)p->priv;
  free(pt->name);
  free(pt);
}

unsigned long lua50_passthrough_calls(const LuaProvider *p) {
  const Pt *pt = (const Pt *)p->priv; unsigned long t = 0;
  for (int i = 0; i < NFUNCS; i++) t += pt->counts[i];
  return t;
}

unsigned long lua50_passthrough_calls_of(const LuaProvider *p, const char *func) {
  const Pt *pt = (const Pt *)p->priv;
  for (int i = 0; i < NFUNCS; i++) if (!strcmp(names[i], func)) return pt->counts[i];
  return 0;
}
