#!/usr/bin/env python3
"""Battle speed (xg-mrj.2, port/src/system/battlespeed.c): a scripted battle at
1x, 2x and 4x reaches the same states in the same order as without it.

    python tools/test_battle_speed.py BUILD OUT [--jp] [--until N] [--no-real]
                                      [--baseline OTHER_BUILD]

The battle selector's scene of tools/statecheck.py (XG_BOOT_MODE=2, its pad
script, the fight's Cross as Circle on the JP disc: the same input on every
game frame of every run), on the stepped clock, four runs: battle speed off
(the original), on at 1x, 2x and 4x.

  - on at 1x is the original to the bit: the battle's state log (XG_SWEEP_LOG),
    the state at every vblank (XG_INVARIANT_LOG), every picture and the whole
    sound (XG_AUDIO_DUMP);
  - at 2x and 4x the state logs and the pictures of every compared vblank are
    the original's (the game's frames are the same, only fewer of them are
    shown), the factor was in effect for most of the battle (the speed log,
    XG_BATTLE_SPEED_LOG), and the music keeps its tempo: every key-on of the
    music's voices (0-15; the sound effects have 16-23) comes at the same time
    of the audio clock as in the original, the sound is shorter by what the
    battle's faster stretches saved;
  - then 4x on the real clock (SDL's disk audio device, which plays in real
    time): the vblanks of the 4x stretches come four times as often, the
    sound device never runs dry (XG_CPU_TIMING's underrun and dry counts).

--baseline: also the same scene with another build (one without battle
speed: the commit before it), which must be the off run to the bit.

Prints one line per check and 'battle-speed: N checks ok' (exit 0), or the
failures (exit 1).
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecheck  # noqa: E402

PERIOD = 263 / (53693175 / 3412.5)  # the NTSC 240-line vblank (hw.h), seconds
MUSIC_VOICES = range(16)
FRAMES = "1000-{until}/3"  # the pictures compared: every third vblank of the battle


def run(build: Path, out: Path, speed: int | None, until: int, jp: bool, real: bool = False) -> int:
    out.mkdir(parents=True, exist_ok=True)
    for name in ("sweep.txt", "inv.txt", "speed.txt", "audio.pcm", "port.log", "device.raw"):
        (out / name).unlink(missing_ok=True)
    (out / "cards").mkdir(exist_ok=True)
    frames = out / "frames"
    frames.mkdir(exist_ok=True)
    for f in frames.iterdir():
        f.unlink()
    statecheck.pad_battle(out / "pad.txt")
    if jp:
        # The fight's confirm is Circle on the JP disc (the US one swaps it
        # with Cross): the same presses, Cross as Circle from the fight on.
        lines = []
        for line in (out / "pad.txt").read_text().splitlines():
            v, b = line.split()
            lines.append(f"{v} {'0020' if int(v) >= 1000 and b == '0040' else b}")
        (out / "pad.txt").write_text("\n".join(lines) + "\n")
    e = dict(os.environ)
    e.update({
        "XG_HEADLESS": "1", "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy", "XG_SETTINGS": "none",
        "XG_QUIT_AT": str(until), "XG_CARD_DIR": str(out / "cards"), "XG_VK_DEVICE": e.get("XG_VK_DEVICE", "intel"),
        "TMPDIR": str(out), "XG_BOOT_MODE": "2", "XG_PAD_SCRIPT": str(out / "pad.txt"),
        "XG_SWEEP_LOG": str(out / "sweep.txt"), "XG_INVARIANT_LOG": str(out / "inv.txt"),
        "XG_BATTLE_SPEED_LOG": str(out / "speed.txt"), "XG_AUDIO_DUMP": str(out / "audio.pcm"),
    })
    e.pop("XG_BATTLE_SPEED", None)
    if speed is not None:
        e["XG_BATTLE_SPEED"] = str(speed)
    if real:
        # The disk driver plays in real time (it sleeps a whole millisecond per
        # 882-frame request: 20 ms at 44.1 kHz).
        e.update({"SDL_AUDIO_DRIVER": "disk", "SDL_AUDIO_DEVICE_SAMPLE_FRAMES": "882", "XG_CPU_TIMING": "1",
                  "SDL_DISK_AUDIO_FILE": str(out / "device.raw")})
    else:
        e.update({"XG_CLOCK": "step", "XG_SPU_LOG": "1", "XG_DUMP_FRAMES": str(frames),
                  "XG_DUMP_RANGE": FRAMES.format(until=until)})
    cmd, _ = statecheck.executable(build)
    disc = ROOT / statecheck.DISCS["jp1" if jp else "us1"]
    # The real-clock run measures pacing: on the Intel GPU it waits for the
    # GPU's lock (tools/intel.sh), so another run there can't halve its speed.
    lock = [str(ROOT / "tools/gpu_lock.sh")] if real and e.get("XG_VK_DEVICE") == "intel" else []
    with open(out / "port.log", "w") as log:
        p = subprocess.run([*lock, "nice", "-n", "19", "timeout", "-s", "INT", "-k", "30", "900", *cmd, str(disc)],
                           env=e, stdout=log, stderr=subprocess.STDOUT, cwd=out)
    (out / "device.raw").unlink(missing_ok=True)
    return p.returncode


def frame_hashes(frames: Path) -> dict[int, str]:
    out = {}
    for f in sorted(frames.iterdir()):
        m = re.match(r"frame_(\d+)\.png$", f.name)
        if m:
            out[int(m.group(1))] = statecheck.png_pixels_hash(f)
    return out


def speed_log(out: Path) -> list[tuple[int, int, float, float]]:
    """(vblank, factor from it on, game time, audio time) per battle vblank (XG_BATTLE_SPEED_LOG)."""
    rows = []
    if (out / "speed.txt").exists():
        for line in (out / "speed.txt").read_text().splitlines():
            v = line.split()
            rows.append((int(v[0]), int(v[1]), float(v[6]), float(v[7])))
    return rows


def key_ons(out: Path) -> list[tuple[float, int]]:
    """(audio time, voice) of every key-on in the SPU log (XG_SPU_LOG=1 prints
    the game's time: the speed log's anchors map it to the audio clock;
    before the first one the two are the same, after the last one they stay
    that far apart)."""
    rows = speed_log(out)
    games = [r[2] for r in rows]
    ons = []
    for line in (out / "port.log").read_text(errors="replace").splitlines():
        m = re.match(r"spu\s+([\d.]+): key on ([0-9a-f]+) off", line)
        if not m:
            continue
        t, mask = float(m.group(1)), int(m.group(2), 16)
        i = bisect.bisect_right(games, t) - 1
        a = t if i < 0 else rows[i][3] + (t - rows[i][2]) / rows[i][1]
        ons += [(a, v) for v in range(24) if mask >> v & 1]
    return ons


def music_matches(base: list, fast: list, start: float, end: float, tol: float = 0.001) -> tuple[int, int, float]:
    """Music key-ons of the original between start and end (audio times) that
    the fast run has at the same audio time: matched, total, worst offset."""
    worst, matched, total = 0.0, 0, 0
    for v in MUSIC_VOICES:
        a = sorted(t for t, vv in base if vv == v and start <= t < end)
        b = sorted(t for t, vv in fast if vv == v and start - 1 <= t < end + 1)
        for t in a:
            total += 1
            i = bisect.bisect_left(b, t - tol)
            if i < len(b) and abs(b[i] - t) <= tol:
                matched += 1
                worst = max(worst, abs(b[i] - t))
    return matched, total, worst


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--jp", action="store_true")
    ap.add_argument("--until", type=int, default=3000, help="the last vblank of each run")
    ap.add_argument("--no-real", action="store_true", help="skip the real-clock run")
    ap.add_argument("--baseline", help="a build without battle speed: its run must be the off run's")
    a = ap.parse_args()
    build = Path(a.build).resolve()
    out = Path(a.out).resolve()
    disc = ROOT / statecheck.DISCS["jp1" if a.jp else "us1"]
    if not disc.exists():
        print(f"battle-speed: no disc image {disc}")
        return 1

    runs = {"off": None, "1x": 1, "2x": 2, "4x": 4}
    builds = {n: build for n in runs}
    if a.baseline:
        runs["baseline"] = None
        builds["baseline"] = Path(a.baseline).resolve()
    with ThreadPoolExecutor(max_workers=len(runs)) as pool:
        codes = dict(zip(runs, pool.map(lambda kv: run(builds[kv[0]], out / kv[0], kv[1], a.until, a.jp), runs.items())))
    ok, bad = [], []

    def check(cond: bool, what: str) -> None:
        (ok if cond else bad).append(what)
        print(f"  {'ok' if cond else 'FAIL':4} {what}", flush=True)

    for name, code in codes.items():
        check(code == 0, f"{name}: the run ends at vblank {a.until} (exit {code})")
    if bad:
        print(f"battle-speed: {len(bad)} failed")
        return 1

    def read(name: str, file: str) -> bytes:
        return (out / name / file).read_bytes()

    sweep = {n: read(n, "sweep.txt") for n in runs}
    inv = {n: read(n, "inv.txt") for n in runs}
    frames = {n: frame_hashes(out / n / "frames") for n in runs}
    battle = [l for l in sweep["off"].decode().splitlines() if l.startswith("B ")]
    check(len(battle) > 500, f"the battle runs: {len(battle)} battle frames by vblank {a.until}")

    if a.baseline:
        check(sweep["baseline"] == sweep["off"] and inv["baseline"] == inv["off"],
              "off: the battle's states and every vblank's state are the build without battle speed's")
        check(read("baseline", "audio.pcm") == read("off", "audio.pcm"), "off: the sound is the build without battle speed's to the bit")
        check(frames["baseline"] == frames["off"], f"off: the {len(frames['off'])} pictures compared are the build without battle speed's")

    # On at 1x: the original to the bit.
    check(sweep["1x"] == sweep["off"] and inv["1x"] == inv["off"], "on at 1x: the battle's states and every vblank's state are the original's")
    check(read("1x", "audio.pcm") == read("off", "audio.pcm"), "on at 1x: the sound is the original's to the bit")
    check(frames["1x"] == frames["off"] and len(frames["off"]) > 100, f"on at 1x: the {len(frames['off'])} pictures compared are the original's")
    check(all(r[1] == 1 and r[2] == r[3] for r in speed_log(out / "off")) and all(r[1] == 1 for r in speed_log(out / "1x")),
          "off and on at 1x: the factor stays 1, the audio clock is the game's")

    audio_off = len(read("off", "audio.pcm")) // 4
    off_ons = key_ons(out / "off")
    for name, k in (("2x", 2), ("4x", 4)):
        check(sweep[name] == sweep["off"], f"{name}: the battle's states (XG_SWEEP_LOG, every battle frame) are the original's, in the same order")
        check(inv[name] == inv["off"], f"{name}: the state at every vblank (XG_INVARIANT_LOG) is the original's")
        check(frames[name] == frames["off"], f"{name}: the {len(frames[name])} pictures compared are the original's")
        rows = speed_log(out / name)
        fast = sum(1 for r in rows if r[1] == k)
        check(fast > len(rows) * 0.6, f"{name}: {fast} of the battle's {len(rows)} vblanks at {k}x (1x while it waits for the player or a name comes up)")
        saved = rows[-1][2] - rows[-1][3] if rows else 0  # game time less audio time: what the battle saved
        frames_audio = len(read(name, "audio.pcm")) // 4
        expect = audio_off - saved * 44100
        check(saved > 1 and abs(frames_audio - expect) < 2 * 44100 * PERIOD,
              f"{name}: the sound is {frames_audio} frames, the original's less the {saved:.2f} s the battle saved ({expect:.0f})")
        ons = key_ons(out / name)
        start = 1000 * PERIOD
        end = max(t for t, _ in ons) - 0.1
        matched, total, worst = music_matches(off_ons, ons, start, end)
        check(total > 100 and matched == total,
              f"{name}: music key-ons at the original's audio times: {matched} of {total} (worst {worst * 1e3:.3f} ms)")

    if not a.no_real:
        real = out / "4x-real"
        code = run(build, real, 4, a.until, a.jp, real=True)
        check(code == 0, f"4x real clock: the run ends (exit {code})")
        log = (real / "port.log").read_text(errors="replace")
        m = re.search(r"(\d+) dry audio pushes, (\d+) audio cuts, (\d+) device underruns", log)
        check(m is not None and m.group(1) == "0" and m.group(3) == "0",
              f"4x real clock: the sound device never ran dry ({m.group(0) if m else 'no summary'})")
        fac = {r[0]: r[1] for r in speed_log(real)}
        walls = []
        for line in log.splitlines():
            w = re.match(r"cpu: vb (\d+) wall ([\d.]+)", line)
            if w and fac.get(int(w.group(1)) - 1) == 4 and fac.get(int(w.group(1))) == 4:
                walls.append(float(w.group(2)))
        mean = sum(walls) / len(walls) if walls else 0
        check(len(walls) > 500 and abs(mean - PERIOD * 1e3 / 4) < PERIOD * 1e3 / 4 * 0.1,
              f"4x real clock: {len(walls)} vblanks at 4x came {mean:.3f} ms apart (a quarter of {PERIOD * 1e3:.3f})")

    # The pictures are the bulk of the scratch: drop them.
    for name in runs:
        for f in (out / name / "frames").iterdir():
            f.unlink()
    if bad:
        print(f"battle-speed: {len(bad)} failed, {len(ok)} ok")
        return 1
    print(f"battle-speed: {len(ok)} checks ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
