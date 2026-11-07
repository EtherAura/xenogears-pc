#!/usr/bin/env python3
"""Check card history and controller profiles using disposable files/devices.

    python tools/polishcheck.py [--build build/pc/release] [--no-build]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/pc/release")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    if not args.no_build:
        subprocess.run(["cmake", "--build", str(build), "--target", "card_historytest", "card_biostest",
                        "pad_profiletest", "-j", "4"], check=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith("XG_")}
    env["SDL_JOYSTICK_HIDAPI"] = "0"
    suffix = ".exe" if os.name == "nt" else ""
    with tempfile.TemporaryDirectory(prefix="xg-polish-") as scratch:
        cards, pads = Path(scratch) / "cards", Path(scratch) / "pads"
        pads.mkdir()
        for target, directory, phase in [
            ("card_historytest", cards, []),
            ("card_historytest", cards, ["restart"]),
            ("card_biostest", cards, []),
            ("pad_profiletest", pads, ["write"]),
            ("pad_profiletest", pads, ["read"]),
            ("pad_profiletest", pads, ["script"]),
        ]:
            result = subprocess.run([str(build / (target + suffix)), str(directory), *phase], env=env,
                                    text=True, capture_output=True, timeout=30)
            print(result.stdout, end="")
            if result.returncode:
                print(result.stderr, end="")
                raise SystemExit(result.returncode)
    print("polishcheck: card history, restart, controller persistence, reconnect and scripted isolation passed")


if __name__ == "__main__":
    main()
