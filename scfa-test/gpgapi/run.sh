#!/bin/sh
# Builds the 5.1 core (without lua.c/luac.c) and the GPG API tests, as C and as C++, and runs them.
# Usage: scfa-test/gpgapi/run.sh [outdir] [extra cflags...]     (run from the repo root)
# Step 3 (scfa-test/cpperrors) is the C++-exception test: same core compiled as C++.
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build/gpgapi}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
INC="-Isrc -Ilua5_0-api/keywords -Ilua5_0-api/types -Ilua5_0-api/gpgapi -Ilua-5_1-adapters/keywords -Ilua-5_1-adapters/types -Ilua-5_1-adapters/gpgapi"
CORE="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"
LIB="lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit"
SRCS=""; for f in $CORE $LIB; do SRCS="$SRCS src/$f.c"; done
# 1. C core + C test
gcc -m32 -O2 -Wall -static-libgcc $INC "$@" -o "$OUT/test_gpgapi_c.exe" scfa-test/gpgapi/test_gpgapi.c $SRCS
"$OUT/test_gpgapi_c.exe"
# 2. C++ core (errors are C++ exceptions) + the same test compiled as C++
g++ -m32 -O2 -Wall -static -x c++ -DSCFA_CPP_ERRORS $INC "$@" -o "$OUT/test_gpgapi_cpp.exe" scfa-test/gpgapi/test_gpgapi.c $SRCS
"$OUT/test_gpgapi_cpp.exe"
# 3. Lua errors as C++ exceptions (destructors run, unprotected errors throw scfa_LuaError)
g++ -m32 -O2 -Wall -static -x c++ -DSCFA_CPP_ERRORS $INC "$@" -o "$OUT/test_cpperrors.exe" scfa-test/cpperrors/test_cpperrors.cpp $SRCS
"$OUT/test_cpperrors.exe"
