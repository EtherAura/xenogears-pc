#!/usr/bin/env python3
"""Refresh the US Original module-check metadata on the disc machine.

Pass a built US Original Ninja game build and the Darwin darwin_module_inputs build.
Configure the latter with CMAKE_EXPORT_COMPILE_COMMANDS=ON. Only the emitted
linker inputs and checked restore plans belong in the public checkout.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import payloadcheck
import pc_link
import pc_sources


def prepare(game, darwin, output):
    cache = (game / "CMakeCache.txt").read_text().splitlines()
    if "XG_ENGINE:STRING=original" not in cache or "XG_VERSION:STRING=US" not in cache:
        raise ValueError("expected a US Original reference build")
    compiles = json.loads((darwin / "compile_commands.json").read_text())
    modules = ("field", "battle")
    selected = {}
    others = []
    for module in pc_sources.modules("us"):
        if module in modules:
            selected[module] = [str(darwin / row["output"]) for row in compiles
                                if f"CMakeFiles/game_{module}.dir/" in row["output"]]
            if not selected[module]:
                raise ValueError(f"missing {module} input objects")
        else:
            others.extend(sorted((game / f"CMakeFiles/game_{module}.dir").rglob("*.o")))
    platform = sorted((game / "CMakeFiles/platform.dir").rglob("*.o"))
    if not others or not platform:
        raise ValueError("build the reference game first")
    output.mkdir(exist_ok=True, parents=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        # Platform consumers and other overlays decide which field/battle names remain global.
        subprocess.run([sys.executable, ROOT / "tools/pc_link.py", "--ver", "us", "--cc", "gcc", "--darwin",
                        tmp, *others, *selected["field"], *selected["battle"], "--platform", *platform],
                       check=True, cwd=ROOT)
        (tmp / "bounds.c").write_text("\n".join(pc_link.module_bounds(modules, True)) + "\n")
        names = ["bounds.c"]
        for module in modules:
            plan = tmp / (module + ".json")
            subprocess.run([sys.executable, ROOT / "tools/pc_srcdata.py", "--macho-exports", tmp / (module + ".exports"),
                            "--write-plan", plan, "--plan-only", module, "partial", "copies", "symbols",
                            module + ".c.o", *selected[module]], cwd=ROOT, check=True)
            # Compact each record. The facts remain ordinary JSON readable with json.tool.
            doc = json.loads(plan.read_text())
            plan.write_text(json.dumps(doc, sort_keys=True, separators=(",", ":")).replace('},{"name":', '},\n{"name":') + "\n")
            names += [module + suffix for suffix in (".c", ".exports", ".json")]
        for name in names:
            if name.endswith(".c"):
                p = tmp / name
                p.write_text(p.read_text().rstrip() + "\n")
        # Compare every informative window in the metadata with every retail module, both regions.
        wanted = payloadcheck.windows({name: (tmp / name).read_bytes() for name in names}, 16)
        for ver in ("us", "jp"):
            for module, image in payloadcheck.retail_images(ver, complete=True).items():
                if payloadcheck.hits(image, wanted, 16):
                    raise ValueError(f"generated inputs share a retail byte window with {ver}/{module}")
        for name in names:
            (output / name).write_bytes((tmp / name).read_bytes())
        print(f"Verified {len(names)} metadata files against both retail regions")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("darwin", type=Path)
    parser.add_argument("--output", type=Path, default=HERE / "us-original")
    args = parser.parse_args()
    prepare(args.game.resolve(), args.darwin.resolve(), args.output)
