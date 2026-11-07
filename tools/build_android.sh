#!/usr/bin/env bash
# The Android app (xg-mrj.3.6): native libraries for each ABI and region, then
# the APK (port/android, Gradle).
#
#   tools/build_android.sh                 # x86_64 + arm64-v8a, US + JP, debug APK
#   tools/build_android.sh --abi x86_64    # one ABI (repeatable)
#   tools/build_android.sh --region us     # one region (repeatable)
#   tools/build_android.sh --release       # release APK, signed with the user's key (below)
#   tools/build_android.sh --native-only   # the libraries, no APK
#
# Needs (docs/ANDROID_PLAN.md "Android tooling"): the Android SDK in
# $ANDROID_HOME (default ~/Android/sdk: platform android-36, build-tools
# 36.1.0, NDK $XG_NDK_VERSION), Gradle (default ~/Android/gradle-9.9.0), a
# JDK 17+ ($JAVA_HOME), SDL3's source ($XG_SDL3_SOURCE, default
# ~/Android/src/SDL3-3.4.16: its Java glue and its library), and what the PC
# build needs (python tools/configure.py, tools/pc_sources.py: the game C,
# the host gcc with -m32 for pc_link's layout probes, glslc).
#
# Outputs under build/android: sdl3-<abi> (SDL3 built for the ABI),
# <abi>-<region> (the port's CMake builds, -DXG_SHARED=ON), jniLibs/<abi>
# (what the APK packs: libSDL3.so, libxgloader.so, libxenogears-<region>.so),
# gradle/ (Gradle's outputs; the APK in gradle/app/outputs/apk/).
#
# Signing: debug builds use the Android debug key (~/.android/debug.keystore,
# made by the Android Gradle plugin). A release build reads the keystore from
# the environment: XG_ANDROID_KEYSTORE (path), XG_ANDROID_KEY_ALIAS,
# XG_ANDROID_KEYSTORE_PASSWORD, XG_ANDROID_KEY_PASSWORD (port/android/app/
# build.gradle); this script never prints them.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ABIS=()
REGIONS=()
VARIANT=debug
NATIVE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
    --abi) ABIS+=("$2"); shift ;;
    --region) REGIONS+=("$2"); shift ;;
    --release) VARIANT=release ;;
    --native-only) NATIVE_ONLY=1 ;;
    -h | --help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "build_android.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
[ ${#ABIS[@]} -gt 0 ] || ABIS=(x86_64 arm64-v8a)
[ ${#REGIONS[@]} -gt 0 ] || REGIONS=(us jp)

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/sdk}"
NDK_VERSION="${XG_NDK_VERSION:-29.0.14206865}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
GRADLE="${XG_GRADLE:-$HOME/Android/gradle-9.9.0/bin/gradle}"
SDL3_SOURCE="${XG_SDL3_SOURCE:-$HOME/Android/src/SDL3-3.4.16}"
API=33 # Android 13: Vulkan 1.3 on current drivers (docs/ANDROID_PLAN.md decisions)
OUT="$ROOT/build/android"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
for need in "$TOOLCHAIN" "$SDL3_SOURCE/CMakeLists.txt" "$GRADLE" "$ROOT/build/pc/sources.cmake"; do
    if [ ! -e "$need" ]; then
        echo "build_android.sh: missing $need" >&2
        exit 1
    fi
done
PY="$ROOT/.venv/bin/python"
[ -x "$PY" ] || PY=python3
GLSLC="$(command -v glslc || echo "$NDK/shader-tools/linux-x86_64/glslc")"

mkdir -p "$OUT"
for abi in "${ABIS[@]}"; do
    # SDL3 for the ABI: a shared library, as SDL's activity loads it.
    sdl="$OUT/sdl3-$abi"
    if [ ! -f "$sdl/lib/cmake/SDL3/SDL3Config.cmake" ]; then
        cmake -S "$SDL3_SOURCE" -B "$OUT/sdl3-build-$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
            -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
            -DCMAKE_INSTALL_PREFIX="$sdl"
        cmake --build "$OUT/sdl3-build-$abi"
        cmake --install "$OUT/sdl3-build-$abi"
    fi
    jni="$OUT/jniLibs/$abi"
    mkdir -p "$jni"
    cp -f "$sdl/lib/libSDL3.so" "$jni/"
    # The modules' partial links need GNU ld and objcopy for the ABI: LLVM's lld keeps common
    # symbols in an `ld -r` (tools/pc_srcdata.py); the final link is the NDK's.
    case "$abi" in
    x86_64) triple=x86_64-pc-linux-gnu ;;
    arm64-v8a) triple=aarch64-unknown-linux-gnu ;;
    *) echo "build_android.sh: no GNU binutils known for $abi" >&2; exit 1 ;;
    esac
    gnu_ld="$(command -v "$triple-ld" || { [ "$abi" = x86_64 ] && command -v ld; })"
    gnu_objcopy="$(command -v "$triple-objcopy" || { [ "$abi" = x86_64 ] && command -v objcopy; })"
    for region in "${REGIONS[@]}"; do
        b="$OUT/$abi-$region"
        cmake -S port -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
            -DANDROID_STL=c++_static -DXG_VERSION="${region^^}" -DXG_EXTRAS=ON -DXG_SHARED=ON -DXG_LTO=OFF \
            -DSDL3_DIR="$sdl/lib/cmake/SDL3" -DVulkan_GLSLC_EXECUTABLE="$GLSLC" \
            -DXG_LD="$gnu_ld" -DXG_OBJCOPY="$gnu_objcopy" -DXG_PROBE_CC="$(command -v gcc)" \
            -DPython3_EXECUTABLE="$PY"
        cmake --build "$b" --target xenogears_game xgloader
        cp -f "$b/libxenogears-$region.so" "$b/libxgloader.so" "$jni/"
    done
done
[ "$NATIVE_ONLY" = 1 ] && exit 0

# The APK: SDL's Java glue from its source, the libraries above.
abilist=$(IFS=,; echo "${ABIS[*]}")
task=assemble${VARIANT^}
"$GRADLE" -p port/android --project-cache-dir "$OUT/gradle-cache" --no-daemon \
    -Pxg.sdlSource="$SDL3_SOURCE" -Pxg.jniLibs="$OUT/jniLibs" -Pxg.abis="$abilist" -Pxg.buildRoot="$OUT/gradle" \
    "$task"
ls -l "$OUT"/gradle/app/outputs/apk/"$VARIANT"/*.apk
