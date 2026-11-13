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

The plan: what is zeroed and copied back, without the disc.

    python tools/pc_srcdata.py --write-plan PLAN.json ...   # from disc/<ver>/, as above, and writes it
    python tools/pc_srcdata.py --plan PLAN.json ...         # from PLAN.json; disc/ and bin/ unread

Finding the retail runs needs the module's file; the plan records what they
were found to be, so a checkout without the disc links the same objects the same
way: both routes give the same zeroing, symbols, partial link, OUT_C and
OUT_SYMS (one code path after the plan). The plan is JSON with addresses,
sizes, offsets and hashes only, never bytes (tools/check_link_tables.py audits
that):

  {"format": "xenogears-pc-srcdata-plan", "version": 1, "region": "us"|"jp",
   "boot": the boot executable's name (config/<ver>/disc.yaml), "module": MODULE,
   "image": {"sha1": the module file's sha1 (disc.yaml's), "size": its bytes},
   "min": 16, "gen_object": GEN_OBJECT's identity, "hash": the restoration hash
   (16 lowercase hex digits, xgSrcHash_<module>),
   "objects": [one per GAME_OBJECT, in order: {"name": its identity, "sections":
     [one per data section, in objdump's order: {"name", "size", "sha256": of
      its bytes, "relocs": sha256 of its relocated offsets (8-byte little-endian,
      ascending), "symbol": the xgsrc_<module>_N index when it is stripped,
      else null, "copies": [[offset in the section, offset in the file,
      size], ...]}]}]}

An identity is the object's path from its last src/ component on (src/main/x.c.o),
else its file name: no machine's own directories. --plan refuses (and never
falls back to the disc) a plan of another format, version, region, boot file,
module, image or threshold; a missing, unknown, repeated or mistyped field (a
bool is no number); other objects, or in another order; another section list,
size, byte hash or relocation layout (so a retail run added since, in a section
or as a new one, makes the plan stale); symbols out of order; a copy outside its
section or its file, over a relocated or a zero byte, overlapping another, or
from main's disc-specific tables; a restoration hash that is not the copied
bytes'. With XG_SRCDATA=off neither option is taken.

What the plan cannot prove without the disc: that the runs it lists are all of
the retail runs and that each copy's file offset holds the bytes it replaces.
It proves the objects are the ones it was made from, and its hash is that of
the bytes it zeroes (taken from the objects); port/src/system/gamedata.c
checks that hash against the player's disc at every start, so a wrong offset
fails there, not silently.
"""

from __future__ import annotations

import argparse
import hashlib
import json
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


PLAN_FORMAT = "xenogears-pc-srcdata-plan"
PLAN_VERSION = 1
PLAN_KEYS = {"format", "version", "region", "boot", "module", "image", "min", "gen_object", "hash", "objects"}
SECTION_KEYS = {"name", "size", "sha256", "relocs", "symbol", "copies"}
HEX64 = re.compile(r"[0-9a-f]{16}")
SHA1 = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
U32 = 0xFFFFFFFF


class PlanError(ValueError):
    """A plan that is not this link's, or not a plan."""


# A stripped data section: (name, size, offset in the object file, None when nothing is zeroed,
# else its (offset, offset in the module's file, size) copies). One list per game object.
Strip = list[tuple[str, int, int, "list[tuple[int, int, int]] | None"]]


def identity(obj: str) -> str:
    """An object's name in a plan: its path from the last src/ component on, else its file name."""
    parts = Path(obj).as_posix().split("/")
    at = max((i for i, p in enumerate(parts[:-1]) if p == "src"), default=None)
    return "/".join(parts[at:]) if at is not None else parts[-1]


def disc_sha1(ver: str, module: str) -> str | None:
    """The module file's sha1 as config/<ver>/disc.yaml gives it (no image read)."""
    m = re.search(r"\{name:\s*" + re.escape(module) + r",[^}]*?\bsha1:\s*([0-9a-fA-F]{40})",
                  (pc_link.ROOT / "config" / ver / "disc.yaml").read_text())
    return m.group(1).lower() if m else None


