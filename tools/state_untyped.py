#!/usr/bin/env python3
"""Pointers no annotation types, in every corpus case, across two layouts (xg-ety3; docs/CANONICAL_STATES.md 17.3).

    tools/state_untyped.py [--config CFG ...] [--case ID ...] [--sample N] [--jobs N] [--build]
                           [--work DIR] [--keep] [--report FILE]
    tools/state_untyped.py pair A.xgs BUILD_A B.xgs BUILD_B [--words]   one pair of states, as the scan reads it
    tools/state_untyped.py sites [BUILD]     the source's allocations alone (below)

A converted state is only as good as the writer's schema: a pointer the schema does not type is carried as
the number it was, which is the writer's address and nothing in the reader. The first canonical release's
corpus is the reference forever, so every such pointer must be typed before then. This finds them the way
14.1's census did, for every case of tools/state_corpus.py (its case list, imported) in every configuration:
two builds of one tree, A at the default link address and B 16 MiB higher, each run to the case's moment and
saved as a player saves there (XG_STATE_SLOT_SAVE: the next safe point); then every 32-bit word, at every byte
offset, that differs between the two states by exactly the layout shift and is an address in A's image, where
the converter would not move it:

  heap     a live heap block's bytes that no PC_HEAP_OF types (a block without a type, a tail past its T,
           a stretch between recorded ranges), or a typed part's member that holds no pointer (an integer,
           padding, a union view the selector does not name);
  arena    the same in the sound arena's blocks;
  static   a module variable's member that holds no pointer (the variable's type, every view of an
           unselected union, the selected view of a PC_STATE_UNION), and a .bss byte outside every variable
           (copied as it is);
  host     a host part (savestate.c XG_STATE_VAR) the same way: a typed part by its type, a raw chunk
           (no type: copied as it is) by every word, mem.scratchpad included;
  unknown  a typed pointer whose target the encoder cannot name (no block, variable, function or constant
           data there: "pointer to nothing known", kept as a number);
  stack    a typed pointer into the game stack, which a stackless load does not carry.

The stack chunks are left out (a stackless load drops them), so are free heap blocks, the heap's block
headers (the converter rebuilds them), constants (each build keeps its own) and .data bytes outside every
variable (the reader's own: jump tables).

A case shows only what its moment holds, so the scan also reads the source each scanned engine compiles
(src/ or engine/src, port/src/game, port/src/extras):

  site     an allocation (allocateHeap, allocateBattleHeap, allocateTaskPair, allocateUpdateTask,
           allocateDrawTask, the sound arena's) whose size names no type (no PC_HEAP_OF, _ONE, _NAMED) while
           the call says it holds a pointer-holding type: a sizeof/offsetof of it, a cast of the result, the
           pointer declared with it. The allocators' own calls are left out (their callers name the type).

Zero findings is the pass. A finding that is a leftover nothing reads again has a reason in
tools/state_untyped_allow.txt, one per entry (`kind | place | path | reason`), reviewed; never a blanket
ignore. An entry that matches nothing in a full run is reported as stale.

Configurations (each skipped, saying so, when either layout is missing): us-original is build/pc/schema and
build/pc/schema2 (RelWithDebInfo, the state schema; schema2 linked with -Wl,-Ttext-segment=0x1400000); the
others, us-enhanced, jp-original and jp-enhanced, are $XG_UNTYPED_LAYOUTS/<config>-a and -b (default
/mnt/Games/scratch/xg-untyped/build), configured the same way with -DXG_ENGINE / -DXG_VERSION
(work/canon/untyped_layouts.sh). A layout whose schema is older than its executable is an error (rebuild it:
the link writes the schema); --build runs cmake --build on every layout first (nice 19, as tools/yield.sh).

--sample N scans N cases per configuration (the same N for a given tree: by a hash of the case id), always
the three battles' command menus; without it, every case. Runs: XG_UNTYPED_JOBS or --jobs at a time
(default: tools/yield.sh's cap); the states and the logs of a case with findings stay in --work."""

from __future__ import annotations

import argparse
import bisect
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import state_corpus as corpus  # noqa: E402 (the cases, the runs)
import statecanon as cn  # noqa: E402 (the schema, the heap's layout)
import statecheck as sc  # noqa: E402 (the state format)

ALLOW = ROOT / "tools" / "state_untyped_allow.txt"
LAYOUTS = Path(os.environ.get("XG_UNTYPED_LAYOUTS", "/mnt/Games/scratch/xg-untyped/build"))
CONFIGS = {
    "us-original": (ROOT / "build/pc/schema", ROOT / "build/pc/schema2"),
    "us-enhanced": (LAYOUTS / "us-enhanced-a", LAYOUTS / "us-enhanced-b"),
    "jp-original": (LAYOUTS / "jp-original-a", LAYOUTS / "jp-original-b"),
    "jp-enhanced": (LAYOUTS / "jp-enhanced-a", LAYOUTS / "jp-enhanced-b"),
}
ALWAYS = ("b-foot", "b-gear", "b-boss")  # a sample keeps the battles' command menus (xg-ety3)
POINTERS = {"ptr", "fnptr", "addr", "otag", "scratch"}  # what the encoder moves (or does not carry: scratch)


class ScanError(Exception):
    pass


# ---- what a type holds at an offset ------------------------------------------------------------------


