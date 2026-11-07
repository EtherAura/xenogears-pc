/*
 * GP0: the PS1 GPU's command port, as the DMA feeds it from an ordering
 * table (or the CPU from DrawPrim). Words go through a small state machine
 * like the GPU's FIFO, so commands may span packets and several commands may
 * share one (MargePrim). Drawing commands become triangles for vk.c; VRAM
 * transfers go to libgpu.c's helpers, which keep the CPU copy exact.
 *
 * Hazards inside a batch: a batch is drawn by one vkCmdDraw, where nothing
 * orders one primitive's texture reads against another's writes (and, without
 * interlock, writes against writes). The tiles each batch has read and written
 * are tracked, and a primitive that would read what the batch wrote, or write
 * what it read, starts a new batch. A primitive that reads its own pixels is
 * drawn in steps (selfDraw).
 *
 * The hi-res copy (hires.c): each primitive also goes to it as real
 * triangles (hiTriangle, hiQuad), in the same batch, with the texels the 1x
 * walk sampled as their texture coordinates' clamp. Its edges may reach one
 * 1x pixel past the 1x pixels, so with the copy on, batches track write
 * boxes one pixel larger.
 */

#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <math.h>

#include "gpu_internal.h"
#include "texture.h"
#include "portrait_pack.h"
#include "interpolation.h"
#include "shaders/prompt.glsl"
#include "pc_wide.h"
#include "gte_pc.h"
#include "pc_geometry.h"
#include "system/cputime.h"
#include "../system/savestate.h"

typedef struct {
    int x, y;
    int u, v;
    int r, g, b;
    float preciseX, preciseY, depth;
    int precise;
    int fallback;   /* poseVertex: no subpixel projection; preciseX/Y hold the integer position */
} PVertex;

/* A batch's native written and read rectangles, exact, beside their tiles: two rectangles in
 * one 16-pixel tile need not meet (nativeHit). Overflowing, the tiles alone decide. */
#define EXACT_RECTS 512
typedef struct {
    int n, overflow;
    VRect r[EXACT_RECTS];
} ExactRects;

static struct {
    uint32_t e1;        /* draw mode: texpage, semi-transparency, depth, dither (bits 0-13) */
    uint32_t texWindow; /* E2 bits 0-19 */
    int clipX0, clipY0, clipX1, clipY1;
    int offX, offY;
    int setMask, checkMask;

    uint32_t cmd[16];
    const uint32_t* cmdAddr[16];
    int count;
    int needed;

    /* Polyline in progress: the last vertex, and whether a colour comes next. */
    int polyline;
    int polyGouraud;
    int polyWantColor;
    uint32_t polyColor;
    uint32_t polyCmd;
    PVertex polyPrev;

    /* CPU-to-VRAM transfer in progress (GP0 A0h). */
    int loading;
    int loadX, loadY, loadW, loadH;
    int loadPos, loadTotal;
    uint16_t loadBuf[VRAM_W * VRAM_H + 2];

    TileSet batchWritten;
    TileSet batchRead;
    ExactRects batchWrittenExact, batchReadExact; /* the same rectangles, exact (nativeHit) */

    /* The wide copy (wide.c): while `wide` is set, a command is being drawn
     * into the wide target, in its coordinates (wd); pkt is the packet's
     * pcWidePrim treatment, wideTex the texel source of the command. */
    int wide;
    int replayed;  /* the command being drawn into VRAM was drawn wide too */
    /* The texel ranges the wide pass's hi-res copy clamped to (the native
     * walk's texels over the wide clip), for the VRAM copy of the same
     * command: clipped at the frame's edge, VRAM's own range left the half
     * pixel there another texel than the wide picture's (xg-2fy.2). */
    struct {
        int flatSaved, flatValid; uint32_t flatMin, flatMax;
        int triSaved[4]; uint32_t triMin[4], triMax[4]; /* by triangle: the quad's first or second half, in the lower or upper frame buffer */
        uint32_t wideTex; /* the wide pass's wide-target texel source (wideTexture): the VRAM copy reads the same */
    } wideUv;
    /* A PC_WIDE_ONLY polygon's VRAM pass (execute): nothing at 1x (the game
     * never drew it there), only the hi-res sliver of a triangle the native
     * walk leaves empty. The game culls polygons beside the 4:3 frame; one
     * that ends within a pixel of it has a seam fringe (hiSeal) the wide
     * picture's centre shows at the frame's first column, and the VRAM copy
     * showed a hairline of the clear colour there instead. */
    int hiSliverOnly;
    int wideShiftY;
    int wideShift; /* target x = VRAM x + wideShift while `wide` */
    TileSet batchWideWritten; /* the wide target's writes */
    TileSet batchWideRead;    /* texels read from the wide target (wideTex) */
    WideDraw wd;
    const WidePacket* pkt;
    uint32_t wideTex;
    /* Complete face canvas for the current polygon, before raster clipping.
     * Upload history is bounded and can forget an otherwise unchanged face. */
    TextureRequest portraitSource;
    TextureRequest glyphSource; /* authenticated glyph/Cel canvas shared by both halves */
    /* A 2D-class polygon's sampled texels, both halves together (flatSpanTexels). */
    int flatUvValid; uint32_t flatUvMin, flatUvMax;
    int poseSkip; /* a part its actor's pose replaced in the copy (spritePose) */
    TextureSample sample;
    int sampled, filter;

    /* The texture cache (psx-spx "GPU Texture Caching"): 256 entries of four
     * VRAM halfwords from one row, the first at an x that is a multiple of
     * four. tag: y << 8 | x >> 2 of the entry's halfwords, -1 when empty.
     * texKey: the page and the 4-bit depth class the entries were read for
     * (cacheKey). */
    struct {
        int32_t tag;
        uint16_t data[4];
    } tex[256];
    uint32_t texKey;
    /* The CLUT cache: the palette of the last 4- or 8-bit textured primitive,
     * clutKey its attribute (bits 0-14) and depth (bit 15), -1 when empty. */
    uint16_t clut[256];
    int32_t clutKey;
} gp;

/* A primitive's native conflict with the batch is decided by the exact rectangles where the
 * tiles hit, so a batch is split only where they really meet: the fire of the opening
 * flashback split hundreds of times a frame (50-60 ms at 12x); exact, its worst frame has 41%
 * fewer hires batches, the pictures unchanged. XG_BATCH_EXACT=0: the tiles alone
 * decide, as before; XG_BATCH_LOG=1 prints the breaks and the hits the rectangles cleared. */
static int exactOn = -1;
static uint64_t exactCleared, exactBreaks;
static void exactSummary(void) {
    fprintf(stderr, "gp0: %llu batch breaks; %llu tile hits the exact rectangles cleared (%s)\n",
            (unsigned long long)exactBreaks, (unsigned long long)exactCleared, exactOn > 0 ? "used" : "counted only");
}
static int exactUsed(void) {
    if (exactOn < 0) {
        const char* e = getenv("XG_BATCH_EXACT");
        exactOn = !(e && *e == '0');
        const char* l = getenv("XG_BATCH_LOG");
        if (l && *l == '1') atexit(exactSummary);
    }
    return exactOn;
}
static void nativeClear(TileSet* t, ExactRects* e) {
    tilesClear(t);
    e->n = 0;
    e->overflow = 0;
}
static void nativeAdd(TileSet* t, ExactRects* e, VRect r) {
    tilesAdd(t, r);
    if (e->n < EXACT_RECTS) e->r[e->n++] = r;
    else e->overflow = 1;
}
static int nativeHit(const TileSet* t, const ExactRects* e, VRect r) {
    if (!tilesHit(t, r)) return 0;
    if (e->overflow) return 1;
    for (int i = 0; i < e->n; i++) {
        const VRect* q = &e->r[i];
        if (q->x0 <= r.x1 && r.x0 <= q->x1 && q->y0 <= r.y1 && r.y0 <= q->y1) return 1;
    }
    ++exactCleared;
    return !exactUsed();
}

/* XG_GP0_PROBE=x,y: log every command that may draw VRAM pixel (x, y). */
static int probeX = -1, probeY = -1;

static void probe(const char* what, int x0, int y0, int x1, int y1) {
    if (probeX < 0 || probeX < x0 || probeX > x1 || probeY < y0 || probeY > y1) {
        return;
    }
    fprintf(stderr, "probe (%d,%d): %s e1=%04x tw=%05x clip=(%d,%d)-(%d,%d) ofs=(%d,%d) mask=%d%d:", probeX, probeY,
            what, gp.e1, gp.texWindow, gp.clipX0, gp.clipY0, gp.clipX1, gp.clipY1, gp.offX, gp.offY, gp.setMask,
            gp.checkMask);
    for (int i = 0; i < gp.needed && i < 16; i++) {
        fprintf(stderr, " %08x", gp.cmd[i]);
    }
    fputc('\n', stderr);
}

static int sext11(uint32_t v) {
    return (int)((v & 0x7FF) ^ 0x400) - 0x400;
}

int gp0SetMask(void) {
    return gp.setMask;
}

int gp0CheckMask(void) {
    return gp.checkMask;
}

void gp0SetMaskBits(uint32_t e6) {
    gp.setMask = e6 & 1;
    gp.checkMask = (e6 >> 1) & 1;
}

static int wideExtends(int textured);
static int wideBegin(void);
static void wideEnd(void);

/*
 * The texture cache and the CLUT cache. psx-spx "GPU Texture Caching" gives
 * the texture cache's entries (four halfwords of one row, 256 of them) and
 * blocks, and that GP0(01h) flushes it (psx-spx "Clear Cache"). The rest was
 * measured with Beetle PSX as a black box (work/clean-room/measure/gpu,
 * mg1_cache.py: M-G1 geometry, M-G2 events, every case on all depths):
 *
 * - Blocks: an entry's number is its position inside a block of 16
 *   halfwords by 64 rows for 4-bit textures (as psx-spx says) and of 32 by
 *   32 for 8- and 15-bit ones (psx-spx gives 8-bit blocks as 32x64 texels,
 *   16 halfwords wide; the measurement says 32 halfwords by 32 rows).
 *   Entries with the same number in different blocks evict each other.
 * - The texture cache empties on GP0(01h), on every CPU-to-VRAM, VRAM-to-VRAM
 *   and VRAM-to-CPU transfer (wherever it lands), on GP1(00h), and when the
 *   texture page's X or Y changes or the depth changes between 4-bit and
 *   8/15-bit (8 and 15 share blocks and keep it), by GP0(E1h) or a textured
 *   polygon's texpage. Fills, drawing, the other draw-mode bits, E2h-E6h and
 *   GP1(01h) keep it, so texels changed by drawing stay stale.
 * - The CLUT cache holds the 16 or 256 palette halfwords of the last 4- or
 *   8-bit textured primitive (psx-spx "Clut Attribute"). Every such primitive
 *   loads it when its palette (bit 15 ignored) or depth differs, even one
 *   that draws nothing (clipped, no area, too large, size 0); 15-bit ones do
 *   not touch it. GP0(01h) and GP1(00h) empty it; transfers and draw-mode
 *   changes do not. (psx-spx says GP1(01h) resets it; the reference keeps it.)
 *
 * The wide pass and geometry-only display copies draw a command a second
 * time; only the native draw counts (GPU-A port constraint).
 */
static int cacheIgnored(void) {
    return gp.wide || (gp.pkt != NULL && gp.pkt->geometry == PC_GEOMETRY_ONLY);
}

/* The texture page and depth class the texture cache's entries belong to. */
static uint32_t cacheKey(uint32_t mode) {
    return (mode & 0x1Fu) | (((mode >> 7) & 3) == 0 ? 0x20u : 0);
}

/*
 * Walking a primitive's texels through the texture cache (cacheRaster) is
 * most of the CPU a frame costs, and it changes the picture only when a
 * cache entry no longer equals VRAM: when something was drawn into a page
 * after the cache read it (render-to-texture, a feedback primitive). Until
 * then each texel the walk would give is VRAM's, so the GPU path draws the
 * primitive and only the walk's effect on the cache (which entries it holds)
 * is owed. Such walks wait in cachePend, in order, and are done
 * (cacheCatchUp) the moment VRAM is about to change where an entry may come
 * from (cacheTiles: the pages the walks used since the cache last emptied):
 * a draw, a fill, a CPU copy of a GPU-drawn tile. From then on (cacheDirty)
 * every primitive walks, until the cache empties again. The entries, and so
 * every picture, are the same as with every walk done at once (xg-htn.41.2).
 */
#define CACHE_PEND_INTS (1 << 20)
static int32_t* cachePend; /* allocated at the first deferral */
static int cachePendUsed;
static int cacheDirty;
static TileSet cacheTiles;
/* With the cache dirty, the entries that may no longer equal VRAM: those
 * whose line lay in a rectangle that changed since they were loaded
 * (gp0VramChanging marks them, a reload clears them). A primitive that can
 * fetch none of them (no marked line inside what it reads) gets VRAM's
 * texels from any entry it hits, so its walk is deferred as a clean one is:
 * a page under palettes cycled inside the ordering table (Lahan after the
 * attack, Dazil) walked every pixel of every primitive after the first
 * palette upload. */
static uint8_t cacheMaybeStale[256];
static int cacheMaybeStaleCount;
/* cacheRaster's clutClean, remembered (the comment there). clutLoads counts
 * cacheClut's loads of the CLUT cache. */
static uint32_t clutLoads;
static struct { int valid, clean; uint32_t loads, serial; unsigned clut, n; uint64_t gen; } clutCleanMemo;

void gp0InvalidateTexture(void) {
    if (!gp.wide) interpolationCacheClear();
    for (int i = 0; i < 256; i++) {
        gp.tex[i].tag = -1;
    }
    cachePendUsed = 0;
    cacheDirty = 0;
    tilesClear(&cacheTiles);
    memset(cacheMaybeStale, 0, sizeof(cacheMaybeStale));
    cacheMaybeStaleCount = 0;
}

void gp0ClearCache(void) {
    gp0InvalidateTexture();
    gp.clutKey = -1;
}

/* An entry loaded from VRAM as it is now: current (cacheMaybeStale). */
static void cacheReloaded(int slot) {
    if (cacheMaybeStale[slot]) {
        cacheMaybeStale[slot] = 0;
        cacheMaybeStaleCount--;
    }
}

/* The entries whose line lies in r (VRAM halfwords), about to change. */
static void cacheMarkStale(VRect r) {
    for (int slot = 0; slot < 256; slot++) {
        int32_t tag = gp.tex[slot].tag;
        if (tag < 0 || cacheMaybeStale[slot]) continue;
        int ay = tag >> 8, ax = (tag & 255) << 2;
        if (ay < r.y0 || ay > r.y1 || ax + 3 < r.x0 || ax > r.x1) continue;
        cacheMaybeStale[slot] = 1;
        cacheMaybeStaleCount++;
    }
}


/* A draw mode (E1h bits, or a textured polygon's texpage) takes effect. */
static void setDrawMode(uint32_t mode) {
    gp.e1 = mode;
    if (!cacheIgnored() && cacheKey(mode) != gp.texKey) {
        gp0InvalidateTexture();
        gp.texKey = cacheKey(mode);
    }
}

/* A textured primitive's CLUT and texpage, before it is drawn. */
static void cacheClut(uint32_t tpage, uint32_t clut) {
    if ((tpage & ~0x60u) == 0x8180u) return; /* controller prompt glyphs */
    unsigned depth = (tpage >> 7) & 3;
    if (cacheIgnored() || depth >= 2) {
        return;
    }
    int32_t key = (int32_t)((clut & 0x7FFFu) | depth << 15);
    if (key == gp.clutKey) {
        return;
    }
    gp.clutKey = key;
    clutLoads++;
    int x = (int)(clut & 0x3F) * 16, y = (int)(clut >> 6) & (VRAM_H - 1), n = depth == 0 ? 16 : 256;
    VRect row = {x, y, x + n - 1 < VRAM_W ? x + n - 1 : VRAM_W - 1, y};
    vramSyncRect(row);
    if (x + n > VRAM_W) {
        VRect rest = {0, y, x + n - 1 - VRAM_W, y};
        vramSyncRect(rest);
    }
    for (int i = 0; i < n; i++) {
        gp.clut[i] = vramShadow[y][(x + i) & (VRAM_W - 1)]; /* wraps inside the row */
    }
}

/* Immutable display replay cannot borrow rendered feedback or replace the
 * PS1's cached palette with a newer upload at the same CLUT address. */
const char* gp0InterpolationTextureReason(const DecodedTexture* d) {
    TextureRequest r = d->request;
    unsigned depth = (r.tpage >> 7) & 3;
    if (d->paletteSize && !cacheIgnored()) {
        int key = (int)((r.clut & 0x7fffu) | depth << 15);
        if (gp.clutKey != key || memcmp(gp.clut, d->palette, (size_t)d->paletteSize * 2)) return "stale-clut";
    }
    /* texture.c owns texels after the native window remap. Replay samples
     * that logical UV patch directly, so it must not apply the window twice. */
    if (r.u < 0 || r.v < 0 || r.u + r.w > 256 || r.v + r.h > 256) return "texture-wrap";
    if (cacheDirty && !cacheIgnored()) {
        /* A different part of the page can be dirty while this complete
         * owned UV patch still agrees with the native cache. Decoding has
         * synchronized these source words; inspect them without changing
         * any entry, register or deferred walk. Actual stale raster reads
         * have already rejected the snapshot in cacheRaster. */
        unsigned shift=depth==0?2:depth==1?1:0;
        unsigned tx=(r.tpage&15)*64,ty=((r.tpage>>4)&1)*256;
        unsigned mx=(r.window&31)*8,my=((r.window>>5)&31)*8;
        unsigned ox=((r.window>>10)&31)*8&mx,oy=((r.window>>15)&31)*8&my;
        for (int v=r.v;v<r.v+r.h;v++) for (int u=r.u;u<r.u+r.w;u++) {
            unsigned ax=(tx+((((unsigned)u&~mx)|ox)>>shift))&1023;
            unsigned ay=(ty+(((unsigned)v&~my)|oy))&511;
            unsigned slot=depth==0?(ay&63)<<2|(ax>>2&3):(ay&31)<<3|(ax>>2&7);
            int32_t tag=(int32_t)(ay<<8|ax>>2);
            if (gp.tex[slot].tag==tag && gp.tex[slot].data[ax&3]!=vramShadow[ay][ax]) return "dirty-texture-cache";
        }
    }
    return NULL;
}
int gp0InterpolationCaptureSafe(void) { return !cacheDirty; }
int gp0InterpolationTextureSafe(const DecodedTexture* d) { return gp0InterpolationTextureReason(d) == NULL; }

/* XG_GP0_PROBE: mark each vertical blank in the probe's output. */
void gp0ProbeVBlank(uint32_t vblank) {
    if (probeX >= 0) {
        fprintf(stderr, "probe vblank %u\n", (unsigned)vblank);
    }
}

void gp0SetPacket(const WidePacket* p) {
    gp.pkt = p;
}

void gp0Reset(void) {
    /* The batch's tracked tiles are forgotten below: what it drew must be
     * finished before anything drawn after can read or overwrite it. */
    gvkBarrier();
    vramOwnedReset();
    gp0ClearCache(); /* GP1(00h) empties both caches (M-G2) */
    gp.e1 = 0;
    gp.texKey = cacheKey(0);
    gp.wide = 0;
    gp.pkt = NULL;
    gp.texWindow = 0;
    gp.clipX0 = gp.clipY0 = gp.clipX1 = gp.clipY1 = 0;
    gp.offX = gp.offY = gp.wideShiftY = 0;
    gp.setMask = gp.checkMask = 0;
    gp.count = 0;
    gp.polyline = 0;
    gp.loading = 0;
    nativeClear(&gp.batchWritten, &gp.batchWrittenExact);
    nativeClear(&gp.batchRead, &gp.batchReadExact);
    tilesClear(&gp.batchWideWritten);
    tilesClear(&gp.batchWideRead);
    const char* p = getenv("XG_GP0_PROBE");
    if (p != NULL) {
        sscanf(p, "%d,%d", &probeX, &probeY);
    }
}

/* ---- batches -------------------------------------------------------------- */

static void batchBreak(void) {
    ++exactBreaks;
    int keepWide = !gp.wide && gvkNativeBarrier();
    if (gp.wide) gvkBarrier();
    nativeClear(&gp.batchWritten, &gp.batchWrittenExact);
    nativeClear(&gp.batchRead, &gp.batchReadExact);
    if (!keepWide) {
        tilesClear(&gp.batchWideWritten);
        tilesClear(&gp.batchWideRead);
    }
}

/* Clip an inclusive rectangle to VRAM; 0 if nothing is left. */
static int clipToVram(VRect* r) {
    if (r->x0 < 0) {
        r->x0 = 0;
    }
    if (r->y0 < 0) {
        r->y0 = 0;
    }
    if (r->x1 > VRAM_W - 1) {
        r->x1 = VRAM_W - 1;
    }
    if (r->y1 > VRAM_H - 1) {
        r->y1 = VRAM_H - 1;
    }
    return r->x0 <= r->x1 && r->y0 <= r->y1;
}

/* The VRAM a primitive may read: its texels (from the UV bounds) and CLUT. */
typedef struct {
    VRect r[4];
    int n;
} ReadSet;

static void addRead(ReadSet* s, int x, int y, int w, int h) {
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces);
    for (int i = 0; i < n && s->n < 4; i++) {
        s->r[s->n++] = pieces[i].r;
    }
}

static void textureReads(ReadSet* s, uint32_t tpage, uint32_t clut, int u0, int u1, int v0, int v1) {
    if ((tpage & ~0x60u) == 0x8180u) return;
    int tx = (int)(tpage & 0xF) * 64;
    int ty = (int)((tpage >> 4) & 1) * 256;
    int depth = (int)((tpage >> 7) & 3);
    /* A window replaces the coordinates' masked bits (u & ~mask | offset &
     * mask): an axis whose mask is zero keeps its range, whatever the offset. */
    if ((gp.texWindow & 0x1F) != 0 || u0 < 0 || u1 > 255) {
        u0 = 0;
        u1 = 255;
    }
    if ((gp.texWindow >> 5 & 0x1F) != 0 || v0 < 0 || v1 > 255) {
        v0 = 0;
        v1 = 255;
    }
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    addRead(s, tx + (u0 >> shift), ty + v0, (u1 >> shift) - (u0 >> shift) + 1, v1 - v0 + 1);
    if (depth < 2) {
        addRead(s, (int)(clut & 0x3F) * 16, (int)(clut >> 6) & 0x1FF, depth == 0 ? 16 : 256, 1);
    }
}

/* The wide target's frame rows (WIDE_FRAME_W x VRAM_H) are tracked with
 * VRAM's tile sets at half the x resolution. */
static VRect wideTiles(VRect r) {
    r.x0 >>= 1;
    r.x1 >>= 1;
    return r;
}

/* The texels a wide copy reads from the wide target itself (gp.wideTex: a
 * frame buffer's wide picture, not a capture slot), in target coordinates. */
static int wideReadRect(const ReadSet* reads, VRect* out) {
    if (gp.wideTex == 0 || reads == NULL || reads->n == 0) {
        return 0;
    }
    int dx = (int16_t)(gp.wideTex & 0xFFFFu);
    int dy = (int32_t)(gp.wideTex << 1) >> 17;
    VRect r = reads->r[0];
    VRect t = {r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy};
    /* Expanded frame buffers also have a nonzero Y translation. Only
     * capture slots past the frame-buffer rows are immutable between copies. */
    t.x0 = t.x0 < 0 ? 0 : t.x0;
    t.x1 = t.x1 > WIDE_FRAME_W - 1 ? WIDE_FRAME_W - 1 : t.x1;
    t.y0 = t.y0 < 0 ? 0 : t.y0;
    t.y1 = t.y1 > VRAM_H - 1 ? VRAM_H - 1 : t.y1;
    if (t.x0 > t.x1 || t.y0 > t.y1) {
        return 0;
    }
    *out = wideTiles(t);
    return 1;
}

/* With the hi-res copy on, a write box one 1x pixel larger (its sloped
 * edges reach past the 1x pixels), inside a target `width` wide. */
static VRect hiPad(VRect w, int width) {
    if (gvkHiScale() != 0) {
        int pad = hiresPrecision() ? 6 : 1;
        w.x0 = w.x0 > pad ? w.x0 - pad : 0;
        w.y0 = w.y0 > pad ? w.y0 - pad : 0;
        w.x1 = w.x1 < width - pad ? w.x1 + pad : width - 1;
        w.y1 = w.y1 < VRAM_H - pad ? w.y1 + pad : VRAM_H - 1;
    }
    return w;
}

/* A mapped 15-bit wide texel comes from the wide images, not native
 * VRAM/the narrow enhanced target. Only the native right-edge wrap can
 * select those targets instead; textureReads already includes its wrapped
 * pieces. Indexed textures still read their native words and palette. */
static int wideReadsNative(const ReadSet* reads) {
    if(!reads || !reads->n)return 0;
    if(!gp.wideTex || ((gp.e1>>7)&3)<2)return 1;
    int tx=(gp.e1&15)*64;
    for(int i=0;i<reads->n;i++)if(reads->r[i].x0<tx)return 1;
    return 0;
}

