#!/usr/bin/env python3
"""Generate what the PC port's link needs beyond the game C.

    python tools/pc_link.py OUTDIR OBJ...      # every module's game objects (CMakeFiles/game_<module>.dir/...)
    python tools/pc_link.py --ver jp --export kernelMain OUTDIR OBJ...

The game C references symbols that no C file defines on the PC: SDK and BIOS
functions, hand-written assembly, other overlays' functions, and data that lives
in the PS1 build's data assembly (asm/<ver>/<module>/data) or only in the symbol
files (overlay BSS). Every module is linked into one executable, each as one
partially linked object whose symbols are local except its exports (the menu
overlays carry their own copies of the menu library under the same names).
Data is resolved by its PS1 address, as the PS1 reaches it: the names give a
reference's address (the module's own labels and symbol file, then main's, then
the one module naming it), and the address its owner: the referencing module's
own image, main's (below the overlays), the image of the one module loaded beside
it that holds the address, else the BSS of the one beside it that starts highest
below the address among those that use it (battle's sub-modules share battle's
BSS). Only where the address cannot tell (main and the upper overlays can sit
beside several overlays at one address) do the names pick. Names for one address
are one object (aliases); the link stops when two PC objects would stand for one
PS1 address in modules loaded together, or a module would hold another's address
(SEPARATE lists the pairs kept apart on purpose). An alias need not have the
16-byte alignment the x86-64 ABI promises an array of 16 bytes or more, so the
link also stops when such a name's declaration lacks PORT_ALIAS (include/common.h).
This writes:

  OUTDIR/stubs.c    one old-style stub per function no module defines: reports
                    itself once (portStub) and returns 0; arguments are ignored.
  OUTDIR/<module>.exports  the symbols the module must keep global: those other
                    modules (or --export, the port) reference.
  OUTDIR/modules.c  the module table: each module's data/BSS bounds (the
                    sections xgdata_<module>/xgbss_<module>) for portResetModule
                    (overlays) and the soft reset (main); portModuleData, each
                    module's file on the disc and its copy list (below); and
                    portSrcData, the game C's own retail bytes the module link
                    zeroes and the disc refills (tools/pc_srcdata.py).
  OUTDIR/<module>.c a definition per missing data symbol the module owns, sized
                    for the PC. No retail byte is written here: an object that
                    holds some is zero storage in .data, and the module's copy
                    list xgDataCopies_<module> (object + offset, offset in the
                    module's file, size) has port/src/system/gamedata.c fill it
                    from the player's disc at startup:
                    - same layout on both (PC size == PS1 size, no pointer inside):
                      the retail bytes (copied), or zero storage when they are all zero;
                    - an array of pointers (or one pointer): the symbols it points
                      at, as initialisers (pulled into the link too);
                    - a struct (array) with pointers whose PC layout follows from
                      its data (relayout()): pointers as initialisers, the other
                      retail bytes copied to their PC offsets;
                    - a symbol inside another emitted one: an alias at the same
                      offset (the container keeps the PS1 layout; another name for
                      its start always is one). A symbol that runs past its
                      container's end grows the container;
                    - data only other data points at: its retail bytes (copied; or
                      a pointer array), up to the next name anything uses;
                    - anything else: zero storage of the PC size, listed in
                      OUTDIR/unconverted.txt for conversion by hand (with aliases
                      whose PC type grows into the next name).
                    Tentative definitions in game C (common symbols) get their
                    retail initial value the same way when it is not zero.
  OUTDIR/report.txt counts by kind, and the one-object-per-address check (the
                    game C's own data placed by the PS1 build, build/<ver>/*.elf).

Data layouts come from the PS1 build's data assembly, which tools/configure.py
generates from the player's disc; the copies' offsets and the check hash from
the extracted images (disc/<ver>/). The executable carries neither the bytes
nor anything derived from them beyond offsets, sizes and the hash
(tools/payloadcheck.py checks it).

Sizes come from the game's own declarations: each file that uses a symbol is
compiled again with `char x[sizeof(sym)]` appended, once for the PC (x86-64)
and once for the PS1 layout (-m32 with XG_PS1_LAYOUT: 4-byte pointers and tags),
by --cc (GCC or clang: the build's compiler, or XG_PROBE_CC). A probe that
cannot give sizes stops the link (without them pointer tables would silently
become byte copies).
"""

from __future__ import annotations

import argparse
import bisect
import concurrent.futures
import os
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_FLAGS = ["-std=gnu89", "-nostdinc", "-fno-builtin", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing",
              "-fcommon", "-D_LANGUAGE_C", "-DPLATFORM_PC", "-Iinclude"]
# What makes each compiler accept the game C as the port's build does (port/CMakeLists.txt,
# XG_GAME_OPTIONS): clang keeps some diagnostics as errors under -w (incompatible pointer
# types), so a probe with GCC's flags failed and the link went on without sizes (xg-htn.32).
# No error limit: every failing probe line is reported at once.
PERMISSIVE = {"gcc": ["-fpermissive", "-w"], "clang": ["-Wno-everything", "-ferror-limit=0"]}
LAYOUTS = {"pc": ["-m64"], "ps1": ["-m32", "-DXG_PS1_LAYOUT"]}
# Host C library functions the game calls under their standard names.
HOST_LIBC = {"abs", "memcpy", "memmove", "memset", "memchr", "memcmp", "strlen", "strcpy", "strcat", "strcmp",
             "strncmp", "strncpy", "strchr", "strrchr", "sprintf", "printf", "puts", "putchar", "toupper",
             "tolower", "bzero", "bcopy", "atoi", "abort"}
DATA_LINE = re.compile(r"^\s+/\* (?P<rom>[0-9A-F]+) (?P<vram>[0-9A-F]{8})(?: (?P<raw>[0-9A-F]+))? \*/ "
                       r"\.(?P<dir>word|short|byte|asciz|space) (?P<arg>.*?)\s*$")
LABEL = re.compile(r"^(?:dlabel|glabel|jlabel) (\w+)")
END_MARKER = 16  # PC bytes for a name that marks the end of a module's BSS (its PS1 extent is 0)


@dataclass
class Image:
    """One module's data as the PS1 sees it: bytes by address, and the pointer slots."""
    module: str
    mem: dict[int, int] = field(default_factory=dict)
    deltas: set[int] = field(default_factory=set)  # file offset - address (one value: the image is contiguous)
    refs: dict[int, tuple[str, int]] = field(default_factory=dict)  # slot address -> (symbol, addend)
    ends: set[int] = field(default_factory=set)  # where an INCLUDE_RODATA blob ends

    def get(self, addr: int, size: int) -> bytes:
        return bytes(self.mem.get(a, 0) for a in range(addr, addr + size))

    def refs_in(self, addr: int, size: int) -> list[tuple[int, str, int]]:
        return [(a, *self.refs[a]) for a in range(addr, addr + size) if a in self.refs]


