#!/usr/bin/env python3
"""Find data the PC port cuts at a label: `extern T name[];` tables that run on.

    python tools/cuttables.py [--ver us|jp]

tools/pc_link.py gives each data label its own object on the PC, as big as
the C declares it or, for an array of unknown size (`extern T name[];`), up
to the next label. splat labels every address the code or data points at, so
a table the game reads past such a label (a spurious split) loses its tail on
the PC: the reads get other data (cut tables, bd memory port-cut-tables).

This lists every incomplete array the game C declares (and never declares
with a size anywhere) whose next label in the same section is a name no C
code uses: the label is probably a spurious split of the table. It also lists
scalars of a basic type whose address the C takes (`&name`, used as a table:
worldmap mode 18's script D_8009AC60 was one) when the data runs past the
scalar to the next label (the PC gives a scalar only its own size). Declare the table's real extent
(`extern T name[N];`), or its size where the label is a real end, and the
line goes away. Exit status 1 when anything is listed.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

INCOMPLETE = re.compile(r"^\s*extern\s+[^;()]*?\b(\w+)\s*\[\s*\]\s*;", re.M)
# A declaration or definition with a size: `[extern] T name[N]...;` or `= {`.
SIZED = re.compile(
    r"^[ \t]*(?!(?:return|else|case|goto|do)\b)(?:\w+[ \t\*]+)+(\w+)\s*\[\s*[^\]\s][^\]]*\](?:\s*\[[^\]]*\])*\s*(?:;|=)",
    re.M)
SCALAR = re.compile(r"^\s*extern\s+(s8|u8|char|s16|u16|s32|u32|int)\s+(\w+)\s*;", re.M)
SCALAR_SIZE = {"s8": 1, "u8": 1, "char": 1, "s16": 2, "u16": 2, "s32": 4, "u32": 4, "int": 4}
LABEL = re.compile(r"^\s*dlabel\s+(\w+)")
ADDR = re.compile(r"/\*\s+(?:[0-9A-F]+\s+)?([0-9A-F]{8})\b")


def c_sources() -> dict[Path, str]:
    files = [p for p in (ROOT / "src").rglob("*.c") if "psyq" not in p.parts]
    files += list((ROOT / "include").rglob("*.h"))
    files += list((ROOT / "port" / "src" / "game").rglob("*.c"))
    return {p: p.read_text(errors="replace") for p in files}


def sections(ver: str):
    """(module, file, [(label, address)]) for every data section splat wrote."""
    for d in sorted((ROOT / "asm" / ver).glob("*/data")):
        for f in sorted(d.glob("*.s")):
            labels = []
            pending = None
            for line in f.read_text(errors="replace").splitlines():
                m = LABEL.match(line)
                if m:
                    pending = m.group(1)
                    continue
                m = ADDR.search(line)
                if m and pending:
                    labels.append((pending, int(m.group(1), 16)))
                    pending = None
            yield d.parent.name, f, labels


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    args = ap.parse_args()

    text = c_sources()
    words = set()
    incomplete: dict[str, list[str]] = {}
    scalars: dict[str, tuple[int, list[str]]] = {}
    sized = set()
    for path, t in text.items():
        words.update(re.findall(r"\b\w+\b", t))
        for m in INCOMPLETE.finditer(t):
            incomplete.setdefault(m.group(1), []).append(str(path.relative_to(ROOT)))
        sized.update(SIZED.findall(t))
        for m in SCALAR.finditer(t):
            # address-of (after `(`, `=`, `,`, `{`, `return` or a cast's `)`), not a binary &
            if re.search(r"(?:[(=,{]\s*|\)|\breturn\s+)&\s*" + m.group(2) + r"\b", t):
                size, where = scalars.get(m.group(2), (SCALAR_SIZE[m.group(1)], []))
                scalars[m.group(2)] = (size, where + [str(path.relative_to(ROOT))])
    if not (ROOT / "asm" / args.ver).is_dir():
        print(f"cuttables-{args.ver}: no asm/{args.ver} (run tools/configure.py)")
        return 1

    found = []
    for module, f, labels in sections(args.ver):
        for i, (name, addr) in enumerate(labels):
            if i + 1 >= len(labels) or name in sized:
                continue
            nxt, naddr = labels[i + 1]
            if name in scalars:
                # the PC object is the scalar's size, whatever label follows
                if naddr - addr <= max(scalars[name][0], 4) or f.name.endswith(".bss.s"):
                    continue
                where = scalars[name][1]
            elif name in incomplete and nxt not in words:
                where = incomplete[name]
            else:
                continue
            used = next(((n, a) for n, a in labels[i + 2:] if n in words), None)
            found.append((module, name, addr, nxt, naddr - addr, used, where))
    for module, name, addr, nxt, size, used, where in found:
        tail = f", next used {used[0]} at +0x{used[1] - addr:X}" if used else ""
        print(f"{module} {name} 0x{addr:08X}: cut at {nxt} (+0x{size:X}){tail}; {', '.join(where)}")
    print(f"cuttables-{args.ver}: {len(found)} table(s) cut at an unused label")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
