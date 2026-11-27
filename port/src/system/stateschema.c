/*
 * The state schema in C (stateschema.h). Each index is built as
 * tools/statecanon.py's Schema builds it, with the same orders and the same
 * first-or-last rules where two entries meet, so the C converter and the
 * reference decide alike.
 */

#include "stateschema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "statecodec.h"

/* ---- an arena for the strings and arrays -------------------------------------------------------- */

typedef struct Chunk {
    struct Chunk* next;
    size_t used, size;
    char data[];
} Chunk;

static void* arenaAlloc(Chunk** arena, size_t n) {
    n = (n + 15) & ~(size_t)15;
    Chunk* c = *arena;
    if (!c || c->used + n > c->size) {
        size_t size = n > (1u << 20) ? n : (1u << 20);
        Chunk* fresh = malloc(sizeof *fresh + size);
        if (!fresh) {
            return NULL;
        }
        fresh->next = c;
        fresh->used = 0;
        fresh->size = size;
        *arena = fresh;
        c = fresh;
    }
    void* p = c->data + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

static const char* arenaString(Chunk** arena, const char* s) {
    size_t n = strlen(s) + 1;
    char* p = arenaAlloc(arena, n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

/* ---- maps: strings or integers to an index ------------------------------------------------------ */

typedef struct {
    uint64_t* keys;     /* integer keys (or a string's hash) */
    const char** names; /* string keys, NULL for an integer map */
    int32_t* values;
    uint32_t cap;       /* a power of two; 0 empty */
    uint32_t used;
} Map;

static uint64_t hashString(const char* s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 1099511628211ull;
    }
    return h;
}

static uint64_t mix(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33;
    return k;
}

static int mapGrow(Map* m, int strings) {
    uint32_t cap = m->cap ? m->cap * 2 : 64;
    uint64_t* keys = calloc(cap, sizeof *keys);
    const char** names = strings ? calloc(cap, sizeof *names) : NULL;
    int32_t* values = malloc(cap * sizeof *values);
    if (!keys || (strings && !names) || !values) {
        free(keys);
        free(names);
        free(values);
        return 0;
    }
    for (uint32_t i = 0; i < cap; i++) {
        values[i] = ST_NONE;
    }
    for (uint32_t i = 0; i < m->cap; i++) {
        if (m->values[i] == ST_NONE) {
            continue;
        }
        uint32_t j = (uint32_t)mix(m->keys[i]) & (cap - 1);
        while (values[j] != ST_NONE) {
            j = (j + 1) & (cap - 1);
        }
        keys[j] = m->keys[i];
        if (strings) {
            names[j] = m->names[i];
        }
        values[j] = m->values[i];
    }
    free(m->keys);
    free(m->names);
    free(m->values);
    m->keys = keys;
    m->names = names;
    m->values = values;
    m->cap = cap;
    return 1;
}

/* The slot of key (empty or holding it). */
static uint32_t mapSlot(const Map* m, uint64_t key, const char* name) {
    uint32_t j = (uint32_t)mix(key) & (m->cap - 1);
    while (m->values[j] != ST_NONE) {
        if (m->keys[j] == key && (!name || strcmp(m->names[j], name) == 0)) {
            break;
        }
        j = (j + 1) & (m->cap - 1);
    }
    return j;
}

/* overwrite: 1 replaces a present key's value (a Python assignment), 0 keeps it (setdefault) */
static int mapPut(Map* m, uint64_t key, const char* name, int32_t value, int overwrite) {
    if ((m->used + 1) * 4 > m->cap * 3 && !mapGrow(m, name != NULL)) {
        return 0;
    }
    uint32_t j = mapSlot(m, key, name);
    if (m->values[j] == ST_NONE) {
        m->keys[j] = key;
        if (name) {
            m->names[j] = name;
        }
        m->values[j] = value;
        m->used++;
    } else if (overwrite) {
        m->values[j] = value;
    }
    return 1;
}

static int32_t mapGet(const Map* m, uint64_t key, const char* name) {
    if (!m->cap) {
        return ST_NONE;
    }
    return m->values[mapSlot(m, key, name)];
}

static int putName(Map* m, const char* name, int32_t value, int overwrite) {
    return mapPut(m, hashString(name), name, value, overwrite);
}

static int32_t getName(const Map* m, const char* name) {
    return mapGet(m, hashString(name), name);
}

static void mapFree(Map* m) {
    free(m->keys);
    free(m->names);
    free(m->values);
    memset(m, 0, sizeof *m);
}

/* ---- the schema --------------------------------------------------------------------------------- */

typedef struct {
    uint64_t addr;
    int32_t names;     /* first in textNames */
    int32_t nameCount;
} TextSyms;

struct StateSchema {
    Chunk* arena;
    uint64_t bias;    /* added to every address the JSON gives (stateSchemaParseAt) */
    const char* version;
    int portable;            /* whether states may convert to or from this build ("portable"; absent: 1) */
    const char* portableWhy; /* why not ("portable_why"), "" otherwise */
    const char* exe;
    const char* imageId; /* "image": {"id"}: the image's identity in hex, "" if none */
    StType* types;
    int32_t typeCount;
    Map typeByTid;
    Map typeNames;    /* type_names: name -> type */
    const char** tidName; /* per type: statecanon tid_name, NULL if none */
    StVar* vars;
    int32_t varCount;
    Map varById, varByName, varPlatform; /* the last keyed by "file\nname" */
    StRange* scratch;
    int32_t scratchCount;
    StFunc* funcs;
    int32_t funcCount;
    Map funcByAddr;   /* the last function at an address (statecanon funcs) */
    Map funcById;     /* identity and "fn:name" -> address index in funcAddrs */
    uint64_t* funcIdAddr;
    int32_t funcIdCount;
    Map retailByName; /* name -> function index (the last of a name) */
    StSymbol* symbols;
    int32_t symbolCount;
    Map symAddr;      /* name -> symbol index (the first) */
    StSymbol** byAddr; /* the symAddr entries sorted by (address, name): symbol_at */
    int32_t byAddrCount;
    TextSyms* text;   /* code addresses with their names, sorted by address */
    int32_t textCount;
    const char** textNames;
    Map textByAddr;
    StRange* roNamed; /* sorted (lo, hi, name) */
    int32_t roNamedCount;
    uint64_t (*ro)[3]; /* lo, hi, file offset */
    int32_t roCount;
    StRange* sections;
    int32_t sectionCount;
    StRange* regions;
    int32_t regionCount;
    StRange* parts;   /* name, and the file in lo as a pointer */
    const char** partFiles;
    int32_t partCount;
    StHeapType* heapTypes;
    int32_t heapTypeCount;
    Map heapByHash;
    StCounted* counted;
    int32_t countedCount;
    Map countedByType, countedByName;
    StUnion* unions;
    int32_t unionCount;
    Map unionByType, unionByName;
    Map fnByRetail;  /* a retail address -> the function index with it, FN_AMBIGUOUS where several addresses have it */
    int64_t rulesLanguage, revision;
    StRule* rules;
    int32_t ruleCount;
};

#define FN_AMBIGUOUS (-2)

static uint32_t tidOf(const char* hex) {
    return (uint32_t)strtoul(hex, NULL, 16);
}

static int kindOf(const char* k) {
    static const char* const names[] = {"struct", "union", "int", "float", "ptr", "arr", "otag", "addr", "scratch"};
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        if (strcmp(k, names[i]) == 0) {
            return i;
        }
    }
    return ST_UNKNOWN;
}

static int64_t intOr(const JsonDoc* d, JsonRef obj, const char* key, int64_t none) {
    JsonRef r = jsonGet(d, obj, key);
    int t = jsonType(d, r);
    return t == JSON_INT || t == JSON_REAL ? jsonInt(d, r) : none;
}

static const char* stringOr(Chunk** arena, const JsonDoc* d, JsonRef obj, const char* key) {
    JsonRef r = jsonGet(d, obj, key);
    return jsonType(d, r) == JSON_STRING ? arenaString(arena, jsonString(d, r)) : NULL;
}

static int32_t typeRef(const StateSchema* s, const JsonDoc* d, JsonRef obj, const char* key) {
    JsonRef r = jsonGet(d, obj, key);
    return jsonType(d, r) == JSON_STRING ? getName(&s->typeByTid, jsonString(d, r)) : ST_NONE;
}

static int loadTypes(StateSchema* s, const JsonDoc* d, JsonRef types) {
    s->typeCount = jsonCount(d, types);
    s->types = arenaAlloc(&s->arena, (size_t)s->typeCount * sizeof *s->types + 1);
    if (!s->types) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, types); r != JSON_NONE; r = jsonNext(d, r), i++) {
        s->types[i].tid = arenaString(&s->arena, jsonKey(d, r));
        if (!s->types[i].tid || !putName(&s->typeByTid, s->types[i].tid, i, 1)) return 0;
    }
    i = 0;
    for (JsonRef r = jsonFirst(d, types); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StType* t = &s->types[i];
        JsonRef k = jsonGet(d, r, "k");
        t->kind = (uint8_t)kindOf(jsonString(d, k));
        t->size = intOr(d, r, "size", 0);
        t->isSigned = jsonType(d, jsonGet(d, r, "signed")) == JSON_TRUE;
        t->fn = jsonType(d, jsonGet(d, r, "fn")) == JSON_TRUE;
        t->to = typeRef(s, d, r, "to");
        t->of = typeRef(s, d, r, "of");
        t->n = intOr(d, r, "n", 0);
        const char* name = stringOr(&s->arena, d, r, "name");
        t->name = name ? name : "";
        /* MinGW can also describe the same aliases as base integer types in
         * individual compilation units, including inside saved structs. */
        if (t->kind == ST_INT && t->size == 8) {
            if (strcmp(t->name, "OTag") == 0) {
                t->kind = ST_OTAG;
            } else if (strcmp(t->name, "uptr") == 0 || strcmp(t->name, "sptr") == 0) {
                t->kind = ST_ADDR;
            }
        }
        JsonRef fields = jsonGet(d, r, "fields");
        if (jsonType(d, fields) == JSON_ARRAY) {
            t->fieldCount = jsonCount(d, fields);
            t->fields = arenaAlloc(&s->arena, (size_t)t->fieldCount * sizeof *t->fields + 1);
            if (!t->fields) return 0;
            int32_t j = 0;
            for (JsonRef f = jsonFirst(d, fields); f != JSON_NONE; f = jsonNext(d, f), j++) {
                StField* sf = &t->fields[j];
                const char* fname = stringOr(&s->arena, d, f, "name");
                sf->name = fname ? fname : "";
                sf->off = intOr(d, f, "off", 0);
                sf->type = typeRef(s, d, f, "type");
                sf->retail = intOr(d, f, "retail", ST_NO_RETAIL);
                sf->bits = (int32_t)intOr(d, f, "bits", 0);
                sf->bit = (int32_t)intOr(d, f, "bit", 0);
            }
        }
    }
    return 1;
}

