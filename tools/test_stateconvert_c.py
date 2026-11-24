#!/usr/bin/env python3
"""The state converter in C (port/src/system/stateconvert.c) against the reference (tools/statecanon.py): for each
pair of states, the writer's a.xgs and the reader's template t.xgs, both convert, and the written files must be the
same byte for byte, with the same warnings.

    test_stateconvert_c.py [PAIR_DIR ...] [--a BUILD] [--b BUILD] [--converter BUILD] [--reverse] [--mutate]
                           [--cross] [--bias] [--rules] [--all]

A PAIR_DIR holds a.xgs and t.xgs, or run/a.xgs and run/t.xgs (work/canon/resume_e2e.py's output): layout A's state
saved at a safe point, and layout B's at the same one. Default: every pair under $XG_CONVERT_PAIRS
(/mnt/Games/scratch/xg-convert, which work/canon/convert_pairs.sh fills). A and B (default build/pc/schema and
build/pc/schema2, linked 16 MiB apart) hold state_schema.json and its .gz; the converter's build (default A's, a Linux
one: it converts Windows builds' states too) holds stateconverttest. Each state is read with its build's schema
rebased by the load bias its header records (a position-independent build's: statecanon state_bias). Each pair
converts stackless (statecanon convert) and with its stack (statecanon decode without --stackless). More cases:
  --reverse   B's state into A's layout (a.xgs the template)
  --mutate    into a copy of B's schema with field keys, variables' identities, union views, heap type names,
              functions and constants changed or dropped, and the unnamed struct a host part's pointer points
              at renumbered (the decoder's warnings and fallbacks)
  --cross     each pair's state with the next pair's template (other chunk sizes and host parts)
  --bias      load biases on both sides: A's schema linked BIAS_W below where its state ran (the state's header
              saying so, a PIE's case), B's linked BIAS_R above (a negative bias, modulo 2^64); besides being
              identical to the reference, the body must be the one the unbiased conversion writes
  --rules     the state rules (docs/CANONICAL_STATES.md 17.2): B's schema as a later build's (revision 2) that renamed
              members, variables and functions, with the rules that say so: A's state into it and its own into A
              (forward and backward) must give the bodies the conversions without them give; and one that also
              renamed heap types and maps values, resets, defaults, removes, and has a rule of an unknown kind
  --all       all five
Skips (exit 0) when a build, a schema or the pairs are missing. On a difference it names the first differing chunk
and offset, and the variable and field, or the heap block and part, there."""
import argparse, copy, gzip, json, os, re, struct, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecanon as cn  # noqa: E402
import statecheck as sc  # noqa: E402
import state_schema as ss  # noqa: E402 (build_image)

PAIRS = Path(os.environ.get("XG_CONVERT_PAIRS", "/mnt/Games/scratch/xg-convert"))


def pair_files(d: Path):
    for base in (d, d / "run"):
        if (base / "a.xgs").exists() and (base / "t.xgs").exists():
            return base / "a.xgs", base / "t.xgs"
    return None


def reference(a_state: Path, sa: cn.Schema, t_state: Path, sb: cn.Schema, stackless: bool, out: Path, cache: dict):
    """statecanon's conversion, as convert (stackless) or encode then decode does it; the doc kept per state."""
    key = (str(a_state), id(sa))
    if key not in cache:
        doc, enc = cn.encode(a_state, sa)
        cache.clear()  # one document at a time: they are large
        cache[key] = (json.loads(json.dumps(doc)), dict(enc.warn))
    doc, ew = cache[key]
    body, dec = cn.decode(doc, sb, t_state, stackless=stackless, writer=sa)
    cn.write_state(t_state, body, out)
    return body, ew, dict(dec.warn), dec, doc


def c_convert(exe: Path, a_state: Path, ja: Path, t_state: Path, jb: Path, stackless: bool, out: Path):
    cmd = [str(exe), "convert", str(a_state), str(ja), str(t_state), str(jb), str(out)] + (["--stackless"] if stackless else [])
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    took = time.time() - t0
    ew, dw, rw = {}, {}, {}
    for line in r.stdout.splitlines()[1:]:
        side, n, text = line.split(" ", 2)
        {"encode": ew, "decode": dw, "rule": rw}[side][text] = int(n)
    return r, ew, dw, rw, took


