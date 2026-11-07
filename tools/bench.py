#!/usr/bin/env python3
"""Benchmark the port: scripted scenes in real time, CPU and GPU time per vblank (docs/ENGINE_PLAN.md).

    python tools/bench.py                                   # both engines, US, the default scenes, 1x/4x/12x
    python tools/bench.py --build build/pc/release --device NVIDIA --upscale 1,4,12
    python tools/bench.py --jp --build build/pc/release-jp --build build/pc/enhanced-jp
    python tools/bench.py --scene f13 --scene b3:0:1 --vblanks 900 --device Intel --env XG_ASPECT=16:9
    python tools/bench.py --window                          # on the desktop (a real window), not headless
    python tools/bench.py --pin 2 --spin 3                  # a steady clock: game on CPU 2, a spinner on its sibling

The game thread sleeps most of every vblank (vsync_idle), so a powersave
governor runs it at a low clock after each wake-up: the CPU buckets, the SPU's
above all, come out two to four times larger than at full clock and vary from
vblank to vblank (xg-hbc.9: 0.4 vs 1.6 ms, 1.3 GHz vs 4.9 GHz measured with
perf stat on an i9-12900K). For A/B comparisons of CPU work, --pin CPU runs the
game under taskset on that CPU and --spin CPU keeps a busy process on another
(the pinned core's sibling thread holds the core at full clock); the numbers
are then the code's, not the governor's.

Every scene runs on the real clock (the game paced by the host, as a player
sees it) with XG_CPU_TIMING=summary and XG_GPU_TIMING=summary, headless
(SDL's offscreen video) on the Vulkan device asked for, and quits at its end
vblank. What is kept per run, from the two summaries:

  wall     mean/worst host milliseconds per vblank (16.71 is the native rate;
           more means the game could not keep up: 1000/mean is the achieved
           vblank rate), the vblanks delivered more than a frame late, and
           the dry audio pushes (audible gaps);
  cpu      the game thread's CPU time per vblank, split: game (the game's
           own code and the virtual PS1), gp0 (GPU command decoding), spans
           (the CPU rasteriser), cache (texture/CLUT cache emulation), spu,
           present, gpuwait, state (save-state/rewind captures), idle;
  gpu      GPU time per vblank from timestamp queries, split by pass kind:
           draw (the exact VRAM), wide, hires/hiwide (the hi-res copy),
           hiutil, copy, present.

The scenes are the oracle's (tools/engine_diff.py: statecheck scenes, fields
"fN", selector battles "bFILE:SCENE[:GEAR]"), with the same pads, so a
benchmark run and an engine_diff run see the same game. Defaults: field 13
(walking), the world map, a Gear battle (file 3, scene 0, attack mashing),
the menu and a movie.

Results: build/bench/<date>-<device>[-jp].json (every run, every number) and
.md (the table), printed too. Two engines or two builds side by side is the
point: pass --build twice.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import engine_diff as ed  # noqa: E402
import statecheck as sc  # noqa: E402

DEFAULT_SCENES = ["f13", "worldmap", "b3:0:1", "menu", "movie"]
CPU_LINE = re.compile(r"cpu: (\d+) vblanks, mean/worst ms per vblank:((?: \w+ [\d.]+/[\d.]+)+)")
LATE_LINE = re.compile(r"cpu: vblanks delivered late: (\d+) by more than 1 ms, (\d+) by more than a frame "
                       r"\(worst ([\d.]+) ms\); (-?\d+) dry audio pushes")
GPU_LINE = re.compile(r"gpu time: (\d+) vblanks, mean ([\d.]+) ms, worst ([\d.]+) ms; per kind mean/worst:"
                      r"((?: \w+ [\d.]+/[\d.]+)*)")
PAIRS = re.compile(r" (\w+) ([\d.]+)/([\d.]+)")


def engine_of(build: Path) -> str:
    return "enhanced" if (build / "xenogears-enhanced").exists() or (build / "xenogears-enhanced.exe").exists() \
        else "original"


def run_one(build: Path, out: Path, scene: str, jp: bool, upscale: str, device: str, window: bool,
            vblanks: int | None, extra: dict, warmup: int = 180, pin: int | None = None) -> dict:
    spec = ed.scene_spec(scene, vblanks)
    out.mkdir(parents=True, exist_ok=True)
    (out / "cards").mkdir(exist_ok=True)
    ed.write_pad(out, spec)
    region = "jp" if jp else "us"
    other = {"1": "2", "2": "1"}
    m = spec["m"]
    env = dict(os.environ)
    for k in list(env):
        if k.startswith("XG_"):
            del env[k]
    env.update({"SDL_AUDIO_DRIVER": "dummy", "XG_SETTINGS": "none", "XG_BIOS": "none", "XG_QUIT_AT": str(m),
                "XG_CARD_DIR": str(out / "cards"), "XG_VK_DEVICE": device, "XG_UPSCALE": upscale,
                "XG_CPU_TIMING": "1", "XG_GPU_TIMING": "1", "XG_PAD_SCRIPT": str(out / "pad.txt"),
                "XG_INVARIANT_LOG": "invariants.log",
                "XG_DISC2": str(ROOT / sc.DISCS[region + other[spec["disc"]]]), "TMPDIR": str(out)})
    if not window:
        env.update({"XG_HEADLESS": "1", "SDL_VIDEO_DRIVER": "offscreen"})
    env.update(spec["env"])
    env.update(extra)
    exe = sc.executable(build)[0]
    started = time.monotonic()
    with open(out / "port.log", "w") as log:
        pinned = ["taskset", "-c", str(pin)] if pin is not None else []
        p = subprocess.run(["timeout", "-s", "INT", "-k", "30", "900", *pinned, *exe, str(ROOT / sc.DISCS[region + spec["disc"]])],
                           env=env, stdout=log, stderr=subprocess.STDOUT, cwd=out)
    seconds = time.monotonic() - started
    text = (out / "port.log").read_text(errors="replace")
    r: dict = {"scene": scene, "build": str(build.relative_to(ROOT)) if build.is_relative_to(ROOT) else str(build),
               "engine": engine_of(build), "upscale": upscale, "device": device, "window": window,
               "end_vblank": m, "exit": p.returncode, "seconds": round(seconds, 2)}
    c = CPU_LINE.search(text)
    if c:
        r["vblanks"] = int(c.group(1))
        for name, mean, worst in PAIRS.findall(c.group(2)):
            r[f"cpu_{name}_mean"] = float(mean)
            r[f"cpu_{name}_worst"] = float(worst)
        if r.get("cpu_wall_mean"):
            r["vblank_rate"] = round(1000.0 / r["cpu_wall_mean"], 2)
    late = LATE_LINE.search(text)
    if late:
        r["late_1ms"], r["late_frame"], r["late_worst"], r["dry"] = int(late.group(1)), int(late.group(2)), \
            float(late.group(3)), int(late.group(4))
    g = GPU_LINE.search(text)
    if g:
        r["gpu_vblanks"], r["gpu_mean"], r["gpu_worst"] = int(g.group(1)), float(g.group(2)), float(g.group(3))
        for name, mean, worst in PAIRS.findall(g.group(4)):
            r[f"gpu_{name}_mean"] = float(mean)
            r[f"gpu_{name}_worst"] = float(worst)
    dev = re.search(r"vulkan: using ([^,\n]+)", text)
    r["device_name"] = dev.group(1).strip() if dev else ""
    steady(text, r, warmup)
    # The scene's course, from the invariant log (port/src/system/invariants.c): vblanks
    # per screen kind and the game-state hash at the end. Runs of one scene that differ
    # here did not play the same game (a load that took longer in real time, say), and
    # their times are not comparable: the table marks them.
    inv = out / "invariants.log"
    if inv.exists():
        modes: dict[str, int] = {}
        last = ""
        for line in inv.read_text().splitlines():
            t = line.split()
            if len(t) >= 6 and t[0] == "v":
                modes[t[3]] = modes.get(t[3], 0) + 1
                last = t[5]
        r["modes"] = modes
        r["final_gs"] = last
    return r


VB_LINE = re.compile(r"^cpu: vb (\d+) wall ([\d.]+) cpu ([\d.]+) \| (.*?) \| late ([\d.]+) dry (\d+)", re.M)
GPU_VB = re.compile(r"^gpu time: vb (\d+) ([\d.]+) ms(.*)$", re.M)


def steady(text: str, r: dict, warmup: int) -> None:
    """Per-vblank samples after the warm-up (boot and the first load: a one-time pipeline
    creation on some drivers lands there): mean, 99th percentile and worst of the wall and
    the parts, the late vblanks and the dry pushes in the steady part only."""
    rows = [(int(m.group(1)), float(m.group(2)), float(m.group(3)), m.group(4), float(m.group(5)), int(m.group(6)))
            for m in VB_LINE.finditer(text)]
    rows = [x for x in rows if x[0] >= warmup]
    if not rows:
        return

    def stats(values: list[float]) -> dict:
        v = sorted(values)
        return {"mean": round(sum(v) / len(v), 3), "p99": round(v[min(len(v) - 1, int(len(v) * 0.99))], 3),
                "worst": round(v[-1], 3)}

    st: dict = {"vblanks": len(rows), "wall": stats([x[1] for x in rows]), "cpu": stats([x[2] for x in rows]),
                "late_1ms": sum(1 for x in rows if x[4] > 1.0), "late_frame": sum(1 for x in rows if x[4] > 16.71),
                "late_worst": max(x[4] for x in rows), "dry": sum(x[5] for x in rows)}
    parts: dict[str, list[float]] = {}
    for x in rows:
        for name, val in re.findall(r"(\w+) ([\d.]+)", x[3]):
            parts.setdefault(name, []).append(float(val))
    for name, vals in parts.items():
        st[name] = stats(vals)
    r["steady"] = st
    gpu = [float(m.group(2)) for m in GPU_VB.finditer(text) if int(m.group(1)) >= warmup]
    if gpu:
        r["steady"]["gpu"] = stats(gpu)


def cell(r: dict) -> str:
    """Steady-state numbers (after the warm-up) when the run has them, else the summaries."""
    if r.get("exit") != 0 and "vblanks" not in r:
        return f"exit {r.get('exit')}"
    st = r.get("steady")
    if st:
        wall, game, gpu = st["wall"]["mean"], st.get("game", {}).get("mean"), st.get("gpu", {}).get("mean")
        late, dry, worst = st["late_frame"], st["dry"], st["wall"]["p99"]
    else:
        wall, game, gpu = r.get("cpu_wall_mean"), r.get("cpu_game_mean"), r.get("gpu_mean")
        late, dry, worst = r.get("late_frame"), r.get("dry"), r.get("cpu_wall_worst")
    s = f"{wall:.2f}" if wall is not None else "-"
    if worst is not None:
        s += f" (p99 {worst:.1f})"
    if late:
        s += f" {late} late"
    s += f" / {game:.2f}" if game is not None else " / -"
    s += f" / {gpu:.2f}" if gpu is not None else " / -"
    if dry:
        s += f" dry {dry}"
    if r.get("course_differs"):
        s += " [other course]"
    return s


def course_marks(results: list[dict]) -> None:
    """Mark the runs of a scene whose course differs from the scene's first run."""
    first: dict[str, dict] = {}
    for r in results:
        ref = first.setdefault(r["scene"], r)
        r["course_differs"] = bool(ref.get("modes") and (r.get("modes") != ref.get("modes") or
                                                        r.get("final_gs") != ref.get("final_gs")))


