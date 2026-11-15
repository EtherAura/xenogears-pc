/*
 * pngfastfuzz: the stored-deflate PNG readers of port/src/gpu/pngfast.c
 * (pngDecodeStored, pngDecodeStoredGray; xg-jr5) under AddressSanitizer and
 * UndefinedBehaviorSanitizer, built by tools/test_pngfast_fuzz.py with the
 * real pngfast.c. Community packs bring third-party PNGs to these parsers.
 *
 *     pngfastfuzz SEED ITERATIONS REPRO   directed cases, then the campaign
 *     pngfastfuzz replay FILE             one file through both readers
 *
 * Every PNG here is made in code (PNG specification, W3C third edition;
 * zlib RFC 1950; stored deflate blocks RFC 1951 3.2.4): RGB, RGBA and
 * greyscale images with every row filter, stored blocks and IDATs split at
 * many sizes. Directed cases check decodes pixel for pixel and walk the
 * failure paths (truncation at every byte, chunk lengths, IHDR fields and
 * the maxSide/maxPixels limits, the zlib header, LEN/NLEN, BTYPE, BFINAL,
 * filter bytes); a fixed-seed mutation campaign then damages those images
 * and checks the readers' contract on whatever comes back. Each input is
 * passed in a heap block of exactly its size, so a read past the end is an
 * AddressSanitizer report. CRCs and the Adler-32 are not checked by the
 * readers (pngfast.c): damaged ones are only required to decode safely.
 * Limits stay small (maxPixels at most 2^22), so no case allocates more
 * than a few megabytes.
 *
 * Exit 0 and a "pngfastfuzz: PASS" line when everything held. When a
 * sanitizer stops the run, the input being decoded is written to REPRO.
 */

#include "gpu/pngfast.h"

#include <limits.h>
#include <sanitizer/common_interface_defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define FUZZ_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FUZZ_ASAN 1
#endif
#endif
#ifndef FUZZ_ASAN
#error "pngfastfuzz is built with -fsanitize=address,undefined (tools/test_pngfast_fuzz.py)"
#endif

#ifndef PNGFASTFUZZ_CONTROL
#define PNGFASTFUZZ_CONTROL 0 /* the driver's negative controls (1 to 6), never in the real run */
#endif

#if defined(VERSION_JP)
#define FUZZ_VERSION "JP"
#else
#define FUZZ_VERSION "US"
#endif

#define SIDE 4096
#define PIXELS (1u << 22)

static int failures;
static char label[160];
static unsigned long long calls[2], decoded[2], directed;

#define EXPECT(c)                                                                                  \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, label, #c);               \
        }                                                                                          \
    } while (0)

static void die(const char* what) {
    fprintf(stderr, "pngfastfuzz: %s\n", what);
    exit(2);
}

/* ---- the input being decoded, for a sanitizer's report -------------------------------------- */

static const uint8_t* curIn;
static size_t curN;
static int curGray;
static const char* reproPath;

static void onDeath(void) {
    if (curIn == NULL) {
        fprintf(stderr, "pngfastfuzz: stopped outside a decoder call, after [%s]\n", label);
        return;
    }
    fprintf(stderr, "pngfastfuzz: stopped in %s on [%s] (%zu bytes)\n",
            curGray ? "pngDecodeStoredGray" : "pngDecodeStored", label, curN);
    if (reproPath != NULL) {
        FILE* f = fopen(reproPath, "wb");
        if (f != NULL) {
            fwrite(curIn, 1, curN, f);
            fclose(f);
            fprintf(stderr, "pngfastfuzz: reproducer written to %s\n", reproPath);
        }
    }
}

/* ---- PNG making -------------------------------------------------------------------------------- */

typedef struct {
    uint8_t* p;
    size_t n, cap;
} Buf;

static void bufPut(Buf* b, const void* d, size_t n) {
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->n + n) c *= 2;
        b->p = realloc(b->p, c);
        if (b->p == NULL) die("out of memory");
        b->cap = c;
    }
    if (n > 0) memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void bufByte(Buf* b, uint8_t v) {
    bufPut(b, &v, 1);
}

static void be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void bufBe32(Buf* b, uint32_t v) {
    uint8_t q[4];
    be32(q, v);
    bufPut(b, q, 4);
}

