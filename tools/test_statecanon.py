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
import gzip, json, shutil, struct, subprocess, sys, tempfile
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


# The state rules (docs/CANONICAL_STATES.md 17.2): one program built before and after a set of changes to saved
# data, each change with its rule (include/pc_state.h's macros, empty here: the schema reads them from the source).
RULES_SOURCE = r"""
#include <stdio.h>
#include <string.h>
#define PC_STATE_REVISION @REV@
#define PC_STATE_WAS(rev, T, member, old)
#define PC_STATE_VAR_WAS(rev, var, old)
#define PC_STATE_TYPE_WAS(rev, T, old)
#define PC_STATE_FN_WAS(rev, fn, old)
#define PC_STATE_VALUES(rev, T, member, map)
#define PC_STATE_VAR_VALUES(rev, var, map)
#define PC_STATE_RESET(rev, T, member)
#define PC_STATE_VAR_RESET(rev, var)
#define PC_STATE_REMOVED(rev, T, value, names)
#define PC_STATE_VAR_REMOVED(rev, value, identities)
#define PC_STATE_DEFAULT(T, member, value)
#define PC_STATE_VAR_DEFAULT(var, value)
#define PC_STATE_HASHES(T, member)
#define DATA __attribute__((section("xgdata_testmod"), used))
typedef struct HeapBlock { struct HeapBlock* m0_next; unsigned int m4_bits; unsigned int mC_pcType; } HeapBlock;
typedef struct { const void* at; unsigned int type; int count; } StateRange;
@HASHES@
@BODY@
DATA HeapBlock* heapFirstBlockData = 0;
DATA struct { int count; int overflow; StateRange ranges[2]; } pcStateRanges = {0};
unsigned char heapArena[1024] __attribute__((aligned(16)));
extern char __start_xgdata_testmod[], __stop_xgdata_testmod[];
static unsigned int typeId(const char* s) {
    unsigned int h = 0x811C9DC5u;
    for (; *s; s++) if (*s != ' ') h = (h ^ (unsigned char)*s) * 0x01000193u;
    return h ? h : 1;
}
#define PC_HEAP_OF(T, size) (pending = typeId(#T), (size))
static unsigned int pending;
static unsigned char* top;
static void* allocate(int size) {
    HeapBlock* h = (HeapBlock*)top;
    size = (size + 15) & ~15;
    h->m0_next = (HeapBlock*)(top + 16 + size + 16);
    h->m4_bits = (10u << 21) | (0x20u << 26);
    h->mC_pcType = pending; pending = 0;
    top += 16 + size;
    return h + 1;
}
int main(int argc, char** argv) {
    FILE* out;
    top = heapArena;
    heapFirstBlockData = (HeapBlock*)(heapArena + 16);
@MAIN@
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
RULES_BEFORE = {"@REV@": "1", "@HASHES@": "PC_STATE_HASHES(StateRange, type)", "@BODY@": r"""
typedef struct {
    int pcOld;          /* renamed */
    int pcMode;         /* its values change meaning: 0 1 2 */
    int pcGone;         /* removed (an older build gives it 4) */
    int pcMood;         /* reset */
    short pcWide;       /* widened to int */
    unsigned char pcBytes[4];   /* elements widened to u16 */
    int pcPair;         /* becomes a struct of two shorts: by its bytes */
    int pcShape;        /* becomes a pointer: not carried */
} Keep;
typedef struct { unsigned char m0[4]; int m4_b; } Game;
typedef struct OldThing { int m0_a; struct OldThing* m8_self; } OldThing;
static void tickOld(void) {}
DATA Keep pcKeep = {11, 1, 4, 7, -3, {1, 2, 3, 255}, 0x00020001, 5};
DATA int pcOldVar = 41;
DATA int pcLevelVar = 0;
DATA Game D_80100000 = {{0x78, 0x56, 0x34, 0x12}, 9};
DATA void (*pcFn)(void) = tickOld;
DATA OldThing* pcThing = 0;
DATA int pcResetVar = 9;       /* reset */
DATA int pcDropped = 5;        /* removed (an older build gives it 21) */
""", "@MAIN@": r"""
    pcThing = allocate(PC_HEAP_OF(OldThing, sizeof(OldThing)));
    pcThing->m0_a = 6; pcThing->m8_self = pcThing;
    pcStateRanges.count = 1; pcStateRanges.ranges[0].at = pcThing; pcStateRanges.ranges[0].type = typeId("OldThing");
    pcStateRanges.ranges[0].count = 1;
