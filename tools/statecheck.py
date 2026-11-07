#!/usr/bin/env python3
"""Save states of the PC port (port/src/system/savestate.h): inspect, compare
and test them.

    statecheck.py info FILE            the header
    statecheck.py chunks FILE          the body's chunks (tag, size)
    statecheck.py diff A B [--all]     chunks whose bytes differ (the header's
                                       time is ignored); exit 1 if any
    statecheck.py run BUILD OUT [--scene NAME ...] [--jp]
                                       the deterministic equivalence test: per
                                       scene, run A saves at N and M, run B
                                       loads N and saves at M (XG_CLOCK=step,
                                       a pad script): the frames after N, the
                                       audio from N's position and the two
                                       states at M must be identical
    statecheck.py rewind BUILD OUT [--scene NAME ...] [--jp]
                                       rewind on and unused, and rewind going
                                       back at N, each against a plain run:
                                       frames, audio and the state at M equal
    statecheck.py refusals BUILD OUT [--jp]
                                       states the build must refuse (another
                                       build, region, a disc without an image,
                                       damaged, cut, missing), each with a
                                       message, the game going on
    statecheck.py inventory BUILD      every writable static of the platform
                                       objects is in
                                       port/src/system/savestate_inventory.txt

The format: "XGSTATE\\0", u32 version, u32 header size, a 256-byte header at
fixed offsets, the packed thumbnail, the packed body (statecodec.h: blocks
of u32 raw size, u32 packed size, LZ77 sequences).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = next((p for p in Path(__file__).absolute().parents if (p / "config" / "us").is_dir()),
            Path(__file__).resolve().parents[1])
HEADER_SIZE = 256


# ---- the format ---------------------------------------------------------------------------


def unpack_block(data: bytes, raw: int) -> bytes:
    out = bytearray()
    ip, n = 0, len(data)
    while ip < n:
        token = data[ip]
        ip += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = data[ip]
                ip += 1
                lit += b
                if b != 255:
                    break
        out += data[ip:ip + lit]
        ip += lit
        if ip == n:
            break
        dist = data[ip] | data[ip + 1] << 8
        ip += 2
        ln = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                b = data[ip]
                ip += 1
                ln += b
                if b != 255:
                    break
        start = len(out) - dist
        if dist >= ln:
            out += out[start:start + ln]
        else:
            pattern = bytes(out[start:])
            reps = ln // dist + 1
            out += (pattern * reps)[:ln]
    if len(out) != raw:
        raise ValueError("damaged block")
    return bytes(out)


def unpack(data: bytes, raw_size: int) -> bytes:
    out = bytearray()
    ip = 0
    while ip < len(data):
        raw, packed = struct.unpack_from("<II", data, ip)
        ip += 8
        block = data[ip:ip + packed]
        ip += packed
        out += block if packed == raw else unpack_block(block, raw)
    if len(out) != raw_size:
        raise ValueError("damaged body")
    return bytes(out)


def read_state(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    if data[:8] != b"XGSTATE\0":
        raise ValueError(f"{path}: not a save state")
    version, size = struct.unpack_from("<II", data, 8)
    if size != HEADER_SIZE:
        raise ValueError(f"{path}: header size {size}")
    h = {
        "version": version,
        "build": data[16:32].hex(),
        "region": data[32:36].split(b"\0")[0].decode(),
        "disc": struct.unpack_from("<I", data, 36)[0],
        "serial": data[40:56].split(b"\0")[0].decode(),
        "sectors": struct.unpack_from("<I", data, 56)[0],
        "vblank": struct.unpack_from("<I", data, 60)[0],
        "savedAt": struct.unpack_from("<q", data, 64)[0],
        "playTime": struct.unpack_from("<I", data, 72)[0],
        "mode": struct.unpack_from("<i", data, 76)[0],
        "place": struct.unpack_from("<i", data, 80)[0],
        "label": data[84:180].split(b"\0")[0].decode(errors="replace"),
        "flags": struct.unpack_from("<I", data, 180)[0],
        "dumpFrames": struct.unpack_from("<Q", data, 184)[0],
        "thumb": struct.unpack_from("<II", data, 192),
        "bodySize": struct.unpack_from("<Q", data, 200)[0],
        "bodyHash": struct.unpack_from("<Q", data, 208)[0],
        # The image's load bias (statefile.c H_IMAGE): a PIE's or a shared library's; 0 before it.
        "image": struct.unpack_from("<Q", data, 232)[0],
    }
    thumb_packed = struct.unpack_from("<I", data, 216)[0]
    body_packed = struct.unpack_from("<Q", data, 224)[0]
    at = HEADER_SIZE + thumb_packed
    body = unpack(data[at:at + body_packed], h["bodySize"])
    return h, body


def chunks(body: bytes) -> list[tuple[str, int, int]]:
    """(tag, offset of the data, size) for each chunk."""
    out = []
    ip = 0
    while ip < len(body):
        tl = body[ip]
        tag = body[ip + 1:ip + 1 + tl].decode(errors="replace")
        size = struct.unpack_from("<I", body, ip + 1 + tl)[0]
        out.append((tag, ip + 1 + tl + 4, size))
        ip += 1 + tl + 4 + size
    return out


# ---- commands -----------------------------------------------------------------------------


def cmd_info(args) -> int:
    h, body = read_state(Path(args.file))
    for k, v in h.items():
        print(f"{k:11} {v}")
    print(f"{'chunks':11} {len(chunks(body))}")
    return 0


def cmd_chunks(args) -> int:
    _, body = read_state(Path(args.file))
    for tag, _, size in chunks(body):
        print(f"{size:10} {tag}")
    return 0


def mask_guard(data: bytes, guard: bytes) -> bytes:
    """The stack protector's value, where frames keep it, as zeros."""
    out = bytearray(data)
    at = data.find(guard)
    while at >= 0:
        out[at:at + 8] = bytes(8)
        at = data.find(guard, at + 1)
    return bytes(out)


