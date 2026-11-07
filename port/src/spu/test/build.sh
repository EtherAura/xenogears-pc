#!/bin/sh
# Builds build/pc/spu-test/spu_test: the SPU core (port/src/spu, without the
# SDL output) and the XA decoder (port/src/cd/xa.c), with the unit tests.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
OUT="$ROOT/build/pc/spu-test"
mkdir -p "$OUT"
CC=${CC:-cc}
DEFS="-DPLATFORM_PC -DVERSION_US"
$CC -g -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter $DEFS -I"$ROOT/port/src" -I"$ROOT/port/src/cd" -I"$ROOT/include" \
    "$ROOT/port/src/spu/core.c" "$ROOT/port/src/spu/voice.c" "$ROOT/port/src/spu/reverb.c" \
    "$ROOT/port/src/spu/tables.c" "$ROOT/port/src/spu/cdin.c" "$ROOT/port/src/cd/xa.c" \
    "$ROOT/port/src/spu/test/spu_test.c" -lm -o "$OUT/spu_test"
echo "$OUT/spu_test"
