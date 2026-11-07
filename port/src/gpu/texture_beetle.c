/* Beetle PSX HW replacement packs: the community's HD packs (made for
 * RetroArch's Beetle PSX HW core, texture replacement on) drawn through the
 * texture pack path (texture_pack.c finds and decodes the files).
 *
 * The naming scheme was measured black-box (beads xg-mrj.1, no emulator
 * source read: tools/beetle_front.c ran the core as a libretro frontend,
 * our own GPU test programs drew known uploads, standard hashes were tried
 * on the dumped names; docs/BEETLE_PACKS.md has the method and results):
 *
 * - Every CPU-to-VRAM transfer is an "upload", named by the CRC-32 (IEEE,
 *   zlib's) of its halfwords as sent, little-endian, row by row.
 * - VRAM keeps, per halfword, the upload that last wrote it and the place
 *   in that upload: an upload takes its rectangle, a fill lets go of it, a
 *   drawing keeps it, a VRAM copy copies it (with the source's places).
 * - A 4/8-bit draw's palette is named by the CRC-32 of its 16/256 entries
 *   as their owning upload sent them, when one upload owns them all
 *   ("missing" otherwise: then nothing is replaced). 15-bit: palette 0.
 * - A replacement is "<upload>-<palette>.png" in lowercase hex without
 *   leading zeros: the whole upload as the draw's depth sees it (an upload
 *   w halfwords wide is 4w texels at 4-bit, 2w at 8-bit, w at 15-bit) at a
 *   power-of-two scale. Each texel is drawn from the replacement of the
 *   upload that owns it (a primitive across two uploads shows both), so a
 *   texel no upload owns, or whose upload has no file for the palette,
 *   stays native. Alpha 0 is clear, 255 opaque, anything between STP.
 *
 * Tracking runs only while a Beetle pack is chosen (texture_pack.c turns it
 * on): what was uploaded before stays native until it is uploaded again.
 * After a state load, a texel keeps its owner only where VRAM still holds
 * what the owner sent (a load into the same room keeps its replacements).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "texture.h"

#define SLOTS 8192 /* uploads owning texels at once (slot 0: none) */
#define SLOT_BITS 13
#define OWN(slot, x, y) ((uint32_t)(slot) | (uint32_t)(x) << SLOT_BITS | (uint32_t)(y) << (SLOT_BITS + 10))
#define OWN_SLOT(o) ((o) & (SLOTS - 1))
#define OWN_X(o) (((o) >> SLOT_BITS) & 1023)
#define OWN_Y(o) ((o) >> (SLOT_BITS + 10))

typedef struct {
    uint32_t crc, refs;
    int w, h;
    uint16_t* data; /* the upload as sent (palettes are named from it) */
} Slot;

static int tracking, held;
static uint32_t* own;     /* [VRAM_H][VRAM_W] */
static uint32_t* scratch; /* a copy's source ownership */
static Slot slots[SLOTS];
static int freeList[SLOTS], freeCount = -1;
static int full; /* slots ran out: logged once */
static BeetleStats stats;

/* ---- the CRC-32 (IEEE 802.3, reflected 0xEDB88320: SDL_crc32) ---- */

uint32_t beetleCrc(const uint16_t* words, size_t count) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    uint32_t crc = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t b[2] = {(uint8_t)words[i], (uint8_t)(words[i] >> 8)};
        crc = SDL_crc32(crc, b, 2);
    }
    return crc;
#else
    return SDL_crc32(0, words, count * 2);
#endif
}

static FILE* nameLog; /* beetle_pack_log */
static void beetleNameLogUpload(uint32_t crc, int x, int y, int w, int h);

/* ---- ownership ---- */

static void slotsInit(void) {
    freeCount = 0;
    for (int i = SLOTS - 1; i >= 1; --i) freeList[freeCount++] = i;
}

static void release(uint32_t o) {
    int s = (int)OWN_SLOT(o);
    if (!s) return;
    if (--slots[s].refs == 0) {
        free(slots[s].data);
        memset(&slots[s], 0, sizeof(slots[s]));
        freeList[freeCount++] = s;
        --stats.slots;
    }
}