def executable(build: Path) -> tuple[list[str], Path]:
    """How to run the build's executable, and the file itself: the Linux
    build's, or the Windows build's (xenogears.exe) under Wine."""
    for name in ("xenogears", "xenogears-enhanced"):  # the Original or the Enhanced engine (XG_ENGINE)
        if (build / name).exists():
            return [str(build / name)], build / name
        if (build / f"{name}.exe").exists():
            return ["wine", str(build / f"{name}.exe")], build / f"{name}.exe"
    return [str(build / "xenogears")], build / "xenogears"


def game_library(exe: Path) -> Path | None:
    """A shared-library build's game (port/CMakeLists.txt XG_SHARED): lib<exe>.so beside the
    executable, when the executable is its launcher (needs it), else None."""
    lib = exe.parent / f"lib{exe.name}.so"
    if not lib.exists():
        return None
    p = subprocess.run(["objdump", "-p", str(exe)], capture_output=True, text=True)
    return lib if re.search(rf"^\s*NEEDED\s+{re.escape(lib.name)}\s*$", p.stdout, re.M) else None


def build_symbols(build: Path | None) -> dict:
    """Addresses the comparison needs from the build: Battling's coroutine
    stacks and the module sections' starts (ELF or PE: a PE symbol has no
    size, so it runs to the next one)."""
    if build is None or not executable(build)[1].exists():
        return {}
    exe = executable(build)[1]
    lib = game_library(exe)
    if lib is not None:
        exe = lib
    out = {}
    text = subprocess.run(["nm", "-n", "-S", str(exe)], capture_output=True, text=True).stdout
    rows = []
    for line in text.splitlines():
        f = line.split()
        if len(f) == 4:
            rows.append((int(f[0], 16), int(f[1], 16), f[3]))
        elif len(f) == 3 and f[1] not in ("A", "a"):
            rows.append((int(f[0], 16), None, f[2]))
    for i, (addr, size, name) in enumerate(rows):
        name = re.sub(r"\.lto_priv\.\d+$", "", name)
        if size is None:
            size = next((a - addr for a, _, _ in rows[i + 1:] if a > addr), 0)
        if name in ("pcThreads", "VSync", "resumerSp") and name not in out:
            out[name] = (addr, size)
        elif name.startswith("__start_xgbss_"):
            out["section." + name[len("__start_xgbss_"):] + ".bss"] = addr
    text = subprocess.run(["objdump", "-h", str(exe)], capture_output=True, text=True).stdout
    for line in text.splitlines():
        f = line.split()
        if len(f) >= 4 and f[1].startswith("xgbss_"):
            out.setdefault("section." + f[1][6:] + ".bss", int(f[3], 16))
    return out


def at_bias(syms: dict, bias: int) -> dict:
    """The symbols' link addresses where a state's process had them: a PIE or a shared library
    sits at its load bias (the header's image field)."""
    if not bias:
        return syms
    return {k: ((v[0] + bias, v[1]) if isinstance(v, tuple) else v + bias) for k, v in syms.items()}


def dead_ranges(chunk: dict, syms: dict) -> dict[str, list[tuple[int, int]]]:
    """Per memory chunk, byte ranges the comparison leaves out:
    - below a stack pointer: what calls that returned left there (host code's
      values among them, different in every process);
    - from the parked stack pointer to the return address into VSync: the
      port's own frames of the park (hwWaitVSync, the state code), whose
      spare slots and saved registers depend on the port's path, not the
      game's (the game's own frames start above)."""
    dead: dict[str, list[tuple[int, int]]] = {}
    if "mem.stack.sp" not in chunk or "mem.stack.low" not in chunk:
        return dead
    sp = struct.unpack("<Q", chunk["mem.stack.sp"])[0]
    low = struct.unpack("<Q", chunk["mem.stack.low"])[0]
    stack = chunk.get("mem.stack", b"")
    vsync = syms.get("VSync")

    def park_frames(data: bytes, base: int, at: int) -> int:
        """The offset just past the return address into VSync above `at`."""
        if vsync is None:
            return at - base
        for off in range((at - base + 7) & ~7, len(data) - 7, 8):
            v = struct.unpack_from("<Q", data, off)[0]
            if vsync[0] <= v < vsync[0] + vsync[1]:
                return off + 8
        return at - base

    if low <= sp < low + len(stack):
        dead["mem.stack"] = [(0, park_frames(stack, low, sp))]
    elif "resumerSp" in syms and "section.battling.bss" in syms and "mem.battling.bss" in chunk:
        # Parked on a Battling stack: the game stack waits in the coroutine switch.
        rs = syms["resumerSp"][0] - syms["section.battling.bss"]
        resumer = struct.unpack_from("<Q", chunk["mem.battling.bss"], rs)[0]
        if low <= resumer < low + len(stack):
            dead["mem.stack"] = [(0, resumer - low)]
    if "pcThreads" in syms and "section.battling.bss" in syms and "mem.battling.bss" in chunk:
        start, size = syms["pcThreads"]
        base = syms["section.battling.bss"]
        bss = chunk["mem.battling.bss"]
        slot = 8 + (1 << 20)  # thread.c PcThread: the saved stack pointer, then its 1 MB stack
        for t in range(2):
            tsp = struct.unpack_from("<Q", bss, start - base + t * slot)[0]
            lo, hi = start + t * slot + 8, start + (t + 1) * slot
            if lo <= sp < hi:  # the park is on this thread's stack
                dead.setdefault("mem.battling.bss", []).append((lo - base, park_frames(bss, base, sp)))
            elif lo <= tsp <= hi:
                dead.setdefault("mem.battling.bss", []).append((lo - base, tsp - base))
            elif tsp == 0:  # never started: all of it
                dead.setdefault("mem.battling.bss", []).append((lo - base, hi - base))
    return dead


