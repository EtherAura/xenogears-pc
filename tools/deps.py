#!/usr/bin/env python3
"""The source tarballs the PC port's builds download: one pinned list, fetched and checked.

    python tools/deps.py list                      # what is pinned, and what is in the cache
    python tools/deps.py fetch [--cache DIR] [NAME...]   # download what is missing, check every sum
    python tools/deps.py check [--cache DIR] [NAME...]   # check what is there; no network
    python tools/deps.py path NAME [--cache DIR]   # the checked tarball's path (for the build scripts)
    python tools/deps.py want NAME                 # "<file> <sha256>": what a build stamp records
    python tools/deps.py version NAME

tools/deps.json names each tarball: its version, the file it is saved as, the
URL on its project's own site and its SHA-256. That file is the one place a
version is stated: tools/build_sdl3_mingw.sh, build_glslang_mingw.sh and
build_dav1d_mingw.sh ask this tool for their tarball, and a release's fetch
step (build-release.sh --fetch-only) is `deps.py fetch --cache DIR`.

Nothing is used unchecked. fetch downloads into the cache under a temporary
name, reads the bytes back, and keeps the file only when its SHA-256 is the
pinned one; a wrong or cut-off download is removed and the command fails. A
file already in the cache with the right sum is not downloaded again; one
with a wrong sum is never replaced silently: the command fails and names it.
path prints a tarball only after checking its sum, looking in the cache
(--cache, else $XG_DEPS_CACHE, else tools/toolchain-src) and then in
/var/cache/distfiles, where a distribution may already keep the same file.

Only https URLs are fetched, and a pinned file name is a plain name: nothing
is written outside the cache. glslang's URL is an archive its host generates
from a tag, not an uploaded file; its bytes have been stable, but a mismatch
there means "look", not "tampered".

Exit status: 0 done, 1 something is missing or does not match, 2 usage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "deps.json"
DEFAULT_CACHE = ROOT / "tools" / "toolchain-src"
ALSO = (Path("/var/cache/distfiles"),)
PLAIN_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class DepsError(Exception):
    pass


def load(manifest: Path | None = None) -> dict[str, dict]:
    manifest = manifest or MANIFEST
    try:
        pins = json.loads(manifest.read_text())
    except (OSError, ValueError) as error:
        raise DepsError(f"{manifest}: {error}") from error
    if not isinstance(pins, dict) or not pins:
        raise DepsError(f"{manifest}: no pins")
    for name, pin in pins.items():
        if not isinstance(pin, dict):
            raise DepsError(f"{manifest}: {name} is not an entry")
        for key in ("version", "file", "url", "sha256"):
            if not isinstance(pin.get(key), str) or not pin[key]:
                raise DepsError(f"{manifest}: {name} has no {key}")
        if not PLAIN_NAME.match(pin["file"]) or pin["file"] in (".", ".."):
            raise DepsError(f"{manifest}: {name}: the file name {pin['file']!r} is not a plain name")
        if not pin["url"].startswith("https://"):
            raise DepsError(f"{manifest}: {name}: only https URLs are fetched, not {pin['url']!r}")
        if not SHA256.match(pin["sha256"]):
            raise DepsError(f"{manifest}: {name}: sha256 is not 64 lower-case hex digits")
    return pins


def cache_dir(given: str | None) -> Path:
    return Path(given or os.environ.get("XG_DEPS_CACHE") or DEFAULT_CACHE).expanduser()


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def matches(path: Path, pin: dict) -> bool:
    return path.is_file() and sha256_of(path) == pin["sha256"]


def download(url: str, out: Path) -> None:
    """The URL's bytes into out. Redirects are followed only to https."""
    request = urllib.request.Request(url, headers={"User-Agent": "xenogears-pc-deps"})
    with urllib.request.urlopen(request, timeout=120) as response:
        if not response.geturl().startswith("https://"):
            raise DepsError(f"{url} was redirected away from https ({response.geturl()})")
        with open(out, "wb") as f:
            for block in iter(lambda: response.read(1 << 20), b""):
                f.write(block)


