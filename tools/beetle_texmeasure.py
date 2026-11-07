#!/usr/bin/env python3
"""How Beetle PSX HW names, dumps and replaces textures, measured black-box.

The Beetle PSX HW core runs as a black box (tools/beetle_front.c's headless
Vulkan context, tools/beetle.py) with our own GPU test program loaded as its
content (tools/beetle_runner.s, a PS-X EXE executing GP0/GP1 lists from a RAM
mailbox). Each case uploads known texels and palettes, draws them, and reads
what the core wrote into '<content>-texture-dump/' or shows from
'<content>-texture-replacements/'. No emulator source is read: the scheme the
port implements (port/src/gpu/texture_beetle.c, docs/BEETLE_PACKS.md, beads
xg-mrj.1) is what these cases print.

    python tools/beetle_texmeasure.py search    # which standard hash names a dump (all candidates)
    python tools/beetle_texmeasure.py naming    # depths, sizes, positions, copies, fills, palettes
    python tools/beetle_texmeasure.py replace   # scales, PNG kinds, alpha classes, names, per-texel owners
    python tools/beetle_texmeasure.py all

Needs the HW core (--core, $XG_BEETLE_HW_CORE or RetroArch's
mednafen_psx_hw_libretro.so), a BIOS in the system folder ($XG_BEETLE_SYSDIR)
and a Vulkan driver ($XG_BEETLE_VK_DEVICE picks the GPU, default Intel).
Scratch goes to --out (default ~/.cache/xg-beetle/texmeasure). Exit status:
the number of cases whose observation differs from the scheme.
"""
from __future__ import annotations

import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import beetle  # noqa: E402

MAILBOX = 0x20000
FAILS: list[str] = []


# ---------------------------------------------------------------- the runner --

def build_runner(out: Path) -> Path:
    """beetle_runner.s as a PS-X EXE (psx-spx: CDROM File Formats, PS-X EXE)."""
    cross = ROOT / "tools/toolchain/bin/mips-linux-gnu-"
    out.mkdir(parents=True, exist_ok=True)
    obj, elf, raw = out / "runner.o", out / "runner.elf", out / "runner.bin"
    subprocess.run([f"{cross}as", "-EL", "-march=r3000", "-mtune=r3000", "-o", obj, HERE / "beetle_runner.s"],
                   check=True)
    subprocess.run([f"{cross}ld", "-EL", "-nostdlib", "-Ttext", "0x80010000", "-e", "_start", "-o", elf, obj],
                   check=True)
    subprocess.run([f"{cross}objcopy", "-O", "binary", "-j", ".text", elf, raw], check=True)
    code = raw.read_bytes()
    code += b"\0" * (-len(code) % 0x800)
    head = bytearray(0x800)
    head[0:8] = b"PS-X EXE"
    struct.pack_into("<IIIIIIIIII", head, 0x10, 0x80010000, 0, 0x80010000, len(code), 0, 0, 0, 0, 0x8001FF00, 0)
    mark = b"Sony Computer Entertainment Inc. for North America area"
    head[0x4C:0x4C + len(mark)] = mark
    exe = out / "runner.exe"
    exe.write_bytes(bytes(head) + code)
    return exe


class Ops:
    """A runner operation list (beetle_runner.s's mailbox format)."""

    def __init__(self) -> None:
        self.words: list[int] = []

    def gp1(self, w: int) -> None:
        self.words += [0x04000000, w]

    def gp0(self, words: list[int]) -> None:
        self.words += [0x01000000 | len(words)] + list(words)

    def load(self, x: int, y: int, w: int, h: int, px: list[int]) -> None:
        """GP0(A0h) of w x h halfwords, the data by DMA2."""
        data = list(px) + ([0] if len(px) & 1 else [])
        words = [data[k] | data[k + 1] << 16 for k in range(0, len(data), 2)]
        words += [0] * (-len(words) % 16)
        self.words += [0x01000003, 0xA0000000, y << 16 | x, h << 16 | w, 0x02000000 | len(words)] + words

    def load_cpu(self, x: int, y: int, w: int, h: int, px: list[int]) -> None:
        """The same, the data written to GP0 by the CPU."""
        data = list(px) + ([0] if len(px) & 1 else [])
        words = [data[k] | data[k + 1] << 16 for k in range(0, len(data), 2)]
        self.words += [0x01000003, 0xA0000000, y << 16 | x, h << 16 | w, 0x06000000 | len(words)] + words