def state_diffs(a: Path, b: Path, build: Path | None = None, live: bool = True) -> tuple[list[str], dict]:
    """Header mismatches, and per chunk the byte offsets that differ (the
    stack protector's values and, with live, dead stack bytes left out)."""
    ha, ba = read_state(a)
    hb, bb = read_state(b)
    syms = build_symbols(build)
    header = []
    for key in ("build", "region", "disc", "vblank", "mode", "place", "dumpFrames"):
        if ha[key] != hb[key]:
            header.append(f"header {key}: {ha[key]} / {hb[key]}")
    ca, cb = chunks(ba), chunks(bb)
    if [c[0] for c in ca] != [c[0] for c in cb]:
        header.append("the chunk lists differ")
    guard_a = next((ba[o:o + s] for t, o, s in ca if t == "mem.guard"), None)
    guard_b = next((bb[o:o + s] for t, o, s in cb if t == "mem.guard"), None)
    dead_a = dead_ranges({t: ba[o:o + s] for t, o, s in ca}, at_bias(syms, ha["image"])) if live else {}
    dead_b = dead_ranges({t: bb[o:o + s] for t, o, s in cb}, at_bias(syms, hb["image"])) if live else {}
    out: dict[str, set] = {}
    for (tag, oa, sa), (_, ob, sb) in zip(ca, cb):
        if sa != sb:
            header.append(f"{tag}: {sa} / {sb} bytes")
            continue
        if tag == "mem.guard":
            continue  # the stack protector's value: each process its own
        x, y = ba[oa:oa + sa], bb[ob:ob + sb]
        if x == y:
            continue
        if tag.startswith("mem.") and guard_a and guard_b:
            x, y = mask_guard(x, guard_a), mask_guard(y, guard_b)
        if x != y and (tag in dead_a or tag in dead_b):
            if dead_a.get(tag) != dead_b.get(tag):
                header.append(f"{tag}: the parked stack pointers differ")
            else:
                x, y = bytearray(x), bytearray(y)
                for lo, hi in dead_a[tag]:
                    lo, hi = max(lo, 0), min(hi, sa)
                    if hi > lo:
                        x[lo:hi] = bytes(hi - lo)
                        y[lo:hi] = bytes(hi - lo)
                x, y = bytes(x), bytes(y)
        if x != y:
            out[tag] = {i for i in range(sa) if x[i] != y[i]}
    return header, out


def describe_diffs(header: list[str], diffs: dict, mask: dict | None = None, limit: int = 40) -> list[str]:
    problems = list(header)
    for tag, offs in diffs.items():
        if mask and tag in mask:
            offs = offs - mask[tag]
        if not offs:
            continue
        first = min(offs)
        problems.append(f"{tag}: {len(offs)} bytes differ, first at +0x{first:X}")
        if len(problems) > limit:
            problems.append("...")
            break
    return problems


def diff_states(a: Path, b: Path, show_all: bool = False, quiet: bool = False, build: Path | None = None,
                live: bool = True, mask: dict | None = None) -> list[str]:
    header, diffs = state_diffs(a, b, build, live)
    problems = describe_diffs(header, diffs, mask, 10 ** 9 if show_all else 40)
    if not quiet:
        for p in problems:
            print(p)
    return problems


def cmd_diff(args) -> int:
    problems = diff_states(Path(args.a), Path(args.b), args.all, build=Path(args.build) if args.build else None,
                           live=not args.raw)
    if not problems:
        print("identical")
    return 1 if problems else 0


# ---- the equivalence test ----------------------------------------------------------------

DISCS = {
    "us1": "bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue",
    "us2": "bin/Xenogears (USA) (Disc 2)/Xenogears (USA) (Disc 2).cue",
    "jp1": "bin/Xenogears (Japan) (Disc 1)/Xenogears (Japan) (Disc 1).cue",
    "jp2": "bin/Xenogears (Japan) (Disc 2)/Xenogears (Japan) (Disc 2).cue",
}