typedef struct {
    const char* name;
    int32_t type;
} NamedType;

static int byTypeName(const void* a, const void* b) {
    return strcmp(((const NamedType*)a)->name, ((const NamedType*)b)->name);
}

/* type_names, and tid_name: for name, t in sorted(type_names.items()): the type's own name wins,
 * else the first name in that order */
static int loadTypeNames(StateSchema* s, const JsonDoc* d, JsonRef names) {
    int32_t n = jsonCount(d, names);
    s->tidName = arenaAlloc(&s->arena, (size_t)s->typeCount * sizeof *s->tidName + 1);
    if (!s->tidName) return 0;
    NamedType* rows = malloc((size_t)(n ? n : 1) * sizeof *rows);
    if (!rows) return 0;
    int32_t m = 0;
    for (JsonRef r = jsonFirst(d, names); r != JSON_NONE; r = jsonNext(d, r)) {
        const char* name = arenaString(&s->arena, jsonKey(d, r));
        int32_t t = jsonType(d, r) == JSON_STRING ? getName(&s->typeByTid, jsonString(d, r)) : ST_NONE;
        if (!name || (t != ST_NONE && !putName(&s->typeNames, name, t, 1))) {
            free(rows);
            return 0;
        }
        rows[m].name = name;
        rows[m].type = t;
        m++;
    }
    /* by name (the keys are unique; Python's order by code point is strcmp's on UTF-8) */
    qsort(rows, (size_t)m, sizeof *rows, byTypeName);
    for (int32_t i = 0; i < m; i++) {
        int32_t t = rows[i].type;
        if (t == ST_NONE) {
            continue;
        }
        if (strcmp(s->types[t].name, rows[i].name) == 0 || s->tidName[t] == NULL) {
            s->tidName[t] = rows[i].name;
        }
    }
    free(rows);
    return 1;
}

static int byAddrVar(const void* a, const void* b) {
    const StVar* x = a;
    const StVar* y = b;
    return (x->addr > y->addr) - (x->addr < y->addr);
}

