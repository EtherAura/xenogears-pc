#!/usr/bin/env bash
# SDL3 for the Windows build (MinGW-w64, x86-64), from a local source tarball.
#
#   tools/build_sdl3_mingw.sh [SDL3-x.y.z.tar.gz]
#
# Default tarball: $SDL3_TARBALL, else tools/toolchain-src/SDL3-3.4.16.tar.gz (a copy
# kept with the project), else /var/cache/distfiles/SDL3-3.4.16.tar.gz
# (no download). Installs into tools/toolchain/mingw64 (gitignored):
#   bin/SDL3.dll, lib/libSDL3.dll.a, lib/cmake/SDL3, include/SDL3
#   vulkan/include/{vulkan,vk_video}: links to the system's Vulkan headers, so
#   the cross build sees them without the rest of /usr/include
# Scratch goes to tools/toolchain-src/sdl3-mingw. Uses the port's toolchain
# file (port/cmake/mingw-w64-x86_64.cmake). Runs again only when the tarball
# changes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_TARBALL="$ROOT/tools/toolchain-src/SDL3-3.4.16.tar.gz"
[ -f "$DEFAULT_TARBALL" ] || DEFAULT_TARBALL=/var/cache/distfiles/SDL3-3.4.16.tar.gz
TARBALL="${1:-${SDL3_TARBALL:-$DEFAULT_TARBALL}}"
PREFIX="$ROOT/tools/toolchain/mingw64"
WORK="$ROOT/tools/toolchain-src/sdl3-mingw"
TOOLCHAIN="$ROOT/port/cmake/mingw-w64-x86_64.cmake"
VULKAN_HEADERS="${VULKAN_HEADERS:-/usr/include}"
JOBS="$(nproc 2>/dev/null || echo 4)"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

[ -f "$TARBALL" ] || { echo "no SDL3 source tarball at $TARBALL" >&2; exit 1; }
command -v x86_64-w64-mingw32-gcc >/dev/null || { echo "x86_64-w64-mingw32-gcc not found" >&2; exit 1; }
[ -f "$VULKAN_HEADERS/vulkan/vulkan.h" ] || { echo "no Vulkan headers in $VULKAN_HEADERS/vulkan" >&2; exit 1; }

STAMP="$PREFIX/.stamp-sdl3"
WANT="$(basename "$TARBALL") $(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/bin/SDL3.dll" ]; then
    log "SDL3 for MinGW is up to date ($PREFIX)"
else
    log "Extracting $(basename "$TARBALL")"
    rm -rf "$WORK"
    mkdir -p "$WORK"
    tar -xzf "$TARBALL" -C "$WORK"
    SRC="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d -name 'SDL3-*' | head -1)"
    [ -n "$SRC" ] || { echo "no SDL3-* directory in $TARBALL" >&2; exit 1; }

    log "Configuring SDL3 for x86_64-w64-mingw32"
    cmake -S "$SRC" -B "$WORK/build" -G Ninja \
        --toolchain "$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_VULKAN=ON

    log "Building SDL3"
    cmake --build "$WORK/build" -j "$JOBS"
    rm -rf "$PREFIX/include/SDL3" "$PREFIX/lib/cmake/SDL3"
    cmake --install "$WORK/build" --strip
    echo "$WANT" > "$STAMP"
fi

# The Vulkan headers, alone in their own include directory.
mkdir -p "$PREFIX/vulkan/include"
for d in vulkan vk_video; do
    if [ -d "$VULKAN_HEADERS/$d" ]; then
        ln -sfn "$VULKAN_HEADERS/$d" "$PREFIX/vulkan/include/$d"
    fi
done

log "SDL3 for MinGW: $PREFIX ($(ls "$PREFIX/bin/SDL3.dll"))"
