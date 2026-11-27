#!/usr/bin/env python3
"""The Linux release tarball of xenogears-pc and its .sha256, from an installed stage.

    python packaging/linux_release.py names   --tag v<YYYY>.<MM>.<N>
    python packaging/linux_release.py package --tag <tag> --stage <installed prefix> --out-dir <dir> \
        --ver <us|jp> --build <original engine's build> [--enhanced-build <enhanced engine's build>]

The version is the release tag without its v (CalVer vYYYY.MM.N, month 01-12,
N a whole number from 0 without leading zeros), spelled as the tag spells it;
a tag of any other form is refused as given, never corrected. The assets are

  xenogears-pc-<version>-linux-x86_64.tar.gz          the stage, in one folder
  xenogears-pc-<version>-linux-x86_64.tar.gz.sha256   sha256sum format (sha256sum -c)

(the source tarball of the release is xenogears-pc-<version>.tar.gz, made by
the release-source workflow; this is the built port, a different asset).

The stage is the prefix a Linux Release build configured to hand out
(-DXG_DIST=ON) installs into, by the install rules of port/cmake/deb.cmake:

    cmake --install <build> --prefix <stage> --strip

For both engines, install the original engine's build and then the enhanced
engine's build (-DXG_ENGINE=enhanced) into the same prefix. The stage must hold
exactly these files, and the folders they are in, nothing else:

  bin/xenogears                                    required, the original engine
  bin/xenogears-enhanced                           optional, the enhanced engine
  share/xenogears-pc/xenogears.xgschema            required, the original engine's state schema
  share/xenogears-pc/xenogears-enhanced.xgschema   with bin/xenogears-enhanced, its state schema
  share/doc/xenogears-pc/COPYING                   required
  share/doc/xenogears-pc/LICENSE-Jukebox-font.txt  required
  share/doc/xenogears-pc/LICENSE-imgui.txt         required
  share/doc/xenogears-pc/LICENSE-glslang.txt       optional (when linked)
  share/doc/xenogears-pc/LICENSE-dav1d.txt         optional (when linked)
  share/xenogears-pc/release.txt                   optional (the .deb's; never packaged as it is)

The tarball always holds its own share/xenogears-pc/release.txt, written here
from the tag: "version=<version>" and "kind=tarball", what the in-app updater
reads (port/src/system/release.h). The stage's, if it has one (the .deb's,
kind=deb), must be such a file of the same version and is left out.

Each executable must be an x86-64 ELF executable (not a shared library or an
object) with its owner's execute bit set; each licence must be UTF-8 text.
Each state schema (port/CMakeLists.txt XG_STATE_SCHEMA: what lets a slot saved
by one version of the port load in another) must be in the packed form the
game reads ("XGSCHEMA") and pass this checkout's tools/state_schema.py
--verify --stripped against its executable's bytes: read from that image (its
identity), no path of the build machine in it, and the executable without
debug information (the install's --strip). The
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

The payload audit. package runs this checkout's tools/payloadcheck.py (the
file next to packaging/ in the checkout this helper is in: never one found
through the working folder, PATH or the environment) on every executable it is
about to archive, and writes nothing, not even --out-dir, unless each one
passes. There is no way to skip it. Per executable it runs, without a shell,

    <this python> -E -s -B <checkout>/tools/payloadcheck.py --exe <copy> --build <build> --ver <ver> --dist

in the checkout, with no standard input and a time limit. --dist is the
auditor's rule for a build to hand out: it fails a build not configured with
-DXG_DIST=ON (port/CMakeLists.txt) and an executable that still names a
folder of the machine that built it (its build folder, its repository, the
builder's home); the auditor decides both, this helper only asks for them. <copy> is the bytes
read from the stage, written read-only into a private temporary folder; the
same bytes in memory go into the tarball, and the copy must still be those
bytes after its audit. --build is the configured CMake build folder the
original engine was linked in and --enhanced-build the enhanced engine's
(required when the stage holds it, refused when it does not): the auditor
reads their objects to say where a run of the game's bytes comes from. Their
CMakeCache.txt must name --ver and the engine. An executable passes only when
the auditor exits 0 and prints exactly its two lines: the heading, naming this
copy, the version and the number of module images it read, and one
"payload ok" line with no run in a failing category, no path of the build
machine and no "not a build to hand out". Every staged executable is audited
before the verdict; any other exit, output, a time-out or a launch error is a
refusal. The auditor's own lines are relayed without the bytes it found, the
build machine's paths it found, the build folder or the checkout's folder.

The module images are the player's own, extracted by tools/extract.py into the
checkout's disc folder for <ver>; only the auditor reads them. The auditor
reads a module's image when its file is there and passes over it silently when
it is not, so a partial extraction would pass an executable against fewer
modules. Therefore this helper reads config/<ver>/disc.yaml the way the
auditor does (its one-line {name: ...} entries; main is the .exe, the others
.bin), requires each of those files before the audit, and requires the
auditor's heading to report that same number: a missing image, an empty list,
a list with entries of another form (which the auditor would not read), or a
count that differs is a refusal. The list is thus read twice, here and in the
auditor, and the two must agree on every run. The auditor, its module, its
allowance file, the list and the images must be the same bytes after the
audits as before them.

What the audit is not. It finds runs of 16 bytes or more of the module images
(as the game loads them) in the executables: not shorter or transformed
copies, not the disc's other files, not the licences. The auditor counts bytes
outside every ELF section as names, not payload. It does not prove that
--build is the build an executable came from, nor that a build of another
version was not staged under --ver. Passing is the auditor's verdict on those
bytes, not a clearance of every kind, and nothing here checks that the game
is correct.

package refuses, before writing anything: a bad tag, a bad stage, a missing
build, auditor, list or image, an --out-dir inside the stage or holding it, an
--out-dir that is not a folder, an --out-dir already holding either asset's
name (a dangling link too), and a failed audit: nothing there is ever
overwritten or deleted. After the audits it checks the stage did not change,
writes the tarball to a temporary
file in --out-dir, checks the stage did not change meanwhile, links it in
under its asset name as a new file, hashes the bytes of that file and writes
the .sha256 the same way. The two are not written as one atomic step. If a
step fails it exits 1, removes the temporary file and every asset and folder
this run made (a sum never names a missing tarball), and says which it removed
or could not remove. The stage's files are only read. A writer that changes a
stage file and puts it back between the reads is not noticed.

This is the packaging step only. It does not compile the port, fetch its
dependencies, sign anything, make the .deb or the Windows assets, write the
SBOM or upload. Nothing here installs or starts the game.
Standard library only.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import re
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

PACKAGE = "xenogears-pc"
ARCH = "x86_64"
TAG = re.compile(r"v(20[0-9][0-9])\.(0[1-9]|1[0-2])\.(0|[1-9][0-9]*)")  # ASCII digits only
NAME_MAX = 255  # bytes in one file name

DOC = "share/doc/xenogears-pc"
ICON = "share/icons/hicolor/scalable/apps/xenogears.svg"
DESKTOPS = {"share/applications/xenogears.desktop": "bin/xenogears",
            "share/applications/xenogears-enhanced.desktop": "bin/xenogears-enhanced"}
EXECUTABLES = {"bin/xenogears": True, "bin/xenogears-enhanced": False}  # name: required
LICENCES = {f"{DOC}/COPYING": True, f"{DOC}/LICENSE-Jukebox-font.txt": True, f"{DOC}/LICENSE-imgui.txt": True,
            f"{DOC}/LICENSE-glslang.txt": False, f"{DOC}/LICENSE-dav1d.txt": False}
RELEASE = "share/xenogears-pc/release.txt"  # what the in-app updater reads (port/src/system/release.h)
# Each executable's state schema (port/CMakeLists.txt XG_STATE_SCHEMA), where the installed game reads it
# (port/src/system/stateportable.c: <prefix>/share/xenogears-pc/<executable>.xgschema).
SCHEMAS = {"share/xenogears-pc/xenogears.xgschema": "bin/xenogears",
           "share/xenogears-pc/xenogears-enhanced.xgschema": "bin/xenogears-enhanced"}
SCHEMA_MAGIC = b"XGSCHEMA"
RELEASE_MAX = 4096  # bytes: the port reads no longer file
FOLDERS = ("bin", "share", "share/applications", "share/doc", DOC, "share/icons", "share/icons/hicolor",
           "share/icons/hicolor/scalable", "share/icons/hicolor/scalable/apps", "share/xenogears-pc")
EXE_MODE, FILE_MODE, DIR_MODE = 0o755, 0o644, 0o755

# The game's data and a player's own files: never in a release.
GAME_EXTENSIONS = {".bin", ".cue", ".iso", ".img", ".ccd", ".sub", ".mds", ".mdf", ".chd", ".pbp", ".ecm",
                   ".toc", ".str", ".xa", ".mcd", ".mcr", ".mc", ".gme", ".vmp", ".psv", ".srm", ".sav"}
PLAYER_FILES = {"settings.ini"}
DISC_EXE = re.compile(r"(slus|slps|scus|sces|sles)_[0-9]{3}\.[0-9]{2}", re.I)
CD_SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
ISO_MARKS = (0x8001, 16 * 2352 + 24 + 1, 16 * 2352 + 16 + 1)  # "CD001": cooked, raw mode 2, raw mode 1

ELF_X86_64, ET_EXEC, ET_DYN, PT_INTERP = 62, 2, 3, 3

# The payload audit: this checkout's auditor and what it reads.
CHECKOUT = Path(__file__).resolve().parent.parent  # packaging/ is in the checkout's root
AUDITOR = CHECKOUT / "tools" / "payloadcheck.py"
AUDITOR_FILES = (AUDITOR, CHECKOUT / "tools" / "payload_pe.py", CHECKOUT / "tools" / "payload_elf.py",
                 CHECKOUT / "tools" / "payload_allow.txt")
VERSIONS = ("us", "jp")
ENGINES = {"bin/xenogears": "original", "bin/xenogears-enhanced": "enhanced"}  # the build's XG_ENGINE
AUDIT_TIMEOUT = 3600  # seconds, each executable
SCHEMA_TOOL = CHECKOUT / "tools" / "state_schema.py"  # its --verify (with statecheck.py and statecanon.py beside it)
SCHEMA_TOOL_FILES = (SCHEMA_TOOL, CHECKOUT / "tools" / "statecheck.py", CHECKOUT / "tools" / "statecanon.py")
SCHEMA_TIMEOUT = 600  # seconds, each schema
AUDIT_MIN_RUN = 16    # the auditor's shortest run (its default; no other is asked for)
IMAGE_ENTRY = re.compile(r"\{name:\s*(\w+),([^}]*)\}")  # a module of disc.yaml, as the auditor reads it
NAME_KEY = re.compile(r"\bname\s*:")                    # a module entry of any form
AUDIT_HEADING = re.compile(r"retail module data in (.+) \((US|JP)\): ([0-9]+) module images, runs of >= ([0-9]+) bytes")
AUDIT_OK = re.compile(r"payload ok: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; "
                      r"allowed [0-9]+/[0-9]+, symbol names [0-9]+/[0-9]+, src \(game C's own data\) 0/0")
HEX_RUN = re.compile(r"[0-9A-Fa-f]{16,}")
BUILD_PATH_LINE = re.compile(r"(\s*build path\s+[0-9]+ places)\s.*")  # the auditor's line for a build machine's path


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


def release_text(version: str) -> bytes:
    """The tarball's release.txt (port/src/system/release.h)."""
    return f"version={version}\nkind=tarball\n".encode()


