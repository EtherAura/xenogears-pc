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

Nothing is built, installed, run on Windows, signed or uploaded; all output stays in build/.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
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


def helper(public: Path, *args, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """The staged helper, run as the public tree would: isolated, a bare environment."""
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LC_ALL": "C.UTF-8"}
    argv = [sys.executable, "-I", str(public / "packaging" / HELPER), *map(str, args)]
    return subprocess.run(argv, cwd=cwd or public, env=env, capture_output=True, text=True)


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


def refused(public: Path, what: str, outputs: list[Path], *args, expect: str) -> None:
    for out in outputs:
        if out.is_dir():
            shutil.rmtree(out)
        else:
            out.unlink(missing_ok=True)
    result = helper(public, *args)
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
    check(deleted == {n.replace("/", "\\") for n in files} | {"uninstall.exe"},
          "script: a Delete line for each staged file and the uninstaller, nothing else in $INSTDIR")
    check(re.findall(r'^  RMDir "\$INSTDIR\\([^"]+)"$', text, re.M) == ["extra\\notes", "extra"]
          and 'RMDir "$INSTDIR"' in text and not re.search(r"RMDir\s+/", text),
          "script: each folder removed only if empty, deepest first, never recursively")
    check(re.findall(r'^  SetOutPath "([^"]+)"$', text, re.M)
          == ["$INSTDIR", "$INSTDIR\\extra", "$INSTDIR\\extra\\notes", "$INSTDIR"],
          "script: SetOutPath for each folder before its files")
    check(not re.search(r"(Delete|RMDir)[^\n]*\$(APPDATA|LOCALAPPDATA|PROFILE|DOCUMENTS)", text),
          "script: the uninstaller touches none of the player's folders")
    check('"$INSTDIR\\xenogears.exe"' in text and '"$INSTDIR\\xenogears-enhanced.exe"' in text
          and text.count("CreateShortcut") == 3, "script: shortcuts for both executables and the uninstaller")
    check(f'MUI_PAGE_LICENSE "{os.path.relpath(stage / "COPYING.txt", script.parent)}"' in text,
          "script: the licence page shows the staged COPYING.txt")
    size = -(-sum(len(b) for b in files.values()) // 1024)
    check(f'"EstimatedSize" {size}\n' in text, f"script: EstimatedSize {size} KiB")


def check_zip(path: Path, folder: str, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path) as z:
        check(z.testzip() is None, f"{path.name}: every entry's CRC is right")
        entries = {i.filename: i for i in z.infolist()}
        inside = {n[len(folder) + 1:]: z.read(n) for n, i in entries.items() if not i.is_dir()}
        check(all(n.startswith(folder + "/") for n in entries), f"{path.name}: everything under {folder}/")
        check(inside == files, f"{path.name}: exactly the stage's {len(files)} files with their bytes")
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
    if failures:
        print(f"check_windows_release: {len(failures)} check(s) failed")
        return 1
    print("check_windows_release: all checks pass (host helper, template and archives only: no NSIS compile "
          "unless makensis was found, no Windows install or start, no workflow, signing, SBOM or release)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
