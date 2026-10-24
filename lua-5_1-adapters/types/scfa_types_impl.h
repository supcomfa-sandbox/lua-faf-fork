/*
** SCFA dialect port "types", adapter for the Lua 5.1 core.
** Included by lvm.c, ltablib.c and lstrlib.c, each with its SCFA_IN_<FILE> macro
** defined; only the section of the including file compiles.
** (The number representation is selected in luaconf.h from scfa_types.h.)
*/

#include "scfa_types.h"


/* --------------------------------------------------------------------- lvm.c */
#if defined(SCFA_IN_LVM)

/* bitwise operators: both operands must be numbers (no coercion, no metamethods) */
static void scfa_vm_bitop (lua_State *L, int op, StkId ra, const TValue *rb, const TValue *rc) {
  const char *sym = "?";
  unsigned int r = 0;
  if (ttisnumber(rb) && ttisnumber(rc)) {
    lua_Number nb = nvalue(rb), nc = nvalue(rc);
    switch (op) {
      case OP_BAND: r = SCFA_BAND(nb, nc); break;
      case OP_BOR: r = SCFA_BOR(nb, nc); break;
      case OP_BSHL: r = SCFA_BSHL(nb, nc); break;
      default: r = SCFA_BSHR(nb, nc); break;
    }
    setnvalue(ra, cast_num(r));
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


/* next, for `for k,v in table do' (Lua 5.0 style; only the Lua API is used) */
static int scfa_next (lua_State *L) {
  lua_settop(L, 2);
  if (lua_next(L, 1)) return 2;
  lua_pushnil(L);
  return 1;
}


/* The one scfa_next closure of a state (registry[lightuserdata &scfa_next_key]), shared by every `for k,v in table do' loop. One object, so a persistence
   library can name it as a permanent (scfa_pushnext): a coroutine suspended inside such a loop holds it on its stack. A fresh closure per loop could not be
   named (lua-faf-fork #213: "C function ... (0 upvalues) is not in the permanents table" saving a game). */
static int scfa_next_key;
static Closure *scfa_shared_next (lua_State *L, Table *env) {
  Table *reg = hvalue(registry(L));
  TValue k;
  const TValue *v;
  setpvalue(&k, &scfa_next_key);
  v = luaH_get(reg, &k);
  if (ttisfunction(v)) return clvalue(v);
  {
    Closure *ncl = luaF_newCclosure(L, 0, env);
    TValue *slot;
    ncl->c.f = scfa_next;
    slot = luaH_set(L, reg, &k);
    setclvalue(L, slot, ncl);
    luaC_barriert(L, reg, slot);
    return ncl;
  }
}

LUA_API void scfa_pushnext (lua_State *L) {
  lua_lock(L);
  setclvalue(L, L->top, scfa_shared_next(L, hvalue(gt(L))));
  incr_top(L);
  lua_unlock(L);
}

/* generic for whose iterator is a table (Lua 5.0): iterate it with next */
static void scfa_tfor_prep (lua_State *L, LClosure *cl, StkId ra) {
  if (ttistable(ra)) {
    Closure *ncl = scfa_shared_next(L, cl->env);
    setobjs2s(L, ra+1, ra);
    setclvalue(L, ra, ncl);
  }
}

#endif


/* ------------------------------------------------------------------- ltablib.c */
#if defined(SCFA_IN_LTABLIB)

#include "scfa_tablefuncs.h"

#endif


/* ------------------------------------------------------------------- lstrlib.c */
#if defined(SCFA_IN_LSTRLIB)

/* string.lualex exists in the game but is a stub here (as in lua-lang) */
static int scfa_lualex (lua_State *L) {
  (void)L;
  return 1;
}

static void scfa_open_stringfuncs (lua_State *L) {
  lua_pushcfunction(L, scfa_lualex);
  lua_setfield(L, -2, "lualex");
}

#endif
