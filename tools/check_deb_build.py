#!/usr/bin/env python3
"""Check packaging/deb_build.py (xenogears-pc): build, cpack, companions, staging, all fail-closed.

    python tools/check_deb_build.py --ver us
    python tools/check_deb_build.py --ver us --no-real   # the fixtures only

1. Public layout: deb_build.py and deb_assets.py, alone in
   build/pc/deb-build-<ver>/public/packaging/ (as tools/publish_decomp.py lays
   tools/publish/repo-port/ over the tree), import only the standard library,
   pass the publisher's date, time and private-text scan (line by line and with
   adjacent string literals joined), and run from there with python -I.
2. Fixture pipeline (FIXTURE evidence only: not an Ubuntu build or install):
   labelled CMake build folders (a CMakeCache.txt and a CPackConfig.cmake of
   the release's settings), stub cmake and cpack on PATH that log their
   arguments and, by the folder's stub-mode, fail or copy a labelled package
   made with the real dpkg-deb --build; the real dpkg-deb, dpkg and the
   unchanged deb_assets.py. The positive run must stage exactly the .deb, its
   .sha256 and its .cdx.json beside an existing folder's contents. Refused
   (exit 1, the failing step named, no success line, the output folder exactly
   as it was, no command run when the inputs are wrong): bad tags, a tag of
   another version, incompatible caches and CPack settings, an output folder
   already holding the set, missing commands, a build failure, a cpack
   failure, no, wrong-named or stale package, a package without the executable,
   a licence or the executable's state schema (or one not in its packed form),
   with a wrong mode, an extra file or another version, strict
   Depends refusal (Depends libvulkan1 alone) and companion failures. Paths
   with spaces and shell metacharacters run inertly.
3. The real build: this check's own build/pc/deb-build-<ver>/real-build,
   configured here as tools/check_deb_package.py configures its build, with
   XG_PACKAGE_VERSION of the test tag (so it needs no other check to have run,
   and never reads build/pc/deb-<ver>, whose version is whatever that folder
   was last configured with: 0~dev when new, which is no release tag). The
   first run builds the port there. Its own Depends decides the
   expected outcome: without libsdl3-0 and libc6 (a host whose dpkg knows none
   of its libraries) it must be refused at the companions step with nothing
   staged; it is never forced through. Not Ubuntu evidence either way.

Nothing is installed, launched or uploaded; all output stays in build/.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGING = ROOT / "tools/publish/repo-port/packaging"
HELPERS = ("deb_build.py", "deb_assets.py")
TAG = "v2026.11.0"
VERSION = TAG[1:]
DEB = f"xenogears-pc_{VERSION}_amd64.deb"
SET = [DEB, f"{DEB}.sha256", f"{DEB}.cdx.json"]
DOC = "usr/share/doc/xenogears-pc"
ICON = "usr/share/icons/hicolor/scalable/apps/xenogears.svg"
DESKTOP = "usr/share/applications/xenogears.desktop"
RELEASE = "usr/share/xenogears-pc/release.txt"  # what the in-app updater reads (port/src/system/release.h)
SCHEMA = "usr/share/xenogears-pc/xenogears.xgschema"  # the executable's state schema (port/cmake/deb.cmake)
FV = "1.0~fixture"  # fixture Depends versions: arbitrary, not any distribution's
GOOD_DEPENDS = f"libc6 (>= {FV}), libsdl3-0 (>= {FV}), libvulkan1"
REAL_TOOLS = ("dpkg-deb", "dpkg", "tar", "gtar", "xz", "zstd", "gzip")
CACHE = {"CMAKE_BUILD_TYPE:STRING": "Release", "XG_VERSION:STRING": "US", "XG_EXTRAS:BOOL": "ON",
         "XG_STATE_SCHEMA:BOOL": "ON", "XG_ENGINE:STRING": "original", "XG_PACKAGE_VERSION:STRING": VERSION,
         "glslang_DIR:PATH": "glslang_DIR-NOTFOUND", "XG_DAV1D_LIBRARY:FILEPATH": "XG_DAV1D_LIBRARY-NOTFOUND"}
CPACK = {"CPACK_GENERATOR": "DEB", "CPACK_PACKAGE_NAME": "xenogears-pc", "CPACK_DEBIAN_PACKAGE_VERSION": VERSION,
         "CPACK_DEBIAN_PACKAGE_ARCHITECTURE": "amd64", "CPACK_DEBIAN_FILE_NAME": DEB,
         "CPACK_PACKAGING_INSTALL_PREFIX": "/usr", "CPACK_DEBIAN_PACKAGE_SHLIBDEPS": "ON",
         "CPACK_DEBIAN_PACKAGE_DEPENDS": "libvulkan1"}

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def run(argv: list, cwd: Path = ROOT, env: dict | None = None) -> subprocess.CompletedProcess:
    print("$", " ".join(str(a) for a in argv), flush=True)
    return subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, text=True)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(folder: Path) -> dict | None:
    if not folder.exists():
        return None
    return {p.relative_to(folder).as_posix(): (p.is_dir(), b"" if p.is_dir() else p.read_bytes(), p.stat().st_mtime_ns)
            for p in sorted(folder.rglob("*"))}


def stage_public(work: Path, name: str = "public") -> Path:
    public = work / name
    shutil.rmtree(public, ignore_errors=True)
    (public / "packaging").mkdir(parents=True)
    for helper in HELPERS:
        shutil.copy2(PACKAGING / helper, public / "packaging" / helper)
    return public


def scan_public(public: Path) -> None:
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    patterns = (publish.LEFTOVER_DATE, publish.TIME_OF_DAY, publish.FORBIDDEN, publish.OPERATIONAL,
                re.compile(r"/home/", re.I))
    for helper in HELPERS:
        text = (public / "packaging" / helper).read_text()
        imports = set()
        for node in ast.walk(ast.parse(text)):
            if isinstance(node, ast.Import):
                imports |= {a.name.split(".")[0] for a in node.names}
            elif isinstance(node, ast.ImportFrom):
                imports.add((node.module or "").split(".")[0])
        outside = sorted(imports - set(sys.stdlib_module_names) - {"__future__"})
        check(not outside, f"{helper} imports only the standard library ({outside or sorted(imports)})")
        joined = re.sub(r"[\"']\s*\+?\s*\n?\s*[fr]?[\"']", "", text)  # "a" "b", "a" + "b", across lines
        flagged = [line for line in text.splitlines() + joined.splitlines() if any(p.search(line) for p in patterns)]
        check(not flagged, f"{helper} passes the publisher's date/time/private scan, split strings joined "
                           f"({flagged or 'clean'})")


STUB = '''#!{python} -I
import json, re, shutil, sys
from pathlib import Path
args = sys.argv[1:]
if "{tool}" == "cmake":
    build = Path(args[args.index("--build") + 1])
else:
    build = Path(args[args.index("--config") + 1]).parent
with open(build / "stub-log", "a") as log:
    log.write(json.dumps(["{tool}", args]) + "\\n")
mode = json.loads((build / "stub-mode").read_text())
if "{tool}" == "cmake":
    if mode["mode"] == "build-fail":
        print("fixture: the build failed", file=sys.stderr)
        sys.exit(2)
    sys.exit(0)
if mode["mode"] == "cpack-fail":
    print("fixture: cpack failed", file=sys.stderr)
    sys.exit(1)
out = Path(args[args.index("-B") + 1])
out.mkdir(parents=True, exist_ok=True)
if mode["mode"] in ("package", "wrong-name"):
    name = re.search(r'^set\\(CPACK_DEBIAN_FILE_NAME "(.*)"\\)$', (build / "CPackConfig.cmake").read_text(), re.M)[1]
    shutil.copy2(mode["deb"], out / (name if mode["mode"] == "package" else "xenogears-pc_{version}_i386.deb"))
'''


def make_bin(work: Path, name: str, without: tuple = ()) -> Path:
    """A PATH folder: stub cmake and cpack, links to the real dpkg tools, minus `without`."""
    folder = work / "bin" / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    for tool in ("cmake", "cpack"):
        if tool not in without:
            (folder / tool).write_text(STUB.format(python=sys.executable, tool=tool, version=VERSION))
            (folder / tool).chmod(0o755)
    for tool in REAL_TOOLS:
        if tool not in without and shutil.which(tool):
            (folder / tool).symlink_to(shutil.which(tool))
    return folder


def fixture_deb(work: Path, label: str, files: dict[str, tuple[str, int]], depends: str | None = GOOD_DEPENDS,
                version: str = VERSION) -> Path:
    """A labelled fixture package made with dpkg-deb --build (not a game package, not release evidence)."""
    tree = work / "fixture-trees" / label
    shutil.rmtree(tree, ignore_errors=True)
    (tree / "DEBIAN").mkdir(parents=True)
    for path, (text, mode) in files.items():
        (tree / path).parent.mkdir(parents=True, exist_ok=True)
        (tree / path).write_text(text)
        (tree / path).chmod(mode)
    (tree / "DEBIAN/control").write_text(
        f"Package: xenogears-pc\nVersion: {version}\nArchitecture: amd64\nMaintainer: fixture\n"
        + (f"Depends: {depends}\n" if depends is not None else "")
        + "Description: test fixture for check_deb_build.py, not a release package\n")
    target = work / "fixture-debs" / label / DEB
    target.parent.mkdir(parents=True, exist_ok=True)
    target.unlink(missing_ok=True)
    built = run(["dpkg-deb", "--root-owner-group", "--build", tree, target])
    shutil.rmtree(tree, ignore_errors=True)
    check(built.returncode == 0 and target.is_file(), f"fixture package {label} built with dpkg-deb")
    return target


def good_files(**changes) -> dict[str, tuple[str, int]]:
    files = {"usr/bin/xenogears": ("#!/bin/sh\necho fixture executable, not the game\n", 0o755)}
    files[ICON] = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"/>\n', 0o644)
    files[DESKTOP] = ("[Desktop Entry]\nType=Application\nName=Xenogears\nExec=xenogears\nIcon=xenogears\n", 0o644)
    for name in ("COPYING", "LICENSE-Jukebox-font.txt", "LICENSE-imgui.txt"):
        files[f"{DOC}/{name}"] = (f"fixture licence text ({name})\n", 0o644)
    files[RELEASE] = (f"version={VERSION}\nkind=deb\n", 0o644)
    files[SCHEMA] = ("XGSCHEMA fixture: the packed form's magic, not a state schema\n", 0o644)
    for path, value in changes.items():
        path = path.replace("__", "/")
        if value is None:
            files.pop(path, None)
        else:
            files[path] = value
    return files


def make_build(work: Path, label: str, mode: str = "package", deb: Path | None = None,
               cache: dict | None = None, cpack: dict | None = None, drop: str | None = None) -> Path:
    """A labelled fixture build folder: the release's cache and CPack settings, with changes."""
    build = work / "builds" / label
    shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True)
    values = dict(CACHE, **(cache or {}))
    (build / "CMakeCache.txt").write_text("# fixture cache\n" + "".join(f"{k}={v}\n" for k, v in values.items() if v is not None))
    sets = dict(CPACK, **(cpack or {}))
    (build / "CPackConfig.cmake").write_text("# fixture\n" + "".join(f'set({k} "{v}")\n' for k, v in sets.items()))
    if drop:
        (build / drop).unlink()
    (build / "stub-mode").write_text(json.dumps({"mode": mode, "deb": str(deb) if deb else None}))
    return build


