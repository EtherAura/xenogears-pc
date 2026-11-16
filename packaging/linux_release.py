#!/usr/bin/env python3
"""The Linux release tarball of xenogears-pc and its .sha256, from an installed stage.

    python packaging/linux_release.py names   --tag v<YYYY>.<MM>.<N>
    python packaging/linux_release.py package --tag <tag> --stage <installed prefix> --out-dir <dir>

The version is the release tag without its v (CalVer vYYYY.MM.N, month 01-12,
N a whole number from 0 without leading zeros), spelled as the tag spells it;
a tag of any other form is refused as given, never corrected. The assets are

  xenogears-pc-<version>-linux-x86_64.tar.gz          the stage, in one folder
  xenogears-pc-<version>-linux-x86_64.tar.gz.sha256   sha256sum format (sha256sum -c)

(the source tarball of the release is xenogears-pc-<version>.tar.gz, made by
the release-source workflow; this is the built port, a different asset).

The stage is the prefix a Linux Release build installs into, by the install
rules of port/cmake/deb.cmake:

    cmake --install <build> --prefix <stage> --strip

For both engines, install the original engine's build and then the enhanced
engine's build (-DXG_ENGINE=enhanced) into the same prefix. The stage must hold
exactly these files, and the folders they are in, nothing else:

  bin/xenogears                                    required, the original engine
  bin/xenogears-enhanced                           optional, the enhanced engine
  share/doc/xenogears-pc/COPYING                   required
  share/doc/xenogears-pc/LICENSE-Jukebox-font.txt  required
  share/doc/xenogears-pc/LICENSE-imgui.txt         required
  share/doc/xenogears-pc/LICENSE-glslang.txt       optional (when linked)
  share/doc/xenogears-pc/LICENSE-dav1d.txt         optional (when linked)

Each executable must be an x86-64 ELF executable (not a shared library or an
object) with its owner's execute bit set; each licence must be UTF-8 text. The
install makes no symbolic links, so the stage may hold none (inside or out of
it), nor any other kind of special file, nor anything that looks like the
game's data or a player's files (disc images, PS-X EXE headers, CD sectors,
memory cards, saves, settings.ini). Nothing of the game is ever packaged: the
port reads the player's own disc.

The tarball holds one folder, xenogears-pc-<version>-linux-x86_64/, and under
it the stage's layout: relative names only, folders and regular files only.
Executables are mode 0755, licences 0644 and folders 0755; every owner is 0
with no owner names, every time is 0, the entries are in a fixed order, and
the gzip header has no name and no time, so the same stage content gives the
same bytes (with the same Python zlib), whatever its files' times or owners.

package refuses, before writing anything: a bad tag, a bad stage, an --out-dir
inside the stage or holding it, an --out-dir that is not a folder, and an
--out-dir already holding either asset's name (a dangling link too): nothing
there is ever overwritten or deleted. It writes the tarball to a temporary
file in --out-dir, checks the stage did not change meanwhile, links it in
under its asset name as a new file, hashes the bytes of that file and writes
the .sha256 the same way. The two are not written as one atomic step. If a
step fails it exits 1, removes the temporary file and every asset and folder
this run made (a sum never names a missing tarball), and says which it removed
or could not remove. The stage's files are only read.

This is the packaging step only. It does not compile the port, fetch its
dependencies, sign anything, make the .deb or the Windows assets, write the
SBOM or upload. The stage checks here (names, file types, a few headers of the
game's data) are not the release's payload audit: passing them does not clear
the assets for upload. Nothing here installs or starts the game.
Standard library only.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import re
import stat
import struct
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

PACKAGE = "xenogears-pc"
ARCH = "x86_64"
TAG = re.compile(r"v(20[0-9][0-9])\.(0[1-9]|1[0-2])\.(0|[1-9][0-9]*)")  # ASCII digits only
NAME_MAX = 255  # bytes in one file name

DOC = "share/doc/xenogears-pc"
EXECUTABLES = {"bin/xenogears": True, "bin/xenogears-enhanced": False}  # name: required
LICENCES = {f"{DOC}/COPYING": True, f"{DOC}/LICENSE-Jukebox-font.txt": True, f"{DOC}/LICENSE-imgui.txt": True,
            f"{DOC}/LICENSE-glslang.txt": False, f"{DOC}/LICENSE-dav1d.txt": False}
FOLDERS = ("bin", "share", "share/doc", DOC)
EXE_MODE, FILE_MODE, DIR_MODE = 0o755, 0o644, 0o755

# The game's data and a player's own files: never in a release.
GAME_EXTENSIONS = {".bin", ".cue", ".iso", ".img", ".ccd", ".sub", ".mds", ".mdf", ".chd", ".pbp", ".ecm",
                   ".toc", ".str", ".xa", ".mcd", ".mcr", ".mc", ".gme", ".vmp", ".psv", ".srm", ".sav"}
PLAYER_FILES = {"settings.ini"}
DISC_EXE = re.compile(r"(slus|slps|scus|sces|sles)_[0-9]{3}\.[0-9]{2}", re.I)
CD_SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
ISO_MARKS = (0x8001, 16 * 2352 + 24 + 1, 16 * 2352 + 16 + 1)  # "CD001": cooked, raw mode 2, raw mode 1

ELF_X86_64, ET_EXEC, ET_DYN, PT_INTERP = 62, 2, 3, 3


class ReleaseError(Exception):
    pass


def release_names(tag: str) -> dict[str, str]:
    """The version, the folder and the asset names of a release tag."""
    if not isinstance(tag, str) or not TAG.fullmatch(tag):
        raise ReleaseError(f"release tag {tag!r} is not vYYYY.MM.N (month 01-12, N a whole number from 0)")
    version = tag[1:]
    folder = f"{PACKAGE}-{version}-linux-{ARCH}"
    tarball = f"{folder}.tar.gz"
    if len(f"{tarball}.sha256".encode()) > NAME_MAX:
        raise ReleaseError(f"release tag {tag!r}: its asset names are longer than {NAME_MAX} bytes")
    return {"version": version, "folder": folder, "tarball": tarball, "sha256": f"{tarball}.sha256"}


def check_elf(data: bytes, rel: str) -> None:
    """An x86-64 ELF executable: ET_EXEC, or ET_DYN with an interpreter (a PIE, not a library)."""
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ReleaseError(f"stage: {rel} is not a 64-bit little-endian ELF file")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_machine != ELF_X86_64:
        raise ReleaseError(f"stage: {rel} is an ELF file for machine {e_machine}, not x86-64")
    (phoff,) = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    if phnum and (phentsize < 56 or phoff + phnum * phentsize > len(data)):
        raise ReleaseError(f"stage: {rel} has program headers past its end")
    interp = any(struct.unpack_from("<I", data, phoff + i * phentsize)[0] == PT_INTERP for i in range(phnum))
    if not (e_type == ET_EXEC or (e_type == ET_DYN and interp)):
        raise ReleaseError(f"stage: {rel} is not an executable (ELF type {e_type}, interpreter {interp})")


def check_licence(data: bytes, rel: str) -> None:
    if not data or b"\x00" in data:
        raise ReleaseError(f"stage: {rel} is not a licence text (empty or binary)")
    try:
        data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ReleaseError(f"stage: {rel} is not UTF-8 text") from error


def check_game_data(data: bytes, rel: str) -> None:
    head = data[:max(ISO_MARKS) + 5]
    if head.startswith(b"PS-X EXE") or head.startswith(CD_SYNC) or any(head[o:o + 5] == b"CD001" for o in ISO_MARKS):
        raise ReleaseError(f"stage: {rel} holds PlayStation executable or CD data; a release holds none")


def read_stage(stage: Path) -> list[dict]:
    """Every file of the stage (name, bytes, SHA-256, kind), checked; sorted by name."""
    if stage.is_symlink() or not stage.is_dir():
        raise ReleaseError(f"--stage {stage}: not a folder")
    files = []
    for here, subdirs, names in os.walk(stage):
        subdirs.sort()
        for name in subdirs + sorted(names):
            path = Path(here) / name
            rel = PurePosixPath(path.relative_to(stage).as_posix()).as_posix()
            lower = name.lower()
            if PurePosixPath(lower).suffix in GAME_EXTENSIONS or lower in PLAYER_FILES or DISC_EXE.fullmatch(name):
                raise ReleaseError(f"stage: {rel} looks like game data or a player's file; a release holds none")
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                raise ReleaseError(f"stage: {rel} is a symbolic link (the install makes none; none is packaged)")
            if stat.S_ISDIR(mode):
                if rel not in FOLDERS:
                    raise ReleaseError(f"stage: folder {rel} is not one the port's install makes")
                continue
            if not stat.S_ISREG(mode):
                raise ReleaseError(f"stage: {rel} is not a regular file")
            if rel not in EXECUTABLES and rel not in LICENCES:
                raise ReleaseError(f"stage: {rel} is not a file the port's install makes (only {', '.join(EXECUTABLES)} "
                                   f"and the licences in {DOC})")
            data = path.read_bytes()
            check_game_data(data, rel)
            if rel in EXECUTABLES:
                check_elf(data, rel)
                if not mode & stat.S_IXUSR:
                    raise ReleaseError(f"stage: {rel} is not executable")
            else:
                check_licence(data, rel)
            files.append({"rel": rel, "data": data, "sha256": hashlib.sha256(data).hexdigest(),
                          "mode": EXE_MODE if rel in EXECUTABLES else FILE_MODE})
    present = {f["rel"] for f in files}
    missing = [n for n, need in {**EXECUTABLES, **LICENCES}.items() if need and n not in present]
    if missing:
        raise ReleaseError(f"stage: no {', '.join(missing)} (not a cmake --install of the port's Linux build)")
    return sorted(files, key=lambda f: f["rel"])


def unchanged(stage: Path, files: list[dict]) -> None:
    again = read_stage(stage)
    if [(f["rel"], f["sha256"], f["mode"]) for f in again] != [(f["rel"], f["sha256"], f["mode"]) for f in files]:
        raise ReleaseError(f"--stage {stage} changed while it was packaged")


def entry(name: str, kind: bytes, mode: int, size: int = 0) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.type, info.mode, info.size = kind, mode, size
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    return info


def write_tarball(out, folder: str, files: list[dict]) -> None:
    """The tarball's bytes into out: the folder, the stage's folders, then its files, by name."""
    top = PurePosixPath(folder)
    used = {p.as_posix() for f in files for p in PurePosixPath(f["rel"]).parents if p != PurePosixPath(".")}
    with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tar:
            for rel in ["."] + [d for d in FOLDERS if d in used]:
                tar.addfile(entry((top / rel).as_posix(), tarfile.DIRTYPE, DIR_MODE))
            for f in files:
                tar.addfile(entry((top / f["rel"]).as_posix(), tarfile.REGTYPE, f["mode"], len(f["data"])),
                            io.BytesIO(f["data"]))


def publish(tmp: Path, dest: Path, written: list[Path]) -> None:
    """tmp's file under dest as a new name: never one already there, even one that appeared meanwhile."""
    try:
        os.link(tmp, dest)
    except FileExistsError as error:
        raise ReleaseError(f"{dest} appeared while packaging; it is not overwritten") from error
    except OSError:  # a file system without hard links: copy into a new file
        data = tmp.read_bytes()
        try:
            f = dest.open("xb")
        except FileExistsError as error:
            raise ReleaseError(f"{dest} appeared while packaging; it is not overwritten") from error
        written.append(dest)
        with f:
            f.write(data)
        os.chmod(dest, FILE_MODE)
        return
    written.append(dest)


