#!/bin/sh
# 32-bit Windows build of the Lua 5.1 SCFA core with msys2 mingw32.
# Usage: ./build-win32.sh [outdir] [extra cflags...]   (add -DLUA_NUMBER_FLOAT32 for float32 numbers)
# Produces <outdir>/lua.exe and <outdir>/luac.exe. The dialect ports are found through -I:
# lua5_0-api/* (the port contracts) and lua-5_1-adapters/* (this core's adapters).
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
INC="-Ilua5_0-api/keywords -Ilua5_0-api/types -Ilua5_0-api/gpgapi -Ilua-5_1-adapters/keywords -Ilua-5_1-adapters/types -Ilua-5_1-adapters/gpgapi"
CORE="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"
LIB="lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit"
SRCS=""; for f in $CORE $LIB; do SRCS="$SRCS src/$f.c"; done
gcc -m32 -O2 -Wall -static-libgcc $INC "$@" -o "$OUT/lua.exe" $SRCS src/lua.c
gcc -m32 -O2 -Wall -static-libgcc $INC "$@" -o "$OUT/luac.exe" $SRCS src/luac.c src/print.c
