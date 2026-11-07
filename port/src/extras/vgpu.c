/*
 * The sandbox's GPU (vgpu.h): VRAM of its own and the primitives the game's
 * packets describe, read as the PS1 GPU reads its GP0 commands (psx-spx
 * "GPU Render Polygon/Line/Rectangle Commands", "GPU Rendering Attributes",
 * "GPU Memory Transfer Commands").
 */

#include "vgpu.h"

#include <stdlib.h>
#include <string.h>

#define MAX_PRIMS (1 << 20)
#define UPLOADS 64 /* as many as the game's own (port/src/gpu/vram.c) */

struct XgVgpu {
    uint16_t* vram;
    uint64_t identity;
    unsigned version;
    XgPrim* prims;
    int primCount, primCap;
    /* Drawing state (GP0 E1h-E6h). */
    uint16_t texpage;   /* E1h bits 0-13 */
    uint32_t window;    /* E2h bits 0-19 */
    int16_t area[4];
    int16_t ofsX, ofsY;
    GpuCapture capture;
    /* The latest loads (xgVgpuUploadCover), each until something writes into it. */
    struct { int16_t x0, y0, x1, y1; uint8_t intact; } uploads[UPLOADS];
    unsigned uploadNext;
};

/* ---- VRAM ---------------------------------------------------------------------------- */

static uint16_t* at(XgVgpu* g, int x, int y) {
    return &g->vram[(y & (XG_VRAM_H - 1)) * XG_VRAM_W + (x & (XG_VRAM_W - 1))];
}

/* A write into x, y, w, h: the loads it overlaps are no longer whole (wrapping writes too). */
static void overwrite(XgVgpu* g, int x, int y, int w, int h) {
    int x1 = x + w - 1, y1 = y + h - 1;
    for (unsigned i = 0; i < UPLOADS; i++) {
        if (!g->uploads[i].intact) continue;
        if (x1 >= XG_VRAM_W || y1 >= XG_VRAM_H ||
            !(g->uploads[i].x0 > x1 || g->uploads[i].x1 < x || g->uploads[i].y0 > y1 || g->uploads[i].y1 < y))
            g->uploads[i].intact = 0;
    }
}

static void loaded(XgVgpu* g, int x, int y, int w, int h) {
    overwrite(g, x, y, w, h);
    if (w < 1 || h < 1 || x + w > XG_VRAM_W || y + h > XG_VRAM_H) return;
    unsigned i = g->uploadNext;
    g->uploadNext = (g->uploadNext + 1) % UPLOADS;
    g->uploads[i].x0 = (int16_t)x; g->uploads[i].y0 = (int16_t)y;
    g->uploads[i].x1 = (int16_t)(x + w - 1); g->uploads[i].y1 = (int16_t)(y + h - 1);
    g->uploads[i].intact = 1;
}

static void load(XgVgpu* g, int x, int y, int w, int h, const uint16_t* src) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            *at(g, x + c, y + r) = src[r * w + c];
        }
    }
    g->version++;
}

void xgVgpuLoad(XgVgpu* g, int x, int y, int w, int h, const uint16_t* src) {
    load(g, x, y, w, h, src);
    loaded(g, x, y, w, h);
}

int xgVgpuUploadCover(const XgVgpu* g, int x0, int y0, int x1, int y1, int* out) {
    for (unsigned n = 0; n < UPLOADS; n++) {
        unsigned i = (g->uploadNext + UPLOADS - 1 - n) % UPLOADS;
        if (!g->uploads[i].intact || g->uploads[i].x0 > x0 || g->uploads[i].y0 > y0 || g->uploads[i].x1 < x1 ||
            g->uploads[i].y1 < y1) continue;
        out[0] = g->uploads[i].x0; out[1] = g->uploads[i].y0; out[2] = g->uploads[i].x1; out[3] = g->uploads[i].y1;
        return 1;
    }
    return 0;
}

void xgVgpuStore(XgVgpu* g, int x, int y, int w, int h, uint16_t* dst) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            dst[r * w + c] = *at(g, x + c, y + r);
        }
    }
}

static void move(XgVgpu* g, int sx, int sy, int dx, int dy, int w, int h) {
    uint16_t* tmp = malloc((size_t)w * h * 2);
    if (tmp == NULL) {
        return;
    }
    xgVgpuStore(g, sx, sy, w, h, tmp);
    load(g, dx, dy, w, h, tmp);
    loaded(g, dx, dy, w, h); /* a clean copy is a load (vram.c vramMarkCopied) */
    free(tmp);
}