void beetleTrackReset(void) {
    if (own) {
        memset(own, 0, (size_t)VRAM_W * VRAM_H * sizeof(*own));
    }
    for (int i = 1; i < SLOTS; ++i) free(slots[i].data);
    memset(slots, 0, sizeof(slots));
    slotsInit();
    stats.slots = 0;
    full = 0;
}

void beetleTrackEnable(int on) {
    if (on && !own) {
        own = calloc((size_t)VRAM_W * VRAM_H, sizeof(*own));
        scratch = malloc((size_t)VRAM_W * VRAM_H * sizeof(*scratch));
        if (!own || !scratch) {
            free(own);
            free(scratch);
            own = scratch = NULL;
            fprintf(stderr, "beetle pack: no memory for upload tracking\n");
            on = 0;
        }
    }
    if (on && !tracking) beetleTrackReset();
    tracking = on && own != NULL;
}

int beetleTrackEnabled(void) {
    return tracking;
}

void beetleTrackHold(int on) {
    held = on;
}

void beetleTrackLoad(int x, int y, int w, int h, const uint16_t* src) {
    if (!tracking || held || w < 1 || h < 1 || w > VRAM_W || h > VRAM_H) return;
    if (freeCount < 0) slotsInit();
    int s = 0;
    if (freeCount > 0) {
        s = freeList[--freeCount];
        Slot* sl = &slots[s];
        sl->data = malloc((size_t)w * h * 2);
        if (!sl->data) {
            freeList[freeCount++] = s;
            s = 0;
        } else {
            memcpy(sl->data, src, (size_t)w * h * 2);
            sl->crc = beetleCrc(src, (size_t)w * h);
            beetleNameLogUpload(sl->crc, x, y, w, h);
            sl->w = w;
            sl->h = h;
            sl->refs = 0;
            ++stats.slots;
            ++stats.uploads;
        }
    }
    if (!s && !full) {
        full = 1;
        fprintf(stderr, "beetle pack: %d uploads tracked at once; later ones stay native\n", SLOTS - 1);
    }
    for (int j = 0; j < h; ++j) {
        uint32_t* row = own + (size_t)((y + j) & (VRAM_H - 1)) * VRAM_W;
        for (int i = 0; i < w; ++i) {
            uint32_t* at = &row[(x + i) & (VRAM_W - 1)];
            uint32_t o = *at;
            *at = s ? OWN(s, i, j) : 0;
            if (s) ++slots[s].refs;
            release(o);
        }
    }
}

void beetleTrackFill(int x, int y, int w, int h) {
    if (!tracking || held) return;
    for (int j = 0; j < h; ++j) {
        uint32_t* row = own + (size_t)((y + j) & (VRAM_H - 1)) * VRAM_W;
        for (int i = 0; i < w; ++i) {
            uint32_t* at = &row[(x + i) & (VRAM_W - 1)];
            uint32_t o = *at;
            *at = 0;
            release(o);
        }
    }
}

void beetleTrackMove(int sx, int sy, int dx, int dy, int w, int h) {
    if (!tracking || held || w < 1 || h < 1 || w > VRAM_W || h > VRAM_H) return;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            uint32_t o = own[(size_t)((sy + j) & (VRAM_H - 1)) * VRAM_W + ((sx + i) & (VRAM_W - 1))];
            scratch[(size_t)j * w + i] = o;
            if (OWN_SLOT(o)) ++slots[OWN_SLOT(o)].refs; /* held across the writes below */
        }
    for (int j = 0; j < h; ++j) {
        uint32_t* row = own + (size_t)((dy + j) & (VRAM_H - 1)) * VRAM_W;
        for (int i = 0; i < w; ++i) {
            uint32_t* at = &row[(dx + i) & (VRAM_W - 1)];
            uint32_t o = *at;
            *at = scratch[(size_t)j * w + i];
            release(o);
        }
    }
}

