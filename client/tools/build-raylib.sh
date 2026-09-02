#!/bin/sh
# Builds raylib as a static library for the window build. Run once; the
# result is cached in third_party/raylib-build/ (git-ignored) and both
# the Windows and the Linux window builds link against it.
#
#   sh tools/build-raylib.sh windows   -> libraylib.a for MinGW
#   sh tools/build-raylib.sh linux     -> libraylib.a for the host
#
# raylib is pinned to a release tag: a window that renders differently
# after an unrelated upstream commit is not a debugging session anyone
# wants.
set -e

RAYLIB_TAG=5.5
TARGET="${1:-linux}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/../third_party/raylib-build/$TARGET"
SRC="$ROOT/../third_party/raylib-src"

if [ ! -d "$SRC" ]; then
    git clone -q --depth 1 --branch "$RAYLIB_TAG" https://github.com/raysan5/raylib "$SRC"
fi

mkdir -p "$OUT"
cd "$SRC/src"
make clean >/dev/null 2>&1 || true

if [ "$TARGET" = "windows" ]; then
    make PLATFORM=PLATFORM_DESKTOP OS=Windows_NT \
        CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar \
        RAYLIB_LIBTYPE=STATIC -j4 >/dev/null
else
    make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC -j4 >/dev/null
fi

cp libraylib.a "$OUT/"
cp raylib.h "$OUT/"
echo "raylib $RAYLIB_TAG built for $TARGET in $OUT"
