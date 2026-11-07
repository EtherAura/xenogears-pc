#!/usr/bin/env python3
"""Inventory field archives and audit *observed* interpolation coverage.

inventory --out FILE [--disc us1 ...] reads the player's images.
probe --build DIR --disc us1 --field 8 --out DIR records an isolated start.
report --inventory FILE --runs DIR [DIR ...] --out FILE merges those probes.

Motion proof requires a logged intermediate and changed presented pixels
relative to its preceding exact endpoint. Endpoint reuse is not motion proof.
This measures sampled starts; it cannot certify every story/entrance state.
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

import statecheck
from hirescheck import png_rows
from xenofs import Disc

ROOT = Path(__file__).resolve().parents[1]
FIELD_COUNT = 730  # Our disc catalogue: port/src/extras/mapfield.h.
EVENT = re.compile(r"interpolation-coverage: field=(-?\d+) event=(\S+) reason=(\S+) vblank=(-?[\d.]+) alpha=(-?[\d.]+)")


def save(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n")


def sha256(path: Path) -> str:
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def png_signature(path: Path):
    """PNG filtering is reversible. Equal filter sequences let us compare
    filtered bytes directly; different sequences require decoded pixels."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    at, packed, size = 8, [], None
    while at < len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        crc, = struct.unpack_from(">I", data, at + 8 + length)
        if zlib.crc32(kind + body) != crc:
            raise ValueError(f"{path}: PNG checksum mismatch")
        if kind == b"IHDR":
            w, h, depth, colour, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            if not w or not h or (depth, colour, compression, filtering, interlace) != (8, 2, 0, 0, 0):
                raise ValueError(f"{path}: expected non-interlaced RGB8 capture")
            size = w, h
        if kind == b"IDAT":
            packed.append(body)
        at += length + 12
    if size is None:
        raise ValueError(f"{path}: missing PNG header")
    raw = zlib.decompress(b"".join(packed))
    w, h = size
    if len(raw) != h * (w * 3 + 1):
        raise ValueError(f"{path}: PNG scanline size mismatch")
    filters = raw[::w * 3 + 1]
    if any(value > 4 for value in filters):
        raise ValueError(f"{path}: invalid PNG filter")
    return w, h, filters, hashlib.sha256(raw).digest()


def inventory(discs: list[str]) -> dict:
    rows, images = [], {}
    for key in discs:
        image = (ROOT / statecheck.DISCS[key]).with_suffix(".bin")
        images[key] = {"path": str(image), "sha256": sha256(image)}
        with Disc(image) as disc:
            for field in range(FIELD_COUNT):
                lba, size = disc.entry(4, 0xB8 + 2 * field)
                _, textures = disc.entry(4, 0xB9 + 2 * field)
                rows.append({"disc": key, "field": field, "archive_lba": lba,
                             "archive_bytes": size, "texture_bytes": textures,
                             "availability": "present" if size >= 0x190 else "placeholder" if size > 0 else "absent"})
    return {"schema": 1, "scope": "730 field entries per disc; archive presence is not a valid-start claim",
            "images": images, "fields": rows}


def evidence(run: dict, directory: Path) -> dict:
    counts, reasons, changed = Counter(), Counter(), Counter()
    snapshots, anchors = Counter(), {}
    files = sorted((int(p.stem.split("_")[-1]), p) for p in (directory / "present").glob("present_*.png"))
    ticks = [tick for tick, _ in files]
    signatures, decoded = {}, {}

    def picture(tick: float):
        index = bisect_right(ticks, tick) - 1
        if index < 0:
            return None
        path = files[index][1]
        if path not in signatures:
            signatures[path] = png_signature(path)
        return path

    def differs(left, right):
        a, b = signatures[left], signatures[right]
        if a[:2] != b[:2]:
            return True
        if a[2] == b[2]:
            return a[3] != b[3]
        for path in (left, right):
            if path not in decoded:
                width, height, rows = png_rows(path)
                decoded[path] = hashlib.sha256(struct.pack("<II", width, height) + b"".join(rows)).digest()
        return decoded[left] != decoded[right]

    log = (directory / "port.log").read_text(errors="replace")
    for match in EVENT.finditer(log):
        field, event, reason, tick, alpha = match.groups()
        field, tick, alpha = int(field), float(tick), float(alpha)
        if field < 0:
            continue  # Startup/menu without a field is not field evidence.
        counts[field, event] += 1
        if event == "snapshot":
            snapshots[field] += 1
        if event in ("native", "reset") or event == "snapshot" and reason != "valid":
            reasons[field, event, reason] += 1
            anchors.pop(field, None)
        if event == "endpoint":
            if alpha == 0:
                anchors[field] = tick
            else:
                anchors.pop(field, None)
        if event == "intermediate" and field in anchors:
            start = anchors[field]
            # Dumps retain unchanged pixels. Both samples must lie inside
            # the requested dump window; pre-window pictures are not proof.
            lo, hi = run["dump_range"]
            if lo <= start < tick <= hi and tick - start <= 4:
                before, middle = picture(start), picture(tick)
                if before is not None and middle is not None and differs(before, middle):
                    changed[field] += 1
    fields = sorted({field for field, _ in counts})
    return {str(field): {"snapshots": snapshots[field],
                        "events": {event: n for (f, event), n in counts.items() if f == field},
                        "fallbacks": {f"{event}:{reason}": n for (f, event, reason), n in reasons.items() if f == field},
                        "changed_intermediates": changed[field]} for field in fields}


