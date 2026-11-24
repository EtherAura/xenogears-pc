/*
 * The JSON reader (json.h), written for the state schema: one pass over the
 * text into an array of nodes in document order, each knowing where the
 * next sibling starts, and one buffer of decoded strings (keys and values).
 */

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 256

typedef struct {
    int32_t next;  /* the node after this one and everything under it */
    int32_t key;   /* an object value's key in the strings, 0 ("") otherwise */
    int32_t parent; /* the array or object it sits in, JSON_NONE for the root */
    uint8_t type;
    union {
        int64_t i;
        double r;
        int64_t str; /* JSON_STRING: its offset in the strings */
    } v;
} Node;

struct JsonDoc {
    Node* nodes;
    int32_t count, cap;
    char* strings;
    size_t used, size;
};

typedef struct {
    const char* p;
    const char* end;
    const char* start;
    JsonDoc* d;
    char* err;
    size_t errSize;
    int failed;
} Parser;

static void fail(Parser* ps, const char* what) {
    if (!ps->failed && ps->err && ps->errSize) {
        snprintf(ps->err, ps->errSize, "%s at byte %ld", what, (long)(ps->p - ps->start));
    }
    ps->failed = 1;
}

static int32_t addNode(Parser* ps, int type, int32_t key, int32_t parent) {
    JsonDoc* d = ps->d;
    if (d->count == d->cap) {
        int32_t cap = d->cap ? d->cap * 2 : 4096;
        Node* n = realloc(d->nodes, (size_t)cap * sizeof *n);
        if (!n) {
            fail(ps, "out of memory");
            return JSON_NONE;
        }
        d->nodes = n;
        d->cap = cap;
    }
    Node* n = &d->nodes[d->count];
    n->next = d->count + 1;
    n->key = key;
    n->parent = parent;
    n->type = (uint8_t)type;
    n->v.i = 0;
    return d->count++;
}

static int reserve(Parser* ps, size_t more) {
    JsonDoc* d = ps->d;
    if (d->used + more <= d->size) {
        return 1;
    }
    size_t size = d->size ? d->size : 1 << 16;
    while (size < d->used + more) {
        size *= 2;
    }
    char* s = realloc(d->strings, size);
    if (!s) {
        fail(ps, "out of memory");
        return 0;
    }
    d->strings = s;
    d->size = size;
    return 1;
}

static void skipSpace(Parser* ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) {
        ps->p++;
    }
}

