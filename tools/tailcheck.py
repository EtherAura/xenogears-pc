#!/usr/bin/env python3
"""Review the PS1 data the PC drops past tables of a declared size (xg-htn.35.154).

    python tools/tailcheck.py --build build/pc/gates [--build build/pc/gates-jp ...]

tools/pc_link.py gives an object of a declared size (`extern u8 t[8][3];`) that
many bytes on the PC, copied from the disc, and an object of unknown size
(`extern u8 t[];`) its label's whole extent. On the PS1 more retail data
follows a label before the next one, so a read past the declared end gets those
bytes on the PS1 and other memory on the PC: battle's perApTable was declared
[8][3], performAttack read its 25th byte, and the PC ran a garbage attack
(926020d1). pc_link lists every such object in <build>/gen/tails.txt: its
module, name, address, declared size, and extent to the next object the PC has.

This checks each build's list. Left out are tails that are only alignment (the
object's end rounded up to 4 bytes) and hold nothing: BSS, or data whose bytes
are all zero (read from the data assembly, asm/<ver>, as pc_link reads it).
Every other line must be reviewed in tools/tailcheck_allow.txt, one per line:

    <module> | <name> | <declared>/<extent> | <reason>

(sizes in hex as tails.txt gives them). The reason says why no read reaches the
tail: an index's bound, plain loads and stores of the whole object, what the
bytes are. A read that does reach it is fixed instead, as perApTable was: under
PLATFORM_PC the table is declared of unknown size (or of its true size), so the
PC copies the retail bytes the PS1 reads. A build without gen/tails.txt (built
before pc_link wrote it) is skipped. Exit status 1 when a line is not reviewed.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
ALLOW = ROOT / "tools" / "tailcheck_allow.txt"
HEADER = re.compile(r"^# xenogears-pc-link-tails region=(us|jp) engine=(\S+)$")
LINE = re.compile(r"^(\w+) (\w+) 0x([0-9A-F]{8}) declared 0x([0-9A-F]+) extent 0x([0-9A-F]+) (data|bss)$")


def load_allow(path: Path) -> dict[tuple[str, str, str], str]:
    allow: dict[tuple[str, str, str], str] = {}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 4 or not re.fullmatch(r"0x[0-9A-F]+/0x[0-9A-F]+", parts[2]) or not parts[3]:
            raise SystemExit(f"tailcheck: {path.name}:{n}: not '<module> | <name> | <declared>/<extent> | <reason>'")
        key = (parts[0], parts[1], parts[2])
        if key in allow:
            raise SystemExit(f"tailcheck: {path.name}:{n}: {' '.join(key)} is listed twice")
        allow[key] = parts[3]
    return allow


def read_tails(path: Path) -> tuple[str, str, list[tuple[str, str, int, int, int, str]]]:
    lines = path.read_text().splitlines()
    m = HEADER.match(lines[0]) if lines else None
    if not m:
        raise SystemExit(f"tailcheck: {path}: not a tools/pc_link.py tails.txt")
    rows = []
    for line in lines[1:]:
        if line.startswith("#"):
            continue
        t = LINE.match(line)
        if not t:
            raise SystemExit(f"tailcheck: {path}: unreadable line {line!r}")
        rows.append((t[1], t[2], int(t[3], 16), int(t[4], 16), int(t[5], 16), t[6]))
    return m[1], m[2], rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", action="append", type=Path, required=True,
                    help="a configured PC build (its gen/tails.txt), or a tails.txt itself; repeatable")
    ap.add_argument("--allow", type=Path, default=ALLOW)
    args = ap.parse_args()
    allow = load_allow(args.allow)

    images: dict[str, dict] = {}
    seen: set[tuple[str, str, str]] = set()
    new: list[str] = []
    checked: list[str] = []
    for b in args.build:
        path = b if b.is_file() else b / "gen" / "tails.txt"
        if not path.is_file():
            print(f"tailcheck: skipped {b}: no gen/tails.txt (not built since tools/pc_link.py writes it)")
            continue
        ver, engine, rows = read_tails(path)
        reviewed = padding = 0
        for m, n, a, q, e, kind in rows:
            end = a + e
            if end <= (a + q + 3) & ~3:  # no further than alignment: padding when it holds nothing
                if kind == "bss":
                    padding += 1
                    continue
                if ver not in images:
                    import pc_link  # the data assembly's bytes, as the link reads them
                    images[ver] = pc_link.load_images(ver)[0]
                img = images[ver].get(m)
                if img is not None and not img.refs_in(a + q, e - q) and not any(img.get(a + q, e - q)):
                    padding += 1
                    continue
            key = (m, n, f"0x{q:X}/0x{e:X}")
            seen.add(key)
            if key in allow:
                reviewed += 1
            else:
                new.append(f"{ver} {engine}: {m} | {n} | 0x{q:X}/0x{e:X} | (0x{a:08X}, {e - q} {kind} byte(s) "
                           f"past the declared end)")
        checked.append(f"{ver} {engine} {len(rows)} ({reviewed} reviewed, {padding} padding)")
    for line in new:
        print(line)
    stale = sorted(k for k in allow if k not in seen)
    if stale and checked:
        print(f"note: {len(stale)} reviewed line(s) no build checked here has: "
              + ", ".join(f"{m} {n} {s}" for m, n, s in stale[:6]) + (" ..." if len(stale) > 6 else ""))
    if not checked:
        print("tailcheck: skipped: no build has gen/tails.txt")
        return 0
    print(f"tailcheck: {len(new)} tail(s) not reviewed in tools/tailcheck_allow.txt; checked "
          + "; ".join(checked))
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