/* The wide copy of a primitive joins the batch before its VRAM copy: its
 * texels must be what the VRAM copy reads, so they count as the batch's
 * reads. Texels it reads from the wide target are ordered against the wide
 * target's writes as VRAM's are against VRAM's (effects that redraw the frame
 * from itself, with pieces only the wide picture has). */
static int batchAddWide(VRect w, const ReadSet* reads) {
    if (gp.pkt != NULL && gp.pkt->dx != 0 && !(gp.pkt->flags & PC_WIDE_ONLY) && w.x1 - gp.pkt->dx >= gp.wd.edgeX0 &&
        w.x0 - gp.pkt->dx <= gp.wd.edgeX1 && w.y0 <= gp.wd.fbY1) {
        /* Moved out of where the 4:3 picture has it: the centre lacks it. */
        wideNoteShifted(&gp.wd, (VRect){w.x0 - gp.pkt->dx, w.y0, w.x1 - gp.pkt->dx, w.y1});
    }
    if (w.x0 < gp.clipX0) {
        w.x0 = gp.clipX0;
    }
    if (w.y0 < gp.clipY0) {
        w.y0 = gp.clipY0;
    }
    if (w.x1 > gp.clipX1) {
        w.x1 = gp.clipX1;
    }
    if (w.y1 > gp.clipY1) {
        w.y1 = gp.clipY1;
    }
    if (w.x0 < 0) {
        w.x0 = 0;
    }
    if (w.x1 > WIDE_FRAME_W - 1) {
        w.x1 = WIDE_FRAME_W - 1;
    }
    if (w.x0 > w.x1 || w.y0 > w.y1) {
        return 0;
    }
    if (gp.pkt != NULL && (gp.pkt->dx != 0 || (gp.pkt->flags & PC_WIDE_ONLY)) && w.x1 >= gp.wd.edgeX0 &&
        w.x0 <= gp.wd.edgeX1 && w.y0 <= gp.wd.fbY1) {
        /* The wide picture's centre is no longer the 4:3 one here. */
        wideNoteShifted(&gp.wd, w);
    }
    /* Without interlock nothing orders overlapping writes inside a batch:
     * the wide target's are tracked like VRAM's (wideTiles). */
    VRect wideRead;
    int hasWideRead = wideReadRect(reads, &wideRead);
    int nativeRead=wideReadsNative(reads);
    gvkWideTouch(w.x1);
    w = hiPad(w, WIDE_FRAME_W);
    if (w.y1 > VRAM_H - 1) {
        w.y1 = VRAM_H - 1;
    }
    w = wideTiles(w);
    int conflict = tilesHit(&gp.batchWideRead, w) || (!gvkOrderedWrites() && tilesHit(&gp.batchWideWritten, w)) ||
                   (hasWideRead && tilesHit(&gp.batchWideWritten, wideRead));
    for (int i = 0; nativeRead && i < reads->n && !conflict; i++) {
        conflict = nativeHit(&gp.batchWritten, &gp.batchWrittenExact, reads->r[i]);
    }
    if (conflict) {
        batchBreak();
    }
    tilesAdd(&gp.batchWideWritten, w);
    if (hasWideRead) {
        tilesAdd(&gp.batchWideRead, wideRead);
    }
    for (int i = 0; nativeRead && i < reads->n; i++) {
        nativeAdd(&gp.batchRead, &gp.batchReadExact, reads->r[i]);
    }
    gpuDrawIssued();
    return 1;
}

/* Account for a primitive about to join the batch; 0 if it draws nothing. */
static int batchAdd(VRect w, const ReadSet* reads) {
    if (gp.wide) {
        return batchAddWide(w, reads);
    }
    VRect clip = {gp.clipX0, gp.clipY0, gp.clipX1, gp.clipY1};
    if (w.x0 < clip.x0) {
        w.x0 = clip.x0;
    }
    if (w.y0 < clip.y0) {
        w.y0 = clip.y0;
    }
    if (w.x1 > clip.x1) {
        w.x1 = clip.x1;
    }
    if (w.y1 > clip.y1) {
        w.y1 = clip.y1;
    }
    if (!clipToVram(&w)) {
        return 0;
    }
    vramKeepDraw(w); /* a picture between's keep (vram.c): the tiles before the draw */
    VRect t = hiPad(w, VRAM_W);
    int conflict = nativeHit(&gp.batchRead, &gp.batchReadExact, t) || (!gvkOrderedWrites() && nativeHit(&gp.batchWritten, &gp.batchWrittenExact, t));
    for (int i = 0; reads != NULL && i < reads->n && !conflict; i++) {
        conflict = nativeHit(&gp.batchWritten, &gp.batchWrittenExact, reads->r[i]);
    }
    if (conflict) {
        batchBreak();
    }
    vramOwnedDraw(w);
    nativeAdd(&gp.batchWritten, &gp.batchWrittenExact, t);
    for (int i = 0; reads != NULL && i < reads->n; i++) {
        nativeAdd(&gp.batchRead, &gp.batchReadExact, reads->r[i]);
    }
    if (gvkHiScale() != 0) {
        hiresMarkDetail(t);
    }
    gp0VramChanging(w);
    vramMarkDrawn(w);
    if (wideSlotsLive) {
        wideNoteWrite(w);
    }
    if (!gp.replayed) {
        wideNoteDraw(w);
    }
    gpuDrawIssued();
    return 1;
}

static uint32_t clipMinWord(void) {
    return (uint32_t)gp.clipX0 | (uint32_t)gp.clipY0 << 16;
}

static uint32_t clipMaxWord(void) {
    return (uint32_t)gp.clipX1 | (uint32_t)gp.clipY1 << 16;
}

static void setVertex(GpuVertex* o, float x, float y, float u, float v, const PVertex* c, uint32_t mode, uint32_t tex) {
    o->x = x;
    o->y = y;
    o->u = u;
    o->v = v;
    o->r = (float)c->r;
    o->g = (float)c->g;
    o->b = (float)c->b;
    o->mode = mode;
    o->tex = tex;
    o->texWindow = gp.texWindow;
    o->clipMin = clipMinWord();
    o->clipMax = clipMaxWord();
    o->span = 0xFFFFFFFFu;
    o->wideTex = gp.wide ? gp.wideTex : 0;
    o->wideEdge = gp.wide ? ((uint32_t)gp.wd.edgeX0 & 0xFFFFu) | (uint32_t)gp.wd.edgeX1 << 16 : 0;
}

/* ---- the hi-res copy ------------------------------------------------------------ */

/* uvMin/uvMax: a texel coordinate pair, 16-bit signed each. */
static uint32_t uvWord(int u, int v) {
    u = u < -32768 ? -32768 : u > 32767 ? 32767 : u;
    v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
    return ((uint32_t)u & 0xFFFFu) | ((uint32_t)v & 0xFFFFu) << 16;
}

static void observeTexture(uint32_t tex,uint32_t lo,uint32_t hi) {
    int kind = gp.pkt && gp.pkt->classified ? gp.pkt->texClass : PC_TEX_3D;
    textureDumpObserve(tex,gp.texWindow,lo,hi,kind==PC_TEX_3D);
    if (gp.pkt && gp.pkt->classified && PC_TEX_IS_TEXT(gp.pkt->texClass)) {
        int u=(int16_t)lo, v=(int16_t)(lo>>16);
        TextureRequest r={(uint16_t)tex,(uint16_t)(tex>>16),gp.texWindow,
                          u,v,(int16_t)hi-u+1,(int16_t)(hi>>16)-v+1,0,0};
        textureGlyphDiscover(r);
    }
}

/* Decode before this primitive is marked rendered. Self-feedback keeps
 * the ordered VRAM/twin path, rather than freezing its evolving source. */
static void prepareTexture(uint32_t mode, uint32_t tex, uint32_t lo, uint32_t hi, VRect box, const ReadSet* reads) {
    gp.sampled = 0;
    if(mode&DRAW_TEXTURED) {
        unsigned depth=(tex>>7)&3;
        /* Enhanced 15-bit reads use their R32 target. The 1x wide pass
         * also uses its own target, except the authored native wrap. */
        if(depth<2 || (gp.wide && wideReadsNative(reads)))
            for(int i=0;i<reads->n;i++)vramFlushCpuWrites(reads->r[i]);
    }
    if ((tex & 0xff9fu) == 0x8180u) return;
    if (!(mode & DRAW_TEXTURED) || !gvkHiScale() || gp.poseSkip) return;
    int kind = gp.pkt && gp.pkt->classified ? gp.pkt->texClass : PC_TEX_3D;
    gp.filter = textureFilter(kind);
    int hudScale=textureHudScale(kind);
    int celScale=textureCelScale(kind);
    int glyphs=textureUseGlyphs(kind);
    int portrait=PC_TEX_IS_PORTRAIT(kind);
    int spriteCel=textureUseSpriteCel(kind) && textureSpriteCelEnabled();
    if ((!gp.filter && !hudScale && !celScale && !spriteCel && !((portrait && portraitPackClassEnabled(kind)) || (textureUsePack(kind) && texturePackActive())) && !glyphs) || (gp.wide && gp.wideTex)) return;
    if (!gp.wide) for (int i = 0; i < reads->n; ++i) {
        VRect r = reads->r[i];
        if (r.x0 <= box.x1 && r.x1 >= box.x0 && r.y0 <= box.y1 && r.y1 >= box.y0) return;
    }
    int u = (int16_t)lo, v = (int16_t)(lo >> 16);
    TextureRequest patch = {(uint16_t)tex, (uint16_t)(tex >> 16), gp.texWindow,
                            u, v, (int16_t)hi - u + 1, (int16_t)(hi >> 16) - v + 1, 0, 0};
    /* Projected portraits conservatively include the excluded UV endpoint
     * in their triangle bounds. The display copy clamps to the face canvas
     * in hiTriangle, so use that same canvas when selecting its artwork. */
    if (portrait && gp.portraitSource.w) patch = gp.portraitSource;
    if (patch.w < 1 || patch.h < 1 || patch.w > 256 || patch.h > 256) return;
    TextureRequest source = gp.glyphSource.w ? gp.glyphSource : portrait && gp.portraitSource.w ? gp.portraitSource :
        kind == PC_TEX_3D ? textureRequestSnapStable(patch) : textureRequestSnap(patch);
    const DecodedTexture* d = textureCacheGet(source);
    /* Above 2x the shader cuts edges at the copy's actual resolution.
     * Keep native texels (including 1x packs), rather than enlarging the
     * cached Scale2x blocks. Authored higher-resolution images stay intact. */
    int sampleFilter = gp.filter == TEXTURE_FILTER_EDGE && gvkHiScale() > 2 ? TEXTURE_FILTER_NEAREST : gp.filter;
    /* a portrait category set to Texture pack (portrait_set=texture-pack) takes the texture pack's way */
    if (portrait && !textureUsePack(kind)) gp.sampled = portraitPackClassSample(d,patch,kind,&gp.sample);
    else if(textureUsePack(kind)) gp.sampled = texturePackSample(d,patch,sampleFilter,&gp.sample);
    else if(spriteCel) {
        gp.sampled = textureSpriteCelSample(d,patch,sampleFilter,&gp.sample);
        /* A sprite the library does not cover draws exactly as Original: the
         * native path, not the enhanced copy (the compass dial lost its
         * one-texel crosshair there). */
        if(!gp.sampled && !gp.filter && !hudScale && !celScale && !glyphs && !(textureUsePack(kind) && texturePackActive())) return;
    }
    if(!gp.sampled && celScale) {
        gp.sampled=textureCelSample(d,gp.glyphSource.w ? gp.glyphSource : patch,celScale,&gp.sample);
        /* Unmatched graphics keep their native pixels. For 4/8-bit pages
         * the unsampled shader path already reads the same native words
         * and live CLUT (celgputest compares every raster path), so a
         * nearest copy of the decoded upload would only cost a span-buffer
         * copy and a frame sample slot. A 15-bit page would instead read
         * the enhanced copy, which can keep drawn detail across a matching
         * upload (hires.c HI_LOAD_KEEP): sample its native texels. */
        if(!gp.sampled && d && d->depth==15)gp.sampled=textureSampleGet(d,patch,TEXTURE_FILTER_NEAREST,&gp.sample);
    }
    if (!gp.sampled && glyphs) {
        d=textureCacheGet(gp.glyphSource.w ? gp.glyphSource : patch);
        gp.sampled=textureGlyphCompose(d,&gp.sample);
        if (gp.sampled && textureGlyphSmooth()) gp.filter=TEXTURE_FILTER_TEXT_SMOOTH;
    }
    if (!gp.sampled && hudScale) gp.sampled=textureUpscaleSample(d,patch,hudScale,&gp.sample);
    if (!gp.sampled && gp.filter) gp.sampled = textureSampleGet(d,patch,sampleFilter,&gp.sample);
}

/* A subpixel position for the hi-res copy, the same in VRAM and in the
 * wide target: the GTE's double snapped to 1/256 of a pixel (exact in a
 * float), the draw offset and the wide shift (whole pixels) added exactly.
 * Added to the double or the float first, the shift changed the last bit of
 * the sum, and the two copies' edges fell on different sides of a pixel
 * centre here and there (xg-2fy.2). */
static float preciseSnap(double v) {
    return (float)(floor(v * 256.0 + 0.5) * (1.0 / 256.0));
}

static void hiVertex(HiVertex* o, float x, float y, float u, float v, float r, float g, float b, uint32_t mode,
                     uint32_t tex, uint32_t uvMin, uint32_t uvMax) {
    o->x = x;
    o->w = 1;
    memset(o->sample,0,sizeof(o->sample));
    o->y = y;
    o->u = u;
    o->v = v;
    o->r = r;
    o->g = g;
    o->b = b;
    o->mode = mode;
    o->tex = tex;
    o->texWindow = gp.texWindow;
    o->clipMin = clipMinWord();
    o->clipMax = clipMaxWord();
    o->uvMin = uvMin;
    o->uvMax = uvMax;
    /* The VRAM copy of a command the wide pass drew reads 15-bit texels as
     * the wide pass did: from the wide target where the texture is a capture
     * with margins (an effect sampling beside the frame, where VRAM has no
     * pixels, finds the margin's), the edges in its own coordinates. */
    o->wideTex = gp.wide ? gp.wideTex : gp.replayed ? gp.wideUv.wideTex : 0;
    o->wideEdge = gp.wide ? ((uint32_t)gp.wd.edgeX0 & 0xFFFFu) | (uint32_t)gp.wd.edgeX1 << 16
                : gp.replayed ? ((uint32_t)(gp.wd.edgeX0 - gp.wd.shift) & 0xFFFFu) | (uint32_t)(gp.wd.edgeX1 - gp.wd.shift) << 16 : 0;
    o->rasterShift = !gp.wide && gp.replayed
        ? ((uint32_t)(gp.wd.shift + (gp.pkt != NULL ? gp.pkt->dx : 0)) & 0xFFFFu) | (uint32_t)gp.wd.shiftY << 16 : 0;
}

/* Own both the native sampled range and the enhanced projected pose. Native
 * cache/feedback resolution may finish before hiTriangle is reached. */
static void recordCapturedTriangle(const PVertex* a,const PVertex* b,const PVertex* c,uint32_t mode,uint32_t tex,
                                   uint32_t lo,uint32_t hi,unsigned piece,int enhanced) {
    if(!interpolationCaptureSample() || !interpolationRecording(gp.wide))return;
    const PVertex* points[3]={a,b,c};HiVertex owned[3];
    int precise=enhanced&&a->precise&&b->precise&&c->precise;
    int kind=gp.pkt&&gp.pkt->classified?gp.pkt->texClass:PC_TEX_3D;
    if(enhanced&&kind==PC_TEX_3D)mode|=DRAW_SURFACE|(precise?DRAW_PRECISE:0);
    for(int k=0;k<3;k++) {
        const PVertex* p=points[k];
        hiVertex(&owned[k],precise?p->preciseX:(float)p->x,precise?p->preciseY:(float)p->y,
            (float)p->u,(float)p->v,(float)p->r,(float)p->g,(float)p->b,mode,tex,lo,hi);
        if(precise&&hiresPerspective())owned[k].w=p->depth/4096.0f;
    }
    interpolationDraw(owned,3,PC_TEX_HUD,gp.wide,gp.wideShift,gp.wideShiftY,piece);
}
static void hiEmit(const HiVertex* v, int n) {
    const TextureSample* sample = gp.sampled && (v[0].mode & DRAW_TEXTURED) ? &gp.sample : NULL;
    if(v[0].sample[3]&INTERP_SAMPLE_NATIVE_STEP)gvkHiDrawCaptured(v,n,gp.wide);
    else gvkHiDrawTexture(v,n,gp.wide,sample,gp.filter);
}

/* Face quads span their whole canvas in UV units (64, 48 or 24). The live
 * sampler excludes the last screen column and row, so one end of each axis
 * is never sampled: the end at the right edge for the texture axis that
 * follows screen x, at the bottom for the one that follows y. Trim that end,
 * so snapshots resolve the same upload/content key. The axis' screen
 * gradient over vertices 0-2 finds the end for any vertex order and quarter
 * turn. For the game's faces (vertex 1 beside vertex 0, vertex 2 below it;
 * mirrored ones swap x0/x1) it is exactly the sign of (x1-x0)*(u1-u0) and
 * (y2-y0)*(v2-v0), the rule used before. A rotated, sheared or non-affine
 * face keeps that rule and is reported under XG_TEXTURE_LOG. Returns 1 to
 * trim the low end. */
static int portraitTrimLow(const PVertex* v, int axis) {
    int64_t dx1 = v[1].x - v[0].x, dy1 = v[1].y - v[0].y, dx2 = v[2].x - v[0].x, dy2 = v[2].y - v[0].y;
    int64_t dt1 = axis ? v[1].v - v[0].v : v[1].u - v[0].u, dt2 = axis ? v[2].v - v[0].v : v[2].u - v[0].u;
    int64_t area = dx1 * dy2 - dx2 * dy1;
    int64_t gx = dt1 * dy2 - dt2 * dy1, gy = dx1 * dt2 - dx2 * dt1; /* the gradient times area */
    if (area != 0 && (gx == 0) != (gy == 0)) return ((gx ? gx : gy) < 0) != (area < 0);
    static int report = -1, reported;
    if (report < 0) { const char* e = getenv("XG_TEXTURE_LOG"); report = e && *e && strcmp(e, "0"); }
    /* A collapsed face (open/close animations) draws nothing: no note. */
    if (report && area != 0 && reported < 32) {
        ++reported;
        gpuLog("texture: portrait face (%d,%d) (%d,%d) (%d,%d) is not axis-aligned; %c trim from vertex %d",
               v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y, axis ? 'v' : 'u', axis ? 2 : 1);
    }
    return axis ? dy2 * dt2 < 0 : dx1 * dt1 < 0;
}

static void polygonUvBounds(const PVertex* v, int count, int kind, uint32_t* lo, uint32_t* hi) {
    int u0 = v[0].u, u1 = u0, v0 = v[0].v, v1 = v0;
    for (int k = 1; k < count; k++) {
        if (v[k].u < u0) u0 = v[k].u;
        if (v[k].u > u1) u1 = v[k].u;
        if (v[k].v < v0) v0 = v[k].v;
        if (v[k].v > v1) v1 = v[k].v;
    }
    if (PC_TEX_IS_PORTRAIT(kind)) {
        int extent = PC_TEX_PORTRAIT_SIZE(kind);
        if (u1-u0 == extent) {
            if (portraitTrimLow(v, 0)) ++u0;
            else --u1;
        }
        if (v1-v0 == extent) {
            if (portraitTrimLow(v, 1)) ++v0;
            else --v1;
        }
    }
    *lo = uvWord(u0, v0); *hi = uvWord(u1, v1);
}

static void recordPolygon(const PVertex* v, int count, uint32_t mode, uint32_t tex,
                          const void* const* addresses, unsigned piece) {
    if (!interpolationRecording(gp.wide) || interpolationCaptureSample() || gp.poseSkip) return;
    if (gp.pkt && ((gp.pkt->geometry == PC_GEOMETRY_ONLY && !gvkHiScale()) ||
                   (gp.pkt->geometry == PC_GEOMETRY_HIDE && gvkHiScale() && hiresPrecision()))) return;
    int kind = gp.pkt && gp.pkt->classified ? gp.pkt->texClass : PC_TEX_3D;
    uint32_t lo, hi; polygonUvBounds(v, count, kind, &lo, &hi);
    if (gp.flatUvValid) { lo = gp.flatUvMin; hi = gp.flatUvMax; } /* the native walk's texels (flatSpanTexels) */
    if (gp.glyphSource.w) {
        TextureRequest r=gp.glyphSource;
        lo=uvWord(r.u,r.v); hi=uvWord(r.u+r.w-1,r.v+r.h-1);
    }
    GtePrecise projected[4];
    int geometry = interpolationGeometryPacket();
    int precision = geometry && addresses != NULL;
    for (int k = 0; precision && k < count; k++) {
        uint32_t word; memcpy(&word, addresses[k], sizeof(word));
        if (!gtePrecisionLookup(addresses[k], word, &projected[k]) ||
            v[k].x != (int16_t)word + gp.offX + gp.wideShift ||
            v[k].y != (int16_t)(word >> 16) + gp.offY + gp.wideShiftY) precision = 0;
    }
    if (gvkHiScale() && kind == PC_TEX_3D) {
        int precise = 1;
        for (int k = 0; k < count; k++) if (!v[k].precise) precise = 0;
        /* Match hiTriangle's sampling phase. Losing DRAW_PRECISE makes
         * stationary textures jump half a native pixel on replay frames. */
        mode |= DRAW_SURFACE | (precise ? DRAW_PRECISE : 0);
    }
    const int index[6] = {0, 1, 2, 1, 2, 3};
    HiVertex out[6];
    int n = count == 4 ? 6 : 3;
    for (int k = 0; k < n; k++) {
        const PVertex* p = &v[index[k]];
        /* Live hiTriangle uses address-validated fractional coordinates for
         * projected HUD too (the compass). Object identity controls motion
         * blending, not the precision of a discrete snapshot's endpoint. */
        hiVertex(&out[k], p->precise ? p->preciseX : (float)p->x, p->precise ? p->preciseY : (float)p->y,
                 (float)p->u, (float)p->v, (float)p->r, (float)p->g, (float)p->b, mode, tex,
                 lo, hi);
        if (p->precise && hiresPerspective()) out[k].w = p->depth / 4096.0f;
        if (precision) {
            out[k].x = preciseSnap(projected[index[k]].x) + (float)(gp.offX + gp.wideShift);
            out[k].y = preciseSnap(projected[index[k]].y) + (float)(gp.offY + gp.wideShiftY);
            if (hiresPerspective() && hiresPrecision()) out[k].w = (float)projected[index[k]].z / 4096.0f;
        }
    }
    interpolationDraw(out, n, kind, gp.wide, gp.wideShift, gp.wideShiftY, piece);
}

static void recordQuad(float x0, float y0, float x1, float y1, float u0, float v0, float du, float dv,
                       const PVertex* c, uint32_t mode, uint32_t tex, uint32_t lo, uint32_t hi, unsigned piece) {
    if ((mode & DRAW_FILL) || !interpolationRecording(gp.wide)) return;
    HiVertex o[6];
    hiVertex(&o[0], x0, y0, u0, v0, c->r, c->g, c->b, mode, tex, lo, hi);
    hiVertex(&o[1], x1, y0, u0 + du, v0, c->r, c->g, c->b, mode, tex, lo, hi);
    hiVertex(&o[2], x0, y1, u0, v0 + dv, c->r, c->g, c->b, mode, tex, lo, hi);
    o[3] = o[1]; o[4] = o[2];
    hiVertex(&o[5], x1, y1, u0 + du, v0 + dv, c->r, c->g, c->b, mode, tex, lo, hi);
    int kind = gp.pkt && gp.pkt->classified ? gp.pkt->texClass : PC_TEX_HUD;
    interpolationDraw(o, 6, kind, gp.wide, gp.wideShift, gp.wideShiftY, piece);
}

/* Keep each original triangle intact, then draw a narrow fringe along
 * the polygon perimeter. Changing a quad's diagonal endpoints independently
 * can introduce single-sample cracks through clip-space rounding. Fringe
 * strips preserve those shared endpoints and the original attribute planes.
 * Extending a strip along its edge seals authored corner junctions
 * without the long spikes an acute triangle's offset-line intersection makes.
 * Only opaque precision-backed polygons receive this display-only overlap.
 * Cap it by the face's projected thickness: a grazing face must approach
 * zero coverage continuously, rather than retain a one-pixel stripe until
 * its winding changes and that stripe jumps to the opposite side. */
static int hiAttribute(const HiVertex src[3], double x, double y, double area, HiVertex* out) {
    double dx1 = src[1].x - src[0].x, dy1 = src[1].y - src[0].y;
    double dx2 = src[2].x - src[0].x, dy2 = src[2].y - src[0].y;
    double dx = x - src[0].x, dy = y - src[0].y;
    double l1 = (dx * dy2 - dy * dx2) / area;
    double l2 = (dx1 * dy - dy1 * dx) / area;
    double weight[3] = {1 - l1 - l2, l1, l2};
    double reciprocal = 0, u = 0, v = 0, r = 0, g = 0, b = 0;
    for (int k = 0; k < 3; k++) {
        double q = weight[k] / src[k].w;
        reciprocal += q;
        u += q * src[k].u; v += q * src[k].v;
        r += weight[k] * src[k].r; g += weight[k] * src[k].g; b += weight[k] * src[k].b;
    }
    if (reciprocal <= 0 || !isfinite(reciprocal)) return 0;
    *out = src[0];
    out->x = (float)x; out->y = (float)y;
    out->w = (float)(1 / reciprocal);
    out->u = (float)(u / reciprocal); out->v = (float)(v / reciprocal);
    out->r = (float)r; out->g = (float)g; out->b = (float)b;
    return 1;
}

