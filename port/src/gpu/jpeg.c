/*
 * A JPEG decoder (jpeg.h) from ITU-T T.81: the marker segments (Annex B),
 * Huffman tables (Annex C), sequential decoding (Annex F) and progressive
 * decoding with spectral selection and successive approximation (Annex G),
 * the inverse DCT of A.3.3, and the JFIF YCbCr to RGB equations.
 *
 * Every block's coefficients are kept until the last scan, then each block
 * is dequantized and transformed into its component's plane, and the planes
 * (scaled up by their sampling factors, nearest sample) are converted to
 * RGBA. The entropy-coded data is read through a bit reader that drops the
 * stuffed zero after each 0xFF and stops at a marker, giving zeros after it
 * (or after the data's end); a scan with no data left stops early.
 */

#include "jpeg.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The natural (row-major) index of the k-th coefficient in zig-zag order
 * (T.81 Figure A.6). */
static const uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

#define FAST_BITS 9

typedef struct {
    int defined;
    uint8_t vals[256];
    int mincode[17], maxcode[17], valptr[17]; /* per code length; maxcode -1: none */
    uint16_t fast[1 << FAST_BITS];            /* the first FAST_BITS bits: length << 8 | symbol, 0 longer */
} Huff;

typedef struct {
    int id, h, v, tq;
    int bw, bh; /* blocks across and down, the frame's MCUs' worth */
    int cw, ch; /* blocks across and down the component's own picture */
    int16_t* coef;
    int pred;
    int td, ta;
} Comp;

typedef struct {
    const uint8_t* data;
    size_t size, pos;
    uint32_t buf; /* the next bits, from the top */
    int cnt;
    int marker;   /* a marker met in the coded data (0: none yet) */
    int padding;  /* zero bytes given past a marker or the end */
} Bits;

typedef struct {
    const uint8_t* data;
    size_t size;
    char* err;
    size_t errCap;
    int failed;
    uint16_t q[4][64]; /* natural order */
    int qdef[4];
    Huff dc[4], ac[4];
    int width, height, ncomp, hmax, vmax, mcux, mcuy, progressive, frame, scans;
    Comp comp[4];
    int restart;
    int adobe, adobeTransform;
    int eobrun;
    Bits bits;
    int maxSide;
    uint64_t maxPixels;
} Dec;

static int fail(Dec* d, const char* fmt, ...) {
    if (!d->failed) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(d->err, d->errCap, fmt, ap);
        va_end(ap);
        d->failed = 1;
    }
    return 0;
}

static unsigned get16(const uint8_t* p) {
    return (unsigned)p[0] << 8 | p[1];
}

/* ---- Huffman tables (Annex C) ---- */

static int buildHuff(Dec* d, Huff* h, const uint8_t* bits, const uint8_t* vals, int count) {
    memset(h, 0, sizeof(*h));
    memcpy(h->vals, vals, (size_t)count);
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        h->valptr[len] = k;
        h->mincode[len] = code;
        code += bits[len - 1];
        k += bits[len - 1];
        if (code > (1 << len)) {
            return fail(d, "a Huffman table with more codes than its lengths allow");
        }
        h->maxcode[len] = bits[len - 1] ? code - 1 : -1;
        code <<= 1;
    }
    /* the short codes, looked up at once */
    for (int len = 1; len <= FAST_BITS; len++) {
        for (int i = 0; i < bits[len - 1]; i++) {
            int c = h->mincode[len] + i;
            int sym = h->vals[h->valptr[len] + i];
            int shift = FAST_BITS - len;
            for (int j = 0; j < (1 << shift); j++) {
                h->fast[(c << shift) | j] = (uint16_t)(len << 8 | sym);
            }
        }
    }
    h->defined = 1;
    return 1;
}

/* ---- the coded data ---- */

