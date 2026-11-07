#include "raster.h"
#include <math.h>
#include <stddef.h>

/* A texel as view.frag reads it: the page and CLUT from VRAM, the texture
 * window, 4/8/15-bit. */
static uint16_t texel(const uint16_t *vram, const XgPrim *p, int u, int v) {
    if (!(p->flags & XG_PRIM_FREE_UV)) {
        u &= 255;
        v &= 255;
        uint32_t w = p->window;
        uint32_t mx = w & 31, my = (w >> 5) & 31, ox = (w >> 10) & 31, oy = (w >> 15) & 31;
        u = (int)(((uint32_t)u & ~(mx * 8)) | ((ox & mx) * 8));
        v = (int)(((uint32_t)v & ~(my * 8)) | ((oy & my) * 8));
    }
    int px = (p->tpage & 15) * 64, py = ((p->tpage >> 4) & 1) * 256;
    int cx = (p->clut & 63) * 16, cy = (p->clut >> 6) & 511;
    int depth = (p->tpage >> 7) & 3;
#define VR(x, y) vram[((y) & 511) * XG_VRAM_W + ((x) & 1023)]
    if (depth == 0) {
        uint16_t w = VR(px + (u >> 2), py + v);
        return VR(cx + ((w >> ((u & 3) * 4)) & 15), cy);
    }
    if (depth == 1) {
        uint16_t w = VR(px + (u >> 1), py + v);
        return VR(cx + ((w >> ((u & 1) * 8)) & 255), cy);
    }
    return VR(px + u, py + v);
#undef VR
}

static int wide5(int c) {
    return c << 3 | c >> 2;
}

typedef struct {
    double x, y, u, v, r, g, b;
} RVert;

/* A pixel of a primitive into the picture (view.frag, then the blend). */
static void plot(const XgPrim *p, const uint16_t *vram, double u, double v, double r, double g, double b,
                 uint32_t *dst, int transparentBg) {
    int semi = (p->flags & XG_PRIM_SEMI) != 0, stp = semi;
    double cr = r, cg = g, cb = b;
    if (p->flags & XG_PRIM_TEXTURED) {
        uint16_t t = texel(vram, p, (int)floor(u), (int)floor(v));
        if (t == 0) {
            return;
        }
        stp = semi && (t & 0x8000);
        double tr = wide5(t & 31), tg = wide5((t >> 5) & 31), tb = wide5((t >> 10) & 31);
        if (p->flags & XG_PRIM_RAW) {
            cr = tr, cg = tg, cb = tb;
        } else {
            cr = fmin(tr * r / 128.0, 255.0);
            cg = fmin(tg * g / 128.0, 255.0);
            cb = fmin(tb * b / 128.0, 255.0);
        }
    }
    int R = (int)(cr + 0.5), G = (int)(cg + 0.5), B = (int)(cb + 0.5);
    uint32_t d = *dst;
    int da = (int)(d >> 24);
    if (stp) {
        int abr = (p->tpage >> 5) & 3;
        if (da == 0 && transparentBg) {
            /* Over nothing: the colour at half strength (an approximation:
             * a PNG has no additive or subtractive blend). */
            *dst = (uint32_t)R | (uint32_t)G << 8 | (uint32_t)B << 16 | 0x80u << 24;
            return;
        }
        int br = (int)(d & 255), bg = (int)(d >> 8 & 255), bb = (int)(d >> 16 & 255);
        switch (abr) {
        case 0:
            R = (br + R) / 2, G = (bg + G) / 2, B = (bb + B) / 2;
            break;
        case 1:
            R = br + R, G = bg + G, B = bb + B;
            break;
        case 2:
            R = br - R, G = bg - G, B = bb - B;
            break;
        default:
            R = br + R / 4, G = bg + G / 4, B = bb + B / 4;
            break;
        }
        R = R < 0 ? 0 : R > 255 ? 255 : R;
        G = G < 0 ? 0 : G > 255 ? 255 : G;
        B = B < 0 ? 0 : B > 255 ? 255 : B;
        da = da > 0x80 ? da : 0x80;
        *dst = (uint32_t)R | (uint32_t)G << 8 | (uint32_t)B << 16 | (uint32_t)(da == 0 ? 255 : da) << 24;
        return;
    }
    *dst = (uint32_t)R | (uint32_t)G << 8 | (uint32_t)B << 16 | 0xFFu << 24;
}

/* A triangle: the pixels whose centres it covers (edges shared by two
 * triangles drawn once: top and left edges in, as the GPU's own rule keeps
 * the right and bottom ones out), attributes interpolated affinely at the
 * centres, as the views' rasteriser does. */
