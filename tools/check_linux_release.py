#!/usr/bin/env python3
"""Checks packaging/linux_release.py (the Linux release tarball, its .sha256 and its payload audit).

    python tools/check_linux_release.py --ver us

The helper is copied into temporary public layouts (packaging/ and tools/ of a
fresh folder whose name has a space) and run from another folder with
python -I and a minimal environment, as a public checkout runs it. Its stages
are SYNTHETIC FIXTURES laid out by the install rules of port/cmake/deb.cmake.
Three kinds of test, kept apart:

  STUB AUDITOR (plumbing only). tools/payloadcheck.py of the layout is a stub
    that logs how it was run and answers as the test tells it; the
    "executables" are a bare x86-64 ELF header and a marker. They prove the
    packaging logic and the audit's plumbing (names, layout, modes, refusals,
    sums, cleanup, repeatability; the argv, the folder, the closed input, the
    order, each refusal), not the auditor.
  REAL AUDITOR, SYNTHETIC IMAGES. The repository's unchanged
    tools/payloadcheck.py, payload_pe.py, payload_elf.py and payload_allow.txt, with module
    images that are hash streams and executables compiled here from a few
    lines of C (one holding 96 bytes of a synthetic image): the real auditor
    passes and refuses through the helper.
  REAL MODULE LISTS, SYNTHETIC IMAGES. The real config/us and config/jp
    disc.yaml (text, no game bytes) with a synthetic file for every name: the
    helper's reading of each list is the unchanged auditor's (the same files,
    the same count, in process and through the command), a missing image is
    refused, entries of another form are refused.

Each staged executable has its state schema (share/xenogears-pc/<name>.xgschema,
port/CMakeLists.txt XG_STATE_SCHEMA). With the stub auditor the layout's
tools/state_schema.py is a STUB SCHEMA CHECK too (it logs how it was run and
answers as told); with the real auditor it is the repository's, and each
compiled executable gets a schema made here naming its image (its identity, as
the port computes it): one of another image, one naming an absolute path and an
executable that keeps debug information are refused.

None of this is a REAL build audited against a REAL disc: that needs a
player's disc and is not a gate. No game bytes are read or written here.

Each archive is opened and checked independently of the helper (names, types,
modes, owners, times, bytes); the .sha256 is checked against the archive's
bytes and by sha256sum -c when it is installed. Production guards are disabled
in copies of the helper, to show the checks here catch them. The assets do not
depend on the version (--ver is accepted for the gate's calling convention).
Exit status 1 on any failure.
"""
from __future__ import annotations

import argparse
import ast
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER = ROOT / "tools/publish/repo-port/packaging/linux_release.py"
AUDITOR_FILES = ("payloadcheck.py", "payload_pe.py", "payload_elf.py", "payload_allow.txt")
DOC = "share/doc/xenogears-pc"
RELEASE = "share/xenogears-pc/release.txt"  # what the in-app updater reads (port/src/system/release.h)
SCHEMA_DIR = "share/xenogears-pc"  # each executable's state schema, <name>.xgschema (port/cmake/deb.cmake)
SCHEMA_FILES = ("state_schema.py", "statecheck.py", "statecanon.py")  # the schema check and the modules it imports
LICENCES = ("COPYING", "LICENSE-Jukebox-font.txt", "LICENSE-imgui.txt", "LICENSE-glslang.txt", "LICENSE-dav1d.txt")
ENV = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}  # main() adds a private TMPDIR
FAILURES: list[str] = []

# SYNTHETIC FIXTURE: a module list in the form of config/<ver>/disc.yaml.
LIST = ("# SYNTHETIC FIXTURE module list\nfiles:\n"
        "  - {name: main, iso: FIXTURE.00, vram: 0x80010000, sha1: 0}\n"
        "  - {name: alpha, dir: 0x01, file: 0x0D, lzss: true, sha1: 0}\n"
        "  - {name: beta,  dir: 0x01, file: 0x0E, sha1: 0}\n")
LIST_FILES = ("main.exe", "alpha.bin", "beta.bin")

STUB = r'''#!/usr/bin/env python3
"""STUB AUDITOR: test plumbing only, not tools/payloadcheck.py. Logs how it was run, answers as told."""
import hashlib, json, os, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
CONTROL = Path(@CONTROL@)
a = sys.argv[1:]
exe, build, ver = a[a.index("--exe") + 1], a[a.index("--build") + 1], a[a.index("--ver") + 1]
mode = json.loads((CONTROL / "mode.json").read_text())
try:
    stdin = sys.stdin.read()
except Exception as error:
    stdin = repr(error)
with (CONTROL / "log.jsonl").open("a") as f:
    f.write(json.dumps({
        "argv": sys.argv, "cwd": os.getcwd(), "stdin": stdin,
        "flags": [sys.flags.ignore_environment, sys.flags.no_user_site, sys.flags.dont_write_bytecode],
        "sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(), "mode": os.stat(exe).st_mode & 0o777,
        "watch": [os.path.lexists(p) for p in mode.get("watch", [])]}) + "\n")
how = mode.get(os.path.basename(exe), "ok")
images = len(list((ROOT / "disc" / ver).iterdir())) - (how == "partial")
if how == "sleep":
    time.sleep(40)
if how == "crash":
    sys.exit(3)
if how == "usage":
    print("payload: no version")
    sys.exit(2)
if how == "mutate-copy":
    os.chmod(exe, 0o700)
    open(exe, "ab").write(b"changed")
if how == "mutate-stage":
    open(mode["stage_exe"], "ab").write(b"changed")
if how == "mutate-input":
    open(ROOT / "tools" / "payload_allow.txt", "a").write("# changed\n")
print(f"retail module data in {exe} ({'JP' if how == 'other-version' else ver.upper()}): {images} module images, "
      f"runs of >= {8 if how == 'short-runs' else 16} bytes")
if how == "no-summary":
    sys.exit(0)
if how in ("fail", "fail-exit0"):
    print("  generated      96 bytes     1 runs  gen/alpha.c")
    print("            exe+0x1000 96 bytes: " + "5ec2e7a1" * 6)
    print("payload FAIL: generated 1 runs/96 bytes, port 0/0, other 0/0; library 0/0; allowed 0/0, "
          "symbol names 0/0, src (game C's own data) 0/0")
    sys.exit(0 if how == "fail-exit0" else 1)
if how == "extra-line":
    print("  over budget: port/src/fixture.c: 40 bytes, tools/payload_allow.txt allows 20")
if how == "dirty-ok":
    print("payload ok: generated 0 runs/0 bytes, port 1/20, other 0/0; library 0/0; allowed 0/0, "
          "symbol names 0/0, src (game C's own data) 0/0")
    sys.exit(0)
print("payload ok: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; allowed 2/40, "
      "symbol names 3/60, src (game C's own data) 0/0")
'''


SCHEMA_STUB = r'''#!/usr/bin/env python3
"""STUB SCHEMA CHECK: test plumbing only, not tools/state_schema.py. Logs how it was run, answers as told."""
import hashlib, json, os, sys
from pathlib import Path
CONTROL = Path(@CONTROL@)
a = sys.argv[1:]
packed, image = a[a.index("--verify") + 1], a[a.index("--image") + 1]
mode = json.loads((CONTROL / "mode.json").read_text())
with (CONTROL / "schema.jsonl").open("a") as f:
    f.write(json.dumps({"argv": sys.argv, "cwd": os.getcwd(), "stdin": sys.stdin.read(),
                        "flags": [sys.flags.ignore_environment, sys.flags.no_user_site, sys.flags.dont_write_bytecode],
                        "image": hashlib.sha256(Path(image).read_bytes()).hexdigest(),
                        "schema": hashlib.sha256(Path(packed).read_bytes()).hexdigest()}) + "\n")
if mode.get("schema") == os.path.basename(image):
    print(f"state_schema: {os.path.basename(packed)}: it was read from another image (fixture)", file=sys.stderr)
    sys.exit(1)
print(f"state-schema: {os.path.basename(packed)} describes {os.path.basename(image)} (STUB), stripped")
'''


def fixture_schema(exe: str) -> bytes:
    """SYNTHETIC FIXTURE: a state schema's packed form ("XGSCHEMA", a size) holding no schema."""
    return b"XGSCHEMA" + bytes(8) + b"SYNTHETIC FIXTURE, not a state schema: " + exe.encode() + b"\n"


def real_schema(exe: Path) -> bytes:
    """A packed state schema naming exe's image (tools/state_schema.py image_identity, statecheck.schema_file):
    what tools/state_schema.py --verify reads of it."""
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import state_schema
        import statecheck
    finally:
        sys.path.remove(str(ROOT / "tools"))
    text = json.dumps({"version": "us", "exe": exe.name, "image": state_schema.image_identity(exe), "variables": []})
    return statecheck.schema_file(text.encode())


def check(ok: bool, what: str) -> bool:
    if not ok:
        FAILURES.append(what)
        print(f"FAIL: {what}")
    return bool(ok)


