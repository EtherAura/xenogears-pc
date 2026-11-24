/* The state converter (stateconvert.h): tools/statecanon.py convert in C.
 *
 *   stateconverttest       the checks, on two small layouts of one program
 *   stateconverttest convert A.xgs SCHEMA_A T.xgs SCHEMA_B OUT.xgs [--stackless] [--moment]
 *                          A's state as B's, the template T giving B's chunk order and host parts,
 *                          written as statecanon.py write_state writes one (T's header and thumbnail,
 *                          the body stored unpacked); the strings come from each schema's "exe".
 *                          --moment: T is B's state at another moment (stateConvertTemplate first, as a
 *                          slot's load does).
 *                          tools/test_stateconvert_c.py compares it with statecanon's own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include "../statecodec.h"
#include "../stateconvert.h"
#include "../stateschema.h"

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* ---- bodies: chunks of tag, u32 size, bytes ------------------------------------------------------ */

typedef struct {
    uint8_t* p;
    size_t n, cap;
} Bytes;

static void bytesAdd(Bytes* b, const void* data, size_t n) {
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n) cap *= 2;
        b->p = realloc(b->p, cap);
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, data, n);
    b->n += n;
}

static void chunk(Bytes* b, const char* tag, const void* data, uint32_t n) {
    uint8_t tl = (uint8_t)strlen(tag);
    bytesAdd(b, &tl, 1);
    bytesAdd(b, tag, tl);
    uint8_t size[4] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
    bytesAdd(b, size, 4);
    bytesAdd(b, data, n);
}

static void put64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* ---- two layouts of one small program ------------------------------------------------------------ */

#define SHIFT 0x1000000ull /* B is linked 16 MiB above A, as build/pc/schema2 is */
#define PAIR_HASH 0x1234abcdu

/* The schema of layout A (shift 0) or B (shift SHIFT, layoutB). A has "gone", B "fresh" in its place.
 * Another shift moves every address by it (modulo 2^64): the same layout linked elsewhere. */
static char* schemaText(uint64_t shift, int layoutB) {
    static const char* const vars[][4] = {
        /* name, offset in main.data, type, retail */
        {"counter", "0", "20", "2147549184"},  {"ptr", "8", "30", "2147549192"},      {"pair", "16", "10", "2147549200"},
        {"fnptr", "32", "40", "2147549216"},   {"str", "40", "50", "2147549224"},     {"safePointId", "48", "20", "null"},
        {"heapFirstBlockData", "56", "80", "null"}, {"heapPtr", "64", "30", "2147549248"}, {"wild", "80", "30", "2147549264"},
    };
    char* out = malloc(16384);
    size_t n = 0;
    n += (size_t)snprintf(out + n, 16384 - n,
        "{\"version\": \"us\", \"exe\": \"\","
        " \"types\": {\"10\": {\"k\": \"struct\", \"size\": 16, \"name\": \"Pair\", \"owner\": \"game\", \"fields\": ["
        "    {\"name\": \"m0_a\", \"off\": 0, \"type\": \"20\", \"retail\": 0},"
        "    {\"name\": \"m8_p\", \"off\": 8, \"type\": \"30\", \"retail\": 4}]},"
        "  \"20\": {\"k\": \"int\", \"size\": 4, \"signed\": true, \"name\": \"s32\"},"
        "  \"30\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false, \"to\": \"20\"},"
        "  \"40\": {\"k\": \"ptr\", \"size\": 8, \"fn\": true},"
        "  \"50\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false, \"to\": \"60\"},"
        "  \"60\": {\"k\": \"int\", \"size\": 1, \"signed\": true, \"name\": \"char\"},"
        "  \"80\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false},"
        "  \"70\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false, \"to\": \"%s\"},"
        "  \"%s\": {\"k\": \"struct\", \"size\": 16, \"name\": \"struct <anon>\", \"owner\": \"port\", \"fields\": ["
        "    {\"name\": \"at\", \"off\": 0, \"type\": \"20\", \"retail\": null},"
        "    {\"name\": \"arg\", \"off\": 8, \"type\": \"30\", \"retail\": null}]}},"
        " \"type_names\": {\"Pair\": \"10\", \"s32\": \"20\"},"
        " \"variables\": [",
        /* hw.queue's element: an unnamed struct (MinGW's LTO drops its typedef), at another DWARF offset in B */
        layoutB ? "91" : "90", layoutB ? "91" : "90");
    for (size_t i = 0; i < sizeof vars / sizeof vars[0]; i++) {
        n += (size_t)snprintf(out + n, 16384 - n,
            "{\"name\": \"%s\", \"addr\": %llu, \"size\": %s, \"module\": \"main\", \"section\": \"data\", \"retail\": %s,"
            " \"platform\": null, \"tid\": \"%s\"}, ",
            vars[i][0], (unsigned long long)(0x1000 + shift + strtoull(vars[i][1], NULL, 10)),
            strcmp(vars[i][2], "10") == 0 ? "16" : strcmp(vars[i][2], "20") == 0 ? "4" : "8", vars[i][3], vars[i][2]);
    }
    n += (size_t)snprintf(out + n, 16384 - n,
        "{\"name\": \"%s\", \"addr\": %llu, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": %s,"
        " \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"konst\", \"addr\": %llu, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null,"
        "   \"platform\": null, \"tid\": \"20\", \"const\": true},"
        "  {\"name\": \"thing\", \"addr\": %llu, \"size\": 4, \"module\": null, \"section\": null, \"retail\": null,"
        "   \"platform\": \"system/hw.c\", \"tid\": \"20\"},"
        "  {\"name\": \"queue\", \"addr\": %llu, \"size\": 8, \"module\": null, \"section\": null, \"retail\": null,"
        "   \"platform\": \"system/hw.c\", \"tid\": \"70\"}],"
        " \"functions\": [{\"name\": \"func_a\", \"addr\": %llu, \"side\": \"game\", \"retail\": 2147614720}],"
        " \"symbols\": [[\"func_a\", %llu, 16, \"T\"], [\"text_hello\", %llu, 0, \"r\"]],"
        " \"ro\": [[%llu, %llu, 0]],"
        " \"sections\": {\"main.data\": [%llu, %llu]},"
        " \"regions\": {\"heapArena\": [%llu, %llu], \"gameStack\": [%llu, %llu], \"pcScratchpad\": [%llu, %llu]},"
        " \"parts\": {\"hw\": \"system/hw.c\"},"
        " \"heap_types\": {\"%08x\": {\"name\": \"Pair\", \"spec\": {\"tid\": \"10\"}}},"
        " \"counted\": {}, \"unions\": {}}",
        layoutB ? "fresh" : "gone", (unsigned long long)(0x1048 + shift), layoutB ? "2147549296" : "2147549256",
        (unsigned long long)(0x1058 + shift), (unsigned long long)(0x1060 + shift),
        (unsigned long long)(0x5000 + shift), (unsigned long long)(0x400000 + shift), (unsigned long long)(0x400000 + shift),
        (unsigned long long)(0x2000 + shift), (unsigned long long)(0x2000 + shift), (unsigned long long)(0x2100 + shift),
        (unsigned long long)(0x1000 + shift), (unsigned long long)(0x1100 + shift), (unsigned long long)(0x10000 + shift),
        (unsigned long long)(0x10100 + shift), (unsigned long long)(0x20000 + shift), (unsigned long long)(0x21000 + shift),
        (unsigned long long)(0x30000 + shift), (unsigned long long)(0x30400 + shift), PAIR_HASH);
    return out;
}

