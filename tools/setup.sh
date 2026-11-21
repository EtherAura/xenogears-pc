#!/usr/bin/env bash
# Bootstrap the project-local toolchain. Needs no root: everything lands in
# tools/toolchain (binaries), tools/toolchain-src (build scratch) and .venv.
#
# Host requirements: bash, curl, tar, GNU make, a C compiler, cpp, python3,
# ninja, git. Install gmake on FreeBSD.
#
# Every download is pinned by sha256. The old PSX compilers are the exact
# decompals/old-gcc builds whose cc1 hashes match the ones known to produce
# Xenogears' code (2.6.0 + 2.7.2 from release 0.13, 2.7.2-cdk from 0.14).
set -euo pipefail

# Optional: the PCSX-Redux validation emulator (tools/build_redux.sh).
if [ "${1:-}" = "--redux" ]; then shift; exec "$(dirname "${BASH_SOURCE[0]}")/build_redux.sh" "$@"; fi
# Optional: the Beetle PSX harness's libretro frontend (tools/beetle.py builds it on demand too).
if [ "${1:-}" = "--beetle" ]; then exec python3 "$(dirname "${BASH_SOURCE[0]}")/beetle.py" build; fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TC="$ROOT/tools/toolchain"
SRC="$ROOT/tools/toolchain-src"
JOBS="$(nproc 2>/dev/null || echo 4)"
HOST_OS="$(uname -s)"
MAKE_BIN=make
if [ "$HOST_OS" = FreeBSD ]; then
    MAKE_BIN=gmake
fi

BINUTILS_VER=2.42
BINUTILS_SHA256=f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800

OLD_GCC=(
    # name            release  tarball sha256
    "gcc-2.6.0-psx    0.13     7b279053692dee54c14ac5bd370f1b3e6e9f2ba6bc3ea09814d73b42d92a9ddc"
    "gcc-2.7.2-psx    0.13     aca64479041aa2d645dc52ebcaace276c0aa06f258c0e3f190ccf6d76701ffbc"
    "gcc-2.7.2-cdk    0.14     e7758645860da51e2c42a8514fc08ee41b4da5c3985a3f92685fee7d0fd10f04"
    "gcc-2.5.7-psx    0.17     0db1fb403c766e51b094354b9a136a6c1f01457aa41ccca28ac457b7cb67d871"
    "gcc-2.6.3-psx    0.17     01e6e8c4933414ea3f8d8e3bc766a1f5fafd4fc0110e0b75d1f691bd791989b1"
)

MASPSX_COMMIT=7686f845a181700534c83c0419183e38aeb3e49c

# Community-published PSY-Q library signatures (byte patterns + symbol
# offsets, no library code), used by tools/psyq_sigs.py.
PSYQ_SIGS_COMMIT=e9e46e7e133ef275a79bfce650924f98edb086bc

OBJDIFF_VER=v3.8.1
OBJDIFF_SHA256=c8290281e82114bcc1a06ff73061110d3902a177822e750337de2537188e358f

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# fetch URL DEST SHA256: download once, always verify.
fetch() {
    local url=$1 dest=$2 sha=$3
    if [ ! -f "$dest" ]; then
        curl -fL --retry 3 -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
    local checksum
    if ! checksum="$(sha256sum < "$dest")" || [ "${checksum%% *}" != "$sha" ]; then
        echo "checksum mismatch for $dest" >&2
        rm -f "$dest"
        exit 1
    fi
}

# stamp NAME VALUE: true if the step already completed with this exact value.
stamp_ok() { [ -f "$TC/.stamp-$1" ] && [ "$(cat "$TC/.stamp-$1")" = "$2" ]; }
stamp_set() { echo "$2" > "$TC/.stamp-$1"; }

mkdir -p "$TC/bin" "$SRC"

# --- GNU binutils for mips-linux-gnu -----------------------------------------
if ! stamp_ok binutils "$BINUTILS_VER"; then
    log "Building binutils $BINUTILS_VER (a few minutes)"
    tarball="$SRC/binutils-$BINUTILS_VER.tar.xz"
    fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz" "$tarball" "$BINUTILS_SHA256"
    rm -rf "$SRC/binutils-$BINUTILS_VER" "$SRC/binutils-build"
    tar -xf "$tarball" -C "$SRC"
    mkdir -p "$SRC/binutils-build"
    (
        cd "$SRC/binutils-build"
        # binutils 2.42 predates C23's static_assert keyword, which is the
        # default in modern GCC; pin the C dialect.
        CFLAGS='-O2 -std=gnu17' CXXFLAGS='-O2' \
            "../binutils-$BINUTILS_VER/configure" \
            --target=mips-linux-gnu --prefix="$TC" \
            --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
            --disable-gprof --disable-gprofng --disable-sim > configure.log 2>&1
        "$MAKE_BIN" -j"$JOBS" > make.log 2>&1
        "$MAKE_BIN" install > install.log 2>&1
    ) || { echo "binutils build failed; see $SRC/binutils-build/*.log" >&2; exit 1; }
    stamp_set binutils "$BINUTILS_VER"