static int loadVars(StateSchema* s, const JsonDoc* d, JsonRef vars) {
    int32_t n = jsonCount(d, vars);
    StVar* all = calloc((size_t)(n ? n : 1), sizeof *all);
    Map seen = {0};
    if (!all) return 0;
    int32_t m = 0;
    for (JsonRef r = jsonFirst(d, vars); r != JSON_NONE; r = jsonNext(d, r)) {
        JsonRef tid = jsonGet(d, r, "tid");
        if (jsonType(d, tid) != JSON_STRING || !jsonString(d, tid)[0]) {
            continue;
        }
        StVar v = {0};
        v.name = stringOr(&s->arena, d, r, "name");
        v.addr = (uint64_t)intOr(d, r, "addr", 0) + s->bias;
        v.size = intOr(d, r, "size", 0);
        v.module = stringOr(&s->arena, d, r, "module");
        v.section = stringOr(&s->arena, d, r, "section");
        v.retail = intOr(d, r, "retail", ST_NO_RETAIL);
        v.platform = stringOr(&s->arena, d, r, "platform");
        v.type = getName(&s->typeByTid, jsonString(d, tid));
        v.isConst = jsonType(d, jsonGet(d, r, "const")) == JSON_TRUE;
        if (!v.name) v.name = "";
        int32_t at = mapGet(&seen, v.addr, NULL);
        if (at == ST_NONE) {
            all[m] = v;
            if (!mapPut(&seen, v.addr, NULL, m, 1)) {
                mapFree(&seen);
                free(all);
                return 0;
            }
            m++;
        } else if (v.size > all[at].size) {
            all[at] = v; /* the larger one of an address */
        }
    }
    mapFree(&seen);
    qsort(all, (size_t)m, sizeof *all, byAddrVar);
    s->vars = arenaAlloc(&s->arena, (size_t)m * sizeof *s->vars + 1);
    if (!s->vars) {
        free(all);
        return 0;
    }
    memcpy(s->vars, all, (size_t)m * sizeof *all);
    free(all);
    s->varCount = m;
    Map taken = {0};
    char buf[512];
    for (int32_t i = 0; i < m; i++) {
        StVar* v = &s->vars[i];
        if (v->module) {
            if (v->retail != ST_NO_RETAIL) {
                snprintf(buf, sizeof buf, "%s:%08llx", v->module, (unsigned long long)v->retail);
            } else {
                snprintf(buf, sizeof buf, "%s:pc:%s", v->module, v->name);
            }
        } else {
            snprintf(buf, sizeof buf, "pc:%s:%s", v->platform ? v->platform : "None", v->name);
        }
        if (getName(&taken, buf) != ST_NONE) {
            size_t len = strlen(buf);
            snprintf(buf + len, sizeof buf - len, ":%s", v->name); /* an alias at one retail address */
        }
        v->id = arenaString(&s->arena, buf);
        if (!v->id || !putName(&taken, v->id, i, 1) || !putName(&s->varById, v->id, i, 1) ||
            !putName(&s->varByName, v->name, i, 0)) {
            mapFree(&taken);
            return 0;
        }
        if (v->platform) {
            snprintf(buf, sizeof buf, "%s\n%s", v->platform, v->name);
            const char* key = arenaString(&s->arena, buf);
            if (!key || !putName(&s->varPlatform, key, i, 1)) {
                mapFree(&taken);
                return 0;
            }
        }
    }
    mapFree(&taken);
    /* scratch64 variables (through arrays) */
    s->scratch = arenaAlloc(&s->arena, (size_t)m * sizeof *s->scratch + 1);
    if (!s->scratch) return 0;
    for (int32_t i = 0; i < m; i++) {
        int32_t t = s->vars[i].type;
        for (int depth = 0; t != ST_NONE && s->types[t].kind == ST_ARR && depth < 64; depth++) {
            t = s->types[t].of; /* (bounded: a damaged schema could loop) */
        }
        if (t != ST_NONE && s->types[t].kind == ST_SCRATCH) {
            s->scratch[s->scratchCount].lo = s->vars[i].addr;
            s->scratch[s->scratchCount].hi = s->vars[i].addr + (uint64_t)s->vars[i].size;
            s->scratchCount++;
        }
    }
    return 1;
}

static void funcIdentity(const char* name, int64_t retail, char* out, size_t size) {
    if (retail != ST_NO_RETAIL) {
        snprintf(out, size, "fn:%08llx:%s", (unsigned long long)retail, name);
    } else {
        snprintf(out, size, "fn:%s", name);
    }
}

static int putFuncId(StateSchema* s, const char* id, uint64_t addr) {
    if (getName(&s->funcById, id) != ST_NONE) {
        return 1; /* setdefault: the first */
    }
    const char* key = arenaString(&s->arena, id);
    if (!key) return 0;
    s->funcIdAddr[s->funcIdCount] = addr;
    if (!putName(&s->funcById, key, s->funcIdCount, 0)) return 0;
    s->funcIdCount++;
    return 1;
}

static int loadFuncs(StateSchema* s, const JsonDoc* d, JsonRef funcs) {
    int32_t n = jsonCount(d, funcs);
    s->funcs = arenaAlloc(&s->arena, (size_t)n * sizeof *s->funcs + 1);
    s->funcIdAddr = arenaAlloc(&s->arena, (size_t)n * 2 * sizeof *s->funcIdAddr + 1);
    if (!s->funcs || !s->funcIdAddr) return 0;
    char buf[512];
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, funcs); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StFunc* f = &s->funcs[i];
        const char* name = stringOr(&s->arena, d, r, "name");
        f->name = name ? name : "";
        f->addr = (uint64_t)intOr(d, r, "addr", 0) + s->bias;
        f->retail = intOr(d, r, "retail", ST_NO_RETAIL);
        funcIdentity(f->name, f->retail, buf, sizeof buf);
        f->id = arenaString(&s->arena, buf);
        if (!f->id || !mapPut(&s->funcByAddr, f->addr, NULL, i, 1) || !putName(&s->retailByName, f->name, i, 1)) {
            return 0;
        }
        if (!putFuncId(s, f->id, f->addr)) return 0;
        snprintf(buf, sizeof buf, "fn:%s", f->name);
        if (!putFuncId(s, buf, f->addr)) return 0;
        if (f->retail != ST_NO_RETAIL) { /* statecanon fn_by_retail: one address, else none */
            int32_t k = mapGet(&s->fnByRetail, (uint64_t)f->retail, NULL);
            if (k == ST_NONE) {
                if (!mapPut(&s->fnByRetail, (uint64_t)f->retail, NULL, i, 0)) return 0;
            } else if (k != FN_AMBIGUOUS && s->funcs[k].addr != f->addr) {
                if (!mapPut(&s->fnByRetail, (uint64_t)f->retail, NULL, FN_AMBIGUOUS, 1)) return 0;
            }
        }
    }
    s->funcCount = n;
    return 1;
}

static int64_t retailOfName(const StateSchema* s, const char* name) {
    int32_t i = getName(&s->retailByName, name);
    return i == ST_NONE ? ST_NO_RETAIL : s->funcs[i].retail;
}

