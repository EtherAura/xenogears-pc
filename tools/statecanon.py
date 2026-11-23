#!/usr/bin/env python3
"""Canonical save states, the reference converter (xg-htn.60.2, phase P1; design: docs/CANONICAL_STATES.md).

A raw save state (port/src/system/savestate.c: memory by address) to a canonical one (the game's data by
stable identity), and back, with the schema of the build that saved it (tools/state_schema.py):

    tools/statecanon.py encode STATE.xgs SCHEMA_BUILD OUT.xgc      raw -> canonical
    tools/statecanon.py decode IN.xgc SCHEMA_BUILD TEMPLATE.xgs OUT.xgs   canonical -> raw
    tools/statecanon.py roundtrip STATE.xgs SCHEMA_BUILD [--out OUT.xgs]  both, then compare byte for byte

Identities (docs/CANONICAL_STATES.md section 2):
  - a game module variable: <module>:<retail address> (config/<ver>/symbols, or a splat label's own), else
    <module>:pc:<name> for the PC files' statics in game modules;
  - a platform static of a saved part: pc:<file>:<name> (savestate_inventory.txt's `state` statics);
  - a struct field: +<retail offset> from its tag (m1C_actor), else pc:<name>;
  - a pointer: the variable it points into, the field path to the scalar it points at, and a byte rest;
    a function by retail address and name; the heap, the game stack and the scratchpad by offset in P1
    (the canonical heap is phase P2, the stack is replaced by safe points in P3/P4).
P1 keeps the heap, the stack, the scratchpad, the stack protector and the hardware chunks raw ("same
build only"): the round trip checks the encoding of everything else, which a later version can then load.

The canonical file (.xgc) is gzipped JSON: {"format", "header", "records": [...], "raw": [...]}."""
import argparse, base64, bisect, gzip, json, re, struct, subprocess, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import statecheck as sc  # noqa: E402 (the raw format's reader)

FORMAT = 1
MASK64 = (1 << 64) - 1
P1, P2, P3 = 0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9


def rotl(v, r):
    return ((v << r) | (v >> (64 - r))) & MASK64


def lane(acc, w):
    return rotl((acc + w * P2) & MASK64, 31) * P1 & MASK64


def state_hash(data: bytes, seed: int = 0) -> int:
    """statecodec.c stateHash (an XXH64-like hash of the body)."""
    n = len(data)
    a, b, c, d = (seed + P1 + P2) & MASK64, (seed + P2) & MASK64, seed, (seed - P1) & MASK64
    i = 0
    words = struct.unpack_from(f"<{n // 8}Q", data) if n >= 8 else ()
    w = 0
    while i + 32 <= n:
        a = lane(a, words[w]); b = lane(b, words[w + 1]); c = lane(c, words[w + 2]); d = lane(d, words[w + 3])
        i += 32; w += 4
    h = (rotl(a, 1) + rotl(b, 7) + rotl(c, 12) + rotl(d, 18) + n) & MASK64
    while i + 8 <= n:
        h = (rotl(h ^ lane(0, words[w]), 27) * P1 + P3) & MASK64
        i += 8; w += 1
    while i < n:
        h = rotl(h ^ (data[i] * P3 & MASK64), 11) * P1 & MASK64
        i += 1
    h ^= h >> 33; h = h * P2 & MASK64; h ^= h >> 29; h = h * P3 & MASK64; h ^= h >> 32
    return h


# ---- the schema -----------------------------------------------------------------------------------