/* CRC-32 of the PNG specification (polynomial 0xEDB88320, bit at a time). */
static uint32_t crcAdd(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

/* The roles of the zlib stream's bytes, for edits aimed at one field. */
enum { R_ZHDR, R_BTYPE, R_LEN, R_NLEN, R_FILTER, R_DATA, R_ADLER, R_COUNT };

typedef struct {
    int w, h, type;   /* type 0 grey, 2 RGB, 6 RGBA */
    int filterStart;  /* row y has filter (y + filterStart) % 5 ... */
    int filterFixed;  /* ... or this one (>= 0) */
    size_t blockSize; /* stored block size (0: 65535) */
    size_t idatSize;  /* IDAT data size (0: one IDAT) */
    int zeroBlocks;   /* an empty stored block between blocks */
    int ancillary;    /* a tEXt between IDATs */
    int emptyIdat;    /* an empty IDAT between IDATs */
    int badCrc;       /* the IDATs' CRCs wrong */
    int noFinal;      /* no block has BFINAL */
    unsigned seed;    /* the pixels */
} Spec;

typedef struct {
    Buf f;
    size_t* zoff; /* file offset of each zlib byte */
    uint8_t* zrole;
    size_t zn;
    size_t* chunk; /* chunk starts */
    size_t nChunk, chunkCap;
    size_t dataEnd; /* end of the chunk holding the last raw byte */
    uint8_t* px;    /* the pixels as made (1, 3 or 4 bytes each) */
    int w, h, type, ch;
} Png;

static void chunkAt(Png* p, size_t at) {
    if (p->nChunk == p->chunkCap) {
        p->chunkCap = p->chunkCap ? p->chunkCap * 2 : 16;
        p->chunk = realloc(p->chunk, p->chunkCap * sizeof(*p->chunk));
        if (p->chunk == NULL) die("out of memory");
    }
    p->chunk[p->nChunk++] = at;
}

/* A chunk; its data's offset. */
static size_t chunkOut(Png* p, const char* type, const uint8_t* d, size_t n, int badCrc) {
    size_t at = p->f.n;
    chunkAt(p, at);
    bufBe32(&p->f, (uint32_t)n);
    bufPut(&p->f, type, 4);
    bufPut(&p->f, d, n);
    uint32_t c = crcAdd(crcAdd(0, (const uint8_t*)type, 4), d, n);
    bufBe32(&p->f, badCrc ? c ^ 0x5A5A5A5Au : c);
    return at + 8;
}

static int predict(int filter, int a, int b, int c) {
    switch (filter) {
    case 1:
        return a;
    case 2:
        return b;
    case 3:
        return (a + b) >> 1;
    case 4: {
        int q = a + b - c, pa = abs(q - a), pb = abs(q - b), pc = abs(q - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }
    default:
        return 0;
    }
}

static void build(Png* p, const Spec* s) {
    memset(p, 0, sizeof(*p));
    p->w = s->w, p->h = s->h, p->type = s->type;
    p->ch = s->type == 0 ? 1 : s->type == 2 ? 3 : 4;
    size_t bytes = (size_t)s->w * p->ch, row = bytes + 1, rawLen = row * s->h;
    p->px = malloc(bytes * s->h);
    uint8_t* raw = malloc(rawLen);
    if (p->px == NULL || raw == NULL) die("out of memory");
    for (size_t i = 0; i < bytes * s->h; i++) {
        unsigned v = (unsigned)i * 151u + (unsigned)(i / 7) * 29u + s->seed * 83u;
        p->px[i] = (uint8_t)(i % 11 == 0 ? 0 : i % 13 == 0 ? 255 : v ^ v >> 8);
    }
    for (int y = 0; y < s->h; y++) {
        int filter = s->filterFixed >= 0 ? s->filterFixed : (y + s->filterStart) % 5;
        const uint8_t* cur = p->px + (size_t)y * bytes;
        const uint8_t* up = y > 0 ? cur - bytes : NULL;
        raw[(size_t)y * row] = (uint8_t)filter;
        for (size_t x = 0; x < bytes; x++) {
            int a = x >= (size_t)p->ch ? cur[x - p->ch] : 0, b = up ? up[x] : 0;
            int c = up && x >= (size_t)p->ch ? up[x - p->ch] : 0;
            raw[(size_t)y * row + 1 + x] = (uint8_t)(cur[x] - predict(filter, a, b, c));
        }
    }
    /* the zlib stream: header, stored blocks, Adler-32 */
    Buf z = {0}, role = {0};
    bufByte(&z, 0x78), bufByte(&role, R_ZHDR);
    bufByte(&z, 0x01), bufByte(&role, R_ZHDR);
    size_t bs = s->blockSize ? s->blockSize : 65535, done = 0, lastRaw = 0;
    do {
        if (done > 0 && s->zeroBlocks) {
            static const uint8_t empty[5] = {0, 0, 0, 0xFF, 0xFF};
            static const uint8_t roles[5] = {R_BTYPE, R_LEN, R_LEN, R_NLEN, R_NLEN};
            bufPut(&z, empty, 5), bufPut(&role, roles, 5);
        }
        size_t n = rawLen - done < bs ? rawLen - done : bs;
        int fin = done + n == rawLen && !s->noFinal;
        bufByte(&z, (uint8_t)fin), bufByte(&role, R_BTYPE);
        bufByte(&z, (uint8_t)n), bufByte(&role, R_LEN);
        bufByte(&z, (uint8_t)(n >> 8)), bufByte(&role, R_LEN);
        bufByte(&z, (uint8_t)~n), bufByte(&role, R_NLEN);
        bufByte(&z, (uint8_t)(~n >> 8)), bufByte(&role, R_NLEN);
        for (size_t i = 0; i < n; i++) {
            lastRaw = z.n;
            bufByte(&z, raw[done + i]), bufByte(&role, (done + i) % row == 0 ? R_FILTER : R_DATA);
        }
        done += n;
    } while (done < rawLen);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawLen; i++) a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    for (int k = 3; k >= 0; k--) bufByte(&z, (uint8_t)((b << 16 | a) >> (k * 8))), bufByte(&role, R_ADLER);
    free(raw);
    /* the file */
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bufPut(&p->f, sig, 8);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)s->w), be32(ihdr + 4, (uint32_t)s->h);
    ihdr[8] = 8, ihdr[9] = (uint8_t)s->type, ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunkOut(p, "IHDR", ihdr, 13, 0);
    p->zn = z.n;
    p->zoff = malloc(z.n * sizeof(*p->zoff));
    p->zrole = role.p;
    if (p->zoff == NULL) die("out of memory");
    size_t per = s->idatSize ? s->idatSize : z.n;
    for (size_t o = 0; o < z.n; o += per) {
        size_t k = z.n - o < per ? z.n - o : per;
        if (o > 0 && s->ancillary) chunkOut(p, "tEXt", (const uint8_t*)"Comment\0xg-jr5", 14, 0);
        if (o > 0 && s->emptyIdat) chunkOut(p, "IDAT", NULL, 0, s->badCrc);
        size_t d = chunkOut(p, "IDAT", z.p + o, k, s->badCrc);
        for (size_t j = 0; j < k; j++) p->zoff[o + j] = d + j;
        if (lastRaw >= o && lastRaw < o + k) p->dataEnd = d + k + 4;
    }
    chunkOut(p, "IEND", NULL, 0, 0);
    free(z.p);
}

