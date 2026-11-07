/* Bounded draw-time texture decoding. Only dependencies in exact 1x VRAM
 * define validity: readbacks do not make a rendered capture an upload. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "texture.h"
#include "gpu.h"

typedef struct {
    int used;
    uint64_t touched;
    DecodedTexture decoded;
    TileSet texTiles, dependencies;
    /* The dependencies' tiles' box, and the newest generation among them
     * when decoded: generations only grow, so a newer one there is a change. */
    int tx0, ty0, tx1, ty1;
    uint64_t newest;
    /* The newest generation among the texel tiles at the last full decode: with
     * the texels unchanged, a palette change recolours the kept indices (and
     * the mask of the pixels outside a stable region) instead of reading the
     * page again (paletteOnly). */
    uint64_t texNewest;
    /* current(): the per-tile newest() found at a block generation, good
     * while the blocks say the same (generations only grow, so an equal
     * block generation means no tile under them changed). */
    uint64_t checkedBlocks, checkedNewest;
    uint8_t* storage;
    size_t bytes;
} Entry;

static Entry cache[TEXTURE_CACHE_SLOTS];
/* The entries by request: chains through `chain` from `bucket` (index + 1). */
#define CACHE_HASH (TEXTURE_CACHE_SLOTS * 2)
static int32_t bucket[CACHE_HASH], chain[TEXTURE_CACHE_SLOTS];
static TextureStats stats;
static uint64_t clock, revision;
/* Recency: the used entries in a list from the most (head) to the least recently used (tail),
 * by index + 1 (0 ends it), so a miss finds its victim without scanning every slot (the scan
 * was the cache's cost when a low camera over a texture-packed room missed hundreds of times
 * a frame); `fresh` counts the slots never used. */
static int32_t lruPrev[TEXTURE_CACHE_SLOTS], lruNext[TEXTURE_CACHE_SLOTS], lruHead, lruTail, fresh;

static void lruUnlink(int32_t i) {
    int32_t p = lruPrev[i - 1], n = lruNext[i - 1];
    if (p) lruNext[p - 1] = n; else lruHead = n;
    if (n) lruPrev[n - 1] = p; else lruTail = p;
    lruPrev[i - 1] = lruNext[i - 1] = 0;
}

static void lruFront(int32_t i) {
    if (lruHead == i) return;
    if (lruPrev[i - 1] || lruNext[i - 1] || lruTail == i) lruUnlink(i);
    lruNext[i - 1] = lruHead;
    if (lruHead) lruPrev[lruHead - 1] = i;
    lruHead = i;
    if (!lruTail) lruTail = i;
}

static void snapMemoReset(void);

void textureCacheReset(void) {
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; ++i) free(cache[i].storage);
    memset(cache, 0, sizeof(cache));
    memset(bucket, 0, sizeof(bucket));
    memset(&stats, 0, sizeof(stats));
    memset(lruPrev, 0, sizeof(lruPrev));
    memset(lruNext, 0, sizeof(lruNext));
    lruHead = lruTail = fresh = 0;
    clock = revision = 0;
    snapMemoReset();
    textureSampleReset();
}

static uint32_t requestHash(TextureRequest r) {
    uint64_t k = (uint64_t)r.tpage | (uint64_t)r.clut << 16 | (uint64_t)r.window << 32;
    k ^= ((uint64_t)(uint16_t)r.u | (uint64_t)(uint16_t)r.v << 16 | (uint64_t)r.w << 32 | (uint64_t)r.h << 48) *
         0x9E3779B97F4A7C15ull;
    k ^= ((uint64_t)r.stableFrom << 32 | r.stableTo) * 0xBF58476D1CE4E5B9ull;
    k ^= k >> 31;
    return (uint32_t)(k * 0x94D049BB133111EBull >> 40) % CACHE_HASH;
}

static void unlinkEntry(Entry* e) {
    int32_t idx = (int32_t)(e - cache) + 1;
    int32_t* at = &bucket[requestHash(e->decoded.request)];
    while (*at && *at != idx) at = &chain[*at - 1];
    if (*at == idx) *at = chain[idx - 1];
}

void textureCacheStats(TextureStats* out) { *out = stats; }

/* snap's answer per request, kept while nothing it looked at changed: every
 * textured polygon asks (prepareTexture, the interpolation's draws), and a
 * crowded field asks thousands of times a frame for the same few hundred
 * patches, each answer scanning the upload ring, hashing the page's tile
 * generations and walking its stamp summaries (a fifth of the game thread
 * in Dazil's square at 12x). The answer depends on the ring entry that
 * covers the sample (if any), and on the page's generations, stamps, stamp
 * summaries and page analysis, which change only with a tile generation of
 * the page (vram.c: stamps move only inside changed()). So an answer stands
 * while the page's newest block generation and the covering upload are as
 * they were, and VRAM's history was not started over (vramLayoutSerial: a
 * reset or a state load). */