static void fill(Bits* b) {
    while (b->cnt <= 24) {
        uint32_t byte = 0;
        if (b->marker == 0 && b->pos < b->size) {
            byte = b->data[b->pos];
            if (byte == 0xFF) {
                int next = b->pos + 1 < b->size ? b->data[b->pos + 1] : -1;
                if (next == 0x00) {
                    b->pos += 2; /* a stuffed 0xFF */
                } else if (next == 0xFF) {
                    b->pos++; /* a fill byte before a marker */
                    continue;
                } else {
                    b->marker = next < 0 ? 0xD9 : next; /* stays for the parser */
                    byte = 0;
                    b->padding++;
                }
            } else {
                b->pos++;
            }
        } else {
            if (b->marker == 0) {
                b->marker = 0xD9; /* the data's end */
            }
            b->padding++;
        }
        b->buf |= byte << (24 - b->cnt);
        b->cnt += 8;
    }
}

static unsigned peek(Bits* b, int n) {
    if (b->cnt < n) {
        fill(b);
    }
    return b->buf >> (32 - n);
}

static void skip(Bits* b, int n) {
    b->buf <<= n;
    b->cnt -= n;
}

static unsigned getBits(Bits* b, int n) {
    if (n == 0) {
        return 0;
    }
    unsigned v = peek(b, n);
    skip(b, n);
    return v;
}

/* A Huffman symbol; -1 on a code no table has. */
static int decode(Dec* d, const Huff* h) {
    unsigned look = peek(&d->bits, 16);
    unsigned f = h->fast[look >> (16 - FAST_BITS)];
    if (f != 0) {
        skip(&d->bits, (int)(f >> 8));
        return (int)(f & 0xFF);
    }
    for (int len = FAST_BITS + 1; len <= 16; len++) {
        int code = (int)(look >> (16 - len));
        if (h->maxcode[len] >= 0 && code <= h->maxcode[len] && code >= h->mincode[len]) {
            skip(&d->bits, len);
            return h->vals[h->valptr[len] + code - h->mincode[len]];
        }
    }
    return -1;
}

/* RECEIVE and EXTEND (F.2.2.1): s magnitude bits as a signed value. */
static int receiveExtend(Bits* b, int s) {
    if (s == 0) {
        return 0;
    }
    if (s > 16) {
        s = 16;
    }
    int v = (int)getBits(b, s);
    return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

static int16_t clamp16(int v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* A bad code: corruption, unless the data ran out (the rest stays zero). */
static int badCode(Dec* d) {
    if (d->bits.padding > 0) {
        return 0;
    }
    return fail(d, "damaged coded data (a code no table has)");
}

/* ---- blocks (Annexes F and G) ---- */

static int blockSequential(Dec* d, Comp* c, int16_t* co) {
    int t = decode(d, &d->dc[c->td]);
    if (t < 0) {
        return badCode(d);
    }
    c->pred += receiveExtend(&d->bits, t);
    c->pred = clamp16(c->pred);
    co[0] = (int16_t)c->pred;
    const Huff* ac = &d->ac[c->ta];
    for (int k = 1; k < 64;) {
        int rs = decode(d, ac);
        if (rs < 0) {
            return badCode(d);
        }
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15) {
                break; /* EOB */
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) {
            return fail(d, "damaged coded data (past the block's end)");
        }
        co[zigzag[k]] = clamp16(receiveExtend(&d->bits, s));
        k++;
    }
    return 1;
}

static int blockDcFirst(Dec* d, Comp* c, int16_t* co, int al) {
    int t = decode(d, &d->dc[c->td]);
    if (t < 0) {
        return badCode(d);
    }
    c->pred += receiveExtend(&d->bits, t);
    c->pred = clamp16(c->pred);
    co[0] = clamp16(c->pred * (1 << al));
    return 1;
}

static int blockDcRefine(Dec* d, int16_t* co, int al) {
    if (getBits(&d->bits, 1)) {
        co[0] = (int16_t)(co[0] | (1 << al));
    }
    return 1;
}

static int blockAcFirst(Dec* d, Comp* c, int16_t* co, int ss, int se, int al) {
    if (d->eobrun > 0) {
        d->eobrun--;
        return 1;
    }
    const Huff* ac = &d->ac[c->ta];
    for (int k = ss; k <= se;) {
        int rs = decode(d, ac);
        if (rs < 0) {
            return badCode(d);
        }
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r < 15) {
                d->eobrun = (1 << r) - 1;
                if (r > 0) {
                    d->eobrun += (int)getBits(&d->bits, r);
                }
                break;
            }
            k += 16; /* ZRL */
            continue;
        }
        k += r;
        if (k > se) {
            return fail(d, "damaged coded data (past the band's end)");
        }
        co[zigzag[k]] = clamp16(receiveExtend(&d->bits, s) * (1 << al));
        k++;
    }
    return 1;
}

