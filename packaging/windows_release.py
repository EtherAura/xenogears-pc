#!/usr/bin/env python3
"""The Windows release assets of xenogears-pc: an NSIS setup script, a portable .zip, their .sha256.

    python packaging/windows_release.py names  --tag v<YYYY>.<MM>.<N>
    python packaging/windows_release.py render --tag <tag> --stage <installed dir> --out <dir>/xenogears-pc.nsi
    python packaging/windows_release.py setup  --tag <tag> --script <dir>/xenogears-pc.nsi [--makensis <exe>]
    python packaging/windows_release.py zip    --tag <tag> --stage <installed dir> --out-dir <dir>
    python packaging/windows_release.py sha256 --tag <tag> <asset> [<asset> ...]
    python packaging/windows_release.py package --tag <tag> --stage <installed dir> --out-dir <dir> [--makensis <exe>]

The version is the release tag without its v (CalVer vYYYY.MM.N, N from 0),
spelled as the tag spells it; the assets are

  xenogears-pc-<version>-windows-x64-setup.exe   makensis on the rendered script
  <setup>.build.json                             the makensis version that made it
  xenogears-pc-<version>-windows-x64.zip         the same files, in one folder
  <asset>.sha256                                 sha256sum format

Each asset also holds release.txt beside xenogears.exe, what the in-app
updater reads (port/src/system/release.h): "version=<version>" and
"kind=zip" in the .zip; setup.exe writes its own at install, "kind=setup",
and its uninstaller deletes it.

The stage is a folder `cmake --install` filled (the executables, the runtime
DLLs, the licences, a README). It must hold xenogears.exe, SDL3.dll and
COPYING.txt; it is refused if it holds release.txt (each asset's own), a symbolic link, an empty folder, a name
Windows or NSIS cannot take as it is, two names that differ only in case, or
anything that looks like the game's data or a player's files (disc images, PS-X
EXE headers, CD sectors, memory cards, settings.ini). Nothing of the game is
ever packaged: the port reads the player's own disc.

Each step is its own command so that a release can sign the executables in the
stage before render and zip, and sign setup.exe before sha256. render writes the
script only; setup builds setup.exe from it, in the script's folder (makensis's
default). setup first asks makensis -VERSION and refuses anything older than
3.10, or a version it cannot read: setup.exe runs elevated, and NSIS up to 3.09
leaves the uninstaller's temporary copy open to other users (CVE-2023-37378;
NVD's CPE range includes 3.09). The same makensis that answered compiles the
script, and the version it gave is written beside setup.exe. The portable .zip
needs no makensis. Exit status 1, with nothing written, on any bad input.

package runs those four steps in one go, for a stage that is final (already
signed, if it is to be): it renders the script in a temporary folder inside
--out-dir, builds setup.exe there through setup's own makensis checks, zips the
same stage, writes both .sha256 and only then copies setup.exe, its build
record, the .zip and the two .sha256 into --out-dir, each as a new file. It
refuses, before running anything, if --out-dir already holds any of those five
names: nothing there is ever overwritten or deleted. If a step fails it exits 1,
removes the files it had copied (a later sum never names a missing asset) and
says which ones it wrote and removed, or could not remove. The temporary folder
and its script are always removed. No signing step fits inside package; a
signed setup.exe is made with the separate steps.

package is the packaging phase only. It does not compile the port, fetch its
dependencies, sign anything, make the Linux assets, write the SBOM or upload;
the stage checks here (names, required files, a few headers of the game's data)
are not the release's payload audit, and passing them does not make the assets
safe to upload. Nothing here installs setup.exe or starts the game on Windows.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import json
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

PACKAGE = "xenogears-pc"
TAG = re.compile(r"v(20\d\d)\.(0[1-9]|1[0-2])\.(0|[1-9]\d*)")
NSIS_PART_MAX = 0xFFFF  # each part of VIProductVersion is a 16-bit number
TEMPLATE = Path(__file__).resolve().parent / "xenogears-pc-installer.nsi.in"
REQUIRED = ("xenogears.exe", "SDL3.dll", "COPYING.txt")
RELEASE_FILE = "release.txt"  # beside xenogears.exe: what the in-app updater reads (port/src/system/release.h)


def release_text(version: str, kind: str) -> bytes:
    """An asset's release.txt: the .zip's (kind zip); setup.exe writes the same with kind setup."""
    return f"version={version}\nkind={kind}\n".encode()
LICENSE_FILE = "COPYING.txt"
SHORTCUTS = (("xenogears.exe", "Xenogears"), ("xenogears-enhanced.exe", "Xenogears (Enhanced engine)"))
PLACEHOLDERS = frozenset({"VERSION", "NSIS_VERSION", "SETUP_NAME", "LICENSE_FILE", "INSTALL_FILES",
                          "UNINSTALL_FILES", "UNINSTALL_DIRS", "SHORTCUTS", "UNSHORTCUTS", "ESTIMATED_SIZE_KB"})
PLACEHOLDER = re.compile(r"@([A-Za-z0-9_]+)@")

SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*")
RESERVED = re.compile(r"(con|prn|aux|nul|com[0-9]|lpt[0-9])(\..*)?", re.I)
NSIS_UNSAFE = re.compile(r'["$`\r\n\x00]')
# The game's data and a player's own files: never in a release.
GAME_EXTENSIONS = {".bin", ".cue", ".iso", ".img", ".ccd", ".sub", ".mds", ".mdf", ".chd", ".pbp", ".ecm",
                   ".toc", ".str", ".xa", ".mcd", ".mcr", ".mc", ".gme", ".vmp", ".psv", ".srm", ".sav"}
PLAYER_FILES = {"settings.ini"}
DISC_EXE = re.compile(r"(slus|slps|scus|sces|sles)_\d{3}\.\d{2}", re.I)
CD_SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
ISO_MARKS = (0x8001, 16 * 2352 + 24 + 1, 16 * 2352 + 16 + 1)  # "CD001": cooked, raw mode 2, raw mode 1

MAKENSIS_MIN = (3, 10)  # CVE-2023-37378: fixed in 3.09, but NVD's CPE range includes 3.09
# makensis -VERSION prints v<major>.<minor>[.<patch>]; a distribution may add a -<revision>.
MAKENSIS_VERSION = re.compile(r"v(\d{1,4})\.(\d{1,4})(?:\.(\d{1,4}))?(?:-\d{1,4})?")
PROBE_SECONDS, COMPILE_SECONDS = 30, 1800


class ReleaseError(Exception):
    pass


def release_names(tag: str) -> dict[str, str]:
    """The version, the NSIS version and the asset names of a release tag."""
    m = TAG.fullmatch(tag)
    if not m:
        raise ReleaseError(f"release tag {tag!r} is not vYYYY.MM.N (month 01-12, N a whole number from 0)")
    if int(m[3]) > NSIS_PART_MAX:
        raise ReleaseError(f"release tag {tag!r}: N is above {NSIS_PART_MAX}, too large for NSIS's 16-bit version parts")
    version = tag[1:]
    base = f"{PACKAGE}-{version}-windows-x64"
    return {"version": version, "nsis_version": f"{int(m[1])}.{int(m[2])}.{int(m[3])}.0",
            "setup": f"{base}-setup.exe", "zip": f"{base}.zip", "folder": base}


def check_name(rel: PurePosixPath) -> None:
    for part in rel.parts:
        if not SAFE_NAME.fullmatch(part) or part.endswith(".") or len(part) > 100:
            raise ReleaseError(f"stage: {rel} has a name not taken as it is (letters, digits and ._+- only)")
        if RESERVED.fullmatch(part):
            raise ReleaseError(f"stage: {rel} uses a name Windows reserves")


def check_contents(path: Path, rel: PurePosixPath) -> None:
    name = rel.name.lower()
    if rel.suffix.lower() in GAME_EXTENSIONS or name in PLAYER_FILES or DISC_EXE.fullmatch(rel.name):
        raise ReleaseError(f"stage: {rel} looks like game data or a player's file; a release holds none")
    with path.open("rb") as f:
        head = f.read(max(ISO_MARKS) + 5)
    if head.startswith(b"PS-X EXE") or head.startswith(CD_SYNC) or any(head[o:o + 5] == b"CD001" for o in ISO_MARKS):
        raise ReleaseError(f"stage: {rel} holds PlayStation executable or CD data; a release holds none")


def read_stage(stage: Path) -> list[dict]:
    """Every file of the stage (path, size, SHA-256), checked; sorted by path."""
    if stage.is_symlink() or not stage.is_dir():
        raise ReleaseError(f"--stage {stage}: not a folder")
    files, dirs, seen = [], [], {}
    for here, subdirs, names in os.walk(stage):
        subdirs.sort()
        here = Path(here)
        for name in subdirs + names:
            path = here / name
            rel = PurePosixPath(path.relative_to(stage).as_posix())
            check_name(rel)
            key = rel.as_posix().casefold()
            if key in seen:
                raise ReleaseError(f"stage: {rel} and {seen[key]} differ only in case")
            seen[key] = rel
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                raise ReleaseError(f"stage: {rel} is a symbolic link")
            if stat.S_ISDIR(mode):
                dirs.append(path)
                continue
            if not stat.S_ISREG(mode):
                raise ReleaseError(f"stage: {rel} is not a regular file")
            check_contents(path, rel)
            data = path.read_bytes()
            files.append({"rel": rel, "path": path, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    for d in dirs:
        if not any(f["path"].is_relative_to(d) for f in files):
            raise ReleaseError(f"stage: folder {d.relative_to(stage).as_posix()} holds no files")
    present = {f["rel"].as_posix() for f in files}
    if "uninstall.exe" in {p.casefold() for p in present}:
        raise ReleaseError("stage: uninstall.exe is the setup's own uninstaller's name")
    if RELEASE_FILE in {p.casefold() for p in present}:
        raise ReleaseError(f"stage: {RELEASE_FILE} is each asset's own (the .zip's and setup.exe's), never the stage's")
    missing = [n for n in REQUIRED if n not in present]
    if missing:
        raise ReleaseError(f"stage: no {', '.join(missing)} (not a cmake --install of the port)")
    return sorted(files, key=lambda f: f["rel"].as_posix())


def unchanged(stage: Path, files: list[dict]) -> None:
    again = read_stage(stage)
    if [(f["rel"], f["sha256"]) for f in again] != [(f["rel"], f["sha256"]) for f in files]:
        raise ReleaseError(f"--stage {stage} changed while it was packaged")


def win(rel: PurePosixPath) -> str:
    return "\\".join(rel.parts)


def nsis_source(script_dir: Path, path: Path) -> str:
    """A File source relative to the script's folder (makensis runs there): never a leading /."""
    try:
        rel = os.path.relpath(path.resolve(), script_dir.resolve())
    except ValueError as error:
        raise ReleaseError(f"{path}: no path from the script's folder {script_dir} ({error})") from error
    rel = rel.replace(os.sep, "/")
    if NSIS_UNSAFE.search(rel) or rel.startswith("/"):
        raise ReleaseError(f"{path}: its path from the script's folder ({rel!r}) cannot be written in NSIS")
    return rel