def probe(args) -> int:
    if not shutil.which("bwrap"):
        raise ValueError("bwrap is required to isolate the player's settings and cards")
    build, out = args.build.resolve(), args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError("probe output must be empty; preserve earlier evidence in a separate directory")
    cache = (build / "CMakeCache.txt").read_text()
    version = re.search(r"^XG_VERSION:\w+=(US|JP)$", cache, re.M)
    if not version or version[1].lower() != args.disc[:2]:
        raise ValueError("build region does not match --disc")
    if not 0 <= args.field < FIELD_COUNT or args.start < 0 or args.frames <= args.start:
        raise ValueError("invalid field or capture interval")
    out.mkdir(parents=True, exist_ok=True)
    pad = out / "pad.txt"
    pad.write_text(args.pad.read_text() if args.pad else
                   "0 0000\n800 0008\n810 0000\n1000 1000\n1100 0000\n1200 2000\n1300 0000\n1400 0004\n1410 0000\n")
    options = dict(item.split("=", 1) for item in args.env)
    # Overrides are game/display fixtures only. Do not allow a fixture to
    # redirect output or replace the isolated settings/card locations.
    allowed = {"XG_GAMESTATE", "XG_PARTY", "XG_PRESET", "XG_TEXTURE_FILTER_3D",
               "XG_TEXTURE_FILTER_SPRITE", "XG_TEXTURE_FILTER_HUD", "XG_TEXTURE_FILTER_TEXT",
               "XG_ASPECT", "XG_EXPANDED_VERTICAL", "XG_UPSCALE", "XG_UPSCALE_COLOR",
               "XG_GEOMETRY_PRECISION", "XG_PERSPECTIVE_TEXTURES", "XG_TEXT_GLYPHS",
               "XG_TEXTURE_PACK", "XG_PRESENT_SHADER"}
    if set(options) - allowed:
        raise ValueError("unsupported fixture option: " + ", ".join(sorted(set(options) - allowed)))
    env = {k: v for k, v in os.environ.items() if not k.startswith("XG_")}
    env.update(options)
    env.update(XG_HEADLESS="1", SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", XG_SETTINGS="none",
               XG_CLOCK="step", XG_FIELD=str(args.field), XG_INTERPOLATION="1", XG_INTERPOLATION_LOG="1",
               XG_DUMP_PRESENT=str(out / "present"), XG_DUMP_FRAMES=str(out / "native"),
               XG_DUMP_RANGE=f"{args.start}-{args.frames}", XG_AUDIO_DUMP=str(out / "audio.pcm"),
               XG_PAD_SCRIPT=str(pad),
               XG_SWEEP_LOG=str(out / "sweep.txt"), XG_SLOWDOWN_LOG=str(out / "vsyncs.txt"),
               XG_QUIT_AT=str(args.frames), XG_CARD_DIR=str(out / "cards"), XG_VK_DEVICE=args.gpu)
    env.setdefault("XG_PARTY", "0")
    env.setdefault("WINEDEBUG", "-all")
    command, binary = statecheck.executable(build)
    command = ["bwrap", "--die-with-parent", "--dev-bind", "/", "/",
               "--tmpfs", str(Path.home() / ".local/share/xenogears"),
               "--tmpfs", str(Path.home() / ".config/xenogears"),
               "--chdir", str(out), *command, str(ROOT / statecheck.DISCS[args.disc])]
    record = {"schema": 1, "disc": args.disc, "field": args.field,
              "fixture": options, "binary": str(binary), "binary_sha256": sha256(binary),
              "pad_script": pad.read_text(),
              "disc_sha256": sha256((ROOT / statecheck.DISCS[args.disc]).with_suffix(".bin")),
              "dump_range": [args.start, args.frames], "gpu": args.gpu}
    with (out / "port.log").open("w") as log:
        try:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout)
            record["exit_code"] = result.returncode
        except subprocess.TimeoutExpired:
            record["exit_code"] = "timeout"
    save(out / "run.json", record)
    print(json.dumps({"run": str(out), "exit_code": record["exit_code"], "evidence": evidence(record, out)}))
    return 0 if record["exit_code"] == 0 else 1