/* Layout A's state (template 0), or a template of layout B: the same chunks, B's own bytes in another
 * order (which the converter must ignore but for their order and sizes). */
static void stateBody(Bytes* b, int template, uint32_t safePoint) {
    uint8_t data[0x100] = {0}, heap[0x100] = {0};
    if (!template) {
        put32(data + 0, 0x11223344);                      /* counter */
        put64(data + 8, 0x1000);                          /* ptr: &counter */
        put32(data + 16, 0xFFFFFFFBu);                    /* pair.m0_a: -5 */
        put64(data + 24, 0x1010);                         /* pair.m8_p: &pair.m0_a */
        put64(data + 32, 0x400000);                       /* fnptr: func_a */
        put64(data + 40, 0x2000);                         /* str: "hello" */
        put32(data + 48, safePoint);                      /* safePointId */
        put64(data + 56, 0x10010);                        /* heapFirstBlockData */
        put64(data + 64, 0x10018);                        /* heapPtr: the heap Pair's m8_p */
        put64(data + 80, 0xDEAD0000);                     /* wild: nothing known */
        put32(data + 72, 0x99);                           /* gone */
        put32(data + 88, 0x77777777);                     /* konst: the writer's own constant, not carried */
        data[0x80] = 0x5A;                                /* outside every variable: not carried */
        put64(heap + 0, 0x10030);                         /* block 0: next */
        put32(heap + 8, (2u << 21) | 0x123);              /* owner 2, a caller */
        put32(heap + 12, PAIR_HASH);                      /* the type the allocation named: Pair */
        put32(heap + 16, 7);                              /* Pair.m0_a */
        put64(heap + 24, 0x1000);                         /* Pair.m8_p: &counter */
        put64(heap + 32, 0);                              /* the end: next */
        put32(heap + 40, (1u << 21) | 0x55);              /* the end marker */
    } else {
        memset(data, 0xEE, sizeof data);
        memset(heap, 0xEE, sizeof heap);
    }
    uint8_t sp[8] = {1, 2, 3, 4, 5, 6, 7, 8}, stack[16] = {9, 9, 9, 9}, thing[4] = {0x0D, 0x0C, 0x0B, 0x0A};
    uint8_t raw[5] = {'a', 'b', 'c', 'd', 'e'};
    uint8_t queue[32];
    memset(queue, 0xEE, sizeof queue);
    if (!template) { /* the array hw.c's queue points at: {at, arg} twice */
        memset(queue, 0, sizeof queue);
        put32(queue + 0, 3);
        put64(queue + 8, 0x1000); /* &counter */
        put32(queue + 16, 0xFFFFFFFFu);
    }
    if (template) {
        chunk(b, "zz.raw", raw, 5);
        chunk(b, "mem.main.data", data, sizeof data);
        chunk(b, "mem.heap", heap, sizeof heap);
        chunk(b, "mem.stack.sp", sp, 8);
        chunk(b, "mem.stack", stack, 16);
        chunk(b, "hw.thing", raw, 4);
        chunk(b, "hw.queue", queue, sizeof queue);
    } else {
        chunk(b, "mem.main.data", data, sizeof data);
        chunk(b, "mem.heap", heap, sizeof heap);
        chunk(b, "mem.stack.sp", sp, 8);
        chunk(b, "mem.stack", stack, 16);
        chunk(b, "hw.thing", thing, 4);
        chunk(b, "hw.queue", queue, sizeof queue);
        chunk(b, "zz.raw", raw, 3);
    }
}

/* What statecanon makes of A's state in B's layout. */
static void expectedBody(Bytes* b, int stackless) {
    uint8_t data[0x100] = {0}, heap[0x100] = {0};
    put32(data + 0, 0x11223344);
    put64(data + 8, 0x1000 + SHIFT);
    put32(data + 16, 0xFFFFFFFBu);
    put64(data + 24, 0x1010 + SHIFT);
    put64(data + 32, 0x400000 + SHIFT);
    put64(data + 40, 0x2000 + SHIFT);
    put32(data + 48, 5);
    put64(data + 56, 0x10010 + SHIFT);
    put64(data + 64, 0x10018 + SHIFT);
    put64(data + 80, 0xDEAD0000); /* a number after all: same build only */
    /* fresh at 72 left zero; outside every variable (the writer's 0x5A at 0x80 among them) and konst: the
     * reader's own bytes, as its template has them (a jump table follows its build's code) */
    memset(data + 4, 0xEE, 4);
    memset(data + 52, 0xEE, 4);
    memset(data + 76, 0xEE, 4);
    memset(data + 88, 0xEE, 0x100 - 88);
    put64(heap + 0, 0x10030 + SHIFT);
    put32(heap + 8, 2u << 21); /* the caller left zero */
    put32(heap + 12, PAIR_HASH);
    put32(heap + 16, 7);
    put64(heap + 24, 0x1000 + SHIFT);
    put32(heap + 40, 1u << 21);
    uint8_t raw[5] = {'a', 'b', 'c', 0, 0}, thing[4] = {0x0D, 0x0C, 0x0B, 0x0A}, zero[8] = {0};
    uint8_t sp[8] = {1, 2, 3, 4, 5, 6, 7, 8}, stack[16] = {9, 9, 9, 9}, queue[32] = {0};
    put32(queue + 0, 3);
    put64(queue + 8, 0x1000 + SHIFT); /* the element of B's own type: its pointer moved */
    put32(queue + 16, 0xFFFFFFFFu);
    chunk(b, "zz.raw", raw, 5);
    chunk(b, "mem.main.data", data, sizeof data);
    chunk(b, "mem.heap", heap, sizeof heap);
    if (stackless) {
        chunk(b, "mem.stack.sp", zero, 8);
        chunk(b, "mem.stack", zero, 0);
    } else {
        chunk(b, "mem.stack.sp", sp, 8);
        chunk(b, "mem.stack", stack, 16);
    }
    chunk(b, "hw.thing", thing, 4);
    chunk(b, "hw.queue", queue, sizeof queue);
}

