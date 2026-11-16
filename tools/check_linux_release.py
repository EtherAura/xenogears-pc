#!/usr/bin/env python3
"""Checks packaging/linux_release.py (the Linux release tarball and its .sha256) on synthetic stages.

    python tools/check_linux_release.py --ver us

The helper is copied alone into a temporary public layout (packaging/ of a
fresh folder) and run from another folder with python -I and a minimal
environment, as a public checkout runs it. Its stages are SYNTHETIC FIXTURES
laid out by the install rules of port/cmake/deb.cmake: the "executables" are a
bare x86-64 ELF header with a PT_INTERP program header and a marker, not
programs and never release executables, and the licences are marker text. They
prove the packaging logic (names, layout, modes, refusals, sums, cleanup,
repeatability), not that a real build installs or runs. No game bytes.

Each archive is opened and checked independently of the helper (names, types,
modes, owners, times, bytes); the .sha256 is checked against the archive's
bytes and by sha256sum -c when it is installed. Two production guards are
disabled in copies of the helper, to show the checks here catch them. The
assets do not depend on the version (--ver is accepted for the gate's
calling convention). Exit status 1 on any failure.
"""
from __future__ import annotations

import argparse
import ast
import contextlib
import gzip
import hashlib
import importlib.util
import io
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER = ROOT / "tools/publish/repo-port/packaging/linux_release.py"
DOC = "share/doc/xenogears-pc"
LICENCES = ("COPYING", "LICENSE-Jukebox-font.txt", "LICENSE-imgui.txt", "LICENSE-glslang.txt", "LICENSE-dav1d.txt")
ENV = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}
FAILURES: list[str] = []


def check(ok: bool, what: str) -> bool:
    if not ok:
        FAILURES.append(what)
        print(f"FAIL: {what}")
    return ok


def fake_elf(marker: bytes, machine: int = 62, e_type: int = 3, interp: bool = True) -> bytes:
    """SYNTHETIC FIXTURE: an ELF64 header and one program header, then a marker; not a program."""
    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    head = ident + struct.pack("<HHIQQQIHHHHHH", e_type, machine, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    ph = struct.pack("<IIQQQQQQ", 3 if interp else 1, 4, 120, 0, 0, 0, 0, 1)
    return head + ph + b"SYNTHETIC FIXTURE, not a release executable: " + marker + b"\n"


def make_stage(path: Path, enhanced: bool = True, optional: bool = True) -> Path:
    """SYNTHETIC FIXTURE: the layout cmake --install gives the Linux build (port/cmake/deb.cmake)."""
    (path / "bin").mkdir(parents=True)
    (path / DOC).mkdir(parents=True)
    exes = ["xenogears"] + (["xenogears-enhanced"] if enhanced else [])
    for exe in exes:
        (path / "bin" / exe).write_bytes(fake_elf(exe.encode()))
        os.chmod(path / "bin" / exe, 0o755)
    for name in LICENCES if optional else LICENCES[:3]:
        (path / DOC / name).write_text(f"SYNTHETIC FIXTURE licence text: {name}\n", encoding="utf-8")
        os.chmod(path / DOC / name, 0o644)
    return path


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
    def __init__(self, helper: Path, cwd: Path):
        self.helper, self.cwd = helper, cwd

    def __call__(self, *args: str | Path) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, "-I", str(self.helper), *map(str, args)], cwd=self.cwd, env=ENV,
                              capture_output=True, text=True, timeout=120)


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


def refused(cli: Cli, label: str, args: list, watch: list[Path], needle: str | None = None) -> None:
    before = [snapshot(p) for p in watch]
    r = cli(*args)
    check(r.returncode == 1, f"{label}: exit {r.returncode}, not 1 (stdout {r.stdout!r})")
    check(r.stderr.startswith("linux_release: "), f"{label}: no error message ({r.stderr!r})")
    if needle is not None:
        check(needle in r.stderr, f"{label}: {needle!r} not in {r.stderr!r}")
    for path, snap in zip(watch, before):
        check(snapshot(path) == snap, f"{label}: {path} changed")


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


def run_inprocess(helper: Path, argv: list[str], patch) -> tuple[int, str]:
    """The helper's main() in this process with one function replaced (an induced failure)."""
    spec = importlib.util.spec_from_file_location("linux_release_under_test", helper)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    patch(mod)
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        code = mod.main(argv)
    return code, err.getvalue()