def kinds_at(s: cn.Schema, tid, mem, base: int, off: int, depth: int = 0) -> list[tuple[str, str]]:
    """(path, kind) of every view of type tid that covers byte off of a value at base in mem, as the encoder
    reads it (statecanon Encoder.value): a scalar's kind (int, float, ptr, fnptr, addr, otag, scratch), "pad"
    for a struct's padding, "bytes" for what is kept as bytes. A PC_STATE_UNION's selected view alone (the
    others' bytes are the member's raw copy); a union without a rule through every view (the most typed one
    is written last: a pointer in any of them is carried)."""
    t = s.types.get(tid)
    if t is None or depth > 40:
        return [("", "bytes")]
    k = t["k"]
    if k == "ptr":
        return [("", "fnptr" if t.get("fn") else "ptr")]
    if k in ("int", "float", "addr", "scratch", "otag"):
        return [("", k)]
    if k == "arr":
        of, n = t.get("of"), t["n"]
        if of not in s.types or n == 0:
            return [("", "bytes")]
        et = s.types[of]
        es = et["size"]
        if et["k"] == "scratch":
            return [("", "scratch")]
        if not es:
            return [("", "bytes")]
        i = off // es
        if et["k"] == "int":
            return [(f"[{i}]", "int")]
        if i >= n:
            return [("", "bytes")]
        return [(f"[{i}]" + p, kk) for p, kk in kinds_at(s, of, mem, base + i * es, off - i * es, depth + 1)]
    if k == "struct":
        ur = cn.rule_for(s, s.unions, s.unions_by_name, tid)
        for f in t["fields"]:
            if f.get("bits") is not None:
                if f["bit"] // 8 <= off < (f["bit"] + f["bits"] + 7) // 8:
                    return [("." + f["name"], "int")]
                continue
            fo, fs = f["off"], s.size(f["type"])
            if not fo <= off < fo + max(fs, 1):
                continue
            if ur is not None and f["name"] == ur["member"]:
                view = cn.selected_view(s, t, ur, mem, base)
                if view is not None:
                    rel = off - fo - view.get("off", 0)
                    pre = f".{f['name']}.{view['name']}"
                    if 0 <= rel < s.size(view["type"]):
                        return [(pre + p, kk) for p, kk in
                                kinds_at(s, view["type"], mem, base + fo + view.get("off", 0), rel, depth + 1)]
                    return [(pre, "bytes")]
            return [("." + f["name"] + p, kk) for p, kk in kinds_at(s, f["type"], mem, base + fo, off - fo, depth + 1)]
        return [(f"+{off:#x}", "pad")]
    if k == "union":
        out = []
        for f in union_views(s, tid, t):
            fo, fs = f.get("off", 0), s.size(f["type"])
            if fo <= off < fo + fs:
                out += [("." + f["name"] + p, kk) for p, kk in kinds_at(s, f["type"], mem, base + fo, off - fo, depth + 1)]
        return out or [("", "bytes")]
    return [("", "bytes")]


_views: dict = {}


def union_views(s: cn.Schema, tid, t: dict) -> list:
    """statecanon union_views, once per schema and type (it sorts by every view's size)."""
    key = (id(s), tid)
    v = _views.get(key)
    if v is None:
        v = _views[key] = cn.union_views(s, t)
    return v


INDEX = re.compile(r"\[\d+\]")


def generic(path: str) -> str:
    return INDEX.sub("[]", path)


def type_name(s: cn.Schema, tid) -> str:
    return cn.stable_name(s, tid) or s.types.get(tid, {}).get("name") or f"<type {tid}>"


# ---- one pair of states --------------------------------------------------------------------------------


@dataclass
class Finding:
    kind: str
    place: str
    path: str
    count: int = 0
    example: str = ""
    cases: set = field(default_factory=set)

    @property
    def key(self) -> tuple:
        return (self.kind, self.place, self.path)


