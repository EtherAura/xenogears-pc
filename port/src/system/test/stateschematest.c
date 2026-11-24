/* The JSON reader (json.h) and the state schema in C (stateschema.h).
 *
 *   stateschematest              the checks
 *   stateschematest SCHEMA       a build's schema's indexes, one per line, as
 *                                tools/test_stateschema_c.py prints the same
 *                                from tools/statecanon.py's Schema to compare */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../json.h"
#include "../statecodec.h"
#include "../stateschema.h"

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void jsonChecks(void) {
    char err[200];
    const char* text = "{\"a\": [1, -2, 3.5, true, false, null], \"s\": \"x\\\"y\\\\z\\n\\u00e9\\ud83d\\ude00\", "
                       "\"o\": {\"k\": {}, \"l\": []}, \"big\": 9007199254740993, \"neg\": -9223372036854775808}";
    JsonDoc* d = jsonParse(text, strlen(text), err, sizeof err);
    check(d != NULL, "a document parses");
    if (!d) return;
    JsonRef root = jsonRoot(d);
    check(jsonType(d, root) == JSON_OBJECT && jsonCount(d, root) == 5, "the root object's entries");
    JsonRef a = jsonGet(d, root, "a");
    check(jsonCount(d, a) == 6, "an array's entries");
    JsonRef e = jsonFirst(d, a);
    check(jsonType(d, e) == JSON_INT && jsonInt(d, e) == 1, "an integer");
    e = jsonNext(d, e);
    check(jsonInt(d, e) == -2, "a negative integer");
    e = jsonNext(d, e);
    check(jsonType(d, e) == JSON_REAL && jsonReal(d, e) == 3.5, "a real");
    e = jsonNext(d, e);
    check(jsonType(d, e) == JSON_TRUE, "true");
    e = jsonNext(d, e);
    check(jsonType(d, e) == JSON_FALSE, "false");
    e = jsonNext(d, e);
    check(jsonType(d, e) == JSON_NULL && jsonNext(d, e) == JSON_NONE, "null, the last");
    check(strcmp(jsonString(d, jsonGet(d, root, "s")), "x\"y\\z\n\xc3\xa9\xf0\x9f\x98\x80") == 0,
          "escapes and \\u sequences (a surrogate pair too) as UTF-8");
    JsonRef o = jsonGet(d, root, "o");
    check(jsonCount(d, jsonGet(d, o, "k")) == 0 && jsonFirst(d, jsonGet(d, o, "l")) == JSON_NONE, "empty containers");
    check(jsonNext(d, jsonGet(d, o, "l")) == JSON_NONE, "a nested object's last entry");
    check(jsonGet(d, root, "o") != JSON_NONE && strcmp(jsonKey(d, jsonGet(d, root, "big")), "big") == 0, "keys");
    check(jsonInt(d, jsonGet(d, root, "big")) == 9007199254740993LL, "integers exactly (past 2^53)");
    check(jsonInt(d, jsonGet(d, root, "neg")) == INT64_MIN, "the smallest integer");
    check(jsonGet(d, root, "missing") == JSON_NONE, "an absent key");
    jsonFree(d);

    const char* bad[] = {"{\"a\" 1}", "[1, 2", "\"abc", "{\"a\": tru}", "[1] 2", "{\"a\": \"\\q\"}", "[18446744073709551616]"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        JsonDoc* b = jsonParse(bad[i], strlen(bad[i]), err, sizeof err);
        char what[100];
        snprintf(what, sizeof what, "refused: %s", bad[i]);
        check(b == NULL && strstr(err, "at byte") != NULL, what);
        jsonFree(b);
    }
}

