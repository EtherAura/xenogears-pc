#!/usr/bin/env python3
"""Story battle speed (xg-mrj.7, port/src/system/battlespeed.c): a scripted
battle starts at 1x and keeps a speed of its own; a random one runs at the
saved speed. The production controller, battle class (gameplay.c) and settings
on the stepped clock, CPU only.

    python tools/test_story_battle_speed.py --ver us|jp [--build DIR]

1. Builds port/src/system/test/storyspeedtest.c with the commands ninja has
   for battlespeedtest (the same sources beside the test: settings.c and
   gameplay.c; the same flags and SDL) in the gates' build of that version
   (build/pc/gates, build/pc/gates-jp), into DIR/storyspeed/, and runs it over
   a scratch settings file: once plain, once with XG_BATTLE_SPEED=4 over a
   saved 2 (the environment's hold, --override).
2. Negative controls: the same test built over a copy of one production file
   with one change must fail, in the section that covers it:
     old-policy          every battle runs at the saved speed
     no-start-reset      a battle start leaves the last scripted battle's speed
     retry-keeps-speed   ... or a retry's start does
     no-load-reset       a state's load leaves it
     step-writes         the control writes the setting in a scripted battle
     override-dropped    XG_BATTLE_SPEED no longer holds a scripted battle

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
CONTROLLER = SYSTEM / "battlespeed.c"
GAMEPLAY = SYSTEM / "gameplay.c"
TEST = SYSTEM / "test/storyspeedtest.c"
HOST = SYSTEM / "test/battlespeedtest.c"  # the target whose commands build the test
TARGET = "battlespeedtest"
EXE = "storyspeedtest"
TIMEOUT = 300

HOOK = "    if (battleSpeedBattleStart != NULL) battleSpeedBattleStart();\n"
CLASS = "    battleClass = !chained && pendingRandomConfig >= 0 && config == pendingRandomConfig ? GAMEPLAY_BATTLE_RANDOM\n"
STEP = "        story.speed = now == 1 ? 2 : now == 2 ? 4 : 1;\n"
# name, file, (the line as it is, the line instead)..., the run, the section that must fail
MUTANTS = [
    ("old-policy", CONTROLLER,
     [("    return battleSpeedChosen() > 0 && !story.exempt && pcGameplayBattleClass != NULL &&\n",
       "    return 0 && battleSpeedChosen() > 0 && !story.exempt && pcGameplayBattleClass != NULL &&\n")],
     "plain", "scripted"),
    ("no-start-reset", GAMEPLAY, [(HOOK, "")], "plain", "consecutive"),
    ("retry-keeps-speed", GAMEPLAY, [(HOOK, ""), (CLASS, HOOK + CLASS)], "plain", "retry"),
    ("no-load-reset", CONTROLLER, [("    if (xgStateLoading != NULL && xgStateLoading(s)) {\n",
                                    "    if (0 && xgStateLoading != NULL && xgStateLoading(s)) {\n")], "plain", "state"),
    ("step-writes", CONTROLLER, [(STEP, STEP + "        battleSpeedSetChosen(story.speed);\n")], "plain", "scripted"),
    ("override-dropped", CONTROLLER, [("        story.exempt = env != NULL && env[0] != '\\0';\n",
                                       "        story.exempt = 0;\n")], "override", "override"),
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
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=TIMEOUT, **kw)


def commands(build_dir: Path) -> list[str] | None:
    """ninja's commands for battlespeedtest: its test, settings.c and gameplay.c, and the link."""
    p = run(["ninja", "-C", build_dir, "-t", "commands", TARGET], cwd=ROOT)
    if p.returncode != 0:
        return None
    lines = [c for c in p.stdout.splitlines() if f"CMakeFiles/{TARGET}.dir/" in c]
    compiles = [c for c in lines if " -c " in c]
    links = [c for c in lines if re.search(rf"-o {TARGET}(\.exe)?(\s|$)", c)]
    if len(compiles) != 3 or len(links) != 1 or sum(f"-c {HOST}" in c for c in compiles) != 1 \
            or sum(f"-c {GAMEPLAY}" in c for c in compiles) != 1:
        return None
    return compiles + links


