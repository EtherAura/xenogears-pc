#!/usr/bin/env python3
"""Inspect retail heap headers and replay address allocation, without host pointers.

This is an investigation model of this project's src/main/heap.c, not a native
allocation-size translator or a replacement allocator. Requests must already
use PS1 byte sizes. No emulator source, game bytes or per-field addresses are
included. See wallslide_probe.py --heap for read-only Beetle observations.

The replay command accepts black-box debugger observations, one per line:
    function decimal_vblank hex_a0 hex_a1 hex_ra [hex_result]
Functions: initHeap, resetHeapBegin, resetHeapEnd, setHeapUser, resetHeapTraceState,
resetMemoryAllocStats, mergeFreeHeapBlocks,
allocateHeap, freeHeap, shrinkAllocation, flagAllocation, unflagAllocation,
unpinAllocation. The reset begin/end bracket its nested freeHeap calls;
rebasing happens at the end. The capture must resolve MIPS load delays
before reading allocateHeap's arguments (entry + 4, not entry).

    python tools/retail_heap.py EVENTS.txt [--allow-unlinked-frees]

An unlinked free accesses a stale header outside the active chain. Its result
cannot be established from the active-chain model; accepting it requires the
explicit flag and is reported separately, never counted as a modeled return.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import struct
from typing import Callable

RAM_START, HEAP_END, HEADER = 0x80000000, 0x801FC000, 8


@dataclass(frozen=True)
class Header:
    data: int
    next: int
    user: int
    pinned: bool
    type: int
    caller: int

    @property
    def size(self) -> int:
        return 0 if self.user == 1 else self.next - self.data - HEADER


def read_heap(read: Callable[[int, int], bytes], first: int,
              end: int = HEAP_END, limit: int = 4096) -> list[Header]:
    """Read a validated forward MCB chain; never write to emulated memory."""
    if not RAM_START + HEADER <= first <= end or first & 3:
        raise ValueError(f"invalid heap data address {first:#x}")
    result = []
    data = first
    for _ in range(limit):
        raw = read(data - HEADER, HEADER)
        if len(raw) != HEADER:
            raise ValueError("short heap header read")
        nxt, bits = struct.unpack("<II", raw)
        block = Header(data, nxt, (bits >> 21) & 15, bool(bits & (1 << 25)),
                       bits >> 26, RAM_START + ((bits & 0x1FFFFF) << 2))
        result.append(block)
        if block.user == 1:
            if data != end or nxt != end:
                raise ValueError("heap end marker at the wrong address")
            return result
        if nxt & 3 or not data + HEADER <= nxt <= end:
            raise ValueError(f"invalid next block {nxt:#x} after {data:#x}")
        data = nxt
    raise ValueError("heap chain exceeds block limit")


@dataclass
class Block:
    data: int
    next: int
    user: int = 0
    pinned: bool = False

    @property
    def size(self) -> int:
        return self.next - self.data - HEADER


class AddressHeap:
    """Original allocator's address choices; owns no game or host memory.

    Blocks include the end sentinel. Adjacent free blocks merge lazily, before
    allocation or heap reset. State consists solely of JSON-compatible values
    so a replay can resume independently of Python object or host addresses.
    Payload writes, stale headers and allocation-size conversion are outside
    this model. In particular, passing native sizeof values is incorrect.
    """

    def __init__(self, start: int, end: int):
        start, end = start & ~3, end & ~3
        if not RAM_START <= start <= end - 2 * HEADER or end > HEAP_END:
            raise ValueError("invalid heap bounds")
        self.blocks = [Block(start + HEADER, end), Block(end, end, 1)]
        self.user = 10
        self.dirty = False

    def state(self) -> dict:
        return dict(blocks=[asdict(b) for b in self.blocks], user=self.user, dirty=self.dirty)

    @classmethod
    def restore(cls, state: dict) -> AddressHeap:
        heap = cls(state["blocks"][0]["data"] - HEADER, state["blocks"][-1]["data"])
        heap.blocks = [Block(**b) for b in state["blocks"]]
        heap.user, heap.dirty = state["user"], state["dirty"]
        return heap

    def find(self, data: int) -> Block | None:
        return next((b for b in self.blocks[:-1] if b.data == data), None)

    def merge(self) -> None:
        i = 0
        while i < len(self.blocks) - 1:
            block, after = self.blocks[i:i + 2]
            if block.user == 0 and after.user == 0:
                block.next = after.next
                del self.blocks[i + 1]
            else:
                i += 1
        self.dirty = False

    def allocate(self, size: int, mode: int) -> int:
        if size < 0 or size > 0x7FFFFFFC:
            raise ValueError("invalid retail allocation size")
        if not 2 <= self.user <= 15:
            raise ValueError("allocation user must be an occupied owner")
        if self.dirty:
            self.merge()
        size = (size + 3) & ~3
        best = exact = None
        for i, block in enumerate(self.blocks[:-1]):
            if block.user:
                continue
            diff = block.size - size
            if diff in (0, 4):
                if mode == 1:
                    exact = i
                    continue
                block.user, block.pinned = self.user, False
                return block.data
            if diff >= 5:
                if mode not in (1, 2):
                    best = i
                    break
                if mode == 1 or best is None or block.size < self.blocks[best].size:
                    best = i
        if best is None and exact is None:
            return 0  # address failure, irrespective of the game's error policy
        if mode == 1 and exact is not None and (best is None or exact > best):
            block = self.blocks[exact]
            block.user, block.pinned = self.user, False
            return block.data
        block = self.blocks[best]
        if mode == 1:
            data = block.next - size - HEADER
            self.blocks.insert(best + 1, Block(data, block.next, self.user))
            block.next = data
            return data
        data = block.data
        nxt = data + size + HEADER
        self.blocks.insert(best + 1, Block(nxt, block.next))
        block.next, block.user, block.pinned = nxt, self.user, False
        return data

    def free(self, data: int) -> int:
        block = self.find(data)
        if block is None:
            raise ValueError(f"free of unlinked pointer {data:#x}")
        if block.pinned:
            return -1
        block.user = 0
        self.dirty = True
        return 0

    def pin(self, data: int, pinned: bool) -> None:
        block = self.find(data)
        if block is None:
            raise ValueError(f"pin of unlinked pointer {data:#x}")
        block.pinned = pinned

    def reset(self, start: int) -> None:
        for block in self.blocks[:-1]:
            if not block.pinned:
                block.user = 0
        self.dirty = True
        self.merge()
        data = (start & ~3) + HEADER
        first = self.blocks[0]
        if first.user or not RAM_START + HEADER <= data <= first.next - HEADER:
            raise ValueError("reset would overlap a surviving pinned allocation")
        first.data = data

    def shrink(self, data: int, size: int) -> int:
        if size < 0 or size & 3:
            raise ValueError("shrink size must be a nonnegative multiple of four")
        block = self.find(data)
        if block is None:
            raise ValueError(f"shrink of unlinked pointer {data:#x}")
        if block.size <= size + 2 * HEADER:
            return 0
        i, nxt = self.blocks.index(block), data + size + HEADER
        self.blocks.insert(i + 1, Block(nxt, block.next))
        block.next, self.dirty = nxt, True
        return data - HEADER


def replay(path: Path, allow_unlinked_frees: bool = False, restore_every: int = 0) -> dict:
    if restore_every < 0:
        raise ValueError("restore interval must be nonnegative")
    heap = None
    pending_reset = None
    checks, events, unlinked_frees, unlinked_shrinks = 0, 0, [], []
    # Old Redux Support.File logs sometimes contain literal backslash-n.
    for line in path.read_text().replace("\\n", "\n").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, frame, a0, a1, ra, *result = line.split()
        a0, a1 = int(a0, 16), int(a1, 16)
        events += 1
        if heap is not None and restore_every and events % restore_every == 0:
            heap = AddressHeap.restore(json.loads(json.dumps(heap.state())))
        if name == "initHeap":
            heap = AddressHeap(a0, a1)
            continue
        if heap is None:
            raise ValueError("event before heap initialization")
        if name == "resetHeapBegin":
            if pending_reset is not None:
                raise ValueError("nested reset")
            pending_reset = a0
        elif name == "resetHeapEnd":
            if pending_reset is None or pending_reset != a0:
                raise ValueError("unmatched reset end")
            heap.reset(pending_reset)
            pending_reset = None
        elif name in ("setHeapUser", "resetMemoryAllocStats"):
            heap.user = a0
        elif name == "resetHeapTraceState":
            heap.user = 10
        elif name == "mergeFreeHeapBlocks":
            heap.merge()
        elif name in ("flagAllocation", "unflagAllocation", "unpinAllocation"):
            heap.pin(a0, name == "flagAllocation")
        elif name in ("allocateHeap", "freeHeap", "shrinkAllocation"):
            if len(result) != 1:
                raise ValueError(f"missing result at event {events}")
            expected = int(result[0], 16)
            if name == "freeHeap" and heap.find(a0) is None and allow_unlinked_frees:
                unlinked_frees.append(dict(frame=int(frame), data=a0, result=expected))
                continue
            if name == "shrinkAllocation" and heap.find(a0) is None:
                # initFieldData calls initModel5 on an embedded model block.
                # That reads payload bytes as a header; MCB topology alone
                # cannot prove the result, even when retail returned zero.
                unlinked_shrinks.append(dict(frame=int(frame), data=a0, result=expected))
                if expected != 0:
                    raise ValueError("unmodeled shrink changed the heap")
                continue
            got = (heap.allocate(a0, a1) if name == "allocateHeap" else
                   heap.free(a0) if name == "freeHeap" else heap.shrink(a0, a1))
            if got & 0xFFFFFFFF != expected:
                raise ValueError(f"{name} at {frame}: model {got & 0xFFFFFFFF:#x}, retail {expected:#x}")
            checks += 1
        else:
            raise ValueError(f"unknown event {name}")
    if heap is None:
        raise ValueError("empty event trace")
    if pending_reset is not None:
        raise ValueError("unfinished reset")
    return dict(events=events, modeled_returns=checks, unlinked_frees=unlinked_frees,
                unlinked_shrinks=unlinked_shrinks, state=heap.state())


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("events", type=Path)
    ap.add_argument("--allow-unlinked-frees", action="store_true")
    ap.add_argument("--restore-every", type=int, default=0, metavar="EVENTS",
                    help="round-trip model state through JSON at this interval (0 disables)")
    args = ap.parse_args()
    print(json.dumps(replay(args.events, args.allow_unlinked_frees, args.restore_every), indent=2))


if __name__ == "__main__":
    main()
