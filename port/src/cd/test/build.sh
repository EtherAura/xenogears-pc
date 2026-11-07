#!/bin/sh
# Builds build/pc/cdtest/cdtest: port/src/cd with the game's loader
# (src/main/filesystem.c) and nothing else of the port.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
OUT="$ROOT/build/pc/cdtest"
mkdir -p "$OUT/out"
CC=${CC:-cc}
GAME="-std=gnu89 -nostdinc -fno-builtin -funsigned-char -fwrapv -fno-strict-aliasing -fcommon -fno-pie -w"
DEFS="-DPLATFORM_PC -DVERSION_US -D_LANGUAGE_C"
SDL_CFLAGS=$(pkg-config --cflags sdl3 2>/dev/null || true)
SDL_LIBS=$(pkg-config --libs sdl3 2>/dev/null || echo -lSDL3)
$CC -c -g -O1 $GAME -I"$ROOT/include" $DEFS "$ROOT/src/main/filesystem.c" -o "$OUT/filesystem.o"
$CC -g -O1 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-pie -no-pie $DEFS -DXG_ROOT="\"$ROOT\"" \
    -I"$ROOT/port/src" -I"$ROOT/include" $SDL_CFLAGS \
    "$ROOT"/port/src/cd/*.c "$ROOT/port/src/cd/test/cdtest.c" "$ROOT/port/src/cd/test/stubs.c" "$ROOT/port/src/cd/test/hwtest.c" \
    "$ROOT/port/src/game/main/retailheap.c" "$ROOT/port/src/system/speedup.c" "$OUT/filesystem.o" $SDL_LIBS -lm -o "$OUT/cdtest"
echo "$OUT/cdtest"
