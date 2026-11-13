#!/usr/bin/env python3
"""Check the Debian package's release companions (packaging/deb_assets.py in xenogears-pc).

    python tools/check_deb_release_assets.py --ver us              # build the real .deb, then check
    python tools/check_deb_release_assets.py --ver us --reuse      # the .deb an earlier run built

1. A real package: tools/check_deb_package.py --ver <ver> --package-version
   <the test tag without v> --build build/pc/deb-assets-<ver> (configure,
   build, cpack, its own checks). Its exit status must be 0.
2. Public layout: the helper, alone in build/pc/deb-assets-<ver>/public/packaging/
   (as tools/publish_decomp.py lays tools/publish/repo-port/ over the tree),
   imports only the standard library, passes the publisher's date, time and
   private-text scan, and runs from there with python -I.
3. The companions for a zero-sequence release (vYYYY.MM.0): the .sha256 must
   hold the package's exact bytes' SHA-256 (sha256sum -c agrees); the SBOM must
   be CycloneDX 1.6 naming the package, its version, architecture and hash,
   each installed file with the hash dpkg-deb -x gives, each Depends entry with
   no resolved version, and say it is incomplete. The package is unchanged,
   and a second run writes the same bytes.
4. Refusals (nothing written): malformed tags, a tag of another version, a
   missing, truncated or non-package file, a package of another name or
   architecture (small labelled fixtures made with dpkg-deb --build), a build
   folder of another version, and no dpkg-deb on PATH.

Nothing is installed, launched or uploaded; all output stays in build/.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER = ROOT / "tools/publish/repo-port/packaging/deb_assets.py"
TAG = "v2026.11.0"  # a zero-sequence release
VERSION = TAG[1:]
DEB = f"xenogears-pc_{VERSION}_amd64.deb"
BAD_TAGS = ["2026.11.0", "v2026.11", "v2026.13.0", "v2026.00.0", "v2026.1.0", "v2026.11.01",
            "v26.11.0", "v2026.11.0-rc1", "v2026.11.-1", " v2026.11.0", "v2026.11.0\n", "V2026.11.0", ""]

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def run(argv: list, cwd: Path = ROOT, env: dict | None = None, show: bool = True) -> subprocess.CompletedProcess:
    print("$", " ".join(str(a) for a in argv), flush=True)
    result = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, text=True)
    if show and result.returncode != 0:
        print(result.stdout[-3000:], result.stderr[-3000:], sep="\n")
    return result


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def helper(public: Path, *args, env: dict | None = None) -> subprocess.CompletedProcess:
    """The staged helper, run as the public tree would: isolated, from its root."""
    return run([sys.executable, "-I", "packaging/deb_assets.py", *args], cwd=public, env=env, show=False)


def refused(public: Path, what: str, out: Path, *args, expect: str | tuple, env: dict | None = None) -> None:
    shutil.rmtree(out, ignore_errors=True)
    result = helper(public, *args, "--out-dir", out, env=env)
    message = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ""
    expect = (expect,) if isinstance(expect, str) else expect
    check(result.returncode == 1 and message.startswith("deb_assets: ") and any(e in message for e in expect)
          and not out.exists(),
          f"refused: {what} ({message or result.stdout.strip()!r})")


def fixture(work: Path, name: str, package: str, arch: str) -> Path:
    """A labelled test fixture (not a release package): one text file, the given control fields."""
    tree = work / f"fixture-{package}-{arch}"
    shutil.rmtree(tree, ignore_errors=True)
    (tree / "DEBIAN").mkdir(parents=True)
    (tree / "usr/share/doc" / package).mkdir(parents=True)
    (tree / "usr/share/doc" / package / "FIXTURE").write_text("test fixture for check_deb_release_assets.py\n")
    (tree / "DEBIAN/control").write_text(
        f"Package: {package}\nVersion: {VERSION}\nArchitecture: {arch}\nMaintainer: fixture\n"
        f"Description: test fixture, not a release package\n")
    target = work / f"fixture-{package}-{arch}-deb" / name
    target.parent.mkdir(exist_ok=True)
    target.unlink(missing_ok=True)
    run(["dpkg-deb", "--root-owner-group", "--build", tree, target])
    shutil.rmtree(tree, ignore_errors=True)
    return target


def stage_public(work: Path) -> Path:
    public = work / "public"
    shutil.rmtree(public, ignore_errors=True)
    (public / "packaging").mkdir(parents=True)
    shutil.copy2(HELPER, public / "packaging/deb_assets.py")
    staged = public / "packaging/deb_assets.py"

    imports = set()
    for node in ast.walk(ast.parse(staged.read_text())):
        if isinstance(node, ast.Import):
            imports |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            imports.add((node.module or "").split(".")[0])
    outside = sorted(imports - set(sys.stdlib_module_names) - {"__future__"})
    check(not outside, f"public helper imports only the standard library ({outside or sorted(imports)})")

    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    flagged = [line for line in staged.read_text().splitlines()
               if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line)
               or publish.FORBIDDEN.search(line)]
    check(not flagged, f"public helper passes the publisher's date/time/private scan ({flagged or 'clean'})")
    return public


def check_companions(deb: Path, sidecar: Path, bom_path: Path, work: Path) -> None:
    digest = sha256(deb)
    check(sidecar.read_text() == f"{digest}  {deb.name}\n", f"{sidecar.name}: {digest}  {deb.name}")
    if shutil.which("sha256sum"):
        check(run(["sha256sum", "-c", sidecar.name], cwd=sidecar.parent).returncode == 0,
              "sha256sum -c accepts the sidecar against the package bytes")
    else:
        print("LIMITATION  no sha256sum: the sidecar is checked by hashlib only")

    bom = json.loads(bom_path.read_text())
    meta = bom.get("metadata", {})
    root = meta.get("component", {})
    props = {p["name"]: p["value"] for p in root.get("properties", [])}
    check(bom.get("bomFormat") == "CycloneDX" and bom.get("specVersion") == "1.6"
          and bom.get("version") == 1 and str(bom.get("serialNumber", "")).startswith("urn:uuid:"),
          "SBOM: CycloneDX 1.6 with a serial number")
    check("timestamp" not in meta, "SBOM: no generated timestamp")
    check(root.get("type") == "application" and root.get("name") == "xenogears-pc"
          and root.get("version") == VERSION, f"SBOM component: xenogears-pc {root.get('version')}")
    check(root.get("hashes") == [{"alg": "SHA-256", "content": digest}], "SBOM component hash: the package bytes")
    control = {}
    for name in ("Architecture", "Depends", "Maintainer", "Homepage"):
        out = run(["dpkg-deb", "-f", deb, name])
        control[name] = out.stdout.strip()
    check(props.get("debian:control:Architecture") == "amd64" == control["Architecture"],
          "SBOM component: Architecture amd64, as dpkg-deb -f reads it")
    check(props.get("debian:control:Depends") == control["Depends"], f"SBOM: Depends {control['Depends']!r}")
    check(root.get("supplier", {}).get("name") == control["Maintainer"]
          and root.get("externalReferences", [{}])[0].get("url") == control["Homepage"],
          "SBOM: supplier and homepage from the control fields")

    tree = work / "extracted"
    shutil.rmtree(tree, ignore_errors=True)
    run(["dpkg-deb", "-x", deb, tree])
    on_disk = {p.relative_to(tree).as_posix(): sha256(p) for p in tree.rglob("*") if p.is_file()}
    shutil.rmtree(tree, ignore_errors=True)
    files = {c["name"]: c["hashes"][0]["content"] for c in bom.get("components", []) if c["type"] == "file"}
    check(bool(on_disk) and files == on_disk, f"SBOM files: all {len(on_disk)} installed files, hashes as dpkg-deb -x gives")
    check(any(n.startswith("usr/bin/xenogears") for n in files)
          and "usr/share/doc/xenogears-pc/COPYING" in files, "SBOM files: the executable and COPYING")

    libs = [c for c in bom.get("components", []) if c["type"] == "library"]
    names = {c["name"] for c in libs}
    declared = {d.split("(")[0].split(":")[0].strip() for g in control["Depends"].split(",")
                for d in g.split("|") if d.strip()}
    check(names == declared and "libvulkan1" in names, f"SBOM Depends entries: {sorted(names)}")
    check(all("version" not in c for c in libs), "SBOM Depends entries carry no (resolved) version")
    deps = bom.get("dependencies", [])
    check(deps == [{"ref": root.get("bom-ref"), "dependsOn": [c["bom-ref"] for c in libs]}],
          "SBOM dependency graph: the package on its Depends entries")
    check(bom.get("compositions") == [{"aggregate": "incomplete", "assemblies": [root.get("bom-ref")]}],
          "SBOM says it is incomplete")
    notes = {p["name"]: p["value"] for p in meta.get("properties", [])}
    check("not installed" in notes.get("xenogears:sbom:depends", "")
          and "linked into the executable" in notes.get("xenogears:sbom:unknown", ""),
          "SBOM states its coverage and unknowns")
    check(notes.get("xenogears:build:XG_PACKAGE_VERSION") == VERSION
          and notes.get("xenogears:build:CMAKE_BUILD_TYPE") == "Release"
          and bool(notes.get("xenogears:build:CMAKE_C_COMPILER_ID")),
          f"SBOM build records: Release, {notes.get('xenogears:build:CMAKE_C_COMPILER_ID')} "
          f"{notes.get('xenogears:build:CMAKE_C_COMPILER_VERSION')}, XG_VERSION {notes.get('xenogears:build:XG_VERSION')}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--reuse", action="store_true", help="use the .deb an earlier run built (no rebuild)")
    args = ap.parse_args()
    work = ROOT / f"build/pc/deb-assets-{args.ver}"
    build = work / "build"

    for tool in ("dpkg-deb", "cmake", "cpack"):
        if not check(shutil.which(tool) is not None, f"{tool} on PATH"):
            return report()
    if not args.reuse:
        made = run([sys.executable, ROOT / "tools/check_deb_package.py", "--ver", args.ver,
                    "--package-version", VERSION, "--build", build], show=False)
        print(made.stdout[-2500:])
        check(made.returncode == 0, "tools/check_deb_package.py made and checked the real package")
    real = build / DEB
    if not check(real.is_file(), f"real package {real.relative_to(ROOT)}"):
        return report()
    before = (sha256(real), real.stat().st_mtime_ns)

    public = stage_public(work)
    dist = public / "dist"
    shutil.rmtree(dist, ignore_errors=True)
    dist.mkdir()
    deb = dist / DEB
    shutil.copy2(real, deb)

    result = helper(public, "--deb", f"dist/{DEB}", "--tag", TAG, "--build-dir", build)
    print(result.stdout, result.stderr, sep="")
    sidecar, bom_path = dist / f"{DEB}.sha256", dist / f"{DEB}.cdx.json"
    if check(result.returncode == 0 and sidecar.is_file() and bom_path.is_file(),
             f"public helper wrote the companions for {TAG} beside the package"):
        check_companions(deb, sidecar, bom_path, work)
        first = (sidecar.read_bytes(), bom_path.read_bytes())
        again = helper(public, "--deb", f"dist/{DEB}", "--tag", TAG, "--build-dir", build)
        check(again.returncode == 0 and (sidecar.read_bytes(), bom_path.read_bytes()) == first,
              "a second run writes the same bytes")
    check(sha256(deb) == sha256(real) and (sha256(real), real.stat().st_mtime_ns) == before,
          "the package itself is unchanged")

    spec = importlib.util.spec_from_file_location("deb_assets", public / "packaging/deb_assets.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for tag in BAD_TAGS:
        try:
            mod.version_from_tag(tag)
            check(False, f"tag {tag!r} refused")
        except mod.AssetError:
            check(True, f"tag {tag!r} refused")
    for tag, version in (("v2026.11.0", "2026.11.0"), ("v2027.01.0", "2027.01.0"), ("v2026.12.10", "2026.12.10")):
        check(mod.version_from_tag(tag) == version, f"tag {tag} gives version {version}")

    out = work / "refused-out"
    refused(public, "a tag without its v", out, "--deb", f"dist/{DEB}", "--tag", VERSION, expect="vYYYY.MM.N")
    refused(public, "a tag of another version", out, "--deb", f"dist/{DEB}", "--tag", "v2026.11.1",
            expect="named xenogears-pc_2026.11.1_amd64.deb")
    renamed = work / "renamed" / "xenogears-pc_2026.11.1_amd64.deb"
    renamed.parent.mkdir(exist_ok=True)
    shutil.copy2(real, renamed)
    refused(public, "a package whose Version is not the tag's", out, "--deb", renamed, "--tag", "v2026.11.1",
            expect="Version is '2026.11.0'")
    refused(public, "a missing package", out, "--deb", work / "missing" / DEB, "--tag", TAG, expect="no such package")
    truncated = work / "truncated" / DEB
    truncated.parent.mkdir(exist_ok=True)
    truncated.write_bytes(real.read_bytes()[: real.stat().st_size // 2])
    refused(public, "a truncated package", out, "--deb", truncated, "--tag", TAG,
            expect=("failed", "cannot be read"))
    junk = work / "junk" / DEB
    junk.parent.mkdir(exist_ok=True)
    junk.write_text("not a Debian package (test fixture)\n")
    refused(public, "a file that is not a package", out, "--deb", junk, "--tag", TAG, expect="failed")
    other = fixture(work, DEB, "other-package", "amd64")
    refused(public, "a package of another name (fixture)", out, "--deb", other, "--tag", TAG,
            expect="Package is 'other-package'")
    arm = fixture(work, DEB, "xenogears-pc", "arm64")
    refused(public, "a package of another architecture (fixture)", out, "--deb", arm, "--tag", TAG,
            expect="Architecture is 'arm64'")
    refused(public, "a build folder that is not a CMake build", out, "--deb", f"dist/{DEB}", "--tag", TAG,
            "--build-dir", work / "public", expect="no CMakeCache.txt")
    other_build = work / "fixture-build"  # a labelled fixture: one cache line
    other_build.mkdir(exist_ok=True)
    (other_build / "CMakeCache.txt").write_text("XG_PACKAGE_VERSION:STRING=2026.11.1\n")
    refused(public, "a build folder of another version (fixture)", out, "--deb", f"dist/{DEB}", "--tag", TAG,
            "--build-dir", other_build, expect="XG_PACKAGE_VERSION is '2026.11.1'")
    empty_path = work / "empty-path"
    empty_path.mkdir(exist_ok=True)
    refused(public, "no dpkg-deb on PATH", out, "--deb", f"dist/{DEB}", "--tag", TAG,
            expect="dpkg-deb not found", env={"PATH": str(empty_path)})
    return report()


def report() -> int:
    if failures:
        print(f"check_deb_release_assets: {len(failures)} check(s) failed")
        return 1
    print("check_deb_release_assets: all checks pass (local companions only: "
          "no workflow, upload, attestation or Ubuntu installation checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
