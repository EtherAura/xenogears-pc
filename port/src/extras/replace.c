/*
 * The installed replacement packs for the extras' pictures (replace.h): the
 * game's content-keyed look-ups, from a view's VRAM.
 */

#include "replace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/texture.h"
#include "system/engine.h"
#include "pc_wide.h"

/* ---- the images found, kept for the views ------------------------------------------------- */

/* The library's pixels stay valid only until its next look-up; the views
 * upload them a moment later, so each image found is copied here, kept by
 * its revision (unique per loaded image) until it has not been asked for in
 * a while and the store is over its budget. */
#define STORE_SLOTS 4096
#define STORE_BYTES ((size_t)384 << 20)
typedef struct {
    uint64_t id, touched;
    XgReplaceImage image;
    size_t bytes;
} Stored;
static Stored store[STORE_SLOTS];
static size_t storeBytes;
static uint64_t storeClock;

static Stored* storeFind(uint64_t id) {
    unsigned h = (unsigned)((id ^ (id >> 31)) * UINT64_C(0x9E3779B97F4A7C15) >> 52) & (STORE_SLOTS - 1);
    for (unsigned n = 0; n < STORE_SLOTS; n++, h = (h + 1) & (STORE_SLOTS - 1)) {
        if (store[h].id == id) return &store[h];
        if (!store[h].id) return NULL;
    }
    return NULL;
}

/* Remove slot i and re-place the entries after it (linear probing). */
static void storeDrop(unsigned i) {
    storeBytes -= store[i].bytes;
    free((void*)store[i].image.pixels);
    memset(&store[i], 0, sizeof(store[i]));
    for (unsigned j = (i + 1) & (STORE_SLOTS - 1); store[j].id; j = (j + 1) & (STORE_SLOTS - 1)) {
        Stored e = store[j];
        memset(&store[j], 0, sizeof(store[j]));
        unsigned h = (unsigned)((e.id ^ (e.id >> 31)) * UINT64_C(0x9E3779B97F4A7C15) >> 52) & (STORE_SLOTS - 1);
        while (store[h].id) h = (h + 1) & (STORE_SLOTS - 1);
        store[h] = e;
    }
}

static void storeMakeRoom(size_t bytes, int slots) {
    for (;;) {
        int used = 0;
        for (unsigned i = 0; i < STORE_SLOTS; i++) used += store[i].id != 0;
        if (storeBytes + bytes <= STORE_BYTES && used + slots < STORE_SLOTS * 3 / 4) return;
        /* the least recently asked for, not asked for in the last two look-ups */
        unsigned victim = STORE_SLOTS;
        for (unsigned i = 0; i < STORE_SLOTS; i++)
            if (store[i].id && store[i].touched + 2 < storeClock &&
                (victim == STORE_SLOTS || store[i].touched < store[victim].touched)) victim = i;
        if (victim == STORE_SLOTS) return; /* everything in use: over budget for now */
        storeDrop(victim);
    }
}

/* The image's id, copied into the store if new; 0 when it cannot be kept. */
static uint64_t storeKeep(uint64_t id, const uint8_t* pixels, int pw, int ph, int indexed) {
    if (!id || !pixels) return 0;
    Stored* s = storeFind(id);
    if (!s) {
        size_t bytes = (size_t)pw * ph * (indexed ? 1 : 4);
        storeMakeRoom(bytes, 1);
        uint8_t* copy = malloc(bytes);
        if (!copy) return 0;
        memcpy(copy, pixels, bytes);
        unsigned h = (unsigned)((id ^ (id >> 31)) * UINT64_C(0x9E3779B97F4A7C15) >> 52) & (STORE_SLOTS - 1);
        while (store[h].id) h = (h + 1) & (STORE_SLOTS - 1);
        s = &store[h];
        s->id = id;
        s->image = (XgReplaceImage){copy, pw, ph, indexed};
        s->bytes = bytes;
        storeBytes += bytes;
    }
    s->touched = storeClock;
    return id;
}

/* Asked for again: 1 while it is held. */
static int storeTouch(uint64_t id) {
    Stored* s = id ? storeFind(id) : NULL;
    if (s) s->touched = storeClock;
    return s != NULL;
}

const XgReplaceImage* xgReplaceImage(uint64_t id) {
    Stored* s = id ? storeFind(id) : NULL;
    return s ? &s->image : NULL;
}

