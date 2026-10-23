/* test_number.c - lua_Number width of the ILua5_0 interface.
 * The game exe's lua_Number is float32 (lua_tonumber returns float in st0, lua_pushnumber takes a 4-byte float on the stack), so the in-game build
 * (-DILUA50_EXE_BUILD) must make it float; the harness/5.1-double builds keep double. Built twice by the Makefile with the two expectations. */
#include <stdio.h>
#include "ilua50.h"

#ifndef EXPECT_NUMBER_SIZE
#error "EXPECT_NUMBER_SIZE (4 or 8) not defined"
#endif

int main(void)
{
    int bad = 0;
    if (sizeof(lua_Number) != EXPECT_NUMBER_SIZE) { printf("FAIL: sizeof(lua_Number)=%u, expected %u\n", (unsigned)sizeof(lua_Number), (unsigned)EXPECT_NUMBER_SIZE); bad = 1; }
    /* the Lua50Funcs/ILua5_0 slots must use the same type (a float lua_pushnumber slot called with a double would corrupt the stack) */
    ILua5_0 api;
    Lua50Funcs orig;
    if (sizeof(api.lua_pushnumber) != sizeof(void *) || sizeof(orig.lua_tonumber) != sizeof(void *)) bad = 1;
    lua_Number n = 1;
    if ((float)n != 1.0f) bad = 1;
    if (!bad) printf("lua_Number is %u bytes as expected\n", (unsigned)sizeof(lua_Number));
    return bad;
}