def pad_script(path: Path, seed: int, start: int = 200, end: int = 20000, buttons=None) -> None:
    """Random presses (the D-pad, Cross, Circle, Triangle, Square, L1, R1)."""
    import random

    rng = random.Random(seed)
    buttons = buttons or ["2000", "8000", "1000", "4000", "0040", "0020", "0010", "0080", "0004", "0008"]
    lines = []
    v = start
    while v < end:
        lines.append(f"{v} {rng.choice(buttons)}")
        v += rng.randint(4, 30)
        lines.append(f"{v} 0000")
        v += rng.randint(2, 20)
    path.write_text("\n".join(lines) + "\n")


def pad_steady(path: Path, button: str, start: int = 300, every: int = 40, end: int = 20000) -> None:
    """One button pressed for 4 vblanks every `every` vblanks."""
    lines = []
    for v in range(start, end, every):
        lines += [f"{v} {button}", f"{v + 4} 0000"]
    path.write_text("\n".join(lines) + "\n")


def pad_battle(path: Path, end: int = 20000) -> None:
    """The battle selector (XG_BOOT_MODE=2): a scene picked, Start, then the
    fight with Triangle (attack) and Cross alternating."""
    presses = [(500, "4000"), (530, "0040"), (700, "0040"), (712, "4000"), (724, "4000"), (736, "4000"),
               (778, "0800")]
    v, k = 1000, 0
    while v < end:
        presses.append((v, "0010" if k % 2 == 0 else "0040"))
        v, k = v + 20, k + 1
    lines = []
    for at, b in presses:
        lines += [f"{at} {b}", f"{at + 6} 0000"]
    path.write_text("\n".join(lines) + "\n")


# Each scene: the disc, N (the load point), M (the end), the environment, the
# input (a seed for random presses from `buttons`, ("steady", button), or
# None), and the disc run B starts from (default: the same).
SCENES = {
    "field": dict(disc="1", n=700, m=1700, env={"XG_FIELD": "8", "XG_PARTY": "0"}, pad=7),
    "menu": dict(disc="1", n=700, m=1500, env={"XG_BOOT_MODE": "5"}, pad=11,
                 buttons=["1000", "4000", "8000", "2000", "0040", "0040", "0020"]),
    "battle": dict(disc="1", n=1600, m=2800, env={"XG_BOOT_MODE": "2"}, pad="battle"),
    "worldmap": dict(disc="1", n=900, m=1900, env={"XG_BOOT_MODE": "3"}, pad=5),
    "movie": dict(disc="1", n=700, m=1500, env={"XG_BOOT_MODE": "6"}, pad=None),
    "opening": dict(disc="1", n=1300, m=2300, env={}, pad=None),
    "battling": dict(disc="1", n=1300, m=2300, env={"XG_BOOT_MODE": "4"}, pad=("steady", "0040"),
                     hold=8),  # the Enhanced arena's picture between right after a restore (its sandbox ring is host memory)
    "disc2": dict(disc="2", n=700, m=1700, env={"XG_FIELD": "616", "XG_PARTY": "0", "XG_PRESET": "disc2"}, pad=13),
    "disc2swap": dict(disc="2", n=700, m=1700, env={"XG_FIELD": "616", "XG_PARTY": "0", "XG_PRESET": "disc2"},
                      pad=13, bdisc="1"),
    # Speedup on (speedup.h): the battle's data load after N runs at 8x in A
    # and in B (loaded at N), then the battle with Speedup's pacing. The
    # stepped clock keeps the original single park per frame, so the
    # real-clock catch-up park (hwWaitVSyncSince) is not reached here.
    "speedup": dict(disc="1", n=600, m=1600, env={"XG_BOOT_MODE": "2", "XG_SPEEDUP": "1"}, pad="battle"),
    # Battle speed at 4x (xg-mrj.2, port/src/system/battlespeed.c): N falls in
    # the battle's 4x stretch (the audio clock a quarter of the game's), M in
    # a later one: the state carries the factor and the audio clock's anchor.
    "battlespeed": dict(disc="1", n=1500, m=2600, env={"XG_BOOT_MODE": "2", "XG_BATTLE_SPEED": "4"}, pad="battle"),
}


def run_port(build: Path, out: Path, disc: str, quit_at: int, env: dict) -> int:
    out.mkdir(parents=True, exist_ok=True)
    (out / "frames").mkdir(exist_ok=True)
    (out / "cards").mkdir(exist_ok=True)
    e = dict(os.environ)
    e.update({
        "XG_HEADLESS": "1", "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy", "XG_SETTINGS": "none",
        "XG_CLOCK": "step", "XG_DUMP_FRAMES": str(out / "frames"), "XG_AUDIO_DUMP": str(out / "audio.pcm"),
        "XG_QUIT_AT": str(quit_at), "XG_CARD_DIR": str(out / "cards"), "XG_VK_DEVICE": e.get("XG_VK_DEVICE", "1"),
        "TMPDIR": str(out),
    })
    e.update(env)
    run = executable(build)[0]
    if run[0] == "wine":
        e.setdefault("WINEDEBUG", "-all")  # paths stay Unix ones: the process's drive is Z:
    with open(out / "port.log", "w") as log:
        p = subprocess.run(["nice", "-n", "19", "ionice", "-c3", "timeout", "-s", "INT", "-k", "30", "1200",
                            *run, str(ROOT / DISCS[disc])], env=e, stdout=log,
                           stderr=subprocess.STDOUT, cwd=out)
    return p.returncode


