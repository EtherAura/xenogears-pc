#!/usr/bin/env python3
"""Check the Windows release helper and its NSIS template (packaging/ in xenogears-pc).

    python tools/check_windows_release.py --ver us

1. Public layout: the helper and the template, alone in
   build/pc/windows-release-<ver>/public/packaging/ (as tools/publish_decomp.py
   lays tools/publish/repo-port/ over the tree, with no tools/, docs/ or work/
   beside them), import only the standard library, pass the publisher's date,
   time and private-text scan, and run from there with python -I and a bare
   environment; also from another folder, finding the template beside the helper.
2. For a zero-sequence release (v2026.11.0), a later one (v2026.12.10) and the
   last N an NSIS version part holds (v2027.01.65535), on a
   synthetic stage (labelled text files under the names port/CMakeLists.txt and
   tools/build_windows.sh install, plus a sub-folder; not a build, not game
   data): names gives the version as the tag spells it, a bounded NSIS version
   and the asset names; render writes a script with that version, a File line
   for every staged file and a Delete line for each, RMDir (never /r) for each
   folder, shortcuts for the executables present, and no placeholder left; zip
   writes one folder holding exactly the stage's files and bytes, the same bytes
   twice; sha256 writes each asset's real SHA-256 (sha256sum -c agrees).
3. Refusals, exit 1 with nothing written: malformed tags and an N past NSIS's
   16-bit limit; templates with a missing, unknown or invalid placeholder; stages
   that are missing, a file, incomplete, or hold a symbolic link, an empty folder,
   an unsafe or reserved name, a case clash, the uninstaller's name, a disc image,
   a PS-X EXE, CD sectors, a memory card or settings.ini; output inside the stage;
   a checksum of a file that is not the tag's asset.
4. makensis, when on PATH, compiles the rendered script (extra proof only).
5. setup's makensis preflight, on fake makensis executables (scripts that log
   each call, print a set -VERSION answer and write labelled stand-in bytes as
   setup.exe; no NSIS needed or run): v3.08, v3.09, v3.9 and v2.51 are refused
   as older than 3.10, and empty, malformed or failing -VERSION answers and a
   missing or unrunnable makensis are refused, all with -VERSION the only call
   (never a compile) and nothing written; a script rendered for another tag is
   refused before any call; v3.10, v3.10-1, v3.11 and v4.0 are probed and then
   compiled by the same executable, and the build record beside setup.exe holds
   the version printed; a failing compile leaves no setup.exe and no record;
   a -VERSION that never answers (the helper's own CLI with its probe limit cut
   to 0.5 s in that child process only) is refused, stopped at the limit with no
   compile and nothing written; a compile that writes part of setup.exe and then
   fails, after an earlier success, leaves neither that setup.exe nor the earlier
   record, and nothing else beside the script changes; the next good compile is
   whole and recorded; no fake makensis is left running;
   names, render and zip still work with no makensis on PATH.
6. package (render, setup, zip and sha256 in one step), run from another folder
   on the fake makensis executables (fake-compiler orchestration tests: its
   setup.exe is stand-in bytes, never a real setup): for each tag of 2., into a
   folder already holding an unrelated file and a previous release's asset, it
   adds exactly setup.exe, its build record, the .zip and both .sha256, and says
   it is the packaging phase only; the same makensis probed and then compiled a
   rendered script (checked as in 2.) in a temporary folder since removed; the
   .zip is the stage's, byte for byte what zip makes; the sums are of the bytes
   written. Refused with --out-dir unchanged and the stage untouched: before any
   makensis call, bad tags and stages, an --out-dir inside the stage or a file,
   an --out-dir already holding any of the five names (or a link under one),
   no makensis; after the probe only, old, malformed, failing and silent
   -VERSION answers (a missing --out-dir it made is removed again); after the
   compile, a partial setup.exe then failure, a failure, and no setup.exe at
   all. A .zip name that appears during the run is kept, and the files package
   had copied are removed and named. With a real makensis it is run once more.

Nothing is built, installed, run on Windows, signed or uploaded; all output stays in build/.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "tools/publish/repo-port/packaging"
HELPER, TEMPLATE = "windows_release.py", "xenogears-pc-installer.nsi.in"
RELEASES = (("v2026.11.0", "2026.11.0", "2026.11.0.0"), ("v2026.12.10", "2026.12.10", "2026.12.10.0"),
            ("v2027.01.65535", "2027.01.65535", "2027.1.65535.0"))  # the last N an NSIS version holds
BAD_TAGS = ["2026.11.0", "v2026.11", "v2026.13.0", "v2026.00.0", "v2026.1.0", "v2026.11.01",
            "v26.11.0", "v2026.11.0-rc1", "v2026.11.-1", " v2026.11.0", "v2026.11.0\n", "V2026.11.0", "",
            "v2026.11.65536", "v2026.11.0.0", "v2026.11.0/../x"]
# What cmake --install (port/CMakeLists.txt) and tools/build_windows.sh put in the folder.
INSTALLED = ["xenogears.exe", "xenogears-enhanced.exe", "SDL3.dll", "COPYING.txt", "LICENSE-Jukebox-font.txt",
             "LICENSE-glslang.txt", "LICENSE-dav1d.txt", "LICENSE-SDL3.txt", "LICENSE-imgui.txt", "README.txt"]
SUBFOLDER = ["extra/notes/FIXTURE.txt", "extra/FIXTURE.txt"]  # exercises folders; the install has none today

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def helper(public: Path, *args, cwd: Path | None = None, path: str | None = None) -> subprocess.CompletedProcess:
    """The staged helper, run as the public tree would: isolated, a bare environment."""
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin") if path is None else path, "LC_ALL": "C.UTF-8"}
    argv = [sys.executable, "-I", str(public / "packaging" / HELPER), *map(str, args)]
    return subprocess.run(argv, cwd=cwd or public, env=env, capture_output=True, text=True, timeout=300)


def stage_public(work: Path) -> Path:
    public = work / "public"
    shutil.rmtree(public, ignore_errors=True)
    (public / "packaging").mkdir(parents=True)
    for name in (HELPER, TEMPLATE):
        shutil.copy2(OVERLAY / name, public / "packaging" / name)
    check(sorted(p.name for p in public.iterdir()) == ["packaging"]
          and sorted(p.name for p in (public / "packaging").iterdir()) == sorted([HELPER, TEMPLATE]),
          "public fixture holds only packaging/ with the helper and the template")
    source = (public / "packaging" / HELPER).read_text()
    imports = set()
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Import):
            imports |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            imports.add((node.module or "").split(".")[0])
    outside = sorted(imports - set(sys.stdlib_module_names) - {"__future__"})
    check(not outside, f"public helper imports only the standard library ({outside or sorted(imports)})")
    body = source.split("\n", 1)[1] if source.startswith("#!") else source  # not the /usr/bin/env line
    private = re.findall(r"\b(?:tools|docs|work|bin|disc)/", body)
    check(not private, f"public helper names no private folder ({private or 'none'})")

    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    for name in (HELPER, TEMPLATE):
        flagged = [line for line in (public / "packaging" / name).read_text().splitlines()
                   if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line)
                   or publish.FORBIDDEN.search(line)]
        check(not flagged, f"{name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    return public


def make_stage(path: Path, names: list[str]) -> Path:
    """A synthetic stage: each file a labelled line of text (not a build, not game data)."""
    shutil.rmtree(path, ignore_errors=True)
    for name in names:
        f = path / name
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(f"synthetic test fixture for check_windows_release.py: {name}\n")
    return path


def stage_files(stage: Path) -> dict[str, bytes]:
    return {p.relative_to(stage).as_posix(): p.read_bytes() for p in stage.rglob("*") if p.is_file()}


def refused(public: Path, what: str, outputs: list[Path], *args, expect: str, path: str | None = None) -> None:
    for out in outputs:
        if out.is_dir():
            shutil.rmtree(out)
        else:
            out.unlink(missing_ok=True)
    result = helper(public, *args, path=path)
    message = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ""
    written = [str(o) for o in outputs if o.exists()]
    check(result.returncode == 1 and message.startswith("windows_release: ") and expect in message
          and not written and not result.stdout,
          f"refused: {what} ({message or result.stdout.strip()!r}{', wrote ' + str(written) if written else ''})")


def check_script(text: str, version: str, nsis: str, setup: str, files: dict[str, bytes], script: Path,
                 stage: Path) -> None:
    check(not re.search(r"@[A-Za-z0-9_]+@", text), "script: no placeholder left")
    check(f'Name "${{PRODUCT}} {version}"' in text and f'"DisplayVersion" "{version}"' in text
          and f'VIAddVersionKey "ProductVersion" "{version}"' in text, f"script: version {version}")
    check(text.count(f'"{nsis}"') == 2, f"script: VIProductVersion and VIFileVersion {nsis}")
    check(f'OutFile "{setup}"' in text, f"script: OutFile {setup}")
    sources = re.findall(r'^  File "([^"]+)"$', text, re.M)
    resolved = {(script.parent / s).resolve().relative_to(stage.resolve()).as_posix() for s in sources}
    check(len(sources) == len(files) and resolved == set(files)
          and not any(s.startswith("/") for s in sources),
          f"script: one relative File line for each of the {len(files)} staged files")
    deleted = set(re.findall(r'^  Delete "\$INSTDIR\\([^"]+)"$', text, re.M))
    check(deleted == {n.replace("/", "\\") for n in files} | {"uninstall.exe", "release.txt"},
          "script: a Delete line for each staged file, the release file and the uninstaller, nothing else in $INSTDIR")
    check(re.search(r'^  FileOpen \$0 "\$INSTDIR\\release\.txt" w\n  FileWrite \$0 "version='
                    + re.escape(version) + r'\$\\nkind=setup\$\\n"\n  FileClose \$0$', text, re.M) is not None,
          f"script: the install writes release.txt, version {version} and kind setup (the in-app updater's)")
    check(re.findall(r'^  RMDir "\$INSTDIR\\([^"]+)"$', text, re.M) == ["extra\\notes", "extra"]
          and 'RMDir "$INSTDIR"' in text and not re.search(r"RMDir\s+/", text),
          "script: each folder removed only if empty, deepest first, never recursively")
    check(re.findall(r'^  SetOutPath "([^"]+)"$', text, re.M)
          == ["$INSTDIR", "$INSTDIR\\extra", "$INSTDIR\\extra\\notes", "$INSTDIR"],
          "script: SetOutPath for each folder before its files")
    update = re.search(r'\$\{GetOptions\} \$R0 "/UPDATE=" \$UpdateExe\n  \$\{If\} \$\{Errors\}\n'
                       r'  \$\{OrIf\} \$UpdateExe != "xenogears\.exe"\n'
                       r'  \$\{AndIf\} \$UpdateExe != "xenogears-enhanced\.exe"\n    StrCpy \$UpdateExe ""', text)
    restart = re.search(r'^    Exec \'"\$WINDIR\\explorer\.exe" "\$INSTDIR\\\$UpdateExe"\'$', text, re.M)
    check(update is not None and restart is not None and "    Call WaitForTheGame\n" in text
          and 'FileOpen $R2 "$INSTDIR\\$UpdateExe" a' in text,
          "script: /UPDATE= takes the two executables only, waits for the game, then starts it again through "
          "Explorer (the in-app updater's silent update)")
    check(not re.search(r"(Delete|RMDir)[^\n]*\$(APPDATA|LOCALAPPDATA|PROFILE|DOCUMENTS)", text),
          "script: the uninstaller touches none of the player's folders")
    check('"$INSTDIR\\xenogears.exe"' in text and '"$INSTDIR\\xenogears-enhanced.exe"' in text
          and text.count("CreateShortcut") == 3, "script: shortcuts for both executables and the uninstaller")
    check(f'MUI_PAGE_LICENSE "{os.path.relpath(stage / "COPYING.txt", script.parent)}"' in text,
          "script: the licence page shows the staged COPYING.txt")
    size = -(-sum(len(b) for b in files.values()) // 1024)
    check(f'"EstimatedSize" {size}\n' in text, f"script: EstimatedSize {size} KiB")


def check_zip(path: Path, folder: str, files: dict[str, bytes]) -> None:
    version = folder.removeprefix("xenogears-pc-").removesuffix("-windows-x64")
    want = dict(files, **{"release.txt": f"version={version}\nkind=zip\n".encode()})  # the in-app updater's
    with zipfile.ZipFile(path) as z:
        check(z.testzip() is None, f"{path.name}: every entry's CRC is right")
        entries = {i.filename: i for i in z.infolist()}
        inside = {n[len(folder) + 1:]: z.read(n) for n, i in entries.items() if not i.is_dir()}
        check(all(n.startswith(folder + "/") for n in entries), f"{path.name}: everything under {folder}/")
        check(inside == want, f"{path.name}: exactly the stage's {len(files)} files with their bytes, and release.txt")
        check({n for n, i in entries.items() if i.is_dir()}
              == {f"{folder}/", f"{folder}/extra/", f"{folder}/extra/notes/"}, f"{path.name}: its folders")
    if shutil.which("unzip"):
        check(subprocess.run(["unzip", "-tq", path], capture_output=True).returncode == 0, "unzip -t accepts it")


def check_sidecar(asset: Path) -> None:
    sidecar = asset.with_name(asset.name + ".sha256")
    digest = hashlib.sha256(asset.read_bytes()).hexdigest()
    check(sidecar.is_file() and sidecar.read_text() == f"{digest}  {asset.name}\n",
          f"{sidecar.name}: {digest[:16]}... of the asset's bytes")
    if shutil.which("sha256sum"):
        check(subprocess.run(["sha256sum", "-c", sidecar.name], cwd=asset.parent, capture_output=True)
              .returncode == 0, f"sha256sum -c accepts {sidecar.name}")


def release(public: Path, work: Path, stage: Path, tag: str, version: str, nsis: str) -> None:
    files = stage_files(stage)
    result = helper(public, "names", "--tag", tag)
    want = {"version": version, "nsis_version": nsis, "setup": f"xenogears-pc-{version}-windows-x64-setup.exe",
            "zip": f"xenogears-pc-{version}-windows-x64.zip", "folder": f"xenogears-pc-{version}-windows-x64"}
    got = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    check(result.returncode == 0 and got == want, f"names {tag}: {got or result.stderr.strip()}")

    out = work / f"out-{version}"
    shutil.rmtree(out, ignore_errors=True)
    script = out / "xenogears-pc.nsi"
    result = helper(public, "render", "--tag", tag, "--stage", stage, "--out", script)
    if check(result.returncode == 0 and script.is_file(), f"render {tag} ({result.stderr.strip() or 'written'})"):
        check_script(script.read_text(), version, nsis, want["setup"], files, script, stage)
    # From another folder: the template is found beside the helper, not in the current folder.
    other = helper(public, "render", "--tag", tag, "--stage", stage, "--out", out / "again.nsi", cwd=work)
    check(other.returncode == 0 and (out / "again.nsi").read_bytes() == script.read_bytes(),
          "render from another folder gives the same script")

    result = helper(public, "zip", "--tag", tag, "--stage", stage, "--out-dir", out)
    archive = out / want["zip"]
    if check(result.returncode == 0 and archive.is_file(), f"zip {tag} ({result.stderr.strip() or archive.name})"):
        check_zip(archive, want["folder"], files)
        first = archive.read_bytes()
        again = helper(public, "zip", "--tag", tag, "--stage", stage, "--out-dir", out)
        check(again.returncode == 0 and archive.read_bytes() == first, "a second zip writes the same bytes")
    check(stage_files(stage) == files, "the stage is unchanged")
    check(sorted(p.name for p in out.iterdir()) == sorted(["xenogears-pc.nsi", "again.nsi", want["zip"]]),
          "no temporary file left beside the outputs")

    setup = out / want["setup"]
    if shutil.which("makensis"):
        made = subprocess.run(["makensis", "-V2", script], capture_output=True, text=True)
        check(made.returncode == 0 and setup.is_file(), f"makensis compiles the script ({made.stdout[-500:]})")
    else:
        print("LIMITATION  makensis not on PATH: the script is not compiled; setup.exe is not made or checked")
    if setup.is_file():
        assets = [setup, archive]
    else:
        assets = [archive]
    result = helper(public, "sha256", "--tag", tag, *assets)
    check(result.returncode == 0, f"sha256 {tag} ({result.stderr.strip() or 'written'})")
    for asset in assets:
        check_sidecar(asset)
    if not setup.is_file():
        # The sidecar format for setup.exe on a labelled stand-in (not a setup): bytes in, their hash out.
        stand_in = work / f"stand-in-{version}" / want["setup"]
        stand_in.parent.mkdir(exist_ok=True)
        stand_in.write_bytes(b"stand-in bytes for the checksum test, not a setup\n" + bytes(range(256)))
        result = helper(public, "sha256", "--tag", tag, stand_in)
        check(result.returncode == 0, "sha256 of a file named as the setup (stand-in bytes)")
        check_sidecar(stand_in)


def refusals(public: Path, work: Path, good: Path) -> None:
    out = work / "refused"
    script = out / "x.nsi"
    archive = out / "xenogears-pc-2026.11.0-windows-x64.zip"
    for tag in BAD_TAGS:
        refused(public, f"tag {tag!r} (names)", [], "names", "--tag", tag,
                expect="16-bit" if tag == "v2026.11.65536" else "vYYYY.MM.N")
    refused(public, "a bad tag (render)", [script], "render", "--tag", "v2026.11", "--stage", good, "--out", script,
            expect="vYYYY.MM.N")
    refused(public, "a bad tag (zip)", [out], "zip", "--tag", "2026.11.0", "--stage", good, "--out-dir", out,
            expect="vYYYY.MM.N")

    templates = work / "templates"
    shutil.rmtree(templates, ignore_errors=True)
    templates.mkdir()
    base = (public / "packaging" / TEMPLATE).read_text()
    for what, text, expect in (
            ("a template missing a placeholder", base.replace("@SHORTCUTS@", ""), "missing placeholders ['SHORTCUTS']"),
            ("a template with an unknown placeholder", base + "; @DATE_STAMP@\n", "unknown placeholders ['DATE_STAMP']"),
            ("a template with a misspelt placeholder", base.replace("@VERSION@", "@VERSON@", 1),
             "unknown placeholders ['VERSON']")):
        t = templates / f"{len(list(templates.iterdir()))}.nsi.in"
        t.write_text(text)
        refused(public, what, [script], "render", "--tag", "v2026.11.0", "--stage", good, "--out", script,
                "--template", t, expect=expect)
    refused(public, "a missing template", [script], "render", "--tag", "v2026.11.0", "--stage", good,
            "--out", script, "--template", templates / "none.nsi.in", expect="no such file")
    refused(public, "an output not named .nsi", [out / "x.txt"], "render", "--tag", "v2026.11.0", "--stage", good,
            "--out", out / "x.txt", expect="*.nsi")
    refused(public, "a script inside the stage", [good / "x.nsi"], "render", "--tag", "v2026.11.0", "--stage", good,
            "--out", good / "x.nsi", expect="inside the stage")
    refused(public, "a zip inside the stage", [good / "dist"], "zip", "--tag", "v2026.11.0", "--stage", good,
            "--out-dir", good / "dist", expect="inside the stage")

    stages = work / "bad-stages"
    shutil.rmtree(stages, ignore_errors=True)
    cases = []

    def bad(what, expect, extra=(), drop=(), edit=None):
        s = make_stage(stages / str(len(cases)), [n for n in INSTALLED if n not in drop] + list(extra))
        if edit:
            edit(s)
        cases.append((what, s, expect))

    bad("no xenogears.exe", "no xenogears.exe", drop=["xenogears.exe"])
    bad("no SDL3.dll", "no SDL3.dll", drop=["SDL3.dll"])
    bad("no COPYING.txt", "no COPYING.txt", drop=["COPYING.txt"])
    bad("a symbolic link to a file", "symbolic link",
        edit=lambda s: (s / "LICENSE-link.txt").symlink_to(s / "COPYING.txt"))
    bad("a symbolic link to a folder", "symbolic link", edit=lambda s: (s / "linked").symlink_to(s.parent))
    bad("an empty folder", "holds no files", edit=lambda s: (s / "empty").mkdir())
    bad("a name with a dollar sign", "not taken as it is", extra=["$PLUGINSDIR.txt"])
    bad("a name with a quote", "not taken as it is", extra=['say"hi.txt'])
    bad("a name with a space", "not taken as it is", extra=["read me.txt"])
    bad("a name ending in a dot", "not taken as it is", extra=["notes."])
    bad("a hidden name", "not taken as it is", extra=[".hidden"])
    bad("a reserved name", "Windows reserves", extra=["CON.txt"])
    bad("a name clashing in case", "only in case", extra=["copying.TXT"])
    bad("the uninstaller's name", "uninstaller's name", extra=["Uninstall.exe"])
    bad("a release file of its own", "each asset's own", extra=["release.txt"])
    bad("a release file in another case", "each asset's own", extra=["Release.TXT"])
    bad("a .cue sheet", "game data", extra=["Xenogears-Disc-1.cue"])
    bad("a .bin image in a folder", "game data", extra=["bin/track.bin"])
    bad("the disc's executable name", "game data", extra=["SLUS_006.64"])
    bad("a memory card", "game data", extra=["card1.mcd"])
    bad("settings.ini", "player's file", extra=["settings.ini"])
    bad("a PS-X EXE under another name", "PlayStation executable",
        edit=lambda s: (s / "data.dat").write_bytes(b"PS-X EXE" + bytes(2040)))
    bad("raw CD sectors under another name", "CD data",
        edit=lambda s: (s / "data.dat").write_bytes(b"\x00" + b"\xff" * 10 + b"\x00" + bytes(2340)))
    bad("an ISO 9660 volume under another name", "CD data",
        edit=lambda s: (s / "data.dat").write_bytes(bytes(0x8001) + b"CD001" + bytes(100)))
    for what, s, expect in cases:
        refused(public, f"stage with {what} (render)", [script], "render", "--tag", "v2026.11.0", "--stage", s,
                "--out", script, expect=expect)
        refused(public, f"stage with {what} (zip)", [out], "zip", "--tag", "v2026.11.0", "--stage", s,
                "--out-dir", out, expect=expect)
    refused(public, "a stage that does not exist", [script], "render", "--tag", "v2026.11.0",
            "--stage", stages / "none", "--out", script, expect="not a folder")
    refused(public, "a stage that is a file", [out], "zip", "--tag", "v2026.11.0",
            "--stage", good / "COPYING.txt", "--out-dir", out, expect="not a folder")

    loose = work / "loose"
    shutil.rmtree(loose, ignore_errors=True)
    loose.mkdir()
    (loose / "notes.zip").write_text("not an asset\n")
    (loose / "xenogears-pc-2026.11.1-windows-x64.zip").write_text("another release's name\n")
    refused(public, "a checksum of a file that is no asset", [loose / "notes.zip.sha256"],
            "sha256", "--tag", "v2026.11.0", loose / "notes.zip", expect="not an asset of v2026.11.0")
    refused(public, "a checksum of another release's asset", [loose / "xenogears-pc-2026.11.1-windows-x64.zip.sha256"],
            "sha256", "--tag", "v2026.11.0", loose / "xenogears-pc-2026.11.1-windows-x64.zip",
            expect="not an asset of v2026.11.0")
    refused(public, "a checksum of a missing asset", [archive.with_name(archive.name + ".sha256")],
            "sha256", "--tag", "v2026.11.0", archive, expect="no such file")


FAKE_MAKENSIS = """#!{python} -I
# A fake makensis for check_windows_release.py: logs each call and its process id, answers -VERSION
# (after an optional sleep) as fake.json says, and "compiles" by writing labelled stand-in bytes (not a
# setup) under the script's OutFile name; a partial compile writes other stand-in bytes there, then fails.
# It keeps a copy of the script it was given (compiled.nsi); it can also exit 0 having written nothing, or
# first plant a file, as another process might, in the folder above the script's (package's --out-dir).
import json, os, re, sys, time
from pathlib import Path
here = Path(__file__).resolve().parent
conf = json.loads((here / "fake.json").read_text())
with (here / "calls.log").open("a") as log:
    log.write(json.dumps(sys.argv) + "\\n")