/* Four independent points of one fringe strip. Keep each point's operations
 * and accumulation order identical to hiAttribute, while sharing the source
 * loads and letting the compiler vectorize across points. In particular,
 * divide by depth/area as before; reciprocal multiplication changes rounding. */
static int hiAttributes(const HiVertex src[3], const double x[4], const double y[4], double area, HiVertex out[4]) {
    double dx1 = src[1].x - src[0].x, dy1 = src[1].y - src[0].y;
    double dx2 = src[2].x - src[0].x, dy2 = src[2].y - src[0].y;
    double weight[3][4];
    for (int i = 0; i < 4; i++) {
        double dx = x[i] - src[0].x, dy = y[i] - src[0].y;
        double l1 = (dx * dy2 - dy * dx2) / area;
        double l2 = (dx1 * dy - dy1 * dx) / area;
        weight[0][i] = 1 - l1 - l2; weight[1][i] = l1; weight[2][i] = l2;
    }
    double reciprocal[4] = {0}, u[4] = {0}, v[4] = {0}, r[4] = {0}, g[4] = {0}, b[4] = {0};
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 4; i++) {
            double q = weight[k][i] / src[k].w;
            reciprocal[i] += q;
            u[i] += q * src[k].u; v[i] += q * src[k].v;
            r[i] += weight[k][i] * src[k].r;
            g[i] += weight[k][i] * src[k].g;
            b[i] += weight[k][i] * src[k].b;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (reciprocal[i] <= 0 || !isfinite(reciprocal[i])) return 0;
        out[i] = src[0];
        out[i].x = (float)x[i]; out[i].y = (float)y[i];
        out[i].w = (float)(1 / reciprocal[i]);
        out[i].u = (float)(u[i] / reciprocal[i]); out[i].v = (float)(v[i] / reciprocal[i]);
        out[i].r = (float)r[i]; out[i].g = (float)g[i]; out[i].b = (float)b[i];
    }
    return 1;
}

/* Differential modes: 0 original, 1 reused edge lengths, 2 also batches
 * attributes (production). The CPU test compares complete vertex bytes.
 * The reference modes stay in this function, not in a test copy, so that
 * test/seamreusetest.c (which includes this file) compares the shipped
 * code with the original order under the same compiler and flags; a copy
 * could drift. Byte equality needs every operation rounded on its own:
 * port/CMakeLists.txt builds all port C with -ffp-contract=off, otherwise
 * an FMA target may contract hiAttributes' loops differently. */
static int hiSeal(const HiVertex src[3], HiVertex out[21], unsigned perimeter, int reuse) {
    memcpy(out, src, 3 * sizeof(*out));
    int n = 3;
    double area = (double)(src[1].x - src[0].x) * (src[2].y - src[0].y) -
                  (double)(src[2].x - src[0].x) * (src[1].y - src[0].y);
    if (fabs(area) < 1e-6) return n;
    /* The second pass uses exactly the same hypot inputs as the first.
     * Production reuses those results; the CPU regression also exercises
     * the original repeated calls and compares complete vertex bytes. */
    double longest = 0, lengths[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        double length = lengths[i] = hypot(src[j].x - src[i].x, src[j].y - src[i].y);
        if (length > longest) longest = length;
    }
    double overlap = fmin(1.0, fabs(area) / (4 * longest));
    for (int i = 0; i < 3; i++) {
        if (!(perimeter & (1u << i))) continue;
        int j = (i + 1) % 3;
        double dx = src[j].x - src[i].x, dy = src[j].y - src[i].y;
        double length = reuse ? lengths[i] : hypot(dx, dy);
        if (length < 1e-6) continue;
        double tx = dx / length * overlap, ty = dy / length * overlap;
        double sign = area > 0 ? 1 : -1;
        double nx = ty * sign, ny = -tx * sign;
        double x0 = src[i].x - tx, y0 = src[i].y - ty;
        double x1 = src[j].x + tx, y1 = src[j].y + ty;
        HiVertex strip[4];
        if (reuse == 2) {
            const double x[4] = {x0, x1, x0 + nx, x1 + nx};
            const double y[4] = {y0, y1, y0 + ny, y1 + ny};
            if (!hiAttributes(src, x, y, area, strip)) continue;
        } else {
            if (!hiAttribute(src, x0, y0, area, &strip[0]) ||
                !hiAttribute(src, x1, y1, area, &strip[1]) ||
                !hiAttribute(src, x0 + nx, y0 + ny, area, &strip[2]) ||
                !hiAttribute(src, x1 + nx, y1 + ny, area, &strip[3])) continue;
        }
        for (int k = 0; k < 4; k++) strip[k].mode |= DRAW_FRINGE;
        out[n++] = strip[0]; out[n++] = strip[1]; out[n++] = strip[2];
        out[n++] = strip[1]; out[n++] = strip[3]; out[n++] = strip[2];
    }
    return n;
}

static void hiTriangle(const PVertex* a, const PVertex* b, const PVertex* c, uint32_t mode, uint32_t tex,
                       uint32_t uvMin, uint32_t uvMax, unsigned perimeter) {
    if (gp.poseSkip) return; /* its actor's pose is drawn instead */
    const PVertex* p[3] = {a, b, c};
    HiVertex o[3];
    /* Opaque geometry separates actual cracks from texture cutout borders
     * in a magenta-clear sweep. Only the display copy changes. */
    static int probeSolid = -1;
    if (probeSolid < 0) {
        const char* value = getenv("XG_GEOMETRY_PROBE_SOLID");
        probeSolid = value != NULL && strcmp(value, "1") == 0;
    }
    /* Projected surfaces sample where their geometry covers the display;
     * sprites and UI retain their native texel block alignment. */
    int kind = gp.pkt && gp.pkt->classified ? gp.pkt->texClass : PC_TEX_3D;
    if (PC_TEX_IS_PORTRAIT(kind) && gp.portraitSource.w) {
        TextureRequest r = gp.portraitSource;
        /* Both halves of a face share its canvas clamp. Per-triangle span
         * bounds otherwise repeat a border texel along the diagonal. */
        uvMin = uvWord(r.u, r.v); uvMax = uvWord(r.u+r.w-1, r.v+r.h-1);
    }
    if (gp.glyphSource.w && gp.sampled) {
        TextureRequest r=gp.glyphSource;
        uvMin=uvWord(r.u,r.v); uvMax=uvWord(r.u+r.w-1,r.v+r.h-1);
    }
    /* The seam fringe is for projected surfaces only: they mark the copy's
     * coverage (DRAW_SURFACE), the only thing a fringe will not paint over.
     * Sprites, HUD and text are flat pixel art whose silhouette is the
     * image's edge; a fringe repeats their border texels up to a native
     * pixel outward over whatever 2D neighbour is there: the next packed
     * line of a text atlas, or the outline the memory card screen draws
     * around each save icon (its LINE_F3s come before the icon's quad). */
    int seal = a->precise && b->precise && c->precise && hiresSeamGuard() && !(mode & DRAW_SEMI) &&
               kind == PC_TEX_3D;
    if (probeSolid) mode = DRAW_FILL;
    if (kind == PC_TEX_3D) mode |= DRAW_SURFACE;
    if (a->precise && b->precise && c->precise && kind == PC_TEX_3D) mode |= DRAW_PRECISE;
    for (int i = 0; i < 3; i++) {
        hiVertex(&o[i], p[i]->precise ? p[i]->preciseX : (float)p[i]->x,
                 p[i]->precise ? p[i]->preciseY : (float)p[i]->y, (float)p[i]->u, (float)p[i]->v, (float)p[i]->r,
                 (float)p[i]->g, (float)p[i]->b, mode, tex, uvMin, uvMax);
        if (probeSolid) { o[i].r = o[i].g = o[i].b = 248; }
        if (p[i]->precise && hiresPerspective()) o[i].w = p[i]->depth / 4096.0f;
    }

    HiVertex sealed[21];
    int n = seal ? hiSeal(o, sealed, perimeter, 2) : 0;
    hiEmit(n ? sealed : o, n ? n : 3);
}

/* An axis-aligned rectangle (x0, y0)-(x1, y1), exclusive right and bottom,
 * texel (u0, v0) at its corner, one texel per 1x pixel. */
static void hiQuad(float x0, float y0, float x1, float y1, float u0, float v0, float du, float dv, const PVertex* c,
                   uint32_t mode, uint32_t tex, uint32_t uvMin, uint32_t uvMax) {
    float u1 = u0 + du, v1 = v0 + dv;
    HiVertex o[6];
    float r = (float)c->r, g = (float)c->g, b = (float)c->b;
    /* Crack sweeps replace only display-copy clears. The game and every
     * native frame dump still see the original fill colour. */
    if (mode & DRAW_FILL) {
        static int probe = -1;
        if (probe < 0) {
            const char* value = getenv("XG_GEOMETRY_PROBE_CLEAR");
            probe = value != NULL && strcmp(value, "1") == 0;
        }
        if (probe) { r = b = 248; g = 0; }
    }
    hiVertex(&o[0], x0, y0, u0, v0, r, g, b, mode, tex, uvMin, uvMax);
    hiVertex(&o[1], x1, y0, u1, v0, r, g, b, mode, tex, uvMin, uvMax);
    hiVertex(&o[2], x0, y1, u0, v1, r, g, b, mode, tex, uvMin, uvMax);
    o[3] = o[1];
    o[4] = o[2];
    hiVertex(&o[5], x1, y1, u1, v1, r, g, b, mode, tex, uvMin, uvMax);
    hiEmit(o, 6);
}

/* An attribute of a triangle as a plane over x, y (the copy's pieces of a
 * triangle drawn in steps). */
typedef struct {
    float a, b, c;
} Plane;

static int trianglePlanes(const PVertex* const* t, Plane out[5]) {
    float x1 = (float)(t[1]->x - t[0]->x), y1 = (float)(t[1]->y - t[0]->y);
    float x2 = (float)(t[2]->x - t[0]->x), y2 = (float)(t[2]->y - t[0]->y);
    float det = x1 * y2 - x2 * y1;
    if (det == 0) {
        return 0;
    }
    for (int k = 0; k < 5; k++) {
        float f[3];
        for (int i = 0; i < 3; i++) {
            const PVertex* p = t[i];
            f[i] = (float)(k == 0 ? p->u : k == 1 ? p->v : k == 2 ? p->r : k == 3 ? p->g : p->b);
        }
        float d1 = f[1] - f[0], d2 = f[2] - f[0];
        out[k].a = (d1 * y2 - d2 * y1) / det;
        out[k].b = (d2 * x1 - d1 * x2) / det;
        out[k].c = f[0] - out[k].a * (float)t[0]->x - out[k].b * (float)t[0]->y;
    }
    return 1;
}

static float planeAt(const Plane* p, float x, float y) {
    return p->a * x + p->b * y + p->c;
}

/* ---- polygons ---------------------------------------------------------------- */

static void decodeColor(PVertex* v, uint32_t word) {
    v->r = (int)(word & 0xFF);
    v->g = (int)((word >> 8) & 0xFF);
    v->b = (int)((word >> 16) & 0xFF);
}

static void decodeXY(PVertex* v, uint32_t word) {
    /* Polygon vertices are signed 11-bit fields (psx-spx "Vertex"); the
     * drawing offset is added to the full value (polygonWrap then moves a
     * polygon past -1024 as a whole). Rectangle origins (rectCommand) and
     * line pixels (line) wrap on their own. */
    v->x = sext11(word) + gp.offX + gp.wideShift;
    v->y = sext11(word >> 16) + gp.offY + gp.wideShiftY;
}

/* Vertex plus drawing offset leaves -1024..1023 for polygons far off the
 * frame. Measured with Beetle PSX as a black box
 * (work/clean-room/measure/gpu, mgI_wrap*.py, M-G5 offsets): a polygon whose
 * leftmost vertex's sum is below -1024 is drawn 2048 further right, whole,
 * its shape kept, whatever the vertex order; the same downward for the
 * topmost vertex. A polygon pushed past +1023 is not moved (it stays off
 * the frame, as in scene 54's dust, where wrapping single vertices would
 * stretch an off-screen quad across the back buffer). The size limit was
 * judged before the offset (edgeTooLong). */
static void polygonWrap(PVertex* v, int n) {
    int minX = v[0].x - gp.wideShift, minY = v[0].y - gp.wideShiftY;
    for (int k = 1; k < n; k++) {
        minX = v[k].x - gp.wideShift < minX ? v[k].x - gp.wideShift : minX;
        minY = v[k].y - gp.wideShiftY < minY ? v[k].y - gp.wideShiftY : minY;
    }
    for (int k = 0; k < n; k++) {
        if (minX < -1024) {
            v[k].x += 2048;
            v[k].preciseX += 2048.0f;
        }
        if (minY < -1024) {
            v[k].y += 2048;
            v[k].preciseY += 2048.0f;
        }
    }
}

static int32_t wrap32(int64_t v) {
    return (int32_t)(uint32_t)(uint64_t)v;
}

#define SPAN_HEADER 8
#define SPAN_ROW 8
/* Cached draws append one decoded texel per covered pixel to these rows. */
static int32_t spanBuf[SPAN_HEADER + SPAN_ROW * VRAM_H + VRAM_W * VRAM_H + 4 * VRAM_H];

/* n / d rounded up, for d > 0. */
static int64_t ceilDiv(int64_t n, int64_t d) {
    int64_t q = n / d;
    return q + (n % d > 0);
}

/* Rows of triangle (a, b, c) inside the draw area: spanBuf gets the header
 * {first y, rows, du, dv, dr, dg, db, start} and per row {xmin, xmax, u, v,
 * r, g, b, 0}, start being the reference vertex's row (the drawing order,
 * selfRowAt), attributes and their per-pixel gradients in 16.16 (the fragment
 * shader adds gradient * (x - xmin) to the row's value and takes the integer
 * part; `interpolate` 0 means flat attributes, gradients 0). Slot 7 of a row
 * is cacheRaster's. Returns the number of int32 written, 0 if nothing is
 * drawn; the bounding box of the drawn pixels goes to *box.
 *
 * Coverage: psx-spx "GPU Render Polygon Commands / Notes" draws polygons up
 * to, but excluding, their lower-right coordinates. The exact rule was
 * measured with Beetle PSX as a black box (work/clean-room/measure/gpu,
 * M-G3: 2236 triangles, short and long edges, slivers): pixel (x, y) is drawn
 * when the point (x, y) lies inside the triangle, where points on a left edge
 * or on a horizontal top edge count as inside and points on a right edge or
 * a horizontal bottom edge do not. A triangle without area draws nothing.
 *
 * Attributes: affine, without perspective (psx-spx "Perspective (in-)correct
 * Rendering"), each sampled per pixel. The fixed point was fitted to the
 * same black box (M-G4: 5067 textured and 849 Gouraud triangles, every
 * vertex order, every pixel exact): each attribute's steps per pixel in x
 * and in y are the plane's slopes in 12 fractional bits, truncated toward
 * zero; a pixel takes the reference vertex's value plus one half plus the
 * steps to it, truncated. The reference vertex is the leftmost one; of two
 * leftmost vertices, the one that follows the other in packet order, the
 * first following the third. Here the 12-bit sums are kept in 16.16 (times
 * 16, which truncates the same) for the shader. */
/* Differential modes: 0 per-row divisions, 1 stepped edges, 2 also rejects
 * fully clipped X bounds (production). The CPU test compares whole buffers.
 * Like hiSeal's, the reference modes live here so test/spansteptest.c
 * checks the production function against them in one translation unit. */
static int buildSpansMode(const PVertex* a, const PVertex* b, const PVertex* c, int interpolate, VRect* box, int32_t* spanBuf, VRect clip, int incremental) {
    const PVertex* p[3] = {a, b, c};
    int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(c->x - a->x) * (b->y - a->y);
    if (area == 0) {
        return 0;
    }
    int top = a->y, bottom = a->y;
    for (int i = 1; i < 3; i++) {
        top = p[i]->y < top ? p[i]->y : top;
        bottom = p[i]->y > bottom ? p[i]->y : bottom;
    }
    int y0 = top > clip.y0 ? top : clip.y0;
    int y1 = bottom - 1 < clip.y1 ? bottom - 1 : clip.y1;
    if (y0 > y1 || clip.x0 > clip.x1) {
        return 0;
    }

    /* The edges, ordered so that the inside is where each edge function
     * ex * (y - sy) - ey * (x - sx) is positive. */
    const PVertex* q[3] = {a, area > 0 ? b : c, area > 0 ? c : b};
    int64_t ex[3], ey[3], sx[3], sy[3];
    for (int i = 0; i < 3; i++) {
        ex[i] = q[(i + 1) % 3]->x - q[i]->x;
        ey[i] = q[(i + 1) % 3]->y - q[i]->y;
        sx[i] = q[i]->x;
        sy[i] = q[i]->y;
    }

    /* The attribute planes in 1/4096: value(x, y) = at + gx * x + gy * y,
     * the half already added. */
    int ref = 0;
    for (int i = 0; i < 3; i++) {
        if (p[i]->x < p[(i + 1) % 3]->x && p[i]->x <= p[(i + 2) % 3]->x) {
            ref = i;
        }
    }
    const PVertex* r = interpolate ? p[ref] : a;
    int64_t at[5], gx[5], gy[5];
    for (int k = 0; k < 5; k++) {
        int f[3];
        for (int i = 0; i < 3; i++) {
            f[i] = k == 0 ? p[i]->u : k == 1 ? p[i]->v : k == 2 ? p[i]->r : k == 3 ? p[i]->g : p[i]->b;
        }
        int fr = k == 0 ? r->u : k == 1 ? r->v : k == 2 ? r->r : k == 3 ? r->g : r->b;
        gx[k] = gy[k] = 0;
        if (interpolate) {
            int64_t dx = (int64_t)(f[1] - f[0]) * (c->y - a->y) - (int64_t)(f[2] - f[0]) * (b->y - a->y);
            int64_t dy = (int64_t)(f[2] - f[0]) * (b->x - a->x) - (int64_t)(f[1] - f[0]) * (c->x - a->x);
            gx[k] = dx * 4096 / area; /* C division truncates toward zero */
            gy[k] = dy * 4096 / area;
        }
        at[k] = ((int64_t)fr << 12) + 2048 - gx[k] * r->x - gy[k] * r->y;
        /* Per pixel in 16.16; the shader's 32-bit sums wrap back to the
         * value, which inside the triangle is in range. */
        spanBuf[2 + k] = (int32_t)(uint32_t)(uint64_t)(gx[k] * 16);
    }
    spanBuf[7] = p[ref]->y;

    /* No native sample can lie beyond every vertex's X coordinate. Keep
     * the original scratch-header and empty-box writes, then skip the
     * edge setup and row walk. The caller still handles precise slivers. */
    if (incremental == 2 &&
        ((a->x < clip.x0 && b->x < clip.x0 && c->x < clip.x0) ||
         (a->x > clip.x1 && b->x > clip.x1 && c->x > clip.x1))) {
        box->x0 = VRAM_W * 4;
        box->x1 = -VRAM_W * 4;
        return 0;
    }

    /* The ceiling of each edge's rational x advances exactly by quotient
     * and remainder. Avoid three signed divisions per scanline without
     * changing the measured inclusive-left/exclusive-right coverage. The
     * original per-row divisions remain available to the CPU regression. */
    int64_t edgeX[3], error[3], stepX[3], stepError[3], divisor[3];
    if (incremental) for (int i = 0; i < 3; i++) {
        if (!ey[i]) continue;
        divisor[i] = ey[i] < 0 ? -ey[i] : ey[i];
        int64_t num = ex[i] * (y0 - sy[i]) + ey[i] * sx[i];
        int64_t step = ex[i];
        if (ey[i] < 0) { num = -num; step = -step; }
        edgeX[i] = ceilDiv(num, divisor[i]);
        error[i] = edgeX[i] * divisor[i] - num;
        stepX[i] = ceilDiv(step, divisor[i]);
        stepError[i] = stepX[i] * divisor[i] - step;
    }

    int first = -1, last = -1;
    box->x0 = VRAM_W * 4;
    box->x1 = -VRAM_W * 4;
    for (int y = y0; y <= y1; y++) {
        int64_t lo = clip.x0, hi = clip.x1;
        for (int i = 0; i < 3; i++) {
            if (ey[i] == 0) {
                continue; /* horizontal: the row range decides */
            }
            if (incremental) {
                int64_t x = edgeX[i] - (ey[i] > 0);
                if (ey[i] < 0) lo = x > lo ? x : lo;
                else hi = x < hi ? x : hi;
                edgeX[i] += stepX[i];
                error[i] += stepError[i];
                if (error[i] >= divisor[i]) { --edgeX[i]; error[i] -= divisor[i]; }
                continue;
            }
            int64_t num = ex[i] * (y - sy[i]) + ey[i] * sx[i]; /* the edge function is num - ey * x */
            if (ey[i] < 0) {
                int64_t x = ceilDiv(-num, -ey[i]); /* a left edge: >= 0 */
                lo = x > lo ? x : lo;
            } else {
                int64_t x = ceilDiv(num, ey[i]) - 1; /* a right edge: > 0 */
                hi = x < hi ? x : hi;
            }
        }
        if (lo > hi && first < 0) {
            continue;
        }
        if (first < 0) {
            first = y;
        }
        int32_t* row = spanBuf + SPAN_HEADER + (y - first) * SPAN_ROW;
        if (lo > hi) {
            row[0] = 1;
            row[1] = 0;
            continue;
        }
        row[0] = (int32_t)lo;
        row[1] = (int32_t)hi;
        for (int k = 0; k < 5; k++) {
            row[2 + k] = (int32_t)((at[k] + gx[k] * lo + gy[k] * y) * 16);
        }
        row[7] = 0;
        last = y;
        box->x0 = (int)lo < box->x0 ? (int)lo : box->x0;
        box->x1 = (int)hi > box->x1 ? (int)hi : box->x1;
    }
    if (first < 0) {
        return 0;
    }
    box->y0 = first;
    box->y1 = last;
    spanBuf[0] = first;
    spanBuf[1] = last - first + 1;
    return SPAN_HEADER + SPAN_ROW * spanBuf[1];
}

static int buildSpans(const PVertex* a, const PVertex* b, const PVertex* c, int interpolate, VRect* box, int32_t* out, VRect clip) {
    return buildSpansMode(a, b, c, interpolate, box, out, clip, 2);
}

/* The texels a triangle's 1x rows sampled (buildSpans' rows: each row's
 * first and last pixel are its extremes), as uvMin and uvMax. */
static void spanTexels(const int32_t* sp, uint32_t* lo, uint32_t* hi) {
    int u0 = 1 << 30, u1 = -(1 << 30), v0 = 1 << 30, v1 = -(1 << 30);
    for (int r = 0; r < sp[1]; r++) {
        const int32_t* row = sp + SPAN_HEADER + r * SPAN_ROW;
        if (row[0] > row[1]) {
            continue;
        }
        int k = row[1] - row[0];
        int ua = row[2] >> 16, ub = wrap32(row[2] + (int64_t)k * sp[2]) >> 16;
        int va = row[3] >> 16, vb = wrap32(row[3] + (int64_t)k * sp[3]) >> 16;
        u0 = ua < u0 ? ua : u0;
        u0 = ub < u0 ? ub : u0;
        u1 = ua > u1 ? ua : u1;
        u1 = ub > u1 ? ub : u1;
        v0 = va < v0 ? va : v0;
        v0 = vb < v0 ? vb : v0;
        v1 = va > v1 ? va : v1;
        v1 = vb > v1 ? vb : v1;
    }
    *lo = uvWord(u0, v0);
    *hi = uvWord(u1, v1);
}

/* A 2D-class polygon (sprite cells, HUD, text: flat pixel art, possibly at
 * fractional positions) clamps its hi-res copy to the texels its native
 * rasterisation sampled, as a whole: the union over both halves of a quad,
 * so the diagonal repeats nothing. The vertex UVs reach a texel further: an
 * x-flipped sprite cell starts one texel early, on the atlas gutter
 * (sprite.c, the PS1's flipped sampling), and the fractional edge of a
 * precise cell sampled that clear texel as a sliver at the join of two
 * cells, a dark line down the actor (xg-nsp). Both halves are walked again
 * here; the polygon's own walk comes after, one half at a time. */