static const char* SCHEMA =
    "{\"version\": \"us\", \"exe\": \"x\","
    " \"types\": {\"10\": {\"k\": \"struct\", \"size\": 8, \"name\": \"Pair\", \"owner\": \"game\", \"fields\": ["
    "    {\"name\": \"m0_a\", \"off\": 0, \"type\": \"20\", \"retail\": 0},"
    "    {\"name\": \"m4_b\", \"off\": 4, \"type\": \"20\", \"retail\": 4, \"bits\": 3, \"bit\": 33}]},"
    "  \"20\": {\"k\": \"int\", \"size\": 4, \"signed\": true, \"name\": \"s32\"},"
    "  \"30\": {\"k\": \"arr\", \"of\": \"30.d0\", \"n\": 2, \"size\": 16},"
    "  \"30.d0\": {\"k\": \"arr\", \"of\": \"40\", \"n\": 1, \"size\": 8},"
    "  \"40\": {\"k\": \"scratch\", \"size\": 8, \"name\": \"scratch64\"},"
    "  \"50\": {\"k\": \"ptr\", \"size\": 8, \"fn\": true}},"
    " \"type_names\": {\"Pair\": \"10\", \"PairAlias\": \"10\", \"s32\": \"20\", \"gone\": \"99\"},"
    " \"variables\": ["
    "  {\"name\": \"small\", \"addr\": 4096, \"size\": 4, \"module\": \"main\", \"section\": \"bss\", \"retail\": 2147483648, \"platform\": null, \"tid\": \"20\"},"
    "  {\"name\": \"large\", \"addr\": 4096, \"size\": 8, \"module\": \"main\", \"section\": \"bss\", \"retail\": 2147483648, \"platform\": null, \"tid\": \"10\"},"
    "  {\"name\": \"alias\", \"addr\": 8192, \"size\": 8, \"module\": \"main\", \"section\": \"data\", \"retail\": 2147483648, \"platform\": null, \"tid\": \"10\"},"
    "  {\"name\": \"pcOnly\", \"addr\": 12288, \"size\": 4, \"module\": \"field\", \"section\": \"bss\", \"retail\": null, \"platform\": null, \"tid\": \"20\"},"
    "  {\"name\": \"hw\", \"addr\": 16384, \"size\": 16, \"module\": null, \"section\": null, \"retail\": null, \"platform\": \"system/hw.c\", \"tid\": \"30\"},"
    "  {\"name\": \"untyped\", \"addr\": 20480, \"size\": 4, \"module\": null, \"section\": null, \"retail\": null, \"platform\": \"x.c\", \"tid\": null}],"
    " \"functions\": [{\"name\": \"tick\", \"addr\": 65536, \"side\": \"game\", \"retail\": 2147491840},"
    "  {\"name\": \"helper\", \"addr\": 65600, \"side\": \"port\", \"retail\": null}],"
    " \"symbols\": [[\"tick\", 65536, 32, \"T\"], [\"tick_alias\", 65536, 32, \"t\"], [\"folded_b\", 65700, 8, \"t\"],"
    "  [\"folded_a\", 65700, 8, \"t\"], [\"table\", 131072, 64, \"R\"], [\".LC1\", 131200, 4, \"r\"], [\"xgsrc_main_0\", 131300, 8, \"r\"]],"
    " \"ro\": [[131072, 135168, 4096]],"
    " \"sections\": {\"main.data\": [8192, 9000]}, \"regions\": {\"heapArena\": [1048576, 2097152]},"
    " \"parts\": {\"hw\": \"system/hw.c\"},"
    " \"heap_types\": {\"479b06ef\": {\"name\": \"Pair\", \"spec\": {\"tid\": \"10\"}},"
    "  \"00000002\": {\"name\": \"PC_PRIMS\", \"spec\": \"prims\"}, \"00000003\": {\"name\": \"Pair[1]\", \"spec\": {\"tid\": \"10\", \"one\": true}}},"
    " \"counted\": {\"10\": {\"name\": \"Pair\", \"array\": \"m4_b\", \"count\": \"m0_a\", \"extra\": 1}},"
    " \"unions\": {\"10\": {\"name\": \"Pair\", \"member\": \"m4_b\", \"selector\": \"m0_a\", \"views\": [\"x\", \"y\"]}}}";