def build(build_dir: Path, cmds: list[str], name: str, path: Path | None, edits: list[tuple[str, str]]) -> Path | None:
    """The test in build_dir/storyspeed/<name>, over a changed copy of `path` (None: the files as they are)."""
    rel = f"storyspeed/{name}"
    top = build_dir / rel
    shutil.rmtree(top, ignore_errors=True)
    (top / "system/test").mkdir(parents=True)
    swap = {str(HOST): str(TEST)}
    copy = None
    if path is not None:
        text = path.read_text()
        if not check(all(text.count(old) == 1 for old, _ in edits),
                     f"control {name}: each line is in {path.relative_to(ROOT)} once"):
            return None
        for old, new in edits:
            text = text.replace(old, new)
        copy = top / "system" / path.name
        copy.write_text(text)
        if path == CONTROLLER:
            # The test includes ../battlespeed.c: its copy goes beside the changed file,
            # and what is not copied (../hw.c, ...) is found in the real directories.
            shutil.copyfile(TEST, top / "system/test" / TEST.name)
            swap[str(HOST)] = str(top / "system/test" / TEST.name)
        else:
            swap[str(path)] = str(copy)
    include = f" -I{SYSTEM} -I{SYSTEM / 'test'}"
    for c in cmds:
        for a, b in swap.items():
            c = c.replace(f"-c {a}", f"-c {b}")
        c = c.replace(f"CMakeFiles/{TARGET}.dir/", f"{rel}/obj/")
        c = re.sub(rf"-o {TARGET}(?=(\.exe)?(\s|$))", f"-o {rel}/{EXE}", c)
        if " -c " in c:
            c += include
        for out in re.findall(r"(?:-o|-MF) (\S+)", c):
            (build_dir / out).parent.mkdir(parents=True, exist_ok=True)
        p = subprocess.run(c, shell=True, cwd=build_dir, capture_output=True, text=True, timeout=TIMEOUT)
        if p.returncode != 0:
            check(False, f"{name}: built", p.stdout[-2000:] + p.stderr[-4000:])
            return None
    read = {os.path.normpath(word) for d in top.rglob("*.d") for word in d.read_text().split()}
    for used in (copy, CONTROLLER if path != CONTROLLER else None, GAMEPLAY if path != GAMEPLAY else None,
                 SYSTEM / "settings.c"):
        if used is not None and not check(str(used) in read, f"{name}: the build read {used.relative_to(ROOT)}"
                                          if used.is_relative_to(ROOT) and not used.is_relative_to(build_dir)
                                          else f"{name}: the build read the copy {used.relative_to(build_dir)}"):
            return None
    exes = [e for e in (top / EXE, top / f"{EXE}.exe") if e.is_file()]
    return exes[0] if check(bool(exes), f"{name}: built {rel}/{EXE}") else None


def launch(exe: Path, work: Path, mode: str) -> tuple[subprocess.CompletedProcess, Path]:
    """One run over a scratch settings file: plain, or XG_BATTLE_SPEED=4 over a saved 2."""
    work.mkdir(parents=True, exist_ok=True)
    ini = work / f"settings-{mode}.ini"
    ini.unlink(missing_ok=True)
    if mode == "override":
        ini.write_text("battle_speed=2\n")
        return run([exe, "--override"], env=clean_env(XG_SETTINGS=str(ini), XG_BATTLE_SPEED="4")), ini
    return run([exe], env=clean_env(XG_SETTINGS=str(ini))), ini


def keys(ini: Path) -> dict[str, str]:
    out = {}
    if ini.is_file():
        for line in ini.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith(("#", ";")):
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--build", help="a configured build of that version (default build/pc/gates[-jp])")
    args = ap.parse_args()
    build_dir = Path(args.build).resolve() if args.build else ROOT / "build/pc" / ("gates" if args.ver == "us" else "gates-jp")

    cache = build_dir / "CMakeCache.txt"
    if not check(cache.is_file(), f"{build_dir} is a configured build (tools/gates.sh, tools/test_field_speed.py make it)"):
        return 1
    check(re.search(rf"^XG_VERSION:\w+={args.ver.upper()}$", cache.read_text(), re.M) is not None,
          f"{build_dir.name} is a {args.ver.upper()} build")
    cmds = commands(build_dir)
    if not check(cmds is not None, f"ninja's commands for {TARGET} (3 compiles, 1 link)"):
        return 1
    check(all(f"-DVERSION_{args.ver.upper()}" in c for c in cmds[:3]), f"they compile for VERSION_{args.ver.upper()}")

    exe = build(build_dir, cmds, "production", None, [])
    if exe is None:
        return 1
    work = build_dir / "storyspeed/settings"
    shutil.rmtree(work, ignore_errors=True)
    for mode, said in (("plain", r"Story battle speed: \d+ checks passed$"),
                       ("override", r"Story battle speed: \d+ checks passed \(override\)$")):
        p, ini = launch(exe, work / "production", mode)
        if check(p.returncode == 0 and re.search(said, p.stdout.strip()) is not None,
                 f"{args.ver}: the production controller, {mode}", p.stdout + p.stderr):
            print(f"      {p.stdout.strip()}")
        got = keys(ini)
        # plain ends with the setting off; under the override the control's step wrote 1 (as before this policy).
        check(got.get("battle_speed") == ("0" if mode == "plain" else "1") and not any("story" in k for k in got),
              f"{mode}: the file holds battle_speed and no story speed", str(got))

    for name, path, edits, mode, section in MUTANTS:
        exe = build(build_dir, cmds, name, path, edits)
        if exe is None:
            continue
        p, _ = launch(exe, work / name, mode)
        where = [line for line in p.stderr.splitlines() if line.startswith("[")]
        check(p.returncode != 0 and "checks passed" not in p.stdout and bool(where)
              and where[0].startswith(f"[{section}] "),
              f"control {name}: the test fails in [{section}]" + (f": {where[0]}" if where else ""),
              f"exit {p.returncode}\n{p.stdout}{p.stderr}")

    print(f"story battle speed ({args.ver}): " + ("passed" if failures == 0 else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