#define SNAP_SLOTS 16384
#define SNAP_WAYS 4
typedef struct {
    uint16_t tpage;
    uint8_t stable, cover;
    int16_t u, v, w, h;
    uint32_t serial, touched;
    uint64_t pageNewest;
    VRect coverRect;
    uint64_t coverGen;
    unsigned coverIndex;
    int16_t ou, ov, ow, oh;
    uint32_t from, to;
} SnapMemo;
static SnapMemo snapMemo[SNAP_SLOTS];
static uint32_t snapClock;

static void snapMemoReset(void) {
    memset(snapMemo, 0, sizeof(snapMemo));
    snapClock = 0;
}

static unsigned snapHash(TextureRequest r, int stable) {
    uint64_t k = (uint64_t)r.tpage | (uint64_t)(uint16_t)r.u << 16 | (uint64_t)(uint16_t)r.v << 32 | (uint64_t)stable << 48;
    k ^= ((uint64_t)(uint16_t)r.w | (uint64_t)(uint16_t)r.h << 16) * 0x9E3779B97F4A7C15ull;
    k ^= k >> 29;
    k *= 0xBF58476D1CE4E5B9ull;
    return (unsigned)(k >> 40) & (SNAP_SLOTS - 1) & ~(SNAP_WAYS - 1);
}

static TextureRequest snapCompute(TextureRequest r, int stable, VRect sample, VRect page, int tx, int ty, int shift,
                                  SnapMemo* m) {
    VRect upload, region;
    m->cover = 0;
    if (vramUploadCoverAt(sample, &upload, &m->coverIndex, &m->coverGen)) {
        m->cover = 1;
        m->coverRect = upload;
        int u = (upload.x0 - tx) * (1 << shift), v = upload.y0 - ty;
        int w = (upload.x1 - upload.x0 + 1) * (1 << shift), h = upload.y1 - upload.y0 + 1;
        if (u >= 0 && v >= 0 && u + w <= 256 && v + h <= 256) {
            r.u = u; r.v = v; r.w = w; r.h = h;
            return r;
        }
    }
    if (!stable) return r;
    uint32_t from, to;
    if (vramStableRegion(sample, page, &region, &from, &to)) {
        r.u = (region.x0 - tx) * (1 << shift); r.v = region.y0 - ty;
        r.w = (region.x1 - region.x0 + 1) * (1 << shift); r.h = region.y1 - region.y0 + 1;
        r.stableFrom = from; r.stableTo = to;
    }
    return r;
}

static TextureRequest snap(TextureRequest r, int stable) {
    if (r.window || r.u < 0 || r.v < 0 || r.w < 1 || r.h < 1 || r.w > 256 || r.h > 256 ||
        r.u > 256 - r.w || r.v > 256 - r.h) return r;
    int depth = (r.tpage >> 7) & 3;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int tx = (r.tpage & 15) * 64, ty = ((r.tpage >> 4) & 1) * 256;
    VRect sample = {tx + (r.u >> shift), ty + r.v,
                    tx + ((r.u + r.w - 1) >> shift), ty + r.v + r.h - 1};
    if (sample.x1 >= VRAM_W) return r;
    /* The logical page in VRAM: 256 texels wide at the request's depth. */
    VRect page = {tx, ty, tx + (256 >> shift) - 1, ty + 255};
    if (page.x1 >= VRAM_W) page.x1 = VRAM_W - 1;
    uint64_t pageNewest = vramBlockNewest(page);
    unsigned first = snapHash(r, stable);
    SnapMemo* m = NULL;
    SnapMemo* victim = &snapMemo[first];
    for (unsigned i = 0; i < SNAP_WAYS; i++) {
        SnapMemo* c = &snapMemo[first + i];
        if (c->touched && c->tpage == r.tpage && c->stable == stable && c->u == r.u && c->v == r.v && c->w == r.w &&
            c->h == r.h) { m = c; break; }
        if (c->touched < victim->touched) victim = c;
    }
    if (m && m->serial == vramLayoutSerial && m->pageNewest == pageNewest &&
        (!m->cover || vramUploadIntact(m->coverIndex, m->coverRect, m->coverGen))) {
        m->touched = ++snapClock;
        r.u = m->ou; r.v = m->ov; r.w = m->ow; r.h = m->oh;
        r.stableFrom = m->from; r.stableTo = m->to;
        return r;
    }
    if (!m) m = victim;
    m->tpage = r.tpage; m->stable = (uint8_t)stable;
    m->u = (int16_t)r.u; m->v = (int16_t)r.v; m->w = (int16_t)r.w; m->h = (int16_t)r.h;
    m->serial = vramLayoutSerial; m->pageNewest = pageNewest;
    m->touched = ++snapClock;
    TextureRequest out = snapCompute(r, stable, sample, page, tx, ty, shift, m);
    m->ou = (int16_t)out.u; m->ov = (int16_t)out.v; m->ow = (int16_t)out.w; m->oh = (int16_t)out.h;
    m->from = out.stableFrom; m->to = out.stableTo;
    return out;
}

