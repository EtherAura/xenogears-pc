#!/usr/bin/env python3
"""A release's update manifest and its signature: what the in-app updater reads.

    python packaging/update_manifest.py names
    python packaging/update_manifest.py make   --tag v<YYYY>.<MM>.<N> --assets DIR --out DIR [--updates off|on]
    python packaging/update_manifest.py sign   --manifest FILE --key KEY.pem
    python packaging/update_manifest.py verify --manifest FILE --pubkey HEX [--pubkey HEX ...]
    python packaging/update_manifest.py keygen --out KEY.pem
    python packaging/update_manifest.py public --key KEY.pem

Every release carries two assets under fixed names, so that GitHub's
releases/latest/download/<name> always gives the latest release's:

  xenogears-pc-update.txt       the manifest (port/src/update/update_manifest.h has the format)
  xenogears-pc-update.txt.sig   its signature: lines of "ed25519 <key id> <base64>"

make writes the manifest from the release's assets in --assets: the Linux
tarball, the .deb, the Windows .zip and setup.exe and the two APKs, each that
is there, with its size and SHA-512 (an asset's .sha256 beside it must agree).
--updates is the switch the app obeys, off unless given: off until a release's
save states load in every other canonical release's, both ways (xg-4oq.14.7).
sign signs it with the release's Ed25519 key, a PEM file (PKCS#8, encrypted or
not; the passphrase from XG_UPDATE_KEY_PASSPHRASE, never an argument), and
checks the signature it wrote. verify checks a manifest's .sig against public
keys (64 hex digits each): exit 0 when one line verifies with one of them.
keygen makes a new key, encrypted with XG_UPDATE_KEY_PASSPHRASE, and prints
its public half; public prints a key's public half and key id. The public
half is what port/src/update/update_manifest.c trusts.

The key id is the public key's first 8 bytes in hex. Ed25519 is RFC 8032's,
over the manifest's exact bytes. The signing and checking are OpenSSL's (3.0
or later, `openssl` on PATH or --openssl); this file uses only the standard
library otherwise. Nothing is overwritten: make, sign and keygen refuse an
existing output. Exit status 1, with nothing written, on any bad input.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # the release helpers beside this one (python -I adds no folder)

import linux_release  # noqa: E402
import windows_release  # noqa: E402

PACKAGE = "xenogears-pc"
MANIFEST = "xenogears-pc-update.txt"
SIGNATURE = MANIFEST + ".sig"
REPO = "https://github.com/EtherAura/xenogears-pc"
PASSPHRASE = "XG_UPDATE_KEY_PASSPHRASE"
ED25519_SPKI = bytes.fromhex("302a300506032b6570032100")  # SubjectPublicKeyInfo of an Ed25519 key, before its 32 bytes
ABIS = ("arm64-v8a", "x86_64")  # packaging/build-android-release.sh
HEX64 = re.compile(r"[0-9a-f]{64}")


class ManifestError(Exception):
    pass


def assets_of(tag: str) -> list[tuple[str, str, str]]:
    """(kind, platform, file name) of every asset the manifest can list, in its order."""
    linux = linux_release.release_names(tag)  # refuses a bad tag
    windows = windows_release.release_names(tag)
    version = linux["version"]
    out = [("tarball", "linux-x86_64", linux["tarball"]), ("deb", "linux-x86_64", f"{PACKAGE}_{version}_amd64.deb"),
           ("zip", "windows-x64", windows["zip"]), ("setup", "windows-x64", windows["setup"])]
    out += [("android", f"android-{abi}", f"{PACKAGE}-{version}-android-{abi}.apk") for abi in ABIS]
    return out


def sidecar_agrees(path: Path, sha256: str) -> None:
    sidecar = path.with_name(path.name + ".sha256")
    if not sidecar.exists():
        return
    want = f"{sha256}  {path.name}\n"
    if sidecar.read_text(errors="replace") != want:
        raise ManifestError(f"{sidecar.name} is not the SHA-256 of {path.name}; nothing written")


def manifest_text(tag: str, assets: Path, updates: str) -> bytes:
    if updates not in ("off", "on"):
        raise ManifestError(f"--updates {updates!r} is not off or on")
    if not assets.is_dir():
        raise ManifestError(f"--assets {assets}: not a folder")
    try:
        listed = assets_of(tag)
    except (linux_release.ReleaseError, windows_release.ReleaseError) as error:
        raise ManifestError(str(error)) from error
    version = tag[1:]
    lines = ["# xenogears-pc's update manifest: what the in-app updater reads (port/src/update/update_manifest.h).",
             "format=1", f"version={version}", f"page={REPO}/releases/tag/{tag}",
             f"download={REPO}/releases/download/{tag}/", f"updates={updates}"]
    for kind, platform, name in listed:
        path = assets / name
        if path.is_symlink() or (path.exists() and not path.is_file()):
            raise ManifestError(f"{name} is not a regular file; nothing written")
        if not path.is_file():
            continue
        data = path.read_bytes()
        sidecar_agrees(path, hashlib.sha256(data).hexdigest())
        lines.append(f"asset={kind} {platform} {len(data)} {hashlib.sha512(data).hexdigest()} {name}")
    return ("\n".join(lines) + "\n").encode()


def write_new(path: Path, data: bytes, mode: int = 0o644) -> None:
    """A new file of these bytes, never over an existing one."""
    if os.path.lexists(path):
        raise ManifestError(f"{path} already exists; nothing is overwritten")
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


def openssl(tool: str, *args, data: bytes | None = None) -> bytes:
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    if PASSPHRASE in os.environ:
        env[PASSPHRASE] = os.environ[PASSPHRASE]
    try:
        r = subprocess.run([tool, *[str(a) for a in args]], input=data, capture_output=True, env=env, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ManifestError(f"openssl ({tool}) could not run: {error}") from error
    if r.returncode != 0:
        detail = " ".join(r.stderr.decode(errors="replace").split())[:300]
        raise ManifestError(f"openssl {args[0]} failed: {detail}")
    return r.stdout


def find_openssl(given: str | None) -> str:
    tool = given or shutil.which("openssl")
    if not tool:
        raise ManifestError("no openssl on PATH (give --openssl)")
    return tool


def passin(key: Path) -> list[str]:
    """OpenSSL's passphrase argument for a key: from the environment when the key is encrypted."""
    text = key.read_text(errors="replace")
    if "ENCRYPTED PRIVATE KEY" not in text:
        return []
    if not os.environ.get(PASSPHRASE):
        raise ManifestError(f"{key.name} is encrypted: set {PASSPHRASE}")
    return ["-passin", f"env:{PASSPHRASE}"]


