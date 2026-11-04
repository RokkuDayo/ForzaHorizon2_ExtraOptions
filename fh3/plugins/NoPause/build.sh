#!/bin/sh
# Builds NoPause.dll with llvm-mingw (clang). Usage: sh build.sh [output.dll]
set -e
cd "$(dirname "$0")"
OUT=${1:-NoPause.dll}
clang --target=x86_64-w64-mingw32 -shared -O2 -Wall -Wextra -I ../.. NoPause.c -o "$OUT"