static void drop(Png* p) {
    free(p->f.p), free(p->zoff), free(p->zrole), free(p->chunk), free(p->px);
}

/* The file offset of the ordinal-th zlib byte of a role (SIZE_MAX: none). */
static size_t roleOff(const Png* p, int role, size_t ordinal) {
    for (size_t i = 0; i < p->zn; i++) {
        if (p->zrole[i] == role && ordinal-- == 0) return p->zoff[i];
    }
    return SIZE_MAX;
}

static size_t roleCount(const Png* p, int role) {
    size_t n = 0;
    for (size_t i = 0; i < p->zn; i++) n += p->zrole[i] == role;
    return n;
}

/* ---- decoding through the real API ----------------------------------------------------------- */

typedef struct {
    uint8_t* px;
    int w, h, other;
} Res;

/* One call, with the input in a heap block of exactly its size, and the
 * contract every call keeps: *other 0 or 1; a result is sized within the
 * limits (every byte of it read here) and *other is 0. */
static Res decode(int gray, const uint8_t* f, size_t n, int maxSide, uint64_t maxPixels) {
    uint8_t* copy = malloc(n ? n : 1);
    if (copy == NULL) die("out of memory");
    if (n > 0) memcpy(copy, f, n);
    curIn = copy, curN = n, curGray = gray;
    Res r = {NULL, -1, -1, -1};
    r.px = (gray ? pngDecodeStoredGray : pngDecodeStored)(copy, n, &r.w, &r.h, maxSide, maxPixels, &r.other);
    curIn = NULL;
    free(copy);
    calls[gray]++;
    EXPECT(r.other == 0 || r.other == 1);
    if (r.px != NULL) {
        decoded[gray]++;
        EXPECT(r.other == 0);
        int sized = r.w >= 1 && r.h >= 1 && (uint32_t)r.w <= (uint32_t)maxSide &&
                    (uint64_t)r.w * (uint64_t)r.h <= maxPixels;
        EXPECT(sized);
        if (sized) {
            volatile unsigned sum = 0;
            size_t bytes = (size_t)r.w * (size_t)r.h * (gray ? 1 : 4);
            for (size_t i = 0; i < bytes; i++) sum += r.px[i];
            (void)sum;
        }
    }
    return r;
}

static int samePixels(const Png* p, const Res* r, int gray) {
    if (r->px == NULL || r->w != p->w || r->h != p->h) return 0;
    size_t n = (size_t)p->w * p->h;
    if (gray) return memcmp(r->px, p->px, n) == 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* s = p->px + i * p->ch;
        const uint8_t* o = r->px + i * 4;
        if (o[0] != s[0] || o[1] != s[1] || o[2] != s[2] || o[3] != (p->ch == 4 ? s[3] : 255)) return 0;
    }
    return 1;
}

/* Decodes to exactly the image made. */
static void expectImage(const Png* p, const uint8_t* f, size_t n, int maxSide, uint64_t maxPixels) {
    int gray = p->type == 0;
    Res r = decode(gray, f, n, maxSide, maxPixels);
    EXPECT(r.px != NULL && r.other == 0);
    EXPECT(samePixels(p, &r, gray));
    free(r.px);
    directed++;
}

/* NULL; *other as wanted (-1: either). */
static void expectNull(int gray, const uint8_t* f, size_t n, int maxSide, uint64_t maxPixels, int other) {
    Res r = decode(gray, f, n, maxSide, maxPixels);
    EXPECT(r.px == NULL);
    EXPECT(other < 0 || r.other == other);
    free(r.px);
    directed++;
}

/* NULL, or exactly the image made (for what the readers do not check). */
static void expectSafe(const Png* p, const uint8_t* f, size_t n, int* gotImage) {
    int gray = p->type == 0;
    Res r = decode(gray, f, n, SIDE, PIXELS);
    EXPECT(r.px == NULL || samePixels(p, &r, gray));
    if (gotImage != NULL) *gotImage += r.px != NULL;
    free(r.px);
    directed++;
}

/* Only the contract (decode's checks), for damage whose outcome the walk's
 * misalignment decides. */
static void probe(int gray, const uint8_t* f, size_t n) {
    Res r = decode(gray, f, n, SIDE, PIXELS);
    free(r.px);
    directed++;
}

/* A working copy of a PNG's file. */
static uint8_t* dup(const Png* p) {
    uint8_t* d = malloc(p->f.n);
    if (d == NULL) die("out of memory");
    memcpy(d, p->f.p, p->f.n);
    return d;
}

static Spec spec(int w, int h, int type) {
    Spec s = {w, h, type, 0, -1, 0, 0, 0, 0, 0, 0, 0, 1};
    return s;
}

/* ---- directed cases ---------------------------------------------------------------------------- */

