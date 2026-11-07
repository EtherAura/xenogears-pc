#!/usr/bin/env python3
"""Find symbols declared with different types in different files (PC port).

    python tools/declcheck.py              # US
    python tools/declcheck.py --ver jp
    python tools/declcheck.py --verbose    # GCC's full notes for each mismatch

A C compiler checks one file at a time, so `extern s32 pMusic;` in one file and
`extern void* pMusic;` in another both compile; on a 64-bit host one of them
truncates. GCC's link-time optimizer merges every file's declarations and warns
when a symbol's types disagree (-Wlto-type-mismatch), even if the symbol is only
declared. This compiles the game C (as tools/hostcheck.py does: x86-64,
PLATFORM_PC, no PsyQ SDK copies) with -flto, then partially links groups of
modules that can see each other's symbols (main with each overlay, plus the
overlays that call into one another) and reports each mismatch once.

Only symbols some file actually uses are checked; an unused declaration costs
nothing on the PC either.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE = ("src/main/psyq/", "src/video_player/psyq/")
CFLAGS = ["-flto", "-c", "-O1", "-std=gnu89", "-nostdinc", "-fno-builtin", "-fpermissive", "-w", "-D_LANGUAGE_C",
          "-DPLATFORM_PC", "-Iinclude"]
# Overlays that reference each other's symbols (by call or data), besides main.
PARTNERS = {
    "field": ["field_mecha", "video_player", "field_debug"],
    "field_mecha": ["field"],
    "movie": ["video_player"],
    "worldmap": ["field_mecha"],
    "menu_gear_shop": ["field_mecha"],
    "battle": ["battle_event", "battle_loader", "battle_result", "battle_debug", "battle_debug_selector",
               "battle_fx_circle_warp", "battle_fx_model_shatter", "battle_fx_screen_ripple",
               "battle_fx_sprite_band", "battle_fx_sprite_wobble", "battle_fx_tiled_sprite", "field_mecha"],
}
WARN = re.compile(r"^(?P<file>[^:\s]+):(?P<line>\d+):\d+: warning: type of ‘(?P<sym>[^’]+)’ does not match original declaration")
NOTE_PREV = re.compile(r"^(?P<file>[^:\s]+):(?P<line>\d+):\d+: note: ‘(?P<sym>[^’]+)’ was previously declared here")
NOTE_WHY = re.compile(r"^[^:\s]+:\d+:\d+: note: (?P<why>(?:return value type|type) mismatch.*|.*parameter.*mismatch.*)")


def module_of(path: str) -> str:
    return Path(path).parts[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--gcc", default=os.environ.get("GCC", "gcc"))
    args = ap.parse_args()

    files = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.c")
                   if not str(p.relative_to(ROOT)).startswith(EXCLUDE))
    modules: dict[str, list[str]] = {}
    for f in files:
        modules.setdefault(module_of(f), []).append(f)

    with tempfile.TemporaryDirectory(prefix="declcheck-") as tmp:
        tmp = Path(tmp)
        obj = {f: tmp / (f.replace("/", "_") + ".o") for f in files}

        def compile_one(f: str) -> str | None:
            p = subprocess.run([args.gcc, *CFLAGS, f"-DVERSION_{args.ver.upper()}", f, "-o", str(obj[f])],
                               cwd=ROOT, capture_output=True, text=True)
            return None if p.returncode == 0 else f"{f}: {p.stderr.strip()}"

        with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
            failed = [e for e in pool.map(compile_one, files) if e]
        if failed:
            print("\n".join(failed))
            return 2

        # Overlays sharing a load address never coexist, so each group holds at most one of them:
        # the battle sub-overlays are linked with battle one at a time.
        groups = [["main"]] + [["main", m] for m in modules if m != "main"]
        groups += [["main", m, *PARTNERS[m]] for m in PARTNERS if m != "battle"]
        groups += [["main", "battle", p] for p in PARTNERS["battle"]]

        def link(group: list[str]) -> str:
            objs = [str(obj[f]) for m in group if m in modules for f in modules[m]]
            p = subprocess.run([args.gcc, "-flto", "-r", "-nostdlib", "-Wl,--allow-multiple-definition",
                                "-Wlto-type-mismatch", "-o", str(tmp / ("_".join(group) + ".o")), *objs],
                               cwd=ROOT, capture_output=True, text=True)
            return p.stderr

        with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
            outputs = list(pool.map(link, groups))

    found: dict[str, dict] = {}
    for out in outputs:
        cur = None
        for line in out.splitlines():
            m = WARN.match(line)
            if m:
                cur = found.setdefault(m["sym"], {"decls": set(), "why": set(), "text": []})
                cur["decls"].add(f"{m['file']}:{m['line']}")
                cur["text"].append(line)
                continue
            if cur is None:
                continue
            m = NOTE_PREV.match(line)
            if m:
                cur["decls"].add(f"{m['file']}:{m['line']}")
            m = NOTE_WHY.match(line)
            if m:
                cur["why"].add(m["why"])
            if line.startswith(("src/", "include/")):
                cur["text"].append(line)

    print(f"declcheck {args.ver.upper()}: {len(files)} files, {len(found)} symbols with mismatched declarations")
    for sym in sorted(found):
        d = found[sym]
        why = "; ".join(sorted(d["why"])) or "type mismatch"
        print(f"{sym}  ({why})")
        for loc in sorted(d["decls"]):
            path, line = loc.rsplit(":", 1)
            try:
                text = (ROOT / path).read_text(errors="replace").splitlines()[int(line) - 1].strip()
            except (OSError, IndexError):
                text = ""
            print(f"    {loc:40} {text}")
        if args.verbose:
            for t in d["text"]:
                print(f"      | {t}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
