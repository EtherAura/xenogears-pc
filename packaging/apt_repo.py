#!/usr/bin/env python3
"""A release's APT repository: the Ubuntu package's index files, signed, so apt can upgrade it.

    python packaging/apt_repo.py names
    python packaging/apt_repo.py make   --deb DIR/xenogears-pc_<version>_amd64.deb --out DIR [--date EPOCH]
    python packaging/apt_repo.py sign   --release DIR/Release --gnupg-home HOME --key FINGERPRINT
    python packaging/apt_repo.py verify --inrelease DIR/InRelease --release DIR/Release --pubkey KEY.asc \\
                                        --fingerprint FINGERPRINT
    python packaging/apt_repo.py keygen --gnupg-home HOME
    python packaging/apt_repo.py public --gnupg-home HOME --key FINGERPRINT --out DIR/xenogears-pc-archive-keyring.asc

The repository is a "flat" one (Debian's repository format, without dists/ or
components) made of each release's own assets, so that GitHub's
releases/latest/download/<name> serves the latest release's:

  Packages                           the .deb's control fields with its file name, size, SHA-256 and SHA-512
  Release                            Packages's size and hashes, the release's version and date
  InRelease                          Release, clear-signed with the repository's OpenPGP key
  xenogears-pc-archive-keyring.asc   that key's public half

A player adds it once (the key in /etc/apt/keyrings, the source naming it):

  deb [signed-by=/etc/apt/keyrings/xenogears-pc-archive-keyring.asc] \\
      https://github.com/EtherAura/xenogears-pc/releases/latest/download/ ./

and apt update / apt upgrade install each new release with apt's own checks:
InRelease's signature by that key, then Packages's and the .deb's hashes. Only
the latest release is offered.

make reads the .deb itself (an ar archive whose control.tar.gz, .xz or .zst holds
./control), checks its name is xenogears-pc_<Version>_amd64.deb with the package
xenogears-pc of architecture amd64 and a release version (YYYY.MM.N), and writes
Packages and Release; --date (seconds since 1970, default SOURCE_DATE_EPOCH, else
now) is Release's Date. sign clear-signs Release into InRelease with the key of
that fingerprint in that GnuPG home, its passphrase from XG_APT_KEY_PASSPHRASE
(never an argument). verify checks InRelease with a public key: signed by the
key of that fingerprint, and its text exactly Release's. keygen makes a new
signing key (Ed25519, no expiry) in an empty or new GnuPG home, protected by
XG_APT_KEY_PASSPHRASE, and prints its fingerprint; public exports its public half.

Nothing is overwritten: make, sign, public and keygen refuse an existing output.
Needs gpg (GnuPG 2.2 or later) for sign, verify, keygen and public; the standard
library otherwise. Exit status 1, with nothing written, on any bad input.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import lzma
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from email.utils import formatdate
from pathlib import Path

PACKAGE = "xenogears-pc"
NAMES = ("Packages", "Release", "InRelease", "xenogears-pc-archive-keyring.asc")
PASSPHRASE = "XG_APT_KEY_PASSPHRASE"
VERSION = re.compile(r"20\d\d\.(0[1-9]|1[0-2])\.(0|[1-9]\d*)")
FINGERPRINT = re.compile(r"[0-9A-F]{40}")
DEB_MAX = 512 << 20
UID = "xenogears-pc APT repository"


class RepoError(Exception):
    pass


# ---- the .deb ------------------------------------------------------------------------------------

def ar_members(data: bytes) -> dict[str, bytes]:
    """An ar archive's members by name (a .deb: debian-binary, control.tar.*, data.tar.*)."""
    if data[:8] != b"!<arch>\n":
        raise RepoError("not a Debian package (no ar header)")
    members, at = {}, 8
    while at + 60 <= len(data):
        head = data[at:at + 60]
        if head[58:60] != b"`\n":
            raise RepoError("a damaged ar member header")
        name = head[:16].decode("ascii", "replace").strip().rstrip("/")
        try:
            size = int(head[48:58].decode("ascii").strip())
        except ValueError as error:
            raise RepoError("an ar member's size is not a number") from error
        body = data[at + 60:at + 60 + size]
        if len(body) != size:
            raise RepoError("the package is cut short")
        members[name] = body
        at += 60 + size + (size & 1)
    return members