static void validImages(void) {
    static const int types[3] = {0, 2, 6};
    static const int dims[][2] = {{1, 1}, {3, 2}, {2, 7}, {17, 5}, {64, 3}};
    for (int t = 0; t < 3; t++) {
        for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); d++) {
            for (int v = 0; v < 6; v++) {
                Spec s = spec(dims[d][0], dims[d][1], types[t]);
                s.filterStart = v % 5, s.seed = (unsigned)(t * 31 + d * 7 + v);
                if (v == 1) s.blockSize = 1, s.idatSize = 1;
                if (v == 2) s.blockSize = 7, s.idatSize = 5, s.zeroBlocks = s.ancillary = s.emptyIdat = 1;
                if (v == 3) s.blockSize = 3, s.idatSize = 2, s.zeroBlocks = 1;
                if (v == 4) s.filterFixed = 4, s.idatSize = 3, s.ancillary = 1;
                if (v == 5) s.filterFixed = 3, s.blockSize = 2;
                Png p;
                build(&p, &s);
                snprintf(label, sizeof(label), "valid type %d %dx%d variant %d", s.type, s.w, s.h, v);
                expectImage(&p, p.f.p, p.f.n, SIDE, PIXELS);
                /* the other reader: a valid PNG of another kind */
                expectNull(s.type != 0, p.f.p, p.f.n, SIDE, PIXELS, 1);
                drop(&p);
            }
        }
    }
    /* more than one full 65535-byte block */
    static const int big[][3] = {{300, 250, 0}, {200, 100, 6}, {120, 190, 2}};
    for (int i = 0; i < 3; i++) {
        Spec s = spec(big[i][0], big[i][1], big[i][2]);
        s.idatSize = 8192, s.seed = 99u + (unsigned)i;
        Png p;
        build(&p, &s);
        snprintf(label, sizeof(label), "multi-block type %d %dx%d", s.type, s.w, s.h);
        EXPECT(roleCount(&p, R_BTYPE) >= 2);
        expectImage(&p, p.f.p, p.f.n, SIDE, PIXELS);
        drop(&p);
    }
}

static void truncation(void) {
    static const int cases[][3] = {{3, 2, 0}, {4, 3, 2}, {2, 2, 6}, {5, 4, 6}};
    for (int c = 0; c < 4; c++) {
        Spec s = spec(cases[c][0], cases[c][1], cases[c][2]);
        s.blockSize = 5, s.idatSize = 7, s.zeroBlocks = c & 1, s.ancillary = c >> 1;
        Png p;
        build(&p, &s);
        int images = 0;
        for (size_t cut = 0; cut < p.f.n; cut++) {
            snprintf(label, sizeof(label), "truncated type %d at %zu of %zu", s.type, cut, p.f.n);
            if (cut < p.dataEnd) {
                expectNull(0, p.f.p, cut, SIDE, PIXELS, -1);
                expectNull(1, p.f.p, cut, SIDE, PIXELS, -1);
            } else {
                expectSafe(&p, p.f.p, cut, &images);
            }
        }
        drop(&p);
    }
}

static void limits(void) {
    for (int gray = 0; gray < 2; gray++) {
        int type = gray ? 0 : 2;
        Spec s = spec(16, 8, type);
        Png p;
        build(&p, &s);
        snprintf(label, sizeof(label), "limits type %d 16x8", type);
        expectImage(&p, p.f.p, p.f.n, 16, 128);
        expectImage(&p, p.f.p, p.f.n, -1, 128); /* maxSide below 0: (uint32_t) in pngfast.c, no side limit */
        expectNull(gray, p.f.p, p.f.n, 15, 128, 0);
        expectNull(gray, p.f.p, p.f.n, 16, 127, 0);
        expectNull(gray, p.f.p, p.f.n, 0, PIXELS, 0);
        expectNull(gray, p.f.p, p.f.n, SIDE, 0, 0);
        drop(&p);
        Spec t = spec(8, 16, type);
        build(&p, &t);
        snprintf(label, sizeof(label), "limits type %d 8x16", type);
        expectImage(&p, p.f.p, p.f.n, 16, 128);
        expectNull(gray, p.f.p, p.f.n, 15, PIXELS, 0);
        /* the IHDR size against limits; the data stays 8x16 */
        static const uint32_t sizes[][2] = {{0, 16},
                                            {8, 0},
                                            {0, 0},
                                            {0xFFFFFFFFu, 0xFFFFFFFFu},
                                            {0x80000000u, 1},
                                            {1, 0x80000000u},
                                            {0x7FFFFFFFu, 0x7FFFFFFFu},
                                            {0x7FFFFFFFu, 1},
                                            {65536, 65536},
                                            {4097, 1},
                                            {1, 4097},
                                            {2048, 2049}};
        uint8_t* f = dup(&p);
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            be32(f + 16, sizes[i][0]), be32(f + 20, sizes[i][1]);
            snprintf(label, sizeof(label), "IHDR %ux%u type %d", sizes[i][0], sizes[i][1], type);
            expectNull(gray, f, p.f.n, SIDE, PIXELS, 0);
            expectNull(gray, f, p.f.n, INT_MAX, PIXELS, 0);
            expectNull(gray, f, p.f.n, -1, 1u << 20, 0);
        }
        /* sizes within the limits that the data does not fill, or overfills */
        static const uint32_t wrong[][2] = {{8, 17}, {9, 16}, {64, 64}, {8, 15}, {7, 16}, {1, 1}};
        for (size_t i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++) {
            be32(f + 16, wrong[i][0]), be32(f + 20, wrong[i][1]);
            snprintf(label, sizeof(label), "IHDR %ux%u for 8x16 data, type %d", wrong[i][0], wrong[i][1], type);
            expectNull(gray, f, p.f.n, SIDE, PIXELS, -1);
        }
        free(f);
        drop(&p);
    }
}

