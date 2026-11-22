#!/usr/bin/env bash
# Build Apple's ld64 955.13 (the source of the macOS runner's ld-classic) for
# Linux, so tools/darwin_link.py can link the macOS module check's field and
# battle modules and its check executable here, as the public "macOS module
# reset" workflow does on Apple Silicon (short of running it).
#
#   tools/build_ld64.sh            # build once; later runs check the pins and stop
#   tools/build_ld64.sh --force    # rebuild cctools (ld64) even when installed
#
# From tpoechtrager's ports of Apple's open source: apple-libtapi (text-based
# stubs, which ld64 reads for libSystem), apple-libdispatch, and cctools-port
# at its ld64-955.13 tag. Everything lands in $XG_DARWIN_LD_DIR (default
# ~/.cache/xg-darwin-ld): libtapi is an LLVM build of several GB, kept out of
# the checkout. The linker is $XG_DARWIN_LD_DIR/prefix/bin/aarch64-apple-darwin-ld.
#
# Host requirements: clang and clang++, cmake, ninja, git, make, python3. libtapi's
# build takes a while (an LLVM subset); cctools a few minutes.
#
# Every download is pinned by commit.
set -euo pipefail

FORCE=0
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        *) echo "usage: $0 [--force]" >&2; exit 2 ;;
    esac
done

LIBTAPI_COMMIT=56030b1bc87b64979d29418b1d3ba44a577e9e4c      # apple-libtapi 1300.6.5
CCTOOLS_COMMIT=f37c14eb36dd28c4bbaf114f183486b9822de080      # cctools-port 1024.3-ld64-955.13
LIBDISPATCH_COMMIT=75d416fbffb9ef05e94559d193917ffb007c6f0d  # apple-libdispatch main

DIR="${XG_DARWIN_LD_DIR:-$HOME/.cache/xg-darwin-ld}"
PREFIX="$DIR/prefix"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
LD="$PREFIX/bin/aarch64-apple-darwin-ld"
STAMP="$PREFIX/.xg-pins"
PINS="libtapi $LIBTAPI_COMMIT cctools $CCTOOLS_COMMIT libdispatch $LIBDISPATCH_COMMIT"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
# ld -v prints the version, then fails for want of input files.
version() { "$LD" -v 2>&1 | sed -n 's/.*PROJECT:\(ld64-[0-9.]*\).*/\1/p' || true; }
for tool in clang clang++ cmake ninja git make python3; do
    command -v "$tool" > /dev/null || { echo "build_ld64: $tool is missing" >&2; exit 1; }
done

# fetch DIR URL COMMIT: a checkout of exactly COMMIT (an existing one is checked, not moved).
fetch() {
    if [ ! -d "$1/.git" ]; then
        log "Fetching $(basename "$1") $3"
        git init --quiet "$1"
        git -C "$1" remote add origin "$2"
        git -C "$1" fetch --quiet --depth 1 origin "$3"
        git -C "$1" checkout --quiet FETCH_HEAD
    fi
    if [ "$(git -C "$1" rev-parse HEAD)" != "$3" ]; then
        echo "build_ld64: $1 is at $(git -C "$1" rev-parse HEAD), not the pinned $3 (remove it to refetch)" >&2
        exit 1
    fi
}

mkdir -p "$DIR"
cd "$DIR"
fetch apple-libtapi https://github.com/tpoechtrager/apple-libtapi "$LIBTAPI_COMMIT"
fetch cctools-port https://github.com/tpoechtrager/cctools-port "$CCTOOLS_COMMIT"
fetch apple-libdispatch https://github.com/tpoechtrager/apple-libdispatch "$LIBDISPATCH_COMMIT"

if [ "$FORCE" = 0 ] && [ -x "$LD" ] && [ "$(cat "$STAMP" 2>/dev/null)" = "$PINS" ]; then
    log "$(version) is built from the pinned sources: $LD"
    exit 0
fi

export CC=clang CXX=clang++
# Some distributions (Gentoo) wrap llvm-config.h per ABI; let the wrapper find its target.
INC=""
if command -v llvm-config > /dev/null; then
    INC="-idirafter $(llvm-config --includedir)"
fi
export CFLAGS="-O2 $INC" CXXFLAGS="-O2 $INC"

if [ ! -e "$PREFIX/lib/libdispatch.so" ]; then
    log "Building apple-libdispatch"
    cmake -S apple-libdispatch -B apple-libdispatch/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF
    cmake --build apple-libdispatch/build -j "$JOBS"
    cmake --install apple-libdispatch/build
fi
if [ ! -e "$PREFIX/lib/libtapi.so" ]; then
    log "Building apple-libtapi (an LLVM subset: this takes a while)"
    (cd apple-libtapi && INSTALLPREFIX="$PREFIX" JOBS="$JOBS" ./build.sh && ./install.sh)
fi
log "Building cctools-port (ld64 955.13)"
(
    cd cctools-port/cctools
    ./configure --prefix="$PREFIX" --with-libtapi="$PREFIX" --with-libdispatch="$PREFIX" \
        --with-libblocksruntime="$PREFIX" --target=aarch64-apple-darwin
    make -j"$JOBS"
    make install
)
[ "$(version)" = ld64-955.13 ] || { echo "build_ld64: $LD is $(version), not ld64-955.13" >&2; exit 1; }
echo "$PINS" > "$STAMP"
log "ld64 built: $LD"
