"""Verify read-only ELF archive ranges using the GNU link map and member bytes."""
from __future__ import annotations

import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from payload_pe import archive_members, digest, map_spans


def record(exe: Path, strip: str) -> None:
    """Bind the map to the link output and to what cmake --install --strip writes."""
    mapping = Path(f"{exe}.map")
    with tempfile.TemporaryDirectory(prefix="payload-strip-") as temp:
        stripped = Path(temp) / exe.name
        result = subprocess.run([strip, "-o", str(stripped), str(exe)], capture_output=True)
        if result.returncode:
            raise ValueError(f"could not strip the linked ELF (exit {result.returncode})")
        seal = {"version": 1, "exe": digest(exe), "map": digest(mapping), "stripped": digest(stripped)}
    Path(f"{exe}.payload.json").write_text(json.dumps(seal, sort_keys=True) + "\n")


def verified_map(linked: Path, candidate: Path) -> str:
    path = Path(f"{linked}.map")
    try:
        seal = json.loads(Path(f"{linked}.payload.json").read_text())
        raw_map = path.read_bytes()
        if seal.get("version") != 1 or seal.get("exe") != digest(linked) or \
                seal.get("map") != hashlib.sha256(raw_map).hexdigest():
            raise ValueError("stale executable/map hashes")
        if digest(candidate) not in (seal["exe"], seal.get("stripped")):
            raise ValueError("candidate differs from both the linked and stripped ELF")
        return raw_map.decode()
    except (OSError, ValueError, AttributeError) as error:
        raise ValueError(f"no verified ELF linker map: {error}") from error


def read_only_sections(blob: bytes) -> dict[str, list[tuple[bytes, bool]]]:
    """Read ordinary allocated, read-only data sections from an x86-64 ELF object."""
    if len(blob) < 64 or blob[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", blob, 16) != (1, 62):
        raise ValueError("unsupported ELF archive member")
    offset, = struct.unpack_from("<Q", blob, 40)
    stride, count, names_index = struct.unpack_from("<HHH", blob, 58)
    if stride != 64 or not 0 < names_index < count or offset + stride * count > len(blob):
        raise ValueError("invalid ELF archive section table")

    def header(index):
        return struct.unpack_from("<IIQQQQIIQQ", blob, offset + stride * index)

    def contents(start, size):
        if start + size > len(blob):
            raise ValueError("truncated ELF archive section")
        return blob[start:start + size]

    _, names_type, _, _, names_offset, names_size, *_ = header(names_index)
    if names_type != 3:
        raise ValueError("invalid ELF archive section names")
    names = contents(names_offset, names_size)
    sections = {}
    for index in range(1, count):
        name_at, kind, flags, _, start, size, _, _, _, entry_size = header(index)
        if kind != 1 or flags & 7 != 2 or not size:
            continue
        name_end = names.find(b"\0", name_at)
        if name_end < 0:
            raise ValueError("invalid ELF archive section name")
        name = names[name_at:name_end].decode()
        sections.setdefault(name, []).append((contents(start, size), flags & 0x30 == 0x30 and entry_size == 1))
    return sections


ARCHIVE_INPUT = re.compile(r"(.+\.a)\(([^()]+)\)")


class Origins:
    def __init__(self, build: Path, mapping: str, objects):
        self.build = build
        self.spans = [span for span in map_spans(mapping)
                      if span.section == ".rodata" or span.section.startswith(".rodata.")]
        self.starts = [span.address for span in self.spans]
        if not self.spans or any(a.address + a.size > b.address for a, b in zip(self.spans, self.spans[1:])):
            raise ValueError("ELF linker map lacks non-overlapping read-only data inputs")
        self.objects = {p.resolve(): (category, source) for category, source, p, _ in objects}
        self.archives = {}
        self.sections = {}

    def origin(self, span, offset, data, isolated):
        match = ARCHIVE_INPUT.fullmatch(span.obj)
        if match:
            path = (self.build / match[1]).resolve()
            member = match[2]
            key = path, member
            if path not in self.archives:
                self.archives[path] = archive_members(path.read_bytes())
            if key not in self.sections:
                self.sections[key] = [read_only_sections(blob) for blob in self.archives[path].get(member, [])]
            category, source = "library", f"library/{path.name}/{member}"
        else:
            path = (self.build / span.obj).resolve()
            if not span.section.startswith(".rodata.str") or path not in self.objects:
                return None  # leave ordinary project objects and LTO input to the usual audit
            category, source = self.objects[path]
            key = path, ""
            if key not in self.sections:
                self.sections[key] = [read_only_sections(path.read_bytes())]

        # The entire source section fingerprint is part of the allowance key.
        # An archive name alone never provides an allowance.
        matches = {hashlib.sha256(section).hexdigest() for sections in self.sections[key]
                   for section, strings in sections.get(span.section, [])
                   if section[offset:offset + len(data)] == data or
                   # --unique isolates the contributor. GNU ld can still merge
                   # strings within that member, changing their offsets.
                   (isolated and strings and b"\0" not in data and data in section)}
        if len(matches) != 1:
            return ("other", f"mapped ELF input bytes disagree or are ambiguous: {source}", "-") if match else None
        return category, source, f"{span.section}@{matches.pop()}" if match else "-"

    def pieces(self, address: int, data: bytes, section: str, vma: int, size: int):
        at, end = address, address + len(data)
        while at < end:
            index = bisect.bisect_right(self.starts, at) - 1
            span = self.spans[index] if index >= 0 and at < self.spans[index].address + self.spans[index].size else None
            limit = span.address + span.size if span else (self.starts[index + 1] if index + 1 < len(self.starts) else end)
            stop = min(end, limit)
            contents = data[at - address:stop - address]
            isolated = span is not None and (span.section, span.address, span.size) == (section, vma, size)
            key = self.origin(span, at - span.address, contents, isolated) if span else None
            yield at - address, stop - address, key
            at = stop