def tpage(x: int, y: int, depth: int) -> int:
    return x // 64 | (y // 256) << 4 | depth << 7


def clut(x: int, y: int) -> int:
    return y << 6 | x >> 4


def sprite(sx: int, sy: int, u: int, v: int, w: int, h: int, cl: int = 0, x: int = 0, y: int = 0, depth: int = 0,
           semi: bool = False) -> list[int]:
    """E1 (the page) and a textured rectangle (GP0 64h, 66h semi-transparent)."""
    return [0xE1000400 | tpage(x, y, depth), (0x66 if semi else 0x64) << 24 | 0x808080, sy << 16 | sx,
            cl << 16 | v << 8 | u, h << 16 | w]


def crc(words: list[int]) -> int:
    return zlib.crc32(struct.pack(f"<{len(words)}H", *words))


def rnd(n: int, lo: int = 0, hi: int = 65536) -> list[int]:
    return [random.randrange(lo, hi) for _ in range(n)]


class Core:
    """The HW core with the runner, texture dumping or replacement on."""

    def __init__(self, out: Path, core: Path, replace: dict[str, bytes] | None = None, scale: str = "1x(native)"):
        self.dir = out
        shutil.rmtree(out / "runner-texture-dump", ignore_errors=True)
        shutil.rmtree(out / "runner-texture-replacements", ignore_errors=True)
        (out / "runner-texture-dump").mkdir(parents=True)
        rep = out / "runner-texture-replacements"
        rep.mkdir()
        for name, data in (replace or {}).items():
            (rep / name).parent.mkdir(parents=True, exist_ok=True)
            (rep / name).write_bytes(data)
        opts = {"beetle_psx_hw_renderer": "hardware_vk", "beetle_psx_hw_track_textures": "enabled",
                "beetle_psx_hw_dump_textures": "disabled" if replace is not None else "enabled",
                "beetle_psx_hw_replace_textures": "enabled" if replace is not None else "disabled",
                "beetle_psx_hw_skip_bios": "enabled", "beetle_psx_hw_internal_resolution": scale}
        self.b = beetle.Beetle(out / "runner.exe", core=core, savedir=out / "save", options=opts, hw=True,
                               option_log=out / "options.log")
        if not self.b.lib.bf_hw_active():
            raise SystemExit("beetle_texmeasure: the core took no Vulkan context")
        for _ in range(3000):
            self.b.run(1)
            if self.b.u32(MAILBOX + 8) == 0x52554E52:
                break
        else:
            raise SystemExit("beetle_texmeasure: the runner did not start")
        self.seq = self.b.u32(MAILBOX)
        self.seen: set[str] = set()
        o = Ops()
        o.gp1(0x00000000), o.gp1(0x03000000), o.gp1(0x08000001), o.gp1(0x05000000)
        o.gp1(0x06C60260), o.gp1(0x07042018)
        o.gp0([0xE1000400, 0xE2000000, 0xE3000000, 0xE4000000 | 511 << 10 | 1023, 0xE5000000, 0xE6000000])
        o.gp0([0x02000000, 0, 512 << 16 | 1024])
        self.run(o)

    def run(self, ops: Ops, after: int = 2) -> None:
        words = ops.words + [0]
        self.b.write(MAILBOX + 16, struct.pack(f"<{len(words)}I", *words))
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        self.b.write(MAILBOX, struct.pack("<I", self.seq))
        for _ in range(20000):
            self.b.run(1)
            if self.b.u32(MAILBOX + 4) == self.seq:
                break
        else:
            raise SystemExit("beetle_texmeasure: the runner did not finish a list")
        self.b.run(after)

    def dumped(self, ops: Ops) -> list[str]:
        """Run ops; the dump's new file names."""
        self.run(ops)
        new = sorted(p.name for p in (self.dir / "runner-texture-dump").iterdir() if p.name not in self.seen)
        self.seen.update(new)
        return new

    def frame(self) -> tuple[int, int, bytes]:
        """The last frame, 8-bit RGB rows."""
        import ctypes
        ptr = self.b.lib.bf_frame(self.b.dims)
        w, h = self.b.dims[0], self.b.dims[1]
        return w, h, ctypes.string_at(ptr, w * h * 3)

    def close(self) -> None:
        self.b.close()


