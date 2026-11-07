#!/usr/bin/env python3
"""Check the PC port's true widescreen against its 4:3 picture (include/pc_wide.h).

    python tools/widecheck.py compare DIR_4_3 DIR_WIDE
    python tools/widecheck.py run --pad PAD --quit N [--aspect 16:9] [--bin B]
                                  [--disc CUE] [--out DIR] [--env K=V ...]

`run` plays the same pad script twice, headless with XG_CLOCK=step: once at
aspect 4:3 and once wide (XG_ASPECT), both with XG_DUMP_FRAMES, then compares.

The checks:
  1. frames: every frame_<vblank>.png both runs wrote is the same. VRAM, and
     so the game, must not notice the wide picture: extra primitives must be
     wide-only, use no game buffer, and leave the GTE as the PS1 has it. A
     difference is an error.
  2. centre: each wide_<vblank>.png that wide.log marks clean must hold its
     frame_<vblank>.png in its middle (from x = m, y = top). A shifted wide
     picture (2D the game moved with pcWidePrim's dx, as the HUD anchored to
     the wide edges, or drew wide-only over the centre) is compared outside
     the places wide.log lists for it (masks=x0,y0,x1,y1;... in frame_ pixels:
     where each moved element was and where it landed); one shifted by a copy
     whose places are not known (no masks=) is only counted. So anchored-HUD
     runs are checked without XG_WIDE_NOSHIFT=1. A difference is an error.
  3. counts: how many wide pictures were 4:3 (clean=0) and why, and how many
     frames had no wide picture at all (wide.log none=1: a movie, a kind not
     ready, a block) and why.

Exit status: the number of errors (0: pass).
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DISC1 = "Xenogears (USA) (Disc 1)"


def read_png(path: Path) -> tuple[int, int, bytes]:
    """8-bit RGB PNG (as port/src/gpu/dump.c writes it) -> (w, h, rgb)."""
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
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


def read_log(d: Path) -> dict[int, dict[str, str]]:
    log = {}
    p = d / "wide.log"
    if not p.exists():
        return log
    for line in p.read_text().splitlines():
        head, _, why = line.partition(" why=")
        parts = head.split()
        fields = dict(kv.split("=", 1) for kv in parts[1:])
        if why:
            fields["why"] = why
        log[int(parts[0])] = fields
    return log


def parse_masks(s: str) -> list[tuple[int, int, int, int]]:
    """wide.log's masks= value: inclusive rectangles in frame_ pixels."""
    out = []
    for r in s.split(";"):
        if r:
            x0, y0, x1, y1 = (int(v) for v in r.split(","))
            out.append((x0, y0, x1, y1))
    return out


def row_spans(masks: list[tuple[int, int, int, int]], y: int, w: int) -> list[tuple[int, int]]:
    """The unmasked [a, b) column spans of row y of a frame w pixels wide."""
    cut = sorted((x0, x1) for x0, y0, x1, y1 in masks if y0 <= y <= y1)
    spans = []
    x = 0
    for x0, x1 in cut:
        if x0 > x:
            spans.append((x, min(x0, w)))
        x = max(x, x1 + 1)
    if x < w:
        spans.append((x, w))
    return spans


