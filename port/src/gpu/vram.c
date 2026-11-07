/*
 * The CPU copy of VRAM. Transfers (LoadImage, fills, copies of clean areas)
 * keep it exact as they go; drawing happens on the device, so the tiles it
 * touches are marked and read back before the CPU looks at them (StoreImage,
 * frame dumps).
 */

#include <stdlib.h>
#include <string.h>

#include "gpu_internal.h"
#include "../system/savestate.h"

uint16_t vramShadow[VRAM_H][VRAM_W];

/* Tiles the device has drawn into since the last readback. */
static TileSet drawnTiles;
static TileSet cpuWritten;
static TileSet renderedTiles;
static uint64_t generations[TILES_Y][TILES_X], generation;
/* The newest generation per block of tiles (vramBlockGeneration). */
static uint64_t blocks[TILES_Y >> VRAM_BLOCK_SHIFT][TILES_X >> VRAM_BLOCK_SHIFT];
static struct { VRect r; uint64_t generation; } uploads[64];
static unsigned uploadNext;

/* Native StoreImage round trips. These belong to texture provenance, not
 * the optional display copy: a load or resolution change can discard that
 * copy without making a captured frame into a replaceable texture. Two
 * recent rectangles, at most two full VRAM images; save only their live
 * prefixes. A rectangle here describes the whole transfer before wrapping. */
static struct { VRect r; uint64_t age; } captures[2];
static uint64_t captureAge;
static size_t captureSize[2];
static uint16_t capturePixels[2][VRAM_W * VRAM_H];

/* Stable texture regions (texture.c textureRequestSnapStable): every pixel's
 * upload stamp, 1 + the vblank of the upload or clean copy that last wrote
 * it; 0 where the last write was a draw, a fill, a rendered copy or a masked
 * upload. stampedTiles: tiles that may hold non-zero stamps. */
static uint32_t stamps[VRAM_H][VRAM_W];
/* Per tile, for vramStableRegion's range scan: the lowest and highest stamp
 * in the tile and whether any is zero, found when first asked and dropped
 * when the tile's stamps change (stamp, unstamp). The scan of every pixel
 * under a texture request, two or three times per draw, was the cost of
 * snapping requests (xg-dx8). */
static struct { uint32_t lo, hi; uint8_t zero, valid; } stampTiles[TILES_Y][TILES_X];
static TileSet stampedTiles;
static uint32_t stampClock = 1;
#define PAGE_SLOTS 96 /* every logical page: 32 texture pages at three depths */
static struct PageInfo* pageCache[PAGE_SLOTS];

void vramReset(void) {
    vramOwnedReset();
    memset(vramShadow, 0, sizeof(vramShadow));
    tilesClear(&drawnTiles);
    tilesClear(&cpuWritten);
    tilesClear(&renderedTiles);
    memset(generations, 0, sizeof(generations));
    memset(blocks, 0, sizeof(blocks));
    memset(stampTiles, 0, sizeof(stampTiles));
    generation = 0;
    memset(uploads, 0, sizeof(uploads));
    uploadNext = 0;
    memset(captures, 0, sizeof(captures));
    captureAge = 0;
    memset(captureSize, 0, sizeof(captureSize));
    memset(capturePixels, 0, sizeof(capturePixels));
    memset(stamps, 0, sizeof(stamps));
    tilesClear(&stampedTiles);
    stampClock = 1;
    for (int i = 0; i < PAGE_SLOTS; ++i) { free(pageCache[i]); pageCache[i] = NULL; }
    vramLayoutSerial++;
}

void vramStampClock(uint32_t vblank) {
    stampClock = vblank + 1 ? vblank + 1 : 1;
}

uint32_t vramStamp(int x, int y) {
    return stamps[y & (VRAM_H - 1)][x & (VRAM_W - 1)];
}

static void stampSummaryDrop(VRect r) {
    for (int ty = r.y0 >> TILE_SHIFT; ty <= r.y1 >> TILE_SHIFT; ++ty)
        for (int tx = r.x0 >> TILE_SHIFT; tx <= r.x1 >> TILE_SHIFT; ++tx) stampTiles[ty][tx].valid = 0;
}

static void stamp(VRect r) {
    for (int y = r.y0; y <= r.y1; ++y)
        for (int x = r.x0; x <= r.x1; ++x) stamps[y][x] = stampClock;
    tilesAdd(&stampedTiles, r);
    stampSummaryDrop(r);
}

/* The lowest and highest stamp under s, or 0 if any pixel there has none:
 * whole tiles from their summaries, the tiles s cuts pixel by pixel. */
/* A tile's stamp summary (lowest, highest, any pixel unstamped), kept until
 * the stamps under it change. */
static void tileSummary(int ty, int tx) {
    if (stampTiles[ty][tx].valid) return;
    int y0 = ty << TILE_SHIFT, x0 = tx << TILE_SHIFT;
    uint32_t a = UINT32_MAX, b = 0; int zero = 0;
    for (int y = y0; y < y0 + (1 << TILE_SHIFT); ++y)
        for (int x = x0; x < x0 + (1 << TILE_SHIFT); ++x) {
            uint32_t v = stamps[y][x];
            zero |= v == 0;
            if (v < a) a = v;
            if (v > b) b = v;
        }
    stampTiles[ty][tx].lo = a; stampTiles[ty][tx].hi = b;
    stampTiles[ty][tx].zero = (uint8_t)zero; stampTiles[ty][tx].valid = 1;
}

static int stampRange(VRect s, uint32_t* lo, uint32_t* hi) {
    uint32_t l = UINT32_MAX, h = 0;
    for (int ty = s.y0 >> TILE_SHIFT; ty <= s.y1 >> TILE_SHIFT; ++ty) {
        int y0 = ty << TILE_SHIFT, y1 = y0 + (1 << TILE_SHIFT) - 1;
        int cy0 = y0 > s.y0 ? y0 : s.y0, cy1 = y1 < s.y1 ? y1 : s.y1;
        for (int tx = s.x0 >> TILE_SHIFT; tx <= s.x1 >> TILE_SHIFT; ++tx) {
            int x0 = tx << TILE_SHIFT, x1 = x0 + (1 << TILE_SHIFT) - 1;
            int cx0 = x0 > s.x0 ? x0 : s.x0, cx1 = x1 < s.x1 ? x1 : s.x1;
            uint32_t tl, th;
            tileSummary(ty, tx);
            if (cx0 == x0 && cx1 == x1 && cy0 == y0 && cy1 == y1) {
                if (stampTiles[ty][tx].zero) return 0;
                tl = stampTiles[ty][tx].lo; th = stampTiles[ty][tx].hi;
            } else if (!stampTiles[ty][tx].zero && stampTiles[ty][tx].lo == stampTiles[ty][tx].hi) {
                /* one stamp over the whole tile (one upload): any part of it is that stamp. Per
                 * pixel, the partly covered tiles at a polygon's edges were most of gp0's time in
                 * crowded 3D scenes (Lahan after the attack: 10 ms a drawn frame at 12x). */
                tl = th = stampTiles[ty][tx].lo;
            } else {
                tl = UINT32_MAX; th = 0;
                for (int y = cy0; y <= cy1; ++y)
                    for (int x = cx0; x <= cx1; ++x) {
                        uint32_t v = stamps[y][x];
                        if (!v) return 0;
                        if (v < tl) tl = v;
                        if (v > th) th = v;
                    }
            }
            if (tl < l) l = tl;
            if (th > h) h = th;
        }
    }
    *lo = l; *hi = h;
    return 1;
}

