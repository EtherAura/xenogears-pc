#!/usr/bin/env python3
"""Sample retail field wall-slide arguments in disposable Beetle frame copies.

The reference instance never runs a hook or loads a state. A second instance
loads its state before each sampled frame and logs the Sub3/Sub4 input edge and
the edge actually used by Sub3Sub2. Compare hashes.jsonl with --no-hooks to
verify that collecting the samples leaves reference pictures, audio and ALL
RAM unchanged. These are samples, not a complete instruction trace: hooks can
push a call past the end of the copied frame. Redux is useful for finding the
writers, but this tool supplies the Beetle evidence.

    python tools/wallslide_probe.py OUT --field 1 --stop 1700
    python tools/wallslide_probe.py CONTROL --field 1 --stop 1700 --no-hooks

Core, BIOS and disc files remain local, unmodified and unbundled. No emulator
source is used. The small MIPS logger is our own, using tools/beetle.py's
assembler and the same documented unused kernel RAM/cache-invalidation method.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path
from types import SimpleNamespace
from dataclasses import asdict

import beetle
from retail_heap import HEAP_END, RAM_START, read_heap

ROOT = Path(__file__).resolve().parents[1]
BASE, CODE, RING, COUNT = 0x8000C000, 0x8000C100, 0x8000C800, 64
FIELDS = ("kind", "actor", "initializing", "sp", "edge_ptr", "angle",
          "edge0", "edge1", "edge2", "edge3", "dx", "dz", "ra")


def symbols(ver: str) -> dict[str, int]:
    result = beetle.read_symbols(ver)
    for line in (ROOT / "config" / ver / "symbols/field.txt").read_text().splitlines():
        match = re.match(r"\s*([\w.$]+)\s*=\s*(0x[\dA-Fa-f]+|\d+)\s*;", line)
        if match:
            result[match[1]] = int(match[2], 0)
    return result


def originals(ver: str, sites: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Expected entry instructions come from the player's extracted images."""
    main = (ROOT / "disc" / ver / "main.exe").read_bytes()
    field = (ROOT / "disc" / ver / "field.bin").read_bytes()
    main_base = struct.unpack_from("<I", main, 0x18)[0]
    yaml = (ROOT / "config" / ver / "field.yaml").read_text()
    field_base = int(re.search(r"vram:\s*(0x[\dA-Fa-f]+)", yaml)[1], 0)
    result = {}
    for name, addr in sites.items():
        data, off = (main, addr - main_base + 0x800) if name == "trap" else (field, addr - field_base)
        words = struct.unpack_from("<II", data, off)
        for word in words:
            op = word >> 26
            if op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and word & 63 in (8, 9)):
                raise ValueError(f"branch in displaced {name} entry")
        result[name] = words
    return result