def public_key(tool: str, key: Path) -> bytes:
    der = openssl(tool, "pkey", "-in", key, *passin(key), "-pubout", "-outform", "DER")
    if len(der) != 44 or der[:12] != ED25519_SPKI:
        raise ManifestError(f"{key.name} is not an Ed25519 key")
    return der[12:]


def check_line(tool: str, text: bytes, line: str, keys: list[bytes]) -> bool:
    parts = line.split()
    if len(parts) != 3 or parts[0] != "ed25519" or not re.fullmatch(r"[0-9a-f]{16}", parts[1]):
        return False
    try:
        signature = base64.b64decode(parts[2], validate=True)
    except ValueError:
        return False
    if len(signature) != 64:
        return False
    for key in keys:
        if key[:8].hex() != parts[1]:
            continue
        with tempfile.TemporaryDirectory(prefix="xg-update-verify-") as work:
            w = Path(work)
            (w / "key.der").write_bytes(ED25519_SPKI + key)
            (w / "data").write_bytes(text)
            (w / "sig").write_bytes(signature)
            try:
                out = openssl(tool, "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey", w / "key.der",
                              "-rawin", "-in", w / "data", "-sigfile", w / "sig")
            except ManifestError:
                continue
            if b"Signature Verified Successfully" in out:
                return True
    return False


