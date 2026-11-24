#!/usr/bin/env python3
"""Test the Gentoo ebuild template and its renderer (packaging/ in xenogears-pc).

    python tools/test_gentoo_package.py

1. Public layout: gentoo_assets.py and xenogears-pc.ebuild.in, alone in a
   scratch public/packaging/ (as tools/publish_decomp.py lays
   tools/publish/repo-port/ over the tree), import only the standard library,
   name no private folder, no release version and no folder of this machine,
   and pass the publisher's own date, time and private-text patterns (loaded
   from the publisher, unchanged); so does this test. The scans are shown to
   fire on a scratch copy with this run's scratch folder, date and time
   planted in it: values read at run time, never written in any file.
2. For labelled fixture releases (made-up versions; stand-in tarballs made
   here: not releases, no game data) the renderer, run as python -I with a
   bare environment from another folder, writes only
   xenogears-pc-<version>.ebuild: the template with its one placeholder
   filled in. bash -n accepts it. Sourced by bash with stand-ins for the
   eclass functions (the version reaches bash only as PV), it gives EAPI 8,
   cmake inherited, ~amd64, the release's tarball asset as SRC_URI, the
   tarball's folder as S, the dependencies, and phases that stop without
   asm/us, write the source list, configure and install. That is the
   ebuild's own logic, not a Portage build.
3. Release flags and libraries, with real CMake on a small fixture project
   shaped like the port (an object library compiled -fno-pie, a C++-linked
   -no-pie executable, plus one library without a type, which the port does
   not have). An "eclass-like" initial cache, written here and labelled as a
   stand-in, does what Gentoo's cmake.eclass is documented to do: it sets the
   build type's compiler and linker flags to nothing and BUILD_SHARED_LIBS
   on. The ebuild's own src_configure arguments and flag environment, run
   through that cache, must give the same cache flags, compile and link
   commands, library types and built program as a plain Release configure
   (the port's documented release configure). The earlier faulty recipe (no
   explicit Release flags, no BUILD_SHARED_LIBS=OFF) must be caught by the
   same comparison: it loses -O3 -DNDEBUG. Fixture evidence, not Gentoo's.
4. The template agrees with the port's rules: the release-source workflow's
   tarball name and prefix, the CMake requirements as dependencies, the
   Ubuntu package's configure options and install rules, the licences, every
   add_library of the port having a type, and no first-run claims
   (pkg_postinst, a setup option, a Vulkan version) left in it.
5. Refusals, exit 1 with nothing written (an earlier ebuild left as it was):
   malformed or shell-unsafe tags and versions, broken templates, a missing
   template, a folder or symlink in the output's place.

No network, GPU, Portage or game data; nothing is installed or uploaded.
Output stays in a scratch folder under build/. Exit status 1 on any failure.
"""
from __future__ import annotations

import ast
import datetime
import gzip
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "tools/publish/repo-port/packaging"
HELPER, TEMPLATE = "gentoo_assets.py", "xenogears-pc.ebuild.in"
# Made-up versions for this test only: not releases.
RELEASES = (("v2026.11.0", "2026.11.0"), ("v2027.01.12", "2027.01.12"), ("v2099.12.123456789", "2099.12.123456789"))
URL = "https://github.com/EtherAura/xenogears-pc"
BAD_TAGS = ["2026.11.0", "v2026.11", "v2026.13.0", "v2026.00.0", "v2026.1.0", "v2026.11.01", "v26.11.0",
            "v2026.11.0-rc1", "v2026.11.-1", " v2026.11.0", "v2026.11.0\n", "V2026.11.0", "", "v2026.11.0.0",
            "v2026.11.0/../x", "v2026.11.0;touch PWNED", "v2026.11.0$(touch PWNED)", "v2026.11.0'", "v2026.11.0`id`",
            "v2026.11.0 x", "v٢026.11.0", "v2026.11.1٢", "vv2026.11.0", "v2026.11.0${IFS}x", "../v2026.11.0",
            "v2026.11.1234567890", "v2026.11.0\"", "v1999.11.0"]
RDEPEND = {"media-libs/libsdl3", "media-libs/vulkan-loader", "dev-util/glslang", "media-libs/dav1d"}
DEPEND = RDEPEND | {"dev-util/vulkan-headers"}
BDEPEND = {"fixture-python-deps", "any-of(dev-python/pyyaml[${PYTHON_USEDEP}])", "media-libs/shaderc",
           "llvm-core/llvm"}
USER_FLAGS = {"CFLAGS": "-O1 -DXG_FIXTURE_USER_FLAG", "CXXFLAGS": "-O1 -DXG_FIXTURE_USER_FLAG",
              "CPPFLAGS": "-DXG_FIXTURE_USER_FLAG", "LDFLAGS": "-Wl,--as-needed"}
FLAG_NAMES = tuple(USER_FLAGS)
RECIPE_LINES = ("-DCMAKE_C_FLAGS_RELEASE=", "-DCMAKE_CXX_FLAGS_RELEASE=", "-DBUILD_SHARED_LIBS=OFF")
CACHE_NAMES = ("CMAKE_BUILD_TYPE", "CMAKE_INSTALL_PREFIX", "CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS",
               "CMAKE_C_FLAGS_RELEASE", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_EXE_LINKER_FLAGS",
               "CMAKE_EXE_LINKER_FLAGS_RELEASE", "CMAKE_SHARED_LINKER_FLAGS_RELEASE",
               "CMAKE_MODULE_LINKER_FLAGS_RELEASE", "CMAKE_STATIC_LINKER_FLAGS_RELEASE")
SHORT, LONG = 60, 900  # seconds: every subprocess is bounded (CMake may wait its turn on a shared host)