TextureRequest textureRequestSnap(TextureRequest r) { return snap(r, 0); }
TextureRequest textureRequestSnapStable(TextureRequest r) { return snap(r, 1); }

static int same(TextureRequest a, TextureRequest b) {
    return a.tpage == b.tpage && a.clut == b.clut && a.window == b.window &&
           a.u == b.u && a.v == b.v && a.w == b.w && a.h == b.h &&
           a.stableFrom == b.stableFrom && a.stableTo == b.stableTo;
}

static void address(TextureRequest r, int x, int y, int* vx, int* vy, int* shift) {
    int u = (r.u + x) & 255, v = (r.v + y) & 255;
    int mx = (int)(r.window & 31) * 8, my = (int)((r.window >> 5) & 31) * 8;
    u = (u & ~mx) | ((int)(r.window >> 10) * 8 & mx);
    v = (v & ~my) | ((int)(r.window >> 15) * 8 & my);
    int depth = (r.tpage >> 7) & 3;
    *shift = depth == 0 ? (u & 3) * 4 : depth == 1 ? (u & 1) * 8 : 0;
    *vx = (((r.tpage & 15) * 64) + (u >> (depth == 0 ? 2 : depth == 1 ? 1 : 0))) & 1023;
    *vy = (((r.tpage >> 4) & 1) * 256 + v) & 511;
}

static void point(TileSet* tiles, int x, int y) {
    tiles->rows[y >> TILE_SHIFT] |= 1ull << (x >> TILE_SHIFT);
}

static void dependencies(Entry* e) {
    TextureRequest r = e->decoded.request;
    tilesClear(&e->texTiles);
    int depth = (r.tpage >> 7) & 3, s = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int tx = (r.tpage & 15) * 64, ty = ((r.tpage >> 4) & 1) * 256;
    if (r.window == 0 && r.u >= 0 && r.v >= 0 && r.u + r.w <= 256 && r.v + r.h <= 256 &&
        tx + ((r.u + r.w - 1) >> s) < VRAM_W) {
        /* no window, no wrap: the texels are one rectangle of VRAM */
        tilesAdd(&e->texTiles, (VRect){tx + (r.u >> s), ty + r.v, tx + ((r.u + r.w - 1) >> s), ty + r.v + r.h - 1});
    } else {
        for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) {
            int vx, vy, shift;
            address(r, x, y, &vx, &vy, &shift);
            point(&e->texTiles, vx, vy);
        }
    }
    e->dependencies = e->texTiles;
    for (int i = 0; i < e->decoded.paletteSize; ++i)
        point(&e->dependencies, ((r.clut & 63) * 16 + i) & 1023, (r.clut >> 6) & 511);
    e->tx0 = TILES_X, e->ty0 = TILES_Y, e->tx1 = -1, e->ty1 = -1;
    for (int y = 0; y < TILES_Y; ++y) {
        if (!e->dependencies.rows[y]) continue;
        int x0 = __builtin_ctzll(e->dependencies.rows[y]), x1 = 63 - __builtin_clzll(e->dependencies.rows[y]);
        e->tx0 = x0 < e->tx0 ? x0 : e->tx0;
        e->tx1 = x1 > e->tx1 ? x1 : e->tx1;
        e->ty0 = y < e->ty0 ? y : e->ty0;
        e->ty1 = y;
    }
}

/* The newest tile generation among the entry's dependencies. */
static uint64_t newest(const Entry* e) {
    uint64_t g = 0;
    /* Only the set tiles: a palette in the frame buffer's rows and texels
     * in a page make a wide, sparse box (xg-dx8). */
    for (int y = e->ty0; y <= e->ty1; ++y) {
        for (uint64_t bits = e->dependencies.rows[y]; bits; bits &= bits - 1) {
            int x = __builtin_ctzll(bits);
            uint64_t t = vramTileGeneration(x, y);
            g = t > g ? t : g;
        }
    }
    return g;
}

static uint64_t newestOf(const TileSet* t) {
    uint64_t g = 0;
    for (int y = 0; y < TILES_Y; ++y)
        for (uint64_t bits = t->rows[y]; bits; bits &= bits - 1) {
            uint64_t v = vramTileGeneration(__builtin_ctzll(bits), y);
            g = v > g ? v : g;
        }
    return g;
}

