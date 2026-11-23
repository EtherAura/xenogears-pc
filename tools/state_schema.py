#!/usr/bin/env python3
"""Canonical save states, phase P0 (xg-htn.60.1; design: docs/CANONICAL_STATES.md, local).

The state schema of a PC build, read from its DWARF, and a coverage report of what a canonical state
could not yet identify:

    tools/state_schema.py BUILD [--json OUT] [--report-only] [--top N]

BUILD is a configured port build whose executable has debug information (a RelWithDebInfo or Debug
build: cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo). Read with llvm-dwarfdump (the system's or the NDK's).

The schema joins each native object to its stable identity:
  - a variable in a game module's sections (xgdata_<module> / xgbss_<module>) to its retail address in
    config/<ver>/symbols/<module>.txt (overlays share address ranges: the module is part of it);
  - a struct field to its retail offset, from its offset-tagged name (m1C_actor: +0x1C);
  - a function to its retail address;
  - a platform static classed `state` in port/src/system/savestate_inventory.txt to its file and name.
The report counts what is not joined yet: module variables without a retail address, struct fields
without an offset tag in types a state saves, unions, pointer fields, and the game C's heap allocation
sites without a retail size (PC_HEAP_*).

Exit status: 0 when the schema was built (the gate only reports in P0), 1 when it could not be."""
import argparse, gzip, json, re, shutil, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEEP_TAGS = {
    "DW_TAG_compile_unit", "DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_class_type", "DW_TAG_typedef",
    "DW_TAG_base_type", "DW_TAG_pointer_type", "DW_TAG_array_type", "DW_TAG_subrange_type",
    "DW_TAG_enumeration_type", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_member",
    "DW_TAG_variable", "DW_TAG_subprogram", "DW_TAG_subroutine_type", "DW_TAG_restrict_type",
}
KEEP_ATTRS = {
    "DW_AT_name", "DW_AT_byte_size", "DW_AT_type", "DW_AT_data_member_location", "DW_AT_location",
    "DW_AT_low_pc", "DW_AT_upper_bound", "DW_AT_count", "DW_AT_bit_size", "DW_AT_data_bit_offset",
    "DW_AT_specification", "DW_AT_abstract_origin", "DW_AT_decl_file", "DW_AT_declaration", "DW_AT_external",
    "DW_AT_linkage_name", "DW_AT_encoding", "DW_AT_bit_offset",
}
TAG_RE = re.compile(r"^0x([0-9a-f]+):(\s+)(DW_TAG_\w+|NULL)")
ATTR_RE = re.compile(r"^\s+(DW_AT_\w+)\s+\((.*)\)\s*$")
FIELD_TAG = re.compile(r"^m([0-9A-Fa-f]+)(?:_|$)")  # m1C_actor, and m15D8 (no name yet)
ADDR_NAME = re.compile(r"^[DF]_([0-9A-Fa-f]{8})$")  # splat labels carry their retail address


def dwarfdump() -> str:
    for c in ("llvm-dwarfdump", *sorted(Path.home().glob("Android/sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-dwarfdump"))):
        p = shutil.which(str(c)) if isinstance(c, str) else (str(c) if c.exists() else None)
        if p:
            return p
    raise SystemExit("state_schema: no llvm-dwarfdump (install LLVM or the Android NDK)")


def parse_value(v: str):
    """An attribute's value: a string, a reference (offset), a number, an address location, a flag."""
    if v.startswith('"'):
        return v[1:v.rindex('"')]
    m = re.match(r"0x([0-9a-f]+)(?: \"(.*)\")?$", v)
    if m:
        return int(m.group(1), 16)
    m = re.match(r"DW_OP_addr 0x([0-9a-f]+)$", v)
    if m:
        return ("addr", int(m.group(1), 16))
    if v in ("true", "false"):
        return v == "true"
    if re.match(r"^-?\d+$", v):
        return int(v)
    return v


