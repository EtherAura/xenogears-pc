/*
 * statecanon.py convert in C (stateconvert.h; docs/CANONICAL_STATES.md §16).
 *
 * statecanon encodes the writer's raw state into a canonical document with
 * the writer's schema (encode), then decodes the document with the reader's
 * schema into the reader's layout (decode). Here the document is never
 * built: a canonical value is a view of the writer's bytes (AVal: what
 * Encoder.value makes of a type at an offset, and the dict, list or number
 * it stands for), made when the decoder asks for it. The memory used is the
 * output, the heap's layout and a few caches per type; the encoder's
 * warnings come from one walk over everything encode would encode.
 *
 * Each function follows the statecanon.py function named in its comment, in
 * Python's semantics where they reach the bytes: a slice clamps at a
 * buffer's end and negative indexes count from it, a slice assignment of
 * another length grows or shrinks the buffer, integers are exact, dict keys
 * are last-wins, sorts are stable. Where the reference raises (a value of one
 * shape decoded as a type that takes another), the conversion fails with a
 * message. statecanon's allocation sites (Sites, addr2line) only fill the
 * heap records' "site", which decode never reads: they are left out.
 */

#include "stateconvert.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HEAP_HEADER = 16, ARENA_HEADER = 32, MAX_STEPS = 72, FN_ID = 512, KEY_MAX = 600 };
/* A part of more elements than this is a damaged state's: statecanon would list them for hours (a block
 * whose size reads as 2^60); here the conversion fails instead. */
#define MAX_ELEMENTS (INT64_C(1) << 24)
#define PRIM_LINK ((UINT64_C(1) << 56) - 1)
#define MASK_BITS(n) ((n) >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << (n)) - 1)

/* ---- Python's semantics on bytes and integers --------------------------------------------------- */

typedef struct {
    const uint8_t* p;
    int64_t n;
} Mem;

/* [a, e) of n bytes as a Python slice takes it: negative from the end, clamped, never reversed. */
static void pySlice(int64_t n, int64_t* a, int64_t* e) {
    int64_t x = *a, y = *e;
    if (x < 0) {
        x += n;
        if (x < 0) x = 0;
    } else if (x > n) {
        x = n;
    }
    if (y < 0) {
        y += n;
        if (y < 0) y = 0;
    } else if (y > n) {
        y = n;
    }
    if (y < x) y = x;
    *a = x;
    *e = y;
}

/* mem[a:e]: its bytes in *p, and how many. */
static int64_t memSlice(const Mem* m, int64_t a, int64_t e, const uint8_t** p) {
    pySlice(m->n, &a, &e);
    *p = m->p + a;
    return e - a;
}

/* A Python int of at most 64 bits, and whether it is negative (its bits beyond 64 all set). */
typedef struct {
    uint64_t v;
    int neg;
} PyInt;

static PyInt pyUint(uint64_t v) {
    PyInt r = {v, 0};
    return r;
}

/* int.from_bytes(p[:n], "little", signed=sign), n <= 8 */
static PyInt bytesInt(const uint8_t* p, int64_t n, int sign) {
    PyInt r = {0, 0};
    for (int64_t i = 0; i < n && i < 8; i++) {
        r.v |= (uint64_t)p[i] << (8 * i);
    }
    if (sign && n > 0 && (p[n - 1] & 0x80)) {
        r.neg = 1;
        if (n < 8) r.v |= ~UINT64_C(0) << (8 * n);
    }
    return r;
}

static PyInt memInt(const Mem* m, int64_t off, int64_t n, int sign) {
    const uint8_t* p;
    int64_t len = memSlice(m, off, off + n, &p);
    return bytesInt(p, len, sign);
}

static uint64_t memU(const Mem* m, int64_t off, int64_t n) { return memInt(m, off, n, 0).v; }

/* (v & ((1 << 8n) - 1)).to_bytes(n, "little") */
static void intOut(PyInt v, uint8_t* out, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        out[i] = i < 8 ? (uint8_t)(v.v >> (8 * i)) : (v.neg ? 0xFF : 0);
    }
}

static int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

static int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

static int64_t align16(int64_t v) { return (int64_t)(((uint64_t)v + 15) & ~(uint64_t)15); }

/* a - b - c as Python computes it, for u64 operands, clamped to what an int64 holds */
static int64_t diffU(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t s = b + c;
    if (a >= s) {
        uint64_t d = a - s;
        return d > (uint64_t)INT64_MAX / 4 ? INT64_MAX / 4 : (int64_t)d;
    }
    uint64_t d = s - a;
    return d > (uint64_t)INT64_MAX / 4 ? -(INT64_MAX / 4) : -(int64_t)d;
}

/* at < data + size, at an unsigned address (Python compares the integers) */
static int below(uint64_t at, uint64_t data, int64_t size) {
    if (size >= 0) return at < data + (uint64_t)size || data + (uint64_t)size < data;
    return data >= (uint64_t)-size && at < data - (uint64_t)-size;
}

/* bisect.bisect_right, as Python's own loop (the same answer on an unsorted list too) */
static int32_t bisectU(const uint64_t* a, int32_t n, uint64_t x) {
    int32_t lo = 0, hi = n;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (x < a[mid]) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

static int32_t bisectI(const int64_t* a, int32_t n, int64_t x) {
    int32_t lo = 0, hi = n;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (x < a[mid]) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

/* ---- the context ------------------------------------------------------------------------------- */

typedef struct PoolChunk {
    struct PoolChunk* next;
    size_t used, size;
    uint8_t data[];
} PoolChunk;

typedef struct {
    uint8_t* p;
    int64_t n, cap;
} Buf;

typedef struct {
    StateConvertWarning* rows;
    int32_t n, cap;
} Warns;

/* A type's rules (statecanon rule_for, counted_rule), looked up once. */
typedef struct {
    uint8_t done;
    const StUnion* ur;  /* PC_STATE_UNION */
    int32_t sel, mem;   /* the first fields named the selector and the member (-1 none) */
    const StCounted* cr; /* counted_rule: NULL when it is None */
    int32_t ca, cc;     /* the array's and the count's fields (cc -1: PC_STATE_TAIL) */
} Rules;

/* One build's schema with the caches the converter keeps per type. */
typedef struct {
    const StateSchema* s;
    StateStrings str;
    int32_t ntypes;
    const char*** keys; /* a struct's field keys (Encoder.field_key) */
    uint8_t* keysDup;   /* a key repeats: the dict keeps the last */
    int32_t** views;    /* union_views, as field indexes */
    int32_t* nviews;    /* -1: not yet */
    int64_t** gaps;     /* Schema.gaps: (lo, hi) pairs */
    int32_t* ngaps;     /* -1: not yet */
    int64_t* gapsTotal;
    Rules* rules;
    int32_t otag;       /* the type "OTag" */
} Side;

enum { PS_BYTES, PS_PTRS, PS_OTAGS, PS_PRIMS, PS_TYPE };

/* One part of a heap or sound-arena block in the writer (layout_part, block_layout) and what
 * encode_part records of it. */
typedef struct {
    int64_t off, size;
    int spec;
    int32_t tid;      /* PS_TYPE: the writer's type */
    int one;
    int64_t* eo;      /* the elements' offsets */
    int64_t* cnt;     /* their counted arrays' lengths, -1: None */
    int32_t neo;
    int partial;      /* a T's first fields (allocateHeap(offsetof(T, m))) */
    const char* name; /* stable_name, NULL: None */
    int counted;
    const char* upto; /* partial: the key of the field it ends at, NULL: None */
    Mem tmp;          /* partial: encode_element's T, its tail zero */
    const Mem* mem;   /* where encode_part reads the part: mem from moff */
    int64_t moff;
} APart;

typedef struct {
    int end, user;
    uint64_t data, next;
    int64_t size;
    uint32_t bits, pctype;
    int pinned, type;
    APart* parts;
    int32_t nparts;
    int64_t* partLo;
    int64_t hdr, gap; /* the sound arena's */
    uint32_t flags, m2;
    PyInt m4;
} ABlock;

typedef struct {
    int present;
    uint64_t lo, hi; /* heapArena */
    Mem mem;
    ABlock* b;
    int32_t n;
    uint64_t* starts;
    int64_t first;
} AHeap;

typedef struct {
    int present;
    int32_t var;
    Mem mem; /* its section's chunk */
    int64_t off;
    uint64_t base;
    int64_t size;
    ABlock* b;
    int32_t n;
    uint64_t* lo;
} AArena;

/* A block where the reader puts it (decode_heap_layout, decode_arena_layout). */
typedef struct {
    int64_t off, size;
    int32_t tid;
    int64_t* eo;
    int32_t neo;
} LPart;

typedef struct {
    uint64_t data;
    int64_t size, hdr;
    int end;
    LPart* parts;
    int32_t nparts;
    const ABlock* rec;
} LBlock;

typedef struct {
    int present;
    LBlock* b;
    int32_t n;
    uint64_t base;
} BLayout;

enum { R_VAR, R_ARENA, R_PART, R_PARTARR };

/* A record of encode's document: a variable, the sound arena, a host part. */
typedef struct {
    int kind;
    int32_t var, chunk;
    int64_t off;
    int32_t tid;      /* R_PART: the part's type; R_PARTARR: the element's */
    int64_t n, es;    /* R_PARTARR */
} ARec;

/* A raw entry of encode's document: the chunk's bytes, or a section's bytes outside its variables. */
typedef struct {
    int32_t chunk;
    int gaps;
    const char* module;
    uint64_t lo, hi;
} ARaw;

typedef struct {
    const uint8_t* tag;
    int tagLen;
    int64_t off, size;
} Chunk;

/* ---- the state rules between the two builds (statecanon Rules; docs/CANONICAL_STATES.md 17.2) ----------- */

typedef struct {
    int32_t tid;      /* a type of the side the rule's schema is (the newer, or the reader for properties) */
    const char* key;  /* its member's key */
    const char* old;  /* was: the key before */
    const StRule* r;  /* values, reset */
    int64_t value;    /* removed, default */
} FieldEntry;

typedef struct {
    const char* cur;  /* the identity or name now (removed, defaults: the identity) */
    const char* old;
    const StRule* r;
    int64_t value;
} NameEntry;

typedef struct {
    FieldEntry* e;
    int32_t n, cap;
} FieldList;

typedef struct {
    NameEntry* e;
    int32_t n, cap;
} NameList;

typedef struct {
    int forward, backward, active;
    int64_t older, unknown;
    FieldList fieldWas, fieldHist, removedFields, defaultsField, hashes;
    NameList varWas, typeWas, tagWas, fnWas, varHist, removedVars, defaultsVar;
    uint8_t* ruledA; /* per writer type: an entry names it (a newer writer's history) */
    uint8_t* ruledB; /* per reader type: an entry names it (a newer reader's history, the reader's properties) */
    int32_t histCount;
} CRules;

typedef struct {
    Side a, b;
    CRules R;
    Warns notes; /* what the rules and the type changes did (statecanon Decoder.applied) */
    int self;    /* > 0: a writer's value laid out by its own schema (writer_bytes): no rules, types or warnings */
    int8_t* plainA; /* plain() per type of each side: -1 not yet, 2 deciding */
    int8_t* plainB;
    uint8_t* defaultsState; /* defaults_in per reader type: 0 not yet, 1 deciding, 2 done */
    struct DefaultList* defaults;
    Chunk* ac;
    int32_t nac;
    Mem* acMem;
    AHeap heap;
    AArena arena;
    ARec* recs;
    int32_t nrec, recCap;
    int32_t* byVar; /* the writer's variable -> its record (by_id), -1 */
    ARaw* raws;
    int32_t nraw;
    BLayout bheap, barena;
    Warns enc, dec;
    PoolChunk* pool;
    char* err;
    size_t errSize;
    int failed;
} Ctx;

static void fail(Ctx* c, const char* fmt, ...) {
    if (c->failed) return;
    c->failed = 1;
    if (c->err && c->errSize) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c->err, c->errSize, fmt, ap);
        va_end(ap);
    }
}

static void* poolAlloc(Ctx* c, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (n == 0) n = 16;
    PoolChunk* k = c->pool;
    if (!k || k->used + n > k->size) {
        size_t size = n > (1u << 20) ? n : (1u << 20);
        PoolChunk* fresh = malloc(sizeof *fresh + size);
        if (!fresh) {
            fail(c, "out of memory");
            return NULL;
        }
        fresh->next = k;
        fresh->used = 0;
        fresh->size = size;
        c->pool = fresh;
        k = fresh;
    }
    void* p = k->data + k->used;
    k->used += n;
    memset(p, 0, n);
    return p;
}

static const char* poolString(Ctx* c, const char* s, size_t n) {
    char* p = poolAlloc(c, n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = 0;
    }
    return p;
}

static int bufGrow(Ctx* c, Buf* b, int64_t need) {
    if (need <= b->cap) return 1;
    int64_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    uint8_t* p = realloc(b->p, (size_t)cap);
    if (!p) {
        fail(c, "out of memory");
        return 0;
    }
    b->p = p;
    b->cap = cap;
    return 1;
}

/* A zeroed buffer of n bytes: bytearray(n). */
static int bufZero(Ctx* c, Buf* b, int64_t n) {
    b->n = 0;
    if (n < 0) n = 0;
    if (!bufGrow(c, b, n)) return 0;
    memset(b->p, 0, (size_t)n);
    b->n = n;
    return 1;
}

/* b[a:e] = data[:m] (zeros for NULL), as Python assigns a bytearray's slice: the slice is replaced, so
 * the buffer grows or shrinks when m differs from its length. data never points into b. */
static void bufSet(Ctx* c, Buf* b, int64_t a, int64_t e, const uint8_t* data, int64_t m) {
    if (c->failed) return;
    pySlice(b->n, &a, &e);
    if (m < 0) m = 0;
    if (m != e - a) {
        int64_t tail = b->n - e;
        if (!bufGrow(c, b, a + m + tail)) return;
        memmove(b->p + a + m, b->p + e, (size_t)tail);
        b->n = a + m + tail;
    }
    if (data) memcpy(b->p + a, data, (size_t)m);
    else memset(b->p + a, 0, (size_t)m);
}

static void warn(Ctx* c, Warns* w, const char* text) {
    if (c->self && w != &c->enc) return; /* a writer's value laid out by its own schema: its decoder's, unkept */
    for (int32_t i = 0; i < w->n; i++) {
        if (strcmp(w->rows[i].text, text) == 0) {
            w->rows[i].count++;
            return;
        }
    }
    if (w->n == w->cap) {
        int32_t cap = w->cap ? w->cap * 2 : 16;
        StateConvertWarning* rows = realloc(w->rows, (size_t)cap * sizeof *rows);
        if (!rows) {
            fail(c, "out of memory");
            return;
        }
        w->rows = rows;
        w->cap = cap;
    }
    size_t n = strlen(text);
    char* t = malloc(n + 1);
    if (!t) {
        fail(c, "out of memory");
        return;
    }
    memcpy(t, text, n + 1);
    w->rows[w->n].text = t;
    w->rows[w->n].count = 1;
    w->n++;
}

static int tagIs(const uint8_t* tag, int n, const char* s) {
    return (size_t)n == strlen(s) && memcmp(tag, s, (size_t)n) == 0;
}

static int tagEq(const uint8_t* a, int an, const uint8_t* b, int bn) {
    return an == bn && memcmp(a, b, (size_t)an) == 0;
}

/* ---- a schema's types -------------------------------------------------------------------------- */

static const StType* ty(const Side* s, int32_t tid) {
    return tid >= 0 && tid < s->ntypes ? stateSchemaTypeAt(s->s, tid) : NULL;
}

static int64_t tsize(const Side* s, int32_t tid) {
    const StType* t = ty(s, tid);
    return t ? t->size : 0;
}

static int sideInit(Ctx* c, Side* s, const StateSchema* schema, StateStrings str) {
    s->s = schema;
    s->str = str;
    s->ntypes = stateSchemaTypeCount(schema);
    size_t n = (size_t)s->ntypes + 1;
    s->keys = calloc(n, sizeof *s->keys);
    s->keysDup = calloc(n, 1);
    s->views = calloc(n, sizeof *s->views);
    s->nviews = malloc(n * sizeof *s->nviews);
    s->gaps = calloc(n, sizeof *s->gaps);
    s->ngaps = malloc(n * sizeof *s->ngaps);
    s->gapsTotal = calloc(n, sizeof *s->gapsTotal);
    s->rules = calloc(n, sizeof *s->rules);
    if (!s->keys || !s->keysDup || !s->views || !s->nviews || !s->gaps || !s->ngaps || !s->gapsTotal || !s->rules) {
        fail(c, "out of memory");
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        s->nviews[i] = -1;
        s->ngaps[i] = -1;
    }
    s->otag = stateSchemaType(schema, "OTag");
    return 1;
}

static void sideFree(Side* s) {
    free(s->keys);
    free(s->keysDup);
    free(s->views);
    free(s->nviews);
    free(s->gaps);
    free(s->ngaps);
    free(s->gapsTotal);
    free(s->rules);
}

/* Encoder.field_key over a struct's fields in order: "+<retail offset>" or "pc:<name>", and ":<name>"
 * after a key already taken. */
static const char* const* keysOf(Ctx* c, Side* s, int32_t tid) {
    if (s->keys[tid]) return s->keys[tid];
    const StType* t = ty(s, tid);
    int32_t n = t->fieldCount;
    const char** k = poolAlloc(c, (size_t)(n + 1) * sizeof *k);
    if (!k) return NULL;
    char buf[KEY_MAX];
    int dup = 0;
    for (int32_t i = 0; i < n; i++) {
        const StField* f = &t->fields[i];
        if (f->retail != ST_NO_RETAIL) snprintf(buf, sizeof buf, "+%llx", (unsigned long long)f->retail);
        else snprintf(buf, sizeof buf, "pc:%s", f->name);
        for (int32_t j = 0; j < i; j++) {
            if (strcmp(k[j], buf) == 0) {
                size_t len = strlen(buf);
                snprintf(buf + len, sizeof buf - len, ":%s", f->name);
                break;
            }
        }
        for (int32_t j = 0; j < i && !dup; j++) {
            if (strcmp(k[j], buf) == 0) dup = 1;
        }
        k[i] = poolString(c, buf, strlen(buf));
        if (!k[i]) return NULL;
    }
    s->keysDup[tid] = (uint8_t)dup;
    s->keys[tid] = k;
    return k;
}

/* The field a dict lookup of key finds: the last with that key (the dict keeps the last value). */
static int32_t lastKey(Ctx* c, Side* s, int32_t tid, const char* key, int32_t hint) {
    const char* const* k = keysOf(c, s, tid);
    if (!k) return -1;
    int32_t n = ty(s, tid)->fieldCount;
    if (!s->keysDup[tid] && hint >= 0 && hint < n && strcmp(k[hint], key) == 0) return hint;
    for (int32_t i = n - 1; i >= 0; i--) {
        if (strcmp(k[i], key) == 0) return i;
    }
    return -1;
}

/* The first field with key (Decoder.offset_of's search). */
static int32_t firstKey(Ctx* c, Side* s, int32_t tid, const char* key) {
    const char* const* k = keysOf(c, s, tid);
    if (!k) return -1;
    int32_t n = ty(s, tid)->fieldCount;
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(k[i], key) == 0) return i;
    }
    return -1;
}

static int32_t fieldNamed(const StType* t, const char* name) {
    if (!t || !name) return -1;
    for (int32_t i = 0; i < t->fieldCount; i++) {
        if (strcmp(t->fields[i].name, name) == 0) return i;
    }
    return -1;
}

/* array_key: the key of the first field with the field's name. */
static const char* arrayKey(Ctx* c, Side* s, int32_t tid, int32_t field) {
    const StType* t = ty(s, tid);
    const char* const* k = keysOf(c, s, tid);
    if (!k) return NULL;
    int32_t i = fieldNamed(t, t->fields[field].name);
    return k[i >= 0 ? i : field];
}

/* rule_for (by tid, else by the type's name, else its tid_name) and counted_rule. */
static const Rules* rulesOf(Side* s, int32_t tid) {
    Rules* r = &s->rules[tid];
    if (r->done) return r;
    r->done = 1;
    const StType* t = ty(s, tid);
    const char* tn = stateSchemaTidName(s->s, tid);
    const StUnion* u = stateSchemaUnion(s->s, tid);
    if (!u && t->name[0]) u = stateSchemaUnionNamed(s->s, t->name);
    if (!u && tn) u = stateSchemaUnionNamed(s->s, tn);
    r->ur = u;
    r->sel = u ? fieldNamed(t, u->selector) : -1;
    r->mem = u ? fieldNamed(t, u->member) : -1;
    const StCounted* k = stateSchemaCounted(s->s, tid);
    if (!k && t->name[0]) k = stateSchemaCountedNamed(s->s, t->name);
    if (!k && tn) k = stateSchemaCountedNamed(s->s, tn);
    r->cr = NULL;
    r->ca = r->cc = -1;
    if (k) {
        int32_t a = fieldNamed(t, k->array);
        int32_t n = k->count ? fieldNamed(t, k->count) : -1;
        if (a >= 0 && (n >= 0 || !k->count)) {
            r->cr = k;
            r->ca = a;
            r->cc = n;
        }
    }
    return r;
}

/* raw_bytes and typed_bytes: how much of a type is plain scalar arrays, and how much typed beyond them. */
static int64_t rawBytes(const Side* s, int32_t tid, int depth) {
    const StType* t = ty(s, tid);
    if (!t || depth > 16) return 0;
    if (t->kind == ST_ARR) {
        const StType* et = ty(s, t->of);
        if (!et || et->kind == ST_INT) return t->size;
        return t->n * rawBytes(s, t->of, depth + 1);
    }
    if (t->kind == ST_STRUCT) {
        int64_t sum = 0;
        for (int32_t i = 0; i < t->fieldCount; i++) sum += rawBytes(s, t->fields[i].type, depth + 1);
        return sum;
    }
    return 0;
}

