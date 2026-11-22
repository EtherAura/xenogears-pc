#!/usr/bin/env python3
"""Link the macOS module check on Linux, as the public workflow does on Apple Silicon.

The "macOS module reset" workflow builds darwin_module_check
(port/cmake/darwin-module-check.cmake): field and battle linked as modules
through tools/pc_srcdata.py and Apple's ld-classic, beside
port/src/system/memory.c and the reset test, which
port/src/system/test/macos_modules/game_check.py then checks and runs. This
does the same here with ld64 955.13, the source of the runner's ld-classic,
built by tools/build_ld64.sh:

  1. field's and battle's objects for arm64-apple-macos15 (tools/darwin_plans.py's build);
  2. each module's generated object, and the module link from its committed plan;
  3. game_check.py imports: the export lists, the generated aliases, the import stand-ins;
  4. the executable, linked against a text stub of libSystem (tools/darwin/sdk:
     declarations only, no Apple SDK);
  5. game_check.py verify --link-only: the aliases in the executable, no source
     window in it.

It stops where the runner starts the executable: Linux cannot run it (nor
codesign it), so the reset itself is checked only there. Up to that point it
prints the runner's PASS lines.

    python tools/darwin_link.py        # the gate (tools/gates.sh: darwin-link)

XG_DARWIN_LD names the linker (default: tools/build_ld64.sh's, under
$XG_DARWIN_LD_DIR or ~/.cache/xg-darwin-ld); without it, or without a clang
that targets AArch64 (XG_DARWIN_CC), the check is skipped. The build is
tools/darwin_plans.py's (build/pc/darwin-plans; --build overrides).
"""

from __future__ import annotations

import argparse
import fcntl
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import darwin_plans
from darwin_plans import META, MODULES, ROOT, TARGET

NAME = "darwin-link"
TESTS = META.parent  # port/src/system/test/macos_modules
SDK = ROOT / "tools/darwin/sdk"
LD_DIR = Path(os.environ.get("XG_DARWIN_LD_DIR") or Path.home() / ".cache/xg-darwin-ld")


def linker() -> Path | None:
    ld = Path(os.environ.get("XG_DARWIN_LD") or LD_DIR / "prefix/bin/aarch64-apple-darwin-ld")
    return ld if ld.is_file() and os.access(ld, os.X_OK) else None


def beside(cc: str, tool: str) -> str | None:
    """An LLVM tool next to the clang, else on PATH."""
    near = Path(shutil.which(cc) or cc).resolve().parent / tool
    return str(near) if near.is_file() else shutil.which(tool)


class Steps:
    """Each command's output goes to the log; a failure ends the check with its last lines."""

    def __init__(self, log: Path):
        self.log = log
        log.write_text("")

    def run(self, what: str, cmd: list, env: dict | None = None) -> subprocess.CompletedProcess:
        r = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True,
                           env=dict(os.environ, **(env or {})))
        with self.log.open("a") as f:
            f.write("$ " + " ".join(map(str, cmd)) + "\n" + r.stdout + r.stderr)
        if r.returncode:
            tail = (r.stdout + r.stderr).strip().splitlines()[-12:]
            print(f"{NAME}: {what} failed (log: {self.log}):\n" + "\n".join("  " + line for line in tail))
            sys.exit(1)
        return r