static int flatSpanTexels(const PVertex* v, int first, int second, int interpolate, uint32_t* lo, uint32_t* hi) {
    static int32_t scratch[SPAN_HEADER + SPAN_ROW * VRAM_H];
    VRect clip = {gp.clipX0, gp.clipY0, gp.clipX1, gp.clipY1}, box;
    int u0 = 1 << 30, u1 = -(1 << 30), v0 = 1 << 30, v1 = -(1 << 30), any = 0;
    for (int half = 0; half < 2; half++) {
        if (!(half ? second : first)) continue;
        if (!buildSpans(&v[half], &v[half + 1], &v[half + 2], interpolate, &box, scratch, clip)) continue;
        uint32_t l, h;
        spanTexels(scratch, &l, &h);
        int lu = (int16_t)l, lv = (int16_t)(l >> 16), hu = (int16_t)h, hv = (int16_t)(h >> 16);
        if (lu < u0) u0 = lu;
        if (hu > u1) u1 = hu;
        if (lv < v0) v0 = lv;
        if (hv > v1) v1 = hv;
        any = 1;
    }
    if (!any) return 0;
    *lo = uvWord(u0, v0);
    *hi = uvWord(u1, v1);
    return 1;
}

/* The transition's clipped UV range follows its intermediate pose. Use the
 * same integer sampler as the authored endpoint, with private scratch and an
 * explicit clip; no native cache, GP0 registers or pending span data changes. */
void gp0InterpolationTransitionBounds(HiVertex* v) {
    PVertex p[3] = {{0}};
    for (int i = 0; i < 3; i++) {
        p[i].x = (int)floorf(v[i].x); p[i].y = (int)floorf(v[i].y);
        p[i].u = (int)v[i].u; p[i].v = (int)v[i].v;
    }
    VRect clip = {(int)(v[0].clipMin&65535),(int)(v[0].clipMin>>16),
                  (int)(v[0].clipMax&65535),(int)(v[0].clipMax>>16)}, box;
    int32_t scratch[SPAN_HEADER + SPAN_ROW * VRAM_H];
    if (clip.y0 < 0 || clip.y1-clip.y0 >= VRAM_H ||
        !buildSpans(&p[0],&p[1],&p[2],1,&box,scratch,clip)) return;
    uint32_t lo, hi; spanTexels(scratch,&lo,&hi);
    for (int i = 0; i < 3; i++) { v[i].uvMin = lo; v[i].uvMax = hi; }
}

int gp0InterpolationSpans(const HiVertex* v, int32_t* rows, VRect* box) {
    PVertex p[3] = {{0}};
    for (int i = 0; i < 3; i++) {
        p[i].x = (int)floorf(v[i].x); p[i].y = (int)floorf(v[i].y);
        p[i].u = (int)v[i].u; p[i].v = (int)v[i].v;
        p[i].r = (int)v[i].r; p[i].g = (int)v[i].g; p[i].b = (int)v[i].b;
    }
    VRect clip = {(int)(v[0].clipMin&65535),(int)(v[0].clipMin>>16),
                  (int)(v[0].clipMax&65535),(int)(v[0].clipMax>>16)};
    if (clip.y0 < 0 || clip.y1-clip.y0 >= VRAM_H) return 0;
    return buildSpans(&p[0],&p[1],&p[2],1,box,rows,clip);
}

/* ---- primitives that read their own pixels ---------------------------------- */

/*
 * An effect that redraws the frame from itself (the battle circle warp) can
 * give a primitive texels under its own pixels. The PS1 draws a primitive
 * row by row, each row left to right (selfRowAt has the order of the rows),
 * so a pixel reads what an earlier pixel of the same primitive wrote there,
 * or the old texel when a later one writes it. One draw orders none of that
 * (interlock orders only writes to one pixel), so such a primitive is drawn
 * in steps with a barrier between them: a pixel goes after the pixel whose
 * write it reads, and before the pixel that overwrites its texel.
 */
typedef struct {
    VRect box;            /* the pixels drawn, in this pass's target */
    const int32_t* spans; /* a triangle's rows (buildSpans), NULL for a rectangle */
    int ints;
    int x, y, u, v; /* a rectangle: its corner and the corner's texel */
    uint32_t tpage, mode, tex;
    const PVertex* color;
    const PVertex* const* tri; /* a triangle's vertices (the hi-res copy's pieces) */
    uint32_t uvMin, uvMax;     /* the texels it samples (hiVertex) */
    int du, dv;              /* rectangle texture directions */
} SelfDraw;

#define STEP_MAX 4096
static uint32_t stepMap[WIDE_FRAME_W * VRAM_H]; /* each pixel's step, box-relative */
static GpuVertex stepBuf[6 * 256];

