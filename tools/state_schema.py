#!/usr/bin/env python3
"""Canonical save states, phase P0 (xg-htn.60.1; design: docs/CANONICAL_STATES.md, local).

The state schema of a PC build, read from its DWARF, and a coverage report of what a canonical state
could not yet identify:

    tools/state_schema.py BUILD [--json OUT] [--ship OUT] [--image IMAGE] [--dwarfdump TOOL] [--gcc GCC] [--quiet]
    tools/state_schema.py --verify SHIPPED --image IMAGE [--stripped] [--portable]
    tools/state_schema.py --verify-apk APK [--portable]

BUILD is a configured port build whose executable has debug information (a RelWithDebInfo or Debug
build: cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo). Read with llvm-dwarfdump (the system's or the NDK's).
The image is an ELF executable, the game library of a shared-library build (XG_SHARED: lib<name>.so,
which the launcher loads, as Android's app does), or a Windows (MinGW, PE/COFF) executable. Addresses
in the schema are the image's link addresses; a position-independent image runs at them plus its load
bias, which states record (statecanon.py Schema(build, bias)). The schema names the image by its
identity ("image": the game's stateImageId), which neither the load address nor stripping the debug
information changes, so a game reading the schema beside it can tell it describes that image.

The schema joins each native object to its stable identity:
  - a variable in a game module's sections (xgdata_<module> / xgbss_<module>) to its retail address in
    config/<ver>/symbols/<module>.txt (overlays share address ranges: the module is part of it);
  - a struct field to its retail offset, from its offset-tagged name (m1C_actor: +0x1C);
  - a function to its retail address;
  - a platform static classed `state` in port/src/system/savestate_inventory.txt to its file and name.
The report counts what is not joined yet: module variables without a retail address, struct fields
without an offset tag in types a state saves, unions, pointer fields, and the game C's heap allocation
sites without a retail size (PC_HEAP_*).

--ship OUT also writes the schema to hand out with the build (port/CMakeLists.txt XG_STATE_SCHEMA; the
packages install it where port/src/system/stateportable.c looks): the same schema with its "exe" the image's
file name, not the build machine's path, and its read-only ranges' file offsets those of the image stripped (a
PE's sections move), in the packed form the game reads ("XGSCHEMA", the JSON's size, the JSON packed by
statePack). The game names a schema by the hash of its JSON, so the packed form keeps the name of those bytes.
--verify checks such a file against the image it ships with: it unpacks, it was read from that image (the
identity), its file offsets are the image's stripped, and no string in it is an absolute path; --stripped also
requires the image to carry no debug information (what a package ships), --portable a schema that lets its states
convert to other builds (not "portable": false). --verify-apk does that for every game library of an Android APK
(lib/<abi>/libxenogears*.so, stripped as the APK packs it) with its schema, assets/schemas/<abi>/<library>.xgschema
(tools/build_android.sh), and requires no other schema there.

An image compiled by clang (Android) has no debug information for extern declarations, which the generated data's
types come from: the game C is compiled once more with the host's GCC (--gcc) to read what they are called, and
each is the image's own type of that name (clang_declared). Without GCC the schema says "portable": false.

Exit status: 0 when the schema was built (the gate only reports in P0), 1 when it could not be."""
from __future__ import annotations  # (deferred: the build runs this with the system's Python, 3.11 here)

import argparse, contextlib, functools, gzip, io, json, os, re, shlex, shutil, struct, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecheck as sc  # noqa: E402 (game_library)
from statecanon import state_hash  # noqa: E402 (the build id's hash, statecodec.c stateHash)
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
    "DW_AT_linkage_name", "DW_AT_encoding", "DW_AT_bit_offset", "DW_AT_producer",
}
TAG_RE = re.compile(r"^0x([0-9a-f]+):(\s+)(DW_TAG_\w+|NULL)")
ATTR_RE = re.compile(r"^\s+(DW_AT_\w+)\s+\((.*)\)\s*$")
FIELD_TAG = re.compile(r"^m([0-9A-Fa-f]+)(?:_|$)")  # m1C_actor, and m15D8 (no name yet)
ADDR_NAME = re.compile(r"^[DF]_([0-9A-Fa-f]{8})$")  # splat labels carry their retail address


def type_id(name: str) -> int:
    """A heap type's identity, as the PC computes it (port/src/game/main/statetypes.c pcStateTypeId): FNV-1a
    (32 bits) of the name without its blanks; 0 means untyped, so 0 becomes 1."""
    h = 0x811C9DC5
    for c in name.encode():
        if c not in b" \t\n":
            h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h or 1