def report(catalogue: dict, directories: list[Path]) -> dict:
    runs, grouped, seen = [], {}, set()
    for directory in directories:
        for path in sorted(directory.rglob("run.json")):
            if path.resolve() in seen:
                continue
            seen.add(path.resolve())
            record = json.loads(path.read_text())
            key = record["disc"]
            if key not in catalogue["images"] or record["disc_sha256"] != catalogue["images"][key]["sha256"]:
                raise ValueError(f"{path}: disc identity differs from inventory")
            record["path"] = str(path)
            record["evidence"] = evidence(record, path.parent)
            runs.append(record)
            for field in {record["field"], *map(int, record["evidence"])}:
                grouped.setdefault((key, field), []).append(len(runs) - 1)
    # US and JP are separate binaries; refuse mixed revisions within either
    # region rather than silently upgrading old evidence to current coverage.
    for region in ("us", "jp"):
        if len({run["binary_sha256"] for run in runs if run["disc"].startswith(region)}) > 1:
            raise ValueError(f"mixed {region} binaries; report one revision at a time")
    rows = []
    for entry in catalogue["fields"]:
        indices = grouped.get((entry["disc"], entry["field"]), [])
        samples = [runs[i]["evidence"].get(str(entry["field"]), {}) for i in indices]
        moving = sum(sample.get("changed_intermediates", 0) for sample in samples)
        events, reasons = Counter(), Counter()
        for sample in samples:
            events.update(sample.get("events", {})); reasons.update(sample.get("fallbacks", {}))
        failed = any(runs[i]["exit_code"] != 0 for i in indices)
        status = "unvisited" if entry["availability"] == "present" else "not-on-disc"
        if indices:
            status = "failed-run" if failed else "motion-demonstrated" if moving else \
                     "intermediate-unverified" if events["intermediate"] else \
                     "endpoints-only" if events["endpoint"] else "native-fallback" if events["snapshot"] else "start-unverified"
        rows.append(dict(entry, status=status, runs=indices, changed_intermediates=moving, fallbacks=dict(reasons)))
    return {"schema": 1, "scope": "sampled starts only; motion proof is not a native-equivalence or full-playthrough claim",
            "summary": dict(Counter(row["status"] for row in rows)), "fields": rows, "runs": runs}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("inventory")
    p.add_argument("--disc", nargs="+", choices=statecheck.DISCS, default=list(statecheck.DISCS))
    p.add_argument("--out", type=Path, required=True)
    p = sub.add_parser("probe")
    p.add_argument("--build", type=Path, required=True); p.add_argument("--out", type=Path, required=True)
    p.add_argument("--disc", choices=statecheck.DISCS, required=True); p.add_argument("--field", type=int, required=True)
    p.add_argument("--env", action="append", default=[]); p.add_argument("--frames", type=int, default=1800)
    p.add_argument("--pad", type=Path, help="fixture-specific input script; copied into the evidence")
    p.add_argument("--start", type=int, default=700); p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--gpu", default="0")
    p = sub.add_parser("report")
    p.add_argument("--inventory", type=Path, required=True); p.add_argument("--runs", nargs="+", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "probe":
            return probe(args)
        else:
            value = inventory(args.disc) if args.command == "inventory" else report(json.loads(args.inventory.read_text()), args.runs)
            save(args.out, value)
            print(json.dumps(value.get("summary", {"entries": len(value["fields"])})))
        return 0
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"coverage: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
