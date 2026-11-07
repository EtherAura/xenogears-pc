#!/usr/bin/env bash
# Khronos glslang for the Windows build (MinGW-w64, x86-64), from a local
# source tarball: the optional shader-preset compiler (port/src/gpu/shaderchain.c,
# CMake option XG_GLSLANG). Static libraries, linked into xenogears.exe.
#
#   tools/build_glslang_mingw.sh [glslang-x.y.z.tar.gz]
#
# Default tarball: $GLSLANG_TARBALL, else tools/toolchain-src/glslang-1.4.357.0.tar.gz
# (a copy kept with the project), else /var/cache/distfiles/glslang-1.4.357.0.tar.gz
# (no download). Exits 0 with a note when there is none: the Windows build then
# has no shader presets. Installs into tools/toolchain/mingw64 (gitignored):
#   lib/libglslang*.a, lib/cmake/glslang, include/glslang
# GLSL front end and SPIR-V output only: no HLSL, no spirv-opt (SPIRV-Tools),
# no glslang binaries or tests. Scratch goes to tools/toolchain-src/glslang-mingw.
# Runs again only when the tarball changes. Licence: port/third_party/glslang/
# LICENSE.txt (glslang's LICENSE.txt), shipped as LICENSE-glslang.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_TARBALL="$ROOT/tools/toolchain-src/glslang-1.4.357.0.tar.gz"
[ -f "$DEFAULT_TARBALL" ] || DEFAULT_TARBALL=/var/cache/distfiles/glslang-1.4.357.0.tar.gz
TARBALL="${1:-${GLSLANG_TARBALL:-$DEFAULT_TARBALL}}"
PREFIX="$ROOT/tools/toolchain/mingw64"
WORK="$ROOT/tools/toolchain-src/glslang-mingw"
TOOLCHAIN="$ROOT/port/cmake/mingw-w64-x86_64.cmake"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

if [ ! -f "$TARBALL" ]; then
    log "no glslang source tarball at $TARBALL: the Windows build has no shader presets"
    exit 0
fi
command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "x86_64-w64-mingw32-g++ not found" >&2; exit 1; }

STAMP="$PREFIX/.stamp-glslang"
WANT="$(basename "$TARBALL") $(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/lib/cmake/glslang/glslang-config.cmake" ]; then
    log "glslang for MinGW is up to date ($PREFIX)"
    exit 0
fi

log "Extracting $(basename "$TARBALL")"
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xzf "$TARBALL" -C "$WORK"
SRC="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d -name 'glslang*' | head -1)"
[ -n "$SRC" ] || { echo "no glslang* directory in $TARBALL" >&2; exit 1; }

log "Configuring glslang for x86_64-w64-mingw32"
cmake -S "$SRC" -B "$WORK/build" -G Ninja \
    --toolchain "$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_EXTERNAL=OFF \
    -DENABLE_OPT=OFF \
    -DENABLE_HLSL=OFF \
    -DENABLE_GLSLANG_BINARIES=OFF \
    -DGLSLANG_TESTS=OFF \
    -DGLSLANG_ENABLE_INSTALL=ON \
    -DENABLE_PCH=OFF

log "Building glslang"
cmake --build "$WORK/build" -j "$JOBS"
rm -rf "$PREFIX/include/glslang" "$PREFIX/lib/cmake/glslang"
cmake --install "$WORK/build"
echo "$WANT" > "$STAMP"
rm -rf "$WORK"

log "glslang for MinGW: $PREFIX ($(ls "$PREFIX"/lib/libglslang.a))"