def macro_args(text: str, start: int) -> list[str] | None:
    """The top-level arguments of the macro call whose "(" is at start."""
    depth = 0; args = []; cur = start + 1
    for i in range(start, len(text)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append(text[cur:i].strip()); return args
        elif c == "," and depth == 1:
            args.append(text[cur:i].strip()); cur = i + 1
    return None


STATE_MACRO = re.compile(r"\b(PC_HEAP_OF|PC_HEAP_ONE|PC_STATE_[A-Z_]+)\s*\(")
# The state rules' macros (include/pc_state.h; docs/CANONICAL_STATES.md 17.2) and their argument counts. The set
# is closed: the rules language is fixed in the first canonical release, so any other PC_STATE_* macro fails the
# gate here, before a schema could carry a rule some release's converter does not know.
RULE_MACROS = {
    "PC_STATE_WAS": 4, "PC_STATE_VAR_WAS": 3, "PC_STATE_TYPE_WAS": 3, "PC_STATE_TAG_WAS": 3, "PC_STATE_FN_WAS": 3,
    "PC_STATE_VALUES": 4, "PC_STATE_VAR_VALUES": 3, "PC_STATE_RESET": 3, "PC_STATE_VAR_RESET": 2,
    "PC_STATE_REMOVED": 4, "PC_STATE_VAR_REMOVED": 3, "PC_STATE_DEFAULT": 3, "PC_STATE_VAR_DEFAULT": 2,
    "PC_STATE_HASHES": 2,
}
OTHER_STATE_MACROS = {"PC_STATE_AT", "PC_STATE_NAME", "PC_STATE_COUNTED", "PC_STATE_TAIL", "PC_STATE_UNION",
                      "PC_STATE_MEMBER", "PC_STATE_VARIABLE"}
REVISION_DEFINE = re.compile(r"^\s*#\s*define\s+PC_STATE_REVISION\s+(\d+)\s*$", re.M)


def state_annotations(roots: list[Path]):
    """include/pc_state.h's annotations in the C a build compiles: the heap type names (PC_HEAP_OF,
    PC_STATE_AT, PC_STATE_NAME), the counted arrays (PC_STATE_COUNTED), the union rules, and the state rules
    (RULE_MACROS, with the macro's arguments), each with where it is; the state revision (PC_STATE_REVISION's
    values found, pc_state.h's own too); and the PC_STATE_* macros this tool does not know."""
    names = defaultdict(list); counted = []; unions = []
    rules = []; revisions = set(); unknown = []
    for root in roots:
        for f in sorted(list(root.rglob("*.c")) + list(root.rglob("*.h"))):
            text = f.read_text(errors="replace")
            if "PC_STATE_REVISION" in text:
                revisions.update(int(v) for v in REVISION_DEFINE.findall(text))
            if f.name == "pc_state.h":
                continue
            if "PC_HEAP_OF" not in text and "PC_STATE_" not in text:
                continue
            for m in STATE_MACRO.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                if text[text.rfind("\n", 0, m.start()) + 1:m.start()].lstrip().startswith(("#define", "*", "//")):
                    continue
                where = f"{f.relative_to(ROOT).as_posix() if ROOT in f.parents else f}:{line}"
                kind = m.group(1)
                if kind.startswith("PC_STATE_") and kind not in RULE_MACROS and kind not in OTHER_STATE_MACROS:
                    unknown.append(f"{kind} ({where}): not a state macro this tool knows (include/pc_state.h)")
                    continue
                args = macro_args(text, m.end() - 1)
                if not args:
                    continue
                if kind in RULE_MACROS:
                    if len(args) != RULE_MACROS[kind]:
                        unknown.append(f"{kind} ({where}): {len(args)} arguments, it takes {RULE_MACROS[kind]}")
                    else:
                        rules.append({"macro": kind, "args": args, "at": where, "file": f.resolve()})
                    continue
                if kind == "PC_STATE_COUNTED" and len(args) == 4:
                    counted.append({"type": args[0], "array": args[1], "count": args[2], "extra": int(args[3], 0),
                                    "at": where})
                elif kind == "PC_STATE_TAIL" and len(args) == 2:  # the array fills the rest of the block
                    counted.append({"type": args[0], "array": args[1], "count": None, "extra": 0, "at": where})
                elif kind == "PC_STATE_UNION" and len(args) == 4:  # the view a selector member names
                    views = [v.strip() for v in args[3].strip().strip("()").split(",")]
                    unions.append({"type": args[0], "member": args[1], "selector": args[2], "views": views, "at": where})
                elif kind == "PC_STATE_AT" and len(args) == 3:
                    names[args[1]].append(where)
                elif kind in ("PC_HEAP_OF", "PC_STATE_NAME") and args:
                    names[args[0]].append(where)
                elif kind == "PC_HEAP_ONE" and args:  # one T, then bytes: its name says so (#T "[1]")
                    names[args[0] + "[1]"].append(where)
    return names, counted, unions, rules, revisions, unknown


DWARFDUMP = None  # --dwarfdump


INT_LITERAL = re.compile(r"\s*([-+]?)\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*$")
STRING_LITERAL = re.compile(r'\s*"([^"\\]*)"\s*$')
ID_MODULE = re.compile(r"^\w+:(?:[0-9a-f]{8}|pc:\w+)(?::\w+)?$")  # field:800adc04, main:pc:name (:name: a collision)
ID_PLATFORM = re.compile(r"^pc:[\w./\-]+:\w+$")                   # pc:system/hw.c:name


def int_arg(text: str):
    m = INT_LITERAL.match(text)
    return None if m is None else (-1 if m.group(1) == "-" else 1) * int(m.group(2), 0)


def string_arg(text: str):
    m = STRING_LITERAL.match(text)
    return None if m is None else m.group(1)


def list_arg(text: str, item) -> list | None:
    """A parenthesized list of items: ("a", "b"), ((0, 1), (2, 3)); None when it does not parse."""
    t = text.strip()
    if not (t.startswith("(") and t.endswith(")")):
        return None
    inner = t[1:-1]
    out = []; depth = 0; cur = 0
    for i, c in enumerate(inner + ","):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            part = inner[cur:i].strip(); cur = i + 1
            if not part:
                if i < len(inner):
                    return None
                continue
            v = item(part)
            if v is None:
                return None
            out.append(v)
    return out if out else None


def pair_arg(text: str):
    """(old, new): two integer literals."""
    t = text.strip()
    if not (t.startswith("(") and t.endswith(")")):
        return None
    parts = t[1:-1].split(",")
    if len(parts) != 2:
        return None
    a, b = int_arg(parts[0]), int_arg(parts[1])
    return None if a is None or b is None else [a, b]


def old_key(text: str) -> str:
    """A member's key from its name as statecanon's field_key makes it (an offset tag: +<hex>, else pc:<name>), or
    the key itself ("+1c", "pc:name", "+1c:name")."""
    if text.startswith(("+", "pc:")):
        return text
    m = FIELD_TAG.match(text)
    return f"+{int(m.group(1), 16):x}" if m else f"pc:{text}"


def state_rules(raw: list, revisions: set, table: dict, variables: list, part_files: dict, type_names: dict,
                struct_keys: dict, fn_names: set) -> tuple[dict, list[str]]:
    """The state rules (include/pc_state.h's RULE_MACROS; docs/CANONICAL_STATES.md 17.2) as the schema keeps them,
    each target resolved against this build's types and variables, and the problems found: an unknown name, a
    member a rule cannot apply to, a revision out of range, an old identity this build still has (no identity is
    used twice: a rule's old name must name nothing now), two rules giving one name at one revision.

    struct_keys: a struct's name (its own or a typedef's) -> the schema type keys of its DWARF entries (one per
    compile unit). A field rule names every entry: the converter finds a type's rules by the entry it holds."""
    import statecanon as cn
    problems = []
    revision = max(revisions) if revisions else 0
    if len(revisions) > 1:
        problems.append(f"PC_STATE_REVISION has several values: {sorted(revisions)}")
    keyed, _ = cn.keyed_variables([dict(v) for v in variables])
    ids = {v["id"] for v in keyed}
    by_name = defaultdict(list)
    for v in keyed:
        by_name[v["name"]].append(v)

    def kind_of(tid):
        return table.get(tid, {}).get("k")

    def int_like(tid, bits=None):
        """An integer (an enum too), a bitfield, or a one-dimensional array of integers: what values and defaults
        apply to; (low, high) of what it holds per element, or None."""
        t = table.get(tid, {})
        if bits is not None:
            return 0, (1 << bits) - 1
        if t.get("k") == "arr" and kind_of(t.get("of")) == "int":
            t = table[t["of"]]
        if t.get("k") != "int" or not t.get("size") or t["size"] > 8:
            return None
        n = 8 * t["size"]  # (and within 64-bit signed: what the schema's JSON carries to the C reader)
        return (-(1 << (n - 1)), (1 << (n - 1)) - 1) if t.get("signed") else (0, min((1 << n) - 1, (1 << 63) - 1))

    def member(tname, path, where):
        """(type keys, key, field) of T's member path through unnamed structs, the same in every entry of T."""
        keys = sorted(struct_keys.get(tname, ()))
        if not keys:
            problems.append(f"{where}: no struct or union {tname} in this build")
            return None
        out, last = [], None
        for k in keys:
            tid, f = k, None
            names = path.split(".")
            for i, n in enumerate(names):
                t = table.get(tid, {})
                if t.get("k") not in ("struct", "union"):
                    f = None; break
                f = next((x for x in t["fields"] if x["name"] == n), None)
                if f is None:
                    break
                if i < len(names) - 1:
                    tid = f["type"]
            if f is None:
                problems.append(f"{where}: {tname} has no member {path}")
                return None
            seen = set(); key = None
            for x in table[tid]["fields"]:
                kx = cn.Encoder.field_key(None, x, seen)
                if x is f:  # (a union's views go by their names)
                    key = x["name"] if table[tid]["k"] == "union" else kx
            if last is not None and (key, f.get("bits")) != (last[0], last[1].get("bits")):
                problems.append(f"{where}: {tname}.{path} is not the same member in every compile unit")
                return None
            last = (key, f)
            out.append(tid)
        return sorted(set(out)), last[0], last[1]

    def variable(name, file, where):
        found = by_name.get(name, [])
        if len(found) > 1:  # statics of one name in several files: the macro's own
            found = [v for v in found if v.get("file") and Path(v["file"]).resolve() == file] or found
        if len(found) != 1:
            problems.append(f"{where}: {'no' if not found else 'more than one'} saved variable {name} in this build")
            return None
        return found[0]

    def fits(rng, values, where, what):
        bad = [x for x in values if not rng[0] <= x <= rng[1]]
        if bad:
            problems.append(f"{where}: {what} {bad[0]} does not fit {rng[0]}..{rng[1]}")
            return False
        return True

    out = []
    for r in raw:
        mac, a, where = r["macro"], r["args"], r["at"]
        hist = mac not in ("PC_STATE_DEFAULT", "PC_STATE_VAR_DEFAULT", "PC_STATE_HASHES")
        rule = {}
        if hist:
            rev = int_arg(a[0])
            if rev is None or not 1 <= rev <= revision:
                problems.append(f"{mac} ({where}): revision {a[0]} is not from 1 to PC_STATE_REVISION ({revision})")
                continue
            rule["rev"] = rev; a = a[1:]
        kind = {"PC_STATE_WAS": "was", "PC_STATE_VAR_WAS": "was", "PC_STATE_TYPE_WAS": "was", "PC_STATE_TAG_WAS": "was",
                "PC_STATE_FN_WAS": "was",
                "PC_STATE_VALUES": "values", "PC_STATE_VAR_VALUES": "values", "PC_STATE_RESET": "reset",
                "PC_STATE_VAR_RESET": "reset", "PC_STATE_REMOVED": "removed", "PC_STATE_VAR_REMOVED": "removed",
                "PC_STATE_DEFAULT": "default", "PC_STATE_VAR_DEFAULT": "default", "PC_STATE_HASHES": "hashes"}[mac]
        rule = {"kind": kind, **rule}
        label = f"{mac} ({where})"
        if mac in ("PC_STATE_WAS", "PC_STATE_VALUES", "PC_STATE_RESET", "PC_STATE_DEFAULT", "PC_STATE_HASHES"):
            tname, path = a[0].strip(), re.sub(r"\s+", "", a[1])
            m = member(tname, path, label)
            if m is None:
                continue
            tids, key, f = m
            rule.update({"type": tname, "member": path, "tids": tids, "field": key})
            rng = int_like(f["type"], f.get("bits"))
            if mac == "PC_STATE_WAS":
                old = string_arg(a[2])
                if not old:
                    problems.append(f"{label}: the old name is not a string"); continue
                union = table[tids[0]]["k"] == "union"
                ok = old if union else old_key(old)
                taken = {x["name"] for x in table[tids[0]]["fields"]} if union else \
                    {cn.Encoder.field_key(None, x, s) for s in [set()] for x in table[tids[0]]["fields"]}
                if ok == key or ok in taken:
                    problems.append(f"{label}: the old key {ok} is a member of {tname} in this build (no key names two "
                                    "members)"); continue
                rule["old"] = ok
            elif mac == "PC_STATE_VALUES":
                pairs = list_arg(a[2], pair_arg)
                if rng is None or pairs is None:
                    problems.append(f"{label}: " + ("not an integer member" if rng is None else
                                                    "the values are not ((old, new), ...) of integer literals"))
                    continue
                if not fits(rng, [x for p in pairs for x in p], label, "the value"):
                    continue
                rule["map"] = pairs
            elif mac == "PC_STATE_DEFAULT":
                v = int_arg(a[2])
                if rng is None or v is None:
                    problems.append(f"{label}: " + ("not an integer member" if rng is None else
                                                    "the default is not an integer literal"))
                    continue
                if not fits(rng, [v], label, "the default"):
                    continue
                rule["value"] = v
            elif mac == "PC_STATE_HASHES" and (f.get("bits") is not None or table.get(f["type"], {}).get("k") != "int"
                                               or table[f["type"]]["size"] != 4):
                problems.append(f"{label}: type hashes are a 32-bit integer member"); continue
        elif mac in ("PC_STATE_VAR_WAS", "PC_STATE_VAR_VALUES", "PC_STATE_VAR_RESET", "PC_STATE_VAR_DEFAULT"):
            v = variable(a[0].strip(), r["file"], label)
            if v is None:
                continue
            rule.update({"var": v["id"], "name": v["name"]})
            rng = int_like(v["tid"])
            if mac == "PC_STATE_VAR_WAS":
                old = string_arg(a[1])
                if not old:
                    problems.append(f"{label}: the old identity is not a string"); continue
                if ":" not in old:  # a PC variable's old name: its identity in the same module or file
                    if v.get("module") and v.get("retail") is not None:
                        problems.append(f"{label}: {v['id']} is a retail address: give the old identity"); continue
                    old = v["id"].rsplit(":", 1)[0] + ":" + old
                if not (ID_MODULE.match(old) or ID_PLATFORM.match(old)) or old == v["id"] or old in ids:
                    problems.append(f"{label}: the old identity {old} is " + (
                        "a variable of this build (no identity names two variables)" if old in ids else "not one"))
                    continue
                rule["old"] = old
            elif mac == "PC_STATE_VAR_VALUES":
                pairs = list_arg(a[1], pair_arg)
                if rng is None or pairs is None:
                    problems.append(f"{label}: " + ("not an integer variable" if rng is None else
                                                    "the values are not ((old, new), ...) of integer literals"))
                    continue
                if not fits(rng, [x for p in pairs for x in p], label, "the value"):
                    continue
                rule["map"] = pairs
            elif mac == "PC_STATE_VAR_DEFAULT":
                d = int_arg(a[1])
                if rng is None or d is None:
                    problems.append(f"{label}: " + ("not an integer variable" if rng is None else
                                                    "the default is not an integer literal"))
                    continue
                if not fits(rng, [d], label, "the default"):
                    continue
                rule["value"] = d
        elif mac == "PC_STATE_TYPE_WAS":
            tname, old = a[0].strip(), string_arg(a[1])
            if tname not in struct_keys or tname not in type_names:
                problems.append(f"{label}: no struct or union {tname} in this build"); continue
            if not old or old in type_names or old == tname:
                problems.append(f"{label}: the old name {old} is " + (
                    "a type of this build (no name names two types)" if old in type_names else "not one"))
                continue
            rule.update({"typename": tname, "old": old})
        elif mac == "PC_STATE_TAG_WAS":
            tag, old = string_arg(a[0]), string_arg(a[1])
            if not tag or not old or tag == old or tag.partition(".")[0] not in part_files or "." not in old:
                problems.append(f"{label}: a tag is \"part.expression\" of a part this build saves"); continue
            rule.update({"tag": tag, "old": old})
        elif mac == "PC_STATE_FN_WAS":
            name, old = a[0].strip(), string_arg(a[1])
            if name not in fn_names:
                problems.append(f"{label}: no function {name} in this build"); continue
            if not old or old == name or old in fn_names:
                problems.append(f"{label}: the old name {old} is " + (
                    "a function of this build (no name names two functions)" if old in fn_names else "not one"))
                continue
            rule.update({"fn": name, "old": old})
        elif mac == "PC_STATE_REMOVED":
            tname, value, olds = a[0].strip(), int_arg(a[1]), list_arg(a[2], string_arg)
            keys = sorted(struct_keys.get(tname, ()))
            if not keys or value is None or not olds:
                problems.append(f"{label}: " + ("no struct or union " + tname if not keys else
                                                "the value is not an integer literal" if value is None else
                                                "the names are not (\"name\", ...)"))
                continue
            taken = {cn.Encoder.field_key(None, x, s) for s in [set()] for x in table[keys[0]]["fields"]}
            fields = [old_key(o) for o in olds]
            if any(k in taken for k in fields):
                problems.append(f"{label}: {tname} still has {next(k for k in fields if k in taken)}"); continue
            rule.update({"type": tname, "tids": keys, "fields": fields, "value": value})
        elif mac == "PC_STATE_VAR_REMOVED":
            value, olds = int_arg(a[0]), list_arg(a[1], string_arg)
            if value is None or not olds or any(not (ID_MODULE.match(o) or ID_PLATFORM.match(o)) for o in olds):
                problems.append(f"{label}: (value, (\"identity\", ...)) with identities like field:800adc04"); continue
            if any(o in ids for o in olds):
                problems.append(f"{label}: {next(o for o in olds if o in ids)} is a variable of this build"); continue
            rule.update({"vars": olds, "value": value})
        rule["at"] = where
        out.append(rule)

    # No identity names two things at any revision: a rename's old name is free from the rename on (above), and two
    # renames never give one old name to two targets that both had it at once.
    space = defaultdict(list)  # (namespace, old) -> [(rev, target)]
    for rule in out:
        if rule["kind"] != "was":
            continue
        ns = ("field", tuple(rule["tids"])) if "field" in rule else ("var",) if "var" in rule else \
            ("type",) if "typename" in rule else ("fn",) if "fn" in rule else ("tag",)
        target = rule.get("field") or rule.get("var") or rule.get("typename") or rule.get("fn") or rule.get("tag")
        space[ns + (rule["old"],)].append((rule["rev"], target, rule["at"]))
    for (*ns, old), uses in space.items():
        if len({t for _, t, _ in uses}) > 1:
            problems.append(f"PC_STATE_*WAS ({uses[0][2]}, {uses[1][2]}): {old} is the old name of two things")
    by_target = defaultdict(list)
    for rule in out:
        if rule["kind"] == "was":
            t = (rule.get("field"), tuple(rule.get("tids", ())), rule.get("var"), rule.get("typename"), rule.get("tag"),
                 rule.get("fn"))
            by_target[t].append(rule)
    for rs in by_target.values():
        revs = [x["rev"] for x in rs]
        if len(set(revs)) != len(revs):
            problems.append(f"PC_STATE_*WAS ({rs[0]['at']}): two renames of one thing at one revision")
    return {"language": cn.RULES_LANGUAGE, "revision": revision, "list": out}, problems


def dwarfdump() -> str:
    if DWARFDUMP:
        return DWARFDUMP
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
    # (an object at an offset into a block of merged globals: AArch64's clang merges a file's statics)
    m = re.match(r"DW_OP_addr 0x([0-9a-f]+)(?:, DW_OP_plus_uconst 0x([0-9a-f]+))?$", v)
    if m:
        return ("addr", int(m.group(1), 16) + int(m.group(2) or "0", 16))
    if v in ("true", "false"):
        return v == "true"
    if re.match(r"^-?\d+$", v):
        return int(v)
    return v


def read_dies(exe: Path, build: Path, keep: set[str] = KEEP_TAGS) -> dict:
    """offset -> {tag, attrs, children, parent}; only the tags (keep) and attributes a schema needs, and only
    the compile units of this repository (the game C, the port) and of the build (the generated sources, gen/,
    in the build's folder, which need not be in the repository's: /mnt/Games/scratch/...)."""
    dies: dict[int, dict] = {}
    stack: list[tuple[int, int]] = []  # (depth, offset)
    cur = None
    skip_cu = False
    root, gen = str(ROOT), str(build)
    proc = subprocess.Popen([dwarfdump(), "--debug-info", str(exe)], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    for line in proc.stdout:
        m = TAG_RE.match(line) if line.startswith("0x") else None  # (an entry's line; its attributes are indented)
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
            if skip_cu or tag not in keep:
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
            v = parse_value(a.group(2))
            if a.group(1) == "DW_AT_decl_file" and isinstance(v, str) and v and not v.startswith("/"):
                # a build to hand out names its files from the repository (-ffile-prefix-map, port/CMakeLists.txt
                # XG_DIST: "port/src/system/hw.c"): read as a development build's absolute names are, below
                # (every test there looks for "/port/src/" and the like)
                v = "/" + v
            elif a.group(1) in ("DW_AT_decl_file", "DW_AT_name") and isinstance(v, str) and v.startswith("/") and \
                    (a.group(1) == "DW_AT_decl_file" or cur["tag"] == "DW_TAG_compile_unit"):
                # by its real path: a tree reached through a symbolic link (a worktree parked on another disk)
                # names its files by the link, and ROOT and the build are resolved
                v = real_path(v)
            cur["attrs"][a.group(1)] = v
            if cur["tag"] == "DW_TAG_compile_unit" and a.group(1) == "DW_AT_name":
                name = cur["attrs"]["DW_AT_name"]
                # LTO's partitions ("<artificial>") hold the definitions of what was optimised across files
                if root not in name and gen not in name and name != "<artificial>" and \
                        not name.startswith(("src/", "port/", "engine/", "gen/", "build/")):
                    skip_cu = True
                    del dies[next(o for o, d in reversed(list(dies.items())) if d is cur)]
                    cur = None
    if proc.wait() != 0:
        raise SystemExit(f"state_schema: {dwarfdump()} --debug-info {exe} failed")
    return dies


# ---- a clang image's declarations, read with the host's GCC (xg-htn.60.8) -----------------------------
# The generated sources define the asm-defined data as byte arrays (gen/: u8 gameState[0x2358]) and the
# headers declare their real types, by which main retypes them; without those a state's pointers there move
# as bytes, and a conversion into another build keeps the writer's addresses. Clang writes no debug
# information for extern declarations; GCC does. So for an image whose compile units come from clang, the
# game C (the build's game_<module> targets, by CMake's compile commands: their defines, include folders and
# dialect) is compiled once more with the host's GCC, only to read what each declaration's type is called: a
# typedef, struct, union or enum, or a base type, by name, through pointers and arrays (their dimensions).
# Each name is then the image's own type of that name (its own compile unit's first): no layout comes from
# GCC, and the retyping's size check still guards each. The objects and what was read from them stay in the
# build (state_schema_gcc/), each compiled again only when its command, its source or a header it includes
# changes.

DECL_FORMAT = 1  # how state_schema_gcc/*.json record an object's declarations; another value reads them again
DECL_TAGS = {
    "DW_TAG_compile_unit", "DW_TAG_variable", "DW_TAG_typedef", "DW_TAG_base_type", "DW_TAG_structure_type",
    "DW_TAG_union_type", "DW_TAG_class_type", "DW_TAG_enumeration_type", "DW_TAG_pointer_type",
    "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type", "DW_TAG_atomic_type",
    "DW_TAG_array_type", "DW_TAG_subrange_type", "DW_TAG_subroutine_type",
}
NAMED_KIND = {"DW_TAG_typedef": "typedef", "DW_TAG_structure_type": "struct", "DW_TAG_class_type": "struct",
              "DW_TAG_union_type": "union", "DW_TAG_enumeration_type": "enum", "DW_TAG_base_type": "base"}
GCC_BASE = {"short int": "short", "short unsigned int": "unsigned short", "long int": "long",
            "long unsigned int": "unsigned long", "long long int": "long long",
            "long long unsigned int": "unsigned long long"}  # GCC's names of the base types, as clang names them
GCC_VALUE_FLAGS = ("-D", "-U", "-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros")
GCC_KEEP_FLAGS = {"-nostdinc", "-undef", "-ansi", "-funsigned-char", "-fsigned-char", "-fms-extensions",
                  "-fplan9-extensions"}


def host_gcc(name: str) -> tuple[str | None, str]:
    """The host's GCC (not a clang called gcc, as macOS has): its path and its version and target, or None
    and why not."""
    path = shutil.which(name)
    if path is None:
        return None, f"no GCC ({name}) on this machine"
    try:
        ver = subprocess.run([path, "--version"], capture_output=True, text=True, timeout=60).stdout
        machine = subprocess.run([path, "-dumpmachine"], capture_output=True, text=True, timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError) as e:
        return None, f"{name} does not run ({e})"
    first = ver.splitlines()[0] if ver.strip() else ""
    if "Free Software Foundation" not in ver:
        return None, f"{name} is not GCC ({first or 'no version'})"
    return path, f"{first} {machine}"


def gcc_flags(args: list[str], cwd: Path) -> list[str]:
    """What of a compile command decides which declarations the C sees: its defines, include folders and
    files, and dialect. The rest (the target, the sysroot, optimisation, warnings, code generation) is the
    compiler's or the platform's; GCC takes its own headers for anything not in the include folders (libc)."""
    out = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in GCC_VALUE_FLAGS and i + 1 < len(args):
            v = args[i + 1]
            if a not in ("-D", "-U") and (a not in ("-include", "-imacros") or (cwd / v).exists()):
                v = str(cwd / v)
            out += [a, v]
            i += 2
            continue
        if a[:2] in ("-D", "-U") or a.startswith("-std=") or a in GCC_KEEP_FLAGS:
            out.append(a)
        elif a.startswith("-I"):
            out.append("-I" + str(cwd / a[2:]))
        i += 1
    return out


def game_commands(build: Path) -> tuple[list[dict], str | None]:
    """The game C's compile commands, from CMake's compile_commands.json (port/CMakeLists.txt exports it with
    XG_STATE_SCHEMA): each source of a game_<module> target, with its folder and the flags GCC takes from it."""
    path = build / "compile_commands.json"
    if not path.exists():
        return [], "the build has no compile_commands.json (CMake: -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)"
    try:
        entries = json.loads(path.read_text())
    except ValueError as e:
        return [], f"the build's compile_commands.json is unreadable ({e})"
    out, seen = [], set()
    for e in entries:
        args = e.get("arguments") or shlex.split(e.get("command", ""))
        output = e.get("output") or next((args[i + 1] for i in range(len(args) - 1) if args[i] == "-o"), "")
        if not re.search(r"(?:^|/)CMakeFiles/game_\w+\.dir/", output.replace("\\", "/")):
            continue
        cwd = Path(e.get("directory") or build)
        src = real_path(str(cwd / e["file"]))
        if src not in seen:
            seen.add(src)
            out.append({"file": src, "dir": cwd, "flags": gcc_flags(args[1:], cwd)})
    if not out:
        return [], "the build's compile_commands.json has no game C (no game_<module> target)"
    return out, None


def describe(dies: dict, off, depth: int = 0):
    """What a type of a GCC object's DWARF is called, as JSON: ["typedef", name], ["struct" | "union" | "enum",
    name], ["base", name, encoding], ["ptr", inner] (inner None: void), ["arr", [dimension or None, ...], element],
    ["fn"];
    None for void; ["?", what] for a type without a name to find it by (an anonymous struct)."""
    if off is None:
        return None
    d = dies.get(off)
    if d is None or depth > 32:
        return ["?", "an entry not read"]
    tag, a = d["tag"], d["attrs"]
    if tag in ("DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type", "DW_TAG_atomic_type"):
        return describe(dies, a.get("DW_AT_type"), depth + 1)
    kind = NAMED_KIND.get(tag)
    if kind == "base":  # (and its encoding: the game C's char is unsigned, the port's signed)
        return ["base", a.get("DW_AT_name"), a.get("DW_AT_encoding")]
    if kind:
        return [kind, a["DW_AT_name"]] if a.get("DW_AT_name") else ["?", f"an anonymous {kind}"]
    if tag == "DW_TAG_pointer_type":
        return ["ptr", describe(dies, a.get("DW_AT_type"), depth + 1)]
    if tag == "DW_TAG_array_type":
        dims = []
        for c in d["children"]:
            s = dies[c]["attrs"]
            ub = s.get("DW_AT_upper_bound")
            dims.append(s["DW_AT_count"] if isinstance(s.get("DW_AT_count"), int) else
                        (0 if ub >= 1 << 63 else ub + 1) if isinstance(ub, int) else None)  # (no bound: T x[])
        return ["arr", dims, describe(dies, a.get("DW_AT_type"), depth + 1)]
    if tag == "DW_TAG_subroutine_type":
        return ["fn"]
    return ["?", tag]


def object_decls(obj: Path, build: Path) -> list:
    """[name, what its type is called] of each declaration in a GCC object (DW_AT_declaration: extern, at file
    scope or in a function), in the object's order."""
    dies = read_dies(obj, build, DECL_TAGS)
    return [[d["attrs"]["DW_AT_name"], describe(dies, d["attrs"]["DW_AT_type"])] for d in dies.values()
            if d["tag"] == "DW_TAG_variable" and d["attrs"].get("DW_AT_declaration") is True
            and d["attrs"].get("DW_AT_name") and d["attrs"].get("DW_AT_type") is not None]


def dep_files(text: str, cwd: Path) -> list[str]:
    """The files a GCC dependency file (-MD) names: the source and each header it included."""
    body = text.replace("\\\n", " ").replace("\\ ", "\0")
    body = body.split(":", 1)[1].split("\n", 1)[0] if ":" in body else ""
    return [str(cwd / p.replace("\0", " ")) for p in body.split()]


def gcc_declarations(build: Path, gcc: str, ident: str, entries: list[dict]) -> tuple[list, dict, list[str]]:
    """Each game C file's declarations, read with GCC (object_decls) or from the build's cache of them:
    ([(source, declarations or None)], counts, problems)."""
    import concurrent.futures, hashlib
    cache = build / "state_schema_gcc"
    digests: dict[str, str | None] = {}

    def digest(p: str):
        if p not in digests:
            try:
                digests[p] = hashlib.sha1(Path(p).read_bytes()).hexdigest()
            except OSError:
                digests[p] = None
        return digests[p]

    def one(e):
        src = e["file"]
        rel = (Path(src).relative_to(ROOT).as_posix() if Path(src).is_relative_to(ROOT) else
               "build/" + Path(src).relative_to(build).as_posix() if Path(src).is_relative_to(build) else
               "other/" + hashlib.sha1(src.encode()).hexdigest()[:16] + "-" + Path(src).name)
        base = cache / rel
        key = hashlib.sha1(json.dumps([DECL_FORMAT, ident, e["flags"], src]).encode()).hexdigest()
        side = base.with_name(base.name + ".json")
        with contextlib.suppress(OSError, ValueError, KeyError, AttributeError, TypeError):
            old = json.loads(side.read_text())
            if old["key"] == key and all(digest(p) == h for p, h in old["deps"].items()):
                return src, old["decls"], "cached", None
        obj, dep = base.with_name(base.name + ".o"), base.with_name(base.name + ".d")
        base.parent.mkdir(parents=True, exist_ok=True)
        p = subprocess.run([gcc, *e["flags"], "-fpermissive", "-w", "-g", "-O0", "-c", src, "-o", str(obj),
                            "-MD", "-MF", str(dep)], cwd=e["dir"], capture_output=True, text=True)
        if p.returncode != 0:
            err = next((ln for ln in p.stderr.splitlines() if "error" in ln), p.stderr.strip()[:300] or "failed")
            return src, None, "failed", f"{rel}: {gcc} cannot compile it: {err}"
        try:
            decls = object_decls(obj, build)
        except SystemExit as x:
            return src, None, "failed", f"{rel}: its object's debug information is unreadable ({x})"
        deps = dep_files(dep.read_text(), e["dir"])
        write_new(side, json.dumps({"key": key, "deps": {p_: digest(p_) for p_ in deps}, "decls": decls}).encode())
        return src, decls, "compiled", None

    jobs = int(os.environ.get("XG_YIELD_JOBS") or os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL") or os.cpu_count() or 4)
    with concurrent.futures.ThreadPoolExecutor(max(1, jobs)) as pool:
        done = list(pool.map(one, entries))
    counts = Counter(how for _, _, how, _ in done)
    return [(src, decls) for src, decls, _, _ in done], counts, [why for *_, why in done if why]


class ImageTypes:
    """An image's own types by name, for what GCC read the declarations' types to be called: typedefs, structs,
    unions, enums and base types (its compile unit's first, else the image's if every one of that name is laid
    out alike); a pointer or an array of one becomes a new entry of the image's (dies), as its DWARF would
    describe it."""

    def __init__(self, dies: dict, types: Types, build: Path):
        self.dies, self.types = dies, types
        self.in_cu: dict[tuple, list[int]] = defaultdict(list)  # (unit, kind, name) -> offsets
        self.anywhere: dict[tuple, list[int]] = defaultdict(list)  # (kind, name) -> offsets
        self.units: dict[str, int] = {}  # a source's real path -> its compile unit
        self.next = max(dies) + 1 if dies else 1
        self.memo, self.shapes, self.global_pick = {}, {}, {}
        self.void_targets = 0  # pointers whose target has no type of its name in the image
        cu = None
        for off, d in list(dies.items()):
            if d["tag"] == "DW_TAG_compile_unit":
                cu = off
                name = str(d["attrs"].get("DW_AT_name", ""))
                self.units.setdefault(real_path(name if name.startswith("/") else str(build / name[6:])
                                                if name.startswith("build/") else str(ROOT / name)), off)
                continue
            kind, name = NAMED_KIND.get(d["tag"]), d["attrs"].get("DW_AT_name")
            if kind and name:
                self.in_cu[(cu, kind, name)].append(off)
                self.anywhere[(kind, name)].append(off)

    def new(self, tag: str, attrs: dict, children=()) -> int:
        off = self.next
        self.next += 1
        self.dies[off] = {"tag": tag, "attrs": attrs, "children": list(children), "parent": None}
        for c in children:
            self.dies[c]["parent"] = off
        return off

    def shape(self, off):
        """What two types of one name must agree on to be the same: kind, name, size, members and their offsets."""
        if off not in self.shapes:
            o = self.types.strip(off)
            d = self.dies.get(o, {"tag": None, "attrs": {}, "children": []})
            a = d["attrs"]
            members = tuple((m["attrs"].get("DW_AT_name"), m["attrs"].get("DW_AT_data_member_location"),
                             m["attrs"].get("DW_AT_bit_size"), m["attrs"].get("DW_AT_data_bit_offset"),
                             self.types.name(m["attrs"].get("DW_AT_type")))
                            for m in (self.dies[c] for c in d["children"]) if m["tag"] == "DW_TAG_member")
            self.shapes[off] = (d["tag"], a.get("DW_AT_name"), self.types.size(o), a.get("DW_AT_encoding"),
                                self.types.name(a.get("DW_AT_type")), members)
        return self.shapes[off]

    def named(self, cu, kind: str, name: str, complete: bool, encoding=None):
        """(offset, None) of the image's type of that kind and name, complete (a size) when asked, a base type of
        that encoding; or (None, why)."""
        ok = (lambda o: self.types.size(o) > 0) if complete else (lambda o: True)
        if encoding is not None:
            ok = lambda o: self.dies[o]["attrs"].get("DW_AT_encoding") == encoding  # noqa: E731
        own = next((o for o in self.in_cu.get((cu, kind, name), ()) if ok(o)), None)
        if own is not None:
            return own, None
        key = (kind, name, complete, encoding)
        if key not in self.global_pick:
            every = [o for o in self.anywhere.get((kind, name), ()) if ok(o)]
            if not every:
                self.global_pick[key] = None, f"no {kind} {name} in its debug information"
            elif len({self.shape(o) for o in every}) > 1:
                self.global_pick[key] = None, f"{kind} {name}: laid out otherwise in another file"
            else:
                self.global_pick[key] = every[0], None
        return self.global_pick[key]

    def resolve(self, what, cu, complete: bool = True):
        """The image's type for what GCC called a declaration's type (describe): (offset, None) or (None, why)."""
        key = (cu, json.dumps(what), complete)
        if key not in self.memo:
            self.memo[key] = self._resolve(what, cu, complete)
        return self.memo[key]

    def _resolve(self, what, cu, complete):
        if what is None:
            return None, "void"
        k = what[0]
        if k in ("typedef", "struct", "union", "enum"):
            return self.named(cu, k, what[1], complete)
        if k == "base":
            return self.named(cu, "base", GCC_BASE.get(what[1], what[1]), True, what[2])
        if k == "fn":
            return self.new("DW_TAG_subroutine_type", {}), None
        if k == "ptr":
            # a pointer still, to void when the image has no type of that name: a state moves it by the address
            # it holds (its target's type types only the port's own parts' arrays: stateconvert.c pointedStruct)
            to, why = (None, None) if what[1] is None else self.resolve(what[1], cu, False)
            if why:
                self.void_targets += 1
            return self.new("DW_TAG_pointer_type", {} if to is None else {"DW_AT_type": to}), None
        if k == "arr":
            of, why = self.resolve(what[2], cu, True)
            if why:
                return None, why
            dims = [self.new("DW_TAG_subrange_type", {} if n is None else {"DW_AT_count": n}) for n in what[1]]
            return self.new("DW_TAG_array_type", {"DW_AT_type": of}, dims), None
        return None, f"{what[1]}, which has no name to find it by"


def plain(what) -> bool:
    """A declared type that holds no pointer, whatever its layout: a base type, or an array of them."""
    return what is not None and (what[0] == "base" or (what[0] == "arr" and plain(what[2])))


def clang_declared(dies: dict, types: Types, build: Path, gcc_name: str, report: list[str]):
    """main's declared (name -> the image's types of its declarations, in the image's order of compile units)
    for a clang image, from GCC's reading of the game C, and why each name declared by a type the image lacks
    (one that may hold a pointer: not plain) is so: (declared, unresolved, None), or (None, {}, why not)."""
    import time
    t0 = time.monotonic()
    gcc, ident = host_gcc(gcc_name)
    if gcc is None:
        return None, {}, ident
    entries, why = game_commands(build)
    if why:
        return None, {}, why
    image = ImageTypes(dies, types, build)
    order = {p: i for i, p in enumerate(image.units)}
    entries.sort(key=lambda e: order.get(e["file"], len(order)))
    files, counts, problems = gcc_declarations(build, gcc, ident, entries)
    for p_ in problems:
        print(f"state_schema: {p_}", file=sys.stderr)
    if problems:
        return None, {}, f"{len(problems)} of {len(entries)} game C files could not be read with {gcc_name} " \
                         f"({'; '.join(problems[:2])})"
    declared = defaultdict(list)
    unresolved = Counter()
    names: dict[str, str] = {}
    n = 0
    for src, decls in files:
        cu = image.units.get(src)
        for name, what in decls:
            n += 1
            off, why = image.resolve(what, cu)
            if off is None:
                unresolved[why] += 1
                if not plain(what):
                    names.setdefault(name, why)
            else:
                declared[name].append(off)
    report.append(f"  declarations read with {ident}: {len(files)} game C files ({counts['compiled']} compiled, "
                  f"{counts['cached']} cached) in {time.monotonic() - t0:.1f} s; {n} declarations, "
                  f"{n - sum(unresolved.values())} found in the image's own types, {sum(unresolved.values())} not; "
                  f"{image.void_targets} pointer types to a type the image lacks, kept as pointers to void")
    for why, k in unresolved.most_common(12):
        report.append(f"    {k}: {why}")
    return declared, names, None


# ---- the image: ELF (readelf, nm) or PE/COFF (its headers, nm) --------------------------------------

@functools.lru_cache(maxsize=None)
def real_path(name: str) -> str:
    return os.path.realpath(name)


def is_pe(exe: Path) -> bool:
    with exe.open("rb") as f:
        return f.read(2) == b"MZ"


class PeImage:
    """A PE32+ image's headers (Microsoft's PE/COFF specification): the preferred image base and the
    section table, each section's name resolved (a long name, "/n", is an offset into the COFF string
    table after the symbols: GNU ld writes long names when the image has debug information)."""
    DISCARDABLE, CODE, INITIALIZED, EXECUTE, READ = 0x02000000, 0x00000020, 0x00000040, 0x20000000, 0x40000000

    def __init__(self, exe: Path):
        self.data = data = exe.read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
            raise SystemExit(f"state_schema: {exe}: not a PE image")
        nsec, _, symptr, nsym, optsize = struct.unpack_from("<HIIIH", data, pe + 6)
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x20B:
            raise SystemExit(f"state_schema: {exe}: not a PE32+ (64-bit) image")
        self.base = struct.unpack_from("<Q", data, opt + 24)[0]
        self.file_alignment = struct.unpack_from("<I", data, opt + 36)[0]
        self.table = opt + optsize  # the section table, 40 bytes a section
        strtab = symptr + nsym * 18 if symptr else None
        self.sections = []
        for i in range(nsec):
            raw, vsize, rva, rawsize, rawptr = struct.unpack_from("<8sIIII", data, opt + optsize + 40 * i)
            flags = struct.unpack_from("<I", data, opt + optsize + 40 * i + 36)[0]
            name = raw.rstrip(b"\0")
            if name.startswith(b"/") and strtab is not None and name[1:].isdigit():
                at = strtab + int(name[1:])
                name = data[at:data.index(b"\0", at)]
            self.sections.append({"name": name.decode(errors="replace"), "rva": rva, "lo": self.base + rva,
                                  "vsize": vsize, "rawsize": rawsize, "rawptr": rawptr, "flags": flags})

    def file_bytes(self, sec) -> bytes:
        """A section as loaded: its raw data, zeros past it up to its virtual size."""
        n = min(sec["vsize"], sec["rawsize"])
        return self.data[sec["rawptr"]:sec["rawptr"] + n] + bytes(sec["vsize"] - n)


def has_debug_info(exe: Path) -> bool:
    if is_pe(exe):
        return any(sec["name"] == ".debug_info" for sec in PeImage(exe).sections)
    return ".debug_info" in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout


def sections(exe: Path) -> dict[str, tuple[int, int]]:
    if is_pe(exe):
        img = PeImage(exe)
        return {sec["name"]: (sec["lo"], sec["lo"] + sec["vsize"]) for sec in img.sections}
    out = {}
    for line in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m and int(m.group(2), 16):
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(2), 16) + int(m.group(3), 16))
    return out


def nm_tool(exe: Path) -> list[str]:
    """An nm that reads this image: the system's (binutils for every target), else MinGW's or LLVM's."""
    for tool in ("nm", "x86_64-w64-mingw32-nm", "llvm-nm"):
        if shutil.which(tool) and subprocess.run([tool, "--defined-only", str(exe)], capture_output=True).returncode == 0:
            return [tool]
    raise SystemExit(f"state_schema: no nm reads {exe}")


def symbols(exe: Path) -> list[list]:
    """Every defined symbol, [name, address, size, nm kind]: a state names functions and constants by them,
    and a build converting another build's state has only that build's schema (no executable). COFF symbols
    have no sizes (0 here; the variables' from DWARF are filled in by main)."""
    rows = []
    for line in subprocess.run(nm_tool(exe) + ["-n", "-S", "--defined-only", str(exe)], capture_output=True,
                               text=True).stdout.splitlines():
        m = re.match(r"([0-9a-f]+) (?:([0-9a-f]+) )?(\S) (\S+)$", line)
        if m and m.group(3) != "N":  # (COFF: symbols of the debug sections, which stripping removes)
            rows.append([m.group(4), int(m.group(1), 16), int(m.group(2) or "0", 16), m.group(3)])
    return rows


def read_only(exe: Path) -> list[list[int]]:
    """The file-backed data that does not change: [start, end, file offset] of .rodata and the writable
    .data sections that hold the game's string tables (allocated PROGBITS, not code). PE: the sections of
    initialised data that stay loaded (not code, not discardable: no debug sections, no .reloc), each to
    the end of its raw data."""
    out = []
    if is_pe(exe):
        img = PeImage(exe)
        for sec in img.sections:
            f = sec["flags"]
            if f & PeImage.INITIALIZED and not f & (PeImage.CODE | PeImage.EXECUTE | PeImage.DISCARDABLE) and \
                    sec["rawsize"] and sec["vsize"]:
                out.append([sec["lo"], sec["lo"] + min(sec["vsize"], sec["rawsize"]), sec["rawptr"]])
        return out
    for line in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)", line)
        if m and m.group(2) == "PROGBITS" and "A" in m.group(6) and "X" not in m.group(6) and int(m.group(3), 16):
            lo = int(m.group(3), 16)
            out.append([lo, lo + int(m.group(5), 16), int(m.group(4), 16)])
    return out


