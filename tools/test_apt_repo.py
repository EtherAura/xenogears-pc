#!/usr/bin/env python3
"""Test packaging/apt_repo.py (the release's APT repository: its index files and their signature, xg-4oq.14.6).

    python tools/test_apt_repo.py

1. Public layout: apt_repo.py alone in a scratch public/packaging/, importing
   only the standard library, passes the publisher's date, time and private-text
   scan, and runs from another folder with python -I and a bare environment.
2. make, on fixture packages built here (an ar archive with control.tar.gz or
   control.tar.xz and a data archive; no release, no game data): Packages and
   Release exactly as expected (the control fields, the file's name, size and
   hashes; Packages's hashes; --date); refusals, writing nothing: a name that is
   not xenogears-pc_<Version>_amd64.deb, another package or architecture, a
   version that is not a release's, a control that already has Filename, not a
   package, no control file, a link, an existing Packages or Release.
3. With gpg: keygen refuses without XG_APT_KEY_PASSPHRASE or into a non-empty
   home, makes one Ed25519 key, public exports it; sign refuses without the
   passphrase and never overwrites InRelease; verify passes the key's own
   signature and refuses another fingerprint, another key's signature, a changed
   InRelease and an InRelease of another Release.

The acceptance test (a clean Ubuntu container: add the repository, install,
apt upgrade, a bad signature refused) is work/updater-e2e/apt_container.sh.
Exit status 1 on any failure.
"""
from __future__ import annotations

import ast
import gzip
import hashlib
import importlib.util
import io
import lzma
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from email.utils import formatdate
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools/publish/repo-port/packaging/apt_repo.py"
DATE = 1795000000
PASS = {"XG_APT_KEY_PASSPHRASE": "fixture-passphrase"}
FAILURES: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        FAILURES.append(what)
    return ok