static void fill(XgVgpu* g, int x, int y, int w, int h, uint32_t rgb) {
    uint16_t c = (uint16_t)((rgb >> 3 & 0x1F) | (rgb >> 11 & 0x1F) << 5 | (rgb >> 19 & 0x1F) << 10);
    for (int r = 0; r < h; r++) {
        for (int i = 0; i < w; i++) {
            *at(g, x + i, y + r) = c;
        }
    }
    g->version++;
    overwrite(g, x, y, w, h);
}

uint16_t* xgVgpuVram(XgVgpu* g) {
    return g->vram;
}

unsigned xgVgpuVramVersion(const XgVgpu* g) {
    return g->version;
}

uint64_t xgVgpuIdentity(const XgVgpu* g) {
    return g->identity;
}

void xgVgpuVramChanged(XgVgpu* g) {
    g->version++;
}

void xgVgpuClearVram(XgVgpu* g) {
    memset(g->vram, 0, XG_VRAM_W * XG_VRAM_H * 2);
    g->version++;
    memset(g->uploads, 0, sizeof(g->uploads));
}

/* ---- primitives ------------------------------------------------------------------------ */

void xgVgpuAddPrim(XgVgpu* g, const XgPrim* p) {
    if (g->primCount == g->primCap) {
        if (g->primCap >= MAX_PRIMS) {
            return;
        }
        int cap = g->primCap ? g->primCap * 2 : 1024;
        XgPrim* n = realloc(g->prims, (size_t)cap * sizeof(XgPrim));
        if (n == NULL) {
            return;
        }
        g->prims = n;
        g->primCap = cap;
    }
    g->prims[g->primCount++] = *p;
}

int xgVgpuPrimCount(const XgVgpu* g) {
    return g->primCount;
}

const XgPrim* xgVgpuPrims(const XgVgpu* g) {
    return g->prims;
}

void xgVgpuClearPrims(XgVgpu* g) {
    g->primCount = 0;
}

void xgVgpuSetArea(XgVgpu* g, int x0, int y0, int x1, int y1, int ofsX, int ofsY) {
    g->area[0] = (int16_t)x0;
    g->area[1] = (int16_t)y0;
    g->area[2] = (int16_t)x1;
    g->area[3] = (int16_t)y1;
    g->ofsX = (int16_t)ofsX;
    g->ofsY = (int16_t)ofsY;
}

/* ---- GP0 ------------------------------------------------------------------------------- */

static int sext11(uint32_t v) {
    return (int)((int32_t)(v << 21) >> 21);
}

static void startPrim(XgVgpu* g, XgPrim* p, uint8_t flags) {
    memset(p, 0, sizeof(*p));
    p->flags = flags;
    p->tpage = g->texpage & 0x1FF;
    p->window = g->window;
    memcpy(p->area, g->area, sizeof(p->area));
}

static void vertex(XgVgpu* g, XgPrim* p, int i, uint32_t xy) {
    p->x[i] = (int16_t)(sext11(xy) + g->ofsX);
    p->y[i] = (int16_t)(sext11(xy >> 16) + g->ofsY);
}

static void color(XgPrim* p, int i, uint32_t c) {
    p->r[i] = (uint8_t)c;
    p->g[i] = (uint8_t)(c >> 8);
    p->b[i] = (uint8_t)(c >> 16);
}

/* GP0(20h-3Fh). Returns the words it takes. */
static int polygon(XgVgpu* g, const uint32_t* w, int avail) {
    uint32_t cmd = w[0] >> 24;
    int gouraud = (cmd & 0x10) != 0, quad = (cmd & 0x08) != 0, textured = (cmd & 0x04) != 0;
    int n = quad ? 4 : 3;
    int len = 1 + n * (1 + textured) + (gouraud ? n - 1 : 0);
    if (len > avail) {
        return avail;
    }
    XgPrim p;
    startPrim(g, &p, (uint8_t)((textured ? XG_PRIM_TEXTURED : 0) | (cmd & 0x02 ? XG_PRIM_SEMI : 0) |
                               (cmd & 0x01 ? XG_PRIM_RAW : 0) | (gouraud ? XG_PRIM_GOURAUD : 0)));
    p.count = (uint8_t)n;
    int k = 0;
    for (int i = 0; i < n; i++) {
        color(&p, i, gouraud && i > 0 ? w[k++] : w[0]);
        if (i == 0) {
            k++;
        }
        vertex(g, &p, i, w[k++]);
        if (textured) {
            uint32_t uv = w[k++];
            p.u[i] = (int16_t)(uv & 0xFF);
            p.v[i] = (int16_t)(uv >> 8 & 0xFF);
            if (i == 0) {
                p.clut = (uint16_t)(uv >> 16);
            } else if (i == 1) {
                /* The polygon's page goes into the GPU's texpage register too. */
                g->texpage = (uint16_t)((g->texpage & ~0x9FFu) | (uv >> 16 & 0x9FF));
                p.tpage = (uint16_t)(uv >> 16 & 0x1FF);
            }
        }
    }
    xgVgpuAddPrim(g, &p);
    return len;
}

