#!/usr/bin/env python3
"""Check the PC port's hi-res copy (port/src/gpu/hires.c, upscale=) against its 1x picture.

    python tools/hirescheck.py frames DIR_1X DIR_N
    python tools/hirescheck.py align DIR_N [--tol T] [--worst K]
    python tools/hirescheck.py run --pad PAD --quit N [--scale 4] [--color true] [--aspect 16:9]
                                   [--bin B] [--out DIR] [--env K=V ...]

The copy is drawn beside the exact 1x VRAM, which the game keeps reading, so:
  1. frames: every frame_<vblank>.png and wide_<vblank>.png that both runs
     wrote (XG_DUMP_FRAMES) must be the same with upscaling on (DIR_N) as
     off (DIR_1X), and wide.log the same: the game never notices the copy.
     A difference is an error.
  2. align: each hires_<vblank>.png (XG_DUMP_HIRES=1: the copy's display
     area, N times the frame) decimated to one pixel per N x N block (the
     one at its centre: the copy puts what a 1x pixel showed around its
     block's centre) must be its frame_<vblank>.png but for primitive edges
     (sloped edges are the device's, not the PS1's staircase), shading and
     texels the PS1 rounds half way (smooth at N times) and the colour mode
     (true colour is 8 bits, the 1x frame 5; compare 15dither1x runs): pixels
     differing by more than --tol in a channel are counted, as "edge" when
     the 1x frame has another colour beside them, else "inside". Misplaced
     primitives, half-pixel texture shifts, missing pieces and blend errors
     show as pixels inside. hireswide_<vblank>.png against wide_<vblank>.png
     the same. Only counted: the worst frames are listed.

`run` plays the pad script twice, headless with XG_CLOCK=step: upscale=1
and upscale=--scale (with XG_DUMP_HIRES), then checks both.

Exit status: the number of errors (frames that differ).
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib
from itertools import accumulate
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def png_rows(path: Path, want=None) -> tuple[int, int, list]:
    """8-bit RGB PNG (port/src/gpu/dump.c writes the Sub filter) -> (w, h, rows):
    each wanted row (all, or the indices in `want`) as bytes; others None."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    idat = []
    w = h = 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype != 2:
                raise ValueError(f"{path}: only 8-bit RGB")
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + n
    raw = zlib.decompress(b"".join(idat))
    stride = w * 3
    rows: list = [None] * h
    wanted = set(range(h)) if want is None else set(want)
    prev = bytes(stride)
    need_prev = False
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)]
        if f == 1 and y not in wanted:
            need_prev = True
            continue
        if f == 1:
            out = bytearray(stride)
            for c in range(3):
                out[c::3] = bytes(v & 255 for v in accumulate(line[c::3]))
        elif f == 0:
            out = bytearray(line)
        else:
            if need_prev:
                raise ValueError(f"{path}: filter {f} after skipped rows")
            out = bytearray(stride)
            for i in range(stride):
                a = out[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                if f == 2:
                    v = b
                elif f == 3:
                    v = (a + b) // 2
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    v = a if pa <= pb and pa <= pc else b if pb <= pc else c
                out[i] = (line[i] + v) & 255
        rows[y] = bytes(out)
        prev = out
    return w, h, rows


def frames(d1: Path, dn: Path) -> int:
    errors = 0
    for pattern in ("frame_*.png", "wide_*.png"):
        names = sorted(p.name for p in d1.glob(pattern))
        common = [f for f in names if (dn / f).exists()]
        differ = []
        for f in common:
            a, b = (d1 / f).read_bytes(), (dn / f).read_bytes()
            if a != b and png_rows(d1 / f)[2] != png_rows(dn / f)[2]:
                differ.append(f)
        only = len(names) - len(common)
        extra = len([p for p in dn.glob(pattern) if not (d1 / p.name).exists()])
        print(f"{pattern[:-6]}: {len(common)} compared, {len(differ)} differ ({only} only in the 1x run, "
              f"{extra} only in the hi-res run)")
        for f in differ[:10]:
            print(f"  DIFFERS {f}")
        errors += len(differ)
    for name in ("wide.log",):
        a, b = d1 / name, dn / name
        if a.exists() and b.exists() and a.read_text() != b.read_text():
            print(f"  DIFFERS {name}")
            errors += 1
    return errors


def align_one(hi: Path, lo: Path, tol: int) -> tuple[int, int, int, int] | None:
    """(pixels, edge mismatches, inside mismatches, N) for one pair."""
    lw, lh, lrows = png_rows(lo)
    with open(hi, "rb") as fh:
        head = fh.read(24)
    hw, hh = struct.unpack(">II", head[16:24])
    if lw == 0 or hw % lw or hh % lh or hw // lw != hh // lh:
        return None
    n = hw // lw
    k = n // 2
    _, _, hrows = png_rows(hi, [y * n + k for y in range(lh)])
    edge = inside = 0
    for y in range(lh):
        hrow = hrows[y * n + k]
        lrow = lrows[y]
        dec = bytearray(lw * 3)
        for c in range(3):
            dec[c::3] = hrow[k * 3 + c::3 * n]
        if dec == lrow:
            continue
        for x in range(lw):
            i = x * 3
            if max(abs(dec[i] - lrow[i]), abs(dec[i + 1] - lrow[i + 1]), abs(dec[i + 2] - lrow[i + 2])) <= tol:
                continue
            here = lrow[i:i + 3]
            near = [lrow[i - 3:i] if x > 0 else here, lrow[i + 3:i + 6] if x + 1 < lw else here,
                    lrows[y - 1][i:i + 3] if y > 0 else here, lrows[y + 1][i:i + 3] if y + 1 < lh else here]
            if any(p != here for p in near):
                edge += 1
            else:
                inside += 1
    return lw * lh, edge, inside, n


def align(dn: Path, tol: int, worst: int) -> None:
    for prefix, base in (("hires", "frame"), ("hireswide", "wide")):
        results = []
        for p in sorted(dn.glob(f"{prefix}_*.png")):
            v = p.stem.split("_")[1]
            lo = dn / f"{base}_{v}.png"
            if not lo.exists():
                continue
            r = align_one(p, lo, tol)
            if r is not None:
                results.append((p.name, *r))
        if not results:
            print(f"{prefix}: none")
            continue
        px = sum(r[1] for r in results)
        edge = sum(r[2] for r in results)
        inside = sum(r[3] for r in results)
        clean = sum(1 for r in results if r[2] == 0 and r[3] == 0)
        print(f"{prefix}: {len(results)} pictures at {results[0][4]}x, {clean} exact; differing pixels "
              f"(tolerance {tol}): edge {edge} ({100.0 * edge / px:.3f}%), inside {inside} ({100.0 * inside / px:.3f}%)")
        for r in sorted(results, key=lambda r: -r[3])[:worst]:
            if r[3] > 0:
                print(f"  {r[0]}: inside {r[3]} ({100.0 * r[3] / r[1]:.2f}%), edge {r[2]}")


def run(args) -> int:
    out = Path(args.out)
    binary = args.bin or str(ROOT / "build/pc/cmake/xenogears")
    env_base = dict(os.environ, XG_SETTINGS="none", XG_HEADLESS="1", XG_CLOCK="step", XG_CD_SPEED="8",
                    SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", XG_PAD_SCRIPT=args.pad,
                    XG_QUIT_AT=str(args.quit), XG_CARD_DIR=str(out / "cards"), XG_ASPECT=args.aspect,
                    XG_UPSCALE_COLOR=args.color)
    for kv in args.env:
        k, v = kv.split("=", 1)
        env_base[k] = v
    dirs = {}
    for name, scale in (("1x", "1"), ("hires", str(args.scale))):
        d = out / name
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        (out / "cards").mkdir(exist_ok=True)
        env = dict(env_base, XG_DUMP_FRAMES=str(d), XG_UPSCALE=scale, XG_DUMP_HIRES="1" if scale != "1" else "0")
        with open(out / f"{name}.log", "w") as log:
            subprocess.run(["timeout", "-s", "KILL", str(args.timeout), binary], env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=False)
        dirs[name] = d
    errors = frames(dirs["1x"], dirs["hires"])
    align(dirs["hires"], args.tol, args.worst)
    print(f"hirescheck: {'pass' if errors == 0 else f'{errors} errors'}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("frames")
    f.add_argument("dir1x")
    f.add_argument("dirn")
    a = sub.add_parser("align")
    a.add_argument("dirn")
    for p in (a,):
        p.add_argument("--tol", type=int, default=0)
        p.add_argument("--worst", type=int, default=10)
    r = sub.add_parser("run")
    r.add_argument("--pad", required=True)
    r.add_argument("--quit", type=int, required=True)
    r.add_argument("--scale", type=int, default=4)
    r.add_argument("--color", default="15dither1x", help="upscale_color for the hi-res run (the alignment "
                   "check is exact in the 15-bit modes)")
    r.add_argument("--aspect", default="4:3tv")
    r.add_argument("--bin")
    r.add_argument("--out", default="/tmp/xg-hirescheck")
    r.add_argument("--env", action="append", default=[])
    r.add_argument("--timeout", type=int, default=900)
    r.add_argument("--tol", type=int, default=0)
    r.add_argument("--worst", type=int, default=10)
    args = ap.parse_args()
    if args.cmd == "frames":
        errors = frames(Path(args.dir1x), Path(args.dirn))
        print(f"hirescheck: {'pass' if errors == 0 else f'{errors} errors'}")
        return errors
    if args.cmd == "align":
        align(Path(args.dirn), args.tol, args.worst)
        return 0
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