static int current(Entry* e) {
    if (!e->decoded.revision) return 0;
    /* The blocks first: a block no newer than the decode means no tile in
     * it is either, and most lookups end here (the per-tile scan was the
     * texture cache's cost at 12x with the interpolation's lookups, xg-dx8). */
    uint64_t g = 0;
    for (int by = e->ty0 >> VRAM_BLOCK_SHIFT; by <= e->ty1 >> VRAM_BLOCK_SHIFT; ++by)
        for (int bx = e->tx0 >> VRAM_BLOCK_SHIFT; bx <= e->tx1 >> VRAM_BLOCK_SHIFT; ++bx) {
            uint64_t t = vramBlockGeneration(bx, by);
            g = t > g ? t : g;
        }
    if (g <= e->newest) return 1;
    /* A block newer than the decode but none of the entry's tiles (the frame
     * buffer drawn beside a palette row): the tiles are visited once per
     * block generation, not per lookup. */
    if (g != e->checkedBlocks) {
        e->checkedBlocks = g;
        e->checkedNewest = newest(e);
    }
    return e->checkedNewest <= e->newest;
}

static uint64_t byte(uint64_t h, uint8_t b) { return (h ^ b) * UINT64_C(1099511628211); }
static uint64_t word(uint64_t h, unsigned n) { return byte(byte(h, (uint8_t)n), (uint8_t)(n >> 8)); }
static uint64_t domain(const char* text) {
    uint64_t h = UINT64_C(14695981039346656037);
    while (*text) h = byte(h, (uint8_t)*text++);
    return h;
}

static void rgba(uint8_t* dst, uint16_t c) {
    for (int k = 0; k < 3; ++k) {
        int a = (c >> (k * 5)) & 31;
        dst[k] = (uint8_t)((a << 3) | (a >> 2));
    }
    dst[3] = c == 0 ? 0 : (c & 0x8000) ? 128 : 255;
}

