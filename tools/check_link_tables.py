#!/usr/bin/env python3
"""Check that link tables (tools/pc_link.py --write-facts files and the like) carry no retail bytes.

    python tools/check_link_tables.py --ver us --tables FILE [FILE ...]

A table meant for a public checkout may hold addresses, sizes, offsets and
hashes, never game bytes. This scans every named file as raw bytes with
tools/payloadcheck.py's rule, through its own functions: every run of at least
16 bytes of a retail module image of the region (config/<ver>/disc.yaml's
images, extracted under disc/<ver>/) that also appears in the file, counted
only where a 16-byte window carries information (at least 4 distinct byte
values and 4 non-zero bytes), overlapping or adjacent windows merged, zero
bytes at a run's ends taken as padding. Every such run fails: there are no
allowances here. It is a raw-byte scan, not a proof against encoded bytes
(hex, base64, compressed) and not a schema check.

The reference must be the region's whole module-image set, each image matching
its sha1 in disc.yaml: a missing, unreadable or other region's image fails, as
does a missing, unreadable or empty table. The report names each file, the
offset and length of each run, and the overall status; never the bytes.
Exit status 0 when every table is clean, 1 when a run is found, 2 when an
input or the reference is missing or wrong.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
import payloadcheck  # noqa: E402

ROOT = payloadcheck.ROOT
MIN = 16  # payloadcheck's --min default; not an option here


class ReferenceSetError(Exception):
    pass


def reference_images(ver: str, root: Path = ROOT) -> dict[str, bytes]:
    """module -> its extracted image, for every module of root/config/<ver>/disc.yaml, each
    checked against the manifest's sha1 (the names as tools/extract.py and payloadcheck use them)."""
    manifest = root / "config" / ver / "disc.yaml"
    try:
        doc = yaml.safe_load(manifest.read_text())
    except (OSError, yaml.YAMLError) as e:
        raise ReferenceSetError(f"cannot read the {ver.upper()} manifest {manifest}: {e}")
    files = doc.get("files") if isinstance(doc, dict) else None
    if not files:
        raise ReferenceSetError(f"{manifest} lists no module images")
    out: dict[str, bytes] = {}
    problems = []
    for entry in files:
        name = entry.get("name") if isinstance(entry, dict) else None
        if not name:
            raise ReferenceSetError(f"{manifest}: an entry without a name")
        path = root / "disc" / ver / (f"{name}.exe" if "iso" in entry else f"{name}.bin")
        want = str(entry.get("sha1") or "")
        if not want:
            problems.append(f"{name}: no sha1 in the manifest")
            continue
        try:
            data = path.read_bytes()
        except OSError as e:
            problems.append(f"{name}: {e.strerror or e}: {path}")
            continue
        if hashlib.sha1(data).hexdigest() != want.lower():
            problems.append(f"{name}: {path} is not the {ver.upper()} image (sha1 differs)")
            continue
        out[name] = data
    if problems:
        raise ReferenceSetError(f"the {ver.upper()} module images are incomplete (run tools/extract.py): "
                             + "; ".join(problems))
    return out


def scan(data: bytes, wanted: set[bytes]) -> list[tuple[int, int]]:
    """[start, end) runs of `data` made of retail windows, as payloadcheck counts them."""
    n = MIN
    found = []
    for start, end in payloadcheck.runs(payloadcheck.hits(data, wanted, n), n):
        # Zero bytes at either end are padding; what is left must still be a whole window.
        while start < end and data[start] == 0:
            start += 1
        while end > start and data[end - 1] == 0:
            end -= 1
        if end - start >= n:
            found.append((start, end))
    return found


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", required=True, choices=["us", "jp"], help="the region whose images are the reference")
    ap.add_argument("--tables", required=True, nargs="+", type=Path, metavar="FILE", help="the files to scan")
    ap.add_argument("--reference-root", type=Path, default=ROOT,
                    help="where config/<ver>/disc.yaml and disc/<ver>/ are (default: this checkout)")
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    try:
        images = reference_images(a.ver, a.reference_root)
    except ReferenceSetError as e:
        print(f"link tables FAIL: {e}")
        return 2
    wanted = payloadcheck.windows(images, MIN)
    print(f"retail module data in link tables ({a.ver.upper()}): {len(images)} module images, "
          f"runs of >= {MIN} bytes")
    unreadable = dirty = runs_total = bytes_total = 0
    for path in a.tables:
        try:
            data = path.read_bytes()
        except OSError as e:
            print(f"  unreadable {path}: {e.strerror or e}")
            unreadable += 1
            continue
        if not data:
            print(f"  empty      {path}: nothing to check")
            unreadable += 1
            continue
        found = scan(data, wanted)
        if not found:
            print(f"  ok         {path}: {len(data)} bytes, no retail run")
            continue
        dirty += 1
        runs_total += len(found)
        bytes_total += sum(e - s for s, e in found)
        print(f"  FAIL       {path}: {len(found)} retail runs, {sum(e - s for s, e in found)} bytes")
        for s, e in found:
            print(f"             {path}+0x{s:X} {e - s} bytes")
    bad = dirty or unreadable
    print(f"link tables {'FAIL' if bad else 'ok'}: {len(a.tables)} files, {dirty} with retail runs "
          f"({runs_total} runs/{bytes_total} bytes), {unreadable} unreadable or empty")
    return 2 if unreadable else 1 if dirty else 0


if __name__ == "__main__":
    sys.exit(main())
