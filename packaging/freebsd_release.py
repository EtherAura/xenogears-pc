#!/usr/bin/env python3
"""Package audited US and Japanese FreeBSD builds for a release tag."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

import linux_release


NAME = "xenogears-pc"
PREFIX = Path("/usr/local")
BUILDS = {"xenogears": "original", "xenogears-enhanced": "enhanced"}
LICENCES = {
    "COPYING": "port/COPYING",
    "LICENSE-imgui.txt": "port/third_party/imgui/LICENSE.txt",
    "LICENSE-Jukebox-font.txt": "port/third_party/jukebox-font/LICENSE.txt",
    "LICENSE-dav1d.txt": "port/third_party/dav1d/COPYING",
    "LICENSE-glslang.txt": "port/third_party/glslang/LICENSE.txt",
}
LAUNCHER = """#!/bin/sh
region=us
case "${1-}" in
    --us|--jp) region=${1#--}; shift ;;
esac
exec /usr/local/libexec/xenogears-pc/"$region"/xenogears "$@"
"""


def output(*command: str) -> str:
    return subprocess.check_output(command, text=True, stdin=subprocess.DEVNULL).strip()


def runtime_packages(executables: list[Path]) -> dict[str, dict[str, str]]:
    # SDL loads the Vulkan loader at runtime, so it does not appear in NEEDED.
    packages = {"vulkan-loader"}
    for executable in executables:
        needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", output("readelf", "-d", str(executable)))
        paths = dict(line.split("\t", 1) for line in
                     output("ldd", "-f", r"%o\t%p\n", str(executable)).splitlines())
        for name in needed:
            path = Path(paths.get(name, ""))
            if not path.is_absolute() or not path.is_file():
                raise linux_release.ReleaseError(f"Unresolved library for {executable.name}: {name}")
            if not (path.is_relative_to("/lib") or path.is_relative_to("/usr/lib")):
                packages.add(output("pkg", "which", "-q", str(path)))
    result = {}
    for package in sorted(packages):
        name, origin, version = output("pkg", "query", r"%n\t%o\t%v", package).split("\t")
        result[name] = {"origin": origin, "version": version}
    return result


def staged_files(build_root: Path, stage: Path, region: str) -> dict[str, dict]:
    builds = {}
    (stage / "bin").mkdir(parents=True)
    licences = stage / linux_release.DOC
    licences.mkdir(parents=True)
    for name, relative in LICENCES.items():
        shutil.copyfile(linux_release.CHECKOUT / relative, licences / name)
    for name, engine in BUILDS.items():
        relative = f"bin/{name}"
        build = linux_release.audit_build(build_root / f"{region}-{engine}", region, relative)
        builds[relative] = build
        executable = stage / relative
        shutil.copyfile(build / name, executable)
        subprocess.run(["strip", "-s", str(executable)], check=True, stdin=subprocess.DEVNULL)
        executable.chmod(0o755)
        if "FreeBSD" not in output("file", "-b", str(executable)):
            raise linux_release.ReleaseError(f"{name} is not a native FreeBSD executable")
    files = linux_release.read_stage(stage)
    for verdict in linux_release.audit(files, builds, region):
        print(verdict)
    linux_release.unchanged(stage, files)
    return {file["rel"]: file for file in files}


def package(tag: str, build_root: Path, out: Path) -> Path:
    version = linux_release.release_names(tag)["version"]
    abi = output("pkg", "config", "ABI")
    if abi.lower() != "freebsd:15:amd64":
        raise linux_release.ReleaseError(f"Expected FreeBSD 15 amd64, got {abi}")
    out.mkdir(parents=True, exist_ok=True)
    artifact = out / f"{NAME}-{version}-freebsd15-amd64.pkg"
    checksum = artifact.with_suffix(".pkg.sha256")
    if os.path.lexists(artifact) or os.path.lexists(checksum):
        raise linux_release.ReleaseError("Package or checksum already exists")

    with TemporaryDirectory(prefix=".freebsd-release-", dir=out) as temporary:
        scratch = Path(temporary)
        root = scratch / "root"
        prefix = root / PREFIX.relative_to("/")
        executables = []
        for region in ("us", "jp"):
            files = staged_files(build_root, scratch / region, region)
            for relative, file in files.items():
                if relative in linux_release.EXECUTABLES:
                    destination = prefix / "libexec" / NAME / region / Path(relative).name
                    executables.append(destination)
                else:
                    destination = prefix / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists() and destination.read_bytes() != file["data"]:
                    raise linux_release.ReleaseError(f"The regional copies of {relative} differ")
                destination.write_bytes(file["data"])
                destination.chmod(file["mode"])

        launcher = prefix / "bin" / NAME
        launcher.parent.mkdir(parents=True)
        launcher.write_text(LAUNCHER)
        launcher.chmod(0o755)
        metadata = scratch / "metadata"
        metadata.mkdir()
        manifest = {
            "name": NAME,
            "version": version,
            "origin": f"games/{NAME}",
            "comment": "Native Xenogears PC port for owner-provided discs",
            "desc": "US and Japanese Original and Enhanced engines. Bring your own disc images.",
            "maintainer": "EtherAura",
            "www": "https://github.com/EtherAura/xenogears-pc",
            "prefix": str(PREFIX),
            "abi": abi,
            "licenses": ["GPLv3+"],
            "licenselogic": "single",
            "deps": runtime_packages(executables),
        }
        (metadata / "+MANIFEST").write_text(json.dumps(manifest, indent=2) + "\n")
        plist = metadata / "plist"
        plist.write_text("".join(f"{path.relative_to(prefix).as_posix()}\n"
                                 for path in sorted(prefix.rglob("*")) if path.is_file()))
        subprocess.run(["pkg", "create", "-r", str(root), "-m", str(metadata),
                        "-p", str(plist), "-o", temporary], check=True, stdin=subprocess.DEVNULL)
        staged = scratch / f"{NAME}-{version}.pkg"
        digest = hashlib.sha256(staged.read_bytes()).hexdigest()
        pending_sum = scratch / checksum.name
        pending_sum.write_text(f"{digest}  {artifact.name}\n")
        published = []
        try:
            linux_release.publish(staged, artifact, published)
            linux_release.publish(pending_sum, checksum, published)
        except (OSError, linux_release.ReleaseError):
            for file in published:
                file.unlink()
            raise
    return artifact


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--build-root", type=Path, required=True,
                        help="directory containing the us/jp original/enhanced CMake builds")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not sys.platform.startswith("freebsd"):
        parser.error("run on the FreeBSD build host")
    try:
        print(package(args.tag, args.build_root.resolve(), args.out.resolve()))
    except (OSError, ValueError, subprocess.CalledProcessError, linux_release.ReleaseError) as error:
        print(f"freebsd_release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