def final_link(steps: Steps, cc: str, ld: Path, out: Path) -> None:
    """darwin_module_check: the reset test, the platform's memory code, the bounds and the
    two modules with their restore tables, against a libSystem stub that exports what they use."""
    objs = []
    (out / "final").mkdir(exist_ok=True)
    for src in (out / "gen/imports.s", TESTS / "game_reset.c", ROOT / "port/src/system/memory.c",
                META / "bounds.c", out / "gen/field.src.c", out / "gen/battle.src.c"):
        obj = out / "final" / (src.name + ".o")
        steps.run(f"compiling {src.name}", [cc, f"--target={TARGET}", "-ffreestanding", "-isystem", SDK / "include",
                                            "-fPIC", "-c", src, "-o", obj])
        objs.append(obj)
    objs += [out / "modules/field.o", out / "modules/battle.o"]
    tbd = out / "sdk/libSystem.tbd"
    tbd.parent.mkdir(exist_ok=True)

    def link(symbols: set[str]) -> subprocess.CompletedProcess:
        tbd.write_text("--- !tapi-tbd\ntbd-version: 4\ntargets: [ arm64-macos ]\n"
                       "install-name: '/usr/lib/libSystem.B.dylib'\nexports:\n  - targets: [ arm64-macos ]\n"
                       "    symbols: [ " + ", ".join(sorted(symbols)) + " ]\n...\n")
        cmd = [ld, "-arch", "arm64", "-platform_version", "macos", "15.0", "15.0", "-L", tbd.parent,
               "-lSystem", "-e", "_main", "-o", out / "darwin_module_check", *objs]
        r = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True)
        with steps.log.open("a") as f:
            f.write("$ " + " ".join(map(str, cmd)) + "\n" + r.stdout + r.stderr)
        return r

    # The stub exports what the first link finds undefined: libSystem's C library, which the
    # reset test and memory.c use. Anything else undefined fails the second link.
    r = link({"dyld_stub_binder"})
    if r.returncode:
        wanted = set(re.findall(r'^  "(\w+)", referenced from', r.stderr, re.M))
        r = link({"dyld_stub_binder"} | wanted)
    if r.returncode:
        tail = r.stderr.strip().splitlines()[-12:]
        print(f"{NAME}: the final link failed (log: {steps.log}):\n" + "\n".join("  " + line for line in tail))
        sys.exit(1)
    print("final link: darwin_module_check (arm64 Mach-O; not run here)")


def check(cc: str, ld: Path, out: Path) -> int:
    nm = beside(cc, "llvm-nm")
    if not nm:
        print(f"{NAME}: no llvm-nm beside {cc} or on PATH (game_check.py reads the modules' symbols with it)")
        return 1
    objects = darwin_plans.build_objects(cc, out)
    steps = Steps(out / "darwin-link.log")
    (out / "modules").mkdir(exist_ok=True)
    (out / "gen").mkdir(exist_ok=True)
    for m in MODULES:
        gen = out / "gen" / f"{m}.c.o"
        # gen_<module> of xg_link_module (port/cmake/game-modules.cmake), with the PIC options of macOS.
        steps.run(f"compiling {m}'s generated object",
                  [cc, f"--target={TARGET}", "-w", "-fno-data-sections", "-fno-common", "-fPIC",
                   "-fno-semantic-interposition", "-c", META / f"{m}.c", "-o", gen])
        r = steps.run(f"linking {m}",
                      [sys.executable, ROOT / "tools/pc_srcdata.py", "--ver", "us", "--ld", ld,
                       "--macho-exports", META / f"{m}.exports", "--plan", META / f"{m}.json", m,
                       out / "modules" / f"{m}.o", out / "gen" / f"{m}.src.c", out / "gen" / f"{m}.srcsyms",
                       gen, *objects[m]])
        print(*[line for line in r.stdout.splitlines() if line.startswith("pc_srcdata")], sep="\n")
    game_check = [sys.executable, TESTS / "game_check.py"]
    r = steps.run("game_check.py imports", [*game_check, "imports", out], env={"XG_NM": nm})
    print(r.stdout.rstrip())
    final_link(steps, cc, ld, out)
    r = steps.run("game_check.py verify", [*game_check, "verify", out, "--link-only"], env={"XG_NM": nm})
    print(r.stdout.rstrip())
    version = subprocess.run([str(ld), "-v"], capture_output=True, text=True).stderr.splitlines()
    project = next((v.split("PROJECT:")[1] for v in version if "PROJECT:" in v), "ld64")
    print(f"{NAME}: field and battle link with {project} and pass the runner's checks up to running "
          f"darwin_module_check, which only macOS can")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", type=Path, default=ROOT / "build/pc/darwin-plans")
    a = ap.parse_args()
    cc, ld = darwin_plans.darwin_clang(), linker()
    if cc is None or ld is None:
        missing = "a clang that targets AArch64 (XG_DARWIN_CC)" if cc is None else "ld64 (tools/build_ld64.sh)"
        print(f"{NAME}: skipped: no {missing}")
        return 0
    out = a.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    with (out / ".lock").open("w") as lock:  # one build of this folder at a time (tools/darwin_plans.py too)
        fcntl.flock(lock, fcntl.LOCK_EX)
        return check(cc, ld, out)


if __name__ == "__main__":
    sys.exit(main())