static void triangle(const XgPrim *p, const uint16_t *vram, const RVert *a, const RVert *b, const RVert *c,
                     uint32_t *out, int w, int h, int transparentBg, const double *clip) {
    double area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        const RVert *t = b;
        b = c;
        c = t;
        area = -area;
    }
    int minX = (int)floor(fmin(a->x, fmin(b->x, c->x))), maxX = (int)ceil(fmax(a->x, fmax(b->x, c->x)));
    int minY = (int)floor(fmin(a->y, fmin(b->y, c->y))), maxY = (int)ceil(fmax(a->y, fmax(b->y, c->y)));
    minX = minX < 0 ? 0 : minX;
    minY = minY < 0 ? 0 : minY;
    maxX = maxX > w ? w : maxX;
    maxY = maxY > h ? h : maxY;
    minX = fmax(minX, ceil(clip[0] - 0.5));
    minY = fmax(minY, ceil(clip[1] - 0.5));
    maxX = fmin(maxX, ceil(clip[2] - 0.5));
    maxY = fmin(maxY, ceil(clip[3] - 0.5));
    const RVert *v[3] = {a, b, c};
    for (int y = minY; y < maxY; y++) {
        for (int x = minX; x < maxX; x++) {
            double px = x + 0.5, py = y + 0.5;
            double l[3];
            int inside = 1;
            for (int e = 0; e < 3 && inside; e++) {
                const RVert *p0 = v[(e + 1) % 3];
                const RVert *p1 = v[(e + 2) % 3];
                double ex = p1->x - p0->x, ey = p1->y - p0->y;
                double f = ex * (py - p0->y) - ey * (px - p0->x);
                /* top-left rule (y down, clockwise area positive) */
                int topLeft = (ey == 0 && ex < 0) || ey > 0;
                if (f < 0 || (f == 0 && !topLeft)) {
                    inside = 0;
                }
                l[e] = f / area;
            }
            if (!inside) {
                continue;
            }
            double u = l[0] * a->u + l[1] * b->u + l[2] * c->u;
            double vv = l[0] * a->v + l[1] * b->v + l[2] * c->v;
            double r = l[0] * a->r + l[1] * b->r + l[2] * c->r;
            double g = l[0] * a->g + l[1] * b->g + l[2] * c->g;
            double bl = l[0] * a->b + l[1] * b->b + l[2] * c->b;
            plot(p, vram, u, vv, r, g, bl, &out[(size_t)y * w + x], transparentBg);
        }
    }
}

void xgVgpuRaster(XgVgpu *gpu, int x0, int y0, int w, int h, double k, uint32_t bg, uint32_t *rgba) {
    const uint16_t *vram = xgVgpuVram(gpu);
    const XgPrim *prims = xgVgpuPrims(gpu);
    int n = xgVgpuPrimCount(gpu);
    for (int i = 0; i < w * h; i++) {
        rgba[i] = bg;
    }
    for (int i = 0; i < n; i++) {
        const XgPrim *p = &prims[i];
        double clip[4] = {0, 0, w, h};
        if (!(p->flags & XG_PRIM_FILL)) {
            clip[0] = (p->area[0] - x0) * k;
            clip[1] = (p->area[1] - y0) * k;
            clip[2] = (p->area[2] + 1 - x0) * k;
            clip[3] = (p->area[3] + 1 - y0) * k;
        }
        RVert v[4];
        for (int j = 0; j < p->count && j < 4; j++) {
            int c = (p->flags & XG_PRIM_GOURAUD) ? j : 0;
            v[j].x = (p->x[j] - x0) * k;
            v[j].y = (p->y[j] - y0) * k;
            v[j].u = p->u[j];
            v[j].v = p->v[j];
            v[j].r = p->r[c];
            v[j].g = p->g[c];
            v[j].b = p->b[c];
        }
        if (p->flags & XG_PRIM_LINE) {
            double dx = v[1].x - v[0].x, dy = v[1].y - v[0].y;
            double length = hypot(dx, dy), nx = length ? -dy / length * k * 0.5 : 0;
            double ny = length ? dx / length * k * 0.5 : k * 0.5;
            RVert a = v[0], b = v[1];
            a.x += k * 0.5;
            a.y += k * 0.5;
            b.x += k * 0.5;
            b.y += k * 0.5;
            v[0] = v[1] = a;
            v[2] = v[3] = b;
            v[0].x += nx;
            v[0].y += ny;
            v[1].x -= nx;
            v[1].y -= ny;
            v[2].x += nx;
            v[2].y += ny;
            v[3].x -= nx;
            v[3].y -= ny;
        }
        if (p->count != 3 && p->count != 4 && !(p->flags & XG_PRIM_LINE))
            continue;
        triangle(p, vram, &v[0], &v[1], &v[2], rgba, w, h, bg == 0, clip);
        if (p->count == 4 || (p->flags & XG_PRIM_LINE)) {
            triangle(p, vram, &v[1], &v[2], &v[3], rgba, w, h, bg == 0, clip);
        }
    }
}