def section_record(objdump: str, obj: str, name: str, blob: bytes) -> dict:
    """What binds a plan to one data section: its size, byte hash and relocation layout."""
    relocs = b"".join(o.to_bytes(8, "little") for o in sorted(relocated(objdump, obj, name)))
    return {"name": name, "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "relocs": hashlib.sha256(relocs).hexdigest()}


def disc_strip(a, objdump: str, off: bool) -> tuple[list[Strip], int, bytes]:
    """The retail runs of each object's data sections, found in the module's extracted file."""
    info = pc_link.disc_files(a.ver).get(a.module)
    if info is None or not info["path"].exists():
        raise SystemExit(f"pc_srcdata: no extracted image for {a.module} (config/{a.ver}/disc.yaml, tools/extract.py)")
    image = info["path"].read_bytes()
    wanted = payloadcheck.windows({a.module: image}, MIN) if not off else set()
    # File offsets: the boot executable has a 0x800-byte header before its load address.
    base = pc_link.module_images(a.ver)[a.module][0] - (0x800 if "iso" in info else 0)
    source = Source(image, [(lo - base, hi - base) for lo, hi in pc_link.DISC_RANGES.get(a.module, [])])

    strips: list[Strip] = []
    for obj in a.objects:
        data = Path(obj).read_bytes()
        secs: Strip = []
        for name, size, at_file in data_sections(objdump, obj) if not off else []:
            blob = bytearray(data[at_file:at_file + size])
            if not find_runs(bytes(blob), wanted):
                secs.append((name, size, at_file, None))
                continue
            try:
                secs.append((name, size, at_file, blank(blob, relocated(objdump, obj, name), wanted, source)))
            except ValueError as e:
                raise SystemExit(f"pc_srcdata: {obj} {name}{e}") from None
        strips.append(secs)
    h = pc_link.fnv1a64(image[src:src + n] for secs in strips for *_, cps in secs for _, src, n in cps or [])
    return strips, h, image


def plan_json(a, objdump: str, strips: list[Strip], h: int, image: bytes) -> str:
    """The plan file for these strips (parse_plan reads it)."""
    sha1 = hashlib.sha1(image).hexdigest()
    if disc_sha1(a.ver, a.module) != sha1:
        raise SystemExit(f"pc_srcdata: the extracted {a.module} is not config/{a.ver}/disc.yaml's (sha1): no plan")
    objects, sym = [], 0
    for obj, secs in zip(a.objects, strips):
        data = Path(obj).read_bytes()
        recs = []
        for name, size, at_file, cps in secs:
            rec = section_record(objdump, obj, name, data[at_file:at_file + size])
            rec["symbol"] = None if cps is None else sym
            rec["copies"] = [list(c) for c in cps or []]
            sym += cps is not None
            recs.append(rec)
        objects.append({"name": identity(obj), "sections": recs})
    doc = {"format": PLAN_FORMAT, "version": PLAN_VERSION, "region": a.ver,
           "boot": pc_link.boot_name(pc_link.disc_files(a.ver)), "module": a.module,
           "image": {"sha1": sha1, "size": len(image)}, "min": MIN, "gen_object": identity(a.gen_object),
           "hash": f"{h:016x}", "objects": objects}
    return json.dumps(doc, indent=1, sort_keys=True) + "\n"


def _no_duplicates(pairs: list[tuple[str, object]]) -> dict:
    out = {}
    for k, v in pairs:
        if k in out:
            raise PlanError(f"key {k!r} given twice")
        out[k] = v
    return out


def _u32(v, what: str) -> int:
    if type(v) is not int or not 0 <= v <= U32:
        raise PlanError(f"{what} {v!r} is not an unsigned 32-bit number")
    return v


def _keys(d, want: set[str], what: str) -> dict:
    if not isinstance(d, dict) or set(d) != want:
        got = sorted(d) if isinstance(d, dict) else type(d).__name__
        raise PlanError(f"{what}: want keys {sorted(want)}, got {got}")
    return d


def parse_plan(text: str, a) -> dict:
    """A plan from its JSON, checked against this link's region, boot file, module, image and
    inputs' identities (not yet against their bytes: plan_strip). PlanError on anything else."""
    try:
        doc = json.loads(text, object_pairs_hook=_no_duplicates)
    except json.JSONDecodeError as e:
        raise PlanError(f"not JSON ({e})") from None
    _keys(doc, PLAN_KEYS, "plan")
    if doc["format"] != PLAN_FORMAT or type(doc["version"]) is not int or doc["version"] != PLAN_VERSION:
        raise PlanError(f"format {doc['format']!r} version {doc['version']!r}, want {PLAN_FORMAT!r} {PLAN_VERSION}")
    if doc["region"] != a.ver:
        raise PlanError(f"region {doc['region']!r}, but this link is {a.ver!r}")
    boot = pc_link.boot_name(pc_link.disc_files(a.ver))
    if not isinstance(doc["boot"], str) or doc["boot"] != boot:
        raise PlanError(f"boot file {doc['boot']!r}, but config/{a.ver}/disc.yaml's is {boot!r}")
    if doc["module"] != a.module:
        raise PlanError(f"module {doc['module']!r}, but this link is {a.module!r}")
    img = _keys(doc["image"], {"sha1", "size"}, "image")
    if not isinstance(img["sha1"], str) or not SHA1.fullmatch(img["sha1"]):
        raise PlanError(f"image sha1 {img['sha1']!r} is not 40 lowercase hex digits")
    if img["sha1"] != disc_sha1(a.ver, a.module):
        raise PlanError(f"image sha1 is not config/{a.ver}/disc.yaml's {a.module}")
    _u32(img["size"], "image size")
    if type(doc["min"]) is not int or doc["min"] != MIN:
        raise PlanError(f"min {doc['min']!r}, but runs here are of {MIN} bytes or more")
    if doc["gen_object"] != identity(a.gen_object):
        raise PlanError(f"gen_object {doc['gen_object']!r}, but this link's is {identity(a.gen_object)!r}")
    if not isinstance(doc["hash"], str) or not HEX64.fullmatch(doc["hash"]):
        raise PlanError(f"hash {doc['hash']!r} is not 16 lowercase hex digits")
    objs = doc["objects"]
    if not isinstance(objs, list):
        raise PlanError("objects is not a list")
    names = [_keys(o, {"name", "sections"}, "object")["name"] for o in objs]
    if len(set(map(str, names))) != len(names):
        raise PlanError("an object identity given twice")
    if names != [identity(o) for o in a.objects]:
        raise PlanError(f"objects {len(names)} are not this link's {len(a.objects)}, in its order")
    sym = 0
    for o in objs:
        if not isinstance(o["sections"], list):
            raise PlanError(f"{o['name']}: sections is not a list")
        seen = set()
        for s in o["sections"]:
            _keys(s, SECTION_KEYS, f"{o['name']} section")
            where = f"{o['name']} {s['name']!r}"
            if not isinstance(s["name"], str) or s["name"] in seen:
                raise PlanError(f"{where}: section name not a string, or given twice")
            seen.add(s["name"])
            size = _u32(s["size"], f"{where} size")
            for k in ("sha256", "relocs"):
                if not isinstance(s[k], str) or not SHA256.fullmatch(s[k]):
                    raise PlanError(f"{where}: {k} is not 64 lowercase hex digits")
            if s["symbol"] is not None:
                if type(s["symbol"]) is not int or s["symbol"] != sym:
                    raise PlanError(f"{where}: symbol {s['symbol']!r}, want {sym}")
                sym += 1
            if not isinstance(s["copies"], list) or (s["symbol"] is None and s["copies"]):
                raise PlanError(f"{where}: copies is not a list, or given for a section not stripped")
            for c in s["copies"]:
                if not isinstance(c, list) or len(c) != 3:
                    raise PlanError(f"{where}: a copy is not [offset, file offset, size]")
                at, src, n = (_u32(v, f"{where} copy") for v in c)
                if n == 0 or at + n > size or src + n > img["size"]:
                    raise PlanError(f"{where}: copy {c} is empty or outside its section or file")
    return doc


def plan_strip(a, objdump: str, doc: dict) -> tuple[list[Strip], int]:
    """The strips a checked plan gives, after checking it against the objects' bytes."""
    info = pc_link.disc_files(a.ver)[a.module]
    base = pc_link.module_images(a.ver)[a.module][0] - (0x800 if "iso" in info else 0)
    own = [(lo - base, hi - base) for lo, hi in pc_link.DISC_RANGES.get(a.module, [])]
    strips: list[Strip] = []
    chunks = []
    for obj, rec in zip(a.objects, doc["objects"]):
        data = Path(obj).read_bytes()
        found = data_sections(objdump, obj)
        if [(n, s) for n, s, _ in found] != [(s["name"], s["size"]) for s in rec["sections"]]:
            raise PlanError(f"{rec['name']}: its data sections are not the plan's (stale plan)")
        secs: Strip = []
        for (name, size, at_file), s in zip(found, rec["sections"]):
            blob = data[at_file:at_file + size]
            got = section_record(objdump, obj, name, blob)
            if got["sha256"] != s["sha256"] or got["relocs"] != s["relocs"]:
                raise PlanError(f"{rec['name']} {name}: bytes or relocations not the plan's (stale plan)")
            if s["symbol"] is None:
                secs.append((name, size, at_file, None))
                continue
            keep = relocated(objdump, obj, name)
            used: set[int] = set()
            cps = [tuple(c) for c in s["copies"]]
            for at, src, n in cps:
                span = set(range(at, at + n))
                if span & used or span & keep or 0 in blob[at:at + n]:
                    raise PlanError(f"{rec['name']} {name}: copy +0x{at:X} overlaps another, a relocation or a zero")
                if any(src < hi and lo < src + n for lo, hi in own):
                    raise PlanError(f"{rec['name']} {name}: copy +0x{at:X} takes bytes each disc has its own")
                used |= span
                chunks.append(blob[at:at + n])
            secs.append((name, size, at_file, cps))
        strips.append(secs)
    h = pc_link.fnv1a64(chunks)
    if f"{h:016x}" != doc["hash"]:
        raise PlanError("hash is not the copied bytes' (stale plan)")
    return strips, h


def main(argv: list[str] | None = None) -> int:
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
    route = ap.add_mutually_exclusive_group()
    route.add_argument("--plan", type=Path, help="take what to zero and copy from this file, not disc/<ver>/")
    route.add_argument("--write-plan", type=Path, help="write what is zeroed and copied (from disc/<ver>/) here")
    a = ap.parse_args(argv)
    objdump = re.sub(r"objcopy(\.exe)?$", r"objdump\1", a.objcopy)
    off = os.environ.get("XG_SRCDATA") == "off"
    if off and (a.plan or a.write_plan):
        raise SystemExit("pc_srcdata: XG_SRCDATA=off strips nothing: no --plan or --write-plan with it")

    if a.plan:
        try:
            try:
                text = a.plan.read_text()
            except (OSError, UnicodeDecodeError) as e:
                raise PlanError(str(e)) from None
            strips, h = plan_strip(a, objdump, parse_plan(text, a))
        except PlanError as e:
            raise SystemExit(f"pc_srcdata: plan {a.plan}: {e}") from None
    else:
        strips, h, image = disc_strip(a, objdump, off)
        if a.write_plan:
            a.write_plan.write_text(plan_json(a, objdump, strips, h, image))

    copies: list[tuple[str, int, int, int]] = []  # (symbol, offset, file offset, size)
    syms: list[str] = []
    linked: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (obj, secs) in enumerate(zip(a.objects, strips)):
            update, rename = [], []
            data = bytearray(Path(obj).read_bytes())
            for name, size, at_file, cps in secs:
                if cps is None:
                    continue
                sym = f"xgsrc_{a.module}_{len(syms)}"
                for o, src, n in cps:
                    data[at_file + o:at_file + o + n] = bytes(n)
                copies += [(sym, o, src, n) for o, src, n in cps]
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
        # A section per symbol (.data.<name>, .bss.<name>: -fdata-sections) would escape the rename into
        # the module's own sections (port/CMakeLists.txt), so the overlay resets and save states.
        headers = subprocess.run(["objdump", "-h", a.out_partial], capture_output=True, text=True).stdout
        split = sorted({m.group(1) for m in re.finditer(r"^\s*\d+\s+(\.(?:data|bss|sdata|sbss)\.\S+)", headers, re.M)
                        if not m.group(1).startswith(".data.rel")})
        if split:
            raise SystemExit(f"pc_srcdata: {a.out_partial} has sections per symbol ({', '.join(split[:5])}...): "
                             "compile the game C with -fno-data-sections")
        nm_out = subprocess.run(["nm", a.out_partial], capture_output=True, text=True).stdout
        commons = [line.split()[-1] for line in nm_out.splitlines() if re.match(r"^\S*\s+C\s", line)]
        if commons:
            raise SystemExit(f"pc_srcdata: {a.ld} left {len(commons)} common symbols in {a.out_partial} "
                             f"({', '.join(commons[:5])}...): link modules with a linker whose -r -d "
                             "allocates them (GNU ld)")

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
