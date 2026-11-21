#!/usr/bin/env python3
"""Link and run two disc-free overlays on Apple Silicon. --objects-only also works with cross-Clang."""
import argparse
import hashlib
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SYSTEM = HERE.parents[1]
sys.path.insert(0, str(HERE.parents[4] / "tools"))
import payloadcheck
import pc_link
import pc_macho
import pc_srcdata


def run(*args, capture=False):
    print("+", *map(str, args), flush=True)
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None).stdout


def strip_initializer(obj, image):
    """Exercise the production reader and writable-section rewrite around a live pointer."""
    macho = pc_macho.Object(obj.read_bytes())
    section = next(s for s in macho.sections if s.key == "__DATA,__data")
    keep = macho.relocated(section.key)
    if keep != set(range(24, 32)):
        raise ValueError("expected an eight-byte relocation between the source runs")
    raw = macho.data[section.offset:section.offset + section.size]
    wanted = payloadcheck.windows({"fixture": image}, pc_srcdata.MIN)
    copies = pc_srcdata.blank(raw, keep, wanted, pc_srcdata.Source(image, []))
    if copies != [(0, 0, 24), (32, 32, 24)]:
        raise ValueError(f"unexpected source-byte restore plan: {copies}")
    if raw[24:32] != macho.data[section.offset + 24:section.offset + 32]:
        raise ValueError("strip changed pointer addend")
    macho.data[section.offset:section.offset + section.size] = raw
    macho.mark_sources({section.key: "syntheticSource"})
    obj.write_bytes(macho.data)
    return copies


def build(tmp, objects_only):
    images = {}
    objects = []
    copy_rows = []
    for i, (entry, peer) in enumerate((("fieldView", "battleView"), ("battleView", "fieldView")), 1):
        # The same data layout as state.c, with a nonzero pointer addend between two runs.
        # Avoid zero bytes in the runs so pc_srcdata produces exactly two copies.
        sample = bytes(b % 255 + 1 for b in hashlib.sha512(entry.encode()).digest()[:48])
        images[entry] = sample[:24] + (4).to_bytes(8, "little") + sample[24:]
        build_dir = tmp / entry
        build_dir.mkdir()
        (build_dir / "payload.inc").write_text(
            "#define BEFORE {" + ",".join(map(str, sample[:24])) + "}\n"
            "#define AFTER {" + ",".join(map(str, sample[24:])) + "}\n")
        flags = ["clang", "-target", "arm64-apple-macos15", "-ffreestanding", "-fcommon", "-Wall", "-Werror",
                 "-I", build_dir, f"-DSEED={i}", f"-DENTRY={entry}", f"-DPEER={peer}"]
        inputs = []
        for name in ("state", "entry"):
            obj = build_dir / (name + ".o")
            run(*flags, "-c", HERE / (name + ".c"), "-o", obj)
            inputs.append(obj)
        for dest, source, length in strip_initializer(inputs[0], images[entry]):
            copy_rows.append(f"    {{{i - 1}, {dest}, {source}, {length}}},\n")
        print(f"PASS: {entry} stripped 48 synthetic bytes around the 8-byte Mach-O relocation", flush=True)
        if objects_only:
            continue
        for obj in inputs:
            symbols = run("nm", "-g", obj, capture=True).splitlines()
            if not any(line.split()[-2:] == ["C", "_common"] for line in symbols):
                raise ValueError(f"{obj.name} did not contribute tentative common storage")
        module = build_dir / "module.o"
        ld = run("xcrun", "--find", "ld-classic", capture=True).strip()
        pc_macho.link(entry.removesuffix("View"), ld, inputs, [entry], module, build_dir)
        run("nm", "-m", module)
        exports = {line.split()[-1] for line in run("nm", "-gU", module, capture=True).splitlines() if line.strip()}
        if exports != {"_" + entry}:
            raise ValueError(f"unexpected module exports: {sorted(exports)}")
        print(f"PASS: {module.parent.name} exports only _{entry}", flush=True)
        objects.append(module)

    if objects_only:
        return
    (tmp / "bounds.inc").write_text("\n".join(pc_link.module_bounds(["field", "battle"], True)))
    (tmp / "copies.inc").write_text("".join(copy_rows))
    source = tmp / "source.bin"
    source.write_bytes(b"".join(images.values()))
    exe = tmp / "module-reset"
    run("xcrun", "clang", "-arch", "arm64", "-Wall", "-Werror", "-Wl,-dead_strip", "-I", tmp,
        HERE / "reset.c", SYSTEM / "memory.c", *objects, "-o", exe)
    run("file", exe)
    run("codesign", "--verify", "--strict", exe)
    wanted = payloadcheck.windows(images, pc_srcdata.MIN)
    if payloadcheck.hits(exe.read_bytes(), wanted, pc_srcdata.MIN):
        raise ValueError("linked executable contains synthetic source bytes")
    print("PASS: linked executable contains no synthetic source windows", flush=True)
    run(exe, source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objects-only", action="store_true", help="cross-compile and verify object stripping")
    args = parser.parse_args()
    if not args.objects_only and (platform.system(), platform.machine()) != ("Darwin", "arm64"):
        parser.error("native run requires Apple Silicon macOS")
    with tempfile.TemporaryDirectory() as directory:
        build(Path(directory), args.objects_only)
