/*
 * Font glyph drawing (Noah: sFont, sFontGlyph).
 *
 * File 0x80025FA8-0x8002709C (main executable). It reaches shapeTransfertTableEnd and
 * shapeTransfertTableCurrentEntry through %hi/%lo while shape_transfer.c defines them ($gp), and
 * the code after it was assembled by an older ASPSX. Built with GCC
 * 2.7.2-cdk.
 */
#include "common.h"
#include "pc_wide.h"
#include "pc_glyph.h"
#include "psyq.h"

/* libgpu.h macros (not yet in psyq.h) */
#define setXYWH(p, _x0, _y0, _w, _h) \
    (p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x0) + (_w), (p)->y1 = (_y0), (p)->x2 = (_x0), \
    (p)->y2 = (_y0) + (_h), (p)->x3 = (_x0) + (_w), (p)->y3 = (_y0) + (_h)
#define setUVWH(p, _u0, _v0, _w, _h) \
    (p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u0) + (_w), (p)->v1 = (_v0), (p)->u2 = (_u0), \
    (p)->v2 = (_v0) + (_h), (p)->u3 = (_u0) + (_w), (p)->v3 = (_v0) + (_h)

/* Noah: sFontGlyphPoly, 0x1C bytes */
typedef struct {
    /* 0x00 */ u16 m0_u;
    /* 0x02 */ s16 m2_v;
    /* 0x04 */ s16 m4_w;
    /* 0x06 */ s16 m6_h;
    /* 0x08 */ s16 m8_x;
    /* 0x0A */ s16 mA_y;
    /* 0x0C */ u16 mC;
    /* 0x0E */ u16 mE;
    /* 0x10 */ s16 m10_colorDepth;
    /* 0x12 */ s16 m12_clutX;
    /* 0x14 */ s16 m14_clutY;
    /* 0x16 */ s16 m16_tpageX;
    /* 0x18 */ s16 m18_tpageY;
    /* 0x1A */ u8 m1A_flipX;
    /* 0x1B */ u8 m1B_flipY;
} FontGlyphPoly;

/* Noah: sFontGlyph */
typedef struct {
    /* 0x00 */ s16 m0_polyCount;
    /* 0x02 */ u16 m2;
    /* 0x04 */ FontGlyphPoly m4_polys[1];
} FontGlyph;

/* Noah: sFont */
typedef struct Font {
    /* 0x00 */ s32 m0_numGlyphs;
    /* 0x04 */ u16 m4_glyphOffsets[1];
} Font;

/* 0x18-byte glyph sprite record filled by setupDamagePoly */
typedef struct {
    /* 0x00 */ s16 m0_x;
    /* 0x02 */ s16 m2_y;
    /* 0x04 */ u8 m4_u;
    /* 0x05 */ u8 m5_v;
    /* 0x06 */ u8 m6_w;
    /* 0x07 */ u8 m7_h;
    /* 0x08 */ u8 m8[2];
    /* 0x0A */ u16 mA_tpage;
    /* 0x0C */ u16 mC_clut;
    /* 0x0E */ u8 mE[0x18 - 0xE];
} FontSprite;

extern u8* shapeTransfertTableEnd; /* GPU packet buffer end */
extern u8* shapeTransfertTableCurrentEntry; /* GPU packet buffer current */
/* Corners of the rotated glyph quad; vz stays 0x1000 (font.c's .data). */
SVECTOR rotatedGlyphCorners[4] = {{0, 0, 0x1000}, {0, 0, 0x1000}, {0, 0, 0x1000}, {0, 0, 0x1000}};
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);