static int bySymbolAddrName(const void* a, const void* b) {
    const StSymbol* x = *(StSymbol* const*)a;
    const StSymbol* y = *(StSymbol* const*)b;
    if (x->addr != y->addr) return (x->addr > y->addr) - (x->addr < y->addr);
    return strcmp(x->name, y->name);
}

static int byRange(const void* a, const void* b) {
    const StRange* x = a;
    const StRange* y = b;
    if (x->lo != y->lo) return (x->lo > y->lo) - (x->lo < y->lo);
    if (x->hi != y->hi) return (x->hi > y->hi) - (x->hi < y->hi);
    return strcmp(x->name, y->name);
}

/* text_syms' names: those with a retail address first, then by name */
typedef struct {
    const char* name;
    int noRetail;
} TextName;

static int byRetailThenName(const void* a, const void* b) {
    const TextName* x = a;
    const TextName* y = b;
    if (x->noRetail != y->noRetail) return x->noRetail - y->noRetail;
    return strcmp(x->name, y->name);
}

static int startsWith(const char* s, const char* p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static int loadSymbols(StateSchema* s, const JsonDoc* d, JsonRef syms) {
    int32_t n = jsonCount(d, syms);
    s->symbols = arenaAlloc(&s->arena, (size_t)n * sizeof *s->symbols + 1);
    s->byAddr = arenaAlloc(&s->arena, (size_t)n * sizeof *s->byAddr + 1);
    s->roNamed = arenaAlloc(&s->arena, (size_t)n * sizeof *s->roNamed + 1);
    s->text = arenaAlloc(&s->arena, (size_t)n * sizeof *s->text + 1);
    s->textNames = arenaAlloc(&s->arena, (size_t)n * sizeof *s->textNames + 1);
    if (!s->symbols || !s->byAddr || !s->roNamed || !s->text || !s->textNames) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, syms); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StSymbol* y = &s->symbols[i];
        JsonRef e = jsonFirst(d, r);
        y->name = arenaString(&s->arena, jsonString(d, e));
        e = jsonNext(d, e);
        y->addr = (uint64_t)jsonInt(d, e) + s->bias;
        e = jsonNext(d, e);
        y->size = (uint64_t)jsonInt(d, e);
        e = jsonNext(d, e);
        y->kind = jsonString(d, e)[0];
        if (!y->name) return 0;
        if (getName(&s->symAddr, y->name) == ST_NONE) {
            if (!putName(&s->symAddr, y->name, i, 0)) return 0;
            s->byAddr[s->byAddrCount++] = y;
        }
    }
    s->symbolCount = n;
    /* text names by address, in the symbols' order, then each address's sorted */
    int32_t* count = calloc((size_t)(n ? n : 1), sizeof *count);
    if (!count) return 0;
    for (i = 0; i < n; i++) {
        StSymbol* y = &s->symbols[i];
        if (y->kind == 't' || y->kind == 'T' || y->kind == 'w' || y->kind == 'W') {
            int32_t t = mapGet(&s->textByAddr, y->addr, NULL);
            if (t == ST_NONE) {
                t = s->textCount++;
                s->text[t].addr = y->addr;
                if (!mapPut(&s->textByAddr, y->addr, NULL, t, 1)) {
                    free(count);
                    return 0;
                }
            }
            count[t]++;
        } else if ((y->kind == 'r' || y->kind == 'R') && y->size && !startsWith(y->name, ".L") &&
                   !startsWith(y->name, "xgsrc_")) {
            StRange* c = &s->roNamed[s->roNamedCount++];
            c->name = y->name;
            c->lo = y->addr;
            c->hi = y->addr + y->size;
        }
    }
    int32_t at = 0;
    for (int32_t t = 0; t < s->textCount; t++) {
        s->text[t].names = at;
        at += count[t];
        count[t] = 0;
    }
    for (i = 0; i < n; i++) {
        StSymbol* y = &s->symbols[i];
        if (y->kind == 't' || y->kind == 'T' || y->kind == 'w' || y->kind == 'W') {
            int32_t t = mapGet(&s->textByAddr, y->addr, NULL);
            s->textNames[s->text[t].names + count[t]++] = y->name;
        }
    }
    TextName* names = malloc((size_t)(n ? n : 1) * sizeof *names);
    if (!names) {
        free(count);
        return 0;
    }
    for (int32_t t = 0; t < s->textCount; t++) {
        s->text[t].nameCount = count[t];
        const char** row = s->textNames + s->text[t].names;
        for (int32_t k = 0; k < count[t]; k++) {
            names[k].name = row[k];
            names[k].noRetail = retailOfName(s, row[k]) == ST_NO_RETAIL;
        }
        qsort(names, (size_t)count[t], sizeof *names, byRetailThenName);
        for (int32_t k = 0; k < count[t]; k++) {
            row[k] = names[k].name;
        }
    }
    free(names);
    free(count);
    qsort(s->byAddr, (size_t)s->byAddrCount, sizeof *s->byAddr, bySymbolAddrName);
    qsort(s->roNamed, (size_t)s->roNamedCount, sizeof *s->roNamed, byRange);
    return 1;
}

static int loadRanges(StateSchema* s, const JsonDoc* d, JsonRef obj, StRange** out, int32_t* count) {
    int32_t n = jsonCount(d, obj);
    *out = arenaAlloc(&s->arena, (size_t)n * sizeof **out + 1);
    if (!*out) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, obj); r != JSON_NONE; r = jsonNext(d, r), i++) {
        JsonRef e = jsonFirst(d, r);
        (*out)[i].name = arenaString(&s->arena, jsonKey(d, r));
        (*out)[i].lo = (uint64_t)jsonInt(d, e) + s->bias;
        (*out)[i].hi = (uint64_t)jsonInt(d, jsonNext(d, e)) + s->bias;
        if (!(*out)[i].name) return 0;
    }
    *count = n;
    return 1;
}