const DecodedTexture* textureCacheGet(TextureRequest r) {
    if (r.w < 1 || r.h < 1 || r.w > 256 || r.h > 256 ||
        r.u < -32768 || r.v < -32768 || r.u > 32767 || r.v > 32767) return NULL;
    r.tpage &= 0x19f;
    int depth = (r.tpage >> 7) & 3;
    if (depth == 3) { r.tpage &= ~0x80; depth = 2; }
    r.clut = depth == 2 ? 0 : r.clut & 0x7fff;
    r.window &= 0xfffff;
    Entry* e = NULL;
    uint32_t hash = requestHash(r);
    for (int32_t i = bucket[hash]; i; i = chain[i - 1])
        if (cache[i - 1].used && same(cache[i - 1].decoded.request, r)) { e = &cache[i - 1]; break; }
    if (e == NULL) {
        /* a slot never used, else the least recently used entry */
        e = fresh < TEXTURE_CACHE_SLOTS ? &cache[fresh++] : &cache[lruTail - 1];
        if (e->used) {
            stats.evictions++; stats.bytes -= e->bytes; free(e->storage); unlinkEntry(e);
            lruUnlink((int32_t)(e - cache) + 1);
        }
        else stats.entries++;
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->decoded.request = r;
        chain[e - cache] = bucket[hash];
        bucket[hash] = (int32_t)(e - cache) + 1;
        e->decoded.depth = depth == 0 ? 4 : depth == 1 ? 8 : 15;
        e->decoded.paletteSize = depth == 0 ? 16 : depth == 1 ? 256 : 0;
        dependencies(e);
    }
    e->touched = ++clock;
    lruFront((int32_t)(e - cache) + 1);
    if (depth == 2 && vramWasRendered(&e->texTiles)) { stats.renderedSkips++; return NULL; }
    if (current(e)) { stats.hits++; return &e->decoded; }
    size_t pixels = (size_t)r.w * r.h;
    if (!e->storage) {
        /* within the byte budget: the least recently used entries' pixels go */
        size_t need = pixels * 6; /* RGBA, then indices (two bytes at 15 bits; one and a mask byte below) */
        int32_t from = lruTail;
        while (stats.bytes + need > TEXTURE_CACHE_BYTES) {
            /* from the least recently used end, the first entry holding pixels */
            Entry* v = NULL;
            for (; from; from = lruPrev[from - 1])
                if (&cache[from - 1] != e && cache[from - 1].storage) { v = &cache[from - 1]; from = lruPrev[from - 1]; break; }
            if (!v) break;
            stats.evictions++;
            stats.bytes -= v->bytes;
            free(v->storage);
            v->storage = NULL;
            v->bytes = 0;
            v->decoded.rgba = v->decoded.texels = NULL;
            v->decoded.revision = 0; /* decoded again when wanted */
        }
        e->bytes = need;
        e->storage = malloc(e->bytes);
        if (!e->storage) { e->bytes = 0; return NULL; }
        stats.bytes += e->bytes;
        e->decoded.rgba = e->storage;
        e->decoded.texels = e->storage + pixels * 4;
    }
    /* The first dirty dependency reads all VRAM; the following calls cost
     * only the stale-tile check. Unrelated GPU writes do not invalidate us. */
    for (int y = 0; y < TILES_Y; ++y) for (int x = 0; x < TILES_X; ++x)
        if (e->dependencies.rows[y] & (1ull << x))
            vramSyncRect((VRect){x * 16, y * 16, x * 16 + 15, y * 16 + 15});
    uint64_t pk = domain("xgpal1");
    pk = word(pk, (unsigned)e->decoded.paletteSize);
    for (int i = 0; i < e->decoded.paletteSize; ++i) {
        uint16_t c = vramShadow[(r.clut >> 6) & 511][((r.clut & 63) * 16 + i) & 1023];
        e->decoded.palette[i] = c;
        pk = word(pk, c);
    }
    uint64_t oldPalette = e->decoded.paletteKey;
    e->decoded.paletteKey = e->decoded.paletteSize ? pk : 0;
    uint8_t* indices = e->storage + pixels * 4;
    /* Only the palette changed (a scene cycling its palettes redecoded whole
     * 256x256 pages, seven palettes of one, every frame: Lahan after the
     * attack, xg-hbc): the kept indices and content key stand, the colours
     * are made again from them. An unchanged palette keeps even those. */
    uint8_t* mask = depth == 2 ? NULL : indices + pixels;
    if (e->decoded.revision && depth != 2 && newestOf(&e->texTiles) <= e->texNewest) {
        if (e->decoded.paletteKey != oldPalette) {
            for (size_t i = 0; i < pixels; ++i) {
                if (mask[i]) memset(e->storage + i * 4, 0, 4);
                else rgba(e->storage + i * 4, e->decoded.palette[indices[i]]);
            }
            e->decoded.revision = ++revision;
        }
        e->newest = newest(e);
        stats.paletteOnly++;
        return &e->decoded;
    }
    uint64_t tk = word(word(byte(domain("xgtex1"), (uint8_t)e->decoded.depth), (unsigned)r.w), (unsigned)r.h);

    /* The usual request, no window and no wrap, addresses its texels row by
     * row without address()'s masking per texel: a scene's first frame
     * decodes every texture it draws at once (xg-8ld). */
    int plain = r.window == 0 && r.u >= 0 && r.v >= 0 && r.u + r.w <= 256 && r.v + r.h <= 256;
    int ptx = (r.tpage & 15) * 64, pty = ((r.tpage >> 4) & 1) * 256, pshift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) {
        int vx, vy, shift;
        if (plain) {
            int u = r.u + x;
            shift = depth == 0 ? (u & 3) * 4 : depth == 1 ? (u & 1) * 8 : 0;
            vx = (ptx + (u >> pshift)) & 1023;
            vy = (pty + r.v + y) & 511;
        } else {
            address(r, x, y, &vx, &vy, &shift);
        }
        uint16_t c = vramShadow[vy][vx];
        size_t at = (size_t)y * r.w + x;
        if (mask) mask[at] = 0;
        if (r.stableTo) {
            uint32_t st = vramStamp(vx, vy);
            if (st < r.stableFrom || st > r.stableTo) {
                /* Outside the stable region's load: clear, zero in the key. */
                if (mask) mask[at] = 1;
                if (depth == 2) { indices[at * 2] = indices[at * 2 + 1] = 0; tk = word(tk, 0); }
                else { indices[at] = 0; tk = byte(tk, 0); }
                memset(e->storage + at * 4, 0, 4);
                continue;
            }
        }
        if (depth == 2) {
            indices[at * 2] = (uint8_t)c;
            indices[at * 2 + 1] = (uint8_t)(c >> 8);
            tk = word(tk, c);
        } else {
            uint8_t index = (uint8_t)((c >> shift) & (depth == 0 ? 15 : 255));
            indices[at] = index;
            tk = byte(tk, index);
            c = e->decoded.palette[index];
        }
        rgba(e->storage + at * 4, c);
    }
    e->decoded.texelKey = tk;
    e->decoded.revision = ++revision;
    e->newest = newest(e);
    e->texNewest = newestOf(&e->texTiles);
    stats.decodes++;
    return &e->decoded;
}

