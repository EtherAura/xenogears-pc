#!/usr/bin/env python3
"""Check the macOS module check's plans against this tree, on Linux.

The "macOS module reset" workflow (.github/workflows/macos-modules.yml) links
US Original field and battle on Apple Silicon without a disc, from the plans in
port/src/system/test/macos_modules/us-original/<module>.json (tools/pc_srcdata.py
--plan). A plan lists every data section of every object it links, so a change
that adds, drops or edits data in a field or battle object leaves it stale and
the run stops ("its data sections are not the plan's (stale plan)"). Nothing
else reads these plans before a push. This builds the two modules' objects for
arm64-apple-macos15 with clang (port/cmake/darwin-module-check.cmake's cross
build: objects only, no Apple linker or SDK) and checks each plan against them
as the runner's link does, up to the link itself (pc_srcdata --plan-only).

    python tools/darwin_plans.py check              # the gate (tools/gates.sh: darwin-plans)
    python tools/darwin_plans.py write [--game DIR] # refresh the metadata from disc/us/, then check

The game C builds at -O0 without system headers, so these objects' data
sections are what Apple clang makes of them on the runner, and plans written
from them pass there. `write` is the metadata refresh of
port/src/system/test/macos_modules/prepare.py (plans, export lists, generated
aliases and bounds, each checked against both regions' retail modules),
given these objects and a US Original PC build of this tree (--game, default
build/pc/gates, the gates' own; it is built first). XG_DARWIN_CC names the
clang (default: clang on PATH); without one that targets AArch64, `check` is
skipped. The build is build/pc/darwin-plans (--build overrides).
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "port/src/system/test/macos_modules/us-original"
MODULES = ("field", "battle")
TARGET = "arm64-apple-macos15"
NAME = "darwin-plans"


def darwin_clang() -> str | None:
    """A clang that can target arm64 macOS, or None."""
    cc = os.environ.get("XG_DARWIN_CC") or shutil.which("clang")
    if not cc:
        return None
    try:
        targets = subprocess.run([cc, "-print-targets"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    return cc if "aarch64" in targets else None


def run_logged(cmd: list[str], log: Path, what: str) -> None:
    with log.open("a") as f:
        f.write("$ " + " ".join(cmd) + "\n")
        f.flush()
        st = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode
    if st:
        tail = log.read_text().splitlines()[-15:]
        sys.exit(f"{NAME}: {what} failed ({log.relative_to(ROOT) if log.is_relative_to(ROOT) else log}):\n"
                 + "\n".join("  " + line for line in tail))


def build_objects(cc: str, out: Path) -> dict[str, list[Path]]:
    """Field's and battle's game objects for arm64 macOS, in the module link's order."""
    out.mkdir(parents=True, exist_ok=True)
    log = out / "darwin-plans.log"
    log.write_text("")
    run_logged([sys.executable, str(ROOT / "tools/pc_sources.py")], log, "tools/pc_sources.py")
    if not (out / "CMakeCache.txt").is_file():
        run_logged(["cmake", "-S", str(ROOT / "port"), "-B", str(out), "-G", "Ninja",
                    "-DCMAKE_SYSTEM_NAME=Darwin", "-DCMAKE_SYSTEM_PROCESSOR=arm64",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",  # no SDK or Apple linker here
                    f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_C_COMPILER_TARGET={TARGET}",
                    "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DXG_DARWIN_MODULE_CHECK=ON",
                    "-DXG_VERSION=US", "-DXG_ENGINE=original", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
                   log, "configuring the arm64 macOS build")
    run_logged(["cmake", "--build", str(out), "--target", "darwin_module_inputs"], log,
               "building the field and battle objects")
    rows = json.loads((out / "compile_commands.json").read_text())
    # The compile database lists each object library's sources in their order,
    # which is $<TARGET_OBJECTS:game_<module>>'s; the plan's own order check
    # (pc_srcdata parse_plan) catches any difference.
    objects = {m: [Path(r["directory"]) / r["output"] for r in rows if f"CMakeFiles/game_{m}.dir/" in r["output"]]
               for m in MODULES}
    for m, objs in objects.items():
        if not objs:
            sys.exit(f"{NAME}: no game_{m} objects in {out / 'compile_commands.json'}")
    return objects


def srcdata(module: str, objs: list[Path], out: Path, route: list[str]) -> subprocess.CompletedProcess:
    """pc_srcdata as the module link runs it, stopped at the plan (no linker)."""
    gen = out / "gen"
    cmd = [sys.executable, str(ROOT / "tools/pc_srcdata.py"), "--ver", "us",
           "--macho-exports", str(META / f"{module}.exports"), *route, "--plan-only",
           module, str(out / "modules" / f"{module}.o"), str(gen / f"{module}.src.c"),
           str(gen / f"{module}.srcsyms"),
           # The generated object: the plan names it (by file name) but nothing reads it before the link.
           f"{module}.c.o", *map(str, objs)]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def check(objects: dict[str, list[Path]], out: Path) -> int:
    lines, stale = [], []
    for m in MODULES:
        r = srcdata(m, objects[m], out, ["--plan", str(META / f"{m}.json")])
        if r.returncode == 0:
            lines.append(f"{m} {len(objects[m])} objects")
        else:
            msg = (r.stderr or r.stdout).strip().splitlines()
            stale.append(m)
            print(f"{m}: " + "\n  ".join(msg[:3]))
    if stale:
        print(f"{NAME}: {', '.join(stale)} plan stale for this tree's arm64 objects; "
              f"rewrite it from the disc: python tools/darwin_plans.py write")
        return 1
    print(f"{NAME}: the macOS module plans fit this tree ({', '.join(lines)}, {TARGET})")
    return 0


def write(objects: dict[str, list[Path]], out: Path, game: Path) -> int:
    cache = game / "CMakeCache.txt"
    if not cache.is_file() or not {"XG_ENGINE:STRING=original", "XG_VERSION:STRING=US"} <= set(
            cache.read_text().splitlines()):
        sys.exit(f"{NAME}: {game} is not a configured US Original PC build (--game)")
    log = out / "darwin-plans.log"
    # prepare.py links against every other module's and the platform's objects of that build.
    run_logged(["cmake", "--build", str(game), "--target", "xenogears"], log, f"building {game}")
    run_logged([sys.executable, str(META.parent / "prepare.py"), str(game), str(out)], log,
               "port/src/system/test/macos_modules/prepare.py")
    print(f"{NAME}: metadata refreshed in {META.relative_to(ROOT)}:")
    st = subprocess.run(["git", "status", "--short", "--", str(META)], cwd=ROOT, capture_output=True, text=True)
    print("\n".join("  " + line for line in st.stdout.splitlines()) or "  (unchanged)")
    # What the runner will do: the written plans checked against the same objects.
    return check(objects, out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("command", choices=["check", "write"])
    ap.add_argument("--build", type=Path, default=ROOT / "build/pc/darwin-plans")
    ap.add_argument("--game", type=Path, default=ROOT / "build/pc/gates",
                    help="write: the US Original PC build prepare.py takes the other modules from")
    a = ap.parse_args()
    cc = darwin_clang()
    if cc is None:
        if a.command == "write":
            sys.exit(f"{NAME}: no clang that targets AArch64 (set XG_DARWIN_CC)")
        print(f"{NAME}: skipped: no clang that targets AArch64 (set XG_DARWIN_CC)")
        return 0
    out = a.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    with (out / ".lock").open("w") as lock:  # one build of this folder at a time
        fcntl.flock(lock, fcntl.LOCK_EX)
        objects = build_objects(cc, out)
        return check(objects, out) if a.command == "check" else write(objects, out, a.game.resolve())


if __name__ == "__main__":
    sys.exit(main())