def check_release_file(data: bytes, rel: str) -> None:
    """A stage's release.txt: short UTF-8 text of version= and kind= lines (port/cmake/deb.cmake writes it)."""
    if len(data) > RELEASE_MAX or b"\x00" in data:
        raise ReleaseError(f"stage: {rel} is not a release file (longer than {RELEASE_MAX} bytes, or binary)")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ReleaseError(f"stage: {rel} is not UTF-8 text") from error
    if not re.fullmatch(r"version=[0-9.]+\nkind=(deb|tarball)\n", text):
        raise ReleaseError(f"stage: {rel} is not a release file of the .deb or the tarball: {text[:80]!r}")


def tarball_files(files: list[dict], version: str) -> list[dict]:
    """The files the tarball holds: the stage's, its release.txt (of the same version) replaced by the tarball's."""
    staged = next((f for f in files if f["rel"] == RELEASE), None)
    if staged is not None and not staged["data"].decode().startswith(f"version={version}\n"):
        raise ReleaseError(f"stage: {RELEASE} names another version than the tag's {version}: "
                           f"{staged['data'][:80]!r}; nothing written")
    data = release_text(version)
    own = {"rel": RELEASE, "data": data, "sha256": hashlib.sha256(data).hexdigest(), "mode": FILE_MODE}
    return sorted([f for f in files if f["rel"] != RELEASE] + [own], key=lambda f: f["rel"])


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
            if (rel not in EXECUTABLES and rel not in LICENCES and rel != RELEASE and rel not in SCHEMAS
                    and rel != ICON and rel not in DESKTOPS):
                raise ReleaseError(f"stage: {rel} is not a file the port's install makes (only {', '.join(EXECUTABLES)}, "
                                   f"the licences in {DOC}, {ICON}, the desktop entries, {RELEASE} and the executables' "
                                   "state schemas)")
            data = path.read_bytes()
            check_game_data(data, rel)
            if rel in EXECUTABLES:
                check_elf(data, rel)
                if not mode & stat.S_IXUSR:
                    raise ReleaseError(f"stage: {rel} is not executable")
            elif rel == RELEASE:
                check_release_file(data, rel)
            elif rel in SCHEMAS:
                if data[:8] != SCHEMA_MAGIC or len(data) < 16:
                    raise ReleaseError(f"stage: {rel} is not a state schema in its packed form (XGSCHEMA)")
            else:
                check_licence(data, rel)
            files.append({"rel": rel, "data": data, "sha256": hashlib.sha256(data).hexdigest(),
                          "mode": EXE_MODE if rel in EXECUTABLES else FILE_MODE})
    present = {f["rel"] for f in files}
    missing = [n for n, need in {**EXECUTABLES, **LICENCES, ICON: True}.items() if need and n not in present]
    missing += [schema for schema, exe in SCHEMAS.items() if exe in present and schema not in present]
    missing += [desktop for desktop, exe in DESKTOPS.items() if exe in present and desktop not in present]
    if missing:
        raise ReleaseError(f"stage: no {', '.join(missing)} (not a cmake --install of the port's Linux build"
                           f"{' with its state schema' if any(n in SCHEMAS for n in missing) else ''})")
    alone = [schema for schema, exe in SCHEMAS.items() if schema in present and exe not in present]
    alone += [desktop for desktop, exe in DESKTOPS.items() if desktop in present and exe not in present]
    if alone:
        raise ReleaseError(f"stage: {', '.join(alone)} without its executable")
    return sorted(files, key=lambda f: f["rel"])