def stripped_read_only(exe: Path) -> list[list[int]]:
    """read_only as the image has it once stripped of its debug information (what a package ships, and the file
    a shipped schema's "exe" names): the image's own without any. ELF: the same (stripping keeps the file offsets of
    what loads). PE: the section table loses the debug sections, the headers end where it then ends (to the file
    alignment) and the raw data that stays follows in its order, each section to the file alignment: binutils'
    strip (--strip-debug, --strip-all) and llvm-strip lay a MinGW executable out so, 0x200 bytes earlier than
    before; --verify checks a shipped image has them."""
    ro = read_only(exe)
    if not is_pe(exe) or not debug_sections(exe):
        return ro
    img = PeImage(exe)
    align = lambda n: -(-n // img.file_alignment) * img.file_alignment  # noqa: E731
    kept = [sec for sec in img.sections if not pe_debug(sec["name"])]
    at, moved = align(img.table + 40 * len(kept)), {}
    for sec in sorted((sec for sec in kept if sec["rawsize"]), key=lambda sec: sec["rawptr"]):
        moved[sec["rawptr"]] = at
        at += align(sec["rawsize"])
    return [[lo, hi, moved[fo]] for lo, hi, fo in ro]


ID_SEEDS = (0x243F6A8885A308D3, 0x13198A2E03707344)  # statefile.c stateBuildId's


def id_mix(ab: list[int], data: bytes) -> None:
    """statefile.c idMix: both halves of the id, the second with another seed."""
    ab[0] = state_hash(data, ab[0])
    ab[1] = state_hash(data, ab[1] ^ 0x5851F42D4C957F2D)


def elf_identity(exe: Path) -> bytes:
    """statefile.c computeImageId for ELF: every allocated section's name, address, size, type and flags in
    the section table's order, and the code (executable PROGBITS) itself; the build id of the image at its
    link addresses (no load bias), from the file. Stripping keeps all of it."""
    data = exe.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"state_schema: {exe}: not a 64-bit little-endian ELF")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    if shentsize != 64 or not shnum or shstrndx >= shnum:
        raise SystemExit(f"state_schema: {exe}: no section table")
    heads = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + 64 * i) for i in range(shnum)]
    strs = heads[shstrndx]
    names = data[strs[4]:strs[4] + strs[5]] + b"\0"
    ab = list(ID_SEEDS)
    for name, typ, flags, addr, off, size, *_ in heads:
        if not flags & 0x2:  # SHF_ALLOC
            continue
        text = names[name:names.index(b"\0", name)] if name < len(names) - 1 else b""
        id_mix(ab, text)
        id_mix(ab, struct.pack("<4Q", addr, size, typ, flags))
        if flags & 0x4 and typ == 1:  # SHF_EXECINSTR, SHT_PROGBITS
            id_mix(ab, data[off:off + size])
    return ab[0].to_bytes(8, "little") + ab[1].to_bytes(8, "little")