uint64_t textureTexelKeyIn(const uint16_t* vram, TextureRequest r) {
    if (r.w < 1 || r.h < 1 || r.w > 256 || r.h > 256) return 0;
    r.tpage &= 0x19f;
    int depth = (r.tpage >> 7) & 3;
    if (depth == 3) { r.tpage &= ~0x80; depth = 2; }
    r.window &= 0xfffff;
    uint64_t tk = word(word(byte(domain("xgtex1"), (uint8_t)(depth == 0 ? 4 : depth == 1 ? 8 : 15)), (unsigned)r.w), (unsigned)r.h);
    for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) {
        int vx, vy, shift;
        address(r, x, y, &vx, &vy, &shift);
        uint16_t c = vram[(size_t)vy * VRAM_W + vx];
        if (depth == 2) tk = word(tk, c);
        else tk = byte(tk, (uint8_t)((c >> shift) & (depth == 0 ? 15 : 255)));
    }
    return tk;
}

uint64_t textureTexelKey(TextureRequest r) { return textureTexelKeyIn(&vramShadow[0][0], r); }

uint64_t texturePaletteKeyIn(const uint16_t* vram, uint16_t clut, int size) {
    uint64_t pk = domain("xgpal1");
    pk = word(pk, (unsigned)size);
    for (int i = 0; i < size; ++i) pk = word(pk, vram[(size_t)((clut >> 6) & 511) * VRAM_W + (((clut & 63) * 16 + i) & 1023)]);
    return size ? pk : 0;
}

/* Pre-filter only a primitive's patch: even a snapped upload can contain
 * other atlas images. 2x/4x variants (Edge, Upscaled 2x/4x), found by hash
 * and evicted least recently used within EDGE_BYTES, independently bounded
 * from the decoded cache: a battle screen draws far more than sixteen
 * distinct HUD, icon and text patches a frame (xg-j85.7). A decode of the
 * cache's own is found by its content (texels, palette, size and the
 * patch's place in it), not its revision: battle re-decodes its HUD
 * textures every frame with the same pixels. Other callers' textures (a
 * pack's recoloured copy, tests) and stable regions (texels outside the
 * load decode as clear, which their key does not tell from index 0) key by
 * revision as before. Every variant has a revision of its own, never reused
 * (not even after a reset or a state load), so the GPU's copies and smooth
 * motion tell an Edge variant from an Upscaled one of the same patch. */