/* A refinement bit for an already nonzero coefficient (G.1.2.3). */
static void refine(Dec* d, int16_t* x, int p1) {
    if (getBits(&d->bits, 1) && (*x & p1) == 0) {
        *x = clamp16(*x >= 0 ? *x + p1 : *x - p1);
    }
}

static int blockAcRefine(Dec* d, Comp* c, int16_t* co, int ss, int se, int al) {
    int p1 = 1 << al;
    int k = ss;
    if (d->eobrun == 0) {
        const Huff* ac = &d->ac[c->ta];
        for (; k <= se;) {
            int rs = decode(d, ac);
            if (rs < 0) {
                return badCode(d);
            }
            int r = rs >> 4, s = rs & 15, value = 0;
            if (s != 0) {
                /* a new coefficient of magnitude 1 << al; its sign bit */
                value = getBits(&d->bits, 1) ? p1 : -p1;
            } else if (r != 15) {
                /* EOBRUN: this block's rest and the next blocks' only get refined */
                d->eobrun = 1 << r;
                if (r > 0) {
                    d->eobrun += (int)getBits(&d->bits, r);
                }
                break;
            }
            /* skip r zero-history coefficients (refining the nonzero ones met),
             * then place the new one */
            while (k <= se) {
                int16_t* x = &co[zigzag[k]];
                if (*x != 0) {
                    refine(d, x, p1);
                } else {
                    if (r == 0) {
                        if (value != 0) {
                            *x = (int16_t)value;
                        }
                        k++;
                        break;
                    }
                    r--;
                }
                k++;
            }
        }
    }
    if (d->eobrun > 0) {
        for (; k <= se; k++) {
            int16_t* x = &co[zigzag[k]];
            if (*x != 0) {
                refine(d, x, p1);
            }
        }
        d->eobrun--;
    }
    return 1;
}

/* ---- scans ---- */

static void restartInterval(Dec* d) {
    Bits* b = &d->bits;
    b->buf = 0;
    b->cnt = 0;
    if (b->marker == 0) {
        /* not reached: the next marker */
        size_t p = b->pos;
        while (p + 1 < b->size && !(b->data[p] == 0xFF && b->data[p + 1] != 0x00 && b->data[p + 1] != 0xFF)) {
            p++;
        }
        b->pos = p;
        b->marker = p + 1 < b->size ? b->data[p + 1] : 0xD9;
    }
    if (b->marker >= 0xD0 && b->marker <= 0xD7) {
        b->pos += 2;
        b->marker = 0;
        b->padding = 0;
    }
    for (int i = 0; i < d->ncomp; i++) {
        d->comp[i].pred = 0;
    }
    d->eobrun = 0;
}

static int block(Dec* d, Comp* c, int bx, int by, int ss, int se, int ah, int al) {
    if (bx >= c->bw || by >= c->bh) {
        return 1; /* cannot happen with checked sizes; never written outside */
    }
    int16_t* co = c->coef + ((size_t)by * c->bw + bx) * 64;
    if (!d->progressive) {
        return blockSequential(d, c, co);
    }
    if (ss == 0) {
        return ah == 0 ? blockDcFirst(d, c, co, al) : blockDcRefine(d, co, al);
    }
    return ah == 0 ? blockAcFirst(d, c, co, ss, se, al) : blockAcRefine(d, c, co, ss, se, al);
}

