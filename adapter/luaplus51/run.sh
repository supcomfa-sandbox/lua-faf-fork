#!/bin/sh
# Builds the 5.1 SCFA core as C++ (-DSCFA_CPP_ERRORS) plus adapter/luaplus51 and runs its tests.
# Usage: adapter/luaplus51/run.sh [outdir] [extra cflags...]     (run from the repo root; -DLUA_NUMBER_FLOAT32 for the game's numbers)
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build/luaplus51}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
INC="-Isrc -Ilua5_0-api/keywords -Ilua5_0-api/types -Ilua5_0-api/gpgapi -Ilua-5_1-adapters/keywords -Ilua-5_1-adapters/types -Ilua-5_1-adapters/gpgapi -Iadapter/luaplus51"
CORE="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"
LIB="lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit"
SRCS=""; for f in $CORE $LIB; do SRCS="$SRCS src/$f.c"; done
g++ -m32 -O2 -Wall -Wno-misleading-indentation -static -x c++ -DSCFA_CPP_ERRORS -DSCFA_LUA_IS_CPP $INC "$@" \
  -o "$OUT/test_luaplus51.exe" adapter/luaplus51/test/test_luaplus51.cpp adapter/luaplus51/luaplus51.cpp $SRCS
"$OUT/test_luaplus51.exe"