#define EDGE_SLOTS 1024
#define EDGE_HASH (EDGE_SLOTS * 2)
#define EDGE_BYTES (64u << 20)
typedef struct {
    uint64_t a, b; /* texel and palette keys, or the decode revision and 0 */
    int content, reqW, reqH, depth, u, v, w, h, scale, labels;
} EdgeKey;
typedef struct {
    EdgeKey key;
    TextureSample sample;
    uint8_t* pixels;
    size_t bytes;
    uint32_t frame; /* hwVSyncCount() of its last use */
    int32_t chain, older, newer; /* hash chain / free list; LRU order (index + 1, 0 none) */
} EdgeEntry;
static EdgeEntry edges[EDGE_SLOTS];
static int32_t edgeBucket[EDGE_HASH], edgeOldest, edgeNewest, edgeFree, edgeTop, edgeCount;
static size_t edgeBytes;
static uint64_t edgeSerial;
static TextureSampleStats edgeStats;
/* The game's vblank count (system/hw.c); tests without that clock see 0. */
__attribute__((weak)) uint32_t hwVSyncCount(void) { return 0; }
void textureSampleReset(void) {
    for (int i = 0; i < EDGE_SLOTS; ++i) free(edges[i].pixels);
    memset(edges, 0, sizeof(edges)); memset(edgeBucket, 0, sizeof(edgeBucket));
    edgeOldest = edgeNewest = edgeFree = edgeTop = edgeCount = 0; edgeBytes = 0;
    memset(&edgeStats, 0, sizeof(edgeStats));
}
void textureSampleStats(TextureSampleStats* out) { *out = edgeStats; out->bytes = edgeBytes; out->entries = edgeCount; }
static EdgeKey edgeKey(const DecodedTexture* d, TextureRequest p, int scale, int labels) {
    /* A decode of the cache's own: its entry's storage, not a stable region. */
    uintptr_t at = (uintptr_t)d, first = (uintptr_t)&cache[0].decoded;
    int own = at >= first && at <= (uintptr_t)&cache[TEXTURE_CACHE_SLOTS - 1].decoded && (at - first) % sizeof(Entry) == 0 &&
              d->rgba == cache[(at - first) / sizeof(Entry)].storage && !d->request.stableTo && d->texelKey;
    EdgeKey k = {own ? d->texelKey : d->revision, own ? d->paletteKey : 0, own, d->request.w, d->request.h, d->depth,
                 own ? p.u - d->request.u : p.u, own ? p.v - d->request.v : p.v, p.w, p.h, scale, labels};
    if (!own) k.reqW = k.reqH = k.depth = 0;
    return k;
}
static uint32_t edgeHash(const EdgeKey* k) {
    uint64_t h = k->a * 0x9E3779B97F4A7C15ull ^ k->b * 0xD6E8FEB86659FD93ull;
    h ^= (uint64_t)(uint16_t)k->u | (uint64_t)(uint16_t)k->v << 16 | (uint64_t)k->w << 32 | (uint64_t)k->h << 44 |
         (uint64_t)k->scale << 56 | (uint64_t)k->labels << 62 | (uint64_t)k->content << 63;
    h ^= ((uint64_t)k->reqW << 16 | (uint64_t)k->reqH << 32 | (uint64_t)k->depth) * 0x94D049BB133111EBull;
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
    return (uint32_t)(h >> 32) % EDGE_HASH;
}
static int edgeSame(const EdgeKey* x, const EdgeKey* y) {
    return x->a == y->a && x->b == y->b && x->content == y->content && x->reqW == y->reqW && x->reqH == y->reqH &&
           x->depth == y->depth && x->u == y->u && x->v == y->v && x->w == y->w && x->h == y->h &&
           x->scale == y->scale && x->labels == y->labels;
}
static void edgeUnlink(EdgeEntry* e) {
    if (e->older) edges[e->older - 1].newer = e->newer; else edgeOldest = e->newer;
    if (e->newer) edges[e->newer - 1].older = e->older; else edgeNewest = e->older;
    e->older = e->newer = 0;
}
static void edgePush(EdgeEntry* e) {
    int32_t idx = (int32_t)(e - edges) + 1;
    e->older = edgeNewest; e->newer = 0;
    if (edgeNewest) edges[edgeNewest - 1].newer = idx; else edgeOldest = idx;
    edgeNewest = idx;
}
static void edgeDrop(EdgeEntry* e) {
    int32_t idx = (int32_t)(e - edges) + 1;
    int32_t* at = &edgeBucket[edgeHash(&e->key)];
    while (*at && *at != idx) at = &edges[*at - 1].chain;
    if (*at == idx) *at = e->chain;
    edgeUnlink(e);
    edgeBytes -= e->bytes; free(e->pixels); --edgeCount;
    memset(e, 0, sizeof(*e));
    e->chain = edgeFree; edgeFree = idx;
    ++edgeStats.evictions;
}
static const uint8_t* patchPixel(const DecodedTexture* d, TextureRequest p, int x, int y) {
    x = x < 0 ? 0 : x >= p.w ? p.w - 1 : x;
    y = y < 0 ? 0 : y >= p.h ? p.h - 1 : y;
    return d->rgba + ((size_t)(p.v + y - d->request.v) * d->request.w + p.u + x - d->request.u) * 4;
}
static int equalPixel(const uint8_t* a, const uint8_t* b) { return !memcmp(a, b, 4); }
static void edgePass(const DecodedTexture* d,TextureRequest p,uint8_t* pixels) {
    /* Scale2x corner rule. Compare opacity and STP as well as RGB. STP
     * remains the nearest original texel's even where a corner changes. */
    ++edgeStats.passes;
    for (int y = 0; y < p.h; ++y) for (int x = 0; x < p.w; ++x) {
        const uint8_t *c = patchPixel(d,p,x,y), *b = patchPixel(d,p,x,y-1),
            *h = patchPixel(d,p,x,y+1), *l = patchPixel(d,p,x-1,y), *r = patchPixel(d,p,x+1,y);
        const uint8_t* q[4] = {c,c,c,c};
        if (!equalPixel(b,h) && !equalPixel(l,r)) {
            if (equalPixel(l,b)) q[0] = l;
            if (equalPixel(b,r)) q[1] = r;
            if (equalPixel(l,h)) q[2] = l;
            if (equalPixel(h,r)) q[3] = r;
        }
        for (int k = 0; k < 4; ++k) {
            uint8_t* dst = pixels + ((size_t)(y*2 + k/2) * (p.w*2) + x*2 + k%2) * 4;
            memcpy(dst, q[k], 3); dst[3] = q[k][3] ? (c[3] == 128 ? 128 : 255) : 0;
        }
    }
}
static int patchFits(const DecodedTexture* d,TextureRequest p) {
    return d && p.w>=1 && p.h>=1 && p.w<=256 && p.h<=256 && p.u>=d->request.u && p.v>=d->request.v &&
        p.u+p.w<=d->request.u+d->request.w && p.v+p.h<=d->request.v+d->request.h;
}
/* XG_TEXTURE_LOG=1: a line for each vblank whose draws made variants, and a
 * running total every 300 vblanks with any (opt-in; read once). */