static void headers(void) {
    for (int gray = 0; gray < 2; gray++) {
        Spec s = spec(4, 3, gray ? 0 : 6);
        Png p;
        build(&p, &s);
        uint8_t* f = dup(&p);
        size_t n = p.f.n;
#define RESET() memcpy(f, p.f.p, n)
        snprintf(label, sizeof(label), "headers %s", gray ? "grey" : "RGBA");
        for (size_t cut = 0; cut <= 33; cut++) expectNull(gray, f, cut, SIDE, PIXELS, 0);
        for (int i = 0; i < 8; i++) {
            RESET();
            f[i] ^= 0x20;
            snprintf(label, sizeof(label), "signature byte %d, %s", i, gray ? "grey" : "RGBA");
            expectNull(gray, f, n, SIDE, PIXELS, 0);
        }
        static const uint32_t ihdrLen[] = {0, 12, 14, 0xFFFFFFFFu};
        for (size_t i = 0; i < 4; i++) {
            RESET();
            be32(f + 8, ihdrLen[i]);
            snprintf(label, sizeof(label), "IHDR length %u", ihdrLen[i]);
            expectNull(gray, f, n, SIDE, PIXELS, 0);
        }
        RESET();
        f[15] = 'X';
        snprintf(label, sizeof(label), "IHDR type IHDX");
        expectNull(gray, f, n, SIDE, PIXELS, 0);
        /* other kinds of PNG: *other 1 */
        static const int fields[][2] = {{24, 16}, {24, 1}, {24, 4}, {24, 0}, {25, 3}, {25, 4}, {25, 2},
                                        {25, 6}, {25, 0}, {26, 1}, {27, 1}, {28, 1}, {28, 0xFF}};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            RESET();
            if (f[fields[i][0]] == fields[i][1]) continue; /* the image's own value */
            f[fields[i][0]] = (uint8_t)fields[i][1];
            be32(f + 29, crcAdd(0, f + 12, 17));
            snprintf(label, sizeof(label), "IHDR byte %d = %d, %s", fields[i][0], fields[i][1], gray ? "grey" : "RGBA");
            /* RGBA data called RGB is this reader's kind, of the wrong size */
            int sameKind = !gray && fields[i][0] == 25 && fields[i][1] == 2;
            expectNull(gray, f, n, SIDE, PIXELS, sameKind ? 0 : 1);
        }
        /* the zlib header */
        size_t z0 = roleOff(&p, R_ZHDR, 0), z1 = roleOff(&p, R_ZHDR, 1);
        static const uint8_t zh[][2] = {{0x79, 0x01}, {0x77, 0x01}, {0x78, 0x02}, {0x78, 0x00},
                                        {0x78, 0xBB}, {0x08, 0x1E}, {0x18, 0x1A}, {0x00, 0x00}};
        for (size_t i = 0; i < sizeof(zh) / sizeof(zh[0]); i++) {
            RESET();
            f[z0] = zh[i][0], f[z1] = zh[i][1];
            snprintf(label, sizeof(label), "zlib header %02X %02X", zh[i][0], zh[i][1]);
            expectNull(gray, f, n, SIDE, PIXELS, 0);
        }
        /* other valid zlib headers (window sizes, levels): still stored */
        static const uint8_t zok[][2] = {{0x78, 0x9C}, {0x78, 0xDA}, {0x68, 0x81}, {0x08, 0x1D}, {0x18, 0x19}};
        for (size_t i = 0; i < sizeof(zok) / sizeof(zok[0]); i++) {
            RESET();
            f[z0] = zok[i][0], f[z1] = zok[i][1];
            snprintf(label, sizeof(label), "zlib header %02X %02X (valid)", zok[i][0], zok[i][1]);
            expectImage(&p, f, n, SIDE, PIXELS);
        }
#undef RESET
        free(f);
        drop(&p);
    }
}

static void chunks(void) {
    for (int gray = 0; gray < 2; gray++) {
        Spec s = spec(5, 4, gray ? 0 : 2);
        s.blockSize = 9, s.idatSize = 11, s.ancillary = 1, s.emptyIdat = 1;
        Png p;
        build(&p, &s);
        uint8_t* f = dup(&p);
        size_t n = p.f.n;
        /* every chunk length (IHDR's aside) too long, past the file, or huge */
        for (size_t c = 1; c < p.nChunk; c++) {
            size_t at = p.chunk[c];
            uint32_t len = (uint32_t)(f[at] << 24 | f[at + 1] << 16 | f[at + 2] << 8 | f[at + 3]);
            int isData = at + 12 <= p.dataEnd;
            const uint32_t bad[] = {(uint32_t)(n - at - 12 + 1), 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
                                    (uint32_t)(n - at - 12), len + 1, len ? len - 1 : 3};
            for (size_t b = 0; b < sizeof(bad) / sizeof(bad[0]); b++) {
                memcpy(f, p.f.p, n);
                be32(f + at, bad[b]);
                snprintf(label, sizeof(label), "chunk %zu (%.4s) length %u, %s", c, f + at + 4, bad[b],
                         gray ? "grey" : "RGB");
                if (isData && b < 4) {
                    expectNull(gray, f, n, SIDE, PIXELS, -1); /* past the file: damaged */
                } else {
                    probe(gray, f, n);
                }
            }
        }
        /* CRCs are not checked (pngfast.c): decoded safely either way */
        snprintf(label, sizeof(label), "IDAT CRCs wrong, %s", gray ? "grey" : "RGB");
        Spec sc = s;
        sc.badCrc = 1;
        Png q;
        build(&q, &sc);
        expectSafe(&q, q.f.p, q.f.n, NULL);
        drop(&q);
        for (size_t c = 0; c < p.nChunk; c++) {
            memcpy(f, p.f.p, n);
            size_t at = p.chunk[c];
            uint32_t len = (uint32_t)(f[at] << 24 | f[at + 1] << 16 | f[at + 2] << 8 | f[at + 3]);
            f[at + 8 + len] ^= 0xFF;
            snprintf(label, sizeof(label), "chunk %zu CRC wrong", c);
            expectSafe(&p, f, n, NULL);
        }
        /* IEND before the IDATs; no IDAT; an IDAT renamed */
        Buf b = {0};
        bufPut(&b, p.f.p, 33);
        bufPut(&b, p.f.p + n - 12, 12);
        bufPut(&b, p.f.p + 33, n - 33);
        snprintf(label, sizeof(label), "IEND first, %s", gray ? "grey" : "RGB");
        expectNull(gray, b.p, b.n, SIDE, PIXELS, 0);
        snprintf(label, sizeof(label), "no IDAT");
        expectNull(gray, b.p, 45, SIDE, PIXELS, 0);
        free(b.p);
        for (size_t c = 1; c + 1 < p.nChunk; c++) {
            memcpy(f, p.f.p, n);
            size_t at = p.chunk[c];
            if (memcmp(f + at + 4, "IDAT", 4) != 0 || at + 12 >= p.dataEnd) continue;
            if (f[at] == 0 && f[at + 1] == 0 && f[at + 2] == 0 && f[at + 3] == 0) continue;
            f[at + 7] = 'X'; /* IDAX: skipped as an unknown chunk, its bytes missing */
            snprintf(label, sizeof(label), "IDAT %zu renamed", c);
            probe(gray, f, n);
        }
        /* bytes after IEND are not read */
        Buf t = {0};
        bufPut(&t, p.f.p, n);
        bufPut(&t, "\xFF\xFF\xFF\xFFIDAT", 8);
        snprintf(label, sizeof(label), "bytes after IEND");
        expectImage(&p, t.p, t.n, SIDE, PIXELS);
        free(t.p);
        free(f);
        drop(&p);
    }
}