def run(public: Path, *args, env: dict | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess:
    base = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C"}
    return subprocess.run([sys.executable, "-I", str(public / "packaging/apt_repo.py"), *map(str, args)],
                          capture_output=True, text=True, env=dict(base, **(env or {})), cwd=cwd, timeout=180)


# ---- fixture packages ----------------------------------------------------------------------------

def tar_of(files: dict[str, bytes]) -> bytes:
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w", format=tarfile.GNU_FORMAT) as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return out.getvalue()


def ar_of(members: list[tuple[str, bytes]]) -> bytes:
    out = bytearray(b"!<arch>\n")
    for name, data in members:
        out += f"{name:<16}{0:<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode()
        out += data + (b"\n" if len(data) & 1 else b"")
    return bytes(out)


FIELDS = [("Package", "xenogears-pc"), ("Version", "2026.12.0"), ("Architecture", "amd64"),
          ("Maintainer", "EtherAura"), ("Installed-Size", "21262"),
          ("Depends", "libvulkan1, libc6 (>= 2.43), libsdl3-0 (>= 3.4.0)"), ("Section", "games"),
          ("Priority", "optional"), ("Homepage", "https://github.com/EtherAura/xenogears-pc"),
          ("Description", "Xenogears PC port (reads the player's own disc)\n fixture: a second line")]


def control_of(fields: list[tuple[str, str]]) -> bytes:
    return ("".join(f"{k}: {v}\n" for k, v in fields)).encode()


def make_deb(path: Path, fields=FIELDS, kind: str = "gz", control: bool = True) -> bytes:
    ctl = tar_of({"./control": control_of(fields)} if control else {"./md5sums": b""})
    packed = gzip.compress(ctl, mtime=0) if kind == "gz" else lzma.compress(ctl)
    data = gzip.compress(tar_of({"./usr/share/xenogears-pc/release.txt": b"version=2026.12.0\nkind=deb\n"}), mtime=0)
    deb = ar_of([("debian-binary", b"2.0\n"), (f"control.tar.{kind}", packed), ("data.tar.gz", data)])
    path.write_bytes(deb)
    return deb


def expected_packages(deb: bytes, fields=FIELDS) -> bytes:
    get = dict(fields)
    lines = [f"{k}: {v}" for k, v in fields if k != "Description"]
    lines += [f"Filename: xenogears-pc_{get['Version']}_amd64.deb", f"Size: {len(deb)}",
              f"SHA256: {hashlib.sha256(deb).hexdigest()}", f"SHA512: {hashlib.sha512(deb).hexdigest()}",
              f"Description: {get['Description']}"]
    return ("\n".join(lines) + "\n").encode()


def expected_release(packages: bytes, version: str) -> bytes:
    lines = ["Origin: xenogears-pc", "Label: xenogears-pc", f"Version: {version}",
             f"Date: {formatdate(DATE, usegmt=True)}", "Architectures: amd64", "Acquire-By-Hash: no",
             "Description: the latest release of xenogears-pc (https://github.com/EtherAura/xenogears-pc)",
             "SHA256:", f" {hashlib.sha256(packages).hexdigest()} {len(packages)} Packages",
             "SHA512:", f" {hashlib.sha512(packages).hexdigest()} {len(packages)} Packages"]
    return ("\n".join(lines) + "\n").encode()


# ---- the tests -------------------------------------------------------------------------------------

def public_layout(work: Path) -> Path:
    public = work / "public tree"
    (public / "packaging").mkdir(parents=True)
    shutil.copy2(TOOL, public / "packaging/apt_repo.py")
    tree = ast.parse(TOOL.read_text())
    mods = {a.name.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
    mods |= {n.module.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.ImportFrom) and n.module}
    foreign = sorted(m for m in mods if m not in sys.stdlib_module_names and m != "__future__")
    check(not foreign, f"apt_repo.py imports only the standard library ({foreign or 'yes'})")
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    for path in (TOOL, Path(__file__)):
        flagged = [l for l in path.read_text().splitlines()
                   if publish.LEFTOVER_DATE.search(l) or publish.TIME_OF_DAY.search(l) or publish.FORBIDDEN.search(l)]
        check(not flagged, f"{path.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    return public


def make_tests(public: Path, work: Path, elsewhere: Path) -> None:
    r = run(public, "names", cwd=elsewhere)
    check(r.returncode == 0 and r.stdout.split() == ["Packages", "Release", "InRelease",
                                                     "xenogears-pc-archive-keyring.asc"],
          "names: the repository's four assets")
    for kind in ("gz", "xz"):
        out = work / f"out-{kind}"
        out.mkdir()
        deb = make_deb(out / "xenogears-pc_2026.12.0_amd64.deb", kind=kind)
        r = run(public, "make", "--deb", out / "xenogears-pc_2026.12.0_amd64.deb", "--out", out, "--date", DATE,
                cwd=elsewhere)
        packages = (out / "Packages").read_bytes() if (out / "Packages").exists() else b""
        check(r.returncode == 0 and packages == expected_packages(deb),
              f"make (control.tar.{kind}): Packages is the control fields, then the file's name, size and hashes, "
              f"the description last")
        check((out / "Release").exists() and (out / "Release").read_bytes() == expected_release(packages, "2026.12.0"),
              f"make (control.tar.{kind}): Release lists Packages's size and hashes, the version and --date")
        r = run(public, "make", "--deb", out / "xenogears-pc_2026.12.0_amd64.deb", "--out", out, "--date", DATE)
        check(r.returncode == 1 and "already exists" in r.stderr and (out / "Packages").read_bytes() == packages,
              f"make (control.tar.{kind}): an existing Packages is not overwritten")

    def refused(what: str, name: str, fields=FIELDS, needle: str = "", **kw) -> None:
        out = work / f"refused {len(list(work.iterdir()))}"
        out.mkdir()
        make_deb(out / name, fields=fields, **kw)
        r = run(public, "make", "--deb", out / name, "--out", out, "--date", DATE)
        written = sorted(p.name for p in out.iterdir() if p.name != name)
        check(r.returncode == 1 and needle in r.stderr and not written,
              f"refused, nothing written: {what} ({r.stderr.strip()[:90]})")

    other = lambda k, v: [(a, v if a == k else b) for a, b in FIELDS]  # noqa: E731
    refused("a file name that is not the package's", "xenogears-pc_2026.12.0_amd64-copy.deb", needle="not named")
    refused("another package", "other_2026.12.0_amd64.deb", fields=other("Package", "other"), needle="not xenogears-pc")
    refused("another architecture", "xenogears-pc_2026.12.0_arm64.deb", fields=other("Architecture", "arm64"),
            needle="not xenogears-pc")
    refused("a version that is not a release's", "xenogears-pc_2026.12.00_amd64.deb",
            fields=other("Version", "2026.12.00"), needle="not xenogears-pc")
    refused("a control that has Filename already", "xenogears-pc_2026.12.0_amd64.deb",
            fields=FIELDS + [("Filename", "x.deb")], needle="only an index has")
    refused("no control file", "xenogears-pc_2026.12.0_amd64.deb", control=False, needle="no control file")
    junk = work / "junk"
    junk.mkdir()
    (junk / "xenogears-pc_2026.12.0_amd64.deb").write_bytes(b"not a package at all\n")
    r = run(public, "make", "--deb", junk / "xenogears-pc_2026.12.0_amd64.deb", "--out", junk)
    check(r.returncode == 1 and "no ar header" in r.stderr and len(list(junk.iterdir())) == 1,
          "refused, nothing written: not a Debian package")
    link = work / "link"
    link.mkdir()
    make_deb(link / "real.deb")
    (link / "xenogears-pc_2026.12.0_amd64.deb").symlink_to(link / "real.deb")
    r = run(public, "make", "--deb", link / "xenogears-pc_2026.12.0_amd64.deb", "--out", link)
    check(r.returncode == 1 and "not a regular file" in r.stderr and len(list(link.iterdir())) == 2,
          "refused, nothing written: a package that is a link")


def gpg_tests(public: Path, work: Path) -> None:
    home, other = work / "gnupg", work / "gnupg-other"
    r = run(public, "keygen", "--gnupg-home", home)
    check(r.returncode == 1 and "XG_APT_KEY_PASSPHRASE" in r.stderr, "keygen: refused without the passphrase")
    r = run(public, "keygen", "--gnupg-home", home, env=PASS)
    fpr = r.stdout.split()[-1] if r.returncode == 0 else ""
    check(r.returncode == 0 and len(fpr) == 40, f"keygen: one key, fingerprint {fpr}")
    r = run(public, "keygen", "--gnupg-home", home, env=PASS)
    check(r.returncode == 1 and "not empty" in r.stderr, "keygen: refused into a home that holds anything")
    r = run(public, "keygen", "--gnupg-home", other, env=PASS)
    other_fpr = r.stdout.split()[-1] if r.returncode == 0 else ""
    out = work / "signed"
    out.mkdir()
    make_deb(out / "xenogears-pc_2026.12.0_amd64.deb")
    run(public, "make", "--deb", out / "xenogears-pc_2026.12.0_amd64.deb", "--out", out, "--date", DATE)
    key = out / "xenogears-pc-archive-keyring.asc"
    r = run(public, "public", "--gnupg-home", home, "--key", fpr, "--out", key)
    check(r.returncode == 0 and key.read_text().startswith("-----BEGIN PGP PUBLIC KEY BLOCK-----"),
          "public: the key's public half, armoured")
    r = run(public, "sign", "--release", out / "Release", "--gnupg-home", home, "--key", fpr)
    check(r.returncode == 1 and "XG_APT_KEY_PASSPHRASE" in r.stderr and not (out / "InRelease").exists(),
          "sign: refused without the passphrase, nothing written")
    r = run(public, "sign", "--release", out / "Release", "--gnupg-home", home, "--key", fpr, env=PASS)
    signed = (out / "InRelease").read_bytes() if (out / "InRelease").exists() else b""
    check(r.returncode == 0 and signed.startswith(b"-----BEGIN PGP SIGNED MESSAGE-----"), "sign: InRelease written")
    r = run(public, "sign", "--release", out / "Release", "--gnupg-home", home, "--key", fpr, env=PASS)
    check(r.returncode == 1 and (out / "InRelease").read_bytes() == signed, "sign: InRelease is not overwritten")

    def verify(inrelease: Path, release: Path, fingerprint: str) -> subprocess.CompletedProcess:
        return run(public, "verify", "--inrelease", inrelease, "--release", release, "--pubkey", key,
                   "--fingerprint", fingerprint)

    r = verify(out / "InRelease", out / "Release", fpr)
    check(r.returncode == 0 and fpr in r.stdout, "verify: the key's own signature over Release")
    r = verify(out / "InRelease", out / "Release", other_fpr)
    check(r.returncode == 1 and "not " + other_fpr in r.stderr, "verify: refused for another fingerprint")
    changed = work / "changed"
    changed.mkdir()
    (changed / "Release").write_bytes((out / "Release").read_bytes())
    (changed / "InRelease").write_bytes(signed.replace(b"Version: 2026.12.0", b"Version: 2026.12.9"))
    r = verify(changed / "InRelease", changed / "Release", fpr)
    check(r.returncode == 1 and "not validly signed" in r.stderr, "verify: refused for a changed InRelease")
    (changed / "Release").write_bytes((out / "Release").read_bytes() + b"Extra: line\n")
    r = verify(out / "InRelease", changed / "Release", fpr)
    check(r.returncode == 1 and "is not Release" in r.stderr, "verify: refused for the InRelease of another Release")
    foreign = work / "foreign"
    foreign.mkdir()
    shutil.copy2(out / "Release", foreign / "Release")
    run(public, "sign", "--release", foreign / "Release", "--gnupg-home", other, "--key", other_fpr, env=PASS)
    r = verify(foreign / "InRelease", foreign / "Release", fpr)
    check(r.returncode == 1 and "not validly signed" in r.stderr, "verify: refused for another key's signature")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="xg apt repo ") as tmp:
        work = Path(tmp)
        elsewhere = work / "another folder"
        elsewhere.mkdir()
        public = public_layout(work)
        make_tests(public, work, elsewhere)
        if shutil.which("gpg"):
            gpg_tests(public, work)
        else:
            print("LIMITATION  no gpg on PATH: keygen, sign and verify not run")
    if FAILURES:
        print(f"test_apt_repo: {len(FAILURES)} check(s) failed")
        return 1
    print("test_apt_repo: all checks pass (fixture packages and throwaway keys only: no release, no real key)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
