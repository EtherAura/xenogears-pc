#!/usr/bin/env python3
"""Gather disc-backed builds' link tables into port/link-tables, checked.

    cmake -S port -B BUILD ... -DXG_WRITE_LINK_TABLES=ON && cmake --build BUILD   # on a machine with the disc
    python tools/link_tables.py BUILD [BUILD ...]

A checkout without a disc links from port/link-tables (port/CMakeLists.txt,
XG_LINK_FROM): what the link otherwise takes from the PS1 split, the extracted
images and the PS1 build, as addresses, sizes, offsets and hashes, never game
bytes. A build configured with XG_WRITE_LINK_TABLES leaves its own in
BUILD/link-tables: the link facts, the PS1 object facts and the assembly layout
with the zero answers that link asked for (tools/pc_link.py), and each
module's source data plan (tools/pc_srcdata.py). This puts them where the
other builds find them:

    port/link-tables/<region>/asm-layout.json           one per region: every build's answers, merged
    port/link-tables/<region>/<engine>-<system>-<cpu>/  facts.json, srcdata/<module>.json and, where the
                                                        build has them, ps1-objects.json (made beside a
                                                        PS1 build), compiler.txt (the compiler they fit)
                                                        and executable.json (packaging/build_release.py:
                                                        the executable a release build made of them)

All of a region's configurations are given together: its layout is written
from the builds named, and a configuration left out keeps its folder but may
find its questions unanswered (--keep merges the answers already there, for a
build added to the others). Every file is then scanned for retail bytes
(tools/check_link_tables.py: the extracted images must be there), and nothing
is left in place when one fails or a table is missing.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_link_tables  # noqa: E402
import pc_link  # noqa: E402

# Copied where a build has them: the PS1 object facts need the PS1 build's images, which the release's builder
# does not make.
OPTIONAL = ("ps1-objects.json", "compiler.txt", "executable.json")


class TablesError(Exception):
    pass


def build_tables(build: Path) -> tuple[str, str, Path]:
    """(region, configuration, folder) of a build's own tables."""
    tables = build / "link-tables"
    try:
        ver, config = (tables / "config.txt").read_text().split()
    except (OSError, ValueError):
        raise TablesError(f"{build}: no link tables (configure it with -DXG_WRITE_LINK_TABLES=ON and build)") from None
    for name in ("facts.json", "asm-layout.json"):
        if not (tables / name).is_file():
            raise TablesError(f"{build}: {name} is missing: the build has not linked since it was configured")
    return ver, config, tables


def gather(builds: list[Path], dest: Path, keep: bool) -> list[str]:
    """Write the builds' tables under dest; the report's lines. An error leaves dest as it was."""
    found: dict[str, dict[str, Path]] = {}
    for build in builds:
        ver, config, tables = build_tables(build)
        if config in found.setdefault(ver, {}):
            raise TablesError(f"{build} and {found[ver][config].parent} are both {ver} {config}")
        found[ver][config] = tables
    report = []
    with tempfile.TemporaryDirectory(dir=dest.parent, prefix=".link-tables-") as tmp:
        staged = Path(tmp)
        for ver, configs in sorted(found.items()):
            boot = pc_link.boot_name(pc_link.disc_files(ver))
            docs = []
            sources = [t / "asm-layout.json" for t in configs.values()]
            if keep and (dest / ver / "asm-layout.json").is_file():
                sources.append(dest / ver / "asm-layout.json")
            for path in sources:
                doc = pc_link.parse_asm_layout(path)
                named = doc.get("modules") if isinstance(doc, dict) else None
                pc_link.asm_layout_check(doc, ver, boot, sorted(named) if isinstance(named, dict) and named else [])
                docs.append(doc)
            merged = pc_link.merge_asm_layouts(docs)
            (staged / ver).mkdir()
            (staged / ver / "asm-layout.json").write_text(json.dumps(merged, separators=(",", ":"), sort_keys=True) + "\n")
            answers = sum(len(f["zero"]) + len(f["nonzero"]) for f in merged["modules"].values())
            report.append(f"{ver}: asm-layout.json from {len(docs)} layouts, {answers} answers")
            files = [staged / ver / "asm-layout.json"]
            for config, tables in sorted(configs.items()):
                out = staged / ver / config
                (out / "srcdata").mkdir(parents=True)
                for name in ("facts.json", *(n for n in OPTIONAL if (tables / n).is_file())):
                    shutil.copyfile(tables / name, out / name)
                    files.append(out / name)
                plans = sorted((tables / "srcdata").glob("*.json"))
                if not plans:
                    raise TablesError(f"{tables}: no source data plans (the modules have not linked)")
                for plan in plans:
                    # One line per object: a change to one file is one line of the diff.
                    doc = json.loads(plan.read_text())
                    (out / "srcdata" / plan.name).write_text(
                        json.dumps(doc, sort_keys=True, separators=(",", ":")).replace('},{"name":', '},\n{"name":') + "\n")
                    files.append(out / "srcdata" / plan.name)
                extras = [n for n in OPTIONAL if (out / n).is_file()]
                report.append(f"{ver} {config}: facts, {len(plans)} source data plans"
                              + (f", {', '.join(extras)}" if extras else ""))
            images = check_link_tables.reference_images(ver, pc_link.ROOT)
            wanted = check_link_tables.payloadcheck.windows(images, check_link_tables.MIN)
            dirty = [str(f.relative_to(staged)) for f in files if check_link_tables.scan(f.read_bytes(), wanted)]
            if dirty:
                raise TablesError(f"{ver}: retail bytes in {', '.join(dirty)}: nothing written")
            report.append(f"{ver}: {len(files)} files, no run of {check_link_tables.MIN} retail bytes "
                          f"({len(images)} module images)")
        for ver, configs in found.items():
            (dest / ver).mkdir(parents=True, exist_ok=True)
            shutil.copyfile(staged / ver / "asm-layout.json", dest / ver / "asm-layout.json")
            for config in configs:
                if (dest / ver / config).exists():
                    shutil.rmtree(dest / ver / config)
                shutil.copytree(staged / ver / config, dest / ver / config)
    return report


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("builds", nargs="+", type=Path, help="build folders configured with XG_WRITE_LINK_TABLES")
    ap.add_argument("--dest", type=Path, default=pc_link.ROOT / "port" / "link-tables")
    ap.add_argument("--keep", action="store_true", help="merge the answers of the layout already in --dest")
    a = ap.parse_args(argv)
    try:
        a.dest.mkdir(parents=True, exist_ok=True)
        report = gather([b.resolve() for b in a.builds], a.dest.resolve(), a.keep)
    except (TablesError, pc_link.AsmLayoutError, check_link_tables.ReferenceSetError) as e:
        print(f"link_tables: {e}", file=sys.stderr)
        return 1
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
