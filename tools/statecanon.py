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

The canonical file (.xgc) is gzipped JSON: {"format", "header", "records": [...], "raw": [...]}.

A schema holds its build's link addresses; a state holds the addresses of the process that saved it, which
sit at link address + that process's load bias (a position-independent executable or library: the header's
image field, statefile.c H_IMAGE; 0 for a build at its link addresses). Each state is read with its build's
schema rebased by its own bias (Schema(build, bias)), the template's for a decode: every address in the
schema plus the bias, file offsets unchanged."""
import argparse, base64, bisect, gzip, json, re, struct, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import statecheck as sc  # noqa: E402 (the raw format's reader)

FORMAT = 1
RULES_LANGUAGE = 1  # the state rules' language (docs 17.2): fixed in the first canonical release
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

def var_identity(v: dict) -> str:
    """A variable's stable identity (docs section 2): <module>:<retail address> or <module>:pc:<name> in a game
    module, pc:<platform file>:<name> for a platform static."""
    if v.get("module"):
        return f"{v['module']}:{v['retail']:08x}" if v.get("retail") is not None else f"{v['module']}:pc:{v['name']}"
    return f"pc:{v.get('platform')}:{v['name']}"


def keyed_variables(variables: list) -> tuple[list, int]:
    """A schema's typed variables, one per address (DWARF can describe one twice: a definition and LTO's copy;
    the larger is kept), in address order, each with its identity in "id"; and how many identities collided (an
    alias at one retail address: the name tells them apart). tools/state_schema.py names a rule's variable so."""
    seen_addr = {}
    for v in variables:
        if not v.get("tid"):
            continue
        o = seen_addr.get(v["addr"])
        if o is None or v["size"] > o["size"]:
            seen_addr[v["addr"]] = v
    out = sorted(seen_addr.values(), key=lambda v: v["addr"])
    collisions = 0
    taken = set()
    for v in out:
        i = var_identity(v)
        if i in taken:
            i = f"{i}:{v['name']}"
            collisions += 1
        taken.add(i)
        v["id"] = i
    return out, collisions


def state_bias(state: Path) -> int:
    """The load bias of the process that saved a state (its header's image field, statefile.c H_IMAGE)."""
    with state.open("rb") as f:
        head = f.read(sc.HEADER_SIZE)
    return struct.unpack_from("<Q", head, 232)[0] if len(head) >= 240 else 0


def rebase(s: dict, bias: int) -> None:
    """A schema's JSON with every address plus bias (modulo 2^64, as the C reader's uint64_t addresses):
    variables, functions, symbols, sections, regions and the read-only ranges (not their file offsets)."""
    if not bias:
        return
    for v in s["variables"]:
        v["addr"] = (v["addr"] + bias) & MASK64
    for f in s["functions"]:
        f["addr"] = (f["addr"] + bias) & MASK64
    for key in ("sections", "regions"):
        s[key] = {k: [(lo + bias) & MASK64, (hi + bias) & MASK64] for k, (lo, hi) in s[key].items()}
    if s.get("symbols"):
        s["symbols"] = [[n, (a + bias) & MASK64, size, kind] for n, a, size, kind in s["symbols"]]
    if s.get("ro"):
        s["ro"] = [[(lo + bias) & MASK64, (hi + bias) & MASK64, fo] for lo, hi, fo in s["ro"]]


class Schema:
    def __init__(self, build: Path, bias: int = 0):
        """build's schema (state_schema.json.gz), its addresses rebased by bias: as a process whose image sits
        bias above its link addresses has them (state_bias of a state it saved)."""
        path = build / "state_schema.json.gz"
        if not path.exists():
            raise SystemExit(f"statecanon: no schema in {build} (tools/state_schema.py {build})")
        s = json.load(gzip.open(path, "rt"))
        self.bias = bias
        self.exe = Path(s["exe"])
        if bias and (not s.get("symbols") or not s.get("ro")):  # (an older schema: read from the executable here)
            s["symbols"] = s.get("symbols") or list(self.nm_symbols())
            s["ro"] = s.get("ro") or [list(r) for r in self.readelf_ro()]
        rebase(s, bias)
        self.types = s["types"]
        self.type_names = s.get("type_names", {})
        # what heap blocks and recorded ranges hold, by the hash of the type's name (include/pc_state.h)
        self.heap_types = {int(k, 16): v for k, v in s.get("heap_types", {}).items()}
        self.counted = s.get("counted", {})  # struct tid -> its counted array (PC_STATE_COUNTED)
        # the source's rules by type name too: each compile unit has its own DWARF entry for a struct
        self.counted_by_name = {r["name"]: r for r in self.counted.values()}
        self.unions = s.get("unions", {})  # struct tid -> its union member's selector (PC_STATE_UNION)
        self.unions_by_name = {r["name"]: r for r in self.unions.values()}
        # a heap part's type by name, the same in every build (a DWARF offset is not): tid -> name
        self.tid_name = {}
        for name, t in sorted(self.type_names.items()):
            if self.types.get(t, {}).get("name") == name or t not in self.tid_name:
                self.tid_name[t] = name
        self.vars, self.collisions = keyed_variables(s["variables"])
        self.sections = {k: tuple(v) for k, v in s["sections"].items()}
        self.regions = {k: tuple(v) for k, v in s["regions"].items()}
        self.parts = s["parts"]
        # the state rules (docs/CANONICAL_STATES.md 17.2): absent in a schema from before them (revision 0)
        self.rules = s.get("rules") or {"language": RULES_LANGUAGE, "revision": 0, "list": []}
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
        by_retail = defaultdict(set)
        for f in s["functions"]:
            self.func_by_id.setdefault(self.func_identity(f), f["addr"])
            self.func_by_id.setdefault("fn:" + f["name"], f["addr"])
            if f.get("retail") is not None:
                by_retail[f["retail"]].add(f["addr"])
        # a game function by its retail address alone, where one function has it (a renamed one: docs 17.2)
        self.fn_by_retail = {r: next(iter(a)) for r, a in by_retail.items() if len(a) == 1}
        self.platform = {}
        for v in self.vars:
            if v.get("platform"):
                self.platform[(v["platform"], v["name"])] = v
        self.by_name = {}
        for v in self.vars:
            self.by_name.setdefault(v["name"], v)
        retail = {f["name"]: f.get("retail") for f in s["functions"]}
        self.text_syms = {}   # address -> [names]: static functions and identical bodies folded into one
        self.sym_addr = {}    # name -> address
        ro_named = []
        for name, a, size, typ in s.get("symbols") or self.nm_symbols():  # (older schemas: from the executable)
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
        # (lo, hi, file offset) of the file-backed data that does not change: .rodata, and the writable .data
        # that holds the game's string tables (xgsrc_*, .LC*: constant in practice; read from the file)
        self.ro = [tuple(r) for r in (s.get("ro") or self.readelf_ro())]
        self._file = None
        self._strings = None

    def nm_symbols(self):
        for line in subprocess.run(["nm", "-n", "-S", "--defined-only", str(self.exe)], capture_output=True,
                                   text=True).stdout.splitlines():
            m = re.match(r"([0-9a-f]+) (?:([0-9a-f]+) )?(\S) (\S+)$", line)
            if m:
                yield m.group(4), int(m.group(1), 16), int(m.group(2) or "0", 16), m.group(3)

    def readelf_ro(self):
        for line in subprocess.run(["readelf", "-SW", str(self.exe)], capture_output=True, text=True).stdout.splitlines():
            m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)", line)
            if m and m.group(2) == "PROGBITS" and "A" in m.group(6) and "X" not in m.group(6) and int(m.group(3), 16):
                lo = int(m.group(3), 16)
                yield lo, lo + int(m.group(5), 16), int(m.group(4), 16)

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
        if a is None:  # a game function renamed: its retail address, when one function of this build has it
            m = re.fullmatch(r"fn:([0-9a-f]{8}):.*", ident, re.S)
            if m:
                a = self.fn_by_retail.get(int(m.group(1), 16))
        return a

    @staticmethod
    def identity(v):
        return var_identity(v)

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

    def gaps(self, tid):
        """A struct's byte ranges outside every field (padding), as (lo, hi) pairs; cached."""
        if not hasattr(self, "_gaps"):
            self._gaps = {}
        g = self._gaps.get(tid)
        if g is None:
            t = self.types.get(tid, {})
            cover = bytearray(t.get("size", 0))
            for f in t.get("fields", []):
                if f.get("bits") is not None:
                    lo_ = f["bit"] // 8; hi_ = (f["bit"] + f["bits"] + 7) // 8
                else:
                    lo_ = f["off"]; hi_ = f["off"] + self.size(f["type"])
                for i in range(max(0, lo_), min(len(cover), hi_)):
                    cover[i] = 1
            g = []; i = 0
            while i < len(cover):
                if not cover[i]:
                    j = i
                    while j < len(cover) and not cover[j]:
                        j += 1
                    g.append((i, j)); i = j
                else:
                    i += 1
            self._gaps[tid] = g
        return g


# ---- the state rules (docs/CANONICAL_STATES.md 17.2) ----------------------------------------------

RULE_KINDS = ("was", "values", "reset", "removed", "default", "hashes")
# what each kind names, the first of these keys a rule has
RULE_TARGETS = {"was": ("field", "var", "typename", "tag", "fn"), "values": ("field", "var"), "reset": ("field", "var"),
                "removed": ("vars", "fields"), "default": ("var", "field"), "hashes": ("field",)}


def rule_int(x) -> bool:
    return isinstance(x, int) and not isinstance(x, bool) and -(1 << 63) <= x < (1 << 63)


def rule_target(r) -> str | None:
    """Which thing a rule names (the first of its kind's RULE_TARGETS it has), or None when the converter cannot
    read it: a kind it does not know, or a key missing or of another type. Such a rule is ignored, never refused."""
    if not isinstance(r, dict) or r.get("kind") not in RULE_KINDS:
        return None
    k = r["kind"]
    if k not in ("default", "hashes") and not rule_int(r.get("rev")):
        return None
    on = next((x for x in RULE_TARGETS[k] if x in r), None)
    strs = lambda x: isinstance(x, list) and all(isinstance(y, str) for y in x)  # noqa: E731
    if on is None or not (strs(r[on]) if on in ("fields", "vars") else isinstance(r[on], str)):
        return None
    if on in ("field", "fields") and not strs(r.get("tids")):
        return None
    if k == "was" and not isinstance(r.get("old"), str):
        return None
    if k == "values" and not (isinstance(r.get("map"), list) and all(
            isinstance(m, list) and len(m) == 2 and rule_int(m[0]) and rule_int(m[1]) for m in r["map"])):
        return None
    if k in ("removed", "default") and not rule_int(r.get("value")):
        return None
    return on


def type_hash(name: str) -> int:
    """pcStateTypeId (port/src/game/main/statetypes.c): FNV-1a of a type's name without its blanks; never 0."""
    h = 0x811C9DC5
    for c in name.encode():
        if c not in b" \t\n":
            h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h or 1


def map_value(v: int, pairs: list, forward: bool) -> int:
    """A values rule on one integer: forward old -> new, else new -> old; the first pair that matches, else v."""
    for old, new in pairs:
        if (old if forward else new) == v:
            return new if forward else old
    return v


