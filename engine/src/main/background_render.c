/*
 * Field background polygons, per frame: project the horizon, split the
 * wrapping panorama texture into strips, add the fill and gradient polys.
 *
 * File 0x800273C4-0x80027D40 (main executable), GCC 2.6.0. It may really
 * be the start of filesystem.c (same compiler and flags); nothing in the
 * code decides it.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"

/* libgpu.h macros (not yet in psyq.h) */
#define setXY4(p, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3) \
    (p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), (p)->x2 = (_x2), (p)->y2 = (_y2), \
    (p)->x3 = (_x3), (p)->y3 = (_y3)
#define setUV4(p, _u0, _v0, _u1, _v1, _u2, _v2, _u3, _v3) \
    (p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u1), (p)->v1 = (_v1), (p)->u2 = (_u2), (p)->v2 = (_v2), \
    (p)->u3 = (_u3), (p)->v3 = (_v3)

/* Noah: sBackgroundPoly (0x34C bytes) */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_ft4[16];
    /* 0x280 */ POLY_F4 m280_f4[4];
    /* 0x2E0 */ POLY_G4 m2E0_g4[2];
    /* 0x328 */ s32 m328_textureWidth;
    /* 0x32C */ s32 m32C_textureHeight;
    /* 0x330 */ s32 m330_scrollScale;
    /* 0x334 */ s16 m334_vramX;
    /* 0x336 */ s16 m336_vramY;
    /* 0x338 */ s16 m338_colorMode;
    /* 0x33A */ s16 m33A_texV;
    /* 0x33C */ s16 m33C;
    /* 0x33E */ s16 m33E_horizonY;
    /* 0x340 */ s16 m340_horizonDistance;
    /* 0x342 */ s16 m342;
    /* 0x344 */ s16 m344_hasFills;
    /* 0x346 */ s16 m346_gradientScale;
    /* 0x348 */ s16 m348_zoomDivisor;
    /* 0x34A */ s16 m34A_zoomStartDistance;
} BackgroundPoly;

void computeBackgroundPoly(BackgroundPoly* pano, s32 scroll, s32 horizonY, s32 zoom, OTag* ot, s32 buf);

/* A full-screen backdrop can be a saved scene (the loss screen uses the
 * last battle), rather than authored artwork. Keep its captured side/extra
 * rows without wrapping the 320-pixel centre like a panorama. */
static void capturedBackdropPiece(POLY_FT4* source, OTag* ot, s32 x, s32 y, s32 w, s32 h, s32 tx, s32 ty) {
    POLY_FT4* p = pcWideAlloc(sizeof(POLY_FT4));
    if (p == NULL) return;
    *p = *source;
    setXY4(p, x, y, x+w, y, x, y+h, x+w, y+h);
    setUV4(p, source->u0, source->v0, source->u0+w, source->v0,
           source->u0, source->v0+h, source->u0+w, source->v0+h);
    addPrim(ot, p);
    pcWidePrimTex2D(p, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, tx, ty);
}

static void capturedBackdropMargins(BackgroundPoly* pano, POLY_FT4* source, OTag* ot) {
    s32 top, bottom, x, end, w;
    s32 margin = pcWideCaptureMargin(pano->m334_vramX, pano->m336_vramY);
    if (margin > pcWideView) margin = pcWideView;
    pcWideCaptureLines(pano->m334_vramX, pano->m336_vramY, &top, &bottom);
    if (pcVerticalCull == 0) top = bottom = 0;
    for (x = -margin; x < 320+margin; x = end) {
        end = x < 0 ? 0 : x < 320 ? 320 : 320+margin;
        if (end-x > 128) end = x+128;
        w = end-x;
        if (x < 0 || x >= 320)
            capturedBackdropPiece(source, ot, x, source->y0, w, pano->m32C_textureHeight, x, 0);
        if (top)
            capturedBackdropPiece(source, ot, x, source->y0-top, w, top, x, -top);
        if (bottom)
            capturedBackdropPiece(source, ot, x, source->y2, w, bottom, x, pano->m32C_textureHeight);
    }
}

/* Zoom the title's 320x220 artwork (four bottom texture rows are unused)
 * to the 240-line display, around its centre. Scale both axes by 12/11;
 * only the display copy changes, and no edge texels are repeated. */
static void titleVerticalZoom(BackgroundPoly* pano, POLY_FT4* source, s32 zoom, OTag* ot) {
    extern s32 fieldMapNumber;
    POLY_FT4* p;

    if (pcVerticalCull == 0 || pcWideGetMode() != PC_WIDE_FIELD || (fieldMapNumber & 0xFFF) != 490 ||
        zoom != 0 || pano->m328_textureWidth != 320 || pano->m32C_textureHeight != 224 ||
        source->y0 != 0 || source->y2 != 224) {
        return;
    }
    p = pcWideAlloc(sizeof(POLY_FT4));
    if (p == NULL) {
        return;
    }
    *p = *source;
    p->x0 = p->x2 = 160 + (source->x0 - 160) * 12 / 11;
    p->x1 = p->x3 = 160 + (source->x1 - 160) * 12 / 11;
    p->y0 = p->y1 = -10;
    p->y2 = p->y3 = -10 + 224 * 12 / 11;
    addPrim(ot, p);
    pcWideOnly(p);
    pcWidePrim(source, 0, PC_WIDE_NATIVE_ONLY);
}

