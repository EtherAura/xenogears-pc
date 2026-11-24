#!/usr/bin/env python3
"""Build the Debian package of a release from a configured CMake build, then its companions.

    python packaging/deb_build.py --build-dir <cmake build> --tag v<YYYY>.<MM>.<N> --out-dir <dir>
    python packaging/deb_build.py ... --shlibdeps-private-dir <dir>   # repeatable, see below
    python packaging/deb_build.py ... --audit-source <source>   # Ubuntu release builder

The build folder must already be configured as the release ships it:

  cmake -S port -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US \\
      -DXG_EXTRAS=ON -DXG_PACKAGE_VERSION=<the tag without its v>

Its CMakeCache.txt must say Release, US, XG_EXTRAS ON, XG_STATE_SCHEMA ON and the tag's version,
and its CPackConfig.cmake the DEB generator, the package xenogears-pc of that
version, amd64, the file name xenogears-pc_<version>_amd64.deb, the /usr
prefix, Depends from dpkg-shlibdeps and libvulkan1. Anything else is refused
before any command runs, as is an --out-dir that already holds one of the
three names below. The tag is checked by packaging/deb_assets.py's rule
(vYYYY.MM.N, N from 0).

The steps, each named in the failure message when it fails:

  tools      cmake, cpack, dpkg-deb and dpkg on PATH
  build      cmake --build <build> --config Release
  cpack      cpack -G DEB -C Release --config <build>/CPackConfig.cmake, its
             output in a temporary folder (never the build folder, so a
             package an earlier run left there is never taken)
  package    that folder's xenogears-pc_<version>_amd64.deb, by its exact
             name; dpkg-deb reads its Package, Version and Architecture, and
             its files must be exactly the executable (/usr/bin/xenogears, or
             xenogears-enhanced for XG_ENGINE=enhanced; mode 0755, owned by
             root), the licences in /usr/share/doc/xenogears-pc (COPYING,
             LICENSE-Jukebox-font.txt, LICENSE-imgui.txt, and LICENSE-glslang.txt
             and LICENSE-dav1d.txt when the cache links them),
             /usr/share/xenogears-pc/release.txt, exactly "version=<version>"
             and "kind=deb" (what the in-app updater reads), and the
             executable's state schema, /usr/share/xenogears-pc/xenogears.xgschema
             (xenogears-enhanced.xgschema), in its packed form ("XGSCHEMA"),
             nothing else
  payload    with --audit-source, check the executable bytes taken from the .deb
             against the extracted retail modules with tools/payloadcheck.py
             --dist, and its state schema against them with
             tools/state_schema.py --verify --stripped (read from that image,
             no path of the build machine, the executable stripped), before
             making any release assets
  companions python -I packaging/deb_assets.py --deb <it> --tag <tag>
             --build-dir <build> --require-deps, into the temporary folder: it
             must write exactly <deb>.sha256 (the package's hash) and
             <deb>.cdx.json
  stage      the three files moved into --out-dir (created if missing), the
             .deb last

Exit status 0 only when every step passed; then --out-dir holds the .deb, its
.sha256 and its .cdx.json beside whatever it held before. On a failure the
exit status is 1, the step is named, and --out-dir is as it was (a staging
step that fails takes back what it moved). The temporary folder is the only
thing removed; the build folder and --out-dir are never deleted.

--shlibdeps-private-dir passes CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS to
cpack, for a host whose dpkg-shlibdeps cannot place a library (a non-Debian
host's GCC runtime). It changes nothing that is checked.

Limits: Depends is what dpkg-shlibdeps on this host put there. --require-deps
checks that it declares libsdl3-0 and libc6 with minimum versions and
libvulkan1 (so a package built where dpkg knows none of the libraries, with
Depends libvulkan1 alone, is refused, never repaired); it does not prove the
constraints complete, the libraries ABI-compatible, or that the package
installs or starts anywhere. Only a run in the oldest target distribution
(an Ubuntu 26.04 container) gives the release's Depends: a run elsewhere
proves nothing about Ubuntu or Debian. Nothing is installed, launched,
downloaded or uploaded, no container is managed and no privilege is asked
for. Standard library only; needs deb_assets.py beside this file.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "deb_assets.py"
DOC = "usr/share/doc/xenogears-pc"
LICENCES = ("COPYING", "LICENSE-Jukebox-font.txt", "LICENSE-imgui.txt")
RELEASE = "usr/share/xenogears-pc/release.txt"  # port/cmake/deb.cmake: what the in-app updater reads
SCHEMA_DIR = "usr/share/xenogears-pc"  # port/cmake/deb.cmake: the executable's state schema, <name>.xgschema


def release_text(version: str) -> bytes:
    """The .deb's release.txt (port/src/system/release.h)."""
    return f"version={version}\nkind=deb\n".encode()