def unchanged(stage: Path, files: list[dict]) -> None:
    again = read_stage(stage)
    if [(f["rel"], f["sha256"], f["mode"]) for f in again] != [(f["rel"], f["sha256"], f["mode"]) for f in files]:
        raise ReleaseError(f"--stage {stage} changed while it was packaged")


def module_images(ver: str) -> list[Path]:
    """The module images the auditor reads for ver, every one of them required.

    The auditor takes the modules from config/<ver>/disc.yaml by its one-line
    {name: ...} entries (an entry with iso: is <name>.exe, the others <name>.bin)
    and leaves out, silently, an image whose file is missing. This reads the
    list the same way and refuses what would make the two differ: no entry, an
    entry of another form, a name twice, a missing or empty image. The auditor's
    heading must then report this many images (audit_executable), so the two
    readings of the list are compared on every run."""
    listing = CHECKOUT / "config" / ver / "disc.yaml"
    try:
        text = listing.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ReleaseError(f"payload audit: no module list {listing} ({error}); nothing written") from error
    entries = IMAGE_ENTRY.findall(text)
    if not entries:
        raise ReleaseError(f"payload audit: {listing} lists no module; nothing written")
    if len(NAME_KEY.findall(text)) != len(entries):
        raise ReleaseError(f"payload audit: {listing} has {len(NAME_KEY.findall(text))} name: keys but "
                           f"{len(entries)} entries of the form the auditor reads; nothing written")
    names = [name for name, _ in entries]
    if len(set(names)) != len(names):
        raise ReleaseError(f"payload audit: {listing} lists a module twice; nothing written")
    folder = CHECKOUT / "disc" / ver
    images = [folder / (f"{name}.exe" if "iso:" in rest else f"{name}.bin") for name, rest in entries]
    gone = [p.name for p in images if not p.is_file() or p.stat().st_size == 0]
    if gone:
        raise ReleaseError(f"payload audit: {len(gone)} of the {len(images)} module images of {listing} are not "
                           f"in {folder} ({', '.join(gone)}): extract the player's disc first "
                           "(tools/extract.py); nothing written")
    return images


