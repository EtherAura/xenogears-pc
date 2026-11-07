#!/usr/bin/env python3
"""Compare grown-window wrapping with independently line-broken text packs.

Uses the player's region-matched Disc 1 and private cards/packs. No retail
text is embedded. Both runs must render the same final dialogue picture.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path

import statecheck as S

TEXT = "A translated conversation should keep extraordinary words together, with a final <name 0> greeting.<wait>"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("build", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--jp", action="store_true")
    args = ap.parse_args()
    build, out = args.build.resolve(), args.out.resolve()
    packs = out / "packs"
    native = "A translated conversation\nshould keep extraordinary\nwords together, with a\nfinal <name 0> greeting.<wait>"
    prop = ("A translated conversation should\nkeep extraordinary words\ntogether, with a final <name 0>" +
            ("\ngreeting.<wait>" if args.jp else " greeting.<wait>"))
    indent = "   A translated\nconversation should keep\nextraordinary words\ntogether, with a final <name 0>\ngreeting.<wait>"
    long = "   " + "W" * 90 + " end.<wait>"
    long_lines = "   " + "W" * 24 + "\n" + "W" * 27 + "\n" + "W" * 27 + "\n" + "W" * 12 + " end.<wait>"
    cases = (("native", TEXT, native, False), ("proportional", TEXT, prop, True),
             ("indent", "   " + TEXT, indent, False), ("long-word", long, long_lines, False))
    failed = 0
    for name, automatic, expected, proportional in cases:
        images = []
        for variant, text in (("auto", automatic), ("lines", expected)):
            ident = name + "-" + variant
            pack = packs / ident
            pack.mkdir(parents=True, exist_ok=True)
            (pack / "pack.ini").write_text(
                "name=Layout regression\nrelease=US\nfont=us\nwindows=" +
                ("grow" if variant == "auto" else "pack") + "\n" +
                ("print=proportional\n" if proportional else ""))
            (pack / "field.txt").write_text("".join(
                f"@@ field/1/14/{i} w=55 h=6\n<speed 255>{text}\n" for i in (0, 7)))
            run = out / ident
            run.mkdir(parents=True, exist_ok=True)
            pad = run / "pad.txt"
            pad.write_text("0 0000\n")
            env = {"XG_FIELD": "14", "XG_PARTY": "0,2,255", "XG_GAMESTATE": "0x1930=3,0x2320=1",
                   "XG_PACK_DIR": str(packs), "XG_TEXT_DIALOGUE": "pack:" + ident,
                   "XG_TEXT_MENUS": "disc", "XG_TEXT_NAMES": "disc", "XG_TEXT_BATTLE": "disc",
                   "XG_PAD_SCRIPT": str(pad), "XG_DUMP_RANGE": "2000-2000/1",
                   "XG_VK_DEVICE": os.environ.get("XG_VK_DEVICE", "0")}
            code = S.run_port(build, run, "jp1" if args.jp else "us1", 2100, env)
            image = run / "frames/frame_002000.png"
            if code or not image.exists():
                print(f"text-wordwrap: {ident} failed to render (exit {code}; {run / 'port.log'})")
                images.append(None)
            else:
                images.append(image.read_bytes())
        same = images[0] is not None and images[0] == images[1]
        print(f"text-wordwrap: {name}: {'identical' if same else 'FAIL'}", flush=True)
        failed += not same
    print(f"text-wordwrap: {len(cases) - failed}/{len(cases)} pictures match explicit line breaks")
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