def logged(build: Path) -> list[str]:
    log = build / "stub-log"
    return [json.loads(line)[0] for line in log.read_text().splitlines()] if log.is_file() else []


def out_folder(work: Path, label: str, exists: bool = True) -> Path:
    out = work / "outs" / label
    shutil.rmtree(out, ignore_errors=True)
    if exists:
        (out / "older").mkdir(parents=True)
        (out / "KEEP").write_text("existing content: deb_build must leave this alone\n")
        (out / "older/xenogears-pc_2026.10.0_amd64.deb").write_text("an unrelated older file (fixture)\n")
    return out


def deb_build(public: Path, build: Path, tag: str, out: Path, env: dict, *extra) -> subprocess.CompletedProcess:
    return run([sys.executable, "-I", "packaging/deb_build.py", "--build-dir", build, "--tag", tag,
                "--out-dir", out, *extra], cwd=public, env=env)


def own_temp(tmp: Path) -> list[str]:
    """deb_build's own temporary folders left in TMPDIR (dpkg-deb -f leaves dpkg-deb.* there itself on some hosts)."""
    return sorted(p.name for p in tmp.iterdir() if p.name.startswith("deb_build-"))


def announced(stdout: str) -> list[str]:
    """What a run printed besides its echoed commands."""
    return [line for line in stdout.splitlines() if line.strip() and not line.startswith("$ ")]