def frame_at(frames: Path, vblank: int, cache: dict) -> str | None:
    """The hash of the picture shown at a vblank (dumps are written on change)."""
    if frames not in cache:
        names = sorted((int(m.group(1)), f) for f in os.listdir(frames)
                       if (m := re.match(r"frame_(\d+)\.png$", f)))
        cache[frames] = names
    best = None
    for v, f in cache[frames]:
        if v <= vblank:
            best = f
        else:
            break
    if best is None:
        return None
    return png_pixels_hash(frames / best)


def png_pixels_hash(path: Path) -> str:
    """The picture's size and pixels (a dump's text chunks name its vblank)."""
    import zlib

    d = path.read_bytes()
    i, idat, ihdr = 8, b"", b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        kind = d[i + 4:i + 8]
        if kind == b"IHDR":
            ihdr = d[i + 8:i + 8 + n]
        elif kind == b"IDAT":
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    return hashlib.sha1(ihdr + zlib.decompress(idat)).hexdigest()


def compare_frames(fa: Path, fb: Path, first: int, last: int, hold: tuple[int, int] | None = None,
                   notes: list[str] | None = None) -> list[str]:
    """The pictures of two runs per vblank. With hold=(v0, span), a vblank in
    v0..v0+span where B shows its previous picture again while A changed is
    noted (appended to notes), not failed: a picture between that B could not
    draw after a rewind restore (the 60 Hz Battling's sandbox ring is host
    memory, not state)."""
    cache: dict = {}
    names_a = {int(m.group(1)) for f in os.listdir(fa) if (m := re.match(r"frame_(\d+)\.png$", f))}
    names_b = {int(m.group(1)) for f in os.listdir(fb) if (m := re.match(r"frame_(\d+)\.png$", f))}
    points = sorted(v for v in names_a | names_b if first <= v <= last) or [first]
    bad, held = [], []
    for v in points:
        if frame_at(fa, v, cache) != frame_at(fb, v, cache):
            if hold and hold[0] <= v <= hold[0] + hold[1] and frame_at(fb, v, cache) == frame_at(fb, v - 1, cache):
                held.append(v)
            else:
                bad.append(v)
    if held and notes is not None:
        notes.append(f"held picture{'s' if len(held) > 1 else ''} after the restore at {', '.join(map(str, held))} (a picture between not replayed)")
    return [f"frames differ at {len(bad)} vblanks, first {bad[0]}"] if bad else []