def mutant(base: Path, helper: Path, name: str, old: str, new: str) -> Cli:
    text = helper.read_text(encoding="utf-8")
    check(text.count(old) == 1, f"mutant {name}: the guard {old!r} is not in the helper exactly once")
    folder = base / f"mutant-{name}" / "packaging"
    folder.mkdir(parents=True)
    (folder / "linux_release.py").write_text(text.replace(old, new), encoding="utf-8")
    run = base / f"mutant-{name}-cwd"
    run.mkdir()
    return Cli(folder / "linux_release.py", run)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.parse_args()
    if not HELPER.is_file():
        print(f"FAIL: no {HELPER}")
        return 1
    static_checks()
    base = Path(tempfile.mkdtemp(prefix="check-linux-release-"))
    try:
        public = base / "public" / "packaging"
        public.mkdir(parents=True)
        helper = public / "linux_release.py"
        shutil.copy2(HELPER, helper)
        elsewhere = base / "elsewhere"
        elsewhere.mkdir()
        cli = Cli(helper, elsewhere)

        r = cli("--help")
        check(r.returncode == 0 and "package" in r.stdout and "names" in r.stdout, f"--help: {r.stderr}")
        r = cli("package", "--help")
        check(r.returncode == 0 and all(a in r.stdout for a in ("--tag", "--stage", "--out-dir")),
              "package --help lacks --tag/--stage/--out-dir")
        flat = " ".join(r.stdout.split())
        check("payload audit" in flat and "do not clear the assets for upload" in flat,
              "package --help does not say it is not the payload audit")

        # Names: the tag's spelling kept, counter zero and later counters accepted.
        stage = make_stage(base / "stage")
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
            refused(cli, f"tag {tag!r}", ["package", "--tag", tag, "--stage", stage, "--out-dir", out],
                    [out, stage], repr(tag))
            check(not os.path.lexists(out), f"tag {tag!r}: --out-dir made")
        r = cli("names", "--tag", "v2026.11." + "9" * 300)
        check(r.returncode == 1, "a tag too long for a file name is accepted")

        # The release: both engines, counter zero, into a folder holding an unrelated file.
        out = base / "release"
        out.mkdir()
        (out / "notes.txt").write_bytes(b"an unrelated file\n")
        (out / "xenogears-pc-2026.11.0.tar.gz").write_bytes(b"the source tarball, unrelated\n")
        others = {n: (out / n).read_bytes() for n in ("notes.txt", "xenogears-pc-2026.11.0.tar.gz")}
        folder, tarball, sidecar = names_of("2026.11.0")
        r = cli("package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", out)
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
        check(snapshot(stage) == stage_before, "the stage changed while it was packaged")

        # Repeatable: same content, other times and another folder give the same bytes.
        copy = base / "stage-copy"
        shutil.copytree(stage, copy)
        for i, p in enumerate(sorted(copy.rglob("*"))):
            os.utime(p, (1_000_000 + i * 7777, 2_000_000 + i * 9999), follow_symlinks=False)
        os.utime(copy, (5, 5))
        check(snapshot(copy) != stage_before, "the copy's times were not changed")
        again = base / "again"
        r = cli("package", "--tag", "v2026.11.0", "--stage", copy, "--out-dir", again)
        if check(r.returncode == 0, f"package again: {r.stderr}") and (out / tarball).is_file():
            check((again / tarball).read_bytes() == (out / tarball).read_bytes(),
                  "same stage content, other times: different tarball bytes")
            check((again / sidecar).read_bytes() == (out / sidecar).read_bytes(), "different .sha256")
            check(sorted(os.listdir(again)) == sorted([tarball, sidecar]), "made more than the two assets")

        # A later counter, and the original engine alone with no optional licence.
        solo = make_stage(base / "stage-solo", enhanced=False, optional=False)
        f2, t2, s2 = names_of("2026.12.7")
        out2 = base / "nested" / "release-later"
        r = cli("package", "--tag", "v2026.12.7", "--stage", solo, "--out-dir", out2)
        if check(r.returncode == 0, f"package v2026.12.7: {r.stderr}"):
            inspect(out2 / t2, solo, f2, "v2026.12.7 original only")
            check_sidecar(out2, t2, s2, "v2026.12.7")
            check(sorted(os.listdir(out2)) == sorted([t2, s2]), "later counter: not exactly two assets")

        # Bad stages: each refused, nothing written, the stage untouched.
        def bad(label: str, change, needle: str | None = None) -> None:
            st = make_stage(base / f"bad-{label}")
            change(st)
            o = base / f"bad-{label}-out"
            refused(cli, f"stage {label}", ["package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", o],
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

        bad("no-exe",lambda st: (st / "bin/xenogears").unlink(), "bin/xenogears")
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
        bad("fifo", lambda st: os.mkfifo(st / DOC / "LICENSE-glslang.txt.fifo"), None)
        bad("fifo-named", lambda st: (st / DOC / "LICENSE-dav1d.txt").unlink()
            or os.mkfifo(st / DOC / "LICENSE-dav1d.txt"), "not a regular file")
        os.symlink(stage, base / "stage-link")
        refused(cli, "stage is a link", ["package", "--tag", "v2026.11.0", "--stage", base / "stage-link",
                                         "--out-dir", base / "link-out"], [stage], "not a folder")
        refused(cli, "no stage", ["package", "--tag", "v2026.11.0", "--stage", base / "nowhere",
                                  "--out-dir", base / "nowhere-out"], [], "not a folder")
        check(not os.path.lexists(base / "link-out") and not os.path.lexists(base / "nowhere-out"),
              "a refused stage made --out-dir")

        # Overlap, both ways; an --out-dir that is a file.
        refused(cli, "out inside stage", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                          "--out-dir", stage / "dist"], [stage], "inside the stage")
        refused(cli, "out is stage", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", stage],
                [stage], "inside the stage")
        outer = base / "outer"
        make_stage(outer / "stage")
        refused(cli, "stage inside out", ["package", "--tag", "v2026.11.0", "--stage", outer / "stage",
                                          "--out-dir", outer], [outer], "inside --out-dir")
        (base / "a-file").write_bytes(b"not a folder\n")
        refused(cli, "out is a file", ["package", "--tag", "v2026.11.0", "--stage", stage,
                                       "--out-dir", base / "a-file"], [base / "a-file"], "not a folder")

        # Collisions: an earlier tarball, an earlier sum alone, a dangling link under the asset's name.
        for label, make in (("tarball", lambda d: (d / tarball).write_bytes(b"an earlier release\n")),
                            ("sidecar", lambda d: (d / sidecar).write_bytes(b"an earlier sum\n")),
                            ("dangling link", lambda d: os.symlink(d / "absent-target", d / tarball)),
                            ("link to a file", lambda d: os.symlink(d / "notes.txt", d / sidecar))):
            d = base / f"collide-{label.replace(' ', '-')}"
            d.mkdir()
            (d / "notes.txt").write_bytes(b"unrelated\n")
            make(d)
            refused(cli, f"collision {label}", ["package", "--tag", "v2026.11.0", "--stage", stage, "--out-dir", d],
                    [d, stage], "already holds")
            check(not os.path.exists(d / "absent-target"), f"collision {label}: a link was followed")

        # Induced failures mid-write: nothing of this run left, earlier files kept.
        for label, patch in (
                ("while writing the tarball", lambda m: setattr(m, "write_tarball", fail_tarball)),
                ("after the tarball, before the sum", lambda m: setattr(m, "publish", fail_second(m.publish)))):
            d = base / f"induced-{len(label)}"
            d.mkdir()
            (d / "notes.txt").write_bytes(b"unrelated\n")
            before = snapshot(d, root=False)  # the folder's own time changes with its temporary file
            code, err = run_inprocess(helper, ["package", "--tag", "v2026.11.0", "--stage", str(stage),
                                               "--out-dir", str(d)], patch)
            check(code == 1, f"induced failure {label}: exit {code}")
            check("induced" in err, f"induced failure {label}: the error is not reported ({err!r})")
            check(snapshot(d, root=False) == before, f"induced failure {label}: --out-dir left {sorted(os.listdir(d))}")
            if "before the sum" in label:
                check(f"removed {tarball}" in err, f"induced failure {label}: the removal is not reported ({err!r})")
            nested = base / f"induced-new-{len(label)}" / "deeper"
            code, err = run_inprocess(helper, ["package", "--tag", "v2026.11.0", "--stage", str(stage),
                                               "--out-dir", str(nested)], patch)
            check(code == 1 and not os.path.lexists(nested.parent),
                  f"induced failure {label}: the folders this run made are left ({err!r})")
        check(snapshot(stage) == stage_before, "the stage changed")

        # The checks above catch a disabled guard (copies of the helper; the helper itself is untouched).
        m = mutant(base, helper, "overlap", "if out_real.is_relative_to(stage_real):", "if False:")
        st = make_stage(base / "mutant-stage")
        snap = snapshot(st)
        r = m("package", "--tag", "v2026.11.0", "--stage", st, "--out-dir", st / "dist")
        # (the stage re-read after writing still refuses it, but only once it wrote into the stage)
        check(r.returncode == 0 or snapshot(st) != snap,
              "the overlap guard disabled: the out-inside-stage check would not notice")
        m = mutant(base, helper, "tag", "(0|[1-9][0-9]*)", "([0-9]+)")
        r = m("package", "--tag", "v2026.11.00", "--stage", stage, "--out-dir", base / "mutant-tag-out")
        check(r.returncode == 0, "the tag rule loosened: the leading-zero check would not notice")
        r = cli("names", "--tag", "v2026.11.00")
        check(r.returncode == 1, "the real helper accepts v2026.11.00")

        leaks = [p for p in base.rglob("*.part")] + [p for p in base.rglob(".xenogears-pc-*")]
        check(not leaks, f"temporary files left: {leaks}")
        check(snapshot(stage) == stage_before, "the stage changed")
    finally:
        shutil.rmtree(base, ignore_errors=True)
    if FAILURES:
        print(f"check_linux_release: {len(FAILURES)} failure(s)")
        return 1
    print("check_linux_release: ok (synthetic stages: packaging logic only, not a real build, run or payload audit)")
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
