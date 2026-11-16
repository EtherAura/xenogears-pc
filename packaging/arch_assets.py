#!/usr/bin/env python3
"""The Arch Linux PKGBUILD of a release, rendered from PKGBUILD.in beside this file.

    python packaging/arch_assets.py --tag v<YYYY>.<MM>.<N> --sha256 dist/xenogears-pc-<version>.tar.gz.sha256 --out-dir <dir>
    python packaging/arch_assets.py --tag <tag> --sha256 <file> --tarball <tarball> --out-dir <dir>

The version is the release tag without its v (CalVer vYYYY.MM.N, N from 0).
The checksum file is the release's source tarball companion in sha256sum
format: one line, a lowercase SHA-256 and the name xenogears-pc-<version>.tar.gz
(the tag's tarball, never another). With --tarball, that file must have the
same name and those exact bytes. It writes <dir>/PKGBUILD: pkgver the version,
the source that release's tarball asset, sha256sums the digest read; and it
reads the result back to check the three agree.

Nothing is built, downloaded, installed or uploaded. Standard library only;
exit status 1 (and nothing written) on any bad input.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tempfile
from pathlib import Path

PACKAGE = "xenogears-pc"
TEMPLATE = Path(__file__).resolve().parent / "PKGBUILD.in"
TAG = re.compile(r"v(20[0-9][0-9])\.(0[1-9]|1[0-2])\.(0|[1-9][0-9]*)")  # ASCII digits only
DIGEST = re.compile(r"[0-9a-f]{64}")
# sha256sum's line: digest, a space, then a space (text) or * (binary), then the name.
CHECKSUM_LINE = re.compile(r"([0-9a-f]{64}) [ *]([^\s/\\]+)")
PLACEHOLDERS = ("@PKGVER@", "@SHA256@")
ANY_PLACEHOLDER = re.compile(r"@[A-Z0-9_]+@")


class AssetError(Exception):
    pass


def version_from_tag(tag: str) -> str:
    """The package version of a release tag: vYYYY.MM.N without its v."""
    if not isinstance(tag, str) or not TAG.fullmatch(tag):
        raise AssetError(f"release tag {tag!r} is not vYYYY.MM.N (month 01-12, N a whole number from 0)")
    return tag[1:]


def tarball_name(version: str) -> str:
    """The release's source tarball asset, as the release-source workflow names it."""
    return f"{PACKAGE}-{version}.tar.gz"


def read_checksum(path: Path, version: str) -> str:
    """The digest of the tag's tarball from its .sha256 file; anything else is refused."""
    want = tarball_name(version)
    if not path.is_file():
        raise AssetError(f"{path}: no such checksum file")
    try:
        text = path.read_bytes().decode("ascii")
    except UnicodeDecodeError as error:
        raise AssetError(f"{path.name}: not a sha256sum line (not ASCII)") from error
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    if len(lines) != 1:
        raise AssetError(f"{path.name}: holds {len(lines)} lines, not the one line of {want}")
    m = CHECKSUM_LINE.fullmatch(lines[0])
    if not m:
        raise AssetError(f"{path.name}: {lines[0]!r} is not '<lowercase sha256>  <file name>'")
    if m[2] != want:
        raise AssetError(f"{path.name}: the checksum is of {m[2]!r}, not of the tag's tarball {want}")
    return m[1]


def check_tarball(tarball: Path, version: str, digest: str) -> None:
    """The supplied tarball is the tag's, by name and by bytes."""
    want = tarball_name(version)
    if not tarball.is_file():
        raise AssetError(f"{tarball}: no such tarball")
    if tarball.name != want:
        raise AssetError(f"{tarball.name}: the release's tarball is named {want}")
    sha = hashlib.sha256()
    with tarball.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            sha.update(block)
    if sha.hexdigest() != digest:
        raise AssetError(f"{tarball.name}: its SHA-256 is {sha.hexdigest()}, not the checksum file's {digest}")


def render(template: str, version: str, digest: str) -> str:
    """The template with pkgver and sha256sums filled in, each placeholder used exactly once."""
    if not TAG.fullmatch("v" + version):
        raise AssetError(f"version {version!r} is not YYYY.MM.N")
    if not DIGEST.fullmatch(digest):
        raise AssetError(f"digest {digest!r} is not a lowercase SHA-256")
    for name in PLACEHOLDERS:
        if template.count(name) != 1:
            raise AssetError(f"template: {name} appears {template.count(name)} times, not once")
    unknown = sorted(set(ANY_PLACEHOLDER.findall(template)) - set(PLACEHOLDERS))
    if unknown:
        raise AssetError(f"template: unknown placeholders {unknown}")
    text = template.replace("@PKGVER@", version).replace("@SHA256@", digest)
    check_rendered(text, version, digest)
    return text


def field(text: str, name: str) -> str:
    """A top-level assignment's value as written (one line)."""
    found = re.findall(rf"^{name}=(.*)$", text, re.M)
    if len(found) != 1:
        raise AssetError(f"PKGBUILD: {len(found)} {name}= lines, not one")
    return found[0]


def check_rendered(text: str, version: str, digest: str) -> None:
    """pkgver, source and sha256sums of a rendered PKGBUILD name the same release."""
    if ANY_PLACEHOLDER.search(text):
        raise AssetError("PKGBUILD: a placeholder is left")
    if field(text, "pkgname") != PACKAGE:
        raise AssetError(f"PKGBUILD: pkgname is not {PACKAGE}")
    if field(text, "pkgver") != version:
        raise AssetError(f"PKGBUILD: pkgver is not {version}")
    source = field(text, "source")
    want = '("$pkgname-$pkgver.tar.gz::$url/releases/download/v$pkgver/$pkgname-$pkgver.tar.gz")'
    if source != want:
        raise AssetError(f"PKGBUILD: source is {source}, not the release's tarball asset {want}")
    if field(text, "sha256sums") != f"('{digest}')":
        raise AssetError(f"PKGBUILD: sha256sums is not ('{digest}')")


def make_pkgbuild(tag: str, checksum: Path, out_dir: Path, tarball: Path | None = None,
                  template: Path = TEMPLATE) -> Path:
    version = version_from_tag(tag)
    digest = read_checksum(checksum, version)
    if tarball is not None:
        check_tarball(tarball, version, digest)
    if not template.is_file():
        raise AssetError(f"{template}: no template")
    text = render(template.read_text(encoding="utf-8"), version, digest)

    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "PKGBUILD"
    fd, tmp = tempfile.mkstemp(prefix=".PKGBUILD.", dir=out_dir)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.chmod(tmp, 0o644)
        os.replace(tmp, out)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    ap.add_argument("--sha256", type=Path, required=True,
                    help="the release tarball's .sha256 (xenogears-pc-<version>.tar.gz.sha256)")
    ap.add_argument("--tarball", type=Path, help="the release tarball itself, checked against the .sha256")
    ap.add_argument("--out-dir", type=Path, required=True, help="where PKGBUILD is written")
    ap.add_argument("--template", type=Path, default=TEMPLATE, help="the template (default: PKGBUILD.in beside this)")
    args = ap.parse_args()
    try:
        print(make_pkgbuild(args.tag, args.sha256, args.out_dir, args.tarball, args.template))
    except (AssetError, OSError) as error:
        print(f"arch_assets: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
