#!/usr/bin/env python3
"""Link a module's PC object without the game C's own retail bytes; the player's disc fills them.

    python tools/pc_srcdata.py --ver us [--ld LD] [--objcopy OBJCOPY] \\
        MODULE OUT_PARTIAL OUT_C OUT_SYMS GEN_OBJECT GAME_OBJECT...

The game C in src/ keeps some of the PS1's initialised data as C initialisers:
decompiled tables, debug strings, file names. Byte for byte they are the retail
module's, so the executable would carry them (tools/payloadcheck.py, 'src').
port/CMakeLists.txt links each module with this instead of a plain `ld -r`:

  - in each game object, every run of retail bytes in a data section (16 bytes
    or more of informative windows the module's file holds, as payloadcheck
    finds them) is zeroed, but for the bytes relocations fill (pointers): in
    the file itself (objcopy --update-section would drop the relocations);
  - such a section becomes part of the module's data section (xgdata_<module>:
    writable, and kept by the overlay resets like the rest of the module's
    data), with a global symbol at its start (OUT_SYMS lists them for objcopy
    --keep-global-symbols); the objects are changed in copies, never in place;
  - the objects (changed or not) and the module's generated object are linked
    (ld -r -d) into OUT_PARTIAL, as before;
  - OUT_C gets the copies that put the bytes back: (symbol + offset, offset in
    the module's file, size), their count and a hash of the bytes, as
    xgSrcCopies_<module>, xgSrcCount_<module>, xgSrcHash_<module>.

port/src/system/gamedata.c applies them at startup with the module data
(tools/pc_link.py's portSrcData table points at them). A run the file holds in
another order (the PC lays strings out its own way) is copied in pieces, each
from wherever the file holds it, but never from the bytes each disc has its own
(main's file tables). The PS1 build is not touched. XG_SRCDATA=off
in the environment links the objects unchanged (an empty copy list), to compare
builds with and without it.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import payloadcheck  # noqa: E402
import pc_link  # noqa: E402

MIN = 16  # payloadcheck's shortest run
# Data sections that hold no initialised game data (unwind tables, constructor lists).
SKIP = re.compile(r"^\.(eh_frame|gcc_except_table|init_array|fini_array|ctors|dtors|note|comment|debug)")


def data_sections(objdump: str, obj: str) -> list[tuple[str, int, int]]:
    """(name, size, offset in the file) of the object's sections with contents that are data (ELF
    or COFF)."""
    out = []
    text = subprocess.run([objdump, "-h", obj], capture_output=True, text=True, check=True).stdout
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s[^\n]*\n"
                         r"\s*([^\n]*)", text, re.M):
        name, size, flags = m.group(1), int(m.group(2), 16), m.group(4)
        if size and "CONTENTS" in flags and "ALLOC" in flags and "CODE" not in flags and not SKIP.match(name):
            out.append((name, size, int(m.group(3), 16)))
    return out


def relocated(objdump: str, obj: str, section: str) -> set[int]:
    """Offsets in the section whose bytes a relocation fills (or, in COFF, holds the addend of)."""
    out: set[int] = set()
    text = subprocess.run([objdump, "-r", "-j", section, obj], capture_output=True, text=True).stdout
    for m in re.finditer(r"^([0-9a-f]+)\s+(\S+)", text, re.M):
        kind = m.group(2)
        width = 8 if re.search(r"(_64|ADDR64|PC64)$", kind) else 2 if kind.endswith("16") else \
            1 if re.search(r"_8$", kind) else 4
        out.update(range(int(m.group(1), 16), int(m.group(1), 16) + width))
    return out


def find_runs(blob: bytes, wanted: set[bytes]) -> list[tuple[int, int]]:
    """[start, end) runs of retail windows, zero bytes at their ends left out (payloadcheck's rule).
    The section may sit between zeros in the executable (alignment, or a neighbour's zeroed
    bytes): windows reaching past its ends see zeros there."""
    pad = bytes(MIN)
    padded = pad + blob + pad
    out = []
    for start, end in payloadcheck.runs(payloadcheck.hits(padded, wanted, MIN), MIN):
        start, end = max(start - MIN, 0), min(end - MIN, len(blob))
        while start < end and blob[start] == 0:
            start += 1
        while end > start and blob[end - 1] == 0:
            end -= 1
        if end - start >= MIN:
            out.append((start, end))
    return out


def blank(blob: bytearray, keep: set[int], wanted: set[bytes], image: Source) -> list[tuple[int, int, int]]:
    """Zero the retail runs of a section (blob, in place) but for the relocated bytes, until none is
    left (zeroing can make a neighbour's windows retail ones: strings between NULs, as the PS1 has
    them); the (offset, offset in the file, size) copies that put the bytes back."""
    out: list[tuple[int, int, int]] = []
    while True:
        done = len(out)
        for start, end in find_runs(bytes(blob), wanted):
            at = start
            while at < end:  # the stretches of non-zero, unrelocated bytes
                if at in keep or blob[at] == 0:
                    at += 1
                    continue
                stop = at
                while stop < end and stop not in keep and blob[stop] != 0:
                    stop += 1
                ps = pieces(bytes(blob[at:stop]), image)
                if ps is None:
                    raise ValueError(f"+0x{at:X}: bytes the module's file does not hold")
                out += [(at + o, src, n) for o, src, n in ps]
                blob[at:stop] = bytes(stop - at)
                at = stop
        if len(out) == done:
            return out


class Source:
    """The module's file as a copy source: every byte but those each disc has its own (main's
    file and directory tables: tools/pc_link.py's DISC_RANGES), which a copy must not use."""

    def __init__(self, image: bytes, skip: list[tuple[int, int]]):
        self.parts = []
        at = 0
        for lo, hi in sorted(skip):
            self.parts.append((at, image[at:lo]))
            at = hi
        self.parts.append((at, image[at:]))

    def find(self, b: bytes) -> int:
        for start, part in self.parts:
            i = part.find(b)
            if i >= 0:
                return start + i
        return -1


def pieces(run: bytes, image: Source) -> list[tuple[int, int, int]] | None:
    """(offset in the run, offset in the file, size): the run as the longest stretches the file
    holds, each from wherever it holds it; None when a byte is nowhere in the file."""
    out, at = [], 0
    while at < len(run):
        if image.find(run[at:at + 1]) < 0:
            return None
        lo, hi = 1, len(run) - at  # the longest prefix of run[at:] the image holds
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if image.find(run[at:at + mid]) >= 0:
                lo = mid
            else:
                hi = mid - 1
        out.append((at, image.find(run[at:at + lo]), lo))
        at += lo
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module")
    ap.add_argument("out_partial")
    ap.add_argument("out_c", type=Path)
    ap.add_argument("out_syms", type=Path)
    ap.add_argument("gen_object")
    ap.add_argument("objects", nargs="+")
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--ld", default="ld")
    ap.add_argument("--objcopy", default="objcopy")
    a = ap.parse_args()
    objdump = re.sub(r"objcopy(\.exe)?$", r"objdump\1", a.objcopy)
    off = os.environ.get("XG_SRCDATA") == "off"

    info = pc_link.disc_files(a.ver).get(a.module)
    if info is None or not info["path"].exists():
        raise SystemExit(f"pc_srcdata: no extracted image for {a.module} (config/{a.ver}/disc.yaml, tools/extract.py)")
    image = info["path"].read_bytes()
    wanted = payloadcheck.windows({a.module: image}, MIN) if not off else set()
    # File offsets: the boot executable has a 0x800-byte header before its load address.
    base = pc_link.module_images(a.ver)[a.module][0] - (0x800 if "iso" in info else 0)
    source = Source(image, [(lo - base, hi - base) for lo, hi in pc_link.DISC_RANGES.get(a.module, [])])

    copies: list[tuple[str, int, int, int]] = []  # (symbol, offset, file offset, size)
    syms: list[str] = []
    linked: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, obj in enumerate(a.objects):
            update, rename = [], []
            data = bytearray(Path(obj).read_bytes())
            for name, size, at_file in data_sections(objdump, obj) if not off else []:
                blob = data[at_file:at_file + size]
                if not find_runs(bytes(blob), wanted):
                    continue
                sym = f"xgsrc_{a.module}_{len(syms)}"
                try:
                    copies += [(sym, o, src, n) for o, src, n in blank(blob, relocated(objdump, obj, name), wanted, source)]
                except ValueError as e:
                    raise SystemExit(f"pc_srcdata: {obj} {name}{e}") from None
                data[at_file:at_file + size] = blob
                update.append(f"--add-symbol={sym}={name}:0,global")
                rename.append(f"--rename-section={name}=xgdata_{a.module},alloc,load,data,contents")
                syms.append(sym)
            if update:
                copy = str(Path(tmp) / f"o{i}.o")
                Path(copy).write_bytes(bytes(data))
                subprocess.run([a.objcopy, *update, copy], check=True)
                subprocess.run([a.objcopy, *rename, copy], check=True)
                linked.append(copy)
            else:
                linked.append(obj)
        subprocess.run([a.ld, "-r", "-d", "-o", a.out_partial, *linked, a.gen_object], check=True)
        # -d gives the module's tentative definitions (common symbols, -fcommon) their space in the
        # partial link, before objcopy makes the module's names local: a local common is no
        # definition (LLVM's lld keeps commons in -r and turned one into address 8: xg-mrj.3.6).
        nm_out = subprocess.run(["nm", a.out_partial], capture_output=True, text=True).stdout
        commons = [line.split()[-1] for line in nm_out.splitlines() if re.match(r"^\S*\s+C\s", line)]
        if commons:
            raise SystemExit(f"pc_srcdata: {a.ld} left {len(commons)} common symbols in {a.out_partial} "
                             f"({', '.join(commons[:5])}...): link modules with a linker whose -r -d "
                             "allocates them (GNU ld)")

    h = pc_link.fnv1a64(image[src:src + n] for _, _, src, n in copies)
    lines = [f"/* Generated by tools/pc_srcdata.py for {a.module}; do not edit. The game C's own retail",
             " * bytes, zeroed in the module's object: (place, offset in the module's file, size). */",
             "typedef struct { void* dst; unsigned int src; unsigned int size; } XgDataCopy;", ""]
    lines += [f"extern char {s}[];" for s in syms]
    lines += [f"const XgDataCopy xgSrcCopies_{a.module}[] = {{"]
    lines += [f"    {{{s} + 0x{o:X}, 0x{src:X}, 0x{n:X}}}," for s, o, src, n in copies]
    lines += ["    {0, 0, 0},", "};", f"const int xgSrcCount_{a.module} = {len(copies)};",
              f"const unsigned long long xgSrcHash_{a.module} = 0x{h:016X}ull;", ""]
    a.out_c.write_text("\n".join(lines))
    a.out_syms.write_text("xg_no_exports\n" + "".join(f"{s}\n" for s in syms))
    print(f"pc_srcdata {a.module}: {sum(c[3] for c in copies)} bytes in {len(copies)} copies "
          f"from {len(syms)} section(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
