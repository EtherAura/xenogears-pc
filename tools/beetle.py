#!/usr/bin/env python3
"""Beetle PSX validation runs from power-on (the PC port's accuracy reference).

The Beetle PSX core (mednafen_psx_libretro.so) runs as a black box in this
process through tools/beetle_front.c, our own libretro frontend: only the
public libretro API is used (content, options, video, audio, input, the
exported main RAM, retro_serialize). The frontend is built on demand into
~/.cache/xg-beetle with the host cc (or `python tools/beetle.py build`,
`tools/setup.sh --beetle`). The core comes from --core / $XG_BEETLE_CORE
(default work/clean-room/bin/mednafen_psx_libretro.so); the BIOS images stay
in the user's directory (--sysdir / $XG_BEETLE_SYSDIR, default RetroArch's
system folder; the core picks scph5501.bin for US, scph5500.bin for JP) and
are never copied.

    python tools/beetle.py capture OUT PADFILE STOP [--ver us|jp] [--disc 1|2]
        [--every 30] [--from 0] [--audio] [--states 6000,9000] [--load-state F]
        [--ram 9000,...] [--vram 9000,...] [--log 0x800CCC5C,...] [--vsync-log]
        [--hashes] [--start XG_FIELD=4 --start XG_PARTY=0,1,255 ...] [--no-hooks]
        [--card1 F] [--card2 F] [--cards-out DIR] [--hw] [--texture-dump] [--texture-replace DIR]
    python tools/beetle.py compare OUT PCFRAMES [--offset N] [--from V --to V]
        [--offset-search W] [--pictures DIR] [--json FILE] [--race-cases FILE]
    python tools/beetle.py logdiff OUT/vsyncs.txt PC_SLOWDOWN_LOG
    python tools/beetle.py options          # the core options chosen, and why
    python tools/beetle.py build            # (re)build beetle_front.so

PADFILE has the port's XG_PAD_SCRIPT format, "<vsync> <hex buttons>" lines
(0x0800 Start, 0x0040 Cross, 0x1000 Up...; '#' comments), with vsyncs from
power-on, BIOS intro included (Beetle's US game starts 1014 vblanks in).
Port 1 is a digital PlayStation Controller, port 2 is empty, both card slots
hold fresh formatted cards (as the port's fresh XG_CARD_DIR), or --card1/--card2
images (in both instances: cards are files, not in retro_serialize; --cards-out
copies the reference's after the run). Core options:
`options` lists the most hardware-like settings chosen (software renderer at
1x, native dither, no PGXP or line hack, no overclock, no frame duping, 2x
CD speed, real BIOS, no overscan crop); each capture's options.log has every
key the core declared and asked for.

--hw runs the Beetle PSX HW core (--core, $XG_BEETLE_HW_CORE, else RetroArch's
mednafen_psx_hw_libretro.so) with its Vulkan renderer, in a headless context
beetle_front.c makes (the GPU by $XG_BEETLE_VK_DEVICE, a part of its name,
default Intel); pictures are read back as with the software core.
--texture-dump turns the core's texture tracking and dumping on (the dump in
OUT/textures: '<upload>-<palette>.png', see tools/beetle_pack.py and
docs/BEETLE_PACKS.md), --texture-replace DIR its replacement from DIR (a
Beetle pack folder); the disc image is staged in OUT/content (links, a copy of
the cue sheet), as the core keeps those folders beside the content. The core
loads replacements in the background: the first pictures of a texture can be
native. At beetle_psx_hw_internal_resolution 1x(native) it samples a 2x
replacement at its top-left pixels: compare at 2x.

Outputs in OUT (vsync = vblanks from power-on; retro_run N starts with vblank
interrupt N: the game's work after it, and the field scanned after it, are N's):

    v<vsync>.png    Beetle's picture of that vblank every --every vsyncs from
                    --from: the field after it (the port's frame_<vsync + 1>);
                    the whole TV frame, overscan not cropped: `compare` finds
                    the display area in it
    audio.pcm       --audio: 44100 Hz stereo s16le from power-on (audio.txt
                    says where each vsync starts)
    s<vsync>.state  --states: retro_serialize states (--load-state F
                    --load-frame V continues from one; pad input still works)
    ram<vsync>.bin  --ram: 2 MB main RAM after that vblank
    vram<vsync>.bin --vram: 1024x512 VRAM (+ .png), read back through the
                    console's own GPU (GP0 C0h) at that vblank's interrupt,
                    in the second instance (below), so the run is unchanged
    ramlog.txt      --log: "<vsync> <u32> ..." for the listed RAM words, every
                    vsync from --from
    hashes.txt      --hashes: per vsync, hashes of the picture, the audio and
                    main RAM: two runs compare without pictures
    vsyncs.txt      --vsync-log: the slowdown replay's timing log
                    (port/src/system/slowdown.c has the format), as
                    tools/redux_capture.py --vsync-log writes it from Redux
    run.txt         what happened: options, start options, counts

Start options (--start, same semantics as the port's and tools/start_state.py:
XG_BOOT_MODE, XG_FIELD, XG_PRESET, XG_PARTY, XG_GAMESTATE) go through main
RAM: kernelMain's setGameMode(6) becomes setGameMode(n) once the BIOS has
loaded that word but not the executable's last sector (so before it runs),
and the gameState pokes and the debug font pointer reset happen at the first
vblank while bootGame loads that mode (bootMode != 0).

The VSync log. libretro has no breakpoints, so a MIPS stub records the events
tools/redux_capture.py takes with breakpoints: VSync, CD_cw and VSyncCallback
at their entry, bootGame's `jalr v0` to a mode's entry, LIBCD's `callback` at
its ready-callback `jalr v0` and just past its sync-bit test, and
trapIntrVSync (each vblank interrupt, for the time base). The stub lives at
0x8000C000-0x8000DF7F, kernel RAM that psx-spx lists as unused
(0000Cxxxh-0000DF7Fh) and that no run here ever wrote; each site's first two
instructions become a jump to its piece, which runs them and jumps back,
using only registers dead there (t0-t9, v1: caller-saved at a call or a
function entry). The pieces append (kind, argument, vblank, root counter 1
hblanks since that vblank) records to a 128-entry ring with interrupts
masked; a VSync call that starts a run is a record and the run's later calls
of -1 or 1 count in it, and the runs join across frames in Python (merged as
tools/redux_capture.py does). The fraction of a frame is hblanks since the
vblank over the steady period between vblanks (263), clamped to .999 as
Redux's is. The game can hold the vblank interrupt off (a mode start: 243
hblanks at the US attract's): such a late trap is found from the steady
spacing, and the records after it are timed from where the vblank was.

The hooks never run in the reference. Code in the game costs CPU cycles, and
a run with the stub always in place drifts from the plain one (one-frame
shifts from vsync 1624, the gear battle different from 20345). So two
instances of the core run side by side (the second loaded with dlmopen):
instance A is the reference: it runs every frame clean and only ever saves
its state. Before each frame, instance B loads that state, gets the stub and
the jumps, and runs the same frame: its records are that frame's events;
then A runs it clean, giving the pictures, audio and RAM. (One instance
saving, running hooked and loading back is not exact: some timing state is
outside retro_serialize.) Proof: a --vsync-log run and a --no-hooks run of
the New Game pad give equal picture, audio and RAM hashes at every one of
26200 vsyncs. B runs from A's exact state, so its events are A's but for the
hooks' own cycles: B is a little behind, and what A did in the last cycles of
a frame B has not done when its frame ends (a VSync(2) at .99 in the worldmap
walk, the battle's VSync(0) at .999, the JP title's ReadN; also a record half
written when the frame ended). So B runs on into the next frame, and its
events there that the next copy (from A's state at the frame's end) does not
make are spliced back into the frame, late in it (Capture.splice says how
they are told from B's own path; '# vsync N: ... spliced' comments mark
them). Waiting calls are checked against the reference's own RAM: LIBETC's
vsyncLastVcount equals vblankCounter after a frame whose vblank ended a
VSync(0)/VSync(n), so a wait the copies' records lack is spliced however late
the copy made it. The log marks the rare frames whose copy showed another
picture. The log's "p" lines are vblanks the BIOS pad/card handler did not
serve (BIOS_TOGGLE did not flip: a long interrupt handler took the vblank).
A's state holds the CPU's code cache too: a hook site's line cached with
its original instructions (VSync's entry in a busy-wait loop at the frame's
end) would run unhooked in B until evicted, so B's first vblank interrupt
invalidates the sites' lines (psx-spx's cache initialisation, sites only);
before that, the JP field lost every other frame's first VSync calls and
the splice put them at the frame before's .999.

A slowdown replay of the port against Beetle:

    python tools/beetle.py capture ref pad.txt 27000 --vsync-log --every 1
    XG_SLOWDOWN=ref/vsyncs.txt XG_SLOWDOWN_PAD=pad.txt XG_OVERLAY_CD=1 \
        XG_CLOCK=step XG_HEADLESS=1 XG_DUMP_FRAMES=pc XG_SLOWDOWN_LOG=pc.log \
        XG_PAD_SCRIPT=empty.pad XG_CARD_DIR=fresh XG_QUIT_AT=N \
        build/pc/<dir>/xenogears
    python tools/beetle.py compare ref pc       # pictures, at the port's offset
    python tools/beetle.py logdiff ref/vsyncs.txt pc.log   # where timing departs

XG_SLOWDOWN_PAD reads the Beetle pad on the log's vblanks while the replay is
lined up (from the log's first mode start). XG_OVERLAY_CD=1 makes the port
read the first mode's overlay as the PS1 does: that read comes before the
log's first mode start, and without it the drive's head is elsewhere. compare
counts pictures equal at the offset, equal at a shift of a few vblanks
(timing), and different (content); the default offset is the port log's
"log vblank N = vblank M here" less one (Beetle's v<N> is frame_<N+1> there);
--offset-search finds the offset of a segment after the replay moved it. Big
outputs and TMPDIR belong in ~/.cache/xg-beetle; retro_serialize states hold the BIOS's RAM copy of the
kernel, so keep them out of the synced tree.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import start_state  # noqa: E402

CACHE = Path(os.environ.get("XG_BEETLE_CACHE", Path.home() / ".cache" / "xg-beetle"))
FRONT_SRC = HERE / "beetle_front.c"
DEFAULT_CORE = ROOT / "work/clean-room/bin/mednafen_psx_libretro.so"
# --hw: the Beetle PSX HW core (its Vulkan renderer: texture dumping and replacement)
DEFAULT_HW_CORE = Path.home() / ".config/retroarch/cores/mednafen_psx_hw_libretro.so"
DEFAULT_SYSDIR = "/mnt/Games/Arcade/Frontends/RetroArch/system"
DISC_NAMES = {"us": "USA", "jp": "Japan"}

# The most hardware-like settings the core offers (python tools/beetle.py
# options prints them with the reasons; run.txt and options.log in a capture
# list every key the core declared and asked for).
CORE_OPTIONS = {
    "beetle_psx_cpu_freq_scale": ("100%", "no CPU overclock"),
    "beetle_psx_gpu_overclock": ("1x(native)", "GPU rasterizer at console speed"),
    "beetle_psx_gte_overclock": ("disabled", "GTE at console timing"),
    "beetle_psx_cd_fastload": ("2x(native)", "CD at the drive's 2x speed"),
    "beetle_psx_cd_access_method": ("sync", "host-side image reads only (no emulated timing effect)"),
    "beetle_psx_skip_bios": ("disabled", "the BIOS boots the disc as on a console"),
    "beetle_psx_override_bios": ("disabled", "the region's own BIOS (scph5501 US, scph5500 JP)"),
    "beetle_psx_internal_resolution": ("1x(native)", "native 1024x512 VRAM rendering"),
    "beetle_psx_dither_mode": ("1x(native)", "the GPU's own dithering"),
    "beetle_psx_pgxp_mode": ("disabled", "no PGXP: GTE results as the console's"),
    "beetle_psx_pgxp_2d_tol": ("disabled", "no PGXP"),
    "beetle_psx_pgxp_nclip": ("disabled", "no PGXP culling"),
    "beetle_psx_line_render": ("disabled", "lines drawn as lines (no line-to-quad hack)"),
    "beetle_psx_widescreen_hack": ("disabled", "no widescreen hack"),
    "beetle_psx_frame_duping": ("disabled", "every frame delivered"),
    "beetle_psx_display_internal_fps": ("disabled", "nothing drawn over the picture"),
    "beetle_psx_display_osd": ("disabled", "no on-screen messages"),
    "beetle_psx_analog_calibration": ("disabled", "digital pad"),
    "beetle_psx_analog_toggle": ("disabled", "digital pad"),
    "beetle_psx_enable_multitap_port1": ("disabled", "no multitap"),
    "beetle_psx_enable_multitap_port2": ("disabled", "no multitap"),
    "beetle_psx_use_mednafen_memcard0_method": ("mednafen", "both cards as files in a fresh save directory"),
    "beetle_psx_enable_memcard1": ("enabled", "a card in slot 2 too, as on the PC"),
    "beetle_psx_shared_memory_cards": ("disabled", "per-run cards"),
    "beetle_psx_crop_overscan": ("disabled", "the whole picture; compare finds the display area"),
    "beetle_psx_image_crop": ("disabled", "no extra cropping"),
    "beetle_psx_image_offset": ("disabled", "no shift"),
    "beetle_psx_initial_scanline": ("0", "every NTSC line"),
    "beetle_psx_last_scanline": ("239", "every NTSC line"),
    "beetle_psx_initial_scanline_pal": ("0", "every PAL line"),
    "beetle_psx_last_scanline_pal": ("287", "every PAL line"),
    "beetle_psx_deinterlacer": ("weave", "interlaced pictures as both fields (the BIOS intro)"),
    "beetle_psx_pal_video_timing_override": ("disabled", "the disc's own video timing"),
    "beetle_psx_core_timing_fps": ("auto_toggle", "reported fps only"),
}

# The port's pad bits (XG_PAD_SCRIPT, active high) -> RetroPad ids (B = Cross,
# A = Circle, Y = Square, X = Triangle: the PlayStation layout by position).
PAD_TO_RETRO = [(0x0001, 12), (0x0002, 13), (0x0004, 10), (0x0008, 11), (0x0010, 9), (0x0020, 8), (0x0040, 0),
                (0x0080, 1), (0x0100, 2), (0x0200, 14), (0x0400, 15), (0x0800, 3), (0x1000, 4), (0x2000, 7),
                (0x4000, 5), (0x8000, 6)]
DEVICE_NONE, DEVICE_JOYPAD = 0, 1
# A kernel RAM word that the BIOS pad/card vblank handler flips between 0 and 1 each time it runs
# (found by RAM diff, US scph5501 and JP scph5500 alike; Capture.bios_vblank)
BIOS_TOGGLE = 0x80007264
MEMORY_SYSTEM_RAM = 2


def retro_mask(buttons: int) -> int:
    m = 0
    for bit, rid in PAD_TO_RETRO:
        if buttons & bit:
            m |= 1 << rid
    return m


def build_frontend(force: bool = False) -> Path:
    """beetle_front.so in the cache, rebuilt when the source is newer."""
    CACHE.mkdir(parents=True, exist_ok=True)
    so = CACHE / "beetle_front.so"
    if force or not so.exists() or so.stat().st_mtime < FRONT_SRC.stat().st_mtime:
        cc = os.environ.get("CC", "cc")
        subprocess.run([cc, "-O2", "-Wall", "-shared", "-fPIC", "-o", str(so), str(FRONT_SRC), "-ldl", "-lz"],
                       check=True)
    return so


def disc_image(ver: str, disc: int) -> Path:
    name = f"Xenogears ({DISC_NAMES[ver]}) (Disc {disc})"
    return ROOT / "bin" / name / f"{name}.cue"


def read_symbols(ver: str) -> dict[str, int]:
    out = {}
    for line in (ROOT / "config" / ver / "symbols" / "main.txt").read_text().splitlines():
        m = re.match(r"\s*([A-Za-z_.$][\w.$]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def sdk_code_range(ver: str) -> tuple[int, int]:
    """main's PsyQ SDK code (config/<ver>/main.yaml), as tools/redux_capture.py: VSync calls
    from it (DrawSync's and LIBCD's timeouts) are not the game's."""
    text = (ROOT / "config" / ver / "main.yaml").read_text()
    start = int(re.search(r"^\s+start:\s*(0x[0-9A-Fa-f]+)", text, re.M).group(1), 16)
    vram = int(re.search(r"^\s+vram:\s*(0x[0-9A-Fa-f]+)", text, re.M).group(1), 16)
    lo = min(int(m, 16) for m in re.findall(r"^\s+- \[(0x[0-9A-Fa-f]+), (?:c|asm), psyq/", text, re.M))
    hi = min(int(m, 16) for m in re.findall(r"^\s+- \[(0x[0-9A-Fa-f]+), \.?data", text, re.M) if int(m, 16) > lo)
    return vram + lo - start, vram + hi - start


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    import zlib
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def vram_png(vram: bytes, path: Path) -> None:
    c5 = [(v << 3) | (v >> 2) for v in range(32)]
    lut = [bytes((c5[p & 31], c5[(p >> 5) & 31], c5[(p >> 10) & 31])) for p in range(32768)]
    words = struct.unpack(f"<{1024 * 512}H", vram)
    write_png(path, 1024, 512, b"".join(lut[p & 0x7FFF] for p in words))


