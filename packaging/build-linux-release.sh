#!/usr/bin/env bash
# Build inside the cached Ubuntu root filesystem with a private source tree.
set -euo pipefail

tag="$1"
archive="$2"
source=/work/source
build=/work/build
out=/work/out

python3 -I -B "$source/packaging/deb_ubuntu.py" --source "$source" --build-dir "$build/original" \
    --tag "$tag" --out-dir "$out"

# Start with the exact Original engine and licences already audited in the DEB.
dpkg-deb --extract "$out/$archive" "$build/stage"
cmake -S "$source/port" -B "$build/enhanced" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DXG_VERSION=US -DXG_ENGINE=enhanced -DXG_EXTRAS=ON -DXG_PACKAGE_VERSION="${tag#v}"
cmake --build "$build/enhanced" --target xenogears
cmake --install "$build/enhanced" --prefix "$build/stage/usr" --strip
python3 -I -B "$source/packaging/linux_release.py" package --tag "$tag" --stage "$build/stage/usr" \
    --out-dir "$out" --ver us --build "$build/original" --enhanced-build "$build/enhanced"