with (here / "pids.log").open("a") as log:
    log.write(str(os.getpid()) + "\\n")
if sys.argv[1:] == ["-VERSION"]:
    time.sleep(conf["probe_sleep"])
    sys.stdout.write(conf["printed"])
    sys.exit(conf["probe_exit"])
script = Path(sys.argv[-1])
(here / "compiled.nsi").write_text(script.read_text())
if conf["compile_exit"] and not conf["partial"]:
    sys.exit(conf["compile_exit"])
if conf["no_output"]:
    sys.exit(0)
name = re.search(r'^\\s*OutFile "([^"]+)"', script.read_text(), re.M)[1]
if conf["plant"]:
    (script.parent.parent / conf["plant"]).write_bytes(b"planted by a fake makensis: another process's file\\n")
if conf["partial"]:
    (script.parent / name).write_bytes(b"partial stand-in bytes from a failing fake makensis\\n")
    (here / "partial.log").write_text(name)
    sys.exit(conf["compile_exit"])
(script.parent / name).write_bytes(b"stand-in bytes from a fake makensis, not a setup\\n")
"""

# The helper's own CLI (main, build_setup, makensis_version) with PROBE_SECONDS cut for this one child
# process only, so a -VERSION that never answers is tested in a fraction of a second.
PROBE_CUT = """import importlib.util, sys
spec = importlib.util.spec_from_file_location("windows_release", sys.argv[1])
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
helper.PROBE_SECONDS = float(sys.argv[2])
sys.argv = [sys.argv[1]] + sys.argv[3:]
sys.exit(helper.main())
"""


def fake_makensis(folder: Path, printed: str, probe_exit: int = 0, compile_exit: int = 0,
                  probe_sleep: float = 0, partial: bool = False, no_output: bool = False,
                  plant: str = "") -> Path:
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    exe = folder / "makensis"
    exe.write_text(FAKE_MAKENSIS.format(python=sys.executable))
    exe.chmod(0o755)
    (folder / "fake.json").write_text(json.dumps({"printed": printed, "probe_exit": probe_exit,
                                                  "compile_exit": compile_exit, "probe_sleep": probe_sleep,
                                                  "partial": partial, "no_output": no_output, "plant": plant}))
    return exe


def calls(exe: Path) -> list[list[str]]:
    log = exe.parent / "calls.log"
    return [json.loads(line) for line in log.read_text().splitlines()] if log.is_file() else []


def running(exe: Path) -> list[int]:
    """The fake's logged processes still alive; any found is killed, so none outlives the check."""
    log = exe.parent / "pids.log"
    alive = []
    for pid in (int(line) for line in log.read_text().split()) if log.is_file() else ():
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            continue
        alive.append(pid)
        os.kill(pid, signal.SIGKILL)
    return alive


