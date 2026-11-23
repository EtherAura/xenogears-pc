#!/usr/bin/env python3
"""Test packaging/update_manifest.py (the release's update manifest and its signature, xg-4oq.14.2).

    python tools/test_update_manifest.py

1. Public layout: update_manifest.py with the two release helpers it reads
   names from (linux_release.py, windows_release.py), alone in a scratch
   public/packaging/, import only the standard library, pass the publisher's
   date, time and private-text scan, and run from another folder with
   python -I and a bare environment.
2. make, on labelled stand-in assets (not releases, no game data): the
   manifest is exactly the expected text for every asset present, in order,
   with sizes and SHA-512s; updates off unless --updates on; refusals (a bad
   tag, --updates other than off/on, no such folder, an existing manifest, an
   asset that is a link, an asset whose .sha256 disagrees) write nothing.
3. sign and verify with FIXTURE keys made here: an unencrypted key, an
   encrypted one (XG_UPDATE_KEY_PASSPHRASE), an RSA key refused; the
   signature verifies with the key's public half and fails with another key,
   with one changed byte of the manifest or of the signature; a line of an
   unknown key beside the good one still verifies; an existing .sig is not
   overwritten.
4. keygen and public: refused without a passphrase or over an existing file;
   the new key is encrypted, mode 0600, and public prints the same half.
5. The C test's fixture (port/src/update/test/updatemanifesttest.c) is the
   tool's output for its public seed: the same manifest bytes and signature.
6. The keys the port trusts (port/src/update/update_manifest.c): 32 bytes
   each, Ed25519 public keys to OpenSSL, the fixture key not among them, and
   the count the table's own.

Needs openssl 3 on PATH. Exit status 1 on any failure.
"""
from __future__ import annotations

import ast
import hashlib
import importlib.util
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGING = ROOT / "tools/publish/repo-port/packaging"
HELPERS = ("update_manifest.py", "linux_release.py", "windows_release.py")
C_TEST = ROOT / "port/src/update/test/updatemanifesttest.c"
C_SOURCE = ROOT / "port/src/update/update_manifest.c"
SPKI_ED25519 = bytes.fromhex("302a300506032b6570032100")
FIXTURE_SEED_TEXT = b"xenogears-pc update manifest test key: public, trusted by no build"
PKCS8_ED25519 = bytes.fromhex("302e020100300506032b657004220420")
REPO = "https://github.com/EtherAura/xenogears-pc"
FAILURES: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        FAILURES.append(what)
    return ok


