#!/usr/bin/env bash
# Both engines for one region, built inside the cached Ubuntu root filesystem
# and installed, stripped, into /work/build/<region>-stage: the executables the
# release's packages hold, without the packaging.
#
#   build-linux-executables.sh <us|jp> <tag> [--write-tables]
#
# The configuration is the release's (packaging/deb_ubuntu.py, build-linux-release.sh).
# The link reads the PS1 split where the source tree has one (asm/<region>, from
# a disc); with --write-tables such a build leaves its link tables in each build
# folder (tools/link_tables.py gathers them). A tree without a split links from
# port/link-tables, and no disc is read (port/CMakeLists.txt XG_LINK_FROM).
set -euo pipefail

region="$1"
tag="$2"
tables=()
[ "${3:-}" = --write-tables ] && tables=(-DXG_WRITE_LINK_TABLES=ON)
source=/work/source
build=/work/build

for engine in original enhanced; do
    cmake -S "$source/port" -B "$build/$region-$engine" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DXG_VERSION="${region^^}" -DXG_ENGINE="$engine" -DXG_EXTRAS=ON -DXG_PACKAGE_VERSION="${tag#v}" "${tables[@]}"
    cmake --build "$build/$region-$engine" --target xenogears
    cmake --install "$build/$region-$engine" --prefix "$build/$region-stage" --strip
done