def where(tag: str, i: int, sb: cn.Schema, dec, doc) -> str:
    """What the reader has at byte i of a chunk: a variable's field, a heap block's part."""
    m = re.fullmatch(r"mem\.(\w+)\.(data|bss)", tag)
    if m and f"{m.group(1)}.{m.group(2)}" in sb.sections:
        a = sb.sections[f"{m.group(1)}.{m.group(2)}"][0] + i
        v = sb.var_at(a)
        if v is None:
            return "outside every variable"
        if dec.arena and v["name"] == "pcSoundArena":
            for j, b in enumerate(dec.arena["blocks"]):
                if b["data"] - cn.ARENA_HEADER <= a < b["data"] + b["size"]:
                    return f"pcSoundArena block {j} (+{a - b['data']})"
        return cn.field_path(sb, v, a - v["addr"])
    if tag == "mem.heap" and dec.heap:
        lo = sb.regions["heapArena"][0]
        for k, b in enumerate(dec.heap):
            if b["data"] - cn.HEAP_HEADER <= lo + i < b["data"] + b["size"]:
                rel = lo + i - b["data"]
                rec = b["rec"]
                site = rec.get("site")
                what = f"heap block {k}" + (f" ({site[0]} {site[1]}:{site[2]})" if site else "")
                if rel < 0:
                    return f"{what}, its header +{rel + cn.HEAP_HEADER}"
                for p, (part, lay) in enumerate(zip(rec.get("parts", []), b.get("parts", []))):
                    if lay["off"] <= rel < lay["off"] + lay["size"]:
                        t = part.get("t")
                        name = t.get("name") if isinstance(t, dict) else (t or "bytes")
                        return f"{what}, part {p} ({name}) +{rel - lay['off']}"
                return f"{what} +{rel}"
    return f"+{i}"


def first_difference(ref: Path, got: Path, sb: cn.Schema, dec, doc) -> str:
    x, y = ref.read_bytes(), got.read_bytes()
    if x[:sc.HEADER_SIZE] != y[:sc.HEADER_SIZE]:
        return "the header differs" + (" (the body differs too)" if x[sc.HEADER_SIZE:] != y[sc.HEADER_SIZE:] else "")
    _, a = sc.read_state(ref)
    _, b = sc.read_state(got)
    ca, cb = sc.chunks(a), sc.chunks(b)
    for (ta, oa, na), (tb, ob, nb) in zip(ca, cb):
        if ta != tb or na != nb:
            return f"chunk {ta} ({na} bytes), C wrote {tb} ({nb} bytes)"
        u, v = a[oa:oa + na], b[ob:ob + nb]
        if u != v:
            i = next(k for k in range(na) if u[k] != v[k])
            n = sum(1 for k in range(na) if u[k] != v[k])
            return (f"chunk {ta} at +{i} (0x{i:x}): {where(ta, i, sb, dec, doc)}: reference {u[i:i + 8].hex()}, "
                    f"C {v[i:i + 8].hex()}; {n} bytes differ in the chunk")
    if len(ca) != len(cb):
        return f"{len(ca)} chunks, C wrote {len(cb)}"
    return "the files differ (the thumbnail or the blocks)"


