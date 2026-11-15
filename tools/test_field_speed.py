#!/usr/bin/env python3
"""Field speed (xg-mrj.6, port/src/system/fieldspeed.c): the controller, the
shared vblank that picks the factor (battlespeed.c on hw.c's stepped clock)
and the field's own movie calls (port/src/game/field/wide.c), CPU only.

    python tools/test_field_speed.py --ver us|jp [--build DIR]

1. Builds the fieldspeedtest target (port/CMakeLists.txt) in the gates' build
   of that version (build/pc/gates, build/pc/gates-jp; configured as
   tools/gates.sh does when it is not there yet) and runs it: synthetic
   screens, battle frames and movie calls on the stepped clock.
2. The setting across processes: defaults, values that are not 0, 1, 2 or 4,
   a save in one process read by the next, battle_speed kept through it all.
3. Negative controls: the same test built from the same commands over a copy
   of one production file with one line changed must fail, in the section
   that covers that line (and the unchanged copy must pass):
     no-movie-hook    pcFieldMovieBlock does not tell the controller
     hook-after-wide  ... or tells it what the wide picture made of `on`
     no-exit-reset    pcFieldWideExit leaves the movie flag
     state-dropped    the movie flag is not in a state
     movie-ignored    the controller does not look at the movie flag
     any-context      every screen but the field counts as the world map
     battle-leak      a battle takes the field's factor

No disc, GPU, emulator or wall clock. Exit 0 when everything held.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYSTEM = ROOT / "port/src/system"
WIDE = ROOT / "port/src/game/field/wide.c"
CONTROLLER = SYSTEM / "fieldspeed.c"
SHARED = SYSTEM / "battlespeed.c"
TEST = SYSTEM / "test/fieldspeedtest.c"
TARGET = "fieldspeedtest"

# name, file, the line as it is, the line instead, the section that must fail
HOOK = "    fieldSpeedMovie(on);\n"
WIDE_BLOCK = "    pcWideBlock(PC_WIDE_BLOCK_MOVIE, on);\n"
MUTANTS = [
    ("identity", WIDE, [], None),
    ("no-movie-hook", WIDE, [(HOOK, "")], "movie"),
    ("hook-after-wide", WIDE, [(HOOK, ""), (WIDE_BLOCK, HOOK + WIDE_BLOCK)], "movie"),
    ("no-exit-reset", WIDE, [("    fieldSpeedMovie(0); /* no movie plays across the mode's start or end */\n", "")],
     "movie lifecycle"),
    ("movie-ignored", CONTROLLER, [("    if (kind == PC_WIDE_FIELD && !fieldMovie) {\n",
                                    "    if (kind == PC_WIDE_FIELD) {\n")], "movie"),
    ("any-context", CONTROLLER, [("    if (kind == PC_WIDE_WORLDMAP) {\n", "    if (kind != PC_WIDE_FIELD) {\n")],
     "contexts"),
    ("battle-leak", SHARED, [("        factor = battleWait || textHold > 0 ? 1 : chosen;\n",
                              "        factor = battleWait || textHold > 0 ? 1 : fieldOn ? fieldSpeedChosen() : chosen;\n")],
     "battles"),
    ("state-dropped", SHARED, [("        fieldSpeedStateIo(s); /* the field's movie flag (fieldspeed.c) */\n",
                                "        (void)0;\n")], "state"),
]

failures = 0


def check(ok: bool, what: str, detail: str = "") -> bool:
    global failures
    if ok:
        print(f"ok    {what}")
    else:
        failures += 1
        print(f"FAIL  {what}" + (f"\n      {detail.strip()}" if detail.strip() else ""))
    return ok


def clean_env(**extra: str) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items()
           if k not in ("XG_FIELD_SPEED", "XG_BATTLE_SPEED", "XG_SETTINGS", "XG_BATTLE_SPEED_LOG", "XG_CLOCK")}
    env.update(extra)
    return env


def run(cmd, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)


# ---- 1. build and run ------------------------------------------------------------------------

def build(build_dir: Path, ver: str) -> Path | None:
    if not (build_dir / "CMakeCache.txt").is_file():
        p = run([sys.executable, ROOT / "tools/pc_sources.py"], cwd=ROOT)
        if p.returncode == 0:
            p = run(["cmake", "-S", ROOT / "port", "-B", build_dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                     f"-DXG_VERSION={ver.upper()}", "-DXG_EXTRAS=ON"], cwd=ROOT)
        if not check(p.returncode == 0, f"configured {build_dir}", p.stdout[-2000:] + p.stderr[-2000:]):
            return None
    exe = build_dir / TARGET
    was = exe.stat().st_mtime_ns if exe.is_file() else None
    p = run(["cmake", "--build", build_dir, "--target", TARGET], cwd=ROOT)
    if not check(p.returncode == 0 and exe.is_file(), f"built {TARGET} in {build_dir}",
                 p.stdout[-4000:] + p.stderr[-2000:]):
        return None
    now = exe.stat().st_mtime_ns
    print(f"      {TARGET}: " + ("built new" if was is None else "rebuilt" if now != was else
                                 "up to date with the sources (ninja had nothing to do)"))
    return exe


def suite(exe: Path, what: str) -> subprocess.CompletedProcess:
    p = run([exe], env=clean_env(), timeout=300)
    check(p.returncode == 0 and re.search(r"Field speed: \d+ checks passed", p.stdout) is not None, what,
          p.stdout + p.stderr)
    if p.returncode == 0:
        print(f"      {p.stdout.strip()}")
    return p


# ---- 2. the setting, a process at a time -----------------------------------------------------

def show(exe: Path, ini: Path, *args: str, **env: str) -> tuple[int, int] | None:
    p = run([exe, *(args or ("--show",))], env=clean_env(XG_SETTINGS=str(ini), **env), timeout=60)
    m = re.fullmatch(r"field_speed=(-?\d+) battle_speed=(-?\d+)\s*", p.stdout)
    if p.returncode != 0 or m is None:
        print(f"      {' '.join(args)}: exit {p.returncode}: {p.stdout.strip()} {p.stderr.strip()}")
        return None
    return int(m.group(1)), int(m.group(2))


def keys(ini: Path) -> dict[str, str]:
    out = {}
    if ini.is_file():
        for line in ini.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith(("#", ";")):
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


def settings(exe: Path, work: Path) -> None:
    ini = work / "settings.ini"
    ini.unlink(missing_ok=True)
    check(show(exe, ini) == (0, 0), "no settings file: field speed off, battle speed off")
    check(not ini.is_file() or "field_speed" not in keys(ini), "reading the default writes no field_speed")

    check(show(exe, ini, "--set-battle", "4") == (0, 4), "battle_speed=4 saved, field speed still off")
    check(show(exe, ini, "--set-field", "2") == (2, 4), "field_speed=2 saved beside it")
    check(keys(ini).get("field_speed") == "2" and keys(ini).get("battle_speed") == "4",
          "the file holds field_speed=2 and battle_speed=4", ini.read_text() if ini.is_file() else "no file")
    check(show(exe, ini) == (2, 4), "a new process reads field 2, battle 4")
    check(show(exe, ini, "--set-field", "9") == (4, 4) and keys(ini).get("field_speed") == "4"
          and keys(ini).get("battle_speed") == "4", "field_speed 9 is saved as 4; battle_speed kept")
    check(show(exe, ini, "--set-battle", "1") == (4, 1) and keys(ini).get("field_speed") == "4",
          "battle_speed changed to 1; field_speed kept")
    check(show(exe, ini, "--set-field", "0") == (0, 1) and keys(ini).get("field_speed") == "0"
          and keys(ini).get("battle_speed") == "1", "field speed turned off and saved; battle_speed kept")
    check(show(exe, ini) == (0, 1), "a new process reads field off, battle 1")

    # What a hand-edited file may hold: read as 0, 1, 2 or 4, the file left alone.
    for text, want in (("7", 4), ("4", 4), ("3", 2), ("2", 2), ("1", 1), ("0", 0), ("-5", 0), ("abc", 0), ("", 0),
                       ("0x4", 4), ("1000000", 4)):
        body = f"# a comment\nbattle_speed=2\nfield_speed={text}\nother_key=kept\n"
        ini.write_text(body)
        got = show(exe, ini)
        check(got == (want, 2) and ini.read_text() == body,
              f"field_speed={text!r} in the file reads as {want}, battle 2, file untouched", f"got {got}")
    ini.write_text("battle_speed=4\n")
    check(show(exe, ini) == (0, 4), "a file from before this setting: field off, battle 4")
    # The environment's override for one run (settings.h): read, never written.
    ini.write_text("battle_speed=2\nfield_speed=0\n")
    check(show(exe, ini, XG_FIELD_SPEED="4") == (4, 2) and keys(ini).get("field_speed") == "0",
          "XG_FIELD_SPEED=4 overrides the file for the run and is not written")
    check(show(exe, ini, XG_BATTLE_SPEED="4") == (0, 4), "XG_BATTLE_SPEED does not reach the field's setting")


# ---- 3. negative controls --------------------------------------------------------------------

def mutate(path: Path, edits: list[tuple[str, str]]) -> str | None:
    text = path.read_text()
    for old, _ in edits:
        if text.count(old) != 1:
            return None
    for old, new in edits:
        text = text.replace(old, new)
    return text


def commands(build_dir: Path) -> list[str] | None:
    p = run(["ninja", "-C", build_dir, "-t", "commands", TARGET], cwd=ROOT)
    if p.returncode != 0:
        return None
    lines = [c for c in p.stdout.splitlines() if f"CMakeFiles/{TARGET}" in c]
    compiles = [c for c in lines if " -c " in c]
    links = [c for c in lines if re.search(rf"-o {TARGET}(\s|$)", c)]
    return compiles + links if len(compiles) == 4 and len(links) == 1 else None


def negative(build_dir: Path, cmds: list[str], name: str, path: Path, edits: list[tuple[str, str]],
             section: str | None) -> None:
    text = mutate(path, edits)
    if not check(text is not None, f"control {name}: each line is in {path.relative_to(ROOT)} once"):
        return
    rel = f"fieldspeed-controls/{name}"
    top = build_dir / rel
    shutil.rmtree(top, ignore_errors=True)
    # The copy keeps its place beside the test (which includes ../battlespeed.c
    # and ../hw.c); what is not copied is found in the real directories.
    (top / "system/test").mkdir(parents=True)
    copy = top / "system" / path.name
    copy.write_text(text)
    swap = {str(path): str(copy)}
    if path == SHARED:
        shutil.copyfile(TEST, top / "system/test" / TEST.name)
        swap[str(TEST)] = str(top / "system/test" / TEST.name)
    include = f" -I{SYSTEM} -I{SYSTEM / 'test'} -I{WIDE.parent}"
    for c in cmds:
        for a, b in swap.items():
            c = c.replace(f"-c {a}", f"-c {b}")
        c = re.sub(rf"CMakeFiles/{TARGET}(_wide)?\.dir/", rf"{rel}/obj\1/", c)  # (the game-code object library too)
        c = re.sub(rf"-o {TARGET}(?=\s|$)", f"-o {rel}/{TARGET}", c)
        if " -c " in c:
            c += include
        for out in re.findall(r"(?:-o|-MF) (\S+)", c):
            (build_dir / out).parent.mkdir(parents=True, exist_ok=True)
        p = subprocess.run(c, shell=True, cwd=build_dir, capture_output=True, text=True)
        if p.returncode != 0:
            check(False, f"control {name}: built", p.stdout[-2000:] + p.stderr[-4000:])
            return
    used = top.rglob("*.d")
    seen = any(os.path.normpath(word) == str(copy) for d in used for word in d.read_text().split())
    if not check(seen, f"control {name}: the build read the copy {copy.relative_to(build_dir)}"):
        return
    p = run([top / TARGET], env=clean_env(), timeout=300)
    if section is None:
        check(p.returncode == 0 and "checks passed" in p.stdout, f"control {name}: the unchanged copy passes",
              p.stdout + p.stderr)
    else:
        where = [line for line in p.stderr.splitlines() if line.startswith("[")]
        check(p.returncode != 0 and "checks passed" not in p.stdout and bool(where)
              and where[0].startswith(f"[{section}] "),
              f"control {name}: the test fails in [{section}]" + (f": {where[0]}" if where else ""),
              f"exit {p.returncode}\n{p.stdout}{p.stderr}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--build", help="a configured build of that version (default build/pc/gates[-jp])")
    args = ap.parse_args()
    build_dir = Path(args.build).resolve() if args.build else ROOT / "build/pc" / ("gates" if args.ver == "us" else "gates-jp")

    exe = build(build_dir, args.ver)
    if exe is None:
        return 1
    cache = (build_dir / "CMakeCache.txt").read_text()
    check(re.search(rf"^XG_VERSION:\w+={args.ver.upper()}$", cache, re.M) is not None,
          f"{build_dir.name} is a {args.ver.upper()} build")
    suite(exe, f"{args.ver}: the controller, the shared vblank and the field's movie calls")

    work = build_dir / "fieldspeed-settings"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    settings(exe, work)

    cmds = commands(build_dir)
    if check(cmds is not None, f"ninja's commands for {TARGET} (4 compiles, 1 link)"):
        for m in MUTANTS:
            negative(build_dir, cmds, *m)

    print(f"field speed ({args.ver}): " + ("passed" if failures == 0 else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