static int rectsOverlap(VRect a, VRect b) {
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

/* Whether a textured primitive may read pixels it draws (cheap and
 * conservative; selfSteps decides). */
static int selfOverlap(VRect box, const ReadSet* reads, uint32_t tpage, uint32_t clut) {
    int depth = (int)((tpage >> 7) & 3);
    if (gp.wide) {
        /* The wide pass reads its own target only for 15-bit texels from a
         * frame buffer or capture (draw.frag), past VRAM's right edge too. */
        if (depth < 2 || gp.wideTex == 0) {
            return 0;
        }
        int dx = (int16_t)(gp.wideTex & 0xFFFFu);
        int dy = (int32_t)(gp.wideTex << 1) >> 17;
        for (int i = 0; i < reads->n; i++) {
            for (int k = 0; k < 2; k++) {
                VRect r = reads->r[i];
                VRect t = {r.x0 + dx + k * VRAM_W, r.y0 + dy, r.x1 + dx + k * VRAM_W, r.y1 + dy};
                if (t.x0 < 0 || t.x1 > WIDE_W - 1 || t.y0 < 0 || t.y1 > WIDE_H - 1 || rectsOverlap(t, box)) {
                    return 1;
                }
            }
        }
        return 0;
    }
    if (depth < 2) {
        VRect c = {(int)(clut & 0x3F) * 16, (int)(clut >> 6) & 0x1FF, 0, 0};
        c.x1 = c.x0 + (depth == 0 ? 15 : 255);
        c.y1 = c.y0;
        if (rectsOverlap(c, box)) {
            gpuWarnOnce("selfclut", "gpu: a primitive draws over its own CLUT (not ordered)");
        }
    }
    for (int i = 0; i < reads->n; i++) {
        if (rectsOverlap(reads->r[i], box)) {
            return 1;
        }
    }
    return 0;
}

/* The order of a primitive's rows, measured with Beetle PSX as a black box
 * (work/clean-room/measure/gpu, mg1_feedback.py: 768 triangles, quads and
 * rectangles, flipped or not, reading their own pixels through the texture
 * cache, every pixel): a triangle starts at the row of its reference vertex
 * (buildSpans: the leftmost one) and draws down to its last row, then the
 * rows above that one from the bottom up; a rectangle draws top to bottom.
 * Each row goes left to right. The i-th row drawn: */
static int selfRowAt(const SelfDraw* s, int i) {
    int first = s->box.y0, last = s->box.y1;
    if (s->spans == NULL) {
        return first + i;
    }
    int start = s->spans[7] < first ? first : s->spans[7];
    int down = start <= last ? last - start + 1 : 0;
    return i < down ? start + i : (start <= last ? start : last + 1) - 1 - (i - down);
}

/* Where row y comes in that order. */
static int selfRowRank(const SelfDraw* s, int y) {
    int first = s->box.y0, last = s->box.y1;
    if (s->spans == NULL) {
        return y - first;
    }
    int start = s->spans[7] < first ? first : s->spans[7];
    int down = start <= last ? last - start + 1 : 0;
    return y >= start ? y - start : down + (start - 1 - y);
}

static void selfRow(const SelfDraw* s, int y, int* xa, int* xb) {
    if (s->spans != NULL) {
        const int32_t* r = s->spans + SPAN_HEADER + (y - s->spans[0]) * SPAN_ROW;
        *xa = r[0];
        *xb = r[1];
    } else {
        *xa = s->box.x0;
        *xb = s->box.x1;
    }
}

/* The texel pixel (x, y) reads, if it is in the target this pass draws (as
 * draw.frag addresses it; a CLUT entry is not followed). */
static int selfTexel(const SelfDraw* s, int x, int y, int* ax, int* ay) {
    int u, v;
    if (s->spans != NULL) {
        const int32_t* r = s->spans + SPAN_HEADER + (y - s->spans[0]) * SPAN_ROW;
        int k = x - r[0];
        u = wrap32(r[2] + (int64_t)k * s->spans[2]) >> 16;
        v = wrap32(r[3] + (int64_t)k * s->spans[3]) >> 16;
    } else {
        u = s->u + s->du * (x - s->x);
        v = s->v + s->dv * (y - s->y);
    }
    u &= 0xFF;
    v &= 0xFF;
    int mx = (int)(gp.texWindow & 0x1F) * 8, my = (int)((gp.texWindow >> 5) & 0x1F) * 8;
    int ox = (int)((gp.texWindow >> 10) & 0x1F) * 8, oy = (int)((gp.texWindow >> 15) & 0x1F) * 8;
    u = (u & ~mx) | (ox & mx);
    v = (v & ~my) | (oy & my);
    int tx = (int)(s->tpage & 0xF) * 64;
    int ty = (int)((s->tpage >> 4) & 1) * 256;
    int depth = (int)((s->tpage >> 7) & 3);
    if (gp.wide) {
        int beside = x < gp.wd.edgeX0 || x > gp.wd.edgeX1;
        if (depth < 2 || gp.wideTex == 0 || (tx + u >= VRAM_W && !beside)) {
            return 0;
        }
        *ax = (tx + u + (int16_t)(gp.wideTex & 0xFFFFu)) & (WIDE_W - 1);
        *ay = (ty + v + ((int32_t)(gp.wideTex << 1) >> 17)) & (WIDE_H - 1);
        return 1;
    }
    *ax = (tx + (depth >= 2 ? u : u >> (depth == 0 ? 2 : 1))) & (VRAM_W - 1);
    *ay = (ty + v) & (VRAM_H - 1);
    return 1;
}

static int selfWrites(const SelfDraw* s, int x, int y) {
    if (y < s->box.y0 || y > s->box.y1 || x < s->box.x0 || x > s->box.x1) {
        return 0;
    }
    int xa, xb;
    selfRow(s, y, &xa, &xb);
    return x >= xa && x <= xb;
}

/* Each pixel's step into stepMap; the last step (0: one draw does), -1 if
 * the chain is too long to draw in steps. */
static int selfSteps(const SelfDraw* s) {
    int bw = s->box.x1 - s->box.x0 + 1;
    memset(stepMap, 0, sizeof(stepMap[0]) * (size_t)bw * (size_t)(s->box.y1 - s->box.y0 + 1));
    int last = 0;
    for (int i = 0; i <= s->box.y1 - s->box.y0; i++) {
        int y = selfRowAt(s, i);
        int xa, xb;
        selfRow(s, y, &xa, &xb);
        for (int x = xa; x <= xb; x++) {
            uint32_t* m = &stepMap[(y - s->box.y0) * bw + (x - s->box.x0)];
            int ax, ay;
            if (selfTexel(s, x, y, &ax, &ay) && (ax != x || ay != y) && selfWrites(s, ax, ay)) {
                uint32_t* t = &stepMap[(ay - s->box.y0) * bw + (ax - s->box.x0)];
                if (*m >= STEP_MAX || *t >= STEP_MAX) {
                    return -1;
                }
                int before = selfRowRank(s, ay) - selfRowRank(s, y);
                if (before < 0 || (before == 0 && ax < x)) {
                    if (*t >= *m) {
                        *m = (*t + 1); /* after the write it reads */
                    }
                } else if (*t <= *m) {
                    *t = (*m + 1); /* the overwrite after this read */
                }
            }
            last = (int)*m > last ? (int)*m : last;
        }
    }
    return last;
}

/* The batch's tiles after a barrier inside a primitive, as batchAdd left
 * them for it. */
static void batchRetrack(VRect w, const ReadSet* reads) {
    if (gp.wide) {
        VRect wideRead;
        if (wideReadRect(reads, &wideRead)) {
            tilesAdd(&gp.batchWideRead, wideRead);
        }
        if (w.y1 > VRAM_H - 1) {
            w.y1 = VRAM_H - 1;
        }
        tilesAdd(&gp.batchWideWritten, wideTiles(w));
    } else if (clipToVram(&w)) {
        nativeAdd(&gp.batchWritten, &gp.batchWrittenExact, w);
    }
    for (int i = 0; i < reads->n; i++) {
        nativeAdd(&gp.batchRead, &gp.batchReadExact, reads->r[i]);
    }
}

static void stepFlush(const SelfDraw* s, int n) {
    if (n == 0) {
        return;
    }
    if (s->spans != NULL) {
        if (gp.wide) {
            gvkDrawSpansWide(stepBuf, n, s->spans, s->ints);
        } else {
            gvkDrawSpans(stepBuf, n, s->spans, s->ints);
        }
    } else if (gp.wide) {
        gvkDrawWide(stepBuf, n);
    } else {
        gvkDraw(stepBuf, n);
    }
}

/* Draw a primitive that already joined the batch; 0 if one draw does it. */
static int selfDraw(const SelfDraw* s, const ReadSet* reads) {
    size_t pixels = (size_t)(s->box.x1 - s->box.x0 + 1) * (size_t)(s->box.y1 - s->box.y0 + 1);
    if (pixels > sizeof(stepMap) / sizeof(stepMap[0]) || !selfOverlap(s->box, reads, s->tpage, s->tex >> 16)) {
        return 0;
    }
    HiVertex enhanced[3];VRect capture={0};int enhancedOverlap=0;
    int enhancedMode=gp.wide && gvkHiScale()>1 && s->tri!=NULL;
    int last;
    if(enhancedMode) {
        for(int k=0;k<3;k++) {
            const PVertex* p=s->tri[k];
            hiVertex(&enhanced[k],(float)p->x,(float)p->y,(float)p->u,(float)p->v,(float)p->r,(float)p->g,(float)p->b,s->mode,s->tex,s->uvMin,s->uvMax);
            int sx=(s->tpage&15)*64+(int16_t)gp.wideTex,sy=((s->tpage>>4)&1)*256+((int32_t)(gp.wideTex<<1)>>17);
            enhanced[k].sample[0]=(uint32_t)(uint16_t)sx|(uint32_t)(uint16_t)sy<<16;
        }
        if(!interpCaptureRect(enhanced,&capture)||capture.x0<0||capture.y0<0||capture.x1>=WIDE_W||capture.y1>=WIDE_H)enhancedMode=0;
    }
    last=enhancedMode?interpCaptureSteps(enhanced,s->spans,s->box,stepMap,gvkHiScale(),&enhancedOverlap):selfSteps(s);
    if (last < 0) {
        gpuWarnOnce("selfsteps", "gpu: a primitive reads its own pixels in a chain too long to order");
    }
    if (last < 0 || (!last && !(enhancedMode&&enhancedOverlap))) return 0;
    int bw = s->box.x1 - s->box.x0 + 1;
    /* The copy draws each 1x pixel's block in its step. */
    Plane pl[5];
    int hi = gvkHiScale() != 0 && !(gp.pkt && gp.pkt->geometry == PC_GEOMETRY_HIDE && hiresPrecision()) &&
             (s->tri == NULL || trianglePlanes(s->tri, pl));
    for (int step = 0; step <= last; step++) {
        if (step > 0) {
            batchBreak();
            batchRetrack(s->box, reads);
        }
        int snapshot=enhancedMode&&gvkHiFeedbackSource(capture);
        int n = 0;
        for (int y = s->box.y0; y <= s->box.y1; y++) {
            const uint32_t* row = &stepMap[(y - s->box.y0) * bw];
            int xa, xb;
            selfRow(s, y, &xa, &xb);
            for (int x = xa; x <= xb;) {
                if (row[x - s->box.x0] != (uint32_t)step) {
                    x++;
                    continue;
                }
                int x0 = x;
                while (x <= xb && row[x - s->box.x0] == (uint32_t)step) {
                    x++;
                }
                float fx0 = (float)x0, fx1 = (float)x, fy0 = (float)y, fy1 = (float)(y + 1);
                float s0 = 0, t0 = 0, s1 = 0, t1 = 0;
                if (s->spans == NULL) {
                    s0 = (float)(s->u + s->du * (x0 - s->x)) - 0.5f * s->du;
                    t0 = (float)(s->v + s->dv * (y - s->y)) - 0.5f * s->dv;
                    s1 = s0 + (float)(x - x0) * s->du;
                    t1 = t0 + s->dv;
                }
                GpuVertex* o = &stepBuf[n];
                setVertex(&o[0], fx0, fy0, s0, t0, s->color, s->mode, s->tex);
                setVertex(&o[1], fx1, fy0, s1, t0, s->color, s->mode, s->tex);
                setVertex(&o[2], fx0, fy1, s0, t1, s->color, s->mode, s->tex);
                setVertex(&o[3], fx1, fy0, s1, t0, s->color, s->mode, s->tex);
                setVertex(&o[4], fx0, fy1, s0, t1, s->color, s->mode, s->tex);
                setVertex(&o[5], fx1, fy1, s1, t1, s->color, s->mode, s->tex);
                n += 6;
                if (hi && s->tri == NULL) {
                    hiQuad(fx0, fy0, fx1, fy1, (float)(s->u + s->du * (x0 - s->x)),
                           (float)(s->v + s->dv * (y - s->y)), (fx1 - fx0) * s->du, (float)s->dv,
                           s->color, s->mode, s->tex, s->uvMin, s->uvMax);
                } else if (hi) {
                    HiVertex q[6];
                    float cx[4] = {fx0, fx1, fx0, fx1}, cy[4] = {fy0, fy0, fy1, fy1};
                    for (int k = 0; k < 4; k++) {
                        hiVertex(&q[k], cx[k], cy[k], planeAt(&pl[0], cx[k], cy[k]), planeAt(&pl[1], cx[k], cy[k]),
                                 planeAt(&pl[2], cx[k], cy[k]), planeAt(&pl[3], cx[k], cy[k]),
                                 planeAt(&pl[4], cx[k], cy[k]), s->mode, s->tex, s->uvMin, s->uvMax);
                    }
                    if(snapshot)for(int k=0;k<4;k++) {
                        q[k].sample[0]=(uint32_t)(uint16_t)((int16_t)enhanced[0].sample[0]-capture.x0)|
                            (uint32_t)(uint16_t)((int16_t)(enhanced[0].sample[0]>>16)-capture.y0)<<16;
                        q[k].sample[3]=INTERP_SAMPLE_CAPTURE|INTERP_SAMPLE_NATIVE_STEP;
                    }
                    q[5] = q[3];
                    q[3] = q[1];
                    q[4] = q[2];
                    hiEmit(q, 6);
                }
                if (n == (int)(sizeof(stepBuf) / sizeof(stepBuf[0]))) {
                    stepFlush(s, n);
                    n = 0;
                }
            }
        }
        stepFlush(s, n);
    }
    return 1;
}

/* The texel pixel (x, y) of a textured primitive samples, called for each
 * drawn pixel in drawing order (selfRowAt). *stale is set when the texel differs from
 * what current VRAM holds, which makes cacheRaster keep the decoded texels.
 *
 * Every fetch goes through the texture cache (see the caches above): a hit
 * uses the entry's halfwords even if VRAM changed since, a miss reloads the
 * entry's four halfwords from current VRAM. The texture window and the 8-bit
 * U/V wrap come first (selfTexel). A 4- or 8-bit texel's palette entry
 * comes from the CLUT cache, which cacheClut loaded for this primitive. */
static uint16_t cacheTexel(const SelfDraw* s, int x, int y, int* stale) {
    if ((s->tpage & ~0x60u) == 0x8180u) {
        int u,v;
        if (s->spans) {
            const int32_t* row = s->spans + SPAN_HEADER + (y-s->spans[0])*SPAN_ROW;
            u=wrap32(row[2]+(int64_t)(x-row[0])*s->spans[2])>>16;
            v=wrap32(row[3]+(int64_t)(x-row[0])*s->spans[3])>>16;
        } else { u=s->u+s->du*(x-s->x); v=s->v+s->dv*(y-s->y); }
        return promptTexel(s->tex>>16,u,v);
    }
    int ax = 0, ay = 0;
    selfTexel(s, x, y, &ax, &ay);
    unsigned depth = (s->tpage >> 7) & 3;
    int slot = depth == 0 ? (ay & 63) << 2 | (ax >> 2 & 3) : (ay & 31) << 3 | (ax >> 2 & 7);
    int32_t tag = ay << 8 | ax >> 2;
    if (gp.tex[slot].tag != tag) {
        memcpy(gp.tex[slot].data, &vramShadow[ay][ax & ~3], sizeof(gp.tex[slot].data));
        gp.tex[slot].tag = tag;
        cacheReloaded(slot);
    }
    uint16_t word = gp.tex[slot].data[ax & 3];
    uint16_t current = vramShadow[ay][ax];
    if (depth >= 2) {
        if (word != current) *stale = 1;
        return word;
    }
    int u;
    if (s->spans) {
        const int32_t* row = s->spans + SPAN_HEADER + (y - s->spans[0]) * SPAN_ROW;
        u = wrap32(row[2] + (int64_t)(x - row[0]) * s->spans[2]) >> 16;
    } else u = s->u + s->du * (x - s->x);
    int mask = (gp.texWindow & 31) * 8, offset = ((gp.texWindow >> 10) & 31) * 8;
    u = ((u & 255) & ~mask) | (offset & mask);
    unsigned shift = depth == 0 ? (u & 3) * 4 : (u & 1) * 8, bits = depth == 0 ? 15 : 255;
    uint16_t texel = gp.clut[(word >> shift) & bits];
    unsigned clut = s->tex >> 16;
    unsigned index = (current >> shift) & bits;
    if (texel != vramShadow[(clut >> 6) & 511][((clut & 63) * 16 + index) & 1023]) *stale = 1;
    return texel;
}

/* cacheTexel for one row of a primitive that does not read its own pixels
 * (cacheRaster's common case): the same fetches in the same order, with the
 * row's texture coordinates stepped as the shader steps them (spanBuf: the
 * row's 16.16 values and the header's per-pixel steps). clutClean says the
 * CLUT cache equals VRAM. Writes the texels to out; returns nonzero when one
 * of them differs from what current VRAM gives. */
static int cacheRow(const SelfDraw* s, const int32_t* row, int clutClean, int32_t* out) {
    uint32_t ua = (uint32_t)row[2], va = (uint32_t)row[3];
    uint32_t du = (uint32_t)spanBuf[2], dv = (uint32_t)spanBuf[3];
    unsigned mx = (gp.texWindow & 31) * 8, my = ((gp.texWindow >> 5) & 31) * 8;
    unsigned ox = ((gp.texWindow >> 10) & 31) * 8 & mx, oy = ((gp.texWindow >> 15) & 31) * 8 & my;
    unsigned tx = (s->tpage & 15) * 64, ty = ((s->tpage >> 4) & 1) * 256, depth = (s->tpage >> 7) & 3;
    unsigned shift = depth == 0 ? 2 : depth == 1 ? 1 : 0, bits = depth == 0 ? 15 : 255;
    unsigned clut = s->tex >> 16;
    const uint16_t* palette = vramShadow[(clut >> 6) & 511];
    unsigned cx = (clut & 63) * 16;
    int stale = 0;
    for (int x = row[0]; x <= row[1]; x++, ua += du, va += dv) {
        unsigned u = ((unsigned)((int32_t)ua >> 16) & 255 & ~mx) | ox;
        unsigned v = ((unsigned)((int32_t)va >> 16) & 255 & ~my) | oy;
        unsigned ax = (tx + (u >> shift)) & (VRAM_W - 1), ay = (ty + v) & (VRAM_H - 1);
        int slot = depth == 0 ? (ay & 63) << 2 | (ax >> 2 & 3) : (ay & 31) << 3 | (ax >> 2 & 7);
        int32_t tag = (int32_t)(ay << 8 | ax >> 2);
        if (gp.tex[slot].tag != tag) {
            memcpy(gp.tex[slot].data, &vramShadow[ay][ax & ~3u], sizeof(gp.tex[slot].data));
            gp.tex[slot].tag = tag;
            cacheReloaded(slot);
        }
        uint16_t word = gp.tex[slot].data[ax & 3], current = vramShadow[ay][ax];
        if (depth >= 2) {
            *out++ = word;
            stale |= word != current;
            continue;
        }
        unsigned sh = depth == 0 ? (u & 3) * 4 : (u & 1) * 8;
        unsigned index = (word >> sh) & bits;
        uint16_t texel = gp.clut[index];
        *out++ = texel;
        if (word != current || !clutClean) {
            stale |= texel != palette[(cx + ((current >> sh) & bits)) & (VRAM_W - 1)];
        }
    }
    return stale;
}

/* A feedback primitive's pixel, shaded and blended on the CPU copy, so that
 * later texels of the same primitive can see it; the same steps as
 * draw.frag: texel 0000h transparent, mask check (psx-spx "GP0(E6h)"),
 * modulation texel * colour / 128 (psx-spx "Modulation"), the dither offset
 * added and the result saturated and divided by 8 (psx-spx dithering), then
 * semi-transparency on the 5-bit values for texels with bit 15 (psx-spx
 * "Semi-transparency"; quantising before blending was measured, M-G6). */
static void cacheShade(const SelfDraw* s, const int32_t* row, int x, int y, uint16_t texel) {
    uint16_t dst = vramShadow[y][x];
    if (((s->mode & DRAW_TEXTURED) && !texel) || ((s->mode & DRAW_CHECK_MASK) && (dst & 0x8000))) return;
    int color[3] = {s->color->r, s->color->g, s->color->b};
    if (s->mode & DRAW_GOURAUD) for (int c = 0; c < 3; c++)
        color[c] = wrap32(row[4 + c] + (int64_t)(x - row[0]) * spanBuf[4 + c]) >> 16;
    static const int8_t dither[16] = {-4,0,-3,1,2,-2,3,-1,-3,1,-4,0,3,-1,2,-2};
    int d = s->mode & DRAW_DITHER ? dither[(y & 3) * 4 + (x & 3)] : 0;
    uint16_t result = (s->mode & DRAW_TEXTURED ? texel & 0x8000 : 0) | (s->mode & DRAW_SET_MASK ? 0x8000 : 0);
    for (int c = 0; c < 3; c++) {
        int front8 = s->mode & DRAW_TEXTURED ? (((texel >> (c * 5)) & 31) * (s->mode & DRAW_RAW ? 128 : color[c]) >> 4) : color[c];
        int front = (front8 + d) >> 3;
        front = front < 0 ? 0 : front > 31 ? 31 : front;
        int back = (dst >> (c * 5)) & 31;
        if ((s->mode & DRAW_SEMI) && (!(s->mode & DRAW_TEXTURED) || (texel & 0x8000))) {
            switch ((s->mode >> DRAW_SEMI_SHIFT) & 3) {
            case 0: front = (back + front) >> 1; break;
            case 1: front += back; break;
            case 2: front = back - front; break;
            case 3: front = back + (front >> 2); break;
            }
            front = front < 0 ? 0 : front > 31 ? 31 : front;
        }
        result |= front << (c * 5);
    }
    vramShadow[y][x] = result;
}

static void cacheHiResolve(VRect box) {
    if (!gvkHiScale()) return;
    vramSyncRect(box);
    gvkHiResolveCpu(box,&vramShadow[box.y0][box.x0],VRAM_W);
}

/* A waiting walk's effect on the cache: cacheRow's fetches, entries only.
 * rec: rows, tpage, texture window, the steps per pixel, then each row's
 * first and last x and its starting coordinates, in the walk's order. */
static void cacheTouch(const int32_t* rec) {
    unsigned tpage = (unsigned)rec[1], window = (unsigned)rec[2];
    uint32_t du = (uint32_t)rec[3], dv = (uint32_t)rec[4];
    unsigned mx = (window & 31) * 8, my = ((window >> 5) & 31) * 8;
    unsigned ox = ((window >> 10) & 31) * 8 & mx, oy = ((window >> 15) & 31) * 8 & my;
    unsigned tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    unsigned shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    for (int r = 0; r < rec[0]; r++) {
        const int32_t* row = rec + 5 + r * 4;
        uint32_t ua = (uint32_t)row[2], va = (uint32_t)row[3];
        for (int x = row[0]; x <= row[1]; x++, ua += du, va += dv) {
            unsigned u = ((unsigned)((int32_t)ua >> 16) & 255 & ~mx) | ox;
            unsigned v = ((unsigned)((int32_t)va >> 16) & 255 & ~my) | oy;
            unsigned ax = (tx + (u >> shift)) & (VRAM_W - 1), ay = (ty + v) & (VRAM_H - 1);
            int slot = depth == 0 ? (ay & 63) << 2 | (ax >> 2 & 3) : (ay & 31) << 3 | (ax >> 2 & 7);
            int32_t tag = (int32_t)(ay << 8 | ax >> 2);
            if (gp.tex[slot].tag != tag) {
                memcpy(gp.tex[slot].data, &vramShadow[ay][ax & ~3u], sizeof(gp.tex[slot].data));
                gp.tex[slot].tag = tag;
                cacheReloaded(slot);
            }
        }
    }
}

/* The waiting walks, done now. */
static void cacheCatchUp(void) {
    for (int i = 0; i < cachePendUsed; i += 5 + 4 * cachePend[i]) {
        cacheTouch(&cachePend[i]);
    }
    cachePendUsed = 0;
}

/* Copy the logical cache, settling owed reads only in private scratch.
 * No native register, entry, pending walk or VRAM synchronisation changes. */
void gp0InterpolationCache(uint32_t* words) {
    for (int i = 0; i < 256; i++) {
        words[i*5] = (uint32_t)gp.tex[i].tag;
        for (int k = 0; k < 4; k++) words[i*5+1+k] = gp.tex[i].data[k];
    }
    for (int off = 0; off < cachePendUsed; off += 5+4*cachePend[off]) {
        const int32_t* rec = cachePend+off;
        unsigned page = (unsigned)rec[1], window = (unsigned)rec[2];
        unsigned mx = (window&31)*8, my = ((window>>5)&31)*8;
        unsigned ox = ((window>>10)&31)*8&mx, oy = ((window>>15)&31)*8&my;
        unsigned tx = (page&15)*64, ty = ((page>>4)&1)*256, depth = (page>>7)&3;
        unsigned shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        for (int r = 0; r < rec[0]; r++) {
            const int32_t* row = rec+5+r*4;
            uint32_t u = (uint32_t)row[2], v = (uint32_t)row[3];
            for (int x = row[0]; x <= row[1]; x++, u += (uint32_t)rec[3], v += (uint32_t)rec[4]) {
                unsigned ax = (tx+(((((unsigned)((int32_t)u>>16)&255)&~mx)|ox)>>shift))&1023;
                unsigned ay = (ty+((((unsigned)((int32_t)v>>16)&255)&~my)|oy))&511;
                unsigned slot = depth == 0 ? (ay&63)<<2|(ax>>2&3) : (ay&31)<<3|(ax>>2&7);
                uint32_t tag = ay<<8|ax>>2;
                if (words[slot*5] != tag) {
                    words[slot*5] = tag;
                    for (int k = 0; k < 4; k++) words[slot*5+1+k] = vramShadow[ay][(ax&~3u)+k];
                }
            }
        }
    }
}

/* The VRAM a walk with this texture page may fetch from. */
static void cachePageTiles(unsigned tpage) {
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256, depth = (int)((tpage >> 7) & 3);
    int w = depth == 0 ? 64 : depth == 1 ? 128 : 256;
    tilesAdd(&cacheTiles, (VRect){tx, ty, tx + w - 1 < VRAM_W ? tx + w - 1 : VRAM_W - 1, ty + 255});
    if (tx + w > VRAM_W) {
        tilesAdd(&cacheTiles, (VRect){0, ty, tx + w - 1 - VRAM_W, ty + 255});
    }
}

/* VRAM (the device's, or the CPU copy the walks read) is about to change in
 * r: a walk still owed must see it as it was, and an entry from there may no
 * longer equal it. */
void gp0VramChanging(VRect r) {
    interpolationSourceChanging(r);
    if (!tilesHit(&cacheTiles, r)) {
        return;
    }
    cacheCatchUp();
    cacheMarkStale(r);
    cacheDirty = 1;
}

/* A walk that cannot change this primitive's texels, kept for later; 0 when
 * there is no room even after the waiting ones are done. */
static int cacheDefer(const SelfDraw* s) {
    int rows = s->box.y1 - s->box.y0 + 1, need = 5 + 4 * rows;
    if (cachePend == NULL && (cachePend = malloc(CACHE_PEND_INTS * sizeof(int32_t))) == NULL) {
        return 0;
    }
    if (cachePendUsed + need > CACHE_PEND_INTS) {
        cacheCatchUp();
        if (need > CACHE_PEND_INTS) {
            return 0;
        }
    }
    int32_t* rec = &cachePend[cachePendUsed];
    rec[0] = rows;
    rec[1] = (int32_t)s->tpage;
    rec[2] = (int32_t)gp.texWindow;
    rec[3] = spanBuf[2];
    rec[4] = spanBuf[3];
    for (int i = 0; i < rows; i++) {
        int y = selfRowAt(s, i);
        const int32_t* row = spanBuf + SPAN_HEADER + (y - spanBuf[0]) * SPAN_ROW;
        memcpy(&rec[5 + i * 4], row, 4 * sizeof(int32_t));
    }
    cachePendUsed += need;
    cachePageTiles(s->tpage);
    return 1;
}

/* Whether a primitive reading these rectangles can hit an entry that may be
 * stale: then its walk must be done now (cacheRaster). */
static int cacheStaleHit(const ReadSet* reads) {
    if (!cacheDirty || cacheMaybeStaleCount == 0) return 0;
    for (int slot = 0; slot < 256; slot++) {
        if (!cacheMaybeStale[slot]) continue;
        int32_t tag = gp.tex[slot].tag;
        int ay = tag >> 8, ax = (tag & 255) << 2;
        for (int i = 0; i < reads->n; i++) {
            VRect r = reads->r[i];
            if (ay >= r.y0 && ay <= r.y1 && ax + 3 >= r.x0 && ax <= r.x1) return 1;
        }
    }
    return 0;
}

/* The VRAM copy of a command the wide pass drew too takes its hi-res
 * triangles as the wide pass does, not the cache walk's resolved pixels: the
 * two copies then agree (the wide pass never walks the cache, cacheIgnored). */
static int plainHiReplay(void) {
    return gp.replayed && !gp.wide && gvkHiScale() != 0;
}

/* 0 ordinary GPU path, 1 cached spans, 2 resolved draw, 3 native wave drawn. */
static int cacheRaster(const SelfDraw* s, const ReadSet* reads, int* ints) {
    if (gp.wide) return 0;
    int feedback = selfOverlap(s->box, reads, s->tpage, s->tex >> 16);
    /* Native wave pieces stay authoritative on the CPU, including pieces
     * whose source and destination do not overlap. This avoids downloading
     * their device result when a later piece touches the same dirty tile.
     * Keep the preceding scene triangles authoritative too: the wave soon
     * samples their framebuffer. Enhanced draws retain the original path.
     * The same for tiles the CPU reads after the frame drew them
     * (vramCpuOwned). */
    int cpuWave = !feedback && ((s->tri && interpolationWavePacket()) || interpolationWaveFrame() || (!gp.wide && vramCpuOwned(s->box)));
    if (feedback) interpolationReject("texture-feedback");
    for (int i = 0; i < reads->n; i++) vramSyncRect(reads->r[i]);
    if (feedback || cpuWave) {
        vramKeepWrite(s->box); /* a picture between's keep (vram.c): shaded on the CPU, into its copy */
        vramSyncRect(s->box);
    }
    if (!s->spans) {
        spanBuf[0] = s->box.y0; spanBuf[1] = s->box.y1 - s->box.y0 + 1;
        spanBuf[2] = s->du * 65536; spanBuf[3] = spanBuf[4] = spanBuf[5] = spanBuf[6] = spanBuf[7] = 0;
        for (int y = s->box.y0; y <= s->box.y1; y++) {
            int32_t* row = spanBuf + SPAN_HEADER + (y - s->box.y0) * SPAN_ROW;
            row[0] = s->box.x0; row[1] = s->box.x1;
            row[2] = (s->u + s->du * (s->box.x0 - s->x)) * 65536;
            row[3] = (s->v + s->dv * (y - s->y)) * 65536;
            row[4] = s->color->r * 65536; row[5] = s->color->g * 65536; row[6] = s->color->b * 65536;
        }
        *ints = SPAN_HEADER + SPAN_ROW * spanBuf[1];
    }
    int count = *ints, stale = 0;
    /* The common case a row at a time (cacheRow); feedback and the prompt
     * glyphs pixel by pixel. */
    int fast = !feedback && (s->tpage & ~0x60u) != 0x8180u;
    int clutClean = 1;
    if (fast && ((s->tpage >> 7) & 3) < 2) {
        unsigned clut = s->tex >> 16, n = ((s->tpage >> 7) & 3) == 0 ? 16 : 256;
        /* Whether the CLUT cache equals VRAM's palette row, once per state
         * of the two rather than 256 compares a primitive: the cache changes
         * only when cacheClut loads it (clutLoads), the row's copy only with
         * a generation of its tiles (an upload, a draw read back or shaded
         * on the CPU: vram.c changed() comes first every time). */
        int x0 = (int)(clut & 63) * 16, y = (int)(clut >> 6) & (VRAM_H - 1);
        VRect row = {x0, y, x0 + (int)n - 1 < VRAM_W ? x0 + (int)n - 1 : VRAM_W - 1, y};
        uint64_t gen = vramBlockNewest(row);
        if (x0 + (int)n > VRAM_W) {
            VRect rest = {0, y, x0 + (int)n - 1 - VRAM_W, y};
            uint64_t g = vramBlockNewest(rest);
            gen = g > gen ? g : gen;
        }
        if (!clutCleanMemo.valid || clutCleanMemo.loads != clutLoads || clutCleanMemo.clut != clut ||
            clutCleanMemo.n != n || clutCleanMemo.gen != gen || clutCleanMemo.serial != vramLayoutSerial) {
            const uint16_t* palette = vramShadow[y];
            for (unsigned i = 0; i < n && clutClean; i++) {
                clutClean = gp.clut[i] == palette[((clut & 63) * 16 + i) & (VRAM_W - 1)];
            }
            clutCleanMemo.valid = 1;
            clutCleanMemo.loads = clutLoads;
            clutCleanMemo.clut = clut;
            clutCleanMemo.n = n;
            clutCleanMemo.gen = gen;
            clutCleanMemo.serial = vramLayoutSerial;
            clutCleanMemo.clean = clutClean;
        }
        clutClean = clutCleanMemo.clean;
    }
    if (!feedback && (s->tpage & ~0x60u) == 0x8180u) {
        return 0; /* the prompt glyphs: no fetch goes through the cache */
    }
    if (fast && !cpuWave && clutClean && !cacheStaleHit(reads) && cacheDefer(s)) {
        return 0; /* every entry it can hit equals VRAM: the walk would give VRAM's texels */
    }
    cacheCatchUp();
    cachePageTiles(s->tpage);
    for (int i = 0; i <= s->box.y1 - s->box.y0; i++) {
        int y = selfRowAt(s, i);
        int32_t* row = spanBuf + SPAN_HEADER + (y - spanBuf[0]) * SPAN_ROW;
        row[7] = count;
        if (fast) {
            stale |= cacheRow(s, row, clutClean, spanBuf + count);
            count += row[1] >= row[0] ? row[1] - row[0] + 1 : 0;
        } else {
            for (int x = row[0]; x <= row[1]; x++) {
                uint16_t texel = cacheTexel(s, x, y, &stale);
                spanBuf[count++] = texel;
                if (feedback) cacheShade(s, row, x, y, texel);
            }
        }
        count = (count + 3) & ~3;
    }
    if (cpuWave) {
        for (int i = 0; i <= s->box.y1 - s->box.y0; i++) {
            int y = selfRowAt(s, i);
            const int32_t* row = spanBuf + SPAN_HEADER + (y - spanBuf[0]) * SPAN_ROW;
            for (int x = row[0]; x <= row[1]; x++)
                cacheShade(s, row, x, y, (uint16_t)spanBuf[row[7] + x - row[0]]);
        }
        if (stale) interpolationReject("stale-cached-texels");
    }
    if (feedback || cpuWave) {
        /* A resolved box reads owned CPU words in the enhanced shader. Its
         * native cache walk is not an additional GPU texture dependency. */
        batchAdd(s->box,feedback || stale ? NULL : reads);
        vramDeferCpuDraw(s->box);
        /* cacheRaster synchronized these tiles before shading them. The
         * upload contains the final pixels; reading them back adds a GPU
         * wait per triangle without changing the CPU image. */
        vramResolveCpuDraw(s->box);
        if (cpuWave && !stale) return 3;
        /* The hi-res copy of a command the wide pass drew comes from its
         * triangles, as the wide pass's does (plainHiReplay): the copies
         * then agree where the native walk drew nothing (a sliver at the
         * frame's edge the wide picture has). */
        if (!plainHiReplay()) cacheHiResolve(s->box);
        return 2;
    }
    if (stale) { interpolationReject("stale-cached-texels"); *ints = count; return 1; }
    return 0;
}

/* The VRAM a textured triangle may read, from its vertices' UV range. */
static void triangleReads(ReadSet* reads, const PVertex* a, const PVertex* b, const PVertex* c, uint32_t tpage,
                          uint32_t clut, int* uv) {
    const PVertex* vs[3] = {a, b, c};
    int u0 = a->u, u1 = a->u, v0 = a->v, v1 = a->v;
    for (int i = 1; i < 3; i++) {
        u0 = vs[i]->u < u0 ? vs[i]->u : u0;
        u1 = vs[i]->u > u1 ? vs[i]->u : u1;
        v0 = vs[i]->v < v0 ? vs[i]->v : v0;
        v1 = vs[i]->v > v1 ? vs[i]->v : v1;
    }
    /* The walker's rounding may step a texel past the vertices' range. */
    if (reads != NULL) {
        textureReads(reads, tpage, clut, u0 > 0 ? u0 - 1 : 0, u1 < 255 ? u1 + 1 : 255, v0 > 0 ? v0 - 1 : 0,
                     v1 < 255 ? v1 + 1 : 255);
    }
    if (uv != NULL) {
        uv[0] = u0;
        uv[1] = u1;
        uv[2] = v0;
        uv[3] = v1;
    }
}

/* A primitive only the hi-res copy draws joins the batch: as batchAdd, but
 * nothing 1x is told (VRAM, the wide picture and the game see nothing). */
static int batchAddHi(VRect w, const ReadSet* reads) {
    int width = gp.wide ? WIDE_FRAME_W : VRAM_W;
    w.x0 = w.x0 < gp.clipX0 ? gp.clipX0 : w.x0;
    w.y0 = w.y0 < gp.clipY0 ? gp.clipY0 : w.y0;
    w.x1 = w.x1 > gp.clipX1 ? gp.clipX1 : w.x1;
    w.y1 = w.y1 > gp.clipY1 ? gp.clipY1 : w.y1;
    w.x0 = w.x0 < 0 ? 0 : w.x0;
    w.y0 = w.y0 < 0 ? 0 : w.y0;
    w.x1 = w.x1 > width - 1 ? width - 1 : w.x1;
    w.y1 = w.y1 > VRAM_H - 1 ? VRAM_H - 1 : w.y1;
    if (w.x0 > w.x1 || w.y0 > w.y1) {
        return 0;
    }
    if (gp.wide) {
        gvkWideTouch(w.x1);
    }
    VRect t = hiPad(w, width);
    TileSet* written = gp.wide ? &gp.batchWideWritten : &gp.batchWritten;
    TileSet* read = gp.wide ? &gp.batchWideRead : &gp.batchRead;
    VRect wideRead;
    int hasWideRead = gp.wide && wideReadRect(reads, &wideRead);
    if (gp.wide) {
        t = wideTiles(t);
    }
    int conflict = (gp.wide ? tilesHit(read, t) : nativeHit(&gp.batchRead, &gp.batchReadExact, t)) ||
                   (!gvkOrderedWrites() && (gp.wide ? tilesHit(written, t) : nativeHit(&gp.batchWritten, &gp.batchWrittenExact, t))) ||
                   (hasWideRead && tilesHit(&gp.batchWideWritten, wideRead));
    for (int i = 0; reads != NULL && i < reads->n && !conflict; i++) {
        conflict = nativeHit(&gp.batchWritten, &gp.batchWrittenExact, reads->r[i]);
    }
    if (conflict) {
        batchBreak();
    }
    if (gp.wide) tilesAdd(written, t);
    else nativeAdd(&gp.batchWritten, &gp.batchWrittenExact, t);
    if (hasWideRead) {
        tilesAdd(&gp.batchWideRead, wideRead);
    }
    for (int i = 0; reads != NULL && i < reads->n; i++) {
        nativeAdd(&gp.batchRead, &gp.batchReadExact, reads->r[i]);
    }
    if (!gp.wide) {
        hiresMarkDetail(t);
    }
    return 1;
}

/* A triangle with an area whose every row misses the 1x sample points (a
 * sliver between two pixels): the copy still draws it, or the polygons
 * beside it would show a hairline gap. */
static void hiSliver(const PVertex* a, const PVertex* b, const PVertex* c, uint32_t mode, uint32_t tpage,
                     uint32_t clut, unsigned perimeter) {
    int64_t area2 = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(c->x - a->x) * (b->y - a->y);
    int precise = a->precise && b->precise && c->precise;
    double preciseArea = precise ?
        ((double)b->preciseX - a->preciseX) * ((double)c->preciseY - a->preciseY) -
        ((double)c->preciseX - a->preciseX) * ((double)b->preciseY - a->preciseY) : 0;
    if (area2 == 0 && preciseArea == 0) {
        return;
    }
    VRect box = {a->x, a->y, a->x, a->y};
    const PVertex* vs[3] = {a, b, c};
    for (int i = 1; i < 3; i++) {
        box.x0 = vs[i]->x < box.x0 ? vs[i]->x : box.x0;
        box.y0 = vs[i]->y < box.y0 ? vs[i]->y : box.y0;
        box.x1 = vs[i]->x > box.x1 ? vs[i]->x : box.x1;
        box.y1 = vs[i]->y > box.y1 ? vs[i]->y : box.y1;
    }
    box.x1--; /* the right and bottom edges are not drawn */
    box.y1--;
    /* Coincident integer vertices can enclose a real fractional wedge.
     * Include its conservative bounds even when an integer extent is zero. */
    if (precise) {
        for (int i = 0; i < 3; i++) {
            int x = (int)vs[i]->preciseX, y = (int)vs[i]->preciseY;
            box.x0 = x - 1 < box.x0 ? x - 1 : box.x0;
            box.y0 = y - 1 < box.y0 ? y - 1 : box.y0;
            box.x1 = x + 1 > box.x1 ? x + 1 : box.x1;
            box.y1 = y + 1 > box.y1 ? y + 1 : box.y1;
        }
    }
    if (box.x1 < box.x0 || box.y1 < box.y0 ||
        (!precise && (int64_t)(box.x1 - box.x0 + 1) * (box.y1 - box.y0 + 1) > 4096)) {
        return;
    }
    ReadSet reads = {0};
    int uv[4] = {0, 0, 0, 0};
    if (mode & DRAW_TEXTURED) {
        triangleReads(&reads, a, b, c, tpage, clut, uv);
    }
    prepareTexture(mode,tpage | clut<<16,uvWord(uv[0],uv[2]),uvWord(uv[1],uv[3]),box,&reads);
    if (!batchAddHi(box, &reads)) {
        return;
    }
    hiTriangle(a, b, c, mode, tpage | clut << 16, uvWord(uv[0], uv[2]), uvWord(uv[1], uv[3]), perimeter);
}

static void triangle(const PVertex* a, const PVertex* b, const PVertex* c, uint32_t mode, uint32_t tpage,
                     uint32_t clut, unsigned perimeter) {
    gp.sampled = 0;
    int geometry = gp.pkt ? gp.pkt->geometry : 0;
    int hi = gvkHiScale() != 0 && !(geometry == PC_GEOMETRY_HIDE && hiresPrecision());
    if (geometry == PC_GEOMETRY_ONLY) {
        if (hi && a->precise && b->precise && c->precise) {
            hiSliver(a, b, c, mode, tpage, clut, perimeter);
        }
        return;
    }
    VRect box;
    CPU_TIME_BEGIN(CPU_SPANS);
    int n = buildSpans(a, b, c, (mode & (DRAW_TEXTURED | DRAW_GOURAUD)) != 0, &box, spanBuf,
                       (VRect){gp.clipX0,gp.clipY0,gp.clipX1,gp.clipY1});
    CPU_TIME_END(CPU_SPANS);
    if (gp.hiSliverOnly && (n != 0 || !hi)) {
        return; /* the native walk would draw it: not a wide-only polygon's sliver (gp.hiSliverOnly) */
    }
    if (n == 0) {
        if (interpolationWavePacket() || (hi&&interpolationCaptureSample()&&a->precise&&b->precise&&c->precise)) {
            int uv[4];triangleReads(NULL,a,b,c,tpage,clut,uv);
            recordCapturedTriangle(a,b,c,mode,tpage|clut<<16,uvWord(uv[0],uv[2]),uvWord(uv[1],uv[3]),perimeter,hi);
        }
        if (hi) {
            hiSliver(a, b, c, mode, tpage, clut, perimeter);
        }
        return;
    }
    ReadSet reads = {0};
    if (mode & DRAW_TEXTURED) {
        triangleReads(&reads, a, b, c, tpage, clut, NULL);
    }
    if ((mode & DRAW_TEXTURED) && !gp.wide && textureDumpEnabled()) {
        uint32_t lo, hi;
        spanTexels(spanBuf, &lo, &hi);
        observeTexture(tpage | clut << 16,lo,hi);
    }
    probe("triangle", box.x0, box.y0, box.x1, box.y1);
    if (probeX >= box.x0 && probeX <= box.x1 && probeY >= box.y0 && probeY <= box.y1) {
        const PVertex* points[3] = {a, b, c};
        for (int k = 0; k < 3; k++) {
            const PVertex* p = points[k];
            if (p->precise) fprintf(stderr, "  vertex %d: %d,%d -> %.6f,%.6f z=%.6f\n", k, p->x, p->y,
                                   p->preciseX, p->preciseY, p->depth);
            else fprintf(stderr, "  vertex %d: %d,%d integer\n", k, p->x, p->y);
        }
    }
    uint32_t tex = tpage | clut << 16;
    uint32_t uvMin = 0, uvMax = 0;
    if ((hi || (interpolationCaptureSample() && interpolationRecording(gp.wide))) && (mode & DRAW_TEXTURED)) {
        if (gp.flatUvValid) {
            /* 2D classes: the whole polygon's native texels (flatSpanTexels). */
            uvMin = gp.flatUvMin;
            uvMax = gp.flatUvMax;
        } else if (a->precise && b->precise && c->precise) {
            /* Fractional edges and projective interpolation can sample
             * beyond the integer walker's clipped affine range. The
             * vertices still bound the texture patch. */
            int uv[4];
            triangleReads(NULL, a, b, c, tpage, clut, uv);
            uvMin = uvWord(uv[0], uv[2]);
            uvMax = uvWord(uv[1], uv[3]);
        } else {
            /* The same triangle in the wide pass (the passes may draw a
             * different number of them: one half can miss every sample in
             * the frame): the quad's half (perimeter 6: the second) in its
             * frame buffer. */
            int k = (perimeter == 6 ? 1 : 0) + ((a->y - (gp.wide ? gp.wideShiftY : 0)) >= 256 ? 2 : 0);
            if (!gp.wide && gp.replayed && gp.wideUv.triSaved[k]) {
                uvMin = gp.wideUv.triMin[k]; uvMax = gp.wideUv.triMax[k];
            } else {
                spanTexels(spanBuf, &uvMin, &uvMax);
                if (gp.wide) { gp.wideUv.triSaved[k] = 1; gp.wideUv.triMin[k] = uvMin; gp.wideUv.triMax[k] = uvMax; }
            }
        }
    }
    /* Captured effects need each triangle's authored sampled bounds,
     * including the integer walker's clamp near the diagonal at >1x. */
    recordCapturedTriangle(a,b,c,mode,tex,uvMin,uvMax,perimeter,hi);
    int cached = 0;
    if (!(mode & DRAW_TEXTURED) && !gp.wide && (interpolationWaveFrame() || vramCpuOwned(box))) {
        vramKeepWrite(box); /* a picture between's keep (vram.c): shaded on the CPU, into its copy */
        vramSyncRect(box);
        const PVertex* tri[3] = {a, b, c};
        SelfDraw s = {box, spanBuf, n, 0, 0, 0, 0, tpage, mode, tex, a, tri, uvMin, uvMax, 0, 0};
        batchAdd(box, &reads);
        for (int y = box.y0; y <= box.y1; y++) {
            const int32_t* row = spanBuf + SPAN_HEADER + (y-spanBuf[0])*SPAN_ROW;
            for (int x = row[0]; x <= row[1]; x++) cacheShade(&s, row, x, y, 0);
        }
        vramDeferCpuDraw(box);
        vramResolveCpuDraw(box);
        prepareTexture(mode,tex,uvMin,uvMax,box,&reads);
        if (hi) hiTriangle(a,b,c,mode,tex,uvMin,uvMax,perimeter);
        return;
    }
    if ((mode & DRAW_TEXTURED) && !gp.wide) {
        const PVertex* tri[3] = {a, b, c};
        SelfDraw s = {box, spanBuf, n, 0, 0, 0, 0, tpage, mode, tex, a, tri, uvMin, uvMax, 0, 0};
        CPU_TIME_BEGIN(CPU_CACHE);
        cached = cacheRaster(&s, &reads, &n);
        CPU_TIME_END(CPU_CACHE);
        if (cached == 2 || cached == 3) {
            if (hi && (cached == 3 || plainHiReplay())) {
                prepareTexture(mode, tex, uvMin, uvMax, box, &reads);
                hiTriangle(a, b, c, mode, tex, uvMin, uvMax, perimeter);
            }
            return;
        }
        if (cached) mode |= DRAW_CACHED;
    }
    prepareTexture(mode,tex,uvMin,uvMax,box,&reads);
    if (!batchAdd(box, &reads)) {
        return;
    }
    if ((mode & DRAW_TEXTURED) && gp.wide) {
        const PVertex* tri[3] = {a, b, c};
        SelfDraw s = {box, spanBuf, n, 0, 0, 0, 0, tpage, mode, tex, a, tri, uvMin, uvMax, 0, 0};
        if (selfDraw(&s, &reads)) {
            return;
        }
    }
    /* The bounding box of the drawn pixels; the spans decide which. */
    GpuVertex out[6];
    float x0 = (float)box.x0, y0 = (float)box.y0, x1 = (float)(box.x1 + 1), y1 = (float)(box.y1 + 1);
    setVertex(&out[0], x0, y0, 0, 0, a, mode, tex);
    setVertex(&out[1], x1, y0, 0, 0, a, mode, tex);
    setVertex(&out[2], x0, y1, 0, 0, a, mode, tex);
    setVertex(&out[3], x1, y0, 0, 0, a, mode, tex);
    setVertex(&out[4], x0, y1, 0, 0, a, mode, tex);
    setVertex(&out[5], x1, y1, 0, 0, a, mode, tex);
    if (gp.wide) {
        gvkDrawSpansWide(out, 6, spanBuf, n);
    } else {
        gvkDrawSpans(out, 6, spanBuf, n);
    }
    if (cached && !plainHiReplay()) { if (!gp.poseSkip) cacheHiResolve(box); }
    else if (hi) {
        hiTriangle(a, b, c, mode, tex, uvMin, uvMax, perimeter);
    }
}

/* psx-spx "Vertex" (size restriction): a polygon with an edge of 1024 or
 * more pixels across, or 512 or more down, is not drawn. Measured with Beetle
 * PSX as a black box (work/clean-room/measure/gpu): the edges are judged
 * before the drawing offset (mgI_wrap.py), and a quad on each of its two
 * triangles' own edges, a valid half still drawn (M-G5 quads, gputest
 * testQuadHalves). */
static int edgeTooLong(const PVertex* a, const PVertex* b) {
    int dx = a->x - b->x, dy = a->y - b->y;
    return dx >= 1024 || dx <= -1024 || dy >= 512 || dy <= -512;
}

/*
 * The extension rule (wide pass): a polygon from edge to edge of the draw
 * area (a fade, a flash, a clear, a band of sky) reaches the margins too.
 * An axis-aligned quad gets a piece on each side, from the widened draw
 * area's edge to its own, with its side's colours (a vertical gradient goes
 * on, nothing changes across), so its own pixels, and the centre, stay
 * exactly the 4:3 picture's; PC_WIDE_EXTEND (textured, or any shape)
 * stretches the polygon itself instead.
 */
static void triangle(const PVertex* a, const PVertex* b, const PVertex* c, uint32_t mode, uint32_t tpage,
                     uint32_t clut, unsigned perimeter);

static void widePolygonSides(PVertex* v, int n, int textured, uint32_t mode, uint32_t tpage, uint32_t clut, unsigned base) {
    int mn = v[0].x, mx = v[0].x, my0 = v[0].y, my1 = v[0].y;
    for (int k = 1; k < n; k++) {
        mn = v[k].x < mn ? v[k].x : mn;
        mx = v[k].x > mx ? v[k].x : mx;
        my0 = v[k].y < my0 ? v[k].y : my0;
        my1 = v[k].y > my1 ? v[k].y : my1;
    }
    if (mn > gp.wd.edgeX0 || mx < gp.wd.edgeX1 + 1) {
        return;
    }
    /* Screen-space stretching has no projected geometry correspondence. */
    for (int k = 0; k < n; k++) v[k].precise = 0;
    if (textured || (gp.pkt != NULL && (gp.pkt->flags & PC_WIDE_EXTEND))) {
        for (int k = 0; k < n; k++) {
            v[k].x = v[k].x == mn ? gp.wd.clipX0 : v[k].x == mx ? gp.wd.clipX1 + 1 : v[k].x;
        }
        return;
    }
    /* An axis-aligned quad: each corner at a min or max of x and y. */
    const PVertex *tl = NULL, *bl = NULL, *tr = NULL, *br = NULL;
    for (int k = 0; k < n; k++) {
        int left = v[k].x == mn, right = v[k].x == mx, top = v[k].y == my0, bottom = v[k].y == my1;
        if (!(left || right) || !(top || bottom)) {
            return;
        }
        if (left && top) {
            tl = &v[k];
        } else if (left) {
            bl = &v[k];
        } else if (top) {
            tr = &v[k];
        } else {
            br = &v[k];
        }
    }
    if (n != 4 || tl == NULL || bl == NULL || tr == NULL || br == NULL) {
        return;
    }
    PVertex q[4];
    if (gp.wd.clipX0 < mn) {
        q[0] = *tl, q[1] = *tl, q[2] = *bl, q[3] = *bl;
        q[0].x = q[2].x = gp.wd.clipX0;
        recordPolygon(q, 4, mode, tpage | clut << 16, NULL, base + 1);
        triangle(&q[1], &q[2], &q[3], mode, tpage, clut, 6);
        triangle(&q[0], &q[1], &q[2], mode, tpage, clut, 5);
    }
    if (gp.wd.clipX1 + 1 > mx) {
        q[0] = *tr, q[1] = *tr, q[2] = *br, q[3] = *br;
        q[1].x = q[3].x = gp.wd.clipX1 + 1;
        recordPolygon(q, 4, mode, tpage | clut << 16, NULL, base + 2);
        triangle(&q[1], &q[2], &q[3], mode, tpage, clut, 6);
        triangle(&q[0], &q[1], &q[2], mode, tpage, clut, 5);
    }
}

static void verticalPolygonBands(PVertex* v, int n, int textured, uint32_t mode, uint32_t tpage, uint32_t clut) {
    if (textured || n != 4 || (gp.wd.clipY0 == gp.wd.edgeY0 && gp.wd.clipY1 == gp.wd.edgeY1)) return;
    const PVertex *tl = &v[0], *tr = &v[1], *bl = &v[2], *br = &v[3];
    if (tl->x != bl->x || tr->x != br->x || tl->y != tr->y || bl->y != br->y ||
        tl->y > gp.wd.edgeY0 || bl->y < gp.wd.edgeY1 + 1) return;
    PVertex q[4];
    if (gp.wd.clipY0 < tl->y) {
        q[0] = q[2] = *tl; q[1] = q[3] = *tr;
        q[0].y = q[1].y = gp.wd.clipY0;
        for (int k = 0; k < 4; k++) q[k].precise = 0;
        widePolygonSides(q, 4, 0, mode, tpage, clut, 4);
        recordPolygon(q, 4, mode, tpage | clut << 16, NULL, 3);
        triangle(&q[1], &q[2], &q[3], mode, tpage, clut, 6);
        triangle(&q[0], &q[1], &q[2], mode, tpage, clut, 5);
    }
    if (gp.wd.clipY1 + 1 > bl->y) {
        q[0] = q[2] = *bl; q[1] = q[3] = *br;
        q[2].y = q[3].y = gp.wd.clipY1 + 1;
        for (int k = 0; k < 4; k++) q[k].precise = 0;
        widePolygonSides(q, 4, 0, mode, tpage, clut, 6);
        recordPolygon(q, 4, mode, tpage | clut << 16, NULL, 4);
        triangle(&q[1], &q[2], &q[3], mode, tpage, clut, 6);
        triangle(&q[0], &q[1], &q[2], mode, tpage, clut, 5);
    }
}

/* ---- field sprite poses (Enhanced) -------------------------------------------------
 *
 * The original builds a field character from part quads whose sheet cells
 * are shared between poses, so a redraw per cell cannot match every pose a
 * cell appears in (a torso cell's top rows hold the chin in one pose and a
 * collar in another). An actor's parts are grouped as the engine submits
 * them (pcSpritePosePart); the group's pose is named by its parts' texel
 * keys, cells and placement, the placement measured in the texel frame of
 * the largest part (vertex 0, and the edges to vertices 1 and 2 over the
 * cell's size), which cancels position, scale, mirroring and the camera;
 * tools/field_sprite_pose.py computes the same key from the authoring set.
 * A pose in the library is drawn once per pass, as one quad through the
 * same affine frame, at the first part the GPU meets; every part then
 * draws its exact native pixels and nothing in the copy or the recording.
 * Without a pose (an unknown frame, a part hidden, no subpixel positions)
 * the parts draw as before. */
static int poseVertex(const void* addr, PVertex* out) {
    uint32_t raw;
    memcpy(&raw, addr, sizeof(raw));
    decodeXY(out, raw);
    GtePrecise pr;
    /* Without the subpixel projection (the precision table holds two vblanks;
     * a slow host frame outlasts it) the integer position stands in. */
    out->fallback = 1; out->preciseX = (float)out->x; out->preciseY = (float)out->y;
    if (!hiresPrecision() || !gtePrecisionLookup(addr, raw, &pr)) return 0;
    if (out->x != (s16)raw + gp.offX + gp.wideShift || out->y != (s16)(raw >> 16) + gp.offY + gp.wideShiftY) return 0;
    out->fallback = 0;
    out->preciseX = preciseSnap(pr.x) + (float)(gp.offX + gp.wideShift);
    out->preciseY = preciseSnap(pr.y) + (float)(gp.offY + gp.wideShiftY);
    out->precise = 1;
    return 1;
}

/* A part's texel frame: vertex 0 and the per-texel steps along its cell. */
typedef struct { double ox, oy, ax, ay, bx, by; } PoseFrame;
/* A part's frame in sprite space (exact; no projection). */
static int poseSpaceFrame(const PosePart* p, PoseFrame* f) {
    if (!p->space) return 0;
    f->ox = p->sx[0]; f->oy = p->sy[0];
    f->ax = (double)(p->sx[1] - p->sx[0]) / p->cell.w; f->ay = (double)(p->sy[1] - p->sy[0]) / p->cell.w;
    f->bx = (double)(p->sx[2] - p->sx[0]) / p->cell.h; f->by = (double)(p->sy[2] - p->sy[0]) / p->cell.h;
    return fabs(f->ax * f->by - f->ay * f->bx) > 1e-9;
}
static int poseFrame(const PosePart* p, PoseFrame* f) {
    PVertex v[3];
    for (int k = 0; k < 3; k++) if (!poseVertex(p->xy[k], &v[k]) && !v[k].fallback) return 0;
    f->ox = v[0].preciseX; f->oy = v[0].preciseY;
    f->ax = (v[1].preciseX - v[0].preciseX) / p->cell.w; f->ay = (v[1].preciseY - v[0].preciseY) / p->cell.w;
    f->bx = (v[2].preciseX - v[0].preciseX) / p->cell.h; f->by = (v[2].preciseY - v[0].preciseY) / p->cell.h;
    return fabs(f->ax * f->by - f->ay * f->bx) > 1e-9;
}
/* A point in a frame's texel coordinates. */
static void poseLocal(const PoseFrame* f, double x, double y, double* a, double* b) {
    double det = f->ax * f->by - f->ay * f->bx, dx = x - f->ox, dy = y - f->oy;
    *a = (dx * f->by - dy * f->bx) / det;
    *b = (f->ax * dy - f->ay * dx) / det;
}

typedef struct { uint64_t key; int u, v, w, h, a, b, fx, fy; } PoseItem;
static int poseItemOrder(const void* x, const void* y) {
    const PoseItem *p = x, *q = y;
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    const int l[8] = {p->u, p->v, p->w, p->h, p->a, p->b, p->fx, p->fy}, r[8] = {q->u, q->v, q->w, q->h, q->a, q->b, q->fx, q->fy};
    for (int i = 0; i < 8; i++) if (l[i] != r[i]) return l[i] < r[i] ? -1 : 1;
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

/* The group's pose key (0: none can be named), its reference part in g->ref.
 * dump: also print every part's key items (XG_TEXTURE_LOG=2, for poses the
 * library lacks), with the unrounded a, b. */
static uint64_t spritePoseKey(PoseGroup* g, FILE* dump) {
    if (g->overflow || g->n < 1) return 0;
    PoseItem item[POSE_PARTS];
    int ref = 0;
    for (int i = 0; i < g->n; i++) {
        const TextureRequest* c = &g->part[i].cell;
        item[i] = (PoseItem){textureTexelKey(*c), c->u, c->v, c->w, c->h, 0, 0, 0, 0};
        const TextureRequest* r = &g->part[ref].cell;
        int area = c->w * c->h, best = r->w * r->h;
        if (area > best || (area == best && (item[i].key < item[ref].key ||
            (item[i].key == item[ref].key && (c->u < r->u || (c->u == r->u && c->v < r->v)))))) ref = i;
    }
    int space = 1;   /* sprite space when every part has it: exact, and independent of the projection */
    for (int i = 0; i < g->n; i++) space &= g->part[i].space;
    PoseFrame rf;
    if (space ? !poseSpaceFrame(&g->part[ref], &rf) : !poseFrame(&g->part[ref], &rf)) return 0;
    for (int i = 0; i < g->n; i++) {
        double px[3], py[3];
        for (int k = 0; k < 3; k++) {
            if (space) { px[k] = g->part[i].sx[k]; py[k] = g->part[i].sy[k]; continue; }
            PVertex v;
            if (!poseVertex(g->part[i].xy[k], &v)) return 0;
            px[k] = v.preciseX; py[k] = v.preciseY;
        }
        double a, b, a1, b1, a2, b2;
        poseLocal(&rf, px[0], py[0], &a, &b);
        poseLocal(&rf, px[1], py[1], &a1, &b1);
        poseLocal(&rf, px[2], py[2], &a2, &b2);
        item[i].a = (int)lround(a); item[i].b = (int)lround(b);
        item[i].fx = a1 - a < 0; item[i].fy = b2 - b < 0;
        if (dump) fprintf(dump, "sprite pose part: texel %016llx uv %d,%d %dx%d at %.3f,%.3f flip %d,%d%s\n",
                          (unsigned long long)item[i].key, item[i].u, item[i].v, item[i].w, item[i].h, a, b,
                          item[i].fx, item[i].fy, i == ref ? " (reference)" : "");
    }
    g->ref = ref;
    qsort(item, (size_t)g->n, sizeof(item[0]), poseItemOrder);
    uint64_t h = fnvBytes(UINT64_C(0xcbf29ce484222325), "xgpose1", 7);
    h = fnvLE(h, (uint64_t)g->n, 1);
    for (int i = 0; i < g->n; i++) {
        h = fnvLE(h, item[i].key, 8);
        h = fnvLE(h, (uint64_t)item[i].u, 2); h = fnvLE(h, (uint64_t)item[i].v, 2);
        h = fnvLE(h, (uint64_t)item[i].w, 2); h = fnvLE(h, (uint64_t)item[i].h, 2);
        h = fnvLE(h, (uint64_t)(uint16_t)item[i].a, 2); h = fnvLE(h, (uint64_t)(uint16_t)item[i].b, 2);
        h = fnvLE(h, (uint64_t)item[i].fx, 1); h = fnvLE(h, (uint64_t)item[i].fy, 1);
    }
    return h;
}

/* In-between drawings: when an actor's pose changes from A to B, the
 * library's A-to-B drawing (its key spritePoseTween(A, B); its corners in
 * B's reference frame) stands for B over the first half of B's hold, the
 * hold measured as A's (ordering tables drawn, widePoseSerial): a walk
 * cycle of n poses shows 2n drawings at the game's own timing. */
static uint64_t spritePoseTween(uint64_t a, uint64_t b) {
    uint64_t h = fnvBytes(UINT64_C(0xcbf29ce484222325), "xgtween1", 8);
    return fnvLE(fnvLE(h, a, 8), b, 8);
}
#define POSE_ACTORS 64
static struct { const void* actor; uint64_t cur, prev; unsigned changed, hold, seen; } poseActors[POSE_ACTORS];
static const SpritePose* spritePoseTimed(const void* actor, uint64_t key, const SpritePose* pose) {
    unsigned now = widePoseSerial(), slot = POSE_ACTORS, oldest = 0;
    for (unsigned i = 0; i < POSE_ACTORS; i++) {
        if (poseActors[i].actor == actor) { slot = i; break; }
        if (poseActors[i].seen < poseActors[oldest].seen) oldest = i;
    }
    if (slot == POSE_ACTORS) {
        slot = oldest;
        poseActors[slot] = (__typeof__(poseActors[0])){actor, key, 0, now, 0, now};
        return pose;
    }
    if (now - poseActors[slot].seen > 8) poseActors[slot].prev = 0; /* not drawn lately: no transition */
    poseActors[slot].seen = now;
    if (poseActors[slot].cur != key) {
        poseActors[slot].hold = now - poseActors[slot].changed;
        poseActors[slot].prev = poseActors[slot].cur;
        poseActors[slot].cur = key;
        poseActors[slot].changed = now;
    }
    unsigned half = poseActors[slot].hold / 2;
    if (poseActors[slot].prev && now - poseActors[slot].changed < (half ? half : 1)) {
        const SpritePose* tween = texturePoseGet(spritePoseTween(poseActors[slot].prev, key));
        static int log = -1;
        if (log < 0) { const char* e = getenv("XG_TEXTURE_LOG"); log = e && *e && strcmp(e, "0"); }
        if (log) fprintf(stderr, "sprite pose: in-between %016llx -> %016llx %s\n", (unsigned long long)poseActors[slot].prev,
                         (unsigned long long)key, tween ? "drawn" : "not in the library");
        if (tween) return tween;
    }
    return pose;
}

/* texture_pack.c; weak: the tests that include this file whole link no pack layer */
int textureReplacementsShown(void) __attribute__((weak));
/* At a part of a grouped sprite actor: draw its pose once per pass. 1 when
 * the pose stands for the part in the copy and the recording. */
static int spritePose(PoseGroup* g, uint32_t mode, const PVertex* colour) {
    if (!g) return 0;
    static int log = -1;
    if (log < 0) { const char* e = getenv("XG_TEXTURE_LOG"); log = e && *e ? atoi(e) ? atoi(e) : strcmp(e, "0") != 0 : 0; }
    if (!g->computed) {
        g->computed = 1;
        g->key = spritePoseKey(g, NULL);
        g->pose = g->key ? texturePoseGet(g->key) : NULL;
        if (g->pose) g->pose = spritePoseTimed(g->actor, g->key, g->pose);
        if (log) fprintf(stderr, "sprite pose: %d parts key %016llx %s\n", g->n, (unsigned long long)g->key,
                         g->pose ? "drawn" : g->key ? "not in the library" : "unnamed");
        if ((log > 1 && !g->pose && g->key) || log > 2) spritePoseKey(g, stderr);
    }
    const SpritePose* s = g->pose;
    if (!s) return 0;
    /* The A/B comparison: the pose found and timed as ever (spritePoseTimed's history goes on), its parts drawn. */
    if (textureReplacementsShown != NULL && !textureReplacementsShown()) return 0;
    int pass = gp.wide ? 1 : 0;
    if (g->drawn[pass]) return 1;
    g->drawn[pass] = 1;
    const PosePart* r = &g->part[g->ref];
    PoseFrame f;
    if (!poseFrame(r, &f)) return 1;
    double tl[2] = {f.ox + s->a[0] * f.ax + s->b[0] * f.bx, f.oy + s->a[0] * f.ay + s->b[0] * f.by};
    double tr[2] = {f.ox + s->a[1] * f.ax + s->b[1] * f.bx, f.oy + s->a[1] * f.ay + s->b[1] * f.by};
    double bl[2] = {f.ox + s->a[2] * f.ax + s->b[2] * f.bx, f.oy + s->a[2] * f.ay + s->b[2] * f.by};
    double br[2] = {tr[0] + bl[0] - tl[0], tr[1] + bl[1] - tl[1]};
    uint32_t tex = r->cell.tpage | (uint32_t)r->cell.clut << 16;
    uint32_t lo = uvWord(0, 0), hi = uvWord(s->w - 1, s->h - 1);
    mode &= ~(DRAW_CACHED | DRAW_FRINGE);
    HiVertex o[6];
    hiVertex(&o[0], (float)tl[0], (float)tl[1], 0, 0, colour->r, colour->g, colour->b, mode, tex, lo, hi);
    hiVertex(&o[1], (float)tr[0], (float)tr[1], (float)s->w, 0, colour->r, colour->g, colour->b, mode, tex, lo, hi);
    hiVertex(&o[2], (float)bl[0], (float)bl[1], 0, (float)s->h, colour->r, colour->g, colour->b, mode, tex, lo, hi);
    o[3] = o[1]; o[4] = o[2];
    hiVertex(&o[5], (float)br[0], (float)br[1], (float)s->w, (float)s->h, colour->r, colour->g, colour->b, mode, tex, lo, hi);
    TextureSample sample = {NULL, s->revision, 0, 0, s->w, s->h, s->scale, s->indices, 1};
    TextureSample keep = gp.sample;
    int sampled = gp.sampled, filter = gp.filter;
    gp.sample = sample; gp.sampled = 1; gp.filter = textureFilter(PC_TEX_SPRITE);
    hiEmit(o, 6);
    gp.sample = keep; gp.sampled = sampled; gp.filter = filter;
    if (interpolationRecording(gp.wide) && !interpolationCaptureSample()) {
        const DecodedTexture* d = textureCacheGet(r->cell);
        if (d && d->rgba) interpolationPoseDraw(o, 6, gp.wide, gp.wideShift, gp.wideShiftY, d, &sample);
    }
    return 1;
}

static void polygon(void) {
    gp.portraitSource = (TextureRequest){0};
    gp.glyphSource = (TextureRequest){0};
    uint32_t op = gp.cmd[0] >> 24;
    int gouraud = (op & 0x10) != 0;
    int quad = (op & 0x08) != 0;
    int textured = (op & 0x04) != 0;
    int semi = (op & 0x02) != 0;
    int raw = (op & 0x01) != 0;
    int n = quad ? 4 : 3;
    PVertex v[4];
    uint32_t raw_[4];
    const uint32_t* addr[4];
    uint32_t tpage = 0, clut = 0;
    int i = 0;
    for (int k = 0; k < n; k++) {
        decodeColor(&v[k], k == 0 || !gouraud ? gp.cmd[0] : gp.cmd[i]);
        if (k == 0) {
            i = 1;
        } else if (gouraud) {
            i++;
        }
        addr[k] = gp.cmdAddr[i];
        raw_[k] = gp.cmd[i++];
        v[k].u = v[k].v = 0;
        if (textured) {
            uint32_t uv = gp.cmd[i++];
            v[k].u = (int)(uv & 0xFF);
            v[k].v = (int)((uv >> 8) & 0xFF);
            if (k == 0) {
                clut = uv >> 16;
            } else if (k == 1) {
                tpage = uv >> 16;
            }
        }
    }
    uint32_t mode = 0;
    uint32_t semiMode;
    if (gp.wide && textured) {
        int u0 = v[0].u, u1 = v[0].u, v0 = v[0].v, v1 = v[0].v;
        for (int k = 1; k < n; k++) {
            u0 = v[k].u < u0 ? v[k].u : u0;
            u1 = v[k].u > u1 ? v[k].u : u1;
            v0 = v[k].v < v0 ? v[k].v : v0;
            v1 = v[k].v > v1 ? v[k].v : v1;
        }
        gp.wideTex = wideTexture(&gp.wd, tpage, u0, u1, v0, v1, gp.pkt);
        gp.wideUv.wideTex = gp.wideTex;
    }
    if (textured) {
        /* A textured polygon's texpage becomes the GPU's draw mode. */
        setDrawMode((gp.e1 & ~0x9FFu) | (tpage & 0x9FFu));
        if (quad) {
            int u0=v[0].u,u1=u0,v0=v[0].v,v1=v0;
            for (int k=1;k<n;++k) {
                if (v[k].u<u0) u0=v[k].u;
                if (v[k].u>u1) u1=v[k].u;
                if (v[k].v<v0) v0=v[k].v;
                if (v[k].v>v1) v1=v[k].v;
            }
            TextureRequest from={(uint16_t)tpage,(uint16_t)clut,gp.texWindow,u0,v0,u1-u0,v1-v0,0,0},to;
            if (texturePromptRemap(from,&to)) {
                for (int k=0;k<n;++k) {
                    v[k].u=to.u+(v[k].u-from.u)*to.w/from.w;
                    v[k].v=to.v+(v[k].v-from.v)*to.h/from.h;
                }
                tpage=to.tpage; clut=to.clut;
            }
        }
        cacheClut(tpage, clut);
        semiMode = (tpage >> 5) & 3;
        mode |= DRAW_TEXTURED | (raw ? DRAW_RAW : 0);
    } else {
        semiMode = (gp.e1 >> 5) & 3;
        if (gp.pkt && gp.pkt->geometry == PC_GEOMETRY_ONLY && gp.pkt->geometryAbr)
            semiMode = (uint32_t)(gp.pkt->geometryAbr - 1);
    }
    if (semi) {
        mode |= DRAW_SEMI | semiMode << DRAW_SEMI_SHIFT;
    }
    if (gp.pkt && gp.pkt->fade) {   /* PC: half and half over every texel (pcFadePrim) */
        mode = (mode & ~(3u << DRAW_SEMI_SHIFT)) | DRAW_SEMI | DRAW_FADE;
    }
    if (gouraud) {
        mode |= DRAW_GOURAUD;
    }
    /* psx-spx "24bit RGB to 15bit RGB Dithering": polygons dither only when
     * Gouraud-shaded or modulated. A raw-textured polygon does not, Gouraud
     * or not (Beetle PSX black box: gputest "raw Gouraud texture ignores
     * dither", the M-G6 random streams). */
    if ((gp.e1 & 0x200) && (gouraud || textured) && !(textured && raw)) {
        mode |= DRAW_DITHER;
    }
    mode |= (gp.setMask ? DRAW_SET_MASK : 0) | (gp.checkMask ? DRAW_CHECK_MASK : 0);
    /* The size limit applies before the drawing offset (edgeTooLong). */
    for (int k = 0; k < n; k++) {
        v[k].x = sext11(raw_[k]);
        v[k].y = sext11(raw_[k] >> 16);
    }
    int firstInvalid = edgeTooLong(&v[0], &v[1]) || edgeTooLong(&v[1], &v[2]) || edgeTooLong(&v[2], &v[0]);
    int secondInvalid = quad && (edgeTooLong(&v[1], &v[2]) || edgeTooLong(&v[1], &v[3]) ||
                                 edgeTooLong(&v[3], &v[2]));
    if (firstInvalid && (!quad || secondInvalid)) return;
    for (int k = 0; k < n; k++) {
        decodeXY(&v[k], raw_[k]);
        v[k].precise = 0;
    }
    if (gvkHiScale() != 0 && hiresPrecision()) {
        GtePrecise precise[4];
        int complete = 1;
        for (int k = 0; k < n; k++) {
            if (!gtePrecisionLookup(addr[k], raw_[k], &precise[k])) complete = 0;
        }
        if (complete) {
            for (int k = 0; k < n; k++) {
                /* A wrapped GP0 coordinate field cannot retain the original
                 * fractional position. The drawing-offset sum does not wrap. */
                if (v[k].x != (s16)raw_[k] + gp.offX + gp.wideShift ||
                    v[k].y != (s16)(raw_[k] >> 16) + gp.offY + gp.wideShiftY) complete = 0;
            }
        }
        if (complete) {
            for (int k = 0; k < n; k++) {
                v[k].precise = 1;
                v[k].preciseX = preciseSnap(precise[k].x) + (float)(gp.offX + gp.wideShift);
                v[k].preciseY = preciseSnap(precise[k].y) + (float)(gp.offY + gp.wideShiftY);
                v[k].depth = (float)precise[k].z;
            }
        }
    }
    polygonWrap(v, n);
    if (quad && textured && !gp.texWindow && gp.pkt && gp.pkt->classified &&
        gvkHiScale() && (textureUseGlyphs(gp.pkt->texClass) || textureCelScale(gp.pkt->texClass))) {
        uint32_t lo,hi; polygonUvBounds(v,n,gp.pkt->texClass,&lo,&hi);
        int u=(int16_t)lo, vv=(int16_t)(lo>>16);
        int w=(int16_t)hi-u, h=(int16_t)(hi>>16)-vv;
        /* Font quads put the excluded endpoint one texel past their atlas
         * crop. A flipped axis excludes its low end instead. Authenticate
         * the resulting canvas against registered native glyph metadata. */
        u+=portraitTrimLow(v,0); vv+=portraitTrimLow(v,1);
        TextureRequest canvas={(uint16_t)tpage,(uint16_t)clut,0,u,vv,w,h,0,0};
        if ((textureUseGlyphs(gp.pkt->texClass) && textureGlyphCanvas(canvas)) ||
            textureCelCanvas(canvas,textureCelScale(gp.pkt->texClass))) gp.glyphSource=canvas;
    }
    if (quad && textured && gp.pkt && gp.pkt->classified && PC_TEX_IS_PORTRAIT(gp.pkt->texClass)) {
        int kind = gp.pkt->texClass;
        int extent = PC_TEX_PORTRAIT_SIZE(kind);
        uint32_t lo, hi; polygonUvBounds(v, n, kind, &lo, &hi);
        int u = (int16_t)lo, vv = (int16_t)(lo >> 16);
        if (!gp.texWindow && (int16_t)hi-u == extent-1 && (int16_t)(hi >> 16)-vv == extent-1)
            gp.portraitSource = (TextureRequest){(uint16_t)tpage,(uint16_t)clut,0,u,vv,extent,extent,0,0};
    }
    if (gp.wide && wideExtends(textured)) {
        verticalPolygonBands(v, n, textured, mode, tpage, clut);
        widePolygonSides(v, n, textured, mode, tpage, clut, 0);
    }
    gp.poseSkip = 0;
    if (quad && textured && gp.pkt && gp.pkt->poseGroup && gp.pkt->classified && gp.pkt->texClass == PC_TEX_SPRITE &&
        gvkHiScale() && textureUseSpriteCel(PC_TEX_SPRITE) && textureSpriteCelEnabled())
        gp.poseSkip = spritePose(widePoseGroup(gp.pkt->poseGroup), mode, &v[0]);
    /* Flat pixel art (every class but 3D and the portraits, which keep their
     * own canvas clamp): the hi-res copy and the smooth-motion recording
     * clamp to the texels the native walk samples over the whole polygon. */
    gp.flatUvValid = 0;
    if (textured && gvkHiScale() && gp.pkt && gp.pkt->classified && gp.pkt->texClass != PC_TEX_3D &&
        !PC_TEX_IS_PORTRAIT(gp.pkt->texClass)) {
        if (!gp.wide && gp.replayed && gp.wideUv.flatSaved) {
            gp.flatUvValid = gp.wideUv.flatValid; gp.flatUvMin = gp.wideUv.flatMin; gp.flatUvMax = gp.wideUv.flatMax;
        } else {
            gp.flatUvValid = flatSpanTexels(v, !firstInvalid, quad && !secondInvalid, 1, &gp.flatUvMin, &gp.flatUvMax);
            if (gp.wide) {
                gp.wideUv.flatSaved = 1; gp.wideUv.flatValid = gp.flatUvValid;
                gp.wideUv.flatMin = gp.flatUvMin; gp.wideUv.flatMax = gp.flatUvMax;
            }
        }
    }
    if (!firstInvalid && !secondInvalid) {
        recordPolygon(v, n, mode, tpage | clut << 16, (const void* const*)addr, 0);
    } else {
        /* Retail skips only the oversized half of a quad. Capture its
         * other triangle too; a topology change holds this face locally. */
        int first = firstInvalid ? 1 : 0;
        PVertex valid[3] = {v[first], v[first + 1], v[first + 2]};
        const void* validAddr[3] = {addr[first], addr[first + 1], addr[first + 2]};
        recordPolygon(valid, 3, mode, tpage | clut << 16, validAddr, 0);
    }
    /* psx-spx "GPU Render Polygon Commands / Notes": a quad is the triangle
     * (v0, v1, v2), then (v1, v2, v3), each in packet order (which matters
     * for buildSpans' reference vertex). */
    if (!firstInvalid) triangle(&v[0], &v[1], &v[2], mode, tpage, clut, quad ? 5 : 7);
    if (quad && !secondInvalid) {
        triangle(&v[1], &v[2], &v[3], mode, tpage, clut, 6);
    }
    gp.flatUvValid = 0;
    gp.poseSkip = 0;
    gp.portraitSource = (TextureRequest){0};
    gp.glyphSource = (TextureRequest){0};
}

/* ---- rectangles ----------------------------------------------------------------- */

/* The texels a rectangle row or column reads, for read tracking, the wide
 * pass and the hi-res copy: coordinates wrap at 8 bits in either direction
 * (psx-spx "Texture Origin and X/Y-Flip", "GP0(E2h)"), so a range that
 * wraps covers 0..255. */
static void rectangleUvBounds(int start, int size, int step, int* lo, int* hi) {
    int end = start + (size - 1) * step;
    *lo = end < start ? end : start;
    *hi = end > start ? end : start;
    if (*lo < 0 || *hi > 255) { *lo = 0; *hi = 255; }
}

/* An axis-aligned rectangle (psx-spx "GPU Render Rectangle Commands"): its
 * corners sit on pixel edges, so the samples at pixel centres cover exactly
 * w x h pixels, and u, v start half a texel low so they round to u0 + dx.
 * Rectangles are never dithered (psx-spx dithering note); a pixel that
 * starts clipped still advances the texel from the unclipped origin. */
static void rectangle(int x, int y, int w, int h, int u0, int v0, const PVertex* color, uint32_t mode,
                      uint32_t tpage, uint32_t clut, unsigned piece) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if ((mode & DRAW_TEXTURED) && !(gp.e1 & 0x3000)) {
        TextureRequest from={(uint16_t)tpage,(uint16_t)clut,gp.texWindow,u0,v0,w,h,0,0},to;
        if (texturePromptRemap(from,&to)) {
            PVertex q[4]={*color,*color,*color,*color};
            for (int k=0;k<4;++k) {
                q[k].x=x+(k&1)*w; q[k].y=y+(k>>1)*h;
                q[k].u=to.u+(k&1)*to.w; q[k].v=to.v+(k>>1)*to.h;
                q[k].precise=0;
            }
            cacheClut(to.tpage,to.clut);
            recordPolygon(q, 4, mode, to.tpage | (uint32_t)to.clut << 16, NULL, piece);
            triangle(&q[0],&q[1],&q[2],mode,to.tpage,to.clut,5);
            triangle(&q[1],&q[2],&q[3],mode,to.tpage,to.clut,6);
            return;
        }
    }
    if (gp.wide && !(mode & (DRAW_FILL | DRAW_TEXTURED)) && wideExtends(0) &&
        y <= gp.wd.edgeY0 && y + h - 1 >= gp.wd.edgeY1) {
        int y1 = y + h;
        if (gp.wd.clipY0 < y) { h += y - gp.wd.clipY0; y = gp.wd.clipY0; }
        if (gp.wd.clipY1 + 1 > y1) h += gp.wd.clipY1 + 1 - y1;
    }
    if (gp.wide && !(mode & DRAW_FILL) && wideExtends((mode & DRAW_TEXTURED) != 0) && x <= gp.wd.edgeX0 &&
        x + w - 1 >= gp.wd.edgeX1) {
        if (mode & DRAW_TEXTURED) {
            /* PC_WIDE_EXTEND: stretched. */
            x = gp.wd.clipX0;
            w = gp.wd.clipX1 - gp.wd.clipX0 + 1;
        } else {
            /* A piece on each side (see widePolygonSides). */
            int x1 = x + w, cx0 = gp.wd.clipX0, cx1 = gp.wd.clipX1 + 1;
            if (cx0 < x) {
                rectangle(cx0, y, x - cx0, h, u0, v0, color, mode, tpage, clut, 1);
            }
            if (x1 < cx1) {
                rectangle(x1, y, cx1 - x1, h, u0, v0, color, mode, tpage, clut, 2);
            }
        }
    }
    int du = (mode & DRAW_TEXTURED) && (gp.e1 & 0x1000) ? -1 : 1;
    int dv = (mode & DRAW_TEXTURED) && (gp.e1 & 0x2000) ? -1 : 1;
    /* psx-spx "Texture Origin and X/Y-Flip": E1 bits 12/13 make U/V step
     * down instead of up, for rectangles only. With an X flip the first texel
     * is the odd one of the pair U addresses, an even U behaving as U + 1:
     * measured with Beetle PSX as a black box (M-G8 flips, the M-G6 random
     * streams, gputest "sprite texture direction"). psx-spx only warns that
     * odd U and widths sample oddly on hardware. */
    if (du < 0) u0 |= 1;
    int ulo, uhi, vlo, vhi;
    rectangleUvBounds(u0, w, du, &ulo, &uhi);
    rectangleUvBounds(v0, h, dv, &vlo, &vhi);
    recordQuad((float)x, (float)y, (float)(x + w), (float)(y + h), (float)u0, (float)v0,
               (float)w * du, (float)h * dv, color, mode, tpage | clut << 16, uvWord(ulo, vlo), uvWord(uhi, vhi), piece);
    ReadSet reads = {0};
    if (mode & DRAW_TEXTURED) textureReads(&reads, tpage, clut, ulo, uhi, vlo, vhi);
    VRect wr = {x, y, x + w - 1, y + h - 1};
    if ((mode & DRAW_TEXTURED) && !gp.wide && textureDumpEnabled()) {
        VRect sample = wr;
        if (sample.x0 < gp.clipX0) sample.x0 = gp.clipX0;
        if (sample.y0 < gp.clipY0) sample.y0 = gp.clipY0;
        if (sample.x1 > gp.clipX1) sample.x1 = gp.clipX1;
        if (sample.y1 > gp.clipY1) sample.y1 = gp.clipY1;
        if (clipToVram(&sample))
            observeTexture(tpage | clut << 16,
                uvWord(u0 + du * (sample.x0 - x), v0 + dv * (sample.y0 - y)),
                uvWord(u0 + du * (sample.x1 - x), v0 + dv * (sample.y1 - y)));
    }
    int cached = 0, cachedInts = 0;
    VRect cacheBox = wr;
    if ((mode & DRAW_TEXTURED) && !gp.wide) {
        if (cacheBox.x0 < gp.clipX0) cacheBox.x0 = gp.clipX0;
        if (cacheBox.y0 < gp.clipY0) cacheBox.y0 = gp.clipY0;
        if (cacheBox.x1 > gp.clipX1) cacheBox.x1 = gp.clipX1;
        if (cacheBox.y1 > gp.clipY1) cacheBox.y1 = gp.clipY1;
        if (clipToVram(&cacheBox)) {
            SelfDraw s = {cacheBox, NULL, 0, x, y, u0, v0, tpage, mode, tpage | clut << 16, color,
                          NULL, uvWord(ulo, vlo), uvWord(uhi, vhi), du, dv};
            CPU_TIME_BEGIN(CPU_CACHE);
            cached = cacheRaster(&s, &reads, &cachedInts);
            CPU_TIME_END(CPU_CACHE);
            if (cached == 2 || cached == 3) {
                if (gvkHiScale() && (cached == 3 || plainHiReplay())) {
                    uint32_t tex=tpage | clut<<16, lo=uvWord(ulo,vlo), hi=uvWord(uhi,vhi);
                    prepareTexture(mode,tex,lo,hi,wr,&reads);
                    hiQuad(x,y,x+w,y+h,u0,v0,w*du,h*dv,color,mode,tex,lo,hi);
                }
                return;
            }
            if (cached) mode |= DRAW_CACHED;
        }
    }
    prepareTexture(mode,tpage | clut<<16,uvWord(ulo,vlo),uvWord(uhi,vhi),wr,&reads);
    probe(mode & DRAW_FILL ? "fill" : "rectangle", x, y, x + w - 1, y + h - 1);
    VRect ownedBox = wr;
    if (!gp.wide && !(mode & (DRAW_TEXTURED | DRAW_FILL)) &&
        (interpolationWaveFrame() || (clipToVram(&ownedBox) && vramCpuOwned(ownedBox)))) {
        VRect box=wr;
        if (box.x0<gp.clipX0) box.x0=gp.clipX0;
        if (box.y0<gp.clipY0) box.y0=gp.clipY0;
        if (box.x1>gp.clipX1) box.x1=gp.clipX1;
        if (box.y1>gp.clipY1) box.y1=gp.clipY1;
        if (!clipToVram(&box)) return;
        vramKeepWrite(box); /* a picture between's keep (vram.c): shaded on the CPU, into its copy */
        vramSyncRect(box);
        if (!batchAdd(wr,&reads)) return;
        SelfDraw draw={.box=box,.mode=mode,.color=color};
        for(int yy=box.y0;yy<=box.y1;yy++)
            for(int xx=box.x0;xx<=box.x1;xx++) cacheShade(&draw,NULL,xx,yy,0);
        vramDeferCpuDraw(box);
        vramResolveCpuDraw(box);
        if(gvkHiScale()) hiQuad(x,y,x+w,y+h,u0,v0,w*du,h*dv,color,mode,tpage|clut<<16,0,0);
        return;
    }
    if ((mode & DRAW_FILL) && gp.wide) {
        /* gp0WideFill: a rectangle of the wide target, already clipped. */
        VRect t = wr;
        if (t.y1 > VRAM_H - 1) {
            t.y1 = VRAM_H - 1;
        }
        gvkWideTouch(t.x1);
        t = wideTiles(t);
        if (tilesHit(&gp.batchWideRead, t) || (!gvkOrderedWrites() && tilesHit(&gp.batchWideWritten, t))) {
            batchBreak();
        }
        tilesAdd(&gp.batchWideWritten, t);
        gpuDrawIssued();
    } else if (mode & DRAW_FILL) {
        if (!clipToVram(&wr)) {
            return;
        }
        int conflict = nativeHit(&gp.batchRead, &gp.batchReadExact, wr) || (!gvkOrderedWrites() && nativeHit(&gp.batchWritten, &gp.batchWrittenExact, wr));
        if (conflict) {
            batchBreak();
        }
        gp0VramChanging(wr);
        nativeAdd(&gp.batchWritten, &gp.batchWrittenExact, wr);
        gpuDrawIssued();
    } else if (!batchAdd(wr, &reads)) {
        return;
    } else if ((mode & DRAW_TEXTURED) && gp.wide) {
        /* The pixels drawn: in the draw area and the target. */
        int tw = gp.wide ? WIDE_W : VRAM_W, th = gp.wide ? WIDE_H : VRAM_H;
        VRect box = {x > gp.clipX0 ? x : gp.clipX0, y > gp.clipY0 ? y : gp.clipY0,
                     x + w - 1 < gp.clipX1 ? x + w - 1 : gp.clipX1, y + h - 1 < gp.clipY1 ? y + h - 1 : gp.clipY1};
        box.x0 = box.x0 < 0 ? 0 : box.x0;
        box.y0 = box.y0 < 0 ? 0 : box.y0;
        box.x1 = box.x1 > tw - 1 ? tw - 1 : box.x1;
        box.y1 = box.y1 > th - 1 ? th - 1 : box.y1;
        SelfDraw s = {box, NULL, 0, x, y, u0, v0, tpage, mode, tpage | clut << 16, color, NULL, uvWord(ulo, vlo),
                      uvWord(uhi, vhi), du, dv};
        if (box.x0 <= box.x1 && box.y0 <= box.y1 && selfDraw(&s, &reads)) {
            return;
        }
    }
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float s0 = (float)u0 - 0.5f * du, t0 = (float)v0 - 0.5f * dv;
    float s1 = s0 + (float)w * du, t1 = t0 + (float)h * dv;
    uint32_t tex = tpage | clut << 16;
    GpuVertex out[6];
    setVertex(&out[0], x0, y0, s0, t0, color, mode, tex);
    setVertex(&out[1], x1, y0, s1, t0, color, mode, tex);
    setVertex(&out[2], x0, y1, s0, t1, color, mode, tex);
    setVertex(&out[3], x1, y0, s1, t0, color, mode, tex);
    setVertex(&out[4], x0, y1, s0, t1, color, mode, tex);
    setVertex(&out[5], x1, y1, s1, t1, color, mode, tex);
    if (gp.wide) {
        gvkDrawWide(out, 6);
    } else if (cached) {
        gvkDrawSpans(out, 6, spanBuf, cachedInts);
    } else {
        gvkDraw(out, 6);
    }
    if (cached) cacheHiResolve(cacheBox);
    else if (gvkHiScale() != 0) {
        /* Its texels one per 1x pixel from (u0, v0) at its corner. */
        uint32_t lo = 0, hi = 0;
        if (mode & DRAW_TEXTURED) {
            lo = uvWord(ulo, vlo);
            hi = uvWord(uhi, vhi);
        }
        hiQuad(x0, y0, x1, y1, (float)u0, (float)v0,
               (float)w * du, (float)h * dv, color, mode, tex, lo, hi);
    }
}

static void rectCommand(void) {
    uint32_t op = gp.cmd[0] >> 24;
    int textured = (op & 0x04) != 0;
    int semi = (op & 0x02) != 0;
    int raw = (op & 0x01) != 0;
    PVertex c;
    decodeColor(&c, gp.cmd[0]);
    PVertex p;
    decodeXY(&p, gp.cmd[1]);
    /* Rectangle origins wrap to 11 bits after the offset is added, unlike
     * polygon vertices (GPU-I; Beetle PSX black box, M-G5 offsets, gputest
     * testOffsetOverflow). */
    p.x = sext11((uint32_t)(p.x - gp.wideShift)) + gp.wideShift;
    p.y = sext11((uint32_t)(p.y - gp.wideShiftY)) + gp.wideShiftY;
    int i = 2;
    int u = 0, v = 0;
    uint32_t clut = 0;
    if (textured) {
        uint32_t uv = gp.cmd[i++];
        u = (int)(uv & 0xFF);
        v = (int)((uv >> 8) & 0xFF);
        clut = uv >> 16;
    }
    int w, h;
    switch ((op >> 3) & 3) {
    case 0:
        w = (int)(gp.cmd[i] & 0x3FF);
        h = (int)((gp.cmd[i] >> 16) & 0x1FF);
        break;
    case 1:
        w = h = 1;
        break;
    case 2:
        w = h = 8;
        break;
    default:
        w = h = 16;
        break;
    }
    uint32_t tpage = gp.e1 & 0x9FF;
    if (textured) cacheClut(tpage, clut);
    uint32_t mode = textured ? DRAW_TEXTURED | (raw ? DRAW_RAW : 0) : 0;
    if (gp.wide && textured) {
        int ulo, uhi, vlo, vhi;
        rectangleUvBounds((gp.e1 & 0x1000) ? u | 1 : u, w, (gp.e1 & 0x1000) ? -1 : 1, &ulo, &uhi);
        rectangleUvBounds(v, h, (gp.e1 & 0x2000) ? -1 : 1, &vlo, &vhi);
        gp.wideTex = wideTexture(&gp.wd, tpage, ulo, uhi, vlo, vhi, gp.pkt);
        gp.wideUv.wideTex = gp.wideTex;
    }
    if (semi) {
        mode |= DRAW_SEMI | ((gp.e1 >> 5) & 3) << DRAW_SEMI_SHIFT;
    }
    mode |= (gp.setMask ? DRAW_SET_MASK : 0) | (gp.checkMask ? DRAW_CHECK_MASK : 0);
    rectangle(p.x, p.y, w, h, u, v, &c, mode, tpage, clut, 0);
}

/* ---- lines ------------------------------------------------------------------------ */

typedef struct {
    int x0, y0, x1, y1;
    int gouraud;
    uint32_t c0, c1;
    uint32_t mode;
    GpuVertex buf[6 * 64];
    int count;
} LineDraw;

static void lineFlush(LineDraw* l) {
    if (l->count > 0) {
        if (gp.wide) {
            gvkDrawWide(l->buf, l->count);
        } else {
            gvkDraw(l->buf, l->count);
        }
        l->count = 0;
    }
}

/* Pixel (x, y) in colour `p`; all drawing-area boundaries are inclusive
 * (psx-spx "GP0(E3h)/(E4h)"). */
static void linePixel(LineDraw* l, int x, int y, const PVertex* p) {
    if (x < gp.clipX0 || y < gp.clipY0 || x > gp.clipX1 || y > gp.clipY1) return;
    GpuVertex* o = &l->buf[l->count];
    float fx0 = (float)x, fy0 = (float)y, fx1 = fx0 + 1.0f, fy1 = fy0 + 1.0f;
    setVertex(&o[0], fx0, fy0, 0, 0, p, l->mode, 0);
    setVertex(&o[1], fx1, fy0, 0, 0, p, l->mode, 0);
    setVertex(&o[2], fx0, fy1, 0, 0, p, l->mode, 0);
    setVertex(&o[3], fx1, fy0, 0, 0, p, l->mode, 0);
    setVertex(&o[4], fx0, fy1, 0, 0, p, l->mode, 0);
    setVertex(&o[5], fx1, fy1, 0, 0, p, l->mode, 0);
    l->count += 6;
    recordQuad(fx0, fy0, fx1, fy1, 0, 0, 0, 0, p, l->mode, 0, 0, 0, 0);
    if (gvkHiScale() != 0) {
        hiQuad(fx0, fy0, fx1, fy1, 0, 0, 0, 0, p, l->mode, 0, 0, 0); /* an N x N block */
    }
    if (l->count == 6 * 64) {
        lineFlush(l);
    }
}

static void line(const PVertex* a, const PVertex* b, int gouraud, uint32_t mode) {
    /* Too long (before the offset): not drawn. */
    int rdx = a->x - b->x, rdy = a->y - b->y;
    if (rdx >= 1024 || rdx <= -1024 || rdy >= 512 || rdy <= -512) {
        return;
    }
    static LineDraw l;
    l.x0 = a->x + gp.offX + gp.wideShift;
    l.y0 = a->y + gp.offY + gp.wideShiftY;
    l.x1 = b->x + gp.offX + gp.wideShift;
    l.y1 = b->y + gp.offY + gp.wideShiftY;
    l.gouraud = gouraud;
    l.c0 = (uint32_t)a->r | (uint32_t)a->g << 8 | (uint32_t)a->b << 16;
    l.c1 = (uint32_t)b->r | (uint32_t)b->g << 8 | (uint32_t)b->b << 16;
    l.mode = mode;
    l.count = 0;
    if (gp.clipY0 > gp.clipY1 || gp.clipX0 > gp.clipX1) return;
    VRect box = {l.x0 < l.x1 ? l.x0 : l.x1, l.y0 < l.y1 ? l.y0 : l.y1,
                 l.x0 > l.x1 ? l.x0 : l.x1, l.y0 > l.y1 ? l.y0 : l.y1};
    if (!gp.wide) {
        /* Offset sums wrap to eleven bits at pixel output. A crossing can
         * occupy either end of that range, so conservatively cover the clip. */
        if ((box.x0 >> 11) != (box.x1 >> 11)) { box.x0 = gp.clipX0; box.x1 = gp.clipX1; }
        else { box.x0 &= 2047; box.x1 &= 2047; }
        if ((box.y0 >> 11) != (box.y1 >> 11)) { box.y0 = gp.clipY0; box.y1 = gp.clipY1; }
        else { box.y0 &= 2047; box.y1 &= 2047; }
    }
    probe("line", box.x0, box.y0, box.x1, box.y1);
    if (!batchAdd(box, NULL)) return;
    /* Both endpoints are drawn (psx-spx "GPU Render Line Commands / Note"),
     * equal endpoints as one pixel in the first vertex's colour (psx-spx).
     * The rest was measured with Beetle PSX as a black box
     * (work/clean-room/measure/gpu, M-G7: 1700 lines in every octant, both
     * endpoint orders, up to the size limit, every pixel and colour): a line
     * goes from its left endpoint (a vertical one from its first vertex),
     * one pixel per step along its major axis, n = max(|dx|, |dy|) steps.
     * The minor coordinate is the ideal line's rounded to the nearest pixel;
     * an exact half goes away from the left endpoint when x is the major
     * axis (|dx| >= |dy|) and to the smaller x when y is. So A to B and B to
     * A draw the same pixels. Gouraud colours start at the left endpoint's
     * and step by (end - start) / n in 12 fractional bits, truncated toward
     * zero, plus one half, truncated at each pixel (the polygons' precision,
     * buildSpans). The output coordinates wrap to 11 bits (GPU-I). */
    PVertex ca = *a, cb = *b;
    int x0 = l.x0, y0 = l.y0, x1 = l.x1, y1 = l.y1;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
        PVertex c = ca;
        ca = cb;
        cb = c;
    }
    int dx = x1 - x0, dy = y1 - y0, ady = dy < 0 ? -dy : dy;
    int n = dx > ady ? dx : ady;
    PVertex color = *a;
    if (n == 0) {
        linePixel(&l, gp.wide ? x0 : sext11((uint32_t)x0), gp.wide ? y0 : sext11((uint32_t)y0), &color);
        lineFlush(&l);
        return;
    }
    int64_t start[3] = {ca.r, ca.g, ca.b}, step[3] = {0, 0, 0};
    if (gouraud) {
        int64_t end[3] = {cb.r, cb.g, cb.b};
        for (int k = 0; k < 3; k++) {
            step[k] = (end[k] - start[k]) * 4096 / n; /* truncates toward zero */
        }
    }
    for (int k = 0; k <= n; k++) {
        int x, y;
        if (dx >= ady) {
            int m = (int)(((int64_t)2 * ady * k + n) / (2 * n));
            x = x0 + k;
            y = y0 + (dy >= 0 ? m : -m);
        } else {
            x = x0 + (int)(((int64_t)2 * dx * k + n - 1) / (2 * n));
            y = y0 + (dy > 0 ? k : -k);
        }
        if (gouraud) {
            color.r = (int)(((start[0] << 12) + 2048 + k * step[0]) >> 12);
            color.g = (int)(((start[1] << 12) + 2048 + k * step[1]) >> 12);
            color.b = (int)(((start[2] << 12) + 2048 + k * step[2]) >> 12);
        }
        linePixel(&l, gp.wide ? x : sext11((uint32_t)x), gp.wide ? y : sext11((uint32_t)y), &color);
    }
    lineFlush(&l);
}

