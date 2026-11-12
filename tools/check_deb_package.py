#!/usr/bin/env python3
"""Check the PC port's Debian package (port/cmake/deb.cmake) without game data.

    python tools/check_deb_package.py --ver us                  # configure, build, cpack, inspect
    python tools/check_deb_package.py --ver us --settings-only  # the CPack settings only (no build)
    python tools/check_deb_package.py --ver us --package-version 2026.11.1 --require-deps

1. Settings: a Release configure (the shipped options, docs/PORT_SETUP.md) in
   build/pc/deb-<ver>; CPackConfig.cmake must name the DEB generator, the
   package, its version and file name, amd64 (on x86_64), the /usr prefix,
   shlibdeps and libvulkan1 (SDL3 opens the Vulkan loader at run time). A
   version that is not a Debian version (a tag's v kept) must be refused.
2. Install plan: cmake --install into a staging folder holds exactly the
   executable and the licences (so no game data).
3. Package: cpack -G DEB, then dpkg-deb -c and -I on the .deb itself (without
   dpkg-deb, the staging folder and the settings stand in, and it says so).
4. Depends: proven only where dpkg knows which package holds each library the
   executable needs (a Debian or Ubuntu host); every one must be in Depends.
   Elsewhere it reports that Depends is unproven: --require-deps makes that a
   failure (for the release builder).

Nothing is installed or launched. Exit status 1 on any failure.
"""
from __future__ import annotations

import argparse
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = "xenogears-pc"
DOC = "usr/share/doc/xenogears-pc"
LICENCES = {"COPYING", "LICENSE-Jukebox-font.txt", "LICENSE-imgui.txt"}
OPTIONAL_LICENCES = {"LICENSE-glslang.txt": "glslang", "LICENSE-dav1d.txt": "dav1d"}

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def run(argv: list[str], cwd: Path = ROOT, quiet: bool = False) -> subprocess.CompletedProcess:
    print("$", " ".join(str(a) for a in argv), flush=True)
    result = subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True)
    if not quiet and result.returncode != 0:
        print(result.stdout[-4000:], result.stderr[-4000:], sep="\n")
    return result