/* Clear the stamps under r, tile by tile, only where tiles hold any. */
static void unstamp(VRect r) {
    for (int ty = r.y0 >> TILE_SHIFT; ty <= r.y1 >> TILE_SHIFT; ++ty) {
        uint64_t bits = stampedTiles.rows[ty];
        if (!bits) continue;
        for (int tx = r.x0 >> TILE_SHIFT; tx <= r.x1 >> TILE_SHIFT; ++tx) {
            if (!(bits & (1ull << tx))) continue;
            int x0 = tx * 16 > r.x0 ? tx * 16 : r.x0, x1 = tx * 16 + 15 < r.x1 ? tx * 16 + 15 : r.x1;
            int y0 = ty * 16 > r.y0 ? ty * 16 : r.y0, y1 = ty * 16 + 15 < r.y1 ? ty * 16 + 15 : r.y1;
            for (int y = y0; y <= y1; ++y) memset(&stamps[y][x0], 0, (size_t)(x1 - x0 + 1) * sizeof(uint32_t));
            stampTiles[ty][tx].valid = 0;
            if (x0 == tx * 16 && x1 == tx * 16 + 15 && y0 == ty * 16 && y1 == ty * 16 + 15)
                stampedTiles.rows[ty] &= ~(1ull << tx);
        }
    }
}

uint64_t vramTileGeneration(int x, int y) {
    return generations[y][x];
}

uint64_t vramBlockGeneration(int bx, int by) {
    return blocks[by][bx];
}

static void changed(VRect r) {
    ++generation;
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; ++y)
        for (int x = r.x0 >> TILE_SHIFT; x <= r.x1 >> TILE_SHIFT; ++x)
            generations[y][x] = generation;
    for (int y = r.y0 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); y <= r.y1 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); ++y)
        for (int x = r.x0 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); x <= r.x1 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); ++x)
            blocks[y][x] = generation;
}

int vramWasRendered(const TileSet* tiles) {
    for (int y = 0; y < TILES_Y; ++y)
        if (tiles->rows[y] & renderedTiles.rows[y]) return 1;
    return 0;
}

/* The same colour agreement used by the display copy (hiresLoadPlan): a
 * small CPU edit of a frame still belongs to that frame. Mask operations
 * affect the resulting pixels before comparison; bit 15 is not colour. */
static int loadMatches(int x, int y, int w, int h, const uint16_t* src,
                       const uint16_t* captured, int setMask, int checkMask) {
    int total = w * h, need = (total * 90 + 99) / 100;
    int matches = 0, misses = 0;
    for (int row = 0; row < h; ++row) {
        const uint16_t* old = vramShadow[(y + row) & (VRAM_H - 1)];
        for (int col = 0; col < w; ++col) {
            int i = row * w + col;
            uint16_t before = old[(x + col) & (VRAM_W - 1)];
            uint16_t after = checkMask && (before & 0x8000) ? before : src[i] | (setMask ? 0x8000 : 0);
            uint16_t target = captured ? captured[i] : before;
            if ((after & 0x7fff) == (target & 0x7fff)) {
                if (++matches >= need) return 1;
            } else if (++misses > total - need) return 0;
        }
    }
    return 0;
}

int vramLoadRendered(int x, int y, int w, int h, const uint16_t* src, int setMask, int checkMask) {
    if (!src || w <= 0 || h <= 0 || w > VRAM_W || h > VRAM_H) return 0;
    x &= VRAM_W - 1;
    y &= VRAM_H - 1;
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces), rendered = 0, captured[2];
    for (int i = 0; i < n; ++i) rendered |= tilesHit(&renderedTiles, pieces[i].r);
    for (int i = 0; i < 2; ++i) {
        VRect r = captures[i].r;
        captured[i] = captures[i].age && (int64_t)r.x1 - r.x0 + 1 == w && (int64_t)r.y1 - r.y0 + 1 == h &&
                      captureSize[i] == (size_t)w * h;
    }
    /* A masked comparison needs authoritative destination pixels too. */
    if (rendered || (checkMask && (captured[0] || captured[1])))
        for (int i = 0; i < n; ++i) vramSyncRect(pieces[i].r);
    if (rendered && loadMatches(x, y, w, h, src, NULL, setMask, checkMask)) return 1;
    for (int i = 0; i < 2; ++i)
        if (captured[i] && loadMatches(x, y, w, h, src, capturePixels[i], setMask, checkMask)) return 1;
    return 0;
}

void vramStoreCapture(int x, int y, int w, int h, const uint16_t* pixels) {
    if (!pixels || w <= 0 || h <= 0 || w > VRAM_W || h > VRAM_H) return;
    x &= VRAM_W - 1;
    y &= VRAM_H - 1;
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces), rendered = 0;
    for (int i = 0; i < n; ++i) rendered |= tilesHit(&renderedTiles, pieces[i].r);
    if (!rendered) return; /* reading an uploaded texture does not make a capture */
    VRect r = {x, y, x + w - 1, y + h - 1};
    int slot = captures[0].age <= captures[1].age ? 0 : 1;
    for (int i = 0; i < 2; ++i)
        if (captures[i].age && memcmp(&captures[i].r, &r, sizeof(r)) == 0) { slot = i; break; }
    captures[slot].r = r;
    captures[slot].age = ++captureAge;
    captureSize[slot] = (size_t)w * h;
    memcpy(capturePixels[slot], pixels, captureSize[slot] * sizeof(uint16_t));
}

void vramMarkUploaded(VRect r, int checkMask) {
    changed(r);
    if (checkMask) { unstamp(r); return; } /* rejected pixels can retain drawn contents */
    stamp(r);
    uploads[uploadNext].r = r;
    uploads[uploadNext].generation = generation;
    uploadNext = (uploadNext + 1) % 64;
    for (int y = (r.y0 + 15) >> TILE_SHIFT; y < (r.y1 + 1) >> TILE_SHIFT; ++y)
        for (int x = (r.x0 + 15) >> TILE_SHIFT; x < (r.x1 + 1) >> TILE_SHIFT; ++x)
            renderedTiles.rows[y] &= ~(1ull << x);
}

uint32_t vramLayoutSerial = 1;

/* Every tile of r at generation g (an upload nothing wrote into since). */
static int uploadUnchanged(VRect r, uint64_t g) {
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; ++y)
        for (int x = r.x0 >> TILE_SHIFT; x <= r.x1 >> TILE_SHIFT; ++x)
            if (generations[y][x] != g) return 0;
    return 1;
}

int vramUploadCoverAt(VRect sample, VRect* upload, unsigned* index, uint64_t* gen) {
    for (unsigned n = 0; n < 64; ++n) {
        unsigned i = (uploadNext + 63 - n) % 64;
        VRect r = uploads[i].r;
        uint64_t g = uploads[i].generation;
        if (!g || r.x0 > sample.x0 || r.y0 > sample.y0 || r.x1 < sample.x1 || r.y1 < sample.y1) continue;
        if (uploadUnchanged(r, g)) {
            *upload = r;
            if (index) *index = i;
            if (gen) *gen = g;
            return 1;
        }
    }
    return 0;
}