def fake_elf(marker: bytes, machine: int = 62, e_type: int = 3, interp: bool = True) -> bytes:
    """SYNTHETIC FIXTURE: an ELF64 header and one program header, then a marker; not a program."""
    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    head = ident + struct.pack("<HHIQQQIHHHHHH", e_type, machine, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    ph = struct.pack("<IIQQQQQQ", 3 if interp else 1, 4, 120, 0, 0, 0, 0, 1)
    return head + ph + b"SYNTHETIC FIXTURE, not a release executable: " + marker + b"\n"


def make_stage(path: Path, enhanced: bool = True, optional: bool = True, release: str | None = "2026.11.0") -> Path:
    """SYNTHETIC FIXTURE: the layout cmake --install gives the Linux build (port/cmake/deb.cmake); with release,
    the .deb's release.txt of that version (the release's stage is the .deb's files)."""
    (path / "bin").mkdir(parents=True)
    (path / DOC).mkdir(parents=True)
    if release is not None:
        (path / RELEASE).parent.mkdir(parents=True)
        (path / RELEASE).write_text(f"version={release}\nkind=deb\n", encoding="utf-8")
        os.chmod(path / RELEASE, 0o644)
    exes = ["xenogears"] + (["xenogears-enhanced"] if enhanced else [])
    (path / SCHEMA_DIR).mkdir(parents=True, exist_ok=True)
    for exe in exes:
        (path / "bin" / exe).write_bytes(fake_elf(exe.encode()))
        os.chmod(path / "bin" / exe, 0o755)
        (path / SCHEMA_DIR / f"{exe}.xgschema").write_bytes(fixture_schema(exe))
        os.chmod(path / SCHEMA_DIR / f"{exe}.xgschema", 0o644)
    for name in LICENCES if optional else LICENCES[:3]:
        (path / DOC / name).write_text(f"SYNTHETIC FIXTURE licence text: {name}\n", encoding="utf-8")
        os.chmod(path / DOC / name, 0o644)
    return path


def stream(seed: str, size: int = 4096) -> bytes:
    """SYNTHETIC FIXTURE: bytes of a hash stream, standing in for a module image."""
    out, i = b"", 0
    while len(out) < size:
        out += hashlib.sha256(f"SYNTHETIC FIXTURE {seed} {i}".encode()).digest()
        i += 1
    return out[:size]


def make_build(path: Path, ver: str = "US", engine: str = "original", dist: str | None = "ON",
               home: str | None = None) -> Path:
    """SYNTHETIC FIXTURE: what the helper needs of a configured CMake build folder (no objects); a build to
    hand out (XG_DIST) unless dist is None (unset) or another value."""
    (path / "CMakeFiles").mkdir(parents=True)
    (path / "CMakeCache.txt").write_text(f"# SYNTHETIC FIXTURE\nXG_ENGINE:STRING={engine}\nXG_VERSION:STRING={ver}\n"
                                         + (f"XG_DIST:BOOL={dist}\n" if dist is not None else "")
                                         + (f"CMAKE_HOME_DIRECTORY:INTERNAL={home}/port\n" if home else ""))
    return path


def make_layout(path: Path, auditor: str, control: Path | None = None, ver: str = "us", listing: str = LIST,
                images: tuple[str, ...] = LIST_FILES) -> Path:
    """A public checkout's files the package command reads: the helper, an auditor ("stub" or "real": the
    repository's unchanged files), a module list and SYNTHETIC images."""
    (path / "packaging").mkdir(parents=True)
    shutil.copy2(HELPER, path / "packaging" / "linux_release.py")
    (path / "tools").mkdir()
    if auditor == "stub":
        (path / "tools" / "payloadcheck.py").write_text(STUB.replace("@CONTROL@", repr(str(control))))
        (path / "tools" / "payload_pe.py").write_text("# STUB AUDITOR's module: plumbing only\n")
        (path / "tools" / "payload_elf.py").write_text("# STUB AUDITOR's module: plumbing only\n")
        (path / "tools" / "payload_allow.txt").write_text("# STUB AUDITOR's allowance file: plumbing only\n")
        (path / "tools" / "state_schema.py").write_text(SCHEMA_STUB.replace("@CONTROL@", repr(str(control))))
        (path / "tools" / "statecheck.py").write_text("# STUB SCHEMA CHECK's module: plumbing only\n")
        (path / "tools" / "statecanon.py").write_text("# STUB SCHEMA CHECK's module: plumbing only\n")
    else:
        for name in AUDITOR_FILES + SCHEMA_FILES:
            shutil.copy2(ROOT / "tools" / name, path / "tools" / name)
    (path / "config" / ver).mkdir(parents=True)
    (path / "config" / ver / "disc.yaml").write_text(listing)
    (path / "disc" / ver).mkdir(parents=True)
    for name in images:
        (path / "disc" / ver / name).write_bytes(stream(f"image {name}"))
    return path


class Stub:
    """The STUB AUDITOR's control folder: what it answers (by executable name) and what it logged."""

    def __init__(self, control: Path):
        self.control = control
        control.mkdir()
        self.set()

    def set(self, **mode) -> None:
        (self.control / "mode.json").write_text(json.dumps({k.replace("_enh", "-enhanced"): v
                                                            for k, v in mode.items()}))
        (self.control / "log.jsonl").write_text("")
        (self.control / "schema.jsonl").write_text("")

    def log(self) -> list[dict]:
        return [json.loads(line) for line in (self.control / "log.jsonl").read_text().splitlines()]

    def schema_log(self) -> list[dict]:
        """The STUB SCHEMA CHECK's runs since the last set()."""
        p = self.control / "schema.jsonl"
        return [json.loads(line) for line in p.read_text().splitlines()] if p.exists() else []


def snapshot(path: Path, root: bool = True) -> dict | None:
    """Every entry under path (and path itself if root): its lstat mode, time, and bytes or link target."""
    if not os.path.lexists(path):
        return None
    st = path.lstat()
    out = {".": (st.st_mode, st.st_mtime_ns)} if root else {}
    if not path.is_dir() or path.is_symlink():
        return out | {"": (path.read_bytes() if path.is_file() else None,)}
    for here, dirs, files in os.walk(path):
        for name in dirs + files:
            p = Path(here) / name
            st = p.lstat()
            if stat.S_ISLNK(st.st_mode):
                body = ("link", os.readlink(p))
            elif stat.S_ISREG(st.st_mode):
                body = ("file", p.read_bytes())
            else:
                body = ("other",)
            out[p.relative_to(path).as_posix()] = (st.st_mode, st.st_mtime_ns, body)
    return out


class Cli:
    def __init__(self, helper: Path, cwd: Path, env: dict | None = None, isolated: bool = True):
        self.helper, self.cwd, self.env, self.isolated = helper, cwd, env, isolated

    def __call__(self, *args: str | Path) -> subprocess.CompletedProcess:
        python = [sys.executable, "-I"] if self.isolated else [sys.executable]
        return subprocess.run([*python, str(self.helper), *map(str, args)], cwd=self.cwd, env=self.env or ENV,
                              capture_output=True, text=True, timeout=300)


def names_of(version: str) -> tuple[str, str, str]:
    folder = f"xenogears-pc-{version}-linux-x86_64"
    return folder, f"{folder}.tar.gz", f"{folder}.tar.gz.sha256"


def inspect(archive: Path, stage: Path, folder: str, label: str) -> None:
    """The archive, read without the helper: exactly the stage's files under folder, with the promised metadata."""
    raw = archive.read_bytes()
    check(raw[:3] == b"\x1f\x8b\x08", f"{label}: gzip magic")
    check(raw[3] & 0x08 == 0 and raw[4:8] == bytes(4), f"{label}: gzip header has a name or a time")
    want = {folder: ("dir", 0o755, None)}
    for here, dirs, files in os.walk(stage):
        for name in dirs:
            want[f"{folder}/{(Path(here) / name).relative_to(stage).as_posix()}"] = ("dir", 0o755, None)
        for name in files:
            rel = (Path(here) / name).relative_to(stage).as_posix()
            want[f"{folder}/{rel}"] = ("file", 0o755 if rel.startswith("bin/") else 0o644,
                                       (Path(here) / name).read_bytes())
    # The tarball's own release.txt, whatever the stage's: its version, kind=tarball.
    version = folder.removeprefix("xenogears-pc-").removesuffix("-linux-x86_64")
    want[f"{folder}/share/xenogears-pc"] = ("dir", 0o755, None)
    want[f"{folder}/{RELEASE}"] = ("file", 0o644, f"version={version}\nkind=tarball\n".encode())
    seen = []
    with tarfile.open(archive, "r:gz") as tar:
        for m in tar.getmembers():
            seen.append(m.name)
            parts = m.name.split("/")
            check(not m.name.startswith("/") and ".." not in parts and "." not in parts and parts[0] == folder,
                  f"{label}: unsafe or rootless name {m.name!r}")
            check(m.isdir() or m.isfile(), f"{label}: {m.name} is neither a folder nor a regular file")
            check(m.uid == 0 and m.gid == 0 and m.uname == "" and m.gname == "", f"{label}: {m.name} owner")
            check(m.mtime == 0, f"{label}: {m.name} time {m.mtime}")
            check(not m.pax_headers, f"{label}: {m.name} pax headers {m.pax_headers}")
            exp = want.get(m.name)
            if not check(exp is not None, f"{label}: {m.name} is not in the stage"):
                continue
            check(("dir" if m.isdir() else "file") == exp[0], f"{label}: {m.name} type")
            check(stat.S_IMODE(m.mode) == exp[1], f"{label}: {m.name} mode {oct(m.mode)}, not {oct(exp[1])}")
            if m.isfile():
                check(tar.extractfile(m).read() == exp[2], f"{label}: {m.name} bytes differ from the stage's")
    check(sorted(seen) == sorted(want), f"{label}: members {sorted(seen)} are not the stage's {sorted(want)}")
    check(len(seen) == len(set(seen)), f"{label}: a member appears twice")
    tool = shutil.which("tar")
    if tool:  # a second reader: GNU tar's listing, its modes and owners
        r = subprocess.run([tool, "--numeric-owner", "-tvzf", str(archive)], capture_output=True, text=True,
                           env=ENV)
        lines = r.stdout.splitlines()
        check(r.returncode == 0 and len(lines) == len(want), f"{label}: tar -tvzf: {r.returncode} {r.stderr}")
        for line in lines:
            fields = line.split()
            name = fields[-1].rstrip("/")
            exp = want.get(name)
            mode = {("dir", 0o755): "drwxr-xr-x", ("file", 0o755): "-rwxr-xr-x", ("file", 0o644): "-rw-r--r--"}
            check(exp is not None and fields[0] == mode[exp[:2]] and fields[1] == "0/0",
                  f"{label}: tar lists {line!r}")
    else:
        print(f"note: no tar here; {label} read by Python's tarfile only")


def check_sidecar(out: Path, tarball: str, sidecar: str, label: str) -> None:
    digest = hashlib.sha256((out / tarball).read_bytes()).hexdigest()
    check((out / sidecar).read_bytes() == f"{digest}  {tarball}\n".encode(), f"{label}: .sha256 is not the archive's")
    tool = shutil.which("sha256sum")
    if tool:
        r = subprocess.run([tool, "-c", sidecar], cwd=out, capture_output=True, text=True)
        check(r.returncode == 0, f"{label}: sha256sum -c failed: {r.stdout}{r.stderr}")
    else:
        print(f"note: no sha256sum here; {label}'s sum checked by Python only")


def refused(cli: Cli, label: str, args: list, watch: list[Path], needle: str | None = None) -> str:
    before = [snapshot(p) for p in watch]
    r = cli(*args)
    check(r.returncode == 1, f"{label}: exit {r.returncode}, not 1 (stdout {r.stdout!r})")
    check(r.stderr.startswith("linux_release: "), f"{label}: no error message ({r.stderr!r})")
    if needle is not None:
        check(needle in r.stderr, f"{label}: {needle!r} not in {r.stderr!r}")
    for path, snap in zip(watch, before):
        check(snapshot(path) == snap, f"{label}: {path} changed")
    return r.stderr


def static_checks() -> None:
    text = HELPER.read_text(encoding="utf-8")
    for node in ast.walk(ast.parse(text)):
        mods = []
        if isinstance(node, ast.Import):
            mods = [a.name for a in node.names]
        elif isinstance(node, ast.ImportFrom):
            mods = [node.module or ""] if node.level == 0 else ["(relative)"]
        for mod in mods:
            check(mod.split(".")[0] in sys.stdlib_module_names, f"helper imports {mod}, not the standard library")
    check(not re.search(r"20[0-9]{2}[.-](0[1-9]|1[0-2])[.-][0-9]", text), "helper holds a dated example")
    check(not re.search(r"/home/|@[a-z0-9-]+\.[a-z]{2,}|\bdocs/|\bwork/|\bdisc/", text),
          "helper holds a personal path, an address, or a private folder")
    check("shell=" not in text and "os.system" not in text and "os.popen" not in text, "helper may use a shell")


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_inprocess(helper: Path, argv: list[str], patch) -> tuple[int, str]:
    """The helper's main() in this process with one function replaced (an induced failure)."""
    mod = load(helper, "linux_release_under_test")
    patch(mod)
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        code = mod.main(argv)
    return code, err.getvalue()


def mutant(base: Path, layout: Path, name: str, old: str, new: str) -> Cli:
    """A copy of a public layout whose helper has one guard replaced."""
    text = HELPER.read_text(encoding="utf-8")
    check(text.count(old) == 1, f"mutant {name}: the guard {old!r} is not in the helper exactly once")
    copy = base / f"mutant-{name}" / layout.name
    shutil.copytree(layout, copy, symlinks=True)
    (copy / "packaging" / "linux_release.py").write_text(text.replace(old, new), encoding="utf-8")
    run = base / f"mutant-{name}-cwd"
    run.mkdir()
    return Cli(copy / "packaging" / "linux_release.py", run)


def variant(base: Path, layout: Path, name: str, change) -> Cli:
    """A copy of a public layout with something of the audit's inputs changed."""
    copy = base / f"variant-{name}" / layout.name
    shutil.copytree(layout, copy, symlinks=True)
    change(copy)
    run = base / f"variant-{name}-cwd"
    run.mkdir()
    return Cli(copy / "packaging" / "linux_release.py", run)


def hostile(base: Path, name: str) -> tuple[Path, dict, Path]:
    """A working folder, a PATH and a PYTHONPATH holding files named as the auditor's; each leaves a mark if run."""
    mark = base / f"{name}-hostile-ran"
    code = f"open({str(mark)!r}, 'a').write(__file__ + '\\n')\n"
    cwd = base / f"{name} hostile cwd"
    for rel in ("payloadcheck.py", "payload_pe.py", "payload_elf.py", "tools/payloadcheck.py", "tools/payload_pe.py",
                "tools/payload_elf.py",
                "packaging/linux_release.py"):
        (cwd / rel).parent.mkdir(parents=True, exist_ok=True)
        (cwd / rel).write_text(code)
    path = base / f"{name} hostile path"
    path.mkdir()
    for exe in ("payloadcheck", "payloadcheck.py"):
        (path / exe).write_text(f"#!/bin/sh\necho {exe} >> '{mark}'\n")
        os.chmod(path / exe, 0o755)
    pythonpath = base / f"{name} hostile pythonpath"
    pythonpath.mkdir()
    for mod in ("payloadcheck.py", "payload_pe.py", "payload_elf.py"):
        (pythonpath / mod).write_text(code)
    env = ENV | {"PATH": f"{path}:{ENV['PATH']}", "PYTHONPATH": str(pythonpath)}
    return cwd, env, mark


def packaging_tests(base: Path, layout: Path, stub: Stub) -> None:
    """STUB AUDITOR: the packaging logic and the audit's plumbing."""
    helper = layout / "packaging" / "linux_release.py"
    auditor = layout / "tools" / "payloadcheck.py"
    elsewhere = base / "elsewhere"
    elsewhere.mkdir()
    cli = Cli(helper, elsewhere)
    b1 = make_build(base / "build original")
    b2 = make_build(base / "build enhanced", engine="enhanced")
    one = ["--ver", "us", "--build", b1]
    both = [*one, "--enhanced-build", b2]

    r = cli("--help")
    check(r.returncode == 0 and "package" in r.stdout and "names" in r.stdout, f"--help: {r.stderr}")
    r = cli("package", "--help")
    check(r.returncode == 0 and all(a in r.stdout for a in ("--tag", "--stage", "--out-dir", "--ver", "--build",
                                                            "--enhanced-build")),
          "package --help lacks --tag/--stage/--out-dir/--ver/--build/--enhanced-build")
    flat = " ".join(r.stdout.split())
    check("payload audit" in flat and "do not clear the assets for upload" in flat,
          "package --help does not say the payload audit does not clear the assets for upload")
    check("tools/payloadcheck.py" in flat, "package --help does not name the auditor")
    check("-DXG_DIST=ON" in flat, "package --help does not say the builds are configured with -DXG_DIST=ON")
    check(stub.log() == [], "--help ran the auditor")

    # Names: the tag's spelling kept, counter zero and later counters accepted.
    stage = make_stage(base / "stage dir")
    stage_before = snapshot(stage)
    for tag in ("v2026.11.0", "v2026.12.7", "v2027.01.10", "v2099.10.123"):
        r = cli("names", "--tag", tag)
        folder, tarball, sidecar = names_of(tag[1:])
        check(r.returncode == 0 and f"tarball={tarball}\n" in r.stdout and f"version={tag[1:]}\n" in r.stdout,
              f"names {tag}: {r.stdout!r} {r.stderr!r}")
    bad_tags = ("v2026.11.00", "v2026.11.01", "v2026.13.0", "v2026.00.0", "v2026.1.0", "2026.11.0", "V2026.11.0",
                "v1999.11.0", "v2026.11.-1", "v2026.11.0 ", " v2026.11.0", "v2026.11", "v2026.11.0.1",
                "v2026.11.٣", "v２026.11.0", "v2026.11.0-rc1", "v2026.11.0/..", "")
    for tag in bad_tags:
        out = base / "out-bad-tag"
        refused(cli, f"tag {tag!r}", ["package", "--tag", tag, "--stage", stage, "--out-dir", out, *both],
                [out, stage], repr(tag))
        check(not os.path.lexists(out), f"tag {tag!r}: --out-dir made")
    r = cli("names", "--tag", "v2026.11." + "9" * 300)
    check(r.returncode == 1, "a tag too long for a file name is accepted")
    check(stub.log() == [], "names or a refused tag ran the auditor")

    # The release: both engines, counter zero, into a folder holding an unrelated file.
    out = base / "release out"
    out.mkdir()
    (out / "notes.txt").write_bytes(b"an unrelated file\n")
    (out / "xenogears-pc-2026.11.0.tar.gz").write_bytes(b"the source tarball, unrelated\n")
    others = {n: (out / n).read_bytes() for n in ("notes.txt", "xenogears-pc-2026.11.0.tar.gz")}
    folder, tarball, sidecar = names_of("2026.11.0")
    stub.set(watch=[str(out / tarball), str(out / sidecar)])
    r = cli("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", out, *both)
    if check(r.returncode == 0, f"package v2026.11.0: {r.stderr}"):
        check(f"tarball={out / tarball}\n" in r.stdout and f"sha256={out / sidecar}\n" in r.stdout,
              f"package output {r.stdout!r}")
        check(sorted(os.listdir(out)) == sorted([*others, tarball, sidecar]),
              f"--out-dir holds {sorted(os.listdir(out))}, not the two assets and the earlier files")
        check(all((out / n).read_bytes() == b for n, b in others.items()), "an earlier file changed")
        check(stat.S_IMODE((out / tarball).stat().st_mode) == 0o644, "tarball file mode")
        inspect(out / tarball, stage, folder, "v2026.11.0")
        check_sidecar(out, tarball, sidecar, "v2026.11.0")
        with tarfile.open(out / tarball, "r:gz") as tar:
            names = tar.getnames()
        check(f"{folder}/bin/xenogears" in names and f"{folder}/bin/xenogears-enhanced" in names,
              "both engines not packaged")
        # The plumbing: each archived executable audited once, in order, exactly so.
        log = stub.log()
        check([Path(e["argv"][2]).name for e in log] == ["xenogears", "xenogears-enhanced"],
              f"both engines: audited {[e['argv'][2] for e in log]}")
        for e, exe, build in zip(log, ("xenogears", "xenogears-enhanced"), (b1, b2)):
            copy = Path(e["argv"][2])
            check(e["argv"] == [str(auditor), "--exe", str(copy), "--build", str(build), "--ver", "us", "--dist"],
                  f"{exe}: auditor argv {e['argv']}")
            check(copy.parent.parent == Path(ENV["TMPDIR"]) and not copy.exists(),
                  f"{exe}: the audited copy {copy} is not in a private temporary folder, or is left")
            check(e["sha256"] == hashlib.sha256((stage / "bin" / exe).read_bytes()).hexdigest(),
                  f"{exe}: the audited bytes are not the stage's")
            check(e["mode"] == 0o500, f"{exe}: the audited copy's mode {oct(e['mode'])}")
            check(e["flags"] == [1, 1, 1], f"{exe}: the auditor's python flags -E -s -B: {e['flags']}")
            check(e["cwd"] == str(layout), f"{exe}: the auditor ran in {e['cwd']}, not the checkout")
            check(e["stdin"] == "", f"{exe}: the auditor's input is not closed ({e['stdin']!r})")
            check(e["watch"] == [False, False], f"{exe}: an asset was written before the audits finished")
        lines = [line for line in r.stdout.splitlines() if line.startswith("audit=")]
        check(len(lines) == 2 and "bin/xenogears (US" in lines[0] and "bin/xenogears-enhanced (US" in lines[1]
              and all("all 3 module images read; payload ok:" in line for line in lines),
              f"the audits are not reported: {lines}")
        # Each state schema checked once against its executable's bytes, after the audits, exactly so.
        checker = layout / "tools" / "state_schema.py"
        runs = stub.schema_log()
        check([Path(e["argv"][2]).name for e in runs] == ["xenogears.xgschema", "xenogears-enhanced.xgschema"],
              f"both schemas: checked {[e['argv'] for e in runs]}")
        for e, exe in zip(runs, ("xenogears", "xenogears-enhanced")):
            packed, image = Path(e["argv"][2]), Path(e["argv"][4])
            check(e["argv"] == [str(checker), "--verify", str(packed), "--image", str(image), "--stripped"]
                  and image.name == exe, f"{exe}: schema check argv {e['argv']}")
            check(packed.parent == image.parent and image.parent.parent == Path(ENV["TMPDIR"]) and not image.exists(),
                  f"{exe}: the checked copies are not in a private temporary folder, or are left")
            check(e["image"] == hashlib.sha256((stage / "bin" / exe).read_bytes()).hexdigest()
                  and e["schema"] == hashlib.sha256((stage / SCHEMA_DIR / f"{exe}.xgschema").read_bytes()).hexdigest(),
                  f"{exe}: the checked bytes are not the stage's")
            check(e["flags"] == [1, 1, 1] and e["cwd"] == str(layout) and e["stdin"] == "",
                  f"{exe}: the schema check ran with {e['flags']} in {e['cwd']}, input {e['stdin']!r}")
        lines = [line for line in r.stdout.splitlines() if line.startswith("schema=")]
        check(len(lines) == 2 and all("describes" in line for line in lines), f"the schema checks are not reported: {lines}")
    check(snapshot(stage) == stage_before, "the stage changed while it was packaged")

    # Repeatable: same content, other times and another folder give the same bytes.
    copy = base / "stage-copy"
    shutil.copytree(stage, copy)
    for i, p in enumerate(sorted(copy.rglob("*"))):
        os.utime(p, (1_000_000 + i * 7777, 2_000_000 + i * 9999), follow_symlinks=False)
    os.utime(copy, (5, 5))
    check(snapshot(copy) != stage_before, "the copy's times were not changed")
    again = base / "again"
    r = cli("package", "--tag", "v2026.11.0", "--stage", copy, "--out-dir", again, *both)
    if check(r.returncode == 0, f"package again: {r.stderr}") and (out / tarball).is_file():
        check((again / tarball).read_bytes() == (out / tarball).read_bytes(),
              "same stage content, other times: different tarball bytes")
        check((again / sidecar).read_bytes() == (out / sidecar).read_bytes(), "different .sha256")
        check(sorted(os.listdir(again)) == sorted([tarball, sidecar]), "made more than the two assets")

    # A later counter, and the original engine alone with no optional licence; --build relative to the cwd.
    solo = make_stage(base / "stage-solo", enhanced=False, optional=False, release=None)  # no release.txt: one written
    f2, t2, s2 = names_of("2026.12.7")
    out2 = base / "nested" / "release-later"
    stub.set(watch=[str(out2)])
    r = cli("package", "--tag", "v2026.12.7", "--stage", solo, "--out-dir", out2, "--ver", "us",
            "--build", os.path.relpath(b1, elsewhere))
    if check(r.returncode == 0, f"package v2026.12.7: {r.stderr}"):
        inspect(out2 / t2, solo, f2, "v2026.12.7 original only")
        check_sidecar(out2, t2, s2, "v2026.12.7")
        check(sorted(os.listdir(out2)) == sorted([t2, s2]), "later counter: not exactly two assets")
        log = stub.log()
        check(len(log) == 1 and Path(log[0]["argv"][2]).name == "xenogears" and log[0]["argv"][4] == str(b1)
              and log[0]["watch"] == [False], f"original only: audited {log}")
    stub.set()

    # The audit's arguments: each engine's build is required, and only then.
    for label, stg, args, needle in (
            ("enhanced staged, no --enhanced-build", stage, one, "needs --enhanced-build"),
            ("--enhanced-build, none staged", solo, both, "holds no bin/xenogears-enhanced"),
            ("--build is not a folder", stage, ["--ver", "us", "--build", base / "no build", "--enhanced-build", b2],
             "not a configured CMake build folder"),
            ("--enhanced-build is not a build", stage, [*one, "--enhanced-build", solo],
             "not a configured CMake build folder"),
            ("a JP build under --ver us", stage, ["--ver", "us", "--build", make_build(base / "build jp", ver="JP"),
                                                  "--enhanced-build", b2], "XG_VERSION JP, not us"),
            ("the builds swapped", stage, ["--ver", "us", "--build", b2, "--enhanced-build", b1], "XG_ENGINE"),
            ("--ver jp, no JP list", stage, ["--ver", "jp", "--build", make_build(base / "build jp2", ver="JP"),
                                             "--enhanced-build", make_build(base / "build jp3", "JP", "enhanced")],
             "no module list")):
        o = base / "out-args"
        refused(cli, label, ["package", "--tag", "v2026.11.0", "--stage", stg, "--out-dir", o, *args], [stg], needle)
        check(not os.path.lexists(o), f"{label}: --out-dir made")
    for label, args in (("no --build", ["--ver", "us"]), ("no --ver", ["--build", b1, "--enhanced-build", b2]),
                        ("--ver xx", ["--ver", "xx", "--build", b1])):
        r = cli("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", base / "out-args", *args)
        check(r.returncode == 2 and not os.path.lexists(base / "out-args"), f"{label}: exit {r.returncode}")
    check(stub.log() == [], "a refused argument ran the auditor")

    # The auditor's answers: anything but a clean pass of every engine writes nothing.
    old = base / "old release"
    old.mkdir()
    (old / "xenogears-pc-2026.10.3-linux-x86_64.tar.gz").write_bytes(b"an earlier release\n")
    hexrun = "5ec2e7a1" * 6
    for exe in ("xenogears", "xenogears_enh"):
        stub.set(**{exe: "fail"})
        err = refused(cli, f"{exe} rejected", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", old,
                                                *both], [old, stage], "payload FAIL: generated 1 runs/96 bytes")
        check(len(stub.log()) == 2, f"{exe} rejected: not every engine was audited ({len(stub.log())})")
        check(hexrun not in err and "[bytes withheld]" in err, f"{exe} rejected: the found bytes are relayed: {err!r}")
        check("passed: bin/xenogears" in err and "nothing written" in err, f"{exe} rejected: verdicts not told")
    for how, needle in (("crash", "exited 3"), ("usage", "exited 2"), ("no-summary", "not its heading and one clean"),
                        ("fail-exit0", "not its heading and one clean"), ("dirty-ok", "not its heading and one clean"),
                        ("extra-line", "not its heading and one clean"), ("partial", "read 2 module images, not all 3"),
                        ("other-version", "another executable or version"), ("short-runs", "runs of 8 bytes"),
                        ("mutate-copy", "audited copy changed"), ("mutate-input", "changed during the audit")):
        for exe in ("xenogears", "xenogears_enh"):
            v = variant(base, layout, f"{how}-{exe}", lambda copy: None)  # mutate-input writes into its layout
            stub.set(**{exe: how})
            o = base / "out-answers"
            refused(v, f"auditor {how} for {exe}", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", o,
                                                    *both], [stage, old], needle)
            check(not os.path.lexists(o), f"auditor {how} for {exe}: --out-dir made")
    for exe in ("xenogears", "xenogears-enhanced"):
        stub.set(schema=exe)
        o = base / "out-schema"
        err = refused(cli, f"{exe}'s schema refused", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                                        "--out-dir", o, *both], [stage, old], "did not all pass")
        check(not os.path.lexists(o) and len(stub.log()) == 2 and len(stub.schema_log()) == 2,
              f"{exe}'s schema refused: --out-dir made, or not every engine and schema checked")
        check(f"share/xenogears-pc/{exe}.xgschema FAILED against bin/{exe}" in err and "another image" in err
              and str(Path(ENV["TMPDIR"])) not in err, f"{exe}'s schema refused: {err!r}")
    victim = make_stage(base / "stage-victim")
    stub.set(xenogears_enh="mutate-stage", stage_exe=str(victim / "bin" / "xenogears"))
    refused(cli, "stage changed during the audit", ["package", "--tag", "v2026.11.0", "--stage", victim, "--out-dir",
                                                    base / "out-victim", *both], [old], "after its payload audit")
    check(not os.path.lexists(base / "out-victim"), "stage changed during the audit: --out-dir made")

    # A time-out (the helper's limit shortened in a copy; the stub sleeps) and a launch error.
    m = mutant(base, layout, "timeout", "AUDIT_TIMEOUT = 3600", "AUDIT_TIMEOUT = 2")
    stub.set(xenogears="sleep")
    start = time.monotonic()
    refused(m, "auditor time-out", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir",
                                    base / "out-timeout", *both], [stage], "no verdict in 2 s")
    check(time.monotonic() - start < 30, "auditor time-out: the helper waited for the auditor")
    check(not os.path.lexists(base / "out-timeout") and len(stub.log()) == 2, "auditor time-out: assets, or one audit")
    stub.set()

    def no_launch(mod) -> None:
        def popen(*args, **kwargs):
            raise OSError("induced launch failure")
        mod.subprocess = types.SimpleNamespace(Popen=popen, DEVNULL=subprocess.DEVNULL, PIPE=subprocess.PIPE,
                                               TimeoutExpired=subprocess.TimeoutExpired)
    code, err = run_inprocess(helper, ["package", "--tag", "v2026.11.0", "--stage", str(stage), "--out-dir",
                                       str(base / "out-launch"), *map(str, both)], no_launch)
    check(code == 1 and "could not be started (induced launch failure)" in err and "nothing written" in err
          and not os.path.lexists(base / "out-launch"), f"launch error: {code} {err!r}")

    # Missing auditor or inputs: refused before any audit.
    def drop(rel: str):
        return lambda copy: (copy / rel).unlink()

    def text(rel: str, new: str):
        return lambda copy: (copy / rel).write_text(new)

    def commented(copy: Path) -> None:
        (copy / "config/us/disc.yaml").write_text(LIST + "  # - {name: gamma, dir: 0x01, file: 0x0F, sha1: 0}\n")

    for name, change, needle in (
            ("no-auditor", drop("tools/payloadcheck.py"), "payloadcheck.py"),
            ("no-auditor-module", drop("tools/payload_pe.py"), "payload_pe.py"),
            ("no-allowance", drop("tools/payload_allow.txt"), "payload_allow.txt"),
            ("helper-alone", lambda copy: shutil.rmtree(copy / "tools"), "from a checkout of the port"),
            ("no-list", drop("config/us/disc.yaml"), "no module list"),
            ("empty-list", text("config/us/disc.yaml", "files:\n"), "lists no module"),
            ("block-list", text("config/us/disc.yaml", LIST + "  - name: gamma\n    dir: 0x01\n"), "4 name: keys but 3"),
            ("spaced-entry", text("config/us/disc.yaml", LIST + "  - { name: gamma, dir: 0x01}\n"), "4 name: keys but 3"),
            ("twice", text("config/us/disc.yaml", LIST + LIST.splitlines()[-1] + "\n"), "twice"),
            ("commented-entry", commented, "1 of the 4 module images"),
            ("no-images", lambda copy: shutil.rmtree(copy / "disc"), "3 of the 3 module images"),
            ("zero-images", lambda copy: [p.unlink() for p in (copy / "disc/us").iterdir()], "3 of the 3"),
            ("one-image-missing", drop("disc/us/beta.bin"), "1 of the 3 module images"),
            ("main-missing", drop("disc/us/main.exe"), "main.exe"),
            ("empty-image", text("disc/us/alpha.bin", ""), "alpha.bin"),
            ("wrong-suffix", lambda copy: (copy / "disc/us/main.exe").rename(copy / "disc/us/main.bin"), "main.exe")):
        v = variant(base, layout, name, change)
        o = base / "out-inputs"
        refused(v, f"inputs {name}", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", o, *both],
                [stage], needle)
        check(not os.path.lexists(o), f"inputs {name}: --out-dir made")
    check(stub.log() == [], "the auditor ran with a missing input")

    # No substitution: files named as the auditor's in the working folder, on PATH and PYTHONPATH never run.
    cwd, env, mark = hostile(base, "stub")
    r = Cli(helper, cwd, env=env, isolated=False)("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir",
                                                  base / "out hostile", *both)
    log = stub.log()
    check(r.returncode == 0 and len(log) == 2, f"hostile folders: {r.returncode} {r.stderr}")
    check(not mark.exists(), f"hostile folders: a substitute ran ({mark.read_text() if mark.exists() else ''!r})")
    check(all(e["argv"][0] == str(auditor) and e["cwd"] == str(layout) and e["flags"] == [1, 1, 1] for e in log),
          "hostile folders: the auditor is not the checkout's, in the checkout, with -E -s -B")
    stub.set()

    # Bad stages: each refused, nothing written, the stage untouched.
    def bad(label: str, change, needle: str | None = None) -> None:
        st = make_stage(base / f"bad-{label}")
        change(st)
        o = base / f"bad-{label}-out"
        refused(cli, f"stage {label}", ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", o, *both],
                [st], needle)
        check(not os.path.lexists(o), f"stage {label}: --out-dir made")

    def put(rel: str, data: bytes, mode: int = 0o644):
        def change(st: Path) -> None:
            (st / rel).parent.mkdir(parents=True, exist_ok=True)
            (st / rel).write_bytes(data)
            os.chmod(st / rel, mode)
        return change

    def link(rel: str, target: str):
        def change(st: Path) -> None:
            if os.path.lexists(st / rel):
                (st / rel).unlink()
            os.symlink(target, st / rel)
        return change

    bad("no-exe", lambda st: (st / "bin/xenogears").unlink(), "bin/xenogears")
    bad("no-schema", lambda st: (st / SCHEMA_DIR / "xenogears.xgschema").unlink(), "with its state schema")
    bad("no-enhanced-schema", lambda st: (st / SCHEMA_DIR / "xenogears-enhanced.xgschema").unlink(),
        "xenogears-enhanced.xgschema")
    bad("schema-alone", lambda st: (st / "bin/xenogears-enhanced").unlink(), "without its executable")
    bad("plain-schema", put(f"{SCHEMA_DIR}/xenogears.xgschema", b'{"version": "us"}\n'), "packed form")
    bad("schema-elsewhere", put("share/xenogears-pc/xenogears-debug.xgschema", fixture_schema("xenogears-debug")),
        "not a file the port's install makes")
    bad("no-copying", lambda st: (st / DOC / "COPYING").unlink(), "COPYING")
    bad("no-imgui", lambda st: (st / DOC / "LICENSE-imgui.txt").unlink(), "LICENSE-imgui.txt")
    bad("script-exe", put("bin/xenogears", b"#!/bin/sh\necho fixture\n", 0o755), "not a 64-bit")
    bad("arm-exe", put("bin/xenogears", fake_elf(b"arm", machine=183), 0o755), "not x86-64")
    bad("library-exe", put("bin/xenogears-enhanced", fake_elf(b"lib", interp=False), 0o755), "not an executable")
    bad("object-exe", put("bin/xenogears", fake_elf(b"obj", e_type=1), 0o755), "not an executable")
    bad("exe-mode", lambda st: os.chmod(st / "bin/xenogears", 0o644), "not executable")
    bad("binary-licence", put(f"{DOC}/COPYING", b"\x00\x01\x02"), "licence")
    bad("psx-licence", put(f"{DOC}/LICENSE-dav1d.txt", b"PS-X EXE" + bytes(64)), "PlayStation")
    bad("cd-licence", put(f"{DOC}/COPYING", b"\x00" + b"\xff" * 10 + b"\x00" + bytes(2340)), "PlayStation")
    bad("disc-image", put(f"{DOC}/xenogears.cue", b"FILE fixture BINARY\n"), "game data")
    bad("disc-bin", put("bin/disc.bin", b"fixture"), "game data")
    bad("disc-exe", put("SLUS_006.64", b"fixture"), "game data")
    bad("memory-card", put("share/xenogears.mcd", b"fixture"), "game data")
    bad("settings", put("settings.ini", b"[fixture]\n"), "player")
    bad("build-artefact", put("CMakeCache.txt", b"fixture\n"), "not a file the port's install makes")
    bad("unknown-exe", put("bin/xenogears-debug", fake_elf(b"dbg"), 0o755), "not a file the port's install makes")
    bad("extra-folder", lambda st: (st / "lib").mkdir(), "not one the port's install makes")
    bad("escaping-link", link("bin/xenogears-enhanced", "/bin/sh"), "symbolic link")
    bad("escaping-relative-link", link(f"{DOC}/LICENSE-host.txt", "../../../../etc/hostname"), "symbolic link")
    bad("internal-link", link(f"{DOC}/LICENSE-dav1d.txt", "COPYING"), "symbolic link")
    bad("dangling-link", link("bin/xenogears-enhanced", "missing"), "symbolic link")
    bad("folder-link", lambda st: shutil.rmtree(st / "share/doc") or os.symlink("/usr/share/doc", st / "share/doc"),
        "symbolic link")
    bad("release-other-version", put(RELEASE, b"version=2026.11.1\nkind=deb\n"), "names another version")
    bad("release-arch", put(RELEASE, b"version=2026.11.0\nkind=arch\n"), "not a release file of the .deb")
    bad("release-junk", put(RELEASE, b"fixture\n"), "not a release file of the .deb")
    bad("release-binary", put(RELEASE, b"version=2026.11.0\x00\nkind=deb\n"), "not a release file")
    bad("release-long", put(RELEASE, b"#" * 5000), "longer than")
    bad("release-elsewhere", put("share/xenogears-pc/notes.txt", b"fixture\n"), "not a file the port's install makes")
    bad("fifo", lambda st: os.mkfifo(st / DOC / "LICENSE-glslang.txt.fifo"), None)
    bad("fifo-named", lambda st: (st / DOC / "LICENSE-dav1d.txt").unlink()
        or os.mkfifo(st / DOC / "LICENSE-dav1d.txt"), "not a regular file")
    os.symlink(stage, base / "stage-link")
    refused(cli, "stage is a link", ["package", "--tag", "v2026.11.0", "--stage", base / "stage-link",
                                     "--out-dir", base / "link-out", *both], [stage], "not a folder")
    refused(cli, "no stage", ["package", "--tag", "v2026.11.0", "--stage", base / "nowhere",
                              "--out-dir", base / "nowhere-out", *both], [], "not a folder")
    check(not os.path.lexists(base / "link-out") and not os.path.lexists(base / "nowhere-out"),
          "a refused stage made --out-dir")

    # Overlap, both ways; an --out-dir that is a file.
    refused(cli, "out inside stage", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                      "--out-dir", stage / "dist", *both], [stage], "inside the stage")
    refused(cli, "out is stage", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", stage, *both],
            [stage], "inside the stage")
    outer = base / "outer"
    make_stage(outer / "stage")
    refused(cli, "stage inside out", ["package", "--tag", "v2026.11.0", "--stage", outer / "stage",
                                      "--out-dir", outer, *both], [outer], "inside --out-dir")
    (base / "a-file").write_bytes(b"not a folder\n")
    refused(cli, "out is a file", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                   "--out-dir", base / "a-file", *both], [base / "a-file"], "not a folder")

    # Collisions: an earlier tarball, an earlier sum alone, a dangling link under the asset's name.
    for label, make in (("tarball", lambda d: (d / tarball).write_bytes(b"an earlier release\n")),
                        ("sidecar", lambda d: (d / sidecar).write_bytes(b"an earlier sum\n")),
                        ("dangling link", lambda d: os.symlink(d / "absent-target", d / tarball)),
                        ("link to a file", lambda d: os.symlink(d / "notes.txt", d / sidecar))):
        d = base / f"collide-{label.replace(' ', '-')}"
        d.mkdir()
        (d / "notes.txt").write_bytes(b"unrelated\n")
        make(d)
        refused(cli, f"collision {label}", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", d,
                                            *both], [d, stage], "already holds")
        check(not os.path.exists(d / "absent-target"), f"collision {label}: a link was followed")
    check(stub.log() == [], "a refused stage, overlap or collision ran the auditor")

    # Induced failures mid-write: nothing of this run left, earlier files kept.
    for label, patch in (
            ("while writing the tarball", lambda m: setattr(m, "write_tarball", fail_tarball)),
            ("after the tarball, before the sum", lambda m: setattr(m, "publish", fail_second(m.publish)))):
        d = base / f"induced-{len(label)}"
        d.mkdir()
        (d / "notes.txt").write_bytes(b"unrelated\n")
        before = snapshot(d, root=False)  # the folder's own time changes with its temporary file
        code, err = run_inprocess(helper, ["package", "--tag", "v2026.11.0", "--stage", str(stage),
                                           "--out-dir", str(d), *map(str, both)], patch)
        check(code == 1, f"induced failure {label}: exit {code}")
        check("induced" in err, f"induced failure {label}: the error is not reported ({err!r})")
        check(snapshot(d, root=False) == before, f"induced failure {label}: --out-dir left {sorted(os.listdir(d))}")
        if "before the sum" in label:
            check(f"removed {tarball}" in err, f"induced failure {label}: the removal is not reported ({err!r})")
        nested = base / f"induced-new-{len(label)}" / "deeper"
        code, err = run_inprocess(helper, ["package", "--tag", "v2026.11.0", "--stage", str(stage),
                                           "--out-dir", str(nested), *map(str, both)], patch)
        check(code == 1 and not os.path.lexists(nested.parent),
              f"induced failure {label}: the folders this run made are left ({err!r})")
    check(snapshot(stage) == stage_before, "the stage changed")

    # The checks above catch a disabled guard (copies of the helper; the helper itself is untouched).
    stub.set()
    m = mutant(base, layout, "overlap", "if out_real.is_relative_to(stage_real):", "if False:")
    st = make_stage(base / "mutant-stage")
    snap = snapshot(st)
    r = m("package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", st / "dist", *both)
    # (the stage re-read after writing still refuses it, but only once it wrote into the stage)
    check(r.returncode == 0 or snapshot(st) != snap,
          "the overlap guard disabled: the out-inside-stage check would not notice")
    m = mutant(base, layout, "tag", "(0|[1-9][0-9]*)", "([0-9]+)")
    plain = make_stage(base / "mutant-tag-stage", release=None)  # the .deb's release.txt names 2026.11.0
    r = m("package", "--tag", "v2026.11.00", "--stage", plain, "--out-dir", base / "mutant-tag-out", *both)
    check(r.returncode == 0, "the tag rule loosened: the leading-zero check would not notice")
    r = cli("names", "--tag", "v2026.11.00")
    check(r.returncode == 1, "the real helper accepts v2026.11.00")

    # The audit's guards, each disabled in a copy: the rejection tests above would then see assets written.
    def disabled(name: str, old_text: str, new_text: str, **mode) -> subprocess.CompletedProcess:
        m = mutant(base, layout, name, old_text, new_text)
        stub.set(**mode)
        return m("package", "--tag", "v2026.11.0", "--stage", make_stage(base / f"mutant-{name}-stage"),
                 "--out-dir", base / f"mutant-{name}-out", *both)

    r = disabled("verdict", "    if failed or not passed:", "    if False:", xenogears_enh="fail")
    check(r.returncode == 0 and (base / "mutant-verdict-out" / tarball).is_file(),
          "the audit's verdict ignored: the rejected-engine test would not notice")
    r = disabled("no-audit", "    audits = audit(files, builds, ver)", "    audits = []", xenogears="fail")
    check(r.returncode == 0 and stub.log() == [], "the audit removed: the rejected-engine test would not notice")
    r = disabled("exit", "    elif proc.returncode != 0:", "    elif False:", xenogears="usage")
    check("exited 2" not in r.stderr, "the exit status ignored: the exit test would not notice")
    r = disabled("count", "    elif int(heading.group(3)) != images:", "    elif False:", xenogears="partial")
    check(r.returncode == 0, "the image count ignored: the partial-images test would not notice")
    r = disabled("summary", "    elif len(lines) != 2 or not AUDIT_OK.fullmatch(lines[1]):", "    elif False:",
                 xenogears_enh="no-summary")
    check(r.returncode != 1 or "not its heading" not in r.stderr,
          "the summary line ignored: the no-summary test would not notice")
    r = disabled("copy", "    if hashlib.sha256(copy.read_bytes()).hexdigest() != f[\"sha256\"]:", "    if False:",
                 xenogears="mutate-copy")
    check(r.returncode == 0, "the audited-copy check disabled: the changed-copy test would not notice")
    m = mutant(base, layout, "reread", "    try:\n        unchanged(stage, files)\n    except ReleaseError as error:",
               "    try:\n        pass\n    except ReleaseError as error:")
    victim = make_stage(base / "mutant-reread-stage")
    stub.set(xenogears_enh="mutate-stage", stage_exe=str(victim / "bin" / "xenogears"))
    r = m("package", "--tag", "v2026.11.0", "--stage", victim, "--out-dir", base / "mutant-reread-out", *both)
    check("after its payload audit" not in r.stderr, "the re-read after the audit disabled: its test would not notice")
    r = disabled("no-schema-check", "    schemas = check_schemas(files)", "    schemas = []", schema="xenogears")
    check(r.returncode == 0 and stub.schema_log() == [],
          "the schema check removed: the refused-schema test would not notice")
    r = disabled("images", "    if gone:", "    if False:", xenogears="partial")
    check(r.returncode == 1 and "not all 3" in r.stderr, "a short count is not refused once the files are there")
    stub.set()
    check(snapshot(stage) == stage_before, "the stage changed")