def control_text(deb: bytes) -> str:
    members = ar_members(deb)
    if members.get("debian-binary", b"").strip() != b"2.0":
        raise RepoError("not a Debian 2.0 package")
    for name, body in members.items():
        if not name.startswith("control.tar"):
            continue
        if name == "control.tar.gz":
            raw = gzip.decompress(body)
        elif name == "control.tar.xz":
            raw = lzma.decompress(body)
        elif name == "control.tar.zst":
            try:
                from compression import zstd  # Python 3.14
            except ImportError as error:
                raise RepoError("control.tar.zst needs Python 3.14's compression.zstd") from error
            raw = zstd.decompress(body)
        elif name == "control.tar":
            raw = body
        else:
            raise RepoError(f"a control member of an unknown kind ({name})")
        with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as tar:
            for member in tar:
                if member.isfile() and member.name.removeprefix("./") == "control":
                    return tar.extractfile(member).read().decode("utf-8")
        raise RepoError("the control archive has no control file")
    raise RepoError("the package has no control archive")


def parse_control(text: str) -> list[tuple[str, str]]:
    """Fields in order, a field's continuation lines kept (with their leading space)."""
    fields: list[tuple[str, str]] = []
    for line in text.rstrip("\n").split("\n"):
        if line[:1] in (" ", "\t"):
            if not fields:
                raise RepoError("the control file starts with a continuation line")
            name, value = fields[-1]
            fields[-1] = (name, value + "\n" + line)
        elif ":" in line:
            name, value = line.split(":", 1)
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]*", name):
                raise RepoError(f"a control field's name is not one ({name[:40]!r})")
            fields.append((name, value.strip()))
        elif line.strip():
            raise RepoError(f"a control line is not a field ({line[:40]!r})")
    return fields


# ---- the index files ------------------------------------------------------------------------------

def packages_text(deb_path: Path) -> bytes:
    if deb_path.is_symlink() or not deb_path.is_file():
        raise RepoError(f"--deb {deb_path}: not a regular file")
    deb = deb_path.read_bytes()
    if len(deb) > DEB_MAX:
        raise RepoError(f"{deb_path.name} is larger than a release's package")
    fields = parse_control(control_text(deb))
    get = dict(fields)
    version = get.get("Version", "")
    if get.get("Package") != PACKAGE or get.get("Architecture") != "amd64" or not VERSION.fullmatch(version):
        raise RepoError(f"{deb_path.name} is not {PACKAGE} for amd64 with a release version "
                        f"(Package {get.get('Package')!r}, Architecture {get.get('Architecture')!r}, "
                        f"Version {version!r})")
    want = f"{PACKAGE}_{version}_amd64.deb"
    if deb_path.name != want:
        raise RepoError(f"{deb_path.name} is not named {want}")
    for name in ("Filename", "Size", "MD5sum", "SHA1", "SHA256", "SHA512"):
        if name in get:
            raise RepoError(f"the package's own control has {name}, which only an index has")
    lines = [f"{name}: {value}" for name, value in fields if name != "Description"]
    lines += [f"Filename: {want}", f"Size: {len(deb)}", f"SHA256: {hashlib.sha256(deb).hexdigest()}",
              f"SHA512: {hashlib.sha512(deb).hexdigest()}"]
    if "Description" in get:
        lines.append(f"Description: {get['Description']}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def release_text(packages: bytes, version: str, when: int) -> bytes:
    lines = [f"Origin: {PACKAGE}", f"Label: {PACKAGE}", f"Version: {version}",
             f"Date: {formatdate(when, usegmt=True)}", "Architectures: amd64", "Acquire-By-Hash: no",
             f"Description: the latest release of {PACKAGE} (https://github.com/EtherAura/xenogears-pc)",
             "SHA256:", f" {hashlib.sha256(packages).hexdigest()} {len(packages)} Packages",
             "SHA512:", f" {hashlib.sha512(packages).hexdigest()} {len(packages)} Packages"]
    return ("\n".join(lines) + "\n").encode("utf-8")


def write_new(path: Path, data: bytes, mode: int = 0o644) -> None:
    """A new file of these bytes, never over an existing one."""
    if os.path.lexists(path):
        raise RepoError(f"{path} already exists; nothing is overwritten")
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, mode)
        os.link(tmp, path)  # fails if the name appeared meanwhile
    finally:
        os.unlink(tmp)


