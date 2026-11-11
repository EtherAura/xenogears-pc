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
# (what the APK packs: libSDL3.so, libc++_shared.so, libxgloader.so,
# libxenogears-<region>.so; each game library passes tools/payloadcheck.py),
# gradle/ (Gradle's outputs; the APK in gradle/app/outputs/apk/).
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
    x86_64) triple=x86_64-pc-linux-gnu; ndk_triple=x86_64-linux-android ;;
    arm64-v8a) triple=aarch64-unknown-linux-gnu; ndk_triple=aarch64-linux-android ;;
    *) echo "build_android.sh: no GNU binutils known for $abi" >&2; exit 1 ;;
    esac
    # The C++ runtime as its own library (ANDROID_STL=c++_shared): the game library then holds
    # only the port's code (tools/payloadcheck.py), and SDL3 keeps its own static copy.
    cp -f "$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$ndk_triple/libc++_shared.so" "$jni/"
    gnu_ld="$(command -v "$triple-ld" || { [ "$abi" = x86_64 ] && command -v ld; })"
    gnu_objcopy="$(command -v "$triple-objcopy" || { [ "$abi" = x86_64 ] && command -v objcopy; })"
    for region in "${REGIONS[@]}"; do
        b="$OUT/$abi-$region"
        cmake -S port -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" \
            -DANDROID_STL=c++_shared -DXG_VERSION="${region^^}" -DXG_EXTRAS=ON -DXG_SHARED=ON -DXG_LTO=OFF \
            -DSDL3_DIR="$sdl/lib/cmake/SDL3" -DVulkan_GLSLC_EXECUTABLE="$GLSLC" \
            -DXG_LD="$gnu_ld" -DXG_OBJCOPY="$gnu_objcopy" -DXG_PROBE_CC="$(command -v gcc)" \
            -DPython3_EXECUTABLE="$PY"
        cmake --build "$b" --target xenogears_game xgloader
        cp -f "$b/libxenogears-$region.so" "$b/libxgloader.so" "$jni/"
        # No retail module data in what the APK carries (the library as built: the same bytes as
        # the packed, stripped one, with the symbols that attribute them).
        "$PY" tools/payloadcheck.py --build "$b" --exe "$b/libxenogears-$region.so"
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

# The APK: SDL's Java glue from its source, the libraries above.
abilist=$(IFS=,; echo "${ABIS[*]}")
task=assemble${VARIANT^}
"$GRADLE" -p port/android --project-cache-dir "$OUT/gradle-cache" --no-daemon \
    -Pxg.sdlSource="$SDL3_SOURCE" -Pxg.jniLibs="$OUT/jniLibs" -Pxg.abis="$abilist" -Pxg.buildRoot="$OUT/gradle" \
    -Pxg.assets="$OUT/assets" \
    "$task"
ls -l "$OUT"/gradle/app/outputs/apk/"$VARIANT"/*.apk
if [ "$VARIANT" = release ]; then
    # The signature as a device checks it, and the certificate it names (its digest only).
    apk=$(ls "$OUT"/gradle/app/outputs/apk/release/*.apk | head -1)
    certs=$("$ANDROID_HOME/build-tools/36.1.0/apksigner" verify --verbose --print-certs "$apk")
    grep -E '^Verified using v[0-9]+ scheme' <<< "$certs" | grep true
    grep -m1 'certificate SHA-256 digest' <<< "$certs"
fi