void beetleTrackStateLoaded(void) {
    if (!tracking) return;
    for (int y = 0; y < VRAM_H; ++y)
        for (int x = 0; x < VRAM_W; ++x) {
            uint32_t* at = &own[(size_t)y * VRAM_W + x];
            uint32_t o = *at;
            if (!o) continue;
            const Slot* s = &slots[OWN_SLOT(o)];
            if (vramShadow[y][x] != s->data[OWN_Y(o) * s->w + OWN_X(o)]) {
                *at = 0;
                release(o);
            }
        }
    textureBeetleReset();
}

int beetleOwner(int x, int y, uint32_t* crc, int* w, int* h, int* ux, int* uy) {
    if (!tracking) return 0;
    uint32_t o = own[(size_t)(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))];
    if (!o) return 0;
    const Slot* s = &slots[OWN_SLOT(o)];
    *crc = s->crc;
    *w = s->w;
    *h = s->h;
    *ux = (int)OWN_X(o);
    *uy = (int)OWN_Y(o);
    return (int)OWN_SLOT(o);
}

int beetlePaletteName(uint16_t clut, int entries, uint32_t* crc) {
    if (!tracking || entries < 1 || entries > 256) return 0;
    int cx = (clut & 63) * 16, cy = (clut >> 6) & 511;
    uint16_t words[256];
    int slot = 0;
    for (int i = 0; i < entries; ++i) {
        uint32_t o = own[(size_t)cy * VRAM_W + ((cx + i) & (VRAM_W - 1))];
        int s = (int)OWN_SLOT(o);
        if (!s || (slot && s != slot)) return 0;
        slot = s;
        words[i] = slots[s].data[OWN_Y(o) * slots[s].w + OWN_X(o)];
    }
    *crc = beetleCrc(words, (size_t)entries);
    return 1;
}

/* ---- drawing: a texture request's replaced texels ----
 *
 * A request (texture.c: a covering upload, a field page's stable region or
 * the sampled patch) is walked once per decode revision (which moves with
 * every upload, fill or copy under its texels or palette, hence with their
 * ownership) and pack image arrival: either one upload's replacement covers
 * it with a plain offset (the image itself is the sample, kept on the GPU
 * as it is), or a composite at the largest scale met is made, native texels
 * enlarged where nothing replaces them. */

#define MEMO_SLOTS 4096
#define COMPOSITES 512
#define COMPOSITE_BYTES (256u << 20)

typedef struct {
    TextureRequest request;
    uint64_t revision, images;
    int kind; /* 0 empty, 1 nothing replaced, 2 one image, 3 composite */
    uint32_t upload, palette;
    int iw, ih;    /* the image's texels (kind 2) */
    int ou, ov;    /* the image's origin in the request's UV space (kind 2) */
    int composite; /* index + 1 (kind 3) */
    uint64_t compositeSerial;
    uint64_t touched;
} Memo;

typedef struct {
    uint8_t* rgba;
    size_t bytes;
    TextureRequest request;
    int scale;
    uint64_t revision, serial, touched;
    uint64_t sig; /* what it shows: each texel's image place or native colour (walk) */
} Composite;

static Memo memos[MEMO_SLOTS];
static Composite composites[COMPOSITES];
static size_t compositeBytes;
static uint64_t memoClock, compositeSerials;
static uint64_t sampleRevision = UINT64_C(0x4000000000000000);

void textureBeetleReset(void) {
    memset(memos, 0, sizeof(memos));
    for (int i = 0; i < COMPOSITES; ++i) free(composites[i].rgba);
    memset(composites, 0, sizeof(composites));
    compositeBytes = 0;
}

void beetleStats(BeetleStats* out) {
    *out = stats;
    out->compositeBytes = compositeBytes;
    out->tracking = tracking;
}

static int sameRequest(TextureRequest a, TextureRequest b) {
    return a.tpage == b.tpage && a.clut == b.clut && a.window == b.window && a.u == b.u && a.v == b.v &&
           a.w == b.w && a.h == b.h && a.stableFrom == b.stableFrom && a.stableTo == b.stableTo;
}

