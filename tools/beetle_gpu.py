#!/usr/bin/env python3
"""Replay captured GP0 commands through a black-box Beetle core.

Our own mailbox workload and the existing libretro frontend, with no game
or emulator code. Requires the local MIPS assembler and the same external
core/BIOS as tools/beetle.py. Generated binaries live outside the checkout.
"""
import os
import struct
import subprocess
from pathlib import Path

from beetle import Beetle as Frontend, ROOT, CACHE

HERE = Path(__file__).resolve().parent
REPLAY_CACHE = Path(os.environ.get("XG_BEETLE_GPU_CACHE", CACHE / "gpu-replay"))

MAILBOX = 0x20000
OPS = MAILBOX + 16
READBUF = 0x100000  # physical; 1 MB up to the end of RAM


def command_length(op: int) -> int:
    """GP0 command lengths, psx-spx "GPU Render Polygon/Line/Rectangle Commands",
    "GPU Memory Transfer Commands"."""
    if 0x20 <= op < 0x40:
        n = 4 if op & 0x08 else 3
        per = 1 + (1 if op & 0x04 else 0)
        return 1 + n * per + ((n - 1) if op & 0x10 else 0)
    if 0x40 <= op < 0x60:
        if op & 0x08:
            return -1
        return 4 if op & 0x10 else 3
    if 0x60 <= op < 0x80:
        return 2 + (1 if op & 0x04 else 0) + (1 if ((op >> 3) & 3) == 0 else 0)
    if 0x80 <= op < 0xA0:
        return 4
    if 0xA0 <= op < 0xE0:
        return 3
    if op == 0x02:
        return 3
    return 1


class Ops:
    def __init__(self) -> None:
        self.words: list[int] = []
        self.reads: list[tuple[int, int, int, int, int]] = []  # x, y, w, h, physical address
        self.next_read = READBUF

    def gp0(self, words: list[int]) -> None:
        """GP0 words, split into commands; A0h data goes by DMA."""
        i = 0
        while i < len(words):
            op = words[i] >> 24
            n = command_length(op)
            if n < 0:  # polyline: up to the terminator
                j = i + 2
                gouraud = op & 0x10
                while j < len(words):
                    if (words[j] & 0xF000F000) == 0x50005000:
                        break
                    j += 2 if gouraud else 1
                n = j + 1 - i
            pkt = words[i:i + n]
            self.words.append(0x01000000 | len(pkt))
            self.words.extend(pkt)
            i += n
            if 0xA0 <= op < 0xC0:
                wh = pkt[2]
                w = ((wh & 0xFFFF) - 1 & 0x3FF) + 1
                h = ((wh >> 16) - 1 & 0x1FF) + 1
                count = (w * h + 1) // 2
                data = words[i:i + count]
                i += count
                self.dma(data)

    def dma(self, data: list[int]) -> None:
        data = list(data) + [0] * (-len(data) % 16)  # padding: GP0(00h) NOPs
        self.words.append(0x02000000 | len(data))
        self.words.extend(data)

    def load(self, x: int, y: int, w: int, h: int, pixels: list[int]) -> None:
        """CPU to VRAM (GP0 A0h) of w*h halfwords."""
        data = list(pixels)
        if len(data) & 1:
            data.append(0)
        words = [data[k] | data[k + 1] << 16 for k in range(0, len(data), 2)]
        self.words.extend([0x01000003, 0xA0000000, (y << 16) | x, (h << 16) | w])
        self.dma(words)

    def gp1(self, word: int) -> None:
        self.words.extend([0x04000000, word])

    def spin(self, n: int) -> None:
        self.words.append(0x05000000 | n)

    def read(self, x: int, y: int, w: int, h: int) -> int:
        """GP0(C0h) of a rectangle into RAM; returns its index in run()'s result."""
        count = (w * h + 1) // 2
        addr = self.next_read
        if addr + count * 4 > 0x200000:
            raise ValueError("read buffer full")
        self.words.extend([0x03000000, (y << 16) | x, (h << 16) | (w & 0x3FF if w < 1024 else 0x400),
                           count, 0x80000000 | addr])
        self.reads.append((x, y, w, h, addr))
        self.next_read += count * 4
        return len(self.reads) - 1

    def read_vram(self) -> int:
        return self.read(0, 0, 1024, 512)


def build_runner():
    REPLAY_CACHE.mkdir(parents=True, exist_ok=True)
    source = HERE / "beetle_gpu_runner.s"
    exe = REPLAY_CACHE / "runner.exe"
    if exe.exists() and exe.stat().st_mtime >= source.stat().st_mtime:
        return exe
    cross = str(ROOT / "tools/toolchain/bin/mips-linux-gnu-")
    obj, elf, raw = (REPLAY_CACHE / n for n in ("runner.o", "runner.elf", "runner.bin"))
    for command in ([cross + "as", "-EL", "-march=r3000", "-mtune=r3000", "-o", obj, source],
                    [cross + "ld", "-EL", "-nostdlib", "-Ttext", "0x80010000", "-e", "_start", "-o", elf, obj],
                    [cross + "objcopy", "-O", "binary", "-j", ".text", elf, raw]):
        subprocess.run(command, check=True, capture_output=True)
    code = raw.read_bytes()
    code += b"\0" * (-len(code) % 0x800)
    header = bytearray(0x800)
    header[:8] = b"PS-X EXE"
    struct.pack_into("<IIIIIIIIII", header, 0x10, 0x80010000, 0, 0x80010000, len(code),
                     0, 0, 0, 0, 0x8001FF00, 0)
    # PS-X EXE region marker, from the documented file format.
    marker = b"Sony Computer Entertainment Inc. for North America area"
    header[0x4C:0x4C + len(marker)] = marker
    exe.write_bytes(header + code)
    return exe


class Beetle:
    def __init__(self):
        self.front = Frontend(build_runner(), savedir=REPLAY_CACHE / "save")
        for _ in range(2000):
            self.front.run(10)
            if self.front.u32(MAILBOX + 8) == 0x52554E52:
                break
        else:
            self.close()
            raise RuntimeError("GPU replay workload did not start")
        self.seq = self.front.u32(MAILBOX)

    def run(self, ops, max_frames=2000):
        words = ops.words + [0]
        if OPS + 4 * len(words) > READBUF:
            raise ValueError("operation list too long")
        self.front.write(OPS, struct.pack(f"<{len(words)}I", *words))
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        self.front.put32(MAILBOX, self.seq)
        for _ in range(max_frames):
            self.front.run()
            if self.front.u32(MAILBOX + 4) == self.seq:
                return [self.front.read(addr, w * h * 2) for x, y, w, h, addr in ops.reads]
        raise RuntimeError("GPU replay workload did not finish")

    def close(self):
        self.front.close()