def asciz_bytes(arg: str) -> bytes:
    s = arg.strip()
    assert s.startswith('"') and s.endswith('"'), arg
    s, out, i = s[1:-1], bytearray(), 0
    while i < len(s):
        if s[i] == "\\":
            n = s[i + 1]
            if n in "01234567":
                j = i + 1
                while j < len(s) and j < i + 4 and s[j] in "01234567":
                    j += 1
                out.append(int(s[i + 1:j], 8) & 0xFF)
                i = j
            elif n == "x":
                j = i + 2
                while j < len(s) and s[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(int(s[i + 2:j], 16) & 0xFF)
                i = j
            else:
                out.append({"n": 10, "t": 9, "r": 13, "f": 12, "v": 11, "a": 7, "b": 8, "e": 27, "\\": 92, '"': 34,
                            "'": 39, "0": 0}.get(n, ord(n)))
                i += 2
        else:
            out += s[i].encode("latin-1", "replace")
            i += 1
    out.append(0)
    return bytes(out)


def load_images(ver: str) -> tuple[dict[str, Image], dict[str, tuple[str, int]]]:
    """Per-module images from asm/<ver>/<module>/data/*.s, and every label's (module, address)."""
    images: dict[str, Image] = {}
    labels: dict[str, tuple[str, int]] = {}
    # Data files, plus the data blocks (dlabel .. enddlabel) inside code files: main's custom
    # poly model D_8001C76C (the battle's target cursor) sits in .text.
    paths = [(p, True) for p in sorted((ROOT / "asm" / ver).glob("*/data/*.s"))]
    paths += [(p, False) for p in sorted((ROOT / "asm" / ver).glob("*/*.s"))]
    # INCLUDE_RODATA blobs (battling's string tables), which data points into by number:
    # splat leaves those words as numbers (no label at the string), so they become refs below.
    paths += [(p, False) for p in sorted((ROOT / "asm" / ver).glob("*/nonmatchings/*/*.s"))]
    numeric_words: list[tuple[str, int, int]] = []  # (module, slot, value) of data words
    blobs: list[tuple[str, str, int, int]] = []  # (module, label, start, end)
    for path, is_data in paths:
        module = path.relative_to(ROOT / "asm" / ver).parts[0]
        is_blob = "nonmatchings" in path.parts
        blob_start = None
        blob_end = blob_last = 0
        text = path.read_text(errors="replace")
        if not is_data and "\ndlabel " not in text:
            continue
        img = images.setdefault(module, Image(module))
        pending = []
        in_block = is_data
        for line in text.splitlines():
            if not is_data:
                if line.startswith("dlabel "):
                    in_block = True
                elif line.startswith("enddlabel "):
                    in_block = False
                    continue
                if not in_block:
                    continue
                line = re.sub(r"\s*/\*[^*]*\*/\s*$", "", line)  # "/* invalid instruction */"
            m = LABEL.match(line)
            if m:
                if not m.group(1).startswith(".L"):
                    pending.append(m.group(1))
                continue
            m = DATA_LINE.match(line)
            if not m:
                continue
            a = int(m["vram"], 16)
            img.deltas.add(int(m["rom"], 16) - a)
            for name in pending:
                labels.setdefault(name, (module, a))
                if is_blob and blob_start is None:
                    blob_start = (name, a)
            pending = []
            blob_end = a
            d, arg = m["dir"], m["arg"]
            if d == "space":
                n = int(arg, 0)
                for i in range(n):
                    img.mem[a + i] = 0
                blob_last = max(blob_last, a + n - 1)
            elif d == "asciz":
                for i, b in enumerate(asciz_bytes(arg)):
                    img.mem[a + i] = b
                    blob_last = a + i
            elif d in ("word", "short", "byte"):
                size = {"word": 4, "short": 2, "byte": 1}[d]
                if re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", arg):
                    v = int(arg, 0) & ((1 << (8 * size)) - 1)
                    raw = v.to_bytes(size, "little")
                    if is_data and size == 4:
                        numeric_words.append((module, a, v))
                else:
                    sym, _, add = arg.partition("+")
                    img.refs[a] = (sym.strip(), int(add, 0) if add.strip() else 0)
                    raw = bytes.fromhex(m["raw"]) if m["raw"] and len(m["raw"]) == 2 * size else bytes(size)
                for i, b in enumerate(raw):
                    img.mem[a + i] = b
                # A blob's tail in .word form (splat's choice for strings it does not see as
                # text: battling's heap type names) is the blob too.
                blob_last = max(blob_last, a + size - 1)
        if blob_start is not None:
            blobs.append((module, blob_start[0], blob_start[1], blob_end))
            img.ends.add(blob_last + 1)
    for module, a, v in numeric_words:
        for bmod, name, start, end in blobs:
            if bmod == module and start <= v <= end and a not in images[module].refs:
                images[module].refs[a] = (name, v - start)
    return images, labels


def load_symbols(ver: str) -> dict[str, tuple[str, int, bool]]:
    """name -> (module, address, is_function) from config/<ver>/symbols/*.txt."""
    syms: dict[str, tuple[str, int, bool]] = {}
    for f in sorted((ROOT / "config" / ver / "symbols").glob("*.txt")):
        for line in f.read_text().splitlines():
            m = re.match(r"\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)", line)
            if m and m.group(1) not in syms:
                syms[m.group(1)] = (f.stem, int(m.group(2), 16), "type:func" in m.group(3))
    return syms


def module_images(ver: str) -> dict[str, tuple[int, int]]:
    """module -> (load address, end of its image) from config/<ver>/disc.yaml and the module configs."""
    out = {}
    disc = (ROOT / "config" / ver / "disc.yaml").read_text()
    for m in re.finditer(r"\{name:\s*(\w+),[^}]*?\bvram:\s*(0x[0-9A-Fa-f]+)", disc):
        cfg = ROOT / "config" / ver / f"{m.group(1)}.yaml"
        ends = re.findall(r"^  - \[(0x[0-9A-Fa-f]+)\]\s*$", cfg.read_text(), re.M) if cfg.is_file() else []
        lo = int(m.group(2), 16)
        out[m.group(1)] = (lo, lo + (int(ends[-1], 16) if ends else 0))
    return out


def module_bss(ver: str) -> dict[str, tuple[int, int]]:
    """module -> (start, end) of its BSS, for the modules whose config gives bss_size (main's
    ends where the overlays load). An overlay's BSS has no recorded bounds."""
    out = {}
    for cfg in sorted((ROOT / "config" / ver).glob("*.yaml")):
        text = cfg.read_text()
        start = re.search(r"^\s+start:\s*(0x[0-9A-Fa-f]+)", text, re.M)
        vram = re.search(r"^\s+vram:\s*(0x[0-9A-Fa-f]+)", text, re.M)
        bss = re.search(r"^\s+bss_size:\s*(0x[0-9A-Fa-f]+)", text, re.M)
        ends = re.findall(r"^  - \[(0x[0-9A-Fa-f]+)\]\s*$", text, re.M)
        if start and vram and bss and ends:
            lo = int(vram.group(1), 16) + int(ends[-1], 16) - int(start.group(1), 16)
            out[cfg.stem] = (lo, lo + int(bss.group(1), 16))
    return out


# Sub-modules load beside their host overlay (battle's at 0x801DE000-0x80280000, the field's
# mecha and debug overlays); nothing at the host's base can be loaded with them.
SUB_MODULES = {m: "battle" for m in ("battle_result", "battle_debug_selector", "battle_loader", "battle_event",
                                     "battle_debug", "battle_fx_circle_warp", "battle_fx_sprite_wobble",
                                     "battle_fx_model_shatter", "battle_fx_tiled_sprite", "battle_fx_sprite_band",
                                     "battle_fx_screen_ripple")}
SUB_MODULES.update(field_debug="field", field_mecha="field")


def coresident(a: str, b: str, extents: dict[str, tuple[int, int]]) -> bool:
    """Whether modules a and b can be loaded at the same time, from their images alone: main
    always is; a sub-module with its host; otherwise neither image may overlap the other's or
    the other's host's."""
    def overlap(x: str, y: str) -> bool:
        return extents[x][0] < extents[y][1] and extents[y][0] < extents[x][1]
    if a == b or "main" in (a, b) or SUB_MODULES.get(a) == b or SUB_MODULES.get(b) == a:
        return True
    if overlap(a, b):
        return False
    return not any(h is not None and h != y and overlap(y, h)
                   for x, y in ((a, b), (b, a)) for h in (SUB_MODULES.get(x),))


def elf_symbols(path: Path) -> list[tuple[str, int, int, int, int, int]]:
    """(name, value, size, type, binding, section index) of a little-endian ELF32 file's .symtab."""
    b = path.read_bytes()
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
        raise ValueError(f"{path}: not a little-endian ELF32 file")
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", b, 0x2E)
    secs = [struct.unpack_from("<IIIIIIIIII", b, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for sec in secs:
        if sec[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[sec[6]][4]
        for off in range(sec[4], sec[4] + sec[5], 16):
            name, value, size, info, _, shndx = struct.unpack_from("<IIIBBH", b, off)
            out.append((b[strtab + name:b.index(b"\0", strtab + name)].decode(), value, size, info & 15, info >> 4,
                        shndx))
    return out


def ps1_c_objects(ver: str, data: dict[str, dict[str, tuple[str, str, int]]]) -> list[tuple[str, str, int, int, str]]:
    """The PS1 memory the C's own data objects stand for, from the PS1 build's images
    (build/<ver>/<module>.elf; the PS1 build passes before a PC build): (module, name, address,
    size, '') for each data object a module's PC objects define (one name per place: aliases
    are the same object): the PS1 C's data where the PS1 build placed it, and the port's own
    definitions of PS1 data (port/src/game/<module>/bss.c) at the symbol files' addresses, one
    byte each (their PS1 extent is not recorded). Missing or unreadable images are skipped with
    a warning."""
    out = []
    for mod, where in sorted(data.items()):
        names = {}
        for name in sorted(where):
            names.setdefault(where[name], name)
        names = set(names.values())
        elf = ROOT / "build" / ver / f"{mod}.elf"
        try:
            syms = elf_symbols(elf)
        except (OSError, ValueError, struct.error, IndexError) as e:
            print(f"warning: pc_link: {elf.relative_to(ROOT)} not read ({e}): its C data is not checked "
                  "against the generated data", file=sys.stderr)
            continue
        for name, value, size, typ, bind, shndx in syms:
            if bind == 1 and typ in (0, 1) and shndx != 0 and name in names:
                out.append((mod, name, value, max(size, 1) if shndx != 0xFFF1 else 1, ""))
    return out


def data_residues(where: dict[str, tuple[str, str, int]]) -> dict[str, int | None]:
    """name -> its address mod 16 once linked, from its object (section offset and the section's
    alignment; ELF or, for the Windows build, COFF objects), or None where that is not known (a
    section aligned to less than 16)."""
    out: dict[str, int | None] = {}
    by_obj: dict[str, list[str]] = {}
    for name, (obj, _, _) in where.items():
        by_obj.setdefault(obj, []).append(name)
    for obj, names in by_obj.items():
        p = subprocess.run(["objdump", "-h", "-t", obj], capture_output=True, text=True)
        align: dict[str, int] = {}
        index: dict[int, str] = {}
        section: dict[str, str] = {}
        for line in p.stdout.splitlines():
            m = re.match(r"\s*(\d+)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+2\*\*(\d+)", line)
            if m:
                index[int(m.group(1))] = m.group(2)
                align[m.group(2)] = 1 << int(m.group(3))
                continue
            m = re.match(r"\[\s*\d+\]\(sec\s+(-?\d+)\).*\s0x[0-9a-f]+\s+(\S+)$", line)  # COFF: 1-based
            if m:
                section[m.group(2)] = index.get(int(m.group(1)) - 1, "")
                continue
            left, _, right = line.partition("\t")  # ELF: value flags section<TAB>size name
            if right and left.split():
                section[right.split()[-1]] = left.split()[-1]
        for name in names:
            sec = section.get(name)
            out[name] = where[name][2] % 16 if sec is not None and align.get(sec, 0) >= 16 else None
    return out


EXTERN_DECL = re.compile(r"\bextern\b[^;{}]*;")
LINE_MARKER = re.compile(r'^# (\d+) "([^"]*)"', re.M)


def alias_declarations(hazards: dict[str, list[str]], ver: str, cc: str) -> list[str]:
    """For each name that sits off the ABI's array alignment and is declared as an array of 16
    bytes or more: the files whose first declaration of it (later ones inherit its attributes)
    lacks __attribute__((aligned(1))) (PORT_ALIAS), read from each referencing file's
    preprocessed text, with where that declaration is. A file that declares it as no array, or
    as one of unknown size, makes no promise. The declaration must name one declarator (a
    trailing attribute applies to the last one only)."""
    by_src: dict[Path, list[str]] = {}
    for name, objs in hazards.items():
        for obj in objs:
            by_src.setdefault(source_of(obj), []).append(name)
    flags = probe_flags(cc) if by_src else []
    out = []
    for src, names in sorted(by_src.items()):
        p = subprocess.run([cc, "-E", *GAME_FLAGS, *flags, *LAYOUTS["pc"], f"-DVERSION_{ver.upper()}", str(src)],
                           cwd=ROOT, capture_output=True, text=True)
        if p.returncode != 0:
            raise ProbeError(f"{src} does not preprocess with {cc}:\n{p.stderr.strip()[-800:]}")
        text = p.stdout
        # A marker '# N "file" ...' says the line after it is line N of that file.
        markers = [(text.find("\n", m.end()) + 1, m.group(2), int(m.group(1))) for m in LINE_MARKER.finditer(text)]
        starts = [m[0] for m in markers]

        def where(pos: int) -> str:
            i = bisect.bisect_right(starts, pos) - 1
            if i < 0:
                return "?"
            start, path, line = markers[i]
            return f"{os.path.relpath(ROOT / path, ROOT)}:{line + text.count(chr(10), start, pos)}"
        decls = [(m.start(), re.sub(r"\s+", " ", m.group(0))) for m in EXTERN_DECL.finditer(text)]
        for name in sorted(set(names)):
            mine = [(pos, d) for pos, d in decls if re.search(rf"\b{name}\b", d)]
            if not any(re.search(rf"\b{name}\s*\[\s*[^\s\]]", d) for _, d in mine):
                continue  # not declared here as an array of known size (an incomplete one promises nothing)
            pos, d = mine[0]
            depth, commas = 0, 0
            for ch in d:
                depth += ch in "([{"
                depth -= ch in ")]}"
                commas += ch == "," and depth == 0
            if commas:
                out.append(f"{name}: {where(pos)}: declare it on its own: {d}")
            elif not re.search(r"\baligned\s*\(\s*1\s*\)", d):
                out.append(f"{name}: {where(pos)}: {d}")
    return sorted(set(out))


# PC objects that overlap PS1 memory another one holds, on purpose: the pair, and why.
SEPARATE = {
    frozenset({("field", "op9DVar1"), ("field", "D_800B235C")}):
        "port/src/game/field/bss.c: the sequence pointer the PS1 keeps in op9DVar1's padding is 8 bytes "
        "on the PC, so it has its own storage",
    frozenset({("main", "battleScriptGameStateVars"), ("main", "g_spuVolumes")}):
        "game_init.c clears 20 s16 (0x28 bytes) of a 16-entry table: on the PS1 the last 8 bytes are "
        "g_spuVolumes' mask and master volume shadow; the PC keeps the two objects apart, as the game "
        "never notices (xg-bwn: the mask is clear then, the shadow is recomputed before each use)",
}


def overlapping_storage(objs: list[tuple[str, str, int, int, str]], extents: dict[str, tuple[int, int]],
                        allowed: dict[frozenset, str] | None = None, used: set | None = None) -> list[str]:
    """Pairs of PC objects whose PS1 ranges overlap in modules that can be loaded together
    (the same module included), unless one of them says why it may or the pair is `allowed`
    (each allowed pair met goes into `used`)."""
    objs = sorted((o for o in objs if o[3] > 0 and o[0] in extents), key=lambda o: (o[2], o[0], o[1]))
    out = []
    for i, (m, n, a, size, why) in enumerate(objs):
        for m2, n2, a2, size2, why2 in objs[i + 1:]:
            if a2 >= a + size:
                break  # sorted by address: nothing later starts inside this one
            if (m, n) == (m2, n2) or why or why2 or not coresident(m, m2, extents):
                continue
            pair = frozenset({(m, n), (m2, n2)})
            if allowed and pair in allowed:
                if used is not None:
                    used.add(pair)
                continue
            out.append(f"{m} {n} 0x{a:08X}+0x{size:X} and {m2} {n2} 0x{a2:08X}+0x{size2:X}")
    return out


def sdk_functions(game_root: Path = ROOT) -> set[str]:
    names = set()
    for h in [game_root / "include/psyq.h", *sorted((game_root / "include/psyq").glob("*.h"))]:
        names |= set(re.findall(r"^[\w\s\*]*?\b(\w+)\s*\([^;{]*\)\s*;", h.read_text(), re.M))
    return names


def nm(objs: list[str]) -> tuple[set[str], dict[str, list[str]], set[str], dict[str, tuple[str, str, int]],
                                dict[str, list[str]]]:
    """(defined names, undefined name -> objects referencing it, common names, defined data name ->
    where it is: (object, nm type, value), names at one place being aliases of one object; and
    every name any of the objects references, those the others define included)."""
    defined, undefined, common, data = set(), {}, set(), {}
    for o in objs:
        out = subprocess.run(["nm", o], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "U":
                undefined.setdefault(parts[1], []).append(o)
            elif len(parts) == 3 and parts[1].isupper():
                if parts[1] == "C":
                    common.add(parts[2])
                    undefined.setdefault(parts[2], []).append(o)
                else:
                    defined.add(parts[2])
                    if parts[1] in "BDGRSV":
                        data[parts[2]] = (o, parts[1], int(parts[0], 16))
    return defined, {k: v for k, v in undefined.items() if k not in defined}, common, data, undefined


def source_of(obj: str) -> Path:
    """CMake object .../CMakeFiles/game_x.dir/<path>.c.o (.c.obj) -> the .c file.

    Game sources outside port/ keep their absolute path; port/src files are
    relative to port/."""
    m = re.search(r"\.dir(/.*\.c)\.o(?:bj)?$", obj)  # .obj: Windows
    if not m:
        raise ValueError(obj)
    path = Path(m.group(1))
    return path if path.exists() else ROOT / "port" / m.group(1).lstrip("/")


class ProbeError(Exception):
    """A layout probe that could not run: the link stops rather than guess sizes."""


def probe_flags(cc: str) -> list[str]:
    """The flags that make the probe compiler accept the game C (PERMISSIVE), by its family."""
    try:
        p = subprocess.run([cc, "-dM", "-E", "-x", "c", "-"], input="", capture_output=True, text=True)
    except OSError as e:
        raise ProbeError(f"cannot run the probe compiler {cc}: {e}") from e
    if p.returncode != 0:
        raise ProbeError(f"the probe compiler {cc} does not preprocess an empty file:\n{p.stderr.strip()}")
    macros = set(re.findall(r"^#define (\w+)", p.stdout, re.M))
    family = "clang" if "__clang__" in macros else "gcc" if "__GNUC__" in macros else None
    if family is None:
        raise ProbeError(f"the probe compiler {cc} is neither GCC nor clang (set XG_PROBE_CC)")
    return PERMISSIVE[family]


PROBE_ERROR = re.compile(r"^(?P<file>.*?):(?P<line>\d+):\d+: (?:fatal )?error: ", re.M)


def probe_sizes(needed: dict[str, list[str]], ver: str, cc: str) -> dict[str, dict[str, int | None]]:
    """name -> {'pc', 'pc_elem', 'ps1', 'ps1_elem'} sizes (None where sizeof fails), from the files using it.

    A probe line may fail (sizeof an incomplete array, the element of a non-array); anything else
    that keeps a probe from giving sizes raises ProbeError: a source that does not compile, an
    error outside the probe lines, a probe the object lacks, a name with no size in a layout."""
    by_src: dict[Path, list[str]] = {}
    for name, objs in needed.items():
        for obj in objs:
            by_src.setdefault(source_of(obj), []).append(name)
    jobs = [(src, names, layout, elem) for src, names in by_src.items() for layout in LAYOUTS for elem in (0, 1)]
    flags = probe_flags(cc)

    def run(job):
        src, names, layout, elem = job
        names = list(dict.fromkeys(names))
        result: dict[str, int | None] = {n: None for n in names}
        what = f"{src} ({layout} layout{', element sizes' if elem else ''})"
        with tempfile.TemporaryDirectory() as tmp:
            c, o = Path(tmp) / "probe.c", Path(tmp) / "probe.o"
            while names:
                lines = [f'#include "{src}"']
                for i, n in enumerate(names):
                    lines.append(f"char xg_probe_{i}[sizeof({'(' + n + ')[0]' if elem else n})];")
                c.write_text("\n".join(lines) + "\n")
                p = subprocess.run([cc, "-c", *GAME_FLAGS, *flags, *LAYOUTS[layout], f"-DVERSION_{ver.upper()}",
                                    str(c), "-o", str(o)], cwd=ROOT, capture_output=True, text=True)
                if p.returncode != 0:
                    bad, other = set(), []
                    for m in PROBE_ERROR.finditer(p.stderr):
                        i = int(m["line"]) - 2  # line 1 includes the source
                        if m["file"] == str(c) and 0 <= i < len(names):
                            bad.add(i)
                        else:
                            other.append(p.stderr[m.start():].splitlines()[0])
                    if other or not bad:
                        raise ProbeError(f"{what} does not compile with {cc}:\n  "
                                         + "\n  ".join(other[:10] or p.stderr.strip().splitlines()[-10:]))
                    names = [n for i, n in enumerate(names) if i not in bad]
                    continue
                nm_out = subprocess.run(["nm", "-S", str(o)], capture_output=True, text=True)
                if nm_out.returncode != 0:
                    raise ProbeError(f"nm -S failed on the probe of {what}:\n{nm_out.stderr.strip()}")
                found = {int(m.group(2)): int(m.group(1), 16)
                         for m in re.finditer(r"^\S+ (\S+) [BbCcDd] xg_probe_(\d+)$", nm_out.stdout, re.M)}
                lost = [n for i, n in enumerate(names) if i not in found]
                if lost:
                    raise ProbeError(f"the probe of {what} compiled, but nm shows no size for "
                                     f"{', '.join(lost[:8])}{' ...' if len(lost) > 8 else ''}")
                for i, v in found.items():
                    result[names[i]] = v
                break
        key = f"{layout}{'_elem' if elem else ''}"
        return key, result

    sizes: dict[str, dict[str, int | None]] = {n: {} for n in needed}
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for key, result in pool.map(run, jobs):
            for n, v in result.items():
                if v is not None and (sizes[n].get(key) is None or v > sizes[n][key]):
                    sizes[n][key] = v
    # Every name has a size in each layout, its own or its element's, and the two layouts agree on
    # which: the declarations are the same, only pointers and tags change size.
    odd = []
    for n, s in sorted(sizes.items()):
        got = {k for k, v in s.items() if v is not None}
        if not got & {"pc", "pc_elem"} or not got & {"ps1", "ps1_elem"} or \
                ({"pc", "ps1"} & got and not {"pc", "ps1"} <= got) or \
                ({"pc_elem", "ps1_elem"} & got and not {"pc_elem", "ps1_elem"} <= got):
            odd.append(f"{n} ({', '.join(sorted(got)) or 'no size at all'})")
    if odd:
        stale = stale_objects([n for n, s in sizes.items() if not any(v is not None for v in s.values())],
                              needed, ver, cc, flags)
        if stale:
            for obj in stale:
                Path(obj).unlink(missing_ok=True)
            raise ProbeError(
                f"{len(stale)} object(s) reference names their sources no longer spell: they were compiled "
                "from other text (a source replaced by an older-dated copy, which make tools do not see as "
                "a change). Removed them; build again:\n  "
                + "\n  ".join(f"{obj} ({', '.join(names[:4])}{' ...' if len(names) > 4 else ''})"
                              for obj, names in list(stale.items())[:20]))
        raise ProbeError(f"layout probes with {cc} left {len(odd)} name(s) without sizes in both layouts:\n  "
                         + "\n  ".join(odd[:20]))
    return sizes


def stale_objects(names: list[str], needed: dict[str, list[str]], ver: str, cc: str,
                  flags: list[str]) -> dict[str, list[str]]:
    """Objects that reference a name (here: one no probe could size) their source does not spell
    even once after preprocessing: compiled from older text, so a rebuild, not the link, is wrong.

    An object only names what its source spelled when it was compiled (through macros too: the
    preprocessed text has the expansion), so a name the current source lacks means the object is
    stale: a file put back with an older modification time than the object (a copy that keeps
    times) is not rebuilt, and the next run of this script meets the old object."""
    spelled: dict[Path, set[str] | None] = {}
    stale: dict[str, list[str]] = {}
    for n in names:
        for obj in dict.fromkeys(needed.get(n, [])):
            src = source_of(obj)
            if src not in spelled:
                p = subprocess.run([cc, "-E", *GAME_FLAGS, *flags, *LAYOUTS["pc"], f"-DVERSION_{ver.upper()}",
                                    str(src)], cwd=ROOT, capture_output=True, text=True)
                spelled[src] = set(re.findall(r"[A-Za-z_]\w*", p.stdout)) if p.returncode == 0 else None
            if spelled[src] is not None and n not in spelled[src]:
                stale.setdefault(obj, []).append(n)
    return stale


def pointers_outside(out_by_module: dict[str, list[str]]) -> list[str]:
    """Generated pointers `(char*)X + N` into a generated byte object X with N at or past its end.

    Byte objects keep the PS1 offsets, so N must lie inside X; a pointer past it reads whatever the
    PC linker put next (battling's heap type names past a string blob cut at its last .asciz)."""
    sizes: dict[str, int] = {}
    refs: list[tuple[str, str, int]] = []
    for mod, lines in out_by_module.items():
        for line in lines:
            for m in re.finditer(r"\bunsigned char (\w+)\[(\d+)\]", line):
                sizes[m.group(1)] = max(sizes.get(m.group(1), 0), int(m.group(2)))
            for m in re.finditer(r"\(char\*\)(\w+) \+ (\d+)\)", line):
                refs.append((mod, m.group(1), int(m.group(2))))
    return [f"{mod}: {sym} + {add} (the object is {sizes[sym]} bytes)"
            for mod, sym, add in refs if sym in sizes and add >= sizes[sym]]


def relayout(img: Image, addr: int, q: int, size_pc: int, s: dict, sources: list | None = None) -> list | None:
    """Retail bytes of a struct (or struct array) with pointers, in the PC layout, as 8-byte words
    (ints, or (symbol, addend) for a pointer); None when the layout cannot be derived. `sources`,
    when given, gets each PC byte's PS1 address (None for a pointer or padding byte).

    The pointer fields are the element offsets where any element holds a symbolised word; every
    other byte keeps its order, and a pointer field moves to the next 8-byte boundary and takes 8
    bytes. Each element must come out at the PC element size the compiler gives the type, short of
    its final padding: a guard against fields the data cannot show (a NULL-only pointer, an 8-byte
    integer, a nested struct's own padding). Read the result where it matters."""
    e1 = s.get("ps1_elem") or q
    e2 = s.get("pc_elem") or size_pc
    if not q or not e1 or q % e1 or size_pc != (q // e1) * e2 or e2 % 8:
        return None
    ptr_offs = {(a - addr) % e1 for a, _, _ in img.refs_in(addr, q)}
    if any(o % 4 for o in ptr_offs):
        return None
    out: list = []  # bytes (int) and pointers ((symbol, addend), 8 bytes each)
    src: list = []  # each byte's PS1 address
    for i in range(q // e1):
        base, start, o = addr + i * e1, len(out), 0
        while o < e1:
            if o in ptr_offs:
                while (len(out) - start) % 8:
                    out.append(0)
                    src.append(None)
                if base + o in img.refs:
                    out.append(img.refs[base + o])
                elif any(img.get(base + o, 4)):
                    return None  # a number in a pointer field
                else:
                    out.append(("", 0))
                out += [None] * 7
                src += [None] * 8
                o += 4
            else:
                out.append(img.mem.get(base + o, 0))
                src.append(base + o)
                o += 1
        if (len(out) - start + 7) // 8 * 8 != e2:
            return None  # the PC type has more (or less) than the fields the data shows
        src += [None] * (e2 - (len(out) - start))
        out += [0] * (e2 - (len(out) - start))
    words: list = []
    for w in range(0, len(out), 8):
        chunk = out[w:w + 8]
        if isinstance(chunk[0], tuple):
            words.append(chunk[0] if chunk[0][0] else 0)
        elif any(isinstance(c, tuple) or c is None for c in chunk):
            return None
        else:
            words.append(int.from_bytes(bytes(chunk), "little"))
    if sources is not None:
        sources[:] = src
    return words


# Where the retail bytes go: the executable holds none of them. Each module's
# generated objects are declared as before (same sizes, same order), as zero
# storage in .data, and a copy list says which bytes of the module's file on
# the player's disc fill them (port/src/system/gamedata.c, at startup, before
# anything reads them). A copy is (object + offset, offset in the file, size).
DISC_SPECIFIC = 0x80000000  # size flag: bytes each disc has its own (not hashed)
# Main's embedded file table (0x8000 bytes at 0x80010004) and directory table
# (0x80 bytes at 0x80018004) differ between the two discs' boot executables
# (port/src/cd/boot.c): the copy takes them from the disc read, and the check
# that the disc is this build's release leaves them out. [start, end) by address.
DISC_RANGES = {"main": [(0x80010004, 0x80018084)]}


def copy_runs(pairs: list[tuple[int, int | None]], img: Image,
              cuts: list[tuple[int, int]] = ()) -> list[tuple[int, int, int]]:
    """(destination offset, PS1 address, size) runs from (destination offset, PS1 address) pairs,
    over the addresses the module's data holds (other bytes stay zero); a run never crosses
    the start or end of a range in `cuts`."""
    edges = {e for r in cuts for e in r}
    runs: list[tuple[int, int, int]] = []
    for off, a in pairs:
        if a is None or a not in img.mem:
            continue
        if runs and runs[-1][0] + runs[-1][2] == off and runs[-1][1] + runs[-1][2] == a and a not in edges:
            runs[-1] = (runs[-1][0], runs[-1][1], runs[-1][2] + 1)
        else:
            runs.append((off, a, 1))
    return runs


def fnv1a64(chunks) -> int:
    h = 0xCBF29CE484222325
    for chunk in chunks:
        for b in chunk:
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def disc_files(ver: str) -> dict[str, dict]:
    """module -> where its file is on the disc (config/<ver>/disc.yaml): {'iso'} for the boot
    executable, else {'dir', 'file', 'lzss'}; and 'path', its extracted image under disc/<ver>/."""
    out = {}
    for m in re.finditer(r"\{(name:[^}]*)\}", (ROOT / "config" / ver / "disc.yaml").read_text()):
        kv = dict((k.strip(), v.strip()) for k, _, v in (p.partition(":") for p in m.group(1).split(", ")
                                                          if ":" in p and not p.strip().startswith("copies")))
        name = kv["name"]
        entry = {"path": ROOT / "disc" / ver / (f"{name}.exe" if "iso" in kv else f"{name}.bin")}
        if "iso" in kv:
            entry["iso"] = kv["iso"]
        else:
            entry.update(dir=int(kv["dir"], 0), file=int(kv["file"], 0), lzss=kv.get("lzss") == "true")
        out[name] = entry
    return out


def module_of(obj: str) -> str:
    m = re.search(r"game_(\w+)\.dir", obj)
    if not m:
        raise ValueError(obj)
    return m.group(1)


def export_platform_symbols(exports: dict[str, set[str]], names: set[str],
                            defined: dict[str, set[str]], generated: dict[str, set[str]]) -> None:
    """Platform consumers see a module's generated data and its compiled C alike."""
    for name in sorted(names):
        owners = {mod for mod in exports if name in defined.get(mod, set()) or
                  name in generated.get(mod, set())}
        if len(owners) > 1:
            raise ValueError(f"platform references ambiguous game symbol {name}: {', '.join(sorted(owners))}")
        if owners:
            exports[next(iter(owners))].add(name)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("objects", nargs="+")
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    ap.add_argument("--export", action="append", default=[], help="a game symbol the port references")
    ap.add_argument("--game-root", type=Path, default=ROOT,
                    help="the tree whose src/ the objects came from and whose include/ the probes use: "
                         "the repository (the Original engine) or engine/ (the Enhanced engine's fork, "
                         "tools/fork_engine.py)")
    ap.add_argument("--platform", nargs="*", default=[],
                    help="the platform layer's objects: what they define is not stubbed, what they use is exported")
    args = ap.parse_args()
    GAME_FLAGS[GAME_FLAGS.index("-Iinclude")] = f"-I{args.game_root / 'include'}"

    syms_all: dict[str, dict[str, tuple[int, bool]]] = {}
    for f in sorted((ROOT / "config" / args.ver / "symbols").glob("*.txt")):
        for line in f.read_text().splitlines():
            m = re.match(r"\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)", line)
            if m:
                syms_all.setdefault(m.group(1), {})[f.stem] = (int(m.group(2), 16), "type:func" in m.group(3))
    images, labels_first = load_images(args.ver)
    labels_all: dict[str, dict[str, int]] = {}
    for mod, img in images.items():
        pass
    blob_files = [p for p in sorted((ROOT / "asm" / args.ver).glob("*/nonmatchings/*/*.s"))
                  if "\ndlabel " in p.read_text(errors="replace")]  # INCLUDE_RODATA data (load_images)
    for path in sorted((ROOT / "asm" / args.ver).glob("*/data/*.s")) + blob_files:
        mod = path.relative_to(ROOT / "asm" / args.ver).parts[0]
        pending = []
        for line in path.read_text(errors="replace").splitlines():
            m = LABEL.match(line)
            if m and not m.group(1).startswith(".L"):
                pending.append(m.group(1))
                continue
            m = DATA_LINE.match(line)
            if m and pending:
                for n in pending:
                    labels_all.setdefault(n, {}).setdefault(mod, int(m["vram"], 16))
                pending = []
    sdk = sdk_functions(args.game_root)
    hand = set(re.findall(r"\b([A-Za-z_]\w+)\b", " ".join(
        l.split("names:", 1)[1] for l in (ROOT / "config/handwritten.yaml").read_text().splitlines() if "names:" in l)))

    by_module: dict[str, list[str]] = {}
    for o in args.objects:
        by_module.setdefault(module_of(o), []).append(o)
    modules = sorted(by_module, key=lambda m: (m != "main", m))
    defined: dict[str, set[str]] = {}
    undefined: dict[str, dict[str, list[str]]] = {}
    common: dict[str, set[str]] = {}
    data_objects: dict[str, dict[str, tuple[str, str, int]]] = {}
    references: dict[str, dict[str, list[str]]] = {}
    for mod in modules:
        defined[mod], undefined[mod], common[mod], data_objects[mod], references[mod] = nm(by_module[mod])
    definers: dict[str, set[str]] = {}
    for mod in modules:
        for n in defined[mod]:
            definers.setdefault(n, set()).add(mod)
    platform_defined, platform_undefined, _, _, _ = nm(args.platform) if args.platform else (set(), {}, set(), {}, {})

    def is_function(name: str) -> bool:
        info = syms_all.get(name)
        if info:
            return any(isf for _, isf in info.values())
        return name.startswith("func_") or name in sdk or name in hand or not (name in labels_all or name.startswith("D_"))

    known_addrs: dict[str, list[int]] = {}
    for name, per in labels_all.items():
        for mod, a in per.items():
            known_addrs.setdefault(mod, []).append(a)
    for name, per in syms_all.items():
        for mod, (a, isf) in per.items():
            if not isf:
                known_addrs.setdefault(mod, []).append(a)
    # The end of each module's data bounds its last label (a table at the end of .data had no
    # extent, so it got 16 zero bytes instead of its retail contents).
    for mod, img in images.items():
        if img.mem:
            known_addrs.setdefault(mod, []).append(max(img.mem) + 1)
    # So does the end of its BSS: main's last BSS table (D_8006FAC8, LIBSPU's malloc table) ran
    # on over the overlay area to the next name main uses, 0x800C3664 (~340 KB).
    bss_bounds = module_bss(args.ver)
    bss_ends = {mod: end for mod, (_, end) in bss_bounds.items()}
    for mod, end in bss_ends.items():
        known_addrs.setdefault(mod, []).append(end)

    def in_data(mod: str, a: int | None) -> str:
        """The section attribute for zero storage that is data on the PS1 (below the BSS of a
        module with known bounds: main). Zero storage goes to .bss on the PC, but main's soft
        reset clears its BSS only, as the PS1's startKernel does."""
        lo = bss_bounds.get(mod, (None, None))[0]
        return ', section(".data")' if lo is not None and a is not None and 0 <= a < lo else ""
    for mod in known_addrs:
        known_addrs[mod] = sorted(set(known_addrs[mod]))

    extents = module_images(args.ver)
    overlay_base = min(lo for m, (lo, _) in extents.items() if m != "main")

    referencers: dict[str, set[str]] = {}
    for mod in modules:
        for n in undefined[mod]:
            referencers.setdefault(n, set()).add(mod)

    def bss_owner(name: str, a: int, user: str) -> str | None:
        """The module whose BSS holds `name` at `a`, outside `user`'s image.

        BSS has no data labels. An overlay's BSS follows its image, and main and the
        overlays it loads name or use it too (battle's sub-overlays name battle's BSS):
        the owner is the module naming or using the name there whose image starts
        highest at or below the address, when that module is unique (or is the user)."""
        cands = {m for m, (ma, f) in syms_all.get(name, {}).items() if ma == a and not f}
        cands = [m for m in cands | referencers.get(name, set()) if m in extents and extents[m][0] <= a]
        if not cands:
            return None
        top = max(extents[m][0] for m in cands)
        best = [m for m in cands if extents[m][0] == top]
        return user if user in best else best[0] if len(best) == 1 else None

    def owner_by_name(name: str, user: str) -> tuple[str, int | None]:
        """(owning module, PS1 address) of a data symbol referenced by module `user`, as the names
        tell it: the address is the one `user` (or main, or the one module naming it) gives the
        name; the owner, what owner_at cannot decide by address alone."""
        for pick in (user, "main"):
            if pick in labels_all.get(name, {}):
                return pick, labels_all[name][pick]
            if pick in syms_all.get(name, {}) and not syms_all[name][pick][1]:
                a = syms_all[name][pick][0]
                holders = [m for m, la in labels_all.get(name, {}).items() if la == a]
                lo, hi = extents.get(pick, (0, 0))
                if pick != "main":
                    # An overlay's symbol file also names addresses it only refers to (JP's are
                    # generated from US by tools/share_symbols.py): main's below the overlays, or
                    # the one other module whose image labels the name there.
                    if extents["main"][0] <= a < overlay_base:
                        return "main", a
                    if not lo <= a < hi and len(holders) == 1:
                        return holders[0], a
                # Unlabelled (BSS) past the module's image: the overlay whose BSS it is.
                if not lo <= a < hi and not holders and a >= overlay_base:
                    host = bss_owner(name, a, pick)
                    if host is not None:
                        return host, a
                return pick, a
        others = sorted(set(labels_all.get(name, {})) | {m for m, (a, f) in syms_all.get(name, {}).items() if not f})
        if len(others) == 1:
            o = others[0]
            a = labels_all.get(name, {}).get(o, syms_all.get(name, {}).get(o, (None, False))[0])
            lo, hi = extents.get(o, (0, 0))
            if name not in labels_all and a is not None and not lo <= a < hi and a >= overlay_base:
                o = bss_owner(name, a, user) or o
            return o, a
        m = re.fullmatch(r"D_([0-9A-F]{8})", name)
        a = int(m.group(1), 16) if m else None
        # Several modules name it: the one address they agree on is its address (JP's symbol files
        # give US names their JP addresses: a D_ name's own digits are the US address there).
        agreed = {labels_all.get(name, {}).get(o, syms_all.get(name, {}).get(o, (None, False))[0]) for o in others}
        if len(others) > 1 and len(agreed) == 1 and None not in agreed:
            a = agreed.pop()
        if a is not None and a < overlay_base and user != "main":
            return "main", a
        lo, hi = extents.get(user, (0, 0))
        if a is not None and not lo <= a < hi and a >= overlay_base:
            host = bss_owner(name, a, user)
            if host is not None:
                return host, a
        return user, a

    exports: dict[str, set[str]] = {m: set() for m in modules}
    exports["main"] |= set(args.export)
    funcs: set[str] = set()
    libc: set[str] = set()
    # What each module's C references: data another module's C defines (exported by name, the
    # PS1 addresses checked below), its own tentative definitions, and data the link generates.
    c_defined: list[tuple[str, str, str]] = []  # (user, name, defining module)
    data_refs: list[tuple[str, str, list[str], bool]] = []  # (user, name, objects, tentative)
    for mod in modules:
        for name, objs in undefined[mod].items():
            if re.match(r"port[A-Z]|_GLOBAL_|__", name) or name in platform_defined:
                continue  # the platform layer's own entry points and definitions, and the toolchain's
            others = definers.get(name, set()) - {mod}
            if name not in common[mod] and len(others) == 1:
                c_defined.append((mod, name, next(iter(others))))
                exports[next(iter(others))].add(name)
                continue
            if name not in common[mod] and len(others) > 1:
                print(f"warning: {mod} references {name}, defined in {sorted(others)}", file=sys.stderr)
                continue
            if name in common[mod]:
                data_refs.append((mod, name, objs, True))
            elif name in HOST_LIBC:
                libc.add(name)
            elif is_function(name):
                funcs.add(name)
            else:
                data_refs.append((mod, name, objs, False))

    probe_input: dict[str, list[str]] = {}
    for _, name, objs, _ in data_refs:
        probe_input.setdefault(name, []).extend(objs)
    # And the data a C file defines that others use: how they declare it (an array of 16 bytes
    # or more at an address the x86-64 ABI does not promise is a hazard, below).
    c_data_refs: list[tuple[str, str, list[str]]] = []  # (defining module, name, referencing objects)
    for mod in modules:
        for name, objs in references[mod].items():
            definer = mod if name in data_objects[mod] else None
            if definer is None and name not in defined[mod]:
                others = [m for m in modules if name in data_objects[m]]
                definer = others[0] if len(others) == 1 and name in exports[others[0]] else None
            if definer is not None:
                c_data_refs.append((definer, name, objs))
                probe_input.setdefault(name, []).extend(objs)
    try:
        sizes = probe_sizes(probe_input, args.ver, args.cc)
    except ProbeError as e:
        raise SystemExit(f"pc_link: {e}") from None

    # Owners by address. On the PS1 a module reaches another loaded module's data by address;
    # names only say where. Every reference's address comes from the names (owner_by_name);
    # its owner, from that address: two names for one address are one object, and the same
    # name in modules that are never loaded together stays two.
    uses: dict[str, list[tuple[int, int]]] = {}  # module -> PS1 ranges it names or uses
    for mod, name, _, _ in data_refs:
        a = owner_by_name(name, mod)[1]
        if a is not None:
            uses.setdefault(mod, []).append((a, a + max(sizes.get(name, {}).get("ps1") or 1, 1)))
    for name, per in labels_all.items():
        for mod, a in per.items():
            uses.setdefault(mod, []).append((a, a + 1))
    for name, per in syms_all.items():
        for mod, (a, isf) in per.items():
            if not isf:
                uses.setdefault(mod, []).append((a, a + 1))
    use_index: dict[str, tuple[list[int], list[int]]] = {}  # module -> range starts, running max of ends
    for mod, ranges in uses.items():
        ranges.sort()
        ends, top = [], 0
        for _, e in ranges:
            top = max(top, e)
            ends.append(top)
        use_index[mod] = ([s for s, _ in ranges], ends)

    def uses_address(mod: str, a: int) -> bool:
        starts, ends = use_index.get(mod, ([], []))
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and ends[i] > a

    def owner_at(a: int, user: str) -> str | None:
        """The module whose storage PS1 address `a` is when `user` uses it, or None when the
        address cannot tell: `user`'s own image; main's (data and BSS below the overlays); the
        image of the one module loaded beside `user` that holds it; else the BSS of the module
        loaded beside `user` that starts highest at or below it, among those that use it
        (an overlay's BSS has no recorded bounds: battle's sub-modules use battle's)."""
        lo, hi = extents.get(user, (0, 0))
        if lo <= a < hi:
            return user
        if extents["main"][0] <= a < overlay_base:
            return "main"
        if a < overlay_base:
            return None  # the kernel's RAM or the scratchpad: no module's
        beside = [m for m in extents if m != "main" and extents[m][0] <= a and coresident(user, m, extents)]
        holding = [m for m in beside if a < extents[m][1]]
        if holding:
            return holding[0] if len(holding) == 1 else None
        users = [m for m in beside + ["main"] if uses_address(m, a)]
        if not users:
            return None
        top = max(extents[m][0] for m in users)
        best = [m for m in users if extents[m][0] == top]
        return best[0] if len(best) == 1 else None

    def owner_of(name: str, user: str) -> tuple[str, int | None]:
        """(owning module, PS1 address) of a data symbol referenced by module `user`."""
        owner, a = owner_by_name(name, user)
        if a is not None:
            owner = owner_at(a, user) or owner
        return owner, a

    needed: dict[tuple[str, str], list[str]] = {}  # (owner, name) -> referencing objects
    addr_of: dict[tuple[str, str], int | None] = {}
    for mod, name, objs, tentative in data_refs:
        if tentative:
            key = (mod, name)
            needed.setdefault(key, []).extend(objs)
            addr_of[key] = owner_of(name, mod)[1]
            continue
        owner, a = owner_of(name, mod)
        key = (owner, name)
        known = addr_of.get(key)
        if known is not None and a is not None and known != a:
            raise SystemExit(f"pc_link: {owner} {name} is at 0x{known:08X} for one module and "
                             f"0x{a:08X} for {mod}: one name for two addresses")
        if known is None:
            addr_of[key] = a
        needed.setdefault(key, []).extend(objs)
        if owner != mod:
            exports[owner].add(name)
    # Data another module's C defines is linked by name: the name must be at the address the
    # referencing module gives it.
    for mod, name, definer in c_defined:
        a, d = owner_by_name(name, mod)[1], owner_by_name(name, definer)[1]
        if a is not None and d is not None and a != d:
            raise SystemExit(f"pc_link: {mod} names 0x{a:08X} {name}, but {definer}'s C defines {name} "
                             f"at 0x{d:08X}: one name for two addresses")

    def ps1_extent(name: str, mod: str, addr: int) -> int | None:
        s = sizes.get(name, {})
        if s.get("ps1"):
            return s["ps1"]
        if addr == bss_ends.get(mod):
            return END_MARKER  # the end of the module's BSS (main's D_8006FAF0: the heap start)
        lst = known_addrs.get(mod)
        if lst:
            i = bisect.bisect_right(lst, addr)
            if i < len(lst):
                return lst[i] - addr
        return None

    generated_by: dict[str, str] = {}
    for gowner, gname in needed:
        generated_by.setdefault(gname, gowner)
    unconverted: list[str] = []
    numeric_refs: list[str] = []
    report = {k: 0 for k in ("bytes", "zero", "pointers", "relaid", "alias", "grown", "pointed-at", "common-init",
                             "unconverted", "no-size", "copies", "copied bytes")}
    files = disc_files(args.ver)
    module_data: dict[str, dict] = {}
    # Addresses some name there stands for (used by C, named in a symbol file, or pointed at by data),
    # with each module's data end: the extent of data that only other data points at.
    named = {n for _, n in needed} | set(syms_all)
    meaningful: dict[str, list[int]] = {}
    for mod, img in images.items():
        targets = {sym for sym, _ in img.refs.values()}
        addrs = {a for n, per in labels_all.items() if mod in per and (n in named or n in targets) for a in [per[mod]]}
        addrs |= {a for n, per in syms_all.items() for m, (a, f) in per.items() if m == mod and not f}
        if img.mem:
            addrs.add(max(img.mem) + 1)
        addrs |= img.ends
        meaningful[mod] = sorted(addrs)
    out_by_module: dict[str, list[str]] = {}
    generated_symbols: dict[str, set[str]] = {}
    # Every PC object that stands for PS1 memory: (module, name, address, PS1 extent, why it may
    # overlap another: '' when it must not).
    storage: list[tuple[str, str, int, int, str]] = []
    alias_offsets: dict[tuple[str, str], int] = {}  # (module, alias) -> its offset in its container
    for owner in modules:
        keys = sorted(k for k in needed if k[0] == owner)
        generated_symbols[owner] = {name for _, name in keys}
        lines = [f"/* Generated by tools/pc_link.py for {owner}; do not edit. The retail bytes are not here: "
                 "xgDataCopies_{owner} fills them from the player's disc at startup. */", ""]
        externs: set[str] = set()
        copies: list[tuple[str, int, int, int, bool]] = []  # (object, offset, PS1 address, size, disc-specific)
        ranges = DISC_RANGES.get(owner, [])

        def add_copies(obj: str, pairs: list[tuple[int, int | None]]) -> None:
            for off, a, n in copy_runs(pairs, img, ranges):
                copies.append((obj, off, a, n, any(lo <= a < hi for lo, hi in ranges)))
        aliases: list[str] = []
        emitted: set[str] = set()
        aliased: set[str] = set()
        pointer_arrays: set[str] = set()
        relaid: set[str] = set()
        placed: dict[str, tuple[int, int, bool]] = {}
        img = images.get(owner)
        entries = []
        for _, name in keys:
            addr = addr_of[(owner, name)]
            s = sizes.get(name, {})
            pc = s.get("pc")
            q = ps1_extent(name, owner, addr) if addr is not None else None
            if pc is None and q and s.get("ps1_elem") and s.get("pc_elem"):
                pc = (q // s["ps1_elem"]) * s["pc_elem"]  # incomplete array: the PS1 element count
            entries.append((addr if addr is not None else -1, -(q or 0), name, q, pc))
        entries.sort()
        # A name inside another one can run past that one's end (a spurious label cut it short,
        # as D_8005A3A0 was cut before g_spuVolumes ended): grow the outermost object over every
        # name that starts inside it, so they all alias one object and none is cut. Only an object
        # whose PC layout is the PS1 one grows: its PC bytes are the PS1 bytes.
        reach: dict[str, int] = {}
        starts = sorted({e[0] for e in entries if e[0] >= 0})
        lead = None  # (name, start, end, same layout)
        for addr, _, name, q, pc in entries:
            if addr < 0:
                continue
            end = addr + (q or pc or 0)
            if lead is not None and lead[1] <= addr < lead[2]:
                if end > lead[2] and lead[3]:
                    lead = (lead[0], lead[1], end, True)
                    reach[lead[0]] = end - lead[1]
            else:
                lead = (name, addr, end, bool(q) and (pc is None or pc == q))
        for addr, _, name, q, pc in entries:
            is_common = name in common[owner]
            if name in reach:
                grown = reach[name]
                numeric_refs.append(f"{owner} {name}: grown from 0x{q:X} to 0x{grown:X} bytes over the names inside it")
                pc, q = (grown if pc is None or pc == q else pc), grown
                report["grown"] += 1
            host = None
            if addr >= 0:
                for hname, (haddr, hext, same) in placed.items():
                    if haddr <= addr < haddr + hext:
                        host = (hname, haddr, same)
                        break
            if host is not None:
                hname, haddr, same = host
                if same or addr == haddr:  # another name for its start is the same object
                    aliases.append(f'__asm__(".globl {name}\\n.set {name}, {hname} + {addr - haddr}");')
                    alias_offsets[(owner, name)] = addr - haddr  # in a container aligned to 16
                    aliased.add(name)
                    report["alias"] += 1
                    nxt = next((a for a in starts if a > addr), None)
                    if pc and q and pc > q and nxt is not None and addr + pc > nxt:
                        unconverted.append(f"{owner} {name}: inside {hname}+0x{addr - haddr:X}, but its PC type "
                                           f"(0x{pc:X} bytes, PS1 0x{q:X}) runs into the next name, 0x{nxt - addr:X} "
                                           f"bytes on: the two overlap on the PC")
                    continue
                unconverted.append(f"{owner} {name}: inside {hname}+0x{addr - haddr:X}, whose PC layout differs; "
                                   f"given its own storage, so it does not alias the container on the PC")
                known = "inside a container whose PC layout differs (unconverted.txt)"
            else:
                known = ""
            size_pc = pc if pc else q
            if not size_pc:
                size_pc = 16
                report["no-size"] += 1
                unconverted.append(f"{owner} {name}: no size known; 16 bytes")
            decl = f"__attribute__((aligned(16){in_data(owner, addr)})) unsigned char {name}[{size_pc}]"
            have = img is not None and q and addr >= 0
            content = img.get(addr, q) if have else b""
            refs = img.refs_in(addr, q) if have else []
            zero = not any(content) and not refs
            same = bool(q) and size_pc == q
            if same and refs:
                numeric_refs.append(f"{owner} {name}: {len(refs)} symbolised word(s) kept as numbers "
                                    f"({', '.join(s for _, s, _ in refs[:4])})")
            if is_common and zero:
                pass  # the tentative definition gives it zero storage
            elif zero or not have:
                lines.append(f"{decl};")
                report["zero"] += 1
            elif same:
                # Zero storage in .data (an overlay's reset and main's soft reset keep it as data),
                # filled from the disc.
                lines.append(f"__attribute__((aligned(16), section(\".data\"))) unsigned char {name}[{size_pc}];")
                add_copies(name, [(i, addr + i) for i in range(q)])
                report["common-init" if is_common else "bytes"] += 1
            elif q % 4 == 0 and size_pc == 2 * q and all(
                    (a in img.refs) or not any(img.get(a, 4)) for a in range(addr, addr + q, 4)):
                items = []
                for a in range(addr, addr + q, 4):
                    if a in img.refs:
                        sym, add = img.refs[a]
                        externs.add(sym)
                        items.append(f"    (void*)((char*){sym} + {add})," if add else f"    (void*){sym},")
                    else:
                        items.append("    0,")
                lines.append(f"__attribute__((aligned(16))) void* {name}[{q // 4}] = {{\n" + "\n".join(items) + "\n};")
                pointer_arrays.add(name)
                report["pointers"] += 1
            elif refs and (words := relayout(img, addr, q, size_pc, sizes.get(name, {}),
                                             sources := [])) is not None:
                items = []
                for w in words:
                    if isinstance(w, tuple):
                        externs.add(w[0])
                        items.append(f"    (unsigned long long)((char*){w[0]} + {w[1]})," if w[1] else
                                     f"    (unsigned long long){w[0]},")
                    else:
                        items.append("    0ull,")  # its bytes come from the disc
                add_copies(name, list(enumerate(sources)))
                lines.append(f"__attribute__((aligned(16))) unsigned long long {name}[{len(words)}] = {{\n"
                             + "\n".join(items) + "\n};")  # it holds a pointer: .data
                pointer_arrays.add(name)  # declared ahead as an array of 8-byte words
                relaid.add(name)
                report["relaid"] += 1
            else:
                lines.append(f"{decl};")
                why = f"PC size {size_pc}, PS1 size {q}" + (f", {len(refs)} pointer slot(s)" if refs else "")
                unconverted.append(f"{owner} {name} (0x{addr:08X}): {why}")
                report["unconverted"] += 1
            if not (is_common and zero):
                emitted.add(name)
            if addr >= 0:
                placed[name] = (addr, q or size_pc, same or (zero and size_pc == q))
                # The end of main's BSS (the heap start) marks an address; it holds nothing there.
                storage.append((owner, name, addr, 0 if addr == bss_ends.get(owner) else q or 1, known))
        # Declarations first, then the data only other data points at (it may point on).
        decl_lines: list[str] = []
        def_lines: list[str] = []
        pending = sorted(externs)
        seen: set[str] = set()
        while pending:
            sym = pending.pop(0)
            if sym in seen:
                continue
            seen.add(sym)
            if sym in emitted:
                kind = "unsigned long long" if sym in relaid else "void*" if sym in pointer_arrays else "unsigned char"
                decl_lines.append(f"extern {kind} {sym}[];")
                continue
            owners = definers.get(sym, set())
            if owner in owners or sym in aliased:
                decl_lines.append(f"extern unsigned char {sym}[];")
            elif not owners and generated_by.get(sym, owner) != owner:
                # Another module's generated data (main's gameState), by name: the address this
                # module's data means must be that object's.
                a, k = owner_by_name(sym, owner)[1], generated_by[sym]
                if a is not None and (a != addr_of[(k, sym)] or owner_at(a, owner) not in (None, k)):
                    raise SystemExit(f"pc_link: {owner}'s data points at {sym} (0x{a:08X}), but {k}'s {sym} "
                                     f"is at 0x{addr_of[(k, sym)]:08X}, or the address is not {k}'s")
                exports[k].add(sym)
                decl_lines.append(f"extern unsigned char {sym}[];")
            elif len(owners) == 1:
                a, d = owner_by_name(sym, owner)[1], owner_by_name(sym, next(iter(owners)))[1]
                if a is not None and d is not None and a != d:
                    raise SystemExit(f"pc_link: {owner}'s data points at {sym} (0x{a:08X}), but "
                                     f"{next(iter(owners))}'s C defines {sym} at 0x{d:08X}")
                exports[next(iter(owners))].add(sym)
                decl_lines.append(f"extern unsigned char {sym}[];")
            elif is_function(sym):
                funcs.add(sym)
                decl_lines.append(f"extern unsigned char {sym}[];")
            else:
                # Data that only other data points at (an animation table's frames, a task list...):
                # its retail bytes up to the next name anything uses; a table of pointers (and
                # zeros) becomes a pointer array, and what it points at is emitted the same way.
                pa = labels_all.get(sym, {}).get(owner)
                ext = None
                if pa is not None and img is not None:
                    lst = meaningful.get(owner, [])
                    i = bisect.bisect_right(lst, pa)
                    ext = lst[i] - pa if i < len(lst) else None
                refs = img.refs_in(pa, ext) if ext else []
                if pa is not None:
                    storage.append((owner, sym, pa, ext or 1, ""))
                if ext and not refs:
                    generated_symbols[owner].add(sym)
                    decl_lines.append(f"extern unsigned char {sym}[];")
                    if any(img.get(pa, ext)):  # .data, as an initialiser that is not all zero puts it
                        def_lines.append(f"__attribute__((aligned(16), section(\".data\"))) unsigned char {sym}[{ext}];"
                                         " /* pointed at only */")
                        add_copies(sym, [(i, pa + i) for i in range(ext)])
                    else:
                        def_lines.append(f"__attribute__((aligned(16))) unsigned char {sym}[{ext}] = {{0}}; "
                                         "/* pointed at only */")
                    report["pointed-at"] += 1
                elif ext and pa % 4 == 0 and ext % 4 == 0 and all(
                        (a in img.refs) or not any(img.get(a, 4)) for a in range(pa, pa + ext, 4)):
                    generated_symbols[owner].add(sym)
                    items = []
                    for a in range(pa, pa + ext, 4):
                        if a in img.refs:
                            t, add = img.refs[a]
                            pending.append(t)
                            items.append(f"    (void*)((char*){t} + {add})," if add else f"    (void*){t},")
                        else:
                            items.append("    0,")
                    decl_lines.append(f"extern void* {sym}[];")
                    def_lines.append(f"__attribute__((aligned(16))) void* {sym}[{ext // 4}] = {{\n" + "\n".join(items)
                                     + "\n}; /* pointed at only */")
                    report["pointed-at"] += 1
                else:
                    generated_symbols[owner].add(sym)
                    decl_lines.append(f"extern unsigned char {sym}[];")
                    def_lines.append(f"__attribute__((aligned(16){in_data(owner, pa)})) unsigned char {sym}[16];"
                                     " /* pointed at only */")
                    unconverted.append(f"{owner} {sym}: only pointed at from data; 16 bytes of storage")
        extern_lines = decl_lines + def_lines
        lines[2:2] = extern_lines + [""]
        lines += ["", *aliases, ""]
        # The copy list: where each object's retail bytes are in the module's file.
        info = files.get(owner)
        if copies and info is None:
            raise SystemExit(f"pc_link: {owner} has data but no file in config/{args.ver}/disc.yaml")
        hash_chunks = []
        items = []
        if copies:
            if len(img.deltas) != 1:
                raise SystemExit(f"pc_link: {owner}'s data is not one contiguous image ({len(img.deltas)} offsets)")
            delta = next(iter(img.deltas))
            image = info["path"].read_bytes()
            for obj, off, a, n, own in copies:
                rom = a + delta
                got = image[rom:rom + n]
                if len(got) != n:
                    raise SystemExit(f"pc_link: {owner} {obj}+0x{off:X}: 0x{n:X} bytes at 0x{rom:X} run past "
                                     f"{info['path'].name}")
                if got != img.get(a, n):
                    numeric_refs.append(f"{owner} {obj}+0x{off:X}: the disc's bytes differ from the data assembly's "
                                        f"(the disc's are used)")
                if not own:
                    hash_chunks.append(got)
                ref = f"(char*){obj} + {off}" if off else f"(char*){obj}"
                items.append(f"    {{{ref}, 0x{rom:X}, 0x{n:X}{' | 0x80000000u' if own else ''}}},")
            report["copies"] += len(copies)
            report["copied bytes"] += sum(c[3] for c in copies)
        module_data[owner] = {"count": len(copies), "hash": fnv1a64(hash_chunks),
                              "size": info["path"].stat().st_size if info and info["path"].exists() else 0,
                              **(info or {})}
        lines += ["/* (object + offset, offset in the module's file, size; 0x80000000: each disc has its own) */",
                  "typedef struct { void* dst; unsigned int src; unsigned int size; } XgDataCopy;",
                  f"const XgDataCopy xgDataCopies_{owner}[] = {{", *items, "    {0, 0, 0},", "};", ""]
        exports[owner].add(f"xgDataCopies_{owner}")
        out_by_module[owner] = lines

    # One PS1 address, one PC object: the game C's own data (where the PS1 build put it) and what
    # this link generated must not overlap in modules that can be loaded together.
    c_objects = ps1_c_objects(args.ver, data_objects)
    separate: set[frozenset] = set()
    collisions = overlapping_storage(storage + c_objects, extents, SEPARATE, separate)
    # Nor may a module hold storage for another module's address (battle_loader's and
    # battle_result's copies of battle's BSS can never overlap: they are not loaded together).
    for m, n, a, size, why in storage:
        k = owner_at(a, m) if size and not why else None
        if k is not None and k != m:
            collisions.append(f"{m} {n} 0x{a:08X}+0x{size:X}: the address is {k}'s")
    if collisions:
        raise SystemExit("pc_link: two PC objects for one PS1 address (each module's copy would miss the "
                         "other's writes):\n  " + "\n  ".join(collisions))

    # Names the link (or the port's bss.c blocks) puts inside another object need not sit on the
    # 16 bytes the x86-64 ABI promises a global array of 16 bytes or more; clang relies on that
    # promise (aligned SSE stores: xg-htn.49). Every such name declared as an array of 16 bytes
    # or more must say so where it is declared (PORT_ALIAS, include/common.h).
    hazards: dict[str, list[str]] = {}  # name -> referencing objects
    for (owner, name), off in alias_offsets.items():
        s = sizes.get(name, {})
        if off % 16 and (s.get("pc") or 0) >= 16 and s.get("pc_elem"):
            hazards.setdefault(name, []).extend(probe_input.get(name, []))
    residues = {m: data_residues(data_objects[m]) for m in {d for d, _, _ in c_data_refs}}
    for definer, name, objs in c_data_refs:
        s = sizes.get(name, {})
        if residues[definer].get(name) != 0 and (s.get("pc") or 0) >= 16 and s.get("pc_elem"):
            hazards.setdefault(name, []).extend(objs)
    try:
        unmarked = alias_declarations(hazards, args.ver, args.cc)
    except ProbeError as e:
        raise SystemExit(f"pc_link: {e}") from None
    alias_arrays = len(hazards)
    if unmarked:
        raise SystemExit("pc_link: arrays at addresses not 16-byte aligned, declared without PORT_ALIAS "
                         "(the compiler may use aligned SSE moves on them):\n  " + "\n  ".join(unmarked))

    # Resolve after emission: generated definitions, aliases and data reached only
    # through another initializer are absent from the original game objects' nm.
    export_platform_symbols(exports, set(platform_undefined), defined, generated_symbols)
    out_stubs = ["/* Generated by tools/pc_link.py; do not edit. */", "", "void portStub(const char* name);", ""]
    all_defined = set().union(*defined.values())
    stubbed = sorted(n for n in funcs if n not in all_defined)
    # A stub for a function the platform layer implements under another name (the symbol file
    # renamed it after the port took the func_ name: _card_info vs bu.c's
    # func_8004E784, and the game saw no memory card) is a silent regression; stop the link.
    detached = []
    for n in stubbed:
        for mod, (a, _) in syms_all.get(n, {}).items():
            old = f"func_{a:08X}"
            if old != n and (old in platform_defined or old in all_defined):
                detached.append(f"{n} (0x{a:08X}, {mod}) is implemented as {old}")
    if detached:
        raise SystemExit("pc_link: stubs for functions the port implements under their old names "
                         "(rename the definition and its callers):\n  " + "\n  ".join(sorted(set(detached))))
    out_stubs += [f'long {n}() {{ portStub("{n}"); return 0; }}' for n in stubbed]

    overlays = list(modules)  # main first: its BSS is cleared by the soft reset
    out_modules = ["/* Generated by tools/pc_link.py; do not edit. */", "",
                   "/* Each module's data and BSS, renamed to sections xgdata_<module> / xgbss_<module>",
                   " * by the per-module link, so the linker provides their bounds. */",
                   "#include <stddef.h>", ""]
    for m in overlays:
        for kind in ("data", "bss"):
            out_modules.append(f"extern char __start_xg{kind}_{m}[] __attribute__((weak));")
            out_modules.append(f"extern char __stop_xg{kind}_{m}[] __attribute__((weak));")
    out_modules += ["", "typedef struct {", "    const char* name;", "    char* dataStart;", "    char* dataEnd;",
                    "    char* bssStart;", "    char* bssEnd;", "} PortModule;", "",
                    f"PortModule portModules[{len(overlays)}] = {{"]
    for m in overlays:
        out_modules.append(f'    {{"{m}", __start_xgdata_{m}, __stop_xgdata_{m}, __start_xgbss_{m}, __stop_xgbss_{m}}},')
    out_modules += ["};", f"const int portModuleCount = {len(overlays)};", ""]
    # What port/src/system/gamedata.c reads from the player's disc (the same structs there).
    out_modules += ["/* Each module's file on the disc and the copies that fill its data from it",
                    " * (port/src/system/gamedata.c): the boot executable (dir -1) or file `file` of",
                    " * directory `dir` of the game's file table, LZSS-compressed or not; its size as",
                    " * loaded, and the FNV-1a hash of the bytes copied (disc-specific ones left out). */",
                    "typedef struct { void* dst; unsigned int src; unsigned int size; } XgDataCopy;",
                    "typedef struct {", "    const char* name;", "    int dir, file, lzss;", "    unsigned int size;",
                    "    unsigned long long hash;", "    const XgDataCopy* copies;", "    int count;",
                    "} PortModuleData;", ""]
    for m in overlays:
        out_modules.append(f"extern const XgDataCopy xgDataCopies_{m}[];")
    out_modules += ["", f"const PortModuleData portModuleData[{len(overlays)}] = {{"]
    for m in overlays:
        d = module_data[m]
        loc = "-1, -1, 0" if "iso" in d or not d.get("path") else f"0x{d['dir']:X}, 0x{d['file']:X}, {int(d['lzss'])}"
        out_modules.append(f'    {{"{m}", {loc}, 0x{d["size"]:X}, 0x{d["hash"]:016X}ull, xgDataCopies_{m}, '
                           f'{d["count"]}}},')
    out_modules += ["};", f'const char portDataRegion[] = "{args.ver.upper()}";', ""]
    # The game C's own retail bytes, zeroed in each module's object at its link (tools/pc_srcdata.py
    # writes gen/<module>.src.c): filled from the same files, in portModuleData's order.
    out_modules += ["/* The game C's own retail bytes per module (tools/pc_srcdata.py): copies from the module's",
                    " * file, their count and hash. */",
                    "typedef struct { const XgDataCopy* copies; const int* count; const unsigned long long* hash; }"
                    " PortSrcData;"]
    for m in overlays:
        out_modules.append(f"extern const XgDataCopy xgSrcCopies_{m}[]; extern const int xgSrcCount_{m}; "
                           f"extern const unsigned long long xgSrcHash_{m};")
    out_modules += ["", f"const PortSrcData portSrcData[{len(overlays)}] = {{"]
    out_modules += [f"    {{xgSrcCopies_{m}, &xgSrcCount_{m}, &xgSrcHash_{m}}}," for m in overlays]
    out_modules += ["};", ""]

    outside = pointers_outside(out_by_module)
    if outside:
        raise SystemExit("pc_link: generated pointers past the end of the byte object they point into (its "
                         "extent is wrong: a blob or table cut short):\n  " + "\n  ".join(outside[:20]))

    args.outdir.mkdir(parents=True, exist_ok=True)
    for old in args.outdir.glob("*.c"):
        if old.stem not in modules and old.name not in ("stubs.c", "modules.c"):
            old.unlink()
    for mod in modules:
        (args.outdir / f"{mod}.c").write_text("\n".join(out_by_module.get(mod, ["/* nothing */"])) + "\n")
        # objcopy --keep-global-symbols rejects an empty list: a name nothing defines keeps it valid.
        (args.outdir / f"{mod}.exports").write_text("xg_no_exports\n" + "".join(f"{n}\n" for n in sorted(exports[mod])))
    (args.outdir / "stubs.c").write_text("\n".join(out_stubs) + "\n")
    (args.outdir / "modules.c").write_text("\n".join(out_modules) + "\n")
    (args.outdir / "unconverted.txt").write_text("\n".join(unconverted) + ("\n" if unconverted else ""))
    summary = [f"modules {len(modules)}, objects {len(args.objects)}",
               f"exports: " + ", ".join(f"{m} {len(exports[m])}" for m in modules if exports[m]),
               f"functions stubbed {len([n for n in funcs if n not in all_defined])}; host libc {len(libc)} "
               f"({' '.join(sorted(libc))})",
               f"data {len(needed)}: " + ", ".join(f"{k} {v}" for k, v in report.items()),
               f"one object per PS1 address: {len(storage)} generated and {len(c_objects)} C objects checked",
               f"arrays off the ABI's 16-byte alignment, all declared PORT_ALIAS: {alias_arrays}"]
    summary += [f"  apart on purpose: {' and '.join(' '.join(x) for x in sorted(pair))}: {SEPARATE[pair]}"
                for pair in sorted(separate, key=sorted)]
    (args.outdir / "report.txt").write_text("\n".join(summary + numeric_refs) + "\n")
    print("\n".join(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
