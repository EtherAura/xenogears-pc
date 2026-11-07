/*
 * Battling's gear shadows (hand-written assembly at 0x8008C3A8 in the PS1
 * build; config/handwritten.yaml): the model's vertices projected onto the
 * ground plane along the light, then drawn as flat polygons into one
 * ordering-table entry. PC versions of the same logic; primitives use the PC
 * layout (fields by name, sizeof steps) and keep host pointers where the PS1
 * strips them to 24 bits.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"

extern SVECTOR* currentModelBlockVertices;
extern u8* currentModelInstanceDrawPrims;
extern OTag* currentOTEntry;
extern s32 fieldPolyCount;
extern s32 screenClippingX;
extern s32 screenClippingY;
extern VECTOR battlingShadowLightDir; /* the light position */

#define GTE_SXY0 12
#define GTE_SXY1 13
#define GTE_SXY2 14
#define GTE_MAC0 24
#define GTE_MAC1 25
#define GTE_MAC2 26
#define GTE_MAC3 27

/* Each vertex transformed by the GTE matrix (RT * v + TR), then moved along
 * the line from the light through it down to height 0: x and z of dst become
 * (p * L.y - L * p.y) / (L.y - p.y); dst's y and pad are left alone, and so is
 * the whole of dst for a vertex at the light's height. count >= 1. */
void projectBattlingShadowVertices(SVECTOR* src, SVECTOR* dst, s32 count) {
    s32 lx = battlingShadowLightDir.vx;
    s32 ly = battlingShadowLightDir.vy;
    s32 lz = battlingShadowLightDir.vz;
    s32 x;
    s32 y;
    s32 z;
    s32 d;

    gte_ldv0(src);
    src++;
    do {
        gte_rt();
        gte_ldv0(src); /* the next vertex; after the last, the one past it */
        y = gteMfc2(GTE_MAC2);
        x = gteMfc2(GTE_MAC1);
        d = ly - y;
        if (d != 0) {
            z = gteMfc2(GTE_MAC3);
            dst->vx = (x * ly - lx * y) / d;
            dst->vz = -(-z * ly + lz * y) / d;
        }
        src++;
        dst++;
    } while (--count != 0);
}

/* The render loops' screen test (port/src/game/main/render.c): some vertex
 * above the bottom edge, and some left of the right edge. With a wide picture
 * (include/pc_wide.h) a polygon only the widened x test keeps returns 2: it is
 * drawn into the wide picture only (the GTE did nothing extra for it). */
static s32 onScreen(const u32* sxy, s32 n) {
    u32 clipY = screenClippingY;
    u32 clipX = screenClippingX;
    s32 i;

    for (i = 0; i < n && !(sxy[i] < clipY); i++) {
    }
    if (i == n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if ((sxy[i] & 0xFFFF) < clipX) {
            return 1;
        }
    }
    for (i = 0; pcWideCull != 0 && i < n; i++) {
        if (PC_WIDE_UX(sxy[i], clipX)) {
            return 2;
        }
    }
    return 0;
}

static void linkPrim(OTag* entry, void* prim, u32 len) {
    OTag next = *entry;

    *entry = OT_LINK(prim);
    *(OTag*)prim = next | OT_LEN(len);
}

/* F3 shadows. Counts every polygon that passes the vertical screen test, front
 * facing or not. Returns nothing (the PS1 leaves the count in $a3). */
void renderBattlingShadowTris(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    SVECTOR* verts = currentModelBlockVertices;
    s32 polys = fieldPolyCount;
    POLY_F3* p = (POLY_F3*)currentModelInstanceDrawPrims - 1;
    OTag* ot = currentOTEntry;
    u32 sxy[3];
    s32 on;

    gte_ldv0(&verts[e[0]]);
    gte_ldv1(&verts[e[1] & 0x1FFF]);
    gte_ldv2(&verts[e[2]]);
    for (;;) {
        gte_rtpt();
        if (count == 0) {
            break;
        }
        count--;
        e += 4;
        p++;
        gte_ldv0(&verts[e[0]]);
        gte_ldv1(&verts[e[1] & 0x1FFF]);
        gte_ldv2(&verts[e[2]]);
        gteSwc2(GTE_SXY0, &sxy[0]);
        gteSwc2(GTE_SXY1, &sxy[1]);
        gteSwc2(GTE_SXY2, &sxy[2]);
        gte_nclip();
        if (!(sxy[0] < (u32)screenClippingY || sxy[1] < (u32)screenClippingY || sxy[2] < (u32)screenClippingY)) {
            continue;
        }
        polys++;
        POLY_F3* out = p;
        if ((s32)gteMfc2(GTE_MAC0) <= 0) {
            if (!pcGeometryRescue() || !(out = pcGeometryAlloc(p, sizeof(*p)))) continue;
        }
        if (!(on = onScreen(sxy, 3))) continue;
        gteCopySxy(&out->x0, &sxy[0]);
        gteCopySxy(&out->x1, &sxy[1]);
        gteCopySxy(&out->x2, &sxy[2]);
        linkPrim(ot, out, 4);
        if (on == 2) {
            pcWideOnly(out);
        }
    }
    fieldPolyCount = polys;
    currentModelInstanceDrawPrims = (u8*)(p + 1);
}

/* F4 shadows. Counts every polygon, front facing or not; the back-face test
 * uses the first triangle. */
void renderBattlingShadowQuads(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    SVECTOR* verts = currentModelBlockVertices;
    s32 polys = fieldPolyCount;
    POLY_F4* p = (POLY_F4*)currentModelInstanceDrawPrims - 1;
    OTag* ot = currentOTEntry;
    SVECTOR* v0;
    SVECTOR* v3;
    u32 sxy[4];
    s32 on;

    v0 = &verts[e[0]];
    gte_ldv1(&verts[e[1] & 0x1FFF]);
    gte_ldv2(&verts[e[2]]);
    for (;;) {
        gte_ldv0(v0);
        gte_rtpt();
        if (count == 0) {
            break;
        }
        count--;
        e += 4;
        p++;
        v0 = &verts[e[0]];
        gte_ldv1(&verts[e[1] & 0x1FFF]);
        gte_ldv2(&verts[e[2]]);
        gteSwc2(GTE_SXY0, &sxy[0]);
        gteSwc2(GTE_SXY1, &sxy[1]);
        gteSwc2(GTE_SXY2, &sxy[2]);
        gte_nclip();
        v3 = &verts[e[-1]];
        polys++;
        gteLwc2(0, v3);
        POLY_F4* out = p;
        GteState saved;
        int rescue = (s32)gteMfc2(GTE_MAC0) <= 0;
        if (rescue) {
            if (!pcGeometryRescue() || !(out = pcGeometryAlloc(p, sizeof(*p)))) continue;
            gteSave(&saved);
        }
        gteLwc2(1, (u8*)v3 + 4);
        gte_rtps();
        gteSwc2(GTE_SXY2, &sxy[3]);
        if (rescue) gteLoad(&saved);
        if (!(on = onScreen(sxy, 4))) {
            continue;
        }
        gteCopySxy(&out->x0, &sxy[0]);
        gteCopySxy(&out->x1, &sxy[1]);
        gteCopySxy(&out->x2, &sxy[2]);
        gteCopySxy(&out->x3, &sxy[3]);
        linkPrim(ot, out, 5);
        if (on == 2) {
            pcWideOnly(out);
        }
    }
    fieldPolyCount = polys;
    currentModelInstanceDrawPrims = (u8*)(p + 1);
}
