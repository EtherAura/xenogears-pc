"""The state schema in C (port/src/system/stateschema.c) against the reference (tools/statecanon.py's Schema):
every index the converter uses, printed by both from one build's schema, must be the same: as it is, and
rebased by a load bias (each address plus it, as a position-independent build's process has them).

    test_stateschema_c.py [BUILD]     BUILD (default build/pc/schema) holds state_schema.json and stateschematest
"""
import gzip, json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecanon as cn


BIASES = (0, 0x7F3A_5C40_0000, 0x5800000000 - 0x400000)  # none, a Linux PIE's, Android's reserved address


def reference(build: Path, bias: int = 0) -> list[str]:
    raw = json.load(gzip.open(build / "state_schema.json.gz", "rt"))
    s = cn.Schema(build, bias)
    raw["symbols"] = [[n, (a + bias) & cn.MASK64, z, k] for n, a, z, k in raw.get("symbols", [])]
    out = [f"version {raw['version']}", f"image {raw.get('image', {}).get('id', '')}",
           f"counts {len(s.types)} {len(s.vars)} {len(raw['functions'])} {len(raw.get('symbols', []))} {len(s.scratch)}"]
    for tid in s.types:
        if tid in s.tid_name:
            out.append(f"tidname {tid} {s.tid_name[tid]}")
    for v in s.vars:
        t = v["tid"] if v["tid"] in s.types else "-"
        out.append(f"var {v['id']} {v['addr']} {v['size']} {t}")
        for probe in (v["addr"] + v["size"] // 2, v["addr"] + max(v["size"], 1)):
            hit = s.var_at(probe)
            out.append(f"varat {probe} {hit['id'] if hit else '-'}")
    for i, (name, a, size, kind) in enumerate(raw.get("symbols", [])):
        if kind in "tTwW":
            fid = s.fn_identity(a)
            if fid:
                out.append(f"fnid {a} {fid}")
                out.append(f"fnaddr {fid} {s.fn_address(fid) or 0}")
        if i % 7 == 0:
            below = s.symbol_at(a + 3)
            out.append(f"below {a + 3} {below[0] if below else '-'} {below[1] if below else 0}")
            c = s.ro_named_at(a + 1)
            out.append(f"const {a + 1} {c[2] if c else '-'}")
    for f in raw["functions"]:
        fid = cn.Schema.func_identity(f)
        out.append(f"fnaddr {fid} {s.fn_address(fid) or 0}")
    return out


def main() -> int:
    build = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/pc/schema").resolve()
    exe, schema = build / "stateschematest", build / "state_schema.json"
    if (build / "CMakeCache.txt").exists():  # the test program, current
        r = subprocess.run(["cmake", "--build", str(build), "--target", "stateschematest"], capture_output=True, text=True)
        if r.returncode:
            print(f"test_stateschema_c: building stateschematest failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
            return 1
    if not exe.exists() or not schema.exists():
        print(f"test_stateschema_c: skipped: no {exe.name} or {schema.name} in {build}")
        return 0
    failed = 0
    for bias in BIASES:
        got = subprocess.run([str(exe), str(schema), f"{bias:x}"], capture_output=True, text=True)
        if got.returncode:
            print(f"test_stateschema_c: the C reader failed: {got.stderr.strip()}")
            return 1
        c, py = got.stdout.splitlines(), reference(build, bias)
        if c == py:
            print(f"test_stateschema_c: bias {bias:#x}: the C schema's indexes equal the reference's ({len(py)} lines)")
            continue
        failed += 1
        bad = [(i, a, b) for i, (a, b) in enumerate(zip(c, py)) if a != b]
        print(f"test_stateschema_c: bias {bias:#x}: {len(bad)} lines differ (C {len(c)}, reference {len(py)} lines)")
        for i, a, b in bad[:12]:
            print(f"  {i}: C   {a}\n  {i}: ref {b}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