static int64_t typedBytes(const Side* s, int32_t tid, int depth) {
    const StType* t = ty(s, tid);
    if (!t || depth > 16) return 0;
    if (t->kind == ST_PTR) return 8;
    if (t->kind == ST_ARR) {
        const StType* et = ty(s, t->of);
        if (!et || et->kind == ST_INT) return 0;
        return t->n * typedBytes(s, t->of, depth + 1);
    }
    if (t->kind == ST_STRUCT) {
        int64_t sum = 0;
        for (int32_t i = 0; i < t->fieldCount; i++) {
            const StType* ft = ty(s, t->fields[i].type);
            if (ft && (ft->kind == ST_ARR || ft->kind == ST_STRUCT || ft->kind == ST_PTR || ft->kind == ST_UNION)) {
                sum += typedBytes(s, t->fields[i].type, depth + 1);
            } else {
                sum += tsize(s, t->fields[i].type);
            }
        }
        return sum;
    }
    return 0;
}

/* union_views: a union's members with a known type, least typed first (a stable sort by
 * (-raw_bytes, typed_bytes)). */
static const int32_t* viewsOf(Ctx* c, Side* s, int32_t tid, int32_t* n) {
    if (s->nviews[tid] >= 0) {
        *n = s->nviews[tid];
        return s->views[tid];
    }
    const StType* t = ty(s, tid);
    int32_t* v = poolAlloc(c, (size_t)(t->fieldCount + 1) * sizeof *v);
    int64_t* key = malloc((size_t)(t->fieldCount + 1) * 2 * sizeof *key);
    if (!v || !key) {
        free(key);
        fail(c, "out of memory");
        *n = 0;
        return NULL;
    }
    int32_t m = 0;
    for (int32_t i = 0; i < t->fieldCount; i++) {
        if (t->fields[i].type == ST_NONE) continue;
        int64_t k0 = -rawBytes(s, t->fields[i].type, 0), k1 = typedBytes(s, t->fields[i].type, 0);
        int32_t j = m;
        while (j > 0 && (key[2 * (j - 1)] > k0 || (key[2 * (j - 1)] == k0 && key[2 * (j - 1) + 1] > k1))) {
            v[j] = v[j - 1];
            key[2 * j] = key[2 * (j - 1)];
            key[2 * j + 1] = key[2 * (j - 1) + 1];
            j--;
        }
        v[j] = i;
        key[2 * j] = k0;
        key[2 * j + 1] = k1;
        m++;
    }
    free(key);
    s->views[tid] = v;
    s->nviews[tid] = m;
    *n = m;
    return v;
}

/* Schema.gaps: a struct's bytes outside every field, as (lo, hi) pairs. */
static const int64_t* gapsOf(Ctx* c, Side* s, int32_t tid, int32_t* n, int64_t* total) {
    if (s->ngaps[tid] < 0) {
        const StType* t = ty(s, tid);
        int64_t size = t->size > 0 ? t->size : 0;
        uint8_t* cover = calloc((size_t)size + 1, 1);
        if (!cover) {
            fail(c, "out of memory");
            *n = 0;
            *total = 0;
            return NULL;
        }
        for (int32_t i = 0; i < t->fieldCount; i++) {
            const StField* f = &t->fields[i];
            int64_t lo, hi;
            if (f->bits) {
                lo = floorDiv(f->bit, 8);
                hi = floorDiv((int64_t)f->bit + f->bits + 7, 8);
            } else {
                lo = f->off;
                hi = f->off + tsize(s, f->type);
            }
            if (lo < 0) lo = 0;
            if (hi > size) hi = size;
            for (int64_t k = lo; k < hi; k++) cover[k] = 1;
        }
        int32_t m = 0;
        for (int64_t i = 0; i < size;) {
            if (cover[i]) {
                i++;
                continue;
            }
            while (i < size && !cover[i]) i++;
            m++;
        }
        int64_t* g = poolAlloc(c, (size_t)(2 * m + 1) * sizeof *g);
        int64_t sum = 0;
        m = 0;
        for (int64_t i = 0; g && i < size;) {
            if (cover[i]) {
                i++;
                continue;
            }
            int64_t j = i;
            while (j < size && !cover[j]) j++;
            g[2 * m] = i;
            g[2 * m + 1] = j;
            sum += j - i;
            m++;
            i = j;
        }
        free(cover);
        if (!g) return NULL;
        s->gaps[tid] = g;
        s->ngaps[tid] = m;
        s->gapsTotal[tid] = sum;
    }
    *n = s->ngaps[tid];
    *total = s->gapsTotal[tid];
    return s->gaps[tid];
}

/* stable_name: a type's name another build resolves (type_names), or NULL. */
static const char* stableName(const Side* s, int32_t tid) {
    const char* n = stateSchemaTidName(s->s, tid);
    if (n && n[0]) return n;
    const StType* t = ty(s, tid);
    if (!t || !t->name[0] || strstr(t->name, "<anon>")) return NULL;
    int32_t named = stateSchemaTypeNamed(s->s, t->name);
    return named != ST_NONE && tsize(s, named) == tsize(s, tid) ? t->name : NULL;
}

/* ---- pointers ---------------------------------------------------------------------------------- */

enum { S_INT, S_KEY, S_VIEW };

typedef struct {
    uint8_t kind;
    int64_t i;
    const char* s; /* S_KEY: the key; S_VIEW: the union member's name */
} Step;

enum { P_NONE, P_RAW, P_BLOCK, P_REGION, P_RO, P_STR, P_FN, P_VAR };
enum { B_OFF, B_PATH, B_WORD, B_BYTE }; /* {"o"}, {"pt","e","p","b"}, {"pt","e","b"}, {"pt","o"} */

/* A canonical pointer (Encoder.pointer's dict). */
typedef struct {
    uint8_t kind;
    uint8_t arena, form;
    int32_t block, part;
    int64_t e, o, b;
    int region;
    uint64_t raw;
    const char* name; /* P_RO: the constant; P_STR: the symbol below (NULL: no "at"); P_VAR: the id */
    const uint8_t* text;
    size_t textLen;
    char fn[FN_ID];
    int32_t nsteps;
    Step steps[MAX_STEPS];
} CPtr;

static const char* const REGIONS[3] = {"heapArena", "gameStack", "pcScratchpad"};

/* Encoder.path_of: the field path from a value of type tid to the scalar at byte off, and the rest. */
static int64_t pathOf(Ctx* c, int32_t tid, int64_t off, CPtr* p) {
    Side* s = &c->a;
    for (int it = 0; it < 64 && p->nsteps < MAX_STEPS; it++) {
        const StType* t = ty(s, tid);
        if (!t) break;
        if (t->kind == ST_ARR && ty(s, t->of) && t->n) {
            int64_t es = tsize(s, t->of);
            if (es <= 0) break;
            int64_t i = floorDiv(off, es);
            if (i >= t->n) break;
            p->steps[p->nsteps].kind = S_INT;
            p->steps[p->nsteps++].i = i;
            off -= i * es;
            tid = t->of;
            continue;
        }
        if (t->kind == ST_UNION) { /* through the most typed view that covers the offset */
            int32_t nv;
            const int32_t* v = viewsOf(c, s, tid, &nv);
            int32_t hit = -1;
            for (int32_t k = 0; k < nv; k++) {
                if (tsize(s, t->fields[v[k]].type) > off) hit = v[k];
            }
            if (hit < 0) break;
            p->steps[p->nsteps].kind = S_VIEW;
            p->steps[p->nsteps++].s = t->fields[hit].name;
            tid = t->fields[hit].type;
            continue;
        }
        if (t->kind == ST_STRUCT) {
            const char* const* keys = keysOf(c, s, tid);
            int32_t hit = -1;
            for (int32_t i = 0; keys && i < t->fieldCount; i++) {
                const StField* f = &t->fields[i];
                int64_t fs = tsize(s, f->type);
                if (!f->bits && f->off <= off && off < f->off + (fs > 1 ? fs : 1)) {
                    hit = i;
                    break;
                }
            }
            if (hit < 0) break;
            p->steps[p->nsteps].kind = S_KEY;
            p->steps[p->nsteps++].s = keys[hit];
            off -= t->fields[hit].off;
            tid = t->fields[hit].type;
            continue;
        }
        break;
    }
    return off;
}

/* Encoder.block_pointer: into a heap block ("hb") or a sound-arena block ("sb"). 0: None. */
static int blockPointer(Ctx* c, int arena, uint64_t v, CPtr* p) {
    const ABlock* blocks = arena ? c->arena.b : c->heap.b;
    int32_t n = arena ? c->arena.n : c->heap.n;
    int64_t head = arena ? ARENA_HEADER : HEAP_HEADER;
    int32_t i = bisectU(arena ? c->arena.lo : c->heap.starts, n, v + (uint64_t)head) - 1;
    if (i < 0) return 0;
    const ABlock* b = &blocks[i];
    int64_t off = (int64_t)(v - b->data);
    if (off > b->size || off < -head) return 0;
    p->kind = P_BLOCK;
    p->arena = (uint8_t)arena;
    p->block = i;
    if (!b->nparts || !(0 <= off && off < b->size)) {
        p->form = B_OFF;
        p->o = off;
        return 1;
    }
    int32_t k = bisectI(b->partLo, b->nparts, off) - 1;
    if (k < 0) {
        fail(c, "a pointer before a block's first part");
        return 1;
    }
    const APart* pt = &b->parts[k];
    int64_t rel = off - pt->off;
    p->part = k;
    if (pt->spec == PS_TYPE) {
        int32_t e = bisectI(pt->eo, pt->neo, rel) - 1;
        if (e < 0) {
            fail(c, "a pointer before a part's first element");
            return 1;
        }
        rel -= pt->eo[e];
        const Rules* r = pt->cnt[e] >= 0 ? rulesOf(&c->a, pt->tid) : NULL;
        const StType* t = ty(&c->a, pt->tid);
        int64_t rest;
        if (r && r->cr && rel >= t->fields[r->ca].off) { /* in the counted array: past its declared length */
            const StField* a = &t->fields[r->ca];
            const StType* at = ty(&c->a, a->type);
            int64_t es = at && at->kind == ST_ARR ? tsize(&c->a, at->of) : 0;
            if (es == 0) {
                fail(c, "a counted array of elements without a size");
                return 1;
            }
            p->steps[0].kind = S_KEY;
            p->steps[0].s = arrayKey(c, &c->a, pt->tid, r->ca);
            p->steps[1].kind = S_INT;
            p->steps[1].i = floorDiv(rel - a->off, es);
            p->nsteps = 2;
            rest = pathOf(c, at->of, floorMod(rel - a->off, es), p);
        } else {
            rest = pathOf(c, pt->tid, rel, p);
        }
        p->form = B_PATH;
        p->e = e;
        p->b = rest;
        return 1;
    }
    if (pt->spec == PS_PTRS || pt->spec == PS_OTAGS) {
        p->form = B_WORD;
        p->e = floorDiv(rel, 8);
        p->b = floorMod(rel, 8);
        return 1;
    }
    p->form = B_BYTE;
    p->o = rel;
    return 1;
}

/* Encoder.pointer: an address in the writer to what it points into. */
static void encPointer(Ctx* c, uint64_t v, int quiet, CPtr* p) {
    p->kind = P_NONE;
    p->nsteps = 0;
    p->b = p->o = p->e = 0;
    p->name = NULL;
    if (v == 0) return;
    const StateSchema* s = c->a.s;
    if (c->heap.present && c->heap.lo <= v && v <= c->heap.hi) {
        if (blockPointer(c, 0, v, p)) return;
        if (!quiet) warn(c, &c->enc, "pointer into the heap outside every block (kept as an offset)");
    }
    for (int k = 0; k < 3; k++) {
        StRange r = stateSchemaRegion(s, REGIONS[k]);
        if (r.name && r.lo <= v && v <= r.hi) {
            p->kind = P_REGION;
            p->region = k;
            p->o = (int64_t)(v - r.lo);
            return;
        }
    }
    if (c->arena.present && c->arena.base <= v && v < c->arena.base + (uint64_t)c->arena.size) {
        if (blockPointer(c, 1, v, p)) return;
    }
    if (stateSchemaFnIdentity(s, v, p->fn, sizeof p->fn)) {
        p->kind = P_FN;
        return;
    }
    int32_t vi = stateSchemaVarHolding(s, v);
    if (vi != ST_NONE) {
        const StVar* var = stateSchemaVarAt(s, vi);
        p->kind = P_VAR;
        p->name = var->id;
        p->b = pathOf(c, var->type, (int64_t)(v - var->addr), p);
        return;
    }
    if (stateSchemaReadOnly(s, v, NULL)) { /* constant data: a named constant, else a string by its content */
        const StRange* named = stateSchemaNamedConstant(s, v);
        if (named) {
            p->kind = P_RO;
            p->name = named->name;
            p->o = (int64_t)(v - named->lo);
            return;
        }
        if (c->a.str.at && c->a.str.at(c->a.str.user, v, &p->text, &p->textLen)) {
            uint64_t at = 0;
            p->kind = P_STR;
            p->name = stateSchemaSymbolBelow(s, v, &at);
            p->o = p->name ? (int64_t)(v - at) : 0;
            return;
        }
    }
    if (!quiet) warn(c, &c->enc, "pointer to nothing known (kept as a number: same build only)");
    p->kind = P_RAW;
    p->raw = v;
}

static CRules* rulesOn(Ctx* c);
static const char* fieldTarget(const CRules* R, int32_t rtid, int32_t wtid, const char* wkey);
static const char* nameTarget(const CRules* R, const NameList* l, const char* ident);
static const char* fnTarget(const CRules* R, const char* ident, char* buf, size_t size);

/* Decoder.offset_of: a field path's offset from a value of type tid in the reader; 0 when the path is
 * not in this build. The path's keys are the writer's, of its type wtid: with rules, followed alongside
 * (a renamed member's key by them). */
static int offsetOf(Ctx* c, int32_t tid, const Step* steps, int32_t n, int64_t* out, int32_t wtid) {
    Side* s = &c->b;
    CRules* R = rulesOn(c);
    int64_t off = 0;
    for (int32_t k = 0; k < n; k++) {
        const StType* t = ty(s, tid);
        if (!t) return 0;
        const StType* wtt = R ? ty(&c->a, wtid) : NULL;
        if (steps[k].kind == S_VIEW) {
            const char* name = R ? fieldTarget(R, tid, wtid, steps[k].s) : steps[k].s;
            int32_t f = name ? fieldNamed(t, name) : -1;
            if (t->kind != ST_UNION || f < 0) return 0;
            int32_t wv = wtt ? fieldNamed(wtt, steps[k].s) : -1;
            tid = t->fields[f].type;
            wtid = wv >= 0 ? wtt->fields[wv].type : ST_NONE;
        } else if (steps[k].kind == S_INT) {
            if (t->kind != ST_ARR) return 0;
            off += steps[k].i * tsize(s, t->of);
            tid = t->of;
            wtid = wtt && wtt->kind == ST_ARR ? wtt->of : ST_NONE;
        } else {
            if (t->kind != ST_STRUCT) return 0;
            const char* key = R ? fieldTarget(R, tid, wtid, steps[k].s) : steps[k].s;
            int32_t f = key ? firstKey(c, s, tid, key) : -1;
            if (f < 0) return 0;
            int32_t wf = wtt && wtt->kind == ST_STRUCT ? firstKey(c, &c->a, wtid, steps[k].s) : -1;
            off += t->fields[f].off;
            tid = t->fields[f].type;
            wtid = wf >= 0 ? wtt->fields[wf].type : ST_NONE;
        }
    }
    *out = off;
    return 1;
}

/* Decoder.pointer: a canonical pointer to the reader's address. */
static uint64_t decPointer(Ctx* c, const CPtr* p) {
    const StateSchema* s = c->b.s;
    switch (p->kind) {
    case P_NONE:
        return 0;
    case P_RAW:
        return p->raw;
    case P_BLOCK: {
        const BLayout* l = p->arena ? &c->barena : &c->bheap;
        if (!l->present || p->block >= l->n) {
            warn(c, &c->dec, "pointer into a heap block not in the state");
            return 0;
        }
        const LBlock* b = &l->b[p->block];
        if (p->form == B_OFF) return b->data + (uint64_t)p->o;
        if (p->part >= b->nparts) {
            warn(c, &c->dec, "pointer into a heap part not in the state");
            return b->data;
        }
        const LPart* lay = &b->parts[p->part];
        uint64_t base = b->data + (uint64_t)lay->off;
        if (p->form == B_BYTE) return base + (uint64_t)p->o;
        const APart* part = &b->rec->parts[p->part];
        if (part->spec == PS_TYPE) {
            if (lay->tid == ST_NONE) {
                warn(c, &c->dec, "heap pointer path not in this build");
                return base;
            }
            int64_t o = 0;
            if (!offsetOf(c, lay->tid, p->steps, p->nsteps, &o, part->tid)) {
                warn(c, &c->dec, "heap pointer path not in this build");
                o = 0;
            }
            int64_t eo = p->e < lay->neo ? lay->eo[p->e] : p->e * tsize(&c->b, lay->tid);
            return base + (uint64_t)eo + (uint64_t)o + (uint64_t)p->b;
        }
        return base + (uint64_t)(p->e * 8) + (uint64_t)p->b;
    }
    case P_REGION: {
        StRange r = stateSchemaRegion(s, REGIONS[p->region]);
        if (!r.name) {
            fail(c, "the reader's schema has no region %s", REGIONS[p->region]);
            return 0;
        }
        return r.lo + (uint64_t)p->o;
    }
    case P_RO: {
        uint64_t a = 0;
        if (!stateSchemaSymbolFind(s, p->name, &a)) {
            warn(c, &c->dec, "constant not in this build");
            return 0;
        }
        return a + (uint64_t)p->o;
    }
    case P_STR: {
        uint64_t a = 0;
        if (p->name && stateSchemaSymbolFind(s, p->name, &a)) {
            uint64_t hinted = a + (uint64_t)p->o;
            const uint8_t* text;
            size_t len;
            if (c->b.str.at && c->b.str.at(c->b.str.user, hinted, &text, &len) && len == p->textLen &&
                memcmp(text, p->text, len) == 0) {
                return hinted;
            }
        }
        if (!c->b.str.find || !c->b.str.find(c->b.str.user, p->text, p->textLen, &a)) {
            warn(c, &c->dec, "string not in this build");
            return 0;
        }
        return a;
    }
    case P_FN: {
        uint64_t a = 0;
        CRules* R = rulesOn(c);
        char renamed[FN_ID];
        if (!stateSchemaFnFind(s, R ? fnTarget(R, p->fn, renamed, sizeof renamed) : p->fn, &a)) {
            warn(c, &c->dec, "function not in this build");
            return 0;
        }
        return a;
    }
    default: {
        CRules* R = rulesOn(c);
        const char* vid = R ? nameTarget(R, &R->varWas, p->name) : p->name;
        int32_t vi = vid ? stateSchemaVarById(s, vid) : ST_NONE;
        if (vi == ST_NONE) {
            warn(c, &c->dec, "pointer to a variable not in this build");
            return 0;
        }
        const StVar* var = stateSchemaVarAt(s, vi);
        int32_t wv = R ? stateSchemaVarById(c->a.s, p->name) : ST_NONE;
        int64_t o = 0;
        if (!offsetOf(c, var->type, p->steps, p->nsteps, &o, wv != ST_NONE ? stateSchemaVarAt(c->a.s, wv)->type : ST_NONE)) {
            warn(c, &c->dec, "pointer path not in this build");
            o = 0;
        }
        return var->addr + (uint64_t)o + (uint64_t)p->b;
    }
    }
}

/* The reader's address of the writer's pointer v (decode(encode(v))). */
static uint64_t movePointer(Ctx* c, uint64_t v) {
    CPtr p;
    encPointer(c, v, 1, &p);
    return decPointer(c, &p);
}

/* ---- the writer's values (Encoder.value) ------------------------------------------------------- */

enum { AV_NONE, AV_INT, AV_FLOAT, AV_ADDR, AV_Z, AV_OTAG, AV_PTR, AV_HEX, AV_LIST, AV_STRUCT, AV_UNION, AV_SEL };

typedef struct AVal AVal;

typedef struct {
    const char* key;
    const AVal* val;
} Override;

/* A canonical value, as a view of the writer's bytes:
 *   AV_NONE None; AV_INT an int (num); AV_FLOAT {"f": n bytes at off}; AV_ADDR {"a": pointer(word)};
 *   AV_Z {"z": n}; AV_OTAG {"n": num, "l": "end" or pointer(word)}; AV_PTR a pointer (word; fn: by
 *   its function's identity); AV_HEX {"x": n bytes at off, "w": w (0: no "w")}; AV_LIST n values of
 *   type tid at off + i * es; AV_STRUCT a struct's dict (type tid at off) with up to two keys
 *   replaced (encode_element's counted array, put_element's first); AV_UNION {"u": each view};
 *   AV_SEL {"x": n bytes at off, "sel": sel, "v": value(tid, selOff)} (a union member's live view). */
struct AVal {
    uint8_t kind, fn, end;
    int32_t tid;
    const Mem* m;
    int64_t off, n, es, w;
    PyInt num;
    uint64_t word;
    const char* sel;
    int64_t selOff;
    int32_t nov;
    Override ov[2];
};

static void makeVal(Ctx* c, int32_t tid, const Mem* m, int64_t off, int warnOn, AVal* v);

/* selected_view: the union member's view its selector names (PC_STATE_UNION): its field index in the
 * member's type, -1 for None. */
static int32_t selectedView(Ctx* c, int32_t tid, const Mem* m, int64_t off) {
    const StType* t = ty(&c->a, tid);
    const Rules* r = rulesOf(&c->a, tid);
    if (r->sel < 0 || r->mem < 0 || t->fields[r->sel].bits) return -1;
    const StField* sf = &t->fields[r->sel];
    int64_t n = tsize(&c->a, sf->type);
    uint64_t sel = memU(m, off + sf->off, n > 8 ? 8 : n);
    if (sel >= (uint64_t)r->ur->viewCount) return -1;
    return fieldNamed(ty(&c->a, t->fields[r->mem].type), r->ur->views[sel]);
}

