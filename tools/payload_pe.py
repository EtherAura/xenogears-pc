"""PE payload origins: GNU ld input ranges, bound to the image, checked
against COFF object bytes. No symbol-size guesses: COFF labels name a run
only after its section-relative bytes agree with the linked image.

COFF/archive field layouts: Microsoft's published PE/COFF specification,
https://learn.microsoft.com/en-us/windows/win32/debug/pe-format . The map
grammar is measured from this project's MinGW GNU ld output.
"""
from __future__ import annotations

from dataclasses import dataclass
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(exe: Path) -> None:
    mapping = Path(str(exe) + ".map")
    seal = {"version": 1, "exe": digest(exe), "map": digest(mapping)}
    Path(str(exe) + ".payload.json").write_text(json.dumps(seal, sort_keys=True) + "\n")


def verified_map(exe: Path) -> str:
    mapping = Path(str(exe) + ".map")
    try:
        seal = json.loads(Path(str(exe) + ".payload.json").read_text())
        if seal != {"version": 1, "exe": digest(exe), "map": digest(mapping)}:
            raise ValueError("stale executable/map hashes")
        return mapping.read_text()
    except (OSError, ValueError) as e:
        raise ValueError(f"no verified PE linker map for {exe}: {e}; rebuild with the current port CMake") from e


@dataclass(frozen=True)
class Span:
    address: int
    size: int
    section: str
    obj: str


def map_spans(text: str) -> list[Span]:
    out = []
    pending = None
    for line in text.splitlines():
        m = re.match(r"^ (\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+\.(?:o|obj)(?:\))?)$", line)
        if m:
            section, address, size, obj = m.groups()
        else:
            m = re.match(r"^ (\S+)\s*$", line)
            if m:
                pending = m[1]
                continue
            m = re.match(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+\.(?:o|obj)(?:\))?)$", line)
            if not pending or not m:
                pending = None
                continue
            section, (address, size, obj) = pending, m.groups()
        pending = None
        if int(address, 16) and int(size, 16):
            out.append(Span(int(address, 16), int(size, 16), section, obj))
    return sorted(out, key=lambda s: s.address)


def archive_members(blob: bytes) -> dict[str, list[bytes]]:
    """GNU/MS archives, including long names and duplicate member names."""
    if not blob.startswith(b"!<arch>\n"):
        raise ValueError("not a COFF archive")
    out: dict[str, list[bytes]] = {}
    at, strings = 8, b""
    while at < len(blob):
        header = blob[at:at + 60]
        if len(header) != 60 or header[58:] != b"`\n":
            raise ValueError("damaged archive header")
        name = header[:16].decode("ascii").rstrip()
        size = int(header[48:58])
        at += 60
        if size < 0 or size > len(blob) - at:
            raise ValueError("invalid archive member size")
        data = blob[at:at + size]
        if len(data) != size:
            raise ValueError("cut archive member")
        at += size + (size & 1)
        if name == "//":
            strings = data
        elif name.startswith("/") and name[1:].isdigit():
            offset = int(name[1:])
            if offset >= len(strings):
                raise ValueError("invalid archive long name")
            name = strings[offset:].split(b"\n", 1)[0].rstrip(b"/\0").decode()
            out.setdefault(name, []).append(data)
        elif not name.startswith("/"):
            out.setdefault(name.rstrip("/"), []).append(data)
    return out