unsigned xgReplaceInstalled(void) {
    unsigned on = 0;
    if (texturePackEnabled()) on |= XG_REPLACE_PACK;
    if (textureSpriteCelEnabled()) {
        on |= XG_REPLACE_CELS;
        if (engineThis() == ENGINE_ENHANCED) on |= XG_REPLACE_POSES;
    }
    return on;
}

unsigned xgReplaceChosen(void) {
    unsigned on = 0, installed = xgReplaceInstalled();
    /* the 3D textures' appearance (Cel 8x, or the user's own pack for every class) */
    if ((installed & XG_REPLACE_PACK) && (textureUsePack(PC_TEX_3D) || texturePackChosen())) on |= XG_REPLACE_PACK;
    /* Sprites -> Cel 8x */
    if (textureUseSpriteCel(PC_TEX_SPRITE)) on |= installed & (XG_REPLACE_CELS | XG_REPLACE_POSES);
    return on;
}

/* ---- a view's answers ------------------------------------------------------------------------ */

#define MEMO_SLOTS 8192 /* answers per primitive signature */
#define KEY_SLOTS 8192  /* texel keys per request */
#define PAGES 96        /* stable region analyses: 32 pages at three depths */
#define BLOCK 3         /* 8x8 blocks, as vram.c's stable regions */
#define BLOCKS ((256 >> BLOCK) * (256 >> BLOCK))
#define STABLE_MIN 8192
#define STABLE_PARTS 256

typedef struct {
    uint16_t tpage, clut;
    uint32_t window;
    int16_t u, v, w, h; /* the texture coordinates' bounds */
    uint8_t valid, libraries, flags;
    XgReplace answer;
} Memo;

typedef struct {
    uint16_t tpage;
    int16_t u, v, w, h;
    uint8_t valid;
    uint64_t key;
} KeyMemo;

typedef struct {
    int valid, tx, ty, x1, bw, bh;
    uint16_t blockPart[BLOCKS]; /* 0xFFFF: no texels */
    int parts;
    struct { uint32_t count; int x0, y0, x1, y1; } part[STABLE_PARTS];
} Page;

struct XgReplacer {
    unsigned version;
    uint64_t identity;
    const uint16_t* vram;
    int haveVram;
    Memo memo[MEMO_SLOTS];
    KeyMemo keys[KEY_SLOTS];
    Page pages[PAGES];
};

XgReplacer* xgReplacerCreate(void) { return calloc(1, sizeof(XgReplacer)); }
void xgReplacerDestroy(XgReplacer* r) { free(r); }

static void forget(XgReplacer* r) {
    for (int i = 0; i < MEMO_SLOTS; i++) r->memo[i].valid = 0;
    for (int i = 0; i < KEY_SLOTS; i++) r->keys[i].valid = 0;
    for (int i = 0; i < PAGES; i++) r->pages[i].valid = 0;
}

static uint64_t texelKey(XgReplacer* r, TextureRequest q) {
    uint64_t k = (uint64_t)(q.tpage & 0x19f) | (uint64_t)(uint16_t)q.u << 9 | (uint64_t)(uint16_t)q.v << 25 |
                 (uint64_t)(uint16_t)q.w << 41;
    k = (k ^ (uint64_t)q.h * UINT64_C(0xBF58476D1CE4E5B9)) * UINT64_C(0x94D049BB133111EB);
    KeyMemo* m = &r->keys[(k >> 40) & (KEY_SLOTS - 1)];
    if (m->valid && m->tpage == (q.tpage & 0x19f) && m->u == q.u && m->v == q.v && m->w == q.w && m->h == q.h) return m->key;
    m->valid = 1; m->tpage = q.tpage & 0x19f; m->u = (int16_t)q.u; m->v = (int16_t)q.v; m->w = (int16_t)q.w; m->h = (int16_t)q.h;
    m->key = textureTexelKeyIn(r->vram, q);
    return m->key;
}

/* vram.c's page analysis with every texel the VRAM holds as one load: the
 * page's 8x8 blocks with texels, 8-connected into parts, each with the box
 * and count of its non-zero texels. */