def read_dies(exe: Path) -> dict:
    """offset -> {tag, attrs, children, parent}; only the tags and attributes a schema needs, and only the
    compile units of this repository (the game C, the generated sources, the port)."""
    dies: dict[int, dict] = {}
    stack: list[tuple[int, int]] = []  # (depth, offset)
    cur = None
    skip_cu = False
    root = str(ROOT)
    proc = subprocess.Popen([dwarfdump(), "--debug-info", str(exe)], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    for line in proc.stdout:
        m = TAG_RE.match(line)
        if m:
            depth = (len(m.group(2)) - 1) // 2
            tag = m.group(3)
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if tag == "NULL":
                cur = None
                continue
            off = int(m.group(1), 16)
            if tag == "DW_TAG_compile_unit":
                skip_cu = False
                stack.clear()
            if skip_cu or tag not in KEEP_TAGS:
                cur = None
                stack.append((depth, -1))
                continue
            parent = next((o for d, o in reversed(stack) if o >= 0), None)
            cur = {"tag": tag, "attrs": {}, "children": [], "parent": parent}
            dies[off] = cur
            if parent is not None and parent in dies:
                dies[parent]["children"].append(off)
            stack.append((depth, off))
            continue
        if cur is None:
            continue
        a = ATTR_RE.match(line)
        if a and a.group(1) in KEEP_ATTRS:
            cur["attrs"][a.group(1)] = parse_value(a.group(2))
            if cur["tag"] == "DW_TAG_compile_unit" and a.group(1) == "DW_AT_name":
                name = cur["attrs"]["DW_AT_name"]
                # LTO's partitions ("<artificial>") hold the definitions of what was optimised across files
                if root not in name and name != "<artificial>" and not name.startswith(("src/", "port/", "engine/", "gen/", "build/")):
                    skip_cu = True
                    del dies[next(o for o, d in reversed(list(dies.items())) if d is cur)]
                    cur = None
    proc.wait()
    return dies


def sections(exe: Path) -> dict[str, tuple[int, int]]:
    out = {}
    for line in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m and int(m.group(2), 16):
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(2), 16) + int(m.group(3), 16))
    return out


def retail_symbols(version: str) -> dict[str, dict[str, int]]:
    """module -> name -> retail address, from config/<ver>/symbols/<module>.txt."""
    out = {}
    for f in sorted((ROOT / "config" / version / "symbols").glob("*.txt")):
        names = {}
        for line in f.read_text(errors="replace").splitlines():
            m = re.match(r"\s*([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;", line)
            if m:
                names[m.group(1)] = int(m.group(2), 16)
        out[f.stem] = names
    return out


def inventory_state() -> set[tuple[str, str]]:
    """(file, name) of the platform statics a raw state saves (classed `state`)."""
    out = set()
    for line in (ROOT / "port/src/system/savestate_inventory.txt").read_text().splitlines():
        parts = line.split(None, 3)
        if len(parts) >= 3 and not line.startswith("#") and parts[2] == "state" and parts[1] != "*":
            out.add((parts[0], parts[1]))
    return out