class Pair:
    """Two states of two layouts at one moment, and what each reads where."""

    def __init__(self, a_state: Path, sa: cn.Schema, b_state: Path, sb: cn.Schema, sites: cn.Sites | None = None):
        self.sa, self.sb = sa, sb
        self.sites = sites or cn.Sites(sa)
        ha, self.ab = sc.read_state(a_state)
        hb, self.bb = sc.read_state(b_state)
        if ha["vblank"] != hb["vblank"]:
            raise ScanError(f"the layouts saved at different vblanks ({ha['vblank']}, {hb['vblank']})")
        self.vblank = ha["vblank"]
        self.ca = [(t, memoryview(self.ab)[o:o + n]) for t, o, n in sc.chunks(self.ab)]
        self.cb = [(t, memoryview(self.bb)[o:o + n]) for t, o, n in sc.chunks(self.bb)]
        self.da = {t: m for t, m in self.ca}
        self.db = {t: m for t, m in self.cb}
        shifts = Counter(sb.sections[k][0] - sa.sections[k][0] for k in sa.sections if k in sb.sections)
        if not shifts:
            raise ScanError("the two schemas share no section")
        self.shift = shifts.most_common(1)[0][0]
        if not self.shift:
            raise ScanError("the two layouts are linked at the same address")
        lo = min([lo for lo, _ in sa.sections.values()] + [f for f in sa.funcs] + [lo for lo, _ in sa.regions.values()])
        hi = max([hi for _, hi in sa.sections.values()] + [hi for _, hi in sa.regions.values()])
        self.image = (lo, hi)
        self.enc = cn.Encoder(sa)  # (its pointer(): what a typed pointer's target is)
        self.found: dict[tuple, Finding] = {}
        self.every: list | None = None  # (pair --words: every word found)

    # candidates: (offset in a, value in a) where b's word at the matching offset is a's + the shift
    def candidates(self, ma, mb, ob_delta: int = 0) -> list[tuple[int, int]]:
        n = min(len(ma), len(mb) - ob_delta) if ob_delta >= 0 else 0
        if n < 4:
            return []
        out = []
        a8 = np.frombuffer(ma, np.uint8, n)
        b8 = np.frombuffer(mb, np.uint8, n, ob_delta)
        lo, hi = self.image
        for k in range(4):
            m = (n - k) // 4
            if m <= 0:
                continue
            a = a8[k:k + 4 * m].view("<u4")
            b = b8[k:k + 4 * m].view("<u4")
            hit = ((b - a) == np.uint32(self.shift & 0xFFFFFFFF)) & (a >= lo) & (a < hi)
            for i in np.nonzero(hit)[0]:
                out.append((k + 4 * int(i), int(a[i])))
        out.sort()
        return out

    def target(self, v: int) -> str:
        """What a's address v is, for the report."""
        s = self.sa
        h = s.regions.get("heapArena")
        if h and h[0] <= v < h[1]:
            return f"heap+{v - h[0]:#x}"
        f = s.fn_identity(v)
        if f is not None:
            return f.rsplit(":", 1)[-1]
        names = s.text_syms.get(v)
        if names:
            return names[0]
        var = s.var_at(v)
        if var is not None:
            return f"{var['name']}+{v - var['addr']:#x}"
        return f"{v:#x}"

    def add(self, kind: str, place: str, path: str, example: str) -> None:
        key = (kind, place, generic(path))
        f = self.found.get(key)
        if f is None:
            f = self.found[key] = Finding(kind, place, generic(path), example=example)
        f.count += 1
        if self.every is not None:
            self.every.append(f"{kind} {place} | {path}: {example}")

    def typed(self, kinds: list, v: int, place: str, path: str, where: str) -> bool:
        """Whether the encoder moves a's word v there; a typed pointer it cannot name is a finding too."""
        ks = {k for _, k in kinds}
        if not ks & POINTERS:
            return False
        if "scratch" in ks:
            return True  # not carried at all (each build's own)
        st = self.sa.regions.get("gameStack")
        if st and st[0] <= v < st[1] and ks & {"ptr", "addr", "otag"}:
            self.add("stack", place, path, f"{where}: {v:#x} -> the game stack +{v - st[0]:#x}")
            return True
        if "fnptr" in ks and not ks & {"ptr", "addr", "otag"}:
            if self.sa.fn_identity(v) is None:
                self.add("unknown", place, path, f"{where}: function pointer {v:#x} ({self.target(v)}) to no known function")
            return True
        p = self.enc.pointer(v, quiet=True)
        if isinstance(p, dict) and "raw" in p:
            self.add("unknown", place, path, f"{where}: {v:#x} ({self.target(v)}) points at nothing the schema knows")
        return True

    # -- the heap and the sound arena --
    def scan_blocks(self, label: str, blocks_a, mem_a, base_a: int, blocks_b, mem_b, base_b: int, places) -> None:
        for x, y in zip(blocks_a, blocks_b):
            if not x.get("user") or x.get("end"):
                continue
            if x["size"] != y["size"]:
                raise ScanError(f"{label}: block sizes differ between the layouts ({x['size']}, {y['size']}): not "
                                "the same moment")
            oa, ob = x["data"] - base_a, y["data"] - base_b
            ma = mem_a[oa:oa + x["size"]]
            mb = mem_b[ob:ob + y["size"]]
            cands = self.candidates(ma, mb)
            if not cands:
                continue
            lo = [p["off"] for p in x["parts"]]
            site, tname = places(x)
            tags = {}  # a primitives part's tags (by part)
            for off, v in cands:
                i = bisect.bisect_right(lo, off) - 1
                p = x["parts"][i] if i >= 0 else None
                if p is None or off >= p["off"] + p["size"]:
                    self.add(label, site, "bytes", f"+{off:#x} = {v:#x} ({self.target(v)})")
                    continue
                rel = off - p["off"]
                spec = p["spec"]
                if spec is None:
                    # (the last 16 bytes apart: an LZSS stream's tail decodes bytes read past its source's end,
                    # the next heap block's header, there: tools/state_untyped_allow.txt)
                    tail = " at the block's end" if off >= x["size"] - 16 else ""
                    self.add(label, site, ("bytes" if tname is None else f"{tname} bytes after it") + tail,
                             f"+{off:#x} = {v:#x} ({self.target(v)})")
                    continue
                if spec in ("ptrs", "otags"):
                    self.typed([("", "ptr" if spec == "ptrs" else "otag")], v, site, spec, f"+{off:#x}")
                    continue
                if spec == "prims":
                    if i not in tags:
                        tags[i] = prim_tags(ma, p["off"], p["size"])
                    if off in tags[i]:
                        self.typed([("", "otag")], v, site, "prims", f"+{off:#x}")
                    else:
                        self.add(label, site, "primitive words", f"+{off:#x} = {v:#x} ({self.target(v)})")
                    continue
                tid = spec["tid"]
                e = bisect.bisect_right(p["eo"], rel) - 1
                rel -= p["eo"][e]
                cr = cn.counted_rule(self.sa, tid) if p["cnt"][e] is not None else None
                base = oa + p["off"] + p["eo"][e]
                if cr and rel >= cr[1]["off"]:
                    a = cr[1]
                    of = self.sa.types[a["type"]]["of"]
                    es = self.sa.size(of)
                    j = (rel - a["off"]) // es
                    ks = [(f".{a['name']}[{j}]" + q, kk) for q, kk in
                          kinds_at(self.sa, of, mem_a, base + a["off"] + j * es, (rel - a["off"]) % es)]
                else:
                    ks = kinds_at(self.sa, tid, mem_a, base, rel)
                name = type_name(self.sa, tid)
                path = ks[0][0] if ks else ""
                if not self.typed(ks, v, name, path, f"{site} +{off:#x}"):
                    self.add(label, name, path, f"{site} +{off:#x} = {v:#x} ({self.target(v)}); "
                             f"{', '.join(sorted({k for _, k in ks}))}")

    def scan_heap(self) -> None:
        sa, sb = self.sa, self.sb
        if "mem.heap" not in self.da or "mem.heap" not in self.db:
            return
        blocks_a, arena_a = heap_layout(sa, self.da, self.sites)
        blocks_b = cn.heap_blocks(sb, self.db)
        if len([b for b in blocks_a if b.get("user")]) != len([b for b in blocks_b if b.get("user")]):
            raise ScanError("the heaps have different numbers of live blocks: not the same moment")

        def place(x):
            fn, f, _line = x["site"]
            site = f"{fn} ({Path(f).name})"
            return site, (type_name(sa, x["spec"]["tid"]) if isinstance(x["spec"], dict) else None)

        self.scan_blocks("heap", blocks_a, self.da["mem.heap"], sa.regions["heapArena"][0], blocks_b,
                         self.db["mem.heap"], sb.regions["heapArena"][0], place)
        if arena_a is not None:
            arena_b = cn.arena_blocks(sb, self.db)

            def aplace(x):
                sp = x["spec"]
                return (f"arena block of type {type_name(sa, sp['tid'])}" if isinstance(sp, dict) else
                        f"arena block of type {x['pctype']:08x}" if x["pctype"] else "arena block without a type"), \
                    (type_name(sa, sp["tid"]) if isinstance(sp, dict) else None)

            self.scan_blocks("arena", arena_a["blocks"], self.da["mem." + arena_a["sec"]],
                             arena_a["base"] - arena_a["off"], arena_b["blocks"], self.db["mem." + arena_b["sec"]],
                             arena_b["base"] - arena_b["off"], aplace)

    # -- module variables --
    def scan_statics(self) -> None:
        sa, sb = self.sa, self.sb
        skip = [sa.regions[r] for r in ("heapArena", "gameStack", "pcScratchpad") if r in sa.regions]
        arena = sa.by_name.get("pcSoundArena")
        for tag, ma in self.ca:
            m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
            if not m or tag not in self.db:
                continue
            sec = f"{m.group(1)}.{m.group(2)}"
            if sec not in sa.sections or sec not in sb.sections:
                continue
            lo_a, lo_b = sa.sections[sec][0], sb.sections[sec][0]
            mb = self.db[tag]
            vars_a = sorted((v for v in sa.vars if v.get("module") == m.group(1) and lo_a <= v["addr"] < sa.sections[sec][1]),
                            key=lambda v: v["addr"])
            starts = [v["addr"] for v in vars_a]
            same = len(ma) == len(mb) and all(
                (vb := sb.by_id.get(v["id"])) is not None and vb["addr"] - lo_b == v["addr"] - lo_a for v in vars_a)
            if same:
                cands = [(o, v, o) for o, v in self.candidates(ma, mb)]
            else:  # variable by variable, each at its own place in b
                cands = []
                for v in vars_a:
                    vb = sb.by_id.get(v["id"])
                    if vb is None or vb["size"] != v["size"]:
                        continue
                    o, ob = v["addr"] - lo_a, vb["addr"] - lo_b
                    for w, x in self.candidates(ma[o:o + v["size"]], mb[ob:ob + v["size"]]):
                        cands.append((o + w, x, ob + w))
            for off, val, _ in cands:
                addr = lo_a + off
                i = bisect.bisect_right(starts, addr) - 1
                v = vars_a[i] if i >= 0 and addr < vars_a[i]["addr"] + max(vars_a[i]["size"], 1) else None
                if v is None:
                    if m.group(2) == "bss":
                        after = vars_a[i]["name"] if i >= 0 else sec
                        self.add("static", f"{sec} outside every variable", f"after {after}",
                                 f"+{addr - (vars_a[i]['addr'] if i >= 0 else lo_a):#x} = {val:#x} ({self.target(val)})")
                    continue
                if v.get("const") or v is arena or any(lo <= v["addr"] < hi for lo, hi in skip):
                    continue
                ks = kinds_at(sa, v["tid"], ma, v["addr"] - lo_a, addr - v["addr"])
                path = ks[0][0] if ks else ""
                if not self.typed(ks, val, v["name"], path, f"{v['name']}+{addr - v['addr']:#x}"):
                    self.add("static", v["name"], path, f"+{addr - v['addr']:#x} = {val:#x} ({self.target(val)}); "
                             f"{', '.join(sorted({k for _, k in ks}))}")

    # -- host parts --
    def scan_host(self) -> None:
        sa = self.sa
        seen_a, seen_b = Counter(), Counter()
        nth_b = defaultdict(list)
        for t, m in self.cb:
            nth_b[t].append(m)
        for tag, ma in self.ca:
            k = seen_a[tag]
            seen_a[tag] += 1
            if tag in cn.STACK_CHUNKS or tag == "mem.heap" or re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag):
                continue
            if k >= len(nth_b[tag]):
                continue
            mb = nth_b[tag][k]
            cands = self.candidates(ma, mb) if len(ma) == len(mb) else []
            if not cands:
                continue
            pt = cn.part_target(sa, tag) if not tag.startswith("mem.") else None
            tid = et = None
            if pt is not None:
                if sa.size(pt[2]) == len(ma):
                    tid = pt[2]
                else:
                    et = cn.part_array(sa, tag, len(ma))
            for off, v in cands:
                if tid is not None:
                    ks = kinds_at(sa, tid, ma, 0, off)
                elif et is not None:
                    es = sa.size(et)
                    ks = [(f"[{off // es}]" + q, kk) for q, kk in kinds_at(sa, et, ma, off - off % es, off % es)]
                else:  # a raw chunk: by offset (the scratchpad's are the PS1's addresses, every build's)
                    self.add("host", tag, f"+{off:#x}", f"+{off:#x} = {v:#x} ({self.target(v)}) (a raw chunk)")
                    continue
                path = ks[0][0] if ks else ""
                if not self.typed(ks, v, tag, path, f"{tag}+{off:#x}"):
                    self.add("host", tag, path, f"+{off:#x} = {v:#x} ({self.target(v)}); "
                             f"{', '.join(sorted({k for _, k in ks}))}")

    def scan(self) -> list[Finding]:
        self.scan_heap()
        self.scan_statics()
        self.scan_host()
        return list(self.found.values())