def fingerprint(paths: list[Path]) -> list[tuple[str, str]]:
    out = []
    for path in paths:
        try:
            out.append((str(path), hashlib.sha256(path.read_bytes()).hexdigest()))
        except OSError as error:
            raise ReleaseError(f"payload audit: cannot read {path} ({error}); nothing written") from error
    return out


def audit_build(build: Path, ver: str, rel: str) -> Path:
    """The configured CMake build folder of one staged executable, absolute; its cache names ver and the engine."""
    build = Path(os.path.abspath(build))
    cache = build / "CMakeCache.txt"
    if not cache.is_file() or not (build / "CMakeFiles").is_dir():
        raise ReleaseError(f"the build of {rel}, {build}: not a configured CMake build folder "
                           "(no CMakeCache.txt and CMakeFiles); nothing written")
    text = cache.read_text(encoding="utf-8", errors="replace")
    for key, want in (("XG_VERSION", ver), ("XG_ENGINE", ENGINES[rel])):
        found = re.search(rf"^{key}:\w+=(\w+)\s*$", text, re.M)
        if found is None or found.group(1).lower() != want:
            raise ReleaseError(f"the build of {rel}, {build}: its CMakeCache.txt has {key} "
                               f"{found.group(1) if found else 'unset'}, not {want}; nothing written")
    return build