int vramUploadCover(VRect sample, VRect* upload) {
    return vramUploadCoverAt(sample, upload, NULL, NULL);
}

uint64_t vramBlockNewest(VRect r) {
    uint64_t g = 0;
    for (int by = r.y0 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); by <= r.y1 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); ++by)
        for (int bx = r.x0 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); bx <= r.x1 >> (TILE_SHIFT + VRAM_BLOCK_SHIFT); ++bx)
            if (blocks[by][bx] > g) g = blocks[by][bx];
    return g;
}

int vramUploadIntact(unsigned index, VRect r, uint64_t gen) {
    if (index >= 64 || uploads[index].generation != gen || memcmp(&uploads[index].r, &r, sizeof(r)) != 0) return 0;
    /* Generations only grow: blocks no newer than the upload mean no tile of it is. */
    if (vramBlockNewest(r) == gen) return 1;
    return uploadUnchanged(r, gen);
}

/* ---- stable regions ----
 * A field streams its textures into VRAM as dozens of strips over a second
 * or two (field 315: 105 uploads in v598-660, strips like 22x46 or 113x9
 * crossing pages), so no single upload covers a polygon's patch and every
 * polygon used to be a texture of its own. A stable region is one load
 * inside a texture page: the pixels whose stamps form one run in time (no
 * two consecutive distinct stamps more than STABLE_GAP vblanks apart: field
 * 315's loader comes back to a page after 46), owning at least STABLE_MIN
 * pixels (half a 4-bit page: a load, not two portraits or a sprite frame
 * uploaded together, which keep their own keys) and at least half of its
 * box (non-zero pixels, connected in 8x8 blocks). Pixels
 * outside the run inside its box (older leftovers, later sprite or palette
 * uploads) are masked by the decoder, so they never enter the content key. */
#define STABLE_GAP 120
#define STABLE_MIN 8192
#define STABLE_BINS 1024
#define STABLE_CHAINS 64
/* A run's pixels also have to touch: its 8x8 blocks, 8-connected, make the
 * parts (the boot's compass ring and a logo left beside it are two). */
#define STABLE_BLOCK 3
#define STABLE_BLOCKS ((256 >> STABLE_BLOCK) * (256 >> STABLE_BLOCK))
#define STABLE_PARTS 256
typedef struct {
    uint32_t from, to;
} StableChain;
typedef struct {
    int chain;
    uint32_t count;
    VRect box;
} StablePart;
typedef struct PageInfo {
    VRect page;
    uint64_t signature;
    uint64_t touched;
    int chains; /* -1: too many distinct stamps to tell */
    StableChain chain[STABLE_CHAINS];
    int parts, bw, bh;
    StablePart part[STABLE_PARTS];
    uint8_t blockChain[STABLE_BLOCKS];  /* 255: none */
    uint16_t blockPart[STABLE_BLOCKS];  /* 0xFFFF: none */
    uint32_t blockCount[STABLE_BLOCKS];
    VRect blockBox[STABLE_BLOCKS];
} PageInfo;
static uint64_t pageClock;

static uint64_t pageSignature(VRect page) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (int y = page.y0 >> TILE_SHIFT; y <= page.y1 >> TILE_SHIFT; ++y)
        for (int x = page.x0 >> TILE_SHIFT; x <= page.x1 >> TILE_SHIFT; ++x)
            h = (h ^ generations[y][x]) * UINT64_C(1099511628211);
    return h;
}

typedef struct { uint32_t stamp; } StampBin;
static int byStamp(const void* a, const void* b) {
    uint32_t x = ((const StampBin*)a)->stamp, y = ((const StampBin*)b)->stamp;
    return x < y ? -1 : x > y;
}

static int chainOf(const PageInfo* p, uint32_t s) {
    int lo = 0, hi = p->chains - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s < p->chain[mid].from) hi = mid - 1;
        else if (s > p->chain[mid].to) lo = mid + 1;
        else return mid;
    }
    return -1;
}

static void analyse(PageInfo* p) {
    static StampBin bins[STABLE_BINS];
    static uint32_t slot[STABLE_BINS * 2];
    int used = 0;
    memset(slot, 0, sizeof(slot));
    p->chains = 0;
    p->parts = 0;
    p->bw = ((p->page.x1 - p->page.x0) >> STABLE_BLOCK) + 1;
    p->bh = ((p->page.y1 - p->page.y0) >> STABLE_BLOCK) + 1;
    /* The page's distinct stamps, then their runs in time. */
    for (int y = p->page.y0; y <= p->page.y1; ++y)
        for (int x = p->page.x0; x <= p->page.x1; ++x) {
            uint32_t s = stamps[y][x];
            if (!s) continue;
            unsigned h = (s * 2654435761u) & (STABLE_BINS * 2 - 1);
            while (slot[h] && slot[h] != s) h = (h + 1) & (STABLE_BINS * 2 - 1);
            if (!slot[h]) {
                if (used == STABLE_BINS) { p->chains = -1; return; }
                slot[h] = s;
                bins[used++].stamp = s;
            }
        }
    qsort(bins, (size_t)used, sizeof(bins[0]), byStamp);
    for (int i = 0; i < used; ++i) {
        uint32_t s = bins[i].stamp;
        if (p->chains && s - p->chain[p->chains - 1].to <= STABLE_GAP) {
            p->chain[p->chains - 1].to = s;
            continue;
        }
        if (p->chains == STABLE_CHAINS) break;
        p->chain[p->chains].from = p->chain[p->chains].to = s;
        p->chains++;
    }
    /* Each block goes to the run owning most of its non-zero pixels
     * (majority vote), with that run's count and box of them there: all-zero
     * texels (an uploaded sheet's empty space) neither join parts nor count
     * towards STABLE_MIN, so a sheet loaded at boot and a logo left beside
     * it stay apart. */
    int blocks = p->bw * p->bh;
    memset(p->blockChain, 255, (size_t)blocks);
    memset(p->blockCount, 0, (size_t)blocks * sizeof(uint32_t));
    for (int y = p->page.y0; y <= p->page.y1; ++y)
        for (int x = p->page.x0; x <= p->page.x1; ++x) {
            uint32_t s = stamps[y][x];
            if (!s || !vramShadow[y][x]) continue;
            int c = chainOf(p, s);
            if (c < 0) continue;
            int b = ((y - p->page.y0) >> STABLE_BLOCK) * p->bw + ((x - p->page.x0) >> STABLE_BLOCK);
            if (p->blockChain[b] == c) p->blockCount[b]++;
            else if (!p->blockCount[b]) { p->blockChain[b] = (uint8_t)c; p->blockCount[b] = 1; }
            else p->blockCount[b]--;
        }
    for (int b = 0; b < blocks; ++b) {
        p->blockCount[b] = 0;
        p->blockBox[b] = (VRect){VRAM_W, VRAM_H, -1, -1};
    }
    for (int y = p->page.y0; y <= p->page.y1; ++y)
        for (int x = p->page.x0; x <= p->page.x1; ++x) {
            uint32_t s = stamps[y][x];
            if (!s || !vramShadow[y][x]) continue;
            int b = ((y - p->page.y0) >> STABLE_BLOCK) * p->bw + ((x - p->page.x0) >> STABLE_BLOCK);
            if (p->blockChain[b] == 255 || chainOf(p, s) != p->blockChain[b]) continue;
            p->blockCount[b]++;
            VRect* r = &p->blockBox[b];
            if (x < r->x0) r->x0 = x;
            if (y < r->y0) r->y0 = y;
            if (x > r->x1) r->x1 = x;
            if (y > r->y1) r->y1 = y;
        }
    /* Parts: blocks of one run that touch (8-connected). */
    static uint16_t queue[STABLE_BLOCKS];
    for (int b = 0; b < blocks; ++b) p->blockPart[b] = 0xFFFF;
    for (int b = 0; b < blocks; ++b) {
        if (p->blockChain[b] == 255 || !p->blockCount[b] || p->blockPart[b] != 0xFFFF) continue;
        if (p->parts == STABLE_PARTS) return;
        StablePart* part = &p->part[p->parts];
        part->chain = p->blockChain[b];
        part->count = 0;
        part->box = (VRect){VRAM_W, VRAM_H, -1, -1};
        int head = 0, tail = 0;
        queue[tail++] = (uint16_t)b;
        p->blockPart[b] = (uint16_t)p->parts;
        while (head < tail) {
            int q = queue[head++], bx = q % p->bw, by = q / p->bw;
            part->count += p->blockCount[q];
            VRect r = p->blockBox[q];
            if (r.x0 < part->box.x0) part->box.x0 = r.x0;
            if (r.y0 < part->box.y0) part->box.y0 = r.y0;
            if (r.x1 > part->box.x1) part->box.x1 = r.x1;
            if (r.y1 > part->box.y1) part->box.y1 = r.y1;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = bx + dx, ny = by + dy;
                    if (nx < 0 || ny < 0 || nx >= p->bw || ny >= p->bh) continue;
                    int n = ny * p->bw + nx;
                    if (p->blockPart[n] != 0xFFFF || p->blockChain[n] != part->chain || !p->blockCount[n]) continue;
                    p->blockPart[n] = (uint16_t)p->parts;
                    queue[tail++] = (uint16_t)n;
                }
        }
        p->parts++;
    }
}

