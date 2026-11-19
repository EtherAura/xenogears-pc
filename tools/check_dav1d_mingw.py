#!/usr/bin/env python3
"""Regression for tools/build_dav1d_mingw.sh: which dav1d source tarball it picks.

    python tools/check_dav1d_mingw.py --ver us|jp

HELPER-SELECTION PROOF ONLY: this is not a dav1d build and not a Windows
compilation. The production helper runs in a throwaway project layout under
build/ with stub meson, ninja, nasm and x86_64-w64-mingw32-gcc first in PATH
and small synthetic tarballs (a marker file in a dav1d-x.y.z/ directory, no
dav1d code). The stubs record the extracted tree and fake the installed
library, so the helper's own source selection, stamp and missing-source exit
are what is checked. The version comes from tools/deps.json (xg-4oq.10): the
helper asks tools/deps.py for the pinned tarball, which is used only when its
SHA-256 is the pinned one:
  - default: the pinned tarball in tools/toolchain-src; a file of the same
    name with another sum in the cache is passed over;
  - fallback: the pinned tarball in <cache> when there is no project copy;
  - the pinned name with a wrong sum everywhere: exit 0 with a note, no build;
  - DAV1D_TARBALL over the defaults, a positional argument over both;
  - no tarball (or a missing positional one): exit 0 with a note, no build;
  - a second run is up to date (the stamp), also with the tarball gone;
  - the pin is dav1d 1.5.5 and no Windows helper script names 1.5.4, or any
    dav1d version of its own.
The helper is copied verbatim into the layout (its root comes from its own
path) beside a copy of tools/deps.py whose one change is its absolute cache
directory, /var/cache/distfiles, pointed at the layout's own, so the host's
cache is never read; each layout's tools/deps.json pins its own synthetic
tarball. dav1d is version-independent; --ver only labels the run (US and JP
share the helper).
"""

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELPER = ROOT / "tools" / "build_dav1d_mingw.sh"
WINDOWS = ROOT / "tools" / "build_windows.sh"
DEPS = ROOT / "tools" / "deps.py"
MANIFEST = ROOT / "tools" / "deps.json"
CACHE = "/var/cache/distfiles"
WANT = "dav1d-1.5.5.tar.xz"
OLD = "1.5.4"

STUB_MESON = """#!/bin/sh
# stub meson: record the source tree's marker (setup BUILD SRC ...)
cat "$3/marker" >> "$XG_STUB_LOG"
"""
STUB_NINJA = """#!/bin/sh
# stub ninja: "install" fakes the installed static library
for a in "$@"; do
    if [ "$a" = install ]; then
        mkdir -p "$XG_STUB_PREFIX/lib" "$XG_STUB_PREFIX/include/dav1d"
        : > "$XG_STUB_PREFIX/lib/libdav1d.a"
    fi
done
"""
STUB_TRUE = "#!/bin/sh\nexit 0\n"


def tarball(path, version, label):
    """A synthetic dav1d-<version>/marker tarball; label makes each distinct."""
    path.parent.mkdir(parents=True, exist_ok=True)
    data = (label + "\n").encode()
    with tarfile.open(path, "w:xz") as tar:
        d = tarfile.TarInfo(f"dav1d-{version}")
        d.type = tarfile.DIRTYPE
        d.mode = 0o755
        tar.addfile(d)
        f = tarfile.TarInfo(f"dav1d-{version}/marker")
        f.size = len(data)
        f.mode = 0o644
        tar.addfile(f, io.BytesIO(data))
    return path