static void blocks(void) {
    for (int gray = 0; gray < 2; gray++) {
        Spec s = spec(6, 5, gray ? 0 : 6);
        s.blockSize = 13, s.idatSize = 10, s.zeroBlocks = 1;
        Png p;
        build(&p, &s);
        uint8_t* f = dup(&p);
        size_t n = p.f.n, nb = roleCount(&p, R_BTYPE);
        for (size_t k = 0; k < nb; k++) {
            size_t bt = roleOff(&p, R_BTYPE, k);
            size_t len0 = roleOff(&p, R_LEN, 2 * k), len1 = roleOff(&p, R_LEN, 2 * k + 1);
            size_t nlen0 = roleOff(&p, R_NLEN, 2 * k), nlen1 = roleOff(&p, R_NLEN, 2 * k + 1);
            uint8_t fin = p.f.p[bt] & 1;
            /* compressed blocks: inflate needed */
            for (int type = 1; type < 4; type++) {
                memcpy(f, p.f.p, n);
                f[bt] = (uint8_t)(fin | type << 1);
                snprintf(label, sizeof(label), "block %zu BTYPE %d, %s", k, type, gray ? "grey" : "RGBA");
                expectNull(gray, f, n, SIDE, PIXELS, 1);
            }
            /* bits above BTYPE belong to the block's data: ignored */
            memcpy(f, p.f.p, n);
            f[bt] = (uint8_t)(fin | 0xF8);
            snprintf(label, sizeof(label), "block %zu header high bits", k);
            expectImage(&p, f, n, SIDE, PIXELS);
            /* NLEN not LEN's complement */
            static const uint8_t flips[] = {0x01, 0x80, 0xFF};
            for (size_t i = 0; i < 3; i++) {
                memcpy(f, p.f.p, n);
                f[nlen0] ^= flips[i];
                snprintf(label, sizeof(label), "block %zu NLEN low ^%02X", k, flips[i]);
                expectNull(gray, f, n, SIDE, PIXELS, 0);
                memcpy(f, p.f.p, n);
                f[nlen1] ^= flips[i];
                snprintf(label, sizeof(label), "block %zu NLEN high ^%02X", k, flips[i]);
                expectNull(gray, f, n, SIDE, PIXELS, 0);
                memcpy(f, p.f.p, n);
                f[len0] ^= flips[i];
                snprintf(label, sizeof(label), "block %zu LEN low ^%02X", k, flips[i]);
                expectNull(gray, f, n, SIDE, PIXELS, 0);
            }
            /* LEN and NLEN changed together: the walk misaligns, or runs past the image */
            static const unsigned lens[] = {0, 1, 0xFFFF, 0x8000, 0x7FFF};
            for (size_t i = 0; i < 5; i++) {
                memcpy(f, p.f.p, n);
                f[len0] = (uint8_t)lens[i], f[len1] = (uint8_t)(lens[i] >> 8);
                f[nlen0] = (uint8_t)~lens[i], f[nlen1] = (uint8_t)(~lens[i] >> 8);
                snprintf(label, sizeof(label), "block %zu LEN %u", k, lens[i]);
                probe(gray, f, n);
            }
            /* BFINAL early: the rest is never read */
            if (k + 1 < nb && (p.f.p[len0] | p.f.p[len1]) != 0) {
                memcpy(f, p.f.p, n);
                f[bt] |= 1;
                snprintf(label, sizeof(label), "block %zu BFINAL early", k);
                expectNull(gray, f, n, SIDE, PIXELS, 0);
            }
        }
        /* no BFINAL: the Adler-32 read as a block header */
        Spec sn = s;
        sn.noFinal = 1;
        Png q;
        build(&q, &sn);
        snprintf(label, sizeof(label), "no BFINAL, %s", gray ? "grey" : "RGBA");
        expectSafe(&q, q.f.p, q.f.n, NULL);
        drop(&q);
        /* the Adler-32 wrong or missing: not checked (pngfast.c) */
        for (size_t i = 0; i < 4; i++) {
            memcpy(f, p.f.p, n);
            f[roleOff(&p, R_ADLER, i)] ^= 0x10;
            snprintf(label, sizeof(label), "Adler-32 byte %zu wrong", i);
            expectSafe(&p, f, n, NULL);
        }
        free(f);
        drop(&p);
    }
}