static int64_t warningCount(const StateConvertWarning* w, int32_t n, const char* text) {
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(w[i].text, text) == 0) return w[i].count;
    }
    return 0;
}

/* A writer's strings that count what the encoding asks for. */
typedef struct {
    StateStrings inner;
    int count;
    uint64_t addr;
} Asked;

static int askedAt(void* user, uint64_t addr, const uint8_t** text, size_t* len) {
    Asked* a = user;
    a->count++;
    a->addr = addr;
    return a->inner.at(a->inner.user, addr, text, len);
}

static void convertChecks(void) {
    char err[300];
    char* ta = schemaText(0, 0);
    char* tb = schemaText(SHIFT, 1);
    StateSchema* sa = stateSchemaParse(ta, strlen(ta), err, sizeof err);
    StateSchema* sb = stateSchemaParse(tb, strlen(tb), err, sizeof err);
    check(sa && sb, "the two layouts' schemas parse");
    free(ta);
    free(tb);
    if (!sa || !sb) {
        printf("  %s\n", err);
        stateSchemaFree(sa);
        stateSchemaFree(sb);
        return;
    }
    uint8_t ro[0x100 + 4096] = {0};
    memcpy(ro, "hello", 6);
    StateRoSegment segA = {0x2000, 0x2100, ro, sizeof ro}, segB = {0x2000 + SHIFT, 0x2100 + SHIFT, ro, sizeof ro};
    StateRoStrings* stra = stateRoStringsNew(&segA, 1);
    StateRoStrings* strb = stateRoStringsNew(&segB, 1);
    const uint8_t* text;
    size_t len;
    uint64_t at = 0;
    StateStrings s = stateRoStrings(strb);
    check(s.at(s.user, 0x2001 + SHIFT, &text, &len) && len == 4 && memcmp(text, "ello", 4) == 0, "a string at an address");
    check(!s.at(s.user, 0x2100 + SHIFT, &text, &len), "no string outside the ranges");
    check(s.find(s.user, (const uint8_t*)"hello", 5, &at) && at == 0x2000 + SHIFT, "a string's address");
    check(!s.find(s.user, (const uint8_t*)"ello", 4, &at), "only strings that start after a NUL");

    for (int stackless = 1; stackless >= 0; stackless--) {
        Bytes a = {0}, t = {0}, want = {0};
        stateBody(&a, 0, 5);
        stateBody(&t, 1, 0);
        expectedBody(&want, stackless);
        StateConvertSide wa = {a.p, a.n, sa, stateRoStrings(stra)};
        StateConvertSide wb = {t.p, t.n, sb, stateRoStrings(strb)};
        StateConvertResult r;
        int ok = stateConvert(&wa, &wb, stackless, &r, err, sizeof err);
        check(ok, stackless ? "a stackless conversion" : "a conversion with the stack");
        if (!ok) printf("  %s\n", err);
        check(ok && r.size == want.n && memcmp(r.body, want.p, want.n) == 0,
              "the reader's body: variables, pointers to variables, the heap, functions and strings moved; raw "
              "chunks and host parts kept, a pointed-at array of an unnamed struct in the reader's type; a module "
              "section's bytes outside every variable the reader's own; the template's order");
        if (ok && (r.size != want.n || memcmp(r.body, want.p, want.n) != 0)) {
            for (size_t i = 0; i < r.size && i < want.n; i++) {
                if (r.body[i] != want.p[i]) {
                    printf("  first difference at byte %zu: %02x, want %02x (sizes %zu, %zu)\n", i, r.body[i], want.p[i],
                           r.size, want.n);
                    break;
                }
            }
        }
        check(ok && r.safePoint == 5, "the safe point it was saved at");
        check(ok && warningCount(r.decodeWarnings, r.decodeWarningCount, "variable not in the state (left zero)") == 1,
              "a variable the writer has not: left zero, warned");
        check(ok && warningCount(r.encodeWarnings, r.encodeWarningCount,
                                 "pointer to nothing known (kept as a number: same build only)") == 1,
              "a pointer to nothing known: warned");
        stateConvertResultFree(&r);
        free(a.p);
        free(t.p);
        free(want.p);
    }

    /* load biases: A linked 0x800 below where it ran and B at A's addresses, each schema rebased by its
     * process's bias (stateSchemaParseAt), convert as the schemas of the addresses they ran at do */
    {
        char* tal = schemaText((uint64_t)0 - 0x800, 0);
        char* tbl = schemaText(0, 1);
        StateSchema* sal = stateSchemaParseAt(tal, strlen(tal), 0x800, err, sizeof err);
        StateSchema* sbl = stateSchemaParseAt(tbl, strlen(tbl), SHIFT, err, sizeof err);
        free(tal);
        free(tbl);
        check(sal && sbl && stateSchemaBias(sbl) == SHIFT, "rebased schemas parse");
        uint64_t lo, hi, fo;
        if (sal && sbl) {
            stateSchemaReadOnlyAt(sbl, 0, &lo, &hi, &fo);
            check(lo == 0x2000 + SHIFT && fo == 0 && stateSchemaVarAt(sal, 0)->addr == 0x1000 &&
                      stateSchemaFnAddress(sbl, "fn:80010000:func_a") == 0x400000 + SHIFT &&
                      stateSchemaSection(sbl, "main.data").lo == 0x1000 + SHIFT &&
                      stateSchemaRegion(sal, "heapArena").lo == 0x10000,
                  "a rebased schema's addresses: variables, functions, sections, regions, read-only ranges (not "
                  "their file offsets)");
        }
        Bytes a = {0}, t = {0}, want = {0};
        stateBody(&a, 0, 5);
        stateBody(&t, 1, 0);
        expectedBody(&want, 1);
        StateConvertSide wa = {a.p, a.n, sal, stateRoStrings(stra)};
        StateConvertSide wb = {t.p, t.n, sbl, stateRoStrings(strb)};
        StateConvertResult r;
        int ok = sal && sbl && stateConvert(&wa, &wb, 1, &r, err, sizeof err);
        check(ok && r.size == want.n && memcmp(r.body, want.p, want.n) == 0,
              "a conversion between rebased schemas: as between the schemas of where the builds ran");
        if (ok) stateConvertResultFree(&r);
        free(a.p);
        free(t.p);
        free(want.p);
        stateSchemaFree(sal);
        stateSchemaFree(sbl);
    }

    /* the encoding alone (a portable slot's strings table): the strings the writer's pointers reach */
    {
        Bytes a = {0};
        stateBody(&a, 0, 5);
        Asked asked = {stateRoStrings(stra), 0, 0};
        StateConvertSide wa = {a.p, a.n, sa, {&asked, askedAt, NULL}};
        StateConvertResult r;
        int ok = stateConvertEncode(&wa, &r, err, sizeof err);
        check(ok && r.safePoint == 5 && r.body == NULL, "the encoding alone: the safe point, no body");
        check(asked.count == 1 && asked.addr == 0x2000, "the encoding asks for each string a pointer reaches");
        check(ok && warningCount(r.encodeWarnings, r.encodeWarningCount,
                                 "pointer to nothing known (kept as a number: same build only)") == 1,
              "the encoding's warnings");
        stateConvertResultFree(&r);
        free(a.p);
    }

    /* a template of another moment: its raw chunk at the writer's size, the others as they are */
    {
        Bytes a = {0}, t = {0}, want = {0};
        stateBody(&a, 0, 5);
        stateBody(&t, 1, 0);
        uint8_t* tp = NULL;
        size_t tn = 0;
        int ok = stateConvertTemplate(t.p, t.n, sb, a.p, a.n, &tp, &tn, err, sizeof err);
        check(ok && tn == t.n - 2 && tp[0] == 6 && memcmp(tp + 1, "zz.raw", 6) == 0 && tp[7] == 3 &&
                  memcmp(tp + 11, "\0\0\0", 3) == 0 && memcmp(tp + 14, t.p + 16, t.n - 16) == 0,
              "a template of another moment: the raw chunk at the writer's size, zeros; the others as they are");
        StateConvertSide wa = {a.p, a.n, sa, stateRoStrings(stra)};
        StateConvertSide wb = {tp, tn, sb, stateRoStrings(strb)};
        StateConvertResult r;
        expectedBody(&want, 1);
        ok = ok && stateConvert(&wa, &wb, 1, &r, err, sizeof err);
        /* the expected body with the raw chunk as the writer has it: "abc" */
        check(ok && r.size == want.n - 2 && memcmp(r.body, "\x06zz.raw\x03\0\0\0abc", 14) == 0 &&
                  memcmp(r.body + 14, want.p + 16, want.n - 16) == 0,
              "converted through it: the writer's raw bytes whole");
        if (ok) stateConvertResultFree(&r);
        free(tp);
        free(a.p);
        free(t.p);
        free(want.p);
    }

    /* a reader whose queue points at no struct: the array left empty, warned */
    {
        char* tq = schemaText(SHIFT, 1);
        char* to = strstr(tq, "\"to\": \"91\"");
        if (to) memcpy(to + 7, "20", 2); /* a pointer to s32 */
        StateSchema* sq = to ? stateSchemaParse(tq, strlen(tq), err, sizeof err) : NULL;
        free(tq);
        Bytes a = {0}, t = {0}, want = {0};
        stateBody(&a, 0, 5);
        stateBody(&t, 1, 0);
        expectedBody(&want, 1);
        StateConvertSide wa = {a.p, a.n, sa, stateRoStrings(stra)};
        StateConvertSide wb = {t.p, t.n, sq, stateRoStrings(strb)};
        StateConvertResult r;
        int ok = sq && stateConvert(&wa, &wb, 1, &r, err, sizeof err);
        check(ok && r.size == want.n - 32 && memcmp(r.body, want.p, want.n - 36) == 0 && /* (up to its size: 0) */
                  memcmp(r.body + r.size - 4, "\0\0\0\0", 4) == 0 &&
                  warningCount(r.decodeWarnings, r.decodeWarningCount,
                               "array part not a struct pointer in this build (left empty)") == 1,
              "a pointed-at array the reader's variable types otherwise: left empty, warned");
        if (ok) stateConvertResultFree(&r);
        free(a.p);
        free(t.p);
        free(want.p);
        stateSchemaFree(sq);
    }

    /* a stackless state must be at a safe point */
    Bytes a = {0}, t = {0};
    stateBody(&a, 0, 0);
    stateBody(&t, 1, 0);
    StateConvertSide wa = {a.p, a.n, sa, stateRoStrings(stra)};
    StateConvertSide wb = {t.p, t.n, sb, stateRoStrings(strb)};
    StateConvertResult r;
    check(!stateConvert(&wa, &wb, 1, &r, err, sizeof err) && strstr(err, "safe point"),
          "a stackless conversion of a state not at a safe point is refused");
    wa.size = a.n - 2; /* the last chunk runs past the body's end */
    check(!stateConvert(&wa, &wb, 0, &r, err, sizeof err) && strstr(err, "damaged"), "a damaged body is refused");
    stateConvertResultFree(&r);
    free(a.p);
    free(t.p);
    stateRoStringsFree(stra);
    stateRoStringsFree(strb);
    stateSchemaFree(sa);
    stateSchemaFree(sb);
}