def last_line(result: subprocess.CompletedProcess) -> str:
    return result.stderr.strip().splitlines()[-1] if result.stderr.strip() else ""


def failure_paths(public: Path, out: Path, script: Path, tag: str, fakes: Path) -> None:
    """A -VERSION that times out, and a compile that writes part of setup.exe and then fails."""
    setup_name = f"xenogears-pc-{tag[1:]}-windows-x64-setup.exe"
    setup, record = out / setup_name, out / f"{setup_name}.build.json"
    for stale in (setup, record):
        stale.unlink(missing_ok=True)
    # The fake would sleep 10 s and then answer v3.10; the cut limit is 0.5 s.
    exe = fake_makensis(fakes, "v3.10", probe_sleep=10)
    started = time.monotonic()
    result = subprocess.run([sys.executable, "-I", "-c", PROBE_CUT, str(public / "packaging" / HELPER), "0.5",
                             "setup", "--tag", tag, "--script", str(script), "--makensis", str(exe)],
                            cwd=public, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C.UTF-8"},
                            capture_output=True, text=True, timeout=60)
    elapsed = time.monotonic() - started
    message = last_line(result)
    written = [p.name for p in (setup, record) if p.exists()]
    check(result.returncode == 1 and message.startswith("windows_release: makensis -VERSION did not run")
          and "timed out after 0.5 seconds" in message and message.endswith("nothing compiled")
          and not result.stdout and not written,
          f"refused: setup with a makensis -VERSION that times out ({message or result.stdout.strip()!r}"
          f"{', wrote ' + str(written) if written else ''})")
    check(calls(exe) == [[str(exe), "-VERSION"]], "  timed-out -VERSION: probed once, never compiled")
    left = running(exe)
    check(elapsed < 8 and len(calls(exe)) == 1 and not left,
          f"  timed-out -VERSION: stopped at the limit ({elapsed:.1f} s), no makensis left running ({left or 'none'})")

    # An earlier success leaves a setup.exe and its record; the next attempt writes part of setup.exe and fails.
    exe = fake_makensis(fakes, "v3.10")
    first = helper(public, "setup", "--tag", tag, "--script", script, "--makensis", exe)
    keep = out / "unrelated-notes.txt"
    keep.write_text("not made by setup; must stay\n")
    others = {p.name: p.read_bytes() for p in out.iterdir() if p not in (setup, record)}
    check(first.returncode == 0 and setup.is_file() and record.is_file(),
          f"partial compile: an earlier setup.exe and record beside the script ({first.stderr.strip() or 'made'})")
    exe = fake_makensis(fakes, "v3.10", compile_exit=3, partial=True)
    result = helper(public, "setup", "--tag", tag, "--script", script, "--makensis", exe)
    message = last_line(result)
    written = [p.name for p in (setup, record) if p.exists()]
    partial = exe.parent / "partial.log"
    check(result.returncode == 1 and message.startswith("windows_release: makensis v3.10 failed (exit 3)")
          and not result.stdout and not written,
          f"refused: setup whose compile writes part of setup.exe, then fails ({message or result.stdout.strip()!r}"
          f"{', left ' + str(written) if written else ''})")
    check(calls(exe) == [[str(exe), "-VERSION"], [str(exe), "-V2", str(script.resolve())]]
          and partial.is_file() and partial.read_text() == setup_name,
          "  partial compile: probed, then compiled by the same makensis, which wrote part of setup.exe")
    check({p.name: p.read_bytes() for p in out.iterdir()} == others,
          "  partial compile: the script and unrelated files untouched, no temporary file left")
    check(not running(exe), "  partial compile: no makensis left running")

    # Recovery: the next good compile is whole and its record names the makensis that made it.
    exe = fake_makensis(fakes, "v3.11")
    result = helper(public, "setup", "--tag", tag, "--script", script, "--makensis", exe)
    info = json.loads(record.read_text()) if record.is_file() else None
    check(result.returncode == 0 and setup.is_file()
          and setup.read_bytes() == b"stand-in bytes from a fake makensis, not a setup\n"
          and info == {"asset": setup_name, "makensis_version": "v3.11", "makensis_minimum": "3.10"},
          f"after the failures, a good compile writes a whole setup.exe and record {info} "
          f"({result.stderr.strip() or 'made'})")
    keep.unlink()


