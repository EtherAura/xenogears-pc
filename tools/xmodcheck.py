#!/usr/bin/env python3
"""Cross-module data references whose names differ between modules.

    python tools/xmodcheck.py [--ver us|jp]

On the PS1 a module reaches another loaded module's data by address. The PC
link (tools/pc_link.py) used to match such references by NAME, so when module
M named an address that module K labels under another name, M got zero
storage of its own and the two never saw each other's writes (seen after a
naming round: the event-battle movie flag, the result screen's label tables,
the field's video-player flags). pc_link now resolves them by address and
stops the build if two PC objects stand for one PS1 address, so a name that
differs no longer splits anything on the PC. This stays as a naming check:
a rename must reach every module that names the address (Beads memory
naming-cross-module), and the PS1 build cannot notice when it does not.

For every module M, each data symbol its linked image (build/<ver>/M.elf)
takes as an absolute address outside M's own image (symbol-file names and
splat's automatic D_ names) is looked up in every other module K whose image
contains that address: K's data labels (asm/<ver>/K/data/*.s) and the data
its C defines; modules whose image overlaps M's or M's host overlay's (at
the same base, never loaded together) are left out. The reference is fine when some K
labels the address with the same name; it is reported when the K's label it
only under other names.

tools/xmodcheck_allow.txt lists references whose PS1 names differ on purpose
(PLATFORM_PC maps them), each with a reason; stale entries fail. Prints one
line per difference and a summary starting with 'xmodcheck'. Exit status:
the number of differences plus stale entries.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pc_link  # noqa: E402

NM = ROOT / "tools/toolchain/bin/mips-linux-gnu-nm"
SYM = re.compile(r"^\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;(.*)$")
DATA_TYPES = set("DdBbRrGgSsVv")
# Sub-modules load beside their host overlay; nothing at the host's base can be
# loaded with them either.
HOST = pc_link.SUB_MODULES


def nm(elf: Path) -> list[tuple[int, str, str]]:
    out = subprocess.run([NM, str(elf)], capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            rows.append((int(p[0], 16), p[1], p[2]))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", default="us")
    a = ap.parse_args()
    ver = a.ver

    funcs: set[str] = set()
    for f in sorted((ROOT / "config" / ver / "symbols").glob("*.txt")):
        for line in f.read_text().splitlines():
            m = SYM.match(line)
            if m and "type:func" in m.group(3):
                funcs.add(m.group(1))

    labels: dict[str, dict[int, set[str]]] = {}
    for path in sorted((ROOT / "asm" / ver).glob("*/data/*.s")):
        mod = path.relative_to(ROOT / "asm" / ver).parts[0]
        pending: list[str] = []
        for line in path.read_text(errors="replace").splitlines():
            m = pc_link.LABEL.match(line)
            if m and not m.group(1).startswith(".L"):
                pending.append(m.group(1))
                continue
            m = pc_link.DATA_LINE.match(line)
            if m and pending:
                for n in pending:
                    labels.setdefault(mod, {}).setdefault(int(m["vram"], 16), set()).add(n)
                pending = []

    images = pc_link.module_images(ver)
    refs: dict[str, list[tuple[str, int]]] = {}
    for mod in images:
        elf = ROOT / "build" / ver / f"{mod}.elf"
        if not elf.exists():
            print(f"xmodcheck {ver}: no {elf.relative_to(ROOT)} (run ninja)")
            return 1
        for addr, t, name in nm(elf):
            if t in DATA_TYPES and not name.endswith(".NON_MATCHING"):
                labels.setdefault(mod, {}).setdefault(addr, set()).add(name)  # data the C defines
            elif t in "Aa" and name not in funcs and not name.startswith("func_"):
                refs.setdefault(mod, []).append((name, addr))

    allow: set[tuple[str, str]] = set()
    for line in (ROOT / "tools/xmodcheck_allow.txt").read_text().splitlines():
        p = line.split("#", 1)[0].split()
        if len(p) == 3 and p[0] == ver:
            allow.add((p[1], p[2]))
    used: set[tuple[str, str]] = set()
    splits = 0
    checked = 0
    for mod, rs in sorted(refs.items()):
        lo, hi = images[mod]
        for name, addr in sorted(rs, key=lambda r: r[1]):
            if lo <= addr < hi:
                continue
            # K must be loadable beside M: overlays at M's (or M's host's) base never are.
            hname = HOST.get(mod, "")
            host = images.get(hname, (0, 0))
            owners = {k: labels.get(k, {}).get(addr, set()) for k, (klo, khi) in images.items()
                      if k != mod and klo <= addr < khi and not (lo < khi and klo < hi)
                      and (k == hname or not (host[0] < khi and klo < host[1]))}
            owners = {k: v for k, v in owners.items() if v}
            if not owners:
                continue
            checked += 1
            if any(name in v for v in owners.values()):
                continue
            if (mod, name) in allow:
                used.add((mod, name))
                continue
            splits += 1  # named otherwise
            where = ", ".join(f"{k}: {'/'.join(sorted(v))}" for k, v in sorted(owners.items()))
            print(f"{mod:24s} {name:36s} {addr:#010x} -> {where}")
    stale = sorted(allow - used)
    for mod, name in stale:
        print(f"stale allow entry: {ver} {mod} {name}")
    print(f"xmodcheck {ver}: {checked} cross-module data references, {splits} named otherwise, "
          f"{len(used)} allowed, {len(stale)} stale allow entries")
    return splits + len(stale)


if __name__ == "__main__":
    sys.exit(main())