/* ---- the state rules (docs/CANONICAL_STATES.md 17.2): one layout before and after a set of changes ---- */

static uint64_t get64(const uint8_t* p);
static uint32_t get32(const uint8_t* p);

static uint32_t nameHash(const char* s) { /* pcStateTypeId */
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) {
        if (*s != ' ') h = (h ^ (uint8_t)*s) * 0x01000193u;
    }
    return h ? h : 1;
}

/* Before (revision 1, no rules) or after (revision 2, a rule for each change): a struct's member renamed
 * (pcOld -> pcNew), one whose values changed meaning (0 1 2 -> 5 7 9), one removed (pcGone: 4 before it), one
 * reset (pcMood), one added with a default (pcLevel: 3); a variable renamed; a function renamed; a game member
 * retyped (u8[4] -> s32: its PS1 bytes); a heap type renamed (its blocks' type hash too); a host part's tag
 * renamed; and a rule of a kind no converter knows yet. */
static char* rulesSchema(int after) {
    char* out = malloc(16384);
    const char* keep = after
        ? "{\"name\": \"pcNew\", \"off\": 0, \"type\": \"20\", \"retail\": null}, {\"name\": \"pcMode\", \"off\": 4, \"type\": \"20\", \"retail\": null},"
          " {\"name\": \"pcMood\", \"off\": 8, \"type\": \"20\", \"retail\": null}, {\"name\": \"pcLevel\", \"off\": 12, \"type\": \"20\", \"retail\": null}"
        : "{\"name\": \"pcOld\", \"off\": 0, \"type\": \"20\", \"retail\": null}, {\"name\": \"pcMode\", \"off\": 4, \"type\": \"20\", \"retail\": null},"
          " {\"name\": \"pcGone\", \"off\": 8, \"type\": \"20\", \"retail\": null}, {\"name\": \"pcMood\", \"off\": 12, \"type\": \"20\", \"retail\": null}";
    const char* game = after ? "{\"name\": \"m0_a\", \"off\": 0, \"type\": \"20\", \"retail\": 0}"
                             : "{\"name\": \"m0\", \"off\": 0, \"type\": \"62\", \"retail\": 0}";
    const char* thing = after ? "NewThing" : "OldThing";
    const char* var = after ? "pcNewVar" : "pcOldVar";
    const char* fn = after ? "tickNew" : "tickOld";
    const char* tag = after ? "newTag" : "oldTag";
    const char* rules = after
        ? "{\"language\": 1, \"revision\": 2, \"list\": ["
          " {\"kind\": \"was\", \"rev\": 2, \"type\": \"Keep\", \"member\": \"pcNew\", \"tids\": [\"10\"], \"field\": \"pc:pcNew\", \"old\": \"pc:pcOld\"},"
          " {\"kind\": \"values\", \"rev\": 2, \"type\": \"Keep\", \"tids\": [\"10\"], \"field\": \"pc:pcMode\", \"map\": [[0, 5], [1, 7], [2, 9]]},"
          " {\"kind\": \"removed\", \"rev\": 2, \"type\": \"Keep\", \"tids\": [\"10\"], \"fields\": [\"pc:pcGone\"], \"value\": 4},"
          " {\"kind\": \"reset\", \"rev\": 2, \"type\": \"Keep\", \"tids\": [\"10\"], \"field\": \"pc:pcMood\"},"
          " {\"kind\": \"default\", \"type\": \"Keep\", \"tids\": [\"10\"], \"field\": \"pc:pcLevel\", \"value\": 3},"
          " {\"kind\": \"was\", \"rev\": 2, \"var\": \"main:pc:pcNewVar\", \"old\": \"main:pc:pcOldVar\"},"
          " {\"kind\": \"was\", \"rev\": 2, \"fn\": \"tickNew\", \"old\": \"tickOld\"},"
          " {\"kind\": \"was\", \"rev\": 2, \"typename\": \"NewThing\", \"old\": \"OldThing\"},"
          " {\"kind\": \"was\", \"rev\": 2, \"tag\": \"hw.newTag\", \"old\": \"hw.oldTag\"},"
          " {\"kind\": \"values\", \"rev\": 2, \"var\": \"main:pc:pcModeVar\", \"map\": [[0, 5], [1, 7]]},"
          " {\"kind\": \"reset\", \"rev\": 2, \"var\": \"main:pc:pcReset\"},"
          " {\"kind\": \"default\", \"var\": \"main:pc:pcAdded\", \"value\": 12},"
          " {\"kind\": \"removed\", \"rev\": 2, \"vars\": [\"main:pc:pcDropped\"], \"value\": 21},"
          " {\"kind\": \"someday\", \"rev\": 2, \"var\": \"main:pc:pcNewVar\"}]}"
        : "{\"language\": 1, \"revision\": 1, \"list\": []}";
    snprintf(out, 16384,
        "{\"version\": \"us\", \"exe\": \"\","
        " \"types\": {\"10\": {\"k\": \"struct\", \"size\": 16, \"name\": \"Keep\", \"owner\": \"port\", \"fields\": [%s]},"
        "  \"11\": {\"k\": \"struct\", \"size\": 8, \"name\": \"Game\", \"owner\": \"game\", \"fields\": [%s,"
        "    {\"name\": \"m4_b\", \"off\": 4, \"type\": \"20\", \"retail\": 4}]},"
        "  \"12\": {\"k\": \"struct\", \"size\": 16, \"name\": \"%s\", \"owner\": \"port\", \"fields\": ["
        "    {\"name\": \"m0_a\", \"off\": 0, \"type\": \"20\", \"retail\": 0}, {\"name\": \"m8_self\", \"off\": 8, \"type\": \"31\", \"retail\": 8}]},"
        "  \"20\": {\"k\": \"int\", \"size\": 4, \"signed\": true, \"name\": \"s32\"},"
        "  \"60\": {\"k\": \"int\", \"size\": 1, \"signed\": false, \"name\": \"u8\"},"
        "  \"62\": {\"k\": \"arr\", \"of\": \"60\", \"n\": 4, \"size\": 4},"
        "  \"31\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false, \"to\": \"12\"},"
        "  \"40\": {\"k\": \"ptr\", \"size\": 8, \"fn\": true},"
        "  \"80\": {\"k\": \"ptr\", \"size\": 8, \"fn\": false}},"
        " \"type_names\": {\"Keep\": \"10\", \"Game\": \"11\", \"%s\": \"12\"},"
        " \"variables\": ["
        "  {\"name\": \"keep\", \"addr\": 4096, \"size\": 16, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"10\"},"
        "  {\"name\": \"%s\", \"addr\": 4112, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"pcFn\", \"addr\": 4120, \"size\": 8, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"40\"},"
        "  {\"name\": \"D_80100000\", \"addr\": 4128, \"size\": 8, \"module\": \"main\", \"section\": \"data\", \"retail\": 2148532224, \"platform\": null, \"tid\": \"11\"},"
        "  {\"name\": \"safePointId\", \"addr\": 4136, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"heapFirstBlockData\", \"addr\": 4144, \"size\": 8, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"80\"},"
        "  {\"name\": \"pcThing\", \"addr\": 4152, \"size\": 8, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"31\"},"
        "  {\"name\": \"pcModeVar\", \"addr\": 4160, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"pcReset\", \"addr\": 4164, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"%s\", \"addr\": 4168, \"size\": 4, \"module\": \"main\", \"section\": \"data\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
        "  {\"name\": \"%s\", \"addr\": 20480, \"size\": 4, \"module\": null, \"section\": null, \"retail\": null, \"platform\": \"system/hw.c\", \"tid\": \"20\"}],"
        " \"functions\": [{\"name\": \"%s\", \"addr\": 4194304, \"side\": \"port\", \"retail\": null}],"
        " \"symbols\": [[\"%s\", 4194304, 16, \"T\"]], \"ro\": [],"
        " \"sections\": {\"main.data\": [4096, 4352]},"
        " \"regions\": {\"heapArena\": [65536, 65792], \"gameStack\": [131072, 131328], \"pcScratchpad\": [196608, 197632]},"
        " \"parts\": {\"hw\": \"system/hw.c\"},"
        " \"heap_types\": {\"%08x\": {\"name\": \"%s\", \"spec\": {\"tid\": \"12\"}}},"
        " \"counted\": {}, \"unions\": {}, \"rules\": %s}",
        keep, game, thing, thing, var, after ? "pcAdded" : "pcDropped", tag, fn, fn, nameHash(thing), thing, rules);
    return out;
}

