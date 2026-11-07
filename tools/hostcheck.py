#!/usr/bin/env python3
"""Compile the game C for a 64-bit host and tally what breaks (PC port, milestone 1).

    python tools/hostcheck.py                 # US: counts per diagnostic and module
    python tools/hostcheck.py --ver jp
    python tools/hostcheck.py --list [FLAG]   # every diagnostic (optionally one -W flag / "error")
    python tools/hostcheck.py --files src/main/heap.c ...

Every src/ C file except the PsyQ SDK copies (src/main/psyq/, src/video_player/psyq/,
which the port's platform layer replaces) is compiled by clang for x86-64 with
-fsyntax-only, PLATFORM_PC defined and the same -nostdinc include setup as the
matching build. 64-bit hazards are reported as errors:

    pointer-to-int-cast, int-to-pointer-cast    explicit casts that drop or invent bits
    int-conversion                              implicit pointer <-> integer
    implicit-function-declaration               an implicit int return truncates pointers
    shorten-64-to-32                            64-bit values (pointer differences,
                                                sizeof) stored into 32-bit integers
    return-type, return-mismatch                a non-void function that falls off its end
                                                or returns no value: the PS1 caller gets
                                                $v0, the PC's would hold anything (use
                                                IMPLICIT_RETURN/FALLOFF_RETURN, common.h)

The milestone is reached when this reports nothing for US and JP and the retail
build still hash-matches.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE = ("src/main/psyq/", "src/video_player/psyq/")
HAZARDS = ["pointer-to-int-cast", "int-to-pointer-cast", "int-conversion", "implicit-function-declaration",
           "shorten-64-to-32", "return-type", "return-mismatch"]
DIAG = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<col>\d+): (?P<kind>error|warning): (?P<msg>.*?)(?: \[(?P<flag>[^\]]+)\])?$")


def sources(files: list[str] | None) -> list[Path]:
    if files:
        return [Path(f) for f in files]
    return sorted(p.relative_to(ROOT) for p in (ROOT / "src").rglob("*.c")
                  if not str(p.relative_to(ROOT)).startswith(EXCLUDE))


def compile_one(path: Path, ver: str, clang: str) -> list[dict]:
    cmd = [clang, "-target", "x86_64-linux-gnu", "-fsyntax-only", "-std=gnu89", "-nostdinc", "-fno-builtin",
           "-ferror-limit=0", "-fno-caret-diagnostics", "-fdiagnostics-show-option", "-D_LANGUAGE_C",
           "-DPLATFORM_PC", f"-DVERSION_{ver.upper()}", "-Iinclude", "-Wno-everything"]
    cmd += [f"-W{h}" for h in HAZARDS] + [f"-Werror={h}" for h in HAZARDS] + [str(path)]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = []
    for line in p.stderr.splitlines():
        m = DIAG.match(line)
        if m:
            d = m.groupdict()
            # -Werror=x reports the flag as "-Werror,-Wx"; keep the bare name, and "error" for hard errors.
            flag = (d["flag"] or "").split(",")[-1].removeprefix("-W")
            d["flag"] = flag or "error"
            out.append(d)
    return out


def module_of(f: str) -> str:
    parts = Path(f).parts
    return parts[1] if len(parts) > 2 and parts[0] == "src" else parts[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--list", nargs="?", const="", metavar="FLAG")
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang"))
    args = ap.parse_args()

    files = sources(args.files)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = list(pool.map(lambda f: compile_one(f, args.ver, args.clang), files))
    diags = [d for r in results for d in r]
    # A header diagnostic repeats once per including file; count it once.
    seen, unique = set(), []
    for d in diags:
        key = (d["file"], d["line"], d["col"], d["flag"], d["msg"])
        if key not in seen:
            seen.add(key)
            unique.append(d)

    if args.list is not None:
        for d in unique:
            if args.list in ("", d["flag"]):
                print(f"{d['file']}:{d['line']}:{d['col']}: [{d['flag']}] {d['msg']}")
        return 0

    by_flag = collections.Counter(d["flag"] for d in unique)
    by_mod = collections.defaultdict(collections.Counter)
    for d in unique:
        by_mod[module_of(d["file"])][d["flag"]] += 1
    flags = sorted(by_flag, key=lambda f: -by_flag[f])
    print(f"hostcheck {args.ver.upper()}: {len(files)} files, {len(unique)} diagnostics")
    for f in flags:
        print(f"  {by_flag[f]:6}  {f}")
    if unique:
        print()
        print("module".ljust(26) + "".join(f[:14].rjust(15) for f in flags))
        for mod in sorted(by_mod, key=lambda m: -sum(by_mod[m].values())):
            print(mod.ljust(26) + "".join(str(by_mod[mod][f] or "").rjust(15) for f in flags))
    return 1 if unique else 0


if __name__ == "__main__":
    sys.exit(main())
