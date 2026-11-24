#!/usr/bin/env bash
# The Android app (xg-mrj.3.6): native libraries for each ABI and region, then
# the APK (port/android, Gradle).
#
#   tools/build_android.sh                 # x86_64 + arm64-v8a, US + JP, debug APK
#   tools/build_android.sh --abi x86_64    # one ABI (repeatable)
#   tools/build_android.sh --region us     # one region (repeatable)
#   tools/build_android.sh --engine original  # one engine (repeatable; default both: the
#                                          # loader loads the one settings.ini's engine names)
#   tools/build_android.sh --id-suffix .dev   # the package name with a suffix: a test build
#                                          # installed beside the app, not over it
#   tools/build_android.sh --release       # release APK, signed with the user's key (below)
#   tools/build_android.sh --version-name 2026.11.21 --version-code 202611021
#                                          # the app's version (a release's, from its tag), instead of
#                                          # git's: the latest tag + short hash, and the commit count
#   tools/build_android.sh --native-only   # the libraries, no APK
#   tools/build_android.sh --updater       # with the opt-in update check and install (-DXG_UPDATER=ON;
#                                          # the INTERNET and REQUEST_INSTALL_PACKAGES permissions:
#                                          # port/src/update, xg-4oq.14.3, .14.5); off by default and
#                                          # in releases until xg-4oq.14.7
#   tools/build_android.sh --updater-test  # the same as a test build, never to hand out
#                                          # (-DXG_UPDATER_TEST=ON: the tests' FIXTURE key trusted,
#                                          # XG_UPDATE_TEST_DIR read instead of the network; built
#                                          # without -DXG_DIST, its payload checked without --dist)
#   XG_ANDROID_OUT=DIR tools/build_android.sh ...   # build in DIR instead of build/android
#
# Needs (docs/ANDROID_PLAN.md "Android tooling"): the Android SDK in
# $ANDROID_HOME (default ~/Android/sdk: platform android-36, build-tools
# 36.1.0, NDK $XG_NDK_VERSION), Gradle (default ~/Android/gradle-9.9.0), a
# JDK 17+ ($JAVA_HOME), SDL3's source (its Java glue and its library: the
# version tools/deps.json pins, unpacked once from the tarball tools/deps.py
# finds and checks into build/android/src; `python tools/deps.py fetch sdl3`
# downloads it; $XG_SDL3_SOURCE names another source folder), and what the PC
# build needs (python tools/configure.py, tools/pc_sources.py: the game C,
# the host gcc with -m32 for pc_link's layout probes, glslc). The host gcc also
# reads the game C's declarations for each library's state schema (clang writes
# no debug information for them: tools/state_schema.py clang_declared); without
# it nothing is built.
#
# Outputs under build/android: sdl3-<abi> (SDL3 built for the ABI),
# <abi>-<region> and <abi>-<region>-enhanced (the port's CMake builds of each
# engine, -DXG_SHARED=ON), jniLibs/<abi> (what the APK packs: libSDL3.so,
# libc++_shared.so, libxgloader.so, libxenogears-<region>.so and
# libxenogears-enhanced-<region>.so; each game library passes tools/payloadcheck.py),
# apk-assets/<abi>/schemas/<abi> (each game library's state schema, <library>.xgschema:
# port/CMakeLists.txt XG_STATE_SCHEMA; only its ABI's APK carries it, as
# assets/schemas/<abi>/, where the game reads it), gradle/ (Gradle's outputs, one run
# per ABI; the APKs in gradle/app/outputs/apk/<variant>/, one per ABI:
# app-arm64-v8a-<variant>.apk, app-x86_64-<variant>.apk). The arm64-v8a APK passes
# tools/check_android_alignment.py (16 KB LOAD segments); every APK passes
# tools/state_schema.py --verify-apk (each game library's schema there, read from it
# as the APK packs it: stripped).
#
# Signing: debug builds use the Android debug key (~/.android/debug.keystore,
# made by the Android Gradle plugin). A release build is signed with the
# user's own key, from the environment: XG_ANDROID_KEYSTORE (path),
# XG_ANDROID_KEY_ALIAS, XG_ANDROID_KEYSTORE_PASSWORD, XG_ANDROID_KEY_PASSWORD
# (port/android/app/build.gradle; docs/ANDROID_PLAN.md "Release signing" has
# the keytool command). Without all four it refuses before building anything;
# after building it checks the signature (apksigner verify) and prints the
# signing certificate's SHA-256 digest (public: what users compare). Only the
# variables' names are ever printed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ABIS=()
REGIONS=()
ENGINES=()
ID_SUFFIX=
VERSION_NAME=
VERSION_CODE=
VARIANT=debug
NATIVE_ONLY=0
UPDATER=OFF
UPDATER_TEST=OFF
while [ $# -gt 0 ]; do
    case "$1" in
    --abi) ABIS+=("$2"); shift ;;
    --region) REGIONS+=("$2"); shift ;;
    --engine)
        case "$2" in original | enhanced) ENGINES+=("$2") ;; *) echo "build_android.sh: --engine original or enhanced" >&2; exit 2 ;; esac
        shift ;;
    --id-suffix) ID_SUFFIX="$2"; shift ;;
    --version-name) VERSION_NAME="$2"; shift ;;
    --version-code)
        case "$2" in '' | *[!0-9]*) echo "build_android.sh: --version-code takes a number" >&2; exit 2 ;; esac
        VERSION_CODE="$2"; shift ;;
    --release) VARIANT=release ;;
    --native-only) NATIVE_ONLY=1 ;;
    --updater) UPDATER=ON ;;
    --updater-test) UPDATER=ON; UPDATER_TEST=ON ;;
    -h | --help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "build_android.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