def pe_identity(exe: Path) -> bytes:
    """statefile.c computeImageId for PE: the preferred image base, then each section that stays loaded (not
    discardable: debug sections and .reloc left out) by its address, size and flags, and the code."""
    img = PeImage(exe)
    ab = list(ID_SEEDS)
    id_mix(ab, struct.pack("<Q", img.base))
    for sec in img.sections:
        if sec["flags"] & PeImage.DISCARDABLE:
            continue
        id_mix(ab, struct.pack("<3I", sec["rva"], sec["vsize"], sec["flags"]))
        if sec["flags"] & PeImage.CODE:
            id_mix(ab, img.file_bytes(sec))
    return ab[0].to_bytes(8, "little") + ab[1].to_bytes(8, "little")


def image_identity(exe: Path) -> dict:
    """The schema's "image": the format and the identity the game computes from its running image (statefile.h
    stateImageIdText): its hex."""
    pe = is_pe(exe)
    return {"format": "pe" if pe else "elf", "id": (pe_identity(exe) if pe else elf_identity(exe)).hex()}


def build_image(build: Path) -> Path | None:
    """The image a build's schema describes: its executable (xenogears or xenogears-enhanced, .exe on
    Windows) or, for a shared-library build, the game library its launcher loads (lib<name>.so)."""
    for name in ("xenogears", "xenogears-enhanced"):
        for exe in (build / name, build / f"{name}.exe"):
            if exe.exists():
                return (sc.game_library(exe) if exe.suffix != ".exe" else None) or exe
    return None


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