int vramStableRegion(VRect sample, VRect page, VRect* region, uint32_t* from, uint32_t* to) {
    if (sample.x0 < page.x0 || sample.y0 < page.y0 || sample.x1 > page.x1 || sample.y1 > page.y1) return 0;
    if (page.x1 - page.x0 >= 256 || page.y1 - page.y0 >= 256) return 0;
    if (!tilesHit(&stampedTiles, sample)) return 0;
    uint32_t lo, hi;
    if (!stampRange(sample, &lo, &hi)) return 0;
    PageInfo* p = NULL;
    int victim = 0;
    for (int i = 0; i < PAGE_SLOTS; ++i) {
        PageInfo* c = pageCache[i];
        if (c && c->page.x0 == page.x0 && c->page.y0 == page.y0 && c->page.x1 == page.x1 && c->page.y1 == page.y1) {
            p = c;
            break;
        }
        if (!c || (pageCache[victim] && c->touched < pageCache[victim]->touched)) victim = i;
    }
    uint64_t sig = pageSignature(page);
    if (!p) {
        if (!pageCache[victim]) pageCache[victim] = malloc(sizeof(PageInfo));
        p = pageCache[victim];
        if (!p) return 0;
        p->page = page;
        p->signature = ~sig;
    }
    p->touched = ++pageClock;
    if (p->signature != sig) {
        p->signature = sig;
        analyse(p);
    }
    if (p->chains <= 0) return 0;
    int c = chainOf(p, lo);
    if (c < 0 || hi > p->chain[c].to) return 0;
    /* The blocks the sample touches belong to one part of that run (blocks
     * of zeros to none), and the part's box holds the whole sample. */
    int part = -1;
    for (int by = (sample.y0 - page.y0) >> STABLE_BLOCK; by <= (sample.y1 - page.y0) >> STABLE_BLOCK; ++by)
        for (int bx = (sample.x0 - page.x0) >> STABLE_BLOCK; bx <= (sample.x1 - page.x0) >> STABLE_BLOCK; ++bx) {
            int q = p->blockPart[by * p->bw + bx];
            if (q == 0xFFFF) continue;
            if (p->part[q].chain != c || (part >= 0 && q != part)) return 0;
            part = q;
        }
    if (part < 0 || p->part[part].count < STABLE_MIN) return 0;
    VRect box = p->part[part].box;
    /* A texture page's load fills its box; a few sprites or a HUD ring left
     * in a mostly empty page are not one image (their keys would depend on
     * whatever else came in with them). */
    if ((uint64_t)p->part[part].count * 2 < (uint64_t)(box.x1 - box.x0 + 1) * (box.y1 - box.y0 + 1)) return 0;
    if (sample.x0 < box.x0 || sample.y0 < box.y0 || sample.x1 > box.x1 || sample.y1 > box.y1) return 0;
    *region = p->part[part].box;
    *from = p->chain[c].from;
    *to = p->chain[c].to;
    return 1;
}

void vramMarkCopied(VRect r, int rendered) {
    if (rendered) {
        changed(r);
        unstamp(r);
        tilesAdd(&renderedTiles, r);
    } else {
        vramMarkUploaded(r, 0);
    }
}

void tilesClear(TileSet* t) {
    memset(t, 0, sizeof(*t));
}

static uint64_t tileMask(VRect r) {
    int a = r.x0 >> TILE_SHIFT;
    int b = r.x1 >> TILE_SHIFT;
    uint64_t hi = b >= 63 ? ~0ull : (2ull << b) - 1;
    return hi & ~((1ull << a) - 1);
}

void tilesAdd(TileSet* t, VRect r) {
    uint64_t m = tileMask(r);
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        t->rows[y] |= m;
    }
}

int tilesHit(const TileSet* t, VRect r) {
    uint64_t m = tileMask(r);
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        if (t->rows[y] & m) {
            return 1;
        }
    }
    return 0;
}

void vramMarkDrawn(VRect r) {
    changed(r);
    unstamp(r);
    tilesAdd(&renderedTiles, r);
    tilesAdd(&drawnTiles, r);
}

int vramIsDrawn(VRect r) {
    return tilesHit(&drawnTiles, r);
}

void vramResolveCpuDraw(VRect r) {
    uint64_t mask = tileMask(r);
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++)
        drawnTiles.rows[y] &= ~mask;
}

/* Each queued tile was synchronized before CPU shading, so all of its
 * pixels are authoritative. Merge only identical row masks: a rectangle
 * spanning a gap could overwrite newer device pixels in that gap. */