CACHE_WANT = {"CMAKE_BUILD_TYPE": "Release", "XG_VERSION": "US", "XG_EXTRAS": "ON", "XG_STATE_SCHEMA": "ON"}
TOOLS = ("cmake", "cpack", "dpkg-deb", "dpkg")


class StepError(Exception):
    def __init__(self, step: str, message: str):
        super().__init__(message)
        self.step = step


def load_assets():
    """packaging/deb_assets.py, for its tag rule and names (loaded by path: python -I adds no folder)."""
    if not ASSETS.is_file():
        raise StepError("inputs", f"{ASSETS.name} is not beside {Path(__file__).name}")
    spec = importlib.util.spec_from_file_location("deb_assets", ASSETS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cmake_sets(path: Path) -> dict[str, str]:
    """NAME:TYPE=value lines of CMakeCache.txt, or set(NAME "value") lines of CPackConfig.cmake."""
    text = path.read_text(errors="replace")
    if path.name == "CMakeCache.txt":
        return {m[1]: m[2] for m in re.finditer(r"^([\w-]+):\w+=(.*)$", text, re.M)}
    return {m[1]: m[2] for m in re.finditer(r'^set\((\w+)\s+"(.*)"\)$', text, re.M)}


def check_build(build: Path, version: str, package: str, arch: str) -> tuple[dict[str, str], str]:
    """The release contract of a configured build: its cache and CPack settings (the file name back)."""
    for name in ("CMakeCache.txt", "CPackConfig.cmake"):
        if not (build / name).is_file():
            raise StepError("inputs", f"--build-dir {build}: no {name} (configure it first)")
    cache = cmake_sets(build / "CMakeCache.txt")
    want = dict(CACHE_WANT, XG_PACKAGE_VERSION=version)
    wrong = [f"{key} is {cache.get(key)!r}, not {value!r}" for key, value in want.items() if cache.get(key) != value]
    deb = f"{package}_{version}_{arch}.deb"
    cpack = cmake_sets(build / "CPackConfig.cmake")
    for key, value in (("CPACK_GENERATOR", "DEB"), ("CPACK_PACKAGE_NAME", package),
                       ("CPACK_DEBIAN_PACKAGE_VERSION", version), ("CPACK_DEBIAN_PACKAGE_ARCHITECTURE", arch),
                       ("CPACK_DEBIAN_FILE_NAME", deb), ("CPACK_PACKAGING_INSTALL_PREFIX", "/usr"),
                       ("CPACK_DEBIAN_PACKAGE_SHLIBDEPS", "ON")):
        if cpack.get(key) != value:
            wrong.append(f"{key} is {cpack.get(key)!r}, not {value!r}")
    if "libvulkan1" not in re.split(r"[,\s]+", cpack.get("CPACK_DEBIAN_PACKAGE_DEPENDS", "")):
        wrong.append(f"CPACK_DEBIAN_PACKAGE_DEPENDS {cpack.get('CPACK_DEBIAN_PACKAGE_DEPENDS')!r} lacks libvulkan1")
    if wrong:
        raise StepError("inputs", f"--build-dir {build} is not the release's configuration: " + "; ".join(wrong))
    return cache, deb


def schema_of(exe: str) -> str:
    """The package's path of an executable's state schema (port/cmake/deb.cmake)."""
    return f"{SCHEMA_DIR}/{Path(exe).name}.xgschema"


def expected_files(cache: dict[str, str]) -> tuple[str, set[str]]:
    """The executable's path and every file the package must hold (port/cmake/deb.cmake's install rules)."""
    exe = "usr/bin/xenogears-enhanced" if cache.get("XG_ENGINE") == "enhanced" else "usr/bin/xenogears"
    need = {exe, RELEASE, schema_of(exe)} | {f"{DOC}/{name}" for name in LICENCES}
    if not cache.get("glslang_DIR", "NOTFOUND").endswith("NOTFOUND") and cache.get("XG_GLSLANG", "ON") == "ON":
        need.add(f"{DOC}/LICENSE-glslang.txt")
    if not cache.get("XG_DAV1D_LIBRARY", "NOTFOUND").endswith("NOTFOUND") and cache.get("XG_DAV1D", "ON") == "ON":
        need.add(f"{DOC}/LICENSE-dav1d.txt")
    return exe, need


def run(step: str, argv: list, cwd: Path | None = None, name: str | None = None) -> None:
    name = name or Path(str(argv[0])).name
    print("$", " ".join(str(a) for a in argv), flush=True)
    try:
        result = subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True)
    except OSError as error:
        raise StepError(step, f"{name} could not run ({error})") from error
    if result.returncode != 0:
        tail = (result.stdout[-3000:] + result.stderr[-3000:]).strip()
        raise StepError(step, f"{name} exit status {result.returncode}" + (f":\n{tail}" if tail else ""))


