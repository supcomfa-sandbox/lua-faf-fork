/*
** SCFA dialect port "types", adapter for the Lua 5.0 core (the changes of
** FAForever/lua-lang's lua-5.0 branch, moved behind the port): metatables for the
** basic types (so strings get methods), the bitwise operator opcodes and the
** string.lualex stub.
** Included by lstate.h, lstate.c, lgc.c, lapi.c, ltm.c, lvm.c and lstrlib.c, each
** with its SCFA_IN_<FILE> macro defined; only the section of the including file compiles.
** (The number representation is selected in lua.h from scfa_types.h.)
*/

#include "scfa_types.h"


/* ----------------------------------------------------------- lstate.h */
#if defined(SCFA_IN_LSTATE_H)

/* global_State member: metatables for the basic types */
#define SCFA_GLOBAL_FIELDS	struct Table *mt[NUM_TAGS];  /* metatables for basic types */

#define SCFA_TYPE_MT(L, t)	(G(L)->mt[t])
#define SCFA_GLOBAL_INIT(g)	{ int i_; for (i_=0; i_<NUM_TAGS; i_++) (g)->mt[i_] = NULL; }

#endif


/* -------------------------------------------------------------- lgc.c */
#if defined(SCFA_IN_LGC)

static void scfa_markmt (GCState *st) {
  int i;
  global_State *g = st->g;
  for (i=0; i<NUM_TAGS; i++)
    if (g->mt[i]) markvalue(st, g->mt[i]);
}

#endif


/* ------------------------------------------------------------- lvm.c */
#if defined(SCFA_IN_LVM)

/* bitwise operators: both operands must be numbers (no coercion, no metamethods) */
static void scfa_vm_bitop (lua_State *L, int op, StkId ra, const TObject *rb, const TObject *rc) {
  const char *sym;
  if (ttisnumber(rb) && ttisnumber(rc)) {
    lua_Number nb = nvalue(rb), nc = nvalue(rc);
    switch (op) {
      case OP_BAND: setnvalue(ra, SCFA_BAND(nb, nc)); break;
      case OP_BOR: setnvalue(ra, SCFA_BOR(nb, nc)); break;
      case OP_BSHL: setnvalue(ra, SCFA_BSHL(nb, nc)); break;
      default: setnvalue(ra, SCFA_BSHR(nb, nc)); break;
    }
    return;
  }
  switch (op) {
    case OP_BAND: sym = "&"; break;
    case OP_BOR: sym = "|"; break;
    case OP_BSHL: sym = "<<"; break;
    default: sym = ">>"; break;
  }
  luaG_runerror(L, "`%s' operands must be numbers", sym);
}

#endif


/* ---------------------------------------------------------- lstrlib.c */
#if defined(SCFA_IN_LSTRLIB)

/* string.lualex exists in the game but is a stub (as in lua-lang) */
static int scfa_lualex (lua_State *L) {
  (void)L;
  return 1;
}

#define SCFA_STRLIB_ENTRIES	{"lualex", scfa_lualex},

/* strings get the string library as their __index: a metatable for the string type */
static void scfa_string_metatable (lua_State *L) {
  lua_newtable(L);  /* create metatable for strings */
  lua_pushliteral(L, "__index");  /* dummy string, but also the metamethod */
  lua_pushvalue(L, -2);
  lua_setmetatable(L, -2);  /* set string metatable */
  lua_pushvalue(L, -3);  /* string library... */
  lua_settable(L, -3);  /* ...is the __index metamethod */
  lua_pop(L, 1);  /* pop metatable */
}

#endif
