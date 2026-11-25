#!/usr/bin/env bash
# The release's Android APKs from a public port checkout (a tree of the release's tag; it need not be
# a git checkout), signed with the maintainer's key:
#
#   bash packaging/build-android-release.sh --discs DIR --cache DIR --version 2026.11.21 --out DIR
#       [--no-start-check]
#
# --discs holds the player's disc images in their Redump folders ("Xenogears (USA) (Disc 1)/...");
# the US and Japanese Disc 1 images are extracted and checked (tools/extract.py --check-image), the
# app carries both regions. --cache keeps the Python environment and the pinned dependencies
# (tools/deps.py) between releases; it is fetched into, with the network. --version is the
# release's calendar version, YYYY.MM.N: the app's versionName, and its versionCode YYYYMMNNN
# (202611021 for 2026.11.21), which grows with every release so Android takes each as an update.
#
# The signing key comes from the environment, as for tools/build_android.sh --release:
# XG_ANDROID_KEYSTORE, XG_ANDROID_KEY_ALIAS, XG_ANDROID_KEYSTORE_PASSWORD, XG_ANDROID_KEY_PASSWORD.
# Without all four nothing is built (exit 3). Only their names are ever printed.
#
# Needs the Android SDK, NDK, Gradle and a JDK where tools/build_android.sh finds them
# (docs/building.md, Android), and what the PC build needs.
#
# Writes to --out, for each ABI (arm64-v8a: phones and tablets; x86_64: emulators):
#   xenogears-pc-<version>-android-<abi>.apk and its .sha256
# after tools/build_android.sh has run the payload check on every game library, the 16 KB
# alignment check (arm64-v8a) and apksigner verify, and after the APKs were started
# (tools/android_start_check.py: the x86_64 APK on headless emulators, Android 13 and a current
# one, as a player's first start and a run per region and engine: each library loaded on Android
# 13, the game itself played to a set vblank on the current one; the arm64-v8a APK's Java and
# manifest must be the x86_64 one's). That needs the emulator, KVM and the system images the
# tool names; a failed or impossible start writes nothing to --out. --no-start-check skips it, for a
# build that is not handed out. The signing certificate's SHA-256 digest is printed last, one line
# per APK.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
die() { echo "build-android-release.sh: $*" >&2; exit 1; }

DISCS= CACHE= VERSION= OUT= START_CHECK=1
while [ $# -gt 0 ]; do
    case "$1" in
    --discs) DISCS="$2"; shift ;;
    --cache) CACHE="$2"; shift ;;
    --version) VERSION="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --no-start-check) START_CHECK=0 ;;
    -h | --help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option $1" ;;
    esac
    shift
done
[ -n "$DISCS" ] && [ -n "$CACHE" ] && [ -n "$VERSION" ] && [ -n "$OUT" ] || die "--discs, --cache, --version and --out are all needed"
[[ "$VERSION" =~ ^(20[0-9][0-9])\.(0[1-9]|1[0-2])\.([0-9]{1,3})$ ]] || die "--version $VERSION: not YYYY.MM.N"
CODE=$(( 10#${BASH_REMATCH[1]} * 100000 + 10#${BASH_REMATCH[2]} * 1000 + 10#${BASH_REMATCH[3]} ))
missing=()
for name in XG_ANDROID_KEYSTORE XG_ANDROID_KEY_ALIAS XG_ANDROID_KEYSTORE_PASSWORD XG_ANDROID_KEY_PASSWORD; do
    [ -n "${!name:-}" ] || missing+=("$name")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "build-android-release.sh: no signing key (not set: ${missing[*]}): no APK built" >&2
    exit 3
fi
[ -d "$DISCS" ] || die "--discs $DISCS: not a folder"
mkdir -p "$CACHE" "$OUT"
DISCS="$(cd "$DISCS" && pwd)" CACHE="$(cd "$CACHE" && pwd)" OUT="$(cd "$OUT" && pwd)"
names=()
for abi in arm64-v8a x86_64; do
    names+=("xenogears-pc-$VERSION-android-$abi.apk" "xenogears-pc-$VERSION-android-$abi.apk.sha256")
done
for name in "${names[@]}"; do
    [ ! -e "$OUT/$name" ] || die "--out $OUT already holds $name"
done
cd "$ROOT"

# The Python environment of requirements.txt, kept in the cache by the file's hash.
want=$(sha256sum requirements.txt | cut -c1-16)
venv="$CACHE/android-venv-$want"
if [ ! -x "$venv/bin/python" ]; then
    rm -rf "$venv.new"
    python3 -m venv "$venv.new"
    "$venv.new/bin/pip" install --disable-pip-version-check -q -r requirements.txt
    mv "$venv.new" "$venv"
fi
ln -sfn "$venv" .venv
PY=.venv/bin/python
"$PY" -I -B tools/deps.py fetch --cache "$CACHE"
export XG_DEPS_CACHE="$CACHE"

# Both regions' Disc 1, where disc.yaml names it (bin/<Redump folder>/<image>.bin).
declare -A disc1
for region in us jp; do
    image=$(sed -n 's|^image: bin/||p' "config/$region/disc.yaml")
    [ -n "$image" ] || die "config/$region/disc.yaml names no image"
    [ -f "$DISCS/$image" ] || die "no $region disc image: $DISCS/$image"
    "$PY" -B tools/extract.py "$region" --image "$DISCS/$image" --check-image </dev/null
    disc1[$region]="$DISCS/$image"
done
"$PY" -B tools/configure.py us jp </dev/null
"$PY" -B tools/pc_sources.py </dev/null

# --updater: the opt-in update check and install, in every release since the first canonical one
# (port/CMakeLists.txt XG_UPDATER; the INTERNET and REQUEST_INSTALL_PACKAGES permissions)
bash tools/build_android.sh --release --updater --version-name "$VERSION" --version-code "$CODE"

apks=build/android/gradle/app/outputs/apk/release
# Started before anything is written to --out (xg-o1dx): a release's APK once died in its activity's
# onCreate on every device, and every check above looks at files only.
if [ "$START_CHECK" = 1 ]; then
    "$PY" -B tools/android_start_check.py --apk "$apks/app-x86_64-release.apk" \
        --twin "$apks/app-arm64-v8a-release.apk" --disc us="${disc1[us]}" --disc jp="${disc1[jp]}" \
        || die "the APKs did not pass the start check (tools/android_start_check.py above); nothing written to --out"
else
    echo "build-android-release.sh: --no-start-check: the APKs were not started; not for handing out" >&2
fi
apksigner="${ANDROID_HOME:-$HOME/Android/sdk}/build-tools/36.1.0/apksigner"
for abi in arm64-v8a x86_64; do
    name="xenogears-pc-$VERSION-android-$abi.apk"
    cp "$apks/app-$abi-release.apk" "$OUT/$name"
    (cd "$OUT" && sha256sum "$name" > "$name.sha256")
done
for abi in arm64-v8a x86_64; do
    name="xenogears-pc-$VERSION-android-$abi.apk"
    digest=$("$apksigner" verify --print-certs "$OUT/$name" | sed -n 's/^Signer #1 certificate SHA-256 digest: //p')
    [ -n "$digest" ] || die "$name: apksigner printed no certificate digest"
    echo "$name: versionName $VERSION, versionCode $CODE, certificate SHA-256 $digest"
done