def logger(syms: dict[str, int], sites: dict[str, int], orig: dict) -> tuple[bytes, dict]:
    a = beetle.Asm(CODE)

    def back(name):
        for word in orig[name]:
            a.word(word)
        a.j(sites[name] + 8)

    for kind, name in enumerate(("sub3", "sub4", "slide"), 1):
        a.label(name)
        a.li("t0", BASE)
        a.lw("t1", 0, "t0")
        a.sltiu("t2", "t1", COUNT)
        a.bne("t2", "zero", name + "_room")
        a.lw("t2", 4, "t0")
        a.addiu("t2", "t2", 1)
        a.sw("t2", 4, "t0")
        a.b(name + "_out")
        a.label(name + "_room")
        a.sll("t2", "t1", 6)
        a.li("t3", RING)
        a.addu("t2", "t2", "t3")
        a.li("t3", kind)
        a.sw("t3", 0, "t2")
        for off, sym in ((4, "updateEntityEventCode3Var0"), (8, "g_updateAllEntitiesSub2Var0")):
            a.li("t3", syms[sym])
            a.lw("t3", 0, "t3")
            a.sw("t3", off, "t2")
        a.sw("sp", 12, "t2")
        edge, delta, angle = ("a1", "a2", "a0") if name == "slide" else ("a2", "a0", "a3")
        a.sw(edge, 16, "t2")
        a.sw(angle, 20, "t2")
        for off in range(0, 16, 4):
            a.lw("t3", off, edge)
            a.sw("t3", 24 + off, "t2")
        for source, dest in ((0, 40), (8, 44)):
            a.lw("t3", source, delta)
            a.sw("t3", dest, "t2")
        a.sw("ra", 48, "t2")
        a.addiu("t1", "t1", 1)
        a.sw("t1", 0, "t0")  # publish only complete records
        a.label(name + "_out")
        back(name)

    # Invalidate just the patched lines on the copy's first vblank. As in
    # beetle.py, execute the cache-tag stores through the uncached alias.
    a.label("trap")
    a.li("t0", BASE)
    a.lw("t1", 8, "t0")
    a.beq("t1", "zero", "trap_out")
    a.sw("zero", 8, "t0")
    a.la("t1", "invalidate", 0x20000000)
    a.jr("t1")
    a.label("invalidate")
    a.lui("t0", 0xFFFE)
    a.lw("t2", 0x130, "t0")
    a.li("t3", 0x804)
    a.sw("t3", 0x130, "t0")
    a.mfc0("t4", 12)
    a.lui("t3", 1)
    a.or_("t3", "t4", "t3")
    a.mtc0("t3", 12)
    a.nop()
    a.nop()
    for line in sorted({(site + off) & ~15 for site in sites.values() for off in (0, 4)}):
        a.li("t3", line)
        a.sw("zero", 0, "t3")
    a.mtc0("t4", 12)
    a.nop()
    a.nop()
    a.sw("t2", 0x130, "t0")
    a.la("t1", "trap_out")
    a.jr("t1")
    a.label("trap_out")
    back("trap")
    code = a.assemble()
    if CODE + len(code) > RING or RING + COUNT * 64 > beetle.STUB_END:
        raise ValueError("logger exceeds unused kernel RAM")
    return code, {name: a.labels[name] for name in sites}