void vramDeferCpuDraw(VRect r) {
    vramResolveCpuDraw(r);tilesAdd(&cpuWritten,r);
}
void vramFlushCpuWrites(VRect r) {
    if(r.x0<0)r.x0=0;
    if(r.y0<0)r.y0=0;
    if(r.x1>=VRAM_W)r.x1=VRAM_W-1;
    if(r.y1>=VRAM_H)r.y1=VRAM_H-1;
    if(r.x1<r.x0 || r.y1<r.y0)return;
    uint64_t mask=tileMask(r);
    for(int y=r.y0/16;y<=r.y1/16;) {
        uint64_t bits=cpuWritten.rows[y]&mask;
        if(!bits){y++;continue;}
        int end=y+1;
        while(end<=r.y1/16 && (cpuWritten.rows[end]&mask)==bits)end++;
        for(int row=y;row<end;row++)cpuWritten.rows[row]&=~bits;
        while(bits) {
            int x0=__builtin_ctzll(bits),x1=x0;
            while(x1<63 && (bits&(UINT64_C(1)<<(x1+1))))x1++;
            VRect box={x0*16,y*16,x1*16+15,end*16-1};
            gvkUpload(box,&vramShadow[box.y0][box.x0],VRAM_W);
            uint64_t run=x1-x0==63?UINT64_MAX:((UINT64_C(1)<<(x1-x0+1))-1)<<x0;
            bits&=~run;
        }
        y=end;
    }
}

/* A CPU overwrite knows all pixels of complete tiles, without downloading
 * the device result it replaced. Edge tiles retain their unread neighbours. */
void vramResolveCpuWrite(VRect r) {
    for (int y=(r.y0+15)>>TILE_SHIFT; y<(r.y1+1)>>TILE_SHIFT; y++)
        for (int x=(r.x0+15)>>TILE_SHIFT; x<(r.x1+1)>>TILE_SHIFT; x++)
            drawnTiles.rows[y] &= ~(UINT64_C(1)<<x);
}

/*
 * Tiles the CPU reads after the frame drew them: a screen distortion mesh
 * whose rows sample the rows drawn just before (the opening flashback's
 * fire), a battle's ClearImage that the next LoadImage compares with. Drawn
 * by the GPU, each read is a readback, a queue submit and wait for the
 * frame's enhanced work (450 a frame in the flashback: xg-4iq, xg-3f2).
 * gp0.c draws the primitives that land in such tiles on the CPU as it draws
 * wave pieces (cacheShade, the same pixels as the GPU's: the frames compared
 * equal at 1x and 12x), the result uploaded, so the CPU copy stays exact
 * there and nothing is read back. Such a tile stays the CPU's for
 * OWNED_VBLANKS after its last read. XG_CPU_OWNED=0: the GPU draws them.
 */
#define OWNED_VBLANKS 8 /* the flashback's mesh comes back every 6 (each buffer every other frame), a battle's ClearImage every 4 */
static struct {
    int on;          /* -1 not read yet */
    uint32_t now;    /* vertical blanks */
    TileSet written; /* drawn since the last vertical blank */
    uint32_t read[TILES_Y][TILES_X]; /* when the CPU last read the tile after drawing it, now + 1 (0: never) */
} owned = {.on = -1};

void vramOwnedDraw(VRect r) {
    tilesAdd(&owned.written, r);
}

int vramCpuOwned(VRect r) {
    if (owned.on < 0) {
        const char* e = getenv("XG_CPU_OWNED");
        owned.on = !(e && *e == '0');
    }
    if (!owned.on) return 0;
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++)
        for (int x = r.x0 >> TILE_SHIFT; x <= r.x1 >> TILE_SHIFT; x++)
            if (owned.read[y][x] && owned.now + 1 - owned.read[y][x] <= OWNED_VBLANKS) return 1;
    return 0;
}

void vramOwnedReset(void) {
    memset(owned.read, 0, sizeof(owned.read));
    tilesClear(&owned.written);
}

void vramOwnedVBlank(void) {
    tilesClear(&owned.written);
    owned.now++;
}

static int keepSyncing; /* a keep's readback (below): no read of the frame's either */
static void keepDeviceRead(VRect r);

void vramSyncRect(VRect r) {
    /* The whole of VRAM (a dump, gpuReadVram) is no read of the frame's. */
    if (!keepSyncing && (r.x0 > 0 || r.y0 > 0 || r.x1 < VRAM_W - 1 || r.y1 < VRAM_H - 1)) {
        uint64_t m = tileMask(r);
        for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++)
            for (uint64_t bits = owned.written.rows[y] & m; bits; bits &= bits - 1)
                owned.read[y][__builtin_ctzll(bits)] = owned.now + 1;
    }
    if (!tilesHit(&drawnTiles, r)) return;
    uint64_t mask = tileMask(r);
    VRect dirty = {VRAM_W, VRAM_H, -1, -1};
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        uint64_t bits = drawnTiles.rows[y] & mask;
        if (!bits) continue;
        int x0 = __builtin_ctzll(bits) * 16, x1 = (64 - __builtin_clzll(bits)) * 16 - 1;
        if (x0 < dirty.x0) dirty.x0 = x0;
        if (x1 > dirty.x1) dirty.x1 = x1;
        if (dirty.y0 == VRAM_H) dirty.y0 = y * 16;
        dirty.y1 = y * 16 + 15;
    }
    /* A freshly drawn screen will be sampled tile by tile. Fetching its
     * dense pending region together avoids a queue wait for every tile,
     * while sparse writes still use only the requested region. */
    VRect pending = {VRAM_W, VRAM_H, -1, -1};
    int tiles = 0;
    for (int y = 0; y < TILES_Y; y++) {
        uint64_t bits = drawnTiles.rows[y];
        if (!bits) continue;
        tiles += __builtin_popcountll(bits);
        int x0 = __builtin_ctzll(bits) * 16, x1 = (64 - __builtin_clzll(bits)) * 16 - 1;
        if (x0 < pending.x0) pending.x0 = x0;
        if (x1 > pending.x1) pending.x1 = x1;
        if (pending.y0 == VRAM_H) pending.y0 = y * 16;
        pending.y1 = y * 16 + 15;
    }
    if (tiles >= 4 && (pending.x1-pending.x0+1)*(pending.y1-pending.y0+1) <= tiles*256*2) {
        dirty = pending;
    } else {
        /* A distant scratch strip can make the global dirty box sparse
         * while the sampled framebuffer is dense. Gather dense source
         * pages independently, so that strip does not force a download
         * for every small triangle of a screen effect. */
        for (int py = r.y0 / 256; py <= r.y1 / 256; py++) {
            for (int px = r.x0 / 256; px <= r.x1 / 256; px++) {
                uint64_t pageMask = UINT64_C(0xffff) << (px * 16);
                VRect page = {VRAM_W, VRAM_H, -1, -1};
                int count = 0;
                for (int y = py * 16; y < (py + 1) * 16; y++) {
                    uint64_t bits = drawnTiles.rows[y] & pageMask;
                    if (!bits) continue;
                    count += __builtin_popcountll(bits);
                    int x0 = __builtin_ctzll(bits) * 16, x1 = (64 - __builtin_clzll(bits)) * 16 - 1;
                    if (x0 < page.x0) page.x0 = x0;
                    if (x1 > page.x1) page.x1 = x1;
                    if (page.y0 == VRAM_H) page.y0 = y * 16;
                    page.y1 = y * 16 + 15;
                }
                if (count < 4 || (page.x1-page.x0+1)*(page.y1-page.y0+1) > count*256*2) continue;
                if (page.x0 < dirty.x0) dirty.x0 = page.x0;
                if (page.x1 > dirty.x1) dirty.x1 = page.x1;
                if (page.y0 < dirty.y0) dirty.y0 = page.y0;
                if (page.y1 > dirty.y1) dirty.y1 = page.y1;
            }
        }
    }
    if (dirty.x1 >= dirty.x0) {
        /* Read whole dirty tiles so adjacent pixels stay authoritative. A
         * screen-feedback tile must not download all of VRAM per triangle. */
        keepDeviceRead(dirty); /* a picture between's device keep: the CPU copy of its tiles first */
        gp0VramChanging(dirty); /* the CPU copy changes there */
        gvkReadback(dirty, &vramShadow[dirty.y0][dirty.x0], VRAM_W);
        vramResolveCpuDraw(dirty);
    }
}