def redact(text: str, copy: Path, rel: str, build: Path) -> str:
    """One line of the auditor's output, without the bytes or the build machine's paths it found."""
    text = BUILD_PATH_LINE.sub(r"\1  [path withheld]", text)
    for folder, name in ((str(copy), rel), (str(build), f"<the build of {rel}>"), (str(CHECKOUT), "<checkout>")):
        text = text.replace(folder, name)
    text = HEX_RUN.sub("[bytes withheld]", text)
    return "".join(c if c.isprintable() else "?" for c in text)


def stop(proc: subprocess.Popen) -> None:
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except OSError:
        proc.kill()
    try:
        proc.communicate(timeout=10)
    except (subprocess.TimeoutExpired, OSError, ValueError):
        pass


def audit_executable(f: dict, build: Path, ver: str, images: int, scratch: Path) -> str:
    """The auditor's verdict on one executable's bytes: its "payload ok" line, or ReleaseError."""
    rel = f["rel"]
    copy = scratch / PurePosixPath(rel).name
    copy.write_bytes(f["data"])
    os.chmod(copy, 0o500)
    if not sys.executable:
        raise ReleaseError(f"payload audit of {rel}: this Python does not know its own path")
    argv = [sys.executable, "-E", "-s", "-B", str(AUDITOR), "--exe", str(copy), "--build", str(build), "--ver", ver,
            "--dist"]
    try:
        proc = subprocess.Popen(argv, cwd=CHECKOUT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)
    except OSError as error:
        raise ReleaseError(f"payload audit of {rel}: the auditor could not be started ({error})") from error
    try:
        out, err = proc.communicate(timeout=AUDIT_TIMEOUT)
    except subprocess.TimeoutExpired as error:
        stop(proc)
        raise ReleaseError(f"payload audit of {rel}: no verdict in {AUDIT_TIMEOUT} s; stopped") from error
    except BaseException:
        stop(proc)
        raise
    lines = out.decode("utf-8", "replace").splitlines()
    heading = AUDIT_HEADING.fullmatch(lines[0]) if lines else None
    if hashlib.sha256(copy.read_bytes()).hexdigest() != f["sha256"]:
        why = "the audited copy changed during its audit"
    elif proc.returncode != 0:
        why = f"the auditor exited {proc.returncode}"
    elif heading is None:
        why = "the auditor's output has no heading"
    elif heading.group(1) != str(copy) or heading.group(2) != ver.upper():
        why = "the auditor's heading names another executable or version"
    elif int(heading.group(3)) != images:
        why = f"the auditor read {int(heading.group(3))} module images, not all {images}"
    elif int(heading.group(4)) != AUDIT_MIN_RUN:
        why = f"the auditor looked for runs of {int(heading.group(4))} bytes, not {AUDIT_MIN_RUN}"
    elif len(lines) != 2 or not AUDIT_OK.fullmatch(lines[1]):
        why = "the auditor's output is not its heading and one clean \"payload ok\" line"
    else:
        return f"{rel} ({ver.upper()}, sha256 {f['sha256']}): all {images} module images read; {lines[1]}"
    said = [redact(line, copy, rel, build) for line in lines[-30:] + err.decode("utf-8", "replace").splitlines()[-5:]]
    raise ReleaseError(f"payload audit of {rel} FAILED: {why}" + "".join(f"\n    | {line}" for line in said))


