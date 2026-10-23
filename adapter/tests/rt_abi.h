/* rt_abi.h - the adapter ABI shared by every Lua runtime DLL (lua50_rt, lua51_rt, ...).
 *
 * The host (lua_host.exe) loads one DLL with LoadLibrary and resolves exactly the
 * functions listed in RT_FUNCS with GetProcAddress, so DLLs are swappable with no
 * recompile of the tests. The shape mirrors what the engine does through LuaPlus:
 * create a state, load chunks, push a global function + args, pcall, read results,
 * register C callbacks, create userdata with a metatable backed by C callbacks, and
 * drive coroutines (sim threads) from C.
 *
 * Conventions: cdecl, stack indices as in the Lua C API (1-based, negative from top).
 * Status codes are normalised: RT_OK / RT_ERR*. Callback return value: n >= 0 = number
 * of results left on top of the stack; RT_YIELD(n) = yield n values from the top;
 * RT_RAISE = raise the error value on top of the stack.
 */
#ifndef RT_ABI_H
#define RT_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

#define RT_ABI_VERSION 1

enum { RT_OK = 0, RT_ERRRUN = 1, RT_ERRSYNTAX = 2, RT_ERRMEM = 3, RT_ERRERR = 4, RT_ERRFILE = 5 };
enum { RT_YIELDED = 100 }; /* rt_resume result: coroutine yielded */
enum { LT_NIL = 0, LT_BOOL, LT_NUMBER, LT_STRING, LT_TABLE, LT_FUNCTION, LT_USERDATA, LT_THREAD, LT_LIGHTUD, LT_NONE = -1 };
enum { RT_RAISE = -100000 };
#define RT_YIELD(n) (-1 - (n))

typedef void *RtState;
typedef int (*RtCallback)(RtState S, void *ud);

/* X(return type, name, (params)) */
#define RT_FUNCS(X) \
  X(int,         rt_abi_version, (void)) \
  X(const char*, rt_version,     (void))  /* e.g. "Lua 5.0.3" */ \
  X(const char*, rt_variant,     (void))  /* "50", "51", "51compat" */ \
  X(RtState,     rt_new_state,   (void))  /* state with base/table/string/math/io/debug libs */ \
  X(void,        rt_close,       (RtState S)) \
  X(const char*, rt_last_error,  (RtState S)) \
  X(int,         rt_dostring,    (RtState S, const char *code, const char *chunkname)) /* results stay on stack */ \
  X(int,         rt_dofile,      (RtState S, const char *path)) \
  X(int,         rt_gettop,      (RtState S)) \
  X(void,        rt_settop,      (RtState S, int idx)) \
  X(int,         rt_type,        (RtState S, int idx)) \
  X(void,        rt_push_nil,    (RtState S)) \
  X(void,        rt_push_bool,   (RtState S, int b)) \
  X(void,        rt_push_number, (RtState S, double n)) \
  X(void,        rt_push_string, (RtState S, const char *s)) \
  X(void,        rt_push_value,  (RtState S, int idx)) \
  X(void,        rt_push_table,  (RtState S)) \
  X(void,        rt_push_callback, (RtState S, RtCallback cb, void *ud)) \
  X(double,      rt_to_number,   (RtState S, int idx)) \
  X(const char*, rt_to_string,   (RtState S, int idx)) \
  X(int,         rt_to_bool,     (RtState S, int idx)) \
  X(void*,       rt_to_pointer,  (RtState S, int idx)) \
  X(void,        rt_getglobal,   (RtState S, const char *name)) \
  X(void,        rt_setglobal,   (RtState S, const char *name))  /* pops */ \
  X(void,        rt_getfield,    (RtState S, int idx, const char *key)) \
  X(void,        rt_setfield,    (RtState S, int idx, const char *key)) /* pops */ \
  X(void,        rt_geti,        (RtState S, int idx, int n)) \
  X(void,        rt_seti,        (RtState S, int idx, int n)) /* pops */ \
  X(int,         rt_len,         (RtState S, int idx))  /* table.getn semantics */ \
  X(int,         rt_next,        (RtState S, int idx))  /* lua_next: pops key, pushes key,value */ \
  X(int,         rt_call,        (RtState S, int nargs, int nres)) /* protected; error text in rt_last_error */ \
  X(void,        rt_register,    (RtState S, const char *name, RtCallback cb, void *ud)) \
  X(void,        rt_class_register, (RtState S, const char *cls, RtCallback index, RtCallback newindex, RtCallback gc, void *ud)) \
  X(void*,       rt_new_userdata, (RtState S, int size, const char *cls)) /* pushes it */ \
  X(RtState,     rt_new_thread,  (RtState S))  /* anchored coroutine; fn + args pushed on it */ \
  X(int,         rt_resume,      (RtState T, int nargs)) /* 0 finished, RT_YIELDED, or RT_ERR* */ \
  X(void,        rt_release_thread, (RtState T)) \
  X(void,        rt_collect,     (RtState S)) \
  X(int,         rt_gc_count_kb, (RtState S))

#ifdef _WIN32
#define RT_API __declspec(dllexport)
#else
#define RT_API
#endif

#ifdef __cplusplus
}
#endif
#endif
