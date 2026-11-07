/*
 * Frame and VRAM dumps for tests without a window (XG_DUMP_FRAMES,
 * XG_DUMP_VRAM, XG_DUMP_PRESENT; see gpu.h), and the small PNG writer they use (deflate with
 * the fixed Huffman code and a hash-chain LZ77; 8-bit RGB, Sub filter).
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_internal.h"
#include "../system/savestate.h"

/* ---- PNG ---------------------------------------------------------------------- */

typedef struct {
    uint8_t* data;
    size_t len, cap;
    uint64_t bits;
    int nbits;
} Out;

static void outByte(Out* o, uint8_t b) {
    if (o->len == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 65536;
        o->data = realloc(o->data, o->cap);
    }
    o->data[o->len++] = b;
}

static void outBits(Out* o, uint32_t v, int n) {
    o->bits |= (uint64_t)v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        outByte(o, (uint8_t)o->bits);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

/* Huffman codes go out most significant bit first. */
static void outCode(Out* o, uint32_t code, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | ((code >> i) & 1);
    }
    outBits(o, r, n);
}

static void outLiteral(Out* o, int lit) {
    if (lit < 144) {
        outCode(o, 0x30 + lit, 8);
    } else if (lit < 256) {
        outCode(o, 0x190 + lit - 144, 9);
    } else if (lit < 280) {
        outCode(o, lit - 256, 7);
    } else {
        outCode(o, 0xC0 + lit - 280, 8);
    }
}

static const uint16_t lenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t lenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,    65,    97,    129,
                                      193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void outMatch(Out* o, int len, int dist) {
    int i = 28;
    while (lenBase[i] > len) {
        i--;
    }
    outLiteral(o, 257 + i);
    outBits(o, (uint32_t)(len - lenBase[i]), lenExtra[i]);
    int j = 29;
    while (distBase[j] > dist) {
        j--;
    }
    outCode(o, (uint32_t)j, 5);
    outBits(o, (uint32_t)(dist - distBase[j]), distExtra[j]);
}

#define HASH_BITS 15
#define WINDOW 32768
#define MAX_CHAIN 48

static void deflateFixed(Out* o, const uint8_t* in, size_t n) {
    int32_t* head = malloc(sizeof(int32_t) << HASH_BITS);
    int32_t* prev = malloc(sizeof(int32_t) * (n ? n : 1));
    memset(head, 0xFF, sizeof(int32_t) << HASH_BITS);
    outBits(o, 1, 1); /* final block */
    outBits(o, 1, 2); /* fixed Huffman */
    size_t i = 0;
    while (i < n) {
        int best = 0, bestDist = 0;
        uint32_t h = 0;
        if (i + 3 <= n) {
            h = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & ((1u << HASH_BITS) - 1);
            int32_t cand = head[h];
            int chain = MAX_CHAIN;
            size_t maxLen = n - i < 258 ? n - i : 258;
            while (cand >= 0 && i - (size_t)cand <= WINDOW && chain-- > 0) {
                size_t l = 0;
                while (l < maxLen && in[(size_t)cand + l] == in[i + l]) {
                    l++;
                }
                if ((int)l > best) {
                    best = (int)l;
                    bestDist = (int)(i - (size_t)cand);
                    if (l == maxLen) {
                        break;
                    }
                }
                cand = prev[cand];
            }
        }
        int step = best >= 3 ? best : 1;
        if (best >= 3) {
            outMatch(o, best, bestDist);
        } else {
            outLiteral(o, in[i]);
        }
        for (int k = 0; k < step; k++, i++) {
            if (i + 3 <= n) {
                uint32_t hk = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & ((1u << HASH_BITS) - 1);
                prev[i] = head[hk];
                head[hk] = (int32_t)i;
            }
        }
    }
    outLiteral(o, 256);
    if (o->nbits > 0) {
        outBits(o, 0, 8 - o->nbits);
    }
    free(head);
    free(prev);
}

static uint32_t crcTable[256];

static uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n) {
    if (crcTable[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            crcTable[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE* f, const char* type, const uint8_t* data, size_t n) {
    uint8_t hdr[8];
    put32(hdr, (uint32_t)n);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (n) {
        fwrite(data, 1, n, f);
    }
    uint32_t crc = crc32Update(crc32Update(0, (const uint8_t*)type, 4), data, n);
    uint8_t c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

int pngWrite(const char* path, int w, int h, const uint8_t* rgb) {
    size_t stride = (size_t)w * 3;
    size_t rawLen = (stride + 1) * (size_t)h;
    uint8_t* raw = malloc(rawLen ? rawLen : 1);
    for (int y = 0; y < h; y++) {
        uint8_t* row = raw + (stride + 1) * (size_t)y;
        const uint8_t* src = rgb + stride * (size_t)y;
        row[0] = 1; /* Sub */
        for (size_t x = 0; x < stride; x++) {
            row[1 + x] = (uint8_t)(src[x] - (x >= 3 ? src[x - 3] : 0));
        }
    }
    Out z = {0};
    outByte(&z, 0x78);
    outByte(&z, 0x01);
    deflateFixed(&z, raw, rawLen);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawLen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    uint8_t adler[4];
    put32(adler, b << 16 | a);
    for (int i = 0; i < 4; i++) {
        outByte(&z, adler[i]);
    }
    free(raw);

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        free(z.data);
        gpuLog("gpu: cannot write %s", path);
        return 0;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z.data, z.len);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z.data);
    return 1;
}

/* ---- the displayed picture ----------------------------------------------------------- */

static uint8_t expand5(uint32_t v) {
    return (uint8_t)(v << 3 | v >> 2);
}

void displayToRgb(const GpuDisplay* d, uint8_t* out) {
    for (int y = 0; y < d->h; y++) {
        const uint16_t* row = vramShadow[(d->y + y) & (VRAM_H - 1)];
        uint8_t* o = out + (size_t)y * d->w * 3;
        for (int x = 0; x < d->w; x++, o += 3) {
            if (!d->enabled) {
                o[0] = o[1] = o[2] = 0;
            } else if (d->rgb24) {
                for (int k = 0; k < 3; k++) {
                    int n = x * 3 + k;
                    uint16_t wd = row[(d->x + (n >> 1)) & (VRAM_W - 1)];
                    o[k] = (uint8_t)(n & 1 ? wd >> 8 : wd);
                }
            } else {
                uint16_t c = row[(d->x + x) & (VRAM_W - 1)];
                o[0] = expand5(c & 31);
                o[1] = expand5((c >> 5) & 31);
                o[2] = expand5((c >> 10) & 31);
            }
        }
    }
}

/* ---- XG_DUMP_FRAMES / XG_DUMP_VRAM --------------------------------------------------- */

static struct {
    char dir[512];
    int frames;
    int vramAll;
    uint32_t vramAt[64];
    int vramAtCount;
    uint64_t lastHash;
    int lastW, lastH;
    uint32_t range[64][3]; /* XG_DUMP_RANGE: inclusive vblank ranges, step */
    int rangeCount;
    int inRange;
    /* XG_DUMP_PRESENT */
    char presentDir[512];
    int present;
    int presentInRange;
    uint64_t presentHash;
    int presentW, presentH;
    uint64_t wideHash;
} dump;

static int rangeHas(uint32_t vblank) {
    int in = dump.rangeCount == 0;
    for (int i = 0; i < dump.rangeCount; i++) {
        in |= vblank >= dump.range[i][0] && vblank <= dump.range[i][1] &&
              (dump.range[i][2] == 0 || (vblank - dump.range[i][0]) % dump.range[i][2] == 0);
    }
    return in;
}

static uint64_t hashBytes(const uint8_t* p, size_t n) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        hash = (hash ^ p[i]) * 1099511628211ull;
    }
    return hash;
}

