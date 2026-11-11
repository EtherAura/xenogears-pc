#!/usr/bin/env bash
# Run every gate the PC port keeps green, for US and JP, and print one summary.
#
#   tools/gates.sh              # every gate
#   tools/gates.sh --no-ninja   # skip the PS1 build (it was just run)
#   tools/gates.sh --no-tests   # skip the port's unit tests
#
# Gates:
#   ninja       the PS1 build: every image hash-matches retail (run under
#               $XG_BUILD_LOCK, default work/agents/build.lock, so parallel
#               agents never build at once)
#   check_bss   C statics at their yaml addresses (sha1 can't see it)
#   hostcheck   64-bit hazards in the game C (per version)
#   declcheck   one symbol declared with different types (per version)
#   heapsizes   literal sizes of types that grow on the PC: 'grows' sites
#               fail, 'partial'/'mismatch' are leads and only reported
#   argorder    call arguments that depend on evaluation order (per version)
#   cuttables   `extern T x[];` tables cut at a label no C names (per version)
#   busywait    game loops that can go round without a safe point (US+JP):
#               each gets PORT_YIELD or a reason in tools/busywait_allow.txt
#   port-tests  the port's unit tests (tools/port_tests.py) in the configured
#               build $XG_TEST_BUILD (default build/pc/gates, configured on
#               first use: US Release with extras; never the user's launcher
#               build/pc/release); skipped when an explicit one is missing
#   uninit      possibly uninitialised locals in the game C (clang and gcc -O2,
#               US+JP): each gets a PC fix or a reason in tools/uninit_allow.txt
#   scratchpad  PS1 scratchpad addresses (0x1F800000-0x1F800FFF) in C only
#               through PSX_SCRATCH (include/common.h; tools/scratchpadcheck.py)
#   xmodcheck   cross-module data references the PC link would split into
#               separate copies (names differ between modules; per version)
#   statevars   every writable static of the platform layer in $XG_TEST_BUILD
#               is classified for save states (port/src/system/
#               savestate_inventory.txt; tools/statecheck.py inventory):
#               skipped when there is no configured build
#   payload     the executable in $XG_TEST_BUILD holds no retail module data
#               (tools/payloadcheck.py; port code that matches by coincidence or
#               is SDK data has a reason in tools/payload_allow.txt); skipped
#               when there is no configured build
#   port-jp     the JP executable links in $XG_TEST_BUILD_JP (default
#               build/pc/gates-jp, configured on first use like the US one):
#               tools/pc_link.py with the JP symbol files and layouts, which the
#               US build never runs; skipped when an explicit one is missing
#   payload-jp  the payload gate on that JP executable
#   payload-android  the payload gate on each Android game library built in
#               build/android (tools/build_android.sh); skipped when none is
#   beetle-capture frame-boundary capture regressions, without a core or discs
#   retail-heap retail heap address-model and read-only inspection regressions
#   interpolation-coverage sampled motion evidence and honest coverage reports
#   payload-tests synthetic PE/ELF payload attribution and rejection regressions
#   port-runner filtered fixtures and concurrent output ownership regressions
#   state-runner reject crashes even when state/pixel/audio captures were written
#
# Full outputs go to build/gates/<gate>.txt ($XG_GATES_OUT overrides the
# directory, so parallel agents keep their own). The exit status is the number of
# failed gates.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
PY="$ROOT/.venv/bin/python"
[ -x "$PY" ] || PY=python3
OUT="${XG_GATES_OUT:-$ROOT/build/gates}"
LOCK="${XG_BUILD_LOCK:-$ROOT/work/agents/build.lock}"
mkdir -p "$OUT" "$(dirname "$LOCK")"

NINJA=1
TESTS=1
TEST_BUILD="${XG_TEST_BUILD:-build/pc/gates}"
TEST_BUILD_JP="${XG_TEST_BUILD_JP:-build/pc/gates-jp}"
for a in "$@"; do
    case "$a" in
    --no-ninja) NINJA=0 ;;
    --no-tests) TESTS=0 ;;
    -h | --help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "gates.sh: unknown option $a" >&2; exit 2 ;;
    esac
done

failed=0
summary=()

# record NAME STATUS DETAIL
record() {
    local mark=ok
    if [ "$2" != 0 ]; then
        mark=FAIL
        failed=$((failed + 1))
    fi
    summary+=("$(printf '%-4s  %-14s %s' "$mark" "$1" "$3")")
}

# gate NAME CMD...: run, keep the output; the detail is the tool's summary
# line (it starts with the tool's name), else the last line.
gate() {
    local name=$1
    shift
    "$@" > "$OUT/$name.txt" 2>&1
    local st=$?
    local line
    line=$(grep -m1 "^${name%-[uj][sp]}" "$OUT/$name.txt" || grep -v '^\s*$' "$OUT/$name.txt" | tail -1)
    record "$name" "$st" "$line"
}

if [ "$NINJA" = 1 ]; then
    if command -v ninja > /dev/null && [ -f build.ninja ]; then
        flock "$LOCK" ninja > "$OUT/ninja.txt" 2>&1
        st=$?
        if [ "$st" = 0 ]; then
            # A second run must have nothing left to do.
            flock "$LOCK" ninja > "$OUT/ninja2.txt" 2>&1
            grep -q "no work to do" "$OUT/ninja2.txt" || st=1
        fi
        n=$(grep -c '^build .*: check ' build.ninja)
        if [ "$st" = 0 ]; then
            record ninja 0 "$n images match retail"
        else
            record ninja 1 "$(grep -m3 -E 'FAILED|WARNING|mismatch|did not' "$OUT/ninja.txt" | tr '\n' ' ')"
        fi
    else
        record ninja 1 "no build.ninja (run tools/configure.py)"
    fi
