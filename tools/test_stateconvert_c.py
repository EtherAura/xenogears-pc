#!/usr/bin/env python3
"""The state converter in C (port/src/system/stateconvert.c) against the reference (tools/statecanon.py): for each
pair of states, the writer's a.xgs and the reader's template t.xgs, both convert, and the written files must be the
same byte for byte, with the same warnings.

    test_stateconvert_c.py [PAIR_DIR ...] [--a BUILD] [--b BUILD] [--reverse] [--mutate] [--cross] [--all]

A PAIR_DIR holds a.xgs and t.xgs, or run/a.xgs and run/t.xgs (work/canon/resume_e2e.py's output): layout A's state
saved at a safe point, and layout B's at the same one. Default: every pair under $XG_CONVERT_PAIRS
(/mnt/Games/scratch/xg-convert, which work/canon/convert_pairs.sh fills). A and B (default build/pc/schema and
build/pc/schema2, linked 16 MiB apart) hold state_schema.json and its .gz; A's build holds stateconverttest. Each pair
converts stackless (statecanon convert) and with its stack (statecanon decode without --stackless). More cases:
  --reverse   B's state into A's layout (a.xgs the template)
  --mutate    into a copy of B's schema with field keys, variables' identities, union views, heap type names,
              functions and constants changed or dropped (the decoder's warnings and fallbacks)
  --cross     each pair's state with the next pair's template (other chunk sizes and host parts)
  --all       all three
Skips (exit 0) when a build, a schema or the pairs are missing. On a difference it names the first differing chunk
and offset, and the variable and field, or the heap block and part, there."""
import argparse, copy, gzip, json, os, re, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecanon as cn  # noqa: E402
import statecheck as sc  # noqa: E402

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
    body, dec = cn.decode(doc, sb, t_state, stackless=stackless)
    cn.write_state(t_state, body, out)
    return body, ew, dict(dec.warn), dec, doc


def c_convert(exe: Path, a_state: Path, ja: Path, t_state: Path, jb: Path, stackless: bool, out: Path):
    cmd = [str(exe), "convert", str(a_state), str(ja), str(t_state), str(jb), str(out)] + (["--stackless"] if stackless else [])
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    took = time.time() - t0
    ew, dw = {}, {}
    for line in r.stdout.splitlines()[1:]:
        side, n, text = line.split(" ", 2)
        (ew if side == "encode" else dw)[text] = int(n)
    return r, ew, dw, took


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
    for flag in ("reverse", "mutate", "cross", "all"):
        ap.add_argument(f"--{flag}", action="store_true")
    args = ap.parse_args()
    if args.all:
        args.reverse = args.mutate = args.cross = True
    A, B = args.a.resolve(), args.b.resolve()
    exe = A / "stateconverttest"
    if (A / "CMakeCache.txt").exists():  # the test program, current
        r = subprocess.run(["cmake", "--build", str(A), "--target", "stateconverttest"], capture_output=True, text=True)
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
        stale = [n for n, (a, t) in pairs if a.stat().st_mtime < (A / "xenogears").stat().st_mtime or
                 t.stat().st_mtime < (B / "xenogears").stat().st_mtime]
    if stale:
        print(f"test_stateconvert_c: skipped: pairs older than the builds ({', '.join(stale)}): "
              f"work/canon/convert_pairs.sh makes new ones")
        return 0
    sa, sb = cn.Schema(A), cn.Schema(B)
    cases = []  # (name, writer state, writer schema dir, template, reader schema dir)
    for name, (a, t) in pairs:
        cases.append((name, a, A, t, B))
    if args.reverse:
        cases += [(f"{name} B to A", t, B, a, A) for name, (a, t) in pairs]
    if args.cross and len(pairs) > 1:
        cases += [(f"{pairs[k][0]} with {pairs[(k + 1) % len(pairs)][0]}'s template", pairs[k][1][0], A,
                   pairs[(k + 1) % len(pairs)][1][1], B) for k in range(len(pairs))]
    tmp = Path(tempfile.mkdtemp(prefix="test_stateconvert_c."))
    schemas = {A: sa, B: sb}
    if args.mutate:
        M = tmp / "mutated"
        write_schema(M, mutate(json.load(gzip.open(B / "state_schema.json.gz", "rt"))))
        schemas[M] = cn.Schema(M)
        cases += [(f"{name} into a mutated B", a, A, t, M) for name, (a, t) in pairs]
    bad = compared = 0
    total = 0
    cache = {}
    for name, a_state, wa, t_state, wb in cases:
        for stackless in (True, False):
            label = f"{name}, {'stackless' if stackless else 'with the stack'}"
            ref, got = tmp / "ref.xgs", tmp / "c.xgs"
            for p in (ref, got):
                p.unlink(missing_ok=True)
            try:
                body, ew, dw, dec, doc = reference(a_state, schemas[wa], t_state, schemas[wb], stackless, ref, cache)
                why = None
            except (Exception, SystemExit) as e:  # noqa: BLE001 (the reference's own failure is a result too)
                why = f"{type(e).__name__}: {e}"
            r, cew, cdw, took = c_convert(exe, a_state, wa / "state_schema.json", t_state, wb / "state_schema.json",
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
            if (ew, dw) != (cew, cdw):
                line += f"; warnings differ: reference encode {ew} decode {dw}, C encode {cew} decode {cdw}"
                bad += 1
            elif dw or any(not k.startswith("union ") for k in ew):
                line += f"; warnings alike: encode {ew}, decode {dw}"
            print(line, flush=True)
            if not same:
                bad += 1
                print(f"    {first_difference(ref, got, schemas[wb], dec, doc)}")
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
