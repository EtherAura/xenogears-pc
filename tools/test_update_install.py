#!/usr/bin/env python3
"""The updater's archive reader on archives made as the releases make them (xg-4oq.14.4).

    python tools/test_update_install.py [--exe build/pc/gates/updateinstalltest] [--real DIR]

port/src/update/update_archive.c reads the release's Windows .zip and Linux
.tar.gz with puff (zlib's reference inflater). updateinstalltest's own checks
make archives whose deflate data is in stored blocks; these are made here by
Python as packaging/windows_release.py and linux_release.py make the real ones
(zipfile, deflate level 9; tarfile in pax format inside gzip level 9, no name,
no time), so the compressed (fixed and dynamic Huffman) data is read too:

1. a zip and a tar.gz of the same files (text, random bytes, a large
   compressible file, an empty one, one under a name longer than a ustar header
   holds, executables in the tar), each listed by "updateinstalltest --unpack"
   exactly as Python reads it: path, size, CRC-32, execute bit, the top folder;
2. refused: a zip entry with "..", a tar holding a symbolic link, an archive
   with two top folders, a zip whose data was changed after it was written;
3. with --real DIR: every .zip and .tar.gz in DIR (a release's own assets),
   listed as Python lists them.

Exit status 1 on any failure.
"""
from __future__ import annotations

import argparse
import gzip
import io
import os
import random
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAILURES: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        FAILURES.append(what)
    return ok


def files() -> dict[str, tuple[bytes, bool]]:
    rnd = random.Random(1427)
    return {
        "bin/xenogears": (bytes(rnd.getrandbits(8) for _ in range(70000)) + b"ELF" * 9000, True),
        "bin/xenogears-enhanced": (b"the enhanced engine " * 40000, True),
        "share/xenogears-pc/release.txt": (b"version=2026.12.3\nkind=tarball\n", False),
        "share/doc/xenogears-pc/COPYING": (b"GNU GENERAL PUBLIC LICENSE\n" * 1200, False),
        "share/doc/xenogears-pc/" + "a-long-name-" * 12 + ".txt": (b"pax", False),
        "share/doc/xenogears-pc/empty.txt": (b"", False),
    }


def python_listing(path: Path) -> list[str]:
    rows: list[str] = []
    tops = set()
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            for i in z.infolist():
                top, _, rel = i.filename.partition("/")
                tops.add(top)
                if i.is_dir() or not rel:
                    continue
                b = z.read(i)
                rows.append(f"{rel} {len(b)} {zlib.crc32(b):08x} 0")
    else:
        with tarfile.open(path) as t:
            for m in t.getmembers():
                top, _, rel = m.name.partition("/")
                tops.add(top)
                if not m.isfile():
                    continue
                b = t.extractfile(m).read()
                rows.append(f"{rel} {len(b)} {zlib.crc32(b):08x} {1 if m.mode & 0o100 else 0}")
    rows.append(f"top {sorted(tops)[0]}")
    return sorted(rows)


def unpack(exe: Path, path: Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(exe), "--unpack", str(path)], capture_output=True, text=True, timeout=120)


def same_listing(exe: Path, path: Path) -> None:
    r = unpack(exe, path)
    mine = sorted(line for line in r.stdout.splitlines() if line)
    check(r.returncode == 0 and mine == python_listing(path),
          f"{path.name}: read as Python reads it ({len(mine) - 1} files)")


def make_zip(path: Path, top: str, content: dict[str, tuple[bytes, bool]], names: list[str] | None = None) -> None:
    with zipfile.ZipFile(path, "w") as z:
        for i, (rel, (data, _)) in enumerate(content.items()):
            info = zipfile.ZipInfo(f"{top}/{rel}" if names is None else names[i], date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if rel.endswith("empty.txt") else zipfile.ZIP_DEFLATED
            z.writestr(info, data, compresslevel=9)


def make_targz(path: Path, top: str, content: dict[str, tuple[bytes, bool]], link: bool = False) -> None:
    out = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tar:
            folder = tarfile.TarInfo(top)
            folder.type, folder.mode = tarfile.DIRTYPE, 0o755
            tar.addfile(folder)
            for rel, (data, executable) in content.items():
                info = tarfile.TarInfo(f"{top}/{rel}")
                info.size, info.mode = len(data), 0o755 if executable else 0o644
                tar.addfile(info, io.BytesIO(data))
            if link:
                info = tarfile.TarInfo(f"{top}/bin/link")
                info.type, info.linkname = tarfile.SYMTYPE, "/etc/passwd"
                tar.addfile(info)
    path.write_bytes(out.getvalue())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/pc/gates/updateinstalltest"))
    ap.add_argument("--real", help="a folder of release assets (.zip, .tar.gz) to read as well")
    args = ap.parse_args()
    exe = Path(args.exe)
    if not exe.is_file():
        print(f"test_update_install: no {exe} (python tools/port_tests.py builds it)")
        return 1
    content = files()
    with tempfile.TemporaryDirectory(prefix="xg update install ") as tmp:
        work = Path(tmp)
        z, t = work / "xenogears-pc-2026.12.3-windows-x64.zip", work / "xenogears-pc-2026.12.3-linux-x86_64.tar.gz"
        make_zip(z, "xenogears-pc-2026.12.3-windows-x64", content)
        make_targz(t, "xenogears-pc-2026.12.3-linux-x86_64", content)
        same_listing(exe, z)
        same_listing(exe, t)
        check(any(len(line.split()[0]) > 100 for line in python_listing(t)), "the tar holds a name only pax can")

        bad = work / "dotdot.zip"
        make_zip(bad, "x", {"a.txt": (b"a", False), "b.txt": (b"b", False)}, names=["x/a.txt", "x/../b.txt"])
        r = unpack(exe, bad)
        check(r.returncode == 1 and ". or .." in r.stdout, "refused: a zip entry with ..")
        make_targz(bad := work / "link.tar.gz", "x", {"a.txt": (b"a", False)}, link=True)
        r = unpack(exe, bad)
        check(r.returncode == 1 and "not a plain file" in r.stdout, "refused: a tar with a symbolic link")
        make_zip(bad := work / "tops.zip", "x", {"a.txt": (b"a", False), "b.txt": (b"b", False)},
                 names=["x/a.txt", "y/b.txt"])
        r = unpack(exe, bad)
        check(r.returncode == 1 and "more than one top folder" in r.stdout, "refused: two top folders")
        data = bytearray(z.read_bytes())
        data[len(data) // 3] ^= 0x10  # inside the compressed data
        (bad := work / "changed.zip").write_bytes(bytes(data))
        r = unpack(exe, bad)
        check(r.returncode == 1 and ("CRC-32" in r.stdout or "damaged" in r.stdout), "refused: a changed zip")

    if args.real:
        for path in sorted(Path(args.real).iterdir()):
            if path.name.endswith((".zip", ".tar.gz")):
                same_listing(exe, path)

    if FAILURES:
        print(f"test_update_install: {len(FAILURES)} check(s) failed")
        return 1
    print("test_update_install: all checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