/* Bits [bit, bit + nb) of mem from off, nb <= 64. */
static uint64_t bitsAt(const Mem* m, int64_t off, int32_t bit, int32_t nb) {
    int64_t base = floorDiv(bit, 8), span = floorDiv((int64_t)(bit % 8) + nb + 7, 8);
    const uint8_t* p;
    int64_t len = memSlice(m, off + base, off + base + span, &p);
    int sh = bit % 8;
    if (span <= 8) {
        uint64_t raw = bytesInt(p, len, 0).v;
        return (raw >> sh) & MASK_BITS(nb);
    }
    uint64_t v = 0;
    for (int32_t i = 0; i < nb; i++) {
        int64_t at = sh + i;
        if (at / 8 < len && ((p[at / 8] >> (at % 8)) & 1)) v |= UINT64_C(1) << i;
    }
    return v;
}

/* A struct dict's value for field i (Encoder.value's struct case). */
static void fieldVal(Ctx* c, const AVal* s, int32_t i, int warnOn, AVal* out) {
    const StType* t = ty(&c->a, s->tid);
    const StField* f = &t->fields[i];
    if (f->bits) {
        memset(out, 0, sizeof *out);
        if (f->bits > 64) {
            fail(c, "a bitfield wider than 64 bits");
            return;
        }
        out->kind = AV_INT;
        out->num = pyUint(bitsAt(s->m, s->off, f->bit, f->bits));
        return;
    }
    const Rules* r = rulesOf(&c->a, s->tid);
    if (r->ur && strcmp(f->name, r->ur->member) == 0) {
        int32_t view = selectedView(c, s->tid, s->m, s->off);
        if (view >= 0) { /* the live view on the bytes (the others' bytes are leftovers) */
            const StField* vf = &ty(&c->a, t->fields[r->mem].type)->fields[view];
            memset(out, 0, sizeof *out);
            out->kind = AV_SEL;
            out->m = s->m;
            out->off = s->off + f->off;
            out->n = tsize(&c->a, f->type);
            out->sel = vf->name;
            out->tid = vf->type;
            out->selOff = s->off + f->off + vf->off;
            return;
        }
    }
    makeVal(c, f->type, s->m, s->off + f->off, warnOn, out);
}

/* Encoder.value: what the writer's bytes of type tid at off encode to. warnOn: count encode's
 * warnings (the walk); off when the decoder reads the value again. */
static void makeVal(Ctx* c, int32_t tid, const Mem* m, int64_t off, int warnOn, AVal* v) {
    memset(v, 0, sizeof *v);
    v->m = m;
    v->off = off;
    v->tid = tid;
    const StType* t = ty(&c->a, tid);
    if (!t) return;
    switch (t->kind) {
    case ST_INT:
        if (t->size > 8) {
            fail(c, "an int of %lld bytes", (long long)t->size);
            return;
        }
        v->kind = AV_INT;
        v->num = memInt(m, off, t->size, t->isSigned);
        return;
    case ST_FLOAT:
        v->kind = AV_FLOAT;
        v->n = t->size;
        return;
    case ST_ADDR: { /* an integer that holds an address: moved as a pointer when it is one */
        uint64_t w = memU(m, off, t->size > 8 ? 8 : t->size);
        v->kind = AV_INT;
        v->num = pyUint(w);
        if (w) {
            CPtr p;
            encPointer(c, w, 1, &p);
            if (p.kind != P_RAW) {
                v->kind = AV_ADDR;
                v->word = w;
            }
        }
        return;
    }
    case ST_SCRATCH:
        v->kind = AV_Z;
        v->n = t->size;
        return;
    case ST_OTAG: {
        if (t->size < 1 || t->size > 8) {
            fail(c, "an OT tag of %lld bytes", (long long)t->size);
            return;
        }
        int bits = (int)(8 * t->size - 8);
        uint64_t tag = memU(m, off, t->size), mask = MASK_BITS(bits);
        v->kind = AV_OTAG;
        v->num = pyUint(tag >> bits);
        v->word = tag & mask;
        v->end = v->word == mask;
        if (!v->end && warnOn) {
            CPtr p;
            encPointer(c, v->word, 0, &p);
        }
        return;
    }
    case ST_PTR: {
        uint64_t w = memU(m, off, 8);
        if (w == 0) return; /* None */
        v->kind = AV_PTR;
        v->word = w;
        if (t->fn) {
            v->fn = 1;
            char id[FN_ID];
            if (warnOn && !stateSchemaFnIdentity(c->a.s, w, id, sizeof id)) {
                warn(c, &c->enc, "function pointer to no known function");
            }
        } else if (warnOn) {
            CPtr p;
            encPointer(c, w, 0, &p);
        }
        return;
    }
    case ST_ARR: {
        const StType* et = ty(&c->a, t->of);
        if (!et || t->n == 0) {
            v->kind = AV_HEX;
            v->n = t->size;
            return;
        }
        if (et->kind == ST_SCRATCH) {
            v->kind = AV_Z;
            v->n = t->size;
            return;
        }
        if (et->kind == ST_INT) { /* scalars: little-endian bytes, the same in every build */
            v->kind = AV_HEX;
            v->n = et->size * t->n;
            v->w = et->size;
            return;
        }
        v->kind = AV_LIST;
        v->tid = t->of;
        v->n = t->n;
        v->es = et->size;
        return;
    }
    case ST_STRUCT:
        v->kind = AV_STRUCT;
        return;
    case ST_UNION: {
        int32_t nv;
        viewsOf(c, &c->a, tid, &nv);
        if (nv == 0) {
            if (warnOn) warn(c, &c->enc, "union kept as bytes");
            v->kind = AV_HEX;
            v->n = t->size;
            return;
        }
        if (warnOn) {
            char text[KEY_MAX];
            snprintf(text, sizeof text, "union %s encoded through every view", t->name);
            warn(c, &c->enc, text);
        }
        v->kind = AV_UNION;
        return;
    }
    default:
        v->kind = AV_HEX;
        v->n = t->size;
        return;
    }
}

/* Every value under v, as encode evaluates them, for its warnings. */
static void walk(Ctx* c, const AVal* v) {
    if (c->failed) return;
    AVal e;
    if (v->kind == AV_LIST) {
        for (int64_t i = 0; i < v->n; i++) {
            makeVal(c, v->tid, v->m, v->off + i * v->es, 1, &e);
            walk(c, &e);
        }
    } else if (v->kind == AV_STRUCT) {
        const StType* t = ty(&c->a, v->tid);
        for (int32_t i = 0; i < t->fieldCount; i++) {
            if (t->fields[i].bits) continue;
            fieldVal(c, v, i, 1, &e);
            if (e.kind == AV_SEL) {
                AVal sv;
                makeVal(c, e.tid, e.m, e.selOff, 1, &sv);
                walk(c, &sv);
            } else {
                walk(c, &e);
            }
        }
    } else if (v->kind == AV_UNION) {
        const StType* t = ty(&c->a, v->tid);
        int32_t nv;
        const int32_t* views = viewsOf(c, &c->a, v->tid, &nv);
        for (int32_t k = 0; k < nv; k++) {
            const StField* f = &t->fields[views[k]];
            makeVal(c, f->type, v->m, v->off + f->off, 1, &e);
            walk(c, &e);
        }
    }
}

/* The dict lookup v[key] of a struct's dict; 0 when the key is not in it. */
static int structGet(Ctx* c, const AVal* v, const char* key, int32_t hint, AVal* out) {
    if (v->kind != AV_STRUCT) return 0;
    for (int32_t k = v->nov - 1; k >= 0; k--) {
        if (strcmp(v->ov[k].key, key) == 0) {
            *out = *v->ov[k].val;
            return 1;
        }
    }
    int32_t i = lastKey(c, &c->a, v->tid, key, hint);
    if (i < 0) return 0;
    fieldVal(c, v, i, 0, out);
    return 1;
}

/* The pointer an AV_PTR stands for. */
static uint64_t valPointer(Ctx* c, const AVal* v) {
    CPtr p;
    if (v->fn) {
        p.nsteps = 0;
        p.kind = stateSchemaFnIdentity(c->a.s, v->word, p.fn, sizeof p.fn) ? P_FN : P_RAW;
        p.raw = v->word;
    } else {
        encPointer(c, v->word, 1, &p);
    }
    return decPointer(c, &p);
}

static const char* shapeName(const AVal* v) {
    static const char* const names[] = {"None", "an int", "a float", "an address", "scratch", "an OT tag",
                                        "a pointer", "bytes", "a list", "a struct", "a union", "a union view"};
    return names[v->kind];
}

/* ---- the reader's bytes (Decoder.put) ---------------------------------------------------------- */

/* The bytes of a dict's "x" (AV_HEX, AV_SEL). */
static int64_t hexBytes(const AVal* v, const uint8_t** p) { return memSlice(v->m, v->off, v->off + v->n, p); }

/* ---- the state rules (statecanon Rules, rule_target; docs/CANONICAL_STATES.md 17.2) -------------------- */

static int fieldAdd(Ctx* c, FieldList* l, FieldEntry e) {
    if (l->n == l->cap) {
        int32_t cap = l->cap ? l->cap * 2 : 16;
        FieldEntry* more = poolAlloc(c, (size_t)cap * sizeof *more);
        if (!more) return 0;
        if (l->n) memcpy(more, l->e, (size_t)l->n * sizeof *more);
        l->e = more;
        l->cap = cap;
    }
    l->e[l->n++] = e;
    return 1;
}

static int nameAdd(Ctx* c, NameList* l, NameEntry e) {
    if (l->n == l->cap) {
        int32_t cap = l->cap ? l->cap * 2 : 16;
        NameEntry* more = poolAlloc(c, (size_t)cap * sizeof *more);
        if (!more) return 0;
        if (l->n) memcpy(more, l->e, (size_t)l->n * sizeof *more);
        l->e = more;
        l->cap = cap;
    }
    l->e[l->n++] = e;
    return 1;
}

/* dict.setdefault: the first entry of (tid, key) */
static const FieldEntry* fieldFirst(const FieldList* l, int32_t tid, const char* key) {
    for (int32_t i = 0; i < l->n; i++) {
        if (l->e[i].tid == tid && strcmp(l->e[i].key, key) == 0) return &l->e[i];
    }
    return NULL;
}

/* {(t, old): cur for ...}: the last entry whose old is key */
static const FieldEntry* fieldBack(const FieldList* l, int32_t tid, const char* old) {
    for (int32_t i = l->n - 1; i >= 0; i--) {
        if (l->e[i].tid == tid && strcmp(l->e[i].old, old) == 0) return &l->e[i];
    }
    return NULL;
}

static const NameEntry* nameFirst(const NameList* l, const char* cur) {
    for (int32_t i = 0; i < l->n; i++) {
        if (strcmp(l->e[i].cur, cur) == 0) return &l->e[i];
    }
    return NULL;
}

static const NameEntry* nameBack(const NameList* l, const char* old) {
    for (int32_t i = l->n - 1; i >= 0; i--) {
        if (strcmp(l->e[i].old, old) == 0) return &l->e[i];
    }
    return NULL;
}

static int ruleIsHistory(const StRule* r) { return r->kind != ST_RULE_DEFAULT && r->kind != ST_RULE_HASHES; }

static void ruled(Ctx* c, int side, int32_t tid) {
    uint8_t* f = side ? c->R.ruledB : c->R.ruledA;
    int32_t n = side ? c->b.ntypes : c->a.ntypes;
    if (f && tid >= 0 && tid < n) f[tid] = 1;
}

/* Rules.__init__: the newer schema's history rules between the two revisions, in revision order; the reader's
 * defaults and type hashes. */
static void rulesInit(Ctx* c) {
    CRules* R = &c->R;
    int64_t wr = stateSchemaRevision(c->a.s), rr = stateSchemaRevision(c->b.s);
    R->forward = wr < rr;
    R->backward = wr > rr;
    R->older = wr < rr ? wr : rr;
    int64_t newerRev = wr > rr ? wr : rr;
    const StateSchema* newer = R->forward ? c->b.s : R->backward ? c->a.s : NULL;
    int newerSide = R->forward ? 1 : 0;
    R->ruledA = poolAlloc(c, (size_t)c->a.ntypes + 1);
    R->ruledB = poolAlloc(c, (size_t)c->b.ntypes + 1);
    int32_t n = newer ? stateSchemaRuleCount(newer) : 0;
    const StRule** hist = n ? poolAlloc(c, (size_t)n * sizeof *hist) : NULL;
    if (c->failed) return;
    int32_t nh = 0;
    for (int32_t i = 0; i < n; i++) {
        const StRule* r = stateSchemaRuleAt(newer, i);
        if (!r->readable) {
            R->unknown++;
        } else if (ruleIsHistory(r) && R->older < r->rev && r->rev <= newerRev) {
            int32_t j = nh++; /* a stable sort by revision */
            while (j > 0 && hist[j - 1]->rev > r->rev) {
                hist[j] = hist[j - 1];
                j--;
            }
            hist[j] = r;
        }
    }
    for (int32_t i = 0; i < nh && !c->failed; i++) {
        const StRule* r = hist[i];
        if (r->kind == ST_RULE_WAS) {
            if (r->on == ST_ON_FIELD) {
                for (int32_t k = 0; k < r->tidCount; k++) {
                    if (r->types[k] == ST_NONE || fieldFirst(&R->fieldWas, r->types[k], r->name)) continue;
                    FieldEntry e = {r->types[k], r->name, r->old, r, 0};
                    fieldAdd(c, &R->fieldWas, e);
                    ruled(c, newerSide, r->types[k]);
                }
            } else {
                NameList* l = r->on == ST_ON_VAR ? &R->varWas : r->on == ST_ON_TYPENAME ? &R->typeWas
                            : r->on == ST_ON_TAG ? &R->tagWas : &R->fnWas;
                if (!nameFirst(l, r->name)) {
                    NameEntry e = {r->name, r->old, r, 0};
                    nameAdd(c, l, e);
                }
            }
        } else if (r->kind == ST_RULE_VALUES || r->kind == ST_RULE_RESET) {
            if (r->on == ST_ON_FIELD) {
                for (int32_t k = 0; k < r->tidCount; k++) {
                    if (r->types[k] == ST_NONE) continue;
                    FieldEntry e = {r->types[k], r->name, NULL, r, 0};
                    fieldAdd(c, &R->fieldHist, e);
                    ruled(c, newerSide, r->types[k]);
                }
            } else {
                NameEntry e = {r->name, NULL, r, 0};
                nameAdd(c, &R->varHist, e);
            }
        } else if (r->kind == ST_RULE_REMOVED) {
            if (r->on == ST_ON_VARS) {
                for (int32_t k = 0; k < r->nameCount; k++) {
                    if (nameFirst(&R->removedVars, r->names[k])) continue;
                    NameEntry e = {r->names[k], NULL, r, r->value};
                    nameAdd(c, &R->removedVars, e);
                }
            } else {
                for (int32_t k = 0; k < r->tidCount; k++) {
                    for (int32_t q = 0; q < r->nameCount && r->types[k] != ST_NONE; q++) {
                        if (fieldFirst(&R->removedFields, r->types[k], r->names[q])) continue;
                        FieldEntry e = {r->types[k], r->names[q], NULL, r, r->value};
                        fieldAdd(c, &R->removedFields, e);
                        ruled(c, newerSide, r->types[k]);
                    }
                }
            }
        }
    }
    R->histCount = nh;
    for (int32_t i = 0; i < stateSchemaRuleCount(c->b.s) && !c->failed; i++) { /* the reader's own properties */
        const StRule* r = stateSchemaRuleAt(c->b.s, i);
        if (!r->readable || ruleIsHistory(r)) continue;
        if (r->kind == ST_RULE_DEFAULT && r->on == ST_ON_VAR) {
            if (!nameFirst(&R->defaultsVar, r->name)) {
                NameEntry e = {r->name, NULL, r, r->value};
                nameAdd(c, &R->defaultsVar, e);
            }
            continue;
        }
        for (int32_t k = 0; k < r->tidCount; k++) {
            FieldList* l = r->kind == ST_RULE_DEFAULT ? &R->defaultsField : &R->hashes;
            if (r->types[k] == ST_NONE || fieldFirst(l, r->types[k], r->name)) continue;
            FieldEntry e = {r->types[k], r->name, NULL, r, r->value};
            fieldAdd(c, l, e);
            ruled(c, 1, r->types[k]);
        }
    }
    R->active = nh > 0 || R->defaultsVar.n > 0 || R->defaultsField.n > 0 || R->hashes.n > 0;
}

/* The rules in force: none in a writer's own layout (writer_bytes), none when nothing applies. */
static CRules* rulesOn(Ctx* c) { return !c->self && c->R.active ? &c->R : NULL; }

/* Rules.source: what the writer calls the reader's thing; NULL when the writer has it not. */
static const char* nameSource(const CRules* R, const NameList* l, const char* ident) {
    if (R->forward) {
        const NameEntry* e = nameFirst(l, ident);
        return e ? e->old : ident;
    }
    if (R->backward) {
        const NameEntry* e = nameBack(l, ident);
        if (e) return e->cur;
        return nameFirst(l, ident) ? NULL : ident;
    }
    return ident;
}

/* Rules.target: the reader's name of the writer's thing; NULL when the reader has it not. */
static const char* nameTarget(const CRules* R, const NameList* l, const char* ident) {
    if (R->forward) {
        const NameEntry* e = nameBack(l, ident);
        if (e) return e->cur;
        return nameFirst(l, ident) ? NULL : ident;
    }
    if (R->backward) {
        const NameEntry* e = nameFirst(l, ident);
        return e ? e->old : ident;
    }
    return ident;
}

/* Rules.field_source: the writer's key of the reader's member key; NULL when the writer has it not. */
static const char* fieldSource(const CRules* R, int32_t rtid, int32_t wtid, const char* key) {
    if (R->forward) {
        if (rtid < 0 || !R->ruledB[rtid]) return key;
        const FieldEntry* e = fieldFirst(&R->fieldWas, rtid, key);
        return e ? e->old : key;
    }
    if (R->backward) {
        if (wtid < 0 || !R->ruledA[wtid]) return key;
        const FieldEntry* e = fieldBack(&R->fieldWas, wtid, key);
        if (e) return e->key;
        return fieldFirst(&R->fieldWas, wtid, key) ? NULL : key;
    }
    return key;
}

/* Rules.field_target: the reader's key of the writer's member key; NULL when the reader has it not. */
static const char* fieldTarget(const CRules* R, int32_t rtid, int32_t wtid, const char* wkey) {
    if (R->forward) {
        if (rtid < 0 || !R->ruledB[rtid]) return wkey;
        const FieldEntry* e = fieldBack(&R->fieldWas, rtid, wkey);
        if (e) return e->key;
        return fieldFirst(&R->fieldWas, rtid, wkey) ? NULL : wkey;
    }
    if (R->backward) {
        if (wtid < 0 || !R->ruledA[wtid]) return wkey;
        const FieldEntry* e = fieldFirst(&R->fieldWas, wtid, wkey);
        return e ? e->old : wkey;
    }
    return wkey;
}

/* Rules.fn_target: the reader's identity of the writer's function (its name through the renames). */
static const char* fnTarget(const CRules* R, const char* ident, char* buf, size_t size) {
    const char* colon = strrchr(ident, ':');
    const char* name = colon ? colon + 1 : ident;
    const char* t = nameTarget(R, &R->fnWas, name);
    if (!t || strcmp(t, name) == 0) return ident;
    snprintf(buf, size, "%.*s:%s", colon ? (int)(colon - ident) : 0, ident, t);
    return buf;
}

/* Rules.history: the values and reset rules of the newer build's member (type, key) or variable, in the
 * order they apply (forward: by revision; backward: the other way). */
static int32_t history(Ctx* c, const CRules* R, int32_t tid, const char* key, const char* var, const StRule** out,
                       int32_t max) {
    int32_t n = 0;
    if (var) {
        for (int32_t i = 0; i < R->varHist.n && n < max; i++) {
            if (strcmp(R->varHist.e[i].cur, var) == 0) out[n++] = R->varHist.e[i].r;
        }
    } else if (tid >= 0 && (R->forward ? R->ruledB : R->ruledA)[tid]) {
        for (int32_t i = 0; i < R->fieldHist.n && n < max; i++) {
            if (R->fieldHist.e[i].tid == tid && strcmp(R->fieldHist.e[i].key, key) == 0) out[n++] = R->fieldHist.e[i].r;
        }
    }
    (void)c;
    if (R->backward) {
        for (int32_t i = 0; i < n / 2; i++) {
            const StRule* t = out[i];
            out[i] = out[n - 1 - i];
            out[n - 1 - i] = t;
        }
    }
    return n;
}

static int pyIntIs(PyInt v, int64_t x) { return v.neg == (x < 0) && v.v == (uint64_t)x; }

/* map_value: forward old -> new, else new -> old; the first pair that matches, else v. */
static PyInt mapValue(PyInt v, const StRule* r, int forward) {
    for (int32_t i = 0; i < r->mapCount; i++) {
        if (pyIntIs(v, r->map[i][forward ? 0 : 1])) {
            int64_t x = r->map[i][forward ? 1 : 0];
            PyInt out = {(uint64_t)x, x < 0};
            return out;
        }
    }
    return v;
}

/* Rules.values: whether a reset rule drops the value (1), else the value through each values rule in *out: an
 * int, or an integer array's elements (as the writer's element type is signed). */
static int ruleValues(Ctx* c, const StRule* const* rs, int32_t n, const AVal* val, int32_t wtype, AVal* out) {
    *out = *val;
    int any = 0;
    for (int32_t i = 0; i < n; i++) {
        if (rs[i]->kind == ST_RULE_RESET) return 1;
        any |= rs[i]->kind == ST_RULE_VALUES;
    }
    if (!any) return 0;
    if (val->kind == AV_INT) {
        for (int32_t i = 0; i < n; i++) {
            if (rs[i]->kind == ST_RULE_VALUES) out->num = mapValue(out->num, rs[i], c->R.forward);
        }
        return 0;
    }
    if (val->kind == AV_HEX && val->w > 0 && val->w <= 8) {
        const uint8_t* x;
        int64_t len = hexBytes(val, &x), w = val->w;
        const StType* wt = ty(&c->a, wtype);
        const StType* et = wt && wt->kind == ST_ARR ? ty(&c->a, wt->of) : NULL;
        int sign = et && et->isSigned;
        uint8_t* p = poolAlloc(c, (size_t)len + 1);
        Mem* m = poolAlloc(c, sizeof *m);
        if (!p || !m) return 0;
        memcpy(p, x, (size_t)len);
        for (int64_t i = 0; i + w <= len; i += w) {
            PyInt e = bytesInt(p + i, w, sign);
            for (int32_t k = 0; k < n; k++) {
                if (rs[k]->kind == ST_RULE_VALUES) e = mapValue(e, rs[k], c->R.forward);
            }
            intOut(e, p + i, w);
        }
        m->p = p;
        m->n = len;
        out->m = m;
        out->off = 0;
        out->n = len;
    }
    return 0;
}

