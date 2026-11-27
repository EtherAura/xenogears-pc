#!/usr/bin/env python3
"""Generate the desktop program icon assets from the project mark.

    python tools/make_icon.py          # write port/cmake/xenogears.svg and .ico
    python tools/make_icon.py --check  # verify the committed files match

The project mark is the red menu cursor on a rounded black square, the same
geometry and colours as the website favicon and the Android launcher icon
(port/android/app/src/main/res/drawable/ic_launcher_foreground.xml):

  - 32x32 viewBox, black rounded square (#000000, rx=6)
  - upper triangle (5,28) (21,3) (21,14) in #FF2A22
  - lower triangle (5,28) (21,14) (30,14) in #C70B0B

Standard library only.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE_DIR = ROOT / "port" / "cmake"
SVG_PATH = CMAKE_DIR / "xenogears.svg"
ICO_PATH = CMAKE_DIR / "xenogears.ico"

ICO_SIZES = (16, 24, 32, 48, 64, 256)

SVG_TEXT = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">\n'
    '  <rect width="32" height="32" rx="6" fill="#000"/>\n'
    '  <polygon points="5,28 21,3 21,14" fill="#ff2a22"/>\n'
    '  <polygon points="5,28 21,14 30,14" fill="#c70b0b"/>\n'
    '</svg>\n'
)


def edge(ax: float, ay: float, bx: float, by: float, px: float, py: float) -> float:
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax)


def sample(u: float, v: float) -> tuple[int, int, int, int] | None:
    """Colour at (u, v) in the 32x32 icon space, or None outside the rounded square."""
    dx = max(0.0, 6.0 - u, u - 26.0)
    dy = max(0.0, 6.0 - v, v - 26.0)
    if dx * dx + dy * dy > 36.0:
        return None
    if (edge(5.0, 28.0, 21.0, 3.0, u, v) >= 0.0
            and edge(21.0, 3.0, 21.0, 14.0, u, v) >= 0.0
            and edge(21.0, 14.0, 5.0, 28.0, u, v) >= 0.0):
        return (0xFF, 0x2A, 0x22, 0xFF)
    if (edge(5.0, 28.0, 21.0, 14.0, u, v) >= 0.0
            and edge(21.0, 14.0, 30.0, 14.0, u, v) >= 0.0
            and edge(30.0, 14.0, 5.0, 28.0, u, v) >= 0.0):
        return (0xC7, 0x0B, 0x0B, 0xFF)
    return (0x00, 0x00, 0x00, 0xFF)


def render_rgba(size: int) -> bytes:
    """Supersampled straight-alpha RGBA8888 pixels, top-down, size x size."""
    ss = 8 if size <= 64 else 4
    total = ss * ss
    inv_size = 32.0 / size
    inv_ss = 1.0 / ss
    out = bytearray(size * size * 4)
    pos = 0
    for py in range(size):
        for px in range(size):
            r = g = b = inside = 0
            for sy in range(ss):
                v = (py + (sy + 0.5) * inv_ss) * inv_size
                for sx in range(ss):
                    u = (px + (sx + 0.5) * inv_ss) * inv_size
                    c = sample(u, v)
                    if c is not None:
                        r += c[0]
                        g += c[1]
                        b += c[2]
                        inside += 1
            if inside:
                out[pos] = (r + inside // 2) // inside
                out[pos + 1] = (g + inside // 2) // inside
                out[pos + 2] = (b + inside // 2) // inside
                out[pos + 3] = (inside * 255 + total // 2) // total
            pos += 4
    return bytes(out)


def encode_png(size: int, rgba: bytes) -> bytes:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    stride = size * 4
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def encode_dib(size: int, rgba: bytes) -> bytes:
    """32-bit BGRA bottom-up DIB plus 1-bit AND mask for an ICO directory entry."""
    stride = size * 4
    xor_buf = bytearray(size * stride)
    and_row = ((size + 31) // 32) * 4
    and_buf = bytearray(size * and_row)
    dst = 0
    for y in range(size - 1, -1, -1):
        row_off = y * stride
        and_off = (size - 1 - y) * and_row
        for x in range(size):
            r = rgba[row_off + x * 4]
            g = rgba[row_off + x * 4 + 1]
            b = rgba[row_off + x * 4 + 2]
            a = rgba[row_off + x * 4 + 3]
            xor_buf[dst] = b
            xor_buf[dst + 1] = g
            xor_buf[dst + 2] = r
            xor_buf[dst + 3] = a
            dst += 4
            if a == 0:
                and_buf[and_off + (x >> 3)] |= 0x80 >> (x & 7)
    header = struct.pack("<IIIHHIIIIII", 40, size, size * 2, 1, 32, 0, len(xor_buf) + len(and_buf), 0, 0, 0, 0)
    return header + bytes(xor_buf) + bytes(and_buf)


def build_ico() -> bytes:
    images: list[tuple[int, bytes]] = []
    for size in ICO_SIZES:
        rgba = render_rgba(size)
        payload = encode_png(size, rgba) if size >= 256 else encode_dib(size, rgba)
        images.append((size, payload))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    directory = bytearray()
    body = bytearray()
    for size, payload in images:
        dim = 0 if size >= 256 else size
        directory.extend(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(payload), offset))
        body.extend(payload)
        offset += len(payload)
    return header + bytes(directory) + bytes(body)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="check committed files instead of writing them")
    args = ap.parse_args()

    svg_bytes = SVG_TEXT.encode("utf-8")
    ico_bytes = build_ico()
    targets = ((SVG_PATH, svg_bytes), (ICO_PATH, ico_bytes))

    if args.check:
        stale = [p for p, want in targets if not p.is_file() or p.read_bytes() != want]
        if stale:
            print("make_icon: out of date:", ", ".join(str(p.relative_to(ROOT)) for p in stale), file=sys.stderr)
            return 1
        print("make_icon: ok")
        return 0

    for path, data in targets:
        path.write_bytes(data)
        print(f"wrote {path.relative_to(ROOT)} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
