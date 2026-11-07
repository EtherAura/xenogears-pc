#!/usr/bin/env bash
# The Windows build of the PC port (x86-64, MinGW-w64 cross-compiled on
# Linux) and its folder to run from, in one command.
#
#   tools/build_windows.sh [--jp] [--debug] [--engine original|enhanced|both] [OUTDIR]
#
# Both engines by default (docs/ENGINE_PLAN.md): xenogears.exe (Original, the
# game as shipped) and xenogears-enhanced.exe (Enhanced) in one folder, so the
# engine choice (settings.ini engine=, the settings menu; port/src/system/engine.c
# looks for the other executable beside this one) works on Windows: the
# executable started hands over to the chosen one, waits for it and exits with
# its code. --engine builds one.
#
# 1. SDL3 for MinGW, once (tools/build_sdl3_mingw.sh, from the local tarball),
#    and Khronos glslang for the shader presets when its tarball is there
#    (tools/build_glslang_mingw.sh; without it the build has no presets),
#    and dav1d for the movie packs' AV1 videos likewise (tools/build_dav1d_mingw.sh);
# 2. the source lists (tools/pc_sources.py);
# 3. CMake with port/cmake/mingw-w64-x86_64.cmake into build/pc/win-<ver> (Original)
#    and build/pc/win-<ver>-enhanced ($XG_WIN_BUILD and $XG_WIN_BUILD_ENHANCED if set;
#    Release, or with --debug Debug and unstripped);
# 4. cmake --install of each into OUTDIR (default build/pc/dist/xenogears-<ver>-windows-x86_64):
#    xenogears.exe, xenogears-enhanced.exe, SDL3.dll, COPYING.txt (the port's licence),
#    the libraries' licences (SDL3, Dear ImGui, the jukebox font, glslang and dav1d
#    when linked) and a README.txt. The two builds' netplay compatibility ids differ
#    (gen/netplay_compat.h: the engines never play together).
#
# Needs x86_64-w64-mingw32-gcc/ld/objcopy, cmake, ninja, glslc, the host gcc
# (pc_link's layout probes) and, like every PC build, the PS1 build's data
# assembly from the player's disc (tools/configure.py). The executable holds
# no module data from that disc: it reads it from the player's image at every
# start (port/src/system/gamedata.c; tools/payloadcheck.py checks the build).
#
# Test under Wine (winevulkan), headless, as on Linux. Name the disc (or set
# XG_DISC): headless, with no argument, it uses the build's default image or exits
# with status 1 (the setup screen opens only when someone can answer it).
#   XG_HEADLESS=1 XG_SETTINGS=none XG_DUMP_FRAMES=frames/ SDL_VIDEO_DRIVER=offscreen \
#       SDL_AUDIO_DRIVER=dummy wine build/pc/win-us/xenogears.exe \
#       "bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue"
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER=US
TYPE=Release
OUT=
ENGINES="original enhanced"
while [ $# -gt 0 ]; do
    case "$1" in
        --jp) VER=JP ;;
        --debug) TYPE=Debug ;;
        --engine) shift; case "${1:-}" in
                      original|enhanced) ENGINES="$1" ;;
                      both) ENGINES="original enhanced" ;;
                      *) echo "--engine takes original, enhanced or both" >&2; exit 1 ;;
                  esac ;;
        -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 1 ;;
        *) OUT="$1" ;;
    esac
    shift
done
ver="$(echo "$VER" | tr 'A-Z' 'a-z')"
BUILD="${XG_WIN_BUILD:-$ROOT/build/pc/win-$ver}"
BUILD_ENHANCED="${XG_WIN_BUILD_ENHANCED:-$ROOT/build/pc/win-$ver-enhanced}"
OUT="${OUT:-$ROOT/build/pc/dist/xenogears-$ver-windows-x86_64}"
PY="$ROOT/.venv/bin/python"
[ -x "$PY" ] || PY=python3

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

"$ROOT/tools/build_sdl3_mingw.sh"
"$ROOT/tools/build_glslang_mingw.sh"
"$ROOT/tools/build_dav1d_mingw.sh"

log "Source lists"
"$PY" "$ROOT/tools/pc_sources.py" >/dev/null

strip=()
[ "$TYPE" = Release ] && strip=(--strip)
rm -rf "$OUT"
for engine in $ENGINES; do
    dir="$BUILD"
    [ "$engine" = enhanced ] && dir="$BUILD_ENHANCED"
    log "Building $VER $engine ($TYPE) in $dir"
    cmake -S "$ROOT/port" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" -DXG_VERSION="$VER" -DXG_ENGINE="$engine" \
        -DPython3_EXECUTABLE="$PY" --toolchain "$ROOT/port/cmake/mingw-w64-x86_64.cmake" >/dev/null
    cmake --build "$dir"
    log "Installing $engine into $OUT"
    cmake --install "$dir" --prefix "$OUT" "${strip[@]}" >/dev/null
done
cp "$ROOT/tools/toolchain/mingw64/share/licenses/SDL3/LICENSE.txt" "$OUT/LICENSE-SDL3.txt"
cp "$ROOT/port/third_party/imgui/LICENSE.txt" "$OUT/LICENSE-imgui.txt"

if [ "$VER" = JP ]; then disc="Xenogears (Japan) (Disc 1)"; else disc="Xenogears (USA) (Disc 1)"; fi
rev="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
dirty="$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null | head -1 || true)"
[ -n "$dirty" ] && rev="$rev+changes"
sed 's/$/\r/' > "$OUT/README.txt" <<EOF
Xenogears PC port, $VER version, Windows x86-64 ($TYPE, $rev, $(date +%Y-%m-%d))

Run
  xenogears.exe "C:\\path\\to\\$disc.cue"
  or drop the .cue onto xenogears.exe. The .cue's .bin files sit next to it.
  xenogears.exe is the game as shipped (the Original engine);
  xenogears-enhanced.exe is the Enhanced engine (smoother field motion at
  60 Hz, the half-resolution large-surfaces option, and more to come). Start
  either: the settings menu's engine choice (F1 > System > Engine) is remembered,
  and the executable you start hands over to the chosen engine beside it.
  With no argument it uses the disc saved by the disc setup (%XG_DISC%
  overrides it). The first start, with none saved, opens the disc setup with
  "$disc.cue" beside xenogears.exe, else
  bin\\$disc\\$disc.cue beside it, and saves your
  choice; xenogears.exe --setup opens it again. Disc 2 is the image of the
  same name with "Disc 2" beside Disc 1, or the one chosen in the setup.

Needs
  64-bit Windows and a GPU driver with Vulkan 1.3. SDL3.dll stays beside
  the exe.

Controls
  Keyboard: arrows, X Cross, C Circle, Z Square, S Triangle, Q/E L1/R1,
  1/3 L2/R2, Enter Start, Backspace Select. Gamepads work through SDL.
  F1 (or the gamepad's Guide button, or both sticks clicked) opens the
  settings menu: display, sound, controls, memory cards, discs. The game
  pauses while it is open.

The game's module data is read from your own disc image at every start.
Keep this build for your own use.

Licence: GNU GPL v3 or later, see COPYING.txt; SDL3, Dear ImGui and the jukebox font keep their own licences.
EOF

log "Done: $OUT"
ls -l "$OUT"
for engine in $ENGINES; do
    dir="$BUILD"; [ "$engine" = enhanced ] && dir="$BUILD_ENHANCED"
    printf '%s netplay compat: %s\n' "$engine" "$(grep -o '0x[0-9a-f]*ull' "$dir/gen/netplay_compat.h" | head -1)"
done
