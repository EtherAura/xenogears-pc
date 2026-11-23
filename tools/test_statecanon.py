#!/usr/bin/env python3
"""tools/statecanon.py across two layouts, on a small program built here twice (gates.sh: statecanon-tests).

The program's module "testmod" holds what a canonical state must carry across builds: a pointer into
another variable's field, a function pointer, a pointer to a string, an ordering-table link (OTag), an
array of structs, and a union of two views. Built at 0x400000 and at 0x1400000, each build's initial data
section is a "state"; the first, encoded with its schema and decoded with the second's, must come out as
the second has it (every pointer moved by the link's 16 MiB), and a round trip in one build must change
nothing."""
import gzip, json, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecanon as cn  # noqa: E402

SOURCE = r"""
typedef unsigned long long OTag;
typedef struct { int m0_hp; short m4_mp; struct Target* m8_next; } Target;
typedef struct { int m0_id; Target* m8_target; void (*m10_fn)(void); const char* m18_name; OTag m20_link; } Holder;
typedef union { Holder m0_whole; struct { unsigned char m0[16]; void (*m10_fn)(void); } m0_tail; } View;
static void tick(void) {}
#define DATA __attribute__((section("xgdata_testmod"), used))
DATA Target D_80100000[4] = {{1, 2, 0}, {3, 4, 0}, {5, 6, 0}, {7, 8, 0}};
DATA Holder D_80100100 = {9, &D_80100000[2], tick, "fei", 0};
DATA Holder pcHolders[2] = {{1, &D_80100000[1], tick, "elly", 0}, {2, 0, 0, "citan", 0}};
DATA View D_80100200 = {{3, &D_80100000[3], tick, "bart", 0}};
int main(void) {
    D_80100100.m20_link = (OTag)0x05 << 56 | ((OTag)(unsigned long)&pcHolders[1] & 0x00FFFFFFFFFFFFFFull);
    return D_80100100.m0_id + pcHolders[0].m0_id + D_80100200.m0_whole.m0_id;
}
"""


def state_bytes(tag: str, data: bytes) -> bytes:
    body = bytes([len(tag)]) + tag.encode() + struct.pack("<I", len(data)) + data
    head = bytearray(256)
    head[:8] = b"XGSTATE\0"
    struct.pack_into("<II", head, 8, 1, 256)
    head[32:35] = b"US\0"
    struct.pack_into("<Q", head, 200, len(body))
    struct.pack_into("<Q", head, 208, cn.state_hash(body))
    blocks = struct.pack("<II", len(body), len(body)) + body
    struct.pack_into("<Q", head, 224, len(blocks))
    return bytes(head) + blocks


def section(exe: Path, name: str) -> bytes:
    for line in subprocess.run(["readelf", "-SW", str(exe)], capture_output=True, text=True).stdout.splitlines():
        parts = line.replace("[ ", "[").split()
        if len(parts) > 5 and parts[1] == name:
            off, size = int(parts[4], 16), int(parts[5], 16)
            return exe.read_bytes()[off:off + size]
    raise SystemExit(f"no section {name} in {exe}")


def main() -> int:
    if not shutil.which("cc"):
        print("statecanon tests: skipped (no C compiler)")
        return 0
    checks = 0

    def check(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            print(f"statecanon tests: FAIL {what}")
            sys.exit(1)

    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="statecanon_test.") as t:
        d = Path(t)
        builds, states = [], []
        for name, base in (("a", "0x400000"), ("b", "0x1400000")):
            b = d / name
            b.mkdir()
            (b / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
            (b / "thing.c").write_text(SOURCE)
            subprocess.run(["cc", "-g", "-O0", "-no-pie", f"-Wl,-Ttext-segment={base}", "-o", str(b / "xenogears"),
                            str(b / "thing.c")], check=True)
            r = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(b)], capture_output=True, text=True)
            check(r.returncode == 0, f"schema of build {name}: {r.stdout}{r.stderr}")
            s = b / "state.xgs"
            s.write_bytes(state_bytes("mem.testmod.data", section(b / "xenogears", "xgdata_testmod")))
            builds.append(cn.Schema(b)); states.append(s)
        a, b = builds
        doc, enc = cn.encode(states[0], a)
        ids = {r["id"] for r in doc["records"]}
        check({"testmod:80100000", "testmod:80100100", "testmod:pc:pcHolders", "testmod:80100200"} <= ids, f"identities {ids}")
        holder = next(r for r in doc["records"] if r["id"] == "testmod:80100100")["v"]
        check(holder["+8"] == {"v": "testmod:80100000", "p": [2, "+0"]}, f"a pointer into an array element ({holder['+8']})")
        check(holder["+10"]["fn"].endswith("tick"), "a function pointer by name")
        check(holder["+18"].get("str") == "fei", "a string by its content")
        check(not enc.unknown, f"no unknown pointer ({dict(enc.unknown)})")
        lines, st = cn.crosscheck(states[0], a, states[1], b)
        check(st["checked"] > 0 and not lines, "decoded into build b as b has it:\n" + "\n".join(lines))
        doc2 = json.loads(json.dumps(doc))
        body, dec = cn.decode(doc2, a, states[0])
        _, orig = cn.sc.read_state(states[0])
        check(body == orig and not dec.warn, "a round trip in one build changes nothing")
    print(f"statecanon tests: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