def dpkg_deb(dpkg_deb_tool: str, *args) -> bytes:
    result = subprocess.run([dpkg_deb_tool, *[str(a) for a in args]], capture_output=True)
    if result.returncode != 0:
        raise StepError("package", f"dpkg-deb {args[0]} failed: " + " ".join(result.stderr.decode(errors="replace").split()))
    return result.stdout


def check_package(tool: str, deb: Path, package: str, version: str, arch: str, exe: str,
                  need: set[str]) -> tuple[bytes, bytes]:
    fields = {}
    for line in dpkg_deb(tool, "-f", deb).decode(errors="replace").splitlines():
        if ":" in line and line[:1] not in (" ", "\t"):
            name, value = line.split(":", 1)
            fields[name] = value.strip()
    for field, want in (("Package", package), ("Version", version), ("Architecture", arch)):
        if fields.get(field) != want:
            raise StepError("package", f"{deb.name}: {field} is {fields.get(field)!r}, not {want!r}")
    members = {}
    payload = release = schema = b""
    try:
        with tarfile.open(fileobj=io.BytesIO(dpkg_deb(tool, "--fsys-tarfile", deb)), mode="r:") as tar:
            for member in tar:
                if not member.isdir():
                    path = member.name.removeprefix("./")
                    if path in members:
                        raise StepError("package", f"{deb.name} repeats the archive member {path}")
                    members[path] = member
                    if path == exe and member.isfile():
                        payload = tar.extractfile(member).read()
                    if path == RELEASE and member.isfile():
                        release = tar.extractfile(member).read()
                    if path == schema_of(exe) and member.isfile():
                        schema = tar.extractfile(member).read()
    except tarfile.TarError as error:
        raise StepError("package", f"{deb.name}: its file archive cannot be read ({error})") from error
    problems = [f"missing {path}" for path in sorted(need - set(members))]
    problems += [f"unexpected {path}" for path in sorted(set(members) - need)]
    problems += [f"{path} is not a regular file" for path in sorted(need & set(members)) if not members[path].isfile()]
    binary = members.get(exe)
    if binary is not None and binary.isfile() and (binary.mode & 0o7777 != 0o755 or binary.uid != 0 or binary.gid != 0):
        problems.append(f"{exe} is mode {binary.mode & 0o7777:04o} owner {binary.uid}:{binary.gid}, not 0755 root")
    if RELEASE in members and members[RELEASE].isfile() and release != release_text(version):
        problems.append(f"{RELEASE} is {release[:80]!r}, not {release_text(version)!r}")
    if schema_of(exe) in members and members[schema_of(exe)].isfile() and schema[:8] != b"XGSCHEMA":
        problems.append(f"{schema_of(exe)} is not a state schema in its packed form (XGSCHEMA)")
    if problems:
        raise StepError("package", f"{deb.name} does not hold the executable, licences, release file and state "
                        "schema alone: " + "; ".join(problems))
    return payload, schema