def read_pad(path: Path) -> list[tuple[int, int]]:
    """XG_PAD_SCRIPT lines -> sorted (vsync, buttons); hotkey lines are the PC's own."""
    events = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        v, b = line.split(None, 1)
        if b.startswith("key"):
            continue
        events.append((int(v), int(b, 16)))
    return sorted(events, key=lambda e: e[0])


# ------------------------------------------------------------- the core --

_LIB = None


def _frontend() -> ctypes.CDLL:
    global _LIB
    if _LIB is None:
        L = ctypes.CDLL(str(build_frontend()))
        L.bf_memory.restype = ctypes.c_void_p
        L.bf_memory_size.restype = ctypes.c_size_t
        L.bf_frame.restype = ctypes.c_void_p
        L.bf_audio_frames.restype = ctypes.c_uint64
        L.bf_touch_map.restype = ctypes.c_void_p
        L.bf_frame_hash.restype = ctypes.c_uint64
        L.bf_audio_hash.restype = ctypes.c_uint64
        L.bf_ram_hash.restype = ctypes.c_uint64
        L.bf_user_ram_hash.restype = ctypes.c_uint64
        L.bf_hash_exclude.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
        L.bf_audio_mute.argtypes = [ctypes.c_int]
        L.bf_write_png.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        _LIB = L
    return _LIB


class _Selected:
    """beetle_front's functions, each acting on one instance."""

    def __init__(self, lib: ctypes.CDLL, index: int):
        self._lib, self._index = lib, index

    def __getattr__(self, name):
        fn = getattr(self._lib, name)
        select, index = self._lib.bf_select, self._index

        def call(*a):
            select(index)
            return fn(*a)
        return call


class Beetle:
    """One Beetle PSX core in this process (beetle_front.so through ctypes);
    index 1 is a second, independent instance (dlmopen)."""

    def __init__(self, content: Path, core: Path | None = None, sysdir: str | None = None,
                 savedir: Path | None = None, options: dict[str, str] | None = None, verbose: bool = False,
                 option_log: Path | None = None, index: int = 0, hw: bool = False, hw_frames: bool = True):
        L = self.lib = _Selected(_frontend(), index)
        opts = {k: v for k, (v, _) in CORE_OPTIONS.items()}
        opts.update(options or {})
        for k, v in opts.items():
            L.bf_option(k.encode(), v.encode())
            if k.startswith("beetle_psx_") and not k.startswith("beetle_psx_hw_"):
                # the Beetle PSX HW core's keys: the same options under beetle_psx_hw_
                hk = "beetle_psx_hw_" + k[len("beetle_psx_"):]
                if hk not in opts:
                    L.bf_option(hk.encode(), v.encode())
        # hw: a Vulkan context for a core that asks for one (the HW core's Vulkan
        # renderer: its texture dumping and replacement); hw_frames reads each frame back
        if hw and not L.bf_hw_enable(1, 1 if hw_frames else 0):
            raise SystemExit("beetle: beetle_front.so was built without <vulkan/vulkan.h>")
        self.core = Path(core or os.environ.get("XG_BEETLE_CORE") or DEFAULT_CORE)
        sysdir = sysdir or os.environ.get("XG_BEETLE_SYSDIR") or DEFAULT_SYSDIR
        savedir = Path(savedir or CACHE / "save")
        savedir.mkdir(parents=True, exist_ok=True)
        if not self.core.exists():
            raise SystemExit(f"beetle: no core at {self.core} (--core or XG_BEETLE_CORE)")
        if not L.bf_open(str(self.core).encode(), str(sysdir).encode(), str(savedir).encode(),
                         str(content).encode(), 1 if verbose else 0, str(option_log or "").encode()):
            raise SystemExit(f"beetle: the core did not load {content}")
        L.bf_port_device(0, DEVICE_JOYPAD)
        L.bf_port_device(1, DEVICE_NONE)
        size = L.bf_memory_size(MEMORY_SYSTEM_RAM)
        self.ram = (ctypes.c_uint8 * size).from_address(L.bf_memory(MEMORY_SYSTEM_RAM))
        self.dims = (ctypes.c_long * 6)()
        self.frame = 0  # retro_run calls so far: the vblanks since power-on

    def run(self, n: int = 1) -> None:
        self.lib.bf_run(n)
        self.frame += n

    def u32(self, addr: int) -> int:
        return struct.unpack_from("<I", self.ram, addr & 0x1FFFFF)[0]

    def put32(self, addr: int, value: int) -> None:
        struct.pack_into("<I", self.ram, addr & 0x1FFFFF, value & 0xFFFFFFFF)

    def read(self, addr: int, size: int) -> bytes:
        a = addr & 0x1FFFFF
        return bytes(self.ram[a:a + size])

    def write(self, addr: int, data: bytes) -> None:
        a = addr & 0x1FFFFF
        self.ram[a:a + len(data)] = data

    def pad(self, port: int, buttons: int) -> None:
        self.lib.bf_buttons(port, retro_mask(buttons))

    def frame_info(self) -> dict:
        self.lib.bf_frame(self.dims)
        d = self.dims
        return {"w": d[0], "h": d[1], "format": d[2], "dupe": d[3], "frames": d[4], "dupes": d[5]}

    def frame_png(self, path: Path) -> bool:
        return bool(self.lib.bf_frame_png(str(path).encode(), 0, 0, 0, 0))

    def save_state(self, path: Path) -> None:
        if not self.lib.bf_state_save(str(path).encode()):
            raise RuntimeError("retro_serialize failed")

    def load_state(self, path: Path) -> None:
        if not self.lib.bf_state_load(str(path).encode()):
            raise RuntimeError(f"retro_unserialize failed for {path}")

    def audio_file(self, path: Path | None) -> None:
        self.lib.bf_audio_file(str(path or "").encode())

    def audio_frames(self) -> int:
        return self.lib.bf_audio_frames()

    def close(self) -> None:
        self.lib.bf_close()


# ------------------------------------------------------ a MIPS assembler --