void dumpInit(void) {
    memset(&dump, 0, sizeof(dump));
    const char* dir = getenv("XG_DUMP_FRAMES");
    const char* vram = getenv("XG_DUMP_VRAM");
    const char* range = getenv("XG_DUMP_RANGE");
    for (const char* p = range; p != NULL && *p != '\0' && dump.rangeCount < 64;) {
        char* end;
        unsigned long lo = strtoul(p, &end, 10);
        if (end == p || *end != '-') {
            break;
        }
        p = end + 1;
        unsigned long hi = strtoul(p, &end, 10);
        if (end == p) {
            break;
        }
        unsigned long step = 0;
        if (*end == '/') {
            p = end + 1;
            step = strtoul(p, &end, 10);
        }
        dump.range[dump.rangeCount][0] = (uint32_t)lo;
        dump.range[dump.rangeCount][1] = (uint32_t)hi;
        dump.range[dump.rangeCount][2] = (uint32_t)step;
        dump.rangeCount++;
        p = *end == ',' ? end + 1 : end;
    }
    if (dir != NULL && dir[0] != '\0') {
        snprintf(dump.dir, sizeof(dump.dir), "%s", dir);
        dump.frames = 1;
    } else {
        snprintf(dump.dir, sizeof(dump.dir), ".");
    }
    if (dump.frames) {
        SDL_CreateDirectory(dump.dir);
    }
    if (vram != NULL) {
        if (strcmp(vram, "all") == 0) {
            dump.vramAll = 1;
        } else {
            const char* p = vram;
            while (*p != '\0' && dump.vramAtCount < 64) {
                char* end;
                unsigned long v = strtoul(p, &end, 10);
                if (end == p) {
                    break;
                }
                dump.vramAt[dump.vramAtCount++] = (uint32_t)v;
                p = *end == ',' ? end + 1 : end;
            }
        }
    }
    if (dump.frames) {
        gpuLog("gpu: dumping displayed frames to %s", dump.dir);
    }
    const char* present = getenv("XG_DUMP_PRESENT");
    if (present != NULL && present[0] != '\0') {
        snprintf(dump.presentDir, sizeof(dump.presentDir), "%s", present);
        dump.present = 1;
        SDL_CreateDirectory(dump.presentDir);
        gpuLog("gpu: dumping presented window images to %s", dump.presentDir);
    }
}

int dumpPresentWanted(uint32_t vblank) {
    if (!dump.present) {
        return 0;
    }
    if (!rangeHas(vblank)) {
        dump.presentInRange = 0;
        return 0;
    }
    return 1;
}

void dumpPresent(uint32_t vblank, int w, int h, const uint8_t* rgba) {
    size_t n = (size_t)w * h;
    uint8_t* rgb = malloc(n * 3 + 1);
    for (size_t i = 0; i < n; i++) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    uint64_t hash = hashBytes(rgb, n * 3);
    /* Like the frames: only changes, and each range's first image. */
    if (!dump.presentInRange || hash != dump.presentHash || w != dump.presentW || h != dump.presentH) {
        char path[600];
        snprintf(path, sizeof(path), "%s/present_%06u.png", dump.presentDir, (unsigned)vblank);
        pngWrite(path, w, h, rgb);
        dump.presentHash = hash;
        dump.presentW = w;
        dump.presentH = h;
        dump.presentInRange = 1;
    }
    free(rgb);
}

void dumpPresentNamed(const char* name, int w, int h, const uint8_t* rgba) {
    size_t n = (size_t)w * h;
    uint8_t* rgb = malloc(n * 3 + 1);
    for (size_t i = 0; i < n; i++) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.png", dump.present ? dump.presentDir : dumpDir(), name);
    pngWrite(path, w, h, rgb);
    gpuLog("gpu: wrote %s", path);
    free(rgb);
}

const char* dumpDir(void) {
    return dump.dir[0] != '\0' ? dump.dir : ".";
}

void dumpImage15(const char* path, const uint16_t* px, int w, int h, int bin) {
    char name[1024];
    uint8_t* rgb = malloc((size_t)w * h * 3);
    if (rgb == NULL) {
        return;
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        for (int k = 0; k < 3; k++) {
            uint32_t v = (px[i] >> (5 * k)) & 31;
            rgb[i * 3 + k] = (uint8_t)(v << 3 | v >> 2);
        }
    }
    snprintf(name, sizeof(name), "%s.png", path);
    pngWrite(name, w, h, rgb);
    free(rgb);
    if (bin) {
        snprintf(name, sizeof(name), "%s.bin", path);
        FILE* f = fopen(name, "wb");
        if (f != NULL) {
            fwrite(px, 2, (size_t)w * h, f);
            fclose(f);
        }
    }
}

void dumpVram(uint32_t vblank) {
    char path[600];
    snprintf(path, sizeof(path), "%s/vram_%06u", dump.dir, (unsigned)vblank);
    gpuDumpVram(path);
}

static void dumpWide(uint32_t vblank, int frameWritten);