/*
 * The wide picture (include/pc_wide.h): the panorama past the 4:3 edges.
 * Strips from screen x sx0 to sxEnd starting at texel x, laid as the PS1's
 * loop lays them (the texture wraps), in primitives from the PC pool drawn
 * only into the wide picture; the first of this buffer's strips gives their
 * code, colour and CLUT.
 */
static void wideBackgroundStrips(BackgroundPoly* pano, s32 x, s32 sx0, s32 sxEnd, s32 horizonY, s32 h, s32 zoom,
                                 OTag* ot, s32 buf) {
    POLY_FT4* p;
    s32 i, t, tpx;
    s16 uoff, u, texW, scrW, nextx;

    t = pano->m334_vramX;
    uoff = (t % 64) << (2 - pano->m338_colorMode);
    for (i = 0; i < 32 && sx0 < sxEnd; i++) {
        p = pcWideAlloc(sizeof(POLY_FT4));
        if (p == NULL) {
            return;
        }
        *p = pano->m0_ft4[(buf & 1) * 8];
        tpx = pano->m334_vramX + (x >> (2 - pano->m338_colorMode));
        u = (x + uoff) & ((0x100 >> pano->m338_colorMode) - 1);
        texW = 0x100 - u;
        if (pano->m328_textureWidth < x + texW) {
            texW = pano->m328_textureWidth - x;
        }
        scrW = (texW << 8) / (zoom + 256);
        if (sx0 + scrW > sxEnd) {
            scrW = sxEnd - sx0;
            texW = scrW * (zoom + 256) / 256;
        }
        if (scrW <= 0 || texW <= 0) {
            return;
        }
        nextx = (s16)(x + texW) % pano->m328_textureWidth;
        setXY4(p, sx0, horizonY - h, sx0 + scrW, horizonY - h, sx0, horizonY, sx0 + scrW, horizonY);
        setUV4(p, u, pano->m33A_texV, u + texW - 1, pano->m33A_texV, u, pano->m33A_texV + pano->m32C_textureHeight,
               u + texW - 1, pano->m33A_texV + pano->m32C_textureHeight);
        p->tpage = GetTPage(pano->m338_colorMode, 0, tpx / 64 * 64, pano->m336_vramY / 256 * 256);
        addPrim(ot, p);
        pcWideOnly(p);
        x = nextx;
        sx0 += scrW;
    }
}

s32 renderBackgroundPoly(BackgroundPoly* pano, SVECTOR* eye, SVECTOR* target, MATRIX* m, OTag* ot, s32 buf) {
    SVECTOR sxy;
    SVECTOR sxy2;
    VECTOR dir;
    SVECTOR pt;
    SVECTOR norm;
    s16 zoom;
    s16 y;

    if (pano == NULL) {
        return 0;
    }
    dir.vx = target->vx - eye->vx;
    dir.vy = 0;
    dir.vz = target->vz - eye->vz;
    VectorNormalS(&dir, &norm);
    pt.vx = norm.vx * pano->m340_horizonDistance / 4096 + target->vx;
    pt.vy = pano->m33E_horizonY;
    pt.vz = norm.vz * pano->m340_horizonDistance / 4096 + target->vz;
    SetRotMatrix(m);
    SetTransMatrix(m);
    gte_ldv0(&pt);
    gte_rtps();
    gte_stsxy(&sxy);
    if (pano->m348_zoomDivisor != 0) {
        dir.vx = target->vx - eye->vx;
        dir.vy = target->vy - eye->vy;
        dir.vz = target->vz - eye->vz;
        zoom = (SquareRoot0(dir.vx * dir.vx + dir.vy * dir.vy + dir.vz * dir.vz) - pano->m34A_zoomStartDistance) / pano->m348_zoomDivisor;
        if (zoom < 0) {
            zoom = 0;
        }
        if (zoom > 0x100) {
            zoom = 0x100;
        }
    } else {
        zoom = 0;
    }
    computeBackgroundPoly(pano, pano->m328_textureWidth * pano->m330_scrollScale * (ratan2(dir.vx, dir.vz) & 0xFFF) / 4096, sxy.vy, zoom, ot, buf);
    if (pano->m344_hasFills > 0) {
        sxy.vx = sxy.vy - pano->m32C_textureHeight;
        if (sxy.vx > 0xF0) {
            sxy.vx = 0xF0;
        }
        if (sxy.vx > 0) {
            pano->m280_f4[buf].y2 = sxy.vx;
            pano->m280_f4[buf].y3 = sxy.vx;
            addPrim(ot, &pano->m280_f4[buf]);
        }
        pt.vx = norm.vx * pano->m340_horizonDistance / 4096 * pano->m346_gradientScale / 256 + target->vx;
        pt.vy = pano->m33E_horizonY * pano->m346_gradientScale / 256;
        pt.vz = norm.vz * pano->m340_horizonDistance / 4096 * pano->m346_gradientScale / 256 + target->vz;
        gte_ldv0(&pt);
        gte_rtps();
        gte_stsxy(&sxy2);
        if (sxy2.vy - sxy.vy > 0xF0) {
            sxy2.vy = sxy.vy + 0xF0;
        }
        if (sxy2.vy >= 0 && sxy.vy < 0xF0) {
            pano->m2E0_g4[buf].y0 = sxy.vy;
            pano->m2E0_g4[buf].y1 = sxy.vy;
            pano->m2E0_g4[buf].y2 = sxy2.vy;
            pano->m2E0_g4[buf].y3 = sxy2.vy;
            addPrim(ot, &pano->m2E0_g4[buf]);
        }
        if (sxy2.vy < 0) {
            y = 0;
        } else {
            y = sxy2.vy;
        }
        if (y < 0xF0) {
            pano->m280_f4[buf + 2].y0 = y;
            pano->m280_f4[buf + 2].y1 = y;
            addPrim(ot, &pano->m280_f4[buf + 2]);
        }
    }
    return sxy.vy;
}