static unsigned memoHash(TextureRequest r) {
    uint64_t k = (uint64_t)r.tpage | (uint64_t)r.clut << 16 | (uint64_t)r.window << 32;
    k ^= ((uint64_t)(uint16_t)r.u | (uint64_t)(uint16_t)r.v << 16 | (uint64_t)r.w << 32 | (uint64_t)r.h << 48) *
         0x9E3779B97F4A7C15ull;
    k ^= (uint64_t)r.stableTo * 0xBF58476D1CE4E5B9ull;
    k ^= k >> 29;
    return (unsigned)((k * 0x94D049BB133111EBull) >> 52) & (MEMO_SLOTS - 1);
}

/* A texel of the request at (x, y): its VRAM halfword and the texel's index
 * inside it, through the texture window and the page's wrap (psx-spx, GPU
 * Texture Window: (t AND NOT(mask*8)) OR ((offset AND mask)*8)). */
static void address(TextureRequest r, int x, int y, int* vx, int* vy, int* sub) {
    int u = (r.u + x) & 255, v = (r.v + y) & 255;
    int mx = (int)(r.window & 31) * 8, my = (int)((r.window >> 5) & 31) * 8;
    u = (u & ~mx) | ((int)((r.window >> 10) & 31) * 8 & mx);
    v = (v & ~my) | ((int)((r.window >> 15) & 31) * 8 & my);
    int depth = (r.tpage >> 7) & 3, shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    *sub = u & ((1 << shift) - 1);
    *vx = ((r.tpage & 15) * 64 + (u >> shift)) & (VRAM_W - 1);
    *vy = (((r.tpage >> 4) & 1) * 256 + v) & (VRAM_H - 1);
}

static void native(uint8_t* dst, uint16_t c) {
    for (int k = 0; k < 3; ++k) {
        int a = (c >> (k * 5)) & 31;
        dst[k] = (uint8_t)((a << 3) | (a >> 2));
    }
    dst[3] = c == 0 ? 0 : (c & 0x8000) ? 128 : 255;
}

/* The images met in one walk, by slot (a request meets few uploads). */
#define WALK_SEEN 64
typedef struct {
    int slot;
    int status; /* 1 image, 0 none, -1 loading */
    TexturePackImage image;
} Seen;

static const Seen* seenFor(Seen* seen, int* count, int slot, uint32_t palette, int tph) {
    for (int i = 0; i < *count; ++i)
        if (seen[i].slot == slot) return &seen[i];
    Seen s = {slot, 0, {0}};
    s.status = texturePackBeetleImage(slots[slot].crc, palette, slots[slot].w * tph, slots[slot].h, &s.image);
    if (*count < WALK_SEEN) {
        seen[(*count)++] = s;
        return &seen[*count - 1];
    }
    seen[WALK_SEEN - 1] = s; /* rare: keep the newest */
    return &seen[WALK_SEEN - 1];
}

static Composite* compositeNew(size_t bytes) {
    Composite* victim = &composites[0];
    for (int i = 0; i < COMPOSITES; ++i) {
        Composite* c = &composites[i];
        if (!c->rgba) {
            victim = c;
            break;
        }
        if (c->touched < victim->touched) victim = c;
    }
    if (victim->rgba) {
        compositeBytes -= victim->bytes;
        free(victim->rgba);
        victim->rgba = NULL;
    }
    while (compositeBytes + bytes > COMPOSITE_BYTES) {
        Composite* old = NULL;
        for (int i = 0; i < COMPOSITES; ++i)
            if (composites[i].rgba && (!old || composites[i].touched < old->touched)) old = &composites[i];
        if (!old) break;
        compositeBytes -= old->bytes;
        free(old->rgba);
        old->rgba = NULL;
        ++stats.compositeEvictions;
    }
    victim->rgba = malloc(bytes);
    if (!victim->rgba) return NULL;
    victim->bytes = bytes;
    compositeBytes += bytes;
    victim->serial = ++compositeSerials;
    return victim;
}

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 0x100000001B3ull;
    return h ^ (h >> 29);
}