def compile_exe(cc: str, folder: Path, name: str, payload: bytes | None, text: str | None = None) -> bytes:
    """SYNTHETIC FIXTURE: a small real x86-64 program; with payload, it holds those bytes in its data; with
    text, that string (a synthetic path of a build machine)."""
    folder.mkdir(parents=True, exist_ok=True)
    table = "" if payload is None else ("static const unsigned char table[] = {" + ",".join(map(str, payload))
                                        + "};\n")
    if text is not None:
        table += "static const char path[] = " + json.dumps(text) + ";\n"
    value = "c" if payload is None and text is None else "table[c]" if text is None else "path[c]"
    body = "int main(int c, char **v) { (void)v; return " + value + "; }\n"
    (folder / f"{name}.c").write_text("/* SYNTHETIC FIXTURE, not the port */\n" + table + body)
    # (a name ending in -g: with debug information, which names no folder of this machine)
    debug = ["-g", f"-ffile-prefix-map={folder}=."] if name.endswith("-g") else []
    r = subprocess.run([cc, "-O1", *debug, "-o", str(folder / name), str(folder / f"{name}.c")], capture_output=True,
                       text=True, cwd=folder)
    check(r.returncode == 0, f"compiling the fixture {name}: {r.stderr}")
    return (folder / name).read_bytes() if r.returncode == 0 else b""


