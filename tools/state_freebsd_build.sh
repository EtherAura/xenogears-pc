#!/bin/sh
# Build the native save-state gate package from a tagged source archive.
set -eu

source_archive=$1
version=$2
retail=$3
root=$4
jobs=$5
export PATH="${XG_FREEBSD_TOOLS:-/usr/local/llvm19/bin}:/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin"

test "$(uname -sm)" = 'FreeBSD amd64'
test -d "$retail/disc"
test -x "$retail/.venv/bin/python"
mkdir -p "$root/source" "$root/out"
cd "$root/source"
if [ ! -f "$root/.configured" ]; then
    tar -xzf "$source_archive" --strip-components 1
    ln -sfn "$retail/.venv" .venv
    ln -sfn "$retail/tools/toolchain" tools/toolchain
    ln -sfn "$retail/disc" disc
    .venv/bin/python tools/configure.py
    touch "$root/.configured"
fi

stage="$root/package"
metadata="$root/metadata"
mkdir -p "$metadata"
plist="$metadata/plist"
: > "$plist"
for region in us jp; do
    upper=$(printf '%s' "$region" | tr '[:lower:]' '[:upper:]')
    prefix="usr/local/libexec/xenogears-pc/$region"
    mkdir -p "$stage/$prefix"
    for engine in original enhanced; do
        name=xenogears
        if [ "$engine" = enhanced ]; then name=xenogears-enhanced; fi
        build="$root/build-$upper-$engine"
        cmake -S port -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION="$upper" \
            -DXG_ENGINE="$engine" -DXG_DIST=ON -DXG_STATE_SCHEMA=ON \
            -DXG_SCHEMA_GCC="${XG_FREEBSD_GCC:-/usr/local/bin/gcc14}" -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
        cmake --build "$build" --parallel "$jobs" --target "$name"
        install -m 0755 "$build/$name" "$stage/$prefix/$name"
        llvm-strip --strip-debug "$stage/$prefix/$name"
        install -m 0644 "$build/$name.xgschema" "$stage/$prefix/$name.xgschema"
        .venv/bin/python tools/state_schema.py --verify "$stage/$prefix/$name.xgschema" \
            --image "$stage/$prefix/$name" --stripped --portable
        printf '/%s/%s\n/%s/%s.xgschema\n' "$prefix" "$name" "$prefix" "$name" >> "$plist"
    done
done

cat > "$metadata/+MANIFEST" <<EOF
name: xenogears-pc
version: $version
origin: games/xenogears-pc
comment: Xenogears native state compatibility build
desc: Xenogears native executables and schemas for release validation against retail discs
maintainer: EtherAura
www: https://github.com/EtherAura/xenogears-pc
prefix: /usr/local
EOF
pkg create -r "$stage" -m "$metadata" -p "$plist" -o "$root/out"
name="xenogears-pc-$version-freebsd15-amd64.pkg"
mv -f "$root/out/xenogears-pc-$version.pkg" "$root/out/$name"
cd "$root/out"
digest=$(/sbin/sha256 -q "$name")
printf '%s  %s\n' "$digest" "$name" > "$name.sha256"