static Page* pageOf(XgReplacer* r, int tpage) {
    int depth = (tpage >> 7) & 3, shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int slot = (tpage & 31) + 32 * (depth > 2 ? 2 : depth);
    Page* p = &r->pages[slot];
    if (p->valid) return p;
    p->valid = 1;
    p->tx = (tpage & 15) * 64; p->ty = ((tpage >> 4) & 1) * 256;
    p->x1 = p->tx + (256 >> shift) - 1;
    if (p->x1 >= XG_VRAM_W) p->x1 = XG_VRAM_W - 1;
    p->bw = ((p->x1 - p->tx) >> BLOCK) + 1; p->bh = 256 >> BLOCK;
    static uint32_t count[BLOCKS];
    static int16_t box[BLOCKS][4];
    int blocks = p->bw * p->bh;
    for (int b = 0; b < blocks; b++) { count[b] = 0; box[b][0] = box[b][1] = 32767; box[b][2] = box[b][3] = -1; }
    for (int y = p->ty; y < p->ty + 256; y++)
        for (int x = p->tx; x <= p->x1; x++) {
            if (!r->vram[(size_t)y * XG_VRAM_W + x]) continue;
            int b = ((y - p->ty) >> BLOCK) * p->bw + ((x - p->tx) >> BLOCK);
            count[b]++;
            if (x < box[b][0]) box[b][0] = (int16_t)x;
            if (y < box[b][1]) box[b][1] = (int16_t)y;
            if (x > box[b][2]) box[b][2] = (int16_t)x;
            if (y > box[b][3]) box[b][3] = (int16_t)y;
        }
    for (int b = 0; b < blocks; b++) p->blockPart[b] = 0xFFFF;
    p->parts = 0;
    static uint16_t queue[BLOCKS];
    for (int b = 0; b < blocks; b++) {
        if (!count[b] || p->blockPart[b] != 0xFFFF) continue;
        if (p->parts == STABLE_PARTS) break;
        int part = p->parts++;
        p->part[part].count = 0;
        p->part[part].x0 = p->part[part].y0 = 32767; p->part[part].x1 = p->part[part].y1 = -1;
        int head = 0, tail = 0;
        queue[tail++] = (uint16_t)b;
        p->blockPart[b] = (uint16_t)part;
        while (head < tail) {
            int q = queue[head++], bx = q % p->bw, by = q / p->bw;
            p->part[part].count += count[q];
            if (box[q][0] < p->part[part].x0) p->part[part].x0 = box[q][0];
            if (box[q][1] < p->part[part].y0) p->part[part].y0 = box[q][1];
            if (box[q][2] > p->part[part].x1) p->part[part].x1 = box[q][2];
            if (box[q][3] > p->part[part].y1) p->part[part].y1 = box[q][3];
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = bx + dx, ny = by + dy;
                    if (nx < 0 || ny < 0 || nx >= p->bw || ny >= p->bh) continue;
                    int n = ny * p->bw + nx;
                    if (!count[n] || p->blockPart[n] != 0xFFFF) continue;
                    p->blockPart[n] = (uint16_t)part;
                    queue[tail++] = (uint16_t)n;
                }
        }
    }
    return p;
}

/* The stable region holding the patch's texels (VRAM x0..x1, y0..y1), as
 * vram.c vramStableRegion decides it: 1 and the request. */
static int stableRegion(XgReplacer* r, TextureRequest q, int x0, int y0, int x1, int y1, TextureRequest* out) {
    int depth = (q.tpage >> 7) & 3, shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    Page* p = pageOf(r, q.tpage);
    if (x0 < p->tx || x1 > p->x1 || y0 < p->ty || y1 > p->ty + 255) return 0;
    int part = -1;
    for (int by = (y0 - p->ty) >> BLOCK; by <= (y1 - p->ty) >> BLOCK; by++)
        for (int bx = (x0 - p->tx) >> BLOCK; bx <= (x1 - p->tx) >> BLOCK; bx++) {
            int k = p->blockPart[by * p->bw + bx];
            if (k == 0xFFFF) continue;
            if (part >= 0 && k != part) return 0;
            part = k;
        }
    if (part < 0 || p->part[part].count < STABLE_MIN) return 0;
    int bx0 = p->part[part].x0, by0 = p->part[part].y0, bx1 = p->part[part].x1, by1 = p->part[part].y1;
    if ((uint64_t)p->part[part].count * 2 < (uint64_t)(bx1 - bx0 + 1) * (by1 - by0 + 1)) return 0;
    if (x0 < bx0 || y0 < by0 || x1 > bx1 || y1 > by1) return 0;
    *out = q;
    out->u = (bx0 - p->tx) << shift; out->v = by0 - p->ty;
    out->w = (bx1 - bx0 + 1) << shift; out->h = by1 - by0 + 1;
    return out->w <= 256 && out->h <= 256;
}

