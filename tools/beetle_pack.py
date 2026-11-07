#!/usr/bin/env python3
"""Beetle PSX HW replacement packs (the community's HD packs) for the PC port.

The port draws a Beetle pack's files where a category uses the texture pack
(F1 > Packs > Beetle PSX pack, settings.ini beetle_pack=<folder>); the naming
scheme was measured black-box (docs/BEETLE_PACKS.md, beads xg-mrj.1):
'<upload>-<palette>.png', CRC-32s in lowercase hex without leading zeros,
'<upload>-0.png' for 15-bit textures, the whole upload at its draw's depth
times 1, 2, 4, 8 or 16.

    python tools/beetle_pack.py check PACK [--dump DUMP]
        what the core and the port take from a pack folder: names, PNG kind,
        scale; with --dump (a Beetle dump folder, or the port's
        beetle_pack_log) each file's size against its upload's
    python tools/beetle_pack.py test-pack DUMP OUT [--scale 2] [--names a,b] [--only-palette P]
        a test pack made from a Beetle texture dump (tools/beetle.py capture
        --texture-dump): every dumped texture (or those named) enlarged and
        visibly marked (colours inverted; --grid adds a line on each texel's
        edges), alpha classes kept; 15-bit dumps ('<upload>.png') become '<upload>-0.png',
        the name the core looks up, and '-missing' palettes are left out (the
        core never applies them)
    python tools/beetle_pack.py name FILE [--depth 4|8|15] [--palette CLUTFILE]
        the CRC-32 name of an upload's halfwords (raw little-endian file), and
        of a palette's 16 or 256 entries

No game data is written anywhere but OUT; packs and dumps are user-owned.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

NAME = re.compile(r"^([0-9a-fA-F]{1,8})-([0-9a-fA-F]{1,8})\.png$", re.I)


def png_header(path: Path) -> tuple[int, int, int, int] | None:
    """(width, height, bit depth, colour type) from the IHDR, None if not a PNG."""
    head = path.read_bytes()[:33]
    if len(head) < 33 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    w, h, bits, kind = struct.unpack(">IIBB", head[16:26])
    return w, h, bits, kind


def canonical(n: str) -> bool:
    """A hex part as the core asks for it: no leading zero."""
    return len(n) == 1 or n[0] != "0"


def dump_sizes(dump: Path) -> dict[str, tuple[int, int]]:
    """Each upload/palette's native size from a Beetle dump folder or the port's name log."""
    out: dict[str, tuple[int, int]] = {}
    if dump.is_file():
        for line in dump.read_text().splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0].endswith(".png"):
                out[parts[0][:-4]] = (int(parts[1]), int(parts[2]))
        return out
    for p in dump.iterdir():
        hd = png_header(p) if p.suffix.lower() == ".png" else None
        if hd:
            stem = p.stem
            out[stem if "-" in stem else stem + "-0"] = hd[:2]
    return out


def cmd_check(args) -> int:
    pack = Path(args.pack)
    sizes = dump_sizes(Path(args.dump)) if args.dump else {}
    ok = bad = 0
    for p in sorted(pack.iterdir()):
        if p.is_dir():
            print(f"{p.name}/: a folder (the core reads the top level only)")
            continue
        m = NAME.match(p.name)
        if not m:
            if p.suffix.lower() == ".png":
                print(f"{p.name}: not '<upload>-<palette>.png' (15-bit textures are '<upload>-0.png'): never applied")
                bad += 1
            continue
        notes = []
        if not (canonical(m.group(1)) and canonical(m.group(2))):
            notes.append("a leading zero: the core asks without it, never applied")
        if p.name != p.name.lower():
            notes.append("upper case: the core opens the lower-case name (loads on Windows only; the port takes it)")
        hd = png_header(p)
        if hd is None:
            notes.append("not a PNG")
        else:
            w, h, bits, kind = hd
            if bits > 8:
                notes.append(f"{bits}-bit channels: refused (8 at most)")
            key = f"{int(m.group(1), 16):x}-{int(m.group(2), 16):x}"
            if key in sizes:
                nw, nh = sizes[key]
                s = w // nw if w % nw == 0 else 0
                if not s or h != nh * s or s & (s - 1) or s > 16:
                    notes.append(f"{w}x{h} is not the upload's {nw}x{nh} times 1, 2, 4, 8 or 16: refused")
                elif s == 16:
                    notes.append("16x: the port halves it to 8x")
        if notes:
            bad += 1
            print(f"{p.name}: " + "; ".join(notes))
        else:
            ok += 1
    print(f"{ok} files as the core takes them, {bad} with notes")
    return 0


def read_rgba(path: Path) -> tuple[int, int, bytearray]:
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    return im.width, im.height, bytearray(im.tobytes())


def write_rgba(path: Path, w: int, h: int, px: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def marked(w: int, h: int, px: bytearray, scale: int, grid: bool = False) -> bytearray:
    """Enlarged, colours inverted (with --grid a line on each texel's top and left edge), alpha kept."""
    W, H = w * scale, h * scale
    out = bytearray(W * H * 4)
    for y in range(H):
        sy = y // scale
        for x in range(W):
            sx = x // scale
            o = (sy * w + sx) * 4
            r, g, b, a = px[o:o + 4]
            if grid and scale > 1 and (x % scale == 0 or y % scale == 0):
                r, g, b = 255, 255, 0
            else:
                r, g, b = 255 - r, 255 - g, 255 - b
            out[(y * W + x) * 4:(y * W + x) * 4 + 4] = bytes((r, g, b, a))
    return out


def cmd_test_pack(args) -> int:
    dump, out = Path(args.dump), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    want = set(args.names.split(",")) if args.names else None
    made = 0
    for p in sorted(dump.iterdir()):
        if p.suffix.lower() != ".png" or "missing" in p.stem:
            continue
        stem = p.stem if "-" in p.stem else p.stem + "-0"
        if want is not None and stem not in want and p.stem not in want:
            continue
        if args.only_palette and stem.split("-")[1] != args.only_palette:
            continue
        w, h, px = read_rgba(p)
        write_rgba(out / f"{stem}.png", w * args.scale, h * args.scale, marked(w, h, px, args.scale, args.grid))
        made += 1
    print(f"{made} replacements in {out}")
    return 0


def cmd_name(args) -> int:
    data = Path(args.file).read_bytes()
    print(f"upload {zlib.crc32(data):x}")
    if args.palette:
        clut = Path(args.palette).read_bytes()
        n = 16 if args.depth == 4 else 256
        print(f"palette {zlib.crc32(clut[:n * 2]):x}" if args.depth != 15 else "palette 0")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("pack")
    c.add_argument("--dump")
    t = sub.add_parser("test-pack")
    t.add_argument("dump")
    t.add_argument("out")
    t.add_argument("--scale", type=int, default=2, choices=[1, 2, 4, 8])
    t.add_argument("--names", help="comma-separated '<upload>-<palette>' names (default every dump)")
    t.add_argument("--only-palette", help="only the dumps of this palette name")
    t.add_argument("--grid", action="store_true", help="also a grid line on each texel's top and left edge")
    n = sub.add_parser("name")
    n.add_argument("file")
    n.add_argument("--depth", type=int, default=4, choices=[4, 8, 15])
    n.add_argument("--palette")
    args = ap.parse_args()
    return {"check": cmd_check, "test-pack": cmd_test_pack, "name": cmd_name}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
