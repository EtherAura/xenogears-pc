#!/usr/bin/env python3
"""The stored-deflate PNG readers under ASan and UBSan (xg-jr5): pngDecodeStored
and pngDecodeStoredGray (port/src/gpu/pngfast.c), which community packs' PNGs
reach (texture-pack index maps, movie packs). CPU only.

    python tools/test_pngfast_fuzz.py --ver us|jp [--seed N] [--iterations N]
    python tools/test_pngfast_fuzz.py --ver us --control KIND   (one negative control)
    python tools/test_pngfast_fuzz.py --ver us --replay FILE    (one file, both readers)

1. Builds port/src/system/test/pngfastfuzz.c with the real pngfast.c (and
   settings.c, engine.c, cputime.c, which pngfast.c links against, as the
   texture-pack tests in port/src/gpu/gpu.cmake do) by the host C compiler,
   -fsanitize=address,undefined -fno-sanitize-recover=all, into a temporary
   directory under TMPDIR (removed afterwards), and runs it: directed cases,
   then a fixed-seed mutation campaign over both readers.
2. Negative controls: the same harness built with PNGFASTFUZZ_CONTROL (a read
   past a decoded image, a signed overflow, an abort, a failing expectation,
   a decoded image never freed, a hang) must each be rejected by the same
   checks; a control that passes fails the run (the sanitizers would be
   doing nothing).

A run fails on a compiler error, a missing sanitizer runtime, a timeout, a
signal, a sanitizer report, a failed expectation or a missing PASS line.
When a sanitizer stops the real run, the input is kept as a reproducer in
work/pngfastfuzz/ (replay it with --replay). pngfast.c is shared PC code:
--ver picks the VERSION_US or VERSION_JP build as the port's CMake does, and
both versions run the same readers; no game engine runs. Exit 0 when
everything held.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port/src"
HARNESS = SRC / "system/test/pngfastfuzz.c"
SOURCES = [SRC / "gpu/pngfast.c", SRC / "system/settings.c", SRC / "system/engine.c", SRC / "system/cputime.c"]
REPRO_DIR = ROOT / "work/pngfastfuzz"

SEED = 0x6A7235  # "jr5"
ITERATIONS = 40000
TIMEOUT = 600  # seconds for the real run
CONTROL_TIMEOUT = 30  # ... and for each control ("hang" waits this long)
COMPILE_TIMEOUT = 300

# name: (PNGFASTFUZZ_CONTROL, what the checks must report)
CONTROLS = {
    "asan-overread": (1, "sanitizer report"),
    "ubsan-overflow": (2, "sanitizer report"),
    "abort": (3, "signal"),
    "expectation": (4, "exit"),
    "leak": (5, "sanitizer report"),
    "hang": (6, "timeout"),
}

SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
SANITIZER_MARKS = ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:",
                   "SUMMARY: UndefinedBehaviorSanitizer", "SUMMARY: AddressSanitizer")


def sanitizer_env() -> dict[str, str]:
    """Inherited sanitizer options are replaced, never merged: nothing outside can relax them."""
    env = {k: v for k, v in os.environ.items() if not k.endswith("SAN_OPTIONS")}
    env["ASAN_OPTIONS"] = ("halt_on_error=1:abort_on_error=0:detect_leaks=1:allocator_may_return_null=0:"
                           "detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:"
                           "exitcode=86")
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1:exitcode=87"
    return env


def compiler() -> str | None:
    cc = os.environ.get("CC")
    if cc:
        return shutil.which(cc)
    for name in ("cc", "gcc", "clang"):
        path = shutil.which(name)
        if path:
            return path
    return None


def sdl_flags() -> tuple[list[str], list[str]]:
    pc = shutil.which("pkg-config")
    if pc:
        cflags = subprocess.run([pc, "--cflags", "sdl3"], capture_output=True, text=True, timeout=30)
        libs = subprocess.run([pc, "--libs", "sdl3"], capture_output=True, text=True, timeout=30)
        if cflags.returncode == 0 and libs.returncode == 0:
            # include paths and defines only: SDL's own flags carry -fno-strict-overflow, which makes
            # signed overflow defined and so invisible to UBSan (the ubsan-overflow control caught it)
            keep = [f for f in cflags.stdout.split() if f.startswith(("-I", "-D"))]
            return keep, libs.stdout.split()
    return [], ["-lSDL3"]


def build(cc: str, out: Path, ver: str, control: int = 0) -> tuple[bool, str, list[str]]:
    cflags, libs = sdl_flags()
    cmd = [cc, "-std=gnu11", "-O1", "-g", *SANITIZE, "-Wall", "-Wextra", "-DPLATFORM_PC", f"-DVERSION_{ver.upper()}",
           f"-DPNGFASTFUZZ_CONTROL={control}", "-I", str(SRC), *cflags, str(HARNESS), *map(str, SOURCES),
           "-o", str(out), *libs]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=COMPILE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, "compiler timed out", cmd
    if r.returncode != 0 or not out.exists():
        return False, f"compiler failed (exit {r.returncode}):\n{r.stderr[-4000:]}", cmd
    return True, r.stderr, cmd


def run(binary: Path, args: list[str], timeout: int) -> tuple[str | None, str, str, int | None]:
    """(None when it held, else why; stdout; stderr; exit status)."""
    try:
        r = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=timeout,
                           env=sanitizer_env(), stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        err = e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
        return f"timeout after {timeout} s", out, err, None
    if any(mark in r.stderr for mark in SANITIZER_MARKS):
        return f"sanitizer report (exit {r.returncode})", r.stdout, r.stderr, r.returncode
    if r.returncode < 0:
        return f"signal {-r.returncode}", r.stdout, r.stderr, r.returncode
    if r.returncode != 0:
        return f"exit {r.returncode}", r.stdout, r.stderr, r.returncode
    if not any(line.strip() == "pngfastfuzz: PASS" for line in r.stdout.splitlines()):
        return "no PASS line", r.stdout, r.stderr, r.returncode
    if "FAIL" in r.stderr:
        return "FAIL lines on stderr", r.stdout, r.stderr, r.returncode
    return None, r.stdout, r.stderr, r.returncode


def tail(text: str, lines: int = 25) -> str:
    return "\n".join(text.strip().splitlines()[-lines:])


def run_control(cc: str, tmp: Path, ver: str, name: str) -> tuple[bool, str]:
    """(rejected as expected, what the checks said)."""
    number, want = CONTROLS[name]
    binary = tmp / f"pngfastfuzz-control-{name}"
    ok, msg, _ = build(cc, binary, ver, number)
    if not ok:
        return False, msg
    why, out, err, _ = run(binary, [str(SEED), "10", str(tmp / f"control-{name}.png")], CONTROL_TIMEOUT)
    if why is None:
        return False, "PASSED: the checks did not see a deliberate defect"
    return why.startswith(want), f"{why}\n{tail(err, 8)}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", required=True, choices=["us", "jp"])
    ap.add_argument("--seed", type=lambda s: int(s, 0), default=SEED)
    ap.add_argument("--iterations", type=int, default=ITERATIONS)
    ap.add_argument("--control", choices=sorted(CONTROLS), help="build and run one negative control only")
    ap.add_argument("--replay", type=Path, help="one file through both readers")
    args = ap.parse_args()
    if not (ROOT / "config" / args.ver).is_dir():
        print(f"FAIL  no config/{args.ver}: not a version of this repository")
        return 2
    if not 1 <= args.iterations <= 1000000:
        print("FAIL  --iterations must be 1 to 1000000")
        return 2
    cc = compiler()
    if cc is None:
        print("FAIL  no C compiler (CC, cc, gcc, clang)")
        return 2
    base = os.environ.get("TMPDIR") or None
    with tempfile.TemporaryDirectory(prefix="pngfastfuzz-", dir=base) as tmpname:
        tmp = Path(tmpname)
        if args.control:
            print(f"NEGATIVE CONTROL {args.control} (deliberately broken harness; the checks must reject it)")
            number, _ = CONTROLS[args.control]
            binary = tmp / "pngfastfuzz-control"
            ok, msg, _ = build(cc, binary, args.ver, number)
            if not ok:
                print(f"FAIL  {msg}")
                return 2
            why, out, err, _ = run(binary, [str(args.seed), "10", str(tmp / "control.png")], CONTROL_TIMEOUT)
            print(tail(err, 20))
            print(f"REJECTED  {why}" if why else "ACCEPTED (the checks missed a deliberate defect)")
            return 1
        binary = tmp / "pngfastfuzz"
        ok, msg, cmd = build(cc, binary, args.ver)
        if not ok:
            print(f"FAIL  build: {msg}")
            return 1
        print("built: " + " ".join(str(c).replace(str(ROOT) + "/", "").replace(str(tmp), "$TMP") for c in cmd))
        if msg.strip():
            print("compiler warnings:\n" + tail(msg, 20))
        if args.replay:
            why, out, err, _ = run(binary, ["replay", str(args.replay.resolve())], TIMEOUT)
            print(out.strip())
            if why:
                print(tail(err, 40))
                print(f"FAIL  replay: {why}")
                return 1
            return 0
        repro = tmp / "repro.png"
        why, out, err, _ = run(binary, [str(args.seed), str(args.iterations), str(repro)], TIMEOUT)
        print(out.strip())
        failures = 0
        if why:
            failures += 1
            print(tail(err, 60))
            print(f"FAIL  harness ({args.ver}): {why}")
            if repro.exists():
                REPRO_DIR.mkdir(parents=True, exist_ok=True)
                kept = REPRO_DIR / f"{args.ver}-seed{args.seed:x}.png"
                shutil.copyfile(repro, kept)
                print(f"      reproducer: {kept.relative_to(ROOT)}"
                      f"  (python tools/test_pngfast_fuzz.py --ver {args.ver} --replay {kept.relative_to(ROOT)})")
        else:
            print(f"ok    harness ({args.ver}): ASan+UBSan clean, seed 0x{args.seed:x}, {args.iterations} iterations")
        print("negative controls (deliberately broken harness builds; each must be rejected):")
        for name in CONTROLS:
            rejected, said = run_control(cc, tmp, args.ver, name)
            first = said.splitlines()[0] if said else ""
            if rejected:
                print(f"ok    control {name}: rejected ({first})")
            else:
                failures += 1
                print(f"FAIL  control {name}: wanted {CONTROLS[name][1]}, got: {said}")
    print("pngfastfuzz: " + ("FAIL" if failures else "PASS") + f" ({args.ver})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
