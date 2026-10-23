/*
** SCFA dialect port "types": number representation, 32-bit bit operations and the
** optional library functions. Core independent; implemented/consumed by
** lua-5_0-adapters/types and lua-5_1-adapters/types (same file names, chosen by -I).
**
**  - Number representation: build with -DLUA_NUMBER_FLOAT32 and the core uses a 32-bit
**    float lua_Number printed with %.7g (the engine's single precision numbers).
**  - Bit operations: operands are converted to unsigned 32-bit (through 64-bit so negative
**    values wrap), shift counts are taken & 31, the result is converted back to a number.
**  - Optional libraries (scfa_open_libs): table.getsize/empty/unpack(t,i,j)/clone and the
**    string.lualex stub. 5.1 adapter only: Lua 5.0 behaviour kept on the 5.1 VM (for k,v in
**    table do, table.setn/getn with field n).
*/

#ifndef scfa_types_h
#define scfa_types_h

#if defined(LUA_NUMBER_FLOAT32)
#define SCFA_NUMBER_TYPE	float
#define SCFA_NUMBER_SCAN	"%f"
#define SCFA_NUMBER_FMT		"%.7g"
#define SCFA_STR2NUMBER(s,p)	((float)strtod((s), (p)))
#else
#define SCFA_NUMBER_TYPE	double
#define SCFA_NUMBER_SCAN	"%lf"
#define SCFA_NUMBER_FMT		"%.14g"
#define SCFA_STR2NUMBER(s,p)	strtod((s), (p))
#endif

#define SCFA_TOBITS(n)		((unsigned int)(long long)(n))
#define SCFA_BAND(a,b)		(SCFA_TOBITS(a) & SCFA_TOBITS(b))
#define SCFA_BOR(a,b)		(SCFA_TOBITS(a) | SCFA_TOBITS(b))
#define SCFA_BSHL(a,b)		(SCFA_TOBITS(a) << (SCFA_TOBITS(b) & 31))
#define SCFA_BSHR(a,b)		(SCFA_TOBITS(a) >> (SCFA_TOBITS(b) & 31))
#define SCFA_XOR(a,b)		(SCFA_TOBITS(a) ^ SCFA_TOBITS(b))

#endif