"""}
RULES_AFTER = {"@REV@": "2", "@HASHES@": "PC_STATE_HASHES(StateRange, type)", "@BODY@": r"""
typedef struct {
    int pcNew;
    int pcMode;         /* 5 7 9 */
    int pcMood;
    int pcWide;
    unsigned short pcBytes[4];
    struct { short lo, hi; } pcPair;
    int* pcShape;
    int pcLevel;        /* new, default 3 */
} Keep;
PC_STATE_WAS(2, Keep, pcNew, "pcOld")
PC_STATE_VALUES(2, Keep, pcMode, ((0, 5), (1, 7), (2, 9)))
PC_STATE_REMOVED(2, Keep, 4, ("pcGone"))
PC_STATE_RESET(2, Keep, pcMood)
PC_STATE_DEFAULT(Keep, pcLevel, 3)
typedef struct { int m0_a; int m4_b; } Game;
typedef struct NewThing { int m0_a; struct NewThing* m8_self; } NewThing;
PC_STATE_TYPE_WAS(2, NewThing, "OldThing")
static void tickNew(void) {}
PC_STATE_FN_WAS(2, tickNew, "tickOld")
DATA Keep pcKeep = {11, 7, 0, -3, {1, 2, 3, 255}, {1, 2}, 0, 3};
DATA int pcNewVar = 41;
PC_STATE_VAR_WAS(2, pcNewVar, "pcOldVar")
DATA short pcLevelVar[2] = {0, 0};
PC_STATE_VAR_VALUES(2, pcLevelVar, ((0, 8)))
DATA Game D_80100000 = {0x12345678, 9};
DATA void (*pcFn)(void) = tickNew;
DATA NewThing* pcThing = 0;
DATA int pcResetVar = 8;
PC_STATE_VAR_RESET(2, pcResetVar)
DATA int pcAddedVar = 12;      /* new, default 12 */
PC_STATE_VAR_DEFAULT(pcAddedVar, 12)
PC_STATE_VAR_REMOVED(2, 21, ("testmod:pc:pcDropped"))
""", "@MAIN@": r"""
    pcThing = allocate(PC_HEAP_OF(NewThing, sizeof(NewThing)));
    pcThing->m0_a = 6; pcThing->m8_self = pcThing;
    pcStateRanges.count = 1; pcStateRanges.ranges[0].at = pcThing; pcStateRanges.ranges[0].type = typeId("NewThing");
    pcStateRanges.ranges[0].count = 1;
    pcLevelVar[0] = 8; pcLevelVar[1] = 8;