/* pcStateTypeId: FNV-1a of a type's name without its blanks; never 0. */
static uint32_t typeHash(const char* s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) {
        if (*s != ' ' && *s != '\t' && *s != '\n') h = (h ^ (uint8_t)*s) * 0x01000193u;
    }
    return h ? h : 1;
}

/* Decoder.remap_hash: the writer's heap type hash as the reader names the type, where a rule renamed it and
 * the reader knows the hash; else as it is. */
static uint32_t remapHash(Ctx* c, uint32_t h) {
    CRules* R = rulesOn(c);
    if (!R || !h || !R->typeWas.n) return h;
    const StHeapType* e = stateSchemaHeapType(c->a.s, h);
    if (!e || !e->name) return h;
    size_t n = strlen(e->name), base = n;
    if (n >= 3 && strcmp(e->name + n - 3, "[1]") == 0) {
        base = n - 3;
    } else {
        while (base > 0 && e->name[base - 1] == '*') base--;
    }
    char b[KEY_MAX], full[KEY_MAX];
    snprintf(b, sizeof b, "%.*s", (int)base, e->name);
    const char* t = nameTarget(R, &R->typeWas, b);
    if (!t || strcmp(t, b) == 0) return h;
    snprintf(full, sizeof full, "%s%s", t, e->name + base);
    uint32_t h2 = typeHash(full);
    if (!stateSchemaHeapType(c->b.s, h2)) return h;
    warn(c, &c->notes, "type hash remapped by a state rule");
    return h2;
}

/* ---- a value of another type than the reader's (statecanon type_class, plain, mismatch, reshape) ------- */

enum { K_INT, K_FLOAT, K_ADDR, K_PTR, K_OTAG, K_SCRATCH, K_ARR, K_STRUCT, K_UNION, K_BYTES };

static int typeClass(const StType* t) {
    switch (t->kind) {
    case ST_INT: return K_INT;
    case ST_FLOAT: return K_FLOAT;
    case ST_ADDR: return K_ADDR;
    case ST_PTR: return K_PTR;
    case ST_OTAG: return K_OTAG;
    case ST_SCRATCH: return K_SCRATCH;
    case ST_ARR: return K_ARR;
    case ST_STRUCT: return K_STRUCT;
    case ST_UNION: return K_UNION;
    default: return K_BYTES;
    }
}

/* plain: no pointer, ordering-table link, address or scratch word inside; a type that holds itself is not. */
static int plainType(Ctx* c, const Side* s, int32_t tid) {
    const StType* t = ty(s, tid);
    if (!t) return 0;
    int8_t* memo = s == &c->a || s->s == c->a.s ? c->plainA : c->plainB;
    if (memo[tid] == 2) return 0;
    if (memo[tid] >= 0) return memo[tid];
    memo[tid] = 2;
    int r;
    if (t->kind == ST_PTR || t->kind == ST_ADDR || t->kind == ST_OTAG || t->kind == ST_SCRATCH) {
        r = 0;
    } else if (t->kind == ST_ARR) {
        r = !ty(s, t->of) || t->n == 0 || plainType(c, s, t->of);
    } else if (t->kind == ST_STRUCT || t->kind == ST_UNION) {
        r = 1;
        for (int32_t i = 0; i < t->fieldCount && r; i++) r = plainType(c, s, t->fields[i].type);
    } else {
        r = 1;
    }
    memo[tid] = (int8_t)r;
    return r;
}

/* bytes_differ: another kind, size, or an array's elements of another size. */
static int bytesDiffer(Ctx* c, int32_t wt, int32_t rt) {
    const StType* a = ty(&c->a, wt);
    const StType* b = ty(&c->b, rt);
    if (!a || !b) return 0;
    if (typeClass(a) != typeClass(b) || a->size != b->size) return 1;
    return a->kind == ST_ARR && tsize(&c->a, a->of) != tsize(&c->b, b->of);
}

/* Decoder.mismatch: the writer's value of type wt cannot be put as the reader's type tid by value. */
static int mismatch(Ctx* c, int32_t wt, int32_t tid, const AVal* val) {
    const StType* a = ty(&c->a, wt);
    const StType* b = ty(&c->b, tid);
    if (!a || !b) return 0;
    int ca = typeClass(a), cb = typeClass(b);
    if (cb == K_SCRATCH) return 0;
    if (ca == cb) {
        if (ca == K_FLOAT) return a->size != b->size;
        if (ca == K_ARR && (val->kind == AV_HEX || val->kind == AV_SEL)) { /* integers (or bytes) into other elements */
            const StType* et = ty(&c->b, b->of);
            return et && b->n > 0 && typeClass(et) != K_INT && typeClass(et) != K_BYTES;
        }
        return 0;
    }
    if ((ca == K_INT && cb == K_ADDR) || (ca == K_ADDR && cb == K_INT)) return cb == K_INT && val->kind != AV_INT;
    return 1;
}

static int put(Ctx* c, int32_t tid, const AVal* val, Buf* buf, int64_t off, int32_t wt);

/* Decoder.writer_bytes: a writer's value as its own build lays it out (its schema both ways, no rules). */
static void writerBytes(Ctx* c, int32_t wt, const AVal* val, Buf* out) {
    if (!bufZero(c, out, tsize(&c->a, wt))) return;
    Side reader = c->b;
    c->b = c->a;
    c->self++;
    put(c, wt, val, out, 0, ST_NONE);
    c->self--;
    c->b = reader;
}

static void carryBytes(Ctx* c, int32_t tid, const AVal* val, Buf* buf, int64_t off, int32_t wt) {
    Buf b = {0};
    writerBytes(c, wt, val, &b);
    int64_t n = b.n < tsize(&c->b, tid) ? b.n : tsize(&c->b, tid);
    if (n < 0) n = 0;
    bufSet(c, buf, off, off + n, b.p, n);
    free(b.p);
    warn(c, &c->notes, "value of another type carried as its bytes");
}

/* ---- the reader's defaults (Decoder.defaults_in, put_defaults, put_int) ------------------------------- */

typedef struct {
    int64_t off;    /* of the struct the member is in */
    int32_t owner;  /* that struct (a reader type) */
    int32_t field;  /* the member */
    int64_t value;
} DefaultAt;

typedef struct DefaultList {
    DefaultAt* e;
    int32_t n;
} DefaultList;

static void putBits(Ctx* c, Buf* buf, int64_t off, int32_t bit, int32_t nbits, PyInt value) {
    int64_t base = floorDiv(bit, 8), span = floorDiv((int64_t)(bit % 8) + nbits + 7, 8);
    uint8_t raw[16] = {0};
    int64_t a = off + base, e = off + base + span;
    pySlice(buf->n, &a, &e);
    memcpy(raw, buf->p + a, (size_t)(e - a));
    int sh = bit % 8;
    for (int32_t k = 0; k < nbits; k++) {
        int64_t at = sh + k;
        int bitv = k < 64 ? (int)((value.v >> k) & 1) : value.neg;
        if (bitv) raw[at / 8] |= (uint8_t)(1u << (at % 8));
        else raw[at / 8] &= (uint8_t) ~(1u << (at % 8));
    }
    bufSet(c, buf, off + base, off + base + span, raw, span);
}

/* put_int: an integer into a reader's member (owner's field; owner ST_NONE: the whole of type tid): a
 * bitfield's bits, an integer, or each element of an integer array. */
static void putInt(Ctx* c, int32_t owner, int32_t field, int32_t tid, int64_t value, Buf* buf, int64_t base) {
    PyInt v = {(uint64_t)value, value < 0};
    uint8_t b[16];
    int64_t at = base;
    if (owner != ST_NONE) {
        const StField* f = &ty(&c->b, owner)->fields[field];
        if (f->bits) {
            if (f->bits <= 64) putBits(c, buf, base, f->bit, f->bits, v);
            return;
        }
        tid = f->type;
        at = base + f->off;
    }
    const StType* t = ty(&c->b, tid);
    if (!t) return;
    if (t->kind == ST_ARR) {
        int64_t es = tsize(&c->b, t->of);
        for (int64_t i = 0; es > 0 && es <= (int64_t)sizeof b && i < t->n && !c->failed; i++) {
            intOut(v, b, es);
            bufSet(c, buf, at + i * es, at + (i + 1) * es, b, es);
        }
    } else if (t->kind == ST_INT && t->size <= (int64_t)sizeof b) {
        intOut(v, b, t->size);
        bufSet(c, buf, at, at + t->size, b, t->size);
    }
}

static int defaultsAdd(Ctx* c, DefaultList* l, int32_t* cap, DefaultAt d) {
    if (l->n == *cap) {
        int32_t more = *cap ? *cap * 2 : 8;
        DefaultAt* e = realloc(l->e, (size_t)more * sizeof *e);
        if (!e) {
            fail(c, "out of memory");
            return 0;
        }
        l->e = e;
        *cap = more;
    }
    l->e[l->n++] = d;
    return 1;
}

/* defaults_in: the reader's defaults inside a type (its members', arrays of structs' too; not through
 * pointers or unions), cached; a type that holds itself: none. */
static const DefaultList* defaultsIn(Ctx* c, int32_t tid) {
    static const DefaultList none = {NULL, 0};
    CRules* R = rulesOn(c);
    const StType* t = ty(&c->b, tid);
    if (!R || !R->defaultsField.n || !t) return &none;
    if (c->defaultsState[tid] == 2) return &c->defaults[tid];
    if (c->defaultsState[tid] == 1) return &none;
    c->defaultsState[tid] = 1;
    DefaultList out = {NULL, 0};
    int32_t cap = 0;
    if (t->kind == ST_STRUCT) {
        const char* const* keys = keysOf(c, &c->b, tid);
        for (int32_t i = 0; keys && i < t->fieldCount && !c->failed; i++) {
            const FieldEntry* d = R->ruledB[tid] ? fieldFirst(&R->defaultsField, tid, keys[i]) : NULL;
            if (d) {
                DefaultAt e = {0, tid, i, d->value};
                defaultsAdd(c, &out, &cap, e);
            } else if (!t->fields[i].bits) {
                const DefaultList* sub = defaultsIn(c, t->fields[i].type);
                for (int32_t k = 0; k < sub->n; k++) {
                    DefaultAt e = sub->e[k];
                    e.off += t->fields[i].off;
                    defaultsAdd(c, &out, &cap, e);
                }
            }
        }
    } else if (t->kind == ST_ARR && ty(&c->b, t->of) && t->n) {
        const DefaultList* sub = defaultsIn(c, t->of);
        int64_t es = tsize(&c->b, t->of);
        for (int64_t i = 0; sub->n && i < t->n && !c->failed; i++) {
            for (int32_t k = 0; k < sub->n; k++) {
                DefaultAt e = sub->e[k];
                e.off += i * es;
                defaultsAdd(c, &out, &cap, e);
            }
        }
    }
    c->defaults[tid] = out;
    c->defaultsState[tid] = 2;
    return &c->defaults[tid];
}

static int64_t putDefaults(Ctx* c, int32_t tid, Buf* buf, int64_t off) {
    const DefaultList* l = defaultsIn(c, tid);
    for (int32_t i = 0; i < l->n && !c->failed; i++) {
        putInt(c, l->e[i].owner, l->e[i].field, ST_NONE, l->e[i].value, buf, off + l->e[i].off);
    }
    return l->n;
}

/* Decoder.reshape: a value of another kind of type, by its bytes when neither type holds a pointer, else not
 * carried (the reader's defaults inside it). 1: not carried. */
static int reshape(Ctx* c, int32_t tid, const AVal* val, Buf* buf, int64_t off, int32_t wt) {
    if (plainType(c, &c->a, wt) && plainType(c, &c->b, tid)) {
        carryBytes(c, tid, val, buf, off, wt);
        return 0;
    }
    warn(c, &c->dec, "value of another type in the state (not carried)");
    putDefaults(c, tid, buf, off);
    return 1;
}

/* The type of the writer struct wt's member key (writer_fields: the last of a key), ST_NONE if none. */
static int32_t writerFieldType(Ctx* c, int32_t wt, const char* key) {
    const StType* t = ty(&c->a, wt);
    if (!t || (t->kind != ST_STRUCT && t->kind != ST_UNION) || !key) return ST_NONE;
    int32_t i = lastKey(c, &c->a, wt, key, -1);
    return i >= 0 ? t->fields[i].type : ST_NONE;
}

/* Decoder.retail_carry: a game member the writer has not as a value of its type: its PS1 bytes, from the
 * writer's members of the same struct that cover them and hold no pointer. 1 when any byte came. */
static int retailCarry(Ctx* c, const StField* f, const AVal* val, int32_t wt, Buf* buf, int64_t off) {
    const StType* wtt = c->self ? NULL : ty(&c->a, wt);
    if (f->retail == ST_NO_RETAIL || f->bits || !wtt || wtt->kind != ST_STRUCT || val->kind != AV_STRUCT ||
        !plainType(c, &c->b, f->type)) {
        return 0;
    }
    int64_t lo = f->retail, size = tsize(&c->b, f->type), hi = lo + size;
    if (size <= 0) return 0;
    const char* const* keys = keysOf(c, &c->a, wt);
    uint8_t* got = calloc((size_t)size, 2);
    if (!got || !keys) {
        free(got);
        fail(c, "out of memory");
        return 0;
    }
    uint8_t* cover = got + size;
    Buf gb = {0};
    for (int32_t i = 0; i < wtt->fieldCount && !c->failed; i++) {
        if (firstKey(c, &c->a, wt, keys[i]) != i) continue; /* writer_fields' order: each key where it first is */
        int32_t gi = lastKey(c, &c->a, wt, keys[i], -1);
        const StField* g = &wtt->fields[gi];
        AVal child;
        if (g->retail == ST_NO_RETAIL || g->bits || !structGet(c, val, keys[i], gi, &child) ||
            !plainType(c, &c->a, g->type)) {
            continue;
        }
        int64_t a = lo > g->retail ? lo : g->retail, gs = tsize(&c->a, g->type);
        int64_t b = hi < g->retail + gs ? hi : g->retail + gs;
        if (a >= b) continue;
        writerBytes(c, g->type, &child, &gb);
        for (int64_t k = a; k < b; k++) {
            int64_t at = k - g->retail;
            got[k - lo] = at < gb.n ? gb.p[at] : 0;
            cover[k - lo] = 1;
        }
    }
    free(gb.p);
    int any = 0;
    for (int64_t i = 0; i < size && !any; i++) any = cover[i];
    if (any) {
        int64_t base = off + f->off;
        for (int64_t i = 0; i < size;) {
            if (!cover[i]) {
                i++;
                continue;
            }
            int64_t j = i;
            while (j < size && cover[j]) j++;
            bufSet(c, buf, base + i, base + j, got + i, j - i);
            i = j;
        }
        warn(c, &c->notes, "carried by its PS1 bytes");
    }
    free(got);
    return any;
}

enum { LOST_MISSING, LOST_RESET, LOST_TYPE };

/* Decoder.field_lost: a reader's member the state does not carry: a newer writer's removed value, else its
 * default or those inside it, else zero. */
static void fieldLost(Ctx* c, int32_t tid, int32_t wt, const char* key, int32_t field, Buf* buf, int64_t off, int why) {
    CRules* R = rulesOn(c);
    const StField* f = &ty(&c->b, tid)->fields[field];
    if (R && why == LOST_MISSING && R->backward && wt >= 0 && R->ruledA[wt]) {
        const FieldEntry* e = fieldFirst(&R->removedFields, wt, key);
        if (e) {
            putInt(c, tid, field, ST_NONE, e->value, buf, off);
            warn(c, &c->dec, "field removed by a later build (given the value its rule names)");
            return;
        }
    }
    const FieldEntry* d = R && R->ruledB[tid] ? fieldFirst(&R->defaultsField, tid, key) : NULL;
    int64_t n = 0;
    if (d) {
        putInt(c, tid, field, ST_NONE, d->value, buf, off);
        n = 1;
    } else if (!f->bits && why != LOST_TYPE) {
        n = putDefaults(c, f->type, buf, off + f->off);
    }
    if (why == LOST_MISSING) {
        warn(c, &c->dec, n ? "field missing in the state (given its default)" : "field missing in the state (left zero)");
    } else if (why == LOST_RESET) {
        warn(c, &c->dec, "field reset by a state rule (not carried)");
    }
}

/* var_lost: a reader's variable the state does not carry. */
static void varLost(Ctx* c, const StVar* v, Buf* buf, int64_t off, int why) {
    CRules* R = rulesOn(c);
    if (R && why == LOST_MISSING && R->backward) {
        const NameEntry* e = nameFirst(&R->removedVars, v->id);
        if (e) {
            putInt(c, ST_NONE, 0, v->type, e->value, buf, off);
            warn(c, &c->dec, "variable removed by a later build (given the value its rule names)");
            return;
        }
    }
    const NameEntry* d = R ? nameFirst(&R->defaultsVar, v->id) : NULL;
    int64_t n = 0;
    if (d) {
        putInt(c, ST_NONE, 0, v->type, d->value, buf, off);
        n = 1;
    } else if (why != LOST_TYPE) {
        n = putDefaults(c, v->type, buf, off);
    }
    if (why == LOST_MISSING) {
        warn(c, &c->dec, n ? "variable not in the state (given its default)" : "variable not in the state (left zero)");
    } else if (why == LOST_RESET) {
        warn(c, &c->dec, "variable reset by a state rule (not carried)");
    }
}

#define MAX_RULES 64 /* values and reset rules of one member, more are not applied (never so many) */

static void putStruct(Ctx* c, int32_t tid, const StType* t, const AVal* val, Buf* buf, int64_t off, int32_t wt) {
    if (val->kind == AV_INT) {
        fail(c, "a struct %s holds an int in the state", t->name);
        return;
    }
    if (val->kind == AV_STRUCT) { /* "pad": the writer's bytes outside every field, where this build has as many */
        int32_t na, nb;
        int64_t ta, tb;
        const int64_t* ga = gapsOf(c, &c->a, val->tid, &na, &ta);
        const int64_t* gb = gapsOf(c, &c->b, tid, &nb, &tb);
        if (na && ga && gb) {
            uint8_t small[256];
            uint8_t* pad = ta <= (int64_t)sizeof small ? small : malloc((size_t)ta);
            if (!pad) {
                fail(c, "out of memory");
                return;
            }
            int64_t len = 0;
            int any = 0;
            for (int32_t k = 0; k < na; k++) {
                const uint8_t* p;
                int64_t n = memSlice(val->m, val->off + ga[2 * k], val->off + ga[2 * k + 1], &p);
                memcpy(pad + len, p, (size_t)n);
                for (int64_t i = 0; i < n && !any; i++) any = p[i] != 0;
                len += n;
            }
            if (any && tb == len) {
                int64_t at = 0;
                for (int32_t k = 0; k < nb; k++) {
                    int64_t lo = gb[2 * k], hi = gb[2 * k + 1];
                    bufSet(c, buf, off + lo, off + hi, pad + at, hi - lo);
                    at += hi - lo;
                }
            }
            if (pad != small) free(pad);
        }
    }
    const char* const* keys = keysOf(c, &c->b, tid);
    if (!keys) return;
    const StType* wtt = c->self ? NULL : ty(&c->a, wt);
    if (wtt && wtt->kind != ST_STRUCT && wtt->kind != ST_UNION) wtt = NULL; /* (writer_fields: {}) */
    CRules* R = rulesOn(c);
    for (int32_t i = 0; i < t->fieldCount && !c->failed; i++) {
        const StField* f = &t->fields[i];
        const char* key = keys[i];
        const char* src = R ? fieldSource(R, tid, wt, key) : key;
        AVal child;
        if (!src || !structGet(c, val, src, i, &child)) {
            if (!retailCarry(c, f, val, wt, buf, off)) fieldLost(c, tid, wt, key, i, buf, off, LOST_MISSING);
            continue;
        }
        if (src != key && strcmp(src, key) != 0) warn(c, &c->notes, "renamed by a state rule");
        int32_t cwt = wtt ? writerFieldType(c, wt, src) : ST_NONE;
        if (R) {
            const StRule* rs[MAX_RULES];
            int32_t nr = history(c, R, R->forward ? tid : wt, R->forward ? key : src, NULL, rs, MAX_RULES);
            if (nr) {
                AVal mapped;
                if (ruleValues(c, rs, nr, &child, cwt, &mapped)) {
                    fieldLost(c, tid, wt, key, i, buf, off, LOST_RESET);
                    continue;
                }
                child = mapped;
                for (int32_t k = 0; k < nr; k++) {
                    if (rs[k]->kind == ST_RULE_VALUES) {
                        warn(c, &c->notes, "values mapped by a state rule");
                        break;
                    }
                }
            }
            if (R->ruledB[tid] && child.kind == AV_INT && !child.num.neg && child.num.v <= 0xFFFFFFFFu &&
                fieldFirst(&R->hashes, tid, key)) {
                child.num = pyUint(remapHash(c, (uint32_t)child.num.v));
            }
        }
        if (f->bits) {
            if (!c->self && child.kind != AV_INT) {
                warn(c, &c->dec, "value of another type in the state (not carried)");
                fieldLost(c, tid, wt, key, i, buf, off, LOST_TYPE);
                continue;
            }
            if (child.kind != AV_INT) {
                fail(c, "the bitfield %s.%s holds %s in the state", t->name, f->name, shapeName(&child));
                return;
            }
            if (f->bits > 64) {
                fail(c, "a bitfield wider than 64 bits");
                return;
            }
            putBits(c, buf, off, f->bit, f->bits, child.num);
            continue;
        }
        if (child.kind == AV_SEL) {
            const uint8_t* x;
            int64_t nx = hexBytes(&child, &x), n = tsize(&c->b, f->type);
            if (nx > n) nx = n < 0 ? 0 : n;
            bufSet(c, buf, off + f->off, off + f->off + nx, x, nx);
            const char* sel = child.sel;
            const StType* wut = c->self ? NULL : ty(&c->a, cwt);
            if (R && wut) sel = fieldTarget(R, f->type, cwt, sel); /* the writer's view by the reader's name */
            int32_t view = sel ? fieldNamed(ty(&c->b, f->type), sel) : -1;
            if (view < 0) {
                warn(c, &c->dec, "union view not in this build (bytes kept)");
                continue;
            }
            if (strcmp(sel, child.sel) != 0) warn(c, &c->notes, "renamed by a state rule");
            int32_t wv = wut ? fieldNamed(wut, child.sel) : -1;
            const StField* vf = &ty(&c->b, f->type)->fields[view];
            AVal vv;
            makeVal(c, child.tid, child.m, child.selOff, 0, &vv);
            put(c, vf->type, &vv, buf, off + f->off + vf->off, wv >= 0 ? wut->fields[wv].type : ST_NONE);
            continue;
        }
        if (cwt != ST_NONE && f->retail != ST_NO_RETAIL && bytesDiffer(c, cwt, f->type) &&
            retailCarry(c, f, val, wt, buf, off)) {
            continue;
        }
        if (put(c, f->type, &child, buf, off + f->off, cwt)) fieldLost(c, tid, wt, key, i, buf, off, LOST_TYPE);
    }
}