def pe_debug(name: str) -> bool:
    """A PE section of debug information: .debug_*, .zdebug_*, or a long name left without its string table."""
    return name.startswith((".debug", ".zdebug")) or re.fullmatch(r"/\d+", name) is not None


def debug_sections(exe: Path) -> list[str]:
    """The image's sections of debug information (.debug_*, .zdebug_*; in a PE, also a long name left without
    its string table)."""
    if is_pe(exe):
        return [sec["name"] for sec in PeImage(exe).sections if pe_debug(sec["name"])]
    data = exe.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"state_schema: {exe}: not a 64-bit little-endian ELF")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    if not shnum or shstrndx >= shnum:
        return []
    heads = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + 64 * i) for i in range(shnum)]
    strs = heads[shstrndx]
    table = data[strs[4]:strs[4] + strs[5]] + b"\0"
    names = [table[h[0]:table.index(b"\0", h[0])].decode(errors="replace") for h in heads]
    return [n for n in names if n.startswith((".debug", ".zdebug"))]


ABSOLUTE = re.compile(r"^(?:/|\\|[A-Za-z]:[\\/])|/home/|/Users/|\\Users\\")


def strings_of(value, out: list[str]) -> list[str]:
    """Every string in a JSON value, keys too."""
    if isinstance(value, str):
        out.append(value)
    elif isinstance(value, dict):
        for k, v in value.items():
            out.append(k)
            strings_of(v, out)
    elif isinstance(value, list):
        for v in value:
            strings_of(v, out)
    return out