def run(public: Path, *args, env: dict | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess:
    base = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C"}
    return subprocess.run([sys.executable, "-I", str(public / "packaging/update_manifest.py"), *map(str, args)],
                          capture_output=True, text=True, env=dict(base, **(env or {})), cwd=cwd)


def openssl(*args) -> bytes:
    return subprocess.run(["openssl", *map(str, args)], capture_output=True, check=True).stdout


def seed_key(path: Path, seed: bytes) -> Path:
    der = path.with_suffix(".der")
    der.write_bytes(PKCS8_ED25519 + seed)
    openssl("pkey", "-inform", "DER", "-in", der, "-out", path)
    der.unlink()
    return path


def assets_for(version: str) -> list[tuple[str, str, str]]:
    return [("tarball", "linux-x86_64", f"xenogears-pc-{version}-linux-x86_64.tar.gz"),
            ("deb", "linux-x86_64", f"xenogears-pc_{version}_amd64.deb"),
            ("zip", "windows-x64", f"xenogears-pc-{version}-windows-x64.zip"),
            ("setup", "windows-x64", f"xenogears-pc-{version}-windows-x64-setup.exe"),
            ("android", "android-arm64-v8a", f"xenogears-pc-{version}-android-arm64-v8a.apk"),
            ("android", "android-x86_64", f"xenogears-pc-{version}-android-x86_64.apk")]


def stand_ins(folder: Path, version: str, only: set[str] | None = None) -> dict[str, bytes]:
    folder.mkdir(parents=True, exist_ok=True)
    made = {}
    for _, _, name in assets_for(version):
        if only is None or name in only:
            data = f"fixture: {name}\n".encode()
            (folder / name).write_bytes(data)
            made[name] = data
    return made


def expected(version: str, made: dict[str, bytes], updates: str = "off") -> bytes:
    tag = f"v{version}"
    lines = ["# xenogears-pc's update manifest: what the in-app updater reads (port/src/update/update_manifest.h).",
             "format=1", f"version={version}", f"page={REPO}/releases/tag/{tag}",
             f"download={REPO}/releases/download/{tag}/", f"updates={updates}"]
    for kind, platform, name in assets_for(version):
        if name in made:
            lines.append(f"asset={kind} {platform} {len(made[name])} {hashlib.sha512(made[name]).hexdigest()} {name}")
    return ("\n".join(lines) + "\n").encode()


def public_layout(work: Path) -> Path:
    public = work / "public tree"
    (public / "packaging").mkdir(parents=True)
    for name in HELPERS:
        shutil.copy2(PACKAGING / name, public / "packaging" / name)
    for name in HELPERS:
        tree = ast.parse((PACKAGING / name).read_text())
        mods = {a.name.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
        mods |= {n.module.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.ImportFrom) and n.module}
        local = {Path(h).stem for h in HELPERS}
        foreign = sorted(m for m in mods - local if m not in sys.stdlib_module_names and m != "__future__")
        check(not foreign, f"{name} imports only the standard library and the helpers beside it ({foreign or 'yes'})")
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    for path in (PACKAGING / "update_manifest.py", Path(__file__)):
        flagged = [l for l in path.read_text().splitlines()
                   if publish.LEFTOVER_DATE.search(l) or publish.TIME_OF_DAY.search(l) or publish.FORBIDDEN.search(l)]
        check(not flagged, f"{path.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    return public


def make_tests(public: Path, work: Path, elsewhere: Path) -> None:
    version = "2026.11.0"
    assets = work / "assets"
    made = stand_ins(assets, version)
    for name, data in made.items():
        if name.endswith(".apk"):
            (assets / f"{name}.sha256").write_text(f"{hashlib.sha256(data).hexdigest()}  {name}\n")
    (assets / "xenogears-pc-2026.11.0-link-tables.tar.gz").write_bytes(b"fixture: not listed\n")
    out = work / "out-all"
    out.mkdir()
    r = run(public, "make", "--tag", f"v{version}", "--assets", assets, "--out", out, cwd=elsewhere)
    manifest = out / "xenogears-pc-update.txt"
    if check(r.returncode == 0 and manifest.is_file(), f"make v{version}: {r.stderr.strip() or 'written'}"):
        check(manifest.read_bytes() == expected(version, made), "make: the manifest is the expected text, every asset")
    r = run(public, "make", "--tag", f"v{version}", "--assets", assets, "--out", out, cwd=elsewhere)
    check(r.returncode == 1 and "already exists" in r.stderr and manifest.read_bytes() == expected(version, made),
          "make: an existing manifest is not overwritten")

    later = "2027.01.12"
    few = work / "assets-few"
    some = stand_ins(few, later, {f"xenogears-pc-{later}-linux-x86_64.tar.gz",
                                  f"xenogears-pc-{later}-android-x86_64.apk"})
    out2 = work / "out-few"
    out2.mkdir()
    r = run(public, "make", "--tag", f"v{later}", "--assets", few, "--out", out2, "--updates", "on")
    check(r.returncode == 0 and (out2 / "xenogears-pc-update.txt").read_bytes() == expected(later, some, "on"),
          "make --updates on: two assets of a later release, the switch on")
    out3 = work / "out-none"
    out3.mkdir()
    empty = work / "assets-none"
    empty.mkdir()
    r = run(public, "make", "--tag", "v2026.12.3", "--assets", empty, "--out", out3)
    check(r.returncode == 0 and (out3 / "xenogears-pc-update.txt").read_bytes() == expected("2026.12.3", {}),
          "make: a release with no binary assets still names its version and page")

    def refused(label: str, args: list, needle: str, assets_dir: Path = assets) -> None:
        o = work / f"out-refused-{re.sub(r'[^a-z0-9]+', '-', label)}"
        o.mkdir()
        r = run(public, "make", "--assets", assets_dir, "--out", o, *args)
        check(r.returncode == 1 and needle in r.stderr and not any(o.iterdir()),
              f"make refuses {label}: {r.stderr.strip()[:120]}")

    for tag in ("2026.11.0", "v2026.13.0", "v2026.11.01", "v2026.11.0-rc1", ""):
        refused(f"tag {tag!r}", ["--tag", tag], "release tag")
    refused("--updates yes", ["--tag", "v2026.11.0", "--updates", "yes"], "is not off or on")
    refused("no assets folder", ["--tag", "v2026.11.0"], "not a folder", work / "missing")
    bad = work / "assets-bad-sum"
    stand_ins(bad, version)
    (bad / f"xenogears-pc-{version}-windows-x64.zip.sha256").write_text("0" * 64 + f"  xenogears-pc-{version}-windows-x64.zip\n")
    refused("an asset whose .sha256 disagrees", ["--tag", f"v{version}"], "is not the SHA-256", bad)
    linked = work / "assets-link"
    stand_ins(linked, version, set())
    os.symlink(assets / f"xenogears-pc-{version}-linux-x86_64.tar.gz",
               linked / f"xenogears-pc-{version}-linux-x86_64.tar.gz")
    refused("an asset that is a link", ["--tag", f"v{version}"], "not a regular file", linked)
    r = run(public, "make", "--tag", f"v{version}", "--assets", assets, "--out", work / "no-such-out")
    check(r.returncode == 1 and "not a folder" in r.stderr, "make refuses an --out that is not a folder")


def sign_tests(public: Path, work: Path) -> None:
    keys = work / "keys"
    keys.mkdir()
    plain = seed_key(keys / "plain.pem", hashlib.sha256(b"fixture key one").digest())
    other = seed_key(keys / "other.pem", hashlib.sha256(b"fixture key two").digest())
    secret = {"XG_UPDATE_KEY_PASSPHRASE": "fixture passphrase, not a real one"}
    locked = keys / "locked.pem"
    openssl("pkey", "-in", plain, "-aes-256-cbc", "-passout", "pass:" + secret["XG_UPDATE_KEY_PASSPHRASE"], "-out", locked)
    rsa = keys / "rsa.pem"
    openssl("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", rsa)

    def half(key: Path, env: dict | None = None) -> str:
        r = run(public, "public", "--key", key, env=env)
        m = re.fullmatch(r"public key ([0-9a-f]{64}) \(key id ([0-9a-f]{16})\)\n", r.stdout)
        check(r.returncode == 0 and m is not None and m[1][:16] == m[2], f"public {key.name}: {r.stdout.strip()}")
        return m[1] if m else ""

    pub, pub_other = half(plain), half(other)
    check(half(locked, secret) == pub, "public of the encrypted copy (passphrase from the environment) is the same half")
    r = run(public, "public", "--key", locked)
    check(r.returncode == 1 and "XG_UPDATE_KEY_PASSPHRASE" in r.stderr, "public of an encrypted key needs the passphrase")
    r = run(public, "public", "--key", rsa)
    check(r.returncode == 1 and "not an Ed25519 key" in r.stderr, "an RSA key is refused")

    folder = work / "signed"
    stand_ins(folder / "assets", "2026.11.0")
    (folder / "out").mkdir()
    run(public, "make", "--tag", "v2026.11.0", "--assets", folder / "assets", "--out", folder / "out")
    manifest = folder / "out/xenogears-pc-update.txt"
    sig = manifest.with_name(manifest.name + ".sig")
    r = run(public, "sign", "--manifest", manifest, "--key", locked)
    check(r.returncode == 1 and not sig.exists(), "sign with an encrypted key and no passphrase: refused, nothing written")
    r = run(public, "sign", "--manifest", manifest, "--key", rsa)
    check(r.returncode == 1 and not sig.exists(), "sign with an RSA key: refused, nothing written")
    r = run(public, "sign", "--manifest", manifest, "--key", locked, env=secret)
    if not check(r.returncode == 0 and sig.is_file(), f"sign with the encrypted key: {r.stderr.strip() or 'written'}"):
        return
    line = sig.read_text()
    check(re.fullmatch(rf"ed25519 {pub[:16]} [A-Za-z0-9+/]{{86}}==\n", line) is not None,
          f"the .sig is one line: ed25519, the key id, 64 bytes in base64 ({line.strip()[:40]}...)")
    r = run(public, "sign", "--manifest", manifest, "--key", plain)
    check(r.returncode == 1 and "already exists" in r.stderr and sig.read_text() == line,
          "sign: an existing .sig is not overwritten")

    def verified(m: Path, s: Path, *keys: str) -> bool:
        r = run(public, "verify", "--manifest", m, "--sig", s, *[a for k in keys for a in ("--pubkey", k)])
        return r.returncode == 0 and r.stdout.startswith("verified: key ")

    check(verified(manifest, sig, pub), "verify with the key's public half")
    check(verified(manifest, sig, pub_other, pub), "verify with two keys, the right one second")
    check(not verified(manifest, sig, pub_other), "verify with another key fails")
    changed = folder / "changed.txt"
    changed.write_bytes(manifest.read_bytes().replace(b"updates=off", b"updates=on "))
    check(not verified(changed, sig, pub), "one changed word of the manifest (the switch) fails")
    flipped = folder / "flipped.sig"
    text = line.split()
    b64 = text[2]
    flipped.write_text(f"{text[0]} {text[1]} {('B' if b64[5] == 'A' else 'A').join([b64[:5], b64[6:]])}\n")
    check(not verified(manifest, flipped, pub), "one changed digit of the signature fails")
    both = folder / "both.sig"
    both.write_text(f"ed25519 {pub_other[:16]} {b64}\n{line}")
    check(verified(manifest, both, pub, pub_other), "a line of another key beside the good one: verified by the good one")
    r = run(public, "verify", "--manifest", manifest, "--pubkey", pub.upper())
    check(r.returncode == 1 and "64 lower-case hex digits" in r.stderr, "verify refuses a key that is not lower-case hex")


def keygen_tests(public: Path, work: Path) -> None:
    out = work / "new-key.pem"
    r = run(public, "keygen", "--out", out)
    check(r.returncode == 1 and not out.exists(), "keygen without a passphrase: refused, nothing written")
    secret = {"XG_UPDATE_KEY_PASSPHRASE": "fixture passphrase two, not a real one"}
    r = run(public, "keygen", "--out", out, env=secret)
    m = re.fullmatch(r"public key ([0-9a-f]{64}) \(key id ([0-9a-f]{16})\)\n", r.stdout)
    if not check(r.returncode == 0 and m is not None and out.is_file(), f"keygen: {r.stdout.strip() or r.stderr.strip()}"):
        return
    check(stat.S_IMODE(out.stat().st_mode) == 0o600, "the new key is mode 0600")
    check("ENCRYPTED PRIVATE KEY" in out.read_text(), "the new key is encrypted")
    r2 = run(public, "public", "--key", out, env=secret)
    check(r2.returncode == 0 and r2.stdout == r.stdout, "public prints the half keygen printed")
    before = out.read_bytes()
    r = run(public, "keygen", "--out", out, env=secret)
    check(r.returncode == 1 and out.read_bytes() == before, "keygen does not overwrite an existing key")
    check(not [p for p in out.parent.iterdir() if p.name.startswith("xg-update-keygen-")],
          "keygen leaves no temporary folder")


def c_string(text: str, name: str) -> str:
    m = re.search(rf"static const char {name}\[\] =\n(.*?);\n", text, re.S)
    if not m:
        return ""
    return "".join(ast.literal_eval(part) for part in re.findall(r'^\s*(".*")$', m[1], re.M))


def fixture_tests(public: Path, work: Path) -> None:
    folder = work / "c-fixture"
    stand_ins(folder / "assets", "2026.11.27")
    (folder / "out").mkdir()
    key = seed_key(work / "c-fixture.pem", hashlib.sha256(FIXTURE_SEED_TEXT).digest())
    run(public, "make", "--tag", "v2026.11.27", "--assets", folder / "assets", "--out", folder / "out")
    run(public, "sign", "--manifest", folder / "out/xenogears-pc-update.txt", "--key", key)
    text = C_TEST.read_text()
    check(c_string(text, "MANIFEST") == (folder / "out/xenogears-pc-update.txt").read_text(),
          "the C test's manifest is the tool's, for its stand-in assets")
    check(c_string(text, "SIGNATURE") == (folder / "out/xenogears-pc-update.txt.sig").read_text(),
          "the C test's signature is the tool's, for its public seed (Ed25519 signatures are deterministic)")
    r = run(public, "public", "--key", key)
    check(re.search(r'FIXTURE_KEY = "([0-9a-f]{64})"', text) is not None
          and re.search(r'FIXTURE_KEY = "([0-9a-f]{64})"', text)[1] in r.stdout, "the C test's fixture key is its seed's")


def trusted_key_tests(work: Path) -> None:
    text = C_SOURCE.read_text()
    m = re.search(r"static const uint8_t TRUSTED_KEYS\[\]\[32\] = \{\n(.*?)\n\};\n", text, re.S)
    if not check(m is not None, "update_manifest.c has its table of trusted keys"):
        return
    body = re.sub(r"/\*.*?\*/", "", m[1], flags=re.S)
    rows = re.findall(r"\{([^{}]*)\}", body)
    keys = [bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})\b", row)) for row in rows]
    check(len(keys) >= 1 and all(len(k) == 32 for k in keys) and len(set(keys)) == len(keys),
          "the trusted keys are 32 bytes each, at least one, none twice")
    fixture = re.search(r'FIXTURE_KEY = "([0-9a-f]{64})"', C_TEST.read_text())
    check(fixture is not None and bytes.fromhex(fixture[1]) not in keys, "the fixture key is not trusted")
    for i, k in enumerate(keys):
        der = work / f"trusted-{i}.der"
        der.write_bytes(SPKI_ED25519 + k)
        r = subprocess.run(["openssl", "pkey", "-pubin", "-inform", "DER", "-in", der, "-noout", "-text"],
                           capture_output=True, text=True)
        check(r.returncode == 0 and "ED25519 Public-Key" in r.stdout,
              f"trusted key {k[:8].hex()} is an Ed25519 public key to OpenSSL")
    check("TRUSTED_COUNT = (int)(sizeof TRUSTED_KEYS / sizeof TRUSTED_KEYS[0]);" in text,
          "the trusted key count is the table's")


def main() -> int:
    if not shutil.which("openssl"):
        print("test_update_manifest: no openssl on PATH")
        return 1
    with tempfile.TemporaryDirectory(prefix="xg update manifest ") as tmp:
        work = Path(tmp)
        elsewhere = work / "another folder"
        elsewhere.mkdir()
        public = public_layout(work)
        r = run(public, "names", cwd=elsewhere)
        check(r.returncode == 0 and r.stdout == "xenogears-pc-update.txt\nxenogears-pc-update.txt.sig\n",
              "names: the two fixed asset names")
        make_tests(public, work, elsewhere)
        sign_tests(public, work)
        keygen_tests(public, work)
        fixture_tests(public, work)
        trusted_key_tests(work)
    if FAILURES:
        print(f"test_update_manifest: {len(FAILURES)} check(s) failed")
        return 1
    print("test_update_manifest: all checks pass (fixture assets and keys only: no release, no real key, no upload)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