def prim_tags(mem, off: int, size: int) -> set:
    """The offsets (in mem) of a primitive buffer's tags, as statecanon encode_prims walks them."""
    out, o = set(), 0
    while o + 8 <= size:
        tag = int.from_bytes(mem[off + o:off + o + 8], "little")
        if tag == 0:
            o += 8
            continue
        n = tag >> 56
        step = (8 + 4 * n + 7) & ~7
        if n > 16 or o + step > size:
            break
        out.add(off + o)
        o += step
    return out


def heap_layout(s: cn.Schema, chunks: dict, sites: cn.Sites):
    """The heap's and the sound arena's blocks with their parts, as statecanon encode_heap lays them out
    (without encoding them)."""
    heap_lo = s.regions["heapArena"][0]
    mem = memoryview(chunks["mem.heap"])
    blocks = cn.heap_blocks(s, chunks)
    live = [b for b in blocks if b.get("user") and not b.get("end")]
    where = sites.lookup([b["caller"] for b in live])
    ranges, _over = cn.state_ranges(s, chunks)
    starts = [b["data"] for b in blocks]
    per_block, per_arena = defaultdict(list), defaultdict(list)
    arena = cn.arena_blocks(s, chunks)
    for at, h, n in ranges:
        i = bisect.bisect_right(starts, at) - 1
        if i >= 0 and blocks[i].get("user") and not blocks[i].get("end") and at < blocks[i]["data"] + blocks[i]["size"]:
            per_block[i].append((at - blocks[i]["data"], cn.spec_of(s, h), n))
            continue
        if arena is not None:
            j = bisect.bisect_right(arena["lo"], at) - 1
            if j >= 0 and at < arena["blocks"][j]["data"] + arena["blocks"][j]["size"]:
                per_arena[j].append((at - arena["blocks"][j]["data"], cn.spec_of(s, h), n))
    for i, b in enumerate(blocks):
        if b.get("user") and not b.get("end"):
            b["site"] = where[b["caller"]]
            sp = cn.spec_of(s, b["pctype"]) if b["pctype"] else None
            b["spec"] = None if sp is False else sp
            b["parts"] = cn.block_layout(s, mem, b["data"] - heap_lo, b["size"], b["spec"], per_block.get(i, []))
    if arena is not None:
        amem = memoryview(chunks["mem." + arena["sec"]])
        for j, b in enumerate(arena["blocks"]):
            sp = cn.spec_of(s, b["pctype"]) if b["pctype"] else None
            b["spec"] = None if sp is False else sp
            b["parts"] = cn.block_layout(s, amem, arena["off"] + b["hdr"] + cn.ARENA_HEADER, b["size"], b["spec"],
                                         per_arena.get(j, []))
    return blocks, arena