static int hex4(const char* p, uint32_t* out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static void putUtf8(char* out, size_t* n, uint32_t c) {
    if (c < 0x80) {
        out[(*n)++] = (char)c;
    } else if (c < 0x800) {
        out[(*n)++] = (char)(0xC0 | c >> 6);
        out[(*n)++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out[(*n)++] = (char)(0xE0 | c >> 12);
        out[(*n)++] = (char)(0x80 | (c >> 6 & 0x3F));
        out[(*n)++] = (char)(0x80 | (c & 0x3F));
    } else {
        out[(*n)++] = (char)(0xF0 | c >> 18);
        out[(*n)++] = (char)(0x80 | (c >> 12 & 0x3F));
        out[(*n)++] = (char)(0x80 | (c >> 6 & 0x3F));
        out[(*n)++] = (char)(0x80 | (c & 0x3F));
    }
}

/* A string at ps->p (its opening quote) into the strings; its offset, or -1. */
static int64_t parseString(Parser* ps) {
    const char* p = ps->p + 1;
    const char* q = p;
    while (q < ps->end && *q != '"') {
        q += *q == '\\' ? 2 : 1;
    }
    if (q >= ps->end) {
        fail(ps, "unterminated string");
        return -1;
    }
    /* decoded text is never longer than the escaped text */
    if (!reserve(ps, (size_t)(q - p) + 1)) {
        return -1;
    }
    JsonDoc* d = ps->d;
    int64_t at = (int64_t)d->used;
    char* out = d->strings + d->used;
    size_t n = 0;
    while (p < q) {
        unsigned char c = (unsigned char)*p++;
        if (c != '\\') {
            if (c < 0x20) {
                ps->p = p - 1;
                fail(ps, "control character in a string");
                return -1;
            }
            out[n++] = (char)c;
            continue;
        }
        c = (unsigned char)*p++;
        switch (c) {
        case '"': out[n++] = '"'; break;
        case '\\': out[n++] = '\\'; break;
        case '/': out[n++] = '/'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            uint32_t u;
            if (q - p < 4 || !hex4(p, &u)) {
                ps->p = p;
                fail(ps, "bad \\u escape");
                return -1;
            }
            p += 4;
            if (u >= 0xD800 && u < 0xDC00) { /* a surrogate pair */
                uint32_t lo;
                if (q - p >= 6 && p[0] == '\\' && p[1] == 'u' && hex4(p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                    p += 6;
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    u = 0xFFFD;
                }
            } else if (u >= 0xDC00 && u < 0xE000) {
                u = 0xFFFD;
            }
            /* 6 escaped bytes give at most 3 (4 from 12): within the reserve */
            putUtf8(out, &n, u);
            break;
        }
        default:
            ps->p = p - 1;
            fail(ps, "bad escape");
            return -1;
        }
    }
    out[n] = 0;
    d->used += n + 1;
    ps->p = q + 1;
    return at;
}

static void parseNumber(Parser* ps, Node* n) {
    const char* p = ps->p;
    const char* s = p;
    int real = 0;
    if (p < ps->end && *p == '-') p++;
    if (p >= ps->end || !(*p >= '0' && *p <= '9')) {
        fail(ps, "bad number");
        return;
    }
    while (p < ps->end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) {
        if (*p == '.' || *p == 'e' || *p == 'E') real = 1;
        p++;
    }
    if (real) {
        char buf[64];
        size_t len = (size_t)(p - s);
        if (len >= sizeof buf) {
            fail(ps, "number too long");
            return;
        }
        memcpy(buf, s, len);
        buf[len] = 0;
        n->type = JSON_REAL;
        n->v.r = strtod(buf, NULL);
    } else {
        int neg = *s == '-';
        uint64_t v = 0;
        for (const char* c = s + neg; c < p; c++) {
            uint64_t d = (uint64_t)(*c - '0');
            if (v > (UINT64_MAX - d) / 10) {
                fail(ps, "integer out of range");
                return;
            }
            v = v * 10 + d;
        }
        if (v > (uint64_t)INT64_MAX + (uint64_t)neg) {
            fail(ps, "integer out of range");
            return;
        }
        n->type = JSON_INT;
        n->v.i = neg ? (int64_t)(0 - v) : (int64_t)v;
    }
    ps->p = p;
}

static int literal(Parser* ps, const char* word) {
    size_t len = strlen(word);
    if ((size_t)(ps->end - ps->p) >= len && memcmp(ps->p, word, len) == 0) {
        ps->p += len;
        return 1;
    }
    fail(ps, "bad literal");
    return 0;
}

static void parseValue(Parser* ps, int32_t key, int32_t parent, int depth) {
    skipSpace(ps);
    if (ps->p >= ps->end) {
        fail(ps, "unexpected end");
        return;
    }
    if (depth > MAX_DEPTH) {
        fail(ps, "nested too deep");
        return;
    }
    char c = *ps->p;
    if (c == '{' || c == '[') {
        int32_t me = addNode(ps, c == '{' ? JSON_OBJECT : JSON_ARRAY, key, parent);
        if (me == JSON_NONE) return;
        char close = c == '{' ? '}' : ']';
        ps->p++;
        skipSpace(ps);
        if (ps->p < ps->end && *ps->p == close) {
            ps->p++;
        } else {
            for (;;) {
                int32_t k = 0;
                if (c == '{') {
                    skipSpace(ps);
                    if (ps->p >= ps->end || *ps->p != '"') {
                        fail(ps, "expected a key");
                        return;
                    }
                    int64_t at = parseString(ps);
                    if (at < 0) return;
                    if (at > INT32_MAX) {
                        fail(ps, "document too large");
                        return;
                    }
                    k = (int32_t)at;
                    skipSpace(ps);
                    if (ps->p >= ps->end || *ps->p != ':') {
                        fail(ps, "expected ':'");
                        return;
                    }
                    ps->p++;
                }
                parseValue(ps, k, me, depth + 1);
                if (ps->failed) return;
                skipSpace(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == close) {
                    ps->p++;
                    break;
                }
                fail(ps, c == '{' ? "expected ',' or '}'" : "expected ',' or ']'");
                return;
            }
        }
        ps->d->nodes[me].next = ps->d->count;
        return;
    }
    int32_t me = addNode(ps, JSON_NULL, key, parent);
    if (me == JSON_NONE) return;
    if (c == '"') {
        int64_t at = parseString(ps);
        if (at < 0) return;
        ps->d->nodes[me].type = JSON_STRING;
        ps->d->nodes[me].v.str = at;
    } else if (c == 't') {
        if (literal(ps, "true")) ps->d->nodes[me].type = JSON_TRUE;
    } else if (c == 'f') {
        if (literal(ps, "false")) ps->d->nodes[me].type = JSON_FALSE;
    } else if (c == 'n') {
        literal(ps, "null");
    } else {
        parseNumber(ps, &ps->d->nodes[me]);
    }
}

JsonDoc* jsonParse(const char* text, size_t n, char* err, size_t errSize) {
    JsonDoc* d = calloc(1, sizeof *d);
    if (!d) {
        if (err && errSize) snprintf(err, errSize, "out of memory");
        return NULL;
    }
    Parser ps = {text, text + n, text, d, err, errSize, 0};
    if (reserve(&ps, 1)) {
        d->strings[0] = 0; /* offset 0: "" (no key) */
        d->used = 1;
    }
    if (!ps.failed) {
        parseValue(&ps, 0, JSON_NONE, 0);
        skipSpace(&ps);
        if (!ps.failed && ps.p != ps.end) {
            fail(&ps, "text after the document");
        }
    }
    if (ps.failed) {
        jsonFree(d);
        return NULL;
    }
    return d;
}

void jsonFree(JsonDoc* d) {
    if (d) {
        free(d->nodes);
        free(d->strings);
        free(d);
    }
}

static const Node* node(const JsonDoc* d, JsonRef r) {
    return d && r >= 0 && r < d->count ? &d->nodes[r] : NULL;
}

JsonRef jsonRoot(const JsonDoc* d) {
    return d && d->count ? 0 : JSON_NONE;
}

int jsonType(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    return n ? n->type : JSON_NULL;
}

JsonRef jsonFirst(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    if (!n || (n->type != JSON_ARRAY && n->type != JSON_OBJECT) || n->next == r + 1) {
        return JSON_NONE;
    }
    return r + 1;
}

/* The node after r's subtree, while it is still inside r's parent. */
static JsonRef nextIn(const JsonDoc* d, JsonRef r, int32_t end) {
    const Node* n = node(d, r);
    if (!n || n->next >= end) {
        return JSON_NONE;
    }
    return n->next;
}

JsonRef jsonNext(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    if (!n) {
        return JSON_NONE;
    }
    return nextIn(d, r, n->parent == JSON_NONE ? d->count : d->nodes[n->parent].next);
}

const char* jsonKey(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    return n ? d->strings + n->key : "";
}

JsonRef jsonGet(const JsonDoc* d, JsonRef obj, const char* key) {
    const Node* o = node(d, obj);
    if (!o || o->type != JSON_OBJECT) {
        return JSON_NONE;
    }
    for (JsonRef r = jsonFirst(d, obj); r != JSON_NONE; r = nextIn(d, r, o->next)) {
        if (strcmp(d->strings + d->nodes[r].key, key) == 0) {
            return r;
        }
    }
    return JSON_NONE;
}

int32_t jsonCount(const JsonDoc* d, JsonRef r) {
    const Node* o = node(d, r);
    int32_t n = 0;
    if (!o) return 0;
    for (JsonRef c = jsonFirst(d, r); c != JSON_NONE; c = nextIn(d, c, o->next)) {
        n++;
    }
    return n;
}

const char* jsonString(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    return n && n->type == JSON_STRING ? d->strings + n->v.str : "";
}

int64_t jsonInt(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    if (!n) return 0;
    if (n->type == JSON_INT) return n->v.i;
    if (n->type == JSON_REAL) return (int64_t)n->v.r;
    return n->type == JSON_TRUE;
}

double jsonReal(const JsonDoc* d, JsonRef r) {
    const Node* n = node(d, r);
    if (!n) return 0;
    if (n->type == JSON_REAL) return n->v.r;
    if (n->type == JSON_INT) return (double)n->v.i;
    return n->type == JSON_TRUE;
}