/* A state of that layout (its own values): keep, the variable, the function pointer, the game struct, the
 * safe point, the heap with its one block, the host part. */
static void rulesBody(Bytes* b, int after, int mode, int mood, int level) {
    uint8_t data[0x100], heap[0x100] = {0}, part[4] = {0x0D, 0x0C, 0x0B, 0x0A};
    memset(data, 0xEE, sizeof data); /* outside every variable: the reader's own */
    put32(data + 0, 11);
    put32(data + 4, (uint32_t)mode);
    put32(data + 8, after ? (uint32_t)mood : 4);
    put32(data + 12, after ? (uint32_t)level : (uint32_t)mood);
    put32(data + 16, 41);
    put32(data + 20, 0xEEEEEEEE);
    put64(data + 24, 0x400000);
    put32(data + 32, 0x12345678);   /* +0: an s32 after, the u8[4] {78 56 34 12} before: the same bytes */
    put32(data + 36, 9);
    put32(data + 40, 5);            /* safePointId */
    put32(data + 44, 0xEEEEEEEE);
    put64(data + 48, 0x10010);      /* heapFirstBlockData */
    put64(data + 56, 0x10010);      /* pcThing */
    put32(data + 64, after ? 7 : 1); /* pcModeVar: 1 -> 7 */
    put32(data + 68, after ? 8 : 9); /* pcReset */
    put32(data + 72, after ? 12 : 5); /* pcAdded (default 12) after, pcDropped before (21 from a later build) */
    put64(heap + 0, 0x10030);
    put32(heap + 8, 2u << 21);
    put32(heap + 12, nameHash(after ? "NewThing" : "OldThing"));
    put32(heap + 16, 6);
    put64(heap + 24, 0x10010);      /* m8_self */
    put32(heap + 40, 1u << 21);     /* the end */
    chunk(b, "mem.main.data", data, sizeof data);
    chunk(b, "mem.heap", heap, sizeof heap);
    chunk(b, after ? "hw.newTag" : "hw.oldTag", part, 4);
}

