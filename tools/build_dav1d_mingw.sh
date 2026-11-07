#!/usr/bin/env bash
# dav1d for the Windows build (MinGW-w64, x86-64), from a local source
# tarball: the optional AV1 decoder of the movie packs' videos
# (port/src/gpu/av1video.c, CMake option XG_DAV1D). A static library, linked
# into xenogears.exe (no DLL beside it).
#
#   tools/build_dav1d_mingw.sh [dav1d-x.y.z.tar.xz]
#
# Default tarball: $DAV1D_TARBALL, else tools/toolchain-src/dav1d-1.5.4.tar.xz
# (a copy kept with the project), else /var/cache/distfiles/dav1d-1.5.4.tar.xz
# (no download). Exits 0 with a note when there is none: the Windows build then
# has no AV1 videos (PNG frames still work). Needs meson, ninja and nasm (its
# x86 assembly; without nasm it is built without, slower). Installs into
# tools/toolchain/mingw64 (gitignored): lib/libdav1d.a, include/dav1d.
# Scratch goes to tools/toolchain-src/dav1d-mingw. Runs again only when the
# tarball changes. Licence: port/third_party/dav1d/COPYING (BSD 2-clause),
# shipped as LICENSE-dav1d.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_TARBALL="$ROOT/tools/toolchain-src/dav1d-1.5.4.tar.xz"
[ -f "$DEFAULT_TARBALL" ] || DEFAULT_TARBALL=/var/cache/distfiles/dav1d-1.5.4.tar.xz
TARBALL="${1:-${DAV1D_TARBALL:-$DEFAULT_TARBALL}}"
PREFIX="$ROOT/tools/toolchain/mingw64"
WORK="$ROOT/tools/toolchain-src/dav1d-mingw"
TRIPLE=x86_64-w64-mingw32
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

if [ ! -f "$TARBALL" ]; then
    log "no dav1d source tarball at $TARBALL: the Windows build has no AV1 movie videos"
    exit 0
fi
command -v meson >/dev/null || { log "meson not found: the Windows build has no AV1 movie videos"; exit 0; }
command -v $TRIPLE-gcc >/dev/null || { echo "$TRIPLE-gcc not found" >&2; exit 1; }

STAMP="$PREFIX/.stamp-dav1d"
WANT="$(basename "$TARBALL") $(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/lib/libdav1d.a" ]; then
    log "dav1d for MinGW is up to date ($PREFIX)"
    exit 0
fi

log "Extracting $(basename "$TARBALL")"
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xJf "$TARBALL" -C "$WORK"
SRC="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d -name 'dav1d*' | head -1)"
[ -n "$SRC" ] || { echo "no dav1d* directory in $TARBALL" >&2; exit 1; }

ASM=true
command -v nasm >/dev/null || ASM=false
cat > "$WORK/cross.txt" <<EOF
[binaries]
c = '$TRIPLE-gcc'
ar = '$TRIPLE-ar'
strip = '$TRIPLE-strip'
windres = '$TRIPLE-windres'
nasm = 'nasm'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

log "Configuring dav1d for $TRIPLE (assembly: $ASM)"
meson setup "$WORK/build" "$SRC" --cross-file "$WORK/cross.txt" --prefix "$PREFIX" --libdir lib \
    --buildtype release --default-library static -Denable_tools=false -Denable_tests=false \
    -Denable_examples=false -Denable_asm=$ASM >/dev/null

log "Building dav1d"
ninja -C "$WORK/build" -j "$JOBS" >/dev/null
rm -rf "$PREFIX/include/dav1d"
ninja -C "$WORK/build" install >/dev/null
echo "$WANT" > "$STAMP"
rm -rf "$WORK"

log "dav1d for MinGW: $PREFIX ($(ls "$PREFIX"/lib/libdav1d.a))"