static void edgeLog(uint32_t frame) {
    static int log = -1;
    static uint32_t logged, summary;
    static TextureSampleStats before;
    if (log < 0) { const char* e = getenv("XG_TEXTURE_LOG"); log = e && *e && strcmp(e, "0"); }
    if (!log || frame == logged) return;
    TextureSampleStats now; textureSampleStats(&now);
    if (now.made < before.made || now.hits < before.hits) memset(&before, 0, sizeof(before)); /* reset */
    if (now.made != before.made)
        fprintf(stderr, "texture: vblank %u: upscale %llu hits, %llu made (%llu Scale2x passes, %llu KiB), %llu evicted, %llu KiB drawn\n",
                (unsigned)logged, (unsigned long long)(now.hits - before.hits), (unsigned long long)(now.made - before.made),
                (unsigned long long)(now.passes - before.passes), (unsigned long long)((now.madeBytes - before.madeBytes) >> 10),
                (unsigned long long)(now.evictions - before.evictions), (unsigned long long)((now.frameBytes - before.frameBytes) >> 10));
    if (frame - summary >= 300) {
        summary = frame;
        fprintf(stderr, "texture: vblank %u: upscale totals %llu hits, %llu made, %llu Scale2x passes, %llu evicted, %llu MiB drawn; %d variants, %.1f MiB\n",
                (unsigned)frame, (unsigned long long)now.hits, (unsigned long long)now.made, (unsigned long long)now.passes,
                (unsigned long long)now.evictions, (unsigned long long)(now.frameBytes >> 20), now.entries, (double)now.bytes / (1024 * 1024));
    }
    before = now; logged = frame;
}
static int scaledSample(const DecodedTexture* d,TextureRequest p,int scale,int nativeLabels,TextureSample* out) {
    if(!patchFits(d,p) || (scale!=2 && scale!=4))return 0;
    uint32_t frame = hwVSyncCount();
    edgeLog(frame);
    EdgeKey key = edgeKey(d, p, scale, nativeLabels);
    uint32_t hash = edgeHash(&key);
    for (int32_t i = edgeBucket[hash]; i; i = edges[i - 1].chain) {
        EdgeEntry* e = &edges[i - 1];
        if (!edgeSame(&e->key, &key)) continue;
        if (edgeNewest != i) { edgeUnlink(e); edgePush(e); }
        if (e->frame != frame) edgeStats.frameBytes += e->bytes;
        e->frame = frame; ++edgeStats.hits;
        *out = e->sample; out->u = p.u; out->v = p.v; /* equal content may lie elsewhere */
        return 1;
    }
    size_t bytes = (size_t)p.w*p.h*scale*scale*4;
    /* The least recently used variants make room (slots and bytes). */
    while (edgeOldest && ((!edgeFree && edgeTop == EDGE_SLOTS) || edgeBytes + bytes > EDGE_BYTES))
        edgeDrop(&edges[edgeOldest - 1]);
    uint8_t* pixels=malloc(bytes);
    uint8_t* mid=scale==4?malloc((size_t)p.w*p.h*16):NULL;
    if(!pixels || (scale==4 && !mid)) {free(pixels);free(mid);return 0;}
    /* Tiny labels already carry authored antialiasing: retain their exact
     * pixels rather than changing the letter shapes at this size. */
    if(nativeLabels && p.h<=8) {
        for(int y=0;y<p.h*scale;y++)for(int x=0;x<p.w*scale;x++)
            memcpy(pixels+((size_t)y*p.w*scale+x)*4,patchPixel(d,p,x/scale,y/scale),4);
        free(mid);mid=NULL;
    } else edgePass(d,p,mid?mid:pixels);
    if(mid) {
        DecodedTexture twice={.request={.w=p.w*2,.h=p.h*2},.rgba=mid};
        edgePass(&twice,twice.request,pixels);free(mid);
    }
    int32_t idx = edgeFree ? edgeFree : ++edgeTop;
    EdgeEntry* e = &edges[idx - 1];
    if (edgeFree) edgeFree = e->chain;
    e->key = key; e->pixels = pixels; e->bytes = bytes; e->frame = frame;
    e->sample = (TextureSample){pixels,UINT64_C(0x5000000000000000)|++edgeSerial,p.u,p.v,p.w,p.h,scale,NULL,0};
    e->chain = edgeBucket[hash]; edgeBucket[hash] = idx;
    edgePush(e);
    edgeBytes += bytes; ++edgeCount; ++edgeStats.made; edgeStats.madeBytes += bytes; edgeStats.frameBytes += bytes;
    *out=e->sample;return 1;
}
int textureUpscaleSample(const DecodedTexture* d,TextureRequest p,int scale,TextureSample* out) {
    return scaledSample(d,p,scale,1,out);
}
int textureSampleGet(const DecodedTexture* d, TextureRequest p, int filter, TextureSample* out) {
    if(!patchFits(d,p))return 0;
    if(filter==TEXTURE_FILTER_EDGE)return scaledSample(d,p,2,0,out);
    *out=(TextureSample){d->rgba,d->revision,d->request.u,d->request.v,d->request.w,d->request.h,1,NULL,0};
    return 1;
}