def verify(shipped: Path, image: Path, stripped: bool, portable: bool = False) -> int:
    """--verify: a schema to hand out against the image it ships with; --portable: one whose states convert."""
    problems = []
    data = shipped.read_bytes()
    try:
        if data[:8] != sc.SCHEMA_MAGIC:
            raise ValueError("not the packed form (XGSCHEMA)")
        schema = json.loads(sc.schema_text(data))
    except ValueError as e:
        print(f"state_schema: {shipped}: {e}", file=sys.stderr)
        return 1
    want = image_identity(image)
    if schema.get("image") != want:
        problems.append(f"it was read from another image ({schema.get('image')}; {image.name} is {want})")
    if schema.get("exe") != image.name:
        problems.append(f"its exe is {schema.get('exe')!r}, not the image's name {image.name!r}")
    if schema.get("ro") != stripped_read_only(image):  # (where statecanon.py reads a string from "exe")
        problems.append(f"its read-only ranges' file offsets are not those of {image.name} stripped")
    paths = sorted({s for s in strings_of(schema, []) if ABSOLUTE.search(s)})
    if paths:
        problems.append(f"{len(paths)} absolute paths in it: {', '.join(paths[:3])}")
    if stripped and (left := debug_sections(image)):
        problems.append(f"{image.name} still has debug information ({', '.join(left[:4])})")
    if portable and schema.get("portable") is False:
        problems.append(f"its states cannot convert to other builds: {schema.get('portable_why', '(no reason given)')}")
    for p_ in problems:
        print(f"state_schema: {shipped.name}: {p_}", file=sys.stderr)
    if not problems:
        print(f"state-schema: {shipped.name} ({len(data)} bytes, {len(sc.schema_text(data))} as JSON) describes "
              f"{image.name} (image {want['format']} {want['id']}){', stripped' if stripped else ''}")
    return 1 if problems else 0


def verify_apk(apk: Path, portable: bool = False) -> int:
    """--verify-apk: each game library of an APK with its schema in the APK's assets."""
    import tempfile, zipfile
    with zipfile.ZipFile(apk) as z, tempfile.TemporaryDirectory() as tmp:
        names = set(z.namelist())
        libs = sorted(n for n in names if re.fullmatch(r"lib/[^/]+/libxenogears[^/]*\.so", n))
        want = {f"assets/schemas/{n.split('/')[1]}/{Path(n).stem}.xgschema": n for n in libs}
        problems = sum(1 for n in sorted(names) if n.startswith("assets/schemas/") and not n.endswith("/")
                       and n not in want)
        for n in sorted(names):
            if n.startswith("assets/schemas/") and not n.endswith("/") and n not in want:
                print(f"state_schema: {apk.name}: {n} is no game library's", file=sys.stderr)
        if not libs:
            print(f"state_schema: {apk.name}: no game library (lib/<abi>/libxenogears*.so)", file=sys.stderr)
            problems += 1
        for asset, lib in want.items():
            if asset not in names:
                print(f"state_schema: {apk.name}: {lib} has no state schema ({asset})", file=sys.stderr)
                problems += 1
                continue
            folder = Path(tmp) / lib.split("/")[1]
            folder.mkdir(exist_ok=True)
            image, packed = folder / Path(lib).name, folder / Path(asset).name
            image.write_bytes(z.read(lib))
            packed.write_bytes(z.read(asset))
            problems += verify(packed, image, True, portable)
    if not problems:
        print(f"state-schema: {apk.name}: {len(want)} game libraries, each with its schema")
    return 1 if problems else 0