/* The native colour word of a request's texel (its live palette's entry at 4/8 bits). */
static uint16_t nativeWord(const DecodedTexture* d, int depth, int vx, int vy, int sub) {
    uint16_t c = vramShadow[vy][vx];
    if (depth == 0) return d->palette[(c >> (sub * 4)) & 15];
    if (depth == 1) return d->palette[(c >> (sub * 8)) & 255];
    return c;
}

/* Walk the request: kind 1 (nothing replaced), 2 (one image with an offset)
 * or 3 (a composite, made here unless one showing the same is kept: a
 * request's decode revision moves with every palette upload in its page,
 * though what the composite shows mostly stays). */
static void walk(const DecodedTexture* d, Memo* m) {
    TextureRequest r = d->request;
    int depth = (r.tpage >> 7) & 3, tph = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    uint32_t palette = 0;
    m->kind = 1;
    if (depth < 2 && !beetlePaletteName(r.clut, depth == 0 ? 16 : 256, &palette)) {
        ++stats.paletteMissing;
        return;
    }
    Seen seen[WALK_SEEN];
    int seenCount = 0, replaced = 0, single = 1, scale = 1, firstSlot = -1, du = 0, dv = 0;
    uint64_t sig = 0xCBF29CE484222325ull;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int tx = (r.tpage & 15) * 64, ty = ((r.tpage >> 4) & 1) * 256;
    int hx0 = tx + (r.u >> shift), hx1 = tx + ((r.u + r.w - 1) >> shift);
    int plain = r.window == 0 && r.u >= 0 && r.v >= 0 && r.u + r.w <= 256 && r.v + r.h <= 256 && hx1 < VRAM_W;
    if (plain) {
        /* no window, no wrap: a halfword at a time (its texels share its owner) */
        int lastSlot = -1, firstX = 0, firstY = 0;
        const Seen* e = NULL;
        uint32_t firstOwn = 0;
        for (int y = 0; y < r.h; ++y) {
            int vy = ty + r.v + y;
            const uint32_t* row = own + (size_t)vy * VRAM_W;
            for (int vx = hx0; vx <= hx1; ++vx) {
                uint32_t o = row[vx];
                int s = (int)OWN_SLOT(o);
                if (s != lastSlot) {
                    lastSlot = s;
                    e = s ? seenFor(seen, &seenCount, s, palette, tph) : NULL;
                    if (e && e->status == 1) sig = mix(sig, e->image.revision);
                }
                if (!e || e->status != 1) {
                    single = 0;
                    /* the native texels' colours (a palette cycle elsewhere in the CLUT changes nothing) */
                    uint64_t c = 0;
                    for (int k = 0; k < tph; ++k) c = c << 16 | nativeWord(d, depth, vx, vy, k);
                    sig = mix(sig, c ^ 0xFFFF000000000000ull);
                    continue;
                }
                ++replaced;
                if (e->image.scale > scale) scale = e->image.scale;
                sig = mix(sig, o >> SLOT_BITS); /* the place in the upload (the image's revision names the upload) */
                if (firstSlot < 0) {
                    firstSlot = s;
                    firstOwn = o;
                    firstX = vx;
                    firstY = y;
                    du = ((vx - tx) << shift) - (int)OWN_X(o) * tph;
                    dv = r.v + y - (int)OWN_Y(o);
                } else if (single) {
                    int ux = (int)OWN_X(firstOwn) + vx - firstX, uy = (int)OWN_Y(firstOwn) + y - firstY;
                    if (ux < 0 || uy < 0 || ux >= slots[firstSlot].w || uy >= slots[firstSlot].h ||
                        o != OWN(firstSlot, ux, uy))
                        single = 0;
                }
            }
        }
    } else {
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                int vx, vy, sub;
                address(r, x, y, &vx, &vy, &sub);
                uint32_t o = own[(size_t)vy * VRAM_W + vx];
                int s = (int)OWN_SLOT(o);
                const Seen* e = s ? seenFor(seen, &seenCount, s, palette, tph) : NULL;
                if (!e || e->status != 1) {
                    single = 0;
                    sig = mix(sig, 0xFFFF0000u | nativeWord(d, depth, vx, vy, sub));
                    continue;
                }
                ++replaced;
                if (e->image.scale > scale) scale = e->image.scale;
                int tu = (int)OWN_X(o) * tph + sub, tv = (int)OWN_Y(o);
                sig = mix(mix(sig, e->image.revision), (uint64_t)tu << 32 | (uint32_t)tv);
                if (firstSlot < 0) {
                    firstSlot = s;
                    du = r.u + x - tu;
                    dv = r.v + y - tv;
                } else if (s != firstSlot || r.u + x - tu != du || r.v + y - tv != dv) {
                    single = 0;
                }
            }
    }
    if (!replaced) return;
    if (single) {
        m->kind = 2;
        m->upload = slots[firstSlot].crc;
        m->palette = palette;
        m->iw = slots[firstSlot].w * tph;
        m->ih = slots[firstSlot].h;
        m->ou = du;
        m->ov = dv;
        ++stats.singles;
        return;
    }
    sig = mix(sig, (uint64_t)scale);
    for (int i = 0; i < COMPOSITES; ++i) {
        Composite* k = &composites[i];
        if (k->rgba && k->sig == sig && sameRequest(k->request, r)) {
            m->kind = 3;
            m->composite = i + 1;
            m->compositeSerial = k->serial;
            ++stats.compositeReuses;
            return;
        }
    }
    size_t bytes = (size_t)r.w * r.h * scale * scale * 4;
    Composite* c = compositeNew(bytes);
    if (!c) return;
    uint8_t* out = c->rgba;
    int stride = r.w * scale;
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            int vx, vy, sub;
            address(r, x, y, &vx, &vy, &sub);
            uint32_t o = own[(size_t)vy * VRAM_W + vx];
            int s = (int)OWN_SLOT(o);
            const Seen* e = s ? seenFor(seen, &seenCount, s, palette, tph) : NULL;
            uint8_t* block = out + ((size_t)y * scale * stride + (size_t)x * scale) * 4;
            if (e && e->status == 1) {
                const TexturePackImage* im = &e->image;
                int k = scale / im->scale, iw = im->w * im->scale;
                int tu = (int)OWN_X(o) * tph + sub, tv = (int)OWN_Y(o);
                for (int py = 0; py < scale; ++py)
                    for (int px = 0; px < scale; ++px) {
                        const uint8_t* p = im->rgba + ((size_t)(tv * im->scale + py / k) * iw + tu * im->scale + px / k) * 4;
                        memcpy(block + ((size_t)py * stride + px) * 4, p, 4);
                    }
            } else {
                uint8_t px4[4];
                native(px4, nativeWord(d, depth, vx, vy, sub));
                for (int py = 0; py < scale; ++py)
                    for (int px = 0; px < scale; ++px) memcpy(block + ((size_t)py * stride + px) * 4, px4, 4);
            }
        }
    c->request = r;
    c->sig = sig;
    c->scale = scale;
    c->revision = ++sampleRevision;
    m->kind = 3;
    m->composite = (int)(c - composites) + 1;
    m->compositeSerial = c->serial;
    ++stats.composites;
}

