#!/usr/bin/env python3
"""The release companions of the Debian package: its .sha256 and its CycloneDX SBOM.

    python packaging/deb_assets.py --deb dist/xenogears-pc_<version>_amd64.deb --tag v<YYYY>.<MM>.<N>
    python packaging/deb_assets.py --deb <deb> --tag <tag> --build-dir <cmake build> --out-dir <dir>

The version is the release tag without its v (CalVer vYYYY.MM.N, N from 0);
the package's own control fields (read with dpkg-deb) must give that version,
the package name xenogears-pc and the architecture amd64, and its file name
must be xenogears-pc_<version>_amd64.deb. It then writes, beside the package
(or in --out-dir):

  <file>.deb.sha256      sha256sum format, for those exact bytes
  <file>.deb.cdx.json    CycloneDX 1.6: the package (its hash and control
                         fields), every file it installs with its hash, each
                         Depends entry as a constraint, and with --build-dir
                         the build's type, options and compilers (CMake's
                         records)

What the SBOM does not know, it says (compositions: incomplete): Depends
entries are constraints from dpkg-shlibdeps on the build host, not installed
versions; the libraries linked into the executable are not inventoried; no
licence is named beyond the licence files the package holds. It has no
timestamp, so the same package and build give the same bytes.

The package is only read: nothing is installed, launched or uploaded. Needs
dpkg-deb; exit status 1 (and nothing written) on any bad input or missing tool.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import tarfile
import uuid
from pathlib import Path

PACKAGE = "xenogears-pc"
ARCH = "amd64"
TAG = re.compile(r"v(20\d\d)\.(0[1-9]|1[0-2])\.(0|[1-9]\d*)")
DOC_PREFIX = f"usr/share/doc/{PACKAGE}/"
BUILD_KEYS = ("CMAKE_BUILD_TYPE", "XG_VERSION", "XG_ENGINE", "XG_EXTRAS", "XG_PACKAGE_VERSION")


class AssetError(Exception):
    pass


def version_from_tag(tag: str) -> str:
    """The package version of a release tag: vYYYY.MM.N without its v."""
    if not TAG.fullmatch(tag):
        raise AssetError(f"release tag {tag!r} is not vYYYY.MM.N (month 01-12, N a whole number from 0)")
    return tag[1:]


def dpkg_deb(*args: str) -> bytes:
    tool = shutil.which("dpkg-deb")
    if tool is None:
        raise AssetError("dpkg-deb not found: the package's control fields and files cannot be read")
    result = subprocess.run([tool, *args], capture_output=True)
    if result.returncode != 0:
        errors = "; ".join(line.strip() for line in result.stderr.decode(errors="replace").splitlines() if line.strip())
        raise AssetError(f"dpkg-deb {' '.join(args[:-1])} failed on {args[-1]}: {errors}")
    return result.stdout


def control_fields(deb: Path) -> dict[str, str]:
    fields: dict[str, str] = {}
    name = None
    for line in dpkg_deb("-f", str(deb)).decode().splitlines():
        if line[:1] in (" ", "\t") and name:
            fields[name] += "\n" + line.strip()
        elif ":" in line:
            name, value = line.split(":", 1)
            fields[name] = value.strip()
    return fields


def package_files(deb: Path) -> list[dict]:
    """Each regular file the package installs: path, mode and SHA-256."""
    stream = io.BytesIO(dpkg_deb("--fsys-tarfile", str(deb)))
    files = []
    try:
        with tarfile.open(fileobj=stream, mode="r:") as tar:
            for member in tar:
                if member.isfile():
                    data = tar.extractfile(member).read()
                    files.append({"path": member.name.removeprefix("./"), "mode": f"{member.mode:04o}",
                                  "sha256": hashlib.sha256(data).hexdigest()})
    except tarfile.TarError as error:
        raise AssetError(f"{deb.name}: its file archive cannot be read ({error})") from error
    if not files:
        raise AssetError(f"{deb.name}: installs no files")
    return sorted(files, key=lambda f: f["path"])


def depends_entries(depends: str) -> list[dict]:
    """Depends as written: each alternative's name and its constraint (never a resolved version)."""
    entries = []
    for group, text in enumerate(part.strip() for part in depends.split(",") if part.strip()):
        for alternative in text.split("|"):
            m = re.fullmatch(r"\s*([a-z0-9][a-z0-9+.-]+)(?::\S+)?\s*(?:\((.+)\))?\s*", alternative)
            if not m:
                raise AssetError(f"Depends entry {alternative.strip()!r} is not a Debian dependency")
            entries.append({"name": m[1], "constraint": (m[2] or "").strip(), "group": group, "text": text})
    return entries


def build_metadata(build: Path, version: str) -> list[tuple[str, str]]:
    """The build's CMake records: build type, the port's options, the compilers."""
    cache_file = build / "CMakeCache.txt"
    if not cache_file.is_file():
        raise AssetError(f"--build-dir {build}: no CMakeCache.txt")
    cache = dict(re.findall(r"^([\w-]+):\w+=(.*)$", cache_file.read_text(), re.M))
    if cache.get("XG_PACKAGE_VERSION") != version:
        raise AssetError(f"--build-dir {build}: XG_PACKAGE_VERSION is {cache.get('XG_PACKAGE_VERSION')!r}, "
                         f"not the tag's {version!r}")
    props = [(f"xenogears:build:{key}", cache[key]) for key in BUILD_KEYS if key in cache]
    for lang, name in (("C", "CMakeCCompiler.cmake"), ("CXX", "CMakeCXXCompiler.cmake")):
        for record in sorted(build.glob(f"CMakeFiles/*/{name}"))[-1:]:
            sets = dict(re.findall(r'^set\((\w+) "(.*)"\)$', record.read_text(), re.M))
            for field in ("COMPILER_ID", "COMPILER_VERSION"):
                if sets.get(f"CMAKE_{lang}_{field}"):
                    props.append((f"xenogears:build:CMAKE_{lang}_{field}", sets[f"CMAKE_{lang}_{field}"]))
    return props