def mutate(raw: dict) -> dict:
    """B's schema changed the way another build's could be: a test of the decoder's fallbacks, not a real layout."""
    s = copy.deepcopy(raw)
    types = s["types"]
    ruled = {r["name"] for r in list(s.get("counted", {}).values()) + list(s.get("unions", {}).values())}
    for k, tid in enumerate(sorted(types)):
        t = types[tid]
        if t["k"] == "struct" and len(t["fields"]) >= 2 and k % 13 == 0 and t.get("name") not in ruled:
            f = t["fields"][-1]  # another key: missing in the state, pointer paths through it gone
            if f.get("retail") is not None:
                f["retail"] += 0x100000
            else:
                f["name"] += "_renamed"
        if t["k"] == "union" and len(t["fields"]) >= 2 and k % 3 == 0:
            t["fields"][0]["name"] += "_renamed"  # a view the state has not
    for rule in s.get("unions", {}).values():  # a selected view this build has not
        for t in types.values():
            if t.get("name") == rule["name"]:
                mf = next((f for f in t["fields"] if f["name"] == rule["member"]), None)
                mt = types.get(mf["type"]) if mf else None
                if mt and mt.get("fields"):
                    mt["fields"][-1]["name"] += "_renamed"
                break
    # the unnamed struct a host part's pointer variable points at (hw.c's queue: Pending) at another DWARF offset and
    # without its typedef, as MinGW's LTO leaves it: the array is the reader's own type's, not the writer's
    for v in s["variables"]:
        t = types.get(v.get("tid") or "", {})
        old = t.get("to") if v.get("platform") and t.get("k") == "ptr" and not t.get("fn") else None
        if old not in types or "<anon>" not in types[old].get("name", "") or old.endswith(".renumbered"):
            continue
        new = f"{old}.renumbered"
        types[new] = types.pop(old)
        for u in types.values():
            for key in ("to", "of"):
                if u.get(key) == old:
                    u[key] = new
            for f in u.get("fields", []):
                if f["type"] == old:
                    f["type"] = new
        for w in s["variables"]:
            if w.get("tid") == old:
                w["tid"] = new
        for h in s.get("heap_types", {}).values():
            if isinstance(h.get("spec"), dict) and h["spec"].get("tid") == old:
                h["spec"]["tid"] = new
        for key in ("counted", "unions"):
            if old in s.get(key, {}):
                s[key][new] = s[key].pop(old)
        s["type_names"] = {n: tid for n, tid in s.get("type_names", {}).items() if tid != old}
    for k, v in enumerate(s["variables"]):
        if v.get("module") and v.get("retail") is not None and k % 11 == 0 and v["name"] != "safePointId":
            v["retail"] += 0x100000  # another identity: left zero, and pointers to it lost
    names = sorted(s.get("type_names", {}))
    for k, n in enumerate(names):
        if k % 5 == 0 and any(h["name"] == n for h in s.get("heap_types", {}).values()):
            del s["type_names"][n]  # a heap part's type this build does not name
    s["functions"] = [f for k, f in enumerate(s["functions"]) if k % 17]
    gone = {f["name"] for k, f in enumerate(raw["functions"]) if k % 17 == 0}
    syms = []
    for k, row in enumerate(s.get("symbols", [])):
        if row[0] in gone or (row[3] in "rR" and k % 9 == 0):
            continue  # functions and constants not in this build
        syms.append(row)
    s["symbols"] = syms
    return s