def compare(d43: Path, dwide: Path) -> int:
    errors = 0
    frames = sorted(p.name for p in d43.glob("frame_*.png"))
    common = [f for f in frames if (dwide / f).exists()]
    differ = [f for f in common if (d43 / f).read_bytes() != (dwide / f).read_bytes()]
    print(f"frames: {len(common)} compared, {len(differ)} differ"
          f" ({len(frames) - len(common)} only in the 4:3 run)")
    for f in differ[:10]:
        print(f"  DIFFERS {f}")
    errors += len(differ)

    log = read_log(dwide)
    checked = shifted = masked = mismatched = 0
    masked_px = 0
    why = Counter()
    for p in sorted(dwide.glob("wide_*.png")):
        v = int(p.stem.split("_")[1])
        fields = log.get(v, {})
        if fields.get("clean") == "0":
            why[fields.get("why", "?")] += 1
            continue
        masks = []
        if fields.get("shifted") == "1":
            if "masks" not in fields:
                shifted += 1
                continue
            masks = parse_masks(fields["masks"])
        frame = dwide / f"frame_{v:06d}.png"
        if not frame.exists():
            continue
        m = int(fields.get("m", "0"))
        ww, wh, wrgb = read_png(p)
        fw, fh, frgb = read_png(frame)
        top = int(fields.get("top", "0"))
        bottom = int(fields.get("bottom", "0"))
        ok = top >= 0 and bottom >= 0 and wh == fh + top + bottom and ww >= fw + 2 * m
        for y in range(fh) if ok else []:
            wrow = ((y + top) * ww + m) * 3
            frow = y * fw * 3
            for a, b in row_spans(masks, y, fw) if masks else [(0, fw)]:
                if wrgb[wrow + a * 3:wrow + b * 3] != frgb[frow + a * 3:frow + b * 3]:
                    ok = False
                    break
            if not ok:
                break
        if masks:
            masked += 1
            masked_px += fw * fh - sum(b - a for y in range(fh) for a, b in row_spans(masks, y, fw))
        checked += 1
        if not ok:
            mismatched += 1
            if mismatched <= 10:
                print(f"  CENTRE DIFFERS {p.name}")
    print(f"centre: {checked} wide pictures checked, {mismatched} differ, {shifted} shifted (not checked)")
    if masked:
        print(f"  of them {masked} shifted, compared outside their moved 2D"
              f" (on average {masked_px / masked:.0f} pixels left out)")
    errors += mismatched
    for k, n in why.most_common():
        print(f"4:3 frames: {n} ({k})")
    none = Counter(f.get("why", "?") for f in log.values() if f.get("none") == "1")
    for k, n in none.most_common():
        print(f"4:3 frames without a wide picture: {n} ({k})")
    print(f"widecheck: {'pass' if errors == 0 else f'{errors} errors'}")
    return errors


def run(args) -> int:
    out = Path(args.out)
    binary = args.bin or str(ROOT / "build/pc/cmake/xenogears")
    env_base = dict(os.environ, XG_SETTINGS="none", XG_HEADLESS="1", XG_CLOCK="step", XG_CD_SPEED="8",
                    SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", XG_PAD_SCRIPT=args.pad,
                    XG_QUIT_AT=str(args.quit), XG_CARD_DIR=str(out / "cards"))
    # With no disc in the settings (XG_SETTINGS=none) and none on the command
    # line, the port opens its disc setup screen and waits there.
    env_base.setdefault("XG_DISC", args.disc)
    for kv in args.env:
        k, v = kv.split("=", 1)
        env_base[k] = v
    dirs = {}
    for name, aspect in (("4x3", "4:3"), ("wide", args.aspect)):
        d = out / name
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        (out / "cards").mkdir(exist_ok=True)
        env = dict(env_base, XG_DUMP_FRAMES=str(d), XG_ASPECT=aspect)
        with open(out / f"{name}.log", "w") as log:
            subprocess.run(["timeout", "-s", "KILL", str(args.timeout), binary], env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=False)
        dirs[name] = d
    return compare(dirs["4x3"], dirs["wide"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compare")
    c.add_argument("dir43")
    c.add_argument("dirwide")
    r = sub.add_parser("run")
    r.add_argument("--pad", required=True)
    r.add_argument("--quit", type=int, required=True)
    r.add_argument("--aspect", default="16:9")
    r.add_argument("--bin")
    r.add_argument("--disc", default=str(ROOT / "bin" / DISC1 / f"{DISC1}.cue"),
                   help="disc image (.cue; default the US disc 1 in bin/), unless XG_DISC is set")
    r.add_argument("--out", default=str(Path.home() / ".cache/xg-widecheck"))
    r.add_argument("--env", action="append", default=[])
    r.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()
    if args.cmd == "compare":
        return compare(Path(args.dir43), Path(args.dirwide))
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