def new_temp(out_dir: Path, name: str, temps: list[Path]) -> tuple[int, Path]:
    fd, tmp = tempfile.mkstemp(dir=out_dir, prefix=f".{name}.", suffix=".part")
    temps.append(Path(tmp))
    os.chmod(tmp, FILE_MODE)
    return fd, Path(tmp)


def undo(written: list[Path], temps: list[Path], made: list[Path], out_dir: Path) -> str:
    """Remove what this run wrote and the folders it made; say what it did and what is left."""
    removed, gone, left = [], [], []
    for path in temps:
        try:
            path.unlink(missing_ok=True)
        except OSError as error:
            left.append(f"{path} ({error})")
    for path in reversed(written):
        try:
            path.unlink()
            removed.append(path.name)
        except OSError as error:
            left.append(f"{path} ({error})")
    for folder in made:  # deepest first
        try:
            folder.rmdir()
            gone.append(str(folder))
        except OSError as error:
            left.append(f"the folder {folder} ({error})")
    notes = [f"wrote and then removed {', '.join(reversed(removed))}"] if removed else []
    notes += [f"made and then removed the folder {', '.join(gone)}"] if gone else []
    notes += [f"left behind {', '.join(left)}"] if left else []
    return "; ".join(notes) or f"no asset written in {out_dir}"