static void rulesChecks(void) {
    char err[300];
    char* tb = rulesSchema(0);
    char* ta = rulesSchema(1);
    StateSchema* before = stateSchemaParse(tb, strlen(tb), err, sizeof err);
    StateSchema* after = stateSchemaParse(ta, strlen(ta), err, sizeof err);
    free(tb);
    free(ta);
    check(before && after && stateSchemaRevision(after) == 2, "rules: the two layouts' schemas parse");
    if (!before || !after) {
        printf("  %s\n", err);
        stateSchemaFree(before);
        stateSchemaFree(after);
        return;
    }
    StateStrings none = {NULL, NULL, NULL};
    for (int dir = 0; dir < 2; dir++) { /* 0: before's state into after (forward), 1: after's into before (backward) */
        Bytes w = {0}, t = {0};
        rulesBody(&w, dir, dir ? 7 : 1, dir ? 6 : 7, 3);
        rulesBody(&t, !dir, 0, 0, 0);
        StateConvertSide ws = {w.p, w.n, dir ? after : before, none};
        StateConvertSide rs = {t.p, t.n, dir ? before : after, none};
        StateConvertResult r;
        int ok = stateConvert(&ws, &rs, 1, &r, err, sizeof err);
        check(ok, dir ? "rules: backward converts" : "rules: forward converts");
        if (!ok) {
            printf("  %s\n", err);
            free(w.p);
            free(t.p);
            continue;
        }
        /* the reader's chunks: its data at 4 + 13 + 4, its heap after, its part last */
        const uint8_t* d = r.body + 1 + 13 + 4;
        const uint8_t* h = d + 0x100 + 1 + 8 + 4;
        const uint8_t* part = h + 0x100 + 1 + 9 + 4;
        if (!dir) {
            check(get32(d + 0) == 11 && get32(d + 4) == 7 && get32(d + 8) == 0 && get32(d + 12) == 3,
                  "forward: a member renamed, a value mapped (1 -> 7), a reset member not carried, a default given");
        } else {
            check(get32(d + 0) == 11 && get32(d + 4) == 1 && get32(d + 8) == 4 && get32(d + 12) == 0,
                  "backward: by the newer writer's rules: renamed, mapped back (7 -> 1), the removed member's value, reset");
        }
        check(get32(d + 16) == 41 && get64(d + 24) == 0x400000, dir ? "backward: a renamed variable and function"
                                                                     : "forward: a renamed variable and function");
        check(memcmp(d + 32, "\x78\x56\x34\x12", 4) == 0 && get32(d + 36) == 9 && get32(d + 40) == 5,
              "the game member retyped, by its PS1 bytes");
        check(get32(h + 12) == nameHash(dir ? "OldThing" : "NewThing") && get32(h + 16) == 6 && get64(h + 24) == 0x10010,
              "a heap block of a renamed type: its part found, its header's hash the reader's name's");
        check(memcmp(part, "\x0D\x0C\x0B\x0A", 4) == 0, "a host part whose tag was renamed");
        check(get32(d + 64) == (dir ? 1u : 7u) && get32(d + 68) == 0 && get32(d + 72) == (dir ? 21u : 12u),
              dir ? "backward: a variable's value mapped back, a reset one not carried, a removed one its rule's value"
                  : "forward: a variable's value mapped, a reset one not carried, an added one its default");
        check(warningCount(r.decodeWarnings, r.decodeWarningCount, "variable reset by a state rule (not carried)") == 1 &&
                  warningCount(r.decodeWarnings, r.decodeWarningCount,
                               dir ? "variable removed by a later build (given the value its rule names)"
                                   : "variable not in the state (given its default)") == 1,
              "the variables' rules, warned");
        check(warningCount(r.decodeWarnings, r.decodeWarningCount, "state rule this build cannot read (ignored)") == 1,
              "a rule of a kind this converter does not know: ignored, counted, not refused");
        check(warningCount(r.decodeWarnings, r.decodeWarningCount, "field reset by a state rule (not carried)") == 1 &&
                  warningCount(r.ruleNotes, r.ruleNoteCount, "values mapped by a state rule") == 2 &&
                  warningCount(r.ruleNotes, r.ruleNoteCount, "carried by its PS1 bytes") == 1 &&
                  warningCount(r.ruleNotes, r.ruleNoteCount, "type hash remapped by a state rule") == 1 &&
                  warningCount(r.ruleNotes, r.ruleNoteCount, "renamed by a state rule") == 4,
              "what the rules did, counted");
        check(warningCount(r.decodeWarnings, r.decodeWarningCount, dir ? "field removed by a later build (given the value its rule names)"
                                                                        : "field missing in the state (given its default)") == 1,
              dir ? "backward: the removed member, warned" : "forward: the added member, warned");
        stateConvertResultFree(&r);
        free(w.p);
        free(t.p);
    }
    /* the same revision both sides: no history rule, the reader's own default still there */
    Bytes w = {0}, t = {0};
    rulesBody(&w, 1, 7, 6, 3);
    rulesBody(&t, 1, 0, 0, 0);
    StateConvertSide ws = {w.p, w.n, after, none}, rs = {t.p, t.n, after, none};
    StateConvertResult r;
    int ok = stateConvert(&ws, &rs, 1, &r, err, sizeof err);
    check(ok && r.ruleNoteCount == 0 && r.decodeWarningCount == 0 && get32(r.body + 18 + 4) == 7 && get32(r.body + 18 + 8) == 6,
          "the same revision: nothing applies, nothing ignored");
    if (ok) stateConvertResultFree(&r);
    free(w.p);
    free(t.p);
    stateSchemaFree(before);
    stateSchemaFree(after);
}