def refused(public: Path, work: Path, what: str, build: Path, step: str, expect: tuple, *, tag: str = TAG,
            bin_dir: Path, out: Path | None = None, commands: list[str] | None = None) -> None:
    out = out or out_folder(work, re.sub(r"\W+", "-", what))
    before = snapshot(out)
    tmp = work / "tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    if (build / "stub-log").exists():
        (build / "stub-log").unlink()
    result = deb_build(public, build, tag, out, {"PATH": str(bin_dir), "TMPDIR": str(tmp)})
    message = result.stderr.strip()
    ok = (result.returncode == 1 and f"FAILED at step {step}:" in message and all(e in message for e in expect)
          and "staged the package" not in result.stdout and not announced(result.stdout)
          and snapshot(out) == before and not own_temp(tmp))
    if commands is not None:
        ok = ok and logged(build) == commands
    first = message.splitlines()[0] if message else result.stdout.strip()
    check(ok, f"refused at {step}: {what}; output folder as it was ({first[:300]!r}"
              + (f", commands {logged(build)}" if commands is not None else "") + ")")


def accepted(public: Path, work: Path, what: str, build: Path, deb: Path, bin_dir: Path, out: Path) -> None:
    before = snapshot(out) or {}
    tmp = work / "tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    (build / "stub-log").unlink(missing_ok=True)
    result = deb_build(public, build, TAG, out, {"PATH": str(bin_dir), "TMPDIR": str(tmp)})
    after = snapshot(out) or {}
    new = sorted(set(after) - set(before))
    ok = (result.returncode == 0 and "staged the package and its two companions" in result.stdout
          and new == sorted(SET) and all(after[k] == v for k, v in before.items()) and not own_temp(tmp))
    check(ok, f"accepted: {what}: exactly {new} added, existing contents unchanged, temporary work removed "
              f"({(result.stderr or result.stdout).strip()[-300:]!r})")
    check(logged(build) == ["cmake", "cpack"], f"{what}: cmake --build, then cpack ({logged(build)})")
    if not ok:
        return
    check(sha256(out / DEB) == sha256(deb), f"{what}: the staged .deb is the package cpack made, byte for byte")
    check((out / SET[1]).read_text() == f"{sha256(deb)}  {DEB}\n", f"{what}: .sha256 holds its hash")
    if shutil.which("sha256sum"):
        check(run(["sha256sum", "-c", SET[1]], cwd=out).returncode == 0, f"{what}: sha256sum -c agrees")
    bom = json.loads((out / SET[2]).read_text())
    files = sorted(c["name"] for c in bom.get("components", []) if c["type"] == "file")
    check(bom.get("specVersion") == "1.6" and files == sorted(good_files())
          and bom.get("compositions", [{}])[0].get("aggregate") == "incomplete",
          f"{what}: the SBOM (unchanged deb_assets.py) lists the package's files and says it is incomplete")
    cmake_args = json.loads((build / "stub-log").read_text().splitlines()[0])[1]
    cpack_args = json.loads((build / "stub-log").read_text().splitlines()[1])[1]
    check(cmake_args == ["--build", str(build.resolve()), "--config", "Release"]
          and cpack_args[:4] == ["-G", "DEB", "-C", "Release"]
          and cpack_args[5] == str(build.resolve() / "CPackConfig.cmake") and cpack_args[6] == "-B",
          f"{what}: the commands' arguments reach them unsplit ({cmake_args}, {cpack_args[:6]})")


