#!/usr/bin/env python3
"""Check that every arm64-v8a shared library in an APK has 16 KB LOAD segments.

    python tools/check_android_alignment.py --apk PATH
    python tools/check_android_alignment.py --apk PATH --readelf /path/to/llvm-readelf
    python tools/check_android_alignment.py --apk PATH --require-readelf

Android's 16 KB page devices map each PT_LOAD segment at its p_align; a
library linked for 4 KB pages (-z max-page-size=4096) fails to load there.
For every lib/arm64-v8a/*.so in the APK (SDL3, libc++_shared and any other
prebuilt included) this lists each PT_LOAD's alignment, read from the
program headers by a validating ELF64 reader here and, when llvm-readelf is
found (or given), also from `llvm-readelf -lW`; the two must agree. It
fails when any LOAD alignment is below 0x4000, and also (never a silent
pass) on a missing or unreadable APK, an APK without arm64-v8a libraries, a
member that is not an AArch64 ELF64 shared object, a malformed program
header table, a library without a LOAD, or an llvm-readelf that fails or
disagrees. The APK is only read: members are copied to a temporary folder
for llvm-readelf, nothing is rewritten.

Exit status 0 when every library passes, 1 otherwise.
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

MIN_ALIGN = 0x4000
LIB_DIR = "lib/arm64-v8a/"
READELF_TIMEOUT = 60

ELFCLASS64 = 2
ELFDATA2LSB = 1
ET_DYN = 3
EM_AARCH64 = 183
PT_LOAD = 1
PN_XNUM = 0xFFFF
EHDR = struct.Struct("<16sHHIQQQIHHHHHH")
PHDR = struct.Struct("<IIQQQQQQ")


class ElfError(Exception):
    pass


def load_aligns(data: bytes) -> list[int]:
    """The p_align of each PT_LOAD of an AArch64 ELF64 shared object, in header order."""
    if len(data) < EHDR.size:
        raise ElfError(f"{len(data)} bytes, shorter than an ELF64 header")
    (ident, e_type, e_machine, e_version, _entry, e_phoff, _shoff, _flags,
     e_ehsize, e_phentsize, e_phnum, _shentsize, _shnum, _shstrndx) = EHDR.unpack_from(data)
    if ident[:4] != b"\x7fELF":
        raise ElfError("not an ELF file")
    if ident[4] != ELFCLASS64 or ident[5] != ELFDATA2LSB or ident[6] != 1 or e_version != 1:
        raise ElfError(f"not a little-endian ELF64 version 1 (class {ident[4]}, data {ident[5]})")
    if e_machine != EM_AARCH64:
        raise ElfError(f"machine {e_machine}, not AArch64 ({EM_AARCH64})")
    if e_type != ET_DYN:
        raise ElfError(f"type {e_type}, not a shared object ({ET_DYN})")
    if e_ehsize != EHDR.size or e_phentsize != PHDR.size:
        raise ElfError(f"header sizes {e_ehsize}/{e_phentsize}, not {EHDR.size}/{PHDR.size}")
    if e_phnum == 0:
        raise ElfError("no program headers")
    if e_phnum == PN_XNUM:
        raise ElfError("extended program header count (PN_XNUM) is not supported")
    if e_phoff < EHDR.size or e_phoff + e_phnum * PHDR.size > len(data):
        raise ElfError(f"program headers at {e_phoff:#x} x {e_phnum} run past the {len(data):#x}-byte file")
    aligns = []
    for i in range(e_phnum):
        p_type, _f, p_offset, p_vaddr, _pa, p_filesz, p_memsz, p_align = PHDR.unpack_from(data, e_phoff + i * PHDR.size)
        if p_type != PT_LOAD:
            continue
        if p_align & (p_align - 1):
            raise ElfError(f"LOAD {len(aligns)}: alignment {p_align:#x} is not a power of two")
        if p_align > 1 and p_offset % p_align != p_vaddr % p_align:
            raise ElfError(f"LOAD {len(aligns)}: offset {p_offset:#x} and address {p_vaddr:#x} differ modulo {p_align:#x}")
        if p_offset + p_filesz > len(data) or p_filesz > p_memsz:
            raise ElfError(f"LOAD {len(aligns)}: file range {p_offset:#x}+{p_filesz:#x} (memory {p_memsz:#x}) is invalid")
        aligns.append(p_align)
    if not aligns:
        raise ElfError("no LOAD segment")
    return aligns


def readelf_aligns(readelf: str, path: Path) -> list[int]:
    """The Align column of each LOAD line of `llvm-readelf -lW`."""
    try:
        result = subprocess.run([readelf, "-lW", str(path)], capture_output=True, text=True, timeout=READELF_TIMEOUT)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise ElfError(f"{readelf} failed: {e}") from e
    if result.returncode != 0:
        raise ElfError(f"{readelf} -lW exited {result.returncode}: {result.stderr.strip()[-500:]}")
    aligns = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "LOAD":
            try:
                aligns.append(int(fields[-1], 16))
            except ValueError as e:
                raise ElfError(f"unreadable llvm-readelf LOAD line: {line.strip()}") from e
    if not aligns:
        raise ElfError("llvm-readelf -lW lists no LOAD segment")
    return aligns


def find_readelf() -> str | None:
    found = shutil.which("llvm-readelf")
    if found:
        return found
    ndk = Path(os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT") or "")
    candidate = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    return str(candidate) if str(ndk) not in ("", ".") and candidate.is_file() else None


def check_apk(apk: Path, readelf: str | None) -> list[str]:
    """Print a line per library; return the failures."""
    failures: list[str] = []
    try:
        with zipfile.ZipFile(apk) as z:
            infos = z.infolist()
            libs = sorted((i for i in infos if i.filename.startswith(LIB_DIR) and i.filename.endswith(".so")),
                          key=lambda i: i.filename)
            print(f"{apk}: {len(infos)} entries, {len(libs)} {LIB_DIR}*.so")
            if not libs:
                failures.append(f"{apk}: no {LIB_DIR}*.so")
                print(f"FAIL  {failures[-1]}")
                return failures
            names = [i.filename for i in libs]
            for name in sorted(set(n for n in names if names.count(n) > 1)):
                failures.append(f"{name}: appears more than once in the APK")
                print(f"FAIL  {failures[-1]}")
            with tempfile.TemporaryDirectory(prefix="xg-apk-align-") as tmp:
                for n, info in enumerate(libs):
                    name = info.filename
                    try:
                        data = z.read(info)
                        aligns = load_aligns(data)
                        if readelf:
                            copy = Path(tmp) / f"{n}.so"
                            copy.write_bytes(data)
                            seen = readelf_aligns(readelf, copy)
                            if seen != aligns:
                                raise ElfError("llvm-readelf LOAD alignments "
                                               f"{', '.join(f'{a:#x}' for a in seen)} disagree with the program headers")
                    except (ElfError, zipfile.BadZipFile, OSError, RuntimeError, ValueError) as e:
                        failures.append(f"{name}: {e}")
                        print(f"FAIL  {failures[-1]}")
                        continue
                    low = [a for a in aligns if a < MIN_ALIGN]
                    listed = ", ".join(f"{a:#x}" for a in aligns)
                    if low:
                        failures.append(f"{name}: LOAD alignment {', '.join(f'{a:#x}' for a in low)} below {MIN_ALIGN:#x} "
                                        f"(LOADs: {listed})")
                        print(f"FAIL  {failures[-1]}")
                    else:
                        print(f"ok    {name}: {len(aligns)} LOAD, align {listed}")
    except (zipfile.BadZipFile, OSError) as e:
        failures.append(f"{apk}: not a readable APK: {e}")
        print(f"FAIL  {failures[-1]}")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--apk", required=True, type=Path, help="the APK to inspect (only read)")
    ap.add_argument("--readelf", help="llvm-readelf to cross-check with (default: PATH, then the NDK's)")
    ap.add_argument("--require-readelf", action="store_true", help="fail when no llvm-readelf is found")
    args = ap.parse_args()

    readelf = args.readelf or find_readelf()
    if readelf:
        print(f"cross-checking with {readelf} -lW")
    elif args.require_readelf:
        print("FAIL  no llvm-readelf found (--readelf, PATH, ANDROID_NDK_HOME)")
        return 1
    else:
        print("note  no llvm-readelf found: program headers read by this script only")
    failures = check_apk(args.apk, readelf)
    if failures:
        print(f"\n{len(failures)} failure(s), minimum LOAD alignment {MIN_ALIGN:#x}:")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"\nall {LIB_DIR}*.so LOAD segments aligned to at least {MIN_ALIGN:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