static void filters(void) {
    for (int gray = 0; gray < 2; gray++) {
        Spec s = spec(7, 4, gray ? 0 : 2);
        s.blockSize = 6;
        Png p;
        build(&p, &s);
        uint8_t* f = dup(&p);
        size_t n = p.f.n;
        static const uint8_t bad[] = {5, 6, 0x7F, 0x80, 0xFF};
        for (int y = 0; y < s.h; y++) {
            size_t at = roleOff(&p, R_FILTER, (size_t)y);
            for (size_t i = 0; i < sizeof(bad); i++) {
                memcpy(f, p.f.p, n);
                f[at] = bad[i];
                snprintf(label, sizeof(label), "row %d filter %d, %s", y, bad[i], gray ? "grey" : "RGB");
                expectNull(gray, f, n, SIDE, PIXELS, 0);
            }
            /* another valid filter: decodes, to other pixels */
            for (int k = 0; k < 5; k++) {
                memcpy(f, p.f.p, n);
                f[at] = (uint8_t)k;
                snprintf(label, sizeof(label), "row %d filter %d, %s", y, k, gray ? "grey" : "RGB");
                Res r = decode(gray, f, n, SIDE, PIXELS);
                EXPECT(r.px != NULL && r.w == s.w && r.h == s.h);
                free(r.px);
                directed++;
            }
        }
        free(f);
        drop(&p);
    }
}

/* ---- the mutation campaign --------------------------------------------------------------------- */

static uint64_t rngState;

