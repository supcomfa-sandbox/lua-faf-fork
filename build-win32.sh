#!/bin/sh
# 32-bit Windows build with msys2 mingw32. Usage: ./build-win32.sh [outdir] [extra cflags...]
# Produces <outdir>/lua.exe, <outdir>/luac.exe. Add -DLUA_NUMBER_FLOAT32 for 32-bit float numbers.
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
CORE="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"
LIB="lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit lscfa_tablefuncs"
SRCS=""; for f in $CORE $LIB; do SRCS="$SRCS src/$f.c"; done
gcc -m32 -O2 -Wall -static-libgcc "$@" -o "$OUT/lua.exe" $SRCS src/lua.c
gcc -m32 -O2 -Wall -static-libgcc "$@" -o "$OUT/luac.exe" $SRCS src/luac.c src/print.c