static int loadHeapTypes(StateSchema* s, const JsonDoc* d, JsonRef obj) {
    int32_t n = jsonCount(d, obj);
    s->heapTypes = arenaAlloc(&s->arena, (size_t)n * sizeof *s->heapTypes + 1);
    if (!s->heapTypes) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, obj); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StHeapType* h = &s->heapTypes[i];
        h->hash = tidOf(jsonKey(d, r));
        h->name = stringOr(&s->arena, d, r, "name");
        h->type = ST_NONE;
        JsonRef spec = jsonGet(d, r, "spec");
        if (jsonType(d, spec) == JSON_OBJECT) {
            h->spec = ST_SPEC_TYPE;
            h->type = typeRef(s, d, spec, "tid");
            h->one = jsonType(d, jsonGet(d, spec, "one")) == JSON_TRUE;
        } else if (jsonType(d, spec) == JSON_STRING) {
            const char* k = jsonString(d, spec);
            h->spec = strcmp(k, "prims") == 0 ? ST_SPEC_PRIMS : strcmp(k, "ptrs") == 0 ? ST_SPEC_PTRS
                    : strcmp(k, "otags") == 0 ? ST_SPEC_OTAGS : ST_SPEC_NONE;
        } else {
            h->spec = ST_SPEC_NONE;
            /* Older MinGW schemas kept the allocation's type name but emitted
             * a null spec when DWARF described the typedef as a plain integer. */
            const char* name = h->name ? h->name : "";
            if (strcmp(name, "uptr") == 0 || strcmp(name, "sptr") == 0 ||
                strcmp(name, "uptr[1]") == 0 || strcmp(name, "sptr[1]") == 0) {
                h->spec = ST_SPEC_PTRS;
            } else if (strcmp(name, "OTag") == 0 || strcmp(name, "OTag[1]") == 0) {
                h->spec = ST_SPEC_OTAGS;
            }
        }
        if (!mapPut(&s->heapByHash, h->hash, NULL, i, 1)) return 0;
    }
    s->heapTypeCount = n;
    return 1;
}

static int loadCounted(StateSchema* s, const JsonDoc* d, JsonRef obj) {
    int32_t n = jsonCount(d, obj);
    s->counted = arenaAlloc(&s->arena, (size_t)n * sizeof *s->counted + 1);
    if (!s->counted) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, obj); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StCounted* c = &s->counted[i];
        c->type = getName(&s->typeByTid, jsonKey(d, r));
        c->name = stringOr(&s->arena, d, r, "name");
        c->array = stringOr(&s->arena, d, r, "array");
        c->count = stringOr(&s->arena, d, r, "count");
        c->extra = intOr(d, r, "extra", 0);
        const char* tid = arenaString(&s->arena, jsonKey(d, r));
        if (!c->name || !tid || !putName(&s->countedByType, tid, i, 1) ||
            !putName(&s->countedByName, c->name, i, 1)) {
            return 0;
        }
    }
    s->countedCount = n;
    return 1;
}

static int loadUnions(StateSchema* s, const JsonDoc* d, JsonRef obj) {
    int32_t n = jsonCount(d, obj);
    s->unions = arenaAlloc(&s->arena, (size_t)n * sizeof *s->unions + 1);
    if (!s->unions) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, obj); r != JSON_NONE; r = jsonNext(d, r), i++) {
        StUnion* u = &s->unions[i];
        u->type = getName(&s->typeByTid, jsonKey(d, r));
        u->name = stringOr(&s->arena, d, r, "name");
        u->member = stringOr(&s->arena, d, r, "member");
        u->selector = stringOr(&s->arena, d, r, "selector");
        JsonRef views = jsonGet(d, r, "views");
        u->viewCount = jsonCount(d, views);
        u->views = arenaAlloc(&s->arena, (size_t)u->viewCount * sizeof *u->views + 1);
        if (!u->name || !u->views) return 0;
        int32_t j = 0;
        for (JsonRef v = jsonFirst(d, views); v != JSON_NONE; v = jsonNext(d, v), j++) {
            u->views[j] = arenaString(&s->arena, jsonString(d, v));
        }
        const char* tid = arenaString(&s->arena, jsonKey(d, r));
        if (!tid || !putName(&s->unionByType, tid, i, 1) || !putName(&s->unionByName, u->name, i, 1)) {
            return 0;
        }
    }
    s->unionCount = n;
    return 1;
}

static int isType(const JsonDoc* d, JsonRef r, int type) { return r != JSON_NONE && jsonType(d, r) == type; }

/* A JSON array of strings (statecanon rule_target's strs), copied into *out; 0 if it is not one. */
static int stringList(StateSchema* s, const JsonDoc* d, JsonRef r, const char*** out, int32_t* n) {
    if (!isType(d, r, JSON_ARRAY)) return 0;
    for (JsonRef e = jsonFirst(d, r); e != JSON_NONE; e = jsonNext(d, e)) {
        if (jsonType(d, e) != JSON_STRING) return 0;
    }
    *n = jsonCount(d, r);
    *out = arenaAlloc(&s->arena, (size_t)*n * sizeof **out + 1);
    if (!*out) return -1;
    int32_t i = 0;
    for (JsonRef e = jsonFirst(d, r); e != JSON_NONE; e = jsonNext(d, e), i++) {
        (*out)[i] = arenaString(&s->arena, jsonString(d, e));
        if (!(*out)[i]) return -1;
    }
    return 1;
}