def fill(template: str, values: dict[str, str]) -> str:
    used = set(PLACEHOLDER.findall(template))
    unknown, missing = sorted(used - PLACEHOLDERS), sorted(PLACEHOLDERS - used)
    if unknown or missing:
        raise ReleaseError(f"template: unknown placeholders {unknown}, missing placeholders {missing}")
    out = PLACEHOLDER.sub(lambda m: values[m[1]], template)
    left = PLACEHOLDER.findall(out)
    if left:
        raise ReleaseError(f"template: placeholders left after rendering: {sorted(set(left))}")
    return out


def render(tag: str, stage: Path, out: Path, template: Path = TEMPLATE) -> Path:
    names = release_names(tag)
    if not template.is_file():
        raise ReleaseError(f"template {template}: no such file")
    if out.suffix != ".nsi":
        raise ReleaseError(f"--out {out}: an NSIS script is named *.nsi")
    files = read_stage(stage)
    if out.resolve().is_relative_to(stage.resolve()):
        raise ReleaseError(f"--out {out} is inside the stage")
    script_dir = out.parent

    install, current = [], None
    by_folder = sorted(files, key=lambda f: (f["rel"].parent != PurePosixPath("."), f["rel"].parent.as_posix(),
                                             f["rel"].name))
    for f in by_folder:
        parent = f["rel"].parent
        if parent != current:
            folder = "" if parent == PurePosixPath(".") else "\\" + win(parent)
            install.append(f'  SetOutPath "$INSTDIR{folder}"')
            current = parent
        install.append(f'  File "{nsis_source(script_dir, f["path"])}"')
    subdirs = sorted({p for f in files for p in f["rel"].parents if p != PurePosixPath(".")},
                     key=lambda p: (-len(p.parts), p.as_posix()))
    present = {f["rel"].as_posix() for f in files}
    links = [(exe, title) for exe, title in SHORTCUTS if exe in present]
    values = {
        "VERSION": names["version"],
        "NSIS_VERSION": names["nsis_version"],
        "SETUP_NAME": names["setup"],
        "LICENSE_FILE": nsis_source(script_dir, stage / LICENSE_FILE),
        "INSTALL_FILES": "\n".join(install),
        "UNINSTALL_FILES": "\n".join(f'  Delete "$INSTDIR\\{win(f["rel"])}"' for f in files),
        "UNINSTALL_DIRS": "\n".join(f'  RMDir "$INSTDIR\\{win(d)}"' for d in subdirs) or "  ; (no sub-folders)",
        "SHORTCUTS": "\n".join(f'  CreateShortcut "$SMPROGRAMS\\${{PRODUCT}}\\{title}.lnk" "$INSTDIR\\{exe}"'
                               for exe, title in links),
        "UNSHORTCUTS": "\n".join(f'  Delete "$SMPROGRAMS\\${{PRODUCT}}\\{title}.lnk"' for _, title in links),
        "ESTIMATED_SIZE_KB": str(-(-sum(f["size"] for f in files) // 1024)),
    }
    text = fill(template.read_text(encoding="utf-8"), values)
    unchanged(stage, files)
    write_atomic(out, text.encode("utf-8"))
    return out


def zip_stage(tag: str, stage: Path, out_dir: Path) -> Path:
    names = release_names(tag)
    files = read_stage(stage)
    if out_dir.resolve().is_relative_to(stage.resolve()):
        raise ReleaseError(f"--out-dir {out_dir} is inside the stage")
    top = PurePosixPath(names["folder"])
    folders = sorted({p for f in files for p in f["rel"].parents if p != PurePosixPath(".")}, key=str)
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / names["zip"]
    fd, tmp = tempfile.mkstemp(dir=out_dir, prefix=f".{names['zip']}.")
    os.close(fd)
    try:
        with zipfile.ZipFile(tmp, "w") as z:
            for rel in [PurePosixPath(".")] + folders:
                info = zipfile.ZipInfo(f"{(top / rel).as_posix()}/", date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system, info.external_attr = 3, (0o40755 << 16) | 0x10
                z.writestr(info, b"")
            for f in files:
                info = zipfile.ZipInfo((top / f["rel"]).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system, info.external_attr = 3, 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                data = f["path"].read_bytes()
                if hashlib.sha256(data).hexdigest() != f["sha256"]:
                    raise ReleaseError(f"--stage {stage}: {f['rel']} changed while it was packaged")
                z.writestr(info, data, compresslevel=9)
            info = zipfile.ZipInfo((top / RELEASE_FILE).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system, info.external_attr = 3, 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, release_text(names["version"], "zip"), compresslevel=9)
        unchanged(stage, files)
        os.replace(tmp, target)
    finally:
        Path(tmp).unlink(missing_ok=True)
    return target


def find_makensis(makensis: str | None) -> str:
    """The makensis to run, as an absolute path: --makensis if given, else the one on PATH."""
    name = makensis or "makensis"
    found = shutil.which(name) if os.sep not in name and "/" not in name else name
    if not found or not os.path.isfile(found) or not os.access(found, os.X_OK):
        raise ReleaseError(f"no makensis at {name!r}: setup.exe needs NSIS {'.'.join(map(str, MAKENSIS_MIN))} "
                           "or later (the portable .zip needs none)")
    return os.path.abspath(found)


def makensis_version(exe: str) -> str:
    """What exe -VERSION prints, if it is a version of at least MAKENSIS_MIN; else ReleaseError."""
    least = ".".join(map(str, MAKENSIS_MIN))
    try:
        probe = subprocess.run([exe, "-VERSION"], stdin=subprocess.DEVNULL, capture_output=True, text=True,
                               errors="replace", timeout=PROBE_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ReleaseError(f"makensis -VERSION did not run ({error}); nothing compiled") from error
    printed = probe.stdout.strip()
    if probe.returncode != 0:
        raise ReleaseError(f"makensis -VERSION failed (exit {probe.returncode}, {printed[:80]!r}); nothing compiled")
    m = MAKENSIS_VERSION.fullmatch(printed)
    if not m:
        raise ReleaseError(f"makensis -VERSION printed {printed[:80]!r}, not a version; nothing compiled")
    if (int(m[1]), int(m[2])) < MAKENSIS_MIN:
        raise ReleaseError(f"makensis {printed} is older than {least}: NSIS before {least} is refused for an "
                           f"elevated setup.exe (CVE-2023-37378, the uninstaller's temporary copy); "
                           f"install NSIS {least} or later; nothing compiled")
    return printed


def build_setup(tag: str, script: Path, makensis: str | None = None) -> tuple[Path, Path, str]:
    """setup.exe from a rendered script, by a makensis of at least MAKENSIS_MIN, and its build record."""
    names = release_names(tag)
    if script.suffix != ".nsi" or script.is_symlink() or not script.is_file():
        raise ReleaseError(f"--script {script}: not a rendered *.nsi")
    if f'OutFile "{names["setup"]}"' not in script.read_text(encoding="utf-8"):
        raise ReleaseError(f"--script {script}: not rendered for {tag} (no OutFile {names['setup']})")
    exe = find_makensis(makensis)
    version = makensis_version(exe)
    setup = script.parent / names["setup"]
    record = setup.with_name(f"{setup.name}.build.json")
    for stale in (setup, record):
        stale.unlink(missing_ok=True)
    try:
        made = subprocess.run([exe, "-V2", str(script.resolve())], stdin=subprocess.DEVNULL, capture_output=True,
                              text=True, errors="replace", timeout=COMPILE_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as error:
        setup.unlink(missing_ok=True)
        raise ReleaseError(f"makensis {version} did not finish ({error})") from error
    if made.returncode != 0 or setup.is_symlink() or not setup.is_file() or setup.stat().st_size == 0:
        setup.unlink(missing_ok=True)
        raise ReleaseError(f"makensis {version} failed (exit {made.returncode}): {(made.stdout + made.stderr)[-500:]}")
    info = {"asset": names["setup"], "makensis_version": version, "makensis_minimum": ".".join(map(str, MAKENSIS_MIN))}
    write_atomic(record, (json.dumps(info, indent=2, sort_keys=True) + "\n").encode())
    return setup, record, version


def write_sha256(tag: str, assets: list[Path]) -> list[Path]:
    names = release_names(tag)
    allowed = (names["setup"], names["zip"])
    lines = []
    for asset in assets:
        if asset.name not in allowed:
            raise ReleaseError(f"{asset.name}: not an asset of {tag} ({' or '.join(allowed)})")
        if asset.is_symlink() or not asset.is_file():
            raise ReleaseError(f"{asset}: no such file")
        lines.append((asset, f"{hashlib.sha256(asset.read_bytes()).hexdigest()}  {asset.name}\n"))
    written = []
    for asset, line in lines:
        sidecar = asset.with_name(f"{asset.name}.sha256")
        write_atomic(sidecar, line.encode())
        written.append(sidecar)
    return written


PACKAGE_SCOPE = ("the packaging phase only: render, setup, zip and sha256 on a final installed stage; not the "
                 "port's compilation, dependency fetching, the payload audit (the stage checks here are not it), "
                 "signing, the Linux assets, the SBOM, an upload, or a Windows install and start")


def package_names(names: dict[str, str]) -> list[str]:
    """What package writes into --out-dir, in the order it copies them (the sums last)."""
    setup, archive = names["setup"], names["zip"]
    return [setup, f"{setup}.build.json", archive, f"{setup}.sha256", f"{archive}.sha256"]


def copy_new(src: Path, dest: Path, written: list[Path]) -> None:
    """src's bytes as a new file dest: never one already there, even one that appeared meanwhile."""
    data = src.read_bytes()
    try:
        f = dest.open("xb")
    except FileExistsError as error:
        raise ReleaseError(f"{dest} appeared while packaging; it is not overwritten") from error
    written.append(dest)
    with f:
        f.write(data)
    if hashlib.sha256(dest.read_bytes()).digest() != hashlib.sha256(data).digest():
        raise ReleaseError(f"{dest}: its bytes differ from the ones packaged")


def undo(written: list[Path], made: list[Path], out_dir: Path, work: Path) -> str:
    """Remove what this package run wrote and the folders it made; say what it did and what is left."""
    shutil.rmtree(work, ignore_errors=True)
    removed, gone, left = [], [], []
    for path in reversed(written):
        try:
            path.unlink()
            removed.append(path.name)
        except OSError as error:
            left.append(f"{path} ({error})")
    if os.path.lexists(work):
        left.append(f"the temporary folder {work}")
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


def package(tag: str, stage: Path, out_dir: Path, makensis: str | None = None) -> dict[str, str]:
    """render, setup, zip and sha256 of one stage; the five assets copied into out_dir as new files."""
    names = release_names(tag)
    files = read_stage(stage)
    if out_dir.resolve().is_relative_to(stage.resolve()):
        raise ReleaseError(f"--out-dir {out_dir} is inside the stage")
    if os.path.lexists(out_dir) and not out_dir.is_dir():
        raise ReleaseError(f"--out-dir {out_dir}: not a folder")
    final = package_names(names)
    taken = [name for name in final if os.path.lexists(out_dir / name)]
    if taken:
        raise ReleaseError(f"--out-dir {out_dir} already holds {', '.join(taken)}; nothing is overwritten "
                           "(move them, or choose another folder); nothing written")
    exe = find_makensis(makensis)
    made, here = [], out_dir.absolute()
    while not os.path.lexists(here):
        made.append(here)
        here = here.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=out_dir, prefix=".package-"))
    written: list[Path] = []
    try:
        script = render(tag, stage, work / "xenogears-pc.nsi")
        setup, record, version = build_setup(tag, script, exe)
        archive = zip_stage(tag, stage, work)
        unchanged(stage, files)
        sums = write_sha256(tag, [setup, archive])
        built = [setup, record, archive, *sums]
        if [p.name for p in built] != final:
            raise ReleaseError(f"package made {[p.name for p in built]}, not {final}")
        for path in built:
            copy_new(path, out_dir / path.name, written)
    except (ReleaseError, OSError) as error:
        raise ReleaseError(f"{error}; {undo(written, made, out_dir, work)}") from error
    finally:
        shutil.rmtree(work, ignore_errors=True)
    keys = ("setup", "build_info", "zip", "setup_sha256", "zip_sha256")
    return {**{key: str(path) for key, path in zip(keys, written)}, "makensis_version": version}


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    finally:
        Path(tmp).unlink(missing_ok=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("names", help="the version and asset names of a tag")
    p.add_argument("--tag", required=True)
    p = sub.add_parser("render", help="the NSIS script of a stage")
    p.add_argument("--tag", required=True)
    p.add_argument("--stage", type=Path, required=True, help="the folder cmake --install filled")
    p.add_argument("--out", type=Path, required=True, help="the .nsi to write (setup.exe is made beside it)")
    p.add_argument("--template", type=Path, default=TEMPLATE, help="default: the template beside this script")
    p = sub.add_parser("setup", help="setup.exe from a rendered script (makensis 3.10 or later)")
    p.add_argument("--tag", required=True)
    p.add_argument("--script", type=Path, required=True, help="the .nsi render wrote (setup.exe is made beside it)")
    p.add_argument("--makensis", help="the makensis to run (default: the one on PATH)")
    p = sub.add_parser("zip", help="the portable .zip of a stage")
    p.add_argument("--tag", required=True)
    p.add_argument("--stage", type=Path, required=True)
    p.add_argument("--out-dir", type=Path, required=True)
    p = sub.add_parser("sha256", help="a .sha256 beside each asset")
    p.add_argument("--tag", required=True)
    p.add_argument("assets", type=Path, nargs="+")
    p = sub.add_parser("package", help="render, setup, zip and sha256 in one step (" + PACKAGE_SCOPE + ")",
                       description="package: " + PACKAGE_SCOPE + ". The five assets are copied into --out-dir "
                       "as new files; it refuses a folder already holding any of them.")
    p.add_argument("--tag", required=True)
    p.add_argument("--stage", type=Path, required=True, help="the folder cmake --install filled, final")
    p.add_argument("--out-dir", type=Path, required=True, help="where the assets go (made if missing)")
    p.add_argument("--makensis", help="the makensis to run (default: the one on PATH)")
    args = ap.parse_args()
    try:
        if args.command == "names":
            for key, value in release_names(args.tag).items():
                print(f"{key}={value}")
        elif args.command == "render":
            print(render(args.tag, args.stage, args.out, args.template))
        elif args.command == "setup":
            setup, record, version = build_setup(args.tag, args.script, args.makensis)
            print(f"setup={setup}\nbuild_info={record}\nmakensis_version={version}")
        elif args.command == "zip":
            print(zip_stage(args.tag, args.stage, args.out_dir))
        elif args.command == "package":
            for key, value in package(args.tag, args.stage, args.out_dir, args.makensis).items():
                print(f"{key}={value}")
            print(f"scope={PACKAGE_SCOPE}")
        else:
            for path in write_sha256(args.tag, args.assets):
                print(path)
    except (ReleaseError, OSError) as error:
        print(f"windows_release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
