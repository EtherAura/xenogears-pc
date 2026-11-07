#!/usr/bin/env python3
"""Replay the same input on two engines and compare what came out (docs/ENGINE_PLAN.md).

    python tools/engine_diff.py --a build/pc/release --b build/pc/enhanced --scene field --scene battle
    python tools/engine_diff.py ... --jp                      # the JP builds
    python tools/engine_diff.py ... --field 8 --field 13      # any field (XG_FIELD, random walking)
    python tools/engine_diff.py ... --fields 0-99             # a range of field ids (absent ones skipped)
    python tools/engine_diff.py ... --present 40              # the first 40 fields present on disc 1
    python tools/engine_diff.py ... --battle 3:0:1            # the selector: file 3, scene 0, gear on
    python tools/engine_diff.py ... --jobs 4 --vblanks 1500 --prune --out work/engine-diff --env XG_UPSCALE=2
    python tools/engine_diff.py ... --tolerate 16 --ignore frames,audio   # a feature that moves things on purpose

The oracle of the Enhanced engine: it cannot bit-match the PS1 the way the
Original does, so it is measured against the Original. Each scene runs
headless on both builds with the same scripted pad, the stepped clock and
every dump, to its end vblank M. Compared, in this order:

  log      the invariant log (XG_INVARIANT_LOG, port/src/system/invariants.c):
           per vblank the screen kind, a hash of gameState, the field
           variables and the leader's position, the world map's entries,
           and every memory-card frame written;
  sweep    the content-sweep collectors (XG_SWEEP_LOG, port/src/game/*/
           sweep.c): per field frame the random seed, the camera, every
           actor's position, script PC and animation; per battle frame the
           seed, the acting entity, the turn, AP, every entity's HP, fuel and
           status, and the camera;
  frames   the picture at every vblank where either build changed it;
  audio    the PCM dump, byte for byte.

The first differing line of each log is printed with its vblank. At the
fork point everything must be identical: that is the first fixture. Later, a
feature that changes pictures on purpose is expected to differ in frames and
equal in the logs, and this reports which.

Scenes: tools/statecheck.py's SCENES (field, battle, worldmap, menu, movie,
opening, battling, disc2, ...), plus ad-hoc fields (--field, --fields,
--present: XG_FIELD with the default party and random walking presses) and
selector battles (--battle FILE:SCENE[:GEAR], the debug selector's rows as
work/agents/PORT_M6/battles/bpad.py drove them, then Triangle/Cross mashing).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecheck as sc  # noqa: E402

CROSS, CIRCLE, TRIANGLE, DOWN, RIGHT, START = 0x0040, 0x0020, 0x0010, 0x4000, 0x2000, 0x0800


# ---- scenes -------------------------------------------------------------------------------


def selector_presses(file: int, scene: int, gear: int) -> list[int]:
    """The debug battle selector's rows (US, battleHandleInput): scene from 3 (Cross +,
    Circle -), the party row, the gear row (Cross per step, Right per member), the
    file row (Triangle +10, Cross +1; Circle from 0 wraps to the events)."""
    p = []
    up = (scene - 3) % 16
    p += [CROSS] * up if up <= 8 else [CIRCLE] * (16 - up)
    p.append(DOWN)
    p.append(DOWN)
    if gear:
        for i in range(3):
            if i:
                p.append(RIGHT)
            p += [CROSS] * gear
    p.append(DOWN)
    if file >= 0:
        p += [TRIANGLE] * (file // 10) + [CROSS] * (file % 10)
    else:
        p += [CIRCLE] * (-file)
    return p


def pad_selector(path: Path, file: int, scene: int, gear: int, end: int) -> None:
    lines = []
    t = 500
    for b in selector_presses(file, scene, gear):
        lines += [f"{t} {b:04x}", f"{t + 6} 0000"]
        t += 12
    start = t + 30
    lines += [f"{start} {START:04x}", f"{start + 8} 0000"]
    v, k = start + 240, 0
    while v < end:
        b = TRIANGLE if k % 2 == 0 else CROSS
        lines += [f"{v} {b:04x}", f"{v + 6} 0000"]
        v, k = v + 20, k + 1
    path.write_text("\n".join(lines) + "\n")


def scene_spec(name: str, vblanks: int | None) -> dict:
    """disc ('1'/'2'), m, env, and how to write the pad: ('named', scene) | ('field', id) | ('battle', f, s, g)."""
    if name in sc.SCENES:
        s = sc.SCENES[name]
        return dict(disc=s["disc"], m=vblanks or s["m"], env=dict(s["env"]), pad=("named", name))
    if name.startswith("f") and name[1:].isdigit():
        fid = int(name[1:])
        return dict(disc="1", m=vblanks or 1200, env={"XG_FIELD": str(fid), "XG_PARTY": "0"}, pad=("field", fid))
    if name.startswith("b"):
        parts = [int(x) for x in name[1:].split(":")]
        file, scene, gear = parts[0], parts[1], parts[2] if len(parts) > 2 else 0
        return dict(disc="1", m=vblanks or 3000, env={"XG_BOOT_MODE": "2"}, pad=("battle", file, scene, gear))
    raise SystemExit(f"unknown scene {name}")


def write_pad(out: Path, spec: dict) -> None:
    kind = spec["pad"]
    if kind[0] == "named":
        s = sc.SCENES[kind[1]]
        seed, buttons = s["pad"], s.get("buttons")
        if seed == "battle":
            sc.pad_battle(out / "pad.txt")
        elif isinstance(seed, tuple):
            sc.pad_steady(out / "pad.txt", seed[1])
        elif seed is not None:
            sc.pad_script(out / "pad.txt", seed, buttons=buttons)
        else:
            (out / "pad.txt").write_text("0 0000\n")
    elif kind[0] == "field":
        # Walking and talking: the D-pad, Cross and Triangle (the menu) from 400.
        sc.pad_script(out / "pad.txt", 7 + kind[1], start=400, buttons=["2000", "8000", "1000", "4000", "0040", "0010"])
    else:
        pad_selector(out / "pad.txt", kind[1], kind[2], kind[3], spec["m"])


def present_fields(jp: bool, count: int) -> list[int]:
    """The first `count` field ids whose archive is present on disc 1 (the coverage tool's rule)."""
    import interpolation_coverage as ic  # noqa: E402 (the disc reader)

    key = ("jp" if jp else "us") + "1"
    rows = ic.inventory([key])["fields"]
    ids = [r["field"] for r in rows if r["availability"] == "present"]
    return ids[:count] if count > 0 else ids


# ---- comparison -----------------------------------------------------------------------------


def first_diff(a: bytes, b: bytes) -> int | None:
    n = min(len(a), len(b))
    for i in range(0, n, 4096):
        if a[i:i + 4096] != b[i:i + 4096]:
            return next(j for j in range(i, min(i + 4096, n)) if a[j] != b[j])
    return None if len(a) == len(b) else n


# Which numbers of a log line are positions (world units) that --tolerate may let
# differ by up to N: everything else (hashes, ids, script PCs, HP, turns, flags)
# stays exact. Invariant lines: "p actor x y z", "w x y z hash". Sweep F lines
# (field/sweep.c): "F vblank frame field actors seed eye(3) target(3) hash" then
# per actor seven tokens from 13 whose first three are the position. Sweep B lines
# (battle/sweep.c): the camera's eye, at and their targets are the twelve
# tokens before the last four (battleIR0, mode, interpolation, mecha load).
def position_slots(tokens: list[str]) -> set[int]:
    tag = tokens[0] if tokens else ""
    slots: set[int] = set()
    if tag == "v":
        for i, t in enumerate(tokens):
            if t == "p" and i + 4 < len(tokens):
                slots |= {i + 2, i + 3, i + 4}
            elif t == "w" and i + 4 < len(tokens):
                slots |= {i + 1, i + 2, i + 3}
    elif tag == "F" and len(tokens) >= 13:
        slots |= set(range(6, 12))
        for i in range(13, len(tokens) - 2, 7):
            slots |= {i, i + 1, i + 2}
    elif tag == "B" and len(tokens) >= 16:
        slots |= set(range(len(tokens) - 16, len(tokens) - 4))
    return slots


def lines_equal(x: str, y: str, tolerate: int) -> bool:
    if x == y:
        return True
    if tolerate <= 0:
        return False
    a, b = x.split(), y.split()
    if len(a) != len(b):
        return False
    slots = position_slots(a)
    for i, (s, t) in enumerate(zip(a, b)):
        if s == t:
            continue
        if i not in slots:
            return False
        try:
            if abs(int(s) - int(t)) > tolerate:
                return False
        except ValueError:
            return False
    return True


def log_diff(la: Path, lb: Path, what: str, tolerate: int = 0) -> str | None:
    if not la.exists() and not lb.exists():
        return f"{what}: neither build wrote one"
    if la.exists() != lb.exists():
        return f"{what}: only one build wrote one"
    a, b = la.read_text().splitlines(), lb.read_text().splitlines()
    for i, (x, y) in enumerate(zip(a, b)):
        if not lines_equal(x, y, tolerate):
            xs, ys = (x[:160] + "...") if len(x) > 160 else x, (y[:160] + "...") if len(y) > 160 else y
            return f"{what}: line {i + 1} of {len(a)}/{len(b)}:\n    a: {xs}\n    b: {ys}"
    if len(a) != len(b):
        return f"{what}: {len(a)} vs {len(b)} lines (the first {min(len(a), len(b))} equal)"
    return None


def prune(out: Path) -> None:
    """Drop the bulky dumps of a compared scene (frames, audio), keeping the logs and pad."""
    import shutil

    for r in "ab":
        shutil.rmtree(out / r / "frames", ignore_errors=True)
        (out / r / "audio.pcm").unlink(missing_ok=True)


def run_scene(a: Path, b: Path, out: Path, name: str, jp: bool, extra: dict, vblanks: int | None,
              keep: bool = True, tolerate: int = 0, ignore: frozenset = frozenset()) -> list[str]:
    spec = scene_spec(name, vblanks)
    out.mkdir(parents=True, exist_ok=True)
    write_pad(out, spec)
    region = "jp" if jp else "us"
    other = {"1": "2", "2": "1"}
    env = {**spec["env"], **extra, "XG_PAD_SCRIPT": str(out / "pad.txt"),
           "XG_INVARIANT_LOG": "invariants.log", "XG_SWEEP_LOG": "sweep.log"}
    env.setdefault("XG_DISC2", str(ROOT / sc.DISCS[region + other[spec["disc"]]]))
    m = spec["m"]
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        fa = pool.submit(sc.run_port, a, out / "a", region + spec["disc"], m + 60, env)
        fb = pool.submit(sc.run_port, b, out / "b", region + spec["disc"], m + 60, env)
        ca, cb = fa.result(), fb.result()
    # The same crash on both engines (a cold field start the game cannot take, say) is
    # not a difference between them: it is noted, and the logs up to it are compared.
    problems = [f"exit codes {ca} / {cb}"] if ca != cb else []
    notes = [f"both exited {ca}"] if ca == cb != 0 else []
    for what, fn in (("invariants", "invariants.log"), ("sweep", "sweep.log")):
        d = log_diff(out / "a" / fn, out / "b" / fn, what, tolerate)
        if d and not d.endswith("neither build wrote one"):
            problems.append(d)
    if "frames" not in ignore:
        problems += sc.compare_frames(out / "a" / "frames", out / "b" / "frames", 0, m)
    if "audio" not in ignore:
        audio = [(out / r / "audio.pcm").read_bytes() if (out / r / "audio.pcm").exists() else b"" for r in "ab"]
        at = first_diff(*audio)
        if at is not None:
            problems.append(f"audio differs from byte {at} ({len(audio[0])} vs {len(audio[1])} bytes)")
    if not keep and not problems:
        prune(out)
    return problems + [f"note: {n}" for n in notes] if problems else (["note: " + n for n in notes] if notes else [])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", default="build/pc/release", help="the Original engine's build")
    ap.add_argument("--b", default="build/pc/enhanced", help="the Enhanced engine's build")
    ap.add_argument("--scene", action="append", default=[], choices=list(sc.SCENES),
                    help="a statecheck scene (default when nothing else is asked: field battle worldmap menu)")
    ap.add_argument("--field", action="append", type=int, default=[], help="an ad-hoc field scene by id")
    ap.add_argument("--fields", help="a range of field ids, A-B (fields absent from the disc are skipped)")
    ap.add_argument("--present", type=int, help="the first N fields present on disc 1 (0: all)")
    ap.add_argument("--battle", action="append", default=[], help="a selector battle FILE:SCENE[:GEAR]")
    ap.add_argument("--vblanks", type=int, help="end every scene at this vblank instead of its default")
    ap.add_argument("--jobs", type=int, default=1, help="scene pairs run at once (each pair is two processes)")
    ap.add_argument("--tolerate", type=int, default=0, metavar="N",
                    help="let positions in the logs (actors, cameras, world-map entries) differ by up to N "
                         "units; hashes, ids, script PCs, HP and flags stay exact")
    ap.add_argument("--ignore", default="", metavar="frames,audio",
                    help="comparisons a feature is expected to fail (pictures or sound changed on purpose)")
    ap.add_argument("--prune", action="store_true",
                    help="delete the frames and audio of scenes that came out the same (sweeps: ~50 MB a run)")
    ap.add_argument("--jp", action="store_true")
    ap.add_argument("--out", default="work/engine-diff")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE", help="for both runs")
    args = ap.parse_args()
    a, b = (ROOT / p if not Path(p).is_absolute() else Path(p) for p in (args.a, args.b))
    out = ROOT / args.out if not Path(args.out).is_absolute() else Path(args.out)
    extra = dict(e.split("=", 1) for e in args.env)

    names = list(args.scene)
    names += [f"f{n}" for n in args.field]
    if args.fields:
        lo, hi = (int(x) for x in args.fields.split("-"))
        present = set(present_fields(args.jp, 0))
        names += [f"f{n}" for n in range(lo, hi + 1) if n in present]
    if args.present is not None:
        names += [f"f{n}" for n in present_fields(args.jp, args.present)]
    names += [f"b{s}" for s in args.battle]
    if not names:
        names = ["field", "battle", "worldmap", "menu"]
    region = "jp-" if args.jp else "us-"

    results: dict[str, list[str]] = {}
    with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
        ignore = frozenset(x for x in args.ignore.split(",") if x)
        futures = {pool.submit(run_scene, a, b, out / region / n, n, args.jp, extra, args.vblanks, not args.prune,
                               args.tolerate, ignore): n for n in names}
        for f in concurrent.futures.as_completed(futures):
            n = futures[f]
            try:
                problems = f.result()
            except Exception as e:  # noqa: BLE001 (a failed run is a result)
                problems = [f"failed: {e}"]
            results[n] = [p for p in problems if not p.startswith("note: ")]
            notes = [p for p in problems if p.startswith("note: ")]
            status = "same" if not results[n] else "DIFFERS"
            print(f"{n}: {status}" + "".join(f"\n  {p}" for p in results[n] + notes), flush=True)
    bad = sum(1 for p in results.values() if p)
    print(f"{len(results)} scenes, {len(results) - bad} same, {bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