def with_rules(raw: dict, everything: bool) -> dict:
    """B's schema as a later build's that renamed things and says so in its rules (revision 2): the last member of
    every 7th struct (not one a counted array or a union rule names), every 11th game variable with a retail address
    (another one now), every 13th function; with everything, also every 5th heap type, values maps (swapping 0 and 1)
    on every 9th struct's first integer member, a reset, a default, removed members and a rule of a kind no converter
    knows."""
    s = copy.deepcopy(raw)
    types = s["types"]; rules = []
    ruled = {r["name"] for r in list(s.get("counted", {}).values()) + list(s.get("unions", {}).values())}
    # the structs a converter reads by their members' names in its own schema (statecanon state_ranges): a rename
    # there is the converter's to follow, not a rule's
    by_name = set()
    for v in s["variables"]:
        if v["name"] == "pcStateRanges" and v.get("tid") in types:
            by_name.add(v["tid"])
            by_name.update(types[f["type"]].get("of") for f in types[v["tid"]].get("fields", [])
                           if types.get(f["type"], {}).get("k") == "arr")
    for k, tid in enumerate(sorted(types)):
        t = types[tid]
        if t["k"] != "struct" or not t["fields"] or t.get("name") in ruled or tid in by_name:
            continue
        keys = lambda: [cn.Encoder.field_key(None, x, se) for se in [set()] for x in t["fields"]]  # noqa: E731
        if k % 7 == 0:
            old = keys()[-1]; f = t["fields"][-1]
            if f.get("retail") is not None:
                f["retail"] += 0x100000
            else:
                f["name"] += "_renamed"
            new = keys()[-1]
            if new != old and len(set(keys())) == len(keys()):
                rules.append({"kind": "was", "rev": 2, "type": t.get("name", ""), "tids": [tid], "field": new, "old": old})
        if everything and k % 9 == 0:
            ints = [(key, f) for key, f in zip(keys(), t["fields"]) if types.get(f["type"], {}).get("k") == "int"]
            if ints:
                rules.append({"kind": "values", "rev": 2, "type": t.get("name", ""), "tids": [tid], "field": ints[0][0],
                              "map": [[0, 1], [1, 0]]})
            if len(ints) > 1:
                rules.append({"kind": "reset", "rev": 2, "type": t.get("name", ""), "tids": [tid], "field": ints[1][0]})
            if len(ints) > 2:
                rules.append({"kind": "default", "type": t.get("name", ""), "tids": [tid], "field": ints[2][0], "value": 3})
            rules.append({"kind": "removed", "rev": 2, "type": t.get("name", ""), "tids": [tid],
                          "fields": ["pc:gone", keys()[0]], "value": 2})
    keyed, _ = cn.keyed_variables(copy.deepcopy(s["variables"]))
    taken = {v["id"] for v in keyed}
    by_addr = {v["addr"]: v["id"] for v in keyed}
    for k, v in enumerate(s["variables"]):
        old = by_addr.get(v["addr"])
        if v.get("module") and v.get("retail") is not None and k % 11 == 0 and v["name"] != "safePointId" and \
                old == cn.var_identity(v):
            v["retail"] += 0x100000
            new = cn.var_identity(v)
            if new in taken:
                v["retail"] -= 0x100000; continue
            rules.append({"kind": "was", "rev": 2, "var": new, "old": old})
    renamed = {}
    for k, f in enumerate(s["functions"]):
        if k % 13 == 0 and f["name"] not in renamed:
            renamed[f["name"]] = f["name"] + "_renamed"
    for f in s["functions"]:
        f["name"] = renamed.get(f["name"], f["name"])
    s["symbols"] = [[renamed.get(n, n), a, z, t] for n, a, z, t in s.get("symbols", [])]
    rules += [{"kind": "was", "rev": 2, "fn": new, "old": old} for old, new in renamed.items()]
    if everything:
        for k, (h, e) in enumerate(sorted(s.get("heap_types", {}).items())):
            if k % 5 or not isinstance(e.get("spec"), dict) or e["name"] not in s.get("type_names", {}) or \
                    e["name"].endswith("[1]"):
                continue
            new = e["name"] + "Renamed"
            tid = s["type_names"].pop(e["name"])
            s["type_names"][new] = tid
            types[tid]["name"] = new
            del s["heap_types"][h]
            s["heap_types"][f"{cn.type_hash(new):08x}"] = {**e, "name": new}
            rules.append({"kind": "was", "rev": 2, "typename": new, "old": e["name"]})
        rules.append({"kind": "someday", "rev": 2, "var": "main:pc:x"})
    s["rules"] = {"language": 1, "revision": 2, "list": rules}
    return s


BIAS_W, BIAS_R = 0x100000, 0x7F00_0000_0000  # --bias: the writer's load bias, the reader's (below 0)


def with_bias(state: Path, delta: int, out: Path) -> Path:
    """A copy of a state whose header says its process ran delta further from its link addresses."""
    data = bytearray(state.read_bytes())
    struct.pack_into("<Q", data, 232, (struct.unpack_from("<Q", data, 232)[0] + delta) & cn.MASK64)
    out.write_bytes(bytes(data))
    return out


def body_of(path: Path) -> bytes:
    return sc.read_state(path)[1]