def sbom(deb: Path, digest: str, control: dict[str, str], files: list[dict],
         build: list[tuple[str, str]] | None) -> dict:
    version = control["Version"]
    root = f"{PACKAGE}@{version}"
    dpkg_version = dpkg_deb("--version").decode().splitlines()[0].strip()
    package_props = [(f"debian:control:{name}", control[name])
                     for name in ("Architecture", "Section", "Priority", "Installed-Size", "Depends")
                     if name in control]
    component = {
        "type": "application", "bom-ref": root, "name": PACKAGE, "version": version,
        "description": control.get("Description", "").split("\n")[0],
        "hashes": [{"alg": "SHA-256", "content": digest}],
        "properties": [{"name": n, "value": v} for n, v in package_props + [("xenogears:file-name", deb.name)]],
    }
    if control.get("Maintainer"):
        component["supplier"] = {"name": control["Maintainer"]}
    if control.get("Homepage"):
        component["externalReferences"] = [{"type": "website", "url": control["Homepage"]}]

    components = []
    for f in files:
        role = "licence text" if f["path"].startswith(DOC_PREFIX) else "installed file"
        components.append({
            "type": "file", "bom-ref": f"file:{f['path']}", "name": f["path"],
            "hashes": [{"alg": "SHA-256", "content": f["sha256"]}],
            "properties": [{"name": "xenogears:role", "value": role}, {"name": "unix:mode", "value": f["mode"]}],
        })
    deps = depends_entries(control.get("Depends", ""))
    for dep in deps:
        components.append({
            "type": "library", "bom-ref": f"depends:{dep['group']}:{dep['name']}", "name": dep["name"],
            "scope": "required",
            "description": "a Debian Depends entry: a constraint the installing system resolves; "
                           "the version installed is not known here",
            "properties": [{"name": "debian:depends-constraint", "value": dep["constraint"] or "(any version)"},
                           {"name": "debian:depends-entry", "value": dep["text"]}],
        })

    coverage = [
        ("xenogears:sbom:covers", "the package file, its control fields, and every file it installs with its SHA-256"),
        ("xenogears:sbom:depends", "Depends as the package declares it (dpkg-shlibdeps on the build host, plus "
                                   "libvulkan1); constraints only, not installed or resolved versions"),
        ("xenogears:sbom:unknown", "the libraries linked into the executable and their versions; the licences "
                                   "of the port and its parts beyond the licence files listed; build provenance"),
    ]
    if build is None:
        coverage.append(("xenogears:build", "not supplied (no --build-dir)"))
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": uuid.uuid5(uuid.NAMESPACE_URL, f"{deb.name}#sha256={digest}").urn,
        "version": 1,
        "metadata": {
            "tools": {"components": [
                {"type": "application", "name": "packaging/deb_assets.py"},
                {"type": "application", "name": "dpkg-deb", "description": dpkg_version}]},
            "component": component,
            "properties": [{"name": n, "value": v} for n, v in coverage + (build or [])],
        },
        "components": components,
        "dependencies": [{"ref": root, "dependsOn": [c["bom-ref"] for c in components if c["type"] == "library"]}],
        "compositions": [{"aggregate": "incomplete", "assemblies": [root]}],
    }


def make_assets(deb: Path, tag: str, out_dir: Path | None = None, build_dir: Path | None = None) -> tuple[Path, Path]:
    version = version_from_tag(tag)
    expected = f"{PACKAGE}_{version}_{ARCH}.deb"
    if not deb.is_file():
        raise AssetError(f"{deb}: no such package")
    if deb.name != expected:
        raise AssetError(f"{deb.name}: the release's package is named {expected}")
    data = deb.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    control = control_fields(deb)
    for field, want in (("Package", PACKAGE), ("Version", version), ("Architecture", ARCH)):
        if control.get(field) != want:
            raise AssetError(f"{deb.name}: {field} is {control.get(field)!r}, not {want!r}")
    files = package_files(deb)
    build = build_metadata(build_dir, version) if build_dir is not None else None
    bom = sbom(deb, digest, control, files, build)
    if hashlib.sha256(deb.read_bytes()).hexdigest() != digest:
        raise AssetError(f"{deb.name} changed while it was read")

    out = out_dir or deb.parent
    out.mkdir(parents=True, exist_ok=True)
    sidecar, bom_path = out / f"{deb.name}.sha256", out / f"{deb.name}.cdx.json"
    sidecar.write_text(f"{digest}  {deb.name}\n")
    bom_path.write_text(json.dumps(bom, indent=2) + "\n")
    return sidecar, bom_path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--deb", type=Path, required=True, help="the package CPack made")
    ap.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    ap.add_argument("--out-dir", type=Path, help="where the companions go (default: beside the package)")
    ap.add_argument("--build-dir", type=Path, help="the CMake build the package came from (its records go in the SBOM)")
    args = ap.parse_args()
    try:
        for path in make_assets(args.deb, args.tag, args.out_dir, args.build_dir):
            print(path)
    except AssetError as error:
        print(f"deb_assets: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