/* Decoder.put: the reader's bytes of the writer's value as type tid; wt: the writer's type of the value
 * (ST_NONE: unknown). 1 when a value of another kind was not carried. */
static int put(Ctx* c, int32_t tid, const AVal* val, Buf* buf, int64_t off, int32_t wt) {
    if (c->failed) return 0;
    const StType* t = ty(&c->b, tid);
    if (!t) return 0;
    if (c->self) wt = ST_NONE;
    if (wt != ST_NONE && mismatch(c, wt, tid, val)) return reshape(c, tid, val, buf, off, wt);
    if (val->kind == AV_NONE && t->kind != ST_PTR) return 0;
    const StType* wtt = ty(&c->a, wt);
    uint8_t b[16];
    switch (t->kind) {
    case ST_INT:
        if (val->kind != AV_INT) {
            fail(c, "an int %s holds %s in the state", t->name, shapeName(val));
            return 0;
        }
        if (t->size > (int64_t)sizeof b) {
            fail(c, "an int of %lld bytes", (long long)t->size);
            return 0;
        }
        intOut(val->num, b, t->size);
        bufSet(c, buf, off, off + t->size, b, t->size);
        return 0;
    case ST_FLOAT: {
        if (val->kind != AV_FLOAT) {
            fail(c, "a float holds %s in the state", shapeName(val));
            return 0;
        }
        const uint8_t* p;
        int64_t n = hexBytes(val, &p);
        bufSet(c, buf, off, off + n, p, n);
        return 0;
    }
    case ST_ADDR: {
        PyInt v;
        if (val->kind == AV_ADDR) {
            v = pyUint(movePointer(c, val->word));
        } else if (val->kind == AV_INT) {
            v = val->num;
        } else {
            fail(c, "an address holds %s in the state", shapeName(val));
            return 0;
        }
        if (t->size > (int64_t)sizeof b) {
            fail(c, "an address of %lld bytes", (long long)t->size);
            return 0;
        }
        intOut(v, b, t->size);
        bufSet(c, buf, off, off + t->size, b, t->size);
        return 0;
    }
    case ST_SCRATCH:
        bufSet(c, buf, off, off + t->size, NULL, t->size);
        return 0;
    case ST_OTAG: {
        if (val->kind != AV_OTAG || t->size < 1 || t->size > 8) {
            fail(c, "an OT tag holds %s in the state", shapeName(val));
            return 0;
        }
        int bits = (int)(8 * t->size - 8);
        uint64_t mask = MASK_BITS(bits);
        uint64_t link = val->end ? mask : movePointer(c, val->word) & mask;
        if (val->num.neg || (val->num.v >> 8) != 0) {
            fail(c, "an OT tag's count does not fit");
            return 0;
        }
        intOut(pyUint((bits < 64 ? val->num.v << bits : 0) | link), b, t->size);
        bufSet(c, buf, off, off + t->size, b, t->size);
        return 0;
    }
    case ST_PTR: {
        uint64_t a = 0;
        if (val->kind == AV_PTR) {
            a = valPointer(c, val);
        } else if (val->kind != AV_NONE) {
            fail(c, "a pointer holds %s in the state", shapeName(val));
            return 0;
        }
        intOut(pyUint(a), b, 8);
        bufSet(c, buf, off, off + 8, b, 8);
        return 0;
    }
    case ST_ARR:
        if (val->kind == AV_Z) {
            bufSet(c, buf, off, off + t->size, NULL, t->size);
        } else if (val->kind == AV_HEX || val->kind == AV_SEL) {
            const uint8_t* p;
            int64_t n = hexBytes(val, &p);
            const StType* et = ty(&c->b, t->of);
            int64_t w = val->kind == AV_HEX ? val->w : 0, es = et ? et->size : 0;
            if (wtt && w && es && w != es && et->kind == ST_INT && es <= (int64_t)sizeof b) {
                /* integers of another width: each element by its value (as the writer's elements are signed) */
                const StType* we = wtt->kind == ST_ARR ? ty(&c->a, wtt->of) : NULL;
                int sign = we && we->isSigned;
                int64_t m = floorDiv(n, w) < t->n ? floorDiv(n, w) : t->n;
                for (int64_t i = 0; i < m && !c->failed; i++) {
                    intOut(bytesInt(p + i * w, w < 8 ? w : 8, sign && w <= 8), b, es);
                    bufSet(c, buf, off + i * es, off + (i + 1) * es, b, es);
                }
                warn(c, &c->notes, "integers of another width carried by value");
            } else {
                if (n > t->size) n = t->size;
                bufSet(c, buf, off, off + n, p, n);
            }
        } else if (val->kind == AV_LIST) {
            int64_t es = tsize(&c->b, t->of), n = val->n < t->n ? val->n : t->n;
            int32_t wof = wtt && wtt->kind == ST_ARR ? wtt->of : ST_NONE;
            for (int64_t i = 0; i < n && !c->failed; i++) {
                AVal e;
                makeVal(c, val->tid, val->m, val->off + i * val->es, 0, &e);
                put(c, t->of, &e, buf, off + i * es, wof);
            }
        } else {
            fail(c, "an array holds %s in the state", shapeName(val));
        }
        return 0;
    case ST_STRUCT:
        putStruct(c, tid, t, val, buf, off, wt);
        return 0;
    case ST_UNION:
        if (val->kind == AV_UNION) { /* least typed first: the fully typed view writes last */
            int32_t nb, na;
            const int32_t* vb = viewsOf(c, &c->b, tid, &nb);
            const int32_t* va = viewsOf(c, &c->a, val->tid, &na);
            const StType* at = ty(&c->a, val->tid);
            CRules* R = rulesOn(c);
            for (int32_t k = 0; k < nb && !c->failed; k++) {
                const StField* f = &t->fields[vb[k]];
                const char* name = R ? fieldSource(R, tid, wt, f->name) : f->name;
                int32_t hit = -1; /* the dict comprehension keeps the last view of a name */
                for (int32_t j = 0; name && j < na; j++) {
                    if (strcmp(at->fields[va[j]].name, name) == 0) hit = va[j];
                }
                if (hit < 0) {
                    warn(c, &c->dec, "union view not in the state");
                    continue;
                }
                if (strcmp(name, f->name) != 0) warn(c, &c->notes, "renamed by a state rule");
                int32_t wv = -1;
                if (wtt) { /* the writer union's member of that name: the last (wv: {name: f for f in ...}) */
                    for (int32_t j = 0; j < wtt->fieldCount; j++) {
                        if (strcmp(wtt->fields[j].name, name) == 0) wv = j;
                    }
                }
                AVal e;
                makeVal(c, at->fields[hit].type, val->m, val->off + at->fields[hit].off, 0, &e);
                put(c, f->type, &e, buf, off + f->off, wv >= 0 ? wtt->fields[wv].type : ST_NONE);
            }
            return 0;
        }
        break;
    default:
        break;
    }
    if (val->kind != AV_HEX && val->kind != AV_SEL) {
        fail(c, "a %s holds %s in the state", t->kind == ST_UNION ? "union" : "value", shapeName(val));
        return 0;
    }
    const uint8_t* p;
    int64_t n = hexBytes(val, &p);
    if (n > t->size) n = t->size;
    bufSet(c, buf, off, off + n, p, n);
    return 0;
}

/* ---- the writer's heap and sound arena (heap_blocks, arena_blocks, block_layout) ---------------- */

static int32_t lastChunk(const Ctx* c, const char* tag) {
    for (int32_t i = c->nac - 1; i >= 0; i--) {
        if (tagIs(c->ac[i].tag, c->ac[i].tagLen, tag)) return i;
    }
    return -1;
}

/* The first section holding addr (the schema's order), -1 if none. */
static int32_t sectionHolding(const StateSchema* s, uint64_t addr) {
    for (int32_t i = 0; i < stateSchemaSectionCount(s); i++) {
        StRange r = stateSchemaSectionAt(s, i);
        if (r.lo <= addr && addr < r.hi) return i;
    }
    return -1;
}

static int32_t sectionChunk(const Ctx* c, int32_t sec) {
    char tag[300];
    snprintf(tag, sizeof tag, "mem.%s", stateSchemaSectionAt(c->a.s, sec).name);
    return lastChunk(c, tag);
}

/* heap_blocks: the heap's blocks from heapFirstBlockData, in their chain. */
static int heapBlocks(Ctx* c) {
    const StateSchema* s = c->a.s;
    StRange arena = stateSchemaRegion(s, "heapArena");
    int32_t vi = stateSchemaVarNamed(s, "heapFirstBlockData");
    if (!arena.name || vi == ST_NONE) {
        fail(c, "the writer's schema has no heapArena or heapFirstBlockData");
        return 0;
    }
    const StVar* v = stateSchemaVarAt(s, vi);
    int32_t sec = sectionHolding(s, v->addr);
    int32_t ci = sec >= 0 ? sectionChunk(c, sec) : -1;
    if (ci < 0) {
        fail(c, "the writer's state has no section holding heapFirstBlockData");
        return 0;
    }
    c->heap.lo = arena.lo;
    c->heap.hi = arena.hi;
    const Mem* heap = &c->heap.mem;
    uint64_t data = memU(&c->acMem[ci], (int64_t)(v->addr - stateSchemaSectionAt(s, sec).lo), 8);
    int32_t cap = 256, n = 0;
    ABlock* b = malloc((size_t)cap * sizeof *b);
    if (!b) {
        fail(c, "out of memory");
        return 0;
    }
    for (int32_t it = 0; it < (1 << 18); it++) {
        int64_t o = diffU(data, HEAP_HEADER, arena.lo);
        if (!(0 <= o && o <= heap->n - HEAP_HEADER)) break;
        if (n == cap) {
            ABlock* more = realloc(b, (size_t)cap * 2 * sizeof *b);
            if (!more) {
                free(b);
                fail(c, "out of memory");
                return 0;
            }
            b = more;
            cap *= 2;
        }
        ABlock* k = &b[n++];
        memset(k, 0, sizeof *k);
        uint64_t nxt = memU(heap, o, 8);
        uint32_t bits = (uint32_t)memU(heap, o + 8, 4);
        k->data = data;
        k->bits = bits;
        k->user = (int)((bits >> 21) & 0xF);
        if (k->user == 1) {
            k->end = 1;
            k->next = nxt;
            break;
        }
        k->size = diffU(nxt, HEAP_HEADER, data);
        k->pinned = (int)((bits >> 25) & 1);
        k->type = (int)((bits >> 26) & 0x3F);
        k->pctype = (uint32_t)memU(heap, o + 12, 4);
        data = nxt;
    }
    c->heap.b = poolAlloc(c, (size_t)(n + 1) * sizeof *b);
    c->heap.starts = poolAlloc(c, (size_t)(n + 1) * sizeof *c->heap.starts);
    if (c->heap.b && c->heap.starts) {
        memcpy(c->heap.b, b, (size_t)n * sizeof *b);
        for (int32_t i = 0; i < n; i++) c->heap.starts[i] = b[i].data;
    }
    free(b);
    c->heap.n = n;
    c->heap.first = n ? (int64_t)(c->heap.b[0].data - arena.lo) : 0;
    return !c->failed;
}

/* arena_blocks: the sound arena (pcSoundArena) and its used blocks in their chain. */
static int arenaBlocks(Ctx* c) {
    const StateSchema* s = c->a.s;
    int32_t vi = stateSchemaVarNamed(s, "pcSoundArena");
    if (vi == ST_NONE) return 1;
    const StVar* v = stateSchemaVarAt(s, vi);
    int32_t sec = sectionHolding(s, v->addr);
    int32_t ci = sec >= 0 ? sectionChunk(c, sec) : -1;
    if (ci < 0) return 1;
    const Mem* mem = &c->acMem[ci];
    int64_t base = (int64_t)(v->addr - stateSchemaSectionAt(s, sec).lo);
    uint64_t a0 = v->addr;
    int32_t cap = 64, n = 0;
    ABlock* b = malloc((size_t)cap * sizeof *b);
    if (!b) {
        fail(c, "out of memory");
        return 0;
    }
    int64_t o = 0;
    for (int it = 0; it < (1 << 12); it++) {
        int64_t h = base + o;
        uint64_t end = memU(mem, h + 8, 8), nxt = memU(mem, h + 16, 8);
        uint64_t data = a0 + (uint64_t)o + ARENA_HEADER;
        int64_t size = end ? diffU(end, data, 0) : 0;
        if (!(0 <= o + ARENA_HEADER + size && o + ARENA_HEADER + size <= v->size)) size = 0;
        if (n == cap) {
            ABlock* more = realloc(b, (size_t)cap * 2 * sizeof *b);
            if (!more) {
                free(b);
                fail(c, "out of memory");
                return 0;
            }
            b = more;
            cap *= 2;
        }
        ABlock* k = &b[n++];
        memset(k, 0, sizeof *k);
        k->hdr = o;
        k->data = data;
        k->size = size;
        k->user = 1;
        k->flags = (uint32_t)memU(mem, h, 2);
        k->m2 = (uint32_t)memU(mem, h + 2, 2);
        k->m4 = memInt(mem, h + 4, 4, 1);
        k->pctype = (uint32_t)memU(mem, h + 24, 4);
        if (!nxt || nxt < a0 || nxt - a0 > (uint64_t)(v->size - ARENA_HEADER) || v->size < ARENA_HEADER) break;
        o = (int64_t)(nxt - a0);
    }
    c->arena.b = poolAlloc(c, (size_t)(n + 1) * sizeof *b);
    c->arena.lo = poolAlloc(c, (size_t)(n + 1) * sizeof *c->arena.lo);
    if (c->arena.b && c->arena.lo) {
        memcpy(c->arena.b, b, (size_t)n * sizeof *b);
        for (int32_t i = 0; i < n; i++) c->arena.lo[i] = b[i].data;
    }
    free(b);
    c->arena.n = n;
    c->arena.present = 1;
    c->arena.var = vi;
    c->arena.mem = *mem;
    c->arena.off = base;
    c->arena.base = a0;
    c->arena.size = v->size;
    return !c->failed;
}

/* A range's or a block's spec (spec_of): None, False (a type the schema does not know), or a heap type. */
enum { RS_NONE, RS_FALSE, RS_PRIMS, RS_PTRS, RS_OTAGS, RS_TYPE };

typedef struct {
    int kind;
    const StHeapType* ht;
} RSpec;

static RSpec specOf(const StateSchema* s, uint32_t hash) {
    RSpec r = {RS_FALSE, NULL};
    const StHeapType* h = stateSchemaHeapType(s, hash);
    if (!h) return r;
    r.ht = h;
    r.kind = h->spec == ST_SPEC_TYPE ? RS_TYPE : h->spec == ST_SPEC_PRIMS ? RS_PRIMS
           : h->spec == ST_SPEC_PTRS ? RS_PTRS : h->spec == ST_SPEC_OTAGS ? RS_OTAGS : RS_NONE;
    return r;
}

typedef struct {
    int64_t ro;
    RSpec spec;
    int64_t n;
} Range;

/* sorted(ranges)'s order: by offset, then spec, then count; specs of different kinds (dicts, False) are
 * not ordered in Python: -2. */
static int rangeCmp(const Range* x, const Range* y) {
    if (x->ro != y->ro) return x->ro < y->ro ? -1 : 1;
    int same = x->spec.kind == y->spec.kind &&
               (x->spec.kind != RS_TYPE || (x->spec.ht->type == y->spec.ht->type && x->spec.ht->one == y->spec.ht->one));
    if (!same) {
        static const char* const strs[] = {NULL, NULL, "prims", "ptrs", "otags", NULL};
        const char* a = strs[x->spec.kind];
        const char* b = strs[y->spec.kind];
        if (!a || !b) return -2;
        return strcmp(a, b) < 0 ? -1 : 1;
    }
    return x->n < y->n ? -1 : x->n > y->n;
}

/* layout_part: one part in the writer, spec's elements from off, as many as fit in room (or count). 0
 * when nothing fits. */
static int layoutPart(Ctx* c, const Mem* mem, int64_t off, int64_t room, RSpec spec, int hasCount, int64_t count,
                      APart* out) {
    memset(out, 0, sizeof *out);
    out->tid = ST_NONE;
    if (spec.kind == RS_PTRS || spec.kind == RS_OTAGS) {
        int64_t n = floorDiv(room, 8);
        if (hasCount && count < n) n = count;
        if (!n) return 0;
        if (n > MAX_ELEMENTS) {
            fail(c, "a heap part of %lld pointers (a damaged state)", (long long)n);
            return 0;
        }
        out->spec = spec.kind == RS_PTRS ? PS_PTRS : PS_OTAGS;
        out->size = 8 * n;
        return 1;
    }
    if (spec.kind == RS_PRIMS) {
        if (!room) return 0;
        out->spec = PS_PRIMS;
        out->size = room;
        return 1;
    }
    if (spec.kind != RS_TYPE || spec.ht->type == ST_NONE) return 0;
    int32_t tid = spec.ht->type;
    int64_t es0 = tsize(&c->a, tid);
    if (spec.ht->one && !hasCount) { /* PC_HEAP_ONE: one T, then the caller's bytes */
        hasCount = 1;
        count = 1;
    }
    const Rules* r = rulesOf(&c->a, tid);
    const StType* t = ty(&c->a, tid);
    int32_t cap = 16, n = 0;
    int64_t* eo = malloc((size_t)cap * sizeof *eo);
    int64_t* cnt = malloc((size_t)cap * sizeof *cnt);
    if (!eo || !cnt) {
        free(eo);
        free(cnt);
        fail(c, "out of memory");
        return 0;
    }
    int64_t o = 0;
    while (o < room && (!hasCount || n < count)) {
        /* element_size: the type's size, or with a counted array its count of elements */
        int64_t es, en = -1;
        if (!r->cr) {
            es = es0;
        } else {
            const StField* a = &t->fields[r->ca];
            const StType* at = ty(&c->a, a->type);
            if (!at || at->kind != ST_ARR) {
                fail(c, "a counted array that is not an array");
                break;
            }
            int64_t aes = tsize(&c->a, at->of);
            if (r->cc < 0) {
                en = aes ? floorDiv(room - o - a->off, aes) : 0;
            } else {
                const StField* cf = &t->fields[r->cc];
                int64_t cs = tsize(&c->a, cf->type);
                PyInt k = memInt(mem, off + o + cf->off, cs > 8 ? 8 : cs, 1);
                en = (int64_t)k.v + r->cr->extra;
            }
            if (en < 0) en = 0;
            es = a->off + en * aes;
        }
        if (es <= 0) break;
        if (o + es > room) {
            if (!n && en < 0 && es0 > room) { /* a T's first fields only (allocateHeap(offsetof(T, m))) */
                out->spec = PS_TYPE;
                out->tid = tid;
                out->size = room;
                out->partial = 1;
                out->neo = 1;
                out->eo = poolAlloc(c, sizeof *out->eo);
                out->cnt = poolAlloc(c, sizeof *out->cnt);
                if (out->cnt) out->cnt[0] = -1;
                free(eo);
                free(cnt);
                return !c->failed;
            }
            break;
        }
        if (n >= MAX_ELEMENTS) {
            fail(c, "a heap part of more than %lld elements (a damaged state)", (long long)MAX_ELEMENTS);
            break;
        }
        if (n == cap) {
            int64_t* m1 = realloc(eo, (size_t)cap * 2 * sizeof *eo);
            if (m1) eo = m1;
            int64_t* m2 = m1 ? realloc(cnt, (size_t)cap * 2 * sizeof *cnt) : NULL;
            if (m2) cnt = m2;
            if (!m1 || !m2) {
                free(eo);
                free(cnt);
                fail(c, "out of memory");
                return 0;
            }
            cap *= 2;
        }
        eo[n] = o;
        cnt[n] = en;
        n++;
        o += es;
        if (en >= 0 && !hasCount) break; /* a counted struct: one, then what follows is another part */
    }
    if (!n || c->failed) {
        free(eo);
        free(cnt);
        return 0;
    }
    out->spec = PS_TYPE;
    out->tid = tid;
    out->one = spec.ht->one;
    out->size = o;
    out->neo = n;
    out->eo = poolAlloc(c, (size_t)n * sizeof *eo);
    out->cnt = poolAlloc(c, (size_t)n * sizeof *cnt);
    if (out->eo && out->cnt) {
        memcpy(out->eo, eo, (size_t)n * sizeof *eo);
        memcpy(out->cnt, cnt, (size_t)n * sizeof *cnt);
    }
    free(eo);
    free(cnt);
    return !c->failed;
}

typedef struct {
    APart* p;
    int32_t n, cap;
} Parts;

static void partsAdd(Ctx* c, Parts* ps, const APart* p) {
    if (ps->n == ps->cap) {
        int32_t cap = ps->cap ? ps->cap * 2 : 8;
        APart* more = realloc(ps->p, (size_t)cap * sizeof *more);
        if (!more) {
            fail(c, "out of memory");
            return;
        }
        ps->p = more;
        ps->cap = cap;
    }
    ps->p[ps->n++] = *p;
}