/* One rule as statecanon rule_target reads it: readable or not. 0 when memory ran out. */
static int loadRule(StateSchema* s, const JsonDoc* d, JsonRef r, StRule* out) {
    static const char* const kinds[] = {"was", "values", "reset", "removed", "default", "hashes"};
    static const char* const keys[] = {"field", "var", "typename", "tag", "fn", "fields", "vars"};
    /* RULE_TARGETS: each kind's keys, in the order a rule's first present one is taken */
    static const int order[6][6] = {{ST_ON_FIELD, ST_ON_VAR, ST_ON_TYPENAME, ST_ON_TAG, ST_ON_FN, -1},
                                    {ST_ON_FIELD, ST_ON_VAR, -1},
                                    {ST_ON_FIELD, ST_ON_VAR, -1},
                                    {ST_ON_VARS, ST_ON_FIELDS, -1},
                                    {ST_ON_VAR, ST_ON_FIELD, -1},
                                    {ST_ON_FIELD, -1}};
    memset(out, 0, sizeof *out);
    out->at = stringOr(&s->arena, d, r, "at");
    if (!isType(d, r, JSON_OBJECT)) return 1;
    JsonRef k = jsonGet(d, r, "kind");
    out->kind = -1;
    for (int i = 0; i < 6 && isType(d, k, JSON_STRING); i++) {
        if (strcmp(jsonString(d, k), kinds[i]) == 0) out->kind = i;
    }
    if (out->kind < 0) return 1;
    if (out->kind != ST_RULE_DEFAULT && out->kind != ST_RULE_HASHES) {
        JsonRef rev = jsonGet(d, r, "rev");
        if (!isType(d, rev, JSON_INT)) return 1;
        out->rev = jsonInt(d, rev);
    }
    JsonRef target = JSON_NONE;
    out->on = -1;
    for (int i = 0; order[out->kind][i] >= 0; i++) {
        target = jsonGet(d, r, keys[order[out->kind][i]]);
        if (target != JSON_NONE) {
            out->on = order[out->kind][i];
            break;
        }
    }
    if (out->on < 0) return 1;
    if (out->on == ST_ON_FIELDS || out->on == ST_ON_VARS) {
        int ok = stringList(s, d, target, &out->names, &out->nameCount);
        if (ok <= 0) return ok < 0 ? 0 : 1;
    } else {
        if (!isType(d, target, JSON_STRING)) return 1;
        out->name = arenaString(&s->arena, jsonString(d, target));
        if (!out->name) return 0;
    }
    if (out->on == ST_ON_FIELD || out->on == ST_ON_FIELDS) {
        int ok = stringList(s, d, jsonGet(d, r, "tids"), &out->tids, &out->tidCount);
        if (ok <= 0) return ok < 0 ? 0 : 1;
        out->types = arenaAlloc(&s->arena, (size_t)out->tidCount * sizeof *out->types + 1);
        if (!out->types) return 0;
        for (int32_t i = 0; i < out->tidCount; i++) out->types[i] = getName(&s->typeByTid, out->tids[i]);
    }
    if (out->kind == ST_RULE_WAS) {
        JsonRef old = jsonGet(d, r, "old");
        if (!isType(d, old, JSON_STRING)) return 1;
        out->old = arenaString(&s->arena, jsonString(d, old));
        if (!out->old) return 0;
    }
    if (out->kind == ST_RULE_VALUES) {
        JsonRef map = jsonGet(d, r, "map");
        if (!isType(d, map, JSON_ARRAY)) return 1;
        for (JsonRef e = jsonFirst(d, map); e != JSON_NONE; e = jsonNext(d, e)) {
            JsonRef a = jsonFirst(d, e);
            JsonRef b = a == JSON_NONE ? JSON_NONE : jsonNext(d, a);
            if (!isType(d, e, JSON_ARRAY) || jsonCount(d, e) != 2 || !isType(d, a, JSON_INT) || !isType(d, b, JSON_INT)) {
                return 1;
            }
        }
        out->mapCount = jsonCount(d, map);
        out->map = arenaAlloc(&s->arena, (size_t)out->mapCount * sizeof *out->map + 1);
        if (!out->map) return 0;
        int32_t i = 0;
        for (JsonRef e = jsonFirst(d, map); e != JSON_NONE; e = jsonNext(d, e), i++) {
            JsonRef a = jsonFirst(d, e);
            out->map[i][0] = jsonInt(d, a);
            out->map[i][1] = jsonInt(d, jsonNext(d, a));
        }
    }
    if (out->kind == ST_RULE_REMOVED || out->kind == ST_RULE_DEFAULT) {
        JsonRef v = jsonGet(d, r, "value");
        if (!isType(d, v, JSON_INT)) return 1;
        out->value = jsonInt(d, v);
    }
    out->readable = 1;
    return 1;
}

/* "rules": {"language", "revision", "list"}; absent (a schema from before rules): revision 0, none. */
static int loadRules(StateSchema* s, const JsonDoc* d, JsonRef obj) {
    s->rulesLanguage = 0;
    s->revision = 0;
    if (!isType(d, obj, JSON_OBJECT)) return 1;
    JsonRef lang = jsonGet(d, obj, "language"), rev = jsonGet(d, obj, "revision"), list = jsonGet(d, obj, "list");
    if (isType(d, lang, JSON_INT)) s->rulesLanguage = jsonInt(d, lang);
    if (isType(d, rev, JSON_INT)) s->revision = jsonInt(d, rev);
    if (!isType(d, list, JSON_ARRAY)) return 1;
    int32_t n = jsonCount(d, list);
    s->rules = arenaAlloc(&s->arena, (size_t)n * sizeof *s->rules + 1);
    if (!s->rules) return 0;
    int32_t i = 0;
    for (JsonRef r = jsonFirst(d, list); r != JSON_NONE; r = jsonNext(d, r), i++) {
        if (!loadRule(s, d, r, &s->rules[i])) return 0;
    }
    s->ruleCount = n;
    return 1;
}

StateSchema* stateSchemaParseAt(const char* json, size_t n, uint64_t bias, char* err, size_t errSize) {
    JsonDoc* d = jsonParse(json, n, err, errSize);
    if (!d) {
        return NULL;
    }
    StateSchema* s = calloc(1, sizeof *s);
    JsonRef root = jsonRoot(d);
    int ok = s && jsonType(d, root) == JSON_OBJECT;
    if (ok) {
        s->bias = bias; /* before anything is indexed by address: the indexes are those of the rebased addresses */
        const char* v = stringOr(&s->arena, d, root, "version");
        s->version = v ? v : "";
        JsonRef portable = jsonGet(d, root, "portable");
        s->portable = jsonType(d, portable) != JSON_FALSE;
        const char* why = stringOr(&s->arena, d, root, "portable_why");
        s->portableWhy = why ? why : "";
        const char* exe = stringOr(&s->arena, d, root, "exe");
        s->exe = exe ? exe : "";
        const char* image = stringOr(&s->arena, d, jsonGet(d, root, "image"), "id");
        s->imageId = image ? image : "";
        ok = loadTypes(s, d, jsonGet(d, root, "types")) && loadTypeNames(s, d, jsonGet(d, root, "type_names")) &&
             loadVars(s, d, jsonGet(d, root, "variables")) && loadFuncs(s, d, jsonGet(d, root, "functions")) &&
             loadSymbols(s, d, jsonGet(d, root, "symbols")) &&
             loadRanges(s, d, jsonGet(d, root, "sections"), &s->sections, &s->sectionCount) &&
             loadRanges(s, d, jsonGet(d, root, "regions"), &s->regions, &s->regionCount) &&
             loadHeapTypes(s, d, jsonGet(d, root, "heap_types")) && loadCounted(s, d, jsonGet(d, root, "counted")) &&
             loadUnions(s, d, jsonGet(d, root, "unions")) && loadRules(s, d, jsonGet(d, root, "rules"));
    }
    if (ok) {
        JsonRef ro = jsonGet(d, root, "ro");
        s->roCount = jsonCount(d, ro);
        s->ro = arenaAlloc(&s->arena, (size_t)s->roCount * sizeof *s->ro + 1);
        ok = s->ro != NULL;
        int32_t i = 0;
        for (JsonRef r = jsonFirst(d, ro); ok && r != JSON_NONE; r = jsonNext(d, r), i++) {
            JsonRef e = jsonFirst(d, r);
            for (int k = 0; k < 3; k++, e = jsonNext(d, e)) {
                s->ro[i][k] = (uint64_t)jsonInt(d, e) + (k < 2 ? s->bias : 0); /* (the file offset as it is) */
            }
        }
        JsonRef parts = jsonGet(d, root, "parts");
        s->partCount = jsonCount(d, parts);
        s->parts = arenaAlloc(&s->arena, (size_t)s->partCount * sizeof *s->parts + 1);
        s->partFiles = arenaAlloc(&s->arena, (size_t)s->partCount * sizeof *s->partFiles + 1);
        ok = ok && s->parts && s->partFiles;
        i = 0;
        for (JsonRef r = jsonFirst(d, parts); ok && r != JSON_NONE; r = jsonNext(d, r), i++) {
            s->parts[i].name = arenaString(&s->arena, jsonKey(d, r));
            s->partFiles[i] = arenaString(&s->arena, jsonString(d, r));
        }
        if (!ok && err && errSize) snprintf(err, errSize, "out of memory");
    } else if (s && err && errSize) {
        snprintf(err, errSize, "not a state schema, or out of memory");
    }
    jsonFree(d);
    if (!ok) {
        stateSchemaFree(s);
        return NULL;
    }
    return s;
}

