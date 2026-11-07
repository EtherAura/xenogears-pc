/*
 * A GPU's primitives drawn on the CPU for exports (maprast.h).
 */

#include "maprast.h"

#include <math.h>
#include <string.h>

typedef struct {
    float x, y, u, v, r, g, b;
} Vert;

typedef struct {
    const uint16_t* vram;
    int textured, raw, freeUv, depth, semi, abr, textures;
    int pageX, pageY, clutX, clutY;
    uint32_t window;
    float area[4]; /* scene units: x0, y0, x1, y1 (exclusive ends) */
    /* the picture */
    uint32_t* rgba;
    int w, h;
    float x0, y0, sx, sy; /* scene = x0 + (pixel + 0.5) * sx */
} Ctx;

static uint16_t fetch(const Ctx* c, int x, int y) {
    return c->vram[(y & (XG_VRAM_H - 1)) * XG_VRAM_W + (x & (XG_VRAM_W - 1))];
}

static float clamp1(float v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* One pixel of a primitive: what view.frag does, then the blend. */
static void shade(const Ctx* c, uint32_t* dst, float u, float v, float r, float g, float b) {
    int stp = c->semi;
    float cr = r, cg = g, cb = b;
    if (c->textured && c->textures) {
        int tx = (int)floorf(u), ty = (int)floorf(v);
        if (!c->freeUv) {
            uint32_t w = c->window;
            uint32_t mx = w & 31, my = (w >> 5) & 31, ox = (w >> 10) & 31, oy = (w >> 15) & 31;
            tx &= 255;
            ty &= 255;
            tx = (int)(((uint32_t)tx & ~(mx * 8)) | ((ox & mx) * 8));
            ty = (int)(((uint32_t)ty & ~(my * 8)) | ((oy & my) * 8));
        }
        uint16_t texel;
        if (c->depth == 0) {
            uint16_t w = fetch(c, c->pageX + (tx >> 2), c->pageY + ty);
            texel = fetch(c, c->clutX + ((w >> ((tx & 3) * 4)) & 15), c->clutY);
        } else if (c->depth == 1) {
            uint16_t w = fetch(c, c->pageX + (tx >> 1), c->pageY + ty);
            texel = fetch(c, c->clutX + ((w >> ((tx & 1) * 8)) & 255), c->clutY);
        } else {
            texel = fetch(c, c->pageX + tx, c->pageY + ty);
        }
        if (texel == 0) {
            return;
        }
        stp = c->semi && (texel & 0x8000) != 0;
        int c5[3] = {texel & 31, (texel >> 5) & 31, (texel >> 10) & 31};
        float t[3];
        for (int k = 0; k < 3; k++) {
            t[k] = (float)((c5[k] << 3) | (c5[k] >> 2)) / 255.0f;
        }
        if (c->raw) {
            cr = t[0], cg = t[1], cb = t[2];
        } else {
            cr = clamp1(t[0] * r * (255.0f / 128.0f));
            cg = clamp1(t[1] * g * (255.0f / 128.0f));
            cb = clamp1(t[2] * b * (255.0f / 128.0f));
        }
    } else if (c->textured) {
        cr = clamp1(r * (255.0f / 128.0f));
        cg = clamp1(g * (255.0f / 128.0f));
        cb = clamp1(b * (255.0f / 128.0f));
        stp = 0;
    }
    uint32_t d = *dst;
    float dr = (float)(d & 255) / 255.0f, dg = (float)(d >> 8 & 255) / 255.0f, db = (float)(d >> 16 & 255) / 255.0f;
    if (stp) {
        switch (c->abr) {
        case 0:
            cr = 0.5f * dr + 0.5f * cr, cg = 0.5f * dg + 0.5f * cg, cb = 0.5f * db + 0.5f * cb;
            break;
        case 1:
            cr = clamp1(dr + cr), cg = clamp1(dg + cg), cb = clamp1(db + cb);
            break;
        case 2:
            cr = clamp1(dr - cr), cg = clamp1(dg - cg), cb = clamp1(db - cb);
            break;
        default:
            cr = clamp1(dr + 0.25f * cr), cg = clamp1(dg + 0.25f * cg), cb = clamp1(db + 0.25f * cb);
            break;
        }
    }
    *dst = (uint32_t)lrintf(cr * 255.0f) | (uint32_t)lrintf(cg * 255.0f) << 8 | (uint32_t)lrintf(cb * 255.0f) << 16 |
           (d & 0xFF000000u);
}

static float edge(const Vert* a, const Vert* b, float x, float y) {
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

/* Shared edges belong to one triangle, as in the view's Vulkan raster.
 * Including both sides blends the diagonal of a transparent quad twice. */
static int topLeft(const Vert* a, const Vert* b, float sign) {
    float dx = (b->x - a->x) * sign, dy = (b->y - a->y) * sign;
    return dy < 0 || (dy == 0 && dx > 0);
}

static void triangle(const Ctx* c, const Vert* a, const Vert* b, const Vert* d) {
    float area = edge(a, b, d->x, d->y);
    if (fabsf(area) < 1e-6f) {
        return;
    }
    float minX = fminf(a->x, fminf(b->x, d->x)), maxX = fmaxf(a->x, fmaxf(b->x, d->x));
    float minY = fminf(a->y, fminf(b->y, d->y)), maxY = fmaxf(a->y, fmaxf(b->y, d->y));
    minX = fmaxf(minX, c->area[0]);
    minY = fmaxf(minY, c->area[1]);
    maxX = fminf(maxX, c->area[2]);
    maxY = fminf(maxY, c->area[3]);
    int px0 = (int)floorf((minX - c->x0) / c->sx - 0.5f), px1 = (int)ceilf((maxX - c->x0) / c->sx - 0.5f);
    int py0 = (int)floorf((minY - c->y0) / c->sy - 0.5f), py1 = (int)ceilf((maxY - c->y0) / c->sy - 0.5f);
    px0 = px0 < 0 ? 0 : px0;
    py0 = py0 < 0 ? 0 : py0;
    px1 = px1 >= c->w ? c->w - 1 : px1;
    py1 = py1 >= c->h ? c->h - 1 : py1;
    float inv = 1.0f / area;
    float sign = area > 0 ? 1.0f : -1.0f;
    int e0 = topLeft(b, d, sign), e1 = topLeft(d, a, sign), e2 = topLeft(a, b, sign);
    for (int py = py0; py <= py1; py++) {
        float y = c->y0 + ((float)py + 0.5f) * c->sy;
        if (y < c->area[1] || y >= c->area[3]) {
            continue;
        }
        for (int px = px0; px <= px1; px++) {
            float x = c->x0 + ((float)px + 0.5f) * c->sx;
            if (x < c->area[0] || x >= c->area[2]) {
                continue;
            }
            float w0 = edge(b, d, x, y) * inv, w1 = edge(d, a, x, y) * inv, w2 = edge(a, b, x, y) * inv;
            if (w0 < 0 || w1 < 0 || w2 < 0 || (w0 == 0 && !e0) || (w1 == 0 && !e1) || (w2 == 0 && !e2)) {
                continue;
            }
            shade(c, &c->rgba[py * c->w + px], w0 * a->u + w1 * b->u + w2 * d->u, w0 * a->v + w1 * b->v + w2 * d->v,
                  w0 * a->r + w1 * b->r + w2 * d->r, w0 * a->g + w1 * b->g + w2 * d->g,
                  w0 * a->b + w1 * b->b + w2 * d->b);
        }
    }
}

static void vertex(Vert* v, const XgPrim* p, int k) {
    int col = (p->flags & XG_PRIM_GOURAUD) ? k : 0;
    v->x = p->x[k];
    v->y = p->y[k];
    v->u = p->u[k];
    v->v = p->v[k];
    v->r = (float)p->r[col] / 255.0f;
    v->g = (float)p->g[col] / 255.0f;
    v->b = (float)p->b[col] / 255.0f;
}

void xgRasterize(const XgPrim* prims, int count, const uint16_t* vram, float x0, float y0, float x1, float y1,
                 int w, int h, uint32_t background, int textures, uint32_t* rgba) {
    for (int i = 0; i < w * h; i++) {
        rgba[i] = background;
    }
    if (w <= 0 || h <= 0 || x1 <= x0 || y1 <= y0) {
        return;
    }
    Ctx c;
    memset(&c, 0, sizeof(c));
    c.vram = vram;
    c.rgba = rgba;
    c.w = w;
    c.h = h;
    c.x0 = x0;
    c.y0 = y0;
    c.sx = (x1 - x0) / (float)w;
    c.sy = (y1 - y0) / (float)h;
    c.textures = textures && vram != NULL;
    for (int i = 0; i < count; i++) {
        const XgPrim* p = &prims[i];
        c.textured = (p->flags & XG_PRIM_TEXTURED) != 0;
        c.raw = (p->flags & XG_PRIM_RAW) != 0;
        c.freeUv = (p->flags & XG_PRIM_FREE_UV) != 0;
        c.depth = p->tpage >> 7 & 3;
        c.semi = (p->flags & XG_PRIM_SEMI) != 0;
        c.abr = p->tpage >> 5 & 3;
        c.pageX = (p->tpage & 15) * 64;
        c.pageY = (p->tpage >> 4 & 1) * 256;
        c.clutX = (p->clut & 63) * 16;
        c.clutY = p->clut >> 6 & 511;
        c.window = p->window & 0xFFFFF;
        if (p->flags & XG_PRIM_FILL) {
            c.area[0] = 0, c.area[1] = 0, c.area[2] = XG_VRAM_W, c.area[3] = XG_VRAM_H;
        } else {
            c.area[0] = p->area[0], c.area[1] = p->area[1];
            c.area[2] = (float)p->area[2] + 1, c.area[3] = (float)p->area[3] + 1;
        }
        if (p->flags & XG_PRIM_LINE) {
            /* one scene unit wide, through the pixel centres (as the views) */
            float ax = p->x[0] + 0.5f, ay = p->y[0] + 0.5f, bx = p->x[1] + 0.5f, by = p->y[1] + 0.5f;
            float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
            float nx = len > 0 ? -dy / len * 0.5f : 0, ny = len > 0 ? dx / len * 0.5f : 0.5f;
            Vert q[4];
            float px[4] = {ax + nx, ax - nx, bx + nx, bx - nx}, py[4] = {ay + ny, ay - ny, by + ny, by - ny};
            for (int k = 0; k < 4; k++) {
                q[k].x = px[k];
                q[k].y = py[k];
                q[k].u = q[k].v = 0;
                q[k].r = (float)p->r[0] / 255.0f;
                q[k].g = (float)p->g[0] / 255.0f;
                q[k].b = (float)p->b[0] / 255.0f;
            }
            c.textured = 0;
            triangle(&c, &q[0], &q[1], &q[2]);
            triangle(&c, &q[1], &q[2], &q[3]);
            continue;
        }
        if (p->count != 3 && p->count != 4) {
            continue;
        }
        Vert v[4];
        for (int k = 0; k < p->count; k++) {
            vertex(&v[k], p, k);
        }
        triangle(&c, &v[0], &v[1], &v[2]);
        if (p->count == 4) {
            triangle(&c, &v[1], &v[2], &v[3]);
        }
    }
}