static void layoutGap(Ctx* c, Parts* ps, RSpec spec, const Mem* mem, int64_t off, int64_t a, int64_t b) {
    APart p;
    if (spec.kind == RS_PRIMS && layoutPart(c, mem, off + a, b - a, spec, 0, 0, &p)) {
        p.off = a;
        partsAdd(c, ps, &p);
        return;
    }
    memset(&p, 0, sizeof p);
    p.tid = ST_NONE;
    p.spec = PS_BYTES;
    p.off = a;
    p.size = b - a;
    partsAdd(c, ps, &p);
}

/* block_layout: a block's parts in the writer, in order: its own type from the start, the ranges
 * recorded in it, bytes between and after (in a block of primitives: primitives). */
static void blockLayout(Ctx* c, ABlock* blk, const Mem* mem, int64_t off, int64_t size, RSpec spec, Range* r,
                        int32_t nr) {
    for (int32_t i = 1; i < nr; i++) { /* sorted(ranges): stable */
        Range x = r[i];
        int32_t j = i;
        while (j > 0) {
            int k = rangeCmp(&x, &r[j - 1]);
            if (k == -2) {
                fail(c, "two typed ranges of different types at one offset (the reference cannot sort them)");
                return;
            }
            if (k >= 0) break;
            r[j] = r[j - 1];
            j--;
        }
        r[j] = x;
    }
    Parts ps = {0};
    int64_t cur = 0, first = nr ? r[0].ro : size;
    APart p;
    if (spec.kind != RS_NONE && layoutPart(c, mem, off, first, spec, 0, 0, &p)) {
        p.off = 0;
        partsAdd(c, &ps, &p);
        cur = p.size;
    }
    for (int32_t i = 0; i < nr && !c->failed; i++) {
        if (r[i].ro < cur || r[i].spec.kind == RS_FALSE || r[i].spec.kind == RS_NONE) continue;
        int64_t nxt = size;
        for (int32_t j = 0; j < nr; j++) {
            if (r[j].ro > r[i].ro) {
                nxt = r[j].ro;
                break;
            }
        }
        if (!layoutPart(c, mem, off + r[i].ro, nxt - r[i].ro, r[i].spec, 1, r[i].n, &p)) continue;
        if (r[i].ro > cur) layoutGap(c, &ps, spec, mem, off, cur, r[i].ro);
        p.off = r[i].ro;
        partsAdd(c, &ps, &p);
        cur = r[i].ro + p.size;
    }
    if (cur < size) layoutGap(c, &ps, spec, mem, off, cur, size);
    blk->nparts = ps.n;
    blk->parts = poolAlloc(c, (size_t)(ps.n + 1) * sizeof *ps.p);
    blk->partLo = poolAlloc(c, (size_t)(ps.n + 1) * sizeof *blk->partLo);
    if (blk->parts && blk->partLo) {
        memcpy(blk->parts, ps.p, (size_t)ps.n * sizeof *ps.p);
        for (int32_t i = 0; i < ps.n; i++) blk->partLo[i] = ps.p[i].off;
    }
    free(ps.p);
}

/* What encode_part records of a part besides its values, and where it reads them: mem from moff. */
static void bindPart(Ctx* c, APart* p, const Mem* mem, int64_t moff) {
    p->mem = mem;
    p->moff = moff;
    if (p->spec != PS_TYPE) return;
    p->name = stableName(&c->a, p->tid);
    for (int32_t j = 0; j < p->neo; j++) {
        if (p->cnt[j] >= 0) p->counted = 1;
    }
    if (!p->partial) return;
    /* the first fields only, up to the first that does not fit; in another build, where it starts there */
    const StType* t = ty(&c->a, p->tid);
    p->upto = NULL;
    for (int32_t i = 0; i < t->fieldCount; i++) {
        const StField* f = &t->fields[i];
        if (!f->bits && f->off + tsize(&c->a, f->type) > p->size) {
            if (f->off == p->size) p->upto = arrayKey(c, &c->a, p->tid, i);
            break;
        }
    }
    /* encode_element: tmp = bytearray(sizeof T); tmp[:size] = mem[off:off + size] */
    int64_t es = tsize(&c->a, p->tid);
    const uint8_t* src;
    int64_t len = memSlice(mem, moff, moff + p->size, &src);
    int64_t keep = p->size < es ? p->size : es;
    int64_t n = len + (es - keep);
    uint8_t* tmp = poolAlloc(c, (size_t)(n > 0 ? n : 1));
    if (!tmp) return;
    memcpy(tmp, src, (size_t)len);
    p->tmp.p = tmp;
    p->tmp.n = n;
}

/* state_ranges: the typed ranges the game recorded (PC_STATE_AT, pcStateRanges): (at, type hash,
 * count), and whether some were not recorded. */
typedef struct {
    uint64_t at;
    uint32_t h;
    int64_t n;
} Recorded;

static int32_t lastNamed(const StType* t, const char* name) {
    for (int32_t i = t->fieldCount - 1; i >= 0; i--) {
        if (strcmp(t->fields[i].name, name) == 0) return i;
    }
    return -1;
}

static Recorded* stateRanges(Ctx* c, int32_t* count, uint32_t* over) {
    const StateSchema* s = c->a.s;
    *count = 0;
    *over = 0;
    int32_t vi = stateSchemaVarNamed(s, "pcStateRanges");
    if (vi == ST_NONE) return NULL;
    const StVar* v = stateSchemaVarAt(s, vi);
    int32_t sec = sectionHolding(s, v->addr);
    int32_t ci = sec >= 0 ? sectionChunk(c, sec) : -1;
    if (ci < 0) return NULL;
    const Mem* data = &c->acMem[ci];
    int64_t base = (int64_t)(v->addr - stateSchemaSectionAt(s, sec).lo);
    const StType* t = ty(&c->a, v->type);
    int32_t fc = t ? lastNamed(t, "count") : -1, fo = t ? lastNamed(t, "overflow") : -1;
    int32_t fr = t ? lastNamed(t, "ranges") : -1;
    const StType* arr = fr >= 0 ? ty(&c->a, t->fields[fr].type) : NULL;
    const StType* et = arr ? ty(&c->a, arr->of) : NULL;
    int32_t eat = et ? lastNamed(et, "at") : -1, ety = et ? lastNamed(et, "type") : -1;
    int32_t ect = et ? lastNamed(et, "count") : -1;
    if (fc < 0 || fo < 0 || !et || eat < 0 || ety < 0 || ect < 0) {
        fail(c, "pcStateRanges is not as statecanon reads it");
        return NULL;
    }
    int64_t n = (int64_t)memInt(data, base + t->fields[fc].off, 4, 1).v;
    *over = (uint32_t)memU(data, base + t->fields[fo].off, 4);
    if (n > arr->n) n = arr->n;
    if (n <= 0) return NULL;
    Recorded* out = malloc((size_t)n * sizeof *out);
    if (!out) {
        fail(c, "out of memory");
        return NULL;
    }
    for (int64_t i = 0; i < n; i++) {
        int64_t o = base + t->fields[fr].off + i * et->size;
        out[i].at = memU(data, o + et->fields[eat].off, 8);
        out[i].h = (uint32_t)memU(data, o + et->fields[ety].off, 4);
        out[i].n = (int64_t)memInt(data, o + et->fields[ect].off, 4, 1).v;
    }
    *count = (int32_t)n;
    return out;
}

/* encode_heap: the heap's blocks with their parts (the type the allocation named, the ranges recorded
 * in them, bytes), and the sound arena's. */
static int encodeHeap(Ctx* c) {
    if (!heapBlocks(c)) return 0;
    int32_t nr;
    uint32_t over;
    Recorded* ranges = stateRanges(c, &nr, &over);
    if (c->failed) return 0;
    if (over) warn(c, &c->enc, "typed ranges not recorded (pcStateRanges full): such a state is not canonical");
    if (!arenaBlocks(c)) {
        free(ranges);
        return 0;
    }
    /* each range to its block, in the recorded order */
    int32_t* where = malloc((size_t)(nr + 1) * sizeof *where); /* block, or -2 - arena block, or -1 */
    if (!where) {
        free(ranges);
        fail(c, "out of memory");
        return 0;
    }
    for (int32_t k = 0; k < nr; k++) {
        uint64_t at = ranges[k].at;
        int32_t i = bisectU(c->heap.starts, c->heap.n, at) - 1;
        where[k] = -1;
        if (i >= 0 && c->heap.b[i].user && !c->heap.b[i].end && below(at, c->heap.b[i].data, c->heap.b[i].size)) {
            where[k] = i;
            continue;
        }
        if (c->arena.present) {
            int32_t j = bisectU(c->arena.lo, c->arena.n, at) - 1;
            if (j >= 0 && below(at, c->arena.b[j].data, c->arena.b[j].size)) {
                where[k] = -2 - j;
                continue;
            }
        }
        warn(c, &c->enc, "typed range outside the live heap blocks (left to the bytes there)");
    }
    Range* mine = malloc((size_t)(nr + 1) * sizeof *mine);
    if (!mine) {
        free(ranges);
        free(where);
        fail(c, "out of memory");
        return 0;
    }
    const Mem* heap = &c->heap.mem;
    for (int32_t i = 0; i < c->heap.n && !c->failed; i++) {
        ABlock* b = &c->heap.b[i];
        if (!b->user || b->end) continue;
        int32_t m = 0;
        for (int32_t k = 0; k < nr; k++) {
            if (where[k] != i) continue;
            mine[m].ro = (int64_t)(ranges[k].at - b->data);
            mine[m].spec = specOf(c->a.s, ranges[k].h);
            mine[m].n = ranges[k].n;
            m++;
        }
        RSpec s = {RS_FALSE, NULL};
        if (b->pctype) s = specOf(c->a.s, b->pctype);
        if (s.kind == RS_FALSE) {
            if (b->pctype) warn(c, &c->enc, "heap block of a type the schema does not name");
            s.kind = RS_NONE;
        }
        int64_t off = (int64_t)(b->data - c->heap.lo);
        blockLayout(c, b, heap, off, b->size, s, mine, m);
        for (int32_t k = 0; k < b->nparts; k++) bindPart(c, &b->parts[k], heap, off + b->parts[k].off);
    }
    for (int32_t j = 0; c->arena.present && j < c->arena.n && !c->failed; j++) {
        ABlock* b = &c->arena.b[j];
        int32_t m = 0;
        for (int32_t k = 0; k < nr; k++) {
            if (where[k] != -2 - j) continue;
            mine[m].ro = (int64_t)(ranges[k].at - b->data);
            mine[m].spec = specOf(c->a.s, ranges[k].h);
            mine[m].n = ranges[k].n;
            m++;
        }
        RSpec s = {RS_NONE, NULL};
        if (b->pctype) s = specOf(c->a.s, b->pctype);
        if (s.kind == RS_FALSE) {
            warn(c, &c->enc, "sound arena block of a type the schema does not name (kept as bytes)");
            s.kind = RS_NONE;
        }
        blockLayout(c, b, &c->arena.mem, c->arena.off + b->hdr + ARENA_HEADER, b->size, s, mine, m);
    }
    free(ranges);
    free(where);
    free(mine);
    return !c->failed;
}

/* One element of a typed part (encode_element): the T, and its counted array listed whole. ov holds
 * the counted array's value. */
static void elementVal(Ctx* c, const APart* p, int32_t j, AVal* v, AVal* ov) {
    if (p->partial) { /* the first fields of a T: the rest of T read as zero */
        makeVal(c, p->tid, &p->tmp, 0, 0, v);
        return;
    }
    makeVal(c, p->tid, p->mem, p->moff + p->eo[j], 0, v);
    if (p->cnt[j] < 0) return;
    const Rules* r = rulesOf(&c->a, p->tid);
    const StType* t = ty(&c->a, p->tid);
    const StField* a = &t->fields[r->ca];
    const StType* at = ty(&c->a, a->type);
    const StType* et = at ? ty(&c->a, at->of) : NULL;
    if (v->kind != AV_STRUCT || !et) {
        fail(c, "a counted array the reference cannot list");
        return;
    }
    memset(ov, 0, sizeof *ov);
    ov->m = p->mem;
    ov->off = p->moff + p->eo[j] + a->off;
    if (et->kind == ST_INT) {
        ov->kind = AV_HEX;
        ov->n = p->cnt[j] * et->size;
        ov->w = et->size;
    } else {
        ov->kind = AV_LIST;
        ov->tid = at->of;
        ov->n = p->cnt[j];
        ov->es = et->size;
    }
    v->ov[0].key = arrayKey(c, &c->a, p->tid, r->ca);
    v->ov[0].val = ov;
    v->nov = 1;
}

/* encode_part's evaluation of a part, for its warnings. */
static void walkPart(Ctx* c, const APart* p) {
    AVal v, ov;
    if (p->spec == PS_OTAGS) {
        for (int64_t k = 0; k < p->size; k += 8) makeVal(c, c->a.otag, p->mem, p->moff + k, 1, &v);
        return;
    }
    if (p->spec != PS_TYPE) return; /* bytes; pointers and primitives' links encode quietly */
    if (!p->name) warn(c, &c->enc, "heap part of an unnamed type (same build only)");
    for (int32_t j = 0; j < p->neo && !c->failed; j++) {
        if (p->partial) {
            makeVal(c, p->tid, &p->tmp, 0, 1, &v);
            walk(c, &v);
            continue;
        }
        makeVal(c, p->tid, p->mem, p->moff + p->eo[j], 1, &v);
        walk(c, &v);
        if (p->cnt[j] >= 0) {
            elementVal(c, p, j, &v, &ov);
            if (ov.kind == AV_LIST) {
                for (int64_t k = 0; k < ov.n && !c->failed; k++) {
                    AVal e;
                    makeVal(c, ov.tid, ov.m, ov.off + k * ov.es, 1, &e);
                    walk(c, &e);
                }
            }
        }
    }
}

/* ---- the reader's layout of the heap and the sound arena ---------------------------------------- */

/* part_tid: a struct part's type in the reader, by its name; an unnamed one by the writer's tid. */
static int32_t partTid(Ctx* c, const APart* p) {
    if (p->name) {
        CRules* R = rulesOn(c);
        const char* name = R ? nameTarget(R, &R->typeWas, p->name) : p->name;
        int32_t t = name ? stateSchemaTypeNamed(c->b.s, name) : ST_NONE;
        if (t == ST_NONE) warn(c, &c->dec, "heap part type not in this build (left zero)");
        else if (strcmp(name, p->name) != 0) warn(c, &c->notes, "renamed by a state rule");
        return t;
    }
    return stateSchemaType(c->b.s, ty(&c->a, p->tid)->tid);
}

/* How many elements a counted element's array holds in the canonical dict (part_size's av). */
static int64_t countedLength(Ctx* c, const AVal* av, int found) {
    if (!found || av->kind == AV_NONE) return 0;
    if (av->kind == AV_LIST) return av->n;
    if (av->kind == AV_INT && av->num.v == 0 && !av->num.neg) return 0; /* len(0 or []) */
    if (av->kind == AV_HEX && av->w > 0) {
        const uint8_t* p;
        return floorDiv(hexBytes(av, &p), av->w);
    }
    (void)c;
    return 0; /* another shape: none (putElement warns) */
}

/* counted_source: the writer's key of the reader's counted array of tid (a renamed member's by the rules), NULL
 * when the writer has it not. */
static const char* countedSource(Ctx* c, int32_t tid, int32_t wt, const Rules* r) {
    const char* key = arrayKey(c, &c->b, tid, r->ca);
    CRules* R = rulesOn(c);
    return R && key ? fieldSource(R, tid, wt, key) : key;
}

/* part_size: a part's size in the reader, and its elements' offsets. */
static int64_t partSize(Ctx* c, const APart* p, int32_t tid, LPart* lay) {
    lay->eo = NULL;
    lay->neo = 0;
    if (p->spec == PS_BYTES) {
        const uint8_t* x;
        return memSlice(p->mem, p->moff, p->moff + p->size, &x);
    }
    if (p->spec == PS_PTRS || p->spec == PS_OTAGS) return p->size > 0 ? 8 * ((p->size + 7) / 8) : 0;
    if (p->spec == PS_PRIMS) return p->size;
    if (tid == ST_NONE) return p->partial ? p->size : 0;
    if (p->partial) {
        lay->eo = poolAlloc(c, sizeof *lay->eo);
        lay->neo = 1;
        int64_t o = 0;
        if (!p->upto) return p->size;
        Step s = {S_KEY, 0, p->upto};
        return offsetOf(c, tid, &s, 1, &o, p->tid) ? o : p->size;
    }
    lay->eo = poolAlloc(c, (size_t)(p->neo + 1) * sizeof *lay->eo);
    if (!lay->eo) return 0;
    lay->neo = p->neo;
    const Rules* r = p->counted ? rulesOf(&c->b, tid) : NULL;
    const StType* t = ty(&c->b, tid);
    int64_t o = 0;
    for (int32_t j = 0; j < p->neo && !c->failed; j++) {
        lay->eo[j] = o;
        if (r && r->cr) {
            const StField* a = &t->fields[r->ca];
            const StType* at = ty(&c->b, a->type);
            if (!at || at->kind != ST_ARR) {
                fail(c, "a counted array that is not an array in the reader");
                return 0;
            }
            AVal v, ov, av;
            elementVal(c, p, j, &v, &ov);
            const char* key = countedSource(c, tid, p->tid, r);
            int found = key && structGet(c, &v, key, r->ca, &av);
            o += a->off + countedLength(c, &av, found) * tsize(&c->b, at->of);
        } else {
            o += tsize(&c->b, tid);
        }
    }
    return o;
}

/* layout_parts: a block's parts one after another in the reader, and their total size. */
static LPart* layoutParts(Ctx* c, const ABlock* rec, int64_t* total) {
    LPart* out = poolAlloc(c, (size_t)(rec->nparts + 1) * sizeof *out);
    int64_t o = 0;
    for (int32_t i = 0; out && i < rec->nparts && !c->failed; i++) {
        const APart* p = &rec->parts[i];
        int32_t tid = p->spec == PS_TYPE ? partTid(c, p) : ST_NONE;
        out[i].off = o;
        out[i].tid = tid;
        out[i].size = partSize(c, p, tid, &out[i]);
        o += out[i].size;
    }
    *total = o;
    return out;
}

/* decode_heap_layout: each block in order from the same start, live ones sized by the reader's types
 * (whole grains, as allocateHeap rounds), free ones as they were. */
static void decodeHeapLayout(Ctx* c) {
    StRange arena = stateSchemaRegion(c->b.s, "heapArena");
    if (!arena.name) {
        fail(c, "the reader's schema has no heapArena");
        return;
    }
    c->bheap.b = poolAlloc(c, (size_t)(c->heap.n + 1) * sizeof *c->bheap.b);
    if (!c->bheap.b) return;
    c->bheap.present = 1;
    uint64_t data = arena.lo + (uint64_t)c->heap.first;
    int32_t n = 0;
    for (int32_t i = 0; i < c->heap.n && !c->failed; i++) {
        const ABlock* rec = &c->heap.b[i];
        LBlock* l = &c->bheap.b[n++];
        l->data = data;
        l->rec = rec;
        if (rec->end) {
            l->end = 1;
            break;
        }
        if (!rec->user) {
            l->size = rec->size;
        } else {
            int64_t o;
            l->parts = layoutParts(c, rec, &o);
            l->nparts = rec->nparts;
            l->size = o != rec->size ? align16(o) : o;
        }
        data += (uint64_t)(l->size + HEAP_HEADER);
    }
    c->bheap.n = n;
}

/* decode_arena_layout: each block after the previous one's end, rounded to 16, and its gap. */
static void decodeArenaLayout(Ctx* c, const StVar* var) {
    c->barena.b = poolAlloc(c, (size_t)(c->arena.n + 1) * sizeof *c->barena.b);
    if (!c->barena.b) return;
    int64_t prevEnd = 0;
    for (int32_t j = 0; j < c->arena.n && !c->failed; j++) {
        const ABlock* rec = &c->arena.b[j];
        LBlock* l = &c->barena.b[j];
        l->hdr = j == 0 ? 0 : align16(prevEnd) + rec->gap;
        int64_t size;
        l->parts = layoutParts(c, rec, &size);
        l->nparts = rec->nparts;
        if (!rec->nparts) size = rec->size;
        l->size = size;
        l->data = var->addr + (uint64_t)l->hdr + ARENA_HEADER;
        l->rec = rec;
        prevEnd = l->hdr + ARENA_HEADER + size;
    }
    c->barena.n = c->arena.n;
    c->barena.base = var->addr;
    c->barena.present = 1;
}

/* ---- the reader's heap bytes (decode_heap, put_parts, put_element, put_prims, put_arena) --------- */

/* put_element: one struct element; a counted one with its first array element in the T, then the
 * array's elements from the array's place. */