# Stand-ins for Portage and the eclasses: the ebuild's own logic, nothing of Gentoo's.
HARNESS = r'''
ebuild=$1 phase=$2
INHERITED=()
inherit() { INHERITED+=("$@"); }
PYTHON_DEPS="fixture-python-deps"
python_gen_any_dep() { printf 'any-of(%s)' "$1"; }
python_has_version() { return 0; }
die() { printf 'die: %s\n' "$*" >&2; exit 77; }
eerror() { printf 'eerror: %s\n' "$*" >&2; }
cmake_src_prepare() { echo cmake_src_prepare >>"$FIXTURE_LOG"; }
cmake_src_configure() {
    echo cmake_src_configure >>"$FIXTURE_LOG"
    printf '%s\0' "${mycmakeargs[@]}" >"$FIXTURE_OUT/args"
    local v
    for v in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS; do
        if [[ -v $v ]]; then printf '%s\0set\0%s\0' "$v" "${!v}"; else printf '%s\0unset\0\0' "$v"; fi
    done >"$FIXTURE_OUT/flags"
}
cmake_src_install() {
    echo cmake_src_install >>"$FIXTURE_LOG"
    mkdir -p "$ED/usr/bin" "$ED/usr/share/doc/$PN"
    : >"$ED/usr/bin/xenogears"
    : >"$ED/usr/share/doc/$PN/COPYING"
}
source "$ebuild" || exit 9
case "$phase" in
vars)
    for n in EAPI KEYWORDS SRC_URI S CMAKE_USE_DIR CMAKE_BUILD_TYPE CMAKE_MAKEFILE_GENERATOR SLOT LICENSE \
             HOMEPAGE DESCRIPTION RESTRICT RDEPEND DEPEND BDEPEND; do
        printf '%s\0%s\0' "$n" "${!n-}"
    done
    printf 'INHERITED\0%s\0' "${INHERITED[*]}"
    printf 'FUNCTIONS\0%s\0' "$(declare -F | cut -d' ' -f3 | tr '\n' ' ')"
    ;;
src_prepare|src_configure|src_install)
    cd "$S" || exit 8
    "$phase"
    ;;
*) exit 7 ;;
esac
'''

FIXTURE_PC_SOURCES = '''# Test fixture, not the port's tools/pc_sources.py: writes an empty source list.
import os
import sys
from pathlib import Path

if os.environ.get("FIXTURE_FAIL"):
    sys.exit(3)
out = Path("build/pc/sources.cmake")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("# test fixture\\n")
'''

