#!/usr/bin/env python3
"""Exercise actual hardware waits, GPU replay and SPU output at host rates.

Native callbacks, input, non-silent raw PCM and native presentation pixels
must agree at 0/60/120/144/240 Hz, in real and stepped clocks. The stepped
clock must also retain the same safe-point count and submit no host frames.
"""
from pathlib import Path
import argparse
import bisect
import hashlib
import json
import os
import re
import subprocess
import statecheck


def run(build: Path, out: Path) -> dict:
    exe = build / "highrefreshtest"
    wine = not exe.exists() and os.name != "nt"
    if not exe.exists():
        exe = exe.with_suffix(".exe")
    command = ["wine", str(exe)] if wine else [str(exe)]
    env = dict(os.environ, XG_SETTINGS="none", SDL_VIDEO_DRIVER="windows" if wine else "offscreen", SDL_AUDIO_DRIVER="dummy")
    env.pop("XG_INTERPOLATION_HZ",None)
    persist=out/"settings"; persist.mkdir(parents=True,exist_ok=False)
    for mode in ("write","read"):
        with (persist/(mode+".log")).open("w") as log:
            subprocess.run(command+["0",str(persist),mode],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    authentic=out/"authentic";authentic.mkdir(parents=True,exist_ok=False)
    with (authentic/"run.log").open("w") as log:
        subprocess.run(command+["120",str(authentic),"authentic"],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
    results = []
    reference = None
    vram_reference = None
    pictures = None
    native = None
    heartbeat = None
    shader_pictures = None
    shaders = "shader=1" in subprocess.check_output(command+["--features"],env=env,text=True)
    for clock in (("real", "step", "poll", "no-idle", "shader") if shaders else ("real","step","poll","no-idle")):
        for hz in ((60,120) if clock=="poll" else (0,240) if clock=="shader" else (240,) if clock=="no-idle" else (0, 60, 120, 144, 240)):
            d = out / f"{clock}-{hz}"
            d.mkdir(parents=True, exist_ok=False)
            with (d / "run.log").open("w") as log:
                subprocess.run(command + [str(hz), str(d), clock], env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=90)
            line = next(l for l in (d / "run.log").read_text().splitlines() if l.startswith("high-refresh: "))
            fields = dict(re.findall(r"(\w+)=([\w.]+)", line))
            original = {k: fields[k] for k in ("native", "pads", "callbacks", "timers", "samples", "events", "state")}
            assert fields["auto"]==fields["actualnative"],fields
            pcm = (d / "audio.pcm").read_bytes()
            vram = (d / "vram.bin").read_bytes()
            frames = sorted((int(p.stem.split("_")[1]),statecheck.png_pixels_hash(p)) for p in d.glob("present_*.png"))
            stamps = [v for v,_ in frames]
            hashes = [frames[bisect.bisect_right(stamps,v)-1][1] for v in range(1,91)]
            assert frames and stamps[0]<=1, (clock,hz,"no initial native picture")
            if reference is None:
                reference, pictures, native, vram_reference = pcm, hashes, original, vram
            assert pcm == reference, (clock, hz, "PCM changed")
            assert vram == vram_reference, (clock,hz,"native VRAM changed")
            if clock=="shader":
                if shader_pictures is None:
                    shader_pictures=hashes
                assert hashes==shader_pictures,(clock,hz,"shader history changed")
                assert fields["host"]=="0",fields
            else:
                assert hashes == pictures, (clock, hz, "native presented pixels changed")
            assert original == native, (clock, hz, "native callbacks/accounting changed", original, native)
            if clock == "step":
                assert fields["host"] == "0", fields
                if heartbeat is None:
                    heartbeat = fields["heartbeat"]
                assert heartbeat == fields["heartbeat"], (clock, hz, "extra safe points")
            elif clock == "no-idle":
                assert fields["host"] == "0" and fields["quietmax"] == "2", fields
                assert 0 < int(fields["fallbacks"]) < int(fields["actualnative"]), fields
            elif hz and clock!="shader":
                assert int(fields["host"]) > 0, fields
                assert list(d.glob("host_*.png")), (clock, hz, "no rendered host samples")
            fields["clock"] = clock
            fields["pcm_sha256"] = hashlib.sha256(pcm).hexdigest()
            results.append(fields)
            print(line, flush=True)
    result = dict(cases=results, native_pcm_pixels_events_equal=True, stepped_safe_points_equal=True)
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    run(args.build.resolve(), args.out.resolve())
    print("high-refresh: real/stepped rates and available shader controls preserve native pixels, PCM, callbacks and Auto accounting")