def package(tag: str, stage: Path, out_dir: Path) -> dict[str, str]:
    """The tarball of one stage and its .sha256, written into out_dir as new files."""
    names = release_names(tag)
    files = read_stage(stage)
    stage_real, out_real = stage.resolve(), out_dir.resolve()
    if out_real.is_relative_to(stage_real):
        raise ReleaseError(f"--out-dir {out_dir} is inside the stage; nothing written")
    if stage_real.is_relative_to(out_real):
        raise ReleaseError(f"--stage {stage} is inside --out-dir {out_dir}; nothing written")
    if os.path.lexists(out_dir) and not out_dir.is_dir():
        raise ReleaseError(f"--out-dir {out_dir}: not a folder; nothing written")
    final = [out_dir / names["tarball"], out_dir / names["sha256"]]
    taken = [p.name for p in final if os.path.lexists(p)]
    if taken:
        raise ReleaseError(f"--out-dir {out_dir} already holds {', '.join(taken)}; nothing is overwritten "
                           "(move them, or choose another folder); nothing written")
    made, here = [], out_dir.absolute()
    while not os.path.lexists(here):
        made.append(here)
        here = here.parent
    written: list[Path] = []
    temps: list[Path] = []
    try:
        out_dir.mkdir(parents=True, exist_ok=True)
        fd, tmp = new_temp(out_dir, names["tarball"], temps)
        with os.fdopen(fd, "wb") as f:
            write_tarball(f, names["folder"], files)
            f.flush()
            os.fsync(f.fileno())
        unchanged(stage, files)
        publish(tmp, final[0], written)
        tmp.unlink()
        digest = hashlib.sha256(final[0].read_bytes()).hexdigest()
        line = f"{digest}  {names['tarball']}\n".encode()
        fd, tmp = new_temp(out_dir, names["sha256"], temps)
        with os.fdopen(fd, "wb") as f:
            f.write(line)
            f.flush()
            os.fsync(f.fileno())
        publish(tmp, final[1], written)
        tmp.unlink()
        if final[1].read_bytes() != line or hashlib.sha256(final[0].read_bytes()).hexdigest() != digest:
            raise ReleaseError(f"{final[0].name}: its bytes or its sum changed after they were written")
    except (ReleaseError, OSError) as error:
        raise ReleaseError(f"{error}; {undo(written, temps, made, out_dir)}") from error
    return {"tarball": str(final[0]), "sha256": str(final[1]), "digest": digest}


