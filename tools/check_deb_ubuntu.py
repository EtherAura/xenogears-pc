#!/usr/bin/env python3
"""Check packaging/deb_ubuntu.py (xenogears-pc): the Ubuntu 26.04 builder's entry point, fail-closed.

    python tools/check_deb_ubuntu.py --ver us

1. Public layout: deb_ubuntu.py beside byte-identical copies of deb_build.py
   and deb_assets.py in build/pc/deb-ubuntu-<ver>/public/packaging/, standard
   library only, the publisher's date, time and private-text scan (split
   strings joined; this check too), exactly its four options, no cache, CPack
   or tag parser of its own (deb_assets.py's rule and names, by path, shown
   by a labelled copy with another rule), no deletion call, no
   --shlibdeps-private-dir, the exact list of refused variables and a
   docstring that names it, both written places and exit statuses 0, 1 and 2.
2. Bytecode control: a real Python importing a sibling module writes
   __pycache__ without PYTHONDONTWRITEBYTECODE and none with it, so the
   fixtures below can see a lost variable.
3. The real CLI on this host, run with python -I from another folder with no
   harness: usage errors exit 2; a complete command line is refused at the
   environment step (this is not an Ubuntu 26.04 builder), exit 1, nothing
   created, the output folder as it was.
4. FIXTURES (not an Ubuntu build, not ABI or install evidence): a test-only
   harness loads the public deb_ubuntu.py by path and points its os-release
   at a labelled fixture file; cmake, cpack, ninja and dpkg-shlibdeps on PATH
   are stubs that log their arguments and PYTHONDONTWRITEBYTECODE outside the
   build and output folders and, for configure, build and cpack, start a real
   Python (not isolated) that imports a sibling module in the fixture source
   tree's tools/; dpkg answers --print-architecture from the fixture and
   passes everything else to the real dpkg; deb_build.py and deb_assets.py
   run unchanged with the real dpkg-deb. Accepted: public and private
   layouts, an empty build folder, build and output inside the source tree's
   build/ (only the documented paths change, no __pycache__), metacharacter
   paths given relative to another working folder, and the variables that
   are not on the refused list. The fixture auditor checks that its input
   is the executable from the synthetic package.
   Refused (exit 1, the step named, no success line, output bytes and times
   unchanged; before configure also the build folder unchanged and no command
   run): every non-empty build folder (contaminated caches, a compatible
   cache, unrelated and hidden-only files and folders, symbolic links),
   unsafe paths, missing inputs and helpers, bad tags, wrong OS, version,
   architecture, each refused variable, missing tools, configure failure and
   deb_build.py's own failures (build, cpack, inputs, package, payload, strict
   Depends). Two failures injected by the harness after deb_build.py's real
   staging (its exit status changed; one staged file taken away): exit 1, no
   success line, the message names what the output folder holds, and nothing
   is deleted.
5. Baseline amd64 (FIXTURES, the contract only: not compiler output, not the
   Ubuntu compiler's default): with copies of this repository's real CMake
   files in a fixture tree, the configure argv the unchanged deb_ubuntu.py
   logs passes no XG_MARCH but an empty one, no *_FLAGS, toolchain, compiler
   or CPU option, and the CMake files it points -S at keep XG_MARCH's cache
   default empty and hold no CPU-specific option (-march, -mtune, -mcpu,
   -mavx..., x86-64-v2..v4, native) outside the guarded
   -march=${XG_MARCH}. Labelled mutations of copies (a native or x86-64-v3
   default, XG_MARCH set again, an unguarded -march, a global -mtune, -mavx2
   in deb.cmake, deb_ubuntu.py configuring with -DXG_MARCH=native,
   x86-64-v3 or -DCMAKE_C_FLAGS=-mtune=...) are each caught for their own
   reason; an explicit empty -DXG_MARCH= is not.
6. C standard: port/CMakeLists.txt requires GNU C17 for the port's own C
   (CMAKE_C_STANDARD 17, CMAKE_C_STANDARD_REQUIRED ON, CMAKE_C_EXTENSIONS
   ON), each set once, before the first target or include, and no port CMake
   file sets another C standard or -std= (the game C's -std=gnu89 and the
   targets' c_std_11 minimums aside).
   REAL configure on this host (not a build, not the Ubuntu compiler): cmake
   with the configure options deb_ubuntu.py passed in 5, compile commands
   exported, on the real port/: every C compile command has -std=gnu17, the
   game C's followed by its own -std=gnu89, hw.c's (C11 atomics) gnu17 alone.
   Labelled mutations of copies (the pin missing, C11, C23, not required,
   extensions off, set again, set after the first targets, a target's own
   C_STANDARD, a c_std_23 minimum, -std=c2x in gpu.cmake) are each caught for their own reason,
   and those that change the compile commands also by a real configure of a
   tree with the mutated CMakeLists.txt.

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
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGING = ROOT / "tools/publish/repo-port/packaging"
HELPERS = ("deb_build.py", "deb_assets.py", "deb_ubuntu.py")
SOURCE_INPUTS = ("port/CMakeLists.txt", "port/cmake/deb.cmake", "tools/pc_link.py", "build/pc/sources.cmake")
# The list deb_ubuntu.py refuses, written out here so a change to it is seen.
REFUSED = ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "DESTDIR",
           "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET",
           "CMAKE_TOOLCHAIN_FILE", "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES",
           "CMAKE_PREFIX_PATH", "CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER",
           "CMAKE_C_LINKER_LAUNCHER", "CMAKE_CXX_LINKER_LAUNCHER")
NOT_REFUSED = ("SDL3_DIR", "SDL3_ROOT", "CMAKE_LIBRARY_PATH", "PKG_CONFIG_PATH")
NO_BYTECODE = "PYTHONDONTWRITEBYTECODE"
PREFLIGHT = ("inputs", "environment", "tools", "architecture")
NOT_STARTED = "nothing staged (deb_build.py was not started)"
NONE_HELD = "nothing staged (--out-dir holds none of the package's three names)"

spec = importlib.util.spec_from_file_location("check_deb_build", ROOT / "tools/check_deb_build.py")
cdb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdb)
check, run, TAG, VERSION, DEB, SET = cdb.check, cdb.run, cdb.TAG, cdb.VERSION, cdb.DEB, cdb.SET

HARNESS = '''# TEST-ONLY harness (tools/check_deb_ubuntu.py): not published, not a production option.
import importlib.util, sys
from pathlib import Path
script, os_release, inject = sys.argv[1:4]
sys.dont_write_bytecode = True  # as when deb_ubuntu.py runs as a script: no cache of itself
spec = importlib.util.spec_from_file_location("deb_ubuntu", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.OS_RELEASE = (Path(os_release),)  # a labelled fixture file, not this host's
if inject != "none":  # a failure after deb_build.py's real staging, which the unchanged helper cannot produce
    real_run = module.run
    def run(step, argv, env):
        result = real_run(step, argv, env)
        if step == "package":
            out = Path(str(argv[argv.index("--out-dir") + 1]))
            if inject == "late-status":
                result.returncode = 3
            elif inject == "drop-sha256":  # stands for another actor taking a staged file away
                for path in out.iterdir():
                    if path.name.endswith(".deb.sha256"):
                        path.unlink()
        return result
    module.run = run
sys.argv = [script] + sys.argv[4:]
sys.exit(module.main())
'''

STUB = '''#!@PYTHON@ -I
# FIXTURE stub (tools/check_deb_ubuntu.py): not the real @TOOL@.
import json, os, re, shutil, subprocess, sys
from pathlib import Path
HERE = Path(@HERE@)
TOOL = "@TOOL@"
args = sys.argv[1:]
mode = json.loads((HERE / "mode.json").read_text())
if TOOL == "dpkg" and args != ["--print-architecture"]:
    os.execv(@DPKG@, ["dpkg"] + args)
with open(HERE / "log.jsonl", "a") as log:
    log.write(json.dumps([TOOL, args, os.environ.get("PYTHONDONTWRITEBYTECODE")]) + "\\n")


def nested(label):
    """A real Python, not isolated, importing a sibling module: as the build's own Python tools do."""
    if mode["nested"]:
        done = subprocess.run([sys.executable, mode["nested"], str(HERE / "nested.log"), label], timeout=60)
        if done.returncode != 0:
            sys.exit(97)


if TOOL == "dpkg":
    if mode["arch"] is None:
        print("fixture: dpkg failed", file=sys.stderr)
        sys.exit(2)
    print(mode["arch"])