def case(name: str, observed, expected) -> None:
    ok = observed == expected
    if not ok:
        FAILS.append(name)
    print(f"{'ok  ' if ok else 'DIFF'} {name}: observed {observed}" + ("" if ok else f", the scheme says {expected}"))


def png_size(path: Path) -> tuple[int, int]:
    return struct.unpack(">II", path.read_bytes()[16:24])


# ---------------------------------------------------------------- the cases --

def cmd_search(args, core: Path) -> None:
    """One 4-bit upload and palette, drawn once: which standard hash of which bytes is the name."""
    random.seed(1)
    tex = rnd(256)
    pal = [0] + rnd(15, 1, 32768)
    c = Core(args.out, core)
    o = Ops()
    o.load(640, 0, 16, 16, tex)
    o.load(0, 480, 16, 1, pal)
    o.gp0(sprite(20, 20, 0, 0, 64, 16, clut(0, 480), 640, 0))
    names = c.dumped(o)
    c.close()
    print("dumped:", names)
    upload, palette = (int(x, 16) for x in names[0][:-4].split("-"))
    T = struct.pack("<256H", *tex)
    P = struct.pack("<16H", *pal)
    inputs = {"texels LE": T, "texels BE": struct.pack(">256H", *tex), "texels + rect": struct.pack("<4H", 640, 0, 16, 16) + T,
              "palette LE": P, "palette BE": struct.pack(">16H", *pal), "palette + rect": struct.pack("<4H", 0, 480, 16, 1) + P,
              "palette as 256": P + bytes(480)}
    for iname, data in inputs.items():
        for hname, f in HASHES.items():
            v = f(data)
            if v in (upload, palette):
                print(f"  {'upload' if v == upload else 'palette'} = {hname}({iname})")
    case("search: upload name", f"{upload:x}", f"{zlib.crc32(T):x}")
    case("search: palette name", f"{palette:x}", f"{zlib.crc32(P):x}")


