#!/usr/bin/env python3
"""Fixture check of packaging/build-windows-zip.sh (tools/publish/repo-port),
the offline Windows ZIP and installer build of xenogears-pc (xg-4oq.3).

    python tools/check_build_windows_zip.py [--ver us|jp] [--keep]

The helper runs as the public tree would run it (bash packaging/build-windows-zip.sh
from another folder, every path with spaces in it), in a fixture public tree:
the real packaging/windows_release.py and tools/deps.py beside DOUBLES of the
disc extraction, configure.py, build_windows.sh (both engines and SDL3 as labelled fixture
bytes, their link maps, seals and cmake_install.cmake strip commands), the strip
it names, payloadcheck.py and makensis. Nothing here is a build, a disc or game
data. The real proof uses a player's disc, the pinned tarballs and NSIS.

Checked: the counter-zero tag and the exact asset names; a missing or corrupt
cache refused before anything is built, with no fetch; a bad seal, a staged
executable that is not the reproducible strip, and either engine's audit refusal
(of the linked executable or of the installed bytes) refused; an incomplete stage
or extraction refused; existing outputs kept; on each refusal nothing new in OUT
and no work folder left; on success the .zip holding exactly the stage's bytes,
its executables the audited bytes, no map or seal, and its .sha256. Installer
mode checks makensis before building, compiles from the audited stage, and
checks the installer checksum and compiler record before promoting assets.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "tools" / "publish" / "repo-port" / "packaging"
HELPER = "build-windows-zip.sh"
TAG, ZIP = "v2026.11.0", "xenogears-pc-2026.11.0-windows-x64.zip"
FOLDER = ZIP[:-4]
SETUP = FOLDER + "-setup.exe"
BUILD_INFO = SETUP + ".build.json"
LABEL = "FIXTURE DOUBLE for tools/check_build_windows_zip.py (not a build, not game data)"
MODULES = ["fixture_main", "fixture_one", "fixture_two"]  # the first an executable (iso:), as main is

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# The doubles. Each is labelled; each records what it was run with in $FIXTURE_LOG.
EXTRACT = f'''# {LABEL}: tools/extract.py
import json, os, sys
from pathlib import Path
root = Path(__file__).resolve().parent.parent
region, image = sys.argv[1], Path(sys.argv[sys.argv.index("--image") + 1])
assert "--check-image" in sys.argv and image.is_file()
with open(os.environ["FIXTURE_LOG"], "a") as log:
    log.write(json.dumps({{"extract": str(image)}}) + "\\n")
folder = root / "disc" / region
folder.mkdir(parents=True)
for name in {MODULES!r}:
    (folder / (name + (".exe" if name == "fixture_main" else ".bin"))).write_text("fixture module " + name)
if os.environ.get("FIXTURE_EXTRACT_EXTRA"):
    (folder / "unexpected.bin").write_text("fixture extra")
'''

CONFIGURE = f'''# {LABEL}: tools/configure.py
print("fixture configure", __import__("sys").argv[1:])
'''

BUILD_SH = f'''#!/usr/bin/env bash
# {LABEL}: tools/build_windows.sh
exec python3 -I "$(dirname "${{BASH_SOURCE[0]}}")/fixture_build.py" "$@"
'''

BUILD_PY = f'''# {LABEL}: what tools/build_windows.sh --dist does, in labelled bytes
import hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
tools = Path(__file__).resolve().parent
args = sys.argv[1:]
assert "--dist" in args and args[args.index("--engine") + 1] == "both"
stage = Path(args[-1])
env = os.environ
assert "SDL3_TARBALL" not in env  # the ZIP build uses the pinned cache even with a developer override
with open(env["FIXTURE_LOG"], "a") as log:
    log.write(json.dumps({{"build": args, "SOURCE_DATE_EPOCH": env.get("SOURCE_DATE_EPOCH"),
                          "XG_DEPS_CACHE": env.get("XG_DEPS_CACHE")}}) + "\\n")
region = "JP" if "--jp" in args else "US"
stage.mkdir(parents=True)
for target, name, build in (("original", "xenogears.exe", env["XG_WIN_BUILD"]),
                            ("enhanced", "xenogears-enhanced.exe", env["XG_WIN_BUILD_ENHANCED"]),
                            ("sdl3", "SDL3.dll", tools / "toolchain-src/sdl3-mingw/build")):
    if target == "enhanced" and env.get("FIXTURE_NO_ENHANCED"):
        continue
    build = Path(build)
    build.mkdir(parents=True)
    if target == "sdl3":
        cache = "CMAKE_PROJECT_NAME:STATIC=SDL3\\nSDL_SHARED:BOOL=ON\\n"
        destination = "bin"
    else:
        cache = f"XG_VERSION:STRING={{region}}\\nXG_ENGINE:STRING={{target}}\\nXG_DIST:BOOL=ON\\n"
        destination = "."
    (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\\n" + cache)
    exe = build / name
    exe.write_bytes(b"MZ fixture linked " + name.encode() + b"\\nSYMBOLS\\nfixture symbols of " + name.encode())
    (build / (name + ".map")).write_text("fixture map of " + name)
    def h(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    seal = {{"version": 1, "exe": h(exe), "map": h(build / (name + ".map"))}}
    if env.get("FIXTURE_BAD_SEAL") == target:
        seal["map"] = "0" * 64
    (build / (name + ".payload.json")).write_text(json.dumps(seal, sort_keys=True) + "\\n")
    strip = tools / "fixture_strip.py"
    (build / "cmake_install.cmake").write_text(
        'if(CMAKE_INSTALL_DO_STRIP)\\n  execute_process(COMMAND "' + str(strip) + '" "--fixture-flag" '
        '"$ENV{{DESTDIR}}${{CMAKE_INSTALL_PREFIX}}/' + destination + '/' + name + '")\\nendif()\\n')
    shutil.copy(exe, stage / name)
    subprocess.run([str(strip), "--fixture-flag", str(stage / name)], check=True)
    if env.get("FIXTURE_STAGE_DIFFERS") == target:
        with open(stage / name, "ab") as f:
            f.write(b"!")
    if target == "sdl3" and env.get("FIXTURE_NO_SDL_MAP"):
        (build / "SDL3.dll.map").unlink()
for n in ("COPYING.txt", "README.txt", "LICENSE-SDL3.txt", "LICENSE-imgui.txt", "LICENSE-Jukebox-font.txt",
          "LICENSE-glslang.txt", "LICENSE-dav1d.txt"):
    (stage / n).write_text("fixture " + n + "\\n")
if env.get("FIXTURE_STAGE_EXE"):
    (stage / "LICENSE-dav1d.txt").write_bytes(b"MZ fixture executable under a licence's name")
'''

STRIP = f'''#!/usr/bin/env python3
# {LABEL}: the strip cmake_install.cmake names (drops the fixture symbol section)
import os, sys
assert sys.argv[1] == "--fixture-flag" and os.environ.get("SOURCE_DATE_EPOCH") == "0"
path = sys.argv[2]
data = open(path, "rb").read()
open(path, "wb").write(data.split(b"\\nSYMBOLS\\n")[0] + b"\\nstripped")
'''

PAYLOAD = f'''# {LABEL}: tools/payloadcheck.py (--record-map, --dist)
import hashlib, json, os, sys
from pathlib import Path
root = Path(__file__).resolve().parent.parent
def h(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
if sys.argv[1] == "--record-map":
    exe = Path(sys.argv[2])
    Path(str(exe) + ".payload.json").write_text(json.dumps({{"version": 1, "exe": h(exe), "map": h(str(exe) + ".map")}},
                                                           sort_keys=True) + "\\n")
    sys.exit(0)
a = sys.argv
exe, ver = Path(a[a.index("--exe") + 1]), a[a.index("--ver") + 1]
assert "--dist" in a and Path(a[a.index("--build") + 1]).is_dir()
assert ("--sdl3" in a) == (exe.name == "SDL3.dll")
with open(os.environ["FIXTURE_LOG"], "a") as log:
    log.write(json.dumps({{"audit": exe.name, "stripped": exe.read_bytes().endswith(b"stripped")}}) + "\\n")
seal = json.loads(Path(str(exe) + ".payload.json").read_text())
if seal != {{"version": 1, "exe": h(exe), "map": h(str(exe) + ".map")}}:
    print("payload: attribution error: the seal does not match")
    sys.exit(1)
n = len(list((root / "disc" / ver).iterdir()))
print(f"retail module data in {{exe}} ({{ver.upper()}}): {{n}} module images, runs of >= 16 bytes")
fail = os.environ.get("FIXTURE_AUDIT_FAIL", "")
if fail == exe.name + (":installed" if exe.read_bytes().endswith(b"stripped") else ":linked"):
    print("  src         64 bytes     1 runs  src/fixture.c")
    print("payload FAIL: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; allowed 0/0, "
          "symbol names 0/0, src (game C's own data) 1/64")
    sys.exit(1)
print("payload ok: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; allowed 2/40, "
      "symbol names 0/0, src (game C's own data) 0/0")
'''


MAKENSIS = f'''#!/usr/bin/env python3
# {LABEL}: makensis
import hashlib, json, os, re, sys
from pathlib import Path
log = Path(os.environ["FIXTURE_LOG"])
def record(event):
    with log.open("a") as stream:
        stream.write(json.dumps(event) + "\\n")
if sys.argv[1:] == ["-VERSION"]:
    version = os.environ.get("FIXTURE_NSIS_VERSION", "v3.10")
    if os.environ.get("FIXTURE_NSIS_DRIFT") and '"nsis": "version"' in log.read_text():
        version = "v3.11"
    record({{"nsis": "version", "version": version}})
    print(version)
    sys.exit(0)
assert len(sys.argv) == 3 and sys.argv[1] == "-V2"
script = Path(sys.argv[2])
text = script.read_text()
sources = [script.parent / rel for rel in re.findall(r'^  File "([^"]+)"$', text, re.M)]
files = {{path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}}
record({{"nsis": "compile", "files": files, "read_only": all(
    not (path.stat().st_mode & 0o222 or path.parent.stat().st_mode & 0o222) for path in sources)}})
name = re.search(r'^OutFile "([^"]+)"$', text, re.M).group(1)
output = script.with_name(name)
output.write_bytes(b"MZ fixture installer\\n" + json.dumps(files, sort_keys=True).encode())
if os.environ.get("FIXTURE_NSIS_FAIL"):
    sys.exit("fixture NSIS compile failure")
'''


def make_public(base: Path, region: str) -> tuple[Path, Path, Path, dict]:
    """The fixture public tree, a disc folder and a cache folder, all with spaces in their paths."""
    public = base / "public tree"
    (public / "packaging").mkdir(parents=True)
    for name in (HELPER, "release_inputs.py", "windows_release.py", "xenogears-pc-installer.nsi.in"):
        shutil.copy2(OVERLAY / name, public / "packaging" / name)
    tools = public / "tools"
    tools.mkdir()
    shutil.copy2(ROOT / "tools" / "deps.py", tools / "deps.py")
    cache = base / "dependency cache"
    cache.mkdir()
    pins = {}
    for name, file in (("sdl3", "fixture-sdl3.tar.gz"), ("glslang", "fixture-glslang.tar.gz"),
                       ("dav1d", "fixture-dav1d.tar.xz")):
        data = f"{LABEL}: tarball {name}\n".encode()
        (cache / file).write_bytes(data)
        pins[name] = {"version": "0.fixture", "file": file, "url": "https://fixture.invalid/never-fetched",
                      "sha256": sha(data), "for": "fixture"}
    (tools / "deps.json").write_text(json.dumps(pins, indent=2) + "\n")
    for name, text in (("extract.py", EXTRACT), ("configure.py", CONFIGURE), ("build_windows.sh", BUILD_SH),
                       ("fixture_build.py", BUILD_PY), ("fixture_strip.py", STRIP), ("payloadcheck.py", PAYLOAD)):
        (tools / name).write_text(text)
    (tools / "fixture_strip.py").chmod(0o755)
    disc_name = "Fixture Disc (Disc 1)"
    entries = "\n".join(f"  - {{name: {m}, {'iso: X' if m == 'fixture_main' else 'lba: 1'}}}" for m in MODULES)
    (public / "config" / region).mkdir(parents=True)
    (public / "config" / region / "disc.yaml").write_text(
        f"# {LABEL}\nimage: bin/{disc_name}/{disc_name}.bin\nmodules:\n{entries}\n")
    discs = base / "player discs"
    (discs / disc_name).mkdir(parents=True)
    (discs / disc_name / f"{disc_name}.bin").write_text(f"{LABEL}: disc image\n")
    return public, discs, cache, pins


def expected_stage() -> dict[str, bytes]:
    out = {}
    for name in ("xenogears.exe", "xenogears-enhanced.exe"):
        out[name] = b"MZ fixture linked " + name.encode() + b"\nstripped"
    out["SDL3.dll"] = b"MZ fixture linked SDL3.dll\nstripped"
    for n in ("COPYING.txt", "README.txt", "LICENSE-SDL3.txt", "LICENSE-imgui.txt", "LICENSE-Jukebox-font.txt",
              "LICENSE-glslang.txt", "LICENSE-dav1d.txt"):
        out[n] = f"fixture {n}\n".encode()
    return out


class Run:
    def __init__(self, base: Path, public: Path, discs: Path, cache: Path, region: str):
        self.base, self.public, self.discs, self.cache, self.region = base, public, discs, cache, region
        self.cwd = base / "another folder"
        self.cwd.mkdir(exist_ok=True)
        self.scratch = base / "scratch space"
        self.scratch.mkdir(exist_ok=True)
        self.log = base / "fixture log.jsonl"
        self.compiler = base / "NSIS compiler" / "makensis"
        self.compiler.parent.mkdir()
        self.compiler.write_text(MAKENSIS)
        self.compiler.chmod(0o755)
        self.installer = ("--installer", "--makensis", self.compiler)

    def __call__(self, out: Path, tag: str = TAG, cache: Path | None = None,
                 extra: tuple[str | Path, ...] = (), **knobs) -> subprocess.CompletedProcess:
        self.log.unlink(missing_ok=True)
        self.log.touch()
        env = {k: v for k, v in os.environ.items() if not k.startswith(("FIXTURE_", "XG_", "SOURCE_DATE"))}
        env.update({"FIXTURE_LOG": str(self.log), "TMPDIR": str(self.scratch)})
        env.update({k: str(v) for k, v in knobs.items()})
        argv = ["bash", str(self.public / "packaging" / HELPER), "--discs", str(self.discs),
                "--cache", str(cache or self.cache), "--version", tag, "--out", str(out), "--region", self.region,
                "--scratch", str(self.scratch), *map(str, extra)]
        return subprocess.run(argv, cwd=self.cwd, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                              text=True, timeout=300)

    def logged(self) -> list[dict]:
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def leftovers(self) -> list[str]:
        return sorted(p.name for p in self.scratch.iterdir())


def refused(run: Run, what: str, expect: str, out: Path | None = None, built: bool | None = None,
            compiled: bool | None = None, **kw) -> None:
    out = out or run.base / f"out {what}"
    before = sorted(os.listdir(out)) if out.exists() else None
    result = run(out, **kw)
    said = [line for line in result.stderr.splitlines() if line.startswith("build-windows-zip: ")]
    last = said[-1] if said else result.stderr.strip()[-200:]
    after = sorted(os.listdir(out)) if out.exists() else None
    logged = run.logged()
    # The refusal is the helper's one "build-windows-zip: " message, which may quote the
    # refusing tool's own lines after it.
    message = result.stderr[result.stderr.rfind("build-windows-zip: "):] if said else ""
    ok = (result.returncode == 1 and last.startswith("build-windows-zip: ") and expect in message
          and after == before and not run.leftovers())
    if built is not None:
        ok = ok and any("build" in e for e in logged) == built
    if compiled is not None:
        ok = ok and any(e.get("nsis") == "compile" for e in logged) == compiled
    check(ok, f"refused: {what} (exit {result.returncode}, {last[:160]!r}, OUT {after}, "
              f"scratch {run.leftovers()})")


def success(run: Run) -> None:
    out = run.base / "out dir" / "release assets"
    result = run(out, SDL3_TARBALL=run.base / "unpinned SDL3.tar.gz")
    if not check(result.returncode == 0, f"helper exit 0 on the fixture ({result.stderr.strip()[-300:]!r})"):
        return
    check(sorted(os.listdir(out)) == sorted([ZIP, ZIP + ".sha256"]),
          f"OUT holds exactly {ZIP} and its .sha256 (counter-zero tag {TAG})")
    logged = run.logged()
    builds = [e for e in logged if "build" in e]
    check(len(builds) == 1 and builds[0]["SOURCE_DATE_EPOCH"] == "0"
          and builds[0]["XG_DEPS_CACHE"] == str(run.cache.resolve()),
          "the builder ran once with SOURCE_DATE_EPOCH=0 and the verified cache as XG_DEPS_CACHE")
    audits = [(e["audit"], e["stripped"]) for e in logged if "audit" in e]
    check(audits == [("xenogears.exe", False), ("xenogears.exe", True),
                     ("xenogears-enhanced.exe", False), ("xenogears-enhanced.exe", True),
                     ("SDL3.dll", False), ("SDL3.dll", True)],
          f"both engines and SDL3 audited: the linked image, then the installed bytes ({audits})")
    want = expected_stage()
    data = (out / ZIP).read_bytes()
    with zipfile.ZipFile(out / ZIP) as z:
        names = z.namelist()
        check(names == [f"{FOLDER}/"] + [f"{FOLDER}/{n}" for n in sorted(want)] + [f"{FOLDER}/release.txt"],
              f"{ZIP}: entries are {FOLDER}/, the stage's {len(want)} files and its release.txt")
        got = {n[len(FOLDER) + 1:]: z.read(n) for n in names if not n.endswith("/")}
    release = got.pop("release.txt", None)
    check(release == f"version={FOLDER.split('-')[2]}\nkind=zip\n".encode(),
          f"{ZIP}: release.txt names its version and kind zip (the in-app updater's): {release!r}")
    check(got == want, f"{ZIP}: every entry holds the expected stage bytes (the executables the stripped, "
                       f"audited bytes)")
    check(not any(n.endswith((".map", ".payload.json")) for n in names), f"{ZIP}: no link map or seal")
    check((out / (ZIP + ".sha256")).read_text() == f"{sha(data)}  {ZIP}\n", f"{ZIP}.sha256 is the .zip's SHA-256")
    if shutil.which("sha256sum"):
        check(subprocess.run(["sha256sum", "-c", ZIP + ".sha256"], cwd=out, capture_output=True).returncode == 0,
              "sha256sum -c accepts it")
    check(not run.leftovers(), "the work folder is removed")
    check(not (run.public / "disc").exists() and not (run.public / "build").exists(),
          "the public tree itself is untouched (extraction and builds in the private copy)")


def installer_checks(run: Run) -> None:
    out = run.base / "Windows release bundle"
    result = run(out, extra=run.installer)
    if check(result.returncode == 0, f"installer build exits 0 ({result.stderr.strip()[-300:]!r})"):
        check({p.name for p in out.iterdir()} == {ZIP, ZIP + ".sha256", SETUP, SETUP + ".sha256", BUILD_INFO},
              "the bundle has the installer, ZIP, both checksums and the compiler record")
        events = run.logged()
        compiled = [i for i, e in enumerate(events) if e.get("nsis") == "compile"]
        audits = [i for i, e in enumerate(events) if "audit" in e]
        check(len(compiled) == 1 and len(audits) == 6 and max(audits) < compiled[0],
              "makensis compiles once after both audits of all three binaries")
        check(events[0] == {"nsis": "version", "version": "v3.10"}, "makensis is checked before extraction")
        if compiled:
            expected = {name: sha(data) for name, data in expected_stage().items()}
            check(events[compiled[0]] == {"nsis": "compile", "files": expected, "read_only": True},
                  "the rendered installer reads the exact audited, read-only stage")
        check((out / ZIP).read_bytes() == (run.base / "out dir" / "release assets" / ZIP).read_bytes(),
              "installer mode produces the same ZIP as the portable build")
        check(json.loads((out / BUILD_INFO).read_text()) == {
            "asset": SETUP, "makensis_minimum": "3.10", "makensis_version": "v3.10"},
            "the installer build record names the compiler and minimum version")
        for name in (SETUP, ZIP):
            check((out / (name + ".sha256")).read_text() == f"{sha((out / name).read_bytes())}  {name}\n",
                  f"{name} matches its checksum")
        check(not run.leftovers(), "the installer work folder is removed")
    refused(run, "a compiler without installer mode", "--makensis requires --installer",
            extra=("--makensis", run.compiler), built=False, compiled=False)
    refused(run, "an absent installer compiler", "no makensis",
            extra=("--installer", "--makensis", run.base / "missing makensis"), built=False, compiled=False)
    refused(run, "makensis 3.09", "older than 3.10", extra=run.installer, FIXTURE_NSIS_VERSION="v3.09",
            built=False, compiled=False)
    existing = run.base / "out existing installer"
    existing.mkdir()
    (existing / BUILD_INFO).write_text("kept\n")
    refused(run, "OUT already holds the installer build record", "already holds", out=existing, extra=run.installer,
            built=False, compiled=False)
    check((existing / BUILD_INFO).read_text() == "kept\n", "the existing installer build record is preserved")
    refused(run, "the SDL3 installed audit fails in installer mode", "FAILED", extra=run.installer,
            FIXTURE_AUDIT_FAIL="SDL3.dll:installed", built=True, compiled=False)
    refused(run, "makensis fails after starting the compile", "package failed", extra=run.installer,
            FIXTURE_NSIS_FAIL=1, built=True, compiled=True)
    refused(run, "makensis version changes after preflight", "expected the preflight makensis version",
            extra=run.installer, FIXTURE_NSIS_DRIFT=1, built=True, compiled=True)


def refusals(run: Run, pins: dict) -> None:
    refused(run, "a tag not vYYYY.MM.N", "release tag", tag="v2026.13.0", built=False)
    # The cache: a missing and a corrupt tarball, refused before any build and with no fetch.
    sdl3 = run.cache / pins["sdl3"]["file"]
    kept = sdl3.read_bytes()
    sdl3.unlink()
    refused(run, "a tarball missing from the cache", "nothing fetched", built=False)
    sdl3.write_bytes(kept + b"corrupt")
    refused(run, "a corrupt tarball in the cache", "nothing fetched", built=False)
    sdl3.write_bytes(kept)
    check(sorted(p.name for p in run.cache.iterdir()) == sorted(p["file"] for p in pins.values()),
          "deps.py fetched nothing into the cache")
    refused(run, "no cache folder", "not a folder", cache=run.base / "no such cache", built=False)
    # Existing outputs are never overwritten.
    out = run.base / "out existing"
    out.mkdir()
    (out / ZIP).write_bytes(b"an existing asset, kept")
    refused(run, "OUT already holds the .zip", "already holds", out=out, built=False)
    check((out / ZIP).read_bytes() == b"an existing asset, kept", "the existing .zip is kept as it was")
    out2 = run.base / "out existing sidecar"
    out2.mkdir()
    (out2 / (ZIP + ".sha256")).write_text("kept\n")
    refused(run, "OUT already holds the .sha256", "already holds", out=out2, built=False)
    check((out2 / (ZIP + ".sha256")).read_text() == "kept\n", "the existing .sha256 is kept as it was")
    # Provenance, byte equality and the audits, for each executable.
    for target, name in (("original", "xenogears.exe"), ("enhanced", "xenogears-enhanced.exe"), ("sdl3", "SDL3.dll")):
        refused(run, f"{target}: a bad original seal", "do not match their seal", FIXTURE_BAD_SEAL=target)
        refused(run, f"{target}: the staged PE is not the reproducible strip", "not the reproducible strip",
                FIXTURE_STAGE_DIFFERS=target)
        refused(run, f"{target}: the linked executable's audit refuses", "FAILED", FIXTURE_AUDIT_FAIL=f"{name}:linked")
        refused(run, f"{target}: the installed bytes' audit refuses", "FAILED",
                FIXTURE_AUDIT_FAIL=f"{name}:installed")
    refused(run, "SDL3: no linker map", "no SDL3.dll.map", FIXTURE_NO_SDL_MAP=1)
    refused(run, "a stage without the Enhanced engine", "no xenogears-enhanced.exe", FIXTURE_NO_ENHANCED=1)
    refused(run, "an unexpected executable in the stage", "unexpected executable", FIXTURE_STAGE_EXE=1)
    refused(run, "an extraction with an unexpected file", "extraction incomplete or unexpected",
            FIXTURE_EXTRACT_EXTRA=1, built=False)


def scan(helper: Path) -> None:
    check(subprocess.run(["bash", "-n", str(helper)], capture_output=True).returncode == 0, f"bash -n {helper.name}")
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    flagged = [line for line in helper.read_text().splitlines()
               if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line) or publish.FORBIDDEN.search(line)]
    check(not flagged, f"{helper.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    found = runs_or_fetches(helper.read_text())
    check(not found, f"{helper.name} fetches nothing and runs no build-release.sh ({found or 'none'})")
    controls = ['bash "$ROOT/build-release.sh"', "curl -fsSLo x.tar.gz https://example.org/x.tar.gz",
                "python3 tools/deps.py fetch sdl3", 'die "no cache"; wget "$URL"']
    check(all(runs_or_fetches(line) for line in controls), "the scan catches a run of build-release.sh and a fetch")


def runs_or_fetches(text: str) -> list[str]:
    """The code lines that run build-release.sh or fetch anything. Comments and quoted prose (a string with a space:
    a message may name build-release.sh as a hint, xg-oerj) are left out; a quoted path or word is code."""
    found = []
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        code = re.sub(r'"(?:[^"\\]|\\.)*"|\'[^\']*\'', lambda m: "" if " " in m.group(0) else m.group(0),
                      line).split(" #", 1)[0]
        if re.search(r"build-release\.sh|\bfetch\b|\b(curl|wget|aria2c)\b|\bgit\s+(clone|fetch|pull)\b|"
                     r"\bdocker\s+pull\b|\bpip3?\s+install\b", code):
            found.append(line.strip())
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us", help="the fixture disc's region")
    ap.add_argument("--keep", action="store_true", help="keep the fixture folder")
    a = ap.parse_args()
    scan(OVERLAY / HELPER)
    base = Path(tempfile.mkdtemp(prefix="check build windows zip "))
    try:
        public, discs, cache, pins = make_public(base, a.ver)
        run = Run(base, public, discs, cache, a.ver)
        success(run)
        installer_checks(run)
        refusals(run, pins)
    finally:
        if a.keep:
            print(f"kept {base}")
        else:
            shutil.rmtree(base, ignore_errors=True)
    print(f"{'FAIL' if failures else 'ok'}: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