/* The patch snapped to the load holding it, as texture.c's snap: 1 and the request. */
static int uploadCover(const XgVgpu* gpu, TextureRequest q, int x0, int y0, int x1, int y1, TextureRequest* out) {
    int rect[4];
    if (!gpu || !xgVgpuUploadCover(gpu, x0, y0, x1, y1, rect)) return 0;
    int depth = (q.tpage >> 7) & 3, shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int tx = (q.tpage & 15) * 64, ty = ((q.tpage >> 4) & 1) * 256;
    int u = (rect[0] - tx) * (1 << shift), v = rect[1] - ty;
    int w = (rect[2] - rect[0] + 1) * (1 << shift), h = rect[3] - rect[1] + 1;
    if (u < 0 || v < 0 || u + w > 256 || v + h > 256) return 0;
    *out = q;
    out->u = u; out->v = v; out->w = w; out->h = h;
    return 1;
}

static int sameRect(TextureRequest a, TextureRequest b) { return a.u == b.u && a.v == b.v && a.w == b.w && a.h == b.h; }

/* One look-up: 1 with *o filled, 0 none, -1 loading. */
static int find(XgReplacer* r, int library, TextureRequest q, int paletteSize, XgReplace* o) {
    uint64_t key = texelKey(r, q);
    if (!key) return 0;
    if (library == TEXTURE_FIND_CEL && !textureSpriteCelKnown(key)) return 0;
    uint64_t palette = library == TEXTURE_FIND_PACK ? texturePaletteKeyIn(r->vram, q.clut, paletteSize) : 0;
    TexturePackImage image;
    int found = texturePackFind(library, key, palette, q.w, q.h, paletteSize, &image);
    if (found <= 0) return found;
    int indexed = image.indices != NULL;
    uint64_t id = storeKeep(image.revision, indexed ? image.indices : image.rgba, image.w * image.scale,
                            image.h * image.scale, indexed);
    if (!id) return 0;
    o->kind = indexed ? XG_REPLACE_INDEXED : XG_REPLACE_RGBA;
    o->scale = (uint8_t)image.scale;
    o->u = (int16_t)q.u; o->v = (int16_t)q.v; o->w = (int16_t)q.w; o->h = (int16_t)q.h;
    o->id = id;
    return 1;
}