s32 battleSetupStringInPolyFTRot(Font* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, s16 scaleX,
                  s16 scaleY, s16 rot) {
    MATRIX m = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, {0, 0, 0}};
    VECTOR scale;
    s32 ofx, ofy;
    s32 p, flag;
    FontGlyph* glyph;
    FontGlyphPoly* gp;
    POLY_FT4* prim;
    s32 i;
    s32 h;
    s16 u, v, w, hh;
    s16 a, b;

    scale.vx = scaleX;
    scale.vy = scaleY;
    scale.vz = 0x1000;
    PushMatrix();
    ScaleMatrixL(&m, &scale);
    RotMatrixZ(rot, &m);
    ReadGeomOffset(&ofx, &ofy);
    h = ReadGeomScreen();
    SetGeomOffset(x, y);
    SetGeomScreen(0x1000);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    for (i = 0; i != glyph->m0_polyCount; i++) {
        prim = &polys[i][oddOrEven];
        SetPolyFT4(prim);
        SetSemiTrans(prim, 0);
        SetShadeTex(prim, 1);
        gp = &glyph->m4_polys[i];
        prim->tpage = GetTPage(gp->m10_colorDepth, 0, gp->m16_tpageX, gp->m18_tpageY);
        prim->clut = GetClut(gp->m12_clutX, gp->m14_clutY);
        w = gp->m4_w;
        hh = gp->m6_h;
        a = gp->m8_x;
        b = gp->mA_y;
        if (gp->m1A_flipX == 0) {
            rotatedGlyphCorners[0].vx = a;
            rotatedGlyphCorners[1].vx = a + w;
            rotatedGlyphCorners[2].vx = a + w;
            rotatedGlyphCorners[3].vx = a;
        } else {
            rotatedGlyphCorners[0].vx = a + w;
            rotatedGlyphCorners[1].vx = a;
            rotatedGlyphCorners[2].vx = a;
            rotatedGlyphCorners[3].vx = a + w;
        }
        if (gp->m1B_flipY == 0) {
            rotatedGlyphCorners[2].vy = b + hh;
            rotatedGlyphCorners[3].vy = b + hh;
            rotatedGlyphCorners[0].vy = b;
            rotatedGlyphCorners[1].vy = b;
        } else {
            rotatedGlyphCorners[2].vy = b;
            rotatedGlyphCorners[3].vy = b;
            rotatedGlyphCorners[0].vy = b + hh;
            rotatedGlyphCorners[1].vy = b + hh;
        }
        RotTransPers4(&rotatedGlyphCorners[0], &rotatedGlyphCorners[1], &rotatedGlyphCorners[2], &rotatedGlyphCorners[3], (s32*)&prim->x0,
                      (s32*)&prim->x1, (s32*)&prim->x3, (s32*)&prim->x2, &p, &flag);
        u = gp->m0_u;
        v = gp->m2_v;
        w = gp->m4_w;
        hh = gp->m6_h;
        if ((rot & 0xFFF) == 0xC00) {
            u--;
        }
        if ((rot & 0xFFF) == 0) {
            if (prim->x3 < prim->x0) {
                u--;
                if (u < 0) {
                    u = 0;
                    w--;
                }
            }
            if (prim->y3 < prim->y0) {
                v--;
                if (v < 0) {
                    v = 0;
                    hh--;
                }
            }
        }
        prim->u0 = u;
        prim->v0 = v;
        prim->u1 = u + w;
        prim->v1 = v;
        prim->u2 = u;
        prim->v2 = v + hh;
        prim->u3 = u + w;
        prim->v3 = v + hh;
        pcGlyphTile(prim->tpage, gp->m0_u, gp->m2_v, gp->m4_w, gp->m6_h);
    }
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
    PopMatrix();
    return glyph->m0_polyCount;
}

void computeMenuBorder(Font* font, s32 glyphIndex, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY,
                   s32* tpageX, s32* tpageY) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    FontGlyphPoly* poly;
    s32 u;
    s32 t;

    *polyCount = glyph->m0_polyCount;
    poly = glyph->m4_polys;
    t = poly->m0_u << 16;
    if (poly->m10_colorDepth != 0) {
        u = t >> 18;
    } else {
        u = t >> 20;
    }
    *colorDepth = poly->m10_colorDepth;
    *clutX = poly->m12_clutX;
    *clutY = poly->m14_clutY;
    *tpageX = (s16)(poly->m16_tpageX & 0xFFC0) + u;
    *tpageY = (s16)(poly->m18_tpageY & 0xFF00) + poly->m2_v;
}