static void putElement(Ctx* c, int32_t tid, const AVal* v, Buf* buf, int64_t off, int counted, int32_t wt) {
    if (!counted) {
        put(c, tid, v, buf, off, wt);
        return;
    }
    const Rules* r = rulesOf(&c->b, tid);
    if (!r->cr) {
        warn(c, &c->dec, "counted array not counted in this build (its elements left out)");
        put(c, tid, v, buf, off, wt);
        return;
    }
    const StType* t = ty(&c->b, tid);
    const StField* a = &t->fields[r->ca];
    const StType* at = ty(&c->b, a->type);
    if (!at || at->kind != ST_ARR) {
        fail(c, "a counted array that is not an array in the reader");
        return;
    }
    const char* key = countedSource(c, tid, wt, r);
    AVal av, firstv;
    int found = key && structGet(c, v, key, r->ca, &av);
    int64_t es = tsize(&c->b, at->of), base = off + a->off;
    AVal first = *v; /* dict(v), its array replaced by its first element */
    if (found && (av.kind == AV_HEX || av.kind == AV_SEL)) {
        firstv = av;
        firstv.kind = AV_HEX;
        firstv.n = av.n < es ? av.n : es;
        firstv.w = es;
    } else if (found && av.kind == AV_LIST) {
        firstv = av;
        firstv.n = av.n < 1 ? av.n : 1;
    } else {
        warn(c, &c->dec, "counted array not in the state (left zero)");
        put(c, tid, v, buf, off, wt);
        return;
    }
    if (first.kind == AV_STRUCT) {
        first.ov[first.nov].key = key;
        first.ov[first.nov].val = &firstv;
        first.nov++;
    }
    put(c, tid, &first, buf, off, wt);
    if (av.kind == AV_LIST) {
        /* the writer's counted array's elements (counted_rule of the writer's type) */
        const StType* wtt = c->self ? NULL : ty(&c->a, wt);
        const Rules* wr = wtt && wtt->kind == ST_STRUCT ? rulesOf(&c->a, wt) : NULL;
        const StType* wat = wr && wr->cr ? ty(&c->a, wtt->fields[wr->ca].type) : NULL;
        int32_t wof = wat ? wat->of : ST_NONE;
        for (int64_t j = 0; j < av.n && !c->failed; j++) {
            AVal e;
            makeVal(c, av.tid, av.m, av.off + j * av.es, 0, &e);
            put(c, at->of, &e, buf, base + j * es, wof);
        }
    } else {
        const uint8_t* x;
        int64_t n = hexBytes(&av, &x);
        bufSet(c, buf, base, base + n, x, n);
    }
}

/* put_prims over encode_prims: each primitive's tag with its link moved, its words as they were, zero
 * runs, then the bytes that did not parse. */
static void putPrims(Ctx* c, const APart* p, Buf* buf, int64_t off) {
    const Mem* m = p->mem;
    int64_t size = p->size, oa = 0, ob = 0; /* the encoder's offset, and the decoder's */
    uint8_t b[8];
    while (oa + 8 <= size && !c->failed) {
        uint64_t tag = memU(m, p->moff + oa, 8);
        if (tag == 0) { /* an unused, zeroed stretch */
            int64_t j = oa;
            for (;;) {
                if (j + 8 > size) break;
                if (p->moff + j >= m->n) { /* past the bytes every word reads as zero: to the last that fits */
                    j += floorDiv(size - j, 8) * 8;
                    break;
                }
                const uint8_t* q;
                int64_t n = memSlice(m, p->moff + j, p->moff + j + 8, &q);
                int any = 0;
                for (int64_t i = 0; i < n; i++) any |= q[i];
                if (any) break;
                j += 8;
            }
            bufSet(c, buf, off + ob, off + ob + (j - oa), NULL, j - oa);
            ob += j - oa;
            oa = j;
            continue;
        }
        uint64_t n = tag >> 56, link = tag & PRIM_LINK;
        int64_t step = (int64_t)((8 + 4 * n + 7) & ~UINT64_C(7));
        if (n > 16 || oa + step > size) break;
        uint64_t moved = link == PRIM_LINK ? PRIM_LINK : movePointer(c, link) & PRIM_LINK;
        intOut(pyUint((n << 56) | moved), b, 8);
        bufSet(c, buf, off + ob, off + ob + 8, b, 8);
        const uint8_t* w;
        int64_t nw = memSlice(m, p->moff + oa + 8, p->moff + oa + step, &w);
        bufSet(c, buf, off + ob + 8, off + ob + 8 + nw, w, nw);
        ob += 8 + nw;
        oa += step;
    }
    const uint8_t* x;
    int64_t nx = memSlice(m, p->moff + oa, p->moff + size, &x);
    int64_t keep = size - ob > 0 ? size - ob : 0;
    bufSet(c, buf, off + ob, off + ob + nx, x, nx < keep ? nx : keep);
}

/* put_parts: a block's parts at base, where the reader lays them out. */
static void putParts(Ctx* c, const ABlock* rec, const LPart* lay, Buf* buf, int64_t base) {
    uint8_t b[8];
    for (int32_t i = 0; i < rec->nparts && !c->failed; i++) {
        const APart* p = &rec->parts[i];
        const LPart* l = &lay[i];
        int64_t off = base + l->off;
        if (p->spec == PS_BYTES) {
            const uint8_t* x;
            int64_t n = memSlice(p->mem, p->moff, p->moff + p->size, &x);
            bufSet(c, buf, off, off + n, x, n);
        } else if (p->spec == PS_PTRS) {
            for (int64_t k = 0; k < p->size && !c->failed; k += 8) {
                intOut(pyUint(movePointer(c, memU(p->mem, p->moff + k, 8))), b, 8);
                bufSet(c, buf, off + k, off + k + 8, b, 8);
            }
        } else if (p->spec == PS_OTAGS) {
            for (int64_t k = 0; k < p->size && !c->failed; k += 8) {
                AVal v;
                makeVal(c, c->a.otag, p->mem, p->moff + k, 0, &v);
                put(c, c->b.otag, &v, buf, off + k, c->a.otag);
            }
        } else if (p->spec == PS_PRIMS) {
            putPrims(c, p, buf, off);
        } else if (l->tid != ST_NONE) {
            if (p->partial) {
                Buf tmp = {0};
                AVal v, ov;
                if (!bufZero(c, &tmp, tsize(&c->b, l->tid))) return;
                elementVal(c, p, 0, &v, &ov);
                put(c, l->tid, &v, &tmp, 0, p->tid);
                int64_t a = 0, e = l->size;
                pySlice(tmp.n, &a, &e);
                bufSet(c, buf, off, off + l->size, tmp.p + a, e - a);
                free(tmp.p);
                continue;
            }
            int32_t n = l->neo < p->neo ? l->neo : p->neo;
            for (int32_t j = 0; j < n && !c->failed; j++) {
                AVal v, ov;
                elementVal(c, p, j, &v, &ov);
                putElement(c, l->tid, &v, buf, off + l->eo[j], p->counted, p->tid);
            }
        }
    }
}

static void decodeHeap(Ctx* c, Buf* buf, int64_t size) {
    uint64_t lo = stateSchemaRegion(c->b.s, "heapArena").lo;
    if (!bufZero(c, buf, size)) return;
    uint8_t b[8];
    for (int32_t k = 0; k < c->bheap.n && !c->failed; k++) {
        const LBlock* l = &c->bheap.b[k];
        int64_t hdr = (int64_t)(l->data - HEAP_HEADER - lo);
        if (hdr < 0 || hdr + HEAP_HEADER > size) {
            warn(c, &c->dec, "heap block outside the arena");
            continue;
        }
        const ABlock* rec = l->rec;
        if (l->end) {
            intOut(pyUint(movePointer(c, rec->next)), b, 8);
            bufSet(c, buf, hdr, hdr + 8, b, 8);
            intOut(pyUint(rec->bits & ~UINT32_C(0x1FFFFF)), b, 4);
            bufSet(c, buf, hdr + 8, hdr + 12, b, 4);
            continue;
        }
        uint64_t nxt = k + 1 < c->bheap.n ? c->bheap.b[k + 1].data : l->data + (uint64_t)l->size + HEAP_HEADER;
        intOut(pyUint(nxt), b, 8);
        bufSet(c, buf, hdr, hdr + 8, b, 8);
        if (!rec->user) {
            intOut(pyUint(((uint64_t)rec->pinned << 25) | ((uint64_t)rec->type << 26)), b, 4);
            bufSet(c, buf, hdr + 8, hdr + 12, b, 4);
            continue;
        }
        /* the caller field (a code address of the saving build) is left zero */
        intOut(pyUint(((uint64_t)rec->user << 21) | ((uint64_t)rec->pinned << 25) | ((uint64_t)rec->type << 26)), b, 4);
        bufSet(c, buf, hdr + 8, hdr + 12, b, 4);
        intOut(pyUint(remapHash(c, rec->pctype)), b, 4);
        bufSet(c, buf, hdr + 12, hdr + 16, b, 4);
        putParts(c, rec, l->parts, buf, (int64_t)(l->data - lo));
    }
}

static void putArena(Ctx* c, Buf* buf, int64_t off, int64_t size) {
    bufSet(c, buf, off, off + size, NULL, size);
    uint8_t b[8];
    for (int32_t j = 0; j < c->barena.n && !c->failed; j++) {
        const LBlock* l = &c->barena.b[j];
        const ABlock* r = l->rec;
        int64_t h = off + l->hdr;
        if (l->hdr + ARENA_HEADER + l->size > size) {
            warn(c, &c->dec, "sound arena block past the arena's end (left out)");
            continue;
        }
        uint64_t nxt = j + 1 < c->barena.n ? c->barena.base + (uint64_t)c->barena.b[j + 1].hdr : 0;
        intOut(pyUint(r->flags), b, 2);
        intOut(pyUint(r->m2), b + 2, 2);
        intOut(r->m4, b + 4, 4);
        bufSet(c, buf, h, h + 8, b, 8);
        intOut(pyUint(l->data + (uint64_t)l->size), b, 8);
        bufSet(c, buf, h + 8, h + 16, b, 8);
        intOut(pyUint(nxt), b, 8);
        bufSet(c, buf, h + 16, h + 24, b, 8);
        intOut(pyUint(remapHash(c, r->pctype)), b, 4);
        bufSet(c, buf, h + 24, h + 28, b, 4);
        putParts(c, r, l->parts, buf, h + ARENA_HEADER);
    }
}

/* ---- records: the writer's variables and host parts (encode's loop over the chunks) ------------- */

/* mem.<module>.(data|bss): the module's name in out (0 if not such a tag). */
static int sectionTag(const uint8_t* tag, int n, char* module, size_t size, const char** kind) {
    if (n < 6 || memcmp(tag, "mem.", 4) != 0) return 0;
    int dot = -1;
    for (int i = n - 1; i >= 4; i--) {
        if (tag[i] == '.') {
            dot = i;
            break;
        }
    }
    if (dot <= 4) return 0;
    int kl = n - dot - 1;
    if (!((kl == 4 && memcmp(tag + dot + 1, "data", 4) == 0) || (kl == 3 && memcmp(tag + dot + 1, "bss", 3) == 0))) return 0;
    for (int i = 4; i < dot; i++) {
        uint8_t ch = tag[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) return 0;
    }
    if ((size_t)(dot - 4) >= size) return 0;
    memcpy(module, tag + 4, (size_t)(dot - 4));
    module[dot - 4] = 0;
    *kind = kl == 4 ? "data" : "bss";
    return 1;
}

typedef struct {
    int32_t var;
    int32_t tid;
    int64_t off;
} PartTarget;

/* part_target: a host part's tag ("gpu.gp.cmdAddr") to the platform variable and member it saves. */
static int partTarget(const Side* s, const uint8_t* tag, int n, PartTarget* out) {
    char buf[256];
    if (n <= 0 || (size_t)n >= sizeof buf) return 0;
    memcpy(buf, tag, (size_t)n);
    buf[n] = 0;
    char* dot = strchr(buf, '.');
    if (!dot) return 0;
    *dot = 0;
    const char* file = stateSchemaPartFile(s->s, buf);
    char* expr = dot + 1;
    if (!file || !file[0]) return 0;
    /* [A-Za-z_]\w*(\.[A-Za-z_]\w*)* */
    int start = 1;
    for (char* q = expr;; q++) {
        char ch = *q;
        int word = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || (ch >= '0' && ch <= '9');
        if (start) {
            if (!word || (ch >= '0' && ch <= '9')) return 0;
            start = 0;
        } else if (ch == '.') {
            start = 1;
        } else if (ch == 0) {
            break;
        } else if (!word) {
            return 0;
        }
    }
    char* name = expr;
    char* next = strchr(name, '.');
    if (next) *next++ = 0;
    int32_t vi = stateSchemaPlatformVar(s->s, file, name);
    if (vi == ST_NONE) vi = stateSchemaVarNamed(s->s, name);
    if (vi == ST_NONE) return 0;
    int32_t tid = stateSchemaVarAt(s->s, vi)->type;
    int64_t off = 0;
    while (next) {
        name = next;
        next = strchr(name, '.');
        if (next) *next++ = 0;
        const StType* t = ty(s, tid);
        if (!t || t->kind != ST_STRUCT) return 0;
        int32_t f = fieldNamed(t, name);
        if (f < 0) return 0;
        off += t->fields[f].off;
        tid = t->fields[f].type;
    }
    out->var = vi;
    out->tid = tid;
    out->off = off;
    return 1;
}

/* pointed_struct: the struct or union a data pointer type points at (the element of the array it points
 * at), or ST_NONE. */
static int32_t pointedStruct(const Side* s, int32_t tid) {
    const StType* t = ty(s, tid);
    if (!t || t->kind != ST_PTR || t->fn || t->to == ST_NONE) return ST_NONE;
    const StType* et = ty(s, t->to);
    return tsize(s, t->to) && (et->kind == ST_STRUCT || et->kind == ST_UNION) ? t->to : ST_NONE;
}

static ARec* addRec(Ctx* c) {
    if (c->nrec == c->recCap) {
        int32_t cap = c->recCap ? c->recCap * 2 : 1024;
        ARec* more = realloc(c->recs, (size_t)cap * sizeof *more);
        if (!more) {
            fail(c, "out of memory");
            return NULL;
        }
        c->recs = more;
        c->recCap = cap;
    }
    ARec* r = &c->recs[c->nrec++];
    memset(r, 0, sizeof *r);
    return r;
}

/* The bytes of a section's chunk outside every variable of its module: the raw entry encode keeps. */
static int gapBytes(Ctx* c, int32_t ci, const char* module, uint64_t lo, uint64_t hi, Buf* out) {
    const Mem* m = &c->acMem[ci];
    if (!bufGrow(c, out, m->n)) return 0;
    memcpy(out->p, m->p, (size_t)m->n);
    out->n = m->n;
    const StateSchema* s = c->a.s;
    for (int32_t i = 0; i < stateSchemaVarCount(s); i++) {
        const StVar* v = stateSchemaVarAt(s, i);
        if (!v->module || strcmp(v->module, module) != 0 || v->addr < lo || v->addr >= hi) continue;
        int64_t o = (int64_t)(v->addr - lo), e = o + v->size;
        if (o >= m->n) continue;
        if (e > m->n) e = m->n;
        if (e > o) memset(out->p + o, 0, (size_t)(e - o));
    }
    return 1;
}

static void addRaw(Ctx* c, int32_t ci, int gaps, const char* module, uint64_t lo, uint64_t hi) {
    ARaw* r = &c->raws[c->nraw++];
    r->chunk = ci;
    r->gaps = gaps;
    r->module = module;
    r->lo = lo;
    r->hi = hi;
}

/* encode's loop over the writer's chunks: its records and raw entries, each record walked for the
 * warnings in encode's order. */
static void encodeRecords(Ctx* c) {
    const StateSchema* s = c->a.s;
    char module[200];
    const char* kind;
    Buf gaps = {0};
    for (int32_t ci = 0; ci < c->nac && !c->failed; ci++) {
        const Chunk* k = &c->ac[ci];
        const Mem* data = &c->acMem[ci];
        if (c->heap.present && tagIs(k->tag, k->tagLen, "mem.heap")) continue;
        if (sectionTag(k->tag, k->tagLen, module, sizeof module, &kind)) {
            char name[300];
            snprintf(name, sizeof name, "%s.%s", module, kind);
            StRange sec = stateSchemaSection(s, name);
            if (!sec.name) {
                addRaw(c, ci, 0, NULL, 0, 0);
                continue;
            }
            const char* mod = poolString(c, module, strlen(module));
            for (int32_t i = 0; i < stateSchemaVarCount(s) && !c->failed; i++) {
                const StVar* v = stateSchemaVarAt(s, i);
                if (!v->module || strcmp(v->module, module) != 0 || v->addr < sec.lo || v->addr >= sec.hi) continue;
                if (v->isConst) continue; /* the build's own constant: not carried (gapBytes leaves it out too) */
                ARec* r = addRec(c);
                if (!r) return;
                r->var = i;
                r->chunk = ci;
                r->off = (int64_t)(v->addr - sec.lo);
                c->byVar[i] = c->nrec - 1;
                if (c->arena.present && i == c->arena.var) { /* the sound arena: its blocks in chain order */
                    r->kind = R_ARENA;
                    int64_t prevEnd = 0;
                    for (int32_t j = 0; j < c->arena.n && !c->failed; j++) {
                        ABlock* b = &c->arena.b[j];
                        b->gap = j == 0 ? 0 : b->hdr - align16(prevEnd);
                        if (b->gap < 0) warn(c, &c->enc, "sound arena blocks out of order (kept as they were)");
                        prevEnd = b->hdr + ARENA_HEADER + b->size;
                        for (int32_t q = 0; q < b->nparts; q++) {
                            bindPart(c, &b->parts[q], data, r->off + b->hdr + ARENA_HEADER + b->parts[q].off);
                            walkPart(c, &b->parts[q]);
                        }
                    }
                    continue;
                }
                r->kind = R_VAR;
                AVal val;
                makeVal(c, v->type, data, r->off, 1, &val);
                walk(c, &val);
            }
            if (!gapBytes(c, ci, module, sec.lo, sec.hi, &gaps)) break;
            int any = 0;
            for (int64_t i = 0; i < gaps.n && !any; i++) any = gaps.p[i] != 0;
            if (any) addRaw(c, ci, 1, mod, sec.lo, sec.hi);
            continue;
        }
        PartTarget pt;
        if (!(k->tagLen >= 4 && memcmp(k->tag, "mem.", 4) == 0) && partTarget(&c->a, k->tag, k->tagLen, &pt)) {
            if (tsize(&c->a, pt.tid) == k->size) {
                ARec* r = addRec(c);
                if (!r) return;
                r->kind = R_PART;
                r->var = pt.var;
                r->chunk = ci;
                r->tid = pt.tid;
                AVal val;
                makeVal(c, pt.tid, data, 0, 1, &val);
                walk(c, &val);
                continue;
            }
            /* part_array: the array a pointer variable points at (hw.queue), element by element */
            int32_t et = pointedStruct(&c->a, pt.tid);
            int64_t es = tsize(&c->a, et);
            if (et != ST_NONE && k->size % es == 0) {
                ARec* r = addRec(c);
                if (!r) return;
                r->kind = R_PARTARR;
                r->var = pt.var;
                r->chunk = ci;
                r->tid = et;
                r->es = es;
                r->n = k->size / es;
                for (int64_t q = 0; q < r->n && !c->failed; q++) {
                    AVal val;
                    makeVal(c, et, data, q * es, 1, &val);
                    walk(c, &val);
                }
                continue;
            }
        }
        addRaw(c, ci, 0, NULL, 0, 0);
    }
    free(gaps.p);
}

/* safe_point_of: the first record whose id ends ":pc:safePointId", its value if an int. */
static int64_t safePointOf(Ctx* c) {
    static const char suffix[] = ":pc:safePointId";
    size_t sl = sizeof suffix - 1;
    for (int32_t i = 0; i < c->nrec; i++) {
        const ARec* r = &c->recs[i];
        const char* id = stateSchemaVarAt(c->a.s, r->var)->id;
        size_t n = strlen(id);
        if (n < sl || strcmp(id + n - sl, suffix) != 0) continue;
        if (r->kind == R_VAR || r->kind == R_PART) {
            AVal v;
            makeVal(c, r->kind == R_VAR ? stateSchemaVarAt(c->a.s, r->var)->type : r->tid, &c->acMem[r->chunk],
                    r->off, 0, &v);
            if (v.kind == AV_INT) return v.num.v ? (int64_t)v.num.v : 0;
        }
        return 0;
    }
    return 0;
}

/* ---- decode: the reader's body, chunk by chunk in the template's order -------------------------- */

static int parseChunks(Ctx* c, const uint8_t* body, size_t size, Chunk** out, int32_t* count) {
    int32_t cap = 512, n = 0;
    Chunk* k = malloc((size_t)cap * sizeof *k);
    if (!k) {
        fail(c, "out of memory");
        return 0;
    }
    size_t ip = 0;
    while (ip < size) {
        size_t tl = body[ip];
        if (ip + 1 + tl + 4 > size) {
            free(k);
            fail(c, "a damaged state body");
            return 0;
        }
        uint32_t len = (uint32_t)bytesInt(body + ip + 1 + tl, 4, 0).v;
        if (n == cap) {
            Chunk* more = realloc(k, (size_t)cap * 2 * sizeof *k);
            if (!more) {
                free(k);
                fail(c, "out of memory");
                return 0;
            }
            k = more;
            cap *= 2;
        }
        k[n].tag = body + ip + 1;
        k[n].tagLen = (int)tl;
        k[n].off = (int64_t)(ip + 1 + tl + 4);
        k[n].size = len;
        if ((size_t)k[n].off + len > size) {
            free(k);
            fail(c, "a damaged state body");
            return 0;
        }
        n++;
        ip += 1 + tl + 4 + len;
    }
    *out = k;
    *count = n;
    return 1;
}

static void emit(Ctx* c, Buf* out, const uint8_t* tag, int tagLen, const uint8_t* data, int64_t n) {
    uint8_t head[4];
    int64_t at = out->n;
    if (!bufGrow(c, out, at + 1 + tagLen + 4 + n)) return;
    out->p[at] = (uint8_t)tagLen;
    memcpy(out->p + at + 1, tag, (size_t)tagLen);
    intOut(pyUint((uint64_t)n), head, 4);
    memcpy(out->p + at + 1 + tagLen, head, 4);
    if (n) memcpy(out->p + at + 1 + tagLen + 4, data, (size_t)n);
    out->n = at + 1 + tagLen + 4 + n;
}

static const ARaw* lastRaw(const Ctx* c, const uint8_t* tag, int n) {
    for (int32_t i = c->nraw - 1; i >= 0; i--) {
        const Chunk* k = &c->ac[c->raws[i].chunk];
        if (tagEq(k->tag, k->tagLen, tag, n)) return &c->raws[i];
    }
    return NULL;
}

static const ARec* lastPart(const Ctx* c, const uint8_t* tag, int n) {
    for (int32_t i = c->nrec - 1; i >= 0; i--) {
        const ARec* r = &c->recs[i];
        if (r->kind != R_PART && r->kind != R_PARTARR) continue;
        const Chunk* k = &c->ac[r->chunk];
        if (tagEq(k->tag, k->tagLen, tag, n)) return r;
    }
    return NULL;
}