REG = {n: i for i, n in enumerate("zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 "
                                  "s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split())}


class Asm:
    """Just enough R3000 assembly for the stub: two passes over labels; a nop
    goes after a load whose result the next instruction reads (load delay)
    and in every branch delay slot unless one is given."""

    def __init__(self, base: int):
        self.base = base
        self.items: list = []  # (kind, ...) in order
        self.labels: dict[str, int] = {}

    # encoding helpers
    @staticmethod
    def _r(rs=0, rt=0, rd=0, sa=0, fn=0):
        return (REG[rs] if isinstance(rs, str) else rs) << 21 | (REG[rt] if isinstance(rt, str) else rt) << 16 | \
            (REG[rd] if isinstance(rd, str) else rd) << 11 | sa << 6 | fn

    @staticmethod
    def _i(op, rs, rt, imm):
        return op << 26 | REG[rs] << 21 | REG[rt] << 16 | (imm & 0xFFFF)

    def _emit(self, word, reads=(), writes=None, load=False):
        self.items.append(("w", word, tuple(reads), writes, load))

    def word(self, w):  # raw word (displaced instructions)
        self.items.append(("w", w, (), None, False))

    def label(self, name):
        self.items.append(("l", name))

    def nop(self):
        self._emit(0)

    # ALU
    def addu(self, d, s, t): self._emit(self._r(s, t, d, 0, 0x21), (s, t), d)
    def subu(self, d, s, t): self._emit(self._r(s, t, d, 0, 0x23), (s, t), d)
    def and_(self, d, s, t): self._emit(self._r(s, t, d, 0, 0x24), (s, t), d)
    def or_(self, d, s, t): self._emit(self._r(s, t, d, 0, 0x25), (s, t), d)
    def sltu(self, d, s, t): self._emit(self._r(s, t, d, 0, 0x2B), (s, t), d)
    def sll(self, d, t, sa): self._emit(self._r(0, t, d, sa, 0x00), (t,), d)
    def srl(self, d, t, sa): self._emit(self._r(0, t, d, sa, 0x02), (t,), d)
    def addiu(self, t, s, imm): self._emit(self._i(9, s, t, imm), (s,), t)
    def sltiu(self, t, s, imm): self._emit(self._i(11, s, t, imm), (s,), t)
    def andi(self, t, s, imm): self._emit(self._i(12, s, t, imm), (s,), t)
    def ori(self, t, s, imm): self._emit(self._i(13, s, t, imm), (s,), t)
    def lui(self, t, imm): self._emit(self._i(15, "zero", t, imm), (), t)

    def li(self, t, value):
        value &= 0xFFFFFFFF
        if value < 0x8000:
            self.addiu(t, "zero", value)
        elif value >= 0xFFFF8000:
            self.addiu(t, "zero", value - 0x100000000)
        else:
            self.lui(t, value >> 16)
            if value & 0xFFFF:
                self.ori(t, t, value & 0xFFFF)

    # memory
    def lw(self, t, off, s): self._emit(self._i(0x23, s, t, off), (s,), t, True)
    def lbu(self, t, off, s): self._emit(self._i(0x24, s, t, off), (s,), t, True)
    def sw(self, t, off, s): self._emit(self._i(0x2B, s, t, off), (s, t), None)

    # COP0
    def mfc0(self, t, rd): self._emit(0x40000000 | REG[t] << 16 | rd << 11, (), t, True)
    def mtc0(self, t, rd): self._emit(0x40800000 | REG[t] << 16 | rd << 11, (t,), None)

    # control flow (delay slot filled with a nop unless `slot` is a callable that emits one)
    def _branch(self, kind, args, slot):
        self.items.append(("b", kind, args))
        if slot:
            slot()
        else:
            self.nop()

    def beq(self, s, t, lab, slot=None): self._branch("beq", (s, t, lab), slot)
    def bne(self, s, t, lab, slot=None): self._branch("bne", (s, t, lab), slot)
    def bltz(self, s, lab, slot=None): self._branch("bltz", (s, lab), slot)
    def b(self, lab, slot=None): self._branch("beq", ("zero", "zero", lab), slot)
    def j(self, addr, slot=None): self._branch("j", (addr,), slot)
    def jr(self, s, slot=None): self._branch("jr", (s,), slot)

    def la(self, t, lab, delta=0):  # t = a label's address + delta (lui + ori)
        self.items.append(("la", t, lab, delta))

    def assemble(self) -> bytes:
        # pass 1: insert load-delay nops, lay out addresses
        out = []
        prev_load = None
        for it in self.items:
            if it[0] == "w":
                _, word, reads, writes, load = it
                if prev_load and prev_load in reads:
                    out.append(("w", 0))
                out.append(("w", word))
                prev_load = writes if load else None
            elif it[0] == "b":
                kind, args = it[1], it[2]
                regs = [a for a in args if isinstance(a, str) and a in REG]
                if prev_load and prev_load in regs:
                    out.append(("w", 0))
                out.append(it)
                prev_load = None
            elif it[0] == "la":
                out.append(("la_hi",) + it[1:])
                out.append(("la_lo",) + it[1:])
                prev_load = None
            else:
                out.append(it)
        addr = self.base
        layout = []
        for it in out:
            if it[0] == "l":
                self.labels[it[1]] = addr
            else:
                layout.append((addr, it))
                addr += 4
        words = []
        for a, it in layout:
            if it[0] == "w":
                words.append(it[1])
                continue
            if it[0] in ("la_hi", "la_lo"):
                _, t, lab, delta = it
                value = (self.labels[lab] + delta) & 0xFFFFFFFF
                words.append(self._i(15, "zero", t, value >> 16) if it[0] == "la_hi"
                             else self._i(13, t, t, value & 0xFFFF))
                continue
            kind, args = it[1], it[2]
            if kind in ("beq", "bne"):
                s, t, lab = args
                off = (self.labels[lab] - (a + 4)) >> 2
                words.append((4 if kind == "beq" else 5) << 26 | REG[s] << 21 | REG[t] << 16 | (off & 0xFFFF))
            elif kind == "bltz":
                s, lab = args
                off = (self.labels[lab] - (a + 4)) >> 2
                words.append(1 << 26 | REG[s] << 21 | (off & 0xFFFF))
            elif kind == "j":
                target = args[0]
                assert (target & 0xF0000000) == (a & 0xF0000000)
                words.append(2 << 26 | (target >> 2) & 0x3FFFFFF)
            elif kind == "jr":
                words.append(REG[args[0]] << 21 | 8)
        return struct.pack(f"<{len(words)}I", *words)


def j_word(target: int) -> int:
    return 2 << 26 | (target >> 2) & 0x3FFFFFF


# ----------------------------------------------------- the VSync log stub --

STUB = 0x8000C000      # state block; psx-spx: 0000Cxxxh-0000DF7Fh unused by the kernel
STUB_END = 0x8000DF80  # 0000DF80h: games' BIOS patches
CODE = STUB + 0x100
RING = 0x800           # ring offset from STUB
RING_N = 128           # records of 32 bytes
MAGIC = 0x4B484758     # 'XGHK'
# state block offsets
S_MAGIC, S_WR, S_RD, S_OVER, S_VBC, S_T1, S_PERIOD = 0x00, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C
S_OPEN, S_ARG, S_OPENIDX = 0x20, 0x24, 0x28  # the copy's open VSync run: its argument, its ring entry
S_VRAM, S_ENABLE, S_SDKLO, S_SDKLEN = 0x38, 0x3C, 0x40, 0x44
S_FLUSH = 0x48  # the copy's first vblank interrupt invalidates the hook sites' code-cache lines
KINDS = {"v": 1, "m": 2, "r": 3, "s": 4, "k": 5, "b": 6}
KIND_NAMES = {k: n for n, k in KINDS.items()}
VRAM_BUF = 0x80100000  # 1 MB: only written while the run is about to be restored from a state

# Hook sites outside the symbol file (the build's main.elf; tools/redux_capture.py has the same):
ENTRY_CALL = {"us": 0x80019C04, "jp": 0x80019C88}  # bootGame's `jalr v0` to the mode entry
READY_CALL = {"us": 0x80042D18, "jp": 0x80042B90}  # callback's `jalr v0` to the CD ready callback
SYNC_SEEN = {"us": 0x80042D2C, "jp": 0x80042BA4}   # callback past its sync-bit test (status at 0($s3))
GAME_MODES = 0x8001808C


def hook_sites(ver: str) -> dict[str, int]:
    syms = read_symbols(ver)
    return {"vsync": syms["VSync"], "cdcw": syms["CD_cw"], "vscb": syms["VSyncCallback"],
            "trap": syms["trapIntrVSync"], "entry": ENTRY_CALL[ver], "ready": READY_CALL[ver],
            "sync": SYNC_SEEN[ver]}


def build_stub(sites: dict[str, int], orig: dict[str, tuple[int, int]]) -> tuple[bytes, dict[str, int]]:
    """The stub's code (at CODE) and the address of each site's piece."""
    a = Asm(CODE)

    def base():
        a.lui("t6", (STUB >> 16) + (1 if STUB & 0x8000 else 0))
        a.addiu("t6", "t6", (STUB & 0xFFFF) - (0x10000 if STUB & 0x8000 else 0))

    def mask():  # t5 = SR; interrupts off
        a.mfc0("t5", 12)
        a.addiu("t4", "zero", -2)
        a.and_("t4", "t5", "t4")
        a.mtc0("t4", 12)
        a.nop()
        a.nop()

    def unmask():
        a.mtc0("t5", 12)

    def now():  # t9 = vblanks counted, t7 = hblanks since the last one
        a.lui("t7", 0x1F80)
        a.lw("t7", 0x1110, "t7")
        a.lw("t8", S_T1, "t6")
        a.lw("t9", S_VBC, "t6")
        a.subu("t7", "t7", "t8")
        a.andi("t7", "t7", 0xFFFF)

    def append(w0, w1, w2, w3, w4, tag):  # uses t8, v1
        a.lw("t8", S_WR, "t6")
        a.lw("v1", S_RD, "t6")
        a.subu("v1", "t8", "v1")
        a.sltiu("v1", "v1", RING_N)
        a.bne("v1", "zero", tag + "_ok")
        a.lw("v1", S_OVER, "t6")
        a.addiu("v1", "v1", 1)
        a.sw("v1", S_OVER, "t6")
        a.b(tag + "_end")
        a.label(tag + "_ok")
        a.andi("v1", "t8", RING_N - 1)
        a.sll("v1", "v1", 5)
        a.addu("v1", "v1", "t6")
        a.sw(w0, RING + 0, "v1")
        a.sw(w1, RING + 4, "v1")
        a.sw(w2, RING + 8, "v1")
        a.sw(w3, RING + 12, "v1")
        a.sw(w4, RING + 16, "v1")
        a.addiu("t8", "t8", 1)
        a.sw("t8", S_WR, "t6")
        a.label(tag + "_end")

    def kind_word(kind):  # t0 = kind | hblanks << 16
        a.sll("t0", "t7", 16)
        a.ori("t0", "t0", KINDS[kind])

    def back_entry(name):  # the displaced first two instructions, then on
        a.word(orig[name][0])
        a.word(orig[name][1])
        a.j(sites[name] + 8)

    def back_call(name):  # a `jalr v0` site: call v0 returning past the delay slot
        a.li("ra", sites[name] + 8)
        a.jr("v0")

    def enabled_or(tag):
        a.lw("t0", S_ENABLE, "t6")
        a.beq("t0", "zero", tag)

    # VSync(a0): a call that starts a run is a record (count 1); later calls of a
    # run of -1 or 1 bump its count and last vblank in the ring in place. A copy's
    # runs start fresh: runs across frames (and spliced events) are joined in Python.
    # SDK callers (ra in the SDK code) are skipped.
    a.label("vsync")
    base()
    enabled_or("vsync_out")
    a.lw("t1", S_SDKLO, "t6")
    a.lw("t2", S_SDKLEN, "t6")
    a.subu("t1", "ra", "t1")
    a.sltu("t1", "t1", "t2")
    a.bne("t1", "zero", "vsync_out")
    mask()
    now()
    a.lw("t0", S_OPEN, "t6")
    a.beq("t0", "zero", "vsync_new")
    a.lw("t1", S_ARG, "t6")
    a.bne("t1", "a0", "vsync_new")
    a.bltz("a0", "vsync_merge")
    a.addiu("t2", "zero", 1)
    a.bne("a0", "t2", "vsync_new")
    a.label("vsync_merge")
    a.lw("t1", S_OPENIDX, "t6")
    a.andi("t1", "t1", RING_N - 1)
    a.sll("t1", "t1", 5)
    a.addu("t1", "t1", "t6")
    a.lw("t2", RING + 12, "t1")
    a.addiu("t2", "t2", 1)
    a.sw("t2", RING + 12, "t1")
    a.sw("t9", RING + 16, "t1")
    a.b("vsync_done")
    a.label("vsync_new")
    kind_word("v")
    a.addiu("t3", "zero", 1)
    append("t0", "a0", "t9", "t3", "t9", "vsync_a")
    a.lw("t1", S_WR, "t6")
    a.addiu("t1", "t1", -1)
    a.sw("t1", S_OPENIDX, "t6")
    a.addiu("t1", "zero", 1)
    a.sw("t1", S_OPEN, "t6")
    a.sw("a0", S_ARG, "t6")
    a.label("vsync_done")
    unmask()
    a.label("vsync_out")
    back_entry("vsync")

    # CD_cw(a0 = command, a1 = parameters): Setloc's three BCD bytes go along
    a.label("cdcw")
    base()
    enabled_or("cdcw_out")
    mask()
    now()
    a.andi("t1", "a0", 0xFF)
    a.addiu("t3", "zero", -1)
    a.addiu("t2", "zero", 2)
    a.bne("t1", "t2", "cdcw_rec")
    a.beq("a1", "zero", "cdcw_rec")
    a.lbu("t3", 0, "a1")
    a.lbu("t2", 1, "a1")
    a.sll("t2", "t2", 8)
    a.or_("t3", "t3", "t2")
    a.lbu("t2", 2, "a1")
    a.sll("t2", "t2", 16)
    a.or_("t3", "t3", "t2")
    a.label("cdcw_rec")
    kind_word("k")
    append("t0", "t1", "t9", "t3", "zero", "cdcw_a")
    unmask()
    a.label("cdcw_out")
    back_entry("cdcw")

    # VSyncCallback(a0): the boot's (logged whatever the enable flag)
    a.label("vscb")
    base()
    mask()
    now()
    kind_word("b")
    append("t0", "a0", "t9", "zero", "zero", "vscb_a")
    unmask()
    back_entry("vscb")

    # bootGame starting a mode: v0 = its entry point
    a.label("entry")
    base()
    enabled_or("entry_out")
    mask()
    now()
    a.sw("zero", S_OPEN, "t6")  # a mode start ends the run (Python closes it)
    kind_word("m")
    append("t0", "v0", "t9", "zero", "zero", "entry_a")
    unmask()
    a.label("entry_out")
    back_call("entry")

    # the CD interrupt calling the ready callback: a0 = status
    a.label("ready")
    base()
    enabled_or("ready_out")
    mask()
    now()
    kind_word("r")
    append("t0", "a0", "t9", "zero", "zero", "ready_a")
    unmask()
    a.label("ready_out")
    back_call("ready")

    # the CD interrupt completing a command: status byte at 0($s3)
    a.label("sync")
    base()
    enabled_or("sync_out")
    mask()
    now()
    a.lbu("t1", 0, "s3")
    kind_word("s")
    append("t0", "t1", "t9", "zero", "zero", "sync_a")
    unmask()
    a.label("sync_out")
    back_entry("sync")

    # the vblank interrupt (LIBETC's handler): count it, keep root counter 1;
    # a VRAM read-back request is served here
    a.label("trap")
    base()
    a.lui("t7", 0x1F80)
    a.lw("t7", 0x1110, "t7")
    a.lw("t8", S_T1, "t6")
    a.lw("t9", S_VBC, "t6")
    a.subu("v1", "t7", "t8")
    a.andi("v1", "v1", 0xFFFF)
    a.sw("v1", S_PERIOD, "t6")
    a.sw("t7", S_T1, "t6")
    a.addiu("t9", "t9", 1)
    a.sw("t9", S_VBC, "t6")
    # A copy runs from the reference's state, its code cache included: a line that holds a
    # hook site's original instructions (VSync's entry in a busy-wait loop at the frame's
    # end) would run unhooked until evicted, and those calls would be missing from the copy
    # (JP field: every other frame's first calls). Its first vblank interrupt invalidates
    # the sites' lines, as psx-spx describes the kernel's cache initialisation:
    # [FFFE0130h] = 804h (tag test) and COP0 SR.IsC, then a store of zero to an address
    # zeroes its line's tag and valid bits; run from uncached memory (KSEG1).
    a.lw("t0", S_FLUSH, "t6")
    a.beq("t0", "zero", "trap_flushed")
    a.sw("zero", S_FLUSH, "t6")
    a.la("t1", "trap_inval", 0x20000000)
    a.jr("t1")
    a.label("trap_inval")
    a.lui("t0", 0xFFFE)
    a.lw("t2", 0x130, "t0")
    a.addiu("t3", "zero", 0x804)
    a.sw("t3", 0x130, "t0")
    a.mfc0("t4", 12)
    a.lui("t3", 0x0001)
    a.or_("t3", "t4", "t3")
    a.mtc0("t3", 12)
    a.nop()
    a.nop()
    lines = sorted({(x + d) & ~0xF for x in sites.values() for d in (0, 4)})
    for line in lines:
        a.lui("t3", (line >> 16) + (1 if line & 0x8000 else 0))
        a.sw("zero", (line & 0xFFFF) - (0x10000 if line & 0x8000 else 0), "t3")
    a.mtc0("t4", 12)
    a.nop()
    a.nop()
    a.sw("t2", 0x130, "t0")
    a.la("t1", "trap_flushed")
    a.jr("t1")
    a.label("trap_flushed")
    a.lw("t0", S_VRAM, "t6")
    a.addiu("t1", "zero", 1)
    a.bne("t0", "t1", "trap_out")
    # VRAM read-back: wait for GPU DMA and the command FIFO, GP0(C0h) of all of
    # VRAM into VRAM_BUF, then spin until the frontend restores its save state
    a.lui("t0", 0x1F80)
    a.label("vr_dma")
    a.lw("t1", 0x10A8, "t0")
    a.lui("t2", 0x0100)
    a.and_("t1", "t1", "t2")
    a.bne("t1", "zero", "vr_dma")
    a.label("vr_rdy")
    a.lw("t1", 0x1814, "t0")
    a.lui("t2", 0x0400)
    a.and_("t1", "t1", "t2")
    a.beq("t1", "zero", "vr_rdy")
    a.lui("t1", 0x0100)  # GP0(01h): clear the texture cache (no effect on VRAM)
    a.sw("t1", 0x1810, "t0")
    a.lui("t1", 0xC000)
    a.sw("t1", 0x1810, "t0")
    a.sw("zero", 0x1810, "t0")
    a.li("t1", (512 << 16) | 1024)
    a.sw("t1", 0x1810, "t0")
    a.label("vr_wait")
    a.lw("t1", 0x1814, "t0")
    a.lui("t2", 0x0800)
    a.and_("t1", "t1", "t2")
    a.beq("t1", "zero", "vr_wait")
    a.lui("t3", VRAM_BUF >> 16)
    a.lui("t4", (VRAM_BUF + 0x100000) >> 16)
    a.label("vr_loop")
    a.lw("t1", 0x1810, "t0")
    a.addiu("t3", "t3", 4)
    a.bne("t3", "t4", "vr_loop", slot=lambda: a.sw("t1", -4, "t3"))
    a.addiu("t1", "zero", 2)
    a.sw("t1", S_VRAM, "t6")
    a.label("vr_spin")
    a.lw("t1", S_VRAM, "t6")
    a.addiu("t2", "zero", 2)
    a.beq("t1", "t2", "vr_spin")
    a.label("trap_out")
    back_entry("trap")

    code = a.assemble()
    if CODE + len(code) > STUB + RING:
        raise RuntimeError(f"stub code too long ({len(code)} bytes)")
    entry = {name: a.labels[name] for name in sites}
    return code, entry


# --------------------------------------------------------------- capture --

class Capture:
    """One run from power-on (see the module docstring)."""

    def __init__(self, args):
        self.args = args
        self.ver = args.ver
        self.out = args.out.resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.notes: list[str] = []
        exe_path = ROOT / "disc" / self.ver / "main.exe"
        if not exe_path.exists():
            raise SystemExit(f"beetle: {exe_path} missing (python tools/extract.py)")
        self.exe = exe_path.read_bytes()
        self.t_addr, self.t_size = struct.unpack_from("<II", self.exe, 0x18)
        self.sites = hook_sites(self.ver)
        self.env = dict(kv.split("=", 1) for kv in args.start)
        for k in self.env:
            if k not in start_state.OPTIONS:
                raise SystemExit(f"beetle: --start {k}: not one of {', '.join(start_state.OPTIONS)}")
        self.mode = start_state.boot_mode(self.env)
        self.patched = None   # vsync of the setGameMode(n) patch
        self.poked = None     # vsync of the gameState pokes
        self.ready = False    # every hook site holds the executable's code
        self.log = None
        self.setloc = None
        self.boot_seen = False
        self.records = 0
        self.shadows = 0      # frames run twice (hooked copy, then clean)
        self.perturbed = []   # frames whose hooked copy showed another picture than the clean run
        self.ram_differs = 0  # frames whose hooked copy ended with other user RAM (hooks' words left out)
        self.touching = False  # the reference's RAM writes are being mapped
        orig = {n: (self.exe_word(a), self.exe_word(a + 4)) for n, a in self.sites.items()}
        for n in ("vsync", "cdcw", "vscb", "trap", "sync"):
            for w in orig[n]:
                op = w >> 26
                if op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and (w & 0x3F) in (8, 9)):
                    raise SystemExit(f"beetle: {n}: a branch among the displaced instructions")
        for n in ("entry", "ready"):
            if orig[n] != (0x0040F809, 0):
                raise SystemExit(f"beetle: {n} at 0x{self.sites[n]:08X} is not `jalr v0; nop`")
        self.orig = orig
        self.code, self.entry = build_stub(self.sites, orig)
        lo, hi = sdk_code_range(self.ver)
        state = bytearray(0x100)
        struct.pack_into("<I", state, S_MAGIC, MAGIC)
        struct.pack_into("<I", state, S_ENABLE, 1)
        struct.pack_into("<II", state, S_SDKLO, lo, hi - lo)
        self.carry = state    # the stub's state block, carried from one hooked copy to the next
        self.pending = None   # the VSync run not written yet: [hblanks, arg, vblank, count, last vblank]
        self.held: list = []  # events after its first call, written after it
        self.prev_cont = None  # the last copy's records from its run on into the next frame
        self.open_run = self.prev_open = (0, 0)  # (open, argument) of the run a copy left open
        self.frame_v = -1     # the frame whose records were fed last, and how late in it the last was
        self.last_frac = 0.0
        self.trap: dict[int, int] = {}    # vblank -> root counter 1 at its trap
        self.expect: dict[int, int] = {}  # vblank -> root counter 1 where its trap belonged
        self.nominal = 263    # hblanks between vblanks (the last on-time period)
        self.late_traps = 0   # vblank traps the game held off (records after them retimed)
        self.spliced = 0      # events the copies did only after their frame's end
        self.unspliced = 0    # run-on events left out (the copy's game went another way)
        self.last_return = None  # the vblank the reference's last VSync(0)/VSync(n) returned at
        syms = read_symbols(self.ver)
        self.libetc = (syms["vsyncLastVcount"], syms["vblankCounter"])  # LIBETC's VSync state in RAM
        self.forced_waits = 0    # waiting calls spliced on the reference's RAM's word
        self.etc_prev = None     # that state after the frame before
        self.toggles: list = []  # (vblank, BIOS_TOGGLE) after the last frames
        self.bios_misses = 0
        self.last_wait = -1.0    # the log's last waiting call (vblank.fraction)
        self.last_wait_arg = 0   # and its argument
        self.synthesized = 0     # waiting calls written from the RAM alone (no copy made them)
        self.blocked_until = 0   # and up to which its main thread waited there
        self.notes_spliced: list[str] = []

    # -- the boot executable --
    def exe_word(self, addr: int) -> int:
        return struct.unpack_from("<I", self.exe, addr - self.t_addr + 0x800)[0]

    def sector_loaded(self, k: int) -> bool:
        off = 0x800 + k * 2048
        return self.b.read(self.t_addr + k * 2048, 2048) == self.exe[off:off + 2048]

    def sites_hold_code(self) -> bool:
        return all(self.b.u32(a + 4 * i) == self.orig[n][i] for n, a in self.sites.items() for i in (0, 1))

    def try_patch_mode(self) -> None:
        """kernelMain's setGameMode(6) -> setGameMode(n), once that word holds the
        executable's code and its last sector is still missing (the BIOS jumps to it
        only after loading all of it, so it has not run)."""
        a = start_state.ADDR[self.ver]["setMode6"]
        if self.b.u32(a) != self.exe_word(a):
            return
        if self.sector_loaded((self.t_size - 1) // 2048):
            raise SystemExit(f"beetle: the executable finished loading before frame {self.b.frame}: "
                             "too late to patch setGameMode")
        if self.b.u32(a) != 0x34040006:
            raise SystemExit("beetle: kernelMain's setGameMode(6) is not where start_state.py says")
        self.b.put32(a, 0x34040000 + self.mode)
        self.patched = self.b.frame
        self.note(f"start: setGameMode({self.mode}) at vsync {self.b.frame}")

    def apply_pokes(self) -> None:
        a = start_state.ADDR[self.ver]
        if self.b.u32(a["bootMode"]) == 0:
            return
        self.b.put32(a["debugFontRam"], 0)
        gs = a["gameState"]
        pre, party, rest = start_state.pokes(self.env)

        def poke(off, size, value):
            self.b.write(gs + off, (value & ((1 << 8 * size) - 1)).to_bytes(size, "little"))

        for p in pre:
            poke(*p)
        if party is not None:
            for i, cid in enumerate(party):
                poke(0x1D34 + i, 1, cid & 0xFF)
            bits = 0
            for cid in party:
                if 0 <= cid < 16:
                    bits |= 1 << cid
            poke(0x1D30, 2, int.from_bytes(self.b.read(gs + 0x1D30, 2), "little") | bits)
        for p in rest:
            poke(*p)
        self.poked = self.b.frame
        self.note(f"start: gameState pokes at vsync {self.b.frame} (bootMode set)")

    # -- the hooked copy of a frame --
    def ring(self, v: int, start: int = 0) -> list:
        """The records the second instance's stub made since its ring was emptied (from
        record `start` on)."""
        h = self.h
        after = h.read(STUB, 0x100)
        wr = struct.unpack_from("<I", after, S_WR)[0]
        over = struct.unpack_from("<I", after, S_OVER)[0]
        if over:
            raise SystemExit(f"beetle: the stub's ring overflowed at vsync {v} ({over} records lost)")
        return [struct.unpack("<IiIII", h.read(STUB + RING + (i & (RING_N - 1)) * 32, 20)) for i in range(start, wr)]

    def shadow(self, v: int, buttons: int, next_buttons: int, vram: bool) -> tuple[list, list | None, bytes | None]:
        """Run frame v in the second instance from the reference's state, with the
        hooks in: its records, the records of its run on into frame v + 1 (None with
        a VRAM read-back) and its VRAM read-back (if asked). The reference only
        serializes its state; it never loads one."""
        a, h = self.b, self.h
        if not a.lib.bf_snap_save():
            raise RuntimeError("retro_serialize failed")
        if not h.lib.bf_snap_load():
            raise RuntimeError("retro_unserialize failed")
        state = bytearray(self.carry)
        struct.pack_into("<III", state, S_WR, 0, 0, 0)
        struct.pack_into("<I", state, S_VBC, a.frame)  # vblanks so far: the frame's interrupt makes it v
        struct.pack_into("<I", state, S_VRAM, 1 if vram else 0)
        struct.pack_into("<I", state, S_FLUSH, 1)
        struct.pack_into("<I", state, S_OPEN, 0)  # runs across frames are joined in Python
        h.write(STUB, bytes(state))
        h.write(CODE, self.code)
        for n, addr in self.sites.items():
            h.put32(addr, j_word(self.entry[n]))
            if n not in ("entry", "ready"):
                h.put32(addr + 4, 0)
        h.pad(0, buttons)
        h.lib.bf_run(1)
        recs = self.ring(v)
        after = h.read(STUB, 0x100)
        vbc = struct.unpack_from("<I", after, S_VBC)[0]
        if vbc != a.frame + 1 and struct.unpack_from("<I", after, S_PERIOD)[0]:
            self.note(f"warning: vsync {v}: {vbc - a.frame} vblank interrupts in one frame")
        self.carry = bytearray(after)
        struct.pack_into("<I", self.carry, S_VRAM, 0)
        self.vblank_trap()
        self.copy_hashes = (h.lib.bf_frame_hash(), h.lib.bf_user_ram_hash())
        vdata = None
        cont = None
        if vram:
            # the read-back takes a few frames of GPU transfer; the copy is thrown away after
            for _ in range(30):
                if h.u32(STUB + S_VRAM) == 2:
                    vdata = h.read(VRAM_BUF, 0x100000)
                    break
                h.lib.bf_run(1)
        else:
            # The copy runs a little slower than the reference (the hooks' cycles), so
            # what the reference did in the last cycles of frame v the copy has not done
            # yet. It runs on into frame v + 1: those events come first there
            # (Capture.splice finds them). The stub's state is left as it is: the frame
            # may have ended inside a piece (a record half written).
            # the run the copy left open: the run-on's calls of it merge into it in place,
            # where the next copy (its runs start fresh) has a record
            self.open_run = struct.unpack_from("<Ii", after, S_OPEN)
            h.put32(STUB + S_RD, len(recs))  # the copy's records are read: the ring has room again
            h.pad(0, next_buttons)
            h.lib.bf_run(1)
            cont = self.ring(v + 1, len(recs))
        self.shadows += 1
        return recs, cont, vdata

    # -- events the copy did late --
    SLIP_LAG = 0.1      # frames: how much later than the next copy the copy's run-on may do an event
    SLIP_EARLY = 0.006  # frames: or earlier (each copy's times count from its own vblank interrupt)
    SLIP_WINDOW = 0.1   # frames into its run-on: where events the reference did in frame v can show

    def bios_vblank(self, v: int) -> None:
        """'p <vblank>' lines: vblanks whose BIOS pad/card handler did not run. A kernel word
        (BIOS_TOGGLE, the same in the US and JP BIOS) flips at every vblank the handler serves;
        it misses one when a CD interrupt is still being handled as the vblank comes (the
        dispatch takes the vblank's I_STAT bit first): the pad buffer keeps the last vblank's
        state (US worldmap area_c3_veh3 2570) and the card driver's slot turn stays. A miss of
        at most 3 vblanks between flips counts (longer: the handler is off); the line is
        written when the next flip comes."""
        w = self.b.u32(BIOS_TOGGLE)
        hist = self.toggles
        hist.append((v, w))
        if len(hist) > 6:
            del hist[0]
        if len(hist) < 3 or hist[-1][1] == hist[-2][1]:
            return
        k = 0  # vblanks before this flip without one
        while k + 2 < len(hist) and hist[-2 - k][1] == hist[-3 - k][1]:
            k += 1
        if 0 < k <= 3 and len(hist) >= k + 3:
            for i in range(k):
                self.write_line(f"p {hist[-2 - i][0]}")
                self.bios_misses += 1

    def wait_missing(self, v: int) -> bool:
        """Whether the log lacks a waiting call the reference made in frame v - 1, from its RAM
        after frame v. LIBETC's VSync sets vsyncLastVcount to vblankCounter when a VSync(0) or
        VSync(n) returns, and a wait returns only right after a vblank: equal after frame v,
        a wait was pending at the end of frame v - 1. The log has it if its last waiting call
        came after the reference's last return before that (the word after frame v - 1)."""
        last, count = (self.b.u32(a) for a in self.libetc)
        prev, self.etc_prev = self.etc_prev, (last, count)
        self.etc_frames = (prev, (last, count))
        if count == 0 or prev is None or prev[1] == 0 or last != count:
            return False
        returned = (v - 1) - ((prev[1] - prev[0]) & 0xFFFFFFFF)  # the last return before, log vblanks
        return self.last_wait < returned

    def resync_return(self, v: int, etc) -> None:
        """The reference's last VSync(0)/VSync(n) return, in log vblanks, from LIBETC's words in
        its RAM after frame v (etc: (vsyncLastVcount, vblankCounter)): what feed() reckons the
        next waiting call's return from. The log's own reckoning drifts (US worldmap
        area_c7_gear: 2 vblanks ahead by 2064, so the reference's VSync(2) at the end of frame
        2065, in the copy's run-on at 2066.004, counted as made while the 2064.008 call still
        waited and was left out; the frame after got it from the RAM check, a frame late)."""
        if etc is not None and etc[1] != 0:
            self.last_return = v - ((etc[1] - etc[0]) & 0xFFFFFFFF)
            if self.last_wait < self.last_return:
                # RAM proves the logged wait finished. Its stale predicted
                # return must not discard later calls as still blocked.
                self.blocked_until = self.last_return

    def splice(self, v: int, cont: list, nxt: list, open_run: tuple[int, int], missing_wait: bool = False) -> list:
        """The events the reference made at the end of frame v that its copy did only in
        its run on into frame v + 1 (cont), found by comparing the run-on with the next
        copy (nxt: frame v + 1 from the reference's exact state). They go to frame v,
        late in it (their run-on times less the run-on's lag behind the next copy).

        The copy's game can take another path than the reference's (its polling loops
        see other times), and the two copies' times differ by a few hblanks either way,
        so: the game's main-thread calls (VSync, mode starts) of the run-on are lined up
        in order with the next copy's (same call, at most SLIP_LAG apart); the fewest
        leading ones that leave the rest all lined up are the reference's, if it was not
        waiting in VSync(0)/VSync(n) through the end of frame v and they come within
        SLIP_WINDOW of the run-on's vblank. Interrupt-side events (ready callback,
        command completion, CD commands) match one by one (same kind and argument, at
        most SLIP_EARLY sooner or SLIP_LAG later in the run-on); one without a match
        that the next copy makes up to SLIP_LAG later is that one, sooner in the copy;
        the rest within SLIP_WINDOW are the reference's. XG_BEETLE_SPLICE_DEBUG=v,...
        prints both lists for those frames."""
        def when(r):
            return self.time(r[2], r[0] >> 16)

        def key(r):
            return r[0] & 0xFF, r[1]

        if str(v) in os.environ.get("XG_BEETLE_SPLICE_DEBUG", "").split(","):
            print(f"splice {v}: run-on", [(KIND_NAMES.get(r[0] & 0xFF), r[1], round(when(r), 3)) for r in cont],
                  "next copy", [(KIND_NAMES.get(r[0] & 0xFF), r[1], round(when(r), 3)) for r in nxt],
                  f"a waiting call missing: {missing_wait}", flush=True)
        main = (KINDS["v"], KINDS["m"], KINDS["b"])
        mc = [c for c in cont if c[0] & 0xFF in main]
        me = [e for e in nxt if e[0] & 0xFF in main]

        def align(me):
            """The fewest leading run-on calls that leave the rest lined up with me, and the lags.
            The run-on can end with calls the next copy does not make: its own game finished the
            frame's work before the vblank where the reference's ran past it (US field 17 at 1043:
            run-on v1 1044.989, v0 .992; the reference's 1045.023, .027), so a rest longer than me
            lines up when its first len(me) calls do and the others come no sooner than me's last
            (a run-on call before that would be one the next copy makes: US battle g_v0s7 2895,
            run-on v-1 2896.004, v0 .008, next copy v-1 .023: the reference's frame-end v-1 and v0,
            then the next frame's v-1; US field 567 1339: run-on ... v1 1340.970, v0 .970, next copy
            ... v1 .970, where the next copy's frame ended)."""
            for first in range(len(mc) + 1):
                rest = mc[first:]
                if len(rest) > len(me):
                    if not me or any(when(c) < when(me[-1]) for c in rest[len(me):]):
                        continue
                    rest = rest[:len(me)]
                if all(key(c) == key(e) and abs(when(c) - when(e)) <= self.SLIP_LAG for c, e in zip(rest, me)):
                    return first, [when(c) - when(e) for c, e in zip(rest, me)]
            return len(mc), []

        k, lags = align(me)
        if me and open_run[0] and key(me[0]) == (KINDS["v"], open_run[1]) and open_run[1] in (-1, 1):
            # The next copy's first record continues the run the copy left open (the run-on's
            # calls of it merged into the copy's record: it has none), or it is a new call of
            # that argument the reference made after its frame-end calls (US battle 21562: the
            # frame start's VSync(-1) left open, then VSync(1), DrawSync, VSync(1) before the
            # vblank and the catch-up VSync(-1) after it). The reading that leaves fewer of the
            # run-on's calls to the reference's frame end is the one.
            k2, lags2 = align(me[1:])
            if k2 <= k:
                k, lags = k2, lags2
        forced = 0
        if missing_wait:
            # The reference's RAM has a wait that returned at vblank v + 1, the log no call that
            # waited through it: the run-on's first waiting call is the reference's, however late
            # the copy made it (US worldmap area_c7_gear 2066: the copy's VSync(2) came at
            # 2067.962, the next copy's next call at 2067.966; two calls, not one).
            for i, c in enumerate(mc):
                if c[0] & 0xFF == KINDS["v"] and c[1] >= 0 and c[1] != 1:
                    forced = i + 1
                    break
            if not forced:
                # the copies never made it (area_c7_gear 2116: the copy of the frame and its
                # run-on have no call at all): the reference's call is written at the frame's
                # end with the argument of the log's last wait
                self.synthesized += 1
            if forced > k:
                k = forced
                lags = [when(c) - when(e) for c, e in zip(mc[k:], me)
                        if key(c) == key(e) and abs(when(c) - when(e)) <= self.SLIP_LAG]
                self.forced_waits += 1
        lags = list(lags)
        late = []
        for i, c in enumerate(mc[:k]):
            if i < forced:
                late.append(c)  # the reference's RAM says so
            elif self.blocked_until > v or when(c) - (v + 1) > self.SLIP_WINDOW:
                self.unspliced += 1  # the reference's main thread was waiting, or too late: another path
            else:
                late.append(c)
        ic = [c for c in cont if c[0] & 0xFF not in main]
        ie = [e for e in nxt if e[0] & 0xFF not in main]
        used = [False] * len(ie)
        lone = []
        for c in ic:
            for i, e in enumerate(ie):
                if not used[i] and key(e) == key(c) and -self.SLIP_EARLY <= when(c) - when(e) <= self.SLIP_LAG:
                    used[i] = True
                    lags.append(when(c) - when(e))
                    break
            else:
                lone.append(c)
        for c in lone:
            if any(not used[i] and key(e) == key(c) and 0 < when(e) - when(c) <= self.SLIP_LAG
                   for i, e in enumerate(ie)) or when(c) - (v + 1) > self.SLIP_WINDOW:
                self.unspliced += 1  # the next copy's own, sooner in the copy; or too late
            else:
                late.append(c)
        lag = max(min(lags, default=0.0), 0.0)
        out = []
        if missing_wait and not forced:
            h = max(int(round(0.999 * self.nominal)) - self.late(v), 0)
            late_wait = (KINDS["v"] | h << 16, self.last_wait_arg, v, 1, v)
        else:
            late_wait = None
        for c in [c for c in cont if any(c is x for x in late)]:  # in the run-on's order
            kind = c[0] & 0xFF
            t = min(max(when(c) - lag, v + self.last_frac), v + 0.999)
            self.last_frac = t - v
            h = max(int(round((t - v) * self.nominal)) - self.late(v), 0)
            out.append((kind | h << 16, c[1], v, 1 if kind == KINDS["v"] else c[3],
                        v if kind == KINDS["v"] else c[4]))
        if late_wait is not None:
            self.last_frac = 0.999
            out.append(late_wait)
        self.spliced += len(out)
        for c in out:
            self.notes_spliced.append(f"{v}: {KIND_NAMES.get(c[0] & 0xFF, '?')}({c[1]})")
        return out

    def feed(self, recs: list) -> None:
        """A copy's records into the log: a run of VSync(-1) or VSync(1) is written when
        the next VSync call or a mode start comes (as Redux's runs close: runs carry on
        across frames and spliced events), the events meanwhile after it, so the lines are
        in the order of their first times; a waiting call (VSync(0), VSync(n)) at once."""
        for w0, arg, vb, w3, w4 in recs:
            kind, h = w0 & 0xFF, w0 >> 16
            if vb == self.frame_v:
                self.last_frac = max(self.last_frac, self.t(vb, h) - vb)
            if kind == KINDS["v"] and arg >= 0 and arg != 1:
                # VSync(0) / VSync(n): the main thread waits (LIBETC VSync: until n - 1
                # vblanks after the last return, then the next one)
                target = vb if self.last_return is None or arg == 0 else self.last_return + arg - 1
                self.last_return = self.blocked_until = max(vb, target) + 1
                self.last_wait = self.time(vb, h)
                self.last_wait_arg = arg
            if kind == KINDS["v"]:
                p = self.pending
                if p is not None and p[1] == arg and arg in (-1, 1):
                    p[3] += w3
                    p[4] = w4
                    continue
                self.flush()
                if arg in (-1, 1):
                    self.pending = [h, arg, vb, w3, w4]
                else:
                    self.record(kind, h, arg, vb, w3, w4)
            elif kind == KINDS["m"]:
                self.flush()
                self.record(kind, h, arg, vb, w3, w4)
            elif self.pending is not None:
                self.held.append((kind, h, arg, vb, w3, w4))
            else:
                self.record(kind, h, arg, vb, w3, w4)

    def flush(self) -> None:
        if self.pending is not None:
            h, arg, vb, count, last = self.pending
            self.pending = None
            self.record(KINDS["v"], h, arg, vb, count, last)
        held, self.held = self.held, []
        for r in held:
            self.record(*r)

    # -- the log --
    def vblank_trap(self) -> None:
        """Where the copy's vblank interrupt (the stub's trap) came, and where it belonged.
        The game can hold the interrupt off (a mode start: the trap for vsync 1546 of
        the US attract came 243 hblanks late); the period between the last two traps is
        then wrong (506, then 20). The vblanks keep their steady spacing (the last
        on-time period, normally 263 hblanks); a trap more than 3 hblanks after it is
        late by that much, one that comes early means the display timing changed (the
        spacing restarts there)."""
        vbc, t1, per = (struct.unpack_from("<I", self.carry, o)[0] for o in (S_VBC, S_T1, S_PERIOD))
        t1 &= 0xFFFF
        if vbc in self.trap or per == 0:
            return
        self.trap[vbc] = t1
        prev = self.expect.get(vbc - 1)
        e = None if prev is None else (prev + self.nominal) & 0xFFFF
        late = 0 if e is None else ((t1 - e + 0x8000) & 0xFFFF) - 0x8000
        if e is None or late <= 3 or (250 <= per <= 276 and late < 40):
            # on time; or earlier than the steady spacing (a trap is never early:
            # the display timing changed); or a steady drift of the period
            self.expect[vbc] = t1
            if 250 <= per <= 276:
                self.nominal = per
        else:
            self.expect[vbc] = e  # a late trap: its vblank was where the steady spacing puts it
            self.late_traps += 1

    def late(self, vb: int) -> int:
        """Hblanks the trap of vblank vb came after the vblank."""
        if vb not in self.trap or vb not in self.expect:
            return 0
        return ((self.trap[vb] - self.expect[vb] + 0x8000) & 0xFFFF) - 0x8000

    def time(self, vb: int, h: int) -> float:
        """A record's time: vblanks, and hblanks since the vblank (not since the
        trap) over the steady period; not clamped."""
        if not self.trap:
            return float(vb)  # before the first vblank interrupt the stub saw
        return vb + (h + self.late(vb)) / self.nominal

    def t(self, vb: int, h: int) -> float:
        """The time written to the log: within its vblank (.999 at most, as Redux's)."""
        return vb + min(max(self.time(vb, h) - vb, 0.0), 0.999)

    def record(self, kind: int, h: int, arg: int, vb: int, w3: int, w4: int) -> None:
        if kind == KINDS["b"]:
            if not self.boot_seen and arg != 0:
                self.boot_seen = True
                self.write_line(f"b {self.t(vb, h):.3f}")
            return
        if vb < self.args.start_vsync:
            return
        self.records += 1
        if kind == KINDS["v"]:
            if w3 == 1:
                self.write_line(f"v {arg} {self.t(vb, h):.3f}")
            else:
                self.write_line(f"v {arg} {self.t(vb, h):.3f} {w4} {w3}")
        elif kind == KINDS["m"]:
            target = arg & 0xFFFFFFFF
            mode = -1
            for i in range(7):
                if self.b.u32(GAME_MODES + 16 * i) == target:
                    mode = i
            self.write_line(f"m {mode} {vb}")
        elif kind == KINDS["r"]:
            self.write_line(f"r {arg} {self.t(vb, h):.3f}")
        elif kind == KINDS["s"]:
            self.write_line(f"s {arg} {self.t(vb, h):.3f}")
        elif kind == KINDS["k"]:
            com = arg & 0xFF
            t = self.t(vb, h)
            self.write_line(f"k {com} {t:.3f}")
            if com == 2 and w3 != 0xFFFFFFFF:
                def bcd(x):
                    return (x >> 4) * 10 + (x & 15)
                self.setloc = (bcd(w3 & 0xFF) * 60 + bcd((w3 >> 8) & 0xFF)) * 75 + bcd((w3 >> 16) & 0xFF) - 150
            elif com in (6, 0x1B):
                self.write_line(f"c {com} {self.setloc if self.setloc is not None else -1} {t:.3f}")
                self.setloc = None

    def write_line(self, line: str) -> None:
        if self.log is not None:
            self.log.write(line + "\n")

    def close_log(self) -> None:
        """The run still open at the end (Redux's vrun at close)."""
        if self.log is None:
            return
        self.flush()
        self.log.close()
        self.log = None

    # -- memory cards --
    def put_cards(self, savedir: Path, content: Path) -> None:
        """--card1/--card2 into an instance's save directory before its core opens: the
        cards are files there, not in retro_serialize, so the hooked copies need their own
        copy of the reference's cards (a load from a card read fresh cards in the copies and
        lost the log's CD commands after it)."""
        for slot, src in enumerate((self.args.card1, self.args.card2)):
            if src is not None:
                shutil.copy(src, savedir / f"{Path(content).stem}.{slot}.mcr")

    def check_cards(self, rundir: Path, content: Path) -> None:
        """--cards-out: the reference's cards after the run. The copies run each frame from the
        reference's state and write what it writes, so their cards end equal: say if not."""
        names = [f"{Path(content).stem}.{slot}.mcr" for slot in (0, 1)]
        if self.args.cards_out is not None:
            self.args.cards_out.mkdir(parents=True, exist_ok=True)
            for n in names:
                if (rundir / "save" / n).exists():
                    shutil.copy(rundir / "save" / n, self.args.cards_out / n)
        if self.h is not None:
            for n in names:
                a, b = rundir / "save" / n, rundir / "save_b" / n
                if a.exists() != b.exists() or (a.exists() and a.read_bytes() != b.read_bytes()):
                    self.note(f"warning: card {n}: the hooked copies' card ended different from the reference's")

    def hw_content(self, content: Path) -> tuple[Path, dict[str, str]]:
        """--hw: the HW core's Vulkan renderer. Its texture dump and replacement folders sit beside the
        disc image (<stem>-texture-dump, <stem>-texture-replacements), so the image is staged in
        OUT/content (a copy of the cue sheet, links to its files): the dump lands in OUT/textures and
        --texture-replace's folder is linked in as the replacements."""
        args = self.args
        options = {"beetle_psx_hw_renderer": "hardware_vk"}
        if not (args.texture_dump or args.texture_replace):
            return content, options
        stage = self.out / "content"
        shutil.rmtree(stage, ignore_errors=True)
        stage.mkdir(parents=True)
        content = Path(content).resolve()
        if content.suffix.lower() == ".cue":
            for line in content.read_text(errors="replace").splitlines():
                m = re.match(r'\s*FILE\s+"([^"]+)"', line)
                if m:
                    (stage / m.group(1)).symlink_to(content.parent / m.group(1))
            shutil.copy(content, stage / content.name)
        else:
            (stage / content.name).symlink_to(content)
        staged = stage / content.name
        options["beetle_psx_hw_track_textures"] = "enabled"
        if args.texture_dump:
            dump = self.out / "textures"
            shutil.rmtree(dump, ignore_errors=True)
            dump.mkdir()
            (stage / f"{content.stem}-texture-dump").symlink_to(dump.resolve())
            options["beetle_psx_hw_dump_textures"] = "enabled"
        if args.texture_replace:
            (stage / f"{content.stem}-texture-replacements").symlink_to(Path(args.texture_replace).resolve())
            options["beetle_psx_hw_replace_textures"] = "enabled"
        return staged, options

    def note(self, text: str) -> None:
        self.notes.append(text)
        print("beetle:", text, flush=True)

    # -- the run --
    def run(self) -> int:
        args = self.args
        events = read_pad(args.pad)
        rundir = CACHE / "runs" / f"{os.getpid()}"
        shutil.rmtree(rundir, ignore_errors=True)
        (rundir / "save").mkdir(parents=True)
        content = args.content or disc_image(self.ver, args.disc)
        options = dict(kv.split("=", 1) for kv in args.option)
        core = args.core
        if args.hw or args.texture_dump or args.texture_replace:
            content, hw_options = self.hw_content(content)
            options = {**hw_options, **options}
            core = core or Path(os.environ.get("XG_BEETLE_HW_CORE") or DEFAULT_HW_CORE)
        self.put_cards(rundir / "save", content)
        self.b = Beetle(content, core=core, sysdir=args.sysdir, savedir=rundir / "save",
                        options=options, verbose=args.verbose,
                        option_log=self.out / "options.log", hw=args.hw or args.texture_dump or bool(args.texture_replace))
        b = self.b
        if (args.hw or args.texture_dump or args.texture_replace) and not b.lib.bf_hw_active():
            raise SystemExit("beetle: the core did not take the Vulkan context (is --core the Beetle PSX HW core?)")
        hooks = args.vsync_log and not args.no_hooks
        vrams = {int(x) for x in args.vram.split(",") if x}
        self.h = None
        if hooks or vrams:
            # the second instance runs each frame again with the hooks in
            (rundir / "save_b").mkdir()
            self.put_cards(rundir / "save_b", content)
            self.h = Beetle(content, core=args.core, sysdir=args.sysdir, savedir=rundir / "save_b",
                            options=dict(kv.split("=", 1) for kv in args.option), index=1)
            self.h.lib.bf_audio_mute(1)
        for inst in (b, self.h):
            if inst is None:
                continue
            # RAM hashes leave the hooks' words out
            inst.lib.bf_hash_exclude(STUB & 0x1FFFFF, STUB_END & 0x1FFFFF)
            for addr in self.sites.values():
                inst.lib.bf_hash_exclude(addr & 0x1FFFFF, (addr & 0x1FFFFF) + 8)
            inst.lib.bf_hash_exclude(VRAM_BUF & 0x1FFFFF, (VRAM_BUF & 0x1FFFFF) + 0x100000)
        audio_idx = None
        if args.audio:
            b.audio_file(self.out / "audio.pcm")
            audio_idx = open(self.out / "audio.txt", "w")
            audio_idx.write("# vsync, first audio frame of its retro_run in audio.pcm (44100 Hz stereo s16le)\n")
        if args.vsync_log:
            self.log = open(self.out / "vsyncs.txt", "w")
            self.log.write(f"# xenogears VSync log 1 (Beetle PSX, {self.ver}, tools/beetle.py; "
                           "vblanks from power-on)\n")
        hashes = None
        if args.hashes:
            hashes = open(self.out / "hashes.txt", "w")
            hashes.write("# vsync, FNV-1a 64 of: the picture, that vblank's audio, main RAM without the hooks' "
                         "words\n")
        ramlog = None
        logaddrs = [int(x, 0) for x in args.log.split(",") if x]
        if logaddrs:
            ramlog = open(self.out / "ramlog.txt", "w")
        states = {int(x) for x in args.states.split(",") if x}
        rams = {int(x) for x in args.ram.split(",") if x}
        if args.load_state:
            b.load_state(args.load_state)
            b.frame = args.load_frame
            self.patched = self.poked = -1
            self.note(f"loaded {args.load_state} as vsync {b.frame}")
        pi, cur = 0, 0
        shots = 0
        t0 = time.monotonic()
        while b.frame < args.stop:
            v = b.frame + 1
            while pi < len(events) and events[pi][0] <= v:
                cur = events[pi][1]
                pi += 1
            b.pad(0, cur)
            if not self.ready and b.frame > 100 and self.sites_hold_code():
                self.ready = True
                if self.h:
                    # from here, watch that the reference never writes the stub's region
                    b.lib.bf_touch_reset()
                    self.touching = True
                if self.h:
                    self.note(f"hook sites loaded by vsync {b.frame}: from here the second instance runs "
                              "each frame again from the reference's state, with the hooks in")
            recs, cont, vdata = [], None, None
            # two frames before --from warm the stub's root counter reading up
            want_log = hooks and self.ready and (v >= args.start_vsync - 2 or not self.boot_seen)
            want_vram = v in vrams
            if v == args.start_vsync:
                self.pending, self.held = None, []  # runs start fresh at --from, as Redux arms there
            shadowed = (want_log or want_vram) and self.ready
            to_splice = None
            if shadowed:
                if not self.sites_hold_code():
                    raise SystemExit(f"beetle: a hook site changed by vsync {b.frame}")
                nxt = cur
                for at, buttons in events[pi:]:
                    if at > v + 1:
                        break
                    nxt = buttons
                recs, cont, vdata = self.shadow(v, cur, nxt, want_vram)
                if self.prev_cont is not None and want_log:
                    to_splice = (self.prev_cont, recs, self.prev_open)
            self.prev_cont = cont if shadowed and want_log else None
            self.prev_open = self.open_run
            if audio_idx:
                audio_idx.write(f"{v} {b.audio_frames()}\n")
            b.run(1)
            if self.mode >= 0 and self.patched is None and b.frame > 100:
                self.try_patch_mode()
            if self.mode >= 0 and self.patched is not None and self.poked is None:
                self.apply_pokes()
            if self.log is not None and v >= args.start_vsync:
                self.bios_vblank(v)
            if to_splice is not None:
                # after the reference ran frame v: its RAM says whether a wait returned at vblank v
                missing = self.wait_missing(v)
                # calls spliced into frame v - 1 reckon from the returns up to vblank v - 1 (one of
                # them may have returned at v); frame v's own calls from those up to vblank v
                self.resync_return(v - 1, self.etc_frames[0])
                late = self.splice(v - 1, *to_splice, missing_wait=missing)
                if late:
                    self.write_line(f"# vsync {v - 1}: {len(late)} event(s) the hooked copy made only after "
                                    "the frame's end (spliced)")
                self.feed(late)
                if not any(c[0] & 0xFF == KINDS["v"] and c[1] >= 0 and c[1] != 1 for c in late):
                    self.resync_return(v, self.etc_frames[1])
            self.frame_v, self.last_frac = v, 0.0
            self.feed(recs)
            # (an interlaced picture weaves in the instance's previous field: the copy's is its run-on's)
            if shadowed and want_log and self.shadows > 1 and b.frame_info()["h"] <= 288:
                if self.copy_hashes[0] != b.lib.bf_frame_hash():
                    self.perturbed.append(v)
                    self.write_line(f"# vsync {v}: the hooked copy showed another picture")
                if self.copy_hashes[1] != b.lib.bf_user_ram_hash():
                    self.ram_differs += 1
            if vdata is not None:
                (self.out / f"vram{v:05d}.bin").write_bytes(vdata)
                vram_png(vdata, self.out / f"vram{v:05d}.png")
            elif want_vram:
                self.note(f"vram {v}: no read-back" + ("" if self.ready else " (the executable is not loaded yet)"))
            if v >= args.start_vsync:
                if args.every > 0 and v % args.every == 0:
                    b.frame_png(self.out / f"v{v:05d}.png")
                    shots += 1
                if hashes:
                    hashes.write(f"{v} {b.lib.bf_frame_hash():016x} {b.lib.bf_audio_hash():016x} "
                                 f"{b.lib.bf_ram_hash():016x}\n")
                if ramlog:
                    ramlog.write(" ".join([str(v)] + [str(b.u32(a)) for a in logaddrs]) + "\n")
            if self.touching:
                b.lib.bf_touch_update()
            if v in rams:
                (self.out / f"ram{v:05d}.bin").write_bytes(bytes(b.ram))
            if v in states:
                b.save_state(self.out / f"s{v:05d}.state")
        self.close_log()
        if self.touching:
            touched = (ctypes.c_uint8 * (len(b.ram) // 4)).from_address(b.lib.bf_touch_map())
            lo, hi = (STUB & 0x1FFFFF) // 4, (STUB_END & 0x1FFFFF) // 4
            n = sum(touched[lo:hi])
            nonzero = sum(1 for x in b.read(STUB, STUB_END - STUB) if x)
            self.note(f"stub region 0x{STUB:08X}-0x{STUB_END - 1:08X} in the reference: {n} words written "
                      f"after the hook sites loaded, {nonzero} bytes not zero at the end"
                      + ("" if n == 0 and nonzero == 0 else " (THE STUB'S REGION IS IN USE: check the log)"))
        for f in (ramlog, hashes, audio_idx):
            if f:
                f.close()
        if args.audio:
            b.audio_file(None)
        took = time.monotonic() - t0
        self.note(f"{b.frame} vsyncs in {took:.0f} s, {shots} pictures, {self.records} log records; "
                  f"{self.shadows} frames also ran hooked: {len(self.perturbed)} of them showed another "
                  f"picture there{' (first ' + str(self.perturbed[:5]) + ')' if self.perturbed else ''}, "
                  f"{self.ram_differs} ended with other user RAM (the kernel's TCB holds the registers the "
                  "hooks used)")
        if self.late_traps:
            self.note(f"{self.late_traps} vblank interrupt(s) the game held off: the records after them "
                      "are timed from where the vblank was")
        if self.bios_misses:
            self.note(f"{self.bios_misses} vblank(s) the BIOS pad/card handler did not serve (p lines)")
        if self.forced_waits or self.synthesized:
            self.note(f"{self.forced_waits} waiting VSync call(s) spliced and {self.synthesized} written because "
                      "the reference's LIBETC state had a wait the copies' records lacked")
        if self.spliced or self.unspliced:
            self.note(f"{self.spliced} event(s) the copies made only after their frame's end, spliced back"
                      + (": " + ", ".join(self.notes_spliced[:40]) if self.spliced else "")
                      + (" ..." if len(self.notes_spliced) > 40 else "")
                      + f"; {self.unspliced} run-on event(s) left out (the copy's game went its own way)")
        b.close()
        if self.h:
            self.h.close()
        self.check_cards(rundir, content)
        shutil.rmtree(rundir, ignore_errors=True)
        with open(self.out / "run.txt", "w") as f:
            f.write(json.dumps({"ver": self.ver, "disc": args.disc, "pad": str(args.pad), "stop": args.stop,
                                "from": args.start_vsync, "core": str(b.core), "vsync_log": bool(args.vsync_log),
                                "start": self.env, "options": {k: v for k, (v, _) in CORE_OPTIONS.items()},
                                "option_overrides": args.option, "perturbed": self.perturbed}, indent=1) + "\n")
            f.write("\n".join(self.notes) + "\n")
        return 0


# --------------------------------------------------------------- compare --

class RaceCases:
    """Explicit RAM/VRAM pairs to replay, never an image-based allowlist.

    JSON: {"schema": 1, "region": "US", "frames": {"1927": {
      "mode": "worldmap", "ram_a": "ram01924.bin", "ram_b": "ram01926.bin",
      "vram": "vram01925.bin"}}}. Paths are relative to the JSON file.
    Other modes: battle, menu, field. An explicit head/head_b/env_at triple
    replaces mode and also works for JP. Optional trace supplies transfers
    and the draw environment from the port's XG_GPU_TRACE.
    """

    def __init__(self, path: Path, timeout: float):
        self.path, self.timeout = path.resolve(), timeout
        data = json.loads(self.path.read_text())
        if data.get("schema") != 1 or data.get("region") not in ("US", "JP"):
            raise ValueError("race cases need schema 1 and region US or JP")
        self.region = data["region"]
        self.frames = data["frames"]
        if not isinstance(self.frames, dict) or timeout <= 0:
            raise ValueError("race frames must be an object and timeout positive")

    @staticmethod
    def accepted(report, different):
        """Both reconstructions must match exactly and explain this comparison."""
        if not isinstance(report, dict) or "error" in report or different <= 0:
            return False
        counts = [report.get(k) for k in ("shared", "plain_vs_port_px", "race_vs_beetle_px",
                                          "plain_vs_beetle_px", "port_vs_beetle_px")]
        return (all(type(n) is int and n >= 0 for n in counts) and counts[0] > 0
                and str(report.get("race_model", "")).startswith("race@0x")
                and report.get("plain_vs_port_px") == 0
                and report.get("race_vs_beetle_px") == 0
                and report.get("plain_vs_beetle_px") == different
                and report.get("port_vs_beetle_px") == different)

    def check(self, v, ref, pc, crop, shape, deep):
        entry = self.frames.get(str(v))
        if entry is None:
            return None
        if deep:
            return {"error": "draw-race replay supports 15-bit pictures only"}
        try:
            if not isinstance(entry, dict):
                raise ValueError("race frame must be an object")
            command = [sys.executable, str(Path(__file__).with_name("beetle_race.py")),
                       "--pic", str(ref.resolve()), "--port-frame", str(pc.resolve()),
                       "--crop", ",".join(map(str, crop)), "--h", str(shape[0])]
            inputs = {"reference": ref, "port": pc}
            for name in ("ram_a", "ram_b", "vram", "trace"):
                if name == "trace" and name not in entry:
                    continue
                path = (self.path.parent / entry[name]).resolve()
                inputs[name] = path
                command.extend(["--" + name.replace("_", "-"), str(path)])
            mode = entry.get("mode")
            if mode is not None:
                if self.region != "US" or mode not in ("worldmap", "battle", "menu", "field"):
                    raise ValueError("automatic rendering-context addresses support US only")
                command.append("--" + mode)
            else:
                for name in ("head", "head_b", "env_at"):
                    value = entry[name]
                    value = int(value, 0) if isinstance(value, str) else int(value)
                    command.extend(["--" + name.replace("_", "-"), hex(value)])
            if "trace_from" in entry:
                command.extend(["--trace-from", str(int(entry["trace_from"]))])
            # A bad OT or a stuck GPU workload cannot stall the whole sweep.
            result = subprocess.run(command, capture_output=True, text=True, timeout=self.timeout)
            if result.returncode:
                raise ValueError(f"replay exited {result.returncode}: {result.stderr[-1000:].strip()}")
            report = json.loads(result.stdout)
            if not isinstance(report, dict):
                raise ValueError("replay result is not an object")
            import hashlib
            report["inputs"] = {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                                for name, path in inputs.items()}
            return report
        except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as exc:
            return {"error": str(exc)}


def cmd_compare(args) -> int:
    """Beetle's pictures (REF/v<vsync>.png, the whole TV frame) against the port's frames
    (PC/frame_<vblank>.png, the display area, written when it changes): for each Beetle
    vsync v, the port frame on screen at vblank v - offset, compared inside the display area
    (found once per pair of sizes). 15-bit pictures compare at 5 bits per channel (Beetle
    shows v<<3, the port v<<3|v>>2), 24-bit ones at 8. A differing picture that the port
    shows exactly up to --search vblanks earlier or later counts as shifted (timing), the
    rest as different content. A different picture each of whose differing pixels the port
    shows in its next picture (or in the picture before last, the buffer's old contents)
    counts as mixed as well, as a diagnostic hint. With --race-cases, replay the specified
    native RAM/VRAM pair: accept a race only when the plain OT matches the port exactly and
    a shared-primitive switch reproduces Beetle exactly. Pixel mixing alone is insufficient."""
    import bisect
    import hashlib
    import numpy as np
    from PIL import Image

    refs = sorted((int(p.stem[1:]), p) for p in args.ref.glob("v*.png")
                  if re.fullmatch(r"v\d+", p.stem))
    pcs = sorted((int(p.stem[6:]), p) for p in args.pc.glob("frame_*.png"))
    pcv = [v for v, _ in pcs]
    pc_path = dict(pcs)
    race_cases = RaceCases(args.race_cases, args.race_timeout) if args.race_cases else None
    offset = args.offset
    if offset is None:
        log = (args.port_log or args.pc.parent / "port.log")
        m = re.search(r"log vblank (\d+) = vblank (\d+) here", log.read_text(errors="replace")) if log.exists() else None
        if not m:
            raise SystemExit("compare: pass --offset (or a port log with the slowdown's 'log vblank N = vblank M here')")
        # The log's vblanks and the port's count the same interrupts, but the pictures
        # are named differently: Beetle's v<N> is the field scanned after vblank N
        # (retro_run N starts with that interrupt), the port's frame_<N> the one it
        # presents at vblank N, scanned before it. So frame_<N + 1> shows v<N>.
        offset = int(m.group(1)) - int(m.group(2)) - 1
    lo = args.start if args.start is not None else (refs[0][0] if refs else 0)
    hi = args.end if args.end is not None else (refs[-1][0] if refs else 0)
    refs = [(v, p) for v, p in refs if lo <= v <= hi]

    def image(p):
        return np.asarray(Image.open(p).convert("RGB"), dtype=np.uint8)

    def digest(x, deep):
        return hashlib.blake2b((x if deep else x >> 3).tobytes(), digest_size=12).digest()

    pc_info: dict = {}  # port vblank -> (shape, 5-bit digest, 8-bit digest)

    def pc_at(vb):
        k = bisect.bisect_right(pcv, vb) - 1
        if k < 0:
            return None, None
        v = pcs[k][0]
        if v not in pc_info:
            b = image(pcs[k][1])
            pc_info[v] = (b.shape, digest(b, False), digest(b, True))
        return v, pc_info[v]

    # Where the display area sits in Beetle's uncropped frame (crop_overscan disabled), found
    # by search for other sizes: the search needs a picture that differs little.
    crops: dict = {((240, 350, 3), (216, 320, 3)): (15, 10), ((240, 350, 3), (224, 320, 3)): (15, 8),
                   ((240, 350, 3), (240, 320, 3)): (15, 0)}
    if args.crop:
        crops = {}
    tries: dict = {}

    def find_crop(a, b):
        bh, bw = b.shape[:2]
        if a.shape[0] < bh or a.shape[1] < bw:
            return None
        deep = bool((a & 7).any())
        found = []
        for dy in range(a.shape[0] - bh + 1):
            for dx in range(a.shape[1] - bw + 1):
                x = a[dy:dy + bh, dx:dx + bw]
                n = int(((x != b) if deep else ((x >> 3) != (b >> 3))).any(axis=2).sum())
                found.append((n, dx, dy))
        found.sort()
        # the one place where this picture matches clearly better than anywhere else
        if len(found) < 2 or found[0][0] > bw * bh // 50 or found[1][0] < found[0][0] + bw * bh // 20:
            return None
        return found[0][1:]

    ref_cache: dict = {}

    def ref_digest(v, p, shape):
        """Beetle picture v cropped to the display area of a port frame of this shape."""
        key = (v, shape)
        if key not in ref_cache:
            c = crops.get((ref_shapes[v], shape))
            if c is None:
                return None
            a = image(p)
            deep = bool((a & 7).any())
            x = a[c[1]:c[1] + shape[0], c[0]:c[0] + shape[1]]
            ref_cache[key] = (deep, digest(x, deep))
        return ref_cache[key]

    ref_shapes: dict = {}

    tried: set = set()

    def ensure_crop(v, p, pv):
        if v not in ref_shapes:
            with Image.open(p) as picture:
                w, h = picture.size
            ref_shapes[v] = (h, w, 3)
        shape = pc_info[pv][0]
        key = (ref_shapes[v], shape)
        if key not in crops:
            if args.crop:
                crops[key] = tuple(int(x) for x in args.crop.split(","))
                return True
            if (v, pv) in tried or tries.get(key, 0) >= 40:
                return False
            tried.add((v, pv))
            tries[key] = tries.get(key, 0) + 1
            b = image(pc_path[pv])
            if ((b >> 3) != 0).any(axis=2).mean() < 0.3:
                return False  # too dark to place it
            c = find_crop(image(p), b)
            if c is None:
                return False
            crops[key] = c
        return True

    def same(v, p, pv):
        d = ref_digest(v, p, pc_info[pv][0])
        if d is None:
            return None
        deep, h = d
        return h == pc_info[pv][2 if deep else 1]

    def mixed(x, mask, pv, deep):
        """A pixel-mixing hint, returning the adjacent picture offsets, or None.
        This can also describe a renderer bug; it never establishes an accepted race."""
        k = bisect.bisect_left(pcv, pv)
        cands = [(1, k + 1), (-2, k - 2)]
        left = mask.copy()
        used = []
        for name, j in cands:
            if not 0 <= j < len(pcs):
                continue
            q = image(pcs[j][1])
            if q.shape != x.shape:
                continue
            hit = left & ~(((x != q) if deep else ((x >> 3) != (q >> 3))).any(axis=2))
            if hit.any():
                used.append(name)
                left &= ~hit
            if not left.any():
                return used
        return None

    for v, p in refs[::5]:  # the display area, from the first pictures clear enough to show it
        pv, info = pc_at(v - offset)
        if info is not None:
            ensure_crop(v, p, pv)

    if args.offset_search:
        # the offset that makes the most sampled pictures equal (a segment of a run whose
        # replay moved its offset before it)
        sample = refs[::max(1, len(refs) // 400)]
        best = None
        for o in range(offset - args.offset_search, offset + args.offset_search + 1):
            n = 0
            for v, p in sample:
                pv, info = pc_at(v - o)
                if info is not None and ensure_crop(v, p, pv):
                    n += bool(same(v, p, pv))
            if best is None or n > best[0]:
                best = (n, o)
        offset = best[1]

    rows = []
    for v, p in refs:
        pv, info = pc_at(v - offset)
        if info is None:
            rows.append({"v": v, "pc": None, "diff": -1})
            continue
        ensure_crop(v, p, pv)
        eq = same(v, p, pv)
        if eq is None:
            rows.append({"v": v, "pc": pv, "diff": -2})
            continue
        row = {"v": v, "pc": pv, "diff": 0}
        if not eq:
            c = crops[(ref_shapes[v], info[0])]
            a = image(p)
            b = image(pc_path[pv])
            h, w = info[0][:2]
            x = a[c[1]:c[1] + h, c[0]:c[0] + w]
            deep = bool((a & 7).any())
            mask = ((x != b) if deep else ((x >> 3) != (b >> 3))).any(axis=2)
            ys, xs = np.nonzero(mask)
            row.update(diff=int(mask.sum()), first=[int(xs[0]), int(ys[0])],
                       box=[int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())], crop=list(c))
            for d in sorted(range(-args.search, args.search + 1), key=abs):
                qv, q = pc_at(v - offset + d)
                if d and q is not None and qv != pv and same(v, p, qv):
                    row["shift"] = d
                    break
            if "shift" not in row:
                mix = mixed(x, mask, pv, deep)
                if mix:
                    row["mixed"] = mix
                if race_cases:
                    proof = race_cases.check(v, p, pc_path[pv], c, info[0], deep)
                    if proof is not None:
                        row["race"] = proof
                        if race_cases.accepted(proof, row["diff"]):
                            row["accepted_race"] = True
        rows.append(row)

    compared = [r for r in rows if r["diff"] >= 0]
    equal = [r for r in compared if r["diff"] == 0]
    other = [r for r in compared if r["diff"] > 0]
    shifted = [r for r in other if "shift" in r]
    accepted = [r for r in other if r.get("accepted_race")]
    content = [r for r in other if "shift" not in r and not r.get("accepted_race")]
    mixed_rows = [r for r in content if "mixed" in r]
    shifts: dict = {}
    for r in shifted:
        shifts[r["shift"]] = shifts.get(r["shift"], 0) + 1
    runs = []
    for r in content:
        if runs and r["v"] <= runs[-1][1] + 2:
            runs[-1][1] = r["v"]
            runs[-1][2] = max(runs[-1][2], r["diff"])
            runs[-1][3] += "mixed" in r
        else:
            runs.append([r["v"], r["v"], r["diff"], int("mixed" in r)])
    summary = {"offset": offset, "vsyncs": [lo, hi], "compared": len(compared), "equal": len(equal),
               "equal_shifted": len(shifted), "shifts": dict(sorted(shifts.items())),
               "accepted_races": len(accepted),
               "content_different": len(content), "first_content_divergence": content[0]["v"] if content else None,
               "content_mixed": len(mixed_rows),
               "first_unexplained": content[0]["v"] if content else None,
               "no_port_frame": sum(1 for r in rows if r["diff"] == -1),
               "no_display_area": sum(1 for r in rows if r["diff"] == -2),
               "crops": {f"{k[0][1]}x{k[0][0]}->{k[1][1]}x{k[1][0]}": list(c) for k, c in crops.items()}}
    if content:
        worst = max(content, key=lambda r: r["diff"])
        summary["worst"] = worst
        if args.pictures:
            args.pictures.mkdir(parents=True, exist_ok=True)
            picks = [content[0]] + sorted(content, key=lambda r: -r["diff"])[:args.worst]
            for r in picks:
                a = image(args.ref / f"v{r['v']:05d}.png")
                b = image(args.pc / f"frame_{r['pc']:06d}.png")
                dx, dy = r["crop"]
                h, w = b.shape[:2]
                x = a[dy:dy + h, dx:dx + w]
                deep = bool((a & 7).any())
                m = ((x != b) if deep else ((x >> 3) != (b >> 3))).any(axis=2)
                dimg = np.zeros_like(b)
                dimg[m] = (255, 0, 255)
                Image.fromarray(np.concatenate([x, b, dimg], axis=1)).save(
                    args.pictures / f"v{r['v']:05d}_pc{r['pc']:06d}_{r['diff']}px.png")
    summary["content_runs"] = runs[:300]
    if args.json:
        args.json.write_text(json.dumps({"summary": summary, "rows": rows}))
    print(json.dumps(summary, indent=1))
    return 0


def cmd_logdiff(args) -> int:
    """Walk the m and v records of a reference VSync log and the port's own
    (XG_SLOWDOWN_LOG) from the first common mode start: where the port's VSync
    calls go later than the log's (the replay moved its offset), or the two
    make other calls."""
    def load(path: Path) -> list[tuple]:
        out = []
        for line in path.read_text().splitlines():
            f = line.split()
            if not f or f[0] not in ("m", "v"):
                continue
            n = int(f[4]) if f[0] == "v" and len(f) > 4 else 1
            out.append((f[0], int(f[1]), float(f[2]), n))
        return out

    ref, pc = load(args.ref), load(args.pc)
    try:
        ri = next(i for i, r in enumerate(ref) if r[0] == "m")
        pi = next(i for i, r in enumerate(pc) if r[0] == "m" and r[1] == ref[ri][1])
    except StopIteration:
        raise SystemExit("logdiff: no common mode start")
    off = ref[ri][2] - pc[pi][2] if args.offset is None else args.offset
    print(f"offset {off:.0f} (log vblank - port vblank) from mode {ref[ri][1]}")
    prev, shown = 0.0, 0
    while ri < len(ref) and pi < len(pc) and shown < args.max:
        r, p = ref[ri], pc[pi]
        if (r[0], r[1]) != (p[0], p[1]):
            shown += 1
            for k in range(1, 30):
                if pi + k < len(pc) and (pc[pi + k][0], pc[pi + k][1]) == (r[0], r[1]):
                    print(f"log {r[2]:.3f}: the port makes {k} more call(s) first: "
                          + ", ".join(f"{x[0]}({x[1]}) at {x[2] + off:.3f}" for x in pc[pi:pi + k][:6]))
                    pi += k
                    break
                if ri + k < len(ref) and (ref[ri + k][0], ref[ri + k][1]) == (p[0], p[1]):
                    print(f"log {r[2]:.3f}: the PS1 makes {k} more call(s) first: "
                          + ", ".join(f"{x[0]}({x[1]}) at {x[2]:.3f}" for x in ref[ri:ri + k][:6]))
                    ri += k
                    break
            else:
                print(f"log {r[2]:.3f}: {r[0]}({r[1]}) where the port has {p[0]}({p[1]}) at {p[2] + off:.3f}")
                ri += 1
                pi += 1
            continue
        late = int(p[2]) + off - int(r[2])
        if abs(late - prev) >= 0.5:
            print(f"log {r[2]:.3f}: {r[0]}({r[1]}) {late:+.0f} vblanks on the port (was {prev:+.0f})")
            prev = late
            shown += 1
        ri += 1
        pi += 1
    return 0


def cmd_options(args) -> int:
    for k, (v, why) in CORE_OPTIONS.items():
        print(f"{k} = {v}    # {why}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture", help="a run from power-on")
    c.add_argument("out", type=Path)
    c.add_argument("pad", type=Path)
    c.add_argument("stop", type=int, help="last vsync (from power-on)")
    c.add_argument("--ver", default="us", choices=["us", "jp"])
    c.add_argument("--disc", type=int, default=1, choices=[1, 2])
    c.add_argument("--content", type=Path, help="another disc image (.cue) instead of bin/'s")
    c.add_argument("--core", type=Path)
    c.add_argument("--sysdir", help="the BIOS directory (never copied)")
    c.add_argument("--every", type=int, default=30, help="picture period in vsyncs (0: none)")
    c.add_argument("--from", dest="start_vsync", type=int, default=0, help="first vsync with pictures and logs")
    c.add_argument("--audio", action="store_true", help="audio.pcm from power-on")
    c.add_argument("--states", default="", help="vsyncs that get a save state")
    c.add_argument("--load-state", type=Path, help="start from this state instead of power-on")
    c.add_argument("--load-frame", type=int, default=0, help="the vsync that state was taken at")
    c.add_argument("--ram", default="", help="vsyncs that get a main RAM dump")
    c.add_argument("--vram", default="", help="vsyncs that get a VRAM dump")
    c.add_argument("--log", default="", help="RAM addresses (u32) logged every vsync from --from")
    c.add_argument("--vsync-log", action="store_true", help="vsyncs.txt for the slowdown replay")
    c.add_argument("--no-hooks", action="store_true", help="never run a hooked copy (the reference for the hooks' proof)")
    c.add_argument("--hashes", action="store_true",
                   help="hashes.txt: picture, audio and RAM (hooks left out) hashes every vsync from --from")
    c.add_argument("--start", action="append", default=[], metavar="XG_OPTION=VALUE",
                   help="a start option (XG_BOOT_MODE, XG_FIELD, XG_PRESET, XG_PARTY, XG_GAMESTATE)")
    c.add_argument("--option", action="append", default=[], metavar="KEY=VALUE", help="override a core option")
    c.add_argument("--hw", action="store_true",
                   help="the Beetle PSX HW core (--core, $XG_BEETLE_HW_CORE or RetroArch's) with its Vulkan renderer "
                        "(a headless context; $XG_BEETLE_VK_DEVICE picks the GPU by name, default Intel)")
    c.add_argument("--texture-dump", action="store_true",
                   help="--hw with the core's texture tracking and dumping: the dump in OUT/textures")
    c.add_argument("--texture-replace", type=Path, metavar="DIR",
                   help="--hw with the core's texture replacement from DIR (a Beetle PSX HW replacements folder)")
    c.add_argument("--card1", type=Path, help="a memory card image (128 KB) in slot 1 (default: a fresh card)")
    c.add_argument("--card2", type=Path, help="a memory card image in slot 2")
    c.add_argument("--cards-out", type=Path, help="copy the reference's cards here after the run")
    c.add_argument("--verbose", action="store_true")
    m = sub.add_parser("compare", help="Beetle pictures against the port's frames")
    m.add_argument("ref", type=Path, help="a capture's directory (v<vsync>.png)")
    m.add_argument("pc", type=Path, help="the port's XG_DUMP_FRAMES directory (frame_<vblank>.png)")
    m.add_argument("--offset", type=int, help="Beetle vsync - port vblank (default: from the port's log: its "
                   "'log vblank N = vblank M here', N - M - 1)")
    m.add_argument("--port-log", type=Path, help="the port's stderr (default PC/../port.log)")
    m.add_argument("--from", dest="start", type=int)
    m.add_argument("--to", dest="end", type=int)
    m.add_argument("--json", type=Path, help="every row as JSON")
    m.add_argument("--pictures", type=Path, help="write the worst frames here (Beetle | port | difference)")
    m.add_argument("--worst", type=int, default=3)
    m.add_argument("--crop", help="DX,DY: the display area's place in Beetle's frame (default: known or searched)")
    m.add_argument("--offset-search", type=int, default=0, metavar="W",
                   help="use the offset within W of the given one that makes the most sampled pictures equal")
    m.add_argument("--search", type=int, default=8,
                   help="a differing picture that the port shows up to this many vblanks earlier or later counts "
                        "as shifted (timing), not as different content")
    m.add_argument("--race-cases", type=Path,
                   help="JSON of per-vsync RAM/VRAM pairs for exact native draw-race replay (see RaceCases)")
    m.add_argument("--race-timeout", type=float, default=30,
                   help="seconds allowed for each draw-race replay (default 30)")
    d = sub.add_parser("logdiff", help="where the port's own VSync log departs from a reference log")
    d.add_argument("ref", type=Path, help="the reference log (a capture's vsyncs.txt)")
    d.add_argument("pc", type=Path, help="the port's XG_SLOWDOWN_LOG of the replay")
    d.add_argument("--offset", type=float, help="log vblank - port vblank (default: from the first mode start)")
    d.add_argument("--max", type=int, default=40, help="stop after this many findings")
    sub.add_parser("options", help="the core options and why")
    sub.add_parser("build", help="build beetle_front.so")
    args = ap.parse_args(argv)
    if args.cmd == "capture":
        if args.vsync_log and args.no_hooks:
            ap.error("--vsync-log needs the hooks")
        if args.vram and args.no_hooks:
            ap.error("--vram needs the hooks")
        return Capture(args).run()
    if args.cmd == "compare":
        return cmd_compare(args)
    if args.cmd == "options":
        return cmd_options(args)
    if args.cmd == "logdiff":
        return cmd_logdiff(args)
    if args.cmd == "build":
        print(build_frontend(force=True))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