s32 initMenuTiles6Sub(Font* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, u16 scale,
                  u8 flipX, u8 flipY) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    FontGlyphPoly* gp;
    POLY_FT4* p;
    s32 i;
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
    s16 u, v, w, h;

    for (i = 0; i != glyph->m0_polyCount; i++) {
        p = &polys[i][oddOrEven];
        gp = &glyph->m4_polys[i];
        x0 = gp->m8_x * scale / 4096;
        y0 = gp->mA_y * scale / 4096;
        x1 = gp->m4_w * scale / 4096;
        y1 = gp->m6_h * scale / 4096;
        SetPolyFT4(p);
        SetSemiTrans(p, 0);
        SetShadeTex(p, 1);
        p->tpage = GetTPage(gp->m10_colorDepth, 0, gp->m16_tpageX, gp->m18_tpageY);
        p->clut = GetClut(gp->m12_clutX, gp->m14_clutY);
        if (flipX) {
            x1 = -x1;
            x0 = -x0;
        }
        if (flipY) {
            y0 = -y0;
            y1 = -y1;
        }
        u = gp->m0_u;
        v = gp->m2_v;
        w = gp->m4_w;
        h = gp->m6_h;
        if (gp->m1A_flipX == 0) {
            s16 a = x + x0;
            s16 b = x1 + a;
            p->x0 = a;
            p->x1 = b;
            p->x2 = a;
            p->x3 = b;
        } else {
            s16 a = x + x0;
            s16 b = x1 + a;
            p->x0 = b;
            p->x1 = a;
            p->x2 = b;
            p->x3 = a;
        }
        if (gp->m1B_flipY == 0) {
            p->y0 = y + y0;
            p->y1 = y + y0;
            p->y2 = y1 + (s16)(y + y0);
            p->y3 = y1 + (s16)(y + y0);
        } else {
            s16 a = y + y0;
            s16 b = y1 + a;
            p->y0 = b;
            p->y1 = b;
            p->y2 = a;
            p->y3 = a;
        }
        if (p->x3 < p->x0) {
            u--;
            if (u < 0) {
                u = 0;
                w--;
            }
        }
        if (p->y3 < p->y0) {
            v--;
            if (v < 0) {
                v = 0;
                h--;
            }
        }
        p->u0 = u;
        p->v0 = v;
        p->u1 = u + w;
        p->v1 = v;
        p->u2 = u;
        p->v2 = v + h;
        p->u3 = u + w;
        p->v3 = v + h;
        pcGlyphTile(p->tpage, gp->m0_u, gp->m2_v, gp->m4_w, gp->m6_h);
    }
    return glyph->m0_polyCount;
}

s32 setupStringInPolyFT4(Font* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, u16 scale) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    FontGlyphPoly* gp;
    POLY_FT4* p;
    s32 i;
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
    s16 u, v, w, h;

    for (i = 0; i != glyph->m0_polyCount; i++) {
        p = &polys[i][oddOrEven];
        gp = &glyph->m4_polys[i];
        x0 = gp->m8_x * scale / 4096;
        y0 = gp->mA_y * scale / 4096;
        x1 = gp->m4_w * scale / 4096;
        y1 = gp->m6_h * scale / 4096;
        SetPolyFT4(p);
        SetSemiTrans(p, 0);
        SetShadeTex(p, 1);
        p->tpage = GetTPage(gp->m10_colorDepth, 0, gp->m16_tpageX, gp->m18_tpageY);
        p->clut = GetClut(gp->m12_clutX, gp->m14_clutY);
        u = gp->m0_u;
        v = gp->m2_v;
        w = gp->m4_w;
        h = gp->m6_h;
        if (gp->m1A_flipX == 0) {
            p->x0 = x + x0;
            p->x1 = x1 + (x + x0);
            p->x2 = x + x0;
            p->x3 = x1 + (x + x0);
        } else {
            p->x0 = x1 + (x + x0);
            p->x1 = x + x0;
            p->x2 = x1 + (x + x0);
            p->x3 = x + x0;
            u--;
            if (u < 0) {
                u = 0;
                w--;
            }
        }
        if (gp->m1B_flipY == 0) {
            p->y0 = y + y0;
            p->y1 = y + y0;
            p->y2 = y1 + (y + y0);
            p->y3 = y1 + (y + y0);
        } else {
            p->y0 = y1 + (y + y0);
            p->y1 = y1 + (y + y0);
            p->y2 = y + y0;
            p->y3 = y + y0;
            v--;
            if (v < 0) {
                v = 0;
                h--;
            }
        }
        p->u0 = u;
        p->v0 = v;
        p->u1 = u + w;
        p->v1 = v;
        p->u2 = u;
        p->v2 = v + h;
        p->u3 = u + w;
        p->v3 = v + h;
        pcGlyphTile(p->tpage, gp->m0_u, gp->m2_v, gp->m4_w, gp->m6_h);
    }
    return glyph->m0_polyCount;
}

