/* ilua50.h - ILua5_0: the Lua 5.0 C API the engine calls, as a table of function pointers.
 *
 * Every method takes the interface pointer first (like a C++ `this`), so one provider
 * implementation can have several independent instances (e.g. two passthrough providers
 * backed by different runtimes). Signatures after `self` are exactly the Lua 5.0 API.
 * The adapter in front of the engine detours each exe API entry and forwards it to the
 * provider chosen by the provider map (see lua_provider_map.h).
 *
 * Core set only. TODO: extend from out/lua-upgrade/seams/faf-exe-lua-seams.csv (census
 * of every Lua entry the FAF exe calls) when it exists; it was not present when this was written.
 */
#ifndef ILUA50_H
#define ILUA50_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_State lua_State;   /* opaque; belongs to the provider that created it */
/* The game exe's lua_Number is float32. -DILUA50_EXE_BUILD (the in-game hook) selects float; otherwise double (harness runtimes). ILUA50_NUMBER overrides both. */
#ifndef ILUA50_NUMBER
#ifdef ILUA50_EXE_BUILD
#define ILUA50_NUMBER float
#else
#define ILUA50_NUMBER double
#endif
#endif
typedef ILUA50_NUMBER lua_Number;

/* Lua 5.0 constants */
#define LUA_GLOBALSINDEX (-10001)
#define LUA_REGISTRYINDEX (-10000)
#define LUA_TNONE (-1)
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER 3
#define LUA_TSTRING 4
#define LUA_TTABLE 5
#define LUA_TFUNCTION 6
#define LUA_TUSERDATA 7
#define LUA_TTHREAD 8
#define LUA_MULTRET (-1)
#define LUA_YIELD 1

typedef struct ILua5_0 ILua5_0;

/* THE provider interface is ILua5_0 (there is no separate ILuaProvider): the seam detours, the
 * middleware (lua_adapter.h) and every implementation (passthrough, a 5.1 provider) all speak it.
 * The list below is the single source of truth: X(ret, name, (self, params), (args), (SELF, args), state-arg).
 * The state-arg is `L` for calls bound to a lua_State, `NULL` for calls that create one. */
#define ILUA50_FUNCS(X) \
  X(lua_State*, lua_open, (ILua5_0 *self), (), (SELF), NULL) \
  X(void, lua_close, (ILua5_0 *self, lua_State *L), (L), (SELF, L), L) \
  X(lua_State*, lua_newthread, (ILua5_0 *self, lua_State *L), (L), (SELF, L), L) \
  X(int, lua_gettop, (ILua5_0 *self, lua_State *L), (L), (SELF, L), L) \
  X(void, lua_settop, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_pushvalue, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_gettable, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_settable, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_rawget, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_rawset, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_pushnumber, (ILua5_0 *self, lua_State *L, lua_Number n), (L, n), (SELF, L, n), L) \
  X(void, lua_pushstring, (ILua5_0 *self, lua_State *L, const char *s), (L, s), (SELF, L, s), L) \
  X(void, lua_pushboolean, (ILua5_0 *self, lua_State *L, int b), (L, b), (SELF, L, b), L) \
  X(void, lua_pushnil, (ILua5_0 *self, lua_State *L), (L), (SELF, L), L) \
  X(lua_Number, lua_tonumber, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(const char*, lua_tostring, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(int, lua_toboolean, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(int, lua_type, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(int, lua_next, (ILua5_0 *self, lua_State *L, int idx), (L, idx), (SELF, L, idx), L) \
  X(void, lua_call, (ILua5_0 *self, lua_State *L, int nargs, int nresults), (L, nargs, nresults), (SELF, L, nargs, nresults), L) \
  X(int, lua_pcall, (ILua5_0 *self, lua_State *L, int nargs, int nresults, int errfunc), (L, nargs, nresults, errfunc), (SELF, L, nargs, nresults, errfunc), L) \
  X(int, lua_resume, (ILua5_0 *self, lua_State *L, int narg), (L, narg), (SELF, L, narg), L) \
  X(void, lua_setgcthreshold, (ILua5_0 *self, lua_State *L, int newthreshold), (L, newthreshold), (SELF, L, newthreshold), L)

/* The interface: one function pointer per entry, `self` first. */
struct ILua5_0 {
#define ILUA50_MEMBER(R, N, PARAMS, ARGS, SARGS, LP) R (*N) PARAMS;
  ILUA50_FUNCS(ILUA50_MEMBER)
#undef ILUA50_MEMBER
};

/* Plain Lua 5.0 signatures (no self): the "original function" table a host fills for the
 * passthrough provider (in-game: trampolines to the exe's entries). */
typedef struct Lua50Funcs {
  lua_State *(*lua_open)(void);
  void       (*lua_close)(lua_State *L);
  lua_State *(*lua_newthread)(lua_State *L);
  int        (*lua_gettop)(lua_State *L);
  void       (*lua_settop)(lua_State *L, int idx);
  void       (*lua_pushvalue)(lua_State *L, int idx);
  void       (*lua_gettable)(lua_State *L, int idx);
  void       (*lua_settable)(lua_State *L, int idx);
  void       (*lua_rawget)(lua_State *L, int idx);
  void       (*lua_rawset)(lua_State *L, int idx);
  void       (*lua_pushnumber)(lua_State *L, lua_Number n);
  void       (*lua_pushstring)(lua_State *L, const char *s);
  void       (*lua_pushboolean)(lua_State *L, int b);
  void       (*lua_pushnil)(lua_State *L);
  lua_Number (*lua_tonumber)(lua_State *L, int idx);
  const char*(*lua_tostring)(lua_State *L, int idx);
  int        (*lua_toboolean)(lua_State *L, int idx);
  int        (*lua_type)(lua_State *L, int idx);
  int        (*lua_next)(lua_State *L, int idx);
  void       (*lua_call)(lua_State *L, int nargs, int nresults);
  int        (*lua_pcall)(lua_State *L, int nargs, int nresults, int errfunc);
  int        (*lua_resume)(lua_State *L, int narg);
  void       (*lua_setgcthreshold)(lua_State *L, int newthreshold);
} Lua50Funcs;

/* Convenience macros in the 5.0 style, over the interface. */
#define ilua_getglobal(I, L, s)  ((I)->lua_pushstring(I, L, s), (I)->lua_gettable(I, L, LUA_GLOBALSINDEX))
#define ilua_pop(I, L, n)        ((I)->lua_settop(I, L, -(n) - 1))

/* A provider: a named implementation of ILua5_0. */
typedef struct LuaProvider LuaProvider;
struct LuaProvider {
  const char *name;
  ILua5_0 *api;
  int  (*init)(LuaProvider *self, void *host_ctx);   /* 0 = ok */
  void (*shutdown)(LuaProvider *self);
  void *priv;
};

#ifdef __cplusplus
}
#endif
#endif