class Types:
    """Normalised types, by DIE offset: kind, size, and per kind its parts."""

    def __init__(self, dies):
        self.dies = dies
        self.memo = {}

    def strip(self, off):
        """Through typedefs and qualifiers to the type itself."""
        seen = 0
        while off is not None and off in self.dies and seen < 64:
            d = self.dies[off]
            if d["tag"] in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type"):
                off = d["attrs"].get("DW_AT_type")
                seen += 1
                continue
            return off
        return off

    def name(self, off):
        d = self.dies.get(off)
        if d is None:
            return "void"
        n = d["attrs"].get("DW_AT_name")
        if n:
            return n
        if d["tag"] == "DW_TAG_pointer_type":
            return self.name(d["attrs"].get("DW_AT_type")) + "*"
        if d["tag"] in ("DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_typedef"):
            return self.name(d["attrs"].get("DW_AT_type"))
        if d["tag"] == "DW_TAG_array_type":
            return self.name(d["attrs"].get("DW_AT_type")) + "[]"
        return {"DW_TAG_structure_type": "struct <anon>", "DW_TAG_union_type": "union <anon>"}.get(d["tag"], d["tag"])

    def size(self, off):
        off = self.strip(off)
        d = self.dies.get(off)
        if d is None:
            return 0
        if "DW_AT_byte_size" in d["attrs"]:
            return d["attrs"]["DW_AT_byte_size"]
        if d["tag"] == "DW_TAG_array_type":
            n = 1
            for c in d["children"]:
                s = self.dies[c]["attrs"]
                n *= s.get("DW_AT_count", s.get("DW_AT_upper_bound", -1) + 1)
            return n * self.size(d["attrs"].get("DW_AT_type"))
        if d["tag"] == "DW_TAG_pointer_type":
            return 8
        return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("build", type=Path)
    ap.add_argument("--json", type=Path, help="write the schema here (default BUILD/state_schema.json.gz)")
    ap.add_argument("--top", type=int, default=12, help="examples listed per finding")
    args = ap.parse_args()
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text() if (build / "CMakeCache.txt").exists() else ""
    version = (re.search(r"^XG_VERSION:\w+=(\w+)", cache, re.M) or [None, "US"])[1].lower()
    exe = next((p for p in (build / "xenogears", build / "xenogears-enhanced") if p.exists()), None)
    if exe is None:
        print(f"state_schema: no executable in {build}")
        return 1
    if ".debug_info" not in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout:
        print(f"state_schema: {exe} has no debug information (configure with -DCMAKE_BUILD_TYPE=RelWithDebInfo)")
        return 1

    secs = sections(exe)
    modsec = []  # (lo, hi, module, kind)
    for n, (lo, hi) in secs.items():
        m = re.match(r"xg(data|bss)_(\w+)$", n)
        if m:
            modsec.append((lo, hi, m.group(2), m.group(1)))
    modsec.sort()

    def module_of(addr):
        for lo, hi, mod, kind in modsec:
            if lo <= addr < hi:
                return mod, kind
        return None, None

    dies = read_dies(exe)
    types = Types(dies)
    retail = retail_symbols(version)
    inv = inventory_state()

    def retail_of(mod, name):
        """The retail address: the module's symbol file, else a splat label's own (D_8004F2F4)."""
        r = retail.get(mod, {}).get(name)
        if r is None:
            m = ADDR_NAME.match(name)
            if m:
                r = int(m.group(1), 16)
        return r

    def resolved(d):
        """A DIE's attributes with those of its declaration (DW_AT_specification) and, under LTO, its
        abstract origin, followed transitively."""
        a = dict(d["attrs"])
        for _ in range(8):
            ref = a.pop("DW_AT_specification", None) or a.pop("DW_AT_abstract_origin", None)
            if ref not in dies:
                break
            for k, v in dies[ref]["attrs"].items():
                a.setdefault(k, v)
        return a

    # ---- variables with a fixed address ------------------------------------------------------
    variables = []
    for off, d in dies.items():
        if d["tag"] != "DW_TAG_variable":
            continue
        loc = d["attrs"].get("DW_AT_location")
        if not (isinstance(loc, tuple) and loc[0] == "addr"):
            continue
        a = resolved(d)
        name = a.get("DW_AT_name")
        if not name:
            continue
        addr = loc[1]
        mod, kind = module_of(addr)
        f = str(a.get("DW_AT_decl_file", ""))
        rel = f.split("/port/src/", 1)[1] if "/port/src/" in f else None
        variables.append({"name": name, "addr": addr, "type": a.get("DW_AT_type"), "size": types.size(a.get("DW_AT_type")),
                          "module": mod, "section": kind, "file": f, "platform": rel,
                          "retail": retail_of(mod, name) if mod else None})

    # The generated sources define the asm-defined data as byte arrays (gen/: u8 gameState[0x2358]); the
    # headers declare their real types. A declaration's type of the same size is the one to encode by.
    declared = defaultdict(list)
    for off, d in dies.items():
        if d["tag"] == "DW_TAG_variable" and "DW_AT_location" not in d["attrs"] and d["attrs"].get("DW_AT_name"):
            t = d["attrs"].get("DW_AT_type")
            if t is not None:
                declared[d["attrs"]["DW_AT_name"]].append(t)

    def is_bytes(off):
        o = types.strip(off)
        d = dies.get(o)
        if d is None or d["tag"] != "DW_TAG_array_type":
            return False
        e = dies.get(types.strip(d["attrs"].get("DW_AT_type")))
        # an array of scalars: the generated data's bytes (.byte) or words (.word, .half)
        return bool(e and e["tag"] == "DW_TAG_base_type")

    def open_array_elem(off):
        """The element type of an array declared without a size (extern T name[];), else None."""
        o = types.strip(off)
        d = dies.get(o)
        if d is None or d["tag"] != "DW_TAG_array_type" or types.size(o):
            return None
        return d["attrs"].get("DW_AT_type")

    retyped = 0
    for v in variables:
        t = v["type"]
        if t is not None and is_bytes(t):
            for cand in declared.get(v["name"], []):
                if not is_bytes(cand) and types.size(cand) == v["size"]:
                    t = cand
                    retyped += 1
                    break
                elem = open_array_elem(cand)
                es = types.size(elem) if elem is not None else 0
                if es and v["size"] % es == 0 and not is_bytes(elem if elem is not None else cand):
                    v["elem"], v["count"] = elem, v["size"] // es  # completed below, in the schema's table
                    retyped += 1
                    break
        v["type"] = t

    game = [v for v in variables if v["module"]]
    platform = [v for v in variables if not v["module"] and v["platform"] and (v["platform"], v["name"]) in inv]

    # ---- types the saved variables hold by value ------------------------------------------------
    struct_fields = Counter(); untagged = defaultdict(list); unions = Counter(); pointer_fields = Counter()
    fnptr_fields = Counter(); seen = set()

    def owner_of(off):
        """Whose layout a type is: the PS1 SDK's (fixed, field names suffice), the port's (PC-only, keyed by
        name) or the game's (needs offset tags)."""
        f = str(dies[off]["attrs"].get("DW_AT_decl_file", ""))
        if re.search(r"/include/(psyq|libgpu|libgte|libspu|libcd|libetc|libapi|libpress|libgs)", f) or f.endswith("/psyq.h"):
            return "PS1 SDK"
        if "/port/src/" in f:
            return "port"
        return "game"

    def walk(off, path, holder):
        shown = types.name(off)  # a typedef's name before it is stripped
        off = types.strip(off)
        if off is None or off not in dies:
            return
        d = dies[off]
        tag = d["tag"]
        if tag == "DW_TAG_array_type":
            walk(d["attrs"].get("DW_AT_type"), path + "[]", holder)
            return
        if tag == "DW_TAG_pointer_type":
            target = dies.get(types.strip(d["attrs"].get("DW_AT_type")))
            if target and target["tag"] == "DW_TAG_subroutine_type":
                fnptr_fields[holder] += 1
            else:
                pointer_fields[holder] += 1
            return
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_class_type"):
            label = shown if "<anon>" not in shown else f"{shown} ({path})"
            if tag == "DW_TAG_union_type":
                unions[label + " in " + holder] += 1
            if off in seen:
                return
            seen.add(off)
            for c in d["children"]:
                m = dies[c]
                if m["tag"] != "DW_TAG_member":
                    continue
                fname = m["attrs"].get("DW_AT_name", "<anon>")
                struct_fields[holder] += 1
                if tag != "DW_TAG_union_type" and not FIELD_TAG.match(fname):
                    untagged[(owner_of(off), label)].append(fname)
                walk(m["attrs"].get("DW_AT_type"), path + "." + fname, holder)

    for v in game:
        walk(v["type"], v["name"], "game")
    for v in platform:
        walk(v["type"], v["name"], "platform")

    # ---- functions ---------------------------------------------------------------------------
    funcs = []
    for off, d in dies.items():
        if d["tag"] == "DW_TAG_subprogram" and "DW_AT_low_pc" in d["attrs"]:
            a = resolved(d)
            name = a.get("DW_AT_name")
            if name:
                f = str(a.get("DW_AT_decl_file", ""))
                side = "port" if "/port/src/" in f else "game"
                ra = None
                if side == "game":
                    for mod, names in retail.items():
                        if name in names:
                            ra = names[name]; break
                funcs.append({"name": name, "addr": a["DW_AT_low_pc"], "side": side, "retail": ra})

    # ---- heap allocation sites in the game C (static count) --------------------------------------
    alloc_re = re.compile(r"\b(allocateHeap|allocateBattleHeap|allocateTextureRamForText|allocateHeapForEntity)\s*\(")
    sites = annotated = 0; unannotated_files = Counter(); site_list = []
    for f in sorted((ROOT / "src").rglob("*.c")):
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            if alloc_re.search(line) and not re.match(r"\s*(void|u16|u8|s32)\s*\*?\s*\w+\(.*\)\s*[;{]", line):
                sites += 1
                sized = "PC_HEAP_" in line
                annotated += sized
                if not sized:
                    unannotated_files[f.relative_to(ROOT).as_posix()] += 1
                site_list.append({"file": f.relative_to(ROOT).as_posix(), "line": n, "sized": sized})

    # ---- report ------------------------------------------------------------------------------
    gj = [v for v in game if v["retail"] is not None]
    gn = [v for v in game if v["retail"] is None]
    print(f"state schema of {exe.relative_to(ROOT) if ROOT in exe.parents else exe} ({version.upper()})")
    print(f"  DWARF entries kept: {len(dies)}; variables at fixed addresses: {len(variables)}")
    print(f"  game module variables: {len(game)} ({sum(v['size'] for v in game)} bytes), "
          f"{len(gj)} with a retail address, {len(gn)} without")
    def origin(v):
        f = v["file"]
        if "/port/src/" in f:
            return "PC files in game modules (port/src/)"
        if "/engine/" in f or re.search(r"/src/", f):
            return "game C (src/, engine/)"
        if "/gen/" in f or "/build/" in f:
            return "generated"
        return "no file (asm-defined data)" if not f else "other"
    for o in sorted({origin(v) for v in game}):
        part = [v for v in game if origin(v) == o]
        miss = [v for v in part if v["retail"] is None]
        print(f"    {o}: {len(part)}, {len(part) - len(miss)} with a retail address")
        if miss and not o.startswith("PC files"):
            by_file = Counter(Path(v["file"]).name or "?" for v in miss)
            print("      without, by file: " + ", ".join(f"{f} {n}" for f, n in by_file.most_common(args.top)))
            print("      e.g. " + ", ".join(v["name"] for v in miss[:args.top]))
    print(f"  platform statics classed state: {len(platform)} of {len(inv)} inventory entries found in DWARF")
    print(f"  struct fields saved by value: game {struct_fields['game']}, platform {struct_fields['platform']}")
    for who, note in (("game", "need an offset tag"), ("PS1 SDK", "the SDK's fixed layout: keyed by name"),
                      ("port", "PC-only: keyed by name")):
        part = {t: fs for (o, t), fs in untagged.items() if o == who}
        n = sum(len(set(v)) for v in part.values())
        print(f"  struct fields without an offset tag, {who} types ({note}): {n} in {len(part)} types")
        if who == "game":
            for t, fs in sorted(part.items(), key=lambda kv: -len(set(kv[1])))[:args.top]:
                print(f"    {t}: {len(set(fs))} ({', '.join(sorted(set(fs))[:6])}{'...' if len(set(fs)) > 6 else ''})")
    print(f"  unions saved by value: {sum(unions.values())} ({len(unions)} distinct)")
    for u, n in unions.most_common(args.top):
        print(f"    {u}: {n}")
    print(f"  pointer fields: game {pointer_fields['game']} data, {fnptr_fields['game']} function; "
          f"platform {pointer_fields['platform']} data, {fnptr_fields['platform']} function")
    fg = [f for f in funcs if f["side"] == "game"]
    print(f"  functions: {len(funcs)}; game {len(fg)}, {sum(1 for f in fg if f['retail'] is not None)} with a retail address")
    print(f"  heap allocation calls in src/: {sites}, {annotated} with a PC_HEAP_* size on the line, "
          f"{sites - annotated} without")
    print("    without, by file: " + ", ".join(f"{f} {n}" for f, n in unannotated_files.most_common(args.top)))

    out = args.json or (build / "state_schema.json.gz")
    # ---- the types a state's variables hold, for the canonical encoder (tools/statecanon.py) -------
    tids: dict[int, str] = {}
    table: dict[str, dict] = {}

    def tid(off):
        """A type's id in the schema (its DIE offset), described in `table` once; typedefs and qualifiers
        are looked through, a struct keeps its typedef's name for messages."""
        shown = types.name(off)
        if shown == "OTag" and off in dies and dies[off]["tag"] == "DW_TAG_typedef":
            # an ordering table's link (include/psyq.h): the length in the top byte, an address below it
            key = "OTag"
            table.setdefault(key, {"k": "otag", "size": types.size(off), "name": "OTag"})
            return key
        off = types.strip(off)
        if off is None or off not in dies:
            return None
        key = f"{off:x}"
        if off in tids:
            return key
        tids[off] = key
        d = dies[off]; tag = d["tag"]; a = d["attrs"]
        if tag == "DW_TAG_base_type":
            enc = str(a.get("DW_AT_encoding", ""))
            table[key] = {"k": "float" if "float" in enc else "int", "size": a.get("DW_AT_byte_size", 0),
                          "signed": "signed" in enc and "unsigned" not in enc, "name": a.get("DW_AT_name", shown)}
        elif tag == "DW_TAG_enumeration_type":
            table[key] = {"k": "int", "size": a.get("DW_AT_byte_size", 4), "signed": False, "name": shown}
        elif tag == "DW_TAG_pointer_type":
            target = dies.get(types.strip(a.get("DW_AT_type")))
            fn = bool(target and target["tag"] == "DW_TAG_subroutine_type")
            table[key] = {"k": "ptr", "size": 8, "fn": fn}
            if not fn:
                table[key]["to"] = tid(a.get("DW_AT_type")) if a.get("DW_AT_type") is not None else None
        elif tag == "DW_TAG_array_type":
            dims = []
            for c in d["children"]:
                sa = dies[c]["attrs"]
                dims.append(sa.get("DW_AT_count", sa.get("DW_AT_upper_bound", -1) + 1))
            elem = tid(a.get("DW_AT_type"))
            for n in reversed(dims[1:]):  # int a[2][3]: an array of 2 arrays of 3
                inner = f"{key}.{n}"
                table[inner] = {"k": "arr", "of": elem, "n": n, "size": n * table[elem]["size"] if elem else 0}
                elem = inner
            n = dims[0] if dims else 0
            table[key] = {"k": "arr", "of": elem, "n": n, "size": types.size(off)}
        elif tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_class_type"):
            table[key] = {"k": "union" if tag == "DW_TAG_union_type" else "struct", "size": a.get("DW_AT_byte_size", 0),
                          "name": shown, "owner": owner_of(off), "fields": []}
            fields = []
            for c in d["children"]:
                m = dies[c]
                if m["tag"] != "DW_TAG_member":
                    continue
                ma = m["attrs"]
                fname = ma.get("DW_AT_name", "")
                tag_m = FIELD_TAG.match(fname)
                f = {"name": fname, "off": ma.get("DW_AT_data_member_location", 0), "type": tid(ma.get("DW_AT_type")),
                     "retail": int(tag_m.group(1), 16) if tag_m else None}
                if "DW_AT_bit_size" in ma:
                    f["bits"] = ma["DW_AT_bit_size"]
                    f["bit"] = ma.get("DW_AT_data_bit_offset", 0)
                    f["off"] = f["bit"] // 8 if "DW_AT_data_member_location" not in ma else f["off"]
                fields.append(f)
            table[key]["fields"] = fields
        else:  # a function type reached by value, void, or something the encoder copies as bytes
            table[key] = {"k": "bytes", "size": types.size(off)}
        return key

    for v in variables:
        if v.get("elem") is not None:
            e = tid(v["elem"])
            key = f"{e}[{v['count']}]"
            table.setdefault(key, {"k": "arr", "of": e, "n": v["count"], "size": v["size"]})
            v["tid"] = key
        else:
            v["tid"] = tid(v["type"]) if v["type"] is not None else None

    # The saved parts' files (savestate.c parts[]: {"gpu", gpuStateIo}): a part's tags (XG_STATE_VAR's
    # expressions) name variables of that file.
    part_files = {}
    parts_src = (ROOT / "port/src/system/savestate.c").read_text()
    io_files = {f["name"]: f for f in funcs}
    io_decl = {}
    for off, d in dies.items():
        if d["tag"] == "DW_TAG_subprogram" and "DW_AT_low_pc" in d["attrs"]:
            a = resolved(d)
            name = str(a.get("DW_AT_name", ""))
            f = str(d["attrs"].get("DW_AT_decl_file", "") or a.get("DW_AT_decl_file", ""))
            if name.endswith("StateIo") and "/port/src/" in f and f.endswith((".c", ".cpp")):
                io_decl[name] = f.split("/port/src/", 1)[1]
    for pname, fn in re.findall(r'\{"(\w+)",\s*(\w+StateIo)\}', parts_src):
        if fn in io_decl:
            part_files[pname] = io_decl[fn]

    schema = {"version": version, "exe": str(exe), "variables": [
        {k: v.get(k) for k in ("name", "addr", "size", "module", "section", "retail", "platform", "tid")}
        for v in variables if v["module"] or v["platform"]],
        "types": table, "parts": part_files, "retyped": retyped,
        # where a raw state's memory chunks sit in this build (statecanon.py reads them by these)
        "sections": {f"{mod}.{kind}": [lo, hi] for lo, hi, mod, kind in modsec},
        "regions": {v["name"]: [v["addr"], v["addr"] + v["size"]] for v in variables
                    if v["name"] in ("heapArena", "gameStack", "pcScratchpad")},
        "functions": funcs, "allocation_sites": site_list}
    with gzip.open(out, "wt") as fh:
        json.dump(schema, fh)
    print(f"  schema: {out} ({out.stat().st_size} bytes)")
    game_untagged = sum(len(set(v)) for (o, _), v in untagged.items() if o == "game")
    print(f"state-schema: {len(gj)} of {len(game)} game variables with a retail address, {game_untagged} game "
          f"fields untagged, {len(unions)} unions, {sites - annotated} of {sites} heap allocation calls unsized")
    return 0


if __name__ == "__main__":
    sys.exit(main())