elif TOOL == "cmake" and "-S" in args:
    if mode["configure"] == "fail":
        print("fixture: configure failed", file=sys.stderr)
        sys.exit(1)
    nested("configure")
    build = Path(args[args.index("-B") + 1])
    build.mkdir(parents=True, exist_ok=True)
    version = [a for a in args if a.startswith("-DXG_PACKAGE_VERSION=")][0].split("=", 1)[1]
    cache = dict(mode["cache"], **{"XG_PACKAGE_VERSION:STRING": version})
    (build / "CMakeCache.txt").write_text("# fixture cache\\n" + "".join(f"{k}={v}\\n" for k, v in cache.items()))
    (build / "CPackConfig.cmake").write_text("# fixture\\n" + "".join(f'set({k} "{v}")\\n' for k, v in mode["cpack"].items()))
elif TOOL == "cmake":
    if mode["build"] == "fail":
        print("fixture: the build failed", file=sys.stderr)
        sys.exit(2)
    nested("build")
elif TOOL == "cpack":
    if mode["cpack_mode"] == "fail":
        print("fixture: cpack failed", file=sys.stderr)
        sys.exit(1)
    nested("cpack")
    build = Path(args[args.index("--config") + 1]).parent
    out = Path(args[args.index("-B") + 1])
    out.mkdir(parents=True, exist_ok=True)
    name = re.search(r'^set\\(CPACK_DEBIAN_FILE_NAME "(.*)"\\)$', (build / "CPackConfig.cmake").read_text(), re.M)[1]
    shutil.copy2(mode["deb"], out / name)
'''
STUBBED = ("cmake", "cpack", "ninja", "dpkg-shlibdeps", "dpkg")
GOOD_OS = 'NAME="Ubuntu"\nID=ubuntu\nVERSION_ID="26.04"\n# fixture os-release, not a real system\n'
NESTED = ("# FIXTURE (tools/check_deb_ubuntu.py): stands for a build tool that imports a module beside it.\n"
          "import sys\nimport fixture_nested_mod\n"
          "with open(sys.argv[1], 'a') as log:\n"
          "    log.write(f'{sys.argv[2]} {sys.flags.dont_write_bytecode} {fixture_nested_mod.VALUE}\\n')\n")
NESTED_MOD = "# FIXTURE module (tools/check_deb_ubuntu.py)\nVALUE = 'imported'\n"
FIXTURE_ELF = "\x7fELF\x02\x01 synthetic packaged bytes\n"
AUDIT_STUB = f'''# FIXTURE auditor: checks only argv and the bytes of the packaged fixture.
from pathlib import Path
import sys
flag, ver_flag, ver, build_flag, build, exe_flag, exe = sys.argv[1:]
if (flag, ver_flag, ver, build_flag, exe_flag) != ("--dist", "--ver", "us", "--build", "--exe") or \
        not (Path(build) / "CMakeCache.txt").is_file() or Path(exe).read_bytes() != {FIXTURE_ELF.encode()!r}:
    sys.exit(13)