/* buf[:len(b)] = b[:size]: a raw entry's bytes over the chunk's. */
static void applyRaw(Ctx* c, const ARaw* r, Buf* buf, Buf* scratch) {
    const Mem* m = &c->acMem[r->chunk];
    const uint8_t* p = m->p;
    int64_t n = m->n;
    if (r->gaps) {
        if (!gapBytes(c, r->chunk, r->module, r->lo, r->hi, scratch)) return;
        p = scratch->p;
        n = scratch->n;
    }
    int64_t k = n < buf->n ? n : buf->n;
    bufSet(c, buf, 0, n, p, k);
}

static const char* const STACK_CHUNKS[] = {"mem.stack.sp", "mem.stack.low", "mem.stack", "mem.guard"};

/* put_variable: a reader's variable from the writer's record (its identity wid there): the rules' values, a
 * game variable's PS1 bytes where its type changed, else the value as the reader's type. */
static void putVariable(Ctx* c, const StVar* v, const ARec* r, const char* wid, Buf* buf, int64_t off) {
    const StVar* wv = stateSchemaVarAt(c->a.s, r->var);
    int32_t wt = wv->type;
    AVal val;
    makeVal(c, wt, &c->acMem[r->chunk], r->off, 0, &val);
    CRules* R = rulesOn(c);
    if (strcmp(wid, v->id) != 0) warn(c, &c->notes, "renamed by a state rule");
    if (R) {
        const StRule* rs[MAX_RULES];
        int32_t nr = history(c, R, ST_NONE, NULL, R->forward ? v->id : wid, rs, MAX_RULES);
        if (nr) {
            AVal mapped;
            if (ruleValues(c, rs, nr, &val, wt, &mapped)) {
                varLost(c, v, buf, off, LOST_RESET);
                return;
            }
            val = mapped;
            for (int32_t k = 0; k < nr; k++) {
                if (rs[k]->kind == ST_RULE_VALUES) {
                    warn(c, &c->notes, "values mapped by a state rule");
                    break;
                }
            }
        }
    }
    if (wt != ST_NONE && v->module && v->retail != ST_NO_RETAIL && bytesDiffer(c, wt, v->type) &&
        plainType(c, &c->a, wt) && plainType(c, &c->b, v->type)) {
        carryBytes(c, v->type, &val, buf, off, wt); /* a game variable's PS1 bytes */
        return;
    }
    if (put(c, v->type, &val, buf, off, wt)) varLost(c, v, buf, off, LOST_TYPE);
}

static void decodeBody(Ctx* c, const uint8_t* tbody, size_t tsize_, int stackless, Buf* out) {
    Chunk* tc = NULL;
    int32_t ntc = 0;
    if (!parseChunks(c, tbody, tsize_, &tc, &ntc)) return;
    if (c->R.unknown) { /* rules a later build may know: ignored, never refused */
        warn(c, &c->dec, "state rule this build cannot read (ignored)");
        c->dec.rows[c->dec.n - 1].count += c->R.unknown - 1;
    }
    CRules* R = rulesOn(c);
    if (c->heap.present) decodeHeapLayout(c);
    for (int32_t i = 0; i < c->nrec && !c->failed; i++) {
        if (c->recs[i].kind != R_ARENA) continue;
        const char* id = stateSchemaVarAt(c->a.s, c->recs[i].var)->id;
        const char* rid = R ? nameTarget(R, &R->varWas, id) : id;
        int32_t vb = rid ? stateSchemaVarById(c->b.s, rid) : ST_NONE;
        if (vb != ST_NONE) decodeArenaLayout(c, stateSchemaVarAt(c->b.s, vb));
    }
    const StateSchema* s = c->b.s;
    Buf buf = {0}, scratch = {0};
    char module[200];
    const char* kind;
    for (int32_t ti = 0; ti < ntc && !c->failed; ti++) {
        const Chunk* k = &tc[ti];
        int stack = 0;
        for (int q = 0; q < 4; q++) stack |= tagIs(k->tag, k->tagLen, STACK_CHUNKS[q]);
        if (stackless && stack) { /* no frames: stack.sp 0 says so (savestate.c memoryRegions) */
            static const uint8_t zero[8] = {0};
            emit(c, out, k->tag, k->tagLen, zero, tagIs(k->tag, k->tagLen, "mem.stack") ? 0 : 8);
            continue;
        }
        if (c->heap.present && tagIs(k->tag, k->tagLen, "mem.heap")) {
            decodeHeap(c, &buf, k->size);
            emit(c, out, k->tag, k->tagLen, buf.p, buf.n);
            continue;
        }
        if (!bufZero(c, &buf, k->size)) break;
        StRange sec = {NULL, 0, 0};
        int section = sectionTag(k->tag, k->tagLen, module, sizeof module, &kind);
        if (section) {
            char name[300];
            snprintf(name, sizeof name, "%s.%s", module, kind);
            sec = stateSchemaSection(s, name);
        }
        char own[260];
        snprintf(own, sizeof own, "%.*s", k->tagLen, (const char*)k->tag);
        const char* ptag = R && !section ? nameSource(R, &R->tagWas, own) : own;
        const ARec* part;
        const ARaw* raw;
        if (sec.name) {
            if (strcmp(kind, "data") == 0) {
                /* its bytes outside every variable are this build's own, as the template has them: constants
                 * no variable names, which follow the code (a switch's jump table: the module link moves a game
                 * file's read-only data here, tools/pc_srcdata.py; MinGW keeps the tables there), not the
                 * writer's */
                bufSet(c, &buf, 0, k->size, tbody + k->off, k->size);
                for (int32_t i = 0; i < stateSchemaVarCount(s); i++) {
                    const StVar* v = stateSchemaVarAt(s, i);
                    if (!v->module || strcmp(v->module, module) != 0 || v->addr < sec.lo || v->addr >= sec.hi) continue;
                    int64_t o = (int64_t)(v->addr - sec.lo), e = o + v->size;
                    if (e > k->size) e = k->size;
                    if (e > o) bufSet(c, &buf, o, e, NULL, e - o);
                }
            } else if ((raw = lastRaw(c, k->tag, k->tagLen)) != NULL) {
                applyRaw(c, raw, &buf, &scratch);
            }
            for (int32_t i = 0; i < stateSchemaVarCount(s) && !c->failed; i++) {
                const StVar* v = stateSchemaVarAt(s, i);
                if (!v->module || strcmp(v->module, module) != 0 || v->addr < sec.lo || v->addr >= sec.hi) continue;
                if (v->isConst) { /* this build's own constant, as the template has it */
                    int64_t o = (int64_t)(v->addr - sec.lo), e = o + v->size;
                    if (e > k->size) e = k->size;
                    if (e > o) bufSet(c, &buf, o, e, tbody + k->off + o, e - o);
                    continue;
                }
                const char* wid = R ? nameSource(R, &R->varWas, v->id) : v->id;
                int32_t ai = wid ? stateSchemaVarById(c->a.s, wid) : ST_NONE;
                int32_t ri = ai != ST_NONE ? c->byVar[ai] : -1;
                int64_t at = (int64_t)(v->addr - sec.lo);
                if (ri < 0) {
                    varLost(c, v, &buf, at, LOST_MISSING);
                    continue;
                }
                const ARec* r = &c->recs[ri];
                if (r->kind == R_ARENA) {
                    putArena(c, &buf, at, v->size);
                    continue;
                }
                putVariable(c, v, r, wid, &buf, at);
            }
        } else if (ptag && (part = lastPart(c, (const uint8_t*)ptag, (int)strlen(ptag))) != NULL) {
            if (strcmp(ptag, own) != 0) warn(c, &c->notes, "renamed by a state rule");
            PartTarget pt;
            int has = partTarget(&c->b, k->tag, k->tagLen, &pt);
            const Mem* data = &c->acMem[part->chunk];
            if (has && part->kind == R_PARTARR) { /* (a pointed-at array: the state's count) */
                /* of the element this build's variable points at (statecanon: not the record's "arr", the
                 * writer's, named by its DWARF offset when it has no name, which only the same build has) */
                int32_t et = pointedStruct(&c->b, pt.tid);
                int64_t es = tsize(&c->b, et);
                if (et == ST_NONE) warn(c, &c->dec, "array part not a struct pointer in this build (left empty)");
                if (!bufZero(c, &buf, part->n * es)) break;
                for (int64_t q = 0; et != ST_NONE && q < part->n && !c->failed; q++) {
                    AVal val;
                    makeVal(c, part->tid, data, q * part->es, 0, &val);
                    put(c, et, &val, &buf, q * es, part->tid);
                }
            } else if (has) {
                AVal val;
                makeVal(c, part->tid, data, 0, 0, &val);
                put(c, pt.tid, &val, &buf, 0, part->tid);
            }
        } else if (ptag && (raw = lastRaw(c, (const uint8_t*)ptag, (int)strlen(ptag))) != NULL) {
            if (strcmp(ptag, own) != 0) warn(c, &c->notes, "renamed by a state rule");
            applyRaw(c, raw, &buf, &scratch);
        } else {
            warn(c, &c->dec, "chunk not in the state (left zero)");
        }
        emit(c, out, k->tag, k->tagLen, buf.p, buf.n);
    }
    free(buf.p);
    free(scratch.p);
    free(tc);
}

static void takeWarnings(Warns* w, StateConvertWarning** rows, int32_t* n) {
    *rows = w->rows;
    *n = w->n;
    w->rows = NULL;
    w->n = w->cap = 0;
}

static void freeWarnings(Warns* w) {
    for (int32_t i = 0; i < w->n; i++) free(w->rows[i].text);
    free(w->rows);
}

static Ctx* ctxNew(char* err, size_t errSize) {
    if (err && errSize) err[0] = 0;
    Ctx* c = calloc(1, sizeof *c);
    if (!c) {
        if (err && errSize) snprintf(err, errSize, "out of memory");
        return NULL;
    }
    c->err = err;
    c->errSize = errSize;
    return c;
}

static void ctxFree(Ctx* c) {
    freeWarnings(&c->enc);
    freeWarnings(&c->dec);
    freeWarnings(&c->notes);
    if (c->defaults) {
        for (int32_t i = 0; i < c->b.ntypes; i++) free(c->defaults[i].e);
    }
    free(c->defaults);
    free(c->defaultsState);
    free(c->plainA);
    free(c->plainB);
    sideFree(&c->a);
    sideFree(&c->b);
    while (c->pool) {
        PoolChunk* next = c->pool->next;
        free(c->pool);
        c->pool = next;
    }
    free(c->ac);
    free(c->acMem);
    free(c->raws);
    free(c->byVar);
    free(c->recs);
    free(c);
}

/* statecanon's Encoder over the writer's body: its chunks, the heap with its blocks' parts, the records. */
static void encodeWriter(Ctx* c, const StateConvertSide* writer) {
    if (!parseChunks(c, writer->body, writer->size, &c->ac, &c->nac)) return;
    c->acMem = malloc((size_t)(c->nac + 1) * sizeof *c->acMem);
    c->raws = malloc((size_t)(c->nac + 1) * sizeof *c->raws);
    c->byVar = malloc((size_t)(stateSchemaVarCount(writer->schema) + 1) * sizeof *c->byVar);
    if (!c->acMem || !c->raws || !c->byVar) fail(c, "out of memory");
    for (int32_t i = 0; !c->failed && i < c->nac; i++) {
        c->acMem[i].p = writer->body + c->ac[i].off;
        c->acMem[i].n = c->ac[i].size;
    }
    for (int32_t i = 0; !c->failed && i < stateSchemaVarCount(writer->schema); i++) c->byVar[i] = -1;
    int32_t hi = c->failed ? -1 : lastChunk(c, "mem.heap");
    if (hi >= 0) { /* encode_heap, before the records: their pointers reach its blocks */
        c->heap.present = 1;
        c->heap.mem = c->acMem[hi];
        if (encodeHeap(c)) {
            for (int32_t i = 0; i < c->heap.n && !c->failed; i++) {
                for (int32_t k = 0; k < c->heap.b[i].nparts; k++) walkPart(c, &c->heap.b[i].parts[k]);
            }
        }
    }
    if (!c->failed) encodeRecords(c);
}

int stateConvert(const StateConvertSide* writer, const StateConvertSide* reader, int stackless,
                 StateConvertResult* out, char* err, size_t errSize) {
    memset(out, 0, sizeof *out);
    Ctx* c = ctxNew(err, errSize);
    if (!c) return 0;
    Buf body = {0};
    if (sideInit(c, &c->a, writer->schema, writer->strings) && sideInit(c, &c->b, reader->schema, reader->strings)) {
        /* the decoder's caches per type (plain, defaults), and the state rules between the two builds */
        c->plainA = malloc((size_t)c->a.ntypes + 1);
        c->plainB = malloc((size_t)c->b.ntypes + 1);
        c->defaultsState = calloc((size_t)c->b.ntypes + 1, 1);
        c->defaults = calloc((size_t)c->b.ntypes + 1, sizeof *c->defaults);
        if (!c->plainA || !c->plainB || !c->defaultsState || !c->defaults) {
            fail(c, "out of memory");
        } else {
            memset(c->plainA, -1, (size_t)c->a.ntypes + 1);
            memset(c->plainB, -1, (size_t)c->b.ntypes + 1);
            rulesInit(c);
        }
        if (!c->failed) encodeWriter(c, writer);
    }
    if (!c->failed) {
        out->safePoint = safePointOf(c);
        if (stackless && !out->safePoint) {
            fail(c, "a stackless state needs one saved at a safe point (XG_STATE_SAVE_SAFE)");
        }
    }
    /* the reader's body is about the template's size: room for it at once, not by doubling */
    if (!c->failed && bufGrow(c, &body, (int64_t)reader->size + (1 << 16))) {
        decodeBody(c, reader->body, reader->size, stackless, &body);
    }
    int ok = !c->failed;
    if (ok) {
        out->body = body.p;
        out->size = (size_t)body.n;
        takeWarnings(&c->enc, &out->encodeWarnings, &out->encodeWarningCount);
        takeWarnings(&c->dec, &out->decodeWarnings, &out->decodeWarningCount);
        takeWarnings(&c->notes, &out->ruleNotes, &out->ruleNoteCount);
    } else {
        free(body.p);
    }
    ctxFree(c);
    return ok;
}

int stateConvertEncode(const StateConvertSide* writer, StateConvertResult* out, char* err, size_t errSize) {
    memset(out, 0, sizeof *out);
    Ctx* c = ctxNew(err, errSize);
    if (!c) return 0;
    /* (the reader's side: the writer's schema, never decoded into) */
    StateStrings none = {NULL, NULL, NULL};
    if (sideInit(c, &c->a, writer->schema, writer->strings) && sideInit(c, &c->b, writer->schema, none)) {
        encodeWriter(c, writer);
    }
    if (!c->failed) out->safePoint = safePointOf(c);
    int ok = !c->failed;
    if (ok) takeWarnings(&c->enc, &out->encodeWarnings, &out->encodeWarningCount);
    ctxFree(c);
    return ok;
}

int stateConvertTemplate(const uint8_t* reader, size_t readerSize, const StateSchema* readerSchema,
                         const uint8_t* writer, size_t writerSize, uint8_t** out, size_t* outSize, char* err,
                         size_t errSize) {
    *out = NULL;
    *outSize = 0;
    Ctx* c = ctxNew(err, errSize);
    if (!c) return 0;
    StateStrings none = {NULL, NULL, NULL};
    Chunk *rc = NULL, *wc = NULL;
    int32_t nr = 0, nw = 0;
    Buf body = {0};
    if (sideInit(c, &c->b, readerSchema, none) && parseChunks(c, reader, readerSize, &rc, &nr) &&
        parseChunks(c, writer, writerSize, &wc, &nw) && bufGrow(c, &body, (int64_t)readerSize + (1 << 16))) {
        for (int32_t i = 0; i < nr && !c->failed; i++) {
            const Chunk* k = &rc[i];
            int64_t size = k->size;
            PartTarget pt;
            /* a variable of the reader's whose type fills the chunk is decoded at the reader's size; the rest
             * is bytes, an array a pointer points at among them (gp0's cachePend; hw.queue is resized by its
             * count anyway) */
            int typed = partTarget(&c->b, k->tag, k->tagLen, &pt) && tsize(&c->b, pt.tid) == k->size;
            if (!(k->tagLen >= 4 && memcmp(k->tag, "mem.", 4) == 0) && !typed) {
                /* the writer's chunk of that tag that comes as many times before (a length and its bytes
                 * can share a tag: gp0's loadBuf) */
                int32_t nth = 0;
                for (int32_t j = 0; j < i; j++) nth += tagEq(rc[j].tag, rc[j].tagLen, k->tag, k->tagLen);
                for (int32_t j = 0; j < nw; j++) {
                    if (tagEq(wc[j].tag, wc[j].tagLen, k->tag, k->tagLen) && nth-- == 0) {
                        size = wc[j].size;
                        break;
                    }
                }
            }
            int64_t at = body.n;
            if (!bufGrow(c, &body, at + 1 + k->tagLen + 4 + size)) break;
            body.p[at] = (uint8_t)k->tagLen;
            memcpy(body.p + at + 1, k->tag, (size_t)k->tagLen);
            for (int q = 0; q < 4; q++) body.p[at + 1 + k->tagLen + q] = (uint8_t)((uint64_t)size >> (8 * q));
            uint8_t* data = body.p + at + 1 + k->tagLen + 4;
            if (size == k->size) {
                if (size) memcpy(data, reader + k->off, (size_t)size);
            } else {
                memset(data, 0, (size_t)size);
            }
            body.n = at + 1 + k->tagLen + 4 + size;
        }
    }
    free(rc);
    free(wc);
    int ok = !c->failed;
    if (ok) {
        *out = body.p;
        *outSize = (size_t)body.n;
    } else {
        free(body.p);
    }
    ctxFree(c);
    return ok;
}

void stateConvertResultFree(StateConvertResult* r) {
    if (!r) return;
    free(r->body);
    for (int32_t i = 0; i < r->encodeWarningCount; i++) free(r->encodeWarnings[i].text);
    for (int32_t i = 0; i < r->decodeWarningCount; i++) free(r->decodeWarnings[i].text);
    for (int32_t i = 0; i < r->ruleNoteCount; i++) free(r->ruleNotes[i].text);
    free(r->encodeWarnings);
    free(r->decodeWarnings);
    free(r->ruleNotes);
    memset(r, 0, sizeof *r);
}

/* ---- strings over the read-only ranges' bytes (Schema.string_at, string_addr) ------------------- */

typedef struct {
    const uint8_t* p;
    size_t len;
    uint64_t addr;
    uint64_t hash; /* 0: an empty slot */
} StrSlot;

struct StateRoStrings {
    const StateRoSegment* seg;
    int32_t count;
    int built;
    StrSlot* slots;
    size_t cap;
};

static uint64_t strHash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h | 1;
}

StateRoStrings* stateRoStringsNew(const StateRoSegment* segments, int32_t count) {
    StateRoStrings* r = calloc(1, sizeof *r);
    if (r) {
        r->seg = segments;
        r->count = count;
    }
    return r;
}

void stateRoStringsFree(StateRoStrings* r) {
    if (!r) return;
    free(r->slots);
    free(r);
}

static int roAt(void* user, uint64_t addr, const uint8_t** text, size_t* len) {
    const StateRoStrings* r = user;
    for (int32_t i = 0; i < r->count; i++) {
        const StateRoSegment* s = &r->seg[i];
        if (s->lo <= addr && addr < s->hi) { /* f.find(b"\0", fo, fo + 4096) */
            size_t rel = (size_t)(addr - s->lo);
            if (rel >= s->size) return 0;
            size_t win = s->size - rel < 4096 ? s->size - rel : 4096;
            const uint8_t* q = memchr(s->bytes + rel, 0, win);
            if (!q) return 0;
            *text = s->bytes + rel;
            *len = (size_t)(q - (s->bytes + rel));
            return 1;
        }
    }
    return 0;
}

/* string_addr's index: every string after a NUL (or a range's start), the first address of each. */
static int roBuild(StateRoStrings* r) {
    size_t n = 0;
    for (int32_t pass = 0; pass < 2; pass++) {
        for (int32_t i = 0; i < r->count; i++) {
            const StateRoSegment* s = &r->seg[i];
            size_t end = (size_t)(s->hi - s->lo) < s->size ? (size_t)(s->hi - s->lo) : s->size;
            size_t at = 0;
            while (at < end) {
                const uint8_t* q = memchr(s->bytes + at, 0, end - at);
                if (!q) break;
                size_t j = (size_t)(q - s->bytes);
                if (j > at) {
                    if (pass == 0) {
                        n++;
                    } else {
                        uint64_t h = strHash(s->bytes + at, j - at);
                        size_t k = (size_t)h & (r->cap - 1);
                        for (;; k = (k + 1) & (r->cap - 1)) {
                            StrSlot* slot = &r->slots[k];
                            if (!slot->hash) {
                                slot->p = s->bytes + at;
                                slot->len = j - at;
                                slot->addr = s->lo + at;
                                slot->hash = h;
                                break;
                            }
                            if (slot->hash == h && slot->len == j - at && memcmp(slot->p, s->bytes + at, j - at) == 0) {
                                break; /* setdefault: the first */
                            }
                        }
                    }
                }
                at = j + 1;
            }
        }
        if (pass == 0) {
            r->cap = 16;
            while (r->cap < 2 * n + 16) r->cap *= 2;
            r->slots = calloc(r->cap, sizeof *r->slots);
            if (!r->slots) return 0;
        }
    }
    r->built = 1;
    return 1;
}

static int roFind(void* user, const uint8_t* text, size_t len, uint64_t* addr) {
    StateRoStrings* r = user;
    if (!r->built && !roBuild(r)) return 0;
    uint64_t h = strHash(text, len);
    for (size_t k = (size_t)h & (r->cap - 1);; k = (k + 1) & (r->cap - 1)) {
        const StrSlot* slot = &r->slots[k];
        if (!slot->hash) return 0;
        if (slot->hash == h && slot->len == len && memcmp(slot->p, text, len) == 0) {
            *addr = slot->addr;
            return 1;
        }
    }
}

StateStrings stateRoStrings(StateRoStrings* r) {
    StateStrings s = {r, roAt, roFind};
    return s;
}
