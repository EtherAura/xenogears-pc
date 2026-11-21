#!/usr/bin/env python3
"""Report decompilation progress from the linked module ELFs.

splat's `nonmatching` macro gives every symbol that is still assembly a
companion `<name>.NON_MATCHING` symbol. Because a module only counts once its
rebuilt image sha1-matches retail (build/<ver>/<mod>.ok), every function
without that marker is C that compiles to the exact retail bytes.

Functions listed in config/handwritten.yaml were hand-written assembly in the
original; they stay assembly, and the "C+asm" column adds them to the matched
C (the strict "code" column leaves them out).

    python tools/progress.py              # every built version
    python tools/progress.py us --json
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
READELF = ROOT / "tools/toolchain/bin/mips-linux-gnu-readelf"


def handwritten() -> dict[str, set[str]]:
    """Module -> names of the functions that were hand-written assembly."""
    data = yaml.safe_load((ROOT / "config/handwritten.yaml").read_text()) or {}
    return {mod: {n for group in groups for n in group["names"]} for mod, groups in data.items()}


def module_stats(elf: Path, hand: set[str] = frozenset()) -> dict:
    out = subprocess.run([str(READELF), "-sW", str(elf)], capture_output=True, text=True, check=True).stdout
    funcs: dict[str, int] = {}
    data: dict[str, int] = {}
    asm: set[str] = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit():
            continue
        size, kind, ndx, name = int(parts[2], 0), parts[3], parts[6], parts[7]
        if ndx in ("ABS", "UND"):
            continue  # external references (undefined_*_auto.txt), not this module's
        if name.endswith(".NON_MATCHING"):
            asm.add(name[: -len(".NON_MATCHING")])
        elif kind == "FUNC":
            funcs[name] = size
        elif kind == "OBJECT":
            data[name] = size
    done_funcs = {n: s for n, s in funcs.items() if n not in asm}
    done_data = {n: s for n, s in data.items() if n not in asm}
    hand_funcs = {n: s for n, s in funcs.items() if n in asm and n in hand}
    return {
        "functions": len(funcs), "functions_matched": len(done_funcs),
        "code": sum(funcs.values()), "code_matched": sum(done_funcs.values()),
        "data": sum(data.values()), "data_matched": sum(done_data.values()),
        "functions_handwritten": len(hand_funcs), "code_handwritten": sum(hand_funcs.values()),
        "names": set(funcs),
    }


def pct(a: int, b: int) -> str:
    return f"{100 * a / b:6.2f}%" if b else "     -"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("versions", nargs="*")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    versions = args.versions or sorted(p.name for p in (ROOT / "build").iterdir() if p.is_dir() and any(p.glob("*.elf")))
    hand = handwritten()
    report: dict = {}
    seen: dict[str, set[str]] = {}
    for version in versions:
        report[version] = {}
        for elf in sorted((ROOT / "build" / version).glob("*.elf")):
            name = elf.stem
            ok = (ROOT / "build" / version / f"{name}.ok").is_file()
            stats = module_stats(elf, hand.get(name, set())) if ok else {}
            seen.setdefault(name, set()).update(stats.pop("names", set()))
            report[version][name] = {"verified": ok, **stats}
    for mod, names in hand.items():
        if not args.versions and mod in seen and names - seen[mod]:  # some names are one version's only
            print(f"warning: config/handwritten.yaml names no function of {mod}: "
                  f"{', '.join(sorted(names - seen[mod]))}", file=sys.stderr)
    for version, mods in report.items():  # the C+asm 100% holds only while this stays silent
        for name, s in mods.items():
            left = s.get("functions", 0) - s.get("functions_matched", 0) - s.get("functions_handwritten", 0)
            if left:
                print(f"warning: {version} {name}: {left} function(s) still assembly and not in "
                      f"config/handwritten.yaml", file=sys.stderr)

    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
        return 0

    for version, mods in report.items():
        total = {"functions": 0, "functions_matched": 0, "code": 0, "code_matched": 0, "code_handwritten": 0}
        print(f"== {version} ==")
        w = max([16, *map(len, mods)])
        print(f"{'module':{w}s} {'functions':>13s} {'code bytes':>21s} {'code':>8s} {'C+asm':>8s}")
        for name, s in mods.items():
            if not s["verified"]:
                print(f"{name:{w}s} {'(image does not match retail: not counted)':>44s}")
                continue
            for k in total:
                total[k] += s[k]
            print(f"{name:{w}s} {s['functions_matched']:>5d}/{s['functions']:<7d} "
                  f"{s['code_matched']:>9d}/{s['code']:<11d} {pct(s['code_matched'], s['code'])} "
                  f"{pct(s['code_matched'] + s['code_handwritten'], s['code'])}")
        print(f"{'TOTAL':{w}s} {total['functions_matched']:>5d}/{total['functions']:<7d} "
              f"{total['code_matched']:>9d}/{total['code']:<11d} {pct(total['code_matched'], total['code'])} "
              f"{pct(total['code_matched'] + total['code_handwritten'], total['code'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