void dumpVBlank(const GpuDisplay* d, uint32_t vblank) {
    int wantVram = dump.vramAll;
    for (int i = 0; i < dump.vramAtCount; i++) {
        wantVram |= dump.vramAt[i] == vblank;
    }
    if (wantVram) {
        dumpVram(vblank);
    }
    if (!dump.frames || d->w <= 0 || d->h <= 0) {
        return;
    }
    if (dump.rangeCount > 0) {
        if (!rangeHas(vblank)) {
            dump.inRange = 0;
            return;
        }
        if (!dump.inRange) {
            dump.lastW = 0; /* a range's first frame is always written */
            dump.inRange = 1;
        }
    }
    VRect all = {0, 0, VRAM_W - 1, VRAM_H - 1};
    vramSyncRect(all);
    size_t size = (size_t)d->w * d->h * 3;
    uint8_t* rgb = malloc(size);
    displayToRgb(d, rgb);
    uint64_t hash = hashBytes(rgb, size);
    int wrote = 0;
    if (hash != dump.lastHash || d->w != dump.lastW || d->h != dump.lastH) {
        char path[600];
        snprintf(path, sizeof(path), "%s/frame_%06u.png", dump.dir, (unsigned)vblank);
        pngWrite(path, d->w, d->h, rgb);
        dump.lastHash = hash;
        dump.lastW = d->w;
        dump.lastH = d->h;
        wrote = 1;
    }
    free(rgb);
    dumpWide(vblank, wrote);
    hiresDump(d, vblank, wrote);
}

/* The wide picture (wide.c), as wide_<vblank>.png when it changed or its
 * frame_ was written, and a line in wide.log: vblank, margin, whether it is
 * shown wide (clean), whether the game moved 2D in it (shifted, so its centre
 * need not be the 4:3 frame), and why it is 4:3. A frame written with no wide
 * picture at all (wide aspect) gets a line with none=1 and the reason. */
static void dumpWide(uint32_t vblank, int frameWritten) {
    const WideView* v = wideCurrentView();
    if (!v->has && v->none != NULL && frameWritten) {
        /* Shown 4:3 without a wide picture at all. */
        char path[600];
        snprintf(path, sizeof(path), "%s/wide.log", dump.dir);
        FILE* f = fopen(path, "a");
        if (f != NULL) {
            fprintf(f, "%u m=0 clean=0 shifted=0 none=1 why=%s\n", (unsigned)vblank, v->none);
            fclose(f);
        }
        return;
    }
    if (!v->has || v->w <= 0 || v->h <= 0 || v->w > WIDE_FRAME_W || v->h > VRAM_H) {
        return;
    }
    size_t n = (size_t)v->w * v->h;
    uint16_t* px = malloc(n * 2);
    uint8_t* rgb = malloc(n * 3);
    gvkReadWide(px, v->x, v->y, v->w, v->h);
    for (size_t i = 0; i < n; i++) {
        uint16_t c = px[i];
        for (int k = 0; k < 3; k++) {
            uint32_t ch = (c >> (5 * k)) & 31;
            rgb[i * 3 + k] = (uint8_t)(ch << 3 | ch >> 2);
        }
    }
    uint64_t hash = hashBytes(rgb, n * 3);
    if (frameWritten || hash != dump.wideHash) {
        char path[600];
        snprintf(path, sizeof(path), "%s/wide_%06u.png", dump.dir, (unsigned)vblank);
        pngWrite(path, v->w, v->h, rgb);
        dump.wideHash = hash;
        snprintf(path, sizeof(path), "%s/wide.log", dump.dir);
        FILE* f = fopen(path, "a");
        if (f != NULL) {
            /* masks=x0,y0,x1,y1;...: where a shifted frame's centre may
             * differ from frame_ (in its pixels); no masks= field: anywhere. */
            char masks[WIDE_MASKS * 24 + 16] = "";
            if (v->shifted && v->nMask >= 0) {
                int n = snprintf(masks, sizeof(masks), " masks=");
                for (int i = 0; i < v->nMask; i++) {
                    n += snprintf(masks + n, sizeof(masks) - (size_t)n, "%s%d,%d,%d,%d", i ? ";" : "", v->mask[i].x0,
                                  v->mask[i].y0, v->mask[i].x1, v->mask[i].y1);
                }
            }
            fprintf(f, "%u m=%d clean=%d shifted=%d top=%d bottom=%d%s%s%s\n", (unsigned)vblank, v->m, v->clean, v->shifted, v->top,
                    v->bottom, masks, v->why != NULL ? " why=" : "", v->why != NULL ? v->why : "");
            fclose(f);
        }
    }
    free(px);
    free(rgb);
}

/* Save states (system/savestate.h): the first picture after a load is
 * written whatever the last one before it was. */
void dumpStateLoaded(void) {
    dump.lastW = 0;
    dump.presentW = 0;
    dump.wideHash = 0;
}