/* GP0(40h-5Fh): single lines and polylines (ended by 5xxx5xxxh). */
static int line(XgVgpu* g, const uint32_t* w, int avail) {
    uint32_t cmd = w[0] >> 24;
    int gouraud = (cmd & 0x10) != 0, poly = (cmd & 0x08) != 0;
    XgPrim p;
    startPrim(g, &p, (uint8_t)(XG_PRIM_LINE | (cmd & 0x02 ? XG_PRIM_SEMI : 0) | (gouraud ? XG_PRIM_GOURAUD : 0)));
    p.count = 2;
    color(&p, 0, w[0]);
    if (avail < 2) {
        return avail;
    }
    vertex(g, &p, 0, w[1]);
    int k = 2;
    for (;;) {
        uint32_t c = w[0];
        if (gouraud) {
            if (k >= avail || (poly && (w[k] & 0xF000F000u) == 0x50005000u)) {
                break;
            }
            c = w[k++];
        }
        if (k >= avail || (poly && (w[k] & 0xF000F000u) == 0x50005000u)) {
            break;
        }
        color(&p, 1, c);
        vertex(g, &p, 1, w[k++]);
        xgVgpuAddPrim(g, &p);
        if (!poly) {
            return k;
        }
        p.x[0] = p.x[1];
        p.y[0] = p.y[1];
        p.r[0] = p.r[1];
        p.g[0] = p.g[1];
        p.b[0] = p.b[1];
    }
    return k < avail ? k + 1 : avail; /* the terminator */
}

/* GP0(60h-7Fh): rectangles (sprites and tiles), drawn as quads. */
static int rect(XgVgpu* g, const uint32_t* w, int avail) {
    uint32_t cmd = w[0] >> 24;
    int textured = (cmd & 0x04) != 0, size = (cmd >> 3) & 3;
    int len = 2 + textured + (size == 0);
    if (len > avail) {
        return avail;
    }
    XgPrim p;
    startPrim(g, &p, (uint8_t)((textured ? XG_PRIM_TEXTURED : 0) | (cmd & 0x02 ? XG_PRIM_SEMI : 0) |
                               (cmd & 0x01 ? XG_PRIM_RAW : 0)));
    p.count = 4;
    int x = sext11(w[1]) + g->ofsX, y = sext11(w[1] >> 16) + g->ofsY;
    int u = 0, v = 0, wd, ht;
    if (textured) {
        u = (int)(w[2] & 0xFF);
        v = (int)(w[2] >> 8 & 0xFF);
        p.clut = (uint16_t)(w[2] >> 16);
    }
    if (size == 0) {
        uint32_t wh = w[2 + textured];
        wd = (int)(wh & 0x3FF);
        ht = (int)(wh >> 16 & 0x1FF);
    } else {
        wd = ht = size == 1 ? 1 : size == 2 ? 8 : 16;
    }
    /* Texpage bits 12, 13: the rectangle's texture flipped in x, y. */
    int du = g->texpage & 0x1000 ? -wd : wd, dv = g->texpage & 0x2000 ? -ht : ht;
    for (int i = 0; i < 4; i++) {
        color(&p, i, w[0]);
        p.x[i] = (int16_t)(x + (i & 1 ? wd : 0));
        p.y[i] = (int16_t)(y + (i & 2 ? ht : 0));
        p.u[i] = (int16_t)(u + (i & 1 ? du : 0));
        p.v[i] = (int16_t)(v + (i & 2 ? dv : 0));
    }
    xgVgpuAddPrim(g, &p);
    return len;
}

