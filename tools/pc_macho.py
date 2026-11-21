"""Darwin arm64 game-module objects. Only plain little-endian MH_OBJECT is accepted."""
import hashlib
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Section:
    index: int
    header: int
    segment: str
    name: str
    address: int
    size: int
    offset: int
    relocations: int
    count: int
    flags: int

    @property
    def key(self):
        return self.segment + "," + self.name


class Object:
    def __init__(self, data):
        self.data = bytearray(data)
        magic, cpu, _, kind, count = struct.unpack_from("<5I", data)
        if (magic, cpu, kind) != (0xfeedfacf, 0x100000c, 1):
            raise ValueError("expected a thin arm64 Mach-O relocatable object")
        self.sections = []
        self.symtab = self.dysymtab = None
        pos = 32
        for _ in range(count):
            cmd, size = struct.unpack_from("<II", data, pos)
            if size < 8 or pos + size > len(data):
                raise ValueError("invalid Mach-O load command")
            if cmd == 0x19:  # LC_SEGMENT_64
                nsects, = struct.unpack_from("<I", data, pos + 64)
                for i in range(nsects):
                    h = pos + 72 + 80 * i
                    sn, sg, addr, length, offset, _, rel, nr, flags = struct.unpack_from("<16s16sQQ5I", data, h)
                    self.sections.append(Section(len(self.sections) + 1, h, sg.rstrip(b"\0").decode(),
                                                 sn.rstrip(b"\0").decode(), addr, length, offset, rel, nr, flags))
            elif cmd == 2:  # LC_SYMTAB
                self.symtab = pos + 8
            elif cmd == 11:  # LC_DYSYMTAB
                self.dysymtab = pos + 8
            pos += size

    def data_sections(self):
        # C initializers, strings, literals and switch tables. Exclude code and linker metadata.
        return [(s.key, s.size, s.offset) for s in self.sections if s.size and
                (s.segment in ("__DATA", "__DATA_CONST") or
                 (s.segment == "__TEXT" and s.name in ("__const", "__cstring", "__literal4", "__literal8",
                                                       "__literal16"))) and s.flags & 255 not in (1, 12, 18)]

    def relocated(self, name):
        s = next(sec for sec in self.sections if sec.key == name)
        keep = set()
        for i in range(s.count):
            at, info = struct.unpack_from("<iI", self.data, s.relocations + 8 * i)
            width = 1 << ((info >> 25) & 3)
            if at < 0 or at + width > s.size:
                raise ValueError(f"invalid relocation in {name}")
            keep.update(range(at, at + width))
        return keep

    def mark_sources(self, anchors):
        """Anchor each stripped input section at its first byte and make its contents writable.

        Insert external N_SECT definitions before the undefined nlist entries. Indices of those
        undefined symbols move, including their uses in relocation_info and the indirect table.
        Leave the section offsets and relocation addends in place.
        """
        data = self.data
        sym, count, strings, size = struct.unpack_from("<4I", data, self.symtab)
        local, nlocal, external, nexternal, undef, nundef = struct.unpack_from("<6I", data, self.dysymtab)
        if (local, external, undef, undef + nundef) != (0, nlocal, nlocal + nexternal, count):
            raise ValueError("unsupported Mach-O symbol ordering")
        if any(struct.unpack_from("<6I", data, self.dysymtab + 24)) or any(
                struct.unpack_from("<4I", data, self.dysymtab + 56)):
            raise ValueError("expected per-section relocations in a plain clang object")
        names = data[strings:strings + size]
        entries = bytearray()
        for sec in self.sections:
            if sec.key in anchors:
                key = anchors[sec.key]
                entries.extend(struct.pack("<IBBHQ", len(names), 0x0f, sec.index, 0, sec.address))
                names.extend(("_" + key).encode() + b"\0")
                struct.pack_into("<16s16s", data, sec.header, b"__data", b"__DATA")
                struct.pack_into("<I", data, sec.header + 64, 0)  # S_REGULAR, no literal pooling
            for i in range(sec.count):
                pos = sec.relocations + 8 * i + 4
                info, = struct.unpack_from("<I", data, pos)
                if info & (1 << 27) and info & 0xffffff >= undef:
                    struct.pack_into("<I", data, pos, info + len(anchors))
        indirect, nindirect = struct.unpack_from("<2I", data, self.dysymtab + 48)
        for i in range(nindirect):
            pos = indirect + i * 4
            idx, = struct.unpack_from("<I", data, pos)
            if not idx & 0xc0000000 and idx >= undef:
                struct.pack_into("<I", data, pos, idx + len(anchors))
        # Keep the original offsets among the bytes that the restore table addresses.
        flags, = struct.unpack_from("<I", data, 24)
        struct.pack_into("<I", data, 24, flags & ~0x2000)  # MH_SUBSECTIONS_VIA_SYMBOLS
        table = data[sym:sym + undef * 16] + entries + data[sym + undef * 16:sym + count * 16]
        data.extend(bytes(-len(data) % 8))
        struct.pack_into("<4I", data, self.symtab, len(data), count + len(anchors), len(data) + len(table), len(names))
        struct.pack_into("<2I", data, self.dysymtab + 12, nexternal + len(anchors), undef + len(anchors))
        data.extend(table)
        data.extend(names)


def section_names(module):
    # Stable within the 16-byte Mach-O limit, independent of which modules are built together.
    suffix = hashlib.sha256(module.encode()).hexdigest()[:12]
    return "xgd" + suffix, "xgb" + suffix


def link(module, linker, objects, exports, output, tmp):
    data, bss = section_names(module)
    partial = Path(tmp) / "allocated.o"
    args = [linker, "-r", "-arch", "arm64"]
    subprocess.run([*args, "-v", "-d", "-rename_section", "__DATA", "__data", "__DATA", data,
                    "-rename_section", "__DATA", "__common", "__DATA", bss,
                    "-rename_section", "__DATA", "__bss", "__DATA", bss, *objects, "-o", partial], check=True)
    # ld-classic exports all newly allocated commons in the first pass. Localize only afterwards.
    names = Path(tmp) / "exports"
    names.write_text("".join("_" + n + "\n" for n in sorted(set(exports))))
    subprocess.run([*args, "-exported_symbols_list", names, partial, "-o", output], check=True)


def bounds(module, kind, end):
    data, bss = section_names(module)
    section = data if kind == "data" else bss
    return f'__asm("section${"end" if end else "start"}$__DATA${section}")'
