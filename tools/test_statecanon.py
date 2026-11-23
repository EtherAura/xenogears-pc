#!/usr/bin/env python3
"""tools/statecanon.py across two layouts, on a small program built here twice (gates.sh: statecanon-tests).

The program's module "testmod" holds what a canonical state must carry across builds: a pointer into
another variable's field, a function pointer, a pointer to a string, an ordering-table link (OTag), an
array of structs, and a union of two views. Its heap (HeapBlock headers as the PC's, include/main/memory.h)
holds what include/pc_state.h's annotations say: an array of a struct named at allocation (PC_HEAP_OF), a
struct with a counted array (PC_STATE_COUNTED), a file's bytes where the program keeps a pointer and a struct
(PC_STATE_AT), a free block and the end marker; and a union whose view a member selects (PC_STATE_UNION).
Built at 0x400000 and at 0x1400000, each build runs and writes its module section and heap; the first,
encoded with its schema and decoded with the second's, must come out as the second has it (every pointer
moved by the link's 16 MiB), and a round trip in one build must change nothing."""
import json, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import statecanon as cn  # noqa: E402

SOURCE = r"""
#include <stdio.h>
#include <string.h>
typedef unsigned long long OTag;
typedef struct { int m0_hp; short m4_mp; struct Target* m8_next; } Target;
typedef struct { int m0_id; Target* m8_target; void (*m10_fn)(void); const char* m18_name; OTag m20_link; } Holder;
typedef union { Holder m0_whole; struct { unsigned char m0[16]; void (*m10_fn)(void); } m0_tail; } View;
typedef struct { int m0_count; int m4_flags; Target* m8_items[1]; } List;
#define PC_STATE_COUNTED(T, array, count, extra)
PC_STATE_COUNTED(List, m8_items, m0_count, 0)
typedef struct { Target* m0_target; } PtrView;
typedef struct { short m0[8]; } ShortView;  /* more typed bytes: written last when every view is kept */
typedef struct { unsigned char m0_kind; union { PtrView ptr; ShortView shorts; } m8_view; } Pick;
#define PC_STATE_UNION(T, member, selector, views)
PC_STATE_UNION(Pick, m8_view, m0_kind, (ptr, shorts))
typedef struct HeapBlock { struct HeapBlock* m0_next; unsigned int m4_bits; unsigned int mC_pcType; } HeapBlock;
typedef struct { const void* at; unsigned int type; int count; } StateRange;
static void tick(void) {}
#define DATA __attribute__((section("xgdata_testmod"), used))
DATA Target D_80100000[4] = {{1, 2, 0}, {3, 4, 0}, {5, 6, 0}, {7, 8, 0}};
DATA Holder D_80100100 = {9, &D_80100000[2], tick, "fei", 0};
DATA Holder pcHolders[2] = {{1, &D_80100000[1], tick, "elly", 0}, {2, 0, 0, "citan", 0}};
DATA View D_80100200 = {{3, &D_80100000[3], tick, "bart", 0}};
DATA HeapBlock* heapFirstBlockData = 0;
DATA Target* D_80100300 = 0;  /* into the heap: a counted array's element */
DATA short* D_80100308 = 0;   /* into the heap: a field of a struct in a file's bytes */
DATA Pick D_80100400[2] = {{0, {.ptr = {&D_80100000[1]}}}, {1, {.shorts = {{1, 2, 3, 4, 5, 6, 7, 8}}}}};
DATA struct { int count; int overflow; StateRange ranges[4]; } pcStateRanges = {0};
unsigned char heapArena[4096] __attribute__((aligned(16)));
extern char __start_xgdata_testmod[], __stop_xgdata_testmod[];

static unsigned int typeId(const char* s) {  /* port/src/game/main/statetypes.c pcStateTypeId */
    unsigned int h = 0x811C9DC5u;
    for (; *s; s++) if (*s != ' ') h = (h ^ (unsigned char)*s) * 0x01000193u;
    return h ? h : 1;
}
#define PC_HEAP_OF(T, size) (pending = typeId(#T), (size))
#define PC_STATE_AT(at_, T, n) (pcStateRanges.ranges[pcStateRanges.count].at = (at_), \
    pcStateRanges.ranges[pcStateRanges.count].type = typeId(#T), pcStateRanges.ranges[pcStateRanges.count++].count = (n))
static unsigned int pending;
static unsigned char* top;
static void* allocate(int size) {  /* first fit at the top, 16-byte grains, as the PC heap */
    HeapBlock* h = (HeapBlock*)top;
    size = (size + 15) & ~15;
    h->m0_next = (HeapBlock*)(top + 16 + size + 16);
    h->m4_bits = (10u << 21) | (0x20u << 26);  /* owner 10, no caller: a round trip leaves it 0 */
    h->mC_pcType = pending; pending = 0;
    top += 16 + size;
    return h + 1;
}
int main(int argc, char** argv) {
    Target* t; List* l; unsigned char* raw; HeapBlock* f; FILE* out;
    D_80100100.m20_link = (OTag)0x05 << 56 | ((OTag)(unsigned long)&pcHolders[1] & 0x00FFFFFFFFFFFFFFull);
    top = heapArena;
    heapFirstBlockData = (HeapBlock*)(heapArena + 16);
    t = allocate(PC_HEAP_OF(Target, 3 * sizeof(Target)));
    t[0].m0_hp = 10; t[0].m8_next = (struct Target*)&t[2]; t[1].m8_next = (struct Target*)&D_80100000[0];
    t[2].m4_mp = 7;
    l = allocate(PC_HEAP_OF(List, sizeof(List) + 2 * sizeof(Target*)));
    l->m0_count = 3; l->m4_flags = 1; l->m8_items[0] = &t[1]; l->m8_items[1] = &t[0]; l->m8_items[2] = &D_80100000[3];
    D_80100300 = (Target*)&l->m8_items[2];
    raw = allocate(64);  /* a file's bytes: a count, a pointer the program keeps at +8, a Target at +16 */
    memset(raw, 0xA5, 64); raw[0] = 2;
    memcpy(raw + 8, &l, sizeof(l)); PC_STATE_AT(raw + 8, List*, 1);
    ((Target*)(raw + 16))->m0_hp = 99; ((Target*)(raw + 16))->m8_next = (struct Target*)&t[0];
    PC_STATE_AT(raw + 16, Target, 1);
    D_80100308 = &((Target*)(raw + 16))->m4_mp;
    f = (HeapBlock*)top;  /* a free block of 32 bytes, then the end marker */
    f->m0_next = (HeapBlock*)(top + 16 + 32 + 16); f->m4_bits = (0x21u << 26); f->mC_pcType = 0;
    top += 16 + 32;
    ((HeapBlock*)top)->m0_next = (HeapBlock*)(top + 16); ((HeapBlock*)top)->m4_bits = 1u << 21;
    out = fopen(argv[1], "wb");
    fwrite(__start_xgdata_testmod, 1, __stop_xgdata_testmod - __start_xgdata_testmod, out);
    fclose(out);
    out = fopen(argv[2], "wb");
    fwrite(heapArena, 1, sizeof(heapArena), out);
    fclose(out);
    return 0;
}
"""


