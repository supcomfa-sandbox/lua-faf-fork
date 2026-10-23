/*
** SCFA dialect port "gpgapi": the C API that GPG added to Lua 5.0.1 and that the engine
** (ForgedAlliance.exe) and FAF's binary patches call. Core independent contract; implemented by
** lua-5_1-adapters/gpgapi (the 5.1 core). The 5.0 core of this fork does not implement it: the
** exe's own 5.0 VM has these functions (see lua-5_0-adapters/gpgapi/README.md).
** Included at the end of lua.h (so LUA_API and lua_State exist), -I selects the adapter.
**
** Evidence (FAF ForgedAlliance.exe, tools/disasm.py / decomp.py ... faf, 2026-10-23):
**   lua_getglobaluserdata 0x924050   return L->l_G->scfa_globalud           (G+0x148, initial 0)
**   lua_setglobaluserdata 0x924060   G+0x148 = p
**   lua_setusergcfunction 0x924080   G+0x14c = f
**   lua_getstateuserdata  0x9240a0   return L+0x44                           (== LuaState::FromCState 0x90a510)
**   lua_setstateuserdata  0x9240b0   L+0x44 = p
**   lua_newthread         0x90c530/0x924300  sets the new thread's L+0x44 = 0: NOT inherited
**   lua_setdefaultmetatable 0x90ad00 pops the top; if it was a table G+0xd4+8*type = {tt,table}
**   luaH_new 0x927320 (new tables get metatable G+0x100 = default[5]); luaS_newudata 0x924a10
**   (new userdata get metatable G+0x118 = default[8]); luaT_gettmbyobj 0x928450 (other types use default[type])
**   lua_touserdata 0x90cbb0 (RRef *ret, L, idx): ret = {udata+0x10, *(udata+0xc)} for tt 8, else {0,0}
**   lua_newuserdata 0x90d7e0 (RRef *ret, L, RType *): udata+0xc = RType*, marked |= 2 if the type has a finalizer
**   GC hook: propagatemark 0x915750 case thread (tt 9): after traversing a thread, if G+0x14c, st[5] = thread, hook(st)
**            markroot 0x915bf0: same call at the end of root marking with the main thread
*/

#ifndef scfa_gpgapi_h
#define scfa_gpgapi_h

/* The engine's type numbers (lua_type() of the 5.1 core uses its own; see lua_gpgtype) */
#define SCFA_GPG_TNIL		0
#define SCFA_GPG_TBOOLEAN	1
#define SCFA_GPG_TLIGHTUSERDATA	2
#define SCFA_GPG_TNUMBER	3
#define SCFA_GPG_TSTRING	4
#define SCFA_GPG_TTABLE		5
#define SCFA_GPG_TCFUNCTION	6
#define SCFA_GPG_TFUNCTION	7
#define SCFA_GPG_TUSERDATA	8
#define SCFA_GPG_TTHREAD	9
#define SCFA_GPG_NUMDEFAULTMETA	12	/* slots G+0xd4 .. G+0x134 */

/* Mirror of the engine's RRef {void *d; RType *t}: a userdata's data and its C++ type */
typedef struct scfa_RRef {
  void *d;
  const void *t;	/* opaque gpg::RType* (owned by the engine) */
} scfa_RRef;

/* Per-global GC hook. Called (by the collector, during the mark phase) with the thread that was
** just traversed, and with the main thread after root marking. Inside it only lua_markvalue and
** non-allocating stack reads on `L' are allowed (no calls that run Lua or collect). */
typedef void (*lua_UserGCFunction) (lua_State *L);

LUA_API void  (lua_setglobaluserdata) (lua_State *L, void *p);
LUA_API void *(lua_getglobaluserdata) (lua_State *L);
LUA_API void  (lua_setstateuserdata) (lua_State *L, void *p);
LUA_API void *(lua_getstateuserdata) (lua_State *L);	/* NULL for a fresh thread (not inherited) */
LUA_API void  (lua_setusergcfunction) (lua_State *L, lua_UserGCFunction f);
LUA_API void  (lua_markvalue) (lua_State *L, int idx);	/* GC hook only: mark the value at idx */

/* pops the top value; if it is a table it becomes the default metatable of `gpgtype' (SCFA_GPG_T*):
** tables and userdata created afterwards start with it as their metatable, other types use it as
** their per-type metatable. Not a table: the value is only popped. */
LUA_API void  (lua_setdefaultmetatable) (lua_State *L, int gpgtype);

/* userdata with a C++ type slot (header word `rtype') */
LUA_API void *(lua_newuserdatat) (lua_State *L, size_t size, const void *rtype);
LUA_API const void *(lua_getuserdatatype) (lua_State *L, int idx);	/* NULL if not a typed userdata */
/* the engine's lua_touserdata signature: ret = {data, type}; {NULL, NULL} if not a full userdata.
** (The stock lua_touserdata(L, idx) keeps returning the data pointer for the libraries.) */
LUA_API void  (lua_touserdata_rref) (scfa_RRef *ret, lua_State *L, int idx);

/* engine type number of the value at idx (6 = C function, 7 = Lua function, 8 = userdata, 9 = thread; -1 none) */
LUA_API int   (lua_gpgtype) (lua_State *L, int idx);

#if defined(__cplusplus)
/* C++ build of the core (-DSCFA_CPP_ERRORS): an error with no protected call active is thrown as this
** exception (the exe throws C++ exceptions from luaD_throw); the stack is reset and the error object
** is the value on top of L. Protected calls (lua_pcall, coroutines) still return a status. */
struct scfa_LuaError {
  lua_State *L;
  int status;	/* LUA_ERRRUN, LUA_ERRMEM, ... */
};
#endif

#endif