def write_new(path: Path, data: bytes) -> None:
    """Replace a file as a whole (a reader never sees half of it)."""
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)


def main() -> int:
    global DWARFDUMP
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("build", type=Path, nargs="?")
    ap.add_argument("--json", type=Path, help="write the schema here (default BUILD/state_schema.json.gz)")
    ap.add_argument("--ship", type=Path, help="also write the schema to hand out here (packed, no path of this machine)")
    ap.add_argument("--image", type=Path, help="the image to read (default: the build's executable, or the game "
                    "library of a shared-library build)")
    ap.add_argument("--dwarfdump", help="the llvm-dwarfdump to read the DWARF with (default: the system's or the NDK's)")
    ap.add_argument("--gcc", default="gcc", help="for an image compiled by clang: the host's GCC, which reads the "
                    "game C's declarations (default: gcc)")
    ap.add_argument("--quiet", action="store_true", help="no coverage report: the schema's line and problems only")
    ap.add_argument("--verify", type=Path, metavar="SHIPPED", help="check a schema to hand out against --image")
    ap.add_argument("--stripped", action="store_true", help="with --verify: the image must have no debug information")
    ap.add_argument("--portable", action="store_true", help="with --verify or --verify-apk: the schema must let its "
                    "states convert to other builds (a clang build's needs the host's GCC: portable_why)")
    ap.add_argument("--verify-apk", type=Path, metavar="APK", help="check each game library of an Android APK with "
                    "its schema in the APK's assets")
    ap.add_argument("--top", type=int, default=12, help="examples listed per finding")
    ap.add_argument("--source", type=Path, action="append",
                    help="read the heap annotations (include/pc_state.h) from this folder instead of the build's game C "
                         "and port/src (tests)")
    args = ap.parse_args()
    if args.verify_apk:
        if args.build is not None or args.verify or args.image:
            ap.error("--verify-apk takes nothing else")
        return verify_apk(args.verify_apk, args.portable)
    if args.verify:
        if args.image is None or args.build is not None:
            ap.error("--verify takes --image and no build")
        return verify(args.verify, args.image, args.stripped, args.portable)
    if args.build is None:
        ap.error("a build folder is needed")
    DWARFDUMP = args.dwarfdump
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text() if (build / "CMakeCache.txt").exists() else ""
    version = (re.search(r"^XG_VERSION:\w+=(\w+)", cache, re.M) or [None, "US"])[1].lower()
    exe = args.image.resolve() if args.image else build_image(build)
    if exe is None or not exe.is_file():
        print(f"state_schema: no executable in {build}", file=sys.stderr)
        return 1
    if not has_debug_info(exe):
        print(f"state_schema: {exe} has no debug information (configure with -DCMAKE_BUILD_TYPE=RelWithDebInfo, "
              "or Release with -DXG_STATE_SCHEMA=ON)", file=sys.stderr)
        return 1
    report = contextlib.ExitStack()  # --quiet: the coverage report goes nowhere
    if args.quiet:
        report.enter_context(contextlib.redirect_stdout(io.StringIO()))

    secs = sections(exe)
    modsec = []  # (lo, hi, module, kind)
    for n, (lo, hi) in secs.items():
        m = re.match(r"xg(data|bss)_(\w+)$", n)
        if m:
            modsec.append((lo, hi, m.group(2), m.group(1)))
    modsec.sort()
    if not modsec:
        print(f"state_schema: {exe} has no xgdata_/xgbss_ sections (a PE image without long section names?)",
              file=sys.stderr)
        return 1

    def module_of(addr):
        for lo, hi, mod, kind in modsec:
            if lo <= addr < hi:
                return mod, kind
        return None, None

    dies = read_dies(exe, build)
    # Clang writes no debug information for extern declarations (GCC does), and the generated sources'
    # data (gen/: u8 arrays) take their real types from them: without them a state's pointers there move
    # as bytes, and a conversion into another build crashes it (xg-htn.60.8). The host's GCC reads them
    # (clang_declared); when it cannot, the schema keeps slots in their own build (stateportable.c:
    # portable false).
    clang = sorted({str(d["attrs"].get("DW_AT_producer", "")).split(" version")[0] for d in dies.values()
                    if d["tag"] == "DW_TAG_compile_unit" and "clang" in str(d["attrs"].get("DW_AT_producer", ""))})
    types = Types(dies)
    portable_why = None
    decl_report: list[str] = []
    gcc_declared, gcc_unresolved = None, {}
    if clang:
        gcc_declared, gcc_unresolved, why = clang_declared(dies, types, build, args.gcc, decl_report)
        if why:
            portable_why = (f"compiled by {clang[0]}, which writes no debug information for extern declarations, "
                            f"and {why}: the generated data's types are unknown (xg-htn.60.8)")
            print(f"state_schema: warning: this build's slots stay in it: {portable_why}", file=sys.stderr)
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

    def is_const(off):
        """Whether an object of this type is constant: const itself, or an array of const elements (a
        position-independent build's tables of pointers: .data.rel.ro, which the module link puts in the
        module's data section, where a fixed-address build has them in .rodata)."""
        for _ in range(16):
            d = dies.get(off)
            if d is None:
                return False
            if d["tag"] == "DW_TAG_const_type":
                return True
            if d["tag"] not in ("DW_TAG_typedef", "DW_TAG_volatile_type", "DW_TAG_array_type"):
                return False
            off = d["attrs"].get("DW_AT_type")
        return False

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
                          "retail": retail_of(mod, name) if mod else None, "const": is_const(a.get("DW_AT_type"))})

    # The generated sources define the asm-defined data as byte arrays (gen/: u8 gameState[0x2358]); the
    # headers declare their real types. A declaration's type of the same size is the one to encode by.
    # (A clang image's: GCC's reading of the game C, as the image's own types: clang_declared.)
    declared = gcc_declared if gcc_declared is not None else defaultdict(list)
    for off, d in dies.items() if gcc_declared is None else ():
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

    def meaningful(off):
        """A declared type that says more than the definition's integer: a pointer, or uptr/sptr/OTag."""
        d = dies.get(off)
        while d is not None and d["tag"] in ("DW_TAG_const_type", "DW_TAG_volatile_type"):
            off = d["attrs"].get("DW_AT_type"); d = dies.get(off)
        if d is None:
            return False
        if d["tag"] == "DW_TAG_typedef" and types.name(off) in ("uptr", "sptr", "OTag"):
            return True
        return dies.get(types.strip(off), {}).get("tag") == "DW_TAG_pointer_type"

    retyped = 0
    for v in variables:
        t = v["defined"] = v["type"]
        st = dies.get(types.strip(t)) if t is not None else None
        if st is not None and st["tag"] == "DW_TAG_base_type":
            for cand in declared.get(v["name"], []):
                if meaningful(cand) and types.size(cand) == v["size"]:
                    t = cand
                    retyped += 1
                    break
        if t is not None and is_bytes(t):
            for cand in declared.get(v["name"], []):
                if not is_bytes(cand) and types.size(cand) == v["size"]:
                    t = cand
                    retyped += 1
                    break
                elem = open_array_elem(cand)
                es = types.size(elem) if elem is not None else 0
                scalar = dies.get(types.strip(elem), {}).get("tag") == "DW_TAG_base_type" if elem is not None else True
                if es and v["size"] % es == 0 and not scalar:  # extern T x[] of a struct or pointer type
                    v["elem"], v["count"] = elem, v["size"] // es  # completed below, in the schema's table
                    retyped += 1
                    break
        v["type"] = t
    # A clang image's generated data left as bytes although the game C declares it, by a type the image's own
    # debug information lacks (one clang left out, an anonymous struct): its pointers would move as bytes
    left = sorted({v["name"] for v in variables if v["module"] and v["name"] in gcc_unresolved and
                   v["type"] == v["defined"] and v.get("elem") is None and v["type"] is not None and
                   (is_bytes(v["type"]) or dies.get(types.strip(v["type"]), {}).get("tag") == "DW_TAG_base_type")})
    if left and portable_why is None:
        portable_why = (f"compiled by {clang[0]}, which writes no debug information for extern declarations, and "
                        f"{len(left)} generated variables are declared by types not in its own "
                        f"({'; '.join(f'{n}: {gcc_unresolved[n]}' for n in left[:3])}) (xg-htn.60.8)")
        print(f"state_schema: warning: this build's slots stay in it: {portable_why}", file=sys.stderr)
    retyped_game = sum(1 for v in variables if v["module"] and (v["type"] != v["defined"] or v.get("elem") is not None))

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
            if name and a["DW_AT_low_pc"] not in (0, (1 << 64) - 1):  # (0, -1: a discarded function's tombstone)
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
    for line in decl_report:
        print(line)
    print(f"  generated data retyped by its declarations: {retyped}, {retyped_game} in game modules"
          + (" (clang: by GCC's reading)" if clang else ""))
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
    complete = {}  # struct/union name -> the DIE of a complete definition (a member may name a declaration)
    for off_, d_ in dies.items():
        if d_["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and d_["children"] and \
                "DW_AT_declaration" not in d_["attrs"] and d_["attrs"].get("DW_AT_name"):
            complete.setdefault((d_["tag"], d_["attrs"]["DW_AT_name"]), off_)

    def tid(off):
        """A type's id in the schema (its DIE offset), described in `table` once; typedefs and qualifiers
        are looked through, a struct keeps its typedef's name for messages."""
        shown = types.name(off)
        if off in dies and dies[off]["tag"] == "DW_TAG_typedef" and shown in ("OTag", "uptr", "sptr", "scratch64"):
            # typedefs with a meaning for a state (include/common.h, include/psyq.h): an ordering table's link
            # (the length in the top byte, an address below it), an integer that holds an address (uptr,
            # sptr), a work area's word that no state carries (scratch64)
            kind = {"OTag": "otag", "uptr": "addr", "sptr": "addr", "scratch64": "scratch"}[shown]
            table.setdefault(shown, {"k": kind, "size": types.size(off), "name": shown})
            return shown
        off = types.strip(off)
        if off is None or off not in dies:
            return None
        d0 = dies[off]
        if d0["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and \
                ("DW_AT_declaration" in d0["attrs"] or not d0["children"]):
            off = complete.get((d0["tag"], d0["attrs"].get("DW_AT_name")), off)
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
            for depth, n in enumerate(reversed(dims[1:])):  # int a[2][3]: an array of 2 arrays of 3
                inner = f"{key}.d{depth}"
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

    # Every named struct and union of this repository's files, reachable from a static or not: the heap's
    # blocks are typed by name (the allocation site's type; P2)
    named_types = {}
    for off, d in dies.items():
        if d["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and d["children"] and \
                "DW_AT_declaration" not in d["attrs"]:
            n = d["attrs"].get("DW_AT_name")
            if n:
                named_types.setdefault(n, off)
        elif d["tag"] == "DW_TAG_typedef":
            n = d["attrs"].get("DW_AT_name"); t = types.strip(d["attrs"].get("DW_AT_type"))
            if n and t in dies and dies[t]["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and dies[t]["children"]:
                named_types.setdefault(n, d["attrs"]["DW_AT_type"])
    type_names = {n: tid(off) for n, off in named_types.items()}

    # ---- what the heap's blocks hold (include/pc_state.h), from the source this build compiles ---------
    engine = (re.search(r"^XG_ENGINE:\w+=(\w+)", cache, re.M) or [None, "original"])[1]
    game_root = ROOT / "engine" if engine == "enhanced" else ROOT
    heap_names, counted_src, unions_src, rules_src, revisions, unknown_macros = state_annotations(
        [r.resolve() for r in args.source] if args.source else
        [game_root / "src", game_root / "include", ROOT / "port" / "src"])
    any_named = dict(named_types)  # typedefs and base types too (u8, OTag, uptr)
    for off, d in dies.items():
        if d["tag"] in ("DW_TAG_typedef", "DW_TAG_base_type"):
            n = d["attrs"].get("DW_AT_name")
            if n and n not in any_named:
                any_named[n] = off
    heap_types = {}; heap_problems = []
    for name, wheres in sorted(heap_names.items()):
        bare = re.sub(r"\s+", "", name)
        one = bare.endswith("[1]")
        if one:
            bare = bare[:-3]
        if bare == "PC_PRIMS":
            spec = "prims"
        elif bare.endswith("*"):
            spec = "ptrs"
        elif bare.removeprefix("struct").removeprefix("union") in any_named:
            t = tid(any_named[bare.removeprefix("struct").removeprefix("union")])
            k = table.get(t, {}).get("k")
            spec = {"tid": t} if k in ("struct", "union") else "otags" if k == "otag" else \
                "ptrs" if k in ("addr", "ptr") else None  # scalars: bytes, the same in every build
            if one and isinstance(spec, dict):
                spec["one"] = True
        else:
            heap_problems.append(f"{name} ({wheres[0]}): no such type in this build")
            continue
        h = f"{type_id(name):08x}"
        bare = bare + "[1]" if one else bare
        if h in heap_types and heap_types[h]["name"] != bare:
            heap_problems.append(f"{name} and {heap_types[h]['name']}: one hash ({h})")
        heap_types[h] = {"name": bare, "spec": spec}
    counted = {}
    for r in counted_src:
        t = tid(any_named[r["type"]]) if r["type"] in any_named else None
        fields = {f["name"]: f for f in table.get(t, {}).get("fields", [])} if t else {}
        if r["array"] not in fields or (r["count"] is not None and r["count"] not in fields):
            heap_problems.append(f"PC_STATE_COUNTED({r['type']}, {r['array']}, {r['count']}) ({r['at']}): no such fields")
            continue
        counted[t] = {"name": r["type"], "array": r["array"], "count": r["count"], "extra": r["extra"]}
    union_rules = {}
    for r in unions_src:
        t = tid(any_named[r["type"]]) if r["type"] in any_named else None
        fields = {f["name"]: f for f in table.get(t, {}).get("fields", [])} if t else {}
        m = fields.get(r["member"])
        views = {f["name"] for f in table.get(m["type"], {}).get("fields", [])} if m else set()
        if m is None or r["selector"] not in fields or not set(r["views"]) <= views:
            heap_problems.append(f"PC_STATE_UNION({r['type']}, {r['member']}, {r['selector']}) ({r['at']}): no such fields")
            continue
        union_rules[t] = {"name": r["type"], "member": r["member"], "selector": r["selector"], "views": r["views"]}

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

    syms = symbols(exe)
    if is_pe(exe):  # COFF symbols have no sizes: a variable's from DWARF, where name and address agree
        sized = {}
        for v in variables:
            sized.setdefault((v["name"], v["addr"]), v["size"])
        for row in syms:
            if not row[2]:
                row[2] = sized.get((re.sub(r"\.lto_priv\.\d+$", "", row[0]), row[1]), 0)
    # ---- the state rules (include/pc_state.h; docs/CANONICAL_STATES.md 17.2) ---------------------------------
    # each struct's schema entries by its names (its own, its typedefs'), one entry per compile unit
    struct_keys = defaultdict(set)
    for key, t in table.items():
        if t.get("k") in ("struct", "union"):
            if t.get("name") and "<anon>" not in t["name"]:
                struct_keys[t["name"]].add(key)
            if re.fullmatch(r"[0-9a-f]+", key) and dies.get(int(key, 16), {}).get("attrs", {}).get("DW_AT_name"):
                struct_keys[dies[int(key, 16)]["attrs"]["DW_AT_name"]].add(key)
    for off, d in dies.items():
        if d["tag"] == "DW_TAG_typedef" and d["attrs"].get("DW_AT_name"):
            t = types.strip(d["attrs"].get("DW_AT_type"))
            td = dies.get(t)
            if td is None or td["tag"] not in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                continue
            if "DW_AT_declaration" in td["attrs"] or not td["children"]:
                t = complete.get((td["tag"], td["attrs"].get("DW_AT_name")), t)
            if f"{t:x}" in table:
                struct_keys[d["attrs"]["DW_AT_name"]].add(f"{t:x}")
    fn_names = {f["name"] for f in funcs} | {r[0] for r in syms if r[3] in "tTwW"}
    rules, rule_problems = state_rules(rules_src, revisions, table,
                                       [v for v in variables if (v["module"] or v["platform"]) and v.get("tid")],
                                       part_files, type_names, struct_keys, fn_names)
    heap_problems += unknown_macros + rule_problems

    schema = {"version": version, "exe": str(exe), "image": image_identity(exe), "variables": [
        # "const": the build's own constant (no state carries it: a reader keeps its own bytes)
        {**{k: v.get(k) for k in ("name", "addr", "size", "module", "section", "retail", "platform", "tid")},
         **({"const": True} if v["const"] else {})}
        for v in variables if v["module"] or v["platform"]],
        "types": table, "type_names": type_names, "parts": part_files, "retyped": retyped,
        # where a raw state's memory chunks sit in this build (statecanon.py reads them by these)
        "sections": {f"{mod}.{kind}": [lo, hi] for lo, hi, mod, kind in modsec},
        "regions": {v["name"]: [v["addr"], v["addr"] + v["size"]] for v in variables
                    if v["name"] in ("heapArena", "gameStack", "pcScratchpad")},
        "functions": funcs, "allocation_sites": site_list,
        # PC_HEAP_OF and PC_STATE_AT's types by the hash a block's header (HeapBlock mC_pcType) or a range keeps
        "heap_types": heap_types, "counted": counted,
        # a union member's live view by another member's value (PC_STATE_UNION): struct tid -> rule
        "unions": union_rules,
        # how states convert across changes of saved data (include/pc_state.h's rules): the language, this source's
        # state revision, the rules (docs/CANONICAL_STATES.md 17.2)
        "rules": rules,
        # whether another build may convert this one's states (False: clang without GCC's declarations: portable_why)
        "portable": portable_why is None, **({"portable_why": portable_why} if portable_why else {}),
        # what statecanon.py once read from the executable: symbol names and addresses, constant data's ranges
        "symbols": syms, "ro": read_only(exe)}
    report.close()
    text = json.dumps(schema).encode()  # (json.dumps: the C encoder; the same bytes as json.dump)
    write_new(out, gzip.compress(text, mtime=0))
    # the same, plain, for the game's own reader (port/src/system/stateschema.c: the port has no inflate)
    plain = out.with_name(out.name.removesuffix(".gz")) if out.name.endswith(".gz") else out.with_name(out.name + ".plain")
    write_new(plain, text)
    shipped = ""
    if args.ship:
        # to hand out: the image named by its file only, and as stripped, where the read-only ranges' bytes are then
        # (statecanon.py reads strings from "exe"; the game never)
        ship = {**schema, "exe": exe.name, "ro": stripped_read_only(exe)}
        write_new(args.ship, sc.schema_file(json.dumps(ship).encode()))
        shipped = f", {args.ship.name} ({args.ship.stat().st_size} bytes)"
    print(f"  schema: {out} ({out.stat().st_size} bytes), {plain.name} ({plain.stat().st_size} bytes){shipped}; "
          f"image {schema['image']['format']} {schema['image']['id']}")
    print(f"  heap types named in the source (include/pc_state.h): {len(heap_types)}; counted arrays: {len(counted)}; "
          f"unions with a selector: {len(union_rules)}; state rules: {len(rules['list'])} (revision {rules['revision']})")
    for p_ in heap_problems:
        print(f"    PROBLEM {p_}")
    game_untagged = sum(len(set(v)) for (o, _), v in untagged.items() if o == "game")
    print(f"state-schema: {len(gj)} of {len(game)} game variables with a retail address, {game_untagged} game "
          f"fields untagged, {len(unions)} unions, {sites - annotated} of {sites} heap allocation calls unsized, "
          f"{len(heap_types)} heap types named" + (f", {len(heap_problems)} PROBLEMS" if heap_problems else ""))
    return 1 if heap_problems else 0


if __name__ == "__main__":
    sys.exit(main())