class Schema:
    def __init__(self, build: Path):
        path = build / "state_schema.json.gz"
        if not path.exists():
            raise SystemExit(f"statecanon: no schema in {build} (tools/state_schema.py {build})")
        s = json.load(gzip.open(path, "rt"))
        self.types = s["types"]
        # one object per address (DWARF can describe one twice: a definition and LTO's copy)
        seen_addr = {}
        for v in s["variables"]:
            if not v.get("tid"):
                continue
            o = seen_addr.get(v["addr"])
            if o is None or v["size"] > o["size"]:
                seen_addr[v["addr"]] = v
        self.vars = sorted(seen_addr.values(), key=lambda v: v["addr"])
        self.sections = {k: tuple(v) for k, v in s["sections"].items()}
        self.regions = {k: tuple(v) for k, v in s["regions"].items()}
        self.parts = s["parts"]
        self.collisions = 0
        taken = set()
        for v in self.vars:
            i = self.identity(v)
            if i in taken:  # two objects, one identity (an alias at one retail address): the name tells them apart
                i = f"{i}:{v['name']}"
                self.collisions += 1
            taken.add(i)
            v["id"] = i
        self.by_id = {v["id"]: v for v in self.vars}
        spans = sorted((v["addr"], v["addr"] + max(v["size"], 1), i) for i, v in enumerate(self.vars))
        self.span_lo = [s0 for s0, _, _ in spans]
        self.spans = spans
        self.scratch = []  # (lo, hi): memory no state carries (scratch64): left out of comparisons
        for v in self.vars:
            t = self.types.get(v["tid"], {})
            while t.get("k") == "arr":
                t = self.types.get(t.get("of"), {})
            if t.get("k") == "scratch":
                self.scratch.append((v["addr"], v["addr"] + v["size"]))
        self.funcs = {f["addr"]: f for f in s["functions"]}
        self.func_by_id = {}
        for f in s["functions"]:
            self.func_by_id.setdefault(self.func_identity(f), f["addr"])
            self.func_by_id.setdefault("fn:" + f["name"], f["addr"])
        self.platform = {}
        for v in self.vars:
            if v.get("platform"):
                self.platform[(v["platform"], v["name"])] = v
        self.by_name = {}
        for v in self.vars:
            self.by_name.setdefault(v["name"], v)
        self.exe = Path(s["exe"])
        retail = {f["name"]: f.get("retail") for f in s["functions"]}
        self.text_syms = {}   # address -> [names]: static functions and identical bodies folded into one
        self.sym_addr = {}    # name -> address
        ro_named = []
        for line in subprocess.run(["nm", "-n", "-S", "--defined-only", str(self.exe)], capture_output=True,
                                   text=True).stdout.splitlines():
            m = re.match(r"([0-9a-f]+) (?:([0-9a-f]+) )?(\S) (\S+)$", line)
            if not m:
                continue
            a, size, typ, name = int(m.group(1), 16), int(m.group(2) or "0", 16), m.group(3), m.group(4)
            self.sym_addr.setdefault(name, a)
            if typ in "tTwW":
                self.text_syms.setdefault(a, []).append(name)
            elif typ in "rR" and size and not name.startswith((".L", "xgsrc_")):
                ro_named.append((a, a + size, name))
        for a, names in self.text_syms.items():
            names.sort(key=lambda n: (retail.get(n) is None, n))
        ro_named.sort()
        self.ro_named = ro_named; self.ro_lo = [r[0] for r in ro_named]
        self.retail_fn = retail
        self.ro = []  # (lo, hi, file offset) of the read-only sections, and the file
        for line in subprocess.run(["readelf", "-SW", str(self.exe)], capture_output=True, text=True).stdout.splitlines():
            m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)", line)
            # file-backed data: .rodata, and the writable .data that holds the game's string tables (xgsrc_*,
            # .LC*: constant in practice; read from the file, as the build starts them)
            if m and m.group(2) == "PROGBITS" and "A" in m.group(6) and "X" not in m.group(6):
                lo = int(m.group(3), 16)
                if lo:
                    self.ro.append((lo, lo + int(m.group(5), 16), int(m.group(4), 16)))
        self._file = None
        self._strings = None

    def file(self):
        if self._file is None:
            self._file = self.exe.read_bytes()
        return self._file

    def ro_at(self, v):
        for lo, hi, fo in self.ro:
            if lo <= v < hi:
                return fo + (v - lo)
        return None

    def string_at(self, v):
        fo = self.ro_at(v)
        if fo is None:
            return None
        f = self.file()
        end = f.find(b"\0", fo, fo + 4096)
        return f[fo:end] if end >= 0 else None

    def string_addr(self, text: bytes):
        if self._strings is None:
            self._strings = {}
            f = self.file()
            for lo, hi, fo in self.ro:
                i = fo; end = fo + (hi - lo)
                while i < end:
                    j = f.find(b"\0", i, end)
                    if j < 0:
                        break
                    if j > i:
                        self._strings.setdefault(f[i:j], lo + (i - fo))
                    i = j + 1
        return self._strings.get(text)

    def symbol_at(self, v):
        """(name, address) of the symbol at or below v, any kind."""
        if not hasattr(self, "_all_lo"):
            rows = sorted((a, n) for n, a in self.sym_addr.items())
            self._all = rows; self._all_lo = [r[0] for r in rows]
        i = bisect.bisect_right(self._all_lo, v) - 1
        return (self._all[i][1], self._all[i][0]) if i >= 0 else None

    def ro_named_at(self, v):
        i = bisect.bisect_right(self.ro_lo, v) - 1
        if i >= 0 and self.ro_named[i][0] <= v < self.ro_named[i][1]:
            return self.ro_named[i]
        return None

    def fn_identity(self, v):
        if v in self.funcs:
            return Schema.func_identity(self.funcs[v])
        names = self.text_syms.get(v)
        if names:
            r = self.retail_fn.get(names[0])
            return f"fn:{r:08x}:{names[0]}" if r is not None else f"fn:{names[0]}"
        return None

    def fn_address(self, ident):
        a = self.func_by_id.get(ident)
        if a is None:
            name = ident.rsplit(":", 1)[-1]
            a = self.func_by_id.get("fn:" + name, self.sym_addr.get(name))
        return a

    @staticmethod
    def identity(v):
        if v.get("module"):
            return f"{v['module']}:{v['retail']:08x}" if v.get("retail") is not None else f"{v['module']}:pc:{v['name']}"
        return f"pc:{v.get('platform')}:{v['name']}"

    @staticmethod
    def func_identity(f):
        return f"fn:{f['retail']:08x}:{f['name']}" if f.get("retail") is not None else f"fn:{f['name']}"

    def var_at(self, addr):
        i = bisect.bisect_right(self.span_lo, addr) - 1
        while i >= 0:
            lo, hi, k = self.spans[i]
            if lo <= addr < hi:
                return self.vars[k]
            if hi <= addr and i < len(self.spans) - 1 and self.spans[i][0] < addr - (1 << 24):
                break
            i -= 1
            if i >= 0 and addr - self.spans[i][0] > (1 << 22):
                break
        return None

    def size(self, tid):
        return self.types[tid]["size"] if tid in self.types else 0