def scene_test(build: Path, out: Path, name: str, jp: bool, extra: dict | None = None) -> tuple[list[str], list[str]]:
    """Problems, and notes. Four runs of the scene with the same input:
       A saves at N and at M; B loads A's N and saves at M; C and D save at
       M only. C and D show what differs between two plain runs (the
       game's own nondeterminism: masked, and named); A against C shows that
       saving changes nothing; B against A that a load continues the game
       exactly (the frames after N, the sound from N, the state at M)."""
    sc = SCENES[name]
    n, m, env, seed, buttons = sc["n"], sc["m"], sc["env"], sc["pad"], sc.get("buttons")
    region = "jp" if jp else "us"
    disc = region + sc["disc"]
    bdisc = region + sc.get("bdisc", sc["disc"])
    out.mkdir(parents=True, exist_ok=True)
    env = {**env, **(extra or {})}
    if seed == "battle":
        pad_battle(out / "pad.txt")
    elif isinstance(seed, tuple):
        pad_steady(out / "pad.txt", seed[1])
    elif seed is not None:
        pad_script(out / "pad.txt", seed, buttons=buttons)
    else:
        (out / "pad.txt").write_text("0 0000\n")
    env["XG_PAD_SCRIPT"] = str(out / "pad.txt")
    other = {"1": "2", "2": "1"}
    benv = dict(env)
    env.setdefault("XG_DISC2", str(ROOT / DISCS[region + other[sc["disc"]]]))
    benv.setdefault("XG_DISC2", str(ROOT / DISCS[region + other[bdisc[-1]]]))
    sa, sm, sb, sc, sd = (out / f for f in ("a_n.xgs", "a_m.xgs", "b_m.xgs", "c_m.xgs", "d_m.xgs"))
    for f in (sa, sm, sb, sc, sd):
        if f.exists():
            f.unlink()
    import concurrent.futures as cf

    with cf.ThreadPoolExecutor(3) as pool:
        ra = pool.submit(run_port, build, out / "a", disc, m + 120, {**env, "XG_STATE_SAVE": f"{n}:{sa},{m}:{sm}"})
        rc = pool.submit(run_port, build, out / "c", disc, m + 120, {**env, "XG_STATE_SAVE": f"{m}:{sc}"})
        rd = pool.submit(run_port, build, out / "d", disc, m + 120, {**env, "XG_STATE_SAVE": f"{m}:{sd}"})
        codes = (ra.result(), rc.result(), rd.result())
    if not (sa.exists() and sm.exists() and sc.exists() and sd.exists()):
        return [f"runs A, C, D (exit {codes}) did not write their states"], []
    benv["XG_PAD_SCRIPT"] = env["XG_PAD_SCRIPT"]
    if bdisc != disc:
        benv.pop("XG_PRESET", None)
    rb = run_port(build, out / "b", bdisc, m + 120, {**benv, "XG_STATE_LOAD": str(sa), "XG_STATE_SAVE": f"{m}:{sb}"})
    if not sb.exists():
        return [f"run B (exit {rb}) wrote no state"], []
    problems = [f"{r} exited {code}" for r, code in zip("ACDB", (*codes, rb)) if code != 0]
    notes = []
    header, nondet = state_diffs(sc, sd, build)
    if header:
        problems += [f"C/D: {p}" for p in header]
    for tag, offs in nondet.items():
        notes.append(f"two plain runs differ: {tag}, {len(offs)} bytes at +0x{min(offs):X} (masked)")
        # Whole words: a host pointer left in a frame differs in a varying number of its bytes.
        nondet[tag] = {(o & ~7) + k for o in offs for k in range(8)}
    ha, hm = read_state(sa)[0], read_state(sm)[0]
    first, last = ha["vblank"] + 1, hm["vblank"]
    audio = {r: (out / r / "audio.pcm").read_bytes() for r in "abc"}
    at, end = ha["dumpFrames"] * 4, hm["dumpFrames"] * 4
    # A save changes nothing.
    problems += [f"A/C (saving): {p}" for p in compare_frames(out / "a" / "frames", out / "c" / "frames", 0, last)]
    if audio["a"][:end] != audio["c"][:end]:
        problems.append("A/C (saving): audio differs")
    problems += [f"A/C (saving): {p}" for p in describe_diffs(*state_diffs(sm, sc, build), nondet)]
    # A load goes on exactly.
    problems += [f"B/A (loading): {p}" for p in compare_frames(out / "a" / "frames", out / "b" / "frames", first, last)]
    if audio["a"][at:end] != audio["b"][at:end]:
        diffs = next((i for i in range(at, min(len(audio["a"]), len(audio["b"]), end))
                      if audio["a"][i] != audio["b"][i]), None)
        problems.append(f"B/A (loading): audio differs from byte {diffs} (the state at {at}, compared to {end})")
    problems += [f"B/A (loading): {p}" for p in describe_diffs(*state_diffs(sm, sb, build), nondet)]
    notes.append(f"N={ha['vblank']} M={hm['vblank']}, {last - first + 1} vblanks, {(end - at) // 4} audio frames")
    return problems, notes


def cmd_run(args) -> int:
    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    names = args.scene or list(SCENES)
    failed = 0
    for name in names:
        extra = dict(e.split("=", 1) for e in (args.env or []))
        problems, notes = scene_test(build, out / name, name, args.jp, extra)
        print(f"{name:10} {'ok' if not problems else 'FAIL'}", flush=True)
        for p in problems[:12]:
            print(f"           {p}")
        for p in notes:
            print(f"           ({p})")
        failed += bool(problems)
    print(f"statecheck: {len(names)} scene{'s' if len(names) != 1 else ''}, {len(names) - failed} ok"
          + (f", {failed} failed" if failed else ""))
    return 1 if failed else 0


# ---- rewind ---------------------------------------------------------------------------------