static void replacePrim(XgReplacer* r, unsigned libraries, const XgVgpu* gpu, const XgPrim* p, XgReplace* o, int* pending) {
    memset(o, 0, sizeof(*o));
    if (!(p->flags & XG_PRIM_TEXTURED) || (p->flags & (XG_PRIM_LINE | XG_PRIM_FILL)) ||
        (p->count != 3 && p->count != 4) || (p->window & 0xFFFFF)) return;
    /* A viewer's own picture (FREE_UV: corners from the page's corner, the last texel's far
     * edge) counts while it stays inside one page, as the game would draw it. */
    int edge = (p->flags & XG_PRIM_FREE_UV) ? 1 : 0;
    int u0 = 32767, v0 = 32767, u1 = -32768, v1 = -32768;
    for (int k = 0; k < p->count; k++) {
        u0 = p->u[k] < u0 ? p->u[k] : u0; u1 = p->u[k] - edge > u1 ? p->u[k] - edge : u1;
        v0 = p->v[k] < v0 ? p->v[k] : v0; v1 = p->v[k] - edge > v1 ? p->v[k] - edge : v1;
    }
    if (u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255 || u1 < u0 || v1 < v0) return;
    int depth = (p->tpage >> 7) & 3;
    uint16_t tpage = (uint16_t)(depth == 3 ? (p->tpage & ~0x80) : p->tpage);
    depth = depth == 3 ? 2 : depth;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0, paletteSize = depth == 0 ? 16 : depth == 1 ? 256 : 0;
    TextureRequest patch = {tpage, (uint16_t)(depth == 2 ? 0 : p->clut & 0x7fff), 0, u0, v0, u1 - u0 + 1, v1 - v0 + 1, 0, 0};
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256;
    int x0 = tx + (u0 >> shift), x1 = tx + (u1 >> shift), y0 = ty + v0, y1 = ty + v1;
    if (x1 >= XG_VRAM_W) return;
    TextureRequest cover, stable;
    int haveCover = uploadCover(gpu, patch, x0, y0, x1, y1, &cover);
    int found = 0;
    if (libraries & XG_REPLACE_PACK) {
        /* the game's 3D snap (cover, else the stable region), and the patch as other classes draw it */
        int haveStable = !haveCover && stableRegion(r, patch, x0, y0, x1, y1, &stable);
        if (haveCover) found = find(r, TEXTURE_FIND_PACK, cover, paletteSize, o);
        if (!found && haveStable) found = find(r, TEXTURE_FIND_PACK, stable, paletteSize, o);
        if (!found && (!haveCover || !sameRect(cover, patch)) && (!haveStable || !sameRect(stable, patch)))
            found = find(r, TEXTURE_FIND_PACK, patch, paletteSize, o);
    }
    if (found <= 0 && (libraries & XG_REPLACE_CELS) && paletteSize) {
        /* the load holding the part (a piece sent to VRAM for this frame is its cell), then the
         * patch and the cells a texel larger (textureSpriteCelSample: a sheet's cells, whose
         * load the game no longer has whole once the field has written beside it) */
        static const uint8_t order[][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 1, 0}, {0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0},
                                           {1, 0, 1, 0}, {0, 1, 0, 1}, {1, 1, 0, 0}, {1, 0, 0, 1}, {0, 1, 1, 0}, {1, 1, 1, 0},
                                           {1, 1, 0, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}};
        int got = haveCover ? find(r, TEXTURE_FIND_CEL, cover, paletteSize, o) : 0;
        for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]) && got == 0; i++) {
            TextureRequest c = patch;
            c.u -= order[i][0]; c.v -= order[i][1]; c.w += order[i][0] + order[i][2]; c.h += order[i][1] + order[i][3];
            if (c.u < 0 || c.v < 0 || c.u + c.w > 256 || c.v + c.h > 256) continue;
            got = find(r, TEXTURE_FIND_CEL, c, paletteSize, o);
        }
        if (got) found = got;
    }
    if (found < 0) {
        ++*pending;
        memset(o, 0, sizeof(*o));
    }
}

/* ---- poses (gp0.c spritePoseKey, from the primitives) --------------------------------------- */

typedef struct { uint64_t key; int u, v, w, h, a, b, fx, fy; } PoseItem;
static int poseItemOrder(const void* x, const void* y) {
    const PoseItem *p = x, *q = y;
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    const int l[8] = {p->u, p->v, p->w, p->h, p->a, p->b, p->fx, p->fy}, m[8] = {q->u, q->v, q->w, q->h, q->a, q->b, q->fx, q->fy};
    for (int i = 0; i < 8; i++) if (l[i] != m[i]) return l[i] < m[i] ? -1 : 1;
    return 0;
}
static uint64_t fnvBytes(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * UINT64_C(0x100000001b3);
    return h;
}
static uint64_t fnvLE(uint64_t h, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; i++) h = (h ^ (uint8_t)(value >> (8 * i))) * UINT64_C(0x100000001b3);
    return h;
}

#define POSE_PARTS 16 /* texture.h's */
typedef struct { double ox, oy, ax, ay, bx, by; } Frame;

/* A part's cell (the sub-sprite's texture rectangle) from its quad: renderSpriteActor
 * gives u .. u + w - 1 and v .. v + h - 1, u one less when the quad is mirrored
 * (vertex 3 left of vertex 0). */
static TextureRequest partCell(const XgPrim* p) {
    int u = p->u[0], w = p->u[1] - p->u[0] + 1;
    if (p->x[3] < p->x[0]) u += 1;
    return (TextureRequest){p->tpage, p->clut, 0, u, p->v[0], w, p->v[2] - p->v[0] + 1, 0, 0};
}

/* The pose for the actor's parts (prims[part[i]]), as the game names it: 1 with its
 * quad in *o (the parts then skipped). */