class Rules:
    """The rules that apply between a writer's schema and a reader's. Only the newer one's history rules count (its
    revision is the higher; it carries every rule since the older one's), those with older < rev <= newer, applied
    forward (a newer reader) or backward (a newer writer); the reader's own properties (default, hashes) always.
    Each history rule names the newer build's own thing: a variable by identity, a struct's member by the newer
    build's type entries and the member's key there, a type by name, a host part's chunk tag."""

    def __init__(self, writer: dict, reader: dict):
        rev = lambda x: x.get("revision", 0) if isinstance(x, dict) and rule_int(x.get("revision", 0)) else 0  # noqa
        wr, rr = rev(writer), rev(reader)
        self.forward = wr < rr
        self.backward = wr > rr
        self.older = min(wr, rr)
        newer = reader if self.forward else writer if self.backward else {}
        listed = lambda x: x.get("list") if isinstance(x, dict) and isinstance(x.get("list"), list) else []  # noqa
        self.unknown = 0  # rules this converter cannot read (a kind it does not know, a damaged one): ignored
        hist = []
        for r in listed(newer):
            on = rule_target(r)
            if on is None:
                self.unknown += 1
            elif r["kind"] not in ("default", "hashes") and self.older < r["rev"] <= max(wr, rr):
                hist.append((r, on))
        hist.sort(key=lambda x: x[0]["rev"])  # stable: one revision's rules in the schema's order
        self.var_was, self.field_was, self.type_was, self.tag_was, self.fn_was = {}, {}, {}, {}, {}
        self.var_rules, self.field_rules = defaultdict(list), defaultdict(list)  # values, reset in revision order
        self.removed_vars, self.removed_fields = {}, {}
        named = {"var": self.var_was, "typename": self.type_was, "tag": self.tag_was, "fn": self.fn_was}
        for r, on in hist:
            if r["kind"] == "was":
                if on == "field":
                    for t in r["tids"]:
                        self.field_was.setdefault((t, r["field"]), r["old"])  # the earliest after the older's
                else:
                    named[on].setdefault(r[on], r["old"])
            elif r["kind"] in ("values", "reset"):
                if on == "field":
                    for t in r["tids"]:
                        self.field_rules[(t, r["field"])].append(r)
                else:
                    self.var_rules[r["var"]].append(r)
            elif r["kind"] == "removed":
                if on == "vars":
                    for v in r["vars"]:
                        self.removed_vars.setdefault(v, r["value"])
                else:
                    for t in r["tids"]:
                        for f in r["fields"]:
                            self.removed_fields.setdefault((t, f), r["value"])
        # a newer writer's things by what the older reader calls them; and those it calls otherwise now
        self.var_back = {old: cur for cur, old in self.var_was.items()}
        self.field_back = {(t, old): cur for (t, cur), old in self.field_was.items()}
        self.type_back = {old: cur for cur, old in self.type_was.items()}
        self.tag_back = {old: cur for cur, old in self.tag_was.items()}
        self.fn_back = {old: cur for cur, old in self.fn_was.items()}
        self.defaults_var, self.defaults_field, self.hashes = {}, {}, set()
        for r in listed(reader):  # the reader's own properties, whatever the revisions
            on = rule_target(r)
            if on is None or r["kind"] not in ("default", "hashes"):
                continue
            if r["kind"] == "default" and on == "var":
                self.defaults_var.setdefault(r["var"], r["value"])
            elif r["kind"] == "default":
                for t in r["tids"]:
                    self.defaults_field.setdefault((t, r["field"]), r["value"])
            else:
                for t in r["tids"]:
                    self.hashes.add((t, r["field"]))
        self.active = bool(hist or self.defaults_var or self.defaults_field or self.hashes)

    def source(self, ident: str, was: dict, back: dict):
        """What the writer calls the reader's thing ident: forward, its name before the newer reader's renames;
        backward, the newer writer's thing the older reader calls ident (none if the writer renamed its own
        ident since: that is another thing)."""
        if self.forward:
            return was.get(ident, ident)
        if self.backward:
            if ident in back:
                return back[ident]
            return None if ident in was else ident
        return ident

    def var_source(self, ident):
        return self.source(ident, self.var_was, self.var_back)

    def target(self, ident, was: dict, back: dict):
        """source the other way: the reader's name of the writer's thing ident (None: the reader has it not)."""
        if self.forward:
            if ident in back:
                return back[ident]
            return None if ident in was else ident
        if self.backward:
            return was.get(ident, ident)
        return ident

    def var_target(self, ident):
        return self.target(ident, self.var_was, self.var_back)

    def fn_target(self, ident: str) -> str:
        """The reader's identity of the writer's function ("fn:<retail>:<name>" or "fn:<name>"): its name as the
        reader calls it (a renamed function: PC_STATE_FN_WAS)."""
        head, _, name = ident.rpartition(":")
        new = self.target(name, self.fn_was, self.fn_back)
        return ident if new is None or new == name else f"{head}:{new}"

    def tag_source(self, tag):
        return self.source(tag, self.tag_was, self.tag_back)

    def type_name(self, name):
        """The reader's name of the writer's type name (heap parts, type hashes)."""
        if self.forward:
            if name in self.type_back:
                return self.type_back[name]
            return None if name in self.type_was else name
        if self.backward:
            return self.type_was.get(name, name)
        return name

    def field_source(self, rtid, wtid, key):
        """The writer's key of the reader's member key (of the reader's type rtid, the writer's value of wtid)."""
        if self.forward:
            return self.field_was.get((rtid, key), key)
        if self.backward:
            if (wtid, key) in self.field_back:
                return self.field_back[(wtid, key)]
            return None if (wtid, key) in self.field_was else key
        return key

    def field_target(self, rtid, wtid, wkey):
        """The reader's key of the writer's member key: field_source the other way (a union's selected view)."""
        if self.forward:
            if (rtid, wkey) in self.field_back:
                return self.field_back[(rtid, wkey)]
            return None if (rtid, wkey) in self.field_was else wkey
        if self.backward:
            return self.field_was.get((wtid, wkey), wkey)
        return wkey

    def history(self, var=None, field=None):
        """The values and reset rules of the newer build's variable or (type entry, key), in the order they apply."""
        rs = self.var_rules.get(var, []) if var is not None else self.field_rules.get(field, [])
        return rs if self.forward else rs[::-1]


    def values(self, rules: list, val, wtype: dict | None, schema_w) -> tuple[object, bool]:
        """A value through history() rules: whether a reset rule drops it, else the value through each values
        rule: an integer, or an integer array's elements ({"x", "w"}, read as the writer's element type is
        signed). Another shape of value is left as it is."""
        if any(r["kind"] == "reset" for r in rules):
            return val, True
        vs = [r["map"] for r in rules if r["kind"] == "values"]
        if not vs:
            return val, False
        if isinstance(val, int):
            for m in vs:
                val = map_value(val, m, self.forward)
            return val, False
        if isinstance(val, dict) and "x" in val and val.get("w") and val["w"] <= 8:
            w = val["w"]; b = bytes.fromhex(val["x"])
            signed = bool(wtype and wtype.get("k") == "arr" and schema_w.types.get(wtype.get("of"), {}).get("signed"))
            out = bytearray()
            for i in range(0, len(b) - len(b) % w, w):
                e = int.from_bytes(b[i:i + w], "little", signed=signed)
                for m in vs:
                    e = map_value(e, m, self.forward)
                out += (e & ((1 << (8 * w)) - 1)).to_bytes(w, "little")
            out += b[len(b) - len(b) % w:]
            return {**val, "x": bytes(out).hex()}, False
        return val, False


# ---- encoding -------------------------------------------------------------------------------------