s32 setupStringInSprt(Font* font, s32 glyphIndex, SPRT* prims, s32 oddOrEven, s16 x, s16 y) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    FontGlyphPoly* poly;
    SPRT* p;
    s32 i;
    s16 px, py;
    s16 u, v;
    s16 w, h;

    for (i = 0; i != glyph->m0_polyCount; i++) {
        p = prims + (i << 1) + oddOrEven;
        poly = &glyph->m4_polys[i];
        SetSprt(p);
        p->clut = GetClut(poly->m12_clutX, poly->m14_clutY);
        SetSemiTrans(p, 0);
        SetShadeTex(p, 1);
        px = x + poly->m8_x;
        py = y + poly->mA_y;
        u = poly->m0_u;
        v = poly->m2_v;
        w = poly->m4_w;
        h = poly->m6_h;
        p->x0 = px;
        p->y0 = py;
        p->u0 = u;
        p->v0 = v;
        p->w = w;
        p->h = h;
        pcGlyphTile(GetTPage(poly->m10_colorDepth, 0, poly->m16_tpageX, poly->m18_tpageY),
                    poly->m0_u, poly->m2_v, poly->m4_w, poly->m6_h);
    }
    poly = glyph->m4_polys;
    SetDrawMode((DR_MODE*)(prims + (i << 1) + oddOrEven), 0, 0,
                GetTPage(poly->m10_colorDepth, 0, poly->m16_tpageX, poly->m18_tpageY), NULL);
    return glyph->m0_polyCount + 1;
}

void func_80026B9C(void) {
}

void setupTotalDamageDisplayPoly(Font* font, s32 glyphIndex, s32 x, s32 y, OTag* ot) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    s32 i;
    s32 count = glyph->m0_polyCount;
    FontGlyphPoly* gp;
    POLY_FT4* p;
    s32 t;
    s32 tpx, tpy;
    s32 u, v, w, h, px, py, depth;

    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        for (i = 0; i != count; i++) {
            p = (POLY_FT4*)shapeTransfertTableCurrentEntry;
            shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
            setlen(p, 9);
            setcode(p, 0x2D);
            gp = &glyph->m4_polys[i];
            t = gp->m0_u << 16;
            if (gp->m10_colorDepth != 0) {
                u = t >> 18;
            } else {
                u = t >> 20;
            }
            v = gp->m2_v;
            depth = gp->m10_colorDepth;
            w = gp->m4_w;
            h = gp->m6_h;
            px = gp->m8_x;
            py = gp->mA_y;
            tpx = (s16)(gp->m16_tpageX & 0xFFC0) + u;
            tpy = (s16)(gp->m18_tpageY & 0xFF00) + v;
            u = (s16)gp->m0_u;
            p->clut = GetClut(gp->m12_clutX, gp->m14_clutY);
            p->tpage = GetTPage(depth, 0, tpx, tpy);
            setXYWH(p, px + x, py + y, w, h);
            setUVWH(p, u, v, w, h);
            pcGlyphTile(p->tpage, u, v, w, h);
            pcTextureClass(p, PC_TEX_TEXT);
            AddPrim(ot, p);
        }
    }
}

s32 setupDamagePoly(Font* font, s32 glyphIndex, FontSprite* out, s16 x, s16 y) {
    FontGlyph* glyph = (FontGlyph*)(font->m4_glyphOffsets[glyphIndex] + (sptr)font);
    s32 count = glyph->m0_polyCount;
    FontGlyphPoly* poly = glyph->m4_polys;
    s32 i;
    s32 u;
    s32 t;
    s32 tpx;
    s32 tpy;
    s32 depth;

    for (i = 0; i != count; i++) {
        t = poly->m0_u << 16;
        if (poly->m10_colorDepth != 0) {
            u = t >> 18;
        } else {
            u = t >> 20;
        }
        depth = poly->m10_colorDepth;
        tpx = (s16)(poly->m16_tpageX & 0xFFC0) + u;
        tpy = (s16)(poly->m18_tpageY & 0xFF00) + poly->m2_v;
        out->mC_clut = GetClut(poly->m12_clutX, poly->m14_clutY);
        out->mA_tpage = GetTPage(depth, 1, tpx, tpy);
        out->m4_u = poly->m0_u;
        out->m5_v = poly->m2_v;
        out->m6_w = poly->m4_w;
        out->m7_h = poly->m6_h;
        out->m0_x = poly->m8_x + x;
        out->m2_y = poly->mA_y + y;
        /* Floating damage has its own setup path. Register every digit,
         * including ones not yet used by the party HP display. */
        pcGlyphTile(out->mA_tpage, poly->m0_u, poly->m2_v, poly->m4_w, poly->m6_h);
        poly++;
        out++;
    }
    return count;
}

/* fadePaletteColors and lerpPaletteColors are hand-written assembly (addi,
 * bne $zero,rX operand order, no delay-slot use): config/handwritten.yaml. */
INCLUDE_ASM("main/nonmatchings/font", fadePaletteColors);

INCLUDE_ASM("main/nonmatchings/font", lerpPaletteColors);