class Coff:
    def __init__(self, blob: bytes):
        if len(blob) < 20:
            raise ValueError("cut COFF header")
        machine, count, _, symbols, nsyms, optional, _ = struct.unpack_from("<HHIIIHH", blob)
        if machine not in (0x8664, 0x14c) or 20 + optional + count * 40 > len(blob):
            raise ValueError("unsupported or cut COFF sections")
        end = symbols + nsyms * 18
        if end + 4 > len(blob):
            raise ValueError("cut COFF symbols")
        length, = struct.unpack_from("<I", blob, end)
        strings = blob[end:end + length]
        if length < 4 or len(strings) != length:
            raise ValueError("cut COFF strings")

        def string(offset: int) -> str:
            if offset < 4 or offset >= len(strings):
                raise ValueError("invalid COFF string offset")
            return strings[offset:].split(b"\0", 1)[0].decode(errors="replace")

        self.sections: list[tuple[str, bytes]] = []
        for i in range(count):
            at = 20 + optional + i * 40
            rawname, _, _, size, offset = struct.unpack_from("<8sIIII", blob, at)
            name = rawname.split(b"\0", 1)[0].decode()
            if name.startswith("/"):
                name = string(int(name[1:]))
            data = blob[offset:offset + size] if offset else b""
            if offset and len(data) != size:
                raise ValueError("cut COFF section")
            self.sections.append((name, data))
        self.labels: dict[int, list[tuple[int, str]]] = {}
        i = 0
        while i < nsyms:
            at = symbols + i * 18
            rawname, value, section, _, storage, aux = struct.unpack_from("<8sIhHBB", blob, at)
            name = string(struct.unpack_from("<I", rawname, 4)[0]) if rawname[:4] == b"\0" * 4 else \
                rawname.split(b"\0", 1)[0].decode(errors="replace")
            if 0 < section <= count and storage in (2, 3) and name != self.sections[section - 1][0]:
                self.labels.setdefault(section, []).append((value, name))
            i += 1 + aux
            if i > nsyms:
                raise ValueError("cut COFF auxiliary symbols")
        for labels in self.labels.values():
            labels.sort()

    def identify(self, section: str, offset: int, data: bytes) -> list[tuple[int, int, str]] | None:
        for i, (name, contents) in enumerate(self.sections, 1):
            if name != section or contents[offset:offset + len(data)] != data:
                continue
            # String pools can follow the last named object in a section:
            # do not pretend a nearest label gives an exact object size.
            if all(32 <= b < 127 for b in data) and (b" " in data or re.fullmatch(rb"[0-9a-fA-F]{16,}", data)):
                return [(0, len(data), "string_" + hashlib.sha256(data).hexdigest()[:16])]
            labels = self.labels.get(i, [])
            points = sorted({0, len(data)} | {v - offset for v, _ in labels if offset < v < offset + len(data)})
            out = []
            for lo, hi in zip(points, points[1:]):
                before = [(v, n) for v, n in labels if v <= offset + lo]
                label = before[-1][1] if before else f"{section}+0x{offset + lo:x}"
                out.append((lo, hi, label))
            return out
        return None


class Origins:
    def __init__(self, build: Path, mapping: str, objects: list[tuple[str, str, Path, str]]):
        self.build = build
        self.spans = map_spans(mapping)
        if not self.spans:
            raise ValueError("PE linker map has no input ranges")
        self.starts = [s.address for s in self.spans]
        if any(a.address + a.size > b.address for a, b in zip(self.spans, self.spans[1:])):
            raise ValueError("overlapping PE linker input ranges")
        self.objects = {p.resolve(): (cat, src) for cat, src, p, _ in objects}
        self.archives: dict[Path, dict[str, list[bytes]]] = {}
        self.coff: dict[tuple[Path, str], list[Coff]] = {}

    def origin(self, span: Span, offset: int, data: bytes):
        obj = span.obj
        archive = re.match(r"^(.+\.a)\((.+)\)$", obj)
        if archive:
            path = (self.build / archive[1]).resolve()
            member = archive[2]
            key = path, member
            if path not in self.archives:
                self.archives[path] = archive_members(path.read_bytes())
            if key not in self.coff:
                self.coff[key] = [Coff(b) for b in self.archives[path].get(member, [])]
            source, category = f"library/{path.name}/{member}", "library"
        else:
            path = (self.build / obj).resolve()
            if path in self.objects:
                category, source = self.objects[path]
            elif path.parent == (self.build / "modules").resolve():
                return [(0, len(data), ("generated", f"modules/{path.name}", "-"))]
            elif re.search(r"\.ltrans\d+(?:\.ltrans)?\.o$", path.name):
                return [(0, len(data), None)]  # GCC LTO: original object/symbol evidence handles these
            else:
                return [(0, len(data), ("other", f"unknown mapped input: {obj}", "-"))]
            key = path, ""
            if key not in self.coff:
                self.coff[key] = [Coff(path.read_bytes())]
        for coff in self.coff[key]:
            labels = coff.identify(span.section, offset, data)
            if labels is not None:
                return [(lo, hi, (category, source, label)) for lo, hi, label in labels]
        # A mapped input that no longer agrees must not borrow another
        # object's allowance (including when a build's objects are stale).
        return [(0, len(data), ("other", f"mapped input bytes disagree: {source}", "-"))]

    def pieces(self, address: int, data: bytes):
        at, end = address, address + len(data)
        while at < end:
            i = bisect.bisect_right(self.starts, at) - 1
            span = self.spans[i] if i >= 0 and at < self.spans[i].address + self.spans[i].size else None
            stop = min(end, span.address + span.size if span else self.starts[i + 1] if i + 1 < len(self.starts) else end)
            contents = data[at - address:stop - address]
            for lo, hi, key in self.origin(span, at - span.address, contents) if span else [(0, len(contents), None)]:
                yield at - address + lo, at - address + hi, key
            at = stop