void xgVgpuGp0(XgVgpu* g, const uint32_t* w, int count) {
    int i = 0;
    while (i < count) {
        uint32_t word = w[i];
        uint32_t cmd = word >> 24;
        int avail = count - i;
        if (cmd >= 0x20 && cmd < 0x40) {
            i += polygon(g, w + i, avail);
        } else if (cmd >= 0x40 && cmd < 0x60) {
            i += line(g, w + i, avail);
        } else if (cmd >= 0x60 && cmd < 0x80) {
            i += rect(g, w + i, avail);
        } else if (cmd == 0x02) {
            if (avail < 3) {
                return;
            }
            int x = (int)(w[i + 1] & 0x3F0), y = (int)(w[i + 1] >> 16 & 0x1FF);
            int wd = (int)(((w[i + 2] & 0x3FF) + 0xF) & ~0xFu), ht = (int)(w[i + 2] >> 16 & 0x1FF);
            fill(g, x, y, wd, ht, word & 0xFFFFFF);
            XgPrim p;
            startPrim(g, &p, XG_PRIM_FILL);
            p.count = 4;
            for (int k = 0; k < 4; k++) {
                color(&p, k, word);
                p.x[k] = (int16_t)(x + (k & 1 ? wd : 0));
                p.y[k] = (int16_t)(y + (k & 2 ? ht : 0));
            }
            xgVgpuAddPrim(g, &p);
            i += 3;
        } else if (cmd >= 0x80 && cmd < 0xA0) {
            if (avail < 4) {
                return;
            }
            int wd = (int)(((w[i + 3] & 0xFFFF) - 1) & 0x3FF) + 1, ht = (int)(((w[i + 3] >> 16) - 1) & 0x1FF) + 1;
            move(g, (int)(w[i + 1] & 0x3FF), (int)(w[i + 1] >> 16 & 0x1FF), (int)(w[i + 2] & 0x3FF),
                 (int)(w[i + 2] >> 16 & 0x1FF), wd, ht);
            i += 4;
        } else if (cmd >= 0xA0 && cmd < 0xC0) {
            if (avail < 3) {
                return;
            }
            int wd = (int)(((w[i + 2] & 0xFFFF) - 1) & 0x3FF) + 1, ht = (int)(((w[i + 2] >> 16) - 1) & 0x1FF) + 1;
            int words = (wd * ht + 1) / 2;
            if (avail < 3 + words) {
                return;
            }
            xgVgpuLoad(g, (int)(w[i + 1] & 0x3FF), (int)(w[i + 1] >> 16 & 0x1FF), wd, ht,
                       (const uint16_t*)(w + i + 3));
            i += 3 + words;
        } else if (cmd >= 0xC0 && cmd < 0xE0) {
            i += 3;
        } else if (cmd == 0xE1) {
            g->texpage = (uint16_t)(word & 0x3FFF);
            i++;
        } else if (cmd == 0xE2) {
            g->window = word & 0xFFFFF;
            i++;
        } else if (cmd == 0xE3) {
            g->area[0] = (int16_t)(word & 0x3FF);
            g->area[1] = (int16_t)(word >> 10 & 0x3FF);
            i++;
        } else if (cmd == 0xE4) {
            g->area[2] = (int16_t)(word & 0x3FF);
            g->area[3] = (int16_t)(word >> 10 & 0x3FF);
            i++;
        } else if (cmd == 0xE5) {
            g->ofsX = (int16_t)sext11(word);
            g->ofsY = (int16_t)sext11(word >> 11);
            i++;
        } else {
            i++; /* NOPs, cache clear, mask bits (not kept), interrupt request */
        }
    }
}

/* ---- the capture ----------------------------------------------------------------------- */

static void capLoad(void* user, int x, int y, int w, int h, const uint16_t* src) {
    xgVgpuLoad(user, x, y, w, h, src);
}

static void capStore(void* user, int x, int y, int w, int h, uint16_t* dst) {
    xgVgpuStore(user, x, y, w, h, dst);
}

static void capMove(void* user, int sx, int sy, int dx, int dy, int w, int h) {
    move(user, sx, sy, dx, dy, w, h);
}

static void capFill(void* user, int x, int y, int w, int h, uint32_t rgb) {
    fill(user, x, y, w, h, rgb);
}

static void capGp0(void* user, const uint32_t* words, int count) {
    xgVgpuGp0(user, words, count);
}

const GpuCapture* xgVgpuCapture(XgVgpu* g) {
    return &g->capture;
}

XgVgpu* xgVgpuCreate(void) {
    static uint64_t nextIdentity = 1;
    XgVgpu* g = calloc(1, sizeof(XgVgpu));
    if (g == NULL) {
        return NULL;
    }
    g->vram = calloc(XG_VRAM_W * XG_VRAM_H, 2);
    if (g->vram == NULL) {
        free(g);
        return NULL;
    }
    g->identity = nextIdentity++;
    g->area[2] = XG_VRAM_W - 1;
    g->area[3] = XG_VRAM_H - 1;
    g->capture.user = g;
    g->capture.load = capLoad;
    g->capture.store = capStore;
    g->capture.move = capMove;
    g->capture.fill = capFill;
    g->capture.gp0 = capGp0;
    return g;
}

void xgVgpuDestroy(XgVgpu* g) {
    if (g != NULL) {
        free(g->vram);
        free(g->prims);
        free(g);
    }
}