static uint32_t lineMode(uint32_t op) {
    uint32_t mode = (gp.e1 & 0x200) ? DRAW_DITHER : 0;
    if (op & 0x02) {
        mode |= DRAW_SEMI | ((gp.e1 >> 5) & 3) << DRAW_SEMI_SHIFT;
    }
    return mode | (gp.setMask ? DRAW_SET_MASK : 0) | (gp.checkMask ? DRAW_CHECK_MASK : 0);
}

/* A line end point before the drawing offset (line() applies it). */
static void decodeLineXY(PVertex* v, uint32_t word) {
    v->x = sext11(word);
    v->y = sext11(word >> 16);
}

static void lineCommand(void) {
    uint32_t op = gp.cmd[0] >> 24;
    int gouraud = (op & 0x10) != 0;
    PVertex a = {0}, b = {0}; /* lines use x, y and colour: the rest zero (a polyline's is kept, save states) */
    decodeColor(&a, gp.cmd[0]);
    decodeLineXY(&a, gp.cmd[1]);
    if (op & 0x08) {
        /* Polyline: the rest arrives word by word until the terminator. */
        gp.polyline = 1;
        gp.polyGouraud = gouraud;
        gp.polyWantColor = gouraud;
        gp.polyColor = gp.cmd[0];
        gp.polyCmd = op;
        gp.polyPrev = a;
        return;
    }
    if (gouraud) {
        decodeColor(&b, gp.cmd[2]);
        decodeLineXY(&b, gp.cmd[3]);
    } else {
        b = a;
        decodeLineXY(&b, gp.cmd[2]);
    }
    line(&a, &b, gouraud, lineMode(op));
}