fi

# --- Old PSX GCC builds (cc1 only is used) ------------------------------------
for entry in "${OLD_GCC[@]}"; do
    read -r name release sha <<< "$entry"
    if ! stamp_ok "$name" "$release-$sha"; then
        log "Fetching $name (old-gcc $release)"
        tarball="$SRC/$name-$release.tar.gz"
        fetch "https://github.com/decompals/old-gcc/releases/download/$release/$name.tar.gz" "$tarball" "$sha"
        rm -rf "$TC/$name"
        mkdir -p "$TC/$name"
        tar -xzf "$tarball" -C "$TC/$name"
        stamp_set "$name" "$release-$sha"
    fi
done

# --- maspsx (PSY-Q assembler compatibility layer) ----------------------------
# Our patches (tools/patches/maspsx-*.patch) add opt-in flags; the stamp
# includes their hash, so editing one re-applies them on a fresh checkout.
MASPSX_PATCHES=("$ROOT"/tools/patches/maspsx-*.patch)
MASPSX_STAMP="$MASPSX_COMMIT $(cat "${MASPSX_PATCHES[@]}" | sha256sum | cut -c1-16)"
if ! stamp_ok maspsx "$MASPSX_STAMP"; then
    log "Fetching maspsx $MASPSX_COMMIT"
    rm -rf "$TC/maspsx"
    git clone --quiet https://github.com/mkst/maspsx.git "$TC/maspsx"
    git -C "$TC/maspsx" checkout --quiet "$MASPSX_COMMIT"
    for patch in "${MASPSX_PATCHES[@]}"; do
        log "Applying $(basename "$patch")"
        git -C "$TC/maspsx" apply "$patch"
    done
    stamp_set maspsx "$MASPSX_STAMP"
fi

# --- objdiff-cli --------------------------------------------------------------
if ! stamp_ok objdiff "$OBJDIFF_VER"; then
    log "Fetching objdiff-cli $OBJDIFF_VER"
    fetch "https://github.com/encounter/objdiff/releases/download/$OBJDIFF_VER/objdiff-cli-linux-x86_64" \
        "$SRC/objdiff-cli-$OBJDIFF_VER" "$OBJDIFF_SHA256"
    install -m 755 "$SRC/objdiff-cli-$OBJDIFF_VER" "$TC/bin/objdiff-cli"
    stamp_set objdiff "$OBJDIFF_VER"
fi

# The unbranded Linux ELF is kept intact in SRC for checksum verification.
if [ "$HOST_OS" = FreeBSD ]; then
    brandelf -t Linux "$TC/bin/objdiff-cli"
fi

# --- PSY-Q signatures -------------------------------------------------------
if ! stamp_ok psyq-sigs "$PSYQ_SIGS_COMMIT"; then
    log "Fetching PSY-Q signatures $PSYQ_SIGS_COMMIT"
    rm -rf "$TC/psyq-signatures"
    git clone --quiet https://github.com/lab313ru/psx_psyq_signatures.git "$TC/psyq-signatures"
    git -C "$TC/psyq-signatures" checkout --quiet "$PSYQ_SIGS_COMMIT"
    stamp_set psyq-sigs "$PSYQ_SIGS_COMMIT"
fi

# --- Python environment -------------------------------------------------------
req_hash="$(sha256sum "$ROOT/requirements.txt" | cut -d' ' -f1)"
if ! stamp_ok python "$req_hash"; then
    log "Creating .venv and installing requirements"
    [ -x "$ROOT/.venv/bin/python" ] || python3 -m venv "$ROOT/.venv"
    "$ROOT/.venv/bin/pip" install --quiet --upgrade pip
    "$ROOT/.venv/bin/pip" install --quiet -r "$ROOT/requirements.txt"
    stamp_set python "$req_hash"
fi

log "Toolchain ready."
echo "   Activate with:  source .venv/bin/activate      (fish: source .venv/bin/activate.fish)"
echo "   Then:           python tools/extract.py && python tools/configure.py && ninja"