def rewind_test(build: Path, out: Path, name: str, jp: bool, extra: dict | None = None) -> tuple[list[str], list[str]]:
    """Four runs of the scene with the same input, each saving at M: C and D
    plain (the game's own nondeterminism, masked), R1 with rewind on and never
    used (capturing changes nothing), R2 with rewind on going back
    REWIND_STEPS captures at N (the replay from there reaches M exactly as C:
    the input is the same, the state at the capture exact). R1 and R2 must
    equal C: the frames, the audio dump (R2's is cut back at the rewind and
    written again) and the state at M."""
    sc = SCENES[name]
    n, m, env, seed, buttons = sc["n"], sc["m"], sc["env"], sc["pad"], sc.get("buttons")
    region = "jp" if jp else "us"
    disc = region + sc["disc"]
    out.mkdir(parents=True, exist_ok=True)
    env = {**env, **(extra or {})}
    if seed == "battle":
        pad_battle(out / "pad.txt")
    elif isinstance(seed, tuple):
        pad_steady(out / "pad.txt", seed[1])
    elif seed is not None:
        pad_script(out / "pad.txt", seed, buttons=buttons)
    else:
        (out / "pad.txt").write_text("0 0000\n")
    env["XG_PAD_SCRIPT"] = str(out / "pad.txt")
    env.setdefault("XG_DISC2", str(ROOT / DISCS[region + {"1": "2", "2": "1"}[sc["disc"]]]))
    states = {r: out / f"{r}_m.xgs" for r in ("c", "d", "r1", "r2")}
    for f in states.values():
        if f.exists():
            f.unlink()
    steps = 60
    runs = {
        "c": {},
        "d": {},
        "r1": {"XG_REWIND": "1"},
        "r2": {"XG_REWIND": "1", "XG_REWIND_AT": f"{n}:{steps}"},
    }
    import concurrent.futures as cf

    with cf.ThreadPoolExecutor(3) as pool:
        futures = {r: pool.submit(run_port, build, out / r, disc, m + 120, {**env, **e, "XG_STATE_SAVE": f"{m}:{states[r]}"})
                   for r, e in runs.items()}
        codes = {r: f.result() for r, f in futures.items()}
    missing = [r for r, f in states.items() if not f.exists()]
    if missing:
        return [f"runs {missing} wrote no state (exits {codes})"], []
    problems = [f"{r.upper()} exited {code}" for r, code in codes.items() if code != 0]
    notes = []
    header, nondet = state_diffs(states["c"], states["d"], build)
    problems += [f"C/D: {p}" for p in header]
    for tag, offs in nondet.items():
        notes.append(f"two plain runs differ: {tag}, {len(offs)} bytes at +0x{min(offs):X} (masked)")
        nondet[tag] = {(o & ~7) + k for o in offs for k in range(8)}
    last = read_state(states["c"])[0]["vblank"]
    end = read_state(states["c"])[0]["dumpFrames"] * 4
    audio_c = (out / "c" / "audio.pcm").read_bytes()[:end]
    log = (out / "r2" / "port.log").read_text(errors="replace")
    went = re.search(r"rewind: from vblank (\d+) back to (\d+)", log)
    for r in ("r1", "r2"):
        hold = (int(went.group(2)), sc["hold"]) if r == "r2" and went and sc.get("hold") else None
        fn: list[str] = []
        problems += [f"{r.upper()}/C: {p}" for p in compare_frames(out / "c" / "frames", out / r / "frames", 0, last, hold, fn)]
        notes += [f"{r.upper()}/C: {p}" for p in fn]
        if (out / r / "audio.pcm").read_bytes()[:end] != audio_c:
            problems.append(f"{r.upper()}/C: audio differs")
        header, diffs = state_diffs(states["c"], states[r], build)
        # A capture parks the game: the park's frames leave other leftovers on
        # the game stack than a run that never parks, and a game frame made
        # later over them keeps one in a padding slot (Battling's battlingMain:
        # 8 bytes beside its stack guard). One or two such words, with the
        # frames, the sound and every other chunk equal, are noted, not failed.
        stack = {t: o - nondet.get(t, set()) for t, o in diffs.items() if t in ("mem.stack", "mem.battling.bss")}
        words = {t: {x & ~7 for x in o} for t, o in stack.items() if o}
        if not header and all(t in stack for t, o in diffs.items() if o - nondet.get(t, set())) and \
                sum(len(w) for w in words.values()) <= 2:
            for t, w in words.items():
                notes.append(f"{r.upper()}/C: {t}: stack word{'s' if len(w) > 1 else ''} at "
                             f"{', '.join(f'+0x{x:X}' for x in sorted(w))} (a leftover in a frame's padding)")
        else:
            problems += [f"{r.upper()}/C: {p}" for p in describe_diffs(header, diffs, nondet)]
    if went is None:
        problems.append("R2 did not rewind")
    else:
        notes.append(f"R2 went back from vblank {went.group(1)} to {went.group(2)}, then on to M={last}")
    return problems, notes


def cmd_rewind(args) -> int:
    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    names = args.scene or ["field", "battle", "movie", "battling"]
    failed = 0
    extra = dict(e.split("=", 1) for e in (args.env or []))
    for name in names:
        problems, notes = rewind_test(build, out / name, name, args.jp, extra)
        print(f"{name:10} {'ok' if not problems else 'FAIL'}", flush=True)
        for p in problems[:12]:
            print(f"           {p}")
        for p in notes:
            print(f"           ({p})")
        failed += bool(problems)
    print(f"rewindcheck: {len(names)} scene{'s' if len(names) != 1 else ''}, {len(names) - failed} ok"
          + (f", {failed} failed" if failed else ""))
    return 1 if failed else 0


# ---- refusals ------------------------------------------------------------------------------