def verify(tool: str, manifest: Path, sig: Path, keys: list[bytes]) -> str:
    text = manifest.read_bytes()
    lines = [l for l in sig.read_text(errors="replace").splitlines() if l.strip()]
    for line in lines:
        if check_line(tool, text, line, keys):
            return line.split()[1]
    raise ManifestError(f"{sig.name}: no line verifies {manifest.name} with the given keys ({len(lines)} lines)")


def sign(tool: str, manifest: Path, key: Path) -> Path:
    sig = manifest.with_name(manifest.name + ".sig")
    if os.path.lexists(sig):
        raise ManifestError(f"{sig} already exists; nothing is overwritten")
    pub = public_key(tool, key)
    with tempfile.TemporaryDirectory(prefix="xg-update-sign-") as work:
        out = Path(work) / "sig"
        openssl(tool, "pkeyutl", "-sign", "-inkey", key, *passin(key), "-rawin", "-in", manifest, "-out", out)
        signature = out.read_bytes()
    if len(signature) != 64:
        raise ManifestError(f"openssl wrote a signature of {len(signature)} bytes, not 64")
    line = f"ed25519 {pub[:8].hex()} {base64.b64encode(signature).decode()}\n"
    if not check_line(tool, manifest.read_bytes(), line, [pub]):
        raise ManifestError("the new signature does not verify; nothing written")
    write_new(sig, line.encode())
    return sig


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    sub.add_parser("names")
    p = sub.add_parser("make")
    p.add_argument("--tag", required=True)
    p.add_argument("--assets", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--updates", default="off")
    for name in ("sign", "verify", "keygen", "public"):
        p = sub.add_parser(name)
        p.add_argument("--openssl")
        if name in ("sign", "verify"):
            p.add_argument("--manifest", required=True, type=Path)
        if name in ("sign", "public"):
            p.add_argument("--key", required=True, type=Path)
        if name == "verify":
            p.add_argument("--sig", type=Path)
            p.add_argument("--pubkey", action="append", required=True)
        if name == "keygen":
            p.add_argument("--out", required=True, type=Path)
    a = ap.parse_args(argv)
    try:
        if a.command == "names":
            print(MANIFEST, SIGNATURE, sep="\n")
        elif a.command == "make":
            if not a.out.is_dir():
                raise ManifestError(f"--out {a.out}: not a folder")
            text = manifest_text(a.tag, a.assets, a.updates)
            write_new(a.out / MANIFEST, text)
            print(a.out / MANIFEST)
        elif a.command == "sign":
            print(sign(find_openssl(a.openssl), a.manifest, a.key))
        elif a.command == "verify":
            keys = []
            for k in a.pubkey:
                if not HEX64.fullmatch(k):
                    raise ManifestError(f"--pubkey {k!r} is not 64 lower-case hex digits")
                keys.append(bytes.fromhex(k))
            sig = a.sig or a.manifest.with_name(a.manifest.name + ".sig")
            print(f"verified: key {verify(find_openssl(a.openssl), a.manifest, sig, keys)}")
        elif a.command == "keygen":
            if not os.environ.get(PASSPHRASE):
                raise ManifestError(f"set {PASSPHRASE}: the new key is encrypted with it")
            if os.path.lexists(a.out):
                raise ManifestError(f"{a.out} already exists; nothing is overwritten")
            tool = find_openssl(a.openssl)
            with tempfile.TemporaryDirectory(prefix="xg-update-keygen-", dir=a.out.parent) as work:
                tmp = Path(work) / "key.pem"
                openssl(tool, "genpkey", "-algorithm", "ed25519", "-aes-256-cbc", "-pass", f"env:{PASSPHRASE}",
                        "-out", tmp)
                os.chmod(tmp, 0o600)
                pub = public_key(tool, tmp)
                os.link(tmp, a.out)
            print(f"public key {pub.hex()} (key id {pub[:8].hex()})")
        elif a.command == "public":
            pub = public_key(find_openssl(a.openssl), a.key)
            print(f"public key {pub.hex()} (key id {pub[:8].hex()})")
    except (ManifestError, OSError) as error:
        print(f"update_manifest: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