def audit_executable(source: Path, work: Path, build: Path, exe: str, payload: bytes) -> None:
    auditor = source / "tools" / "payloadcheck.py"
    if not auditor.is_file():
        raise StepError("payload", "the source tree lacks tools/payloadcheck.py")
    if not payload.startswith(b"\x7fELF\x02\x01"):
        raise StepError("payload", "the packaged executable must be a little-endian ELF64 image")
    candidate = work / Path(exe).name
    candidate.write_bytes(payload)
    candidate.chmod(0o500)
    try:
        checked = subprocess.run([sys.executable, "-E", "-s", "-B", str(auditor), "--dist", "--ver", "us",
                                  "--build", str(build), "--exe", str(candidate)],
                                 capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=3600)
    except OSError as error:
        raise StepError("payload", "could not start tools/payloadcheck.py") from error
    except subprocess.TimeoutExpired as error:
        raise StepError("payload", "payloadcheck --dist timed out") from error
    if candidate.read_bytes() != payload:
        raise StepError("payload", "the extracted executable changed during its audit")
    lines = checked.stdout.splitlines()
    heading = f"retail module data in {candidate} (US): "
    if checked.returncode or len(lines) != 2 or not lines[0].startswith(heading) or not lines[1].startswith("payload ok:"):
        summary = next((line for line in reversed(lines) if line.startswith("payload FAIL:")), "")
        detail = f" ({summary})" if summary else ""
        raise StepError("payload", f"packaged {Path(exe).name} did not pass payloadcheck --dist (exit "
                                  f"{checked.returncode}){detail}")
    print(f"{Path(exe).name} in the .deb: {lines[1]}", flush=True)


def check_schema(source: Path, work: Path, exe: str, payload: bytes, schema: bytes) -> None:
    """The packaged state schema against the packaged executable: tools/state_schema.py --verify --stripped."""
    tool = source / "tools" / "state_schema.py"
    if not tool.is_file():
        raise StepError("payload", "the source tree lacks tools/state_schema.py")
    folder = work / "schema"
    folder.mkdir()
    image, packed = folder / Path(exe).name, folder / Path(schema_of(exe)).name
    image.write_bytes(payload)
    packed.write_bytes(schema)
    try:
        checked = subprocess.run([sys.executable, "-E", "-s", "-B", str(tool), "--verify", str(packed), "--image",
                                  str(image), "--stripped"], capture_output=True, text=True, stdin=subprocess.DEVNULL,
                                 timeout=600)
    except OSError as error:
        raise StepError("payload", "could not start tools/state_schema.py") from error
    except subprocess.TimeoutExpired as error:
        raise StepError("payload", "state_schema.py --verify timed out") from error
    said = (checked.stdout + checked.stderr).replace(str(folder) + "/", "").strip()
    if checked.returncode or image.read_bytes() != payload or packed.read_bytes() != schema:
        raise StepError("payload", f"the packaged state schema did not pass state_schema.py --verify --stripped "
                                   f"(exit {checked.returncode}): {said[-600:]}")
    print(f"{packed.name} in the .deb: {said.splitlines()[-1] if said else 'verified'}", flush=True)


def companions(work: Path, deb: Path, tag: str, build: Path) -> list[Path]:
    out = work / "assets"
    run("companions", [sys.executable, "-I", ASSETS, "--deb", deb, "--tag", tag, "--build-dir", build,
                       "--out-dir", out, "--require-deps"], name=ASSETS.name)
    names = sorted(p.name for p in out.iterdir()) if out.is_dir() else []
    sidecar, bom = out / f"{deb.name}.sha256", out / f"{deb.name}.cdx.json"
    if names != sorted([sidecar.name, bom.name]):
        raise StepError("companions", f"deb_assets.py wrote {names}, not {[sidecar.name, bom.name]}")
    digest = hashlib.sha256(deb.read_bytes()).hexdigest()
    if sidecar.read_text() != f"{digest}  {deb.name}\n":
        raise StepError("companions", f"{sidecar.name} does not hold {deb.name}'s SHA-256")
    return [sidecar, bom]