static void schemaChecks(void) {
    char err[200];
    StateSchema* s = stateSchemaParse(SCHEMA, strlen(SCHEMA), err, sizeof err);
    check(s != NULL, "a schema parses");
    if (!s) {
        printf("  %s\n", err);
        return;
    }
    check(strcmp(stateSchemaVersion(s), "us") == 0, "its version");
    int32_t pair = stateSchemaType(s, "10");
    const StType* t = stateSchemaTypeAt(s, pair);
    check(t && t->kind == ST_STRUCT && t->size == 8 && t->fieldCount == 2, "a struct");
    check(t && t->fields[1].bits == 3 && t->fields[1].bit == 33 && t->fields[0].retail == 0, "fields, a bitfield");
    check(t && t->fields[0].type == stateSchemaType(s, "20"), "a field's type");
    check(stateSchemaTypeAt(s, stateSchemaType(s, "50"))->fn == 1, "a function pointer");
    check(stateSchemaTypeNamed(s, "PairAlias") == pair && stateSchemaTypeNamed(s, "gone") == ST_NONE, "type names");
    check(strcmp(stateSchemaTidName(s, pair), "Pair") == 0, "a type's own name wins as its stable name");
    check(stateSchemaVarCount(s) == 4, "variables: one per address, untyped ones left out");
    const StVar* v0 = stateSchemaVarAt(s, 0);
    check(strcmp(v0->name, "large") == 0 && strcmp(v0->id, "main:80000000") == 0, "the larger at an address, its identity");
    check(strcmp(stateSchemaVarAt(s, 1)->id, "main:80000000:alias") == 0, "an identity taken: the name added");
    check(strcmp(stateSchemaVarAt(s, 2)->id, "field:pc:pcOnly") == 0, "a PC-only game variable");
    check(strcmp(stateSchemaVarAt(s, 3)->id, "pc:system/hw.c:hw") == 0, "a platform static");
    check(stateSchemaVarById(s, "main:80000000:alias") == 1 && stateSchemaVarNamed(s, "hw") == 3, "by identity, name");
    check(stateSchemaPlatformVar(s, "system/hw.c", "hw") == 3, "by platform file and name");
    check(stateSchemaVarHolding(s, 4100) == 0 && stateSchemaVarHolding(s, 4104) == ST_NONE && stateSchemaVarHolding(s, 16399) == 3,
          "the variable holding an address");
    check(stateSchemaScratchCount(s) == 1 && stateSchemaScratch(s, 0).lo == 16384, "scratch64 through nested arrays");
    check(stateSchemaType(s, "30.d0") != ST_NONE && stateSchemaType(s, "30.d0") != stateSchemaType(s, "30"),
          "an inner dimension's own key (\"30.d0\", not 0x30)");
    char id[200];
    check(stateSchemaFnIdentity(s, 65536, id, sizeof id) && strcmp(id, "fn:80002000:tick") == 0, "a function's identity");
    check(stateSchemaFnIdentity(s, 65700, id, sizeof id) && strcmp(id, "fn:folded_a") == 0,
          "a folded body: its names sorted");
    check(!stateSchemaFnIdentity(s, 65701, id, sizeof id), "no function there");
    check(stateSchemaFnAddress(s, "fn:80002000:tick") == 65536 && stateSchemaFnAddress(s, "fn:12345678:helper") == 65600,
          "an identity's address, by name when the retail address differs");
    check(stateSchemaFnAddress(s, "fn:folded_b") == 65700 && stateSchemaFnAddress(s, "fn:nothing") == 0,
          "a symbol's address, or none");
    uint64_t at = 0;
    check(strcmp(stateSchemaSymbolBelow(s, 65710, &at), "folded_b") == 0 && at == 65700,
          "the symbol below an address (the last by name at the nearest address)");
    const StRange* c = stateSchemaNamedConstant(s, 131100);
    check(c && strcmp(c->name, "table") == 0 && !stateSchemaNamedConstant(s, 131201), "named constants (.L and xgsrc_ left out)");
    uint64_t fo = 0;
    check(stateSchemaReadOnly(s, 131080, &fo) && fo == 4104 && !stateSchemaReadOnly(s, 135168, NULL), "read-only data");
    check(stateSchemaSection(s, "main.data").hi == 9000 && stateSchemaRegion(s, "heapArena").lo == 1048576, "sections, regions");
    check(strcmp(stateSchemaPartFile(s, "hw"), "system/hw.c") == 0 && !stateSchemaPartFile(s, "x"), "host parts");
    const StHeapType* h = stateSchemaHeapType(s, 0x479b06ef);
    check(h && h->spec == ST_SPEC_TYPE && h->type == pair && !h->one, "a heap type");
    check(stateSchemaHeapType(s, 2)->spec == ST_SPEC_PRIMS && stateSchemaHeapType(s, 3)->one, "prims, PC_HEAP_ONE");
    const StCounted* k = stateSchemaCounted(s, pair);
    check(k && strcmp(k->array, "m4_b") == 0 && k->extra == 1 && stateSchemaCountedNamed(s, "Pair") == k, "counted arrays");
    const StUnion* u = stateSchemaUnion(s, pair);
    check(u && u->viewCount == 2 && strcmp(u->views[1], "y") == 0 && stateSchemaUnionNamed(s, "Pair") == u, "union rules");
    stateSchemaFree(s);

    /* the save folder's form: "XGSCHEMA", the JSON's size, the JSON packed by statePack */
    size_t len = strlen(SCHEMA);
    uint8_t* packed = malloc(statePackBound(len) + 16);
    size_t size = statePack((const uint8_t*)SCHEMA, len, packed + 16);
    uint64_t raw = len;
    memcpy(packed, "XGSCHEMA", 8);
    memcpy(packed + 8, &raw, 8);
    const char* path = "stateschematest.packed";
    FILE* f = fopen(path, "wb");
    check(f && fwrite(packed, 1, size + 16, f) == size + 16, "a packed schema written");
    if (f) fclose(f);
    s = stateSchemaLoad(path, err, sizeof err);
    check(s && stateSchemaVarCount(s) == 4, "a packed schema loads");
    stateSchemaFree(s);
    packed[20] ^= 0xFF;
    f = fopen(path, "wb");
    if (f) {
        fwrite(packed, 1, size + 16, f);
        fclose(f);
    }
    s = stateSchemaLoad(path, err, sizeof err);
    check(s == NULL, "a damaged packed schema is refused");
    stateSchemaFree(s);
    remove(path);
    free(packed);
    check(stateSchemaLoad("no/such/schema.json", err, sizeof err) == NULL && strstr(err, "cannot open"), "a missing file");
}