print(f"retail module data in {{exe}} (US): 1 module images, runs of >= 16 bytes")
print("payload ok: fixture bytes supplied to the stub auditor")
'''
# Options that pick a CPU above baseline x86-64 (or name one: native, x86-64-v2..v4).
CPU_OPTION = re.compile(r"-m(arch|tune|cpu)\b|-m(avx|sse|fma|bmi|f16c|popcnt|lzcnt|movbe|pclmul|aes)\w*|"
                        r"x86-64-v[234]|\bnative\b", re.I)
MARCH_DEFAULT = re.compile(r'set\(XG_MARCH "([^"]*)" CACHE STRING ')
MARCH_GUARD = re.compile(r"^if\(XG_MARCH\)\n(?:[ \t]*target_compile_options\(\w+ PRIVATE -march=\$\{XG_MARCH\}\)\n)+"
                         r"[ \t]*endif\(\)\n", re.M)
NOT_ON_DEB_PATH = ("aarch64-linux-gnu.cmake", "mingw-w64-x86_64.cmake")  # cross toolchain files; no toolchain is passed
# The port's C dialect: required GNU C17, set before any target (port/CMakeLists.txt).
C_PIN = ("set(CMAKE_C_STANDARD 17)", "set(CMAKE_C_STANDARD_REQUIRED ON)", "set(CMAKE_C_EXTENSIONS ON)")
C_PIN_LINE = re.compile(r"set\(CMAKE_C_(STANDARD|STANDARD_REQUIRED|EXTENSIONS)\b")
C_DIALECT, GAME_DIALECT = "-std=gnu17", "-std=gnu89"
GAME_OPTIONS = "set(XG_GAME_OPTIONS -std=gnu89 "  # the game C's own dialect, after the port's on its command lines
C_SETTING = re.compile(r"CMAKE_C_STANDARD|CMAKE_C_EXTENSIONS|\bC_STANDARD\b|\bC_EXTENSIONS\b|\bc_std_\w+|-std=")
# A target's minimum C standard at or below C17 (the platform layer and GPU tests ask for C11): compatible with the pin.
C_MINIMUM = re.compile(r"target_compile_features\([\w${}]+ PRIVATE c_std_(90|99|11|17)\)$")
FIRST_TARGET = re.compile(r"^[ \t]*(add_executable|add_library|add_custom_target|include|function|macro)\(", re.M)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def slug(text: str) -> str:
    return re.sub(r"\W+", "-", text)


def fresh(path: Path) -> Path:
    if path.is_symlink() or path.is_file():
        path.unlink()
    shutil.rmtree(path, ignore_errors=True)
    return path


def entry(path: Path) -> tuple:
    stat = path.lstat()
    if path.is_symlink():
        return ("link", os.readlink(path), stat.st_mtime_ns)
    if path.is_dir():
        return ("dir", "", stat.st_mtime_ns)
    return ("file", path.read_bytes(), stat.st_mtime_ns)


def snap(folder: Path) -> dict | None:
    """Kind, bytes (or link target) and modification time of a path and everything under it; links not followed."""
    if not folder.exists() and not folder.is_symlink():
        return None
    result = {".": entry(folder)}
    if folder.is_dir() and not folder.is_symlink():
        for top, dirs, files in os.walk(folder):
            for name in dirs + files:
                path = Path(top) / name
                result[path.relative_to(folder).as_posix()] = entry(path)
    return result


def pycaches(folder: Path) -> list[str]:
    return sorted(str(Path(top).relative_to(folder) / "__pycache__") for top, dirs, _ in os.walk(folder)
                  if "__pycache__" in dirs)


def make_bin(work: Path, label: str, deb: Path, without: tuple = (), arch: str | None = "amd64",
             configure: str = "ok", build: str = "ok", cpack_mode: str = "package", cpack: dict | None = None,
             nested: Path | None = None) -> Path:
    folder = fresh(work / "bin" / label)
    folder.mkdir(parents=True)
    for tool in STUBBED:
        if tool not in without:
            text = (STUB.replace("@PYTHON@", sys.executable).replace("@TOOL@", tool)
                    .replace("@HERE@", repr(str(folder))).replace("@DPKG@", repr(shutil.which("dpkg"))))
            (folder / tool).write_text(text)
            (folder / tool).chmod(0o755)
    for tool in cdb.REAL_TOOLS:
        if tool not in without and tool != "dpkg" and shutil.which(tool):
            (folder / tool).symlink_to(shutil.which(tool))
    (folder / "mode.json").write_text(json.dumps({
        "arch": arch, "configure": configure, "build": build, "cpack_mode": cpack_mode, "deb": str(deb),
        "nested": str(nested) if nested else None,
        "cache": dict(cdb.CACHE), "cpack": dict(cdb.CPACK, **(cpack or {}))}))
    return folder


def logged(bin_dir: Path) -> list[list]:
    log = bin_dir / "log.jsonl"
    return [json.loads(line) for line in log.read_text().splitlines()] if log.is_file() else []


def names(entries: list[list]) -> list[str]:
    return [("configure" if "-S" in e[1] else "build") if e[0] == "cmake" else ("arch" if e[0] == "dpkg" else e[0])
            for e in entries]


def make_source(work: Path, label: str, layout: str = "public") -> tuple[Path, Path]:
    """A labelled fixture source tree and the deb_ubuntu.py of its layout (helpers byte-identical)."""
    src = fresh(work / "sources" / label)
    for name in SOURCE_INPUTS:
        (src / name).parent.mkdir(parents=True, exist_ok=True)
        (src / name).write_text(f"# fixture {name}, not the real file\n")
    shutil.copy2(ROOT / "port/cmake/deb.cmake", src / "port/cmake/deb.cmake")
    (src / "tools/fixture_nested.py").write_text(NESTED)
    (src / "tools/fixture_nested_mod.py").write_text(NESTED_MOD)
    (src / "tools/payloadcheck.py").write_text(AUDIT_STUB)
    (src / "src").mkdir()
    (src / "src/fixture.c").write_text("/* fixture source, not game code */\n")
    (src / "NOTES.txt").write_text("unrelated fixture file\n")
    packaging = src / ("packaging" if layout == "public" else "tools/publish/repo-port/packaging")
    packaging.mkdir(parents=True, exist_ok=True)
    for helper in HELPERS:
        shutil.copy2(PACKAGING / helper, packaging / helper)
    return src, packaging / "deb_ubuntu.py"


def out_folder(work: Path, label: str, exists: bool = True, at: Path | None = None) -> Path:
    out = fresh(at or work / "outs" / label)
    if exists:
        (out / "older").mkdir(parents=True)
        (out / "KEEP").write_text("existing content: deb_ubuntu must leave this alone\n")
        (out / "older/xenogears-pc_0.0.1_amd64.deb").write_text("an unrelated older file (fixture)\n")
    return out


class Runner:
    def __init__(self, work: Path):
        self.work = work
        self.harness = work / "harness/fake_os_release.py"
        self.harness.parent.mkdir(parents=True, exist_ok=True)
        self.harness.write_text(HARNESS)
        self.osdir = work / "os-release"
        self.osdir.mkdir(exist_ok=True)

    def invoke(self, script: Path, src, build, out, tag: str, bin_dir: Path, os_text: str | None,
               cwd: Path, extra_env: dict | None, label: str, inject: str = "none"):
        tmp = fresh(self.work / "tmp")
        tmp.mkdir()
        for name in ("log.jsonl", "nested.log"):
            (bin_dir / name).unlink(missing_ok=True)
        env = dict({"PATH": str(bin_dir), "TMPDIR": str(tmp)}, **(extra_env or {}))
        osrel = self.osdir / slug(label)
        osrel.unlink(missing_ok=True)
        if os_text is not None:
            osrel.write_text(os_text)
        result = run([sys.executable, "-I", self.harness, script, osrel, inject, "--source", src,
                      "--build-dir", build, "--tag", tag, "--out-dir", out], cwd=cwd, env=env)
        return result, tmp

    def refused(self, what: str, script: Path, src: Path, build: Path, out: Path, step: str, expect: tuple, *,
                bin_dir: Path, tag: str = TAG, os_text: str | None = GOOD_OS, extra_env: dict | None = None,
                commands: list[str] | None = None, also: tuple = ()) -> None:
        preflight = step in PREFLIGHT
        before_out, before_build, before_also = snap(out), snap(build), [snap(p) for p in also]
        result, tmp = self.invoke(script, src, build, out, tag, bin_dir, os_text, self.work, extra_env, what)
        message = result.stderr.strip()
        entries = logged(bin_dir)
        expect_cmds = commands if commands is not None else (["arch"] if step == "architecture" else [])
        note = NONE_HELD if step.startswith("package") else NOT_STARTED
        ok = (result.returncode == 1 and f"deb_ubuntu: FAILED at step {step}:" in message
              and all(e in message for e in expect) and f"deb_ubuntu: {note}" in message and "HOLDS" not in message
              and "staged the package" not in result.stdout
              and snap(out) == before_out and [snap(p) for p in also] == before_also and not cdb.own_temp(tmp)
              and names(entries) == expect_cmds and all(e[2] == "1" for e in entries))
        if preflight:
            echoed = [l for l in result.stdout.splitlines() if l.startswith("$ ")]
            ok = ok and snap(build) == before_build and len(echoed) == (1 if step == "architecture" else 0)
        first = next((l for l in message.splitlines() if "FAILED" in l), message[:200])
        check(ok, f"refused at {step}: {what}; exit 1, output{' and build' if preflight else ''} bytes and times "
                  f"as they were, commands {names(entries)} ({first[:300]!r})")

    def accepted(self, what: str, script: Path, src, build, out, *, bin_dir: Path, deb: Path,
                 cwd: Path | None = None, abs_src: Path | None = None, abs_build: Path | None = None,
                 abs_out: Path | None = None, extra_env: dict | None = None, nested: bool = False) -> bool:
        abs_src, abs_build, abs_out = abs_src or src, abs_build or build, abs_out or out
        before = snap(abs_out) or {}
        result, tmp = self.invoke(script, src, build, out, TAG, bin_dir, GOOD_OS, cwd or self.work, extra_env, what)
        after = snap(abs_out) or {}
        new = sorted(set(after) - set(before) - {"."})  # "." is the output folder itself, when it is new
        ok = (result.returncode == 0 and "deb_ubuntu: staged the package and its two companions" in result.stdout
              and new == sorted(SET) and all(after[k] == v for k, v in before.items() if k != ".")
              and not cdb.own_temp(tmp))
        check(ok, f"FIXTURE accepted: {what}: exactly {new} added, existing output unchanged "
                  f"({(result.stderr or result.stdout).strip()[-300:]!r})")
        entries = logged(bin_dir)
        check(names(entries) == ["arch", "configure", "build", "cpack"],
              f"{what}: dpkg --print-architecture, configure, deb_build.py's build and cpack ({names(entries)})")
        if not ok or len(entries) != 4:
            return False
        check([e[2] for e in entries[1:]] == ["1", "1", "1"],
              f"{what}: configure, build and cpack each received {NO_BYTECODE}=1 ({[e[2] for e in entries]})")
        if nested:
            lines = (bin_dir / "nested.log").read_text().splitlines() if (bin_dir / "nested.log").is_file() else []
            check(lines == [f"{step} 1 imported" for step in ("configure", "build", "cpack")]
                  and not pycaches(abs_src),
                  f"{what}: a real Python started by configure, build and cpack imported a module in the source "
                  f"tree's tools/ with bytecode writing off; no __pycache__ in the tree ({lines}, {pycaches(abs_src) or 'none'})")
        rb = abs_build.resolve()
        check(entries[1][1] == ["-S", str(abs_src.resolve() / "port"), "-B", str(rb), "-G", "Ninja",
                                "-DCMAKE_BUILD_TYPE=Release", "-DXG_VERSION=US", "-DXG_EXTRAS=ON",
                                f"-DXG_PACKAGE_VERSION={VERSION}"],
              f"{what}: the shipping configure arguments, unsplit ({entries[1][1]})")
        check(entries[2][1] == ["--build", str(rb), "--config", "Release"]
              and entries[3][1][:4] == ["-G", "DEB", "-C", "Release"]
              and not any("PRIVATE_DIRS" in a for a in entries[3][1]),
              f"{what}: deb_build.py's build and cpack, no private shlibdeps folders ({entries[3][1]})")
        check(sha256(abs_out / DEB) == sha256(deb) and (abs_out / SET[1]).read_text() == f"{sha256(deb)}  {DEB}\n",
              f"{what}: the staged .deb is the package cpack made; its .sha256 holds its hash")
        return True

    def late(self, what: str, inject: str, script: Path, src: Path, build: Path, out: Path, *, bin_dir: Path,
             deb: Path, held: list[str], expect: tuple) -> None:
        """A failure injected by the harness after deb_build.py really staged (TEST-ONLY)."""
        before = snap(out)
        result, tmp = self.invoke(script, src, build, out, TAG, bin_dir, GOOD_OS, self.work, None, what, inject)
        message = result.stderr.strip()
        after = snap(out)
        new = sorted(set(after) - set(before))
        lacks = [n for n in SET if n not in held]
        note = next((l for l in message.splitlines() if "HOLDS" in l), "")
        ok = (result.returncode == 1 and "deb_ubuntu: FAILED at step package:" in message
              and all(e in message for e in expect)
              and "deb_ubuntu: staged the package" not in result.stdout and "nothing staged" not in message
              and note.startswith(f"deb_ubuntu: --out-dir {out.resolve()} HOLDS {', '.join(held)} (")
              and "nothing is deleted" in note and "not a release package set" in note
              and (f"it lacks {', '.join(lacks)}" in note if lacks else "it lacks" not in note)
              and new == sorted(held) and all(after[k] == v for k, v in before.items() if k != ".")
              and (DEB not in held or sha256(out / DEB) == sha256(deb)))
        check(ok, f"INJECTED failure after staging ({what}): exit 1, no success line, never 'nothing staged'; the "
                  f"message names what the output holds; unrelated output unchanged, nothing deleted: {new} "
                  f"({note[:400]!r})")


def check_public(work: Path) -> Path:
    public = fresh(work / "public")
    (public / "packaging").mkdir(parents=True)
    for helper in HELPERS:
        shutil.copy2(PACKAGING / helper, public / "packaging" / helper)
        check(sha256(public / "packaging" / helper) == sha256(PACKAGING / helper), f"public {helper} is byte-identical")
    cdb.HELPERS = HELPERS  # the publisher's scan, now over the three files
    cdb.scan_public(public)
    own = fresh(work / "self-scan")
    (own / "packaging").mkdir(parents=True)
    shutil.copy2(Path(__file__).resolve(), own / "packaging" / Path(__file__).name)
    cdb.HELPERS = (Path(__file__).name,)  # and over this check
    cdb.scan_public(own)
    cdb.HELPERS = HELPERS

    script = public / "packaging/deb_ubuntu.py"
    text = script.read_text()
    tree = ast.parse(text)
    imports = {a.name for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
    imports |= {n.module for n in ast.walk(tree) if isinstance(n, ast.ImportFrom)}
    check("re" not in imports and "CMakeCache" not in text and "CPackConfig" not in text and "cmake_sets" not in text
          and "finditer" not in text and "findall" not in text and "v(20" not in text and "xenogears-pc_{" not in text
          and '"xenogears-pc"' not in text and '"amd64.deb"' not in text,
          "deb_ubuntu.py parses no cache, CPack settings or tag itself and holds no package-name rule "
          "(no re, no cache file names)")
    check(all(s in text for s in ("assets.version_from_tag(", "assets.PACKAGE", "assets.ARCH", "spec_from_file_location")),
          "deb_ubuntu.py takes the tag rule and package names from deb_assets.py, loaded by path")
    used = {n.attr for n in ast.walk(tree) if isinstance(n, ast.Attribute)} | \
           {n.id for n in ast.walk(tree) if isinstance(n, ast.Name)}
    deleting = sorted(used & {"rmtree", "unlink", "rmdir", "remove", "removedirs", "rename", "replace", "move"})
    check(not deleting, f"deb_ubuntu.py calls no deletion or move ({deleting or 'none'})")
    doc = tree.body[0].value.value  # the raw docstring (ast.get_docstring dedents it)
    strings = [n.value for n in ast.walk(tree) if isinstance(n, ast.Constant) and isinstance(n.value, str) and n.value != doc]
    check(not [s for s in strings if "shlibdeps-private" in s or "PRIVATE_DIRS" in s],
          "deb_ubuntu.py never passes --shlibdeps-private-dir (the non-Debian workaround)")
    options = sorted(n.args[0].value for n in ast.walk(tree) if isinstance(n, ast.Call)
                     and getattr(n.func, "attr", "") == "add_argument")
    check(options == ["--build-dir", "--out-dir", "--source", "--tag"], f"exactly four options ({options})")

    flat = " ".join(doc.split())
    refused = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                   and getattr(n.targets[0], "id", "") == "REFUSED_VARIABLES")
    check(refused == REFUSED and not set(NOT_REFUSED) & set(refused),
          f"REFUSED_VARIABLES is exactly the existing list of {len(REFUSED)}; {', '.join(NOT_REFUSED)} are not in it")
    listed = flat[flat.index("none of exactly these variables"):flat.index("That list is all that is refused")]
    in_doc = tuple(re.findall(r"\b[A-Z][A-Z0-9_]+\b", listed))
    after_list = flat[flat.index("That list is all that is refused"):flat.index("tools cmake, ninja")]
    check(in_doc == REFUSED and all(v in after_list for v in NOT_REFUSED) and "are not looked at" in after_list
          and "not every variable" in after_list,
          f"the docstring names the refused list exactly, says it is not exhaustive and that {', '.join(NOT_REFUSED)} "
          f"are not refused ({len(in_doc)} names)")
    check("0 only when every step passed" in flat and "1 when a step failed" in flat
          and "2 for a command line the option parser refuses" in flat and "1 otherwise" not in flat,
          "the docstring documents exit statuses 0, 1 and 2")
    check("must be a new or an empty folder" in flat and "This script itself creates and deletes nothing" in flat
          and "Its commands write two places: the build folder" in flat and "and --out-dir" in flat
          and "Both may sit under the source tree's build/ folder" in flat
          and "a run can write inside the source tree there" in flat,
          "the docstring names both written places, also under the source tree's build/, and the new-or-empty rule")
    check(f"{NO_BYTECODE}=1" in flat and "which their children inherit" in flat
          and "does not read that variable" in flat and "no bytecode cache" not in flat
          and "Nothing else in the source tree is written" not in flat and "Nothing is written" not in flat,
          "the docstring states the bytecode measure it enforces and its limit, with no promise about the tree")
    check("a failure after its staging" in flat and "leaves staged files there" in flat
          and "says what --out-dir holds of the three names" in flat and "nothing is deleted" in flat
          and "--out-dir is as it was. Exit" not in flat,
          "the docstring says a failure after staging leaves staged files, which the message names")
    check(text.count(f'env[NO_BYTECODE] = "1"') == 1 and "dict(os.environ)" in text
          and text.count("], env)") == 3 and "sys.dont_write_bytecode = True" in text and '"-I", "-B", BUILD_HELPER' in text,
          "deb_ubuntu.py gives its three commands a copy of the environment with the variable set, and keeps "
          "its own protection (no cache of deb_assets.py, deb_build.py with -B)")
    return script


def check_bytecode_control(work: Path) -> None:
    """The nested import the stubs make writes __pycache__ exactly when the variable is missing or empty."""
    for label, env, want in (("without the variable", {}, True), ("with it empty", {NO_BYTECODE: ""}, True),
                             ("with it set to 1", {NO_BYTECODE: "1"}, False)):
        folder = fresh(work / "bytecode-control" / slug(label))
        (folder / "tools").mkdir(parents=True)
        (folder / "tools/fixture_nested.py").write_text(NESTED)
        (folder / "tools/fixture_nested_mod.py").write_text(NESTED_MOD)
        result = run([sys.executable, folder / "tools/fixture_nested.py", folder / "log", "control"],
                     cwd=work, env=dict({"PATH": os.environ.get("PATH", "")}, **env))
        check(result.returncode == 0 and bool(pycaches(folder)) == want,
              f"control: a real Python importing a sibling module {label} "
              f"{'writes' if want else 'writes no'} __pycache__ ({pycaches(folder) or 'none'})")


def check_real_host(work: Path) -> None:
    """The real CLI, no harness, this host's own os-release and environment."""
    os_text = next((p.read_text() for p in (Path("/etc/os-release"), Path("/usr/lib/os-release")) if p.is_file()), "")
    host = {k: v.strip("\"'") for k, _, v in (l.partition("=") for l in os_text.splitlines()) if v}
    print(f"this host: ID {host.get('ID')!r}, VERSION_ID {host.get('VERSION_ID')!r}")
    src, script = make_source(work, "real-host")
    elsewhere = work / "real-host/elsewhere"
    elsewhere.mkdir(parents=True, exist_ok=True)
    build = fresh(work / "real-host/build")
    out = out_folder(work, "real-host")
    before, before_src = snap(out), snap(src)
    full = ["--source", src, "--build-dir", build, "--tag", TAG, "--out-dir", out]

    # Usage errors: argparse's exit status 2, before any step (so also on this host).
    for what, argv in (("no option", []), ("no --tag", full[:4] + full[6:]), ("no --source", full[2:]),
                       ("an unknown option", full + ["--clean"]),
                       ("--shlibdeps-private-dir (deb_build.py's, not offered here)", full + ["--shlibdeps-private-dir", "/usr/lib"]),
                       ("an abbreviated option", full[:6] + ["--out", out]),
                       ("a stray argument", full + ["extra"])):
        result = run([sys.executable, "-I", script, *argv], cwd=elsewhere, env=dict(os.environ))
        check(result.returncode == 2 and "usage:" in result.stderr and "FAILED at step" not in result.stderr
              and not result.stdout.strip() and not build.exists() and snap(out) == before and snap(src) == before_src,
              f"REAL: usage error ({what}): exit status 2, no step run, nothing created "
              f"({result.stderr.strip().splitlines()[-1][:200] if result.stderr.strip() else ''!r})")

    if host.get("ID") == "ubuntu" and host.get("VERSION_ID") == "26.04":
        print("NOTE  this host says Ubuntu 26.04: the real CLI is not run here (it would start a real build)")
        return
    sources = [("a fixture source tree", src)]
    if (ROOT / "build/pc/sources.cmake").is_file():
        sources.append(("this repository's tree", ROOT))
    else:
        print("NOTE  no build/pc/sources.cmake here: the real CLI is run on the fixture source tree only")
    for what, source in sources:
        held = sorted(p.name for p in script.parent.iterdir())
        result = run([sys.executable, "-I", script, "--source", source, "--build-dir", build, "--tag", TAG,
                      "--out-dir", out], cwd=elsewhere, env=dict(os.environ))
        message = result.stderr.strip()
        check(result.returncode == 1 and "deb_ubuntu: FAILED at step environment:" in message
              and "this is not an Ubuntu 26.04 builder" in message and f"deb_ubuntu: {NOT_STARTED}" in message
              and "staged the package" not in result.stdout and not build.exists() and snap(out) == before
              and not [l for l in result.stdout.splitlines() if l.startswith("$ ")]
              and sorted(p.name for p in script.parent.iterdir()) == held and snap(src) == before_src,
              f"REAL: the CLI refuses this host at the environment step with {what} as the source: exit 1, no command "
              f"run, no build folder, nothing new beside the script, output as it was "
              f"({message.splitlines()[0][:300] if message else result.stdout[:200]!r})")


def check_fixtures(work: Path) -> None:
    r = Runner(work)
    audited_files = cdb.good_files(usr__bin__xenogears=(FIXTURE_ELF, 0o755))
    good = cdb.fixture_deb(work, "good", audited_files)
    full = make_bin(work, "full", good)

    # Accepted: the public layout, then the private layout into an empty build folder and a new output folder.
    src, script = make_source(work, "public-layout")
    r.accepted("public layout, new build folder", script, src, fresh(work / "builds/public"),
               out_folder(work, "public"), deb=good, nested=True,
               bin_dir=make_bin(work, "nested-public", good, nested=src / "tools/fixture_nested.py"))
    psrc, pscript = make_source(work, "private-layout", "private")
    empty = fresh(work / "builds/empty")
    empty.mkdir(parents=True)
    r.accepted(f"private layout, empty build folder, new nested output folder, the caller's {NO_BYTECODE} empty",
               pscript, psrc, empty, fresh(work / "outs/new") / "nested", deb=good, nested=True,
               extra_env={NO_BYTECODE: ""},
               bin_dir=make_bin(work, "nested-private", good, nested=psrc / "tools/fixture_nested.py"))

    # Build and output inside the source tree's build/: only the documented paths change.
    ssrc, sscript = make_source(work, "inside-source")
    sout = out_folder(work, "", at=ssrc / "build/pc/deb-out")
    sbuild = ssrc / "build/pc/ubuntu-release"
    before = snap(ssrc)
    if r.accepted("build and output under the source tree's build/", sscript, ssrc, sbuild, sout, deb=good,
                  nested=True, bin_dir=make_bin(work, "nested-inside", good, nested=ssrc / "tools/fixture_nested.py")):
        after = snap(ssrc)
        written = {"build/pc", "build/pc/deb-out", "build/pc/ubuntu-release"}  # the two folders and their parent's time
        new_out = {f"build/pc/deb-out/{n}" for n in SET}
        other = sorted(k for k in set(before) | set(after)
                       if k not in written and k not in new_out and not k.startswith("build/pc/ubuntu-release/")
                       and before.get(k) != after.get(k))
        check(not other and new_out <= set(after) and not new_out & set(before)
              and all(after[k][:2] == before[k][:2] for k in written & set(before))
              and all(after[name] == before[name] for name in SOURCE_INPUTS) and not pycaches(ssrc),
              "inside the source tree only the build folder and the output's three files are new; source inputs, "
              f"unrelated files and the output's earlier contents keep bytes and times; no __pycache__ "
              f"(other changes: {other or 'none'})")

    # Spaces and shell metacharacters, relative paths from another working folder.
    odd = "odd $(touch PWNED) ; `touch PWNED` & | 'q' \"d\" *"
    osrc, oscript = make_source(work, odd)
    obuild, oout = fresh(work / "builds" / odd), out_folder(work, odd)
    elsewhere = work / "elsewhere"
    elsewhere.mkdir(exist_ok=True)
    rel = lambda p: os.path.relpath(p, elsewhere)
    r.accepted("metacharacter paths given relative to another working folder", oscript, rel(osrc), rel(obuild),
               rel(oout), bin_dir=full, deb=good, cwd=elsewhere, abs_src=osrc, abs_build=obuild, abs_out=oout)
    pwned = [p for p in (ROOT, work, elsewhere, osrc, obuild, oout) if (p / "PWNED").exists()]
    check(not pwned, f"no shell ran the metacharacters (no PWNED file: {pwned or 'none'})")

    # A synthetic package must fail when pointed at this repository's real auditor.
    if (ROOT / "build/pc/sources.cmake").is_file():
        held = sorted(p.name for p in PACKAGING.iterdir())
        r.refused("the real auditor refuses a synthetic package", script, ROOT, fresh(work / "builds/repo-src"),
                  out_folder(work, "repo-src"), "package/payload", ("did not pass payloadcheck",),
                  bin_dir=full, commands=["arch", "configure", "build", "cpack"])
        check(sorted(p.name for p in PACKAGING.iterdir()) == held, "nothing new beside the repository's own helpers")

    # The variables that are NOT on the refused list do not stop a run (the docstring says so).
    r.accepted(f"{', '.join(NOT_REFUSED)} set (not on the refused list)", script, src, fresh(work / "builds/not-refused"),
               out_folder(work, "not-refused"), bin_dir=full, deb=good,
               extra_env={name: "/opt/fixture" for name in NOT_REFUSED})

    # A failure after deb_build.py's real staging: named truthfully, nothing deleted.
    r.late("deb_build.py's exit status changed to 3 after it staged", "late-status", script, src,
           fresh(work / "builds/late-status"), out_folder(work, "late-status"), bin_dir=full, deb=good,
           held=SET, expect=("deb_build.py exit status 3",))
    r.late("the staged .sha256 taken away before the last look", "drop-sha256", script, src,
           fresh(work / "builds/drop-sha256"), out_folder(work, "drop-sha256"), bin_dir=full, deb=good,
           held=[SET[0], SET[2]], expect=("reported success but --out-dir lacks", SET[1]))

    # Every non-empty build folder is refused at inputs: nothing in it changed, nothing run, not even dpkg.
    port = str(src.resolve() / "port")
    compatible = {"CMAKE_HOME_DIRECTORY:INTERNAL": port, "CMAKE_GENERATOR:INTERNAL": "Ninja",
                  "CMAKE_BUILD_TYPE:STRING": "Release", "XG_VERSION:STRING": "US", "XG_EXTRAS:BOOL": "ON",
                  "XG_ENGINE:STRING": "original", "XG_PACKAGE_VERSION:STRING": VERSION}
    contaminants = (("XG_MARCH=native", {"XG_MARCH:STRING": "native"}), ("XG_LTO OFF", {"XG_LTO:BOOL": "OFF"}),
                    ("XG_GLSLANG OFF", {"XG_GLSLANG:BOOL": "OFF"}), ("XG_DAV1D OFF", {"XG_DAV1D:BOOL": "OFF"}),
                    ("XG_PIE ON", {"XG_PIE:BOOL": "ON"}), ("XG_SHARED ON", {"XG_SHARED:BOOL": "ON"}),
                    ("custom CMAKE_C_FLAGS", {"CMAKE_C_FLAGS:STRING": "-O0 -g"}),
                    ("custom CMAKE_CXX_FLAGS", {"CMAKE_CXX_FLAGS:STRING": "-O0 -g"}),
                    ("an otherwise compatible cache", {}))
    for label, extra in contaminants:
        b = fresh(work / "builds" / ("cache-" + slug(label)))
        b.mkdir(parents=True)
        (b / "CMakeCache.txt").write_text("# fixture cache\n" + "".join(f"{k}={v}\n" for k, v in dict(compatible, **extra).items()))
        (b / "CPackConfig.cmake").write_text("# fixture\n" + "".join(f'set({k} "{v}")\n' for k, v in cdb.CPACK.items()))
        r.refused(f"build folder with {label} (the release's keys all match)", script, src, b,
                  out_folder(work, "o"), "inputs", ("is not empty", "nothing is reused, cleaned or deleted"), bin_dir=full)
    for label, files in (("unrelated files", {"notes.txt": "x\n", "sub/data.bin": "y\n"}),
                         ("a hidden file only", {".keep": ""}), ("a hidden folder only", {".cache/x": "z\n"}),
                         ("an empty hidden folder only", {".cache/": None}), ("an empty folder only", {"sub/": None})):
        b = fresh(work / "builds" / slug(label))
        for name, body in files.items():
            if body is None:
                (b / name).mkdir(parents=True)
            else:
                (b / name).parent.mkdir(parents=True, exist_ok=True)
                (b / name).write_text(body)
        r.refused(f"build folder with {label}", script, src, b, out_folder(work, "o"), "inputs", ("is not empty",),
                  bin_dir=full)
    target = fresh(work / "builds/link-target")
    target.mkdir(parents=True)
    full_target = fresh(work / "builds/link-target-full")
    full_target.mkdir(parents=True)
    (full_target / "CMakeCache.txt").write_text("# fixture cache\n")
    for label, to in (("an empty folder", target), ("a folder holding a build", full_target),
                      ("nothing (dangling)", work / "builds/link-nowhere")):
        link = fresh(work / "builds" / ("link-" + slug(label)))
        link.symlink_to(to)
        r.refused(f"build folder that is a symbolic link to {label}", script, src, link, out_folder(work, "o"),
                  "inputs", ("symbolic link",), bin_dir=full, also=(target, full_target))
    b = fresh(work / "builds/holds-link")
    b.mkdir(parents=True)
    (b / "inner").symlink_to(target)
    r.refused("build folder holding only a symbolic link", script, src, b, out_folder(work, "o"), "inputs",
              ("is not empty",), bin_dir=full, also=(target,))
    afile = fresh(work / "builds/a-file")
    afile.write_text("not a folder\n")
    r.refused("build folder that is a file", script, src, afile, out_folder(work, "o"), "inputs", ("not a folder",),
              bin_dir=full)
    r.refused("output folder that is a file", script, src, fresh(work / "builds/n"), afile, "inputs",
              ("not a folder",), bin_dir=full)

    # Unsafe paths.
    nb = lambda: fresh(work / "builds/new")
    for what, build, out, expect in (
            ("build folder inside port/", src / "port/build", out_folder(work, "o"), "not under its build/"),
            ("build folder at the source root", src / "newbuild", out_folder(work, "o"), "not under its build/"),
            ("output folder inside src/", nb(), src / "src/out", "not under its build/"),
            ("output folder is the source tree", nb(), src, "is or holds"),
            ("output folder holds the source tree", nb(), src.parent, "is or holds"),
            ("output folder holds build/pc/sources.cmake", nb(), src / "build/pc", "is or holds"),
            ("output folder inside the script's folder", nb(), src / "packaging/out", "this script's folder"),
            ("output inside the build folder", work / "builds/new", work / "builds/new/out", "one holds the other"),
            ("build folder inside the output", out_folder(work, "o") / "b", work / "outs/o", "one holds the other"),
            ("build and output the same new folder", nb(), work / "builds/new", "the same folder")):
        r.refused(what, script, src, build, out, "inputs", (expect,), bin_dir=full, also=(src,))
    taken = out_folder(work, "taken")
    (taken / SET[1]).write_text("an earlier companion (fixture)\n")
    r.refused("output already holding one of the set", script, src, nb(), taken, "inputs", ("already holds", SET[1]),
              bin_dir=full)

    # Missing inputs, helpers and tags; the tag rule is deb_assets.py's own.
    for name in SOURCE_INPUTS:
        msrc, mscript = make_source(work, "missing-" + slug(name))
        (msrc / name).unlink()
        r.refused(f"source without {name}", mscript, msrc, nb(), out_folder(work, "o"), "inputs", (name,), bin_dir=full)
    r.refused("no source folder", script, work / "sources/none", nb(), out_folder(work, "o"), "inputs",
              ("no such folder",), bin_dir=full)
    for helper in ("deb_build.py", "deb_assets.py"):
        hsrc, hscript = make_source(work, "no-" + helper)
        (hscript.parent / helper).unlink()
        r.refused(f"{helper} not beside it", hscript, hsrc, nb(), out_folder(work, "o"), "inputs",
                  (f"{helper} is not beside",), bin_dir=full)
    for tag in (VERSION, TAG[:-2], TAG.replace(".11.", ".13."), TAG + "1" if TAG.endswith("0") else TAG, TAG + "-rc1",
                " " + TAG, TAG + "\n", TAG.upper(), TAG + ";touch PWNED", ""):
        r.refused(f"tag {tag!r}", script, src, nb(), out_folder(work, "o"), "inputs", ("vYYYY.MM.N",), tag=tag,
                  bin_dir=full)
    rsrc, rscript = make_source(work, "other-tag-rule")
    assets = rscript.parent / "deb_assets.py"
    rule = '    if not TAG.fullmatch(tag):\n'
    text = assets.read_text()
    check(text.count(rule) == 1, "fixture deb_assets.py with another tag rule (a labelled test change of a copy)")
    assets.write_text(text.replace(rule, '    if True:  # FIXTURE: every tag refused\n'))
    r.refused("a good tag, refused by a copy of deb_assets.py with another rule (the rule is its own)", rscript, rsrc,
              nb(), out_folder(work, "o"), "inputs", ("vYYYY.MM.N",), bin_dir=full)

    # Environment: OS, version, and each refused variable, before any command.
    for label, text in (("Debian 14", 'ID=debian\nVERSION_ID="14"\n'), ("Debian forky (no VERSION_ID)", "ID=debian\n"),
                        ("Ubuntu 24.04", 'ID=ubuntu\nVERSION_ID="24.04"\n'), ("Ubuntu 26.10", 'ID=ubuntu\nVERSION_ID="26.10"\n'),
                        ("an Ubuntu derivative", 'ID=linuxmint\nID_LIKE="ubuntu debian"\nVERSION_ID="26.04"\n'),
                        ("Ubuntu without VERSION_ID", "ID=ubuntu\n"), ("Arch", "ID=arch\n"), ("no os-release", None)):
        r.refused(f"OS {label}", script, src, nb(), out_folder(work, "o"), "environment", (), os_text=text, bin_dir=full)
    for var in REFUSED:
        for value in ("/opt/fixture", ""):
            r.refused(f"{var}={value!r} in the environment", script, src, nb(), out_folder(work, "o"), "environment",
                      (f" {var} (unset them)",), extra_env={var: value}, bin_dir=full)
    r.refused("several refused variables at once", script, src, nb(), out_folder(work, "o"), "environment",
              ("CC, CFLAGS, CMAKE_PREFIX_PATH (unset them)",),
              extra_env={"CMAKE_PREFIX_PATH": "/opt", "CFLAGS": "-O0", "CC": "clang"}, bin_dir=full)

    # Tools and architecture.
    for tool in ("cmake", "ninja", "cpack", "dpkg", "dpkg-deb", "dpkg-shlibdeps"):
        r.refused(f"no {tool} on PATH", script, src, nb(), out_folder(work, "o"), "tools", (tool,),
                  bin_dir=make_bin(work, "no-" + tool, good, without=(tool,)))
    for arch in ("arm64", "i386", ""):
        r.refused(f"dpkg architecture {arch!r}", script, src, nb(), out_folder(work, "o"), "architecture",
                  (f"{arch!r}, not 'amd64'",), bin_dir=make_bin(work, "arch-" + (arch or "empty"), good, arch=arch))
    r.refused("dpkg --print-architecture fails", script, src, nb(), out_folder(work, "o"), "architecture",
              ("exit status 2",), bin_dir=make_bin(work, "arch-fail", good, arch=None))

    # Configure and deb_build.py's own failures: its step named, output unchanged, nothing staged.
    r.refused("configure fails", script, src, nb(), out_folder(work, "o"), "configure",
              ("cmake exit status 1", "fixture: configure failed"),
              bin_dir=make_bin(work, "configure-fail", good, configure="fail"), commands=["arch", "configure"])
    weak = cdb.fixture_deb(work, "depends-vulkan-only", audited_files, depends="libvulkan1")
    noexe = cdb.fixture_deb(work, "no-executable", cdb.good_files(usr__bin__xenogears=None))
    wrong = cdb.fixture_deb(work, "wrong-payload", cdb.good_files(usr__bin__xenogears=(FIXTURE_ELF + "added", 0o755)))
    for what, bin_dir, step, expect, cmds in (
            ("the build fails", make_bin(work, "build-fail", good, build="fail"), "package/build",
             ("fixture: the build failed",), ["arch", "configure", "build"]),
            ("cpack fails", make_bin(work, "cpack-fail", good, cpack_mode="fail"), "package/cpack",
             ("fixture: cpack failed",), ["arch", "configure", "build", "cpack"]),
            ("configure gives an arm64 CPack", make_bin(work, "cpack-arm64", good,
             cpack={"CPACK_DEBIAN_PACKAGE_ARCHITECTURE": "arm64"}), "package/inputs", ("arm64",), ["arch", "configure"]),
            ("a package without the executable", make_bin(work, "no-exe", noexe), "package/package",
             ("missing usr/bin/xenogears",), ["arch", "configure", "build", "cpack"]),
            ("auditor rejects the packaged bytes", make_bin(work, "wrong-payload", wrong), "package/payload",
             ("did not pass payloadcheck", "exit 13"), ["arch", "configure", "build", "cpack"]),
            ("strict Depends: libvulkan1 alone (as this host's real package)", make_bin(work, "weak", weak),
             "package/companions", ("no mandatory libsdl3-0", "no mandatory libc6"),
             ["arch", "configure", "build", "cpack"])):
        r.refused(what, script, src, nb(), out_folder(work, "o"), step, expect, bin_dir=bin_dir, commands=cmds)

    left = {p.name: pycaches(p) for p in sorted((work / "sources").iterdir()) if pycaches(p)}
    check(not left, f"no fixture source tree holds a __pycache__ after all runs ({left or 'none'})")


def cmake_code(text: str) -> str:
    """CMake text without its comments (a # outside double quotes ends the line)."""
    lines = []
    for line in text.splitlines():
        quoted = False
        for i, ch in enumerate(line):
            if ch == '"' and (i == 0 or line[i - 1] != "\\"):
                quoted = not quoted
            elif ch == "#" and not quoted:
                line = line[:i]
                break
        lines.append(line.rstrip())
    return "\n".join(lines) + "\n"


def baseline_problems(args: list[str]) -> dict[str, str]:
    """Faults of a logged configure (cmake's arguments) against baseline amd64, by kind (empty: none)."""
    if "-S" not in args:
        return {"configure": f"no -S in the configure arguments ({args})"}
    found: dict[str, str] = {}
    options = [a for i, a in enumerate(args) if not (i and args[i - 1] in ("-S", "-B"))]
    bad = [a for a in options if CPU_OPTION.search(a) or re.match(r"-DXG_MARCH(:\w+)?=.", a)
           or re.match(r"-D\w*(FLAGS|TOOLCHAIN_FILE|COMPILER|COMPILER_TARGET|SYSTEM_PROCESSOR)(:\w+)?=", a)]
    if bad:
        found["configure"] = f"the configure passes a CPU, flag, compiler or non-empty XG_MARCH option: {bad}"
    port = Path(args[args.index("-S") + 1])
    files = [port / "CMakeLists.txt"] + sorted(p for p in port.rglob("*.cmake") if p.name not in NOT_ON_DEB_PATH)
    defaults, stray = [], []
    for path in files:
        code = cmake_code(path.read_text())
        defaults += MARCH_DEFAULT.findall(code)
        if path == port / "CMakeLists.txt":
            code = MARCH_GUARD.sub("", code, count=1)
        stray += [f"{path.relative_to(port)}: {line.strip()}" for line in code.splitlines()
                  if not MARCH_DEFAULT.search(line) and (CPU_OPTION.search(line) or "XG_MARCH" in line)]
    if defaults != [""]:
        found["default"] = f"XG_MARCH's cache default is not one empty value ({defaults})"
    if stray:
        found["cmake"] = f"a CPU option or an XG_MARCH use outside the guarded -march=${{XG_MARCH}}: {stray}"
    return found


def check_baseline(work: Path) -> list[str] | None:
    """Baseline amd64 on the .deb path: the real CMake files and deb_ubuntu.py's own configure argv (FIXTURES).
    Returns the pristine run's configure arguments (None if it was not accepted)."""
    r = Runner(work)
    pristine = None
    good = cdb.fixture_deb(work, "baseline", cdb.good_files(usr__bin__xenogears=(FIXTURE_ELF, 0o755)))
    full = make_bin(work, "baseline", good)
    cmake_files = [ROOT / "port/CMakeLists.txt", *sorted((ROOT / "port").rglob("*.cmake"))]

    def source(label: str, edits: tuple = ()) -> tuple[Path, Path]:
        src, script = make_source(work, "baseline-" + slug(label))
        for path in cmake_files:
            (src / path.relative_to(ROOT)).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, src / path.relative_to(ROOT))
        for target, old, new in edits:
            path = script if target == "deb_ubuntu.py" else src / target
            text = path.read_text()
            check(old is None or text.count(old) == 1, f"FIXTURE baseline mutation {label}: a labelled test change "
                                                       f"of a copy of {target}")
            path.write_text(text + new if old is None else text.replace(old, new))
        return src, script

    src, script = source("pristine")
    if r.accepted("baseline: this repository's CMake files, the unchanged deb_ubuntu.py", script, src,
                  fresh(work / "builds/baseline-pristine"), out_folder(work, "baseline-pristine"), bin_dir=full, deb=good):
        args = pristine = logged(full)[1][1]
        found = baseline_problems(args)
        check(not found and Path(args[args.index("-S") + 1]) == src.resolve() / "port",
              f"baseline amd64: deb_ubuntu.py configures the copied real port/ with no XG_MARCH but empty, no flag, "
              f"compiler or CPU option, and its CMake keeps XG_MARCH empty and every -march guarded "
              f"({found or 'clean'}; not the Ubuntu compiler's default)")

    cmake, setter, configure = "port/CMakeLists.txt", 'set(XG_MARCH "" CACHE STRING', '"-DXG_EXTRAS=ON",'
    ui = "target_compile_options(ui PRIVATE -march=${XG_MARCH})"
    for label, edits, kind in (
            ("XG_MARCH default native", ((cmake, setter, setter.replace('""', '"native"')),), "default"),
            ("XG_MARCH default x86-64-v3", ((cmake, setter, setter.replace('""', '"x86-64-v3"')),), "default"),
            ("XG_MARCH set again after its cache entry", ((cmake, "if(XG_MARCH)\n",
                                                           "set(XG_MARCH x86-64-v3)\nif(XG_MARCH)\n"),), "cmake"),
            ("an unguarded -march for the menu", ((cmake, ui, ui.replace("${XG_MARCH}", "x86-64-v3")),), "cmake"),
            ("-mtune for every target", ((cmake, setter, "add_compile_options(-mtune=znver4)\n" + setter),), "cmake"),
            ("-mavx2 in deb.cmake", (("port/cmake/deb.cmake", None, "target_compile_options(xenogears PRIVATE -mavx2)\n"),),
             "cmake"),
            ("configure with -DXG_MARCH=native", (("deb_ubuntu.py", configure, configure + ' "-DXG_MARCH=native",'),),
             "configure"),
            ("configure with -DXG_MARCH=x86-64-v3", (("deb_ubuntu.py", configure, configure + ' "-DXG_MARCH=x86-64-v3",'),),
             "configure"),
            ("configure with -DCMAKE_C_FLAGS=-mtune=skylake",
             (("deb_ubuntu.py", configure, configure + ' "-DCMAKE_C_FLAGS=-mtune=skylake",'),), "configure"),
            ("configure with an explicit empty -DXG_MARCH=", (("deb_ubuntu.py", configure, configure + ' "-DXG_MARCH=",'),),
             None)):
        src, script = source(label, edits)
        r.invoke(script, src, fresh(work / "builds" / ("baseline-" + slug(label))),
                 out_folder(work, "baseline-" + slug(label)), TAG, full, GOOD_OS, work, None, label)
        args = next((e[1] for e in logged(full) if e[0] == "cmake" and "-S" in e[1]), [])
        found = baseline_problems(args)
        check(set(found) == ({kind} if kind else set()),
              f"FIXTURE baseline mutation {label}: {'caught as ' + kind if kind else 'not flagged'} "
              f"({'; '.join(found.values())[:300] or 'clean'})")
    return pristine


def c_standard_problems(port: Path) -> dict[str, str]:
    """Faults of a port/ tree's CMake files against the required GNU C17 pin, by kind (empty: none)."""
    found: dict[str, str] = {}
    main = cmake_code((port / "CMakeLists.txt").read_text())
    lines = [line.strip() for line in main.splitlines()]
    pins = [line for line in lines if C_PIN_LINE.match(line)]
    if sorted(pins) != sorted(C_PIN):
        found["pin"] = f"CMakeLists.txt does not set {' '.join(C_PIN)} once each ({pins})"
    first = FIRST_TARGET.search(main)
    late = [p for p in C_PIN if p in lines and (first is None or main.index(p) > first.start())]
    if late:
        found["order"] = f"set after the first target or include ({first[0].strip() if first else 'none'}): {late}"
    stray = []
    for path in [port / "CMakeLists.txt"] + sorted(p for p in port.rglob("*.cmake") if p.name not in NOT_ON_DEB_PATH):
        for line in cmake_code(path.read_text()).splitlines():
            line = line.strip()
            if C_SETTING.search(line) and not line.startswith(GAME_OPTIONS) and not C_MINIMUM.match(line) \
                    and not (path == port / "CMakeLists.txt" and C_PIN_LINE.match(line)):
                stray.append(f"{path.relative_to(port)}: {line}")
    if stray:
        found["override"] = f"another C standard or -std= setting: {stray}"
    return found


def dialect_problems(build: Path) -> tuple[dict[str, str], str]:
    """Faults of a real configure's C compile commands against GNU C17 (game C: then gnu89), and a summary."""
    path = build / "compile_commands.json"
    if not path.is_file():
        return {"dialect": f"no {path.name}"}, "none"
    pairs = [(e["file"], [t for t in (e.get("command") or " ".join(e.get("arguments", []))).split()
                          if t.startswith("-std=")]) for e in json.loads(path.read_text()) if e["file"].endswith(".c")]
    port_only = sum(stds == [C_DIALECT] for _, stds in pairs)
    game = sum(stds == [C_DIALECT, GAME_DIALECT] for _, stds in pairs)
    bad = sorted({f"{Path(f).name}: {' '.join(s) or 'no -std'}" for f, s in pairs
                  if s not in ([C_DIALECT], [C_DIALECT, GAME_DIALECT])})
    hw = [s for f, s in pairs if f.endswith("port/src/system/hw.c")]
    found = {}
    if bad or not port_only or not game or not hw or any(s != [C_DIALECT] for s in hw):
        found["dialect"] = (f"{len(bad)} C compile command(s) not {C_DIALECT} (then {GAME_DIALECT} for game C): "
                            f"{bad[:4]}; hw.c {hw}; {port_only} port, {game} game")
    return found, f"{len(pairs)} C compile commands: {port_only} {C_DIALECT}, {game} {C_DIALECT} then {GAME_DIALECT}"


def check_c_standard(work: Path, deb_args: list[str] | None) -> None:
    """The port's C dialect: the pin in the real CMake files, and a real configure's compile commands (REAL)."""
    cmake_files = [ROOT / "port/CMakeLists.txt", *sorted((ROOT / "port").rglob("*.cmake"))]
    base = work / "cstd"

    def copied(label: str, edits: tuple) -> Path:
        """Labelled copies of the real CMake files (static check)."""
        root = fresh(base / "static" / slug(label))
        for path in cmake_files:
            (root / path.relative_to(ROOT)).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, root / path.relative_to(ROOT))
        for target, old, new in edits:
            text = (root / target).read_text()
            check(old is None or text.count(old) == 1, f"FIXTURE C standard mutation {label}: a labelled test change "
                                                       f"of a copy of {target}")
            (root / target).write_text(text + new if old is None else text.replace(old, new))
        return root / "port"

    def shadow(label: str, edits: tuple) -> Path:
        """A tree for a real configure: this repository by symbolic links, port/CMakeLists.txt a (changed) copy."""
        root = fresh(base / "real" / slug(label) / "root")
        (root / "port").mkdir(parents=True)
        (root / "build/pc").mkdir(parents=True)
        for path in ROOT.iterdir():
            if path.name not in ("port", "build"):
                (root / path.name).symlink_to(path)
        for path in (ROOT / "port").iterdir():
            if path.name != "CMakeLists.txt":
                (root / "port" / path.name).symlink_to(path)
        (root / "build/pc/sources.cmake").symlink_to(ROOT / "build/pc/sources.cmake")
        text = (ROOT / "port/CMakeLists.txt").read_text()
        for _, old, new in edits:
            text = text + new if old is None else text.replace(old, new)
        (root / "port/CMakeLists.txt").write_text(text)
        return root / "port"

    def configure(label: str, port: Path) -> dict[str, str] | None:
        """Configure port/ for real with deb_ubuntu.py's own options (no -S/-B), compile commands exported."""
        build = fresh(base / "real" / slug(label) / "build")
        env = {k: v for k, v in os.environ.items() if k not in REFUSED}
        result = run(["cmake", "-S", port, "-B", build, *options, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], env=env)
        if not check(result.returncode == 0, f"REAL C standard: {label}: cmake configures "
                                             f"({(result.stderr.strip().splitlines() or [''])[-1][:200]!r})"):
            return None
        found, summary = dialect_problems(build)
        print(f"      {label}: {summary}")
        return found

    pristine = c_standard_problems(ROOT / "port")
    check(not pristine, f"C standard: port/CMakeLists.txt requires GNU C17 before its first target, and no port CMake "
                        f"file sets another ({pristine or 'clean'})")
    real = bool(deb_args) and (ROOT / "build/pc/sources.cmake").is_file() and shutil.which("cmake") is not None
    options = []
    check(bool(deb_args), "C standard: deb_ubuntu.py's configure options from the baseline run")
    if deb_args and not real:
        print("NOTE  no build/pc/sources.cmake or cmake here: the C standard is checked in the CMake files only")
    elif real:
        options = [a for i, a in enumerate(deb_args) if a not in ("-S", "-B") and not (i and deb_args[i - 1] in ("-S", "-B"))]
        found = configure("this repository's port/", ROOT / "port")
        check(found == {}, f"REAL C standard: configuring this repository's port/ with {options}, every C compile "
                           f"command is {C_DIALECT}, the game C's then {GAME_DIALECT} ({found or 'clean'}; this host's "
                           f"compiler, not Ubuntu's)")

    cmake = "port/CMakeLists.txt"
    block = "".join(p + "\n" for p in C_PIN)
    standard, required, extensions = C_PIN
    for label, edits, kind, live in (
            ("C standard pin missing", ((cmake, standard + "\n", ""),), "pin", True),
            ("C11 pinned", ((cmake, standard, standard.replace("17", "11")),), "pin", True),
            ("C23 pinned", ((cmake, standard, standard.replace("17", "23")),), "pin", False),
            ("C17 not required", ((cmake, required, required.replace(" ON)", " OFF)")),), "pin", False),
            ("GNU extensions off", ((cmake, extensions, extensions.replace(" ON)", " OFF)")),), "pin", True),
            ("C standard set again at the end", ((cmake, None, "set(CMAKE_C_STANDARD 23)\n"),), "pin", False),
            ("pin after the first targets", ((cmake, block, ""), (cmake, "add_library(platform OBJECT",
                                                                  block + "add_library(platform OBJECT")), "order", True),
            ("platform's own C_STANDARD 23", ((cmake, None, "set_target_properties(platform PROPERTIES C_STANDARD 23)\n"),),
             "override", True),
            ("platform's minimum raised to c_std_23", ((cmake, "target_compile_features(platform PRIVATE c_std_11)",
                                                        "target_compile_features(platform PRIVATE c_std_23)"),),
             "override", True),
            ("-std=c2x in gpu.cmake", (("port/src/gpu/gpu.cmake", None, "add_compile_options(-std=c2x)\n"),),
             "override", False)):
        found = c_standard_problems(copied(label, edits))
        check(set(found) == {kind}, f"FIXTURE C standard mutation {label}: caught as {kind} "
                                    f"({'; '.join(found.values())[:300] or 'clean'})")
        if live and options:
            found = configure(label, shadow(label, edits))
            check(found is not None and set(found) == {"dialect"},
                  f"REAL C standard mutation {label}: a real configure's compile commands show it "
                  f"({'; '.join((found or {}).values())[:300] or 'clean'})")


def report() -> int:
    if cdb.failures:
        print(f"check_deb_ubuntu: {len(cdb.failures)} check(s) failed")
        return 1
    print("check_deb_ubuntu: all checks pass (the real CLI's refusal of this host, FIXTURE runs and this host's "
          "real CMake configure of the C dialect only: no Ubuntu build, Depends, installation or start checked)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    args = ap.parse_args()
    work = ROOT / f"build/pc/deb-ubuntu-{args.ver}"
    work.mkdir(parents=True, exist_ok=True)
    for tool in ("dpkg-deb", "dpkg"):
        if not check(shutil.which(tool) is not None, f"{tool} on PATH (fixtures are made and read with it)"):
            return report()
    check_public(work)
    check_bytecode_control(work)
    check_real_host(work)
    check_fixtures(work)
    check_c_standard(work, check_baseline(work))
    return report()


if __name__ == "__main__":
    sys.exit(main())
