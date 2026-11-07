#!/usr/bin/env python3
"""The enemies Hard's curated profile names are the same in both releases.

    python tools/test_hard_profile_ids.py US.bin JP.bin [US.bin JP.bin ...]

port/src/system/gameplay.c hardPercent keys its exceptions on the battle's
enemy set (currentBattleConfig.m0_battleId: directory 0x0D file 2 + 2 * set,
as loadEnemies and the bestiary read it) and the type (the 0x170-byte record
loadEnemies copies). The US names spell which enemies those are; the JP names
are in the JP font's codes, so the JP record at each key must be the US one,
byte for byte, in a set with as many types. Each argument pair is one disc of
the two releases (raw 2352-byte-sector images).
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xenofs import Disc  # noqa: E402

RECORD = 0x170
# (set, type) -> the US name. Margie and Miang are the support actors of the
# Ramsus fight (45/1, ordinary Hard; listed to pin down the set).
PROFILE = {
    (4, 3): "Aveh Corporal",   # 150% HP, 125% offense
    (11, 0): "Rankar Dragon",  # story encounter: neutral
    (13, 1): "Alpha Weltall",  # story encounter: neutral
    (45, 0): "Margie",         # support actor: neutral
    (45, 1): "Ramsus",
    (45, 2): "Miang",          # support actor: neutral
    (53, 0): "Shakhan",        # 150% HP, 125% offense
    (55, 1): "Hammer",         # 150% HP, 125% offense
}


def u16(b: bytes, at: int) -> int:
    return struct.unpack_from("<H", b, at)[0]


def us_text(raw: bytes) -> str | None:
    """A US battle name: the font's codes 0x20.. 'A'..'Z', 0x3D.. 'a'..'z', 0x10 a space."""
    out = []
    for c in raw:
        if c == 0x10:
            out.append(" ")
        elif 0x20 <= c < 0x20 + 26:
            out.append(chr(ord("A") + c - 0x20))
        elif 0x3D <= c < 0x3D + 26:
            out.append(chr(ord("a") + c - 0x3D))
        else:
            return None
    return "".join(out)


def set_file(disc: Disc, s: int) -> tuple[int, list[bytes], list[bytes]]:
    """A set's type count, records and names (as loadEnemies and bestiary_data.c read them)."""
    b = disc.read_file(0x0D, 2 + 2 * s)
    strings = u16(b, 0x30)
    types = 0
    while types < 24 and u16(b, 2 * types) != 0:
        types += 1
    while types > 0 and 0x32 + types * RECORD > min(strings, len(b)):
        types -= 1
    records = [b[0x32 + k * RECORD:0x32 + (k + 1) * RECORD] for k in range(types)]
    names = []
    for k in range(types):
        at = strings + u16(b, strings + 4 + 2 * (k + 1))
        names.append(b[at:b.index(b"\0", at)])
    return types, records, names


def main(argv: list[str]) -> int:
    if len(argv) < 2 or len(argv) % 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    failed = checks = 0
    for us_path, jp_path in zip(argv[0::2], argv[1::2]):
        with Disc(us_path) as us, Disc(jp_path) as jp:
            for s in sorted({s for s, _ in PROFILE}):
                us_types, us_records, us_names = set_file(us, s)
                jp_types, jp_records, _ = set_file(jp, s)
                for (ps, t), want in PROFILE.items():
                    if ps != s:
                        continue
                    checks += 1
                    name = us_text(us_names[t]) if t < us_types else None
                    problems = []
                    if name != want:
                        problems.append(f"US name {name!r}, expected {want!r}")
                    if jp_types != us_types:
                        problems.append(f"JP set has {jp_types} types, US {us_types}")
                    elif t >= jp_types or jp_records[t] != us_records[t]:
                        problems.append("JP record differs from the US one")
                    if problems:
                        failed += 1
                        print(f"FAIL {Path(us_path).stem} / {Path(jp_path).stem}: set {s} type {t}: "
                              + "; ".join(problems))
    print(f"hard profile ids: {checks - failed} of {checks} keys the same enemy in US and JP "
          f"({len(argv) // 2} disc pair{'s' if len(argv) > 2 else ''})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