/* ---- the indexes of a real schema, for tools/test_stateschema_c.py ---------------------------------- */

static void dump(const char* path) {
    char err[300];
    StateSchema* s = stateSchemaLoad(path, err, sizeof err);
    if (!s) {
        fprintf(stderr, "stateschematest: %s\n", err);
        exit(2);
    }
    printf("version %s\n", stateSchemaVersion(s));
    printf("counts %d %d %d %d %d\n", stateSchemaTypeCount(s), stateSchemaVarCount(s), stateSchemaFuncCount(s),
           stateSchemaSymbolCount(s), stateSchemaScratchCount(s));
    for (int32_t i = 0; i < stateSchemaTypeCount(s); i++) {
        const char* n = stateSchemaTidName(s, i);
        if (n) printf("tidname %s %s\n", stateSchemaTypeAt(s, i)->tid, n);
    }
    for (int32_t i = 0; i < stateSchemaVarCount(s); i++) {
        const StVar* v = stateSchemaVarAt(s, i);
        printf("var %s %llu %lld %s\n", v->id, (unsigned long long)v->addr, (long long)v->size,
               v->type == ST_NONE ? "-" : stateSchemaTypeAt(s, v->type)->tid);
        /* var_at inside it, and just past its end */
        uint64_t probes[2] = {v->addr + (uint64_t)(v->size / 2), v->addr + (uint64_t)(v->size > 1 ? v->size : 1)};
        for (int p = 0; p < 2; p++) {
            int32_t at = stateSchemaVarHolding(s, probes[p]);
            printf("varat %llu %s\n", (unsigned long long)probes[p], at == ST_NONE ? "-" : stateSchemaVarAt(s, at)->id);
        }
    }
    char id[600];
    for (int32_t i = 0; i < stateSchemaSymbolCount(s); i++) {
        const StSymbol* y = stateSchemaSymbolAt(s, i);
        if (y->kind == 't' || y->kind == 'T' || y->kind == 'w' || y->kind == 'W') {
            if (stateSchemaFnIdentity(s, y->addr, id, sizeof id)) {
                printf("fnid %llu %s\n", (unsigned long long)y->addr, id);
                printf("fnaddr %s %llu\n", id, (unsigned long long)stateSchemaFnAddress(s, id));
            }
        }
        if (i % 7 == 0) {
            uint64_t at = 0;
            const char* b = stateSchemaSymbolBelow(s, y->addr + 3, &at);
            printf("below %llu %s %llu\n", (unsigned long long)(y->addr + 3), b ? b : "-", (unsigned long long)at);
            const StRange* c = stateSchemaNamedConstant(s, y->addr + 1);
            printf("const %llu %s\n", (unsigned long long)(y->addr + 1), c ? c->name : "-");
        }
    }
    for (int32_t i = 0; i < stateSchemaFuncCount(s); i++) {
        const StFunc* f = stateSchemaFuncAt(s, i);
        printf("fnaddr %s %llu\n", f->id, (unsigned long long)stateSchemaFnAddress(s, f->id));
    }
    stateSchemaFree(s);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        dump(argv[1]);
        return 0;
    }
    jsonChecks();
    schemaChecks();
    if (failures == 0) {
        printf("stateschematest: all checks passed\n");
    }
    return failures != 0;
}