static void scan(Dec* d, Comp** sc, int ns, int ss, int se, int ah, int al, size_t* pos) {
    memset(&d->bits, 0, sizeof(d->bits));
    d->bits.data = d->data;
    d->bits.size = d->size;
    d->bits.pos = *pos;
    for (int i = 0; i < d->ncomp; i++) {
        d->comp[i].pred = 0;
    }
    d->eobrun = 0;
    long units = ns == 1 ? (long)sc[0]->cw * sc[0]->ch : (long)d->mcux * d->mcuy;
    for (long u = 0; u < units && !d->failed; u++) {
        if (d->restart > 0 && u > 0 && u % d->restart == 0) {
            restartInterval(d);
        }
        if (d->bits.padding > 64) {
            break; /* the data ran out: the rest stays as it is */
        }
        if (ns == 1) {
            block(d, sc[0], (int)(u % sc[0]->cw), (int)(u / sc[0]->cw), ss, se, ah, al);
        } else {
            int mx = (int)(u % d->mcux), my = (int)(u / d->mcux);
            for (int i = 0; i < ns && !d->failed; i++) {
                Comp* c = sc[i];
                for (int v = 0; v < c->v; v++) {
                    for (int h = 0; h < c->h; h++) {
                        block(d, c, mx * c->h + h, my * c->v + v, ss, se, ah, al);
                    }
                }
            }
        }
    }
    *pos = d->bits.pos;
    d->scans++;
}

/* ---- marker segments (Annex B) ---- */

static int frameHeader(Dec* d, const uint8_t* s, unsigned len, int marker) {
    if (d->frame) {
        return fail(d, "more than one frame");
    }
    if (len < 6) {
        return fail(d, "a frame header cut short");
    }
    int p = s[0];
    d->height = (int)get16(s + 1);
    d->width = (int)get16(s + 3);
    d->ncomp = s[5];
    if (p != 8) {
        return fail(d, "%d-bit samples are not supported", p);
    }
    if (d->ncomp != 1 && d->ncomp != 3) {
        return fail(d, d->ncomp == 4 ? "four components (CMYK) are not supported" : "%d components", d->ncomp);
    }
    if (len < 6u + 3u * (unsigned)d->ncomp) {
        return fail(d, "a frame header cut short");
    }
    if (d->width <= 0 || d->height <= 0) {
        return fail(d, "no size (a height given later, by DNL, is not supported)");
    }
    if (d->width > d->maxSide || d->height > d->maxSide || (uint64_t)d->width * d->height > d->maxPixels) {
        return fail(d, "%dx%d is too large", d->width, d->height);
    }
    d->progressive = marker == 0xC2;
    d->hmax = d->vmax = 1;
    for (int i = 0; i < d->ncomp; i++) {
        Comp* c = &d->comp[i];
        c->id = s[6 + 3 * i];
        c->h = s[7 + 3 * i] >> 4;
        c->v = s[7 + 3 * i] & 15;
        c->tq = s[8 + 3 * i];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3) {
            return fail(d, "a component with sampling %dx%d or table %d", c->h, c->v, c->tq);
        }
        for (int j = 0; j < i; j++) {
            if (d->comp[j].id == c->id) {
                return fail(d, "two components with id %d", c->id);
            }
        }
        d->hmax = c->h > d->hmax ? c->h : d->hmax;
        d->vmax = c->v > d->vmax ? c->v : d->vmax;
    }
    d->mcux = (d->width + 8 * d->hmax - 1) / (8 * d->hmax);
    d->mcuy = (d->height + 8 * d->vmax - 1) / (8 * d->vmax);
    for (int i = 0; i < d->ncomp; i++) {
        Comp* c = &d->comp[i];
        c->bw = d->mcux * c->h;
        c->bh = d->mcuy * c->v;
        int pw = (d->width * c->h + d->hmax - 1) / d->hmax, ph = (d->height * c->v + d->vmax - 1) / d->vmax;
        c->cw = (pw + 7) / 8;
        c->ch = (ph + 7) / 8;
        c->coef = calloc((size_t)c->bw * c->bh * 64, sizeof(int16_t));
        if (c->coef == NULL) {
            return fail(d, "out of memory for %dx%d", d->width, d->height);
        }
    }
    d->frame = 1;
    return 1;
}