fi

gate check_bss "$PY" tools/check_bss.py
gate busywait "$PY" tools/busywait.py
gate uninit "$PY" tools/uninit.py
gate scratchpad "$PY" tools/scratchpadcheck.py

for ver in us jp; do
    gate "hostcheck-$ver" "$PY" tools/hostcheck.py --ver "$ver"
    gate "declcheck-$ver" "$PY" tools/declcheck.py --ver "$ver"

    "$PY" tools/heapsizes.py --ver "$ver" > "$OUT/heapsizes-$ver.txt" 2>&1
    st=$?
    line=$(grep '^heapsizes ' "$OUT/heapsizes-$ver.txt" | tail -1)
    grows=$(sed -n 's/.*grows \([0-9]*\).*/\1/p' <<< "$line")
    [ "$st" = 0 ] && [ "${grows:-0}" != 0 ] && st=1
    [ -z "$line" ] && st=1 && line=$(tail -1 "$OUT/heapsizes-$ver.txt")
    record "heapsizes-$ver" "$st" "${line#*; }"

    gate "argorder-$ver" "$PY" tools/argorder.py --ver "$ver"
    gate "cuttables-$ver" "$PY" tools/cuttables.py --ver "$ver"
    gate "xmodcheck-$ver" "$PY" tools/xmodcheck.py --ver "$ver"
done

# The default test build is the gates' own (the tests build the executable
# too, and build/pc/release is the user's launcher).
if [ "$TESTS" = 1 ] && [ -z "${XG_TEST_BUILD:-}" ] && [ ! -f "$TEST_BUILD/CMakeCache.txt" ]; then
    "$PY" tools/pc_sources.py > /dev/null &&
        cmake -S port -B "$TEST_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON \
            > "$OUT/configure.txt" 2>&1
fi

if [ "$TESTS" = 1 ]; then
    gate beetle-capture "$PY" tools/test_beetle.py
    gate retail-heap "$PY" tools/test_retail_heap.py
    gate interpolation-coverage "$PY" tools/test_interpolation_coverage.py
    gate payload-tests "$PY" tools/test_payloadcheck.py
    gate port-runner "$PY" tools/test_port_tests.py
    gate state-runner "$PY" tools/test_statecheck.py
    if [ -f "$TEST_BUILD/CMakeCache.txt" ]; then
        gate port-tests "$PY" tools/port_tests.py --build "$TEST_BUILD"
    else
        record port-tests 0 "skipped: no configured build in $TEST_BUILD"
    fi
fi

if [ -f "$TEST_BUILD/CMakeCache.txt" ]; then
    gate payload "$PY" tools/payloadcheck.py --build "$TEST_BUILD"
    gate statevars "$PY" tools/statecheck.py inventory "$TEST_BUILD"
else
    record payload 0 "skipped: no configured build in $TEST_BUILD"
    record statevars 0 "skipped: no configured build in $TEST_BUILD"
fi

# The JP executable: pc_link with the JP symbol files and layouts (only a JP
# build runs it), then its payload.
if [ "$TESTS" = 1 ] && [ -z "${XG_TEST_BUILD_JP:-}" ] && [ ! -f "$TEST_BUILD_JP/CMakeCache.txt" ]; then
    "$PY" tools/pc_sources.py > /dev/null &&
        cmake -S port -B "$TEST_BUILD_JP" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=JP -DXG_EXTRAS=ON \
            > "$OUT/configure-jp.txt" 2>&1
fi
if [ "$TESTS" = 1 ] && [ -f "$TEST_BUILD_JP/CMakeCache.txt" ]; then
    cmake --build "$TEST_BUILD_JP" --target xenogears > "$OUT/port-jp.txt" 2>&1
    st=$?
    if [ "$st" = 0 ]; then
        record port-jp 0 "JP executable linked in ${TEST_BUILD_JP#"$ROOT"/}"
        gate payload-jp "$PY" tools/payloadcheck.py --build "$TEST_BUILD_JP"
    else
        line=$(grep -m1 '^pc_link:' "$OUT/port-jp.txt" || grep -m1 'error:' "$OUT/port-jp.txt" ||
            grep -m1 'FAILED' "$OUT/port-jp.txt")
        record port-jp 1 "$(cut -c1-200 <<< "$line")"
    fi
elif [ "$TESTS" = 1 ]; then
    record port-jp 0 "skipped: no configured build in $TEST_BUILD_JP"
fi

# The Android game libraries (tools/build_android.sh), when built in this tree: what an APK
# carries, checked as the Linux executable is.
android_libs=()
for lib in build/android/*-*/libxenogears-*.so; do
    [ -f "$lib" ] && android_libs+=("$lib")
done
if [ ${#android_libs[@]} -gt 0 ]; then
    : > "$OUT/payload-android.txt"
    st=0
    for lib in "${android_libs[@]}"; do
        echo "== $lib" >> "$OUT/payload-android.txt"
        "$PY" tools/payloadcheck.py --build "$(dirname "$lib")" --exe "$lib" >> "$OUT/payload-android.txt" 2>&1 || st=1
    done
    record payload-android "$st" "$(grep -c '^payload ok' "$OUT/payload-android.txt") of ${#android_libs[@]} Android libraries hold no retail module data"
else
    record payload-android 0 "skipped: no Android libraries in build/android (tools/build_android.sh)"
fi

echo "gates (full outputs in ${OUT#"$ROOT"/}/):"
printf '  %s\n' "${summary[@]}"
if [ "$failed" = 0 ]; then
    echo "all gates pass"
else
    echo "$failed gate(s) failed"
fi
exit "$failed"