static uint64_t rnd(void) { /* splitmix64 */
    uint64_t z = (rngState += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static size_t below(size_t n) {
    return n ? (size_t)(rnd() % n) : 0;
}

#define MAX_MUTANT 65536

static void campaign(uint64_t seed, unsigned long iterations, unsigned long long* tried) {
    static const Spec bases[] = {
        {5, 4, 2, 0, -1, 7, 9, 0, 1, 0, 0, 0, 1},   {3, 3, 6, 1, -1, 0, 0, 0, 0, 0, 0, 0, 2},
        {8, 6, 0, 2, -1, 5, 4, 1, 0, 1, 0, 0, 3},   {1, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 4},
        {1, 1, 2, 3, -1, 1, 1, 1, 1, 1, 0, 0, 5},   {16, 2, 6, 4, -1, 11, 1, 0, 0, 0, 0, 0, 6},
        {64, 64, 0, 0, -1, 1000, 700, 0, 0, 0, 0, 0, 7}, {9, 9, 2, 0, 4, 4, 3, 1, 1, 1, 0, 0, 8},
        {12, 3, 6, 2, 3, 0, 5, 0, 0, 0, 1, 0, 9},   {4, 4, 0, 1, -1, 3, 2, 0, 0, 0, 0, 1, 10},
    };
    enum { NB = sizeof(bases) / sizeof(bases[0]) };
    Png png[NB];
    for (int i = 0; i < NB; i++) build(&png[i], &bases[i]);
    static const uint8_t interesting8[] = {0, 1, 2, 4, 5, 6, 7, 8, 0x0F, 0x10, 0x7F, 0x80, 0xFE, 0xFF};
    uint8_t* m = malloc(MAX_MUTANT);
    if (m == NULL) die("out of memory");
    rngState = seed;
    for (unsigned long it = 0; it < iterations; it++) {
        const Png* p = &png[below(NB)];
        size_t n = p->f.n;
        memcpy(m, p->f.p, n);
        int edits = 1 + (int)below(6);
        for (int e = 0; e < edits && n > 0; e++) {
            switch (below(11)) {
            case 0: /* a bit */
                m[below(n)] ^= (uint8_t)(1u << below(8));
                break;
            case 1: /* a byte */
                m[below(n)] = below(3) ? interesting8[below(sizeof(interesting8))] : (uint8_t)rnd();
                break;
            case 2: { /* a byte of one zlib field: header, BTYPE, LEN, NLEN, filter, data, Adler-32 */
                int role = (int)below(R_COUNT);
                size_t at = roleOff(p, role, below(roleCount(p, role)));
                if (at < n) m[at] = below(2) ? interesting8[below(sizeof(interesting8))] : (uint8_t)rnd();
                break;
            }
            case 3: { /* LEN and NLEN together, still complements */
                size_t k = below(roleCount(p, R_BTYPE));
                size_t at[4] = {roleOff(p, R_LEN, 2 * k), roleOff(p, R_LEN, 2 * k + 1), roleOff(p, R_NLEN, 2 * k),
                                roleOff(p, R_NLEN, 2 * k + 1)};
                unsigned len = below(2) ? (unsigned)below(0x10000) : (unsigned)below(64);
                uint8_t v[4] = {(uint8_t)len, (uint8_t)(len >> 8), (uint8_t)~len, (uint8_t)(~len >> 8)};
                for (int i = 0; i < 4; i++) {
                    if (at[i] < n) m[at[i]] = v[i];
                }
                break;
            }
            case 4: { /* a chunk length */
                size_t at = p->chunk[below(p->nChunk)];
                const uint32_t v[] = {0, 1, 12, 13, (uint32_t)n, (uint32_t)(n - (at < n ? at : 0)), 0x7FFFFFFFu,
                                      0x80000000u, 0xFFFFFFFFu, (uint32_t)rnd(), (uint32_t)below(64)};
                if (at + 4 <= n) be32(m + at, v[below(sizeof(v) / sizeof(v[0]))]);
                break;
            }
            case 5: { /* the IHDR size */
                const uint32_t v[] = {0, 1, 2, 63, 64, 65, 4096, 4097, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
                                      (uint32_t)rnd(), (uint32_t)below(80)};
                if (n >= 24) be32(m + 16 + 4 * below(2), v[below(sizeof(v) / sizeof(v[0]))]);
                break;
            }
            case 6: /* IHDR depth, colour type, methods */
                if (n >= 29) m[24 + below(5)] = interesting8[below(sizeof(interesting8))];
                break;
            case 7: /* truncated */
                n = below(n + 1);
                break;
            case 8: { /* a span deleted */
                size_t at = below(n), k = 1 + below(n - at < 32 ? n - at : 32);
                memmove(m + at, m + at + k, n - at - k);
                n -= k;
                break;
            }
            case 9: { /* a span copied in from elsewhere (chunks repeated, moved) */
                size_t from = below(n), k = 1 + below(n - from < 64 ? n - from : 64), at = below(n + 1);
                if (n + k > MAX_MUTANT) break;
                uint8_t tmp[64];
                memcpy(tmp, m + from, k);
                memmove(m + at + k, m + at, n - at);
                memcpy(m + at, tmp, k);
                n += k;
                break;
            }
            default: { /* random bytes inserted */
                size_t k = 1 + below(16), at = below(n + 1);
                if (n + k > MAX_MUTANT) break;
                memmove(m + at + k, m + at, n - at);
                for (size_t i = 0; i < k; i++) m[at + i] = (uint8_t)rnd();
                n += k;
                break;
            }
            }
        }
        snprintf(label, sizeof(label), "mutant %lu (seed 0x%llx, base %dx%d type %d, %zu bytes)", it,
                 (unsigned long long)seed, p->w, p->h, p->type, n);
        /* both readers, at limits around the image's own size */
        const struct {
            int side;
            uint64_t pixels;
        } lim[4] = {{SIDE, PIXELS}, {64, 4096}, {p->w > p->h ? p->w : p->h, (uint64_t)p->w * p->h}, {-1, 4096}};
        for (int gray = 0; gray < 2; gray++) {
            int l = (int)below(4);
            Res r = decode(gray, m, n, lim[l].side, lim[l].pixels);
            free(r.px);
            (*tried)++;
        }
    }
    free(m);
    for (int i = 0; i < NB; i++) drop(&png[i]);
}

/* ---- the driver's negative controls (built only with PNGFASTFUZZ_CONTROL) ---------------------- */

static void control(void) {
#if PNGFASTFUZZ_CONTROL
    Spec s = spec(4, 4, 6);
    Png p;
    build(&p, &s);
    snprintf(label, sizeof(label), "negative control %d", PNGFASTFUZZ_CONTROL);
    Res r = decode(0, p.f.p, p.f.n, SIDE, PIXELS);
    volatile int big = INT_MAX;
    switch (PNGFASTFUZZ_CONTROL) {
    case 1: /* a read past the decoded image */
        fprintf(stderr, "control: %d\n", r.px[(size_t)r.w * r.h * 4]);
        break;
    case 2: /* signed overflow */
        big = big + (int)p.f.n;
        break;
    case 3: /* a crash */
        abort();
    case 4: /* a decoder expectation that fails */
        EXPECT(r.px == NULL);
        break;
    case 5: /* the decoded image never freed */
        r.px = NULL;
        break;
    case 6: /* never ends */
        for (;;) big ^= 1;
    }
    (void)big;
    free(r.px);
    drop(&p);
#endif
}

/* ---- main ---------------------------------------------------------------------------------------- */

static int replay(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) die("cannot open the replay file");
    Buf b = {0};
    uint8_t chunk[4096];
    size_t k;
    while ((k = fread(chunk, 1, sizeof(chunk), f)) > 0 && b.n < MAX_MUTANT * 16) bufPut(&b, chunk, k);
    fclose(f);
    snprintf(label, sizeof(label), "replay %s", path);
    const struct {
        int side;
        uint64_t pixels;
    } lim[4] = {{SIDE, PIXELS}, {64, 4096}, {16, 256}, {-1, 4096}};
    for (int gray = 0; gray < 2; gray++) {
        for (int l = 0; l < 4; l++) {
            Res r = decode(gray, b.p, b.n, lim[l].side, lim[l].pixels);
            printf("%s limits %d/%llu: %s (%dx%d, other %d)\n", gray ? "pngDecodeStoredGray" : "pngDecodeStored",
                   lim[l].side, (unsigned long long)lim[l].pixels, r.px ? "decoded" : "NULL", r.px ? r.w : 0,
                   r.px ? r.h : 0, r.other);
            free(r.px);
        }
    }
    free(b.p);
    printf("pngfastfuzz: replay %s, %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    __sanitizer_set_death_callback(onDeath);
    if (argc == 3 && strcmp(argv[1], "replay") == 0) {
        return replay(argv[2]);
    }
    if (argc != 4) {
        fprintf(stderr, "usage: pngfastfuzz SEED ITERATIONS REPRO | pngfastfuzz replay FILE\n");
        return 2;
    }
    uint64_t seed = strtoull(argv[1], NULL, 0);
    unsigned long iterations = strtoul(argv[2], NULL, 0);
    reproPath = argv[3];
    if (iterations > 1000000ul) die("at most 1000000 iterations");
    control();
    validImages();
    truncation();
    limits();
    headers();
    chunks();
    blocks();
    filters();
    unsigned long long directedCalls = calls[0] + calls[1], tried = 0;
    campaign(seed, iterations, &tried);
    printf("pngfastfuzz: version %s (shared PC code; no game engine runs)\n", FUZZ_VERSION);
    printf("pngfastfuzz: directed %llu cases, %llu calls; campaign seed 0x%llx, %lu iterations, %llu calls\n",
           directed, directedCalls, (unsigned long long)seed, iterations, tried);
    printf("pngfastfuzz: pngDecodeStored %llu calls (%llu decoded), pngDecodeStoredGray %llu calls (%llu decoded)\n",
           calls[0], decoded[0], calls[1], decoded[1]);
    if (failures) {
        printf("pngfastfuzz: FAIL, %d expectation(s)\n", failures);
        return 1;
    }
    printf("pngfastfuzz: PASS\n");
    return 0;
}