# ---- encoding -------------------------------------------------------------------------------------

class Encoder:
    def __init__(self, schema: Schema):
        self.s = schema
        self.warn = Counter()
        self.current = ""
        self.unknown = Counter()  # (holder, value's symbol) of pointers to nothing known

    def field_key(self, f, seen):
        k = f"+{f['retail']:x}" if f.get("retail") is not None else f"pc:{f['name']}"
        if f.get("bits") is not None and f.get("retail") is None:
            k = f"pc:{f['name']}"
        if k in seen:
            k = f"{k}:{f['name']}"
        seen.add(k)
        return k

    def path_of(self, tid, off):
        """The field path from a value of type tid to the scalar at byte off, and the rest of the offset."""
        path = []
        for _ in range(64):
            t = self.s.types.get(tid)
            if t is None:
                break
            if t["k"] == "arr" and t.get("of") in self.s.types and t["n"]:
                es = self.s.size(t["of"])
                if es <= 0:
                    break
                i = off // es
                if i >= t["n"]:
                    break
                path.append(i)
                off -= i * es
                tid = t["of"]
                continue
            if t["k"] == "union":  # through the most typed view that covers the offset
                views = [f for f in union_views(self.s, t) if self.s.size(f["type"]) > off]
                if not views:
                    break
                f = views[-1]
                path.append({"u": f["name"]})
                tid = f["type"]
                continue
            if t["k"] == "struct":
                seen = set(); hit = None
                for f in t["fields"]:
                    key = self.field_key(f, seen)
                    fs = self.s.size(f["type"]) if f.get("type") else 0
                    if f.get("bits") is None and f["off"] <= off < f["off"] + max(fs, 1):
                        hit = (key, f)
                        break
                if hit is None:
                    break
                path.append(hit[0])
                off -= hit[1]["off"]
                tid = hit[1]["type"]
                continue
            break
        return path, off

    def pointer(self, v, quiet=False):
        if v == 0:
            return None
        for name, key in (("heapArena", "heap"), ("gameStack", "stack"), ("pcScratchpad", "scratchpad")):
            lo, hi = self.s.regions.get(name, (0, 0))
            if lo <= v <= hi:
                return {key: v - lo}
        f = self.s.fn_identity(v)
        if f is not None:
            return {"fn": f}
        var = self.s.var_at(v)
        if var is not None:
            path, rest = self.path_of(var["tid"], v - var["addr"])
            out = {"v": var["id"], "p": path}
            if rest:
                out["b"] = rest
            return out
        if self.s.ro_at(v) is not None:  # constant data: a named constant, else a string by its content
            named = self.s.ro_named_at(v)
            if named is not None:
                return {"ro": named[2], "o": v - named[0]}
            text = self.s.string_at(v)
            if text is not None:
                hint = self.s.symbol_at(v)  # identical strings sit in several places: where this one was
                out = {"str": text.decode("latin-1")}
                if hint is not None:
                    out["at"] = hint[0]; out["o"] = v - hint[1]
                return out
        if not quiet:
            self.warn["pointer to nothing known (kept as a number: same build only)"] += 1
            self.unknown[(self.current, v)] += 1
        return {"raw": v}

    def value(self, tid, mem, off):
        t = self.s.types.get(tid)
        if t is None:
            return None
        k = t["k"]
        if k == "int":
            n = t["size"]
            return int.from_bytes(mem[off:off + n], "little", signed=t.get("signed", False))
        if k == "float":
            return {"f": mem[off:off + t["size"]].hex()}
        if k == "addr":  # an integer that holds an address (uptr, sptr): moved as a pointer when it is one
            v = int.from_bytes(mem[off:off + t["size"]], "little")
            p = self.pointer(v, quiet=True) if v else None
            if p is None or "raw" in p:
                return v  # zero, or a number after all (an offset, a count)
            return {"a": p}
        if k == "scratch":
            return {"z": t["size"]}
        if k == "otag":
            tag = int.from_bytes(mem[off:off + t["size"]], "little")
            bits = 8 * t["size"] - 8
            link = tag & ((1 << bits) - 1)
            return {"n": tag >> bits, "l": "end" if link == (1 << bits) - 1 else self.pointer(link)}
        if k == "ptr":
            v = int.from_bytes(mem[off:off + 8], "little")
            if t.get("fn"):
                if v == 0:
                    return None
                f = self.s.fn_identity(v)
                if f is not None:
                    return {"fn": f}
                self.warn["function pointer to no known function"] += 1
                self.unknown[(self.current + " (function pointer)", v)] += 1
                return {"raw": v}
            return self.pointer(v)
        if k == "arr":
            of = t.get("of"); n = t["n"]
            if of not in self.s.types or n == 0:
                return {"x": bytes(mem[off:off + t["size"]]).hex()}
            et = self.s.types[of]; es = et["size"]
            if et["k"] == "scratch":
                return {"z": t["size"]}
            if et["k"] == "int":  # scalars: little-endian bytes, the same in every build (not OT links)
                return {"x": bytes(mem[off:off + es * n]).hex(), "w": es}
            return [self.value(of, mem, off + i * es) for i in range(n)]
        if k == "struct":
            out = {}; seen = set()
            for f in t["fields"]:
                key = self.field_key(f, seen)
                if f.get("bits") is not None:
                    bit = f["bit"]; nb = f["bits"]
                    base = (bit // 8)
                    span = (bit % 8 + nb + 7) // 8
                    raw = int.from_bytes(mem[off + base:off + base + span], "little")
                    out[key] = (raw >> (bit % 8)) & ((1 << nb) - 1)
                    continue
                out[key] = self.value(f["type"], mem, off + f["off"])
            return out
        if k == "union":
            members = union_views(self.s, t)
            if not members:
                self.warn["union kept as bytes"] += 1
                return {"x": bytes(mem[off:off + t["size"]]).hex()}
            self.warn[f"union {t.get('name', '?')} encoded through every view"] += 1
            return {"u": {f["name"]: self.value(f["type"], mem, off + f["off"]) for f in members}}
        return {"x": bytes(mem[off:off + t["size"]]).hex()}


# ---- decoding -------------------------------------------------------------------------------------

class Decoder:
    def __init__(self, schema: Schema):
        self.s = schema
        self.warn = Counter()

    def offset_of(self, tid, path):
        off = 0
        for step in path:
            t = self.s.types.get(tid)
            if t is None:
                return None
            if isinstance(step, dict):  # a union's view
                f = next((f for f in t.get("fields", []) if f["name"] == step["u"]), None)
                if t["k"] != "union" or f is None:
                    return None
                tid = f["type"]
                continue
            if isinstance(step, int):
                if t["k"] != "arr":
                    return None
                off += step * self.s.size(t["of"])
                tid = t["of"]
            else:
                if t["k"] != "struct":
                    return None
                seen = set(); hit = None
                for f in t["fields"]:
                    if Encoder.field_key(None, f, seen) == step:
                        hit = f
                        break
                if hit is None:
                    return None
                off += hit["off"]
                tid = hit["type"]
        return off

    def pointer(self, p):
        if p is None:
            return 0
        if "raw" in p:
            return p["raw"]
        for name, key in (("heapArena", "heap"), ("gameStack", "stack"), ("pcScratchpad", "scratchpad")):
            if key in p:
                return self.s.regions[name][0] + p[key]
        if "ro" in p:
            a = self.s.sym_addr.get(p["ro"])
            if a is None:
                self.warn["constant not in this build"] += 1
                return 0
            return a + p.get("o", 0)
        if "str" in p:
            text = p["str"].encode("latin-1")
            if "at" in p and p["at"] in self.s.sym_addr:
                hinted = self.s.sym_addr[p["at"]] + p.get("o", 0)
                if self.s.string_at(hinted) == text:
                    return hinted
            a = self.s.string_addr(text)
            if a is None:
                self.warn["string not in this build"] += 1
                return 0
            return a
        if "fn" in p:
            a = self.s.fn_address(p["fn"])
            if a is None:
                self.warn["function not in this build"] += 1
                return 0
            return a
        var = self.s.by_id.get(p["v"])
        if var is None:
            self.warn["pointer to a variable not in this build"] += 1
            return 0
        o = self.offset_of(var["tid"], p.get("p", []))
        if o is None:
            self.warn["pointer path not in this build"] += 1
            o = 0
        return var["addr"] + o + p.get("b", 0)

    def put(self, tid, val, buf, off):
        t = self.s.types.get(tid)
        if t is None or val is None and t["k"] not in ("ptr",):
            return
        k = t["k"]
        if k == "int":
            n = t["size"]
            buf[off:off + n] = (val & ((1 << (8 * n)) - 1)).to_bytes(n, "little")
        elif k == "float":
            b = bytes.fromhex(val["f"]); buf[off:off + len(b)] = b
        elif k == "addr":
            v = self.pointer(val["a"]) if isinstance(val, dict) else val
            buf[off:off + t["size"]] = (v & ((1 << (8 * t["size"])) - 1)).to_bytes(t["size"], "little")
        elif k == "scratch":
            buf[off:off + t["size"]] = bytes(t["size"])
        elif k == "otag":
            bits = 8 * t["size"] - 8
            link = (1 << bits) - 1 if val["l"] == "end" else self.pointer(val["l"]) & ((1 << bits) - 1)
            buf[off:off + t["size"]] = ((val["n"] << bits) | link).to_bytes(t["size"], "little")
        elif k == "ptr":
            buf[off:off + 8] = (self.pointer(val) & MASK64).to_bytes(8, "little")
        elif k == "arr":
            if isinstance(val, dict) and "z" in val:
                buf[off:off + t["size"]] = bytes(t["size"])
            elif isinstance(val, dict):
                b = bytes.fromhex(val["x"]); n = min(len(b), t["size"]); buf[off:off + n] = b[:n]
            else:
                es = self.s.size(t["of"])
                for i, e in enumerate(val[:t["n"]]):
                    self.put(t["of"], e, buf, off + i * es)
        elif k == "struct":
            seen = set()
            for f in t["fields"]:
                key = Encoder.field_key(None, f, seen)
                if key not in val:
                    self.warn["field missing in the state (left zero)"] += 1
                    continue
                if f.get("bits") is not None:
                    bit = f["bit"]; nb = f["bits"]; base = bit // 8; span = (bit % 8 + nb + 7) // 8
                    raw = int.from_bytes(buf[off + base:off + base + span], "little")
                    m = ((1 << nb) - 1) << (bit % 8)
                    raw = (raw & ~m) | ((val[key] << (bit % 8)) & m)
                    buf[off + base:off + base + span] = raw.to_bytes(span, "little")
                    continue
                self.put(f["type"], val[key], buf, off + f["off"])
        elif k == "union" and isinstance(val, dict) and "u" in val:
            for f in union_views(self.s, t):  # least typed first: the fully typed view writes last
                if f["name"] not in val["u"]:
                    self.warn["union view not in the state"] += 1
                    continue
                self.put(f["type"], val["u"][f["name"]], buf, off + f["off"])
        else:
            b = bytes.fromhex(val["x"]); n = min(len(b), t["size"]); buf[off:off + n] = b[:n]


def typed_bytes(schema: "Schema", tid, depth=0) -> int:
    """How many of a type's bytes are typed beyond plain scalars (pointers, and structs' scalar fields)."""
    t = schema.types.get(tid)
    if t is None or depth > 16:
        return 0
    if t["k"] == "ptr":
        return 8
    if t["k"] == "arr":
        et = schema.types.get(t.get("of"))
        if et is None or et["k"] == "int":
            return 0
        return t["n"] * typed_bytes(schema, t["of"], depth + 1)
    if t["k"] == "struct":
        return sum(typed_bytes(schema, f["type"], depth + 1) if schema.types.get(f["type"], {}).get("k") in ("arr", "struct", "ptr", "union")
                   else schema.size(f["type"]) for f in t["fields"])
    return 0


def union_views(schema: "Schema", t: dict) -> list:
    """A union's members, least typed first. The game's unions are views of one memory (BattleBssBlock:
    the whole block as one struct, or bytes then its tail's struct): every view is kept, and on a load the
    most typed one is written last, so its pointers win where views overlap. A union whose views disagree
    on what holds a pointer would need a rule of its own."""
    ms = [f for f in t["fields"] if f.get("type") in schema.types]
    return sorted(ms, key=lambda f: (-raw_bytes(schema, f["type"]), typed_bytes(schema, f["type"])))


def raw_bytes(schema: "Schema", tid, depth=0) -> int:
    """How many of a type's bytes are plain scalar arrays (a view's untyped stretch, like BattleBssBlock
    m0_tail's leading u8 m0[0x8E38])."""
    t = schema.types.get(tid)
    if t is None or depth > 16:
        return 0
    if t["k"] == "arr":
        et = schema.types.get(t.get("of"))
        if et is None or et["k"] == "int":
            return t["size"]
        return t["n"] * raw_bytes(schema, t["of"], depth + 1)
    if t["k"] == "struct":
        return sum(raw_bytes(schema, f["type"], depth + 1) for f in t["fields"])
    return 0


# ---- raw states <-> canonical ---------------------------------------------------------------------

def part_target(schema: Schema, tag: str):
    """A host part's chunk tag ("gpu.gp.cmdAddr": part gpu, XG_STATE_VAR(s, gp.cmdAddr)) to the platform
    variable and member path it saves, or None."""
    part, _, expr = tag.partition(".")
    file = schema.parts.get(part)
    if not file or not re.fullmatch(r"[A-Za-z_]\w*(\.[A-Za-z_]\w*)*", expr):
        return None
    names = expr.split(".")
    var = schema.platform.get((file, names[0])) or schema.by_name.get(names[0])
    if var is None:
        return None
    tid = var["tid"]; off = 0; path = []
    for m in names[1:]:
        t = schema.types.get(tid)
        if t is None or t["k"] != "struct":
            return None
        f = next((f for f in t["fields"] if f["name"] == m), None)
        if f is None:
            return None
        off += f["off"]; tid = f["type"]; path.append(m)
    return var, path, tid, off


def encode(state: Path, schema: Schema) -> tuple[dict, Encoder]:
    h, body = sc.read_state(state)
    enc = Encoder(schema)
    records, raw = [], []
    covered = Counter(); total = Counter()
    for tag, at, size in sc.chunks(body):
        data = memoryview(body)[at:at + size]
        m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
        if m:
            sec = schema.sections.get(f"{m.group(1)}.{m.group(2)}")
            if sec is None:
                raw.append({"tag": tag, "b64": base64.b64encode(bytes(data)).decode(), "why": "section not in schema"})
                continue
            lo, hi = sec
            mask = bytearray(size)
            for v in schema.vars:
                if v.get("module") == m.group(1) and lo <= v["addr"] < hi:
                    o = v["addr"] - lo
                    enc.current = v["name"]
                    records.append({"id": v["id"], "v": enc.value(v["tid"], data, o)})
                    for i in range(o, min(o + v["size"], size)):
                        mask[i] = 1
            gaps = bytes(b if not mask[i] else 0 for i, b in enumerate(bytes(data)))
            n_cov = sum(mask); covered["module sections"] += n_cov; total["module sections"] += size
            if any(gaps):
                raw.append({"tag": tag, "b64": base64.b64encode(gaps).decode(), "why": "bytes outside every variable"})
            continue
        pt = part_target(schema, tag) if not tag.startswith("mem.") else None
        if pt is not None:
            var, path, tid, _ = pt
            if schema.size(tid) == size:
                enc.current = tag
                records.append({"part": tag, "id": var["id"], "p": path, "v": enc.value(tid, data, 0)})
                covered["host parts"] += size; total["host parts"] += size
                continue
        why = "same build only (P2: heap, P3/P4: stack)" if tag.startswith("mem.") else "part not in the schema"
        raw.append({"tag": tag, "b64": base64.b64encode(bytes(data)).decode(), "why": why})
        total["raw" if tag.startswith("mem.") else "host parts"] += size
    doc = {"format": FORMAT, "header": h, "records": records, "raw": raw,
           "coverage": {k: [covered[k], total[k]] for k in total}}
    return doc, enc


def decode(doc: dict, schema: Schema, template: Path) -> tuple[bytes, Decoder]:
    """The raw body for this build, chunk by chunk in the template's order (P1: the template is a raw state
    of the same build; later the order comes from the build's own part list)."""
    _, tbody = sc.read_state(template)
    dec = Decoder(schema)
    by_id = {r["id"]: r for r in doc["records"] if "part" not in r}
    by_part = {r["part"]: r for r in doc["records"] if "part" in r}
    raws = {r["tag"]: r for r in doc["raw"]}
    out = bytearray()
    for tag, at, size in sc.chunks(tbody):
        buf = bytearray(size)
        m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
        if m and f"{m.group(1)}.{m.group(2)}" in schema.sections:
            lo, hi = schema.sections[f"{m.group(1)}.{m.group(2)}"]
            if tag in raws:
                b = base64.b64decode(raws[tag]["b64"]); buf[:len(b)] = b[:size]
            for v in schema.vars:
                if v.get("module") == m.group(1) and lo <= v["addr"] < hi:
                    r = by_id.get(v["id"])
                    if r is None:
                        dec.warn["variable not in the state (left zero)"] += 1
                        continue
                    dec.put(v["tid"], r["v"], buf, v["addr"] - lo)
        elif tag in by_part:
            r = by_part[tag]
            pt = part_target(schema, tag)
            if pt is not None:
                dec.put(pt[2], r["v"], buf, 0)
        elif tag in raws:
            b = base64.b64decode(raws[tag]["b64"]); buf[:len(b)] = b[:size]
        else:
            dec.warn["chunk not in the state (left zero)"] += 1
        t = tag.encode()
        out += bytes([len(t)]) + t + struct.pack("<I", size) + buf
    return bytes(out), dec


def write_state(template: Path, body: bytes, out: Path) -> None:
    """A raw state file: the template's header and thumbnail, this body stored unpacked."""
    data = template.read_bytes()
    thumb_packed = struct.unpack_from("<I", data, 216)[0]
    head = bytearray(data[:sc.HEADER_SIZE])
    thumb = data[sc.HEADER_SIZE:sc.HEADER_SIZE + thumb_packed]
    blocks = bytearray()
    for i in range(0, len(body), 1 << 20):
        part = body[i:i + (1 << 20)]
        blocks += struct.pack("<II", len(part), len(part)) + part
    struct.pack_into("<Q", head, 200, len(body))
    struct.pack_into("<Q", head, 208, state_hash(body))
    struct.pack_into("<Q", head, 224, len(blocks))
    out.write_bytes(bytes(head) + thumb + bytes(blocks))


def compare(a: bytes, b: bytes, schema: Schema, limit=12) -> list[str]:
    ca = {t: (o, n) for t, o, n in sc.chunks(a)}
    cb = {t: (o, n) for t, o, n in sc.chunks(b)}
    lines, where = [], Counter()
    for tag, (o, n) in ca.items():
        if tag not in cb or cb[tag][1] != n:
            lines.append(f"  {tag}: missing or another size"); continue
        x = a[o:o + n]; y = b[cb[tag][0]:cb[tag][0] + n]
        if x == y:
            continue
        diff = [i for i in range(n) if x[i] != y[i]]
        m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
        if m:
            lo = schema.sections[f"{m.group(1)}.{m.group(2)}"][0]
            diff = [i for i in diff if not any(a <= lo + i < b for a, b in schema.scratch)]
            for i in diff:
                v = schema.var_at(lo + i)
                where[f"{tag} {v['name'] if v else '(no variable)'}"] += 1
        else:
            where[tag] += len(diff)
    for k, n in where.most_common(limit):
        lines.append(f"  {k}: {n} bytes differ")
    if len(where) > limit:
        lines.append(f"  ... {len(where) - limit} more")
    return lines


def field_path(schema: Schema, var, off: int) -> str:
    path, rest = Encoder(schema).path_of(var["tid"], off)
    t = ".".join(x["u"] if isinstance(x, dict) else str(x) for x in path)
    return f"{var['name']}{'.' + t if t else ''}{'+' + str(rest) if rest else ''}"


def crosscheck(a_state: Path, a: Schema, b_state: Path, b: Schema, limit=25) -> tuple[list[str], dict]:
    """A's state, encoded with A's schema and decoded with B's, against B's own state at the same moment:
    every chunk P1 encodes must come out as B has it. The raw chunks (heap, stack: P2, P3/P4) are left out."""
    doc, enc = encode(a_state, a)
    doc = json.loads(json.dumps(doc))
    body, dec = decode(doc, b, b_state)
    _, bbody = sc.read_state(b_state)
    raw_tags = {r["tag"] for r in doc["raw"] if r["why"].startswith(("same build", "part not"))}
    ca = {t: (o, n) for t, o, n in sc.chunks(body)}
    where = Counter(); checked = 0
    for tag, o, n in sc.chunks(bbody):
        if tag in raw_tags or tag not in ca:
            continue
        x = body[ca[tag][0]:ca[tag][0] + n]; y = bbody[o:o + n]
        checked += n
        if x == y:
            continue
        m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
        lo = b.sections[f"{m.group(1)}.{m.group(2)}"][0] if m else None
        for i in range(n):
            if x[i] != y[i]:
                if lo is not None and any(sa <= lo + i < sb for sa, sb in b.scratch):
                    continue
                if lo is not None:
                    v = b.var_at(lo + i)
                    where[field_path(b, v, lo + i - v["addr"]) if v else f"{tag} (outside variables)"] += 1
                else:
                    where[tag] += 1
    lines = [f"  {k}: {n} bytes" for k, n in where.most_common(limit)]
    if len(where) > limit:
        lines.append(f"  ... {len(where) - limit} more places")
    return lines, {"checked": checked, "differing": sum(where.values()), "encode": dict(enc.warn), "decode": dict(dec.warn)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("encode"); e.add_argument("state", type=Path); e.add_argument("build", type=Path); e.add_argument("out", type=Path)
    d = sub.add_parser("decode"); d.add_argument("canon", type=Path); d.add_argument("build", type=Path)
    d.add_argument("template", type=Path); d.add_argument("out", type=Path)
    r = sub.add_parser("roundtrip"); r.add_argument("state", type=Path); r.add_argument("build", type=Path)
    r.add_argument("--out", type=Path)
    x = sub.add_parser("crosscheck"); x.add_argument("a_state", type=Path); x.add_argument("a_build", type=Path)
    x.add_argument("b_state", type=Path); x.add_argument("b_build", type=Path)
    a = ap.parse_args()
    if a.cmd == "crosscheck":
        lines, st = crosscheck(a.a_state, Schema(a.a_build.resolve()), a.b_state, Schema(a.b_build.resolve()))
        print(f"statecanon crosscheck: {st['checked']} bytes checked, {st['differing']} differ; encode {st['encode']}; "
              f"decode {st['decode']}")
        print("\n".join(lines) if lines else "  every encoded byte as the other build has it")
        return 1 if lines else 0
    schema = Schema(a.build.resolve())
    if a.cmd == "encode":
        doc, enc = encode(a.state, schema)
        with gzip.open(a.out, "wt") as fh:
            json.dump(doc, fh)
        print(f"statecanon: {len(doc['records'])} records, {len(doc['raw'])} raw chunks; coverage {doc['coverage']}; {dict(enc.warn)}")
        return 0
    if a.cmd == "decode":
        doc = json.load(gzip.open(a.canon, "rt"))
        body, dec = decode(doc, schema, a.template)
        write_state(a.template, body, a.out)
        print(f"statecanon: wrote {a.out} ({len(body)} bytes); {dict(dec.warn)}")
        return 0
    doc, enc = encode(a.state, schema)
    doc = json.loads(json.dumps(doc))  # as a file would carry it
    body, dec = decode(doc, schema, a.state)
    _, orig = sc.read_state(a.state)
    lines = compare(orig, body, schema)
    cov = ", ".join(f"{k} {c}/{t} bytes" for k, (c, t) in doc["coverage"].items())
    print(f"statecanon roundtrip: {len(doc['records'])} records ({cov}); encode {dict(enc.warn)}; decode {dict(dec.warn)}")
    print("\n".join(lines) if lines else "  every chunk identical")
    if a.out:
        write_state(a.state, body, a.out)
        print(f"  wrote {a.out}")
    return 1 if lines else 0


if __name__ == "__main__":
    sys.exit(main())