# ---- the allowlist -------------------------------------------------------------------------------------


def load_allow(path: Path = ALLOW) -> dict:
    """(kind, place, path) -> reason. A line without a reason is an error: every entry is reviewed."""
    allow = {}
    if not path.exists():
        return allow
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 4 or not parts[3]:
            raise ScanError(f"{path.name}:{n}: not `kind | place | path | reason`: {line}")
        allow[tuple(parts[:3])] = parts[3]
    return allow


# ---- the runs -------------------------------------------------------------------------------------------


def save_case(build: corpus.Build, case: corpus.Case, work: Path) -> tuple[Path, int, int]:
    """Run to the case's moment and save there as a player does (state_corpus make_case's first half): the
    slot, the vblank it was saved at and its safe point."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    corpus.write_pad(case, work / "pad.txt")
    states = work / "states"
    other = "2" if case.disc == "1" else "1"
    env = {**case.boot_env(), "XG_PAD_SCRIPT": str(work / "pad.txt"), "XG_STATE_DIR": str(states),
           "XG_STATE_SLOT_SAVE": f"{case.asked}:1", "XG_DISC2": str(corpus.disc_path(build.region, other))}
    code = corpus.run_game(build.exe, work / "run", corpus.disc_path(build.region, case.disc),
                           case.asked + 2 * corpus.WAIT + 2, env)
    log = (work / "run" / "port.log").read_text(errors="replace") if (work / "run" / "port.log").exists() else ""
    if code != 0:
        raise ScanError(f"{build.exe.parent.name}: the run exited {code} "
                        f"({'; '.join(corpus.state_lines(log)[-2:]) or 'no state line'})")
    saved = re.search(r"state: saving \S*slot1\.xgs at vblank (\d+)", log)
    slot = states / "slot1.xgs"
    if not saved or not slot.is_file():
        raise ScanError(f"{build.exe.parent.name}: no slot saved (asked at {case.asked})")
    trailer = sc.read_trailer(slot)
    if trailer is None:
        raise ScanError(f"{build.exe.parent.name}: the slot saved at {saved.group(1)} is not portable (no safe point)")
    shutil.rmtree(work / "run" / "cards", ignore_errors=True)
    return slot, int(saved.group(1)), trailer["safePoint"]


@dataclass
class Config:
    name: str
    a: corpus.Build
    b: corpus.Build


def layout(path: Path, name: str) -> corpus.Build:
    builds = corpus.resolve_builds(path)
    b = builds[0]
    if b.config != name:
        raise ScanError(f"{path}: a {b.config} build, not {name}")
    gz = path / "state_schema.json.gz"
    if not gz.is_file():
        raise ScanError(f"{path}: no state schema (configure with -DXG_STATE_SCHEMA=ON)")
    if gz.stat().st_mtime < b.exe.stat().st_mtime:
        raise ScanError(f"{path}: its state schema is older than its executable (tools/state_schema.py {path})")
    return b


def build_layouts(paths: list[Path]) -> None:
    jobs = os.environ.get("XG_YIELD_JOBS") or str(max(2, (os.cpu_count() or 4) // 4))
    for p in paths:
        t0 = time.monotonic()
        r = subprocess.run(["nice", "-n", "19", "cmake", "--build", str(p)], capture_output=True, text=True,
                           env={**os.environ, "CMAKE_BUILD_PARALLEL_LEVEL": jobs})
        if r.returncode != 0:
            tail = [ln for ln in (r.stdout + r.stderr).splitlines() if "error" in ln.lower() or "FAILED" in ln][:3]
            raise ScanError(f"cmake --build {p} failed: {' / '.join(tail)}")
        print(f"built {p} ({time.monotonic() - t0:.0f} s)", flush=True)


def sample_cases(cases: list, n: int | None) -> list:
    if n is None or n >= len(cases):
        return cases
    keep = [c for c in cases if c.id in ALWAYS]
    rest = sorted((c for c in cases if c.id not in ALWAYS), key=lambda c: hashlib.sha1(c.id.encode()).hexdigest())
    return keep + rest[:max(0, n - len(keep))]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--config", action="append", choices=list(CONFIGS), help="a configuration (default: all)")
    ap.add_argument("--case", action="append", default=[], help="a corpus case id (default: all)")
    ap.add_argument("--sample", type=int, help="N cases per configuration (the battles' command menus always)")
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("XG_UNTYPED_JOBS") or os.environ.get("XG_YIELD_JOBS")
                                                     or max(2, (os.cpu_count() or 4) // 4)))
    ap.add_argument("--build", action="store_true", help="cmake --build every layout first")
    ap.add_argument("--work", type=Path, default=Path("/mnt/Games/scratch/xg-untyped/work"))
    ap.add_argument("--keep", action="store_true", help="keep every case's states, not only those with findings")
    ap.add_argument("--report", type=Path, help="the findings as JSON")
    ap.add_argument("-v", "--verbose", action="store_true", help="every finding's example and cases")
    if argv is None:
        argv = sys.argv[1:]
    if argv and argv[0] == "pair":
        return pair_main(argv[1:])
    if argv and argv[0] == "sites":
        return sites_main(argv[1:])
    args = ap.parse_args(argv)
    t0 = time.monotonic()
    try:
        allow = load_allow()
    except ScanError as error:
        print(f"state-untyped: {error}")
        return 1
    names = args.config or list(CONFIGS)
    configs, skipped, errors = [], [], []
    if args.build:
        try:
            build_layouts([p for n in names for p in CONFIGS[n] if (p / "CMakeCache.txt").is_file()])
        except ScanError as error:
            print(f"state-untyped: {error}")
            return 1
    for n in names:
        pa, pb = CONFIGS[n]
        if not (pa / "CMakeCache.txt").is_file() or not (pb / "CMakeCache.txt").is_file():
            skipped.append(f"{n} (no {pa if not (pa / 'CMakeCache.txt').is_file() else pb})")
            continue
        try:
            configs.append(Config(n, layout(pa, n), layout(pb, n)))
        except (ScanError, corpus.CorpusError) as error:
            errors.append(f"{n}: {error}")
    if errors:
        print("\n".join(f"ERROR {e}" for e in errors))
        print(f"state-untyped: {len(errors)} configuration(s) not usable: {errors[0]}")
        return 1
    if not configs:
        print(f"state-untyped: skipped: no layouts ({'; '.join(skipped)})")
        return 0
    findings: dict[tuple, Finding] = {}
    engines = {}
    for c in configs:
        engines.setdefault(c.a.engine, c.a.exe.parent)
    for engine, build in engines.items():
        _, found = site_findings(build)
        for x in found:
            f = findings.get(x["key"])
            if f is None:
                f = findings[x["key"]] = Finding(*x["key"], example=f"{x['file']}:{x['line']}: {x['call']}({x['size'][:80]})")
            f.count += 1
            f.cases.add(f"{engine} source")
    print(f"source: the {' and '.join(engines)} engine"
          f"{'s' if len(engines) > 1 else ''}: {sum(f.count for f in findings.values())} allocations of "
          "pointer-holding types name no type", flush=True)
    work = args.work.absolute()
    work.mkdir(parents=True, exist_ok=True)
    jobs = []
    for c in configs:
        cases = corpus.find_cases(args.case, c.a.region)
        for case in sample_cases(cases, args.sample):
            jobs.append((c, case))
    print(f"cases: {len(jobs)} in {', '.join(c.name for c in configs)}"
          + (f" (skipped: {'; '.join(skipped)})" if skipped else "") + f", {2 * len(jobs)} runs, {args.jobs} at a time",
          flush=True)

    per_case = {}
    failed = []
    t_run = t_scan = 0.0
    sides = defaultdict(dict)
    import multiprocessing
    with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as runs, \
            concurrent.futures.ProcessPoolExecutor(max(1, min(args.jobs, len(jobs)) // 2 or 1),
                                                   mp_context=multiprocessing.get_context("spawn")) as scans:
        def timed_save(b, case, d):
            t = time.monotonic()
            r = save_case(b, case, d)
            return r, time.monotonic() - t

        futs = {}
        for c, case in jobs:
            for side, b in (("a", c.a), ("b", c.b)):
                futs[runs.submit(timed_save, b, case, work / c.name / case.id / side)] = (c, case, side)
        scanning = {}
        bad = set()
        for f in concurrent.futures.as_completed(futs):
            c, case, side = futs[f]
            where = f"{c.name}/{case.id}"
            try:
                r, secs = f.result()
                t_run += secs
            except (ScanError, corpus.CorpusError, ValueError) as error:
                if where not in bad:
                    failed.append(f"{where}: {error}")
                    print(f"ERROR {where}: {error}", flush=True)
                bad.add(where)
                continue
            sides[where][side] = r
            if len(sides[where]) == 2 and where not in bad:
                (sa_, va, pa_), (sb_, vb, pb_) = sides[where]["a"], sides[where]["b"]
                if pa_ != pb_ or va != vb:
                    failed.append(f"{where}: the layouts saved at different moments ({pa_:#x} at {va}, {pb_:#x} at {vb})")
                    print(f"ERROR {failed[-1]}", flush=True)
                    continue
                scanning[scans.submit(scan_pair, str(c.a.exe.parent), str(c.b.exe.parent), str(sa_), str(sb_))] = \
                    (c, case, va, pa_)
        for f in concurrent.futures.as_completed(scanning):
            c, case, v, safe = scanning[f]
            where = f"{c.name}/{case.id}"
            try:
                got, secs = f.result()
                t_scan += secs
            except (ScanError, ValueError, KeyError) as error:
                failed.append(f"{where}: {error}")
                print(f"ERROR {where}: {error}", flush=True)
                continue
            new = [g for g in got if g.key not in allow]
            per_case[where] = {"vblank": v, "safe_point": f"{safe:#x}", "findings": len(new),
                               "allowed": len(got) - len(new)}
            print(f"{'ok  ' if not new else 'FIND'} {where}: saved at {v} at {safe:#x}, "
                  f"{sum(g.count for g in new)} words in {len(new)} places"
                  + (f" ({len(got) - len(new)} allowed)" if len(got) > len(new) else ""), flush=True)
            for g in got:
                f2 = findings.get(g.key)
                if f2 is None:
                    f2 = findings[g.key] = Finding(*g.key, example=f"{where}: {g.example}")
                f2.count += g.count
                f2.cases.add(where)
            if not new and not args.keep:
                shutil.rmtree(work / c.name / case.id, ignore_errors=True)
    new = sorted((f for f in findings.values() if f.key not in allow), key=lambda f: (f.kind, f.place, f.path))
    allowed = [f for f in findings.values() if f.key in allow]
    full = not args.case and args.sample is None and not args.config and not skipped
    stale = sorted(k for k in allow if k not in findings) if full and not failed else []
    for f in new:
        print(f"  {f.kind:7} {f.place} | {f.path}: {f.count} words in {len(f.cases)} cases")
        print(f"          e.g. {f.example}")
        if args.verbose:
            print(f"          cases: {', '.join(sorted(f.cases))}")
    for f in allowed if args.verbose else []:
        print(f"  allowed {f.kind} {f.place} | {f.path}: {f.count} words in {len(f.cases)} cases ({allow[f.key]})")
    for k in stale:
        print(f"  stale allowlist entry (found nowhere): {' | '.join(k)}")
    seconds = time.monotonic() - t0
    line = (f"state-untyped: {len(per_case)} of {len(jobs)} cases scanned in {len(configs)} configurations"
            + (f" (skipped: {'; '.join(skipped)})" if skipped else "")
            + (f", sampled {args.sample} per configuration" if args.sample is not None else "")
            + f": {len(new)} findings ({sum(f.count for f in new)} words)"
            + (f", {len(allowed)} allowed" if allowed else "") + (f", {len(failed)} errors" if failed else "")
            + (f", {len(stale)} stale allowlist entries" if stale else "")
            + f"; {seconds:.0f} s (runs {t_run:.0f} s and scans {t_scan:.0f} s of CPU time)")
    print(line)
    if args.report:
        args.report.write_text(json.dumps({
            "passed": not new and not failed, "line": line, "seconds": round(seconds, 1),
            "configs": [c.name for c in configs], "skipped": skipped, "errors": failed, "cases": per_case,
            "findings": [{"kind": f.kind, "place": f.place, "path": f.path, "words": f.count, "cases": sorted(f.cases),
                          "example": f.example, "allowed": allow.get(f.key)} for f in sorted(findings.values(), key=lambda f: f.key)],
            "stale": [list(k) for k in stale],
        }, indent=1) + "\n")
    return 1 if new or failed else 0


# ---- the source: allocations that name no type -------------------------------------------------------

ALLOCATORS = {"allocateHeap": 0, "allocateBattleHeap": 0, "allocateTaskPair": 0, "allocateUpdateTask": 1,
              "allocateDrawTask": 1, "allocateFromSoundArena": 0, "allocateFromSoundArenaTop": 0}
ALLOC_CALL = re.compile(r"\b(" + "|".join(ALLOCATORS) + r")\s*\(")
TYPED = re.compile(r"\bPC_HEAP_(OF|ONE|NAMED)\s*\(")
SIZEOF = re.compile(r"\b(?:sizeof|offsetof)\s*\(\s*(?:struct\s+)?(\w+)")
CAST = re.compile(r"\(\s*(?:struct\s+)?(\w+)\s*\*\s*\)\s*$")
FUNC_DEF = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*\{", re.M)
DECL = re.compile(r"\b(?:struct\s+)?(\w+)\s*\*\s*\w+\s*=\s*(?:\([^()]*\)\s*)?$")


def call_args(text: str, start: int) -> tuple[list[str], int] | None:
    """The top-level arguments of the call whose '(' ends at start, and where it closes."""
    depth, args, cur = 0, [], start
    for i in range(start, min(len(text), start + 4000)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            if depth == 0:
                args.append(text[cur:i].strip())
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append(text[cur:i].strip())
            cur = i + 1
    return None


class PointerTypes:
    """Which of a schema's named types hold a pointer (a data or function pointer, an ordering-table link, an
    address: what the encoder moves)."""

    def __init__(self, s: cn.Schema):
        self.s = s
        self.memo = {}

    def holds(self, tid, depth=0) -> bool:
        if tid in self.memo:
            return self.memo[tid]
        t = self.s.types.get(tid)
        r = False
        if t is None or depth > 40:
            r = False
        elif t["k"] in ("ptr", "otag", "addr"):
            r = True
        elif t["k"] == "arr":
            r = self.holds(t.get("of"), depth + 1)
        elif t["k"] in ("struct", "union"):
            r = any(self.holds(f.get("type"), depth + 1) for f in t.get("fields", []))
        self.memo[tid] = r
        return r

    def named(self, name: str):
        tid = self.s.type_names.get(name)
        return tid, (self.holds(tid) if tid is not None else False)


def untyped_sites(s: cn.Schema, roots: list[Path]) -> list[dict]:
    """Every allocation in the C under roots whose size names no type (no PC_HEAP_OF, _ONE, _NAMED) while the
    call says what it holds: a sizeof/offsetof of a type, a cast of the result, or the pointer declared with
    it; each with that type and whether it holds pointers."""
    pt = PointerTypes(s)
    out = []
    for root in roots:
        for f in sorted(root.rglob("*.c")):
            text = f.read_text(errors="replace")
            if not ALLOC_CALL.search(text):
                continue
            for m in ALLOC_CALL.finditer(text):
                line_start = text.rfind("\n", 0, m.start()) + 1
                head = text[line_start:m.start()]
                if head.lstrip().startswith(("*", "//", "#")) or re.search(r"\b(?:void|TaskHeader|s32|u8)\s*\*?\s*$", head) \
                        and not re.search(r"[=(,]\s*(?:\([^()]*\)\s*)?$", head):
                    continue  # a comment, a prototype or the definition
                got = call_args(text, m.end())
                if got is None:
                    continue
                args, _ = got
                k = ALLOCATORS[m.group(1)]
                if len(args) <= k or re.match(r"(?:s32|int|size_t|uptr)\s+\w+$", args[k]):
                    continue  # a prototype's parameter list
                size = args[k]
                if TYPED.search(size):
                    continue
                fn = FUNC_DEF.findall(text[:line_start])
                if fn and fn[-1] in ALLOCATORS:
                    continue  # an allocator's own call (task.c): its callers name the type
                names = [n for n in SIZEOF.findall(size)]
                c = CAST.search(head) or DECL.search(head)
                if c:
                    names.append(c.group(1))
                hits = []
                for n in dict.fromkeys(names):
                    tid, holds = pt.named(n)
                    if tid is not None:
                        hits.append((n, holds))
                if not any(h for _, h in hits):
                    continue
                line = text.count("\n", 0, m.start()) + 1
                out.append({"file": str(f.relative_to(ROOT)), "line": line, "call": m.group(1), "size": size,
                            "types": [n for n, h in hits if h], "bytes": "PC_HEAP_BYTES" in size})
    return out


def engine_of(build: Path) -> str:
    cache = (build / "CMakeCache.txt").read_text(errors="replace") if (build / "CMakeCache.txt").exists() else ""
    return (re.search(r"^XG_ENGINE:\w+=(\w+)", cache, re.M) or [None, "original"])[1]


def site_findings(build: Path, s: cn.Schema | None = None) -> tuple[str, list[dict]]:
    """The build's engine and its source's untyped allocations of pointer-holding types (untyped_sites), keyed
    as the allowlist's "site" entries."""
    s = s or cn.Schema(build.resolve())
    engine = engine_of(build)
    game = ROOT / "engine" if engine == "enhanced" else ROOT
    found = untyped_sites(s, [game / "src", ROOT / "port" / "src" / "game", ROOT / "port" / "src" / "extras"])
    for x in found:
        x["key"] = ("site", re.sub(r"^engine/", "", x["file"]), ", ".join(x["types"]))
    return engine, found


