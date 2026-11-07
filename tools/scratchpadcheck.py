#!/usr/bin/env python3
"""The PS1 scratchpad's addresses in C go through PSX_SCRATCH (include/common.h).

    python tools/scratchpadcheck.py          # gate: list raw scratchpad literals, exit 1 if any
    python tools/scratchpadcheck.py --fix    # wrap each one in PSX_SCRATCH(...)

The game C reaches the PS1's 1 KB scratchpad by its address, 0x1F800000 to
0x1F8003FF ((MATRIX*)0x1F800000, STACK_SWITCH(0x1F8003FC), ...). On the PS1
PSX_SCRATCH(addr) is the address itself (every retail image unchanged); on
the PC it is the same offset in the platform's scratchpad block
(pcScratchpad, port/src/system/memory.c), wherever the host put it: the PS1
address is not free in every process (an Android app's Java heap holds it,
xg-mrj.3.4). A literal in that range anywhere in src/, include/, engine/ or
port/src that is not PSX_SCRATCH's own argument fails the gate (comments
and strings do not count); include/common.h's PSX_SCRATCH_BASE line holds the
base itself.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRS = ["src", "include", "engine/src", "engine/include", "port/src"]
SUFFIXES = {".c", ".h", ".cpp", ".inc"}
# 0x1F800000-0x1F800FFF: the scratchpad (1 KB) and the unmapped rest of its page.
LITERAL = re.compile(r"\b0x1[fF]800[0-9a-fA-F]{3}[uUlL]*\b")
WRAPPED = re.compile(r"PSX_SCRATCH\(\s*$")
BASE_LINE = re.compile(r"^\s*#\s*define\s+PSX_SCRATCH_BASE\b")


def code_mask(text: str) -> list[bool]:
    """Per character: True where it is code (not a comment, string or character literal)."""
    mask = [True] * len(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j += 1
        else:
            i += 1
            continue
        for k in range(i, min(j, n)):
            mask[k] = False
        i = j
    return mask


def raw_literals(text: str) -> list[re.Match]:
    """The literals in code that PSX_SCRATCH does not take."""
    mask = code_mask(text)
    out = []
    for m in LITERAL.finditer(text):
        if not mask[m.start()]:
            continue
        line_start = text.rfind("\n", 0, m.start()) + 1
        if BASE_LINE.match(text[line_start:m.start()]):
            continue
        if WRAPPED.search(text[line_start:m.start()]):
            continue
        out.append(m)
    return out


def files() -> list[Path]:
    return [p for d in DIRS for p in sorted((ROOT / d).rglob("*")) if p.suffix in SUFFIXES and p.is_file()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fix", action="store_true", help="wrap every raw literal in PSX_SCRATCH(...)")
    args = ap.parse_args()
    found = 0
    changed = 0
    for p in files():
        text = p.read_text(errors="surrogateescape")
        hits = raw_literals(text)
        if not hits:
            continue
        found += len(hits)
        if args.fix:
            for m in reversed(hits):
                literal = re.sub(r"[uUlL]+$", "", m.group(0))
                text = text[:m.start()] + f"PSX_SCRATCH({literal})" + text[m.end():]
            p.write_text(text, errors="surrogateescape")
            changed += 1
        else:
            for m in hits:
                line = text.count("\n", 0, m.start()) + 1
                src = text.splitlines()[line - 1].strip()
                print(f"{p.relative_to(ROOT)}:{line}: {m.group(0)} not through PSX_SCRATCH: {src[:120]}")
    if args.fix:
        print(f"scratchpadcheck: wrapped {found} literals in {changed} files")
        return 0
    print(f"scratchpadcheck: {len(files())} files, {found} raw scratchpad literals")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