if [ "$VARIANT" = release ] && [ "$NATIVE_ONLY" = 0 ]; then
    missing=()
    for name in XG_ANDROID_KEYSTORE XG_ANDROID_KEY_ALIAS XG_ANDROID_KEYSTORE_PASSWORD XG_ANDROID_KEY_PASSWORD; do
        [ -n "${!name:-}" ] || missing+=("$name")
    done
    if [ ${#missing[@]} -eq 0 ] && [ ! -r "$XG_ANDROID_KEYSTORE" ]; then
        missing+=("XG_ANDROID_KEYSTORE (it names no readable file)")
    fi
    if [ ${#missing[@]} -gt 0 ]; then
        echo "build_android.sh: a release APK is signed with your own key; not set: ${missing[*]}." >&2
        echo "  See docs/ANDROID_PLAN.md, \"Release signing\" (the keytool command, the four variables)." >&2
        echo "  Nothing was built." >&2
        exit 2
    fi
fi
[ ${#ABIS[@]} -gt 0 ] || ABIS=(x86_64 arm64-v8a)
[ ${#REGIONS[@]} -gt 0 ] || REGIONS=(us jp)
[ ${#ENGINES[@]} -gt 0 ] || ENGINES=(original enhanced)
# A test build of the updater is never handed out: no -DXG_DIST (CMake refuses the two together).
DIST=ON
DIST_CHECK=--dist
if [ "$UPDATER_TEST" = ON ]; then
    DIST=OFF
    DIST_CHECK=
fi
case " ${ABIS[*]} " in *" arm64-v8a "*) HAS_ARM64=1 ;; *) HAS_ARM64=0 ;; esac

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/sdk}"
NDK_VERSION="${XG_NDK_VERSION:-29.0.14206865}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
GRADLE="${XG_GRADLE:-$HOME/Android/gradle-9.9.0/bin/gradle}"
API=33 # Android 13: Vulkan 1.3 on current drivers (docs/ANDROID_PLAN.md decisions)
OUT="${XG_ANDROID_OUT:-$ROOT/build/android}"
# A build to hand out maps the repository's and the home's paths out of its debug information
# (port/CMakeLists.txt XG_DIST), not other folders': SDL3's headers under an OUT outside both
# would stay in it and fail the payload audit at the end.
if [ "$DIST" = ON ]; then
    case "$(realpath -ms "$OUT")/" in
        "$(realpath -ms "$ROOT")"/* | "$(realpath -ms "$HOME")"/*) ;;
        *) echo "build_android.sh: XG_ANDROID_OUT=$OUT is outside the repository and \$HOME; a build to hand" >&2
           echo "  out would carry its path. Build under one of them (or --updater-test). Nothing was built." >&2
           exit 2 ;;
    esac
fi
PY="$ROOT/.venv/bin/python"
[ -x "$PY" ] || PY=python3
# SDL3's source: the pinned version, unpacked from its checked tarball (xg-mrj.3.17), unless
# XG_SDL3_SOURCE names one. SDL_STAMP names what was built from it, so a change of version (or
# of folder) rebuilds SDL3 for every ABI.
if [ -n "${XG_SDL3_SOURCE:-}" ]; then
    SDL3_SOURCE="$XG_SDL3_SOURCE"
    SDL_STAMP="source $SDL3_SOURCE"
else
    SDL_STAMP="$("$PY" "$ROOT/tools/deps.py" want sdl3)"
    SDL3_SOURCE="$OUT/src/SDL3-$("$PY" "$ROOT/tools/deps.py" version sdl3)"
    if [ "$(cat "$SDL3_SOURCE/.xg-stamp" 2>/dev/null)" != "$SDL_STAMP" ]; then
        tarball="$("$PY" "$ROOT/tools/deps.py" path sdl3)" || exit 1
        rm -rf "$SDL3_SOURCE"
        mkdir -p "$OUT/src"
        tar -xzf "$tarball" -C "$OUT/src"
        [ -f "$SDL3_SOURCE/CMakeLists.txt" ] || { echo "build_android.sh: $tarball holds no $(basename "$SDL3_SOURCE")" >&2; exit 1; }
        echo "$SDL_STAMP" > "$SDL3_SOURCE/.xg-stamp"
    fi
fi
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
READELF="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
for need in "$TOOLCHAIN" "$SDL3_SOURCE/CMakeLists.txt" "$GRADLE" "$ROOT/build/pc/sources.cmake"; do
    if [ ! -e "$need" ]; then
        echo "build_android.sh: missing $need" >&2
        exit 1
    fi
done
if [ "$HAS_ARM64" = 1 ] && [ "$NATIVE_ONLY" = 0 ] && [ ! -x "$READELF" ]; then
    echo "build_android.sh: missing $READELF (the APK's 16 KB alignment check needs it)" >&2
    exit 1
fi
# 16 KB page devices (xg-6gm): every lib/arm64-v8a/*.so the APK packs, SDL3 and libc++_shared
# included, must have each LOAD segment aligned to 0x4000 or more; read from the APK itself and
# cross-checked with the NDK's llvm-readelf (tools/check_android_alignment.py).
check_alignment() {
    if ! "$PY" tools/check_android_alignment.py --apk "$1" --require-readelf --readelf "$READELF"; then
        echo "build_android.sh: $1: arm64-v8a LOAD alignment below 16 KB, or unreadable (above)" >&2
        exit 1
    fi
}
GLSLC="$(command -v glslc || echo "$NDK/shader-tools/linux-x86_64/glslc")"
# The host's GCC: pc_link's layout probes, and the declarations each game library's state schema
# takes its generated data's types from (a schema without them keeps its slots in its own build).
HOST_GCC="$(command -v gcc || true)"
if [ -z "$HOST_GCC" ]; then
    echo "build_android.sh: no gcc (the host's GCC: pc_link's layout probes and the state schemas' declarations)" >&2
    exit 1
fi

mkdir -p "$OUT"
for abi in "${ABIS[@]}"; do
    # SDL3 for the ABI: a shared library, as SDL's activity loads it.
    sdl="$OUT/sdl3-$abi"
    if [ ! -f "$sdl/lib/cmake/SDL3/SDL3Config.cmake" ] || [ "$(cat "$sdl/.xg-stamp" 2>/dev/null)" != "$SDL_STAMP" ]; then
        rm -rf "$sdl" "$OUT/sdl3-build-$abi"
        cmake -S "$SDL3_SOURCE" -B "$OUT/sdl3-build-$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
            -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
            -DCMAKE_INSTALL_PREFIX="$sdl"
        cmake --build "$OUT/sdl3-build-$abi"
        cmake --install "$OUT/sdl3-build-$abi"
        echo "$SDL_STAMP" > "$sdl/.xg-stamp"
    fi
    jni="$OUT/jniLibs/$abi"
    mkdir -p "$jni"
    cp -f "$sdl/lib/libSDL3.so" "$jni/"
    # The modules' partial links need GNU ld and objcopy for the ABI: LLVM's lld keeps common
    # symbols in an `ld -r` (tools/pc_srcdata.py); the final link is the NDK's.
    case "$abi" in
    x86_64) triple=x86_64-pc-linux-gnu; ndk_triple=x86_64-linux-android ;;
    arm64-v8a) triple=aarch64-unknown-linux-gnu; ndk_triple=aarch64-linux-android ;;
    *) echo "build_android.sh: no GNU binutils known for $abi" >&2; exit 1 ;;
    esac
    # The C++ runtime as its own library (ANDROID_STL=c++_shared): the game library then holds
    # only the port's code (tools/payloadcheck.py), and SDL3 keeps its own static copy.
    cp -f "$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$ndk_triple/libc++_shared.so" "$jni/"
    gnu_ld="$(command -v "$triple-ld" || { [ "$abi" = x86_64 ] && command -v ld; })"
    gnu_objcopy="$(command -v "$triple-objcopy" || { [ "$abi" = x86_64 ] && command -v objcopy; })"
    # A library a game engine and region (xg-mrj.3.21): the loader loads the one the
    # settings ask for. An engine not built this time leaves the APK, or the APK would
    # carry an old build of it.
    schemas="$OUT/apk-assets/$abi/schemas/$abi"
    mkdir -p "$schemas"
    case " ${ENGINES[*]} " in *" enhanced "*) ;; *) rm -f "$jni"/libxenogears-enhanced-*.so "$schemas"/libxenogears-enhanced-*.xgschema ;; esac
    case " ${ENGINES[*]} " in *" original "*) ;; *) rm -f "$jni"/libxenogears-{us,jp}.so "$schemas"/libxenogears-{us,jp}.xgschema ;; esac
    for engine in "${ENGINES[@]}"; do
        for region in "${REGIONS[@]}"; do
            if [ "$engine" = enhanced ]; then
                b="$OUT/$abi-$region-enhanced"
                lib="libxenogears-enhanced-$region.so"
            else
                b="$OUT/$abi-$region"
                lib="libxenogears-$region.so"
            fi
            cmake -S port -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
                -DANDROID_STL=c++_shared -DXG_VERSION="${region^^}" -DXG_ENGINE="$engine" -DXG_EXTRAS=ON \
                -DXG_SHARED=ON -DXG_LTO=OFF -DXG_DIST="$DIST" -DXG_UPDATER="$UPDATER" \
                -DXG_UPDATER_TEST="$UPDATER_TEST" \
                -DSDL3_DIR="$sdl/lib/cmake/SDL3" -DVulkan_GLSLC_EXECUTABLE="$GLSLC" \
                -DXG_LD="$gnu_ld" -DXG_OBJCOPY="$gnu_objcopy" -DXG_PROBE_CC="$HOST_GCC" -DXG_SCHEMA_GCC="$HOST_GCC" \
                -DPython3_EXECUTABLE="$PY"
            cmake --build "$b" --target xenogears_game xgloader
            cp -f "$b/$lib" "$b/libxgloader.so" "$jni/"
            # No retail module data in what the APK carries (the library as built: the same bytes as
            # the packed, stripped one, with the symbols that attribute them), and no folder of this
            # machine (--dist, xg-4oq.9: an app never runs from the repository, so every Android
            # build is one to hand out).
            "$PY" tools/payloadcheck.py $DIST_CHECK --build "$b" --exe "$b/$lib"
            # Its state schema, the one written from this library (packed, no path of this machine), with
            # which its slots load in other builds too (portable: the game C's declarations read with HOST_GCC).
            "$PY" tools/state_schema.py --verify "$b/${lib%.so}.xgschema" --image "$b/$lib" --portable
            cp -f "$b/${lib%.so}.xgschema" "$schemas/"
        done
    done
done
[ "$NATIVE_ONLY" = 1 ] && exit 0

# The licences as files in the APK too (assets/licenses); F1 > System > About shows them.
licences="$OUT/assets/licenses"
mkdir -p "$licences"
cp -f port/COPYING "$licences/COPYING-GPL-3.0.txt"
cp -f "$SDL3_SOURCE/LICENSE.txt" "$licences/LICENSE-SDL3.txt"
cp -f port/third_party/imgui/LICENSE.txt "$licences/LICENSE-Dear-ImGui.txt"
cp -f port/third_party/jukebox-font/LICENSE.txt "$licences/LICENSE-Jukebox-font-Noto-Sans-CJK.txt"
cp -f port/third_party/llvm-libcxx/LICENSE.txt "$licences/LICENSE-LLVM-libcxx-libunwind.txt"
cat > "$licences/README.txt" <<'NOTICE'
Xenogears PC port for Android: its code is under the GNU General Public
License, version 3 or later (COPYING-GPL-3.0.txt). It contains SDL 3 (zlib
licence), Dear ImGui (MIT), a Japanese font subset of Noto Sans CJK (SIL Open
Font Licence 1.1) and the Android NDK's LLVM libc++ and libunwind (Apache 2.0
with LLVM exceptions); their licences are beside this file. The game's own data
is read from the player's discs and is not part of this app.
NOTICE

# The APKs, one per ABI: SDL's Java glue from its source, the libraries above. The variant's
# earlier APKs go first, so none of another build (a universal APK, an ABI not built this
# time) is listed, checked, signed or handed out. One Gradle run per ABI, each with its
# ABI's schemas as assets of its own (apk-assets/<abi>): the other ABI's would be dead weight
# in it. Each run's APK is kept aside until all have run.
task=assemble${VARIANT^}
apks="$OUT/gradle/app/outputs/apk/$VARIANT"
held="$OUT/gradle/apk-held"
rm -rf "$held"
rm -f "$apks"/*.apk
mkdir -p "$held"
for abi in "${ABIS[@]}"; do
    "$GRADLE" -p port/android --project-cache-dir "$OUT/gradle-cache" --no-daemon \
        -Pxg.sdlSource="$SDL3_SOURCE" -Pxg.jniLibs="$OUT/jniLibs" -Pxg.abis="$abi" -Pxg.buildRoot="$OUT/gradle" \
        -Pxg.assets="$OUT/assets" -Pxg.abiAssets="$OUT/apk-assets/$abi" -Pxg.idSuffix="$ID_SUFFIX" \
        -Pxg.updater="${UPDATER,,}" \
        ${VERSION_NAME:+-Pxg.versionName="$VERSION_NAME"} ${VERSION_CODE:+-Pxg.versionCode="$VERSION_CODE"} \
        "$task"
    mv "$apks"/*.apk "$held/"
done
mv "$held"/*.apk "$apks/"
rmdir "$held"
ls -l "$apks"/*.apk
for built in "$apks"/*.apk; do
    if ! "$PY" tools/state_schema.py --verify-apk "$built" --portable; then
        echo "build_android.sh: $built: a game library without its state schema, or one that does not describe it" >&2
        exit 1
    fi
done
if [ "$HAS_ARM64" = 1 ]; then
    for built in "$OUT"/gradle/app/outputs/apk/"$VARIANT"/*arm64-v8a*.apk; do
        check_alignment "$built"
    done
else
    echo "build_android.sh: note: no arm64-v8a in --abi (${ABIS[*]}): the 16 KB LOAD check is not run; no APK is for arm64 devices" >&2
fi
if [ "$VARIANT" = release ]; then
    # The signature as a device checks it, on every APK, and the certificate they name (its digest only).
    for apk in "$OUT"/gradle/app/outputs/apk/release/*.apk; do
        echo "$apk:"
        certs=$("$ANDROID_HOME/build-tools/36.1.0/apksigner" verify --verbose --print-certs "$apk")
        grep -E '^Verified using v[0-9]+ scheme' <<< "$certs" | grep true
    done
    grep -m1 'certificate SHA-256 digest' <<< "$certs"
fi
