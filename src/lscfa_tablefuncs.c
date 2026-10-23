/*
** SCFA extra table functions: table.getsize, table.empty, table.unpack(t,i,j)
** and table.clone, as added by the Forged Alliance executable on top of the
** stock table library. Optional: enabled by LUA_SCFA_TABLEFUNCS (luaconf.h).
*/

#define lscfa_tablefuncs_c
#define LUA_CORE

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/* table.getsize(t): number of non-nil entries (array + hash); 0 for non-tables */
static int tf_getsize (lua_State *L) {
  int n = 0;
  if (lua_istable(L, 1)) {
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
      n++;
      lua_pop(L, 1);
    }
  }
  lua_pushinteger(L, n);
  return 1;
}


/* table.empty(t): true if t has no entries; true for non-tables */
static int tf_empty (lua_State *L) {
  if (lua_istable(L, 1)) {
    lua_pushnil(L);
    if (lua_next(L, 1) != 0) {
      lua_pop(L, 2);
      lua_pushboolean(L, 0);
      return 1;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}


/* table.unpack(t [, i [, j]]) */
static int tf_unpack (lua_State *L) {
  int i, e, n;
  luaL_checktype(L, 1, LUA_TTABLE);
  i = luaL_optint(L, 2, 1);
  e = luaL_opt(L, luaL_checkint, 3, luaL_getn(L, 1));
  if (i > e) return 0;
  n = e - i + 1;
  if (n <= 0 || !lua_checkstack(L, n))
    return luaL_error(L, "too many results to unpack");
  for (; i <= e; i++)
    lua_rawgeti(L, 1, i);
  return n;
}


/* Deep copy of the table at absolute index `src'; `seen' (absolute index)
   maps source tables to their copies so that cycles and shared sub-tables
   are preserved.  Pushes the copy.  Metatables are shared, not copied. */
static void clone_table (lua_State *L, int src, int seen) {
  int copy;
  lua_pushvalue(L, src);
  lua_rawget(L, seen);
  if (!lua_isnil(L, -1)) return;  /* already cloned */
  lua_pop(L, 1);
  lua_newtable(L);
  copy = lua_gettop(L);
  lua_pushvalue(L, src);
  lua_pushvalue(L, copy);
  lua_rawset(L, seen);
  lua_pushnil(L);
  while (lua_next(L, src) != 0) {  /* stack: key value */
    lua_pushvalue(L, -2);  /* key for the store */
    if (lua_istable(L, -2))
      clone_table(L, lua_gettop(L) - 1, seen);
    else
      lua_pushvalue(L, -2);
    lua_rawset(L, copy);  /* copy[key] = value (or clone) */
    lua_pop(L, 1);  /* keep the key for the next iteration */
  }
  if (lua_getmetatable(L, src))
    lua_setmetatable(L, copy);
}


/* table.clone(t): deep, cycle-safe copy; non-tables are returned unchanged */
static int tf_clone (lua_State *L) {
  if (!lua_istable(L, 1)) {
    lua_settop(L, 1);
    return 1;
  }
  lua_settop(L, 1);
  lua_newtable(L);  /* seen */
  clone_table(L, 1, 2);
  return 1;
}


static const luaL_Reg tablefuncs[] = {
  {"getsize", tf_getsize},
  {"empty", tf_empty},
  {"unpack", tf_unpack},
  {"clone", tf_clone},
  {NULL, NULL}
};


/* overlay the SCFA functions onto the table at the top of the stack */
LUALIB_API int luaopen_scfa_tablefuncs (lua_State *L) {
  luaL_register(L, LUA_TABLIBNAME, tablefuncs);
  return 1;
}