PACKAGE_SCOPE = ("the packaging step only: the Linux tarball and its .sha256 from a final installed stage; not the "
                 "port's compilation, dependency fetching, the payload audit (the stage checks here are not it and "
                 "do not clear the assets for upload), signing, the .deb or Windows assets, the SBOM, an upload, or "
                 "an install and start")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("names", help="the version, folder and asset names of a tag")
    p.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    p = sub.add_parser("package", help="the tarball and its .sha256 (" + PACKAGE_SCOPE + ")",
                       description="package: " + PACKAGE_SCOPE + ". The stage is what cmake --install --prefix "
                       "<stage> writes for the port's Linux Release build: bin/xenogears (required), "
                       "bin/xenogears-enhanced (optional) and the licences in " + DOC + ". The two assets are "
                       "written into --out-dir as new files; it refuses a folder already holding either.")
    p.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N (N from 0)")
    p.add_argument("--stage", type=Path, required=True, help="the installed prefix, final (only read)")
    p.add_argument("--out-dir", type=Path, required=True, help="where the two assets go (made if missing)")
    args = ap.parse_args(argv)
    try:
        if args.command == "names":
            for key, value in release_names(args.tag).items():
                print(f"{key}={value}")
        else:
            for key, value in package(args.tag, args.stage, args.out_dir).items():
                print(f"{key}={value}")
            print(f"scope={PACKAGE_SCOPE}")
    except (ReleaseError, OSError) as error:
        print(f"linux_release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