def table(results: list[dict], builds: list[str]) -> str:
    course_marks(results)
    scenes = list(dict.fromkeys(r["scene"] for r in results))
    scales = list(dict.fromkeys(r["upscale"] for r in results))
    lines = ["Cells, steady state after the warm-up: wall ms per vblank (99th percentile) [vblanks more than a "
             "frame late] / game CPU ms / GPU ms. Native: 16.71 ms wall. [other course]: the run did not play the "
             "same game as the scene's first run (screen-kind timeline or final game state differ): not comparable.",
             "",
             "| scene | upscale | " + " | ".join(builds) + " |", "|---|---|" + "---|" * len(builds)]
    for scene in scenes:
        for scale in scales:
            row = [scene, scale]
            for b in builds:
                r = next((x for x in results if x["scene"] == scene and x["upscale"] == scale and x["build"] == b), None)
                row.append(cell(r) if r else "-")
            lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="append", default=[], help="a build directory (default: release and enhanced)")
    ap.add_argument("--scene", action="append", default=[], help=f"default: {' '.join(DEFAULT_SCENES)}")
    ap.add_argument("--upscale", default="1,4,12", help="comma-separated upscale factors (1, 2..12, auto)")
    ap.add_argument("--device", default="NVIDIA", help="XG_VK_DEVICE: a number or part of the device's name")
    ap.add_argument("--vblanks", type=int, help="end every scene at this vblank instead of its default")
    ap.add_argument("--warmup", type=int, default=180, help="vblanks left out of the steady-state numbers (boot)")
    ap.add_argument("--jp", action="store_true")
    ap.add_argument("--window", action="store_true", help="a real window on the desktop instead of headless")
    ap.add_argument("--out", default="build/bench")
    ap.add_argument("--tag", default="", help="added to the result file names")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE", help="for every run")
    ap.add_argument("--pin", type=int, metavar="CPU", help="run the game on this CPU (taskset)")
    ap.add_argument("--spin", type=int, metavar="CPU", help="keep a busy process on this CPU during the runs (a steady clock)")
    args = ap.parse_args()
    builds = [ROOT / b if not Path(b).is_absolute() else Path(b) for b in (args.build or ["build/pc/release", "build/pc/enhanced"])]
    scenes = args.scene or DEFAULT_SCENES
    scales = [s.strip() for s in args.upscale.split(",") if s.strip()]
    extra = dict(e.split("=", 1) for e in args.env)
    out = ROOT / args.out if not Path(args.out).is_absolute() else Path(args.out)
    stamp = dt.datetime.now().strftime("%Y-%m-%d-%H%M")
    name = f"{stamp}-{re.sub(r'[^A-Za-z0-9]+', '', args.device)}{'-jp' if args.jp else ''}{('-' + args.tag) if args.tag else ''}"
    results: list[dict] = []
    spinner = None
    if args.spin is not None:
        spinner = subprocess.Popen(["taskset", "-c", str(args.spin), "sh", "-c", "while :; do :; done"])
    for scene in scenes:
        for scale in scales:
            for b in builds:
                run_dir = out / name / f"{scene}-{scale}x-{b.name}"
                r = run_one(b, run_dir, scene, args.jp, scale, args.device, args.window, args.vblanks, extra,
                            args.warmup, args.pin)
                results.append(r)
                print(f"{scene:9} {scale:>4}x {b.name:14} {cell(r)}  ({r['seconds']} s)", flush=True)
                (out / f"{name}.json").parent.mkdir(parents=True, exist_ok=True)
                (out / f"{name}.json").write_text(json.dumps({"date": stamp, "device": args.device,
                                                               "device_name": r.get("device_name", ""),
                                                               "jp": args.jp, "window": args.window, "env": extra, "pin": args.pin, "spin": args.spin,
                                                               "runs": results}, indent=1) + "\n")
    if spinner is not None:
        spinner.kill()
        spinner.wait()
    build_names = [str(b.relative_to(ROOT)) if b.is_relative_to(ROOT) else str(b) for b in builds]
    md = f"# Benchmark {stamp}, device {args.device} ({results[0].get('device_name', '') if results else ''})" \
         f"{', JP' if args.jp else ''}{', window' if args.window else ', headless'}\n\n" + table(results, build_names) + "\n"
    (out / f"{name}.md").write_text(md)
    print("\n" + md)
    print(f"results: {out / name}.json, {out / name}.md")
    return 1 if any(r["exit"] != 0 for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