void computeBackgroundPoly(BackgroundPoly* pano, s32 scroll, s32 horizonY, s32 zoom, OTag* ot, s32 buf) {
    POLY_FT4* p;
    s32 i;
    s32 half;
    s16 x;
    s16 sx0;
    s16 texW;
    s16 scrW;
    s16 u;
    s16 tpx;
    s16 h;
    s16 uoff;
    s16 nextx;
    s16 bottom;
    s32 t;
    s32 captured = zoom == 0 && scroll == 0 && pano->m338_colorMode == 2 &&
                   pano->m328_textureWidth == 320 && pano->m32C_textureHeight == 224 &&
                   pcWideCapturedScene(pano->m334_vramX, pano->m336_vramY, 320, 224);

    half = (320 - (pano->m328_textureWidth << 8) / (zoom + 256)) / 2;
    half += half * zoom / 256;
    x = (s16)(scroll - half) % pano->m328_textureWidth;
    if (x < 0) {
        x += pano->m328_textureWidth;
    }
    if (horizonY < 0 || pano->m32C_textureHeight + 0xF0 < horizonY) {
        h = 0;
    } else {
        h = (pano->m32C_textureHeight << 8) / (zoom + 256);
    }
    sx0 = 0;
    p = &pano->m0_ft4[(buf & 1) * 8];
    if (h > 0 && pcWideView != 0 && !captured) {
        /* The left margin: from the texel M screen pixels before x. */
        s32 span = pcWideView * (zoom + 256) / 256;
        s32 xl = ((s32)x - span) % pano->m328_textureWidth;
        if (xl < 0) {
            xl += pano->m328_textureWidth;
        }
        wideBackgroundStrips(pano, xl, -(span * 256 / (zoom + 256)), 0, horizonY, h, zoom, ot, buf);
    }
    if (h > 0) {
        t = pano->m334_vramX;
        uoff = (t % 64) << (2 - pano->m338_colorMode);
        for (i = 0; i < 8; i++) {
            tpx = pano->m334_vramX + (x >> (2 - pano->m338_colorMode));
            u = (x + uoff) & ((0x100 >> pano->m338_colorMode) - 1);
            texW = 0x100 - u;
            if (pano->m328_textureWidth < x + texW) {
                texW = pano->m328_textureWidth - x;
            }
            scrW = (texW << 8) / (zoom + 256);
            if (sx0 + scrW > 320) {
                scrW = 320 - sx0;
                texW = scrW * (zoom + 256) / 256;
            }
            nextx = (s16)(x + texW) % pano->m328_textureWidth;
            bottom = horizonY;
            setXY4(p, sx0, horizonY - h, sx0 + scrW, horizonY - h, sx0, bottom, sx0 + scrW, bottom);
            setUV4(p, u, pano->m33A_texV, u + texW - 1, pano->m33A_texV, u, pano->m33A_texV + pano->m32C_textureHeight, u + texW - 1,
                   pano->m33A_texV + pano->m32C_textureHeight);
            x = nextx;
            p->tpage = GetTPage(pano->m338_colorMode, 0, tpx / 64 * 64, pano->m336_vramY / 256 * 256);
            if (captured) {
                pcWidePrim(p, 0, PC_WIDE_HANDLED);
                if (sx0 == 0) capturedBackdropMargins(pano, p, ot);
            } else titleVerticalZoom(pano, p, zoom, ot);
            addPrim(ot, p);
            sx0 += scrW;
            if (sx0 >= 320) {
                break;
            }
            p++;
        }
        /* The right margin, from where the PS1's strips end. */
        if (pcWideView != 0 && !captured) {
            wideBackgroundStrips(pano, x, sx0, 320 + pcWideView, horizonY, h, zoom, ot, buf);
        }
    }
}
