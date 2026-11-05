#!/bin/sh
# Builds GrooveMusicRadio.dll with llvm-mingw (clang). Usage: sh build.sh [output.dll]
set -e
cd "$(dirname "$0")"
OUT=${1:-GrooveMusicRadio.dll}
TMP=${TMPDIR:-/tmp}/grm-build
mkdir -p "$TMP"
# dr_mp3 / dr_flac / dr_wav: third-party code, built without our warnings.
clang --target=x86_64-w64-mingw32 -c -O2 -w -I third_party/dr_libs decoder.c -o "$TMP/decoder.o"
clang++ --target=x86_64-w64-mingw32 -std=c++20 -shared -O2 -Wall -Wextra -Wno-missing-field-initializers \
    -DUNICODE -D_UNICODE -I ../.. GrooveMusicRadio.cpp library.cpp reader.cpp live.cpp spotify.cpp apps.cpp capture.cpp "$TMP/decoder.o" \
    -static -lole32 -lshell32 -lpropsys -luuid -lwinhttp -lruntimeobject -lmmdevapi -o "$OUT"
