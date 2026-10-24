/*
** scfa_persist: persistence of the Lua 5.1 universe (tables, closures, protos, upvalues, threads,
** typed userdata) for the SCFA 5.1 core. Derived from Pluto 2.4 (see persist/PROVENANCE.md and
** persist/upstream/); the file format is NOT Pluto's.
**
** Persisting:    stack = [perms, root]            -> perms, root unchanged (bytes go to the writer)
** Unpersisting:  stack = [perms]                  -> [perms, root]
** `perms' maps object -> key when persisting and key -> object when unpersisting (the same names;
** build the reverse with scfa_persist_reverse_perms). Objects found in perms are written as their
** key (any persistable value, normally a string) and never walked, so C functions and the
** engine-made tables must be listed there. A table in perms is written by identity only unless
** SCFA_PERSIST_PERMTABLE_CONTENT is set: then its metatable and fields are written too and merged
** into the existing table on load (the engine does this for the named tables, _G first of all).
*/
#ifndef scfa_persist_h
#define scfa_persist_h

#include <stddef.h>
#include "lua.h"

#define SCFA_PERSIST_VERSION		1

/* opts->flags */
#define SCFA_PERSIST_PERMTABLE_CONTENT	1	/* see above */
#define SCFA_PERSIST_ALLOW_LIGHTUD	2	/* light userdata is written/read as its pointer value */

/* byte sink / source handed to the userdata callbacks */
typedef struct scfa_persist_io scfa_persist_io;
/* both raise a Lua error (truncated stream, failing writer) */
LUA_API void scfa_persist_write (scfa_persist_io *io, const void *p, size_t n);
LUA_API void scfa_persist_read (scfa_persist_io *io, void *p, size_t n);
LUA_API lua_State *scfa_persist_state (scfa_persist_io *io);

typedef struct scfa_persist_opts {
  int flags;
  int max_depth;	/* nesting limit (0 = default 100000). Each level costs C stack: run on a big-stack thread */
  /* Userdata whose type slot (lua_getuserdatatype) is non-NULL are written by `save': write whatever
  ** `load' needs (normally the type name, then the payload). The metatable is written by the library.
  ** `save' returns 0, or non-zero for "cannot persist this one" (a Lua error is raised).
  ** `load' pushes the new userdata and returns 0. */
  int (*udata_save) (lua_State *L, int idx, const void *rtype, scfa_persist_io *io, void *ud);
  int (*udata_load) (lua_State *L, scfa_persist_io *io, void *ud);
  void *udata_ud;
} scfa_persist_opts;

/* Errors are Lua errors (lua_error); message on the stack top. */
LUA_API void scfa_persist (lua_State *L, lua_Writer writer, void *wud, const scfa_persist_opts *opts);

/* Protected: returns 0 or a lua_pcall status with the message on the stack top; the collector
** state is restored either way. The stream must have come from the same build (header check). */
LUA_API int scfa_unpersist (lua_State *L, lua_Reader reader, void *rud, const scfa_persist_opts *opts);

/* stack [perms] -> [perms, reversed]: reversed[perms[k]] = k  for every pair */
LUA_API void scfa_persist_reverse_perms (lua_State *L, int permsidx);

/* "scfa-persist v1 <lua_Number> <sizeof...>" : tag of the stream header (also checked on load) */
LUA_API const char *scfa_persist_build_tag (void);

#endif