"""}


def rules_checks(d: Path, check) -> None:
    """Every rule kind and the automatic type changes, both ways: before -> after (a newer reader: its rules forward),
    after -> before (a newer writer: its rules backward); and the gate: an unknown macro, a rule naming nothing."""
    builds = {}
    for name, sub in (("before", RULES_BEFORE), ("after", RULES_AFTER)):
        b = d / f"rules-{name}"
        b.mkdir()
        (b / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
        text = RULES_SOURCE
        for k, v in sub.items():
            text = text.replace(k, v)
        (b / "thing.c").write_text(text)
        subprocess.run(["cc", "-g", "-O0", "-no-pie", "-w", "-o", str(b / "xenogears"), str(b / "thing.c")], check=True)
        r = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(b), "--source", str(b)],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"rules: the schema of {name}: {r.stdout[-1500:]}{r.stderr[-800:]}")
        subprocess.run([str(b / "xenogears"), str(b / "data.bin"), str(b / "heap.bin")], check=True)
        s = b / "state.xgs"
        s.write_bytes(state_bytes([("mem.testmod.data", (b / "data.bin").read_bytes()),
                                   ("mem.heap", (b / "heap.bin").read_bytes())]))
        builds[name] = (b, cn.Schema(b), s)
    (bb, sb, stb), (ab, sa, sta) = builds["before"], builds["after"]
    rules = sa.rules
    kinds = sorted({(r["kind"], next(k for k in ("field", "var", "typename", "fn", "tag", "fields", "vars") if k in r))
                    for r in rules["list"]})
    check(sb.rules["revision"] == 1 and rules["revision"] == 2 and rules["language"] == 1,
          f"rules: the revisions and the language ({sb.rules['revision']}, {rules['revision']})")
    check(sb.rules["list"] and all(r["kind"] == "hashes" for r in sb.rules["list"]) and
          kinds == sorted([("default", "field"), ("default", "var"), ("hashes", "field"), ("removed", "fields"),
                           ("removed", "vars"), ("reset", "field"), ("reset", "var"), ("values", "field"),
                           ("values", "var"), ("was", "field"), ("was", "fn"), ("was", "typename"), ("was", "var")]),
          f"rules: the kinds in the schema ({kinds})")
    was = next(r for r in rules["list"] if r["kind"] == "was" and "field" in r)
    check(was["field"] == "pc:pcNew" and was["old"] == "pc:pcOld" and was["rev"] == 2 and was["tids"] and
          was["type"] == "Keep", f"rules: a member's rename, by its keys and the type's entries ({was})")

    # the C converter (port/src/system/stateconvert.c), built here: it must write the same bytes and count the same
    exe = d / "stateconverttest"
    sysdir = ROOT / "port/src/system"
    built = subprocess.run(["cc", "-std=gnu17", "-O1", "-w", "-o", str(exe), str(sysdir / "test/stateconverttest.c"),
                            *(str(sysdir / f) for f in ("stateconvert.c", "stateschema.c", "json.c", "statecodec.c"))],
                           capture_output=True, text=True)
    check(built.returncode == 0, f"rules: the C converter builds ({built.stderr[-600:]})")

    def convert(src, ssrc, tmpl, sdst):
        doc, enc = cn.encode(src, ssrc)
        doc = json.loads(json.dumps(doc))
        body, dec = cn.decode(doc, sdst, tmpl, writer=ssrc)
        out = d / "converted.xgs"
        cn.write_state(tmpl, body, out)
        got = d / "converted-c.xgs"
        r = subprocess.run([str(exe), "convert", str(src), str(ssrc.exe.parent / "state_schema.json"), str(tmpl),
                            str(sdst.exe.parent / "state_schema.json"), str(got)], capture_output=True, text=True)
        rows = {"encode": {}, "decode": {}, "rule": {}}
        for line in r.stdout.splitlines()[1:]:
            side, n, text = line.split(" ", 2)
            rows[side][text] = int(n)
        check(r.returncode == 0 and got.read_bytes() == out.read_bytes() and
              (rows["encode"], rows["decode"], rows["rule"]) == (dict(enc.warn), dict(dec.warn), dict(dec.applied)),
              f"rules: the C converter as statecanon ({ssrc.exe.parent.name} into {sdst.exe.parent.name}): "
              f"{r.stderr.strip()[:300]} C {rows}, statecanon {dict(dec.warn)} {dict(dec.applied)}")
        back, _ = cn.encode(out, sdst)  # what the reader then holds, by identity
        return {r["id"]: r["v"] for r in back["records"] if "v" in r}, back, dec

    fwd, fdoc, fdec = convert(stb, sb, sta, sa)
    keep = fwd["testmod:pc:pcKeep"]
    check(keep["pc:pcNew"] == 11, f"forward: a renamed member ({keep})")
    check(keep["pc:pcMode"] == 7, f"forward: a value mapped old to new (1 -> 7) ({keep['pc:pcMode']})")
    check(keep["pc:pcMood"] == 0, f"forward: a reset member not carried ({keep['pc:pcMood']})")
    check(keep["pc:pcLevel"] == 3, f"forward: an added member gets its default ({keep['pc:pcLevel']})")
    check(keep["pc:pcWide"] == -3, f"forward: a widened integer keeps its value ({keep['pc:pcWide']})")
    check(bytes.fromhex(keep["pc:pcBytes"]["x"]) == bytes([1, 0, 2, 0, 3, 0, 255, 0]),
          f"forward: an integer array's elements widened by value ({keep['pc:pcBytes']})")
    check(keep["pc:pcPair"] == {"pc:lo": 1, "pc:hi": 2}, f"forward: an int become a struct, by its bytes ({keep['pc:pcPair']})")
    check(keep["pc:pcShape"] is None, f"forward: an int become a pointer, not carried ({keep['pc:pcShape']})")
    check(fwd.get("testmod:pc:pcNewVar") == 41, "forward: a renamed variable")
    check(fwd.get("testmod:pc:pcResetVar") == 0 and fwd.get("testmod:pc:pcAddedVar") == 12 and
          fdec.warn.get("variable reset by a state rule (not carried)") == 1 and
          fdec.warn.get("variable not in the state (given its default)") == 1,
          f"forward: a reset variable not carried, an added one its default ({dict(fdec.warn)})")
    check(bytes.fromhex(fwd["testmod:pc:pcLevelVar"]["x"]) == bytes([8, 0, 0, 0]),
          f"forward: a variable's int become a short array, by value; its first element mapped 0 -> 8 "
          f"({fwd['testmod:pc:pcLevelVar']})")
    check(fwd["testmod:80100000"]["+0"] == 0x12345678, f"forward: a game member retyped, by its PS1 bytes ({fwd['testmod:80100000']})")
    check(fwd["testmod:pc:pcFn"]["fn"] == "fn:tickNew", f"forward: a pointer to a renamed function ({fwd['testmod:pc:pcFn']})")
    blk = fdoc["heap"]["blocks"][0]
    check(blk["parts"][0]["t"]["name"] == "NewThing" and blk.get("pct") == cn.type_hash("NewThing"),
          f"forward: a heap block of a renamed type, its header's hash the new name's ({blk})")
    rng = fwd["testmod:pc:pcStateRanges"]["pc:ranges"][0]
    check(rng["pc:type"] == cn.type_hash("NewThing"), f"forward: a recorded range's type hash (PC_STATE_HASHES) ({rng})")
    check(fdec.warn.get("field reset by a state rule (not carried)") == 1 and
          fdec.warn.get("value of another type in the state (not carried)") == 1 and
          fdec.applied.get("renamed by a state rule") == 3 and fdec.applied.get("values mapped by a state rule") == 2 and
          fdec.applied.get("carried by its PS1 bytes") == 1 and fdec.applied.get("type hash remapped by a state rule") == 2,
          f"forward: what the rules did, counted ({dict(fdec.warn)}, {dict(fdec.applied)})")

    bwd, bdoc, bdec = convert(sta, sa, stb, sb)
    keep = bwd["testmod:pc:pcKeep"]
    check(keep["pc:pcOld"] == 11, f"backward: a renamed member, by the newer writer's rule ({keep})")
    check(keep["pc:pcMode"] == 1, f"backward: a value mapped new to old (7 -> 1) ({keep['pc:pcMode']})")
    check(keep["pc:pcGone"] == 4, f"backward: a member the newer build removed gets its rule's value ({keep['pc:pcGone']})")
    check(keep["pc:pcMood"] == 0, f"backward: a reset member not carried ({keep['pc:pcMood']})")
    check(keep["pc:pcWide"] == -3 and bytes.fromhex(keep["pc:pcBytes"]["x"]) == bytes([1, 2, 3, 255]),
          f"backward: narrowed back by value ({keep['pc:pcWide']}, {keep['pc:pcBytes']})")
    check(keep["pc:pcPair"] == 0x00020001 and keep["pc:pcShape"] == 0, f"backward: the struct by its bytes, the pointer "
          f"not carried ({keep['pc:pcPair']:#x}, {keep['pc:pcShape']})")
    check(bwd.get("testmod:pc:pcResetVar") == 0 and bwd.get("testmod:pc:pcDropped") == 21 and
          bdec.warn.get("variable removed by a later build (given the value its rule names)") == 1,
          f"backward: a reset variable not carried, a removed one the newer writer's value ({dict(bdec.warn)})")
    check(bwd.get("testmod:pc:pcOldVar") == 41 and bwd.get("testmod:pc:pcLevelVar") == 0,
          f"backward: a renamed variable; a mapped one 8 -> 0 ({bwd.get('testmod:pc:pcLevelVar')})")
    check(bwd["testmod:80100000"]["+0"] == {"x": "78563412", "w": 1}, f"backward: the game member's PS1 bytes ({bwd['testmod:80100000']})")
    check(bwd["testmod:pc:pcFn"]["fn"] == "fn:tickOld", "backward: the function by its old name")
    blk = bdoc["heap"]["blocks"][0]
    check(blk["parts"][0]["t"]["name"] == "OldThing" and blk.get("pct") == cn.type_hash("OldThing") and
          bwd["testmod:pc:pcStateRanges"]["pc:ranges"][0]["pc:type"] == cn.type_hash("OldThing"),
          f"backward: the heap block and the recorded range by the old type name ({blk})")
    # the same build both ways: nothing applies
    same, _, sdec = convert(sta, sa, sta, sa)
    check(not sdec.applied and not sdec.warn and same["testmod:pc:pcKeep"]["pc:pcMode"] == 7, "the same revision: no rule")

    # a tag's rename, and a renamed game function found by its retail address (schemas edited: the program has no
    # host part and no retail function)
    raw = json.load(gzip.open(ab / "state_schema.json.gz", "rt"))
    raw["parts"] = {"hw": "system/hw.c"}
    raw["rules"]["list"].append({"kind": "was", "rev": 2, "tag": "hw.newName", "old": "hw.oldName", "at": "-"})
    raw["functions"] = [{**f, "retail": 0x80010000} if f["name"] == "tickNew" else f for f in raw["functions"]]
    rs = cn.Rules({"revision": 1, "list": []}, raw["rules"])
    check(rs.forward and rs.tag_source("hw.newName") == "hw.oldName" and rs.tag_source("hw.other") == "hw.other",
          "a tag's rename, forward")
    rs = cn.Rules(raw["rules"], {"revision": 1, "list": []})
    check(rs.backward and rs.tag_source("hw.oldName") == "hw.newName" and rs.tag_source("hw.newName") is None,
          "a tag's rename, backward (the newer writer's own name is another thing to the older reader)")
    edited = d / "rules-edited"
    edited.mkdir()
    (edited / "state_schema.json.gz").write_bytes(gzip.compress(json.dumps(raw).encode()))
    se = cn.Schema(edited)
    check(se.fn_address("fn:80010000:func_80010000") == se.fn_address("fn:tickNew") and
          se.fn_address("fn:80020000:func_80020000") is None,
          "a renamed game function by its retail address alone")
    rs = cn.Rules({"revision": 0, "list": [{"kind": "someday"}]}, {"revision": 3, "list": [{"kind": "someday", "rev": 2}]})
    check(rs.unknown == 1 and not rs.active, "a rule of a kind this converter does not know: ignored, counted")

    # the gate: a macro this tool does not know, a rule naming nothing, an old name still in use
    bad = d / "rules-bad"
    bad.mkdir()
    (bad / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
    text = RULES_SOURCE
    for k, v in RULES_AFTER.items():
        text = text.replace(k, v)
    text = text.replace("PC_STATE_DEFAULT(Keep, pcLevel, 3)", "PC_STATE_DEFAULT(Keep, pcLevel, 3)\n"
                        "PC_STATE_SOMEDAY(2, Keep, pcLevel)\nPC_STATE_WAS(2, Keep, pcNope, \"pcOld2\")\n"
                        "PC_STATE_WAS(2, Keep, pcMode, \"pcMood\")\nPC_STATE_VALUES(3, Keep, pcMode, ((1, 2)))\n"
                        "PC_STATE_DEFAULT(Keep, pcPair, 1)")
    (bad / "thing.c").write_text(text.replace("#define PC_STATE_HASHES(T, member)",
                                              "#define PC_STATE_HASHES(T, member)\n#define PC_STATE_SOMEDAY(a, b, c)"))
    subprocess.run(["cc", "-g", "-O0", "-no-pie", "-w", "-o", str(bad / "xenogears"), str(bad / "thing.c")], check=True)
    r = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(bad), "--source", str(bad)],
                       capture_output=True, text=True)
    found = [p for p in ("PC_STATE_SOMEDAY", "no member pcNope", "is a member of Keep", "revision 3",
                         "not an integer member") if p in r.stdout]
    check(r.returncode == 1 and len(found) == 5, f"the gate refuses unknown macros and bad rules ({found}):\n"
          f"{r.stdout[-1200:]}")


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
        rules_checks(d, check)
    print(f"statecanon tests: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
