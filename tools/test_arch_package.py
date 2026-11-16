#!/usr/bin/env python3
"""Test the Arch PKGBUILD template and its renderer (packaging/ in xenogears-pc).

    python tools/test_arch_package.py

1. Public layout: arch_assets.py and PKGBUILD.in, alone in a scratch
   public/packaging/ (as tools/publish_decomp.py lays tools/publish/repo-port/
   over the tree), import only the standard library, name no private folder,
   pass the publisher's date, time and private-text scan, and run from there
   with python -I and a bare environment, also from another folder.
2. For two fixture releases (v2026.11.0 and v2027.01.12; labelled stand-in
   tarballs made here, the same bytes every run: not releases, no game data),
   the rendered PKGBUILD is the template with only pkgver and the digest filled
   in; bash -n accepts it; sourced by bash, pkgver is the version, source the
   release's tarball asset under the tag, sha256sums the fixture's real
   SHA-256 (sha256sum -c agrees with the .sha256). makepkg --printsrcinfo runs
   too when makepkg is on PATH (it is not required).
3. The template agrees with the port's rules: the tarball name and prefix of
   the release-source workflow, the CMake requirements (SDL3, Vulkan with glslc,
   glslang, dav1d, Python, Ninja) as depends/makedepends, the Ubuntu package's
   configure options, install rule and Vulkan loader dependency, makepkg's LTO
   off (the game C's partial link), and the version accepted by deb.cmake.
4. Refusals, exit 1 with nothing written (an earlier PKGBUILD left as it was):
   a missing, empty, malformed, multi-line or non-ASCII checksum, uppercase or
   short digests, a checksum of another tarball, a tarball whose name or bytes
   are not the checksum's, malformed or shell-unsafe tags, and templates with
   a missing, repeated or unknown placeholder.

No network, GPU or game data; nothing is built, installed or uploaded. Output
stays in a scratch folder under build/. Exit status 1 on any failure.
"""
from __future__ import annotations

import ast
import gzip
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

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "tools/publish/repo-port/packaging"
HELPER, TEMPLATE = "arch_assets.py", "PKGBUILD.in"
RELEASES = (("v2026.11.0", "2026.11.0"), ("v2027.01.12", "2027.01.12"))
URL = "https://github.com/EtherAura/xenogears-pc"
BAD_TAGS = ["2026.11.0", "v2026.11", "v2026.13.0", "v2026.00.0", "v2026.1.0", "v2026.11.01", "v26.11.0",
            "v2026.11.0-rc1", "v2026.11.-1", " v2026.11.0", "v2026.11.0\n", "V2026.11.0", "", "v2026.11.0.0",
            "v2026.11.0/../x", "v2026.11.0;touch PWNED", "v2026.11.0$(touch PWNED)", "v2026.11.0'", "v2026.11.0`id`",
            "v2026.11.0 x", "v٢026.11.0", "v2026.11.1٢"]