# The real-CMake fixture: the port's shape in miniature, reporting its own flags.
FIXTURE_CMAKE = '''# Test fixture, not the port's build: the shape of port/CMakeLists.txt in
# miniature (C, then C++; an object library compiled -fno-pie; an executable
# linked as C++ with -no-pie; an install rule), plus one library without a
# type, which the port does not have, to show what BUILD_SHARED_LIBS changes.
cmake_minimum_required(VERSION 3.25)
project(eclass_like_fixture C)
enable_language(CXX)
set(XG_VERSION "US" CACHE STRING "")
option(XG_EXTRAS "" ON)
set(XG_PACKAGE_VERSION "0~dev" CACHE STRING "")
add_library(objects OBJECT probe.c)
target_compile_options(objects PRIVATE -fno-pie)
add_library(untyped EXCLUDE_FROM_ALL untyped.c)
add_executable(probe main.cpp $<TARGET_OBJECTS:objects>)
target_compile_options(probe PRIVATE -fno-pie)
set_target_properties(probe PROPERTIES LINKER_LANGUAGE CXX)
target_link_options(probe PRIVATE -no-pie)
get_target_property(objects_type objects TYPE)
get_target_property(untyped_type untyped TYPE)
file(WRITE "${CMAKE_BINARY_DIR}/fixture-types.txt" "objects=${objects_type}\\nuntyped=${untyped_type}\\n")
install(TARGETS probe RUNTIME DESTINATION bin)
'''
FIXTURE_HEADER = '''/* Test fixture: the flags this file was compiled with, as text. */
#ifdef __OPTIMIZE__
#define FIXTURE_OPTIMIZE "optimize=1"
#else
#define FIXTURE_OPTIMIZE "optimize=0"
#endif
#ifdef NDEBUG
#define FIXTURE_NDEBUG " ndebug=1"
#else
#define FIXTURE_NDEBUG " ndebug=0"
#endif
#ifdef __PIC__
#define FIXTURE_PIC " pic=1"
#else
#define FIXTURE_PIC " pic=0"
#endif
#ifdef XG_FIXTURE_USER_FLAG
#define FIXTURE_USER " userflag=1"
#else
#define FIXTURE_USER " userflag=0"
#endif
#define FIXTURE_FLAGS FIXTURE_OPTIMIZE FIXTURE_NDEBUG FIXTURE_PIC FIXTURE_USER
'''
FIXTURE_SOURCES = {
    "CMakeLists.txt": FIXTURE_CMAKE,
    "flags.h": FIXTURE_HEADER,
    "probe.c": '#include "flags.h"\nconst char *fixture_c_flags(void) { return FIXTURE_FLAGS; }\n',
    "untyped.c": "int fixture_untyped(void) { return 0; }\n",
    "main.cpp": '#include <cstdio>\n#include "flags.h"\nextern "C" const char *fixture_c_flags(void);\n'
                'int main() { std::printf("c: %s\\ncxx: %s\\n", fixture_c_flags(), FIXTURE_FLAGS); return 0; }\n',
}
PROBE_RELEASE = "c: optimize=1 ndebug=1 pic=0 userflag=0\ncxx: optimize=1 ndebug=1 pic=0 userflag=0\n"

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def load(path: Path, name: str = "gentoo_assets_under_test"):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bare_env(**extra: str) -> dict[str, str]:
    return {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C", **extra}


def run_cli(helper: Path, *args: str, cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-I", str(helper), *args], cwd=cwd, env=bare_env(),
                          capture_output=True, text=True, timeout=SHORT)


def fixture_tarball(directory: Path, version: str) -> Path:
    """A labelled stand-in for the release tarball, in the release's layout: the same bytes every run."""
    name = f"xenogears-pc-{version}"
    members = ((f"{name}/FIXTURE.txt", f"test fixture for {version}, not a release\n"),
               (f"{name}/port/CMakeLists.txt", "# test fixture, not the port's build\n"),
               (f"{name}/tools/pc_sources.py", FIXTURE_PC_SOURCES))
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for member, text in members:
            data = text.encode()
            info = tarfile.TarInfo(member)
            info.size, info.mtime, info.mode, info.uname, info.gname = len(data), 0, 0o644, "", ""
            tar.addfile(info, io.BytesIO(data))
    out = directory / f"{name}.tar.gz"
    with out.open("wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as gz:
        gz.write(raw.getvalue())
    return out


def machine_paths(text: str) -> list[str]:
    """This machine's own folders named in a text (read at run time, written nowhere)."""
    return [needle for needle in {str(ROOT), str(Path.home())} if needle in text]


def publisher_flags(publish, text: str) -> list[str]:
    return [line for line in text.splitlines()
            if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line) or publish.FORBIDDEN.search(line)]


def public_layout(scratch: Path) -> Path:
    public = scratch / "public/packaging"
    public.mkdir(parents=True)
    for name in (HELPER, TEMPLATE):
        shutil.copy2(OVERLAY / name, public / name)
    source = (public / HELPER).read_text()
    imports = set()
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Import):
            imports |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom) and node.module:
            imports.add(node.module.split(".")[0])
    outside = sorted(imports - set(sys.stdlib_module_names) - {"__future__"})
    check(not outside, f"public helper imports only the standard library ({outside or sorted(imports)})")
    body = source.split("\n", 1)[1] if source.startswith("#!") else source
    private = re.findall(r"\b(?:tools|docs|work|bin|disc)/", body)
    check(not private, f"public helper names no private folder ({private or 'none'})")
    check(not any(w in source for w in ("subprocess", "os.system", "popen", "eval(", "exec(")),
          "public helper runs no command and evaluates no input")
    publish = load(ROOT / "tools/publish_decomp.py", "publish_decomp_patterns")
    files = [public / HELPER, public / TEMPLATE, Path(__file__).resolve()]
    for path in files:
        text = path.read_text()
        flagged = publisher_flags(publish, text)
        check(not flagged, f"{path.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
        check(not machine_paths(text), f"{path.name} names no folder of this machine ({len(machine_paths(text))} found)")
    for name in (HELPER, TEMPLATE):
        found = re.findall(r"20[0-9][0-9]\.[0-9]{1,2}\.[0-9]+", (public / name).read_text())
        check(not found, f"{name} names no release version ({found or 'none'})")
    # The scans fire: a scratch copy (never a public file) with this run's own values planted.
    template = (public / TEMPLATE).read_text()
    check(bool(machine_paths(template + f"# {scratch}\n")), "the machine-folder scan flags this run's scratch folder")
    now = datetime.datetime.now()
    check(bool(publisher_flags(publish, template + f"# {now.date().isoformat()}\n")),
          "the publisher's patterns flag this run's date")
    check(bool(publisher_flags(publish, template + f"# {now.strftime('T%H:%M:%S')}\n")),
          "the publisher's patterns flag this run's time")
    return public / HELPER


def harness(ebuild: Path, phase: str, version: str, work: Path, **extra: str) -> subprocess.CompletedProcess:
    """Run one part of the ebuild in bash with the stand-in eclass functions; the version only as PV."""
    out = work / "harness-out"
    out.mkdir(parents=True, exist_ok=True)
    env = bare_env(PV=version, PN="xenogears-pc", P=f"xenogears-pc-{version}", PF=f"xenogears-pc-{version}",
                   WORKDIR=str(work / "work"), ED=str(work / "image"), EPYTHON=sys.executable,
                   FIXTURE_LOG=str(out / "log"), FIXTURE_OUT=str(out))
    env.update(extra)
    return subprocess.run(["bash", "--noprofile", "--norc", "-c", HARNESS, "_", str(ebuild), phase], env=env,
                          capture_output=True, timeout=SHORT)


def log_of(work: Path) -> list[str]:
    log = work / "harness-out/log"
    return log.read_text().split() if log.exists() else []


def configure_capture(ebuild: Path, version: str, work: Path) -> tuple[list[str], dict[str, str | None]] | None:
    """src_configure's CMake arguments and the flag environment it leaves, from a run with user flags set."""
    for name in ("args", "flags"):
        (work / "harness-out" / name).unlink(missing_ok=True)
    r = harness(ebuild, "src_configure", version, work, **USER_FLAGS)
    args_file, flags_file = work / "harness-out/args", work / "harness-out/flags"
    if r.returncode != 0 or not args_file.exists() or not flags_file.exists():
        return None
    args = [a.decode() for a in args_file.read_bytes().split(b"\0")[:-1]]
    fields = [f.decode() for f in flags_file.read_bytes().split(b"\0")[:-1]]
    flags = {fields[i]: (fields[i + 2] if fields[i + 1] == "set" else None) for i in range(0, len(fields), 3)}
    return args, flags


def renders(helper: Path, scratch: Path) -> dict[str, Path]:
    module = load(helper)
    template = (helper.parent / TEMPLATE).read_text()
    check(template.count("@TAG@") == 1, "the template has one placeholder, @TAG@")
    fixtures = scratch / "fixtures"
    fixtures.mkdir()
    elsewhere = scratch / "elsewhere"
    elsewhere.mkdir()
    ebuilds: dict[str, Path] = {}
    captured = {}
    for tag, version in RELEASES:
        name = f"xenogears-pc-{version}.ebuild"
        out = scratch / f"out-{version}"
        r = run_cli(helper, "--tag", tag, "--out-dir", str(out), cwd=elsewhere)
        check(r.returncode == 0 and r.stdout.strip() == str(out / name),
              f"{tag}: renders with python -I from another folder ({r.stderr.strip() or 'ok'})")
        ebuild = out / name
        if not ebuild.is_file():
            check(False, f"{tag}: {name} written")
            continue
        check(sorted(p.name for p in out.iterdir()) == [name], f"{tag}: only {name} is written")
        check(ebuild.stat().st_mode & 0o777 == 0o644, f"{tag}: mode 644")
        text = ebuild.read_text()
        ebuilds[version] = ebuild
        check(text == template.replace("@TAG@", tag), f"{tag}: the ebuild is the template with only @TAG@ filled in")
        check(text == module.render(template, version), f"{tag}: CLI and render() agree")
        check(module.ebuild_name(version) == name and module.tarball_name(version) == f"xenogears-pc-{version}.tar.gz",
              f"{tag}: the helper's file names")
        other = scratch / f"by-version-{version}"
        r = run_cli(helper, "--version", version, "--out-dir", str(other), cwd=helper.parent)
        check(r.returncode == 0 and (other / name).read_text() == text,
              f"{tag}: --version gives the same ebuild, run beside the template")
        r = run_cli(helper, "--tag", tag, "--out-dir", str(out), cwd=elsewhere)
        check(r.returncode == 0 and ebuild.read_text() == text and sorted(p.name for p in out.iterdir()) == [name],
              f"{tag}: rendering again gives the same bytes and no other file")
        syntax = subprocess.run(["bash", "-n", str(ebuild)], capture_output=True, text=True, timeout=SHORT)
        check(syntax.returncode == 0, f"{tag}: bash -n accepts the ebuild ({syntax.stderr.strip() or 'ok'})")

        work = scratch / f"harness-{version}"
        r = harness(ebuild, "vars", version, work)
        fields = r.stdout.decode().split("\0")[:-1]
        got = dict(zip(fields[::2], fields[1::2]))
        check(r.returncode == 0, f"{tag}: bash sources the ebuild ({r.stderr.decode().strip() or 'ok'})")
        check(got.get("EAPI") == "8", f"{tag}: EAPI 8 ({got.get('EAPI')})")
        check(got.get("INHERITED", "").split() == ["cmake", "python-any-r1"], f"{tag}: inherits cmake ({got.get('INHERITED')})")
        check(got.get("KEYWORDS") == "~amd64", f"{tag}: KEYWORDS ~amd64 ({got.get('KEYWORDS')})")
        tarball = fixture_tarball(fixtures, version)
        check(tarball.read_bytes() == fixture_tarball(elsewhere, version).read_bytes(),
              f"{tag}: fixture tarball is the same bytes twice")
        want_uri = f"{URL}/releases/download/{tag}/{tarball.name}"
        check(got.get("SRC_URI") == want_uri, f"{tag}: SRC_URI is the release's tarball asset ({got.get('SRC_URI')})")
        workdir = work / "work"
        workdir.mkdir(parents=True)
        with tarfile.open(tarball) as tar:
            tar.extractall(workdir, filter="data")
        source = workdir / f"xenogears-pc-{version}"
        check(got.get("S") == str(source) and source.is_dir(), f"{tag}: S is the fixture tarball's one folder")
        check(got.get("CMAKE_USE_DIR") == str(source / "port") and (source / "port/CMakeLists.txt").is_file(),
              f"{tag}: CMAKE_USE_DIR holds the unpacked port/CMakeLists.txt")
        check(got.get("CMAKE_BUILD_TYPE") == "Release" and got.get("CMAKE_MAKEFILE_GENERATOR") == "ninja",
              f"{tag}: a Release build with Ninja")
        check(got.get("SLOT") == "0" and got.get("LICENSE") == "GPL-3+ MIT OFL-1.1" and got.get("HOMEPAGE") == URL
              and got.get("RESTRICT") == "test" and bool(got.get("DESCRIPTION")),
              f"{tag}: SLOT, LICENSE, HOMEPAGE, RESTRICT and a DESCRIPTION")
        check(set(got.get("RDEPEND", "").split()) == RDEPEND, f"{tag}: RDEPEND {got.get('RDEPEND', '').split()}")
        check(set(got.get("DEPEND", "").split()) == DEPEND, f"{tag}: DEPEND is RDEPEND and the Vulkan headers")
        check(set(got.get("BDEPEND", "").split()) == BDEPEND, f"{tag}: BDEPEND {got.get('BDEPEND', '').split()}")
        functions = set(got.get("FUNCTIONS", "").split())
        check({"src_prepare", "src_configure", "src_install", "python_check_deps"} <= functions
              and not {"pkg_postinst", "pkg_setup", "src_unpack", "src_compile", "src_test"} & functions,
              f"{tag}: defines the prepare, configure and install phases and no others")

        # src_prepare: the release tarball has no asm/us (xg-4oq.8), so it stops there.
        r = harness(ebuild, "src_prepare", version, work)
        err = r.stderr.decode()
        check(r.returncode == 77 and "asm/us" in err and "player's own extracted disc" in err
              and log_of(work) == [] and not (source / "build").exists(),
              f"{tag}: src_prepare stops on a source without asm/us, before CMake or any file")
        (source / "asm/us").mkdir(parents=True)  # an empty stand-in folder: no assembly, no game data
        r = harness(ebuild, "src_prepare", version, work, FIXTURE_FAIL="1")
        check(r.returncode == 77 and "pc_sources" in r.stderr.decode() and not (source / "build").exists(),
              f"{tag}: src_prepare stops when tools/pc_sources.py fails")
        (work / "harness-out/log").unlink(missing_ok=True)
        r = harness(ebuild, "src_prepare", version, work)
        check(r.returncode == 0 and log_of(work) == ["cmake_src_prepare"] and (source / "build/pc/sources.cmake").is_file(),
              f"{tag}: with asm/us, src_prepare writes build/pc/sources.cmake ({r.stderr.decode().strip() or 'ok'})")

        capture = configure_capture(ebuild, version, work)
        check(capture is not None, f"{tag}: src_configure reaches cmake_src_configure")
        if capture:
            args, flags = capture
            captured[version] = capture
            want = ["-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG", "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG",
                    "-DBUILD_SHARED_LIBS=OFF", "-DXG_VERSION=US", "-DXG_EXTRAS=ON", f"-DXG_PACKAGE_VERSION={version}",
                    "-DXG_INSTALL_KIND=gentoo"]
            check(args == want, f"{tag}: src_configure's arguments ({args})")
            check(flags == dict.fromkeys(FLAG_NAMES), f"{tag}: the user's {', '.join(FLAG_NAMES)} are unset ({flags})")

        r = harness(ebuild, "src_install", version, work)
        image = work / "image/usr"
        check(r.returncode == 0 and (image / "bin/xenogears").is_file()
              and (image / f"share/doc/xenogears-pc-{version}/COPYING").is_file()
              and not (image / "share/doc/xenogears-pc").exists(),
              f"{tag}: src_install keeps /usr/bin/xenogears and moves the licences to the versioned doc folder")
    check(len({p.read_text() for p in ebuilds.values()}) == len(RELEASES) == len(ebuilds),
          "each fixture release renders its own ebuild")
    if len(captured) == len(RELEASES):
        strip = [[a for a in captured[v][0] if not a.startswith("-DXG_PACKAGE_VERSION=")] for _, v in RELEASES]
        check(all(s == strip[0] for s in strip), "every fixture release configures alike, but for its version")
    return ebuilds


# ---- release flags and libraries: real CMake, an eclass-like initial cache ----

def eclass_like_cache(build_type: str) -> str:
    """A stand-in, written for this test, for what cmake.eclass's initial cache is documented to do."""
    upper = build_type.upper()
    lines = ["# Test fixture: an eclass-like initial cache. Not cmake.eclass, and no Portage build:",
             "# it models two documented behaviours of Gentoo's cmake.eclass, a shared-library",
             "# default and the build type's own flags set to nothing.",
             'set(BUILD_SHARED_LIBS ON CACHE BOOL "")']
    lines += [f'set(CMAKE_{lang}_FLAGS_{upper} "" CACHE STRING "")' for lang in ("ASM", "C", "CXX")]
    lines += [f'set(CMAKE_{kind}_LINKER_FLAGS_{upper} "" CACHE STRING "")' for kind in ("EXE", "MODULE", "SHARED", "STATIC")]
    return "\n".join(lines) + "\n"


def cmake_env(flags: dict[str, str | None]) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if k not in FLAG_NAMES}
    env["LC_ALL"] = "C"
    env.update({k: v for k, v in flags.items() if v is not None})
    return env