static int pose(XgReplacer* r, const XgPrim* prims, const int* part, int n, XgReplace* o) {
    PoseItem item[POSE_PARTS];
    TextureRequest cell[POSE_PARTS];
    int ref = 0;
    for (int i = 0; i < n; i++) {
        cell[i] = partCell(&prims[part[i]]);
        if (cell[i].w < 1 || cell[i].h < 1 || cell[i].u < 0 || cell[i].u + cell[i].w > 256) return 0;
        item[i] = (PoseItem){textureTexelKeyIn(r->vram, cell[i]), cell[i].u, cell[i].v, cell[i].w, cell[i].h, 0, 0, 0, 0};
        int area = cell[i].w * cell[i].h, best = cell[ref].w * cell[ref].h;
        if (area > best || (area == best && (item[i].key < item[ref].key ||
            (item[i].key == item[ref].key && (cell[i].u < cell[ref].u || (cell[i].u == cell[ref].u && cell[i].v < cell[ref].v)))))) ref = i;
    }
    const XgPrim* rp = &prims[part[ref]];
    Frame f = {rp->x[0], rp->y[0], (double)(rp->x[1] - rp->x[0]) / cell[ref].w, (double)(rp->y[1] - rp->y[0]) / cell[ref].w,
               (double)(rp->x[2] - rp->x[0]) / cell[ref].h, (double)(rp->y[2] - rp->y[0]) / cell[ref].h};
    double det = f.ax * f.by - f.ay * f.bx;
    if (fabs(det) < 1e-9) return 0;
    for (int i = 0; i < n; i++) {
        const XgPrim* p = &prims[part[i]];
        double a[3], b[3];
        for (int k = 0; k < 3; k++) {
            double dx = p->x[k] - f.ox, dy = p->y[k] - f.oy;
            a[k] = (dx * f.by - dy * f.bx) / det;
            b[k] = (f.ax * dy - f.ay * dx) / det;
        }
        item[i].a = (int)lround(a[0]); item[i].b = (int)lround(b[0]);
        item[i].fx = a[1] - a[0] < 0; item[i].fy = b[2] - b[0] < 0;
    }
    qsort(item, (size_t)n, sizeof(item[0]), poseItemOrder);
    uint64_t h = fnvBytes(UINT64_C(0xcbf29ce484222325), "xgpose1", 7);
    h = fnvLE(h, (uint64_t)n, 1);
    for (int i = 0; i < n; i++) {
        h = fnvLE(h, item[i].key, 8);
        h = fnvLE(h, (uint64_t)item[i].u, 2); h = fnvLE(h, (uint64_t)item[i].v, 2);
        h = fnvLE(h, (uint64_t)item[i].w, 2); h = fnvLE(h, (uint64_t)item[i].h, 2);
        h = fnvLE(h, (uint64_t)(uint16_t)item[i].a, 2); h = fnvLE(h, (uint64_t)(uint16_t)item[i].b, 2);
        h = fnvLE(h, (uint64_t)item[i].fx, 1); h = fnvLE(h, (uint64_t)item[i].fy, 1);
    }
    const SpritePose* s = texturePoseGet(h);
    static int log = -1;
    if (log < 0) { const char* e = getenv("XG_TEXTURE_LOG"); log = e && *e && strcmp(e, "0"); }
    if (log) fprintf(stderr, "extras: sprite pose: %d parts key %016llx %s\n", n, (unsigned long long)h, s ? "drawn" : "not in the library");
    if (!s) return 0;
    uint64_t id = storeKeep(s->revision, s->indices, s->w * s->scale, s->h * s->scale, 1);
    if (!id) return 0;
    memset(o, 0, sizeof(*o));
    o->kind = XG_REPLACE_INDEXED;
    o->pose = 1;
    o->scale = (uint8_t)s->scale;
    o->w = (int16_t)s->w; o->h = (int16_t)s->h;
    o->clut = rp->clut;
    o->id = id;
    /* corners top left, top right, bottom left, bottom right (gp0.c spritePose) */
    double tl[2] = {f.ox + s->a[0] * f.ax + s->b[0] * f.bx, f.oy + s->a[0] * f.ay + s->b[0] * f.by};
    double tr[2] = {f.ox + s->a[1] * f.ax + s->b[1] * f.bx, f.oy + s->a[1] * f.ay + s->b[1] * f.by};
    double bl[2] = {f.ox + s->a[2] * f.ax + s->b[2] * f.bx, f.oy + s->a[2] * f.ay + s->b[2] * f.by};
    o->x[0] = (float)tl[0]; o->y[0] = (float)tl[1];
    o->x[1] = (float)tr[0]; o->y[1] = (float)tr[1];
    o->x[2] = (float)bl[0]; o->y[2] = (float)bl[1];
    o->x[3] = (float)(tr[0] + bl[0] - tl[0]); o->y[3] = (float)(tr[1] + bl[1] - tl[1]);
    return 1;
}