DEPENDS = {"glibc", "gcc-libs", "sdl3", "vulkan-icd-loader", "glslang", "dav1d"}
MAKEDEPENDS = {"cmake", "ninja", "python", "python-yaml", "vulkan-headers", "shaderc"}

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def load(path: Path):
    spec = importlib.util.spec_from_file_location("arch_assets_under_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixture_tarball(directory: Path, version: str) -> Path:
    """A labelled stand-in for the release tarball: the same bytes every run."""
    name = f"xenogears-pc-{version}"
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for member, data in ((f"{name}/FIXTURE.txt", f"test fixture for {version}, not a release\n".encode()),):
            info = tarfile.TarInfo(member)
            info.size, info.mtime, info.mode, info.uname, info.gname = len(data), 0, 0o644, "", ""
            tar.addfile(info, io.BytesIO(data))
    out = directory / f"{name}.tar.gz"
    with out.open("wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as gz:
        gz.write(raw.getvalue())
    return out


def write_checksum(tarball: Path) -> Path:
    sidecar = tarball.with_name(tarball.name + ".sha256")
    sidecar.write_text(f"{hashlib.sha256(tarball.read_bytes()).hexdigest()}  {tarball.name}\n")
    return sidecar


def run_cli(helper: Path, *args: str, cwd: Path) -> subprocess.CompletedProcess:
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C"}
    return subprocess.run([sys.executable, "-I", str(helper), *args], cwd=cwd, env=env,
                          capture_output=True, text=True)


def sourced(pkgbuild: Path) -> dict[str, list[str]]:
    """The PKGBUILD's top-level arrays as bash sees them (its own file, ours; functions are not run)."""
    names = ["pkgname", "pkgver", "pkgrel", "arch", "url", "license", "depends", "makedepends", "options",
             "source", "sha256sums"]
    script = 'source "$1" || exit 9; for n in ' + " ".join(names) + \
             '; do declare -n a="$n"; printf "%s" "$n"; printf "\\t%s" "${a[@]}"; printf "\\n"; unset -n a; done'
    result = subprocess.run(["bash", "--noprofile", "--norc", "-c", script, "_", str(pkgbuild)],
                            env={"PATH": os.environ.get("PATH", "/usr/bin:/bin")}, capture_output=True, text=True)
    if result.returncode != 0:
        return {"error": [result.stderr]}
    return {line.split("\t")[0]: line.split("\t")[1:] for line in result.stdout.splitlines()}


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
    publish = load(ROOT / "tools/publish_decomp.py")
    for name in (HELPER, TEMPLATE):
        flagged = [line for line in (public / name).read_text().splitlines()
                   if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line)
                   or publish.FORBIDDEN.search(line)]
        check(not flagged, f"{name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    return public / HELPER


def renders(helper: Path, scratch: Path) -> dict[str, str]:
    module = load(helper)
    template = (helper.parent / TEMPLATE).read_text()
    fixtures = scratch / "fixtures"
    fixtures.mkdir()
    texts = {}
    for tag, version in RELEASES:
        tarball = fixture_tarball(fixtures, version)
        again = fixture_tarball(scratch, version)
        check(tarball.read_bytes() == again.read_bytes(), f"{tag}: fixture tarball is the same bytes twice")
        again.unlink()
        sidecar = write_checksum(tarball)
        digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
        if shutil.which("sha256sum"):
            ok = subprocess.run(["sha256sum", "-c", "--status", sidecar.name], cwd=fixtures).returncode == 0
            check(ok, f"{tag}: sha256sum -c accepts the fixture's .sha256")
        out = scratch / f"out-{version}"
        elsewhere = scratch / "elsewhere"
        elsewhere.mkdir(exist_ok=True)
        r = run_cli(helper, "--tag", tag, "--sha256", str(sidecar), "--tarball", str(tarball), "--out-dir", str(out),
                    cwd=elsewhere)
        check(r.returncode == 0, f"{tag}: renders with python -I from another folder ({r.stderr.strip() or 'ok'})")
        pkgbuild = out / "PKGBUILD"
        if not pkgbuild.is_file():
            check(False, f"{tag}: PKGBUILD written")
            continue
        text = pkgbuild.read_text()
        texts[version] = text
        check(text == template.replace("@PKGVER@", version).replace("@SHA256@", digest),
              f"{tag}: PKGBUILD is the template with only pkgver and the digest filled in")
        check(text == module.render(template, version, digest), f"{tag}: CLI and render() agree")
        r = run_cli(helper, "--tag", tag, "--sha256", str(sidecar), "--out-dir", str(scratch / f"plain-{version}"),
                    cwd=helper.parent)
        check(r.returncode == 0 and (scratch / f"plain-{version}/PKGBUILD").read_text() == text,
              f"{tag}: the same PKGBUILD without --tarball, run beside the template")
        syntax = subprocess.run(["bash", "-n", str(pkgbuild)], capture_output=True, text=True)
        check(syntax.returncode == 0, f"{tag}: bash -n accepts the PKGBUILD ({syntax.stderr.strip() or 'ok'})")
        got = sourced(pkgbuild)
        check(got.get("pkgname") == ["xenogears-pc"] and got.get("pkgver") == [version] and got.get("pkgrel") == ["1"],
              f"{tag}: pkgname, pkgver {version}, pkgrel 1 ({got.get('pkgver')})")
        want_source = f"{tarball.name}::{URL}/releases/download/{tag}/{tarball.name}"
        check(got.get("source") == [want_source], f"{tag}: source is the release's tarball asset ({got.get('source')})")
        check(got.get("sha256sums") == [digest], f"{tag}: sha256sums is the tarball's real SHA-256")
        check(set(got.get("depends", [])) == DEPENDS and set(got.get("makedepends", [])) == MAKEDEPENDS,
              f"{tag}: depends {got.get('depends')} and makedepends {got.get('makedepends')}")
        check(got.get("arch") == ["x86_64"] and got.get("url") == [URL], f"{tag}: arch x86_64 and the port's url")
        check("!lto" in got.get("options", []), f"{tag}: makepkg's LTO off ({got.get('options')})")
        makepkg = shutil.which("makepkg")
        if makepkg:
            info = subprocess.run([makepkg, "--printsrcinfo"], cwd=out, capture_output=True, text=True)
            check(info.returncode == 0 and f"pkgver = {version}" in info.stdout and f"sha256sums = {digest}" in info.stdout,
                  f"{tag}: makepkg --printsrcinfo agrees")
        else:
            print(f"note {tag}: makepkg not on PATH, --printsrcinfo not run")
    check(len(set(texts.values())) == len(RELEASES), "the two releases render different PKGBUILDs")
    return texts


def port_rules(text: str) -> None:
    """The template's fields against the port's build and release rules."""
    workflow = (ROOT / "tools/publish/repo-port/.github/workflows/release-source.yml").read_text()
    for needle in ('name="xenogears-pc-$VERSION"', '--prefix="$name/"', 'sha256sum "$name.tar.gz" > "$name.tar.gz.sha256"'):
        check(needle in workflow, f"release-source.yml makes the tarball the PKGBUILD expects ({needle})")
    cmake = (ROOT / "port/CMakeLists.txt").read_text()
    gpu = (ROOT / "port/src/gpu/gpu.cmake").read_text()
    deb = (ROOT / "port/cmake/deb.cmake").read_text()
    pc_sources = (ROOT / "tools/pc_sources.py").read_text()
    rules = [
        ("find_package(SDL3 REQUIRED" in cmake, "sdl3", "depends"),
        ("find_package(Vulkan REQUIRED COMPONENTS glslc)" in gpu, "vulkan-headers", "makedepends"),
        ("find_package(Vulkan REQUIRED COMPONENTS glslc)" in gpu, "shaderc", "makedepends"),
        ("CPACK_DEBIAN_PACKAGE_DEPENDS libvulkan1" in deb, "vulkan-icd-loader", "depends"),
        ("find_package(glslang CONFIG" in cmake, "glslang", "depends"),
        ("find_library(XG_DAV1D_LIBRARY NAMES dav1d)" in cmake, "dav1d", "depends"),
        ("LINKER_LANGUAGE CXX" in cmake, "gcc-libs", "depends"),
        ("find_package(Python3 REQUIRED" in cmake, "python", "makedepends"),
        (re.search(r"^import yaml$", pc_sources, re.M) is not None, "python-yaml", "makedepends"),
        ("-G Ninja" in deb, "ninja", "makedepends"),
        ("cmake_minimum_required" in cmake, "cmake", "makedepends"),
    ]
    for present, package, array in rules:
        line = field_line(text, array)
        check(present and re.search(rf"'{re.escape(package)}'", line) is not None,
              f"port rule found and {package} in {array}")
    configure = re.search(r"cmake -S port -B <build> -G Ninja (.*?)\\\n#\s+(.*?)\n", deb)
    flags = [w for w in (configure[1] + " " + configure[2]).split() if w.startswith("-D")] if configure else []
    check(bool(flags), f"deb.cmake documents the release configure ({flags})")
    for flag in flags:
        name = flag.split("=")[0]
        check(name in text, f"build() passes {name} as the Ubuntu release does")
    check("install(TARGETS xenogears RUNTIME DESTINATION bin)" in deb and "share/doc/xenogears-pc" in deb
          and "-DCMAKE_INSTALL_PREFIX=/usr" in text and 'DESTDIR="$pkgdir" cmake --install' in text
          and 'ln -s "../doc/$pkgname" "$pkgdir/usr/share/licenses/$pkgname"' in text,
          "package() installs by the port's install rules into /usr, licences under /usr/share/licenses")
    check("pc_link links each module with ld -r" in cmake and "options=('!lto'" in text,
          "makepkg's LTO off: the game C's per-module partial link needs ordinary objects")
    check("python tools/pc_sources.py" in text and "XG_SOURCES_CMAKE" in cmake,
          "build() writes the PC source list CMake requires")
    cmake_version = re.search(r'XG_PACKAGE_VERSION MATCHES "([^"]+)"', deb)
    for _, version in RELEASES:
        check(bool(cmake_version and re.fullmatch(cmake_version[1].strip("^$"), version)),
              f"deb.cmake accepts XG_PACKAGE_VERSION={version}")
    check(re.search(r"\.(bin|cue|iso|img|chd|ccd|mds|ecm|pbp)\b", text, re.I) is None,
          "the template names no disc image")


def field_line(text: str, name: str) -> str:
    found = re.findall(rf"^{name}=(.*)$", text, re.M)
    return found[0] if len(found) == 1 else ""


def refusals(helper: Path, scratch: Path) -> None:
    module = load(helper)
    template = (helper.parent / TEMPLATE).read_text()
    fixtures = scratch / "fixtures"
    tag, version = RELEASES[0]
    other_tag, other_version = RELEASES[1]
    tarball = fixtures / f"xenogears-pc-{version}.tar.gz"
    other = fixtures / f"xenogears-pc-{other_version}.tar.gz"
    digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
    bad = scratch / "bad"
    bad.mkdir()
    out = scratch / "refused"
    out.mkdir()
    sentinel = "# an earlier PKGBUILD, left as it was\n"
    (out / "PKGBUILD").write_text(sentinel)

    def refused(what: str, *args: str) -> None:
        r = run_cli(helper, *args, "--out-dir", str(out), cwd=scratch)
        left = sorted(p.name for p in out.iterdir())
        check(r.returncode == 1 and left == ["PKGBUILD"] and (out / "PKGBUILD").read_text() == sentinel
              and "arch_assets:" in r.stderr and "Traceback" not in r.stderr,
              f"refused, nothing written: {what} ({r.stderr.strip().splitlines()[-1] if r.stderr.strip() else r.returncode})")

    def sidecar(label: str, content: bytes) -> Path:
        path = bad / label / f"xenogears-pc-{version}.tar.gz.sha256"
        path.parent.mkdir()
        path.write_bytes(content)
        return path

    name = tarball.name
    refused("no checksum file", "--tag", tag, "--sha256", str(bad / "absent.sha256"))
    cases = [
        ("empty checksum file", b""),
        ("blank line", b"\n"),
        ("digest only", f"{digest}\n".encode()),
        ("one space between digest and name", f"{digest} {name}\n".encode()),
        ("63-digit digest", f"{digest[:63]}  {name}\n".encode()),
        ("65-digit digest", f"{digest}0  {name}\n".encode()),
        ("uppercase digest", f"{digest.upper()}  {name}\n".encode()),
        ("non-hex digest", f"{'g' * 64}  {name}\n".encode()),
        ("SKIP for a digest", f"SKIP  {name}\n".encode()),
        ("two lines", f"{digest}  {name}\n{digest}  {name}\n".encode()),
        ("trailing text", f"{digest}  {name} extra\n".encode()),
        ("CRLF line", f"{digest}  {name}\r\n".encode()),
        ("a path, not a file name", f"{digest}  dist/{name}\n".encode()),
        ("not ASCII", f"{digest}  {name}é\n".encode()),
        ("checksum of another release's tarball", f"{digest}  {other.name}\n".encode()),
        ("checksum of the Debian package", f"{digest}  xenogears-pc_{version}_amd64.deb\n".encode()),
        ("checksum of an unversioned tarball", f"{digest}  xenogears-pc.tar.gz\n".encode()),
    ]
    for i, (what, content) in enumerate(cases):
        refused(what, "--tag", tag, "--sha256", str(sidecar(f"case{i}", content)))
    good = fixtures / f"{name}.sha256"
    refused("tarball whose bytes are not the checksum's", "--tag", tag, "--sha256", str(good),
            "--tarball", str(_renamed(other, bad / "swap", name)))
    refused("tarball of another release", "--tag", tag, "--sha256", str(good), "--tarball", str(other))
    refused("no tarball", "--tag", tag, "--sha256", str(good), "--tarball", str(bad / name))
    refused("checksum for the other tag", "--tag", other_tag, "--sha256", str(good))
    for bad_tag in BAD_TAGS:
        refused(f"tag {bad_tag!r}", "--tag", bad_tag, "--sha256", str(good))
    check(not any(scratch.rglob("PWNED")) and not Path("PWNED").exists(), "no tag ran as shell code")

    for what, text in (("no @PKGVER@", template.replace("@PKGVER@", "2026.11.0")),
                       ("@SHA256@ twice", template + "# @SHA256@\n"),
                       ("an unknown placeholder", template + "# @PKGREL@\n")):
        path = bad / f"template-{len(what)}-{what.split()[-1].strip('@')}.in"
        path.write_text(text)
        refused(f"template with {what}", "--tag", tag, "--sha256", str(good), "--template", str(path))
    refused("no template", "--tag", tag, "--sha256", str(good), "--template", str(bad / "absent.in"))
    for what, args in (("version with a quote", ("2026.11.0'", digest)), ("version with $()", ("2026.11.0$(id)", digest)),
                       ("SKIP for a digest", (version, "SKIP")), ("uppercase digest", (version, digest.upper()))):
        try:
            module.render(template, *args)
            check(False, f"render() refuses {what}")
        except module.AssetError:
            check(True, f"render() refuses {what}")


def _renamed(source: Path, directory: Path, name: str) -> Path:
    """Another tarball's bytes under this release's tarball name."""
    directory.mkdir()
    return Path(shutil.copy2(source, directory / name))


def main() -> int:
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="arch-package-test-", dir=ROOT / "build") as tmp:
        scratch = Path(tmp)
        helper = public_layout(scratch)
        texts = renders(helper, scratch)
        port_rules((OVERLAY / TEMPLATE).read_text())
        if texts:
            refusals(helper, scratch)
    print(f"{len(failures)} failure(s)" if failures else "all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