def stage(files: list[Path], out_dir: Path) -> None:
    """Move the set into out_dir, the .deb last; on any error take back what was moved."""
    created = not out_dir.exists()
    moved: list[Path] = []
    try:
        out_dir.mkdir(parents=True, exist_ok=True)
        holding = Path(tempfile.mkdtemp(prefix=".deb_build-", dir=out_dir))
        try:
            for path in files:  # same file system as out_dir, so the final moves are renames
                shutil.copy2(path, holding / path.name)
            for path in files:
                target = out_dir / path.name
                if target.exists():
                    raise StepError("stage", f"{target} appeared during the run")
                os.replace(holding / path.name, target)
                moved.append(target)
        finally:
            shutil.rmtree(holding, ignore_errors=True)
    except (OSError, StepError) as error:
        for target in moved:
            target.unlink(missing_ok=True)
        if created and out_dir.is_dir() and not any(out_dir.iterdir()):
            out_dir.rmdir()
        raise error if isinstance(error, StepError) else StepError("stage", f"{out_dir}: {error}") from error


def build_package(build: Path, tag: str, out_dir: Path, private_dirs: list[Path],
                  audit_source: Path | None = None) -> list[Path]:
    assets = load_assets()
    try:
        version = assets.version_from_tag(tag)
    except assets.AssetError as error:
        raise StepError("inputs", str(error)) from error
    if not build.is_dir():
        raise StepError("inputs", f"--build-dir {build}: no such folder")
    cache, name = check_build(build, version, assets.PACKAGE, assets.ARCH)
    names = [name, f"{name}.sha256", f"{name}.cdx.json"]
    if out_dir.exists() and not out_dir.is_dir():
        raise StepError("inputs", f"--out-dir {out_dir} is not a folder")
    taken = [n for n in names if (out_dir / n).exists() or (out_dir / n).is_symlink()]
    if taken:
        raise StepError("inputs", f"--out-dir {out_dir} already holds {', '.join(taken)} (remove it or choose another)")
    tools = {tool: shutil.which(tool) for tool in TOOLS}
    if missing := [tool for tool, path in tools.items() if path is None]:
        raise StepError("tools", f"not on PATH: {', '.join(missing)}")
    exe, need = expected_files(cache)

    with tempfile.TemporaryDirectory(prefix="deb_build-") as tmp:
        work = Path(tmp)
        run("build", [tools["cmake"], "--build", build, "--config", "Release"])
        cpack_argv = [tools["cpack"], "-G", "DEB", "-C", "Release", "--config", build / "CPackConfig.cmake",
                      "-B", work / "cpack"]
        if private_dirs:
            cpack_argv += ["-D", "CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS=" + ";".join(str(d) for d in private_dirs)]
        run("cpack", cpack_argv, cwd=build)
        deb = work / "cpack" / name
        if not deb.is_file() or deb.is_symlink():
            made = sorted(p.name for p in (work / "cpack").glob("*.deb")) if (work / "cpack").is_dir() else []
            raise StepError("cpack", f"cpack did not make {name} (it made {made or 'no package'})")
        payload, schema = check_package(tools["dpkg-deb"], deb, assets.PACKAGE, version, assets.ARCH, exe, need)
        if audit_source is not None:
            audit_executable(audit_source, work, build, exe, payload)
            check_schema(audit_source, work, exe, payload, schema)
        files = companions(work, deb, tag, build) + [deb]
        stage(files, out_dir)
    return [out_dir / path.name for path in files]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", type=Path, required=True, help="the configured Release build (CMakeCache.txt)")
    ap.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    ap.add_argument("--out-dir", type=Path, required=True, help="where the .deb and its companions go")
    ap.add_argument("--audit-source", type=Path,
                    help="audit the packaged executable using this source tree's auditor and retail modules")
    ap.add_argument("--shlibdeps-private-dir", type=Path, action="append", default=[],
                    help="a folder dpkg-shlibdeps may search for the executable's libraries (repeatable)")
    args = ap.parse_args()
    try:
        staged = build_package(args.build_dir.resolve(), args.tag, args.out_dir.resolve(),
                               args.shlibdeps_private_dir, args.audit_source.resolve() if args.audit_source else None)
    except StepError as error:
        print(f"deb_build: FAILED at step {error.step}: {error}", file=sys.stderr)
        print("deb_build: nothing staged; this is not a release package set", file=sys.stderr)
        return 1
    for path in staged:
        print(path)
    print("deb_build: staged the package and its two companions (Depends as this host's dpkg-shlibdeps gave it; "
          "nothing installed or started)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