def compiler(public: Path, work: Path, stage: Path) -> None:
    tag, version = "v2026.11.0", "2026.11.0"
    setup_name = f"xenogears-pc-{version}-windows-x64-setup.exe"
    cases = work / "makensis-cases"
    shutil.rmtree(cases, ignore_errors=True)
    out = cases / "out"
    script = out / "xenogears-pc.nsi"
    nowhere = cases / "empty-path"
    nowhere.mkdir(parents=True)
    # The portable route needs no makensis: names, render and zip with nothing on PATH.
    names = helper(public, "names", "--tag", tag, path=str(nowhere))
    result = helper(public, "render", "--tag", tag, "--stage", stage, "--out", script, path=str(nowhere))
    archive = helper(public, "zip", "--tag", tag, "--stage", stage, "--out-dir", cases / "zip", path=str(nowhere))
    check(names.returncode == 0 and result.returncode == 0 and script.is_file() and archive.returncode == 0
          and (cases / "zip" / f"xenogears-pc-{version}-windows-x64.zip").is_file(),
          f"names, render and zip need no makensis on PATH ({(result.stderr + archive.stderr).strip() or 'ok'})")
    setup, record = out / setup_name, out / f"{setup_name}.build.json"

    for printed, expect in (("v3.08", "older than 3.10"), ("v3.09", "older than 3.10"),
                            ("v3.9", "older than 3.10"),  # 3.9 < 3.10 as numbers ("3.9" > "3.10" as text)
                            ("v2.51\n", "older than 3.10"), ("", "not a version"), ("garbage\n", "not a version"),
                            ("v3", "not a version"), ("3.10", "not a version"), ("v3.10 beta", "not a version"),
                            ("v3.10\nv3.08", "not a version")):
        exe = fake_makensis(cases / "fake", printed)
        refused(public, f"setup with a makensis printing {printed!r}", [setup, record],
                "setup", "--tag", tag, "--script", script, "--makensis", exe, expect=expect)
        check(calls(exe) == [[str(exe), "-VERSION"]], f"  makensis {printed!r}: probed once, never compiled")
    exe = fake_makensis(cases / "fake", "v3.10", probe_exit=1)
    refused(public, "setup with a failing makensis -VERSION", [setup, record],
            "setup", "--tag", tag, "--script", script, "--makensis", exe, expect="failed (exit 1")
    check(calls(exe) == [[str(exe), "-VERSION"]], "  failing -VERSION: probed once, never compiled")
    refused(public, "setup with no makensis on PATH", [setup, record],
            "setup", "--tag", tag, "--script", script, expect="no makensis", path=str(nowhere))
    refused(public, "setup with a --makensis that does not exist", [setup, record],
            "setup", "--tag", tag, "--script", script, "--makensis", cases / "none" / "makensis", expect="no makensis")
    exe = fake_makensis(cases / "fake", "v3.10")
    exe.chmod(0o644)
    refused(public, "setup with a makensis that cannot run", [setup, record],
            "setup", "--tag", tag, "--script", script, "--makensis", exe, expect="no makensis")
    check(calls(exe) == [], "  unrunnable makensis: never called")
    exe = fake_makensis(cases / "fake", "v3.10")
    refused(public, "setup of a script rendered for another tag", [setup, record],
            "setup", "--tag", "v2026.12.10", "--script", script, "--makensis", exe, expect="not rendered for")
    check(calls(exe) == [], "  another tag's script: makensis never called")
    exe = fake_makensis(cases / "fake", "v3.10", compile_exit=2)
    refused(public, "setup whose makensis compile fails", [setup, record],
            "setup", "--tag", tag, "--script", script, "--makensis", exe, expect="failed (exit 2)")
    check(calls(exe) == [[str(exe), "-VERSION"], [str(exe), "-V2", str(script.resolve())]],
          "  failing compile: probed, then compiled by the same makensis; no setup.exe or record left")
    failure_paths(public, out, script, tag, cases / "fake")

    for printed, via_path in (("v3.10", False), ("v3.10\n", True), ("v3.10-1", False), ("v3.11", False),
                              ("v4.0", False)):
        exe = fake_makensis(cases / "fake", printed)
        for stale in (setup, record):
            stale.unlink(missing_ok=True)
        args = ("setup", "--tag", tag, "--script", script) + (() if via_path else ("--makensis", exe))
        result = helper(public, *args, path=str(exe.parent) if via_path else str(nowhere))
        got = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        what = f"setup with a makensis printing {printed!r}{' (found on PATH)' if via_path else ''}"
        check(result.returncode == 0 and got == {"setup": str(setup), "build_info": str(record),
                                                 "makensis_version": printed.strip()},
              f"{what}: accepted ({result.stderr.strip() or got})")
        check(calls(exe) == [[str(exe), "-VERSION"], [str(exe), "-V2", str(script.resolve())]],
              f"  makensis {printed!r}: probed, then compiled by the same executable")
        check(setup.is_file() and setup.read_bytes().startswith(b"stand-in bytes from a fake makensis"),
              f"  makensis {printed!r}: setup.exe (stand-in bytes) beside the script")
        info = json.loads(record.read_text()) if record.is_file() else None
        check(info == {"asset": setup_name, "makensis_version": printed.strip(), "makensis_minimum": "3.10"},
              f"  makensis {printed!r}: build record {info}")
    result = helper(public, "sha256", "--tag", tag, setup)
    check(result.returncode == 0, f"sha256 of the setup.exe setup wrote ({result.stderr.strip() or 'written'})")
    check_sidecar(setup)