def real_stage(path: Path, original: bytes, enhanced: bytes | None) -> Path:
    make_stage(path, enhanced=enhanced is not None, release=None)  # packaged under several tags
    (path / "bin" / "xenogears").write_bytes(original)
    if enhanced is not None:
        (path / "bin" / "xenogears-enhanced").write_bytes(enhanced)
    for exe in ("xenogears", "xenogears-enhanced"):
        if (path / "bin" / exe).exists():
            (path / SCHEMA_DIR / f"{exe}.xgschema").write_bytes(real_schema(path / "bin" / exe))
    return path


def real_auditor_tests(base: Path, cc: str) -> None:
    """REAL AUDITOR, SYNTHETIC IMAGES: the repository's unchanged auditor through the helper."""
    layout = make_layout(base / "real auditor" / "public checkout", "real")
    for name in AUDITOR_FILES:
        check((layout / "tools" / name).read_bytes() == (ROOT / "tools" / name).read_bytes(),
              f"real auditor: tools/{name} is not the repository's")
    elsewhere = base / "real elsewhere"
    elsewhere.mkdir()
    cli = Cli(layout / "packaging" / "linux_release.py", elsewhere)
    b1 = make_build(base / "real build original")
    b2 = make_build(base / "real build enhanced", engine="enhanced")
    both = ["--ver", "us", "--build", b1, "--enhanced-build", b2]
    found = stream("image alpha.bin")[1000:1096]
    clean = compile_exe(cc, base / "real-c", "clean", None)
    dirty = compile_exe(cc, base / "real-c", "dirty", found)
    if not clean or not dirty:
        return
    check(found in dirty and found not in clean, "real auditor: the dirty fixture does not hold the image's bytes")
    folder, tarball, sidecar = names_of("2026.11.0")

    stage = real_stage(base / "real stage clean", clean, clean)
    out = base / "real out"
    r = cli("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", out, *both)
    if check(r.returncode == 0, f"real auditor, clean stage: {r.stderr}"):
        inspect(out / tarball, stage, folder, "real auditor, clean stage")
        check_sidecar(out, tarball, sidecar, "real auditor, clean stage")
        lines = [line for line in r.stdout.splitlines() if line.startswith("audit=")]
        check(len(lines) == 2 and all("all 3 module images read; payload ok: generated 0 runs/0 bytes" in line
                                      for line in lines), f"real auditor, clean stage: {lines}")
        lines = [line for line in r.stdout.splitlines() if line.startswith("schema=")]
        check(len(lines) == 2 and all("describes xenogears" in line and "stripped" in line for line in lines),
              f"real schema check, clean stage: {lines}")
    solo = real_stage(base / "real stage solo", clean, None)
    r = cli("package", "--tag", "v2026.12.7", "--stage", solo, "--out-dir", base / "real out solo", "--ver", "us",
            "--build", b1)
    check(r.returncode == 0 and r.stdout.count("audit=") == 1, f"real auditor, original only: {r.stderr}")

    # The real schema check: a schema of another image, one naming a path of the build machine, and an
    # executable that keeps its debug information are refused, after the audits, with nothing written.
    debug = compile_exe(cc, base / "real-c", "clean-g", None)
    other = real_stage(base / "real stage other schema", clean, clean)
    (other / SCHEMA_DIR / "xenogears-enhanced.xgschema").write_bytes(real_schema(base / "real-c" / "dirty"))
    pathy = real_stage(base / "real stage schema path", clean, clean)
    sys.path.insert(0, str(ROOT / "tools"))
    import statecheck
    sys.path.remove(str(ROOT / "tools"))
    text = json.loads(statecheck.schema_text((pathy / SCHEMA_DIR / "xenogears.xgschema").read_bytes()))
    text["exe"] = "/synthetic-builder/build/xenogears"
    (pathy / SCHEMA_DIR / "xenogears.xgschema").write_bytes(statecheck.schema_file(json.dumps(text).encode()))
    unstripped = real_stage(base / "real stage unstripped", debug, clean) if debug else None
    for label, st, needle in (("a schema of another image", other, "another image"),
                              ("a schema naming the build machine's path", pathy, "absolute paths"),
                              ("an executable with debug information", unstripped, "debug information")):
        if st is None:
            continue
        o = base / f"real out {label}"
        err = refused(cli, f"real schema check, {label}", ["package", "--tag", "v2026.11.0", "--stage", st,
                                                            "--out-dir", o, *both], [st], needle)
        check(not os.path.lexists(o) and "did not all pass" in err, f"real schema check, {label}: {err!r}")

    for label, original, enhanced in (("enhanced", clean, dirty), ("original", dirty, clean)):
        st = real_stage(base / f"real stage dirty {label}", original, enhanced)
        o = base / f"real out dirty {label}"
        err = refused(cli, f"real auditor, {label} engine holds image bytes",
                      ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", o, *both], [st], "payload FAIL")
        check(not os.path.lexists(o), f"real auditor, dirty {label}: --out-dir made")
        check("passed: bin/xenogears" in err and "exited 1" in err, f"real auditor, dirty {label}: {err!r}")
        check(found[:8].hex() not in err.lower(), f"real auditor, dirty {label}: the found bytes are relayed")
    st = real_stage(base / "real stage dirty solo", dirty, None)
    refused(cli, "real auditor, original only holds image bytes",
            ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", base / "real out dirty solo", "--ver", "us",
             "--build", b1], [st], "payload FAIL")
    check(not os.path.lexists(base / "real out dirty solo"), "real auditor, dirty original only: --out-dir made")

    # A build to hand out: the auditor's --dist refuses an engine whose build is not XG_DIST=ON (unset or OFF),
    # and a dist executable that names a folder of the build machine (SYNTHETIC paths: a repository that is
    # nowhere, and the fixture build folder itself). The relayed lines name neither.
    unset = make_build(base / "real build original not dist", dist=None)
    off = make_build(base / "real build enhanced dist off", engine="enhanced", dist="OFF")
    for label, args, passed in (
            ("original build without XG_DIST", ["--ver", "us", "--build", unset, "--enhanced-build", b2],
             "passed: bin/xenogears-enhanced"),
            ("enhanced build with XG_DIST=OFF", ["--ver", "us", "--build", b1, "--enhanced-build", off],
             "passed: bin/xenogears "),
            ("original only, without XG_DIST", ["--ver", "us", "--build", unset], None)):
        st = solo if passed is None else stage
        o = base / f"real out {label}"
        err = refused(cli, f"real auditor, {label}", ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", o,
                                                      *args], [st], "not a build to hand out")
        check(not os.path.lexists(o), f"real auditor, {label}: --out-dir made")
        check("exited 1" in err and (passed is None or passed in err), f"real auditor, {label}: {err!r}")
        check(str(base) not in err and "<the build of bin/xenogears" in err,
              f"real auditor, {label}: a private folder is relayed: {err!r}")
    repo = "/synthetic-builder/fixture-repo"
    leak_build = make_build(base / "real build leaky", home=repo)
    leak_enh = make_build(base / "real build leaky enhanced", engine="enhanced")
    in_repo = compile_exe(cc, base / "real-c", "in-repo", None, text=f"{repo}/port/src/fixture.c")
    in_build = compile_exe(cc, base / "real-c", "in-build", None, text=f"{leak_enh}/generated/fixture.h")
    if not in_repo or not in_build:
        return
    for label, original, enhanced, leaked in (("original", in_repo, clean, repo),
                                              ("enhanced", clean, in_build, str(leak_enh))):
        st = real_stage(base / f"real stage path {label}", original, enhanced)
        o = base / f"real out path {label}"
        err = refused(cli, f"real auditor, the {label} engine names a build machine's folder",
                      ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", o, "--ver", "us",
                       "--build", leak_build, "--enhanced-build", leak_enh], [st], "paths of the build machine")
        check(not os.path.lexists(o), f"real auditor, path in {label}: --out-dir made")
        check("exited 1" in err and "[path withheld]" in err and "passed: bin/xenogears" in err,
              f"real auditor, path in {label}: {err!r}")
        check(leaked not in err and "synthetic-builder" not in err and str(base) not in err,
              f"real auditor, path in {label}: the build machine's path is relayed: {err!r}")
    # The checks above catch the --dist request or the path redaction removed (copies of the helper).
    m = mutant(base, layout, "real-no-dist", '"--ver", ver,\n            "--dist"]', '"--ver", ver]')
    r = m("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", base / "real out mutant dist",
          "--ver", "us", "--build", unset, "--enhanced-build", off)
    check(r.returncode == 0, f"--dist not asked for: the not-dist tests would not notice ({r.stderr!r})")
    m = mutant(base, layout, "real-unredacted", '    text = BUILD_PATH_LINE.sub(r"\\1  [path withheld]", text)\n', "")
    r = m("package", "--tag", "v2026.11.0", "--stage", real_stage(base / "real stage mutant path", in_repo, clean),
          "--out-dir", base / "real out mutant path", "--ver", "us", "--build", leak_build, "--enhanced-build", leak_enh)
    check(r.returncode == 1 and repo in r.stderr, "the path redaction removed: the build-path tests would not notice")

    # With the helper's own image check disabled, the real auditor's count still refuses a partial extraction.
    v = variant(base, layout, "real-partial", lambda copy: (copy / "disc/us/beta.bin").unlink())
    refused(v, "real auditor, an image missing", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir",
                                                  base / "real out partial", *both], [stage], "1 of the 3")
    m = mutant(base, layout, "real-images", "    if gone:", "    if False:")
    (m.helper.parent.parent / "disc/us/beta.bin").unlink()
    refused(m, "real auditor, an image missing and the helper's check disabled",
            ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", base / "real out partial", *both],
            [stage], "cannot read")
    # A helper that reads the list more widely than the auditor: the real auditor's own count refuses it.
    m = mutant(base, layout, "real-wider", 'IMAGE_ENTRY = re.compile(r"\\{name:', 'IMAGE_ENTRY = re.compile(r"\\{\\s*name:')
    wide = m.helper.parent.parent
    (wide / "config/us/disc.yaml").write_text(LIST + "  - { name: gamma, dir: 0x01, file: 0x0F, sha1: 0}\n")
    (wide / "disc/us/gamma.bin").write_bytes(stream("image gamma.bin"))
    refused(m, "real auditor, the helper lists a module the auditor does not read",
            ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", base / "real out partial", *both],
            [stage], "read 3 module images, not all 4")
    v = variant(base, layout, "real-none", lambda copy: shutil.rmtree(copy / "disc"))
    refused(v, "real auditor, no images", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir",
                                           base / "real out partial", *both], [stage], "3 of the 3")
    check(not os.path.lexists(base / "real out partial"), "real auditor, images missing: --out-dir made")

    cwd, env, mark = hostile(base, "real")
    r = Cli(cli.helper, cwd, env=env, isolated=False)("package", "--tag", "v2026.11.0", "--stage", stage,
                                                      "--out-dir", base / "real out hostile", *both)
    check(r.returncode == 0 and not mark.exists(), f"real auditor, hostile folders: {r.returncode} {r.stderr}")
    check(not list(layout.rglob("__pycache__")), "real auditor: bytecode was written into the checkout")
    check((layout / "tools" / "payloadcheck.py").read_bytes() == (ROOT / "tools" / "payloadcheck.py").read_bytes(),
          "real auditor: the auditor changed")
    check(all((layout / "tools" / n).read_bytes() == (ROOT / "tools" / n).read_bytes() for n in SCHEMA_FILES),
          "real schema check: tools/state_schema.py or its modules changed")