void gpuReadVram(uint16_t* dst) {
    VRect all = {0, 0, VRAM_W - 1, VRAM_H - 1};
    vramSyncRect(all);
    if (dst != NULL) {
        memcpy(dst, vramShadow, sizeof(vramShadow));
    }
}

int vramWrap(int x, int y, int w, int h, VPiece out[4]) {
    int n = 0;
    x &= VRAM_W - 1;
    y &= VRAM_H - 1;
    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (w > VRAM_W) {
        w = VRAM_W;
    }
    if (h > VRAM_H) {
        h = VRAM_H;
    }
    int w0 = x + w > VRAM_W ? VRAM_W - x : w;
    int h0 = y + h > VRAM_H ? VRAM_H - y : h;
    for (int py = 0; py < 2; py++) {
        int ph = py == 0 ? h0 : h - h0;
        if (ph <= 0) {
            continue;
        }
        for (int px = 0; px < 2; px++) {
            int pw = px == 0 ? w0 : w - w0;
            if (pw <= 0) {
                continue;
            }
            VPiece* p = &out[n++];
            p->r.x0 = px == 0 ? x : 0;
            p->r.y0 = py == 0 ? y : 0;
            p->r.x1 = p->r.x0 + pw - 1;
            p->r.y1 = p->r.y0 + ph - 1;
            p->dx = px == 0 ? 0 : w0;
            p->dy = py == 0 ? 0 : h0;
        }
    }
    return n;
}

/* ---- VRAM-to-VRAM copies -------------------------------------------------- */

/* Whether cyclic ranges [a, a + an) and [b, b + bn) of a VRAM row meet. */
static int rowRangesMeet(int a, int an, int b, int bn) {
    return ((b - a) & (VRAM_W - 1)) < an || ((a - b) & (VRAM_W - 1)) < bn;
}

int vramCopyMeets(const VPiece* src, int ns, const VPiece* dst, int nd) {
    for (int i = 0; i < ns; i++) {
        for (int j = 0; j < nd; j++) {
            VRect a = src[i].r, b = dst[j].r;
            if (a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1) {
                return 1;
            }
        }
    }
    return 0;
}

int vramCopySteps(int sx, int sy, int dx, int dy, int w, int h, uint16_t* step) {
    int pieces = (w + COPY_PIECE - 1) / COPY_PIECE, last = 0;
    for (int r = 0; r < h; r++) {
        /* The copy row whose destination row is this row's source row. */
        int from = (sy + r - dy) & (VRAM_H - 1);
        for (int k = 0; k < pieces; k++) {
            int s = 0, len = w - k * COPY_PIECE < COPY_PIECE ? w - k * COPY_PIECE : COPY_PIECE;
            /* Earlier pieces (that row before this one, or this row's to the
             * left) whose destination this piece's source meets. */
            for (int i = 0; from < h && (from < r || (from == r && i < k)) && i < pieces; i++) {
                int ilen = w - i * COPY_PIECE < COPY_PIECE ? w - i * COPY_PIECE : COPY_PIECE;
                if (rowRangesMeet(dx + i * COPY_PIECE, ilen, sx + k * COPY_PIECE, len) &&
                    step[from * pieces + i] + 1 > s) {
                    s = step[from * pieces + i] + 1;
                }
            }
            step[r * pieces + k] = (uint16_t)s;
            last = s > last ? s : last;
        }
    }
    return last + 1;
}

int vramCopyStepRects(const uint16_t* step, int w, int h, int s, VRect* out) {
    int pieces = (w + COPY_PIECE - 1) / COPY_PIECE, n = 0, open = 0;
    for (int r = 0; r < h; r++) {
        VRect runs[VRAM_W / COPY_PIECE];
        int nr = 0;
        for (int k = 0; k < pieces; k++) {
            if (step[r * pieces + k] != s) {
                continue;
            }
            int x0 = k * COPY_PIECE, x1 = (x0 + COPY_PIECE < w ? x0 + COPY_PIECE : w) - 1;
            if (nr > 0 && runs[nr - 1].x1 + 1 == x0) {
                runs[nr - 1].x1 = x1;
            } else {
                runs[nr++] = (VRect){x0, r, x1, r};
            }
        }
        /* The same runs as the row above: those rectangles grow. */
        int same = nr == open && nr > 0;
        for (int i = 0; same && i < nr; i++) {
            same = out[n - open + i].x0 == runs[i].x0 && out[n - open + i].x1 == runs[i].x1;
        }
        if (same) {
            for (int i = 0; i < nr; i++) {
                out[n - open + i].y1 = r;
            }
        } else {
            for (int i = 0; i < nr; i++) {
                out[n++] = runs[i];
            }
            open = nr;
        }
    }
    return n;
}

/* ---- save states (system/savestate.h) ---- */

/* The CPU copy as it would be after a full readback, made aside: the
 * device's pixels where it has drawn since the last readback, the copy's
 * elsewhere. Nothing changes here (a readback into the copy would count as
 * VRAM changing for the texture cache's owed walks: saving changes nothing). */
static uint16_t savings[2][VRAM_H][VRAM_W];
static uint16_t (*saving)[VRAM_W] = savings[0];

static void snapshot(XgState* s) {
    /* Each in the other copy: rewind's pack worker may still be comparing
     * the last rewind capture's (rewind.c settlePixels). */
    saving = savings[saving == savings[0]];
    memcpy(saving, vramShadow, sizeof(savings[0]));
    for (int y = 0; y < TILES_Y; y++) {
        uint64_t bits = drawnTiles.rows[y];
        while (bits) {
            int x0 = __builtin_ctzll(bits);
            int x1 = x0;
            while (x1 + 1 < TILES_X && (bits >> (x1 + 1) & 1)) {
                x1++;
            }
            VRect r = {x0 * 16, y * 16, x1 * 16 + 15, y * 16 + 15};
            /* In a rewind capture they come behind the frame (savestate.h). */
            if (!xgStateLate(s, 0, r.x0, r.y0, r.x1 - r.x0 + 1, 16, &saving[r.y0][r.x0], VRAM_W)) {
                gvkReadback(r, &saving[r.y0][r.x0], VRAM_W);
            }
            bits &= ~(((x1 - x0 == 63) ? ~0ull : ((1ull << (x1 - x0 + 1)) - 1)) << x0);
        }
    }
}

/* The CPU copy (exact: see above) and the provenance texture replacement
 * keys on. After a load the device gets the CPU copy (gpuStateLoaded):
 * nothing is drawn-only any more. The upload stamps (texture stability)
 * start over. */
/* The bookkeeping kept aside and put back around a picture the 60 Hz
 * Battling draws into the displayed buffer and takes away again
 * (port/src/game/battling/between.c): the whole-VRAM StoreImage before it,
 * the draw and the put back after move the generations, the drawn and
 * rendered tiles and the upload ring, all in the save state (not the
 * owned hints: their vblank count runs on through the wait), which a rewind restore cannot replay (the sandbox ring is host
 * memory: a restored run skips one picture between, and its counters
 * would differ from the continuous run's for good, xg-hbc.22). The pixels
 * go back with the put back; so does this. */
