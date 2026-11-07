#!/usr/bin/env bash
# Build PCSX-Redux (headless validation emulator, used by tools/redux.py)
# from pinned sources. No root: everything lands in tools/toolchain
# (redux-deps/, pcsx-redux/) and tools/toolchain-src (sources, build logs).
#
#   tools/build_redux.sh             # or: tools/setup.sh --redux
#   tools/build_redux.sh --mips-gcc  # also build a mipsel-none-elf GCC for OpenBIOS
#
# Host requirements beyond tools/setup.sh's: cmake, pkg-config, g++ with
# C++23, and development packages for freetype2, libavcodec, libavformat,
# libavutil, libswresample (FFmpeg 8 works), libcurl, libuv, sdl3, zlib,
# OpenGL/X11. Capstone is built here because distributions lag or lack it.
# GLFW is not needed: PCSX-Redux replaced it with SDL3 in May 2026 (fae2c450).
#
# OpenBIOS (PCSX-Redux's free BIOS replacement) needs a mipsel-none-elf GCC
# cross-compiler (binutils + gcc). It is built when one is on PATH (or
# MIPS_PREFIX points at one, e.g. MIPS_PREFIX=/opt/mips/bin/mipsel-none-elf),
# or with --mips-gcc, which first builds binutils 2.47 + gcc 16.2.0 into
# tools/toolchain/mipsel (the versions and hashes PCSX-Redux's own
# tools/macos-mips formulas pin; 55 + 107 MB of sources, needs gmp, mpfr, mpc
# headers). Otherwise this script says what is missing and finishes without
# OpenBIOS. Clang
# cannot stand in: LLVM's MIPS-I backend (experimental) aborts on OpenBIOS's
# memory fences ("Cannot select: MipsISD::Sync"), checked with clang 22.
#
# Every download is pinned: tarballs by sha256, git by commit.
set -euo pipefail

MIPS_GCC=0
for arg in "$@"; do
    case "$arg" in
        --mips-gcc) MIPS_GCC=1 ;;
        *) echo "usage: $0 [--mips-gcc]" >&2; exit 2 ;;
    esac
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE"
until [ -f "$ROOT/config/splat.common.yaml" ]; do
    [ "$ROOT" != / ] || { echo "cannot find the repository root" >&2; exit 1; }
    ROOT="$(dirname "$ROOT")"
done
TC="${REDUX_TC:-$ROOT/tools/toolchain}"
SRC="${REDUX_SRC:-$ROOT/tools/toolchain-src}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

CAPSTONE_VER=5.0.9
CAPSTONE_SHA256=1b70351879f6998998ebcbe09bd5f3c5e27127e985af14722cbe52c11c35178e

# mipsel-none-elf cross toolchain for OpenBIOS (--mips-gcc only).
MIPS_BINUTILS_VER=2.47
MIPS_BINUTILS_SHA256=15f5abd0db9cf8ea116f516a9ce12bc1cefef491ef9e50942868093d8f92a6f3   # .tar.gz
MIPS_GCC_VER=16.2.0
MIPS_GCC_SHA256=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e        # .tar.xz

# main ("Merge pull request #2207 from rixnobis/i18n-ctx").
REDUX_COMMIT=bf4c9cebdd1c649dd006736bb012fc7f1d6fd988

# Local patches, applied in name order: tools/patches/pcsx-redux-*.patch
# (tools/patches/).
REDUX_PATCHES=("$HERE"/patches/pcsx-redux-*.patch)

DEPS="$TC/redux-deps"
REDUX_SRC_DIR="$SRC/pcsx-redux"
REDUX_PREFIX="$TC/pcsx-redux"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { echo "build_redux: $*" >&2; exit 1; }

fetch() {
    local url=$1 dest=$2 sha=$3
    if [ ! -f "$dest" ]; then
        curl -fsSL --retry 3 -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
    echo "$sha  $dest" | sha256sum --check --quiet || { rm -f "$dest"; die "checksum mismatch for $dest"; }
}

stamp_ok() { [ -f "$TC/.stamp-$1" ] && [ "$(cat "$TC/.stamp-$1")" = "$2" ]; }
stamp_set() { echo "$2" > "$TC/.stamp-$1"; }

mkdir -p "$TC" "$SRC"

# --- host checks ----------------------------------------------------------------
for tool in cmake pkg-config git curl make g++ convert; do   # convert: make install renders the icon
    command -v "$tool" > /dev/null || die "missing host tool: $tool"
done
missing=()
for pkg in freetype2 libavcodec libavformat libavutil libswresample libcurl libuv sdl3 zlib gl x11; do
    pkg-config --exists "$pkg" || missing+=("$pkg")