def cmd_refusals(args) -> int:
    """States the build must refuse, each with a message, the game going on: one
    from another build, a JP state in the US build (and back), one on a disc
    whose image is not set, a damaged one, a cut one, a missing one."""
    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    region = "jp" if args.jp else "us"
    other = "us" if args.jp else "jp"
    env = {"XG_FIELD": "8", "XG_PARTY": "0", "XG_PAD_SCRIPT": str(out / "pad.txt")}
    (out / "pad.txt").write_text("0 0000\n")
    good = out / "good.xgs"
    if not good.exists():
        run_port(build, out / "make", region + "1", 420, {**env, "XG_STATE_SAVE": f"400:{good}"})
    if not good.exists():
        print("refusals: no state to start from")
        return 1
    data = bytearray(good.read_bytes())
    cases = {}
    b = bytearray(data)
    b[16] ^= 0xFF  # the build id
    cases["build"] = (bytes(b), "saved by another build")
    b = bytearray(data)
    b[220] ^= 1  # the engine (system/engine.h): the other one's state
    cases["engine"] = (bytes(b), "saved by the (Original|Enhanced) engine")
    b = bytearray(data)
    b[32:34] = other.upper().encode()
    cases["region"] = (bytes(b), f"a {other.upper()} state")
    b = bytearray(data)
    b[36] = 2  # on disc 2, which this run has no image for
    cases["disc"] = (bytes(b), "on Disc 2")
    b = bytearray(data)
    b[len(b) // 2] ^= 0x5A
    cases["damaged"] = (bytes(b), "damaged|does not fit")
    cases["cut"] = (bytes(data[:len(data) // 3]), "cut short|damaged")
    cases["missing"] = (None, "no such file")
    failed = 0
    for name, (content, expect) in cases.items():
        f = out / f"{name}.xgs"
        if content is None:
            f.unlink(missing_ok=True)
        else:
            f.write_bytes(content)
        code = run_port(build, out / name, region + "1", 700,
                        {**env, "XG_STATE_LOAD": str(f), "XG_STATE_SAVE": f"600:{out / name}.after.xgs",
                         "XG_DISC2": "none"})
        log = (out / name / "port.log").read_text(errors="replace")
        said = re.search(r"state: Cannot load[^\n]*", log)
        ok = code == 0 and said is not None and re.search(expect, said.group(0)) is not None and \
            (out / f"{name}.after.xgs").exists()
        failed += not ok
        print(f"{name:8} {'ok' if ok else 'FAIL'}  {said.group(0) if said else '(no refusal in the log)'}")
    print(f"refusals: {len(cases)} cases, {len(cases) - failed} refused with a message and the game went on")
    return 1 if failed else 0


# ---- the inventory -------------------------------------------------------------------------

def cmd_inventory(args) -> int:
    build = Path(args.build).resolve()
    inv = ROOT / "port/src/system/savestate_inventory.txt"
    known: dict[tuple[str, str], str] = {}
    bad = []
    for n, line in enumerate(inv.read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 3)
        if len(parts) < 3 or parts[2] not in ("state", "derived", "host"):
            bad.append(f"{inv.name}:{n}: expected '<file> <static|*> <state|derived|host> [why]'")
            continue
        known[(parts[0], parts[1])] = parts[2]
    objdir = build / "CMakeFiles/platform.dir/src"
    if not objdir.is_dir():
        print(f"inventory: no platform objects in {build}")
        return 1
    found = []
    for obj in sorted(objdir.rglob("*.o")):
        rel = str(obj.relative_to(objdir))[:-2]  # gpu/gp0.c
        text = subprocess.run(["objdump", "-t", str(obj)], capture_output=True, text=True).stdout
        for line in text.splitlines():
            f = line.split()
            # (.data.rel.ro: constants with pointers in position-independent code, read-only once relocated)
            if len(f) >= 6 and f[2] == "O" and (f[3].startswith(".bss") or f[3].startswith(".data")) and \
                    not f[3].startswith(".data.rel.ro"):
                found.append((rel, re.sub(r"\.\d+$", "", f[-1])))
    found = sorted(set(found))
    files = {o for o, _ in found}
    missing = [(o, s) for o, s in found if (o, s) not in known and (o, "*") not in known]
    stale = [k for k in known if k[0] in files and k[1] != "*" and k not in set(found)]
    for b in bad:
        print(b)
    for o, s in missing:
        print(f"unclassified: {o} {s} (add it to port/src/system/savestate_inventory.txt; if the game can see it, "
              f"save it in the file's xxxStateIo)")
    for o, s in stale:
        print(f"note: not in this build: {o} {s} (the other version's, or gone: then remove it)")
    states = sum(1 for o, s in found if known.get((o, s), known.get((o, "*"))) == "state")
    print(f"statevars: {len(found)} statics in {len(files)} files ({states} saved), "
          f"{len(missing)} unclassified" + (f", {len(stale)} entries not in this build" if stale else ""))
    return 1 if missing or bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("file")
    p.set_defaults(fn=cmd_info)
    p = sub.add_parser("chunks")
    p.add_argument("file")
    p.set_defaults(fn=cmd_chunks)
    p = sub.add_parser("diff")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--all", action="store_true")
    p.add_argument("--build", help="the build that wrote them (Battling's stacks)")
    p.add_argument("--raw", action="store_true", help="dead stack bytes too")
    p.set_defaults(fn=cmd_diff)
    p = sub.add_parser("run")
    p.add_argument("build")
    p.add_argument("out")
    p.add_argument("--scene", action="append", choices=list(SCENES))
    p.add_argument("--jp", action="store_true")
    p.add_argument("--env", action="append", metavar="KEY=VALUE", help="for every run (XG_ASPECT=16:9, XG_UPSCALE=2)")
    p.set_defaults(fn=cmd_run)
    p = sub.add_parser("rewind")
    p.add_argument("build")
    p.add_argument("out")
    p.add_argument("--scene", action="append", choices=list(SCENES))
    p.add_argument("--jp", action="store_true")
    p.add_argument("--env", action="append", metavar="KEY=VALUE")
    p.set_defaults(fn=cmd_rewind)
    p = sub.add_parser("refusals")
    p.add_argument("build")
    p.add_argument("out")
    p.add_argument("--jp", action="store_true")
    p.set_defaults(fn=cmd_refusals)
    p = sub.add_parser("inventory")
    p.add_argument("build")
    p.set_defaults(fn=cmd_inventory)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
