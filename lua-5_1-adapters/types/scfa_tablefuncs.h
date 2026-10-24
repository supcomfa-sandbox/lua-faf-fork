/*
** SCFA extra table functions: table.getsize, table.empty, table.unpack(t,i,j)
** and table.clone, as added by the Forged Alliance executable on top of the
** stock table library, plus the Lua 5.0 table.getn/setn (field n). Included by
** ltablib.c (SCFA dialect port "types", 5.1 adapter) and overlaid on the table
** library by scfa_open_tablefuncs.
*/


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


/* Lua 5.0 table functions that depend on getn/setn, ported from the 5.0 table library (identical to the engine's:
   exe table.remove 0x927d30, concat 0x927dd0, getn 0x927c20 ...). luaL_getn/luaL_setn are the 5.0.2 ones (lauxlib.c, LUA_COMPAT_GETN):
   field n, then the weak `sizes' table, then a linear count from 1. They replace the 5.1 versions, which use the 5.1 border. */
#define scfa_aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))

static int scfa_getn (lua_State *L) {
  lua_pushnumber(L, (lua_Number)scfa_aux_getn(L, 1));
  return 1;
}


static int scfa_setn (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_setn(L, 1, luaL_checkint(L, 2));
  return 0;
}


static int scfa_foreachi (lua_State *L) {
  int i;
  int n = scfa_aux_getn(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  for (i=1; i<=n; i++) {
    lua_pushvalue(L, 2);  /* function */
    lua_pushnumber(L, (lua_Number)i);  /* 1st argument */
    lua_rawgeti(L, 1, i);  /* 2nd argument */
    lua_call(L, 2, 1);
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 1);  /* remove nil result */
  }
  return 0;
}


static int scfa_tinsert (lua_State *L) {
  int v = lua_gettop(L);  /* number of arguments */
  int n = scfa_aux_getn(L, 1) + 1;
  int pos;  /* where to insert new element */
  if (v == 2)  /* called with only 2 arguments */
    pos = n;  /* insert new element at the end */
  else {
    pos = luaL_checkint(L, 2);  /* 2nd argument is the position */
    if (pos > n) n = pos;  /* `grow' array if necessary */
    v = 3;  /* function may be called with more than 3 args */
  }
  luaL_setn(L, 1, n);  /* new size */
  while (--n >= pos) {  /* move up elements */
    lua_rawgeti(L, 1, n);
    lua_rawseti(L, 1, n+1);  /* t[n+1] = t[n] */
  }
  lua_pushvalue(L, v);
  lua_rawseti(L, 1, pos);  /* t[pos] = v */
  return 0;
}


static int scfa_tremove (lua_State *L) {
  int n = scfa_aux_getn(L, 1);
  int pos = luaL_optint(L, 2, n);
  if (n <= 0) return 0;  /* table is `empty' */
  luaL_setn(L, 1, n-1);  /* t.n = n-1 */
  lua_rawgeti(L, 1, pos);  /* result = t[pos] */
  for ( ;pos<n; pos++) {
    lua_rawgeti(L, 1, pos+1);
    lua_rawseti(L, 1, pos);  /* t[pos] = t[pos+1] */
  }
  lua_pushnil(L);
  lua_rawseti(L, 1, n);  /* t[n] = nil */
  return 1;
}


static int scfa_concat (lua_State *L) {
  luaL_Buffer b;
  size_t lsep;
  const char *sep = luaL_optlstring(L, 2, "", &lsep);
  int i = luaL_optint(L, 3, 1);
  int n = luaL_optint(L, 4, 0);
  luaL_checktype(L, 1, LUA_TTABLE);
  if (n == 0) n = luaL_getn(L, 1);
  luaL_buffinit(L, &b);
  for (; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    luaL_argcheck(L, lua_isstring(L, -1), 1, "table contains non-strings");
    luaL_addvalue(&b);
    if (i != n)
      luaL_addlstring(&b, sep, lsep);
  }
  luaL_pushresult(&b);
  return 1;
}


static const luaL_Reg tablefuncs[] = {
  {"getsize", tf_getsize},
  {"empty", tf_empty},
  {"unpack", tf_unpack},
  {"clone", tf_clone},
  {"getn", scfa_getn},
  {"setn", scfa_setn},
  {"foreachi", scfa_foreachi},
  {"insert", scfa_tinsert},
  {"remove", scfa_tremove},
  {"concat", scfa_concat},
  {NULL, NULL}
};


/* overlay the SCFA functions onto the table library */
static void scfa_open_tablefuncs (lua_State *L) {
  luaL_register(L, LUA_TABLIBNAME, tablefuncs);
  lua_pop(L, 1);
}