def configure(build: Path, ver: str, version: str | None, quiet: bool = False) -> subprocess.CompletedProcess:
    argv = ["cmake", "-S", ROOT / "port", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DXG_VERSION={ver.upper()}", "-DXG_EXTRAS=ON", f"-DPython3_EXECUTABLE={sys.executable}"]
    if version is not None:
        argv.append(f"-DXG_PACKAGE_VERSION={version}")
    return run(argv, quiet=quiet)


def cmake_sets(path: Path) -> dict[str, str]:
    """set(NAME "value") lines of a generated CMake file (CPackConfig.cmake, CMakeCache.txt)."""
    text = path.read_text()
    if path.name == "CMakeCache.txt":
        return {m[1]: m[2] for m in re.finditer(r"^([\w-]+):\w+=(.*)$", text, re.M)}
    return {m[1]: m[2] for m in re.finditer(r'^set\((\w+)\s+"(.*)"\)$', text, re.M)}


def settings(build: Path, version: str) -> tuple[dict[str, str], str]:
    cpack = cmake_sets(build / "CPackConfig.cmake")
    arch = cpack.get("CPACK_DEBIAN_PACKAGE_ARCHITECTURE", "")
    deb = f"{PACKAGE}_{version}_{arch}.deb"
    check(cpack.get("CPACK_GENERATOR") == "DEB", f"generator DEB ({cpack.get('CPACK_GENERATOR')!r})")
    check(cpack.get("CPACK_PACKAGE_NAME") == PACKAGE, f"package name {PACKAGE}")
    check(cpack.get("CPACK_DEBIAN_PACKAGE_VERSION") == version, f"version {version!r}")
    if platform.machine() in ("x86_64", "AMD64"):
        check(arch == "amd64", f"architecture amd64 ({arch!r})")
    check(cpack.get("CPACK_DEBIAN_FILE_NAME") == deb, f"file name {deb} ({cpack.get('CPACK_DEBIAN_FILE_NAME')!r})")
    check(cpack.get("CPACK_PACKAGING_INSTALL_PREFIX") == "/usr", "install prefix /usr")
    check(cpack.get("CPACK_DEBIAN_PACKAGE_SHLIBDEPS") == "ON", "Depends from dpkg-shlibdeps")
    check("libvulkan1" in re.split(r"[,\s]+", cpack.get("CPACK_DEBIAN_PACKAGE_DEPENDS", "")),
          "libvulkan1 in the extra Depends (the Vulkan loader, opened at run time)")
    return cpack, deb


def expected_files(build: Path) -> tuple[str, set[str]]:
    """The executable's path and the files the package must hold (and nothing else)."""
    cache = cmake_sets(build / "CMakeCache.txt")
    exe = "usr/bin/xenogears-enhanced" if cache.get("XG_ENGINE") == "enhanced" else "usr/bin/xenogears"
    need = {exe} | {f"{DOC}/{name}" for name in LICENCES}
    linked = {"glslang": not cache.get("glslang_DIR", "NOTFOUND").endswith("NOTFOUND")
              and cache.get("XG_GLSLANG", "ON") == "ON",
              "dav1d": not cache.get("XG_DAV1D_LIBRARY", "NOTFOUND").endswith("NOTFOUND")
              and cache.get("XG_DAV1D", "ON") == "ON"}
    for name, lib in OPTIONAL_LICENCES.items():
        if linked[lib]:
            need.add(f"{DOC}/{name}")
    return exe, need


def plan_check(files: set[str], need: set[str], allowed: set[str], where: str) -> None:
    check(need <= files, f"{where}: the executable and the licences ({sorted(need - files) or 'all there'})")
    check(files <= allowed, f"{where}: nothing else, so no game data ({sorted(files - allowed) or 'nothing else'})")


def needed_libraries(exe: Path) -> list[str]:
    result = run(["readelf", "-d", exe], quiet=True)
    return re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", result.stdout)


def owners(exe: Path, libs: list[str]) -> dict[str, str | None]:
    """Each needed library's package by dpkg's records (None: dpkg does not know it)."""
    paths = dict(re.findall(r"^\s*(\S+) => (/\S+)", run(["ldd", exe], quiet=True).stdout, re.M))
    return {lib: dpkg_owner(paths[lib]) if lib in paths else None for lib in libs}


def dpkg_owner(path: str) -> str | None:
    """The package dpkg records for a file, under its /lib or /usr/lib name (merged /usr)."""
    if not shutil.which("dpkg"):
        return None
    names = [path, str(Path(path).resolve())]
    names += ["/usr" + n for n in names if n.startswith("/lib")]
    for name in dict.fromkeys(names):
        result = run(["dpkg", "-S", name], quiet=True)
        if result.returncode == 0:
            return result.stdout.split(":")[0].split(",")[0].strip()
    return None


def debian_host() -> bool:
    libc = run(["gcc", "-print-file-name=libc.so.6"], quiet=True).stdout.strip()
    return bool(libc) and dpkg_owner(libc) is not None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--build", type=Path, help="build directory (default build/pc/deb-<ver>)")
    ap.add_argument("--package-version", help="XG_PACKAGE_VERSION (default: the build's, 0~dev when new)")
    ap.add_argument("--settings-only", action="store_true", help="the CPack settings only: no build or package")
    ap.add_argument("--require-deps", action="store_true",
                    help="fail when this host cannot prove Depends (no dpkg records of its libraries)")
    args = ap.parse_args()
    build = (args.build or ROOT / f"build/pc/deb-{args.ver}").resolve()

    if run([sys.executable, ROOT / "tools/pc_sources.py"], quiet=False).returncode != 0:
        return check(False, "tools/pc_sources.py") or 1
    if not check(configure(build, args.ver, args.package_version).returncode == 0, f"Release configure in {build}"):
        return 1
    version = cmake_sets(build / "CMakeCache.txt").get("XG_PACKAGE_VERSION", "")
    print(f"package version {version!r}")
    cpack, deb = settings(build, version)

    bad = build.with_name(build.name + "-badversion")
    refused = configure(bad, args.ver, "v2026.11.0", quiet=True)
    check(refused.returncode != 0 and "is not a Debian version" in refused.stdout + refused.stderr,
          "a version with the tag's v is refused at configure time")
    shutil.rmtree(bad, ignore_errors=True)

    if args.settings_only:
        print("settings only: no build, install plan or package checked")
        return report(dependencies_proven=None)

    if not check(run(["cmake", "--build", build]).returncode == 0, "Release build"):
        return report(dependencies_proven=None)
    exe_rel, need = expected_files(build)
    allowed = need

    stage = build / "deb-stage"
    shutil.rmtree(stage, ignore_errors=True)
    if check(run(["cmake", "--install", build, "--prefix", stage / "usr"]).returncode == 0, "cmake --install"):
        staged = {p.relative_to(stage).as_posix() for p in stage.rglob("*") if not p.is_dir()}
        plan_check(staged, need, allowed, "install plan")
    shutil.rmtree(stage, ignore_errors=True)

    debian = debian_host()
    cpack_argv = ["cpack", "-G", "DEB"]
    if not debian:
        # dpkg-shlibdeps refuses libraries it cannot place; here the GCC runtime
        # lives in a versioned folder outside the linker's search paths.
        gcc_dirs = sorted({str(Path(run(["gcc", f"-print-file-name={lib}"], quiet=True).stdout.strip()).parent)
                           for lib in ("libstdc++.so.6", "libgcc_s.so.1")})
        cpack_argv += ["-D", "CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS=" + ";".join(gcc_dirs)]
    (build / deb).unlink(missing_ok=True)
    packed = run(cpack_argv, cwd=build)
    print(packed.stdout[-2000:], packed.stderr[-2000:], sep="\n")
    if not check(packed.returncode == 0 and (build / deb).is_file(), f"cpack -G DEB made {deb}"):
        return report(dependencies_proven=None)

    depends: list[str] = []
    if shutil.which("dpkg-deb"):
        listing = run(["dpkg-deb", "-c", build / deb])
        info = run(["dpkg-deb", "-I", build / deb])
        print(listing.stdout, info.stdout, sep="\n")
        entries = {}
        for line in listing.stdout.splitlines():
            fields = line.split(None, 5)
            if len(fields) == 6 and not fields[0].startswith("d"):
                entries[fields[5].removeprefix("./")] = (fields[0], fields[1])
        plan_check(set(entries), need, allowed, "dpkg-deb -c")
        check(entries.get(exe_rel) == ("-rwxr-xr-x", "root/root"), f"/{exe_rel} rwxr-xr-x root/root")
        control = dict(re.findall(r"^\s*([\w-]+): (.*)$", info.stdout, re.M))
        check(control.get("Package") == PACKAGE, f"Package: {control.get('Package')}")
        check(control.get("Version") == version, f"Version: {control.get('Version')}")
        check(control.get("Architecture") == cpack.get("CPACK_DEBIAN_PACKAGE_ARCHITECTURE"),
              f"Architecture: {control.get('Architecture')}")
        depends = [d.split("(")[0].strip() for d in control.get("Depends", "").split(",") if d.strip()]
        check("libvulkan1" in depends, f"Depends: {control.get('Depends')}")
    else:
        print("LIMITATION  no dpkg-deb: the .deb's contents and control fields are not read; "
              "the install plan and the CPack settings above stand in")

    exe = build / Path(exe_rel).name
    libs = needed_libraries(exe)
    if not check(bool(libs), f"the executable's shared libraries (readelf -d): {', '.join(libs)}"):
        return report(dependencies_proven=None)
    if not debian or not depends:
        print("LIMITATION  Depends is NOT proven here: " + ("this host's dpkg has no record of its libraries "
              "(not Debian or Ubuntu), so dpkg-shlibdeps adds nothing for them" if not debian else
              "the control fields were not read") + f"; Depends is {depends or 'unread'}")
        return report(dependencies_proven=False, required=args.require_deps)
    for lib, package in owners(exe, libs).items():
        if check(package is not None, f"dpkg knows {lib}'s package ({package})"):
            check(package in depends, f"{lib}: {package} in Depends")
    return report(dependencies_proven=True)


def report(dependencies_proven: bool | None, required: bool = False) -> int:
    if dependencies_proven is False and required:
        check(False, "--require-deps: Depends proven on this host")
    if failures:
        print(f"check_deb_package: {len(failures)} check(s) failed")
        return 1
    note = {True: "Depends proven", False: "Depends NOT proven on this host", None: "Depends not checked"}
    print(f"check_deb_package: all checks pass ({note[dependencies_proven]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