static struct {
    int on;
    TileSet drawn, rendered, cpuWritten;
    uint64_t generations[TILES_Y][TILES_X], generation;
    __typeof__(blocks) blocks;
    __typeof__(uploads) uploads;
    unsigned uploadNext;
    __typeof__(captures) captures;
    uint64_t captureAge;
} kept;

void vramBookkeepingKeep(void) {
    kept.drawn = drawnTiles;
    kept.rendered = renderedTiles;
    kept.cpuWritten = cpuWritten;
    memcpy(kept.generations, generations, sizeof(generations));
    kept.generation = generation;
    memcpy(kept.blocks, blocks, sizeof(blocks));
    memcpy(kept.uploads, uploads, sizeof(uploads));
    kept.uploadNext = uploadNext;
    memcpy(kept.captures, captures, sizeof(captures));
    kept.captureAge = captureAge;
    kept.on = 1;
}

void vramBookkeepingPutBack(void) {
    if (!kept.on) return;
    kept.on = 0;
    drawnTiles = kept.drawn;
    renderedTiles = kept.rendered;
    cpuWritten = kept.cpuWritten;
    memcpy(generations, kept.generations, sizeof(generations));
    generation = kept.generation;
    memcpy(blocks, kept.blocks, sizeof(blocks));
    memcpy(uploads, kept.uploads, sizeof(uploads));
    uploadNext = kept.uploadNext;
    memcpy(captures, kept.captures, sizeof(captures));
    captureAge = kept.captureAge;
}

/* ---- the keep around a picture between ----
 * The 60 Hz Battling's picture between (between.c) draws into the displayed
 * buffer and uploads (the HUD's text, CLUTs) where the native frame then
 * draws from; every pixel it changes goes back before that frame. Rather
 * than the whole VRAM (a 1 MB readback and upload a frame), the tiles it
 * may write are kept before the write: the displayed buffer eagerly
 * (vramKeepRect, from between.c) and anything else at its first write
 * (vramKeepWrite, from the upload, move, fill and draw paths: libgpu.c,
 * gp0.c batchAdd), read back where drawn-only, as a StoreImage's pixels,
 * without the owned-hint marks a frame's read leaves (keepSyncing). After
 * the picture the kept tiles whose generation moved since the bookkeeping
 * keep (vramBookkeepingKeep, which must come first) are the changed ones:
 * vramKeepChanged hands them out as runs of tiles in a row, packed, for
 * the put back (libgpu.c pcVramPutBack). A tile that changed without a
 * keep (a write path without the hook) is counted and warned about once:
 * its pixels are lost. The displayed buffer itself is kept on the device
 * (libgpu.c pcVramKeepDevice, vramKeepDeviceRect): a draw into a tile
 * wholly inside it writes nothing the CPU holds (vramKeepDraw), so no
 * readback; the uploads, fills and CPU draws write the CPU copy and keep
 * their tiles here wherever they are. */
static struct {
    int on;
    TileSet tiles;               /* the kept tiles */
    uint16_t (*pixels)[VRAM_W];  /* their pixels, by VRAM coordinates */
    uint64_t bytes;              /* kept this time */
    int missed;                  /* changed tiles no keep covered, this time */
    int deviceOn;                /* the device keeps the pixels inside device (vramKeepDeviceRect) */
    VRect device;
    int deviceChanged;           /* a tile inside device changed (vramKeepEnd) */
} keep;

/* The tiles of row ty wholly inside the device-kept rectangle. */
static uint64_t deviceTiles(int ty) {
    if (!keep.deviceOn) return 0;
    int y0 = ty << TILE_SHIFT, y1 = y0 + (1 << TILE_SHIFT) - 1;
    if (y0 < keep.device.y0 || y1 > keep.device.y1) return 0;
    int tx0 = (keep.device.x0 + (1 << TILE_SHIFT) - 1) >> TILE_SHIFT, tx1 = ((keep.device.x1 + 1) >> TILE_SHIFT) - 1;
    if (tx1 < tx0) return 0;
    return (tx1 - tx0 == 63 ? UINT64_MAX : (UINT64_C(1) << (tx1 - tx0 + 1)) - 1) << tx0;
}

/* A readback of r while the device keeps the displayed buffer (a frame dump
 * of the picture between, a CPU read of it): the CPU copy of the device-kept
 * tiles it covers goes aside first, as any write's, so that the put back
 * restores it where the picture between drew (vramKeepChanged); else the
 * CPU copy would keep the picture between's pixels in tiles the bookkeeping
 * then calls clean. */
static void keepDeviceRead(VRect r) {
    if (!keep.on || !keep.deviceOn) return;
    uint64_t m = tileMask(r);
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        uint64_t miss = m & deviceTiles(y) & ~keep.tiles.rows[y];
        for (uint64_t bits = miss; bits; bits &= bits - 1) {
            int px = __builtin_ctzll(bits) << TILE_SHIFT, py = y << TILE_SHIFT;
            for (int row = 0; row < (1 << TILE_SHIFT); row++)
                memcpy(&keep.pixels[py + row][px], &vramShadow[py + row][px], (1 << TILE_SHIFT) * sizeof(uint16_t));
            keep.bytes += (1 << TILE_SHIFT) * (1 << TILE_SHIFT) * sizeof(uint16_t);
        }
        keep.tiles.rows[y] |= miss;
    }
}

void vramKeepDeviceRect(VRect r) {
    if (!keep.on) return;
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > VRAM_W - 1) r.x1 = VRAM_W - 1;
    if (r.y1 > VRAM_H - 1) r.y1 = VRAM_H - 1;
    if (r.x0 > r.x1 || r.y0 > r.y1) return;
    keep.device = r;
    keep.deviceOn = 1;
}

int vramKeeping(void) {
    return keep.on;
}

int vramKeepBegin(void) {
    if (!kept.on) return 0; /* the generations the changed tiles are judged against */
    if (keep.pixels == NULL) {
        keep.pixels = malloc(sizeof(vramShadow));
        if (keep.pixels == NULL) return 0;
    }
    tilesClear(&keep.tiles);
    keep.bytes = 0;
    keep.missed = 0;
    keep.deviceOn = 0;
    keep.deviceChanged = 0;
    keep.on = 1;
    return 1;
}

/* The tiles of r not kept yet (for a draw, not those the device keeps):
 * read back where drawn, then aside. */