class Encoder:
    def __init__(self, schema: Schema):
        self.s = schema
        self.warn = Counter()
        self.heap = None      # the heap's blocks (encode_heap), for pointers into them
        self.heap_lo = []
        self.arena = None     # the sound arena's (arena_blocks)
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

    def heap_pointer(self, v):
        return self.block_pointer("hb", self.heap, self.heap_lo, v)

    def block_pointer(self, key, blocks, lo, v):
        """A pointer into a block of the heap ("hb") or the sound arena ("sb"): the block's index, the part of
        it (block_layout), and by the part's type the element and field path (structs), the word (pointers, OT
        links), or the byte (primitives, raw); a header's bytes relative to the block's data."""
        i = bisect.bisect_right(lo, v + HEAP_HEADER if key == "hb" else v + ARENA_HEADER) - 1
        if i < 0:
            return None
        b = blocks[i]
        off = v - b["data"]
        head = HEAP_HEADER if key == "hb" else ARENA_HEADER
        if off > b["size"] or off < -head:
            return None
        parts = b.get("parts")
        if not parts or not 0 <= off < b["size"]:
            return {key: i, "o": off}
        k = bisect.bisect_right(b["part_lo"], off) - 1
        p = parts[k]; rel = off - p["off"]; spec = p["spec"]
        if isinstance(spec, dict):
            e = bisect.bisect_right(p["eo"], rel) - 1
            tid = spec["tid"]; rel -= p["eo"][e]
            path, rest = [], rel
            cr = counted_rule(self.s, tid) if p["cnt"][e] is not None else None
            if cr and rel >= cr[1]["off"]:  # in the counted array: past its declared length
                a = cr[1]; of = self.s.types[a["type"]]["of"]; es = self.s.size(of)
                j = (rel - a["off"]) // es
                sub, rest = self.path_of(of, (rel - a["off"]) % es)
                path = [array_key(self.s, tid, a), j] + sub
            else:
                path, rest = self.path_of(tid, rel)
            out = {key: i, "pt": k, "e": e, "p": path}
            if rest:
                out["b"] = rest
            return out
        if spec in ("ptrs", "otags"):
            return {key: i, "pt": k, "e": rel // 8, "b": rel % 8}
        return {key: i, "pt": k, "o": rel}

    def pointer(self, v, quiet=False):
        if v == 0:
            return None
        if self.heap is not None and self.s.regions["heapArena"][0] <= v <= self.s.regions["heapArena"][1]:
            hp = self.heap_pointer(v)
            if hp is not None:
                return hp
            if not quiet:
                self.warn["pointer into the heap outside every block (kept as an offset)"] += 1
        for name, key in (("heapArena", "heap"), ("gameStack", "stack"), ("pcScratchpad", "scratchpad")):
            lo, hi = self.s.regions.get(name, (0, 0))
            if lo <= v <= hi:
                return {key: v - lo}
        if self.arena is not None and self.arena["base"] <= v < self.arena["base"] + self.arena["var"]["size"]:
            bp = self.block_pointer("sb", self.arena["blocks"], self.arena["lo"], v)
            if bp is not None:
                return bp
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
            ur = rule_for(self.s, self.s.unions, self.s.unions_by_name, tid)
            gaps = self.s.gaps(tid)
            if gaps:
                pad = bytes(b for lo_, hi_ in gaps for b in mem[off + lo_:off + hi_])
                if any(pad):
                    out["pad"] = pad.hex()
            for f in t["fields"]:
                key = self.field_key(f, seen)
                if f.get("bits") is not None:
                    bit = f["bit"]; nb = f["bits"]
                    base = (bit // 8)
                    span = (bit % 8 + nb + 7) // 8
                    raw = int.from_bytes(mem[off + base:off + base + span], "little")
                    out[key] = (raw >> (bit % 8)) & ((1 << nb) - 1)
                    continue
                if ur is not None and f["name"] == ur["member"]:
                    view = selected_view(self.s, t, ur, mem, off)
                    if view is not None:  # the live view on the bytes (the others' bytes are leftovers)
                        n = self.s.size(f["type"])
                        out[key] = {"x": bytes(mem[off + f["off"]:off + f["off"] + n]).hex(), "sel": view["name"],
                                    "v": self.value(view["type"], mem, off + f["off"] + view["off"])}
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

def type_class(t: dict) -> str:
    k = t.get("k")
    return k if k in ("int", "float", "addr", "ptr", "otag", "scratch", "arr", "struct", "union") else "bytes"


def plain(schema: "Schema", tid) -> bool:
    """Whether a type holds no pointer (nor an ordering-table link, an address or a scratch word): its bytes are
    the same in every build, the PS1's for the game's types. Cached per schema; a type that holds itself (only a
    damaged schema's) is not plain."""
    cache = schema.__dict__.setdefault("_plain", {})
    if tid in cache:
        return cache[tid] is True
    t = schema.types.get(tid)
    if t is None:
        return False
    cache[tid] = None  # (being decided)
    k = t["k"]
    if k in ("ptr", "addr", "otag", "scratch"):
        r = False
    elif k == "arr":
        r = t.get("of") not in schema.types or t["n"] == 0 or plain(schema, t["of"])
    elif k in ("struct", "union"):
        r = all(plain(schema, f["type"]) for f in t["fields"])
    else:
        r = True
    cache[tid] = r
    return r


def bytes_differ(sw: "Schema", wt, sr: "Schema", rt) -> bool:
    """Whether a member's type in the writer and the reader give its bytes another meaning: another kind, size, or
    an array's elements of another size (where a game member's PS1 bytes are what carries)."""
    a, b = sw.types.get(wt), sr.types.get(rt)
    if a is None or b is None:
        return False
    if type_class(a) != type_class(b) or a.get("size") != b.get("size"):
        return True
    return a["k"] == "arr" and sw.size(a.get("of")) != sr.size(b.get("of"))


class Decoder:
    def __init__(self, schema: Schema, writer: Schema | None = None, rules: Rules | None = None):
        """schema: the reader's. writer: the writer's schema (a conversion: the types of what the state holds, so
        a value of another type than the reader's is carried by its bytes or left out, not refused; docs 17.2),
        rules: the state rules between the two (Rules)."""
        self.s = schema
        self.w = writer
        self.rules = rules if rules is not None and rules.active else None
        self.warn = Counter()
        self.applied = Counter()  # what the rules and the type changes did (not losses: those are warnings)
        self.heap = None  # the target layout's blocks (decode_heap_layout)
        self.arena = None  # the sound arena's (decode_arena_layout)
        self._defaults = {}

    # ---- types changed between the builds, and the rules ----

    def writer_type(self, wt):
        return self.w.types.get(wt) if self.w is not None and wt is not None else None

    def writer_bytes(self, wt, val) -> bytes:
        """A writer's value as its own build lays it out (a decode with its schema, no rules)."""
        tmp = bytearray(self.w.size(wt))
        Decoder(self.w).put(wt, val, tmp, 0)
        return bytes(tmp)

    def mismatch(self, wt, tid, val) -> bool:
        """Whether the writer's value of type wt cannot be put as the reader's type tid by value."""
        a, b = self.writer_type(wt), self.s.types.get(tid)
        if a is None or b is None:
            return False
        ca, cb = type_class(a), type_class(b)
        if cb == "scratch":
            return False
        if ca == cb:
            if ca == "float":
                return a["size"] != b["size"]
            if ca == "arr" and isinstance(val, dict) and "x" in val:  # integers (or bytes) into other elements
                et = self.s.types.get(b.get("of"))
                return et is not None and b["n"] > 0 and type_class(et) not in ("int", "bytes")
            return False
        if {ca, cb} == {"int", "addr"}:  # an address as a number, a number as an address
            return cb == "int" and not isinstance(val, int)
        return True

    def carry_bytes(self, tid, val, buf, off, wt) -> None:
        b = self.writer_bytes(wt, val)
        n = min(len(b), self.s.size(tid))
        buf[off:off + n] = b[:n]
        self.applied["value of another type carried as its bytes"] += 1

    def reshape(self, tid, val, buf, off, wt) -> bool:
        """A value of another kind of type: by its bytes when neither type holds a pointer, else not carried (the
        reader's defaults inside it)."""
        if plain(self.w, wt) and plain(self.s, tid):
            self.carry_bytes(tid, val, buf, off, wt)
            return True
        self.warn["value of another type in the state (not carried)"] += 1
        self.put_defaults(tid, buf, off)
        return False

    def defaults_in(self, tid) -> list:
        """The reader's defaults inside a type (its struct members', arrays of structs' too; not through pointers or
        unions): (offset of the struct, member, value), cached (a type that holds itself: none)."""
        if self.rules is None or not self.rules.defaults_field:
            return []
        if tid in self._defaults:
            return self._defaults[tid] or []
        self._defaults[tid] = None
        out = []
        t = self.s.types.get(tid, {})
        if t.get("k") == "struct":
            seen = set()
            for f in t["fields"]:
                key = Encoder.field_key(None, f, seen)
                d = self.rules.defaults_field.get((tid, key))
                if d is not None:
                    out.append((0, f, d))
                elif f.get("bits") is None:
                    out += [(f["off"] + o, g, v) for o, g, v in self.defaults_in(f["type"])]
        elif t.get("k") == "arr" and t.get("of") in self.s.types and t["n"]:
            sub = self.defaults_in(t["of"])
            es = self.s.size(t["of"])
            out = [(i * es + o, g, v) for i in range(t["n"]) for o, g, v in sub] if sub else []
        self._defaults[tid] = out
        return out

    def put_defaults(self, tid, buf, off) -> int:
        n = 0
        for o, f, v in self.defaults_in(tid):
            self.put_int(f, v, buf, off + o); n += 1
        return n

    def put_int(self, f, value, buf, base) -> None:
        """An integer into a member (f: a struct's field, or {"off": 0, "type": a variable's}): a bitfield's bits,
        an integer, or each element of an integer array."""
        if f.get("bits") is not None:
            bit = f["bit"]; nb = f["bits"]; b0 = bit // 8; span = (bit % 8 + nb + 7) // 8
            raw = int.from_bytes(buf[base + b0:base + b0 + span], "little")
            m = ((1 << nb) - 1) << (bit % 8)
            raw = (raw & ~m) | ((value << (bit % 8)) & m)
            buf[base + b0:base + b0 + span] = raw.to_bytes(span, "little")
            return
        t = self.s.types.get(f["type"], {}); at = base + f["off"]
        if t.get("k") == "arr":
            es = self.s.size(t.get("of"))
            for i in range(t["n"] if es else 0):
                buf[at + i * es:at + (i + 1) * es] = (value & ((1 << (8 * es)) - 1)).to_bytes(es, "little")
        elif t.get("k") == "int":
            n = t["size"]
            buf[at:at + n] = (value & ((1 << (8 * n)) - 1)).to_bytes(n, "little")

    def remap_hash(self, h: int) -> int:
        """A heap type hash of the writer (a block's header, a recorded range's: PC_HEAP_OF's name) as the reader
        names that type, where a rule renamed it and the reader knows the hash; else as it is."""
        R = self.rules
        if R is None or not h or not (R.type_was or R.type_back) or self.w is None:
            return h
        e = self.w.heap_types.get(h)
        if e is None:
            return h
        name = e["name"]
        base = name[:-3] if name.endswith("[1]") else name.rstrip("*")
        new = R.type_name(base)
        if new is None or new == base:
            return h
        h2 = type_hash(new + name[len(base):])
        if h2 not in self.s.heap_types:
            return h
        self.applied["type hash remapped by a state rule"] += 1
        return h2

    def writer_fields(self, wt) -> dict:
        """The writer struct's fields by key, the last of a key (what its dict holds)."""
        t = self.writer_type(wt)
        if t is None or t.get("k") not in ("struct", "union"):
            return {}
        seen = set(); out = {}
        for f in t["fields"]:
            out[Encoder.field_key(None, f, seen)] = f
        return out

    def retail_carry(self, f, val, wt, buf, off) -> bool:
        """A game member (an offset tag) the writer has not as a value of its type: its PS1 bytes, from the writer's
        members of the same struct that cover them and hold no pointer. Whether any byte came."""
        wtt = self.writer_type(wt)
        if f.get("retail") is None or f.get("bits") is not None or wtt is None or wtt.get("k") != "struct" or \
                not isinstance(val, dict) or not plain(self.s, f["type"]):
            return False
        lo = f["retail"]; size = self.s.size(f["type"]); hi = lo + size
        if size <= 0:
            return False
        got = bytearray(size); cover = bytearray(size)
        for key, g in self.writer_fields(wt).items():
            if g.get("retail") is None or g.get("bits") is not None or key not in val or not plain(self.w, g["type"]):
                continue
            a, b = max(lo, g["retail"]), min(hi, g["retail"] + self.w.size(g["type"]))
            if a >= b:
                continue
            gb = self.writer_bytes(g["type"], val[key])
            got[a - lo:b - lo] = gb[a - g["retail"]:b - g["retail"]]
            cover[a - lo:b - lo] = b"\1" * (b - a)
        if not any(cover):
            return False
        base = off + f["off"]; i = 0
        while i < size:
            if not cover[i]:
                i += 1; continue
            j = i
            while j < size and cover[j]:
                j += 1
            buf[base + i:base + j] = got[i:j]; i = j
        self.applied["carried by its PS1 bytes"] += 1
        return True

    def field_lost(self, tid, wt, key, f, buf, off, why) -> None:
        """A reader's member the state does not carry (why: "missing", "reset", "type"): a newer writer's removed
        value, else the member's default or those inside it, else zero."""
        R = self.rules
        if R is not None and why == "missing" and R.backward and (wt, key) in R.removed_fields:
            self.put_int(f, R.removed_fields[(wt, key)], buf, off)
            self.warn["field removed by a later build (given the value its rule names)"] += 1
            return
        d = R.defaults_field.get((tid, key)) if R is not None else None
        if d is not None:
            self.put_int(f, d, buf, off); n = 1
        else:
            n = self.put_defaults(f["type"], buf, off + f["off"]) if f.get("bits") is None and why != "type" else 0
        if why == "missing":
            self.warn["field missing in the state (given its default)" if n else "field missing in the state (left zero)"] += 1
        elif why == "reset":
            self.warn["field reset by a state rule (not carried)"] += 1

    def offset_of(self, tid, path, wtid=None):
        """A field path's offset from a value of type tid; the path's keys are the writer's, of its type wtid
        (followed alongside: a renamed member's key by the rules)."""
        off = 0
        R = self.rules
        for step in path:
            t = self.s.types.get(tid)
            if t is None:
                return None
            wtt = self.writer_type(wtid)
            if isinstance(step, dict):  # a union's view
                name = R.field_target(tid, wtid, step["u"]) if R is not None else step["u"]
                f = next((f for f in t.get("fields", []) if f["name"] == name), None)
                if t["k"] != "union" or f is None:
                    return None
                wv = next((g for g in wtt.get("fields", []) if g["name"] == step["u"]), None) if wtt else None
                tid = f["type"]; wtid = wv["type"] if wv else None
                continue
            if isinstance(step, int):
                if t["k"] != "arr":
                    return None
                off += step * self.s.size(t["of"])
                tid = t["of"]; wtid = wtt.get("of") if wtt and wtt.get("k") == "arr" else None
            else:
                if t["k"] != "struct":
                    return None
                key = R.field_target(tid, wtid, step) if R is not None else step
                seen = set(); hit = None
                for f in t["fields"]:
                    if Encoder.field_key(None, f, seen) == key:
                        hit = f
                        break
                if hit is None:
                    return None
                wf = None
                if wtt is not None and wtt.get("k") == "struct":
                    seen = set()
                    wf = next((g for g in wtt["fields"] if Encoder.field_key(None, g, seen) == step), None)
                off += hit["off"]
                tid = hit["type"]; wtid = wf["type"] if wf else None
        return off

    def pointer(self, p):
        if p is None:
            return 0
        if "raw" in p:
            return p["raw"]
        if "hb" in p or "sb" in p:
            key = "hb" if "hb" in p else "sb"
            blocks = self.heap if key == "hb" else (self.arena or {}).get("blocks")
            if blocks is None or p[key] >= len(blocks):
                self.warn["pointer into a heap block not in the state"] += 1
                return 0
            b = blocks[p[key]]
            if "pt" not in p:
                return b["data"] + p.get("o", 0)
            if p["pt"] >= len(b.get("parts", [])):
                self.warn["pointer into a heap part not in the state"] += 1
                return b["data"]
            lay = b["parts"][p["pt"]]; base = b["data"] + lay["off"]
            if "o" in p:
                return base + p["o"]
            part = b["rec"]["parts"][p["pt"]]
            t = part.get("t")
            if isinstance(t, dict):
                if lay.get("tid") is None:
                    self.warn["heap pointer path not in this build"] += 1
                    return base
                o = self.offset_of(lay["tid"], p.get("p", []), t.get("tid") if self.w is not None else None)
                if o is None:
                    self.warn["heap pointer path not in this build"] += 1
                    o = 0
                eo = lay["eo"][p["e"]] if p["e"] < len(lay["eo"]) else p["e"] * self.s.size(lay["tid"])
                return base + eo + o + p.get("b", 0)
            return base + p["e"] * 8 + p.get("b", 0)
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
            ident = self.rules.fn_target(p["fn"]) if self.rules is not None else p["fn"]
            a = self.s.fn_address(ident)
            if a is None:
                self.warn["function not in this build"] += 1
                return 0
            return a
        vid = self.rules.var_target(p["v"]) if self.rules is not None else p["v"]
        var = self.s.by_id.get(vid) if vid is not None else None
        if var is None:
            self.warn["pointer to a variable not in this build"] += 1
            return 0
        wv = self.w.by_id.get(p["v"]) if self.w is not None else None
        o = self.offset_of(var["tid"], p.get("p", []), wv["tid"] if wv else None)
        if o is None:
            self.warn["pointer path not in this build"] += 1
            o = 0
        return var["addr"] + o + p.get("b", 0)

    def put(self, tid, val, buf, off, wt=None):
        """The reader's bytes of a canonical value at off of buf, as type tid. wt: the writer's type of the value
        (a conversion's: Decoder(writer=...)): a value of another kind is carried by its bytes or left out."""
        t = self.s.types.get(tid)
        if t is None:
            return None
        if self.w is not None and wt is not None and self.mismatch(wt, tid, val):
            return self.reshape(tid, val, buf, off, wt)
        if val is None and t["k"] not in ("ptr",):
            return None
        wtt = self.writer_type(wt)
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
                b = bytes.fromhex(val["x"])
                w = val.get("w", 0); et = self.s.types.get(t.get("of"), {}); es = et.get("size", 0)
                if wtt is not None and w and es and w != es and et.get("k") == "int":
                    # integers of another width: each element by its value (as the writer's elements are signed)
                    signed = bool(wtt.get("k") == "arr" and self.w.types.get(wtt.get("of"), {}).get("signed"))
                    for i in range(min(len(b) // w, t["n"])):
                        e = int.from_bytes(b[i * w:(i + 1) * w], "little", signed=signed)
                        buf[off + i * es:off + (i + 1) * es] = (e & ((1 << (8 * es)) - 1)).to_bytes(es, "little")
                    self.applied["integers of another width carried by value"] += 1
                else:
                    n = min(len(b), t["size"]); buf[off:off + n] = b[:n]
            else:
                es = self.s.size(t["of"])
                wof = wtt.get("of") if wtt is not None and wtt.get("k") == "arr" else None
                for i, e in enumerate(val[:t["n"]]):
                    self.put(t["of"], e, buf, off + i * es, wof)
        elif k == "struct":
            seen = set()
            if "pad" in val:  # the saving build's bytes outside every field, where this build has the same layout
                gaps = self.s.gaps(tid); pad = bytes.fromhex(val["pad"]); at = 0
                if sum(h - l for l, h in gaps) == len(pad):
                    for lo_, hi_ in gaps:
                        buf[off + lo_:off + hi_] = pad[at:at + hi_ - lo_]; at += hi_ - lo_
            wf = self.writer_fields(wt) if wtt is not None else {}
            R = self.rules
            for f in t["fields"]:
                key = Encoder.field_key(None, f, seen)
                src = R.field_source(tid, wt, key) if R is not None else key
                if src is None or src not in val:
                    if not self.retail_carry(f, val, wt, buf, off):
                        self.field_lost(tid, wt, key, f, buf, off, "missing")
                    continue
                if src != key:
                    self.applied["renamed by a state rule"] += 1
                child = val[src]
                cwt = wf[src]["type"] if src in wf else None
                if R is not None:
                    rs = R.history(field=(tid, key) if R.forward else (wt, src))
                    if rs:
                        child, reset = R.values(rs, child, self.writer_type(cwt), self.w)
                        if reset:
                            self.field_lost(tid, wt, key, f, buf, off, "reset")
                            continue
                        if any(r["kind"] == "values" for r in rs):
                            self.applied["values mapped by a state rule"] += 1
                    if (tid, key) in R.hashes and isinstance(child, int) and 0 <= child <= 0xFFFFFFFF:
                        child = self.remap_hash(child)
                if f.get("bits") is not None:
                    if self.w is not None and not isinstance(child, int):
                        self.warn["value of another type in the state (not carried)"] += 1
                        self.field_lost(tid, wt, key, f, buf, off, "type")
                        continue
                    bit = f["bit"]; nb = f["bits"]; base = bit // 8; span = (bit % 8 + nb + 7) // 8
                    raw = int.from_bytes(buf[off + base:off + base + span], "little")
                    m = ((1 << nb) - 1) << (bit % 8)
                    raw = (raw & ~m) | ((child << (bit % 8)) & m)
                    buf[off + base:off + base + span] = raw.to_bytes(span, "little")
                    continue
                if isinstance(child, dict) and "sel" in child:
                    v = child; n = self.s.size(f["type"]); x = bytes.fromhex(v["x"])[:n]
                    buf[off + f["off"]:off + f["off"] + len(x)] = x
                    sel = v["sel"]
                    if R is not None:  # the writer's view by the reader's name
                        wut = self.writer_type(cwt)
                        sel = R.field_target(f["type"], cwt, sel) if wut is not None else sel
                    view = next((m for m in self.s.types.get(f["type"], {}).get("fields", []) if m["name"] == sel), None)
                    if view is None:
                        self.warn["union view not in this build (bytes kept)"] += 1
                    else:
                        if sel != v["sel"]:
                            self.applied["renamed by a state rule"] += 1
                        wv = next((m for m in (self.writer_type(cwt) or {}).get("fields", []) if m["name"] == v["sel"]), None)
                        self.put(view["type"], v["v"], buf, off + f["off"] + view["off"], wv["type"] if wv else None)
                    continue
                if cwt is not None and f.get("retail") is not None and bytes_differ(self.w, cwt, self.s, f["type"]) and \
                        self.retail_carry(f, val, wt, buf, off):
                    continue
                if self.put(f["type"], child, buf, off + f["off"], cwt) is False:
                    self.field_lost(tid, wt, key, f, buf, off, "type")
        elif k == "union" and isinstance(val, dict) and "u" in val:
            wv = {f["name"]: f for f in (wtt or {}).get("fields", [])} if wtt is not None else {}
            for f in union_views(self.s, t):  # least typed first: the fully typed view writes last
                name = self.rules.field_source(tid, wt, f["name"]) if self.rules is not None else f["name"]
                if name is None or name not in val["u"]:
                    self.warn["union view not in the state"] += 1
                    continue
                if name != f["name"]:
                    self.applied["renamed by a state rule"] += 1
                self.put(f["type"], val["u"][name], buf, off + f["off"], wv[name]["type"] if name in wv else None)
        else:
            b = bytes.fromhex(val["x"]); n = min(len(b), t["size"]); buf[off:off + n] = b[:n]
        return None


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


def selected_view(schema: "Schema", t: dict, rule: dict, mem, off: int):
    """The union member's view its selector names (PC_STATE_UNION), or None (another value: every view)."""
    sf = next((f for f in t["fields"] if f["name"] == rule["selector"]), None)
    mf = next((f for f in t["fields"] if f["name"] == rule["member"]), None)
    if sf is None or mf is None or sf.get("bits") is not None:
        return None
    n = schema.size(sf["type"])
    sel = int.from_bytes(mem[off + sf["off"]:off + sf["off"] + n], "little")
    if not 0 <= sel < len(rule["views"]):
        return None
    return next((v for v in schema.types.get(mf["type"], {}).get("fields", []) if v["name"] == rule["views"][sel]), None)


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


def pointed_struct(schema: Schema, tid):
    """The struct or union a data pointer type points at (the element of the array it points at), or None."""
    t = schema.types.get(tid, {})
    if t.get("k") != "ptr" or t.get("fn") or t.get("to") not in schema.types:
        return None
    return t["to"] if schema.size(t["to"]) and schema.types[t["to"]].get("k") in ("struct", "union") else None


def part_array(schema: Schema, tag: str, size: int):
    """A host part's chunk that holds the array a pointer variable points at (hw.queue: xgStateIo(s, "queue",
    queue, count * sizeof(*queue))): the element type, or None."""
    pt = part_target(schema, tag)
    et = pointed_struct(schema, pt[2]) if pt is not None else None
    return et if et is not None and size % schema.size(et) == 0 else None


def same_bias(state: Path, h: dict, schema: Schema) -> None:
    if h.get("image", 0) != schema.bias:
        raise SystemExit(f"statecanon: {state} was saved at load bias {h.get('image', 0):#x}; its schema is rebased by "
                         f"{schema.bias:#x} (Schema(build, state_bias(state)))")


def encode(state: Path, schema: Schema, heap: bool = True, infer: bool = False) -> tuple[dict, Encoder]:
    h, body = sc.read_state(state)
    same_bias(state, h, schema)
    enc = Encoder(schema)
    records, raw = [], []
    covered = Counter(); total = Counter()
    chunks = {t: body[o:o + n] for t, o, n in sc.chunks(body)}
    heap_doc = encode_heap(schema, chunks, enc, infer) if heap and "mem.heap" in chunks else None
    for tag, at, size in sc.chunks(body):
        data = memoryview(body)[at:at + size]
        if tag == "mem.heap" and heap_doc is not None:
            covered["heap"] += size; total["heap"] += size
            continue
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
                    if v.get("const"):  # the build's own constant: not carried (a reader keeps its own)
                        for i in range(o, min(o + v["size"], size)):
                            mask[i] = 1
                        continue
                    enc.current = v["name"]
                    if enc.arena is not None and v is enc.arena["var"]:
                        records.append({"id": v["id"], "arena": encode_arena(enc, data, o)})
                    else:
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
            et = part_array(schema, tag, size)
            if et is not None:  # the array the variable points at, element by element
                enc.current = tag; es = schema.size(et)
                records.append({"part": tag, "id": var["id"], "p": path, "arr": stable_name(schema, et) or et,
                                "v": [enc.value(et, data, k * es) for k in range(size // es)]})
                covered["host parts"] += size; total["host parts"] += size
                continue
        why = "same build only (P2: heap, P3/P4: stack)" if tag.startswith("mem.") else "part not in the schema"
        raw.append({"tag": tag, "b64": base64.b64encode(bytes(data)).decode(), "why": why})
        total["raw" if tag.startswith("mem.") else "host parts"] += size
    doc = {"format": FORMAT, "header": h, "records": records, "raw": raw, "heap": heap_doc,
           "coverage": {k: [covered[k], total[k]] for k in total}}
    return doc, enc


STACK_CHUNKS = ("mem.stack.sp", "mem.stack.low", "mem.stack", "mem.guard")


def safe_point_of(doc: dict) -> int:
    """The safe point a state was saved at (port/src/game/main/statetypes.c safePointId; 0: none)."""
    r = next((r for r in doc["records"] if r["id"].endswith(":pc:safePointId")), None)
    return r["v"] if r is not None and isinstance(r.get("v"), int) else 0


def var_lost(dec: "Decoder", v: dict, buf, off: int, why: str) -> None:
    """A reader's variable the state does not carry (why: "missing", "reset", "type"): a newer writer's removed
    value, else its default or those inside it, else zero."""
    R = dec.rules
    whole = {"off": 0, "type": v["tid"]}
    if R is not None and why == "missing" and R.backward and v["id"] in R.removed_vars:
        dec.put_int(whole, R.removed_vars[v["id"]], buf, off)
        dec.warn["variable removed by a later build (given the value its rule names)"] += 1
        return
    d = R.defaults_var.get(v["id"]) if R is not None else None
    if d is not None:
        dec.put_int(whole, d, buf, off); n = 1
    else:
        n = dec.put_defaults(v["tid"], buf, off) if why != "type" else 0
    if why == "missing":
        dec.warn["variable not in the state (given its default)" if n else "variable not in the state (left zero)"] += 1
    elif why == "reset":
        dec.warn["variable reset by a state rule (not carried)"] += 1


def put_variable(dec: "Decoder", v: dict, r: dict, wid: str, buf, off: int) -> None:
    """A reader's variable from the writer's record (its identity wid there): the rules' values, a game
    variable's PS1 bytes where its type changed, else the value as the reader's type."""
    val = r["v"]
    wv = dec.w.by_id.get(wid) if dec.w is not None else None
    wt = wv["tid"] if wv is not None else None
    R = dec.rules
    if wid != v["id"]:
        dec.applied["renamed by a state rule"] += 1
    if R is not None:
        rs = R.history(var=v["id"] if R.forward else wid)
        if rs:
            val, reset = R.values(rs, val, dec.writer_type(wt), dec.w)
            if reset:
                var_lost(dec, v, buf, off, "reset")
                return
            if any(x["kind"] == "values" for x in rs):
                dec.applied["values mapped by a state rule"] += 1
    if wt is not None and v.get("module") and v.get("retail") is not None and bytes_differ(dec.w, wt, dec.s, v["tid"]) \
            and plain(dec.w, wt) and plain(dec.s, v["tid"]):
        dec.carry_bytes(v["tid"], val, buf, off, wt)  # a game variable's PS1 bytes
        return
    if dec.put(v["tid"], val, buf, off, wt) is False:
        var_lost(dec, v, buf, off, "type")


def decode(doc: dict, schema: Schema, template: Path, stackless: bool = False,
           writer: Schema | None = None) -> tuple[bytes, Decoder]:
    """The raw body for this build, chunk by chunk in the template's order (P1: the template is a raw state
    of the same build; later the order comes from the build's own part list). stackless: without the game's
    frames, which no other build can take (a state saved at a safe point: the load starts the game again
    there, include/pc_state.h). writer: the writing build's schema (a conversion): its types and state rules
    (docs/CANONICAL_STATES.md 17.2) apply; without it, the document's values go in as they are."""
    if stackless and not safe_point_of(doc):
        raise SystemExit("statecanon: a stackless state needs one saved at a safe point (XG_STATE_SAVE_SAFE)")
    th, tbody = sc.read_state(template)
    same_bias(template, th, schema)
    rules = Rules(writer.rules, schema.rules) if writer is not None else None
    dec = Decoder(schema, writer, rules)
    if rules is not None and rules.unknown:
        dec.warn["state rule this build cannot read (ignored)"] += rules.unknown
    R = dec.rules
    by_id = {r["id"]: r for r in doc["records"] if "part" not in r}
    by_part = {r["part"]: r for r in doc["records"] if "part" in r}
    raws = {r["tag"]: r for r in doc["raw"]}
    if doc.get("heap"):
        decode_heap_layout(doc["heap"], schema, dec)
    for r in doc["records"]:
        rid = R.var_target(r["id"]) if R is not None and "arena" in r else r["id"]
        if "arena" in r and rid in schema.by_id:
            decode_arena_layout(r["arena"], schema, schema.by_id[rid], dec)
    out = bytearray()
    for tag, at, size in sc.chunks(tbody):
        if stackless and tag in STACK_CHUNKS:  # no frames: stack.sp 0 says so (savestate.c memoryRegions)
            t = tag.encode()
            data = b"" if tag == "mem.stack" else bytes(8)
            out += bytes([len(t)]) + t + struct.pack("<I", len(data)) + data
            continue
        buf = bytearray(size)
        if tag == "mem.heap" and doc.get("heap"):
            buf = decode_heap(doc["heap"], schema, dec, size)
            t = tag.encode()
            out += bytes([len(t)]) + t + struct.pack("<I", size) + buf
            continue
        m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
        ptag = R.tag_source(tag) if R is not None and not m else tag
        if m and f"{m.group(1)}.{m.group(2)}" in schema.sections:
            lo, hi = schema.sections[f"{m.group(1)}.{m.group(2)}"]
            if m.group(2) == "data":
                # its bytes outside every variable are this build's own, as the template has them: constants no
                # variable names, which follow the code (a switch's jump table: the module link moves a game
                # file's read-only data here, tools/pc_srcdata.py; MinGW keeps the tables there), not the writer's
                buf[:] = tbody[at:at + size]
                for v in schema.vars:
                    if v.get("module") == m.group(1) and lo <= v["addr"] < hi:
                        o = v["addr"] - lo; n = max(0, min(o + v["size"], size) - o)
                        buf[o:o + n] = bytes(n)
            elif tag in raws:
                b = base64.b64decode(raws[tag]["b64"]); buf[:len(b)] = b[:size]
            for v in schema.vars:
                if v.get("module") == m.group(1) and lo <= v["addr"] < hi:
                    if v.get("const"):  # this build's own constant, as the template has it
                        o = v["addr"] - lo
                        buf[o:o + v["size"]] = tbody[at + o:at + min(o + v["size"], size)]
                        continue
                    wid = R.var_source(v["id"]) if R is not None else v["id"]
                    r = by_id.get(wid) if wid is not None else None
                    if r is None:
                        var_lost(dec, v, buf, v["addr"] - lo, "missing")
                        continue
                    if "arena" in r:
                        put_arena(dec, r["arena"], buf, v["addr"] - lo, v["size"])
                        continue
                    put_variable(dec, v, r, wid, buf, v["addr"] - lo)
        elif ptag is not None and ptag in by_part:
            r = by_part[ptag]
            if ptag != tag:
                dec.applied["renamed by a state rule"] += 1
            pt = part_target(schema, tag)
            wpt = part_target(writer, ptag) if writer is not None else None
            if pt is not None and "arr" in r:
                # of the element this build's variable points at: "arr" names the writer's, by its DWARF offset when
                # it has no name (hw.c's Pending: MinGW's LTO drops the typedef), which only the same build has
                et = pointed_struct(schema, pt[2])
                wet = pointed_struct(writer, wpt[2]) if wpt is not None else None
                if et is None:
                    dec.warn["array part not a struct pointer in this build (left empty)"] += 1
                    buf = bytearray()
                else:
                    es = schema.size(et)
                    buf = bytearray(len(r["v"]) * es)
                    for k, v in enumerate(r["v"]):
                        dec.put(et, v, buf, k * es, wet)
            elif pt is not None:
                dec.put(pt[2], r["v"], buf, 0, wpt[2] if wpt is not None else None)
        elif ptag is not None and ptag in raws:
            if ptag != tag:
                dec.applied["renamed by a state rule"] += 1
            b = base64.b64decode(raws[ptag]["b64"]); buf[:len(b)] = b[:size]
        else:
            dec.warn["chunk not in the state (left zero)"] += 1
        t = tag.encode()
        out += bytes([len(t)]) + t + struct.pack("<I", len(buf)) + buf  # (a pointed-at array: the state's count)
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


def masked_heap(schema: Schema, body: bytes) -> bytes:
    """A body with its heap's caller fields (code addresses of the saving build: heap debugging) and free
    blocks' contents (leftovers) zeroed: what a canonical state does not carry."""
    chunks = {t: (o, n) for t, o, n in sc.chunks(body)}
    if "mem.heap" not in chunks:
        return body
    o0, n0 = chunks["mem.heap"]
    out = bytearray(body)
    heap_lo = schema.regions["heapArena"][0]
    blocks = heap_blocks(schema, {t: body[o:o + n] for t, (o, n) in chunks.items()})
    for b in blocks:
        hdr = o0 + b["data"] - HEAP_HEADER - heap_lo
        if b.get("end"):
            bits = int.from_bytes(out[hdr + 8:hdr + 12], "little") & ~0x1FFFFF
            out[hdr + 8:hdr + 16] = bits.to_bytes(4, "little") + bytes(4)
            continue
        bits = int.from_bytes(out[hdr + 8:hdr + 12], "little") & ~0x1FFFFF
        out[hdr + 8:hdr + 12] = bits.to_bytes(4, "little")
        if not b["user"]:
            out[hdr + 12:hdr + 16] = bytes(4)  # a free block names no type (states before HeapBlock mC_pcType: a pad)
            d = o0 + b["data"] - heap_lo
            out[d:d + b["size"]] = bytes(b["size"])
    if blocks:  # after the end marker: unused
        e = o0 + blocks[-1]["data"] - heap_lo
        out[e:o0 + n0] = bytes(max(0, o0 + n0 - e))
    arena = arena_blocks(schema, {t: body[o:o + n] for t, (o, n) in chunks.items()})
    if arena is not None:  # the sound arena: what lies outside its blocks, and the headers' pads
        a = chunks["mem." + arena["sec"]][0] + arena["off"]
        keep = bytearray(arena["var"]["size"])
        for b in arena["blocks"]:
            keep[b["hdr"]:b["hdr"] + 28] = b"\1" * 28
            keep[b["hdr"] + ARENA_HEADER:b["hdr"] + ARENA_HEADER + b["size"]] = b"\1" * b["size"]
        for i, k in enumerate(keep):
            if not k:
                out[a + i] = 0
    return bytes(out)


def compare(a: bytes, b: bytes, schema: Schema, limit=12) -> list[str]:
    a, b = masked_heap(schema, a), masked_heap(schema, b)
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
    body, dec = decode(doc, b, b_state, writer=a)
    _, bbody = sc.read_state(b_state)
    body, bbody = masked_heap(b, body), masked_heap(b, bbody)
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


# ---- the game heap (phase P2) -----------------------------------------------------------------

HEAP_HEADER = 16  # sizeof(HeapBlock) on the PC (include/main/memory.h)
# The sound arena's block header on the PC (src/main/sound.c ArenaBlock, rounded to 16): u16 m0_flags, u16 m2,
# s32 m4, u8* m8_end (the data's end), ArenaBlock* mC_next (the next used block), u32 pcType (PC_HEAP_OF)
ARENA_HEADER = 32


def arena_blocks(schema: Schema, chunks: dict):
    """The sound arena (pcSoundArena, src/main/sound.c: a second allocator in main's BSS): its head block at
    its start, then the used blocks in their chain, each with its header's offset, data address, size
    (m8_end - data), flags, m2, m4 and type. None when the build or the state has no arena."""
    v = schema.by_name.get("pcSoundArena")
    if v is None:
        return None
    sec = next((k for k, (lo, hi) in schema.sections.items() if lo <= v["addr"] < hi), None)
    if sec is None or "mem." + sec not in chunks:
        return None
    mem = chunks["mem." + sec]; base = v["addr"] - schema.sections[sec][0]; a0 = v["addr"]
    blocks = []; o = 0
    for _ in range(1 << 12):
        h = base + o
        end = int.from_bytes(mem[h + 8:h + 16], "little"); nxt = int.from_bytes(mem[h + 16:h + 24], "little")
        data = a0 + o + ARENA_HEADER
        size = end - data if end else 0
        if not 0 <= o + ARENA_HEADER + size <= v["size"]:
            size = 0
        blocks.append({"hdr": o, "data": data, "size": size, "user": 1, "flags": int.from_bytes(mem[h:h + 2], "little"),
                       "m2": int.from_bytes(mem[h + 2:h + 4], "little"),
                       "m4": int.from_bytes(mem[h + 4:h + 8], "little", signed=True),
                       "pctype": int.from_bytes(mem[h + 24:h + 28], "little")})
        if not nxt or not 0 <= nxt - a0 <= v["size"] - ARENA_HEADER:
            break
        o = nxt - a0
    return {"var": v, "sec": sec, "off": base, "base": a0, "blocks": blocks, "lo": [b["data"] for b in blocks]}


def align16(v: int) -> int:
    return (v + 15) & ~15


def heap_blocks(schema: Schema, chunks: dict) -> list[dict]:
    """The heap's blocks from heapFirstBlockData, in order: data address, size, owner (0 free), caller (the
    allocating call's return address: HeapBlock m4_caller keeps it >> 2 in 21 bits), type, pinned, and the
    type the allocation named (HeapBlock mC_pcType: PC_HEAP_OF's hash, 0 for none)."""
    heap_lo = schema.regions["heapArena"][0]
    heap = chunks["mem.heap"]
    v = schema.by_name["heapFirstBlockData"]
    sec = next(k for k, (lo, hi) in schema.sections.items() if lo <= v["addr"] < hi)
    data = int.from_bytes(chunks["mem." + sec][v["addr"] - schema.sections[sec][0]:][:8], "little")
    blocks = []
    for _ in range(1 << 18):
        o = data - HEAP_HEADER - heap_lo
        if not (0 <= o <= len(heap) - HEAP_HEADER):
            break
        nxt = int.from_bytes(heap[o:o + 8], "little")
        bits = int.from_bytes(heap[o + 8:o + 12], "little")
        user = (bits >> 21) & 0xF
        if user == 1:
            blocks.append({"data": data, "size": 0, "user": 1, "end": True, "next": nxt, "bits": bits})
            break
        caller = (bits & 0x1FFFFF) << 2
        blocks.append({"data": data, "size": nxt - HEAP_HEADER - data, "user": user, "caller": caller,
                       "pinned": (bits >> 25) & 1, "type": (bits >> 26) & 0x3F, "bits": bits,
                       "pctype": int.from_bytes(heap[o + 12:o + 16], "little")})
        data = nxt
    return blocks


class Sites:
    """Allocation call sites: a block's caller to its source line and the type allocated there."""

    def __init__(self, schema: Schema):
        self.s = schema
        self.cache = {}
        self.names = dict(schema.type_names)

    def full(self, caller):
        # 21 bits of address >> 2 keep 8 MiB: this executable's text (from 0x400000) is unique in it
        return caller + (1 << 23) if caller < 0x400000 else caller

    @staticmethod
    def call_of(a):
        # allocateHeap keeps (return address - 8) & ~3 (the PS1's jal): the return address is 8 to 11 bytes
        # on, and the call instruction (5 bytes) holds a + 7
        return a + 7

    def lookup(self, callers):
        todo = sorted({self.full(c) for c in callers} - set(self.cache))
        if todo:
            out = subprocess.run(["addr2line", "-f", "-i", "-e", str(self.s.exe)] + [hex(self.call_of(a)) for a in todo],
                                 capture_output=True, text=True).stdout.splitlines()
            # -i prints inlined frames too: the first pair is the innermost (the call's own line)
            it = iter(out); rows = []
            for fn in it:
                loc = next(it, "?:0")
                rows.append((fn, loc))
            k = 0
            for a in todo:
                fn, loc = rows[k] if k < len(rows) else ("?", "?:0")
                k += 1
                f, _, line = loc.rpartition(":")
                line = int(re.match(r"\d+", line).group(0)) if re.match(r"\d+", line) else 0
                self.cache[a] = (fn, f, line)
            # inlined frames shift the pairing: redo one by one where needed
            if len(rows) != len(todo):
                for a in todo:
                    o = subprocess.run(["addr2line", "-f", "-e", str(self.s.exe), hex(self.call_of(a))], capture_output=True,
                                       text=True).stdout.splitlines()
                    f, _, line = (o[1] if len(o) > 1 else "?:0").rpartition(":")
                    self.cache[a] = (o[0] if o else "?", f, int(re.match(r"\d+", line).group(0)) if re.match(r"\d+", line) else 0)
        return {c: self.cache[self.full(c)] for c in callers}

    # P2 prototype: what a site allocates where its line does not say (sizeof) and no static's field takes it.
    # The C runtime replaces this table with the type each site's request names (PC_HEAP_*, carl: types in the
    # source); until then: (file, line) -> "prims" (a primitive buffer: OT tags and GPU words), "ptrs" (an
    # array of pointers), or a type name.
    OVERRIDES = {
        ("model.c", 507): "prims", ("shape_transfer.c", 112): "prims",
        ("convert.c", 12): "ptrs",
    }

    def type_of(self, site):
        """The type allocated at a site: the tool's table, sizeof(T) in the request, or the pointer type of
        the static field the block is assigned to (fieldSceneState.m298_walkMesh = allocateHeap(...))."""
        fn, f, line = site
        o = self.OVERRIDES.get((Path(f).name, line))
        if o:
            return o
        try:
            text = Path(f).read_text(errors="replace").splitlines()
        except OSError:
            return None
        stmt = text[line - 1] if 0 < line <= len(text) else ""
        k = line
        while ";" not in stmt and k < min(len(text), line + 3):  # a call over several lines
            stmt += " " + text[k]; k += 1
        m = re.search(r"sizeof\s*\(\s*(?:struct\s+)?(\w+)\s*\)", stmt)
        if m and m.group(1) in self.names:
            return m.group(1)
        m = re.search(r"\b(?:struct\s+)?(\w+)\s*\*\s*\w+\s*=\s*(?:\([^)]*\)\s*)?allocate\w*\(", stmt)  # T* x = allocate...
        if m and m.group(1) in self.names:
            return m.group(1)
        m = re.search(r"([A-Za-z_][\w.]*(?:\[\w+\])?(?:\.[A-Za-z_]\w*)*)\s*=\s*(?:\([^)]*\)\s*)?allocate\w*\(", stmt)
        if m:
            path = re.sub(r"\[\w+\]", "", m.group(1)).split(".")
            v = self.s.by_name.get(path[0])
            if v is not None:
                tid = v["tid"]
                for name in path[1:]:
                    t = self.s.types.get(tid, {})
                    while t.get("k") == "arr":
                        tid = t["of"]; t = self.s.types.get(tid, {})
                    f2 = next((x for x in t.get("fields", []) if x["name"] == name), None)
                    if f2 is None:
                        return None
                    tid = f2["type"]
                t = self.s.types.get(tid, {})
                while t.get("k") == "arr":
                    tid = t["of"]; t = self.s.types.get(tid, {})
                if t.get("k") == "ptr" and t.get("to") in self.s.types:
                    tt = self.s.types[t["to"]]
                    if tt.get("k") in ("struct", "union", "ptr", "arr"):
                        return {"tid": t["to"]}
        return None


def heap_report(state: Path, schema: Schema, other=None, top: int = 40) -> None:
    """Each allocation site's live blocks and the type the prototype gives them; with the same moment from a
    build linked 0x1000000 higher (other: (state, schema)), the words that are pointers (they moved by it)."""
    h, body = sc.read_state(state)
    chunks = {t: body[o:o + n] for t, o, n in sc.chunks(body)}
    blocks = [b for b in heap_blocks(schema, chunks) if b.get("user") and not b.get("end")]
    ptrs = {}
    if other is not None:
        _, ob = sc.read_state(other[0])
        oc = {t: ob[o:o + n] for t, o, n in sc.chunks(ob)}
        oblocks = [b for b in heap_blocks(other[1], oc) if b.get("user") and not b.get("end")]
        ha, hb = chunks["mem.heap"], oc["mem.heap"]
        a0, b0 = schema.regions["heapArena"][0], other[1].regions["heapArena"][0]
        for x, y in zip(blocks, oblocks):
            n = 0
            for w in range(0, min(x["size"], y["size"]) - 7, 8):
                oa = x["data"] - a0 + w; ob_ = y["data"] - b0 + w
                va = int.from_bytes(ha[oa:oa + 8], "little"); vb = int.from_bytes(hb[ob_:ob_ + 8], "little")
                if va != vb and (vb - va == 0x1000000 or any(((vb >> k) & 0xFFFFFFFF) - ((va >> k) & 0xFFFFFFFF) == 0x1000000 for k in (0, 32))):
                    n += 1
            ptrs[x["data"]] = n
    sites = Sites(schema)
    where = sites.lookup([b["caller"] for b in blocks])
    by = {}
    for b in blocks:
        site = where[b["caller"]]
        named = schema.heap_types.get(b["pctype"]) if b["pctype"] else None
        if named is not None:  # the allocation named it (PC_HEAP_OF)
            t = named["name"] + " (source)"
        else:
            t = sites.type_of(site)
            if isinstance(t, dict):
                t = schema.types[t["tid"]].get("name") or f"<{t['tid']}>"
        key = (site[0], Path(site[1]).name, site[2], t)
        st = by.setdefault(key, [0, 0, 0])
        st[0] += 1; st[1] += b["size"]; st[2] += ptrs.get(b["data"], 0)
    typed = sum(st[1] for k, st in by.items() if k[3])
    total = sum(st[1] for st in by.values())
    need = [k for k, st in by.items() if st[2] and not k[3]]
    print(f"statecanon heap: {len(blocks)} live blocks, {total} bytes; {len(by)} sites; typed: "
          f"{sum(1 for k in by if k[3])} sites, {typed} bytes" +
          (f"; untyped sites holding pointers: {len(need)}" if other is not None else ""))
    for (fn, f, line, t), (n, size, p) in sorted(by.items(), key=lambda kv: -kv[1][1])[:top]:
        print(f"  {size:8d} {n:4d} {p if other is not None else '':>6}  {fn} ({f}:{line})  {t or '-'}")


PRIM_LINK = (1 << 56) - 1


def type_spec(schema: Schema, sites: "Sites", site) -> object:
    """A block's type by its allocation site, where its header names none (the prototype's inference: the
    tool's table, sizeof(T), T* x = ...): {"tid"} for a struct (an array of them), or "prims", "ptrs",
    "otags", or None (raw bytes)."""
    t = sites.type_of(site)
    if isinstance(t, dict):
        tt = schema.types.get(t["tid"], {})
        if tt.get("k") == "ptr":
            return "ptrs"
        if tt.get("k") == "otag":
            return "otags"
        return {"tid": t["tid"]} if tt.get("k") in ("struct", "union") else None
    if t in ("prims", "ptrs", "otags"):
        return t
    if isinstance(t, str) and t in schema.type_names:
        return {"tid": schema.type_names[t]}
    return None


def encode_prims(enc: "Encoder", mem, off: int, size: int):
    """A primitive buffer: each primitive's tag (the words' count and the link) and its GPU words. Stops at what
    does not parse as primitives; the rest is kept as bytes."""
    prims, o = [], 0
    while o + 8 <= size:
        tag = int.from_bytes(mem[off + o:off + o + 8], "little")
        if tag == 0:  # no primitive here: an arena's unused, zeroed stretch (a run of zero words)
            j = o
            while j + 8 <= size and not any(mem[off + j:off + j + 8]):
                j += 8
            prims.append({"z": j - o}); o = j
            continue
        n = tag >> 56; link = tag & PRIM_LINK
        step = (8 + 4 * n + 7) & ~7
        if n > 16 or o + step > size:
            break
        prims.append({"n": n, "l": "end" if link == PRIM_LINK else enc.pointer(link, quiet=True),
                      "w": bytes(mem[off + o + 8:off + o + step]).hex()})
        o += step
    return {"prims": prims, "x": bytes(mem[off + o:off + size]).hex()}


def put_prims(dec: "Decoder", val, buf, off: int, size: int):
    o = 0
    for p in val["prims"]:
        if "z" in p:
            buf[off + o:off + o + p["z"]] = bytes(p["z"]); o += p["z"]
            continue
        link = PRIM_LINK if p["l"] == "end" else dec.pointer(p["l"]) & PRIM_LINK
        buf[off + o:off + o + 8] = ((p["n"] << 56) | link).to_bytes(8, "little")
        w = bytes.fromhex(p["w"]); buf[off + o + 8:off + o + 8 + len(w)] = w
        o += 8 + len(w)
    x = bytes.fromhex(val["x"]); buf[off + o:off + o + len(x)] = x[:max(0, size - o)]


def state_ranges(schema: Schema, chunks: dict) -> tuple[list, int]:
    """The typed ranges the game recorded (PC_STATE_AT: port/src/game/main/statetypes.c pcStateRanges): (address,
    type hash, count), and whether one was not recorded (overflow)."""
    v = schema.by_name.get("pcStateRanges")
    if v is None:
        return [], 0
    sec = next((k for k, (lo, hi) in schema.sections.items() if lo <= v["addr"] < hi), None)
    if sec is None or "mem." + sec not in chunks:
        return [], 0
    data = chunks["mem." + sec]; base = v["addr"] - schema.sections[sec][0]
    t = schema.types[v["tid"]]; f = {x["name"]: x for x in t["fields"]}
    count = int.from_bytes(data[base + f["count"]["off"]:][:4], "little", signed=True)
    over = int.from_bytes(data[base + f["overflow"]["off"]:][:4], "little")
    arr = schema.types[f["ranges"]["type"]]; et = schema.types[arr["of"]]; es = et["size"]
    ef = {x["name"]: x for x in et["fields"]}
    out = []
    for i in range(max(0, min(count, arr["n"]))):
        o = base + f["ranges"]["off"] + i * es
        out.append((int.from_bytes(data[o + ef["at"]["off"]:][:8], "little"),
                    int.from_bytes(data[o + ef["type"]["off"]:][:4], "little"),
                    int.from_bytes(data[o + ef["count"]["off"]:][:4], "little", signed=True)))
    return out, over


def spec_of(schema: Schema, h: int):
    """A type hash's encoder spec (the schema's heap_types), or False when the schema does not know it."""
    e = schema.heap_types.get(h)
    return False if e is None else e["spec"]


def field_named(schema: Schema, tid, name):
    return next((f for f in schema.types.get(tid, {}).get("fields", []) if f["name"] == name), None)


def rule_for(schema: Schema, by_tid: dict, by_name: dict, tid):
    r = by_tid.get(tid)
    if r is None and by_name:
        n = schema.types.get(tid, {}).get("name")
        r = by_name.get(n) or by_name.get(schema.tid_name.get(tid))
    return r


def counted_rule(schema: Schema, tid):
    """A counted array's rule for a struct (PC_STATE_COUNTED; PC_STATE_TAIL: no count field, the array fills the
    block), with its fields: (rule, array field, count field or None)."""
    r = rule_for(schema, schema.counted, schema.counted_by_name, tid)
    if r is None:
        return None
    a = field_named(schema, tid, r["array"])
    c = field_named(schema, tid, r["count"]) if r.get("count") is not None else None
    return (r, a, c) if a and (c or r.get("count") is None) else None


def element_size(schema: Schema, tid, mem, off, room=None):
    """A struct element's size at off: its type's, or with a counted array the array's count of elements (a
    tail array: as many as fit in room)."""
    cr = counted_rule(schema, tid)
    if cr is None:
        return schema.size(tid), None
    r, a, c = cr
    es = schema.size(schema.types[a["type"]]["of"])
    if c is None:
        n = (room - a["off"]) // es if room is not None and es else 0
    else:
        n = int.from_bytes(mem[off + c["off"]:off + c["off"] + schema.size(c["type"])], "little", signed=True) + r["extra"]
    return a["off"] + max(0, n) * es, max(0, n)


def layout_part(schema: Schema, mem, off, room, spec, count=None):
    """One part of a block in the saving build: spec's elements from off, as many as fit in room (or count of
    them). A dict with the part's size, its elements' offsets and sizes, or None when nothing fits."""
    if spec in ("ptrs", "otags"):
        n = room // 8 if count is None else min(count, room // 8)
        return {"spec": spec, "size": 8 * n, "n": n} if n else None
    if spec == "prims":
        return {"spec": spec, "size": room} if room else None
    if not isinstance(spec, dict) or spec["tid"] not in schema.types:
        return None
    tid = spec["tid"]; es0 = schema.size(tid)
    if spec.get("one") and count is None:  # PC_HEAP_ONE: one T, then the caller's bytes
        count = 1
    eo, esz, cnt = [], [], []; o = 0
    while o < room and (count is None or len(eo) < count):
        es, n = element_size(schema, tid, mem, off + o, room - o)
        if es <= 0:
            break
        if o + es > room:
            if not eo and n is None and es0 > room:  # a T's first fields only (allocateHeap(offsetof(T, m)))
                return {"spec": spec, "size": room, "eo": [0], "esz": [room], "cnt": [None], "partial": True}
            break
        eo.append(o); esz.append(es); cnt.append(n); o += es
        if n is not None and count is None:  # a counted struct: one, then what follows is another part
            break
    if not eo:
        return None
    return {"spec": spec, "size": o, "eo": eo, "esz": esz, "cnt": cnt}


def block_layout(schema: Schema, mem, off: int, size: int, spec, ranges) -> list:
    """A block's parts in the saving build, in order: its own type from the start, the recorded ranges in it,
    bytes between and after; a block of primitives (a bump arena's: the shape transfers') has primitives in
    every stretch the ranges leave. Each part: {"off", "size", "spec", ...} (spec None: bytes)."""
    parts = []; cur = 0
    starts = sorted(ranges)

    def gap(a, b):
        if spec == "prims":
            p = layout_part(schema, mem, off + a, b - a, "prims")
            if p is not None:
                p["off"] = a; parts.append(p); return
        parts.append({"off": a, "size": b - a, "spec": None})

    first = starts[0][0] if starts else size
    if spec is not None:
        p = layout_part(schema, mem, off, first, spec)
        if p is not None:
            p["off"] = 0; parts.append(p); cur = p["size"]
    for ro, rspec, n in starts:
        if ro < cur or rspec is False or rspec is None:
            continue  # overlapping, unknown or plain bytes: the bytes part keeps them
        nxt = next((r[0] for r in starts if r[0] > ro), size)
        p = layout_part(schema, mem, off + ro, nxt - ro, rspec, n)
        if p is None:
            continue
        if ro > cur:
            gap(cur, ro)
        p["off"] = ro; parts.append(p); cur = ro + p["size"]
    if cur < size:
        gap(cur, size)
    return parts


def encode_element(enc: "Encoder", tid, mem, off, n, partial_size=None):
    """One struct element; with a counted array (n elements), the array's elements all listed."""
    if partial_size is not None:  # the first fields of a T: the rest of T read as zero
        es = enc.s.size(tid); tmp = bytearray(es); tmp[:partial_size] = mem[off:off + partial_size]
        return enc.value(tid, memoryview(tmp), 0)
    v = enc.value(tid, mem, off)
    if n is not None:
        r, a, _ = counted_rule(enc.s, tid)
        key = array_key(enc.s, tid, a)
        of = enc.s.types[a["type"]]["of"]; es = enc.s.size(of); base = off + a["off"]
        et = enc.s.types[of]
        if et["k"] == "int":
            v[key] = {"x": bytes(mem[base:base + n * es]).hex(), "w": es}
        else:
            v[key] = [enc.value(of, mem, base + j * es) for j in range(n)]
    return v


def array_key(schema: Schema, tid, field):
    seen = set()
    for f in schema.types[tid]["fields"]:
        k = Encoder.field_key(None, f, seen)
        if f is field or f["name"] == field["name"]:
            return k
    return None


def encode_part(enc: "Encoder", mem, off, p) -> dict:
    spec = p["spec"]
    if spec is None:
        return {"x": bytes(mem[off:off + p["size"]]).hex()}
    if spec == "ptrs":
        return {"t": spec, "v": [enc.pointer(int.from_bytes(mem[off + k:off + k + 8], "little"), quiet=True)
                                 for k in range(0, p["size"], 8)]}
    if spec == "otags":
        return {"t": spec, "v": [enc.value("OTag", mem, off + k) for k in range(0, p["size"], 8)]}
    if spec == "prims":
        return {"t": spec, "size": p["size"], "v": encode_prims(enc, mem, off, p["size"])}
    tid = spec["tid"]
    name = stable_name(enc.s, tid)
    if name is None:
        enc.warn["heap part of an unnamed type (same build only)"] += 1
    out = {"t": {"tid": tid, "name": name}, "v": [encode_element(enc, tid, mem, off + o, n, p["size"] if p.get("partial") else None)
                                    for o, n in zip(p["eo"], p["cnt"])]}
    if p.get("partial"):
        # the first fields only: up to the first that does not fit (allocateHeap(offsetof(T, m)))
        # (offsetof(T, m): the part ends where field m starts; in another build, where m starts there)
        nxt = next((f for f in enc.s.types[tid]["fields"] if f.get("bits") is None and
                    f["off"] + enc.s.size(f["type"]) > p["size"]), None)
        out["upto"] = array_key(enc.s, tid, nxt) if nxt is not None and nxt["off"] == p["size"] else None
        out["size"] = p["size"]
    if any(n is not None for n in p["cnt"]):
        out["counted"] = True
    return out


def encode_heap(schema: Schema, chunks: dict, enc: "Encoder", infer: bool = False) -> dict:
    """The game heap as its blocks in order (the canonical heap, phase P2): free blocks by size, live ones
    with owner, flags, the allocation site, and their contents as typed parts: the type the allocation named
    (PC_HEAP_OF, in the block's header) and the ranges the game recorded in it (PC_STATE_AT); a block whose
    allocation named nothing is bytes (infer: the prototype's guess from its call site, for diagnosis)."""
    heap_lo = schema.regions["heapArena"][0]
    mem = memoryview(chunks["mem.heap"])
    blocks = heap_blocks(schema, chunks)
    sites = Sites(schema)
    live = [b for b in blocks if b.get("user") and not b.get("end")]
    where = sites.lookup([b["caller"] for b in live])
    ranges, over = state_ranges(schema, chunks)
    if over:
        enc.warn["typed ranges not recorded (pcStateRanges full): such a state is not canonical"] += 1
    starts = [b["data"] for b in blocks]
    per_block = defaultdict(list)
    arena = arena_blocks(schema, chunks); per_arena = defaultdict(list)
    for at, h, n in ranges:
        i = bisect.bisect_right(starts, at) - 1
        if i >= 0 and blocks[i].get("user") and not blocks[i].get("end") and at < blocks[i]["data"] + blocks[i]["size"]:
            per_block[i].append((at - blocks[i]["data"], spec_of(schema, h), n))
            continue
        if arena is not None:
            j = bisect.bisect_right(arena["lo"], at) - 1
            if j >= 0 and at < arena["blocks"][j]["data"] + arena["blocks"][j]["size"]:
                per_arena[j].append((at - arena["blocks"][j]["data"], spec_of(schema, h), n))
                continue
        enc.warn["typed range outside the live heap blocks (left to the bytes there)"] += 1
    for i, b in enumerate(blocks):
        if b.get("user") and not b.get("end"):
            b["site"] = where[b["caller"]]
            s = spec_of(schema, b["pctype"]) if b["pctype"] else False
            if s is False:
                if b["pctype"]:
                    enc.warn["heap block of a type the schema does not name"] += 1
                s = type_spec(schema, sites, b["site"]) if infer else None
                b["by"] = "site" if infer else None
            else:
                b["by"] = "source"
            b["spec"] = s
            b["parts"] = block_layout(schema, mem, b["data"] - heap_lo, b["size"], s, per_block.get(i, []))
            b["part_lo"] = [p["off"] for p in b["parts"]]
    enc.heap = blocks
    enc.heap_lo = starts
    if arena is not None:  # the sound arena: typed by what its allocations named, else bytes
        amem = memoryview(chunks["mem." + arena["sec"]])
        for j, b in enumerate(arena["blocks"]):
            sp = spec_of(schema, b["pctype"]) if b["pctype"] else None
            if sp is False:
                enc.warn["sound arena block of a type the schema does not name (kept as bytes)"] += 1
                sp = None
            b["spec"] = sp
            b["parts"] = block_layout(schema, amem, arena["off"] + b["hdr"] + ARENA_HEADER, b["size"], sp, per_arena.get(j, []))
            b["part_lo"] = [p["off"] for p in b["parts"]]
        enc.arena = arena
    out = []
    for i, b in enumerate(blocks):
        if b.get("end"):
            out.append({"end": True, "next": enc.pointer(b["next"], quiet=True), "bits": b["bits"] & ~0x1FFFFF}); continue
        if not b["user"]:
            out.append({"free": b["size"], "pin": b["pinned"], "ty": b["type"]}); continue
        rec = {"u": b["user"], "pin": b["pinned"], "ty": b["type"], "size": b["size"], "by": b["by"],
               "site": [b["site"][0], Path(b["site"][1]).name, b["site"][2]]}
        if b["pctype"]:
            rec["pct"] = b["pctype"]
        off = b["data"] - heap_lo
        enc.current = f"heap block {i} ({b['site'][0]})"
        rec["parts"] = [encode_part(enc, mem, off + p["off"], p) for p in b["parts"]]
        out.append(rec)
    return {"first": blocks[0]["data"] - heap_lo if blocks else 0, "blocks": out}


def stable_name(schema: Schema, tid):
    """A struct type's name, which another build resolves (type_names), for one of the DWARF entries that
    describe it (each compile unit has its own)."""
    n = schema.tid_name.get(tid)
    if n:
        return n
    n = schema.types.get(tid, {}).get("name")
    if n and "<anon>" not in n and n in schema.type_names and schema.size(schema.type_names[n]) == schema.size(tid):
        return n
    return None


def part_tid(schema: Schema, t: dict, dec: "Decoder"):
    """A struct part's type in this build: by its name (a renamed type's: the rules); an unnamed one by the saving
    build's DWARF offset, which only the same build has."""
    if t.get("name"):
        name = dec.rules.type_name(t["name"]) if dec.rules is not None else t["name"]
        tid = schema.type_names.get(name) if name is not None else None
        if tid is None:
            dec.warn["heap part type not in this build (left zero)"] += 1
        elif name != t["name"]:
            dec.applied["renamed by a state rule"] += 1
        return tid
    return t["tid"] if t.get("tid") in schema.types else None


def part_size(schema: Schema, part: dict, dec: "Decoder", tid=None) -> tuple[int, list]:
    """A part's size in this build, and its elements' offsets."""
    if "x" in part:
        return len(part["x"]) // 2, []
    t = part["t"]
    if t in ("ptrs", "otags"):
        return 8 * len(part["v"]), []
    if t == "prims":
        return part["size"], []
    if tid is None:
        return part.get("size", 0), []
    wt = t.get("tid") if dec.w is not None and isinstance(t, dict) else None  # (the writer's type of the part)
    if "upto" in part:
        if part["upto"] is None:
            return part["size"], [0]
        o = Decoder.offset_of(dec, tid, [part["upto"]], wt)
        return (o if o is not None else part.get("size", 0)), [0]
    eo, o = [], 0
    cs = counted_source(dec, tid, wt) if part.get("counted") else None
    for v in part["v"]:
        eo.append(o)
        if cs:
            (r, a, _), key = cs
            av = v.get(key) if key is not None else None
            if isinstance(av, list):
                n = len(av)
            elif isinstance(av, dict) and "x" in av and av.get("w"):
                n = len(bytes.fromhex(av["x"])) // av["w"]
            else:
                n = 0  # (put_element warns)
            o += a["off"] + n * schema.size(schema.types[a["type"]]["of"])
        else:
            o += schema.size(tid)
    return o, eo


def decode_heap_layout(h: dict, schema: Schema, dec: "Decoder") -> None:
    """Where each block goes in this build: in order from the same start, each live one sized by this build's
    types (a struct array's elements may have grown; whole grains, as allocateHeap rounds), free ones as they
    were."""
    heap_lo = schema.regions["heapArena"][0]
    data = heap_lo + h["first"]
    layout = []
    for rec in h["blocks"]:
        if rec.get("end"):
            layout.append({"data": data, "size": 0, "end": True, "rec": rec}); break
        if "free" in rec:
            size = rec["free"]; parts = []
        else:
            parts, o = layout_parts(schema, rec["parts"], dec)
            size = (o + HEAP_HEADER - 1) & ~(HEAP_HEADER - 1) if o != rec["size"] else o
        layout.append({"data": data, "size": size, "parts": parts, "rec": rec})
        data += size + HEAP_HEADER
    dec.heap = layout


def counted_source(dec: "Decoder", tid, wt):
    """The reader's counted array of struct tid (its rule, field) and the writer's key of it (a renamed member's by
    the rules), or None when the reader does not count tid."""
    cr = counted_rule(dec.s, tid)
    if cr is None:
        return None
    key = array_key(dec.s, tid, cr[1])
    return cr, (dec.rules.field_source(tid, wt, key) if dec.rules is not None else key)


def put_element(dec: "Decoder", tid, v, buf, off, counted, wt=None):
    """One struct element of a part (wt: the writer's type of it). A counted array the reader does not count, or
    the state does not hold as an array, leaves the element's own members only (warned)."""
    if counted:
        cs = counted_source(dec, tid, wt)
        if cs is None:
            dec.warn["counted array not counted in this build (its elements left out)"] += 1
            dec.put(tid, v, buf, off, wt)
            return
        (r, a, _), key = cs
        av = v.get(key) if key is not None else None
        if not (isinstance(av, list) or isinstance(av, dict) and "x" in av):
            dec.warn["counted array not in the state (left zero)"] += 1
            dec.put(tid, v, buf, off, wt)
            return
        of = dec.s.types[a["type"]]["of"]; es = dec.s.size(of); base = off + a["off"]
        first = dict(v)
        if isinstance(av, dict):
            first[key] = {"x": av["x"][:2 * es], "w": es}
        else:
            first[key] = av[:1]
        dec.put(tid, first, buf, off, wt)
        if isinstance(av, dict):
            b = bytes.fromhex(av["x"]); buf[base:base + len(b)] = b
        else:
            wcr = counted_rule(dec.w, wt) if dec.w is not None and wt is not None else None
            wof = dec.w.types.get(wcr[1]["type"], {}).get("of") if wcr is not None else None
            for j, e in enumerate(av):
                dec.put(of, e, buf, base + j * es, wof)
        return
    dec.put(tid, v, buf, off, wt)


def decode_heap(h: dict, schema: Schema, dec: "Decoder", heap_size: int) -> bytearray:
    heap_lo = schema.regions["heapArena"][0]
    buf = bytearray(heap_size)
    for k, b in enumerate(dec.heap):
        hdr = b["data"] - HEAP_HEADER - heap_lo
        if hdr < 0 or hdr + HEAP_HEADER > heap_size:
            dec.warn["heap block outside the arena"] += 1; continue
        if b.get("end"):
            rec = b.get("rec", {})
            buf[hdr:hdr + 8] = (dec.pointer(rec.get("next")) & MASK64).to_bytes(8, "little")
            buf[hdr + 8:hdr + 12] = rec.get("bits", 1 << 21).to_bytes(4, "little")
            continue
        nxt = dec.heap[k + 1]["data"] if k + 1 < len(dec.heap) else b["data"] + b["size"] + HEAP_HEADER
        buf[hdr:hdr + 8] = nxt.to_bytes(8, "little")
        rec = b["rec"]
        if "free" in rec:
            buf[hdr + 8:hdr + 12] = ((rec.get("pin", 0) << 25) | (rec.get("ty", 0) << 26)).to_bytes(4, "little")
            continue
        # the caller field (a code address of the saving build) is left zero: the heap's own debugging uses it
        bits = (rec["u"] << 21) | (rec["pin"] << 25) | (rec["ty"] << 26)
        buf[hdr + 8:hdr + 12] = bits.to_bytes(4, "little")
        buf[hdr + 12:hdr + 16] = dec.remap_hash(rec.get("pct", 0)).to_bytes(4, "little")
        put_parts(dec, rec["parts"], b["parts"], buf, b["data"] - heap_lo)
    return buf


def put_parts(dec: "Decoder", parts: list, layout: list, buf, base: int) -> None:
    """A block's typed parts (encode_part) at base, where this build lays them out (part_size)."""
    schema = dec.s
    for part, lay in zip(parts, layout):
        off = base + lay["off"]
        if "x" in part:
            x = bytes.fromhex(part["x"]); buf[off:off + len(x)] = x; continue
        t = part["t"]
        if t == "ptrs":
            for i, v in enumerate(part["v"]):
                buf[off + i * 8:off + i * 8 + 8] = (dec.pointer(v) & MASK64).to_bytes(8, "little")
        elif t == "otags":
            for i, v in enumerate(part["v"]):
                dec.put("OTag", v, buf, off + i * 8, "OTag" if dec.w is not None else None)
        elif t == "prims":
            put_prims(dec, part["v"], buf, off, part["size"])
        elif lay.get("tid") is not None:
            tid = lay["tid"]
            wt = t.get("tid") if dec.w is not None else None  # (the writer's type of the part)
            if "upto" in part:
                tmp = bytearray(schema.size(tid)); dec.put(tid, part["v"][0], tmp, 0, wt)
                buf[off:off + lay["size"]] = tmp[:lay["size"]]
                continue
            for e, v in zip(lay["eo"], part["v"]):
                put_element(dec, tid, v, buf, off + e, part.get("counted"), wt)


def layout_parts(schema: Schema, parts: list, dec: "Decoder") -> tuple[list, int]:
    out = []; o = 0
    for p in parts:
        tid = part_tid(schema, p["t"], dec) if isinstance(p.get("t"), dict) else None
        n, eo = part_size(schema, p, dec, tid)
        out.append({"off": o, "size": n, "eo": eo, "tid": tid}); o += n
    return out, o


def encode_arena(enc: "Encoder", data, off: int) -> dict:
    """The sound arena (arena_blocks) in chain order: each block with the gap before its header (after the
    previous block's end, rounded to 16 as the allocator places them), flags, m2, m4, its type, and its
    contents as typed parts."""
    ar = enc.arena; out = []; prev_end = 0
    for j, b in enumerate(ar["blocks"]):
        gap = 0 if j == 0 else b["hdr"] - align16(prev_end)
        if gap < 0:
            enc.warn["sound arena blocks out of order (kept as they were)"] += 1
        prev_end = b["hdr"] + ARENA_HEADER + b["size"]
        rec = {"gap": gap, "f": b["flags"], "m2": b["m2"], "m4": b["m4"], "size": b["size"]}
        if b["pctype"]:
            rec["pct"] = b["pctype"]
        enc.current = f"sound arena block {j}"
        rec["parts"] = [encode_part(enc, data, off + b["hdr"] + ARENA_HEADER + p["off"], p) for p in b["parts"]]
        out.append(rec)
    return {"blocks": out}


def decode_arena_layout(arena: dict, schema: Schema, var: dict, dec: "Decoder") -> None:
    """Where the sound arena's blocks go in this build: each after the previous one's end, rounded to 16, and
    the gap it had; sized by this build's types."""
    blocks = []; prev_end = 0
    for j, r in enumerate(arena["blocks"]):
        hdr = 0 if j == 0 else align16(prev_end) + r["gap"]
        parts, size = layout_parts(schema, r["parts"], dec)
        if not r["parts"]:
            size = r["size"]
        blocks.append({"hdr": hdr, "data": var["addr"] + hdr + ARENA_HEADER, "size": size, "parts": parts, "rec": r})
        prev_end = hdr + ARENA_HEADER + size
    dec.arena = {"blocks": blocks, "base": var["addr"], "lo": [b["data"] for b in blocks]}


def put_arena(dec: "Decoder", arena: dict, buf, off: int, size: int) -> None:
    buf[off:off + size] = bytes(size)
    blocks = dec.arena["blocks"]
    for j, b in enumerate(blocks):
        h = off + b["hdr"]; r = b["rec"]
        if b["hdr"] + ARENA_HEADER + b["size"] > size:
            dec.warn["sound arena block past the arena's end (left out)"] += 1
            continue
        nxt = dec.arena["base"] + blocks[j + 1]["hdr"] if j + 1 < len(blocks) else 0
        buf[h:h + 8] = (r["f"].to_bytes(2, "little") + r["m2"].to_bytes(2, "little") +
                        (r["m4"] & 0xFFFFFFFF).to_bytes(4, "little"))
        buf[h + 8:h + 16] = (b["data"] + b["size"]).to_bytes(8, "little")
        buf[h + 16:h + 24] = nxt.to_bytes(8, "little")
        buf[h + 24:h + 28] = dec.remap_hash(r.get("pct", 0)).to_bytes(4, "little")
        put_parts(dec, r["parts"], b["parts"], buf, h + ARENA_HEADER)


def untyped(a_state: Path, a: Schema, b_state: Path, b: Schema, shift: int = 0x1000000) -> dict:
    """Pointers the source types nowhere: between two layouts linked `shift` apart, saved at the same moment,
    the words of a heap or sound-arena block's untyped bytes (no PC_HEAP_OF, a tail past its T, a stretch
    between recorded ranges) that differ by exactly the shift. By allocation site (the arena: by block)."""
    _, enc = encode(a_state, a)
    _, bb = sc.read_state(b_state)
    cb = {t: bb[o:o + n] for t, o, n in sc.chunks(bb)}
    _, ab = sc.read_state(a_state)
    ca = {t: ab[o:o + n] for t, o, n in sc.chunks(ab)}
    out = Counter()

    def scan(blocks_a, mem_a, base_a, blocks_b, mem_b, base_b, label):
        for x, y in zip(blocks_a, blocks_b):
            if not x.get("user") or x.get("end") or x["size"] != y["size"]:
                continue
            for p in x.get("parts", []):
                if p["spec"] is not None:
                    continue
                lo = p["off"] - p["off"] % 8
                for w in range(lo, p["off"] + p["size"] - 7, 8):
                    va = int.from_bytes(mem_a[x["data"] - base_a + w:][:8], "little")
                    vb = int.from_bytes(mem_b[y["data"] - base_b + w:][:8], "little")
                    if va != vb and (vb - va == shift or any(((vb >> k) & 0xFFFFFFFF) - ((va >> k) & 0xFFFFFFFF) == shift
                                                             for k in (0, 32))):
                        out[label(x)] += 1

    hb = heap_blocks(b, cb)
    scan(enc.heap, ca["mem.heap"], a.regions["heapArena"][0], hb, cb["mem.heap"], b.regions["heapArena"][0],
         lambda x: f"{x['site'][0]} ({Path(x['site'][1]).name}:{x['site'][2]})")
    if enc.arena is not None:
        arb = arena_blocks(b, cb)
        ma = ca["mem." + enc.arena["sec"]]; mb = cb["mem." + arb["sec"]]
        scan(enc.arena["blocks"], ma, enc.arena["base"] - enc.arena["off"], arb["blocks"], mb, arb["base"] - arb["off"],
             lambda x: f"sound arena block of type {x['pctype']:08x}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("encode"); e.add_argument("state", type=Path); e.add_argument("build", type=Path); e.add_argument("out", type=Path)
    d = sub.add_parser("decode"); d.add_argument("canon", type=Path); d.add_argument("build", type=Path)
    d.add_argument("template", type=Path); d.add_argument("out", type=Path)
    d.add_argument("--stackless", action="store_true", help="without the game's frames (a safe point's state)")
    cv = sub.add_parser("convert", help="a state of one build as another build's, stackless (a safe point's)")
    cv.add_argument("state", type=Path); cv.add_argument("from_build", type=Path); cv.add_argument("to_build", type=Path)
    cv.add_argument("template", type=Path, help="a state of the target build (its chunk order; parts not yet canonical)")
    cv.add_argument("out", type=Path)
    r = sub.add_parser("roundtrip"); r.add_argument("state", type=Path); r.add_argument("build", type=Path)
    r.add_argument("--out", type=Path)
    hp = sub.add_parser("heap"); hp.add_argument("state", type=Path); hp.add_argument("build", type=Path)
    hp.add_argument("--other", nargs=2, type=Path, metavar=("STATE", "BUILD"))
    x = sub.add_parser("crosscheck"); x.add_argument("a_state", type=Path); x.add_argument("a_build", type=Path)
    x.add_argument("b_state", type=Path); x.add_argument("b_build", type=Path)
    u = sub.add_parser("untyped", help="pointers in heap bytes no annotation types (two layouts 16 MiB apart)")
    u.add_argument("a_state", type=Path); u.add_argument("a_build", type=Path)
    u.add_argument("b_state", type=Path); u.add_argument("b_build", type=Path)
    a = ap.parse_args()
    if a.cmd == "convert":
        sw = Schema(a.from_build.resolve(), state_bias(a.state))
        doc, enc = encode(a.state, sw)
        doc = json.loads(json.dumps(doc))
        body, dec = decode(doc, Schema(a.to_build.resolve(), state_bias(a.template)), a.template, stackless=True,
                           writer=sw)
        write_state(a.template, body, a.out)
        print(f"statecanon convert: safe point {safe_point_of(doc)}; wrote {a.out} ({len(body)} bytes); "
              f"encode {dict(enc.warn)}; decode {dict(dec.warn)}; rules {dict(dec.applied)}")
        return 0
    if a.cmd == "untyped":
        found = untyped(a.a_state, Schema(a.a_build.resolve(), state_bias(a.a_state)), a.b_state,
                        Schema(a.b_build.resolve(), state_bias(a.b_state)))
        print(f"statecanon untyped: {sum(found.values())} pointer words in untyped heap bytes, {len(found)} places")
        for k, n in found.most_common():
            print(f"  {n:6d} {k}")
        return 1 if found else 0
    if a.cmd == "heap":
        heap_report(a.state, Schema(a.build.resolve(), state_bias(a.state)),
                    (a.other[0], Schema(a.other[1].resolve(), state_bias(a.other[0]))) if a.other else None)
        return 0
    if a.cmd == "crosscheck":
        lines, st = crosscheck(a.a_state, Schema(a.a_build.resolve(), state_bias(a.a_state)), a.b_state,
                               Schema(a.b_build.resolve(), state_bias(a.b_state)))
        print(f"statecanon crosscheck: {st['checked']} bytes checked, {st['differing']} differ; encode {st['encode']}; "
              f"decode {st['decode']}")
        print("\n".join(lines) if lines else "  every encoded byte as the other build has it")
        return 1 if lines else 0
    schema = Schema(a.build.resolve(), state_bias(a.state if a.cmd != "decode" else a.template))
    if a.cmd == "encode":
        doc, enc = encode(a.state, schema)
        with gzip.open(a.out, "wt") as fh:
            json.dump(doc, fh)
        print(f"statecanon: {len(doc['records'])} records, {len(doc['raw'])} raw chunks; coverage {doc['coverage']}; {dict(enc.warn)}")
        return 0
    if a.cmd == "decode":
        doc = json.load(gzip.open(a.canon, "rt"))
        body, dec = decode(doc, schema, a.template, stackless=a.stackless)
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
