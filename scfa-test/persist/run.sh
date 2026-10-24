#!/bin/sh
# Builds the 5.1 core + persist/scfa_persist.c + the persistence tests and runs them:
#   1. C core, float32 numbers (the game's configuration)
#   2. C++ core (errors as C++ exceptions, -DSCFA_CPP_ERRORS), float32
#   3. C core, double numbers (the library does not depend on the number type)
# Usage: scfa-test/persist/run.sh [outdir] [extra cflags...]     (run from the repo root)
set -e
export PATH=/c/msys64/mingw32/bin:$PATH
OUT=${1:-build/persist}; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
INC="-Isrc -Ipersist -Ilua5_0-api/keywords -Ilua5_0-api/types -Ilua5_0-api/gpgapi -Ilua-5_1-adapters/keywords -Ilua-5_1-adapters/types -Ilua-5_1-adapters/gpgapi"
CORE="lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"
LIB="lauxlib lbaselib ldblib liolib lmathlib loslib ltablib lstrlib loadlib linit"
SRCS=""; for f in $CORE $LIB; do SRCS="$SRCS src/$f.c"; done
SRCS="$SRCS persist/scfa_persist.c"
T=scfa-test/persist/test_persist.c
echo "== C core, float32"
gcc -m32 -O2 -Wall -static-libgcc $INC -DLUA_NUMBER_FLOAT32 "$@" -o "$OUT/test_persist_c.exe" $T $SRCS
"$OUT/test_persist_c.exe"
echo "== C++ core, float32"
g++ -m32 -O2 -Wall -static -x c++ -DSCFA_CPP_ERRORS $INC -DLUA_NUMBER_FLOAT32 "$@" -o "$OUT/test_persist_cpp.exe" $T $SRCS
"$OUT/test_persist_cpp.exe"
echo "== C core, double"
gcc -m32 -O2 -Wall -static-libgcc $INC "$@" -o "$OUT/test_persist_double.exe" $T $SRCS
"$OUT/test_persist_double.exe"
