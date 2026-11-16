#!/usr/bin/env python3
"""Configure and package the release .deb inside an Ubuntu 26.04 amd64 builder.

    python packaging/deb_ubuntu.py --source <tree> --build-dir <new folder> \\
        --tag v<YYYY>.<MM>.<N> --out-dir <dir>

Run it inside a builder the caller has already provisioned: an Ubuntu 26.04
amd64 system (a container, for instance) with cmake, ninja, cpack, dpkg,
dpkg-deb, dpkg-shlibdeps, a C and C++ compiler and the port's build
dependencies from Ubuntu's own packages. This script installs, downloads,
starts and uploads nothing, manages no container and asks for no privilege.

--source is the tree holding port/ (this repository, or the private tree it
is published from). It must already hold build/pc/sources.cmake, the module
source lists port/CMakeLists.txt includes; this script never generates it
(tools/pc_sources.py does, from a disc split).

The steps, each named in the failure message when it fails:

  inputs        the tag, by packaging/deb_assets.py's rule (vYYYY.MM.N);
                deb_build.py and deb_assets.py beside this file; the source
                tree's port/CMakeLists.txt, port/cmake/deb.cmake,
                tools/pc_link.py and build/pc/sources.cmake; the paths:
                --build-dir must be a new or an empty folder (not even hidden
                files; never a symbolic link): an existing build is never
                reused, cleaned or deleted, so no earlier option or flag can
                reach the package. --out-dir may already exist (its contents
                are kept) but must not hold the package's three names. Neither
                folder may be, hold or sit inside the other, hold the source
                tree, hold or sit inside this script's folder, or hold one of
                the inputs above; inside the source tree both must sit under
                its build/ folder.
  environment   /etc/os-release (or /usr/lib/os-release) says ID ubuntu,
                VERSION_ID 26.04, and none of exactly these variables is set
                in the environment: CC, CXX, CFLAGS, CXXFLAGS, CPPFLAGS,
                LDFLAGS, DESTDIR, CMAKE_GENERATOR, CMAKE_GENERATOR_PLATFORM,
                CMAKE_GENERATOR_TOOLSET, CMAKE_TOOLCHAIN_FILE,
                CMAKE_BUILD_TYPE, CMAKE_CONFIGURATION_TYPES,
                CMAKE_PREFIX_PATH, CMAKE_C_COMPILER_LAUNCHER,
                CMAKE_CXX_COMPILER_LAUNCHER, CMAKE_C_LINKER_LAUNCHER,
                CMAKE_CXX_LINKER_LAUNCHER. That list is all that is refused:
                it is not every variable CMake, pkg-config or a compiler
                reads. SDL3_DIR, SDL3_ROOT, CMAKE_LIBRARY_PATH and
                PKG_CONFIG_PATH, for instance, are not looked at; keeping the
                builder's environment clean of them is the caller's part.
  tools         cmake, ninja, cpack, dpkg, dpkg-deb, dpkg-shlibdeps on PATH
  architecture  dpkg --print-architecture says amd64 (the first command run)
  configure     cmake -S <source>/port -B <build> -G Ninja
                -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON
                -DXG_PACKAGE_VERSION=<the tag without its v>: deb_build.py's
                documented configuration, every other option at its default
  package       python -I -B packaging/deb_build.py --build-dir <build> --tag
                <tag> --out-dir <out>, unchanged and without
                --shlibdeps-private-dir: the build, cpack, the package's files,
                the strict Depends check (--require-deps), the .sha256 and SBOM
                and the staging are all its own; its failing step is named
                (package/<its step>). When it reports success this script
                looks once more for the three files in --out-dir.

What is written, and where. This script itself creates and deletes nothing.
Its commands write two places: the build folder (cmake and the build) and
--out-dir (deb_build.py's staging of xenogears-pc_<version>_amd64.deb, its
.sha256 and its .cdx.json). Both may sit under the source tree's build/
folder, so a run can write inside the source tree there. What the build's
own commands write is theirs; the one thing this script enforces about it is
Python bytecode: it caches none itself, starts deb_build.py with -B, and gives
the configure and package commands a copy of its environment with
PYTHONDONTWRITEBYTECODE=1, which their children inherit (the build runs
Python tools from the source tree's tools/ folder). A Python started with -E
or -I, or by a command that drops its environment, does not read that
variable.

When the inputs, environment, tools or architecture step fails, nothing has
been created or changed. When configure fails, the build folder holds what
cmake left and --out-dir is untouched. When package fails, the build folder
holds what the build left, and --out-dir is as deb_build.py left it: it
stages only after its own checks pass and takes back a staging that fails, so
normally --out-dir is as it was; but a failure after its staging (its exit
status, or this script's last look) leaves staged files there. So the failure
message says what --out-dir holds of the three names at that moment, and
nothing is deleted: remove them, and choose a new build folder, before
another run.

Exit status: 0 only when every step passed and the three files are in
--out-dir; 1 when a step failed (the step named); 2 for a command line the
option parser refuses (a missing or unknown option), before any step.

Limits: this builder check selects where the release is built; it is not an
ABI proof. Depends is what this builder's dpkg-shlibdeps gave, checked by
deb_build.py's strict rule only; that the package installs and starts on a
clean Ubuntu 26.04 or Debian is not checked here. Standard library only.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD_HELPER = HERE / "deb_build.py"
ASSETS = HERE / "deb_assets.py"
OS_RELEASE = (Path("/etc/os-release"), Path("/usr/lib/os-release"))
WANT_OS = {"ID": "ubuntu", "VERSION_ID": "26.04"}
WANT_ARCH = "amd64"
TOOLS = ("cmake", "ninja", "cpack", "dpkg", "dpkg-deb", "dpkg-shlibdeps")
SOURCE_INPUTS = ("port/CMakeLists.txt", "port/cmake/deb.cmake", "tools/pc_link.py", "build/pc/sources.cmake")
REFUSED_VARIABLES = ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "DESTDIR",
                     "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET",
                     "CMAKE_TOOLCHAIN_FILE", "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES",
                     "CMAKE_PREFIX_PATH", "CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER",
                     "CMAKE_C_LINKER_LAUNCHER", "CMAKE_CXX_LINKER_LAUNCHER")
NO_BYTECODE = "PYTHONDONTWRITEBYTECODE"


class StepError(Exception):
    """A failed step; `watch` is (--out-dir, the three names) once deb_build.py has been started."""

    def __init__(self, step: str, message: str, watch: tuple[Path, list[str]] | None = None):
        super().__init__(message)
        self.step = step
        self.watch = watch


def load_assets():
    """packaging/deb_assets.py, for its tag rule and package names (by path: python -I adds no folder)."""
    for helper in (BUILD_HELPER, ASSETS):
        if not helper.is_file():
            raise StepError("inputs", f"{helper.name} is not beside {Path(__file__).name}")
    sys.dont_write_bytecode = True  # no __pycache__ beside the helpers (often inside the source tree)
    spec = importlib.util.spec_from_file_location("deb_assets", ASSETS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def within(path: Path, folder: Path) -> bool:
    """path is folder or inside it (both resolved)."""
    return path == folder or folder in path.parents


def check_paths(source: Path, build_arg: Path, out_arg: Path, names: list[str]) -> tuple[Path, Path]:
    if not source.is_dir():
        raise StepError("inputs", f"--source {source}: no such folder")
    missing = [name for name in SOURCE_INPUTS if not (source / name).is_file()]
    if missing:
        raise StepError("inputs", f"--source {source} lacks {', '.join(missing)}"
                        + (" (made by tools/pc_sources.py; this script never generates it)"
                           if "build/pc/sources.cmake" in missing else ""))
    if build_arg.is_symlink():
        raise StepError("inputs", f"--build-dir {build_arg} is a symbolic link")
    build, out = build_arg.resolve(), out_arg.resolve()
    if build.exists():
        if not build.is_dir():
            raise StepError("inputs", f"--build-dir {build} is not a folder")
        if any(build.iterdir()):
            raise StepError("inputs", f"--build-dir {build} is not empty: only a new or empty folder is "
                                      "configured (nothing is reused, cleaned or deleted; choose another)")
    if out.exists() and not out.is_dir():
        raise StepError("inputs", f"--out-dir {out} is not a folder")
    protected = [source / name for name in SOURCE_INPUTS] + [source, HERE]
    for label, folder in (("--build-dir", build), ("--out-dir", out)):
        if held := [str(p) for p in protected if within(p, folder)]:
            raise StepError("inputs", f"{label} {folder} is or holds {held[0]}")
        if within(folder, HERE):
            raise StepError("inputs", f"{label} {folder} is inside this script's folder {HERE}")
        if within(folder, source) and not within(folder, source / "build"):
            raise StepError("inputs", f"{label} {folder} is inside the source tree but not under its build/ folder")
    if within(build, out) or within(out, build):
        raise StepError("inputs", f"--build-dir {build} and --out-dir {out} are the same folder or one holds the other")
    taken = [n for n in names if (out / n).exists() or (out / n).is_symlink()]
    if taken:
        raise StepError("inputs", f"--out-dir {out} already holds {', '.join(taken)} (remove it or choose another)")
    return build, out


def check_os() -> None:
    release = next((path for path in OS_RELEASE if path.is_file()), None)
    if release is None:
        raise StepError("environment", f"no os-release file ({', '.join(str(p) for p in OS_RELEASE)})")
    fields = {}
    for line in release.read_text(errors="replace").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip().strip("\"'")
    wrong = [f"{key} is {fields.get(key)!r}, not {want!r}" for key, want in WANT_OS.items() if fields.get(key) != want]
    if wrong:
        raise StepError("environment", f"this is not an Ubuntu 26.04 builder ({release}): " + "; ".join(wrong))
    if set_vars := [name for name in REFUSED_VARIABLES if name in os.environ]:
        raise StepError("environment", f"set in the environment, which would change the release's configuration: "
                                       f"{', '.join(set_vars)} (unset them)")


def run(step: str, argv: list, env: dict[str, str]) -> subprocess.CompletedProcess:
    print("$", " ".join(str(a) for a in argv), flush=True)
    try:
        return subprocess.run([str(a) for a in argv], env=env, capture_output=True, text=True)
    except OSError as error:
        raise StepError(step, f"{Path(str(argv[0])).name} could not run ({error})") from error


def failed(step: str, name: str, result: subprocess.CompletedProcess, watch=None) -> StepError:
    tail = (result.stdout[-3000:] + result.stderr[-3000:]).strip()
    return StepError(step, f"{name} exit status {result.returncode}" + (f":\n{tail}" if tail else ""), watch)


def package(source: Path, build_arg: Path, tag: str, out_arg: Path) -> list[Path]:
    assets = load_assets()
    try:
        version = assets.version_from_tag(tag)
    except assets.AssetError as error:
        raise StepError("inputs", str(error)) from error
    deb = f"{assets.PACKAGE}_{version}_{assets.ARCH}.deb"
    names = [deb, f"{deb}.sha256", f"{deb}.cdx.json"]
    build, out = check_paths(source.resolve(), build_arg, out_arg, names)
    check_os()
    tools = {tool: shutil.which(tool) for tool in TOOLS}
    if missing := [tool for tool, path in tools.items() if path is None]:
        raise StepError("tools", f"not on PATH: {', '.join(missing)}")
    env = dict(os.environ)
    env[NO_BYTECODE] = "1"  # inherited by cmake, ninja, cpack and the Python tools they start

    result = run("architecture", [tools["dpkg"], "--print-architecture"], env)
    if result.returncode != 0:
        raise failed("architecture", "dpkg --print-architecture", result)
    if result.stdout.strip() != WANT_ARCH:
        raise StepError("architecture", f"dpkg architecture is {result.stdout.strip()!r}, not {WANT_ARCH!r}")

    result = run("configure", [tools["cmake"], "-S", source.resolve() / "port", "-B", build, "-G", "Ninja",
                               "-DCMAKE_BUILD_TYPE=Release", "-DXG_VERSION=US", "-DXG_EXTRAS=ON",
                               f"-DXG_PACKAGE_VERSION={version}"], env)
    if result.returncode != 0:
        raise failed("configure", "cmake", result)

    watch = (out, names)  # from here deb_build.py may have staged: a failure says what --out-dir holds
    try:
        result = run("package", [sys.executable, "-I", "-B", BUILD_HELPER, "--build-dir", build, "--tag", tag,
                                 "--out-dir", out], env)
    except StepError as error:
        error.watch = watch
        raise
    print(result.stdout, end="", flush=True)
    if result.returncode != 0:
        marker = "deb_build: FAILED at step "
        line = next((l for l in result.stderr.splitlines() if l.startswith(marker)), None)
        if line is None:
            raise failed("package", BUILD_HELPER.name, result, watch)
        inner, _, message = line[len(marker):].partition(": ")
        raise StepError(f"package/{inner}", f"{BUILD_HELPER.name}: {message}\n{result.stderr.strip()[-3000:]}", watch)
    staged = [out / name for name in names]
    if absent := [p.name for p in staged if not p.is_file() or p.is_symlink()]:
        raise StepError("package", f"{BUILD_HELPER.name} reported success but --out-dir lacks {', '.join(absent)} "
                                   "(as regular files)", watch)
    return staged


def staging_note(watch: tuple[Path, list[str]] | None) -> str:
    """What a failed run left in --out-dir, looked up, never assumed."""
    if watch is None:
        return "nothing staged (deb_build.py was not started)"
    out, names = watch
    try:
        present = [n for n in names if (out / n).exists() or (out / n).is_symlink()]
    except OSError as error:
        return f"--out-dir {out} could not be read ({error}): it may hold staged files"
    if not present:
        return "nothing staged (--out-dir holds none of the package's three names)"
    absent = [n for n in names if n not in present]
    return (f"--out-dir {out} HOLDS {', '.join(present)} (absent when this run began; left in place, nothing is "
            "deleted)" + (f"; it lacks {', '.join(absent)}" if absent else "") + "; remove them before another run")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], allow_abbrev=False)
    ap.add_argument("--source", type=Path, required=True, help="the tree holding port/ and build/pc/sources.cmake")
    ap.add_argument("--build-dir", type=Path, required=True, help="a new or empty folder for the Release build")
    ap.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    ap.add_argument("--out-dir", type=Path, required=True, help="where the .deb and its companions go")
    args = ap.parse_args()
    try:
        staged = package(args.source, args.build_dir, args.tag, args.out_dir)
    except StepError as error:
        print(f"deb_ubuntu: FAILED at step {error.step}: {error}", file=sys.stderr)
        print(f"deb_ubuntu: {staging_note(error.watch)}; this is not a release package set", file=sys.stderr)
        return 1
    for path in staged:
        print(path)
    print("deb_ubuntu: staged the package and its two companions (built in this Ubuntu 26.04 amd64 builder; "
          "nothing installed or started)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