done
[ ${#missing[@]} = 0 ] || die "missing development packages (pkg-config): ${missing[*]}"
GENERATOR=()
command -v ninja > /dev/null && GENERATOR=(-G Ninja)

# --- capstone (static, into redux-deps) -----------------------------------------
if ! stamp_ok redux-capstone "$CAPSTONE_VER-$CAPSTONE_SHA256"; then
    log "Building capstone $CAPSTONE_VER"
    tarball="$SRC/capstone-$CAPSTONE_VER.tar.xz"
    fetch "https://github.com/capstone-engine/capstone/releases/download/$CAPSTONE_VER/capstone-$CAPSTONE_VER.tar.xz" \
        "$tarball" "$CAPSTONE_SHA256"
    rm -rf "$SRC/capstone-$CAPSTONE_VER" "$SRC/capstone-build"
    tar -xf "$tarball" -C "$SRC"
    (
        cmake -S "$SRC/capstone-$CAPSTONE_VER" -B "$SRC/capstone-build" "${GENERATOR[@]}" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib \
            -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DCAPSTONE_BUILD_TESTS=OFF -DCAPSTONE_BUILD_CSTOOL=OFF
        cmake --build "$SRC/capstone-build" -j "$JOBS"
        cmake --install "$SRC/capstone-build"
    ) > "$SRC/capstone-build.log" 2>&1 || die "capstone build failed; see $SRC/capstone-build.log"
    stamp_set redux-capstone "$CAPSTONE_VER-$CAPSTONE_SHA256"
fi
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# --- PCSX-Redux -----------------------------------------------------------------
patch_hash="$(cat /dev/null "${REDUX_PATCHES[@]}" 2>/dev/null | sha256sum | cut -c1-16)"
REDUX_STAMP="$REDUX_COMMIT $patch_hash capstone-$CAPSTONE_VER"
if ! stamp_ok redux "$REDUX_STAMP" || [ ! -x "$REDUX_PREFIX/bin/pcsx-redux" ]; then
    log "Fetching PCSX-Redux $REDUX_COMMIT (+ submodules, ~2.4 GB)"
    if [ ! -d "$REDUX_SRC_DIR/.git" ]; then
        git clone --quiet https://github.com/grumpycoders/pcsx-redux.git "$REDUX_SRC_DIR"
    fi
    git -C "$REDUX_SRC_DIR" cat-file -e "$REDUX_COMMIT^{commit}" 2>/dev/null || git -C "$REDUX_SRC_DIR" fetch --quiet origin
    # -f drops a previous run's patches; build objects stay, so rebuilds are incremental.
    git -C "$REDUX_SRC_DIR" checkout --quiet -f "$REDUX_COMMIT"
    (
        cd "$REDUX_SRC_DIR"
        # Upstream's CI does the same: xmake-psx's codeberg home rate-limits clones.
        export GIT_CONFIG_COUNT=1
        export GIT_CONFIG_KEY_0=url.https://github.com/pcsx-redux/xmake-psx.git.insteadOf
        export GIT_CONFIG_VALUE_0=https://codeberg.org/malucart/xmake-psx.git
        git submodule sync --quiet --recursive
        git submodule update --quiet --init --recursive --force --jobs 8
    ) || die "submodule update failed"
    for patch in "${REDUX_PATCHES[@]}"; do
        [ -f "$patch" ] || continue
        log "Applying $(basename "$patch")"
        git -C "$REDUX_SRC_DIR" apply "$patch"
    done
    log "Building PCSX-Redux (about 10 minutes on 12 jobs)"
    make -C "$REDUX_SRC_DIR" -j"$JOBS" all > "$SRC/pcsx-redux-build.log" 2>&1 \
        || die "PCSX-Redux build failed; see $SRC/pcsx-redux-build.log"
    rm -rf "$REDUX_PREFIX"
    make -C "$REDUX_SRC_DIR" install DESTDIR="$REDUX_PREFIX" > "$SRC/pcsx-redux-install.log" 2>&1 \
        || die "PCSX-Redux install failed; see $SRC/pcsx-redux-install.log"
    rm -f "$TC/.stamp-redux-openbios"   # the install replaced resources/
    stamp_set redux "$REDUX_STAMP"
fi

# --- mipsel-none-elf GCC (opt-in) -----------------------------------------------
MIPS_TC="$TC/mipsel"
if [ $MIPS_GCC = 1 ] && ! stamp_ok redux-mips-gcc "$MIPS_BINUTILS_VER $MIPS_GCC_VER"; then
    log "Building mipsel-none-elf binutils $MIPS_BINUTILS_VER + gcc $MIPS_GCC_VER (15-30 minutes)"
    fetch "https://ftpmirror.gnu.org/gnu/binutils/binutils-$MIPS_BINUTILS_VER.tar.gz" \
        "$SRC/binutils-$MIPS_BINUTILS_VER.tar.gz" "$MIPS_BINUTILS_SHA256"
    fetch "https://ftpmirror.gnu.org/gnu/gcc/gcc-$MIPS_GCC_VER/gcc-$MIPS_GCC_VER.tar.xz" \
        "$SRC/gcc-$MIPS_GCC_VER.tar.xz" "$MIPS_GCC_SHA256"
    rm -rf "$SRC/mipsel-binutils-$MIPS_BINUTILS_VER" "$SRC/mipsel-gcc-$MIPS_GCC_VER" "$MIPS_TC"
    mkdir -p "$SRC/mipsel-binutils-$MIPS_BINUTILS_VER" "$SRC/mipsel-gcc-$MIPS_GCC_VER"
    tar -xzf "$SRC/binutils-$MIPS_BINUTILS_VER.tar.gz" -C "$SRC/mipsel-binutils-$MIPS_BINUTILS_VER" --strip-components=1
    tar -xJf "$SRC/gcc-$MIPS_GCC_VER.tar.xz" -C "$SRC/mipsel-gcc-$MIPS_GCC_VER" --strip-components=1
    (
        # Same configuration as PCSX-Redux's tools/linux-mips/spawn-compiler.sh.
        cd "$SRC/mipsel-binutils-$MIPS_BINUTILS_VER"
        ./configure --target=mipsel-none-elf --disable-multilib --disable-nls --disable-werror --prefix="$MIPS_TC"
        make -j"$JOBS"
        make install-strip
        mkdir -p "$SRC/mipsel-gcc-$MIPS_GCC_VER/build"
        cd "$SRC/mipsel-gcc-$MIPS_GCC_VER/build"
        ../configure --target=mipsel-none-elf --without-isl --disable-nls --disable-threads --disable-shared \
            --disable-libssp --disable-libstdcxx-pch --disable-libgomp --disable-werror --without-headers \
            --disable-hosted-libstdcxx --with-as="$MIPS_TC/bin/mipsel-none-elf-as" \
            --with-ld="$MIPS_TC/bin/mipsel-none-elf-ld" --enable-languages=c,c++ --prefix="$MIPS_TC"
        make -j"$JOBS" all-gcc
        make install-strip-gcc
        make -j"$JOBS" all-target-libgcc
        make install-strip-target-libgcc
        make -j"$JOBS" all-target-libstdc++-v3
        make install-strip-target-libstdc++-v3
    ) > "$SRC/mipsel-gcc-build.log" 2>&1 || die "mipsel-none-elf toolchain build failed; see $SRC/mipsel-gcc-build.log"
    stamp_set redux-mips-gcc "$MIPS_BINUTILS_VER $MIPS_GCC_VER"
fi
[ -x "$MIPS_TC/bin/mipsel-none-elf-gcc" ] && export PATH="$MIPS_TC/bin:$PATH"

# --- OpenBIOS (optional: needs a mipsel-none-elf GCC) ---------------------------
MIPS_PREFIX="${MIPS_PREFIX:-mipsel-none-elf}"
if command -v "$MIPS_PREFIX-gcc" > /dev/null && command -v "$MIPS_PREFIX-objcopy" > /dev/null; then
    gcc_ver="$("$MIPS_PREFIX-gcc" -dumpversion)"
    if ! stamp_ok redux-openbios "$REDUX_STAMP gcc-$gcc_ver"; then
        log "Building OpenBIOS with $MIPS_PREFIX-gcc $gcc_ver"
        (
            make -C "$REDUX_SRC_DIR/src/mips/openbios" PREFIX="$MIPS_PREFIX" clean
            make -C "$REDUX_SRC_DIR/src/mips/openbios" PREFIX="$MIPS_PREFIX" -j"$JOBS" all
        ) > "$SRC/openbios-build.log" 2>&1 || die "OpenBIOS build failed; see $SRC/openbios-build.log"
        install -D -m 644 "$REDUX_SRC_DIR/src/mips/openbios/openbios.bin" \
            "$REDUX_PREFIX/share/pcsx-redux/resources/openbios.bin"
        stamp_set redux-openbios "$REDUX_STAMP gcc-$gcc_ver"
    fi
else
    cat <<EOF
==> OpenBIOS skipped: no $MIPS_PREFIX-gcc on PATH. PCSX-Redux still runs, but a
    game boots only with a BIOS: pass one with \`redux.py start --bios FILE\`
    (or PCSX_REDUX_BIOS), or rerun with --mips-gcc to build binutils
    $MIPS_BINUTILS_VER + gcc $MIPS_GCC_VER for mipsel-none-elf first (downloads 55 + 107 MB).
EOF
fi

log "PCSX-Redux ready: $REDUX_PREFIX/bin/pcsx-redux"