int textureBeetleSample(const DecodedTexture* d, TextureRequest patch, TextureSample* out) {
    (void)patch;
    if (!tracking || !d) return 0;
    uint64_t images = texturePackBeetleGeneration();
    unsigned h = memoHash(d->request);
    Memo* m = &memos[h];
    int fresh = !(m->kind && sameRequest(m->request, d->request) && m->revision == d->revision && m->images == images);
    if (!fresh && m->kind == 3) {
        Composite* c = &composites[m->composite - 1];
        fresh = !c->rgba || c->serial != m->compositeSerial;
    }
    if (fresh) {
        memset(m, 0, sizeof(*m));
        m->request = d->request;
        m->revision = d->revision;
        m->images = images;
        uint64_t t0 = SDL_GetTicksNS();
        walk(d, m);
        if (!m->kind) m->kind = 1;
        ++stats.walks;
        stats.walkNs += SDL_GetTicksNS() - t0;
    }
    m->touched = ++memoClock;
    if (m->kind == 2) {
        TexturePackImage im;
        if (texturePackBeetleImage(m->upload, m->palette, m->iw, m->ih, &im) != 1) return 0;
        *out = (TextureSample){im.rgba, im.revision, m->ou, m->ov, im.w, im.h, im.scale, NULL, texturePackGpuKeeps()};
        ++stats.draws;
        return 1;
    }
    if (m->kind == 3) {
        Composite* c = &composites[m->composite - 1];
        c->touched = memoClock;
        *out = (TextureSample){c->rgba, c->revision, c->request.u, c->request.v, c->request.w, c->request.h, c->scale, NULL,
                               texturePackGpuKeeps()};
        ++stats.draws;
        return 1;
    }
    return 0;
}