def check_fixtures(work: Path) -> None:
    public = stage_public(work)
    scan_public(public)
    full = make_bin(work, "full")
    good = fixture_deb(work, "good", good_files())

    # The positive pipeline: an existing output folder, then a new one.
    build = make_build(work, "good", deb=good)
    out = out_folder(work, "good")
    accepted(public, work, "fixture pipeline into an existing folder", build, good, full, out)
    refused(public, work, "a second run into a folder already holding the set", build, "inputs",
            ("already holds", DEB), bin_dir=full, out=out, commands=[])
    fresh = out_folder(work, "fresh", exists=False)
    accepted(public, work, "fixture pipeline into a new folder", build, good, full, fresh / "nested")

    # Paths with spaces and shell metacharacters are only arguments.
    odd = "odd $(touch PWNED) ; `touch PWNED` & | 'q' \"d\" *"
    build = make_build(work, odd, deb=good)
    out = out_folder(work, odd)
    accepted(public, work, "build and output paths with spaces and shell metacharacters", build, good, full, out)
    pwned = [p for p in (ROOT, work, public, build, out, Path.cwd()) if (p / "PWNED").exists()]
    check(not pwned, f"no shell ran the metacharacters (no PWNED file: {pwned or 'none'})")

    # Inputs: refused before any command runs.
    build = make_build(work, "inputs", deb=good)
    for tag in ("2026.11.0", "v2026.11", "v2026.13.0", "v2026.11.01", "v2026.11.0-rc1", " v2026.11.0",
                "v2026.11.0\n", "V2026.11.0", "v2026.11.0;touch PWNED", ""):
        refused(public, work, f"tag {tag!r}", build, "inputs", ("vYYYY.MM.N",), tag=tag, bin_dir=full, commands=[])
    refused(public, work, "a tag of another version", build, "inputs", ("XG_PACKAGE_VERSION is '2026.11.0'",),
            tag="v2026.11.1", bin_dir=full, commands=[])
    for label, cache, cpack, expect in (
            ("Debug build", {"CMAKE_BUILD_TYPE:STRING": "Debug"}, None, "CMAKE_BUILD_TYPE is 'Debug'"),
            ("no build type", {"CMAKE_BUILD_TYPE:STRING": None}, None, "CMAKE_BUILD_TYPE is None"),
            ("JP build", {"XG_VERSION:STRING": "JP"}, None, "XG_VERSION is 'JP'"),
            ("extras off", {"XG_EXTRAS:BOOL": "OFF"}, None, "XG_EXTRAS is 'OFF'"),
            ("no state schema", {"XG_STATE_SCHEMA:BOOL": "OFF"}, None, "XG_STATE_SCHEMA is 'OFF'"),
            ("cache of another version", {"XG_PACKAGE_VERSION:STRING": "0~dev"}, None, "XG_PACKAGE_VERSION is '0~dev'"),
            ("TGZ generator", None, {"CPACK_GENERATOR": "TGZ"}, "CPACK_GENERATOR is 'TGZ'"),
            ("CPack of another version", None, {"CPACK_DEBIAN_PACKAGE_VERSION": "2026.11.1"}, "CPACK_DEBIAN_PACKAGE_VERSION"),
            ("arm64", None, {"CPACK_DEBIAN_PACKAGE_ARCHITECTURE": "arm64"}, "ARCHITECTURE is 'arm64'"),
            ("another file name", None, {"CPACK_DEBIAN_FILE_NAME": "DEB-DEFAULT"}, "CPACK_DEBIAN_FILE_NAME"),
            ("prefix /usr/local", None, {"CPACK_PACKAGING_INSTALL_PREFIX": "/usr/local"}, "PREFIX is '/usr/local'"),
            ("shlibdeps off", None, {"CPACK_DEBIAN_PACKAGE_SHLIBDEPS": "OFF"}, "SHLIBDEPS is 'OFF'"),
            ("no libvulkan1", None, {"CPACK_DEBIAN_PACKAGE_DEPENDS": ""}, "lacks libvulkan1")):
        b = make_build(work, "cache-" + re.sub(r"\W+", "-", label), deb=good, cache=cache, cpack=cpack)
        refused(public, work, f"incompatible build: {label}", b, "inputs", (expect,), bin_dir=full, commands=[])
    for drop in ("CMakeCache.txt", "CPackConfig.cmake"):
        b = make_build(work, "drop-" + drop, deb=good, drop=drop)
        refused(public, work, f"a build folder without {drop}", b, "inputs", (f"no {drop}",), bin_dir=full, commands=[])
    refused(public, work, "a missing build folder", work / "builds/missing", "inputs", ("no such folder",), bin_dir=full)
    lone = work / "public-lone"
    shutil.rmtree(lone, ignore_errors=True)
    (lone / "packaging").mkdir(parents=True)
    shutil.copy2(PACKAGING / "deb_build.py", lone / "packaging/deb_build.py")
    refused(lone, work, "deb_assets.py not beside it", build, "inputs", ("deb_assets.py is not beside",),
            bin_dir=full, commands=[])

    # Missing commands: refused before any command runs.
    for tool in ("cmake", "cpack", "dpkg-deb", "dpkg"):
        refused(public, work, f"no {tool} on PATH", build, "tools", (tool,), bin_dir=make_bin(work, "no-" + tool, (tool,)),
                commands=[])

    # Build and cpack: their failures, and no package taken that this run's cpack did not make.
    b = make_build(work, "build-fail", "build-fail", good)
    refused(public, work, "the build fails", b, "build", ("exit status 2", "fixture: the build failed"),
            bin_dir=full, commands=["cmake"])
    b = make_build(work, "cpack-fail", "cpack-fail", good)
    refused(public, work, "cpack fails", b, "cpack", ("exit status 1", "fixture: cpack failed"),
            bin_dir=full, commands=["cmake", "cpack"])
    b = make_build(work, "no-package", "none", good)
    refused(public, work, "cpack succeeds but makes no package", b, "cpack", ("did not make", "no package"),
            bin_dir=full, commands=["cmake", "cpack"])
    b = make_build(work, "wrong-name", "wrong-name", good)
    refused(public, work, "cpack makes a package of another name", b, "cpack",
            ("did not make", f"xenogears-pc_{VERSION}_i386.deb"), bin_dir=full, commands=["cmake", "cpack"])
    b = make_build(work, "stale", "none", good)
    shutil.copy2(good, b / DEB)  # a package an earlier run left in the build folder
    refused(public, work, "only a stale package in the build folder", b, "cpack", ("did not make",),
            bin_dir=full, commands=["cmake", "cpack"])
    check(sha256(b / DEB) == sha256(good), "the stale package in the build folder is left as it was")

    # The package's own metadata and payload.
    for label, files, version, expect in (
            ("no executable", good_files(usr__bin__xenogears=None), VERSION, "missing usr/bin/xenogears"),
            ("no COPYING", good_files(**{f"{DOC}/COPYING".replace("/", "__"): None}), VERSION, f"missing {DOC}/COPYING"),
            ("no imgui licence", good_files(**{f"{DOC}/LICENSE-imgui.txt".replace("/", "__"): None}), VERSION,
             "missing usr/share/doc/xenogears-pc/LICENSE-imgui.txt"),
            ("no release file", good_files(**{RELEASE.replace("/", "__"): None}), VERSION, f"missing {RELEASE}"),
            ("no state schema", good_files(**{SCHEMA.replace("/", "__"): None}), VERSION, f"missing {SCHEMA}"),
            ("a state schema not in its packed form", good_files(**{SCHEMA.replace("/", "__"): (
                '{"version": "us"}\n', 0o644)}), VERSION, "not a state schema in its packed form"),
            ("a release file of the tarball", good_files(**{RELEASE.replace("/", "__"): (
                f"version={VERSION}\nkind=tarball\n", 0o644)}), VERSION, f"{RELEASE} is b'version={VERSION}\\nkind=tarball"),
            ("a release file of another version", good_files(**{RELEASE.replace("/", "__"): (
                "version=2026.11.1\nkind=deb\n", 0o644)}), VERSION, f"{RELEASE} is b'version=2026.11.1"),
            ("executable mode 0644", good_files(usr__bin__xenogears=("fixture\n", 0o644)), VERSION, "mode 0644"),
            ("an extra file", good_files(usr__share__xenogears__disc__bin=("fixture: not game data\n", 0o644)),
             VERSION, "unexpected usr/share/xenogears/disc/bin"),
            ("another Version", good_files(), "2026.11.1", "Version is '2026.11.1'")):
        deb = fixture_deb(work, "pkg-" + re.sub(r"\W+", "-", label), files, version=version)
        b = make_build(work, "pkg-" + re.sub(r"\W+", "-", label), deb=deb)
        refused(public, work, f"package: {label}", b, "package", (expect,), bin_dir=full, commands=["cmake", "cpack"])
    b = make_build(work, "pkg-dav1d", deb=good, cache={"XG_DAV1D_LIBRARY:FILEPATH": "/usr/lib/libdav1d.so"})
    refused(public, work, "package without the dav1d licence of a build linking dav1d", b, "package",
            ("missing usr/share/doc/xenogears-pc/LICENSE-dav1d.txt",), bin_dir=full, commands=["cmake", "cpack"])
    b = make_build(work, "pkg-enhanced", deb=good, cache={"XG_ENGINE:STRING": "enhanced"})
    refused(public, work, "package of the original engine for an enhanced build", b, "package",
            ("missing usr/bin/xenogears-enhanced", "unexpected usr/bin/xenogears",
             "missing usr/share/xenogears-pc/xenogears-enhanced.xgschema"), bin_dir=full,
            commands=["cmake", "cpack"])

    # Strict Depends and the companions.
    weak = fixture_deb(work, "depends-vulkan-only", good_files(), depends="libvulkan1")
    b = make_build(work, "depends-vulkan-only", deb=weak)
    refused(public, work, "Depends libvulkan1 alone (as this host's real package)", b, "companions",
            ("--require-deps", "no mandatory libsdl3-0", "no mandatory libc6"), bin_dir=full, commands=["cmake", "cpack"])
    unversioned = fixture_deb(work, "depends-unversioned", good_files(), depends="libc6, libsdl3-0, libvulkan1")
    b = make_build(work, "depends-unversioned", deb=unversioned)
    refused(public, work, "unversioned libc6 and libsdl3-0", b, "companions", ("has no minimum version",),
            bin_dir=full, commands=["cmake", "cpack"])
    build = make_build(work, "companions", deb=good)
    tail = 'if __name__ == "__main__":\n    sys.exit(main())\n'
    for label, replacement, expect in (
            ("deb_assets.py fails", tail.replace("main())", "3)"), "deb_assets.py exit status 3"),
            ("deb_assets.py writes no SBOM", tail.replace("    sys.exit(main())",
             "    status = main()\n    for p in Path(sys.argv[sys.argv.index('--out-dir') + 1]).glob('*.cdx.json'):\n"
             "        p.unlink()\n    sys.exit(status)"), "deb_assets.py wrote"),
            ("deb_assets.py writes a wrong .sha256", tail.replace("    sys.exit(main())",
             "    status = main()\n    for p in Path(sys.argv[sys.argv.index('--out-dir') + 1]).glob('*.sha256'):\n"
             "        p.write_text('0' * 64 + '  ' + p.name[:-7] + '\\n')\n    sys.exit(status)"), "does not hold")):
        broken = stage_public(work, "public-" + re.sub(r"\W+", "-", label))
        text = (broken / "packaging/deb_assets.py").read_text()
        check(text.endswith(tail), f"fixture companion helper: {label} (a labelled test change of a copy)")
        (broken / "packaging/deb_assets.py").write_text(text.removesuffix(tail) + replacement + "\n")
        refused(broken, work, label, build, "companions", (expect,), bin_dir=full, commands=["cmake", "cpack"])


