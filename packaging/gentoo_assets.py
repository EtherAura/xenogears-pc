#!/usr/bin/env python3
"""The Gentoo ebuild of a release, rendered from xenogears-pc.ebuild.in beside this file.

    python packaging/gentoo_assets.py --tag v<YYYY>.<MM>.<N> --out-dir <dir>
    python packaging/gentoo_assets.py --version <YYYY>.<MM>.<N> --out-dir <dir>
    python packaging/gentoo_assets.py --tag <tag> --out-dir <dir> --template <file>

The version is the release tag without its v (CalVer vYYYY.MM.N, N from 0);
give the tag or the version, not both. It writes
<dir>/xenogears-pc-<version>.ebuild: an ebuild's version is its file name, so
the template holds none, and its SRC_URI is that release's
xenogears-pc-<version>.tar.gz asset. The template is checked before anything
is written (EAPI 8, cmake.eclass, ~amd64, the asset's SRC_URI and folder, no
version written out), and the file appears whole or not at all.

Nothing is built, downloaded, installed or uploaded, and no Manifest is made
(ebuild <file> manifest does that, from the downloaded tarball). Standard
library only; exit status 1 (and nothing written) on any bad input.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

PACKAGE = "xenogears-pc"
TEMPLATE = Path(__file__).resolve().parent / "xenogears-pc.ebuild.in"
VERSION = re.compile(r"(20[0-9][0-9])\.(0[1-9]|1[0-2])\.(0|[1-9][0-9]{0,8})")  # ASCII digits only
ANY_VERSION = re.compile(r"20[0-9][0-9]\.[0-9]{1,2}\.[0-9]+")
PLACEHOLDER = "@TAG@"
ANY_PLACEHOLDER = re.compile(r"@[A-Za-z0-9_]+@")
MAX_TEMPLATE = 64 * 1024
# The lines every rendered ebuild must hold exactly once, as written.
REQUIRED = {
    "KEYWORDS": 'KEYWORDS="~amd64"',
    "SRC_URI": 'SRC_URI="https://github.com/EtherAura/xenogears-pc/releases/download/v${PV}/${P}.tar.gz"',
    "S": 'S="${WORKDIR}/${P}"',
    "CMAKE_USE_DIR": 'CMAKE_USE_DIR="${S}/port"',
    "CMAKE_BUILD_TYPE": "CMAKE_BUILD_TYPE=Release",
}


class AssetError(Exception):
    pass


def check_version(version: object) -> str:
    """A release's package version, YYYY.MM.N; anything else is refused."""
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise AssetError(f"version {version!r} is not YYYY.MM.N (month 01-12, N a whole number from 0)")
    return version


def version_from_tag(tag: object) -> str:
    """The package version of a release tag: vYYYY.MM.N without its v."""
    if not isinstance(tag, str) or not tag.startswith("v") or not VERSION.fullmatch(tag[1:]):
        raise AssetError(f"release tag {tag!r} is not vYYYY.MM.N (month 01-12, N a whole number from 0)")
    return tag[1:]


def ebuild_name(version: str) -> str:
    return f"{PACKAGE}-{check_version(version)}.ebuild"


def tarball_name(version: str) -> str:
    """The release's source tarball asset, as the release-source workflow names it."""
    return f"{PACKAGE}-{check_version(version)}.tar.gz"


def check_template(template: str) -> None:
    """The template is an EAPI 8 cmake.eclass ebuild of the release asset, with one placeholder."""
    if not template:
        raise AssetError("template: empty")
    if len(template) > MAX_TEMPLATE:
        raise AssetError(f"template: longer than {MAX_TEMPLATE} characters")
    if not template.isascii():
        raise AssetError("template: not ASCII")
    if "\0" in template or "\r" in template:
        raise AssetError("template: a NUL or a carriage return")
    if not template.endswith("\n"):
        raise AssetError("template: no final newline")
    if template.count(PLACEHOLDER) != 1:
        raise AssetError(f"template: {PLACEHOLDER} appears {template.count(PLACEHOLDER)} times, not once")
    unknown = sorted(set(ANY_PLACEHOLDER.findall(template)) - {PLACEHOLDER})
    if unknown:
        raise AssetError(f"template: unknown placeholders {unknown}")
    literal = ANY_VERSION.findall(template)
    if literal:
        raise AssetError(f"template: a version is written out ({literal[0]}); the file name carries it")
    lines = template.split("\n")
    code = [line for line in lines if line.strip() and not line.lstrip().startswith("#")]
    if not code or code[0] != "EAPI=8":
        raise AssetError("template: its first statement is not EAPI=8")
    if sum(1 for line in lines if re.match(r"\s*EAPI=", line)) != 1:
        raise AssetError("template: more than one EAPI= line")
    inherits = [line for line in lines if re.match(r"\s*inherit\b", line)]
    if len(inherits) != 1 or "cmake" not in inherits[0].split()[1:]:
        raise AssetError(f"template: {len(inherits)} inherit lines, or none that inherits cmake")
    for name, want in REQUIRED.items():
        found = [line for line in lines if re.match(rf"\s*{name}=", line)]
        if found != [want]:
            raise AssetError(f"template: {name} is {found}, not the one line {want}")


def render(template: str, version: str) -> str:
    """The template with its one placeholder filled in with the release's tag."""
    check_version(version)
    check_template(template)
    text = template.replace(PLACEHOLDER, "v" + version)
    if ANY_PLACEHOLDER.search(text):
        raise AssetError("ebuild: a placeholder is left")
    return text


def make_ebuild(version: str, out_dir: Path, template: Path = TEMPLATE) -> Path:
    name = ebuild_name(version)
    if not template.is_file():
        raise AssetError(f"{template}: no template")
    try:
        source = template.read_bytes().decode("ascii")
    except UnicodeDecodeError as error:
        raise AssetError("template: not ASCII") from error
    text = render(source, version)

    out = out_dir / name
    if out.is_symlink() or (out.exists() and not out.is_file()):
        raise AssetError(f"{out}: in the way, and not a plain file")
    out_dir.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=f".{name}.", dir=out_dir)
    try:
        with os.fdopen(fd, "w", encoding="ascii", newline="\n") as f:
            f.write(text)
        os.chmod(tmp, 0o644)
        os.replace(tmp, out)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    which = ap.add_mutually_exclusive_group(required=True)
    which.add_argument("--tag", help="the release tag, vYYYY.MM.N")
    which.add_argument("--version", help="the release's version, YYYY.MM.N (the tag without its v)")
    ap.add_argument("--out-dir", type=Path, required=True, help="where xenogears-pc-<version>.ebuild is written")
    ap.add_argument("--template", type=Path, default=TEMPLATE,
                    help="the template (default: xenogears-pc.ebuild.in beside this)")
    args = ap.parse_args()
    try:
        version = version_from_tag(args.tag) if args.tag is not None else check_version(args.version)
        print(make_ebuild(version, args.out_dir, args.template))
    except (AssetError, OSError) as error:
        print(f"gentoo_assets: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