def auditor_images(layout: Path, ver: str) -> list[str]:
    """The image files the unchanged auditor would read in a layout: its own retail_images(), in this process."""
    sys.path.insert(0, str(layout / "tools"))
    bytecode, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        mod = load(layout / "tools" / "payloadcheck.py", "payloadcheck_under_test")
        check(mod.ROOT == layout, "the auditor's root is not the layout")
        return sorted(mod.retail_images(ver))
    finally:
        sys.dont_write_bytecode = bytecode
        sys.path.remove(str(layout / "tools"))
        sys.modules.pop("payload_pe", None)
        sys.modules.pop("payload_elf", None)


def real_list_tests(base: Path, cc: str) -> None:
    """REAL MODULE LISTS, SYNTHETIC IMAGES: the helper reads config/us and config/jp as the unchanged auditor does."""
    clean = compile_exe(cc, base / "list-c", "clean", None)
    if not clean:
        return
    stage = real_stage(base / "list stage", clean, clean)
    for ver in ("us", "jp"):
        text = (ROOT / "config" / ver / "disc.yaml").read_text()
        # A count made a third way: the list's lines, comments removed, that open an entry with a name.
        lines = [line.split("#")[0] for line in text.splitlines()]
        third = [line.split("name:")[1].split(",")[0].strip() for line in lines if "name:" in line]
        check(len(third) >= 20, f"{ver}: the real list has only {len(third)} modules")
        # Every name, with both suffixes: more files than either reader should take.
        every = tuple(f"{name}{suffix}" for name in re.findall(r"name\s*:\s*(\w+)", text) for suffix in (".exe", ".bin"))
        layout = make_layout(base / f"list {ver}" / "public checkout", "real", ver=ver, listing=text, images=every)
        helper = load(layout / "packaging" / "linux_release.py", f"linux_release_list_{ver}")
        mine = sorted(p.name for p in helper.module_images(ver))
        theirs = auditor_images(layout, ver)
        check(sorted(Path(n).stem for n in mine) == theirs == sorted(third),
              f"{ver}: the helper lists {len(mine)} modules, the auditor {len(theirs)}, the list's lines {len(third)}")
        check(sum(n.endswith(".exe") for n in mine) == 1 and "main.exe" in mine, f"{ver}: the executable's image")
        # Only the helper's files: the auditor still reads every module (the suffixes are the auditor's).
        for name in every:
            if name not in mine:
                (layout / "disc" / ver / name).unlink()
        check(auditor_images(layout, ver) == theirs, f"{ver}: the auditor reads other files than the helper requires")
        # Each of the helper's files is one the auditor reads: without it, the auditor has one module fewer.
        for name in mine:
            data = (layout / "disc" / ver / name).read_bytes()
            (layout / "disc" / ver / name).unlink()
            check(len(auditor_images(layout, ver)) == len(theirs) - 1, f"{ver}: the auditor does not read {name}")
            try:
                helper.module_images(ver)
                check(False, f"{ver}: the helper accepts the images without {name}")
            except helper.ReleaseError as error:
                check(name in str(error), f"{ver}: without {name}: {error}")
            (layout / "disc" / ver / name).write_bytes(data)
        # Through the command: the unchanged auditor's own heading reports the helper's count.
        b1 = make_build(base / f"list build {ver}", ver=ver.upper())
        b2 = make_build(base / f"list build enhanced {ver}", ver=ver.upper(), engine="enhanced")
        both = ["--ver", ver, "--build", b1, "--enhanced-build", b2]
        elsewhere = base / f"list elsewhere {ver}"
        elsewhere.mkdir()
        cli = Cli(layout / "packaging" / "linux_release.py", elsewhere)
        r = cli("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", base / f"list out {ver}", *both)
        check(r.returncode == 0 and r.stdout.count(f"all {len(mine)} module images read; payload ok:") == 2,
              f"{ver}: the real list through the command: {r.returncode} {r.stdout!r} {r.stderr!r}")
        print(f"note: {ver}: config/{ver}/disc.yaml lists {len(mine)} module images for the helper, "
              f"{len(theirs)} for the unchanged auditor, {len(third)} by its lines (synthetic images)")
        o = base / f"list out refused {ver}"
        v = variant(base, layout, f"list-partial-{ver}", lambda copy: (copy / "disc" / ver / mine[-1]).unlink())
        refused(v, f"{ver}: real list, one image missing", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                                            "--out-dir", o, *both], [stage], mine[-1])
        m = mutant(base, layout, f"list-wider-{ver}", 'IMAGE_ENTRY = re.compile(r"\\{name:',
                   'IMAGE_ENTRY = re.compile(r"\\{\\s*name:')
        wide = m.helper.parent.parent
        (wide / "config" / ver / "disc.yaml").write_text(text + "  - { name: fixture, dir: 0x01, file: 0x7F, sha1: 0}\n")
        (wide / "disc" / ver / "fixture.bin").write_bytes(stream("image fixture.bin"))
        refused(m, f"{ver}: real list, a helper reading it more widely than the auditor",
                ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", o, *both], [stage],
                f"read {len(mine)} module images, not all {len(mine) + 1}")
        v = variant(base, layout, f"list-none-{ver}", lambda copy: shutil.rmtree(copy / "disc" / ver))
        refused(v, f"{ver}: real list, no image", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", o,
                                                   *both], [stage], f"{len(mine)} of the {len(mine)} module images")
        # Entries of another form in the real list: block style refused; a commented one-line entry is one the
        # auditor reads when its file is there, so its image is required, and then both count it.
        listing = layout / "config" / ver / "disc.yaml"
        v = variant(base, layout, f"list-block-{ver}",
                    lambda copy: (copy / "config" / ver / "disc.yaml").write_text(
                        text + "  - name: fixture\n    dir: 0x01\n    file: 0x7F\n"))
        refused(v, f"{ver}: real list with a block-style entry", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                                                  "--out-dir", o, *both], [stage], "name: keys but")
        listing.write_text(text + "  # - {name: fixture, dir: 0x01, file: 0x7F, sha1: 0}\n")
        refused(cli, f"{ver}: real list with a commented entry, no image",
                ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", o, *both], [stage], "fixture.bin")
        (layout / "disc" / ver / "fixture.bin").write_bytes(stream("image fixture.bin"))
        check(len(helper.module_images(ver)) == len(auditor_images(layout, ver)) == len(mine) + 1,
              f"{ver}: a commented entry is counted differently by the helper and the auditor")
        check(not os.path.lexists(o), f"{ver}: a refused real list made --out-dir")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.parse_args()
    if not HELPER.is_file():
        print(f"FAIL: no {HELPER}")
        return 1
    static_checks()
    base = Path(tempfile.mkdtemp(prefix="check-linux-release-")).resolve()
    tmp = base / "tmp"
    tmp.mkdir()
    ENV["TMPDIR"] = str(tmp)
    tempfile.tempdir = str(tmp)  # the helper run in this process, too
    try:
        stub = Stub(base / "stub-control")
        layout = make_layout(base / "public" / "public checkout", "stub", stub.control)
        packaging_tests(base, layout, stub)
        cc = shutil.which("cc") or shutil.which("gcc")
        if check(cc is not None, "no C compiler (cc or gcc): the real-auditor tests need a small real executable"):
            real_auditor_tests(base, cc)
            real_list_tests(base, cc)
        leaks = [p for p in base.rglob("*.part")] + [p for p in base.rglob(".xenogears-pc-*")]
        check(not leaks, f"temporary files left: {leaks}")
        check(os.listdir(tmp) == [], f"the audits left {os.listdir(tmp)} in the temporary folder")
    finally:
        tempfile.tempdir = None
        shutil.rmtree(base, ignore_errors=True)
    if FAILURES:
        print(f"check_linux_release: {len(FAILURES)} failure(s)")
        return 1
    print("check_linux_release: ok (synthetic stages: the packaging logic and the audit's plumbing with a stub "
          "auditor; the real auditor on synthetic images and on the real US and JP module lists; not a real build, "
          "disc, run or release audit)")
    return 0


def fail_tarball(out, folder, files) -> None:
    out.write(b"\x1f\x8b partial bytes")
    raise OSError("induced failure while writing the tarball")


def fail_second(real):
    calls = []

    def publish(tmp, dest, written):
        calls.append(dest)
        if len(calls) == 2:
            raise OSError("induced failure before the .sha256")
        real(tmp, dest, written)
    return publish


if __name__ == "__main__":
    sys.exit(main())