/* ---- the names a draw meets (beetle_pack_log), as the core would dump them ---- */

static uint64_t* logged;
static size_t loggedCap, loggedCount;

static int loggedAdd(uint64_t k) {
    if (loggedCount * 2 >= loggedCap) {
        size_t cap = loggedCap ? loggedCap * 2 : 4096;
        uint64_t* t = calloc(cap, sizeof(*t));
        if (!t) return 0;
        for (size_t i = 0; i < loggedCap; ++i)
            if (logged[i]) {
                size_t j = (size_t)(logged[i] * 0x9E3779B97F4A7C15ull) & (cap - 1);
                while (t[j]) j = (j + 1) & (cap - 1);
                t[j] = logged[i];
            }
        free(logged);
        logged = t;
        loggedCap = cap;
    }
    size_t j = (size_t)(k * 0x9E3779B97F4A7C15ull) & (loggedCap - 1);
    while (logged[j]) {
        if (logged[j] == k) return 0;
        j = (j + 1) & (loggedCap - 1);
    }
    logged[j] = k;
    ++loggedCount;
    return 1;
}

uint32_t hwVSyncCount(void) __attribute__((weak)); /* port/src/system/hw.c; standalone tests have none */
/* With the name log: every upload too ("upload <crc> <x> <y> <w> <h> <vblank>", halfwords). */
static void beetleNameLogUpload(uint32_t crc, int x, int y, int w, int h) {
    if (nameLog) fprintf(nameLog, "upload %x %d %d %d %d %u\n", crc, x, y, w, h, hwVSyncCount ? hwVSyncCount() : 0);
}

void beetleNameLogOpen(const char* path) {
    if (nameLog) fclose(nameLog);
    nameLog = path && *path ? fopen(path, "a") : NULL;
    if (path && *path && !nameLog) fprintf(stderr, "beetle pack: cannot write the name log %s\n", path);
}

void beetleNameLogPatch(TextureRequest patch, uint32_t vblank) {
    if (!nameLog || !tracking) return;
    int depth = (patch.tpage >> 7) & 3, tph = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    uint32_t palette = 0;
    int owned = depth == 2 || beetlePaletteName(patch.clut, depth == 0 ? 16 : 256, &palette);
    int last = -1;
    for (int y = 0; y < patch.h; ++y)
        for (int x = 0; x < patch.w; ++x) {
            int vx, vy, sub;
            address(patch, x, y, &vx, &vy, &sub);
            int s = (int)OWN_SLOT(own[(size_t)vy * VRAM_W + vx]);
            if (!s || s == last) continue;
            last = s;
            uint64_t key = ((uint64_t)slots[s].crc << 32 | (owned ? palette : 0xFFFFFFFFu)) ^
                           (uint64_t)(depth + 1) * 0x9E3779B97F4A7C15ull;
            if (!key) key = 1;
            if (!loggedAdd(key)) continue;
            /* the dump's name and size (a 15-bit dump has no palette part) */
            if (depth == 2) fprintf(nameLog, "%x.png %d %d %u\n", slots[s].crc, slots[s].w * tph, slots[s].h, vblank);
            else if (owned) fprintf(nameLog, "%x-%x.png %d %d %u\n", slots[s].crc, palette, slots[s].w * tph, slots[s].h, vblank);
            else fprintf(nameLog, "%x-missing.png %d %d %u\n", slots[s].crc, slots[s].w * tph, slots[s].h, vblank);
            fflush(nameLog);
        }
}