def cmake_build(source: Path, build: Path, args: list[str], flags: dict[str, str | None],
                build_type: str, initial_cache: Path | None) -> dict | None:
    """Configure as cmake.eclass orders its command line, build the probe, and report what came out."""
    command = ["cmake", "-S", str(source), "-B", str(build)]
    if initial_cache:
        command += ["-C", str(initial_cache)]
    command += ["-G", "Ninja", "-DCMAKE_INSTALL_PREFIX=/usr", *args, f"-DCMAKE_BUILD_TYPE={build_type}",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    env = cmake_env(flags)
    try:
        r = subprocess.run(command, env=env, capture_output=True, text=True, timeout=LONG)
        if r.returncode != 0:
            print(f"note {build.name}: configure failed\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
            return None
        r = subprocess.run(["cmake", "--build", str(build), "--target", "probe"], env=env, capture_output=True,
                           text=True, timeout=LONG)
        if r.returncode != 0:
            print(f"note {build.name}: build failed\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
            return None
        probe = subprocess.run([str(build / "probe")], capture_output=True, text=True, timeout=SHORT, env=bare_env())
    except subprocess.TimeoutExpired as error:
        print(f"note {build.name}: timed out ({error})")
        return None
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        m = re.match(r"([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if m:
            cache[m[1]] = m[2]
    commands = {}
    for entry in json.loads((build / "compile_commands.json").read_text()):
        commands[Path(entry["file"]).name] = entry["command"].replace(str(build), "<build>")
    link, keep = [], False
    for line in (build / "build.ninja").read_text().splitlines():
        if line.startswith("build "):
            keep = line.startswith("build probe:") or "CMakeFiles/probe.dir" in line or "CMakeFiles/objects.dir" in line
        elif keep and re.match(r"\s+(FLAGS|DEFINES|LINK_FLAGS|LINK_LIBRARIES) = ", line):
            link.append(line.strip())
    header = (build / "probe").read_bytes()[:18]
    return {"cache": {name: cache.get(name) for name in CACHE_NAMES},
            "shared": cache.get("BUILD_SHARED_LIBS"),
            "commands": {name: commands.get(name) for name in ("probe.c", "main.cpp")},
            "flags": link,
            "types": (build / "fixture-types.txt").read_text().split(),
            "probe": probe.stdout if probe.returncode == 0 else f"exit {probe.returncode}",
            "elf": int.from_bytes(header[16:18], "little") if header[:4] == b"\x7fELF" else -1}


def contract_problems(release: dict, got: dict) -> list[str]:
    """How a build differs from the plain Release build: nothing, if the release contract holds."""
    problems = [f"cache {name}: {got['cache'][name]!r}, not {release['cache'][name]!r}"
                for name in CACHE_NAMES if got["cache"][name] != release["cache"][name]]
    problems += [f"compile command of {name}: {got['commands'][name]}"
                 for name in release["commands"] if got["commands"][name] != release["commands"][name]]
    for key in ("flags", "types", "probe", "elf"):
        if got[key] != release[key]:
            problems.append(f"{key}: {got[key]!r}, not {release[key]!r}")
    return problems


def faulty_recipe(ebuild: Path, directory: Path) -> Path | None:
    """The earlier, rejected src_configure: the same ebuild without its explicit Release flags and library default."""
    lines = ebuild.read_text().split("\n")
    kept = [line for line in lines if not any(line.strip().startswith(part) for part in RECIPE_LINES)]
    if len(lines) - len(kept) != len(RECIPE_LINES):
        return None
    directory.mkdir()
    out = directory / ebuild.name
    out.write_text("\n".join(kept))
    return out


def release_flags(ebuilds: dict[str, Path], scratch: Path) -> None:
    missing = [tool for tool in ("cmake", "ninja") if not shutil.which(tool)]
    check(not missing, f"CMake and Ninja are here for the flag fixture ({missing or 'found'})")
    tag, version = RELEASES[0]
    if missing or version not in ebuilds:
        check(False, "the release-flag fixture ran")
        return
    ebuild = ebuilds[version]
    root = scratch / "eclass-like"
    source = root / "source"
    source.mkdir(parents=True)
    for name, text in FIXTURE_SOURCES.items():
        (source / name).write_text(text)
    initial = root / "eclass_like_initial_cache.cmake"
    initial.write_text(eclass_like_cache("Release"))

    # 1. The contract: the port's documented release configure (port/cmake/deb.cmake), plain CMake.
    port_args = ["-DXG_VERSION=US", "-DXG_EXTRAS=ON", f"-DXG_PACKAGE_VERSION={version}", "-DXG_INSTALL_KIND=gentoo"]
    release = cmake_build(source, root / "plain-release", port_args, dict.fromkeys(FLAG_NAMES), "Release", None)
    check(release is not None, "fixture: a plain CMake Release build of the fixture project")
    if release is None:
        return
    check(release["cache"]["CMAKE_C_FLAGS_RELEASE"] == "-O3 -DNDEBUG" == release["cache"]["CMAKE_CXX_FLAGS_RELEASE"]
          and release["cache"]["CMAKE_EXE_LINKER_FLAGS_RELEASE"] == "" and release["cache"]["CMAKE_C_FLAGS"] == "",
          f"fixture: CMake's own Release flags here are -O3 -DNDEBUG, no linker flags ({release['cache']})")
    check(all("-O3 -DNDEBUG" in (c or "") and "-fno-pie" in (c or "") and "XG_FIXTURE_USER_FLAG" not in (c or "")
              for c in release["commands"].values()),
          f"fixture: plain Release compile commands carry -O3 -DNDEBUG -fno-pie ({release['commands']['probe.c']})")
    check(release["probe"] == PROBE_RELEASE and release["elf"] == 2 and "-no-pie" in " ".join(release["flags"]),
          f"fixture: the plain Release program is optimised, NDEBUG, non-PIC and a fixed-address executable "
          f"({release['probe'].strip()!r}, e_type {release['elf']})")
    check(release["types"] == ["objects=OBJECT_LIBRARY", "untyped=STATIC_LIBRARY"],
          f"fixture: plain library types ({release['types']})")

    # 2. The ebuild's src_configure through the eclass-like initial cache, the user's flags set beforehand.
    capture = configure_capture(ebuild, version, scratch / f"harness-{version}")
    if capture is None:
        check(False, "fixture: the ebuild's src_configure was captured")
        return
    args, flags = capture
    recipe = cmake_build(source, root / "ebuild-recipe", args, flags, "Release", initial)
    check(recipe is not None, "fixture: the ebuild's recipe configures and builds through the eclass-like cache")
    if recipe is not None:
        problems = contract_problems(release, recipe)
        check(not problems, f"fixture: the ebuild's recipe keeps the Release contract: cache flags, compile and link "
                            f"commands, library types and program all as plain Release ({problems or 'no difference'})")
        check(recipe["shared"] == "OFF", f"fixture: BUILD_SHARED_LIBS is OFF in the cache, over the initial cache's ON "
                                         f"({recipe['shared']})")
        check(recipe["cache"]["CMAKE_INSTALL_PREFIX"] == "/usr", "fixture: installs under /usr")
        stage = root / "stage"
        try:
            r = subprocess.run(["cmake", "--install", str(root / "ebuild-recipe")], env={**cmake_env({}), "DESTDIR": str(stage)},
                               capture_output=True, text=True, timeout=LONG)
            check(r.returncode == 0 and (stage / "usr/bin/probe").is_file(), "fixture: DESTDIR install lands in usr/bin")
        except subprocess.TimeoutExpired:
            check(False, "fixture: DESTDIR install finished in time")

    # 3. The regression: the earlier recipe (flags unset, nothing given back) through the same cache.
    faulty = faulty_recipe(ebuild, root / "faulty-ebuild")
    check(faulty is not None, "regression: the faulty recipe is the ebuild without its three explicit arguments")
    if faulty is None:
        return
    work = scratch / "harness-faulty"
    (work / "work" / f"xenogears-pc-{version}").mkdir(parents=True)
    capture = configure_capture(faulty, version, work)
    if capture is None:
        check(False, "regression: the faulty src_configure was captured")
        return
    old_args, old_flags = capture
    check(old_args == port_args and old_flags == dict.fromkeys(FLAG_NAMES),
          f"regression: the faulty recipe passes only the port's options, flags unset ({old_args})")
    old = cmake_build(source, root / "faulty-recipe", old_args, old_flags, "Release", initial)
    check(old is not None, "regression: the faulty recipe configures and builds through the eclass-like cache")
    if old is None:
        return
    problems = contract_problems(release, old)
    print("note regression: the faulty recipe differs from plain Release in:")
    for problem in problems:
        print(f"note   {problem}")
    check(any(p.startswith("cache CMAKE_C_FLAGS_RELEASE: ''") for p in problems)
          and any(p.startswith("cache CMAKE_CXX_FLAGS_RELEASE: ''") for p in problems),
          "regression: the comparison catches the faulty recipe's empty Release flags (the eclass-like cache blanks them)")
    check(all("-O3" not in (c or "") and "-DNDEBUG" not in (c or "") for c in old["commands"].values())
          and old["probe"] == PROBE_RELEASE.replace("optimize=1 ndebug=1", "optimize=0 ndebug=0"),
          f"regression: the faulty recipe's compile commands and program have lost -O3 -DNDEBUG ({old['probe'].strip()!r})")
    check(old["shared"] == "ON" and old["types"] == ["objects=OBJECT_LIBRARY", "untyped=SHARED_LIBRARY"],
          f"regression: under BUILD_SHARED_LIBS=ON an object library stays one; only a library without a type "
          f"turns shared ({old['types']})")
    check(old["elf"] == 2, "regression: the executable's fixed-address link does not depend on those settings")


# ---- the template against the port's rules ----

def port_rules(text: str) -> None:
    workflow = (ROOT / "tools/publish/repo-port/.github/workflows/release-source.yml").read_text()
    for needle in ('name="xenogears-pc-$VERSION"', '--prefix="$name/"', '-o "dist/$name.tar.gz"',
                   'echo "version=${TAG#v}"', 'gh release upload "$TAG" "$TARBALL"'):
        check(needle in workflow, f"release-source.yml makes the tarball the ebuild expects ({needle})")
    check("releases/download/v${PV}/${P}.tar.gz" in text and 'S="${WORKDIR}/${P}"' in text
          and 'CMAKE_USE_DIR="${S}/port"' in text and (ROOT / "port/CMakeLists.txt").is_file(),
          "SRC_URI, S and CMAKE_USE_DIR follow that name, prefix and tag, and the port's CMakeLists.txt")
    cmake = (ROOT / "port/CMakeLists.txt").read_text()
    gpu = (ROOT / "port/src/gpu/gpu.cmake").read_text()
    deb = (ROOT / "port/cmake/deb.cmake").read_text()
    pc_sources = (ROOT / "tools/pc_sources.py").read_text()
    pc_link = (ROOT / "tools/pc_link.py").read_text()
    rules = [
        ("find_package(SDL3 REQUIRED" in cmake, "media-libs/libsdl3", "RDEPEND"),
        ("CPACK_DEBIAN_PACKAGE_DEPENDS libvulkan1" in deb, "media-libs/vulkan-loader", "RDEPEND"),
        ("find_package(glslang CONFIG" in cmake, "dev-util/glslang", "RDEPEND"),
        ("find_library(XG_DAV1D_LIBRARY NAMES dav1d)" in cmake, "media-libs/dav1d", "RDEPEND"),
        ("find_package(Vulkan REQUIRED COMPONENTS glslc)" in gpu, "dev-util/vulkan-headers", "DEPEND"),
        ("find_package(Vulkan REQUIRED COMPONENTS glslc)" in gpu, "media-libs/shaderc", "BDEPEND"),
        ("find_package(Python3 REQUIRED" in cmake, "${PYTHON_DEPS}", "BDEPEND"),
        (re.search(r"^import yaml$", pc_sources, re.M) is not None, "dev-python/pyyaml", "BDEPEND"),
        ("find_program(XG_DWARFDUMP NAMES llvm-dwarfdump" in cmake, "llvm-core/llvm", "BDEPEND"),
    ]
    for present, package, variable in rules:
        block = re.search(rf'^{variable}="\n(.*?)^"$', text, re.M | re.S)
        check(present and block is not None and package in block[1], f"port rule found and {package} in {variable}")
    check("-G Ninja" in deb and "CMAKE_MAKEFILE_GENERATOR=ninja" in text and "cmake_minimum_required" in cmake
          and re.search(r"^inherit cmake\b", text, re.M) is not None,
          "CMake and Ninja come from cmake.eclass, with the port's generator")
    configure = re.search(r"cmake -S port -B <build> -G Ninja (.*?)\\\n#\s+(.*?)\n", deb)
    flags = [w for w in (configure[1] + " " + configure[2]).split() if w.startswith("-D")] if configure else []
    check(bool(flags), f"deb.cmake documents the release configure ({flags})")
    for flag in flags:
        name, _, value = flag.partition("=")
        if name == "-DCMAKE_BUILD_TYPE":
            check(f"CMAKE_BUILD_TYPE={value}\n" in text, f"the ebuild's build type is the Ubuntu release's ({value})")
        elif name == "-DXG_PACKAGE_VERSION":
            check('-DXG_PACKAGE_VERSION="${PV}"' in text, "src_configure passes the release's version")
        else:
            check(re.search(rf"^\t\t{re.escape(flag)}$", text, re.M) is not None, f"src_configure passes {flag} as the Ubuntu release does")
    check(re.search(r"^\t\t-DXG_INSTALL_KIND=gentoo$", text, re.M) is not None and "XG_INSTALL_KIND" in deb,
          "src_configure names the package gentoo (release.txt's kind, port/cmake/deb.cmake)")
    for option in ("XG_SHARED", "XG_PIE", "XG_LTO", "XG_MARCH", "XG_ENGINE", "XG_GLSLANG", "XG_DAV1D"):
        check(f"-D{option}" not in text, f"the ebuild leaves {option} at the port's default")
    check("install(TARGETS xenogears RUNTIME DESTINATION bin)" in deb and "set(XG_DOC_DIR share/doc/xenogears-pc)" in deb
          and "cmake_src_install" in text and 'mv "${ED}/usr/share/doc/${PN}" "${ED}/usr/share/doc/${PF}" || die' in text,
          "src_install uses the port's install rules, the licences moved to Gentoo's doc folder")
    check("GNU GENERAL PUBLIC LICENSE" in (ROOT / "port/COPYING").read_text()[:200]
          and "Version 3" in (ROOT / "port/COPYING").read_text()[:200]
          and "MIT License" in (ROOT / "port/third_party/imgui/LICENSE.txt").read_text()[:200]
          and "Open Font License" in (ROOT / "port/third_party/jukebox-font/LICENSE.txt").read_text()
          and all(w in deb for w in ('COPYING"', "jukebox-font/LICENSE.txt", "${XG_IMGUI_DIR}/LICENSE.txt"))
          and 'LICENSE="GPL-3+ MIT OFL-1.1"' in text,
          "LICENSE names the licences of the files the port installs (GPL 3, MIT, OFL)")
    check('ROOT / "asm" / ver' in pc_link and "pc_link.py" in cmake and "[[ ! -d ${S}/asm/us ]]" in text,
          "src_prepare tests for the split assembly the PC link reads (asm/<version>)")
    tracked = subprocess.run(["git", "-C", str(ROOT), "ls-files", "--", "asm"], capture_output=True, text=True, timeout=SHORT)
    check(tracked.returncode == 0 and tracked.stdout == "",
          "asm/ is not tracked, so git archive (the release tarball) has none: the ebuild stops in src_prepare today")
    check("python tools/pc_sources.py" in cmake and "XG_SOURCES_CMAKE" in cmake and "build/pc/sources.cmake" in pc_sources
          and '"${EPYTHON}" tools/pc_sources.py' in text, "src_prepare writes the PC source list CMake requires")

    # Release flags: what the port relies on, and what the recipe says.
    check("CMAKE_C_FLAGS" not in cmake and "CMAKE_CXX_FLAGS" not in cmake and "-O2" not in cmake and "-O3" not in cmake,
          "the port sets no optimisation level itself: Release's flags are CMake's own")
    check("unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS" in text and '-DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG"' in text
          and '-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"' in text,
          "src_configure gives CMake's Release flags explicitly and leaves the user's out")
    check("pc_link links each module with ld -r" in cmake and "INTERPROCEDURAL_OPTIMIZATION TRUE" in cmake
          and "flto" not in text.replace("-flto off the game C", ""),
          "the ebuild adds no LTO of its own: the game C's partial link needs ordinary objects (XG_LTO is the port's)")
    check("target_link_options(xenogears PRIVATE -no-pie)" in cmake and "set(XG_PIC_OPTIONS -fno-pie)" in cmake,
          "the port asks for its non-PIE layout itself, whatever the toolchain's default")
    # BUILD_SHARED_LIBS: only an add_library without a type follows it.
    sources = [ROOT / "port/CMakeLists.txt", *sorted((ROOT / "port/cmake").glob("*.cmake")),
               *sorted((ROOT / "port/src").rglob("*.cmake"))]
    calls = []
    for path in sources:
        calls += re.findall(r"\badd_library\(\s*(\S+)\s+(\S+)", path.read_text())
    untyped = [name for name, kind in calls if kind not in ("OBJECT", "SHARED", "STATIC", "MODULE", "INTERFACE")]
    check(len(calls) >= 8 and not untyped,
          f"all {len(calls)} add_library calls of the port name their type, so BUILD_SHARED_LIBS changes none ({untyped or 'none untyped'})")
    shared = [name for name, kind in calls if kind == "SHARED"]
    guarded = re.search(r"^if\(XG_SHARED\)\n    set\(XG_GAME_TARGET xenogears_game\)\n(.*?)^else\(\)\n", cmake, re.M | re.S)
    check(sorted(shared) == ["xenogears_game", "xgloader"] and guarded is not None
          and all(f"add_library({name} SHARED" in guarded[1] for name in shared)
          and 'option(XG_SHARED "the game as libxenogears.so plus a launcher (the Android app\'s shape)" OFF)' in cmake,
          "its only shared libraries are XG_SHARED's, off by default and not asked for by the ebuild")
    check(not any("BUILD_SHARED_LIBS" in path.read_text() or "add_subdirectory" in path.read_text() for path in sources),
          "the port reads BUILD_SHARED_LIBS nowhere and adds no subproject that could")
    check("-DBUILD_SHARED_LIBS=OFF" in text, "the ebuild still passes BUILD_SHARED_LIBS=OFF")

    # First-run claims: none are kept, so none need tying to port code.
    check("pkg_postinst" not in text and "elog" not in text and "--setup" not in text
          and re.search(r"Vulkan\s+1\.\d", text) is None,
          "the template has no pkg_postinst and claims no setup option or Vulkan version")
    deb_version = re.search(r'XG_PACKAGE_VERSION MATCHES "([^"]+)"', deb)
    for _, version in RELEASES:
        check(bool(deb_version and re.fullmatch(deb_version[1].strip("^$"), version)),
              f"deb.cmake accepts XG_PACKAGE_VERSION={version}")
    check(re.search(r"\.(bin|cue|iso|img|chd|ccd|mds|ecm|pbp)\b", text, re.I) is None,
          "the template names no disc image")


# ---- refusals ----

def refusals(helper: Path, scratch: Path) -> None:
    module = load(helper)
    template = (helper.parent / TEMPLATE).read_text()
    tag, version = RELEASES[0]
    name = f"xenogears-pc-{version}.ebuild"
    bad = scratch / "bad"
    bad.mkdir()
    out = scratch / "refused"
    out.mkdir()
    sentinel = "# an earlier ebuild, left as it was\n"
    (out / name).write_text(sentinel)

    def refused(what: str, *args: str) -> None:
        r = run_cli(helper, *args, "--out-dir", str(out), cwd=scratch)
        left = sorted(p.name for p in out.iterdir())
        check(r.returncode == 1 and left == [name] and (out / name).read_text() == sentinel
              and "gentoo_assets:" in r.stderr and "Traceback" not in r.stderr,
              f"refused, nothing written: {what} ({r.stderr.strip().splitlines()[-1] if r.stderr.strip() else r.returncode})")

    for bad_tag in BAD_TAGS:
        refused(f"tag {bad_tag!r}", "--tag", bad_tag)
        as_version = bad_tag[1:] if bad_tag.startswith("v") and bad_tag != tag else bad_tag
        if as_version != version:
            refused(f"version {as_version!r}", "--version", as_version)
    refused("a tag given as the version", "--version", tag)
    refused("a version given as the tag", "--tag", version)
    check(not any(scratch.rglob("PWNED")) and not Path("PWNED").exists() and not (ROOT / "PWNED").exists(),
          "no tag or version ran as shell code")
    fresh = scratch / "never-made"
    r = run_cli(helper, "--tag", BAD_TAGS[1], "--out-dir", str(fresh), cwd=scratch)
    check(r.returncode == 1 and not fresh.exists(), "a refused run makes no output folder")
    for what, args in (("both a tag and a version", ("--tag", tag, "--version", version, "--out-dir", str(out))),
                       ("neither a tag nor a version", ("--out-dir", str(out))),
                       ("no output folder", ("--tag", tag))):
        r = run_cli(helper, *args, cwd=scratch)
        check(r.returncode == 2 and (out / name).read_text() == sentinel and "Traceback" not in r.stderr,
              f"usage error, nothing written: {what}")
    r = run_cli(helper, "--help", cwd=scratch)
    check(r.returncode == 0 and all(w in r.stdout for w in ("--tag", "--version", "--out-dir", "--template")),
          "--help documents the options")

    uri = 'SRC_URI="https://github.com/EtherAura/xenogears-pc/releases/download/v${PV}/${P}.tar.gz"'
    broken = [
        ("no @TAG@", template.replace("@TAG@", "the tag")),
        ("@TAG@ twice", template + "# @TAG@\n"),
        ("an unknown placeholder", template + "# @PKGREL@\n"),
        ("a lowercase placeholder", template + "# @tag@\n"),
        ("EAPI 7", template.replace("EAPI=8\n", "EAPI=7\n")),
        ("EAPI not first", template.replace("EAPI=8\n", "FIRST=1\nEAPI=8\n")),
        ("a second EAPI", template + "EAPI=7\n"),
        ("no cmake inherited", template.replace("inherit cmake python-any-r1", "inherit python-any-r1")),
        ("a second inherit", template + "inherit flag-o-matic\n"),
        ("a stable keyword", template.replace('KEYWORDS="~amd64"', 'KEYWORDS="amd64"')),
        ("another keyword line", template + 'KEYWORDS="~arm64"\n'),
        ("a version written in SRC_URI", template.replace("v${PV}/${P}", "v2026.11.0/xenogears-pc-2026.11.0")),
        ("a version written in a comment", template + "# 2026.11.0\n"),
        ("a branch archive for SRC_URI", template.replace(uri, f'SRC_URI="{URL}/archive/refs/heads/main.tar.gz"')),
        ("another S", template.replace('S="${WORKDIR}/${P}"', 'S="${WORKDIR}"')),
        ("no CMAKE_USE_DIR", template.replace('CMAKE_USE_DIR="${S}/port"\n', "")),
        ("another build type", template.replace("CMAKE_BUILD_TYPE=Release\n", "CMAKE_BUILD_TYPE=RelWithDebInfo\n")),
        ("a character outside ASCII", template + "# é\n"),
        ("CRLF line ends", template.replace("\n", "\r\n")),
        ("a NUL", template + "#\0\n"),
        ("no final newline", template.rstrip("\n")),
        ("nothing in it", ""),
        ("too much in it", template + "#" * 70000 + "\n"),
    ]
    for i, (what, text) in enumerate(broken):
        check(text != template, f"broken template differs from the real one: {what}")
        path = bad / f"template-{i}.in"
        path.write_bytes(text.encode("utf-8"))
        refused(f"template with {what}", "--tag", tag, "--template", str(path))
        try:
            module.render(text, version)
            check(False, f"render() refuses a template with {what}")
        except module.AssetError:
            check(True, f"render() refuses a template with {what}")
    refused("no template", "--tag", tag, "--template", str(bad / "absent.in"))
    refused("a folder for a template", "--tag", tag, "--template", str(bad))

    for what, value in (("a quote", "2026.11.0'"), ("$()", "2026.11.0$(id)"), ("a newline", "2026.11.0\n"),
                        ("a leading v", "v2026.11.0"), ("a path", "../2026.11.0"), ("no text", None), ("bytes", b"2026.11.0")):
        try:
            module.render(template, value)
            check(False, f"render() refuses a version with {what}")
        except module.AssetError:
            check(True, f"render() refuses a version with {what}")

    # A folder or a symlink where the ebuild goes; a stale ebuild; a write that fails.
    blocked = scratch / "blocked"
    (blocked / name).mkdir(parents=True)
    r = run_cli(helper, "--tag", tag, "--out-dir", str(blocked), cwd=scratch)
    check(r.returncode == 1 and (blocked / name).is_dir() and sorted(p.name for p in blocked.iterdir()) == [name]
          and "Traceback" not in r.stderr, "refused: a folder where the ebuild goes")
    linked = scratch / "linked"
    linked.mkdir()
    target = scratch / "link-target.txt"
    target.write_text("left alone\n")
    (linked / name).symlink_to(target)
    r = run_cli(helper, "--tag", tag, "--out-dir", str(linked), cwd=scratch)
    check(r.returncode == 1 and (linked / name).is_symlink() and target.read_text() == "left alone\n"
          and sorted(p.name for p in linked.iterdir()) == [name], "refused: a symlink where the ebuild goes, its target untouched")
    stale = scratch / "stale"
    stale.mkdir()
    (stale / name).write_text(sentinel * 2000)
    r = run_cli(helper, "--tag", tag, "--out-dir", str(stale), cwd=scratch)
    check(r.returncode == 0 and (stale / name).read_text() == template.replace("@TAG@", tag)
          and sorted(p.name for p in stale.iterdir()) == [name], "a stale ebuild is replaced whole")
    failing = scratch / "failing"
    real_replace = module.os.replace

    def no_replace(*_args, **_kwargs):
        raise OSError("fixture: the move into place fails")

    module.os.replace = no_replace
    try:
        module.make_ebuild(version, failing)
        check(False, "a failed move into place is reported")
    except OSError:
        check(sorted(p.name for p in failing.iterdir()) == [], "a failed move into place leaves no ebuild and no temporary file")
    finally:
        module.os.replace = real_replace


def main() -> int:
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gentoo-package-test-", dir=ROOT / "build") as tmp:
        scratch = Path(tmp)
        helper = public_layout(scratch)
        ebuilds = renders(helper, scratch)
        port_rules((OVERLAY / TEMPLATE).read_text())
        release_flags(ebuilds, scratch)
        if ebuilds:
            refusals(helper, scratch)
    print(f"{len(failures)} failure(s)" if failures else "all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