def cmd_naming(args, core: Path) -> None:
    random.seed(7)
    c = Core(args.out, core)
    pal16 = [0] + rnd(15, 1, 32768)
    pal256 = [0] + rnd(255, 1, 32768)
    o = Ops()
    o.load(0, 480, 16, 1, pal16)
    o.load(0, 481, 256, 1, pal256)
    c.run(o)
    P16, P256 = crc(pal16), crc(pal256)
    # depths and sizes: the whole upload at the draw's depth
    t = rnd(256)
    o = Ops()
    o.load(640, 0, 16, 16, t)
    o.gp0(sprite(0, 0, 8, 2, 8, 4, clut(0, 480), 640, 0, 0))
    o.gp0(sprite(0, 20, 0, 0, 32, 16, clut(0, 481), 640, 0, 1))
    o.gp0(sprite(0, 40, 0, 0, 16, 16, 0, 640, 0, 2))
    got = c.dumped(o)
    want = sorted([f"{crc(t):x}-{P16:x}.png", f"{crc(t):x}-{P256:x}.png", f"{crc(t):x}.png"])
    case("4/8/15-bit draws of one upload: three dumps", got, want)
    sizes = {n: png_size(c.dir / "runner-texture-dump" / n) for n in got}
    case("dump sizes (a sub-rectangle drawn: the whole upload) at 4, 8, 15 bits",
         [sizes.get(n) for n in (f"{crc(t):x}-{P16:x}.png", f"{crc(t):x}-{P256:x}.png", f"{crc(t):x}.png")],
         [(64, 16), (32, 16), (16, 16)])
    # an upload over two pages, an odd size, the CPU's GP0 writes
    wide, odd, cpu = rnd(128 * 16), rnd(3 * 5), rnd(256)
    o = Ops()
    o.load(0, 256, 128, 16, wide)
    o.load(700, 300, 3, 5, odd)
    o.load_cpu(960, 128, 16, 16, cpu)
    o.gp0(sprite(100, 0, 0, 0, 16, 4, clut(0, 480), 0, 256))
    o.gp0(sprite(100, 20, 240, 44, 12, 5, clut(0, 480), 640, 256))
    o.gp0(sprite(100, 40, 0, 128, 64, 16, clut(0, 480), 960, 0))
    got = c.dumped(o)
    case("wide (512 texels), odd (3x5 hw), CPU-written uploads", got,
         sorted([f"{crc(wide):x}-{P16:x}.png", f"{crc(odd):x}-{P16:x}.png", f"{crc(cpu):x}-{P16:x}.png"]))
    case("their sizes", [png_size(c.dir / "runner-texture-dump" / n) for n in
                         (f"{crc(wide):x}-{P16:x}.png", f"{crc(odd):x}-{P16:x}.png")], [(512, 16), (12, 5)])
    # ownership per halfword: a 1x1 upload inside a 64x64 one, probes beside it
    A, B = rnd(64 * 64), [0x5555]
    o = Ops()
    o.load(512, 64, 64, 64, A)
    o.load(533, 77, 1, 1, B)
    c.run(o)
    owners = []
    for k, (x, y) in enumerate([(532, 77), (533, 77), (534, 77), (533, 76), (533, 78)]):
        p = [0] + rnd(15, 1, 32768)
        o = Ops()
        o.load(16 * (k + 1), 482, 16, 1, p)
        o.gp0(sprite(0, 60, (x - 512) * 4, y, 4, 1, clut(16 * (k + 1), 482), 512, 0))
        got = c.dumped(o)
        owners.append("B" if got == [f"{crc(B):x}-{crc(p):x}.png"] else "A" if got == [f"{crc(A):x}-{crc(p):x}.png"] else str(got))
    case("owners of (532..534, 77), (533, 76), (533, 78)", owners, ["A", "B", "A", "A", "A"])
    # a fill lets go (the rest still dumps as the upload), a drawing does not, a copy carries the source's name
    F, G, H = rnd(256), rnd(256), rnd(256)
    o = Ops()
    o.load(832, 0, 16, 16, F)
    o.gp0([0x02000000, 4 << 16 | 832, 2 << 16 | 16])
    o.load(896, 0, 16, 16, G)
    o.gp0([0x60FF0000, 2 << 16 | 896, 2 << 16 | 4])
    o.load(960, 256, 16, 16, H)
    o.gp0([0x02000000, 260 << 16 | 960, 2 << 16 | 16])
    o.gp0([0x80000000, 256 << 16 | 960, 320 << 16 | 960, 16 << 16 | 16])
    o.gp0(sprite(0, 80, 0, 0, 64, 16, clut(0, 480), 832, 0))
    o.gp0(sprite(0, 100, 0, 0, 64, 16, clut(0, 480), 896, 0))
    o.gp0(sprite(0, 120, 0, 64, 64, 16, clut(0, 480), 960, 256))
    got = c.dumped(o)
    case("filled into, drawn into, copied after a fill: the uploads' own names", got,
         sorted([f"{crc(F):x}-{P16:x}.png", f"{crc(G):x}-{P16:x}.png", f"{crc(H):x}-{P16:x}.png"]))
    # palettes: owned by one upload (a sub-range of a wider one), else 'missing'
    T = rnd(256)
    W = rnd(64, 1, 32768)
    o = Ops()
    o.load(640, 64, 16, 16, T)
    o.load(512, 500, 64, 1, W)
    o.gp0(sprite(160, 0, 0, 64, 64, 16, clut(544, 500), 640, 0))
    case("a palette inside a wider CLUT upload", c.dumped(o), [f"{crc(T):x}-{crc(W[32:48]):x}.png"])
    T2 = rnd(256)
    o = Ops()
    o.load(704, 64, 16, 16, T2)
    o.load(515, 500, 1, 1, [0x1234])
    o.gp0(sprite(160, 20, 0, 64, 64, 16, clut(512, 500), 704, 0))
    case("a palette with an entry another upload wrote", c.dumped(o), [f"{crc(T2):x}-missing.png"])
    T3, Q = rnd(256), [0] + rnd(15, 1, 32768)
    o = Ops()
    o.load(768, 64, 16, 16, T3)
    o.load(576, 500, 16, 1, Q)
    o.gp0([0x68001F00, 500 << 16 | 579])
    o.gp0(sprite(160, 40, 0, 64, 64, 16, clut(576, 500), 768, 0))
    case("a palette with an entry drawn over", c.dumped(o), [f"{crc(T3):x}-{crc(Q):x}.png"])
    # the dump's pixels: the upload as sent, floor(c5 * 255 / 31), alpha 0 / 127 (STP) / 255
    P = [0x0000, 0x8000, 0x801F, 0x001F, 0x7FFF] + [0x03E0] * 11
    T4 = [0x3210] * 256
    o = Ops()
    o.load(768, 500, 16, 1, P)
    o.load(832, 64, 16, 16, T4)
    o.gp0(sprite(160, 60, 0, 64, 64, 16, clut(768, 500), 832, 0))
    n = c.dumped(o)[0]
    from PIL import Image
    im = Image.open(c.dir / "runner-texture-dump" / n).convert("RGBA")
    case("dump pixels of 0000, 8000, 801F, 001F", [im.getpixel((x, 0)) for x in range(4)],
         [(0, 0, 0, 0), (0, 0, 0, 127), (255, 0, 0, 127), (255, 0, 0, 255)])
    c.close()