StateSchema* stateSchemaParse(const char* json, size_t n, char* err, size_t errSize) {
    return stateSchemaParseAt(json, n, 0, err, errSize);
}

#define PACKED_MAGIC "XGSCHEMA"

StateSchema* stateSchemaLoadAt(const char* path, uint64_t bias, char* err, size_t errSize) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (err && errSize) snprintf(err, errSize, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = size > 0 ? malloc((size_t)size) : NULL;
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        if (err && errSize) snprintf(err, errSize, "cannot read %s", path);
        return NULL;
    }
    fclose(f);
    StateSchema* s = NULL;
    if ((size_t)size >= 16 && memcmp(data, PACKED_MAGIC, 8) == 0) {
        uint64_t raw;
        memcpy(&raw, data + 8, 8);
        char* text = raw < ((uint64_t)1 << 32) ? malloc((size_t)raw + 1) : NULL;
        if (text && stateUnpack((const uint8_t*)data + 16, (size_t)size - 16, (uint8_t*)text, (size_t)raw)) {
            s = stateSchemaParseAt(text, (size_t)raw, bias, err, errSize);
        } else if (err && errSize) {
            snprintf(err, errSize, "damaged packed schema %s", path);
        }
        free(text);
    } else {
        s = stateSchemaParseAt(data, (size_t)size, bias, err, errSize);
    }
    free(data);
    return s;
}

StateSchema* stateSchemaLoad(const char* path, char* err, size_t errSize) {
    return stateSchemaLoadAt(path, 0, err, errSize);
}

void stateSchemaFree(StateSchema* s) {
    if (!s) return;
    Map* maps[] = {&s->typeByTid, &s->typeNames, &s->varById, &s->varByName, &s->varPlatform, &s->funcByAddr,
                   &s->funcById, &s->retailByName, &s->symAddr, &s->textByAddr, &s->heapByHash, &s->countedByType,
                   &s->countedByName, &s->unionByType, &s->unionByName, &s->fnByRetail};
    for (size_t i = 0; i < sizeof maps / sizeof maps[0]; i++) {
        mapFree(maps[i]);
    }
    while (s->arena) {
        Chunk* next = s->arena->next;
        free(s->arena);
        s->arena = next;
    }
    free(s);
}

/* ---- queries ------------------------------------------------------------------------------------ */

const char* stateSchemaVersion(const StateSchema* s) { return s->version; }
int stateSchemaPortable(const StateSchema* s, const char** why) {
    if (why) *why = s->portableWhy;
    return s->portable;
}
uint64_t stateSchemaBias(const StateSchema* s) { return s->bias; }
const char* stateSchemaImageId(const StateSchema* s) { return s->imageId; }
int32_t stateSchemaTypeCount(const StateSchema* s) { return s->typeCount; }
const StType* stateSchemaTypeAt(const StateSchema* s, int32_t i) {
    return i >= 0 && i < s->typeCount ? &s->types[i] : NULL;
}
int32_t stateSchemaType(const StateSchema* s, const char* tid) { return getName(&s->typeByTid, tid); }
int32_t stateSchemaTypeNamed(const StateSchema* s, const char* name) { return getName(&s->typeNames, name); }
const char* stateSchemaTidName(const StateSchema* s, int32_t type) {
    return type >= 0 && type < s->typeCount ? s->tidName[type] : NULL;
}

int32_t stateSchemaVarCount(const StateSchema* s) { return s->varCount; }
const StVar* stateSchemaVarAt(const StateSchema* s, int32_t i) {
    return i >= 0 && i < s->varCount ? &s->vars[i] : NULL;
}
int32_t stateSchemaVarById(const StateSchema* s, const char* id) { return getName(&s->varById, id); }
int32_t stateSchemaVarNamed(const StateSchema* s, const char* name) { return getName(&s->varByName, name); }
int32_t stateSchemaPlatformVar(const StateSchema* s, const char* file, const char* name) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s\n%s", file, name);
    return getName(&s->varPlatform, buf);
}

/* statecanon Schema.var_at: the spans are the variables' (address, address + max(size, 1)) */
int32_t stateSchemaVarHolding(const StateSchema* s, uint64_t addr) {
    int32_t lo = 0, hi = s->varCount; /* bisect_right on the addresses */
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (s->vars[mid].addr <= addr) lo = mid + 1;
        else hi = mid;
    }
    int64_t a = (int64_t)addr;
    for (int32_t i = lo - 1; i >= 0;) {
        int64_t vlo = (int64_t)s->vars[i].addr;
        int64_t vhi = vlo + (s->vars[i].size > 1 ? s->vars[i].size : 1);
        if (vlo <= a && a < vhi) {
            return i;
        }
        if (vhi <= a && i < s->varCount - 1 && vlo < a - (1 << 24)) {
            break;
        }
        i--;
        if (i >= 0 && a - (int64_t)s->vars[i].addr > (1 << 22)) {
            break;
        }
    }
    return ST_NONE;
}

int32_t stateSchemaScratchCount(const StateSchema* s) { return s->scratchCount; }
StRange stateSchemaScratch(const StateSchema* s, int32_t i) { return s->scratch[i]; }

int32_t stateSchemaFuncCount(const StateSchema* s) { return s->funcCount; }
const StFunc* stateSchemaFuncAt(const StateSchema* s, int32_t i) {
    return i >= 0 && i < s->funcCount ? &s->funcs[i] : NULL;
}

int stateSchemaFnIdentity(const StateSchema* s, uint64_t addr, char* out, size_t size) {
    int32_t f = mapGet(&s->funcByAddr, addr, NULL);
    if (f != ST_NONE) {
        snprintf(out, size, "%s", s->funcs[f].id);
        return 1;
    }
    int32_t t = mapGet(&s->textByAddr, addr, NULL);
    if (t != ST_NONE && s->text[t].nameCount) {
        const char* name = s->textNames[s->text[t].names];
        funcIdentity(name, retailOfName(s, name), out, size);
        return 1;
    }
    return 0;
}