def fetch_one(name: str, pin: dict, cache: Path, get=download) -> str:
    """Puts pin's tarball into cache, checked. Returns what it did: 'kept' or 'fetched'."""
    out = cache / pin["file"]
    if out.exists():
        if matches(out, pin):
            return "kept"
        raise DepsError(f"{name}: {out} is there with another SHA-256 ({sha256_of(out) if out.is_file() else 'not a file'}); "
                        f"the pin is {pin['sha256']}. Nothing was replaced: look at it, then remove it and fetch again")
    cache.mkdir(parents=True, exist_ok=True)
    part = cache / (pin["file"] + ".part")
    try:
        try:
            get(pin["url"], part)
        except DepsError:
            raise
        except Exception as error:  # the network's errors are many kinds
            raise DepsError(f"{name}: {pin['url']}: {error}") from error
        got = sha256_of(part) if part.is_file() else "nothing"
        if got != pin["sha256"]:
            raise DepsError(f"{name}: {pin['url']} gave SHA-256 {got} ({part.stat().st_size if part.is_file() else 0} "
                            f"bytes); the pin is {pin['sha256']}. Not kept")
        os.replace(part, out)
    finally:
        if part.exists():
            part.unlink()
    return "fetched"


def find(name: str, pin: dict, cache: Path, also: tuple[Path, ...] = ALSO) -> Path:
    """The pinned tarball where it already is, its sum checked; DepsError when it is nowhere."""
    seen = []
    for folder in (cache, *also):
        path = folder / pin["file"]
        if path.is_file():
            if matches(path, pin):
                return path
            seen.append(f"{path} (another SHA-256)")
    raise DepsError(f"{name}: no {pin['file']} with the pinned SHA-256 in {cache}"
                    + "".join(f" or {folder}" for folder in also)
                    + (": " + ", ".join(seen) if seen else "")
                    + f". Run: python tools/deps.py fetch {name}")


def chosen(pins: dict, names: list[str]) -> dict:
    unknown = [n for n in names if n not in pins]
    if unknown:
        raise DepsError(f"not pinned: {', '.join(unknown)} (pinned: {', '.join(pins)})")
    return {n: pins[n] for n in names} if names else pins


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    for command in ("list", "fetch", "check"):
        p = sub.add_parser(command)
        p.add_argument("names", nargs="*", metavar="NAME")
        p.add_argument("--cache", help="the folder the tarballs are kept in")
    for command in ("path", "want", "version"):
        p = sub.add_parser(command)
        p.add_argument("name", metavar="NAME")
        if command == "path":
            p.add_argument("--cache", help="the folder the tarballs are kept in")
    a = ap.parse_args(argv)
    try:
        pins = load()
        if a.command in ("want", "version", "path"):
            pin = chosen(pins, [a.name])[a.name]
            if a.command == "want":
                print(f"{pin['file']} {pin['sha256']}")
            elif a.command == "version":
                print(pin["version"])
            else:
                print(find(a.name, pin, cache_dir(a.cache)))
            return 0
        cache = cache_dir(a.cache)
        failed = 0
        for name, pin in chosen(pins, a.names).items():
            try:
                if a.command == "fetch":
                    print(f"{name} {pin['version']}: {fetch_one(name, pin, cache)} {cache / pin['file']}")
                elif a.command == "check":
                    print(f"{name} {pin['version']}: ok {find(name, pin, cache, ())}")
                else:
                    here = cache / pin["file"]
                    state = "in the cache, checked" if matches(here, pin) else \
                        "in the cache with ANOTHER SHA-256" if here.exists() else "not in the cache"
                    print(f"{name} {pin['version']}  {pin['file']}  {state}\n    {pin['url']}\n    sha256 {pin['sha256']}")
            except DepsError as error:
                print(f"deps: {error}", file=sys.stderr)
                failed += 1
        return 1 if failed else 0
    except DepsError as error:
        print(f"deps: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