/* ---- real states ---------------------------------------------------------------------------------- */

static uint8_t* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* p = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

static uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static uint32_t get32(const uint8_t* p) { return (uint32_t)get64((const uint8_t[8]){p[0], p[1], p[2], p[3]}); }

enum { HEADER_SIZE = 256 };

/* The process's peak resident memory so far in MiB (0 where unknown): the conversion's own is the rise. */
static double peakMiB(void) {
#if defined(__unix__) || defined(__APPLE__)
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) {
#ifdef __APPLE__
        return (double)u.ru_maxrss / (1024.0 * 1024.0);
#else
        return (double)u.ru_maxrss / 1024.0;
#endif
    }
#endif
    return 0;
}

/* statecheck.py read_state: the file and its body unpacked. */
static uint8_t* readState(const char* path, uint8_t** file, size_t* fileSize, size_t* bodySize) {
    *file = readFile(path, fileSize);
    if (!*file || *fileSize < HEADER_SIZE || memcmp(*file, "XGSTATE\0", 8) != 0 || get32(*file + 12) != HEADER_SIZE) {
        fprintf(stderr, "stateconverttest: %s: not a save state\n", path);
        return NULL;
    }
    uint64_t raw = get64(*file + 200), thumb = get32(*file + 216), packed = get64(*file + 224);
    if (HEADER_SIZE + thumb + packed > *fileSize) {
        fprintf(stderr, "stateconverttest: %s: cut short\n", path);
        return NULL;
    }
    uint8_t* body = malloc(raw ? raw : 1);
    if (!body || !stateUnpack(*file + HEADER_SIZE + thumb, packed, body, raw)) {
        fprintf(stderr, "stateconverttest: %s: a damaged body\n", path);
        free(body);
        return NULL;
    }
    *bodySize = raw;
    return body;
}

/* The executable's read-only ranges, as statecanon reads them from the file: each range's bytes and the
 * 4096 after it (a string's NUL is searched for that far). */
typedef struct {
    StateRoSegment* seg;
    int32_t count;
    StateRoStrings* strings;
} ExeStrings;

