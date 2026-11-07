#!/usr/bin/env python3
"""Check texture decoding, GPU writes, filters, glyph overlays and keyed dumps.

    python tools/texturecheck.py [--build build/pc/release] [--no-build]
                                [--cpu-only] [--device N] [--no-interlock]

No disc images or settings changes are needed. Temporary GPU runs serialize
with the feature GPU lock. PNG/key verification uses Python's standard library.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from contextlib import contextmanager
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def read_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    at, packed = 8, []
    while at < len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        crc, = struct.unpack_from(">I", data, at + 8 + length)
        assert zlib.crc32(kind + body) == crc, path
        if kind == b"IHDR":
            w, h, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and color in (2, 6) and compression == filtering == interlace == 0, path
            channels = 4 if color == 6 else 3
        if kind == b"IDAT":
            packed.append(body)
        at += length + 12
    raw = zlib.decompress(b"".join(packed))
    stride = w * channels
    assert len(raw) == h * (stride + 1), path
    previous = bytearray(stride)
    out = bytearray()
    for y in range(h):
        kind = raw[y * (stride + 1)]
        assert kind in range(5), path
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            prediction = 0
            if kind == 1:
                prediction = a
            elif kind == 2:
                prediction = b
            elif kind == 3:
                prediction = (a + b) // 2
            elif kind == 4:
                p = a + b - c
                da, db, dc = abs(p - a), abs(p - b), abs(p - c)
                prediction = a if da <= db and da <= dc else b if db <= dc else c
            row[i] = (row[i] + prediction) & 255
        if channels == 4:
            out.extend(row)
        else:
            for x in range(w):
                out.extend(row[x * 3:x * 3 + 3])
                out.append(255)
        previous = row
    return w, h, bytes(out)


def key(data: bytes) -> str:
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return f"{h:016x}"


def verify_dump(directory: Path) -> list[dict]:
    records = [json.loads(line) for line in (directory / "observations.jsonl").read_text().splitlines()]
    assert records, directory
    for r in records:
        assert r["schema"] == "xg-texture-dump-1"
        w, h, depth = r["width"], r["height"], r["depth"]
        assert 1 <= w <= 256 and 1 <= h <= 256 and depth in (4, 8, 15)
        for field in ("rgba", "texels", "palette", "indices"):
            assert Path(r[field]).name == r[field] if r[field] else True
        texels = (directory / r["texels"]).read_bytes()
        assert len(texels) == w * h * (2 if depth == 15 else 1)
        assert key(b"xgtex1" + bytes([depth]) + struct.pack("<HH", w, h) + texels) == r["texel_key"]
        if depth < 15:
            palette = (directory / r["palette"]).read_bytes()
            count = 16 if depth == 4 else 256
            assert len(palette) == count * 2
            assert key(b"xgpal1" + struct.pack("<H", count) + palette) == r["palette_key"]
            colors = struct.unpack(f"<{count}H", palette)
            words = [colors[index] for index in texels]
            expected_indices = bytes(c for index in texels for c in (index, index, index, 255))
            assert read_png(directory / r["indices"]) == (w, h, expected_indices)
        else:
            assert r["palette_key"] == "0000000000000000" and not r["palette"] and not r["indices"]
            words = struct.unpack(f"<{w*h}H", texels)
        rgba = bytearray()
        for word in words:
            for shift in (0, 5, 10):
                c = (word >> shift) & 31
                rgba.append((c << 3) | (c >> 2))
            rgba.append(0 if word == 0 else 128 if word & 0x8000 else 255)
        assert read_png(directory / r["rgba"]) == (w, h, bytes(rgba)), r
    assert not list(directory.glob("*.tmp")), directory
    return records


def verify_glyph_dump(directory: Path) -> list[dict]:
    records = [json.loads(line) for line in (directory / "observations.jsonl").read_text().splitlines()]
    assert records, directory
    for r in records:
        assert r["schema"] == "xg-glyph-dump-1"
        w, h, depth, limit = r["width"], r["height"], r["depth"], r["index_limit"]
        assert 1 <= w <= 256 and 1 <= h <= 64
        assert (depth == 4 and limit in (4, 16)) or (depth == 8 and limit == 256)
        pw, ph, rgba = read_png(directory / (r["key"] + ".indices.png"))
        assert (pw, ph) == (w, h), r
        indices = rgba[::4]
        assert all(rgba[i:i + 4] == bytes((index, index, index, 255)) and index < limit
                   for i, index in zip(range(0, len(rgba), 4), indices)), r
        assert key(b"xgtex1" + bytes([depth]) + struct.pack("<HH", w, h) + indices) == r["key"], r
    assert not list(directory.glob("*.tmp")), directory
    return records


@contextmanager
def gpu_lock():
    if os.name == "nt":
        yield
        return
    import fcntl
    path = ROOT / "work/agents/feature-gpu.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/pc/release")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--cpu-only", action="store_true")
    parser.add_argument("--device", type=int)
    parser.add_argument("--no-interlock", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    targets = ["texturecachetest", "texturepacktest", "glyphtest"] + ([] if args.cpu_only else ["texturetest", "texturefiltertest", "texturepackgputest", "glyphgputest"])
    if not args.no_build:
        subprocess.run(["cmake", "--build", str(build), "--target", *targets, "-j", "4"], check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("XG_")}
    env.update(SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", XG_HEADLESS="1", XG_SETTINGS="none")
    if args.device is not None:
        env["XG_VK_DEVICE"] = str(args.device)
    if args.no_interlock:
        env["XG_VK_NO_INTERLOCK"] = "1"
    suffix = ".exe" if os.name == "nt" else ""
    subprocess.run([str(build / ("texturecachetest" + suffix))], env=env, check=True, timeout=60)
    with tempfile.TemporaryDirectory(prefix="xg-packs-") as temp:
        root, settings = Path(temp) / "pack-é", Path(temp) / "packs.ini"
        subprocess.run([str(build / ("texturepacktest" + suffix)), str(root), str(settings)], env=env, check=True, timeout=60)
        subprocess.run([str(build / ("texturepacktest" + suffix)), str(root), str(settings), "--reload"], env=env, check=True, timeout=60)
    with tempfile.TemporaryDirectory(prefix="xg-glyphs-") as temp:
        root, settings = Path(temp) / "glyphs-é", Path(temp) / "glyphs.ini"
        subprocess.run([str(build / ("glyphtest" + suffix)), str(root), str(settings)], env=env, check=True, timeout=60)
        subprocess.run([str(build / ("glyphtest" + suffix)), str(root), str(settings), "--reload"], env=env, check=True, timeout=60)
        boot, = (root / "dump-é").iterdir()
        records = verify_glyph_dump(boot / "glyphs")
        print(f"texturecheck: {len(records)} glyph observations have exact keys and native indices")
    if not args.cpu_only:
        with tempfile.TemporaryDirectory(prefix="xg-textures-") as temp:
            directory = Path(temp) / "textures-é"
            with gpu_lock():
                subprocess.run([str(build / ("texturepackgputest" + suffix)), str(Path(temp) / "pack-é")], env=env, check=True, timeout=60)
                subprocess.run([str(build / ("glyphgputest" + suffix)), str(Path(temp) / "glyphs-é")], env=env, check=True, timeout=60)
                subprocess.run([str(build / ("texturetest" + suffix)), str(directory)], env=env, check=True, timeout=60)
                settings = Path(temp) / "filters.ini"
                subprocess.run([str(build / ("texturefiltertest" + suffix)), str(settings)], env=env, check=True, timeout=60)
                subprocess.run([str(build / ("texturefiltertest" + suffix)), str(settings), "--reload"], env=env, check=True, timeout=60)
            boot, = directory.iterdir()
            records = verify_dump(boot)
            glyph_boot, = (Path(temp) / "glyphs-é/discovery-é").iterdir()
            glyphs = verify_glyph_dump(glyph_boot / "glyphs")
            assert len(glyphs) == 2, "late discovery missed a packed glyph plane"
            print(f"texturecheck: {len(glyphs)} already-built GPU glyph sources have exact dump keys")
            locations, palettes = defaultdict(set), defaultdict(set)
            for r in records:
                locations[(r["texel_key"], r["palette_key"])].add((r["tpage"], r["clut"]))
                palettes[r["texel_key"]].add(r["palette_key"])
            assert any(len(v) > 1 for v in locations.values()), "relocation observation missing"
            assert any(len(v) > 1 for v in palettes.values()), "palette update observation missing"
            print(f"texturecheck: {len(records)} observations have exact keys, RGBA, indices and CLUTs")


if __name__ == "__main__":
    main()