static int huffmanTables(Dec* d, const uint8_t* s, unsigned len) {
    unsigned i = 0;
    while (i < len) {
        if (len - i < 17) {
            return fail(d, "a Huffman table cut short");
        }
        int tc = s[i] >> 4, th = s[i] & 15;
        int count = 0;
        for (int k = 0; k < 16; k++) {
            count += s[i + 1 + k];
        }
        if (tc > 1 || th > 3 || count > 256 || len - i - 17 < (unsigned)count) {
            return fail(d, "a bad Huffman table");
        }
        if (!buildHuff(d, tc == 0 ? &d->dc[th] : &d->ac[th], s + i + 1, s + i + 17, count)) {
            return 0;
        }
        i += 17u + (unsigned)count;
    }
    return 1;
}

static int quantTables(Dec* d, const uint8_t* s, unsigned len) {
    unsigned i = 0;
    while (i < len) {
        int pq = s[i] >> 4, tq = s[i] & 15;
        unsigned need = 1u + (pq ? 128u : 64u);
        if (pq > 1 || tq > 3 || len - i < need) {
            return fail(d, "a bad quantization table");
        }
        for (int k = 0; k < 64; k++) {
            d->q[tq][zigzag[k]] = (uint16_t)(pq ? get16(s + i + 1 + 2 * k) : s[i + 1 + k]);
        }
        d->qdef[tq] = 1;
        i += need;
    }
    return 1;
}

static int startOfScan(Dec* d, const uint8_t* s, unsigned len, size_t* pos) {
    if (!d->frame) {
        return fail(d, "a scan before the frame header");
    }
    if (len < 1) {
        return fail(d, "a scan header cut short");
    }
    int ns = s[0];
    if (ns < 1 || ns > d->ncomp || len < 4u + 2u * (unsigned)ns) {
        return fail(d, "a bad scan header");
    }
    Comp* sc[4];
    for (int i = 0; i < ns; i++) {
        int id = s[1 + 2 * i], t = s[2 + 2 * i];
        sc[i] = NULL;
        for (int j = 0; j < d->ncomp; j++) {
            if (d->comp[j].id == id) {
                sc[i] = &d->comp[j];
            }
        }
        for (int j = 0; j < i; j++) {
            if (sc[j] == sc[i]) {
                sc[i] = NULL;
            }
        }
        if (sc[i] == NULL) {
            return fail(d, "a scan of an unknown or repeated component %d", id);
        }
        sc[i]->td = t >> 4;
        sc[i]->ta = t & 15;
        if (sc[i]->td > 3 || sc[i]->ta > 3) {
            return fail(d, "a scan with Huffman table %d/%d", sc[i]->td, sc[i]->ta);
        }
    }
    int ss = s[1 + 2 * ns], se = s[2 + 2 * ns], ah = s[3 + 2 * ns] >> 4, al = s[3 + 2 * ns] & 15;
    int dcScan = 1, acScan = 1;
    if (d->progressive) {
        if (ss > se || se > 63 || (ss == 0 && se != 0) || (ss > 0 && ns != 1) || ah > 13 || al > 13) {
            return fail(d, "a bad progressive scan (%d-%d, %d/%d)", ss, se, ah, al);
        }
        dcScan = ss == 0 && ah == 0;
        acScan = ss > 0;
    } else {
        ss = 0, se = 63, ah = al = 0;
    }
    if (ns > 1) {
        long blocks = 0;
        for (int i = 0; i < ns; i++) {
            blocks += sc[i]->h * sc[i]->v;
        }
        if (blocks > 10) {
            return fail(d, "an MCU of %ld blocks (at most 10)", blocks);
        }
    }
    for (int i = 0; i < ns; i++) {
        if ((dcScan && !d->dc[sc[i]->td].defined) || (acScan && !d->ac[sc[i]->ta].defined)) {
            return fail(d, "a scan with a Huffman table not defined");
        }
    }
    scan(d, sc, ns, ss, se, ah, al, pos);
    return !d->failed;
}

/* ---- pictures ---- */

