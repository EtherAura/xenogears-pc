#!/usr/bin/env python3
"""Compare movie-pack formats using the same user-supplied footage.

Requires ffmpeg and aomenc as external fixture-generation tools. Nothing
from the source movie or those tools ships with the port. Output holds
private footage: use a cache directory outside the synced repository.

  python tools/moviepack_bench.py SOURCE --test BUILD/moviepacktest --out DIR
  python tools/moviepack_bench.py SOURCE --test BUILD/moviepacktest --out DIR --reuse

Sequential timings include reading and packed RGBA conversion, but not
opening/indexing the AV1 file. Paced checks use the production reader at
15 fps, with cold and 500 ms primed starts. GPU upload/presentation, MDEC,
decoder workspace, disk-cold I/O and full-game contention are not measured.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


def stored_png(path: Path, width: int, height: int, rgb: bytes):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    pitch = width * 3
    rows = b"".join(b"\0" + rgb[y * pitch:(y + 1) * pitch] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows, level=0)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", type=Path)
    ap.add_argument("--test", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True, help="fresh private directory; --reuse keeps fixtures")
    ap.add_argument("--reuse", action="store_true")
    ap.add_argument("--scales", type=int, nargs="+", default=[2, 4, 8])
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--start", type=int, default=15, help="source frame to begin with (default 15)")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--version", choices=["SLUS-00664", "SLPS-01160"], default="SLUS-00664")
    a = ap.parse_args()
    if not 1 <= a.frames <= 1000 or not 1 <= a.runs <= 20 or a.start < 0 or any(s not in (1, 2, 4, 8) for s in a.scales):
        ap.error("frames 1..1000, runs 1..20, start >= 0, scales 1/2/4/8 required")
    a.source, a.test, a.out = a.source.resolve(), a.test.resolve(), a.out.resolve()
    config = dict(source=str(a.source), source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),
                  frames=a.frames, start=a.start, scales=a.scales, version=a.version)
    manifest = a.out / "fixtures.json"
    if a.reuse:
        if json.loads(manifest.read_text())["config"] != config:
            ap.error("fixture configuration differs; choose a fresh output directory")
    elif a.out.exists() and any(a.out.iterdir()):
        ap.error("output must be empty; use --reuse for existing matching fixtures")
    a.out.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(command, log):
        commands.append([str(x) for x in command])
        with log.open("w") as f:
            subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, check=True, timeout=600)

    if not a.reuse:
        for scale in a.scales:
            width, height = 320 * scale, 224 * scale
            directory = a.out / f"{scale}x"
            pngs = directory / "compressed" / a.version / "movies" / "d1s05"
            stored = directory / "stored" / a.version / "movies" / "d1s05"
            video = directory / "av1" / a.version / "movies"
            for d in (pngs, stored, video):
                d.mkdir(parents=True, exist_ok=True)
            # Every format starts with these same RGB frames. Scaling measures
            # replacement resolution, without claiming a particular art quality.
            raw = directory / "frames.rgb"
            filt = f"trim=start_frame={a.start}:end_frame={a.start + a.frames},setpts=PTS-STARTPTS,scale={width}:{height}:flags=neighbor"
            run(["ffmpeg", "-v", "error", "-threads", "2", "-filter_threads", "2", "-i", a.source, "-an", "-vf", filt,
                 "-frames:v", str(a.frames), "-pix_fmt", "rgb24", "-f", "rawvideo", "-y", raw], directory / "extract.log")
            frame_bytes = width * height * 3
            if raw.stat().st_size != frame_bytes * a.frames:
                raise RuntimeError("source has fewer frames than requested")
            with raw.open("rb") as f:
                for i in range(a.frames):
                    stored_png(stored / f"{i + 1:05d}.png", width, height, f.read(frame_bytes))
            raw_input = ["ffmpeg", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgb24",
                         "-video_size", f"{width}x{height}", "-framerate", "15", "-i", raw, "-threads", "2", "-filter_threads", "2"]
            run([*raw_input, "-pred", "mixed", "-compression_level", "6", "-y", pngs / "%05d.png"], directory / "png.log")
            y4m = directory / "frames.y4m"
            run([*raw_input, "-pix_fmt", "yuv420p", "-f", "yuv4mpegpipe", "-y", y4m], directory / "y4m.log")
            ivf = directory / "frames.ivf"
            run(["aomenc", "--ivf", "--passes=1", "--cpu-used=6", "--threads=2", "--row-mt=1",
                 "--end-usage=q", "--cq-level=24", "--kf-max-dist=15", "--lag-in-frames=0",
                 "-o", ivf, y4m], directory / "av1.log")
            run(["ffmpeg", "-v", "error", "-i", ivf, "-c:v", "copy", "-y", video / "d1s05.webm"], directory / "webm.log")
            raw.unlink()
            y4m.unlink()
            ivf.unlink()
        manifest.write_text(json.dumps(dict(config=config, commands=commands), indent=2))
    env = dict(os.environ, XG_SETTINGS="none", XG_HEADLESS="1", SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
    results = []
    for scale in a.scales:
        for format in ("compressed", "stored", "av1"):
            root = a.out / f"{scale}x" / format
            what = root / a.version / "movies" / ("d1s05.webm" if format == "av1" else "d1s05")
            row = dict(scale=scale, format=format, bytes=sum(p.stat().st_size for p in root.rglob("*") if p.is_file()),
                       sequential=[], paced=[])
            for run_number in range(a.runs):
                command = [str(a.test), str(a.out / "scratch"), "bench", str(what)]
                result = subprocess.run(command, env=env, capture_output=True, text=True, check=True, timeout=120)
                (a.out / f"{scale}x-{format}-decode-{run_number}.log").write_text(result.stdout + result.stderr)
                parsed = json.loads(next(s.split(": ", 1)[1] for s in result.stdout.splitlines() if s.startswith("moviepackbench: ")))
                if parsed["frames"] != a.frames:
                    raise RuntimeError("benchmark did not decode every requested frame")
                row["sequential"].append(parsed)
            for prime in (0, 500):
                result = subprocess.run([str(a.test), str(a.out / "scratch"), "playbench", str(root), str(a.frames), str(prime)],
                                        env=env, capture_output=True, text=True, check=True, timeout=120)
                (a.out / f"{scale}x-{format}-paced-{prime}.log").write_text(result.stdout + result.stderr)
                row["paced"].append(json.loads(next(s.split(": ", 1)[1] for s in result.stdout.splitlines() if s.startswith("moviepackplay: "))))
            results.append(row)
            (a.out / "results.json").write_text(json.dumps(dict(config=config, executable_sha256=hashlib.sha256(a.test.read_bytes()).hexdigest(),
                                                              results=results), indent=2))
            print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main()