def png_bytes(w: int, h: int, rgba: tuple[int, int, int, int] | list, bits: int = 8, kind: int = 6) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    if isinstance(rgba, tuple):
        rows = [bytes(rgba) * w] * h
    else:
        rows = [bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h)]
    if bits == 16:
        rows = [bytes(b for v in r for b in (v, v)) for r in rows]
    raw = b"".join(b"\0" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bits, kind, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def cmd_replace(args, core: Path) -> None:
    random.seed(53)
    pal = [0] + rnd(15, 1, 32768)
    P = crc(pal)
    T = [rnd(256) for _ in range(16)]
    T15a, T15b = rnd(64 * 16), rnd(64 * 16)
    MAG = (255, 0, 255, 255)
    files: dict[str, bytes] = {}
    alphas = [0, 1, 64, 127, 128, 191, 192, 254, 255]
    band = []
    for y in range(16):
        for x in range(64):
            band += [255, 0, 0, alphas[min(len(alphas) - 1, x * len(alphas) // 64)]]
    files[f"{crc(T[0]):x}-{P:x}.png"] = png_bytes(64, 16, band)
    scales = [(1, 1), (2, 2), (3, 3), (4, 4), (8, 8), (16, 16), (2, 3)]
    for i, (sx, sy) in enumerate(scales):
        files[f"{crc(T[1 + i]):x}-{P:x}.png"] = png_bytes(64 * sx, 16 * sy, MAG)
    files[f"{crc(T[8]):x}-{P:x}.png"] = png_bytes(64, 16, MAG, bits=16)
    files["sub/" + f"{crc(T[9]):x}-{P:x}.png"] = png_bytes(64, 16, MAG)
    files[f"0{crc(T[10]):x}-{P:x}.png"] = png_bytes(64, 16, MAG)
    files[f"{crc(T15a):x}.png"] = png_bytes(64, 16, MAG)
    files[f"{crc(T15b):x}-0.png"] = png_bytes(64, 16, MAG)
    c = Core(args.out, core, replace=files, scale="2x")
    def draws(o: Ops, first: bool) -> None:
        o.gp0([0x027F7F7F, 0, 256 << 16 | 512])  # grey
        if first:
            o.load(0, 480, 16, 1, pal)
            for k in range(11):
                o.load(512 + (k % 4) * 16, 256 + (k // 4) * 16, 16, 16, T[k])
            o.load(768, 256, 64, 16, T15a)
            o.load(768, 272, 64, 16, T15b)
        for k in range(11):
            o.gp0(sprite((k % 4) * 70, (k // 4) * 20, (k % 4) * 64, (k // 4) * 16, 64, 16, clut(0, 480), 512, 256))
        o.gp0(sprite(0, 80, 0, 0, 64, 16, clut(0, 480), 512, 256, semi=True))
        o.gp0(sprite(0, 100, 0, 0, 64, 16, 0, 768, 256, 2) + sprite(70, 100, 0, 16, 64, 16, 0, 768, 256, 2)[1:])
    for first in (True, False):  # the core loads replacements in the background: the second pass shows them
        o = Ops()
        draws(o, first)
        c.run(o, after=3)
    w, h, rgb = c.frame()
    s = w // 350 if w >= 350 else 1  # the frame: overscan included, at the internal scale
    def px(x: int, y: int) -> tuple[int, int, int]:
        X, Y = (x + 15) * s + s // 2, y * s + s // 2
        return tuple(rgb[(Y * w + X) * 3:(Y * w + X) * 3 + 3])
    def replaced(k: int) -> bool:
        return px((k % 4) * 70 + 40, (k // 4) * 20 + 8) == (255, 0, 255)
    case("scales 1, 2, 3, 4, 8, 16, 2x3 taken", [replaced(1 + i) for i in range(len(scales))],
         [True, True, False, True, True, True, False])
    case("a 16-bit PNG, a subfolder, a leading zero taken", [replaced(8), replaced(9), replaced(10)], [False, False, False])
    case("15-bit '<upload>.png' and '<upload>-0.png' taken", [px(40, 108) == (255, 0, 255), px(110, 108) == (255, 0, 255)],
         [False, True])
    grey = (123, 123, 123)
    opaque = [px(x * 64 // len(alphas) + 3, 8) for x in range(len(alphas))]
    semi = [px(x * 64 // len(alphas) + 3, 88) for x in range(len(alphas))]
    case("alpha 0 clear, 1..255 drawn", [p != px(250, 140) for p in opaque], [False] + [True] * 8)
    case("alpha 1..254 STP (blended in a semi-transparent draw), 255 not",
         [p != (255, 0, 0) for p in semi[1:]], [True] * 7 + [False])
    del grey
    c.close()


# ------------------------------------------------- standard hash candidates --
# Written from the algorithms' public specifications (check values below).

M32, M64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF


def _table(poly: int, reflect: bool) -> list[int]:
    t = []
    for i in range(256):
        c = i if reflect else i << 24
        for _ in range(8):
            if reflect:
                c = (c >> 1) ^ poly if c & 1 else c >> 1
            else:
                c = ((c << 1) ^ poly) & M32 if c & 0x80000000 else (c << 1) & M32
        t.append(c)
    return t


_TC, _TB = _table(0x82F63B78, True), _table(0x04C11DB7, False)


def _crc32c(d: bytes) -> int:
    c = M32
    for b in d:
        c = _TC[(c ^ b) & 255] ^ (c >> 8)
    return c ^ M32


def _crc_bzip2(d: bytes, xorout: int = M32) -> int:
    c = M32
    for b in d:
        c = ((c << 8) & M32) ^ _TB[((c >> 24) ^ b) & 255]
    return c ^ xorout


def _fnv(d: bytes, bits: int, a: bool) -> int:
    h, p, m = (0x811C9DC5, 0x01000193, M32) if bits == 32 else (0xCBF29CE484222325, 0x100000001B3, M64)
    for b in d:
        h = ((h ^ b) * p) & m if a else ((h * p) & m) ^ b
    return h


def _rotl(x: int, r: int, m: int = M32, n: int = 32) -> int:
    return ((x << r) | (x >> (n - r))) & m


def _murmur3(d: bytes, seed: int = 0) -> int:
    c1, c2, h, n = 0xCC9E2D51, 0x1B873593, seed, len(d) // 4
    for i in range(n):
        k = struct.unpack_from("<I", d, i * 4)[0]
        h ^= (_rotl((k * c1) & M32, 15) * c2) & M32
        h = (_rotl(h, 13) * 5 + 0xE6546B64) & M32
    k = int.from_bytes(d[n * 4:], "little")
    if len(d) & 3:
        h ^= (_rotl((k * c1) & M32, 15) * c2) & M32
    h ^= len(d)
    h ^= h >> 16; h = (h * 0x85EBCA6B) & M32; h ^= h >> 13; h = (h * 0xC2B2AE35) & M32
    return h ^ (h >> 16)


def _xxh32(d: bytes, seed: int = 0) -> int:
    p1, p2, p3, p4, p5 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1
    i, n = 0, len(d)
    if n >= 16:
        v = [(seed + p1 + p2) & M32, (seed + p2) & M32, seed, (seed - p1) & M32]
        while i + 16 <= n:
            for j in range(4):
                v[j] = (_rotl((v[j] + struct.unpack_from("<I", d, i)[0] * p2) & M32, 13) * p1) & M32
                i += 4
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & M32
    else:
        h = (seed + p5) & M32
    h = (h + n) & M32
    while i + 4 <= n:
        h = (_rotl((h + struct.unpack_from("<I", d, i)[0] * p3) & M32, 17) * p4) & M32
        i += 4
    while i < n:
        h = (_rotl((h + d[i] * p5) & M32, 11) * p1) & M32
        i += 1
    h ^= h >> 15; h = (h * p2) & M32; h ^= h >> 13; h = (h * p3) & M32
    return h ^ (h >> 16)


def _djb2(d: bytes) -> int:
    h = 5381
    for b in d:
        h = (h * 33 + b) & M32
    return h


def _oaat(d: bytes) -> int:
    h = 0
    for b in d:
        h = (h + b) & M32; h = (h + (h << 10)) & M32; h ^= h >> 6
    h = (h + (h << 3)) & M32; h ^= h >> 11
    return (h + (h << 15)) & M32


HASHES = {
    "crc32 (zlib)": zlib.crc32, "crc32c": _crc32c, "crc32/bzip2": _crc_bzip2, "crc32/mpeg2": lambda d: _crc_bzip2(d, 0),
    "adler32": zlib.adler32, "fnv1-32": lambda d: _fnv(d, 32, False), "fnv1a-32": lambda d: _fnv(d, 32, True),
    "fnv1a-64 low": lambda d: _fnv(d, 64, True) & M32, "fnv1a-64 high": lambda d: _fnv(d, 64, True) >> 32,
    "murmur3-32": _murmur3, "xxh32": _xxh32, "djb2": _djb2, "one-at-a-time": _oaat,
}


def selftest() -> None:
    d = b"123456789"
    assert zlib.crc32(d) == 0xCBF43926 and _crc32c(d) == 0xE3069283
    assert _crc_bzip2(d) == 0xFC891918 and _crc_bzip2(d, 0) == 0x0376E6E7
    assert _fnv(b"a", 32, True) == 0xE40C292C and _fnv(b"a", 64, True) == 0xAF63DC4C8601EC8C
    assert _xxh32(b"") == 0x02CC5D05 and _murmur3(b"hello") == 0x248BFA47


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["search", "naming", "replace", "all"])
    ap.add_argument("--core", type=Path)
    ap.add_argument("--out", type=Path, default=beetle.CACHE / "texmeasure")
    args = ap.parse_args()
    selftest()
    core = Path(args.core or os.environ.get("XG_BEETLE_HW_CORE") or beetle.DEFAULT_HW_CORE)
    if not core.exists():
        raise SystemExit(f"beetle_texmeasure: no HW core at {core}")
    build_runner(args.out)
    for what in (["search", "naming", "replace"] if args.what == "all" else [args.what]):
        print(f"== {what}")
        {"search": cmd_search, "naming": cmd_naming, "replace": cmd_replace}[what](args, core)
    print(f"beetle_texmeasure: {len(FAILS)} case(s) differ from the scheme" + (": " + ", ".join(FAILS) if FAILS else ""))
    return len(FAILS)


if __name__ == "__main__":
    sys.exit(main())