def configure_real(build: Path, ver: str) -> bool:
    """This check's own real build folder, configured as tools/check_deb_package.py does, for TAG's version."""
    spec = importlib.util.spec_from_file_location("check_deb_package", ROOT / "tools/check_deb_package.py")
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    if not check(run([sys.executable, ROOT / "tools/pc_sources.py"]).returncode == 0, "tools/pc_sources.py"):
        return False
    configured = package.configure(build, ver, VERSION)
    return check(configured.returncode == 0 and (build / "CMakeCache.txt").is_file(),
                 f"real build {build.relative_to(ROOT)}: Release configure with XG_PACKAGE_VERSION={VERSION}")


def check_real(work: Path, ver: str) -> None:
    """A real build of this check's own, of TAG's version: never forced through strict mode."""
    build = work / "real-build"
    if not configure_real(build, ver):
        return
    version = dict(re.findall(r"^([\w-]+):\w+=(.*)$", (build / "CMakeCache.txt").read_text(), re.M)).get("XG_PACKAGE_VERSION", "")
    tag = TAG
    print(f"real build: XG_PACKAGE_VERSION {version!r}, tag {tag!r}")
    if not check(version == VERSION, f"the real build's XG_PACKAGE_VERSION is the tag's ({version!r})"):
        return
    public = stage_public(work, "public-real")
    out = out_folder(work, "real")
    before = snapshot(out)
    existing = {p.name: (sha256(p), p.stat().st_mtime_ns) for p in build.glob("*.deb")}
    tmp = work / "tmp-real"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    extra = []
    libc = run(["gcc", "-print-file-name=libc.so.6"]).stdout.strip()
    if run(["dpkg", "-S", libc]).returncode != 0 and run(["dpkg", "-S", "/usr" + libc]).returncode != 0:
        # As tools/check_deb_package.py: dpkg-shlibdeps here cannot place the GCC runtime otherwise.
        dirs = {str(Path(run(["gcc", f"-print-file-name={lib}"]).stdout.strip()).parent)
                for lib in ("libstdc++.so.6", "libgcc_s.so.1")}
        for folder in sorted(dirs):
            extra += ["--shlibdeps-private-dir", folder]
    result = deb_build(public, build, tag, out, dict(os.environ, TMPDIR=str(tmp)), *extra)
    print(result.stdout[-2500:], result.stderr[-2500:], sep="\n")
    check({p.name: (sha256(p), p.stat().st_mtime_ns) for p in build.glob("*.deb")} == existing,
          f"the packages already in the real build folder are left as they were ({sorted(existing)})")
    check(not own_temp(tmp), "real run: its temporary work is removed")
    if result.returncode == 0:
        names = sorted(set(snapshot(out)) - set(before))
        depends = run(["dpkg-deb", "-f", out / f"xenogears-pc_{version}_amd64.deb", "Depends"]).stdout.strip()
        print(f"NOTE  the real package passed the strict Depends check on this host: Depends {depends!r}")
        check(names == sorted([f"xenogears-pc_{version}_amd64.deb" + s for s in ("", ".sha256", ".cdx.json")]),
              f"real run staged exactly the set ({names})")
        return
    message = result.stderr.strip()
    check(result.returncode == 1 and "FAILED at step companions:" in message and "no mandatory libsdl3-0" in message
          and "staged the package" not in result.stdout and snapshot(out) == before,
          "real build refused at the companions step by --require-deps (its Depends lacks libsdl3-0 and libc6); "
          f"nothing staged, the output folder as it was ({message.splitlines()[0][:400]!r})")
    print("NOT PUBLISHABLE  this host's real package lacks the strict Depends: it is not a release package")


def report() -> int:
    if failures:
        print(f"check_deb_build: {len(failures)} check(s) failed")
        return 1
    print("check_deb_build: all checks pass (fixture pipeline and this host's real build only: "
          "no Ubuntu container build, installation, start or upload checked)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--no-real", action="store_true", help="the fixtures only, not the real build")
    args = ap.parse_args()
    work = ROOT / f"build/pc/deb-build-{args.ver}"
    work.mkdir(parents=True, exist_ok=True)
    for tool in ("dpkg-deb", "dpkg"):
        if not check(shutil.which(tool) is not None, f"{tool} on PATH (fixtures are made and read with it)"):
            return report()
    check_fixtures(work)
    if not args.no_real:
        check_real(work, args.ver)
    return report()


if __name__ == "__main__":
    sys.exit(main())