static void keepTiles(VRect r, int draw) {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > VRAM_W - 1) r.x1 = VRAM_W - 1;
    if (r.y1 > VRAM_H - 1) r.y1 = VRAM_H - 1;
    if (r.x0 > r.x1 || r.y0 > r.y1) return;
    uint64_t m = tileMask(r);
    VRect need = {VRAM_W, VRAM_H, -1, -1};
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        uint64_t miss = m & ~keep.tiles.rows[y] & ~(draw ? deviceTiles(y) : 0);
        if (!miss) continue;
        int x0 = __builtin_ctzll(miss) << TILE_SHIFT, x1 = ((64 - __builtin_clzll(miss)) << TILE_SHIFT) - 1;
        if (x0 < need.x0) need.x0 = x0;
        if (x1 > need.x1) need.x1 = x1;
        if (need.y0 == VRAM_H) need.y0 = y << TILE_SHIFT;
        need.y1 = (y << TILE_SHIFT) + (1 << TILE_SHIFT) - 1;
    }
    if (need.x1 < need.x0) return;
    keepSyncing = 1;
    vramSyncRect(need);
    keepSyncing = 0;
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        uint64_t miss = m & ~keep.tiles.rows[y] & ~(draw ? deviceTiles(y) : 0);
        for (uint64_t bits = miss; bits; bits &= bits - 1) {
            int tx = __builtin_ctzll(bits), px = tx << TILE_SHIFT, py = y << TILE_SHIFT;
            for (int row = 0; row < (1 << TILE_SHIFT); row++)
                memcpy(&keep.pixels[py + row][px], &vramShadow[py + row][px], (1 << TILE_SHIFT) * sizeof(uint16_t));
            keep.bytes += (1 << TILE_SHIFT) * (1 << TILE_SHIFT) * sizeof(uint16_t);
        }
        keep.tiles.rows[y] |= miss;
    }
}

void vramKeepRect(int x, int y, int w, int h) {
    if (!keep.on || w <= 0 || h <= 0) return;
    keepTiles((VRect){x, y, x + w - 1, y + h - 1}, 0);
}

void vramKeepWrite(VRect r) {
    if (!keep.on) return;
    keepTiles(r, 0);
}

void vramKeepDraw(VRect r) {
    if (!keep.on) return;
    keepTiles(r, 1);
}

/* After vramKeepEnd: whether the picture between changed a device-kept tile. */
int vramKeepDeviceChanged(void) {
    return keep.deviceOn && keep.deviceChanged;
}

unsigned long long vramKeepBytes(void) {
    return keep.bytes;
}

/* The GPU's test programs link vram.c without libgpu.c (gpuWarnOnce). */
extern void gpuWarnOnce(const char* key, const char* fmt, ...) __attribute__((weak));

/* Writes stop being kept; the changed tiles are counted (kept or not). */
void vramKeepEnd(void) {
    if (!keep.on) return;
    keep.on = 0;
    if (!kept.on) return;
    keep.deviceChanged = 0;
    for (int y = 0; y < TILES_Y; y++) {
        uint64_t device = deviceTiles(y), covered = keep.tiles.rows[y] | device;
        for (int x = 0; x < TILES_X; x++) {
            if (generations[y][x] == kept.generations[y][x]) continue;
            if (!(covered >> x & 1)) keep.missed++;
            if (device >> x & 1) keep.deviceChanged = 1;
        }
    }
    if (keep.missed && gpuWarnOnce != NULL)
        gpuWarnOnce("keepmissed", "vram: a picture between changed %d tiles no keep covered", keep.missed);
}

/* The next run of changed kept tiles in a row from *index (0 first; tile
 * order), its rectangle and its pixels packed into out (pitch w, at most
 * VRAM_W x 16); 0 when there are no more. Before vramBookkeepingPutBack. */
int vramKeepChanged(int* index, int* x, int* y, int* w, int* h, uint16_t* out) {
    if (!kept.on || keep.pixels == NULL) return 0;
    for (int i = *index; i < TILES_X * TILES_Y; i++) {
        int ty = i / TILES_X, tx = i % TILES_X;
        if (!(keep.tiles.rows[ty] >> tx & 1) || generations[ty][tx] == kept.generations[ty][tx]) continue;
        int tx1 = tx;
        while (tx1 + 1 < TILES_X && (keep.tiles.rows[ty] >> (tx1 + 1) & 1) &&
               generations[ty][tx1 + 1] != kept.generations[ty][tx1 + 1])
            tx1++;
        *x = tx << TILE_SHIFT;
        *y = ty << TILE_SHIFT;
        *w = (tx1 - tx + 1) << TILE_SHIFT;
        *h = 1 << TILE_SHIFT;
        for (int row = 0; row < *h; row++) memcpy(out + (size_t)row * *w, &keep.pixels[*y + row][*x], (size_t)*w * sizeof(uint16_t));
        *index = ty * TILES_X + tx1 + 1;
        return 1;
    }
    *index = TILES_X * TILES_Y;
    return 0;
}

void vramStateIo(XgState* s) {
    if (xgStateSaving(s)) {
        snapshot(s);
    }
    if (xgStateSaving(s)) {
        /* the copy aside (the other one next time): a rewind capture compares it on its worker */
        xgStateIoAside(s, "vramShadow", &saving[0][0], sizeof(vramShadow));
    } else {
        xgStateIo(s, "vramShadow", &vramShadow[0][0], sizeof(vramShadow));
    }
    XG_STATE_VAR(s, renderedTiles);
    /* The CPU-owned tile hints (vramCpuOwned: where the CPU read after the
     * frame drew, the next frames draw on the CPU) choose the drawing path
     * of the frames after: the pictures are the same either way, the
     * texture cache's walks are not (now or deferred), so a run loaded here
     * followed the other path and its cache words differed from the
     * continuous run's (xg-hbc.21: the battle's ClearImage compares). */
    XG_STATE_VAR(s, owned.now);
    XG_STATE_VAR(s, owned.written);
    XG_STATE_VAR(s, owned.read);
    XG_STATE_VAR(s, generations);
    XG_STATE_VAR(s, generation);
    XG_STATE_VAR(s, uploads);
    XG_STATE_VAR(s, uploadNext);
    XG_STATE_VAR(s, captures);
    XG_STATE_VAR(s, captureAge);
    for (int i = 0; i < 2; ++i) {
        size_t count = xgStateLen(s, i ? "capture1Size" : "capture0Size", captureSize[i]);
        /* A damaged length must fail the state's size check, never exceed
         * storage, including in the checking pass before a load. */
        if (count > VRAM_W * VRAM_H) count = 0;
        xgStateIo(s, i ? "capture1Pixels" : "capture0Pixels", capturePixels[i], count * sizeof(uint16_t));
        if (xgStateLoading(s)) captureSize[i] = count;
    }
    if (xgStateLoading(s)) {
        tilesClear(&drawnTiles);
        tilesClear(&cpuWritten);
        memset(stamps, 0, sizeof(stamps));
        tilesClear(&stampedTiles);
        memset(stampTiles, 0, sizeof(stampTiles)); /* their summaries were of the stamps just dropped */
        /* the newest generation per block, from the loaded tiles' (vramBlockGeneration) */
        memset(blocks, 0, sizeof(blocks));
        for (int y = 0; y < TILES_Y; y++)
            for (int x = 0; x < TILES_X; x++)
                if (generations[y][x] > blocks[y >> VRAM_BLOCK_SHIFT][x >> VRAM_BLOCK_SHIFT])
                    blocks[y >> VRAM_BLOCK_SHIFT][x >> VRAM_BLOCK_SHIFT] = generations[y][x];
        for (int i = 0; i < PAGE_SLOTS; ++i) {
            free(pageCache[i]);
            pageCache[i] = NULL;
        }
        vramLayoutSerial++;
    }
}

/* The display's pixels as the snapshot has them (savestate.c's thumbnail). */
const uint16_t* vramSaved(void) {
    return &saving[0][0];
}