class Layout:
    def __init__(self, base, name):
        self.root = base / name
        self.cache = self.root / "cache"
        self.local = self.root / "tools" / "toolchain-src"
        self.prefix = self.root / "tools" / "toolchain" / "mingw64"
        self.stubs = self.root / "stubs"
        self.log = self.root / "stub.log"
        for d in (self.cache, self.local, self.prefix, self.stubs):
            d.mkdir(parents=True)
        deps = DEPS.read_text()
        if f'"{CACHE}"' not in deps:
            raise SystemExit(f"{DEPS}: no {CACHE} fallback to redirect")
        (self.root / "tools" / "deps.py").write_text(deps.replace(f'"{CACHE}"', f'"{self.cache}"'))
        self.helper = self.root / "tools" / "build_dav1d_mingw.sh"
        self.helper.write_text(HELPER.read_text())
        self.helper.chmod(0o755)
        for name, body in (("meson", STUB_MESON), ("ninja", STUB_NINJA),
                           ("nasm", STUB_TRUE),
                           ("x86_64-w64-mingw32-gcc", STUB_TRUE)):
            p = self.stubs / name
            p.write_text(body)
            p.chmod(p.stat().st_mode | stat.S_IXUSR)

    def pin(self, path):
        """This layout's tools/deps.json: dav1d pinned to path's bytes."""
        (self.root / "tools" / "deps.json").write_text(json.dumps({"dav1d": {
            "version": "1.5.5", "file": WANT, "url": f"https://example.invalid/{WANT}", "sha256": sha(path)}}))
        return path

    def run(self, *args, env_tarball=None):
        env = {k: v for k, v in os.environ.items()
               if k not in ("DAV1D_TARBALL", "JOBS", "XG_DEPS_CACHE")}
        env["PATH"] = f"{self.stubs}{os.pathsep}{env.get('PATH', '')}"
        env["XG_STUB_LOG"] = str(self.log)
        env["XG_STUB_PREFIX"] = str(self.prefix)
        env["JOBS"] = "1"
        if env_tarball is not None:
            env["DAV1D_TARBALL"] = str(env_tarball)
        r = subprocess.run(["bash", str(self.helper), *map(str, args)],
                           env=env, cwd=self.root, capture_output=True,
                           text=True, timeout=120)
        return r

    def built(self):
        return self.log.read_text().split() if self.log.exists() else []

    def stamp(self):
        s = self.prefix / ".stamp-dav1d"
        return s.read_text().strip() if s.exists() else None


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ver", choices=("us", "jp"), required=True)
    ap.add_argument("--keep", action="store_true", help="keep the scratch layout")
    a = ap.parse_args()

    fails = []

    def check(name, ok, detail=""):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}" + (f": {detail}" if detail and not ok else ""))
        if not ok:
            fails.append(name)

    print(f"dav1d MinGW helper, source selection ({a.ver}; stubbed tools, no real build)")
    for script in (HELPER, WINDOWS):
        lines = [f"{script.name}:{i}" for i, l in enumerate(script.read_text().splitlines(), 1)
                 if OLD in l]
        check(f"no dav1d {OLD} reference in {script.name}", not lines, ", ".join(lines))
    pinned = json.loads(MANIFEST.read_text())["dav1d"]
    check(f"tools/deps.json pins {WANT}", pinned["file"] == WANT and pinned["version"] == "1.5.5",
          f"{pinned['file']} {pinned['version']}")
    own = sorted(set(re.findall(r"dav1d-\d[\d.]*", HELPER.read_text() + WINDOWS.read_text())))
    check("no Windows helper script names a dav1d version of its own", not own, ", ".join(own))

    (ROOT / "build").mkdir(exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix=f"check_dav1d_mingw-{a.ver}-", dir=ROOT / "build"))
    try:
        def selected(lay, r, label, path):
            want_stamp = f"{path.name} {sha(path)}"
            return (r.returncode == 0 and lay.built() == [label]
                    and lay.stamp() == want_stamp
                    and f"Extracting {path.name}" in r.stdout), \
                f"exit {r.returncode}, built {lay.built()}, stamp {lay.stamp()!r}, " \
                f"want {want_stamp!r}; {r.stdout.strip()} {r.stderr.strip()}"

        # The pinned project copy; stale 1.5.4 decoys and a cache file of the same name with
        # another sum must lose.
        lay = Layout(base, "local")
        local = lay.pin(tarball(lay.local / WANT, "1.5.5", "local"))
        tarball(lay.cache / WANT, "1.5.5", "cache")
        tarball(lay.local / f"dav1d-{OLD}.tar.xz", OLD, "old-local")
        tarball(lay.cache / f"dav1d-{OLD}.tar.xz", OLD, "old-cache")
        r = lay.run()
        ok, why = selected(lay, r, "local", local)
        check(f"default: project-local {WANT}", ok, why)
        r = lay.run()
        check("same tarball again: up to date, no rebuild",
              r.returncode == 0 and "up to date" in r.stdout and lay.built() == ["local"],
              f"exit {r.returncode}, built {lay.built()}; {r.stdout.strip()}")
        local.unlink()
        (lay.cache / WANT).unlink()
        r = lay.run()
        check("the pinned build is up to date with its tarball gone",
              r.returncode == 0 and "up to date" in r.stdout and lay.built() == ["local"],
              f"exit {r.returncode}, built {lay.built()}; {r.stdout.strip()} {r.stderr.strip()}")

        lay = Layout(base, "wrong-sum")
        lay.pin(tarball(lay.root / "elsewhere" / WANT, "1.5.5", "pinned"))
        tarball(lay.local / WANT, "1.5.5", "local-other")
        tarball(lay.cache / WANT, "1.5.5", "cache-other")
        r = lay.run()
        check("the pinned name with another SHA-256 everywhere: exit 0, note, no build",
              r.returncode == 0 and "no pinned dav1d source tarball" in r.stdout and "another SHA-256" in r.stderr
              and not lay.built() and lay.stamp() is None,
              f"exit {r.returncode}, built {lay.built()}; {r.stdout.strip()} {r.stderr.strip()}")

        lay = Layout(base, "cache")
        cache = lay.pin(tarball(lay.cache / WANT, "1.5.5", "cache"))
        tarball(lay.cache / f"dav1d-{OLD}.tar.xz", OLD, "old-cache")
        r = lay.run()
        ok, why = selected(lay, r, "cache", cache)
        check(f"fallback: cache {WANT} without a project copy", ok, why)

        lay = Layout(base, "env")
        lay.pin(tarball(lay.local / WANT, "1.5.5", "local"))
        tarball(lay.cache / WANT, "1.5.5", "cache")
        env = tarball(lay.root / "custom" / "dav1d-9.9.9.tar.xz", "9.9.9", "env")
        r = lay.run(env_tarball=env)
        ok, why = selected(lay, r, "env", env)
        check("DAV1D_TARBALL over the defaults", ok, why)

        lay = Layout(base, "positional")
        lay.pin(tarball(lay.local / WANT, "1.5.5", "local"))
        env = tarball(lay.root / "custom" / "dav1d-9.9.9.tar.xz", "9.9.9", "env")
        pos = tarball(lay.root / "arg" / "dav1d-8.8.8.tar.xz", "8.8.8", "positional")
        r = lay.run(pos, env_tarball=env)
        ok, why = selected(lay, r, "positional", pos)
        check("positional argument over DAV1D_TARBALL", ok, why)

        lay = Layout(base, "missing-positional")
        lay.pin(tarball(lay.local / WANT, "1.5.5", "local"))
        env = tarball(lay.root / "custom" / "dav1d-9.9.9.tar.xz", "9.9.9", "env")
        gone = lay.root / "arg" / "dav1d-0.0.0.tar.xz"
        r = lay.run(gone, env_tarball=env)
        check("missing positional tarball: exit 0, note, no build",
              r.returncode == 0 and f"no dav1d source tarball at {gone}" in r.stdout
              and not lay.built() and lay.stamp() is None,
              f"exit {r.returncode}, built {lay.built()}; {r.stdout.strip()} {r.stderr.strip()}")

        lay = Layout(base, "missing")
        lay.pin(tarball(lay.root / "elsewhere" / WANT, "1.5.5", "pinned"))
        r = lay.run()
        check("no tarball anywhere: exit 0, a note that says how to fetch it, no build",
              r.returncode == 0 and "no pinned dav1d source tarball" in r.stdout
              and "deps.py fetch dav1d" in r.stderr and not lay.built() and lay.stamp() is None,
              f"exit {r.returncode}, built {lay.built()}; {r.stdout.strip()} {r.stderr.strip()}")
    finally:
        if a.keep:
            print(f"  scratch kept: {base}")
        else:
            shutil.rmtree(base, ignore_errors=True)

    if fails:
        print(f"FAILED: {len(fails)} check(s)")
        return 1
    print("all helper-selection checks passed (stubbed: not a dav1d build or Windows compilation)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