# ---- GnuPG ------------------------------------------------------------------------------------------

def gpg(home: Path, *args, data: bytes | None = None, secret: bool = False) -> subprocess.CompletedProcess:
    tool = shutil.which("gpg")
    if not tool:
        raise RepoError("no gpg on PATH")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "GNUPGHOME": str(home), "LC_ALL": "C"}
    argv = [tool, "--homedir", str(home), "--batch", "--no-tty"]
    if secret:
        phrase = os.environ.get(PASSPHRASE)
        if not phrase:
            raise RepoError(f"set {PASSPHRASE}: the key's passphrase")
        argv += ["--pinentry-mode", "loopback", "--passphrase-fd", "0"]
        data = phrase.encode() + b"\n" + (data or b"")
    try:
        return subprocess.run(argv + [str(a) for a in args], input=data, capture_output=True, env=env, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RepoError(f"gpg could not run: {error}") from error


def gpg_ok(r: subprocess.CompletedProcess, what: str) -> bytes:
    if r.returncode != 0:
        detail = " ".join(r.stderr.decode(errors="replace").split())[:300]
        raise RepoError(f"gpg {what} failed: {detail}")
    return r.stdout


def stop_agent(home: Path) -> None:
    tool = shutil.which("gpgconf")
    if tool:
        subprocess.run([tool, "--homedir", str(home), "--kill", "gpg-agent"], capture_output=True, timeout=30)


def check_fingerprint(fpr: str) -> str:
    fpr = fpr.replace(" ", "").upper()
    if not FINGERPRINT.fullmatch(fpr):
        raise RepoError(f"{fpr!r} is not a 40-digit OpenPGP fingerprint")
    return fpr


def sign(release: Path, home: Path, fpr: str) -> Path:
    out = release.with_name("InRelease")
    if os.path.lexists(out):
        raise RepoError(f"{out} already exists; nothing is overwritten")
    text = release.read_bytes()
    try:
        signed = gpg_ok(gpg(home, "--local-user", fpr, "--digest-algo", "SHA512", "--clearsign", "--output", "-",
                            data=text, secret=True), "--clearsign")
    finally:
        stop_agent(home)
    write_new(out, signed)
    return out


def verify(inrelease: Path, release: Path, pubkey: Path, fpr: str) -> str:
    with tempfile.TemporaryDirectory(prefix="xg-apt-verify-") as tmp:
        home = Path(tmp)
        os.chmod(home, 0o700)
        text = home / "signed-text"
        try:
            gpg_ok(gpg(home, "--import", pubkey), "--import")
            r = gpg(home, "--status-fd", "2", "--output", text, "--decrypt", inrelease)
            status = r.stderr.decode(errors="replace").splitlines()
            signed = text.read_bytes() if text.exists() else b""
        finally:
            stop_agent(home)
    valid = [line.split()[2] for line in status if line.startswith("[GNUPG:] VALIDSIG ")]
    if r.returncode != 0 or not valid:
        raise RepoError(f"{inrelease.name} is not validly signed by a key in {pubkey.name}")
    if fpr not in valid:
        raise RepoError(f"{inrelease.name} is signed by {valid[0]}, not {fpr}")
    if signed != release.read_bytes():
        raise RepoError(f"{inrelease.name}'s signed text is not {release.name}")
    return fpr


def signing_fingerprints(home: Path) -> list[str]:
    """The fingerprints of the primary keys this home can sign with (public values, safe to print)."""
    out = gpg_ok(gpg(home, "--with-colons", "--list-secret-keys"), "--list-secret-keys").decode()
    fprs, primary = [], False
    for line in out.splitlines():
        f = line.split(":")
        if f[0] == "sec":
            primary = True
        elif f[0] == "fpr" and primary:
            fprs.append(f[9])
            primary = False
    return fprs


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    sub.add_parser("names")
    p = sub.add_parser("make")
    p.add_argument("--deb", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--date", type=int)
    p = sub.add_parser("sign")
    p.add_argument("--release", required=True, type=Path)
    p.add_argument("--gnupg-home", required=True, type=Path)
    p.add_argument("--key", required=True)
    p = sub.add_parser("verify")
    p.add_argument("--inrelease", required=True, type=Path)
    p.add_argument("--release", required=True, type=Path)
    p.add_argument("--pubkey", required=True, type=Path)
    p.add_argument("--fingerprint", required=True)
    p = sub.add_parser("keygen")
    p.add_argument("--gnupg-home", required=True, type=Path)
    p = sub.add_parser("public")
    p.add_argument("--gnupg-home", required=True, type=Path)
    p.add_argument("--key", required=True)
    p.add_argument("--out", required=True, type=Path)
    a = ap.parse_args(argv)
    try:
        if a.command == "names":
            print(*NAMES, sep="\n")
        elif a.command == "make":
            if not a.out.is_dir():
                raise RepoError(f"--out {a.out}: not a folder")
            for name in ("Packages", "Release"):
                if os.path.lexists(a.out / name):
                    raise RepoError(f"{a.out / name} already exists; nothing is overwritten")
            packages = packages_text(a.deb)
            version = re.search(rb"^Version: (.+)$", packages, re.M)[1].decode()
            when = a.date if a.date is not None else int(os.environ.get("SOURCE_DATE_EPOCH") or time.time())
            release = release_text(packages, version, when)
            write_new(a.out / "Packages", packages)
            write_new(a.out / "Release", release)
            print(a.out / "Packages", a.out / "Release", sep="\n")
        elif a.command == "sign":
            print(sign(a.release, a.gnupg_home, check_fingerprint(a.key)))
        elif a.command == "verify":
            fpr = verify(a.inrelease, a.release, a.pubkey, check_fingerprint(a.fingerprint))
            print(f"verified: signed by {fpr}")
        elif a.command == "keygen":
            home = a.gnupg_home
            if home.exists() and any(home.iterdir()):
                raise RepoError(f"{home} is not empty; nothing is overwritten")
            home.mkdir(mode=0o700, parents=True, exist_ok=True)
            os.chmod(home, 0o700)
            try:
                gpg_ok(gpg(home, "--quick-generate-key", UID, "ed25519", "sign", "never", secret=True),
                       "--quick-generate-key")
                fprs = signing_fingerprints(home)
            finally:
                stop_agent(home)
            if len(fprs) != 1:
                raise RepoError(f"{home} holds {len(fprs)} secret keys, not the one made")
            print(f"fingerprint {fprs[0]}")
        elif a.command == "public":
            fpr = check_fingerprint(a.key)
            try:
                if fpr not in signing_fingerprints(a.gnupg_home):
                    raise RepoError(f"{a.gnupg_home} has no secret key {fpr}")
                key = gpg_ok(gpg(a.gnupg_home, "--armor", "--export", fpr), "--export")
            finally:
                stop_agent(a.gnupg_home)
            write_new(a.out, key)
            print(a.out)
    except (RepoError, OSError, ValueError) as error:
        print(f"apt_repo: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