static float idctTab[8][8]; /* [x][u]: C(u) / 2 * cos((2x + 1) u pi / 16) */

static void idctInit(void) {
    if (idctTab[0][0] != 0.0f) {
        return;
    }
    for (int x = 0; x < 8; x++) {
        for (int u = 0; u < 8; u++) {
            double cu = u == 0 ? 1.0 / sqrt(2.0) : 1.0;
            idctTab[x][u] = (float)(cu / 2.0 * cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0));
        }
    }
}

static uint8_t sample(float v) {
    int i = (int)lrintf(v + 128.0f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

/* A component's blocks, dequantized and transformed, into its plane (bw x
 * bh blocks of 8 x 8 samples). */
static void plane(Dec* d, const Comp* c, uint8_t* out) {
    const uint16_t* q = d->q[c->tq];
    size_t stride = (size_t)c->bw * 8;
    for (int by = 0; by < c->bh; by++) {
        for (int bx = 0; bx < c->bw; bx++) {
            const int16_t* co = c->coef + ((size_t)by * c->bw + bx) * 64;
            uint8_t* o = out + (size_t)by * 8 * stride + (size_t)bx * 8;
            float in[64];
            int ac = 0;
            for (int i = 0; i < 64; i++) {
                in[i] = (float)co[i] * (float)q[i];
                ac |= i > 0 && co[i] != 0;
            }
            if (!ac) {
                uint8_t v = sample(in[0] / 8.0f);
                for (int y = 0; y < 8; y++) {
                    memset(o + (size_t)y * stride, v, 8);
                }
                continue;
            }
            float tmp[64];
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    float s = 0.0f;
                    for (int u = 0; u < 8; u++) {
                        s += in[y * 8 + u] * idctTab[x][u];
                    }
                    tmp[y * 8 + x] = s;
                }
            }
            for (int x = 0; x < 8; x++) {
                for (int y = 0; y < 8; y++) {
                    float s = 0.0f;
                    for (int v = 0; v < 8; v++) {
                        s += tmp[v * 8 + x] * idctTab[y][v];
                    }
                    o[(size_t)y * stride + x] = sample(s);
                }
            }
        }
    }
}

static uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static uint8_t* picture(Dec* d) {
    idctInit();
    for (int i = 0; i < d->ncomp; i++) {
        if (!d->qdef[d->comp[i].tq]) {
            fail(d, "a component's quantization table is not defined");
            return NULL;
        }
    }
    uint8_t* planes[4] = {NULL};
    uint8_t* out = malloc((size_t)d->width * d->height * 4);
    int* cols[4] = {NULL};
    int ok = out != NULL;
    for (int i = 0; ok && i < d->ncomp; i++) {
        const Comp* c = &d->comp[i];
        planes[i] = malloc((size_t)c->bw * 8 * c->bh * 8);
        cols[i] = malloc(sizeof(int) * (size_t)d->width);
        ok = planes[i] != NULL && cols[i] != NULL;
        if (ok) {
            plane(d, c, planes[i]);
            for (int x = 0; x < d->width; x++) {
                cols[i][x] = x * c->h / d->hmax;
            }
        }
    }
    int rgb = d->ncomp == 3 && ((d->adobe && d->adobeTransform == 0) ||
                                (!d->adobe && d->comp[0].id == 'R' && d->comp[1].id == 'G' && d->comp[2].id == 'B'));
    for (int y = 0; ok && y < d->height; y++) {
        uint8_t* o = out + (size_t)y * d->width * 4;
        const uint8_t* rows[4] = {NULL, NULL, NULL, NULL};
        for (int i = 0; i < d->ncomp; i++) {
            const Comp* c = &d->comp[i];
            rows[i] = planes[i] + (size_t)(y * c->v / d->vmax) * c->bw * 8;
        }
        for (int x = 0; x < d->width; x++, o += 4) {
            if (d->ncomp == 1) {
                o[0] = o[1] = o[2] = rows[0][cols[0][x]];
            } else if (rgb) {
                o[0] = rows[0][cols[0][x]], o[1] = rows[1][cols[1][x]], o[2] = rows[2][cols[2][x]];
            } else {
                /* JFIF: R = Y + 1.402 Cr', G = Y - 0.344136 Cb' - 0.714136 Cr', B = Y + 1.772 Cb'
                 * (Cb' = Cb - 128, Cr' = Cr - 128), in 16.16 fixed point */
                int Y = rows[0][cols[0][x]] << 16, cb = rows[1][cols[1][x]] - 128, cr = rows[2][cols[2][x]] - 128;
                o[0] = clamp8((Y + 91881 * cr + 32768) >> 16);
                o[1] = clamp8((Y - 22554 * cb - 46802 * cr + 32768) >> 16);
                o[2] = clamp8((Y + 116130 * cb + 32768) >> 16);
            }
            o[3] = 255;
        }
    }
    for (int i = 0; i < 4; i++) {
        free(planes[i]);
        free(cols[i]);
    }
    if (!ok) {
        free(out);
        fail(d, "out of memory for %dx%d", d->width, d->height);
        return NULL;
    }
    return out;
}