def run(args) -> dict:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    syms = symbols(args.ver)
    sites = {"sub3": syms["updateEntityEventCode3Sub3"], "sub4": syms["updateEntityEventCode3Sub4"],
             "slide": syms["updateEntityEventCode3Sub3Sub2"], "trap": syms["trapIntrVSync"]}
    orig = originals(args.ver, sites)
    code, entries = logger(syms, sites, orig)
    content = beetle.disc_image(args.ver, args.disc)
    options = {"beetle_psx_skip_bios": "enabled"} if args.fastboot else {}
    start = [f"XG_FIELD={args.field}"] + (["XG_PRESET=disc2"] if args.disc == 2 else []) + args.start
    boot = beetle.Capture(SimpleNamespace(ver=args.ver, out=out, start=start))
    ref = copy = None
    records, samples, observed = 0, 0, 0
    heaps, last_heap = [], None
    pad = beetle.read_pad(args.pad) if args.pad else [(0, 0)]
    pi, buttons = 0, 0
    metadata = dict(ver=args.ver, disc=args.disc, field=args.field, stop=args.stop,
                    fastboot=args.fastboot, hooks=not args.no_hooks, heap=args.heap, sites=sites, start=start,
                    main_sha256=hashlib.sha256((ROOT / "disc" / args.ver / "main.exe").read_bytes()).hexdigest(),
                    field_sha256=hashlib.sha256((ROOT / "disc" / args.ver / "field.bin").read_bytes()).hexdigest(),
                    pad=str(args.pad.resolve()) if args.pad else None,
                    pad_sha256=hashlib.sha256(args.pad.read_bytes()).hexdigest() if args.pad else None,
                    core_sha256=hashlib.sha256(Path(os.environ.get("XG_BEETLE_CORE") or
                                                    beetle.DEFAULT_CORE).read_bytes()).hexdigest(),
                    probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (out / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    try:
        ref = beetle.Beetle(content, savedir=out / "cards", options=options)
        boot.b = ref
        if not args.no_hooks:
            copy = beetle.Beetle(content, savedir=out / "copy-cards", options=options, index=1)
            copy.lib.bf_audio_mute(1)
        with (out / "edges.jsonl").open("w") as edges, (out / "hashes.jsonl").open("w") as hashes:
            while ref.frame < args.stop:
                v = ref.frame + 1
                while pi < len(pad) and pad[pi][0] <= v:
                    buttons = pad[pi][1]
                    pi += 1
                ref.pad(0, buttons)
                if copy and all(tuple(struct.unpack("<II", ref.read(site, 8))) == orig[name]
                                for name, site in sites.items()):
                    if not ref.lib.bf_snap_save() or not copy.lib.bf_snap_load():
                        raise RuntimeError("cannot clone reference frame")
                    copy.write(BASE, struct.pack("<III", 0, 0, 1))
                    copy.write(CODE, code)
                    for name, site in sites.items():
                        copy.write(site, struct.pack("<II", beetle.j_word(entries[name]), 0))
                    copy.pad(0, buttons)
                    copy.lib.bf_run(1)
                    count, overflow = struct.unpack("<II", copy.read(BASE, 8))
                    if count > COUNT or overflow:
                        raise RuntimeError(f"sample ring overflow at frame {v}: {count}/{overflow}")
                    observed += 1
                    current = None
                    for i in range(count):
                        row = dict(zip(FIELDS, struct.unpack("<13I", copy.read(RING + i * 64, 52))))
                        row["frame"] = v
                        row["edge"] = [row.pop(f"edge{n}") for n in range(4)]
                        if row["kind"] != 3:
                            current = row
                        else:
                            # Equality identifies an unchanged buffer, not whether the
                            # collision routine executed a same-value store.
                            row["unchanged_input"] = (row["edge"] == current["edge"]
                                if current and row["edge_ptr"] == current["edge_ptr"]
                                and row["actor"] == current["actor"] else None)
                            samples += 1
                        edges.write(json.dumps(row) + "\n")
                        records += 1
                ref.run()
                if boot.patched is None and ref.frame > 100:
                    boot.try_patch_mode()
                if boot.patched is not None and boot.poked is None:
                    boot.apply_pokes()
                if args.heap:
                    first = struct.unpack("<I", ref.read(syms["heapFirstBlockData"], 4))[0]
                    if RAM_START + 8 <= first <= HEAP_END:
                        blocks = [dict(asdict(b), size=b.size) for b in read_heap(ref.read, first)]
                        if blocks != last_heap:
                            heaps.append(dict(frame=v, blocks=blocks))
                            last_heap = blocks
                hashes.write(json.dumps([v, ref.lib.bf_frame_hash(), ref.lib.bf_audio_hash(),
                                         ref.lib.bf_ram_hash()]) + "\n")
        if boot.poked is None or (not args.no_hooks and not samples):
            raise RuntimeError("fixture did not reach a wall slide")
    finally:
        if copy:
            copy.close()
        if ref:
            ref.close()
    result = dict(metadata, records=records, slides=samples, sampled_frames=observed,
                  heap_snapshots=len(heaps), boot_patch=boot.patched, start_pokes=boot.poked)
    if args.heap:
        (out / "heap.jsonl").write_text("".join(json.dumps(row) + "\n" for row in heaps))
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--disc", choices=(1, 2), type=int, default=1)
    ap.add_argument("--field", type=int, required=True)
    ap.add_argument("--stop", type=int, default=1700)
    ap.add_argument("--pad", type=Path)
    ap.add_argument("--start", action="append", default=[], metavar="XG_OPTION=VALUE",
                    help="additional XG_PARTY or XG_GAMESTATE fixture option")
    ap.add_argument("--fastboot", action="store_true")
    ap.add_argument("--no-hooks", action="store_true")
    ap.add_argument("--heap", action="store_true",
                    help="read retail MCB chains after each reference frame; write changed chains to heap.jsonl")
    args = ap.parse_args()
    if args.stop <= 0 or not 0 <= args.field < 1024:
        ap.error("positive stop and valid field id required")
    if any(option.partition("=")[0] not in ("XG_PARTY", "XG_GAMESTATE") or "=" not in option
           for option in args.start):
        ap.error("--start accepts XG_PARTY or XG_GAMESTATE")
    print(json.dumps(run(args), indent=2))


if __name__ == "__main__":
    main()