def snapshot(path: Path):
    """A folder's (or file's) names and bytes, to tell whether a refused package run changed it."""
    if not os.path.lexists(path):
        return None
    if path.is_symlink() or not path.is_dir():
        return os.readlink(path) if path.is_symlink() else path.read_bytes()
    return {p.name: snapshot(p) for p in sorted(path.iterdir())}


def package_checks(public: Path, work: Path, stage: Path) -> None:
    """package (render, guarded setup, zip, sha256 in one step), on fake makensis executables: fake-compiler
    orchestration tests. setup.exe here is labelled stand-in bytes, never a real setup."""
    root = work / "package"
    shutil.rmtree(root, ignore_errors=True)
    fakes, nowhere = root / "fake", root / "empty-path"
    nowhere.mkdir(parents=True)
    files = stage_files(stage)
    stand_in = b"stand-in bytes from a fake makensis, not a setup\n"
    keep = {"unrelated-notes.txt": b"not made by package; must stay\n",
            "xenogears-pc-2026.10.0-windows-x64.zip": b"a previous release's asset; must stay\n"}

    def run(*args, path: str | None = None) -> subprocess.CompletedProcess:
        # From another folder than the public tree's, with nothing on PATH unless asked.
        return helper(public, "package", *args, path=str(nowhere) if path is None else path, cwd=root)

    def assets(version: str) -> list[str]:
        setup, archive = f"xenogears-pc-{version}-windows-x64-setup.exe", f"xenogears-pc-{version}-windows-x64.zip"
        return [setup, f"{setup}.build.json", archive, f"{setup}.sha256", f"{archive}.sha256"]

    def fill(out: Path, extra: dict | None = None) -> None:
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        for name, data in {**keep, **(extra or {})}.items():
            (out / name).write_bytes(data)

    def compiled_once(exe: Path, out: Path) -> bool:
        log = calls(exe)
        if len(log) != 2 or log[0] != [str(exe), "-VERSION"] or log[1][:2] != [str(exe), "-V2"] or len(log[1]) != 3:
            return False
        script = Path(log[1][2])
        return script.name == "xenogears-pc.nsi" and script.parent.parent == out.resolve() and not script.parent.exists()

    def refuse(what: str, out: Path, *args, expect: str, exe: Path | None = None, probed: bool = False,
               compiled: bool = False, path: str | None = None) -> str:
        before = snapshot(out)
        result = run(*args, path=path)
        message = last_line(result)
        if compiled:
            called = compiled_once(exe, out)
        else:
            called = calls(exe) == ([[str(exe), "-VERSION"]] if probed else []) if exe else True
        after = snapshot(out)
        check(result.returncode == 1 and message.startswith("windows_release: ") and expect in message
              and not result.stdout and after == before and stage_files(stage) == files and called,
              f"refused: package with {what} ({message or result.stdout.strip()!r}"
              f"{'' if after == before else ', changed ' + str(out)}{'' if called else ', calls ' + str(calls(exe))})")
        return message

    # Accepted: a zero-sequence tag, a later one and the last N; into a folder already holding unrelated files.
    for tag, version, nsis in RELEASES:
        exe = fake_makensis(fakes, "v3.10")
        out = root / f"out-{version}"
        fill(out)
        names = assets(version)
        result = run("--tag", tag, "--stage", stage, "--out-dir", out, "--makensis", exe)
        got = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        want = {"setup": str(out / names[0]), "build_info": str(out / names[1]), "zip": str(out / names[2]),
                "setup_sha256": str(out / names[3]), "zip_sha256": str(out / names[4]), "makensis_version": "v3.10"}
        scope = got.pop("scope", "")
        check(result.returncode == 0 and got == want, f"package {tag} (fake makensis): {got or last_line(result)}")
        check(scope.startswith("the packaging phase only") and "payload audit" in scope and "signing" in scope
              and "SBOM" in scope, f"  package {tag}: says it is the packaging phase only ({scope[:60]!r}...)")
        check(sorted(p.name for p in out.iterdir()) == sorted(names + list(keep)),
              f"  package {tag}: exactly the five assets added; no .nsi, workspace or temporary file left")
        check(all((out / n).read_bytes() == b for n, b in keep.items()),
              f"  package {tag}: an unrelated file and a previous release's asset untouched")
        check(compiled_once(exe, out), f"  package {tag}: probed, then compiled once by the same makensis, "
              "in a temporary folder inside --out-dir, since removed")
        script = fakes / "compiled.nsi"
        if check(script.is_file(), f"  package {tag}: the fake makensis was given a rendered script"):
            check_script(script.read_text(), version, nsis, names[0], files, Path(calls(exe)[1][2]), stage)
        check((out / names[0]).read_bytes() == stand_in, f"  package {tag}: setup.exe is the fake's stand-in bytes")
        info = json.loads((out / names[1]).read_text()) if (out / names[1]).is_file() else None
        check(info == {"asset": names[0], "makensis_version": "v3.10", "makensis_minimum": "3.10"},
              f"  package {tag}: build record {info}")
        check_zip(out / names[2], f"xenogears-pc-{version}-windows-x64", files)
        alone = helper(public, "zip", "--tag", tag, "--stage", stage, "--out-dir", root / f"zip-{version}")
        check(alone.returncode == 0 and (root / f"zip-{version}" / names[2]).read_bytes() == (out / names[2]).read_bytes(),
              f"  package {tag}: its .zip has the same bytes as zip's of the same stage")
        check_sidecar(out / names[0])
        check_sidecar(out / names[2])
        check(stage_files(stage) == files, f"  package {tag}: the stage is unchanged")
        check(not running(exe), f"  package {tag}: no makensis left running")

    # Refused before any makensis call, with nothing written.
    exe = fake_makensis(fakes, "v3.10")
    absent = root / "never-made"
    for tag in ("v2026.11", "2026.11.0", "v2026.11.01", "V2026.11.0", " v2026.11.0", "v2026.11.0-rc1",
                "v2026.11.65536"):
        refuse(f"tag {tag!r}", absent, "--tag", tag, "--stage", stage, "--out-dir", absent, "--makensis", exe,
               exe=exe, expect="16-bit" if tag == "v2026.11.65536" else "vYYYY.MM.N")
    bad = root / "bad-stages"
    for what, drop, extra, expect in (("no xenogears.exe", ["xenogears.exe"], {}, "no xenogears.exe"),
                                      ("settings.ini", [], {"settings.ini": b"x\n"}, "player's file"),
                                      ("a PS-X EXE", [], {"data.dat": b"PS-X EXE" + bytes(2040)}, "PlayStation")):
        s = make_stage(bad / str(len(list(bad.iterdir())) if bad.is_dir() else 0),
                       [n for n in INSTALLED if n not in drop])
        for name, data in extra.items():
            (s / name).write_bytes(data)
        refuse(f"a stage with {what}", absent, "--tag", "v2026.11.0", "--stage", s, "--out-dir", absent,
               "--makensis", exe, exe=exe, expect=expect)
    refuse("a stage that does not exist", absent, "--tag", "v2026.11.0", "--stage", bad / "none",
           "--out-dir", absent, "--makensis", exe, exe=exe, expect="not a folder")
    refuse("--out-dir inside the stage", stage / "dist", "--tag", "v2026.11.0", "--stage", stage,
           "--out-dir", stage / "dist", "--makensis", exe, exe=exe, expect="inside the stage")
    a_file = root / "a-file"
    a_file.write_bytes(b"a file, not a folder\n")
    refuse("--out-dir that is a file", a_file, "--tag", "v2026.11.0", "--stage", stage, "--out-dir", a_file,
           "--makensis", exe, exe=exe, expect="not a folder")
    collide = root / "collide"
    for name in assets("2026.11.0"):
        fill(collide, {name: b"a previous run's file of the same name; must stay\n"})
        refuse(f"--out-dir already holding {name}", collide, "--tag", "v2026.11.0", "--stage", stage,
               "--out-dir", collide, "--makensis", exe, exe=exe, expect=f"already holds {name}")
    fill(collide)
    (collide / assets("2026.11.0")[2]).symlink_to(root / "nowhere.zip")
    refuse("--out-dir holding a dangling link named as the .zip", collide, "--tag", "v2026.11.0", "--stage", stage,
           "--out-dir", collide, "--makensis", exe, exe=exe, expect="already holds")
    refuse("no makensis on PATH", absent, "--tag", "v2026.11.0", "--stage", stage, "--out-dir", absent,
           expect="no makensis")
    refuse("a --makensis that does not exist", absent, "--tag", "v2026.11.0", "--stage", stage, "--out-dir", absent,
           "--makensis", fakes / "none", expect="no makensis")

    # Refused by setup's own -VERSION checks: probed once, never compiled, nothing left in --out-dir.
    out = root / "probe"
    for printed, probe_exit, expect in (("v3.09", 0, "older than 3.10"), ("v2.51\n", 0, "older than 3.10"),
                                        ("garbage\n", 0, "not a version"), ("", 0, "not a version"),
                                        ("v3.10", 1, "failed (exit 1")):
        exe = fake_makensis(fakes, printed, probe_exit=probe_exit)
        fill(out)
        message = refuse(f"a makensis printing {printed!r} (exit {probe_exit})", out, "--tag", "v2026.11.0",
                         "--stage", stage, "--out-dir", out, "--makensis", exe, exe=exe, probed=True, expect=expect)
        check(message.endswith(f"no asset written in {out}"), "  says no asset was written")
    exe = fake_makensis(fakes, "v3.09")
    nested = root / "made" / "deeper"
    message = refuse("an old makensis and a missing --out-dir", root / "made", "--tag", "v2026.11.0",
                     "--stage", stage, "--out-dir", nested, "--makensis", exe, exe=exe, probed=True,
                     expect="older than 3.10")
    check("made and then removed the folder" in message and "left behind" not in message,
          "  the folders it made are removed again, and it says so")
    exe = fake_makensis(fakes, "v3.10", probe_sleep=10)
    fill(out)
    before = snapshot(out)
    started = time.monotonic()
    result = subprocess.run([sys.executable, "-I", "-c", PROBE_CUT, str(public / "packaging" / HELPER), "0.5",
                             "package", "--tag", "v2026.11.0", "--stage", str(stage), "--out-dir", str(out),
                             "--makensis", str(exe)], cwd=root, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C.UTF-8"},
                            capture_output=True, text=True, timeout=60)
    elapsed, message = time.monotonic() - started, last_line(result)
    check(result.returncode == 1 and "timed out after 0.5 seconds" in message and snapshot(out) == before
          and calls(exe) == [[str(exe), "-VERSION"]] and elapsed < 8 and not running(exe),
          f"refused: package with a -VERSION that never answers ({message!r}, {elapsed:.1f} s)")

    # Refused after the compile: nothing copied into --out-dir, no sum of a missing setup.exe.
    for what, kwargs, expect in (("a compile that writes part of setup.exe, then fails",
                                  {"compile_exit": 3, "partial": True}, "failed (exit 3)"),
                                 ("a compile that fails", {"compile_exit": 2}, "failed (exit 2)"),
                                 ("a compile that exits 0 and writes no setup.exe", {"no_output": True},
                                  "failed (exit 0)")):
        exe = fake_makensis(fakes, "v3.10", **kwargs)
        fill(out)
        refuse(what, out, "--tag", "v2026.11.0", "--stage", stage, "--out-dir", out, "--makensis", exe,
               exe=exe, compiled=True, expect=expect)
        if kwargs.get("partial"):
            check((fakes / "partial.log").is_file(), "  the fake makensis did write part of setup.exe")

    # A file named as the .zip appears in --out-dir during the compile: it is kept, and what package had
    # copied (setup.exe and its record) is removed again and named; no sum is written.
    names = assets("2026.11.0")
    exe = fake_makensis(fakes, "v3.10", plant=names[2])
    fill(out)
    before = snapshot(out)
    result = run("--tag", "v2026.11.0", "--stage", stage, "--out-dir", out, "--makensis", exe)
    message = last_line(result)
    planted = b"planted by a fake makensis: another process's file\n"
    check(result.returncode == 1 and "appeared while packaging" in message
          and f"wrote and then removed {names[0]}, {names[1]}" in message and not result.stdout
          and snapshot(out) == {**before, names[2]: planted} and compiled_once(exe, out),
          f"refused: package whose .zip name appears meanwhile ({message!r}); the other file kept, its own removed")

    if shutil.which("makensis"):
        out = root / "real"
        result = run("--tag", "v2026.11.0", "--stage", stage, "--out-dir", out, path=os.environ.get("PATH", ""))
        message = last_line(result)
        check(result.returncode == 0 or "older than 3.10" in message,
              f"package with the real makensis on PATH ({message or 'made'})")
    else:
        print("LIMITATION  makensis not on PATH: package ran only with fake makensis executables; "
              "no real setup.exe was compiled")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us",
                    help="names the work folder; the helper and template do not depend on it")
    args = ap.parse_args()
    work = ROOT / f"build/pc/windows-release-{args.ver}"
    work.mkdir(parents=True, exist_ok=True)
    public = stage_public(work)
    stage = make_stage(work / "stage", INSTALLED + SUBFOLDER)
    for tag, version, nsis in RELEASES:
        release(public, work, stage, tag, version, nsis)
    refusals(public, work, stage)
    compiler(public, work, stage)
    package_checks(public, work, stage)
    if failures:
        print(f"check_windows_release: {len(failures)} check(s) failed")
        return 1
    print("check_windows_release: all checks pass (host helper, template and archives only: no NSIS compile "
          "unless makensis was found, the makensis preflight on fake executables, no Windows install or start, "
          "no workflow, signing, SBOM or release)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