def check_schemas(files: list[dict]) -> list[str]:
    """Every staged state schema against its executable's bytes (tools/state_schema.py --verify --stripped): its
    verdict lines, or ReleaseError if any failed."""
    absent = [str(p) for p in SCHEMA_TOOL_FILES if not p.is_file()]
    if absent:
        raise ReleaseError(f"state schemas: no {', '.join(absent)}: run packaging/linux_release.py from a checkout "
                           "of the port; nothing written")
    by_rel = {f["rel"]: f for f in files}
    passed, failed = [], []
    scratch = Path(tempfile.mkdtemp(prefix="xenogears-pc-schema-"))
    try:
        for schema, exe in SCHEMAS.items():
            if schema not in by_rel:
                continue
            copies = []
            for rel in (exe, schema):
                copy = scratch / PurePosixPath(rel).name
                copy.write_bytes(by_rel[rel]["data"])
                os.chmod(copy, 0o400)
                copies.append(copy)
            argv = [sys.executable, "-E", "-s", "-B", str(SCHEMA_TOOL), "--verify", str(copies[1]), "--image",
                    str(copies[0]), "--stripped"]
            try:
                done = subprocess.run(argv, cwd=CHECKOUT, stdin=subprocess.DEVNULL, capture_output=True,
                                      timeout=SCHEMA_TIMEOUT)
            except (OSError, subprocess.TimeoutExpired) as error:
                failed.append(f"state schema {schema}: tools/state_schema.py --verify did not finish ({error})")
                continue
            said = (done.stdout + done.stderr).decode("utf-8", "replace").replace(str(scratch) + "/", "").strip()
            if done.returncode != 0 or any(hashlib.sha256(c.read_bytes()).hexdigest() != by_rel[r]["sha256"]
                                           for c, r in zip(copies, (exe, schema))):
                failed.append(f"state schema {schema} FAILED against {exe}: {said[-600:]}")
            else:
                passed.append(f"{schema}: {said.splitlines()[-1] if said else 'verified'}")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if failed:
        raise ReleaseError("\n".join(failed + ["the state schemas did not all pass; nothing written"]))
    return passed


def audit(files: list[dict], builds: dict[str, Path], ver: str) -> list[str]:
    """Every staged executable through this checkout's auditor; the verdicts, or ReleaseError if any failed."""
    absent = [str(p) for p in AUDITOR_FILES if not p.is_file()]
    if absent:
        raise ReleaseError(f"payload audit: no {', '.join(absent)}: run packaging/linux_release.py from a checkout "
                           "of the port; nothing written")
    images = module_images(ver)
    inputs = [*AUDITOR_FILES, CHECKOUT / "config" / ver / "disc.yaml", *images]
    before = fingerprint(inputs)
    passed, failed = [], []
    scratch = Path(tempfile.mkdtemp(prefix="xenogears-pc-audit-"))
    try:
        for f in files:
            if f["rel"] in EXECUTABLES:
                try:
                    passed.append(audit_executable(f, builds[f["rel"]], ver, len(images), scratch))
                except (ReleaseError, OSError) as error:
                    failed.append(str(error))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if os.path.lexists(scratch):
        failed.append(f"payload audit: the temporary folder {scratch} could not be removed")
    if not failed and fingerprint(inputs) != before:
        failed.append("payload audit: the auditor, the module list or an image changed during the audit")
    if failed or not passed:
        raise ReleaseError("\n".join(failed + [f"passed: {line}" for line in passed]
                                     + ["the payload audit did not pass every executable; nothing written"]))
    return passed


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


