#!/usr/bin/env python3
"""Check GTE precision and the display copy's edges and texture projection.

    python tools/geometrycheck.py [--build build/pc/release] [--no-build]
                                 [--cpu-only] [--device N|NAME] [--no-interlock]

GPU tests use disposable settings and serialize with the feature GPU lock.
No disc image or game save is needed.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def gpu_lock(enabled: bool):
    if not enabled or os.name == "nt":
        yield
        return
    import fcntl
    lock = ROOT / "work/agents/feature-gpu.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/pc/release")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--cpu-only", action="store_true")
    parser.add_argument("--device", help="XG_VK_DEVICE: a device number or part of its name (intel, nvidia)")
    parser.add_argument("--no-interlock", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    targets = ["gte_precisiontest", "model_geometrytest"]
    if not args.cpu_only:
        targets.append("geometrytest")
    if not args.no_build:
        subprocess.run(["cmake", "--build", str(build), "--target", *targets, "-j", "4"], check=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith("XG_")}
    env.update(SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", SDL_JOYSTICK_HIDAPI="0")
    if args.device is not None:
        env["XG_VK_DEVICE"] = args.device
    if args.no_interlock:
        env["XG_VK_NO_INTERLOCK"] = "1"
    suffix = ".exe" if os.name == "nt" else ""
    with tempfile.TemporaryDirectory(prefix="xg-geometry-") as scratch:
        env["XG_SETTINGS"] = str(Path(scratch) / "settings.ini")
        for target in targets:
            command = [str(build / (target + suffix))]
            with gpu_lock(target == "geometrytest"):
                result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60)
            print(result.stdout, end="")
            print(result.stderr, end="")
            if result.returncode:
                raise SystemExit(result.returncode)
    print("geometrycheck: projection, integer registers, packet tags" +
          (" passed" if args.cpu_only else ", shared edges and perspective passed"))


if __name__ == "__main__":
    main()
