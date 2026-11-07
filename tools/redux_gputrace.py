#!/usr/bin/env python3
"""Record the game's LIBGPU calls in PCSX-Redux, with VRAM at frame markers.

    python tools/redux_gputrace.py state --vsyncs 1250 --save intro.sav [--bios FILE]
    python tools/redux_gputrace.py trace --state intro.sav --frames 60 --out DIR

`state` boots the disc (dynarec, fast) and saves a state after N vsyncs.
`trace` loads a state in the interpreter (breakpoints), loads redux_gputrace.lua
and records DIR/trace.txt; at every --every-th VSync(0) the emulator pauses and
DIR/vram_<marker>.bin is fetched. port/src/gpu/test/gpureplay.c replays it.
States live in work/redux_gputrace/. The BIOS comes from --bios or
PCSX_REDUX_BIOS (tools/redux.py).
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import redux  # noqa: E402

RUNDIR = ROOT / "work" / "redux_gputrace"


def retry(fn, tries: int = 5):
    """PCSX-Redux's web server sometimes drops a connection while it pauses."""
    for i in range(tries):
        try:
            return fn()
        except (ConnectionError, OSError, redux.ReduxError):
            if i == tries - 1:
                raise
            time.sleep(0.2)


def wait_paused(client: redux.Redux, timeout: float) -> None:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if not retry(client.flow)["running"]:
            return
        time.sleep(0.25)
    raise SystemExit(f"still running after {timeout}s")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["state", "trace"])
    ap.add_argument("--vsyncs", type=int, default=1250)
    ap.add_argument("--save", type=Path, default=Path("intro.sav"))
    ap.add_argument("--state", type=Path)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--every", type=int, default=1, help="VRAM every N markers (0: only the first)")
    ap.add_argument("--shots", action="store_true", help="save the display area at every marker")
    ap.add_argument("--out", type=Path, default=RUNDIR / "trace")
    ap.add_argument("--bios", type=Path, help="BIOS image (or set PCSX_REDUX_BIOS)")
    ap.add_argument("--port", type=int, default=8196)
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--skip", type=int, default=0, help="trace: vsyncs to run before tracing (BIOS)")
    ap.add_argument("--prep", default="", help="trace: before tracing, 'run N' / 'seq PADSTEPS' commands, ';'-separated")
    args = ap.parse_args()

    iso = redux.disc_image("us", 1)
    RUNDIR.mkdir(parents=True, exist_ok=True)
    if args.cmd == "state":
        client, proc = redux.start(RUNDIR, args.port, iso=iso, bios=args.bios, paused=True, speed=0)
        try:
            client.run_vsyncs(args.vsyncs)
            print(client.save_state(RUNDIR / args.save))
        finally:
            client.quit()
            proc.wait(10)
        return 0

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    client, proc = redux.start(RUNDIR, args.port, iso=iso, bios=args.bios, debugger=True, paused=True,
                               lua_files=(HERE / "redux_gputrace.lua",), speed=0)
    try:
        if args.state:
            client.load_state(RUNDIR / args.state)
        if args.skip:
            client.run_vsyncs(args.skip)
        for cmd in filter(None, (c.strip() for c in args.prep.split(";"))):
            op, _, arg = cmd.partition(" ")
            if op == "run":
                client.run_vsyncs(int(arg), timeout=3600)
            elif op == "seq":
                client.run_sequence(redux.parse_pad_steps(arg), timeout=3600)
            else:
                raise SystemExit(f"unknown prep command {cmd!r}")
            print(f"prep: {cmd}", flush=True)
        client.run_lua(f"return GT.start('{out / 'trace.txt'}', {args.every}, {'true' if args.shots else 'false'}, {args.frames})")
        marker = 0
        snap = out / "trace.txt.snap"
        while marker < args.frames:
            snap.unlink(missing_ok=True)
            retry(client.resume)
            end = time.monotonic() + args.timeout
            nudge = time.monotonic() + 5
            while not snap.exists():
                if time.monotonic() > end:
                    raise SystemExit(f"no frame marker after {args.timeout}s")
                if time.monotonic() > nudge:
                    # A resume can land before the previous pause took effect,
                    # leaving the emulator paused: resume it again.
                    nudge = time.monotonic() + 5
                    if not retry(client.flow)["running"]:
                        retry(client.resume)
                time.sleep(0.05)
            time.sleep(0.05)
            wait_paused(client, args.timeout)
            marker = int(snap.read_text())
            (out / f"vram_{marker:05d}.bin").write_bytes(retry(client.vram))
            print(f"marker {marker}", flush=True)
        retry(client.pause)
        client.run_lua("return GT.stop()")
    finally:
        client.quit()
        proc.wait(10)
    return 0


if __name__ == "__main__":
    sys.exit(main())