static void polylineWord(uint32_t word) {
    if ((word & 0xF000F000u) == 0x50005000u) {
        gp.polyline = 0;
        return;
    }
    if (gp.polyWantColor) {
        gp.polyColor = word;
        gp.polyWantColor = 0;
        return;
    }
    PVertex v = {0};
    decodeColor(&v, gp.polyColor);
    decodeLineXY(&v, word);
    gp.replayed = 0;
    if (wideBegin()) {
        line(&gp.polyPrev, &v, gp.polyGouraud, lineMode(gp.polyCmd));
        wideEnd();
        gp.replayed = 1;
    }
    if (!(gp.pkt != NULL && (gp.pkt->flags & PC_WIDE_ONLY))) {
        line(&gp.polyPrev, &v, gp.polyGouraud, lineMode(gp.polyCmd));
    }
    gp.polyPrev = v;
    gp.polyWantColor = gp.polyGouraud;
}

/* ---- transfers ------------------------------------------------------------------------- */

static void fillCommand(void) {
    int x = (int)(gp.cmd[1] & 0x3F0);
    int y = (int)((gp.cmd[1] >> 16) & 0x1FF);
    int w = (int)(((gp.cmd[2] & 0x3FF) + 0xF) & ~0xFu);
    int h = (int)((gp.cmd[2] >> 16) & 0x1FF);
    gpuFill(x, y, w, h, gp.cmd[0] & 0xFFFFFF);
}