def write_schema(d: Path, s: dict) -> None:
    d.mkdir(parents=True, exist_ok=True)
    text = json.dumps(s)
    (d / "state_schema.json").write_text(text)
    with gzip.open(d / "state_schema.json.gz", "wt") as fh:
        fh.write(text)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("pairs", nargs="*", type=Path)
    ap.add_argument("--a", type=Path, default=ROOT / "build/pc/schema")
    ap.add_argument("--b", type=Path, default=ROOT / "build/pc/schema2")
    ap.add_argument("--converter", type=Path, help="the build holding stateconverttest (default A's)")
    for flag in ("reverse", "mutate", "cross", "bias", "rules", "all"):
        ap.add_argument(f"--{flag}", action="store_true")
    args = ap.parse_args()
    if args.all:
        args.reverse = args.mutate = args.cross = args.bias = args.rules = True
    A, B = args.a.resolve(), args.b.resolve()
    C = (args.converter or args.a).resolve()
    exe = C / "stateconverttest"
    if (C / "CMakeCache.txt").exists():  # the test program, current
        r = subprocess.run(["cmake", "--build", str(C), "--target", "stateconverttest"], capture_output=True, text=True)
        if r.returncode:
            print(f"test_stateconvert_c: building stateconverttest failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
            return 1
    for need in (exe, A / "state_schema.json", A / "state_schema.json.gz", B / "state_schema.json", B / "state_schema.json.gz"):
        if not need.exists():
            print(f"test_stateconvert_c: skipped: no {need}")
            return 0
    dirs = args.pairs or (sorted(p for p in PAIRS.iterdir() if p.is_dir()) if PAIRS.is_dir() else [])
    pairs = [(d.name, f) for d in dirs if (f := pair_files(d))]
    if not pairs:
        print(f"test_stateconvert_c: skipped: no state pairs (a.xgs, t.xgs) in {', '.join(map(str, dirs)) or PAIRS}")
        return 0
    if args.pairs:  # named pairs are compared even when stale; the default set must be the builds' own
        stale = []
    else:
        ia, ib = ss.build_image(A), ss.build_image(B)  # (a shared-library build's: the game library)
        stale = [n for n, (a, t) in pairs if a.stat().st_mtime < ia.stat().st_mtime or t.stat().st_mtime < ib.stat().st_mtime]
    if stale:
        print(f"test_stateconvert_c: skipped: pairs older than the builds ({', '.join(stale)}): "
              f"work/canon/convert_pairs.sh makes new ones")
        return 0
    loaded = {}

    def schema(d: Path, state: Path) -> cn.Schema:
        """d's schema at the addresses state's process had (its header's load bias); one per (d, bias)."""
        key = (d, cn.state_bias(state))
        if key not in loaded:
            loaded[key] = cn.Schema(d, key[1])
        return loaded[key]

    cases = []  # (name, writer state, writer schema dir, template, reader schema dir[, the plain case's output])
    for name, (a, t) in pairs:
        cases.append((name, a, A, t, B))
    if args.reverse:
        cases += [(f"{name} B to A", t, B, a, A) for name, (a, t) in pairs]
    if args.cross and len(pairs) > 1:
        cases += [(f"{pairs[k][0]} with {pairs[(k + 1) % len(pairs)][0]}'s template", pairs[k][1][0], A,
                   pairs[(k + 1) % len(pairs)][1][1], B) for k in range(len(pairs))]
    tmp = Path(tempfile.mkdtemp(prefix="test_stateconvert_c."))
    if args.mutate:
        M = tmp / "mutated"
        write_schema(M, mutate(json.load(gzip.open(B / "state_schema.json.gz", "rt"))))
        cases += [(f"{name} into a mutated B", a, A, t, M) for name, (a, t) in pairs]
    if args.bias:
        AW, BR = tmp / "a-linked-lower", tmp / "b-linked-higher"
        for d, src, delta in ((AW, A, -BIAS_W), (BR, B, BIAS_R)):
            raw = json.load(gzip.open(src / "state_schema.json.gz", "rt"))
            low = min([v["addr"] for v in raw["variables"]] + [f["addr"] for f in raw["functions"]] +
                      [r[1] for r in raw.get("symbols", [])] + [r[0] for r in raw.get("ro", [])] +
                      [lo for lo, _ in list(raw["sections"].values()) + list(raw["regions"].values())])
            if low + delta < 0:
                print(f"test_stateconvert_c: --bias: {src}'s lowest address {low:#x} is below {-delta:#x}")
                return 1
            cn.rebase(raw, delta & cn.MASK64)
            write_schema(d, raw)
        (tmp / "states").mkdir()
        for k, (name, (a, t)) in enumerate(pairs):
            wa = with_bias(a, BIAS_W, tmp / "states" / f"{k}a.xgs")
            wt = with_bias(t, -BIAS_R, tmp / "states" / f"{k}t.xgs")
            cases.append((f"{name} at load biases {BIAS_W:#x} and {-BIAS_R & cn.MASK64:#x}", wa, AW, wt, BR, (a, A, t, B)))
    if args.rules:
        RN, RA = tmp / "b-renamed", tmp / "b-rules"
        raw = json.load(gzip.open(B / "state_schema.json.gz", "rt"))
        write_schema(RN, with_rules(raw, False))
        write_schema(RA, with_rules(raw, True))
        for name, (a, t) in pairs:
            cases.append((f"{name} into B renamed (its rules forward)", a, A, t, RN, (a, A, t, B)))
            cases.append((f"{name} B renamed into A (its rules backward)", t, RN, a, A, (t, B, a, A)))
            cases.append((f"{name} into B with every kind of rule", a, A, t, RA))
            cases.append((f"{name} B with every kind of rule into A", t, RA, a, A))
    bad = compared = 0
    total = 0
    cache = {}
    for name, a_state, wa, t_state, wb, *plain in cases:
        for stackless in (True, False):
            label = f"{name}, {'stackless' if stackless else 'with the stack'}"
            ref, got = tmp / "ref.xgs", tmp / "c.xgs"
            for p in (ref, got):
                p.unlink(missing_ok=True)
            try:
                body, ew, dw, dec, doc = reference(a_state, schema(wa, a_state), t_state, schema(wb, t_state), stackless,
                                                   ref, cache)
                why = None
            except (Exception, SystemExit) as e:  # noqa: BLE001 (the reference's own failure is a result too)
                why = f"{type(e).__name__}: {e}"
            r, cew, cdw, crw, took = c_convert(exe, a_state, wa / "state_schema.json", t_state, wb / "state_schema.json",
                                               stackless, got)
            compared += 1
            if why is not None:
                if r.returncode:
                    print(f"  {label}: both refuse (reference {why[:120]}; C {r.stderr.strip()[:160]})")
                else:
                    bad += 1
                    print(f"  {label}: the reference fails ({why[:200]}), C converted")
                continue
            if r.returncode:
                bad += 1
                print(f"  {label}: C failed: {r.stderr.strip()[:300]}")
                continue
            same = ref.read_bytes() == got.read_bytes()
            total += len(body)
            line = f"  {label}: {len(body)} bytes {'identical' if same else 'DIFFER'}; C {took:.2f} s"
            if plain:  # the same conversion without the biases or the renames (C, fast): the same body
                unbiased = tmp / "plain.xgs"
                ps, pw, pt, pr = plain[0]
                rp, *_ = c_convert(exe, ps, pw / "state_schema.json", pt, pr / "state_schema.json", stackless, unbiased)
                if rp.returncode or body_of(unbiased) != body:
                    bad += 1
                    line += "; NOT the plain conversion's body" + (f" (it failed: {rp.stderr.strip()[:200]})"
                                                                     if rp.returncode else "")
                else:
                    line += "; the plain conversion's body"
            rw = dict(dec.applied)
            if (ew, dw, rw) != (cew, cdw, crw):
                line += (f"; warnings differ: reference encode {ew} decode {dw} rules {rw}, C encode {cew} decode {cdw} "
                         f"rules {crw}")
                bad += 1
            elif dw or rw or any(not k.startswith("union ") for k in ew):
                line += f"; warnings alike: encode {ew}, decode {dw}" + (f", rules {rw}" if rw else "")
            print(line, flush=True)
            if not same:
                bad += 1
                print(f"    {first_difference(ref, got, schema(wb, t_state), dec, doc)}")
    for p in tmp.rglob("*"):
        if p.is_file():
            p.unlink()
    for p in sorted(tmp.rglob("*"), reverse=True):
        p.rmdir()
    tmp.rmdir()
    if bad:
        print(f"test_stateconvert_c: {bad} of {compared} conversions differ from the reference")
        return 1
    print(f"test_stateconvert_c: {compared} conversions ({len(pairs)} pairs, {total} bytes) byte-identical to the reference")
    return 0


if __name__ == "__main__":
    sys.exit(main())