int stateSchemaFnFind(const StateSchema* s, const char* identity, uint64_t* addr) {
    int32_t i = getName(&s->funcById, identity);
    if (i != ST_NONE) {
        *addr = s->funcIdAddr[i];
        return 1;
    }
    const char* name = strrchr(identity, ':'); /* ident.rsplit(":", 1)[-1] */
    name = name ? name + 1 : identity;
    char buf[512];
    snprintf(buf, sizeof buf, "fn:%s", name);
    i = getName(&s->funcById, buf);
    if (i != ST_NONE) {
        *addr = s->funcIdAddr[i];
        return 1;
    }
    if (stateSchemaSymbolFind(s, name, addr)) return 1;
    /* a game function renamed: "fn:<8 hex digits>:...", its retail address where one function has it */
    if (strncmp(identity, "fn:", 3) != 0 || strlen(identity) < 12 || identity[11] != ':') return 0;
    uint64_t retail = 0;
    for (int k = 3; k < 11; k++) {
        char ch = identity[k];
        int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (v < 0) return 0;
        retail = retail * 16 + (uint64_t)v;
    }
    int32_t f = mapGet(&s->fnByRetail, retail, NULL);
    if (f < 0) return 0;
    *addr = s->funcs[f].addr;
    return 1;
}

uint64_t stateSchemaFnAddress(const StateSchema* s, const char* identity) {
    uint64_t a = 0;
    return stateSchemaFnFind(s, identity, &a) ? a : 0;
}

int32_t stateSchemaSymbolCount(const StateSchema* s) { return s->symbolCount; }
const StSymbol* stateSchemaSymbolAt(const StateSchema* s, int32_t i) {
    return i >= 0 && i < s->symbolCount ? &s->symbols[i] : NULL;
}

uint64_t stateSchemaSymbolAddress(const StateSchema* s, const char* name) {
    int32_t i = getName(&s->symAddr, name);
    return i == ST_NONE ? 0 : s->symbols[i].addr;
}

int stateSchemaSymbolFind(const StateSchema* s, const char* name, uint64_t* addr) {
    int32_t i = getName(&s->symAddr, name);
    if (i == ST_NONE) {
        return 0;
    }
    *addr = s->symbols[i].addr;
    return 1;
}

const char* stateSchemaExe(const StateSchema* s) { return s->exe; }

const char* stateSchemaSymbolBelow(const StateSchema* s, uint64_t addr, uint64_t* at) {
    int32_t lo = 0, hi = s->byAddrCount;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (s->byAddr[mid]->addr <= addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) {
        return NULL;
    }
    if (at) *at = s->byAddr[lo - 1]->addr;
    return s->byAddr[lo - 1]->name;
}

const StRange* stateSchemaNamedConstant(const StateSchema* s, uint64_t addr) {
    int32_t lo = 0, hi = s->roNamedCount;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (s->roNamed[mid].lo <= addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0 && s->roNamed[lo - 1].lo <= addr && addr < s->roNamed[lo - 1].hi) {
        return &s->roNamed[lo - 1];
    }
    return NULL;
}

int stateSchemaReadOnly(const StateSchema* s, uint64_t addr, uint64_t* fileOffset) {
    for (int32_t i = 0; i < s->roCount; i++) {
        if (s->ro[i][0] <= addr && addr < s->ro[i][1]) {
            if (fileOffset) *fileOffset = s->ro[i][2] + (addr - s->ro[i][0]);
            return 1;
        }
    }
    return 0;
}

int32_t stateSchemaReadOnlyCount(const StateSchema* s) { return s->roCount; }

void stateSchemaReadOnlyAt(const StateSchema* s, int32_t i, uint64_t* lo, uint64_t* hi, uint64_t* fileOffset) {
    *lo = s->ro[i][0];
    *hi = s->ro[i][1];
    *fileOffset = s->ro[i][2];
}

static StRange findRange(const StRange* r, int32_t n, const char* name) {
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(r[i].name, name) == 0) {
            return r[i];
        }
    }
    StRange none = {NULL, 0, 0};
    return none;
}

StRange stateSchemaSection(const StateSchema* s, const char* name) { return findRange(s->sections, s->sectionCount, name); }
int32_t stateSchemaSectionCount(const StateSchema* s) { return s->sectionCount; }
StRange stateSchemaSectionAt(const StateSchema* s, int32_t i) { return s->sections[i]; }
StRange stateSchemaRegion(const StateSchema* s, const char* name) { return findRange(s->regions, s->regionCount, name); }

const char* stateSchemaPartFile(const StateSchema* s, const char* part) {
    for (int32_t i = 0; i < s->partCount; i++) {
        if (strcmp(s->parts[i].name, part) == 0) {
            return s->partFiles[i];
        }
    }
    return NULL;
}

const StHeapType* stateSchemaHeapType(const StateSchema* s, uint32_t hash) {
    int32_t i = mapGet(&s->heapByHash, hash, NULL);
    return i == ST_NONE ? NULL : &s->heapTypes[i];
}
int32_t stateSchemaHeapTypeCount(const StateSchema* s) { return s->heapTypeCount; }

const StCounted* stateSchemaCounted(const StateSchema* s, int32_t type) {
    if (type < 0 || type >= s->typeCount) return NULL;
    int32_t i = getName(&s->countedByType, s->types[type].tid);
    return i == ST_NONE ? NULL : &s->counted[i];
}
const StCounted* stateSchemaCountedNamed(const StateSchema* s, const char* name) {
    int32_t i = getName(&s->countedByName, name);
    return i == ST_NONE ? NULL : &s->counted[i];
}
const StUnion* stateSchemaUnion(const StateSchema* s, int32_t type) {
    if (type < 0 || type >= s->typeCount) return NULL;
    int32_t i = getName(&s->unionByType, s->types[type].tid);
    return i == ST_NONE ? NULL : &s->unions[i];
}
const StUnion* stateSchemaUnionNamed(const StateSchema* s, const char* name) {
    int32_t i = getName(&s->unionByName, name);
    return i == ST_NONE ? NULL : &s->unions[i];
}

int64_t stateSchemaRulesLanguage(const StateSchema* s) { return s->rulesLanguage; }
int64_t stateSchemaRevision(const StateSchema* s) { return s->revision; }
int32_t stateSchemaRuleCount(const StateSchema* s) { return s->ruleCount; }
const StRule* stateSchemaRuleAt(const StateSchema* s, int32_t i) {
    return i >= 0 && i < s->ruleCount ? &s->rules[i] : NULL;
}