static void copyCommand(void) {
    int sx = (int)(gp.cmd[1] & 0x3FF);
    int sy = (int)((gp.cmd[1] >> 16) & 0x1FF);
    int dx = (int)(gp.cmd[2] & 0x3FF);
    int dy = (int)((gp.cmd[2] >> 16) & 0x1FF);
    int w = (int)(((gp.cmd[3] & 0xFFFF) - 1) & 0x3FF) + 1;
    int h = (int)(((gp.cmd[3] >> 16) - 1) & 0x1FF) + 1;
    gpuMove(sx, sy, dx, dy, w, h);
}

static void loadStart(void) {
    gp.loadX = (int)(gp.cmd[1] & 0x3FF);
    gp.loadY = (int)((gp.cmd[1] >> 16) & 0x1FF);
    gp.loadW = (int)(((gp.cmd[2] & 0xFFFF) - 1) & 0x3FF) + 1;
    gp.loadH = (int)(((gp.cmd[2] >> 16) - 1) & 0x1FF) + 1;
    gp.loadTotal = gp.loadW * gp.loadH;
    gp.loadPos = 0;
    gp.loading = 1;
}

static void loadWord(uint32_t word) {
    gp.loadBuf[gp.loadPos++] = (uint16_t)word;
    gp.loadBuf[gp.loadPos++] = (uint16_t)(word >> 16);
    if (gp.loadPos >= gp.loadTotal) {
        gp.loading = 0;
        gpuLoad(gp.loadX, gp.loadY, gp.loadW, gp.loadH, gp.loadBuf);
    }
}

/* libgpu.c's transfers end the batch: what they copy or overwrite must see
 * everything drawn before. */
void gp0Flush(void) {
    batchBreak();
}

/* A fill (GP0 02h, ClearImage): no draw area, offset, mask or blending. */
void gp0Fill(VRect r, uint32_t rgb) {
    interpolationClear(r, rgb);
    PVertex c;
    decodeColor(&c, rgb);
    rectangle(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, 0, 0, &c, DRAW_FILL, 0, 0, 0);
    wideFill(r, rgb);
}

/* Retail LIBGPU _clr (src/main/psyq/libgpu/sys.c) sends a fill (GP0(02h))
 * when x and width are both multiples of 64, else a variable tile drawn
 * with drawing area 0..max, offset 0, mask bits 0 and E1's low bits kept,
 * then restores the drawing area and offset but not the mask settings. The
 * difference shows at VRAM edges: a fill wraps, a tile clips. */
void gp0ClearImage(int x, int y, int w, int h, uint32_t rgb) {
    interpolationClear((VRect){x, y, x + w - 1, y + h - 1}, rgb);
    gp0SetMaskBits(0);
    setDrawMode(gp.e1 & 0x7FF);
    if (!((x | w) & 63)) {
        gpuFill(x, y, w, h, rgb);
        return;
    }
    int x0 = gp.clipX0, y0 = gp.clipY0, x1 = gp.clipX1, y1 = gp.clipY1;
    int ox = gp.offX, oy = gp.offY;
    gp.clipX0 = gp.clipY0 = gp.offX = gp.offY = 0;
    gp.clipX1 = VRAM_W - 1; gp.clipY1 = VRAM_H - 1;
    PVertex color;
    decodeColor(&color, rgb);
    rectangle(sext11(x), sext11(y), w, h, 0, 0, &color, 0, 0, 0, 0);
    wideFill((VRect){x, y, x + w - 1, y + h - 1}, rgb);
    gp.clipX0 = x0; gp.clipY0 = y0; gp.clipX1 = x1; gp.clipY1 = y1;
    gp.offX = ox; gp.offY = oy;
}

void gp0WideFill(VRect r, uint32_t rgb) {
    if (r.x0 < 0) {
        r.x0 = 0;
    }
    if (r.x1 > WIDE_FRAME_W - 1) {
        r.x1 = WIDE_FRAME_W - 1;
    }
    if (r.x0 > r.x1 || r.y0 > r.y1) {
        return;
    }
    PVertex c;
    decodeColor(&c, rgb);
    int was = gp.wide;
    gp.wide = 1;
    gp.wideTex = 0;
    rectangle(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, 0, 0, &c, DRAW_FILL, 0, 0, 0);
    gp.wide = was;
}

/* ---- the command stream ------------------------------------------------------------------- */

static int commandLength(uint32_t op) {
    if (op >= 0x20 && op < 0x40) {
        int n = (op & 0x08) ? 4 : 3;
        int perVertex = 1 + ((op & 0x04) ? 1 : 0);
        return 1 + n * perVertex + ((op & 0x10) ? n - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) {
        if (op & 0x08) {
            return 2; /* polyline: the first vertex, then word by word */
        }
        return (op & 0x10) ? 4 : 3;
    }
    if (op >= 0x60 && op < 0x80) {
        return 2 + ((op & 0x04) ? 1 : 0) + (((op >> 3) & 3) == 0 ? 1 : 0);
    }
    if (op >= 0x80 && op < 0xA0) {
        return 4;
    }
    if (op >= 0xA0 && op < 0xE0) {
        return 3;
    }
    if (op == 0x02) {
        return 3;
    }
    return 1;
}

/* ---- the wide copy ---------------------------------------------------------------- */

/* Whether the extension rule applies to this primitive (see wide.c). */
static int wideExtends(int textured) {
    unsigned f = gp.pkt != NULL ? gp.pkt->flags : 0;
    if ((f & PC_WIDE_NOEXTEND) || (gp.pkt != NULL && gp.pkt->dx != 0)) {
        return 0;
    }
    return !textured || (f & PC_WIDE_EXTEND);
}

static int savedClipX0, savedClipX1, savedClipY0, savedClipY1, savedShift, savedShiftY;

/* Switch to the wide target for one command, if its draw area is a frame
 * buffer of a wide frame. */
static int wideBegin(void) {
    if (!wideActive() || !wideDrawTarget(gp.clipX0, gp.clipY0, gp.clipX1, gp.clipY1, &gp.wd)) {
        return 0;
    }
    int dx = gp.pkt != NULL ? gp.pkt->dx : 0;
    savedClipX0 = gp.clipX0;
    savedClipX1 = gp.clipX1;
    savedClipY0 = gp.clipY0;
    savedClipY1 = gp.clipY1;
    savedShift = gp.wideShift;
    savedShiftY = gp.wideShiftY;
    gp.clipX0 = gp.wd.clipX0;
    gp.clipX1 = gp.wd.clipX1;
    gp.clipY0 = gp.wd.clipY0;
    gp.clipY1 = gp.wd.clipY1;
    if (gp.pkt != NULL && (gp.pkt->flags & PC_WIDE_BELOW) && gp.clipY1 >= gp.wd.fbY1) {
        gp.clipY1 = gp.wd.belowY1; /* and the scratch rows below the frame */
        gp.clipX0 = 0;             /* and beside it */
        gp.clipX1 = WIDE_FRAME_W - 1;
    }
    /* Added after the GPU's 11-bit wrap of vertex + offset, so a vertex
     * far off the screen lands where the 4:3 copy has it. */
    gp.wideShift = gp.wd.shift + dx;
    gp.wideShiftY = gp.wd.shiftY;
    gp.wide = 1;
    gp.wideTex = 0;
    return 1;
}

static void wideEnd(void) {
    gp.wide = 0;
    gp.clipX0 = savedClipX0;
    gp.clipX1 = savedClipX1;
    gp.clipY0 = savedClipY0;
    gp.clipY1 = savedClipY1;
    gp.wideShift = savedShift;
    gp.wideShiftY = savedShiftY;
}

static void executeDraw(uint32_t op) {
    if (op >= 0x20 && op < 0x40) {
        polygon();
    } else if (op >= 0x40 && op < 0x60) {
        lineCommand();
    } else {
        rectCommand();
    }
}

static void execute(void) {
    uint32_t word = gp.cmd[0];
    uint32_t op = word >> 24;
    uint32_t nativeE1 = gp.e1;
    if (op >= 0x20 && op < 0x80) {
        /* A polyline's first command only starts it: its segments come word
         * by word (polylineWord). */
        int polylineStart = op >= 0x40 && op < 0x60 && (op & 0x08);
        gp.replayed = 0;
        memset(&gp.wideUv, 0, sizeof(gp.wideUv));
        if (!polylineStart && wideBegin()) {
            uint32_t e1 = gp.e1;
            if (gp.pkt == NULL || !(gp.pkt->flags & PC_WIDE_NATIVE_ONLY)) {
                executeDraw(op);
            }
            wideEnd();
            gp.replayed = 1;
            if (gp.pkt != NULL && (gp.pkt->flags & PC_WIDE_ONLY)) {
                /* A textured polygon sets the draw mode from its texpage; one
                 * VRAM never sees leaves the mode as the PS1 has it. */
                gp.e1 = e1;
            }
        }
        if (gp.pkt != NULL && (gp.pkt->flags & PC_WIDE_ONLY) && !polylineStart) {
            if (op >= 0x20 && op < 0x40 && gp.replayed && gvkHiScale() != 0) {
                gp.hiSliverOnly = 1;
                polygon();
                gp.hiSliverOnly = 0;
            }
            return;
        }
    }
    if (op >= 0x20 && op < 0x40) {
        polygon();
    } else if (op >= 0x40 && op < 0x60) {
        lineCommand();
    } else if (op >= 0x60 && op < 0x80) {
        rectCommand();
    } else if (op >= 0x80 && op < 0xA0) {
        copyCommand();
    } else if (op >= 0xA0 && op < 0xC0) {
        loadStart();
    } else if (op >= 0xC0 && op < 0xE0) {
        /* Its data is not returned, but a VRAM-to-CPU transfer empties the
         * texture cache (see the caches). */
        gp0InvalidateTexture();
        gpuWarnOnce("gp0c0", "gpu: GP0(C0h) VRAM read inside a packet ignored");
    } else {
        switch (op) {
        case 0x02:
            fillCommand();
            break;
        case 0x01:
            gp0ClearCache();
            break;
        case 0xE1:
            setDrawMode(word & 0x3FFF);
            break;
        case 0xE2:
            gp.texWindow = word & 0xFFFFF;
            break;
        case 0xE3:
            gp.clipX0 = (int)(word & 0x3FF);
            gp.clipY0 = (int)((word >> 10) & 0x1FF);
            break;
        case 0xE4:
            gp.clipX1 = (int)(word & 0x3FF);
            gp.clipY1 = (int)((word >> 10) & 0x1FF);
            break;
        case 0xE5:
            gp.offX = sext11(word);
            gp.offY = sext11(word >> 11);
            break;
        case 0xE6:
            gp0SetMaskBits(word);
            break;
        default:
            break; /* NOP, cache clear, IRQ request */
        }
    }
    /* An extra textured face must not change the next native primitive's
     * texture page or blend mode. Both its narrow and wide draws are copies. */
    if (gp.pkt && gp.pkt->geometry == PC_GEOMETRY_ONLY) gp.e1 = nativeE1;
}

void gp0Write(const uint32_t* words, int n) {
    CPU_TIME_BEGIN(CPU_GP0);
    for (int i = 0; i < n; i++) {
        uint32_t word = words[i];
        if (gp.loading) {
            loadWord(word);
            continue;
        }
        if (gp.polyline) {
            polylineWord(word);
            continue;
        }
        if (gp.count == 0) {
            gp.needed = commandLength(word >> 24);
        }
        gp.cmdAddr[gp.count] = &words[i];
        gp.cmd[gp.count++] = word;
        if (gp.count >= gp.needed) {
            gp.count = 0;
            execute();
        }
    }
    CPU_TIME_END(CPU_GP0);
}

/* ---- save states (system/savestate.h) ---- */

/* Before a save: the batch ended, so what the device has drawn can be read
 * back (vram.c's part); nothing of the emulation is touched (a readback into
 * the CPU copy would settle owed texture-cache walks early: saving must
 * change nothing). */
void gp0StateFlush(void) {
    gp0Flush();
}

/* The drawing environment, a command or an image load in progress (at most
 * loadPos halfwords of loadBuf matter), the emulated texture and CLUT
 * caches. The batch's tile sets are the device's (cleared: the batch ended). */
void gp0StateIo(XgState* s) {
    XG_STATE_VAR(s, gp.e1);
    XG_STATE_VAR(s, gp.texWindow);
    XG_STATE_VAR(s, gp.clipX0);
    XG_STATE_VAR(s, gp.clipY0);
    XG_STATE_VAR(s, gp.clipX1);
    XG_STATE_VAR(s, gp.clipY1);
    XG_STATE_VAR(s, gp.offX);
    XG_STATE_VAR(s, gp.offY);
    XG_STATE_VAR(s, gp.setMask);
    XG_STATE_VAR(s, gp.checkMask);
    XG_STATE_VAR(s, gp.cmd);
    XG_STATE_VAR(s, gp.cmdAddr);
    XG_STATE_VAR(s, gp.count);
    XG_STATE_VAR(s, gp.needed);
    XG_STATE_VAR(s, gp.polyline);
    XG_STATE_VAR(s, gp.polyGouraud);
    XG_STATE_VAR(s, gp.polyWantColor);
    XG_STATE_VAR(s, gp.polyColor);
    XG_STATE_VAR(s, gp.polyCmd);
    XG_STATE_VAR(s, gp.polyPrev);
    XG_STATE_VAR(s, gp.loading);
    XG_STATE_VAR(s, gp.loadX);
    XG_STATE_VAR(s, gp.loadY);
    XG_STATE_VAR(s, gp.loadW);
    XG_STATE_VAR(s, gp.loadH);
    XG_STATE_VAR(s, gp.loadPos);
    XG_STATE_VAR(s, gp.loadTotal);
    size_t loaded = xgStateLen(s, "loadBuf", gp.loading ? (size_t)gp.loadPos : 0);
    xgStateIo(s, "loadBuf", gp.loadBuf, loaded <= sizeof(gp.loadBuf) / 2 ? loaded * 2 : 0);
    XG_STATE_VAR(s, gp.tex);
    XG_STATE_VAR(s, gp.texKey);
    XG_STATE_VAR(s, gp.clut);
    XG_STATE_VAR(s, gp.clutKey);
    XG_STATE_VAR(s, cacheDirty);
    XG_STATE_VAR(s, cacheTiles);
    XG_STATE_VAR(s, cacheMaybeStale);
    XG_STATE_VAR(s, cacheMaybeStaleCount);
    /* The texture-cache walks still owed, as they are. */
    size_t owed = xgStateLen(s, "cachePendUsed", (size_t)cachePendUsed);
    if (owed > CACHE_PEND_INTS) {
        owed = 0;
    }
    if (xgStateLoading(s) && owed > 0 && cachePend == NULL &&
        (cachePend = malloc(CACHE_PEND_INTS * sizeof(int32_t))) == NULL) {
        fprintf(stderr, "gp0: out of memory\n");
        abort();
    }
    xgStateIo(s, "cachePend", cachePend, owed * sizeof(int32_t));
    if (xgStateLoading(s)) {
        cachePendUsed = (int)owed;
        nativeClear(&gp.batchWritten, &gp.batchWrittenExact);
        nativeClear(&gp.batchRead, &gp.batchReadExact);
        tilesClear(&gp.batchWideWritten);
        tilesClear(&gp.batchWideRead);
    }
}