def sites_main(argv) -> int:
    ap = argparse.ArgumentParser(prog="state_untyped.py sites",
                                 description="allocations whose type holds pointers but whose size names none")
    ap.add_argument("build", type=Path, nargs="?", default=ROOT / "build/pc/schema",
                    help="a build with a state schema: its types, and its engine's source (default build/pc/schema)")
    args = ap.parse_args(argv)
    engine, found = site_findings(args.build)
    allow = load_allow()
    new, allowed = [], []
    for x in found:
        (allowed if x["key"] in allow else new).append(x)
    for x in new:
        print(f"{x['file']}:{x['line']}: {x['call']}({x['size'][:90]}) holds {', '.join(x['types'])}"
              + (" (said to be bytes)" if x["bytes"] else ""))
    print(f"state-untyped sites: {len(new)} allocations of pointer-holding types name no type ({engine} engine)"
          + (f", {len(allowed)} allowed" if allowed else ""))
    return 1 if new else 0


_schemas: dict = {}


def scan_pair(a_build: str, b_build: str, a_state: str, b_state: str) -> tuple[list, float]:
    """One pair's findings (a worker process: each keeps its schemas and allocation sites)."""
    t = time.monotonic()
    for b in (a_build, b_build):
        if b not in _schemas:
            s = cn.Schema(Path(b).resolve())
            _schemas[b] = (s, cn.Sites(s))
    (sa, sites), (sb, _) = _schemas[a_build], _schemas[b_build]
    got = Pair(Path(a_state), sa, Path(b_state), sb, sites).scan()
    return got, time.monotonic() - t


def pair_main(argv) -> int:
    ap = argparse.ArgumentParser(prog="state_untyped.py pair")
    ap.add_argument("a_state", type=Path)
    ap.add_argument("a_build", type=Path)
    ap.add_argument("b_state", type=Path)
    ap.add_argument("b_build", type=Path)
    ap.add_argument("--words", action="store_true", help="every word found, not one example a place")
    args = ap.parse_args(argv)
    sa = cn.Schema(args.a_build.resolve(), cn.state_bias(args.a_state))
    sb = cn.Schema(args.b_build.resolve(), cn.state_bias(args.b_state))
    allow = load_allow()
    pair = Pair(args.a_state, sa, args.b_state, sb)
    if args.words:
        pair.every = []
    got = sorted(pair.scan(), key=lambda f: f.key)
    for w in pair.every or []:
        print("    " + w)
    for f in got:
        print(f"  {f.kind:7} {f.place} | {f.path}: {f.count} words" + (f" (allowed: {allow[f.key]})" if f.key in allow else ""))
        print(f"          e.g. {f.example}")
    new = [f for f in got if f.key not in allow]
    print(f"state-untyped pair: {len(new)} findings ({sum(f.count for f in new)} words), {len(got) - len(new)} allowed")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