def state_bytes(chunks: list[tuple[str, bytes]]) -> bytes:
    body = b"".join(bytes([len(t)]) + t.encode() + struct.pack("<I", len(d)) + d for t, d in chunks)
    head = bytearray(256)
    head[:8] = b"XGSTATE\0"
    struct.pack_into("<II", head, 8, 1, 256)
    head[32:35] = b"US\0"
    struct.pack_into("<Q", head, 200, len(body))
    struct.pack_into("<Q", head, 208, cn.state_hash(body))
    blocks = struct.pack("<II", len(body), len(body)) + body
    struct.pack_into("<Q", head, 224, len(blocks))
    return bytes(head) + blocks


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
            r = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(b), "--source", str(b)],
                               capture_output=True, text=True)
            check(r.returncode == 0, f"schema of build {name}: {r.stdout}{r.stderr}")
            subprocess.run([str(b / "xenogears"), str(b / "data.bin"), str(b / "heap.bin")], check=True)
            s = b / "state.xgs"
            s.write_bytes(state_bytes([("mem.testmod.data", (b / "data.bin").read_bytes()),
                                       ("mem.heap", (b / "heap.bin").read_bytes())]))
            builds.append(cn.Schema(b)); states.append(s)
        a, b = builds
        check({"List", "List*", "Target"} <= {e["name"] for e in a.heap_types.values()},
              f"the heap types named in the source ({sorted(e['name'] for e in a.heap_types.values())})")
        doc, enc = cn.encode(states[0], a)
        ids = {r["id"] for r in doc["records"]}
        check({"testmod:80100000", "testmod:80100100", "testmod:pc:pcHolders", "testmod:80100200"} <= ids, f"identities {ids}")
        rec = {r["id"]: r["v"] for r in doc["records"]}
        holder = rec["testmod:80100100"]
        check(holder["+8"] == {"v": "testmod:80100000", "p": [2, "+0"]}, f"a pointer into an array element ({holder['+8']})")
        check(holder["+10"]["fn"].endswith("tick"), "a function pointer by name")
        check(holder["+18"].get("str") == "fei", "a string by its content")
        check(holder["+20"]["l"].get("v") == "testmod:pc:pcHolders", f"an OT link by its target ({holder['+20']})")
        blocks = doc["heap"]["blocks"]
        check([("free" in x, x.get("end", False)) for x in blocks] ==
              [(False, False)] * 3 + [(True, False), (False, True)], f"the heap's blocks ({blocks})")
        check(blocks[0]["by"] == "source" and blocks[0]["parts"][0]["t"]["tid"] == a.type_names["Target"]
              and len(blocks[0]["parts"][0]["v"]) == 3, "a block of the type its allocation named, as many as fit")
        lst = blocks[1]["parts"][0]
        check(lst.get("counted") and len(lst["v"][0]["+8"]) == 3, f"a counted array, all its elements ({lst})")
        kinds = [("x" if "x" in p else p["t"] if isinstance(p["t"], str) else "struct") for p in blocks[2]["parts"]]
        check(kinds == ["x", "ptrs", "struct", "x"], f"a file's bytes with the recorded pointer and struct ({kinds})")
        check(blocks[2]["parts"][1]["v"][0] == {"hb": 1, "pt": 0, "e": 0, "p": ["+0"]}, f"the kept pointer, to its block ({blocks[2]['parts'][1]})")
        check(rec["testmod:80100300"] == {"hb": 1, "pt": 0, "e": 0, "p": ["+8", 2]},
              f"a pointer past a counted array's declared length ({rec['testmod:80100300']})")
        check(rec["testmod:80100308"] == {"hb": 2, "pt": 2, "e": 0, "p": ["+4"]},
              f"a pointer into a recorded struct's field ({rec['testmod:80100308']})")
        pick = rec["testmod:80100400"]
        check(pick[0]["+8"].get("sel") == "ptr" and pick[1]["+8"].get("sel") == "shorts",
              f"a union's view by its selector ({pick})")
        check(not enc.unknown, f"no unknown pointer ({dict(enc.unknown)})")
        lines, st = cn.crosscheck(states[0], a, states[1], b)
        check(st["checked"] > 0 and not lines, "decoded into build b as b has it:\n" + "\n".join(lines))
        doc2 = json.loads(json.dumps(doc))
        body, dec = cn.decode(doc2, a, states[0])
        _, orig = cn.sc.read_state(states[0])
        diff = [i for i in range(min(len(body), len(orig))) if body[i] != orig[i]][:8]
        check(body == orig and not dec.warn, f"a round trip in one build changes nothing ({dict(dec.warn)}, {len(body)} {len(orig)} {diff} {[(body[i], orig[i]) for i in diff]})")
    print(f"statecanon tests: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