/* ---- every primitive ------------------------------------------------------------------------ */

int xgReplacePrims(XgReplacer* r, unsigned libraries, const XgVgpu* gpu, const uint16_t* vram, unsigned version,
                   uint64_t identity, const XgPrim* prims, int n, int poseParts, XgReplace* out, int* pending) {
    *pending = 0;
    ++storeClock;
    libraries &= xgReplaceInstalled();
    if (!r || !vram || n <= 0) {
        for (int i = 0; i < n; i++) memset(&out[i], 0, sizeof(out[i]));
        return 0;
    }
    if (!r->haveVram || r->vram != vram || r->version != version || r->identity != identity) {
        forget(r);
        r->haveVram = 1; r->vram = vram; r->version = version; r->identity = identity;
    }
    int replaced = 0;
    int part[POSE_PARTS], parts = 0;
    for (int i = 0; i < n; i++) {
        const XgPrim* p = &prims[i];
        if (poseParts > 0 && (p->flags & XG_PRIM_TEXTURED) && p->count == 4 && !(p->flags & (XG_PRIM_LINE | XG_PRIM_FILL))) {
            if (parts < POSE_PARTS) part[parts] = i;
            parts++;
        }
        int u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        for (int c = 0; c < p->count && c < 4; c++) {
            if (!c || p->u[c] < u0) u0 = p->u[c];
            if (!c || p->v[c] < v0) v0 = p->v[c];
            if (!c || p->u[c] > u1) u1 = p->u[c];
            if (!c || p->v[c] > v1) v1 = p->v[c];
        }
        uint8_t flags = (uint8_t)((p->flags & (XG_PRIM_TEXTURED | XG_PRIM_LINE | XG_PRIM_FILL | XG_PRIM_FREE_UV)) | (p->count >= 3 ? 0x80 : 0));
        uint64_t k = (uint64_t)p->tpage | (uint64_t)p->clut << 16 | (uint64_t)(uint16_t)u0 << 32 | (uint64_t)(uint16_t)v0 << 48;
        k ^= ((uint64_t)(uint16_t)u1 | (uint64_t)(uint16_t)v1 << 16 | (uint64_t)flags << 32) * UINT64_C(0x9E3779B97F4A7C15);
        k ^= (uint64_t)p->window * UINT64_C(0xD6E8FEB86659FD93);
        k = (k ^ (k >> 29)) * UINT64_C(0xBF58476D1CE4E5B9);
        Memo* m = &r->memo[(k >> 40) & (MEMO_SLOTS - 1)];
        if (m->valid && m->libraries == libraries && m->tpage == p->tpage && m->clut == p->clut && m->window == p->window &&
            m->flags == flags && m->u == u0 && m->v == v0 && m->w == u1 && m->h == v1 &&
            (m->answer.kind == XG_REPLACE_NONE || storeTouch(m->answer.id))) {
            out[i] = m->answer;
        } else {
            int wait = 0;
            replacePrim(r, libraries, gpu, p, &out[i], &wait);
            *pending += wait;
            if (!wait) {
                m->valid = 1; m->libraries = (uint8_t)libraries; m->tpage = p->tpage; m->clut = p->clut;
                m->window = p->window; m->flags = flags;
                m->u = (int16_t)u0; m->v = (int16_t)v0; m->w = (int16_t)u1; m->h = (int16_t)v1;
                m->answer = out[i];
            }
        }
        replaced += out[i].kind != XG_REPLACE_NONE;
    }
    if ((libraries & XG_REPLACE_POSES) && poseParts > 0 && parts == poseParts && parts <= POSE_PARTS) {
        XgReplace o;
        if (pose(r, prims, part, parts, &o)) {
            for (int i = 0; i < parts; i++) {
                replaced += out[part[i]].kind == XG_REPLACE_NONE;
                memset(&out[part[i]], 0, sizeof(out[0]));
                out[part[i]].kind = XG_REPLACE_SKIP;
            }
            /* drawn at the first part the GPU meets */
            out[part[0]] = o;
        }
    }
    return replaced;
}
