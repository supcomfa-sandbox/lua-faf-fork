#!/bin/sh
# 32-bit Windows build of the Lua 5.0 SCFA core (lua-5.0/) with msys2 mingw32.
# Usage: ./build-win32-50.sh [outdir] [extra cflags...]   (-DLUA_NUMBER_FLOAT32 for float32 numbers)
# Produces <outdir>/lua50.exe. The dialect ports come through -I: lua5_0-api/* and lua-5_0-adapters/*.
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build50}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
INC="-Ilua-5.0/include -Ilua-5.0/src -Ilua5_0-api/keywords -Ilua5_0-api/types -Ilua-5_0-adapters/keywords -Ilua-5_0-adapters/types"
SRCS="$(ls lua-5.0/src/*.c lua-5.0/src/lib/*.c)"
gcc -m32 -O2 -w -std=gnu89 -static-libgcc $INC "$@" -o "$OUT/lua50.exe" $SRCS lua-5.0/src/lua/lua.c