def package(tag: str, stage: Path, out_dir: Path, ver: str, build: Path,
            enhanced_build: Path | None = None) -> dict:
    """The tarball of one stage and its .sha256, written into out_dir as new files once every executable passed."""
    names = release_names(tag)
    files = read_stage(stage)
    packed = tarball_files(files, names["version"])
    if ver not in VERSIONS:
        raise ReleaseError(f"--ver {ver!r} is not one of {', '.join(VERSIONS)}; nothing written")
    staged = [f["rel"] for f in files if f["rel"] in EXECUTABLES]
    given = {"bin/xenogears": build, "bin/xenogears-enhanced": enhanced_build}
    if "bin/xenogears-enhanced" in staged and enhanced_build is None:
        raise ReleaseError("the stage holds bin/xenogears-enhanced: its audit needs --enhanced-build; nothing written")
    if "bin/xenogears-enhanced" not in staged and enhanced_build is not None:
        raise ReleaseError("--enhanced-build given, but the stage holds no bin/xenogears-enhanced; nothing written")
    builds = {rel: audit_build(given[rel], ver, rel) for rel in staged}
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
    audits = audit(files, builds, ver)
    schemas = check_schemas(files)
    try:
        unchanged(stage, files)
    except ReleaseError as error:
        raise ReleaseError(f"{error} (after its payload audit); nothing written") from error
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
            write_tarball(f, names["folder"], packed)
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
    return {"audit": audits, "schema": schemas, "tarball": str(final[0]), "sha256": str(final[1]), "digest": digest}


PACKAGE_SCOPE = ("the packaging step only: the Linux tarball and its .sha256 from a final installed stage, written "
                 "only after this checkout's tools/payloadcheck.py passed every executable in it; not the port's "
                 "compilation, dependency fetching, signing, the .deb or Windows assets, the SBOM, an upload, or an "
                 "install and start. The stage checks and the payload audit have limits (runs of the module images "
                 "in the executables, as that auditor finds them) and do not clear the assets for upload by "
                 "themselves")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("names", help="the version, folder and asset names of a tag")
    p.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    p = sub.add_parser("package", help="the tarball and its .sha256 (" + PACKAGE_SCOPE + ")",
                       description="package: " + PACKAGE_SCOPE + ". The stage is what cmake --install --prefix "
                       "<stage> writes for the port's Linux Release build: bin/xenogears (required), "
                       "bin/xenogears-enhanced (optional), each executable's state schema in share/xenogears-pc "
                       "and the licences in " + DOC + ". The two assets are "
                       "written into --out-dir as new files; it refuses a folder already holding either.")
    p.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N (N from 0)")
    p.add_argument("--stage", type=Path, required=True, help="the installed prefix, final (only read)")
    p.add_argument("--out-dir", type=Path, required=True, help="where the two assets go (made if missing)")
    p.add_argument("--ver", choices=VERSIONS, required=True,
                   help="the game version the engines were built for; its extracted module images are audited against")
    p.add_argument("--build", type=Path, required=True,
                   help="the configured CMake build folder bin/xenogears was linked in, configured with "
                        "-DXG_DIST=ON (only read)")
    p.add_argument("--enhanced-build", type=Path,
                   help="the same for bin/xenogears-enhanced: required when the stage holds it")
    args = ap.parse_args(argv)
    try:
        if args.command == "names":
            for key, value in release_names(args.tag).items():
                print(f"{key}={value}")
        else:
            result = package(args.tag, args.stage, args.out_dir, args.ver, args.build, args.enhanced_build)
            for key, value in result.items():
                for item in value if isinstance(value, list) else [value]:
                    print(f"{key}={item}")
            print(f"scope={PACKAGE_SCOPE}")
    except (ReleaseError, OSError) as error:
        print(f"linux_release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