static int exeStrings(const StateSchema* s, ExeStrings* out) {
    const char* exe = stateSchemaExe(s);
    FILE* f = fopen(exe, "rb");
    if (!f) {
        fprintf(stderr, "stateconverttest: cannot open %s (the schema's exe)\n", exe);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    out->count = stateSchemaReadOnlyCount(s);
    out->seg = calloc((size_t)out->count + 1, sizeof *out->seg);
    for (int32_t i = 0; i < out->count; i++) {
        uint64_t lo, hi, fo;
        stateSchemaReadOnlyAt(s, i, &lo, &hi, &fo);
        size_t want = (size_t)(hi - lo) + 4096;
        size_t have = fo < (uint64_t)fileSize ? (size_t)((uint64_t)fileSize - fo) : 0;
        size_t n = want < have ? want : have;
        uint8_t* bytes = malloc(n + 1);
        fseek(f, (long)fo, SEEK_SET);
        if (!bytes || fread(bytes, 1, n, f) != n) {
            fclose(f);
            return 0;
        }
        out->seg[i].lo = lo;
        out->seg[i].hi = hi;
        out->seg[i].bytes = bytes;
        out->seg[i].size = n;
    }
    fclose(f);
    out->strings = stateRoStringsNew(out->seg, out->count);
    return out->strings != NULL;
}

static void exeStringsFree(ExeStrings* e) {
    for (int32_t i = 0; i < e->count; i++) free((void*)e->seg[i].bytes);
    free(e->seg);
    stateRoStringsFree(e->strings);
}

static void printWarnings(const char* side, const StateConvertWarning* w, int32_t n) {
    for (int32_t i = 0; i < n; i++) printf("%s %lld %s\n", side, (long long)w[i].count, w[i].text);
}

static int convertFiles(int argc, char** argv) {
    if (argc < 7) {
        fprintf(stderr, "usage: stateconverttest convert A.xgs SCHEMA_A T.xgs SCHEMA_B OUT.xgs [--stackless] "
                        "[--moment]\n");
        return 2;
    }
    int stackless = 0, moment = 0;
    for (int i = 7; i < argc; i++) {
        stackless |= strcmp(argv[i], "--stackless") == 0;
        moment |= strcmp(argv[i], "--moment") == 0;
    }
    char err[400];
    clock_t t0 = clock();
    uint8_t *fa, *ft;
    size_t na, nt, ba, bt;
    uint8_t* bodyA = readState(argv[2], &fa, &na, &ba);
    uint8_t* bodyT = bodyA ? readState(argv[4], &ft, &nt, &bt) : NULL;
    if (!bodyA || !bodyT) return 1;
    /* each schema at the addresses its state's process had: rebased by the load bias its header records
     * (statecanon state_bias: H_IMAGE, at 232) */
    StateSchema* sa = stateSchemaLoadAt(argv[3], get64(fa + 232), err, sizeof err);
    StateSchema* sb = sa ? stateSchemaLoadAt(argv[5], get64(ft + 232), err, sizeof err) : NULL;
    if (!sa || !sb) {
        fprintf(stderr, "stateconverttest: %s\n", err);
        return 1;
    }
    ExeStrings ea = {0}, eb = {0};
    if (!exeStrings(sa, &ea) || !exeStrings(sb, &eb)) return 1;
    if (moment) {
        uint8_t* adjusted;
        size_t n;
        if (!stateConvertTemplate(bodyT, bt, sb, bodyA, ba, &adjusted, &n, err, sizeof err)) {
            fprintf(stderr, "stateconverttest: %s\n", err);
            return 1;
        }
        free(bodyT);
        bodyT = adjusted;
        bt = n;
    }
    clock_t t1 = clock();
    double before = peakMiB();
    StateConvertSide wa = {bodyA, ba, sa, stateRoStrings(ea.strings)};
    StateConvertSide wb = {bodyT, bt, sb, stateRoStrings(eb.strings)};
    StateConvertResult r;
    if (!stateConvert(&wa, &wb, stackless, &r, err, sizeof err)) {
        fprintf(stderr, "stateconverttest: %s\n", err);
        return 1;
    }
    clock_t t2 = clock();
    double after = peakMiB();
    /* statecanon write_state: the template's header and thumbnail, the body in stored 1 MiB blocks */
    uint32_t thumb = get32(ft + 216);
    uint8_t head[HEADER_SIZE];
    memcpy(head, ft, HEADER_SIZE);
    size_t blocks = 0;
    for (size_t i = 0; i < r.size; i += STATE_PACK_BLOCK) blocks += 8 + (r.size - i < STATE_PACK_BLOCK ? r.size - i : STATE_PACK_BLOCK);
    put64(head + 200, r.size);
    put64(head + 208, stateHash(r.body, r.size, 0));
    put64(head + 224, blocks);
    FILE* f = fopen(argv[6], "wb");
    int ok = f && fwrite(head, 1, HEADER_SIZE, f) == HEADER_SIZE && fwrite(ft + HEADER_SIZE, 1, thumb, f) == thumb;
    for (size_t i = 0; ok && i < r.size; i += STATE_PACK_BLOCK) {
        uint32_t n = (uint32_t)(r.size - i < STATE_PACK_BLOCK ? r.size - i : STATE_PACK_BLOCK);
        uint8_t sizes[8];
        put32(sizes, n);
        put32(sizes + 4, n);
        ok = fwrite(sizes, 1, 8, f) == 8 && fwrite(r.body + i, 1, n, f) == n;
    }
    if (f) ok = fclose(f) == 0 && ok;
    if (!ok) {
        fprintf(stderr, "stateconverttest: cannot write %s\n", argv[6]);
        return 1;
    }
    printf("stateconverttest convert: safe point %lld; wrote %s (%zu bytes); load %.3f s, convert %.3f s; "
           "peak memory %.1f MiB loaded, %.1f MiB after converting\n",
           (long long)r.safePoint, argv[6], r.size, (double)(t1 - t0) / CLOCKS_PER_SEC,
           (double)(t2 - t1) / CLOCKS_PER_SEC, before, after);
    printWarnings("encode", r.encodeWarnings, r.encodeWarningCount);
    printWarnings("decode", r.decodeWarnings, r.decodeWarningCount);
    printWarnings("rule", r.ruleNotes, r.ruleNoteCount);
    stateConvertResultFree(&r);
    exeStringsFree(&ea);
    exeStringsFree(&eb);
    free(bodyA);
    free(bodyT);
    free(fa);
    free(ft);
    stateSchemaFree(sa);
    stateSchemaFree(sb);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "convert") == 0) {
        return convertFiles(argc, argv);
    }
    convertChecks();
    rulesChecks();
    if (failures) {
        printf("stateconverttest: %d checks failed\n", failures);
        return 1;
    }
    printf("stateconverttest: all checks passed\n");
    return 0;
}
