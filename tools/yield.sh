#!/usr/bin/env bash
# Run a build or a test run so that what the user runs meanwhile (the game
# above all) keeps the CPU: nice 19, the idle I/O class, and at most
# XG_YIELD_JOBS jobs side by side (default: a quarter of the CPUs, at least 2).
#
#   tools/yield.sh ninja
#   tools/yield.sh cmake --build build/pc/release
#   tools/yield.sh tools/gates.sh
#   tools/yield.sh python tools/port_tests.py --build build/pc/gates
#
# The cap is the part that frees CPUs most: a scheduler that judges tasks by how
# they behave (sched_ext LAVD, the default on CachyOS) gives nice little
# weight, and a game thread waiting for a CPU loses its vblank and drains
# the sound's queue. cmake --build and make read the cap from the environment
# (CMAKE_BUILD_PARALLEL_LEVEL, MAKEFLAGS); ninja gets -j here unless the
# command names one; tools/gates.sh and tools/port_tests.py read XG_YIELD_JOBS.
set -euo pipefail
[ $# -gt 0 ] || { sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
cpus=$(nproc 2>/dev/null || echo 4)
jobs=${XG_YIELD_JOBS:-$(( cpus / 4 > 2 ? cpus / 4 : 2 ))}
export XG_YIELD_JOBS=$jobs CMAKE_BUILD_PARALLEL_LEVEL=$jobs MAKEFLAGS="-j$jobs"
if [ "$(basename "$1")" = ninja ]; then
    named=0
    for a in "$@"; do case "$a" in -j*) named=1 ;; esac; done
    [ "$named" = 1 ] || set -- "$1" -j "$jobs" "${@:2}"
fi
# nice 19 itself, not 19 more: an agent's shell may start below 0 (an Electron
# host app raises its own main thread to -8, and every build it starts inherits it).
by=$(( 19 - $(nice) ))
if command -v ionice > /dev/null; then
    exec nice -n "$by" ionice -c 3 "$@"
fi
exec nice -n "$by" "$@"
