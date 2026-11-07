#!/usr/bin/env python3
"""Check what cdtest loaded against the disc, read with tools/xenofs.py.

    python port/src/cd/test/cdcheck.py [disc.bin] [outdir]
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import xenofs  # noqa: E402

disc_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).bin"
out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build/pc/cdtest/out"
bad = 0
with xenofs.Disc(disc_path) as disc:
    checks = {
        "filetable.bin": b"".join(disc.sector(xenofs.TABLE_LBA + i) for i in range(xenofs.TABLE_SECTORS)),
        "dirtable.bin": disc.sector(xenofs.DIR_INDEX_LBA)[:0x7A],
    }
    for n in range(2, 8):
        checks[f"file{n}.bin"] = disc.read_file(1, n)
    for name, want in checks.items():
        got = (out / name).read_bytes()
        ok = got == want
        bad += not ok
        print(f"{name:14} {len(got):7} bytes  {'ok' if ok else 'MISMATCH'}")
sys.exit(1 if bad else 0)