int jpegIs(const uint8_t* data, size_t size) {
    return size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

uint8_t* jpegDecode(const uint8_t* data, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels, char* err,
                    size_t errCap) {
    static Dec zero;
    Dec* d = malloc(sizeof(Dec));
    if (d == NULL) {
        snprintf(err, errCap, "out of memory");
        return NULL;
    }
    *d = zero;
    d->data = data;
    d->size = size;
    d->err = err;
    d->errCap = errCap;
    d->maxSide = maxSide;
    d->maxPixels = maxPixels;
    err[0] = '\0';
    uint8_t* out = NULL;
    if (!jpegIs(data, size)) {
        fail(d, "not a JPEG file");
    }
    size_t pos = 2;
    int done = 0;
    while (!d->failed && !done) {
        /* the next marker (anything else between segments is skipped) */
        while (pos < size && data[pos] != 0xFF) {
            pos++;
        }
        while (pos < size && data[pos] == 0xFF) {
            pos++;
        }
        if (pos >= size) {
            break; /* no EOI: the picture so far */
        }
        int m = data[pos++];
        if (m == 0x00 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xD8) {
            continue; /* no segment */
        }
        if (m == 0xD9) {
            done = 1;
            break;
        }
        if (pos + 2 > size) {
            break;
        }
        unsigned len = get16(data + pos);
        if (len < 2 || pos + len > size) {
            fail(d, "a marker segment runs past the end");
            break;
        }
        const uint8_t* s = data + pos + 2;
        unsigned n = len - 2;
        pos += len;
        switch (m) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
            frameHeader(d, s, n, m);
            break;
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            fail(d, "lossless or hierarchical JPEG (SOF%d) is not supported", m - 0xC0);
            break;
        case 0xC9:
        case 0xCA:
            fail(d, "arithmetic-coded JPEG is not supported");
            break;
        case 0xC4:
            huffmanTables(d, s, n);
            break;
        case 0xCC:
            fail(d, "arithmetic-coded JPEG is not supported");
            break;
        case 0xDB:
            quantTables(d, s, n);
            break;
        case 0xDD:
            if (n < 2) {
                fail(d, "a restart interval cut short");
            } else {
                d->restart = (int)get16(s);
            }
            break;
        case 0xDA:
            startOfScan(d, s, n, &pos);
            break;
        case 0xDC:
            fail(d, "a height given later (DNL) is not supported");
            break;
        case 0xEE:
            if (n >= 12 && memcmp(s, "Adobe", 5) == 0) {
                d->adobe = 1;
                d->adobeTransform = s[11];
            }
            break;
        default:
            break; /* APPn, COM and others: skipped */
        }
    }
    if (!d->failed && d->frame && d->scans > 0) {
        out = picture(d);
        *w = d->width;
        *h = d->height;
    } else if (!d->failed) {
        fail(d, d->frame ? "no scan" : "no frame");
    }
    for (int i = 0; i < 4; i++) {
        free(d->comp[i].coef);
    }
    free(d);
    return out;
}
