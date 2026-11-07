/*
 * The model render loops (hand-written assembly at 0x8002E010-0x8003014C and
 * 0x80030750 in the PS1 build; config/handwritten.yaml), called through
 * model.c's polyRenderDefs: one routine per render mode and primitive family.
 * Each projects the mesh block's polygons with the GTE, culls them, fills in
 * the screen coordinates (and lit or fogged colours) of the primitives that
 * initModel2 prepared, and links them into the ordering table.
 *
 * PC versions of the same logic, through the GTE macros of include/gte.h.
 * Differences from the PS1 code, none of them visible to the game:
 * - Primitives use the PC layout (include/psyq.h: an 8-byte tag), so fields
 *   are written by name and the buffer advances by sizeof the PC primitive.
 * - The PS1 strips the primitive pointer to 24 bits (a KUSEG mirror of the
 *   same RAM) before linking; the PC keeps the host pointer.
 * - GT3_FAKE and prim2_1 keep three normal pointers in the scratchpad
 *   (0x1F800000-0x1F800008); the PC keeps them in locals.
 * - With a wide picture (include/pc_wide.h) the x test also keeps polygons in
 *   its margins: those are linked PC_WIDE_ONLY (never drawn into VRAM), and
 *   the GTE work the PS1 skips for them is undone at the next polygon, so
 *   VRAM and the GTE stay as the PS1 has them.
 * - With subpixel geometry, fractional winding decides face visibility in
 *   the display copy. Faces the integer test drops are tagged display-only,
 *   and their extra GTE work and polygon counts are restored the same way.
 * Everything else is kept as the PS1 does it, including GTE commands that run
 * on paths where their result is not used, primitive fields written for
 * polygons that are then dropped, and one extra RTPT at the end of each loop
 * on the entry after the last one (the pipelined loops load the next polygon
 * before they know whether there is one).
 *
 * Arguments: the mesh block's index entries (8 bytes per polygon: vertex
 * numbers v0, v1, v2 and, for quads, v3) and the polygon count.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
#include "pc_interpolation.h"
#include "pc_camera.h"
#include "pc_wide.h"

extern SVECTOR* currentModelBlockVertices;
extern SVECTOR* currentModelBlockNormals;
extern u8* currentModelInstanceDrawPrims;
extern u8* currentModeBlock18;   /* per-polygon instance data (colours, face normals) */
extern OTag* currentOTEntry;
extern s32 fieldPolyCount;
extern s32 screenClippingX;      /* screen width */
extern s32 screenClippingY;      /* (screen height - 1) << 16 */
extern s32 gDepthDivider;
extern CVECTOR fogNearColor;

/* GTE data registers. */
#define GTE_RGBC 6
#define GTE_OTZ 7
#define GTE_SXY0 12
#define GTE_SXY1 13
#define GTE_SXY2 14
#define GTE_SZ0 16
#define GTE_SZ1 17
#define GTE_SZ2 18
#define GTE_SZ3 19
#define GTE_RGB0 20
#define GTE_RGB1 21
#define GTE_RGB2 22
#define GTE_MAC0 24
#define GTE_MAC1 25
#define GTE_MAC2 26
#define GTE_LZCR 31

/* Commands include/gte.h has no macro for (the PS1 code's encodings). */
#define GTE_NCT 0x0D80420
#define GTE_NCCT 0x118043F

/* Where a primitive type keeps what the loops write. */
typedef struct {
    u16 size; /* sizeof the PC primitive: the step through the buffer */
    u8 len;   /* the tag's word count */
    u8 xy[4]; /* offsets of x0..x3 */
} PrimShape;

#define OFS(type, field) ((u8)__builtin_offsetof(type, field))
#define XY3(type) { OFS(type, x0), OFS(type, x1), OFS(type, x2), 0 }
#define XY4(type) { OFS(type, x0), OFS(type, x1), OFS(type, x2), OFS(type, x3) }

static const PrimShape shapeF3 = { sizeof(POLY_F3), 4, XY3(POLY_F3) };
static const PrimShape shapeFT3 = { sizeof(POLY_FT3), 7, XY3(POLY_FT3) };
static const PrimShape shapeG3 = { sizeof(POLY_G3), 6, XY3(POLY_G3) };
static const PrimShape shapeGT3 = { sizeof(POLY_GT3), 9, XY3(POLY_GT3) };
static const PrimShape shapeF4 = { sizeof(POLY_F4), 5, XY4(POLY_F4) };
static const PrimShape shapeFT4 = { sizeof(POLY_FT4), 9, XY4(POLY_FT4) };
static const PrimShape shapeG4 = { sizeof(POLY_G4), 8, XY4(POLY_G4) };
static const PrimShape shapeGT4 = { sizeof(POLY_GT4), 12, XY4(POLY_GT4) };

/* The colour word after the tag (r0, g0, b0, code) and the code byte. */
#define PRIM_RGBC(prim) (*(u32*)&((P_TAG*)(prim))->r0)
#define PRIM_CODE(prim) ((u32)((P_TAG*)(prim))->code)

/* What every loop loads in its prologue and stores back at the end. */
typedef struct {
    SVECTOR* verts;
    u8* prim;   /* the primitive of the polygon being processed */
    OTag* ot;
    s32 polys;  /* fieldPolyCount */
    u32 clipY;
    u32 clipX;
    s32 shift;  /* OT index = z >> shift */
    s32 wideOnly;   /* this polygon only shows in the wide picture */
    s32 geometry;   /* integer and fractional face tests disagree */
    GteState gte;   /* the GTE before its extra work */
    s32 widePolys;  /* the count before it */
} Loop;

static void loopBegin(Loop* l, s32 step, s32 extraShift) {
    l->verts = currentModelBlockVertices;
    l->polys = fieldPolyCount;
    l->prim = currentModelInstanceDrawPrims - step;
    l->ot = currentOTEntry;
    l->clipY = screenClippingY;
    l->clipX = screenClippingX;
    l->shift = gDepthDivider + extraShift;
    l->wideOnly = 0;
    l->geometry = 0;
}

/* Undo work the native renderer skips, for wide or fractional-only faces. */
static void wideRestore(Loop* l) {
    if (l->wideOnly || l->geometry == PC_GEOMETRY_ONLY) {
        gteLoad(&l->gte);
        l->polys = l->widePolys;
        l->wideOnly = 0;
    }
    l->geometry = 0;
}

/* Keep the native NCLIP decision for VRAM and game state. A thin face can
 * round to zero, or reverse winding, while still visible in the fractional
 * picture. Submit that face only to the display copy and undo its extra
 * GTE work before processing the next polygon. */
static s32 frontFacing(Loop* l, s32 mac0) {
    double area;
    if (pcGeometryActive() && gtePrecisionNclip(&area)) {
        if (mac0 <= 0 && area > 1e-6) {
            if (!l->wideOnly) {
                gteSave(&l->gte);
                l->widePolys = l->polys;
            }
            l->geometry = PC_GEOMETRY_ONLY;
            return 1;
        }
        if (mac0 > 0 && area <= 1e-6) l->geometry = PC_GEOMETRY_HIDE;
    }
    return mac0 > 0;
}

static void loopEnd(Loop* l, s32 step) {
    wideRestore(l);
    fieldPolyCount = l->polys;
    currentModelInstanceDrawPrims = l->prim + step;
}

/* Vertex numbers of an index entry. The loops form v1's offset as
 * (word >> 13) & 0xFFF8, which keeps 13 bits of it; prim8_1 and
 * primD_1 do the same to v0 inside their loop. */
#define IDX0(e) ((u32)(e)[0])
#define IDX0_13(e) ((u32)(e)[0] & 0x1FFF)
#define IDX1(e) ((u32)(e)[1] & 0x1FFF)
#define IDX2(e) ((u32)(e)[2])

/* Every loop skips a polygon when bit 31 of cop2 data register 31 is set.
 * That register is LZCR (a leading-bit count, 1..32), not FLAG, so the test
 * never fires on the hardware; it is kept as written. */
static s32 lzcrSkip(void) {
    return (s32)gteMfc2(GTE_LZCR) < 0;
}

static void readSXY3(u32* sxy) {
    gteSwc2(GTE_SXY0, &sxy[0]);
    gteSwc2(GTE_SXY1, &sxy[1]);
    gteSwc2(GTE_SXY2, &sxy[2]);
}

/* The screen test rejects a polygon only when all its vertices are off the
 * same edge. It compares the packed SXY word (y << 16 | x) unsigned against
 * (height - 1) << 16, so y >= height - 1 and y < 0 both count as off the
 * bottom, and (u16)x against the width, so x < 0 counts as off the right. */
static s32 onScreenY(Loop* l, const u32* sxy, s32 n) {
    s32 i;

    for (i = 0; i < n; i++) {
        if (sxy[i] < l->clipY) {
            return 1;
        }
    }
    if (pcVerticalCull != 0) for (i = 0; i < n; i++) {
        if (PC_WIDE_UY(sxy[i] >> 16, l->clipY >> 16)) {
            if (!l->wideOnly && l->geometry != PC_GEOMETRY_ONLY) {
                l->widePolys = l->polys;
                gteSave(&l->gte);
            }
            l->wideOnly = 1;
            return 1;
        }
    }
    return 0;
}

/* With a wide picture (include/pc_wide.h) the x test covers the margins:
 * PC_WIDE_UX is the same unsigned test when pcWideCull is 0. */
static s32 onScreenX(Loop* l, const u32* sxy, s32 n) {
    s32 i;

    for (i = 0; i < n; i++) {
        if ((sxy[i] & 0xFFFF) < l->clipX) {
            return 1;
        }
    }
    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (PC_WIDE_UX(sxy[i], l->clipX)) {
            if (l->geometry != PC_GEOMETRY_ONLY) {
                l->widePolys = l->polys;
                gteSave(&l->gte);
            }
            l->wideOnly = 1;
            return 1;
        }
    }
    return 0;
}

static void storeXY(u8* prim, const PrimShape* s, s32 i, const u32* sxy) {
    gteCopySxy(prim + s->xy[i], sxy);
}

/* The free field camera's scenery this frame (pcSceneryScope, set by renderObjects around the
 * room's meshes): each primitive and its OT index, for pcSceneryFade. */
int pcSceneryScope = 0; /* initialised: a common symbol would not count as defined (tools/pc_link.py stubs it) */
#define SCENERY_MAX 16384
static struct { u8* prim; s32 ot; } scenery[SCENERY_MAX];
static s32 sceneryCount;

void pcSceneryReset(void) { sceneryCount = 0; }

/* Whether the primitive (a triangle, or a quad as the GPU splits it: 0-1-2 and 1-2-3) covers
 * the screen point (x, y). */
static s32 sideOf(s32 ax, s32 ay, s32 bx, s32 by, s32 x, s32 y) {
    return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}
static s32 inTri(const s32* v, s32 a, s32 b, s32 c, s32 x, s32 y) {
    s32 d0 = sideOf(v[a * 2], v[a * 2 + 1], v[b * 2], v[b * 2 + 1], x, y);
    s32 d1 = sideOf(v[b * 2], v[b * 2 + 1], v[c * 2], v[c * 2 + 1], x, y);
    s32 d2 = sideOf(v[c * 2], v[c * 2 + 1], v[a * 2], v[a * 2 + 1], x, y);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}
static s32 primCovers(const u8* prim, s32 x, s32 y) {
    static const PrimShape* const shapes[8] = {&shapeF3, &shapeFT3, &shapeF4, &shapeFT4,
                                               &shapeG3, &shapeGT3, &shapeG4, &shapeGT4};
    u32 code = PRIM_CODE(prim);
    const PrimShape* sh = shapes[(code >> 2) & 7];
    s32 n = (code & 8) ? 4 : 3;
    s32 v[8];
    s32 i;
    for (i = 0; i < n; i++) {
        v[i * 2] = *(const s16*)(prim + sh->xy[i]);
        v[i * 2 + 1] = *(const s16*)(prim + sh->xy[i] + 2);
    }
    return inTri(v, 0, 1, 2, x, y) || (n == 4 && inTri(v, 1, 2, 3, x, y));
}

void pcSceneryFade(int feetX, int feetY, int headX, int headY, int ot) {
    /* Scenery that covers the party itself, nearer the camera: a primitive whose triangles hold
     * a point of its head, chest or waist (on the line from head to feet). Ground and low cover
     * beside or before its feet hold none of them and stay as drawn: faded, they darkened the
     * floor around the party as it walked. */
    s32 i, k, px[3], py[3];
    static const s32 at[3] = {15, 40, 65}; /* percent of the way from head to feet */
    for (k = 0; k < 3; k++) {
        px[k] = headX + (feetX - headX) * at[k] / 100;
        py[k] = headY + (feetY - headY) * at[k] / 100;
    }
    for (i = 0; i < sceneryCount; i++) {
        if (scenery[i].ot >= ot - 1) continue;          /* not in front of the party */
        for (k = 0; k < 3; k++) {
            if (primCovers(scenery[i].prim, px[k], py[k])) {
                pcFadePrim(scenery[i].prim);
                break;
            }
        }
    }
}

/* Link the primitive at the head of OT entry z >> shift. */
static void linkPrim(const Loop* l, u32 z, u8* prim, u32 len) {
    OTag* entry = &l->ot[(s32)z >> (l->shift & 31)];
    OTag next = *entry;

    if (pcSceneryScope && sceneryCount < SCENERY_MAX) {
        scenery[sceneryCount].prim = prim;
        scenery[sceneryCount++].ot = (s32)z >> (l->shift & 31);
    }

    *entry = OT_LINK(prim);
    *(OTag*)prim = next | OT_LEN(len);
    if (l->wideOnly || pcWideOnlyScope) {
        pcWideOnly(prim);
    }
    if (l->geometry) pcGeometryPrim(prim, l->geometry);
    pcInterpolationPrim(prim);
}

/* z for the depth-sorted modes: the farthest (mode 2, 5) or the nearest
 * (mode 3) vertex. */
static s32 pickZ(s32 z, s32 sz, s32 nearest) {
    if (nearest) {
        return sz < z ? sz : z;
    }
    return sz < z ? z : sz;
}

/* ---- Mode 0: average Z (AVSZ3/AVSZ4) ------------------------------------ */

/* prim3_0 (GT3), prim2_0 (G3), prim4_0 (F3), prim5_0 (FT3). */
static s32 trisAverage(const u16* e, s32 n, const PrimShape* s) {
    Loop l;
    u32 sxy[3];
    s32 mac0;
    u32 z;

    loopBegin(&l, s->size, 0);
    gte_ldv0(&l.verts[IDX0(e)]);
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += s->size;
        gte_ldv0(&l.verts[IDX0(e)]);
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        gte_avsz3();
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        storeXY(l.prim, s, 0, &sxy[0]);
        storeXY(l.prim, s, 1, &sxy[1]);
        storeXY(l.prim, s, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        if (z != 0) {
            linkPrim(&l, z, l.prim, s->len);
        }
    }
    loopEnd(&l, s->size);
    return 0;
}

/* primF_0 (GT4), primE_0 (G4), primC_0 (F4), primD_0 (FT4). The back-face
 * test uses the first triangle; v3 is projected after it. */
static s32 quadsAverage(const u16* e, s32 n, const PrimShape* s) {
    Loop l;
    SVECTOR* v0;
    u32 sxy[4];
    u32 i3;
    s32 skip;
    u32 z;

    loopBegin(&l, s->size, 0);
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += s->size;
        v0 = &l.verts[IDX0(e)];
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        skip = lzcrSkip();
        gte_nclip();
        if (skip) {
            continue;
        }
        i3 = e[-1];
        gteSwc2(GTE_SXY0, &sxy[0]);
        if (!frontFacing(&l, (s32)gteMfc2(GTE_MAC0))) {
            continue;
        }
        gteSwc2(GTE_SXY1, &sxy[1]);
        gte_ldv0(&l.verts[i3]);
        gteSwc2(GTE_SXY2, &sxy[2]);
        gte_rtps();
        skip = lzcrSkip();
        gteSwc2(GTE_SXY2, &sxy[3]);
        gte_avsz4();
        if (skip) {
            continue;
        }
        if (!onScreenY(&l, sxy, 4) || !onScreenX(&l, sxy, 4)) {
            continue;
        }
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        if (z == 0) {
            continue;
        }
        linkPrim(&l, z, l.prim, s->len);
        storeXY(l.prim, s, 0, &sxy[0]);
        storeXY(l.prim, s, 1, &sxy[1]);
        storeXY(l.prim, s, 2, &sxy[2]);
        storeXY(l.prim, s, 3, &sxy[3]);
    }
    loopEnd(&l, s->size);
    return 0;
}

s32 prim3_0(u8* indices, s32 count) {
    return trisAverage((u16*)indices, count, &shapeGT3);
}

s32 prim2_0(u8* indices, s32 count) {
    return trisAverage((u16*)indices, count, &shapeG3);
}

s32 prim4_0(u8* indices, s32 count) {
    return trisAverage((u16*)indices, count, &shapeF3);
}

s32 prim5_0(u8* indices, s32 count) {
    return trisAverage((u16*)indices, count, &shapeFT3);
}

s32 primF_0(u8* indices, s32 count) {
    return quadsAverage((u16*)indices, count, &shapeGT4);
}

s32 primE_0(u8* indices, s32 count) {
    return quadsAverage((u16*)indices, count, &shapeG4);
}

s32 primC_0(u8* indices, s32 count) {
    return quadsAverage((u16*)indices, count, &shapeF4);
}

s32 primD_0(u8* indices, s32 count) {
    return quadsAverage((u16*)indices, count, &shapeFT4);
}

/* ---- Modes 2 and 3: the farthest or nearest vertex's Z, OT shift + 2 ---- */

/* prim6_2/prim6_3 (GT3), prim2_2/prim2_3 (G3), prim4_2/prim4_3 (F3),
 * prim5_2/prim5_3 (FT3). */
static s32 trisDepth(const u16* e, s32 n, const PrimShape* s, s32 nearest) {
    Loop l;
    u32 sxy[3];
    s32 mac0;
    s32 z;

    loopBegin(&l, s->size, 2);
    gte_ldv0(&l.verts[IDX0(e)]);
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += s->size;
        gte_ldv0(&l.verts[IDX0(e)]);
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        storeXY(l.prim, s, 0, &sxy[0]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        storeXY(l.prim, s, 1, &sxy[1]);
        storeXY(l.prim, s, 2, &sxy[2]);
        z = pickZ(gteMfc2(GTE_SZ1), gteMfc2(GTE_SZ2), nearest);
        z = pickZ(z, gteMfc2(GTE_SZ3), nearest);
        l.polys++;
        if (z != 0) {
            linkPrim(&l, z, l.prim, s->len);
        }
    }
    loopEnd(&l, s->size);
    return 0;
}

/* primF_2/primF_3 (GT4), primE_2/primE_3 (G4), primC_2/primC_3
 * (F4), primD_2/primD_3 (FT4). A vertex at Z 0 drops the polygon. */
static s32 quadsDepth(const u16* e, s32 n, const PrimShape* s, s32 nearest) {
    Loop l;
    SVECTOR* v0;
    u32 sxy[4];
    u32 i3;
    s32 skip;
    s32 sz;
    s32 z;
    s32 i;

    loopBegin(&l, s->size, 2);
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += s->size;
        v0 = &l.verts[IDX0(e)];
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        i3 = e[-1];
        if (!frontFacing(&l, (s32)gteMfc2(GTE_MAC0))) {
            continue;
        }
        gte_ldv0(&l.verts[i3]);
        gte_rtps();
        skip = lzcrSkip();
        gteSwc2(GTE_SXY2, &sxy[3]);
        if (skip || !onScreenY(&l, sxy, 4)) {
            continue;
        }
        storeXY(l.prim, s, 0, &sxy[0]);
        if (!onScreenX(&l, sxy, 4)) {
            continue;
        }
        storeXY(l.prim, s, 1, &sxy[1]);
        storeXY(l.prim, s, 2, &sxy[2]);
        z = gteMfc2(GTE_SZ0);
        if (z == 0) {
            continue;
        }
        storeXY(l.prim, s, 3, &sxy[3]);
        for (i = 1; i < 4; i++) {
            sz = gteMfc2(GTE_SZ0 + i);
            if (sz == 0) {
                break;
            }
            z = pickZ(z, sz, nearest);
        }
        if (i < 4) {
            continue;
        }
        l.polys++;
        if (z != 0) {
            linkPrim(&l, z, l.prim, s->len);
        }
    }
    loopEnd(&l, s->size);
    return 0;
}

s32 prim6_2(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeGT3, 0);
}

s32 prim2_2(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeG3, 0);
}

s32 prim4_2(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeF3, 0);
}

s32 prim5_2(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeFT3, 0);
}

s32 primF_2(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeGT4, 0);
}

s32 primE_2(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeG4, 0);
}

s32 primC_2(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeF4, 0);
}

s32 primD_2(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeFT4, 0);
}

s32 prim6_3(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeGT3, 1);
}

s32 prim2_3(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeG3, 1);
}

s32 prim4_3(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeF3, 1);
}

s32 prim5_3(u8* indices, s32 count) {
    return trisDepth((u16*)indices, count, &shapeFT3, 1);
}

s32 primF_3(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeGT4, 1);
}

s32 primE_3(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeG4, 1);
}

s32 primC_3(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeF4, 1);
}

s32 primD_3(u8* indices, s32 count) {
    return quadsDepth((u16*)indices, count, &shapeFT4, 1);
}

/* ---- Mode 1: lit from per-polygon or per-vertex normals ------------------ */

/* F3 (polyRenderDefs row 0): flat colour from the instance data's colour
 * word and face normal (12 bytes per polygon), NCCS. Links even at Z 0. */
s32 prim0_1(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* v0;
    u8* inst;
    u32 sxy[3];
    s32 mac0;
    u32 z;
    u32 c;

    loopBegin(&l, sizeof(POLY_F3), 0);
    v0 = &l.verts[IDX0(e)];
    inst = currentModeBlock18;
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_F3);
        v0 = &l.verts[IDX0(e)];
        inst += 12;
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        storeXY(l.prim, &shapeF3, 0, &sxy[0]);
        gte_avsz3();
        storeXY(l.prim, &shapeF3, 1, &sxy[1]);
        storeXY(l.prim, &shapeF3, 2, &sxy[2]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        c = *(u32*)(inst - 12);
        gte_ldv0(inst - 8);
        gteMtc2(GTE_RGBC, c);
        gte_nccs();
        PRIM_RGBC(l.prim) = (c & 0xFF000000) | (gteMfc2(GTE_RGB2) & 0xFFFFFF);
        linkPrim(&l, z, l.prim, 4);
    }
    currentModeBlock18 = inst;
    loopEnd(&l, sizeof(POLY_F3));
    return 0;
}

/* FT3 (row 1): face normal in the instance data (8 bytes per polygon), NCS;
 * the colour word keeps the primitive's code. */
s32 FT3_FAKE(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* v0;
    u8* inst;
    u32 sxy[3];
    s32 mac0;
    u32 z;

    loopBegin(&l, sizeof(POLY_FT3), 0);
    inst = currentModeBlock18;
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_FT3);
        v0 = &l.verts[IDX0(e)];
        inst += 8;
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        storeXY(l.prim, &shapeFT3, 0, &sxy[0]);
        storeXY(l.prim, &shapeFT3, 1, &sxy[1]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        gte_avsz3();
        storeXY(l.prim, &shapeFT3, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        gteLwc2(0, inst - 8);
        if (z == 0) {
            continue;
        }
        gteLwc2(1, inst - 4);
        gte_ncs();
        PRIM_RGBC(l.prim) = (PRIM_CODE(l.prim) << 24) | (gteMfc2(GTE_RGB2) & 0xFFFFFF);
        linkPrim(&l, z, l.prim, 7);
    }
    currentModeBlock18 = inst;
    loopEnd(&l, sizeof(POLY_FT3));
    return 0;
}

/* GT3 (row 3): per-vertex normals from the model, NCT; Gouraud colours into
 * the three colour words (the first keeps the primitive's code). */
s32 GT3_FAKE(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* normals = currentModelBlockNormals;
    SVECTOR* n0;
    SVECTOR* n1;
    SVECTOR* n2;
    u32 i0;
    u32 i1;
    u32 i2;
    POLY_GT3* p;
    u32 sxy[3];
    s32 mac0;
    u32 z;

    loopBegin(&l, sizeof(POLY_GT3), 0);
    i0 = IDX0(e);
    i1 = IDX1(e);
    i2 = IDX2(e);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(&l.verts[i0]);
        gte_ldv1(&l.verts[i1]);
        gte_ldv2(&l.verts[i2]);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n0 = &normals[i0];
        n1 = &normals[i1];
        n2 = &normals[i2];
        n--;
        e += 4;
        l.prim += sizeof(POLY_GT3);
        i0 = IDX0(e);
        i1 = IDX1(e);
        i2 = IDX2(e);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        storeXY(l.prim, &shapeGT3, 0, &sxy[0]);
        storeXY(l.prim, &shapeGT3, 1, &sxy[1]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        gte_avsz3();
        storeXY(l.prim, &shapeGT3, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        if (z == 0) {
            continue;
        }
        l.polys++;
        gte_ldv0(n0);
        gte_ldv1(n1);
        gte_ldv2(n2);
        gteCommand(GTE_NCT);
        p = (POLY_GT3*)l.prim;
        PRIM_RGBC(p) = (PRIM_CODE(p) << 24) | (gteMfc2(GTE_RGB0) & 0xFFFFFF);
        gteSwc2(GTE_RGB1, &p->r1);
        gteSwc2(GTE_RGB2, &p->r2);
        linkPrim(&l, z, l.prim, 9);
    }
    loopEnd(&l, sizeof(POLY_GT3));
    return 0;
}

/* G3 (row 2): per-vertex normals from the model and a colour word per
 * polygon in the instance data (4 bytes), NCCT. */
s32 prim2_1(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* normals = currentModelBlockNormals;
    SVECTOR* n0;
    SVECTOR* n1;
    SVECTOR* n2;
    u8* inst;
    u32 i0;
    u32 i1;
    u32 i2;
    POLY_G3* p;
    u32 sxy[3];
    s32 mac0;
    u32 z;
    u32 c;

    loopBegin(&l, sizeof(POLY_G3), 0);
    inst = currentModeBlock18;
    i0 = IDX0(e);
    i1 = IDX1(e);
    i2 = IDX2(e);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(&l.verts[i0]);
        gte_ldv1(&l.verts[i1]);
        gte_ldv2(&l.verts[i2]);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n0 = &normals[i0];
        n1 = &normals[i1];
        n2 = &normals[i2];
        n--;
        e += 4;
        inst += 4;
        l.prim += sizeof(POLY_G3);
        i0 = IDX0(e);
        i1 = IDX1(e);
        i2 = IDX2(e);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        storeXY(l.prim, &shapeG3, 0, &sxy[0]);
        storeXY(l.prim, &shapeG3, 1, &sxy[1]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        gte_avsz3();
        storeXY(l.prim, &shapeG3, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        if (z == 0) {
            continue;
        }
        l.polys++;
        gte_ldv0(n0);
        gte_ldv1(n1);
        c = *(u32*)(inst - 4);
        gte_ldv2(n2);
        gteMtc2(GTE_RGBC, c);
        gteCommand(GTE_NCCT);
        p = (POLY_G3*)l.prim;
        PRIM_RGBC(p) = (c & 0xFF000000) | (gteMfc2(GTE_RGB0) & 0xFFFFFF);
        gteSwc2(GTE_RGB1, &p->r1);
        gteSwc2(GTE_RGB2, &p->r2);
        linkPrim(&l, z, l.prim, 6);
    }
    currentModeBlock18 = inst;
    loopEnd(&l, sizeof(POLY_G3));
    return 0;
}

/* F4 (row 8): colour word and face normal in the instance data (12 bytes
 * per polygon), NCCS, average Z. */
s32 prim8_1(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* v0;
    u8* inst;
    u32 sxy[4];
    u32 i3;
    s32 skip;
    u32 z;
    u32 c;

    loopBegin(&l, sizeof(POLY_F4), 0);
    inst = currentModeBlock18;
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_F4);
        v0 = &l.verts[IDX0_13(e)];
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        inst += 12;
        skip = lzcrSkip();
        gte_nclip();
        if (skip) {
            continue;
        }
        i3 = e[-1];
        gteSwc2(GTE_SXY0, &sxy[0]);
        if (!frontFacing(&l, (s32)gteMfc2(GTE_MAC0))) {
            continue;
        }
        gteSwc2(GTE_SXY1, &sxy[1]);
        gte_ldv0(&l.verts[i3]);
        gteSwc2(GTE_SXY2, &sxy[2]);
        gte_rtps();
        skip = lzcrSkip();
        gteSwc2(GTE_SXY2, &sxy[3]);
        gte_avsz4();
        if (skip) {
            continue;
        }
        if (!onScreenY(&l, sxy, 4) || !onScreenX(&l, sxy, 4)) {
            continue;
        }
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        if (z == 0) {
            continue;
        }
        c = *(u32*)(inst - 12);
        gte_ldv0(inst - 8);
        gteMtc2(GTE_RGBC, c);
        gte_nccs();
        storeXY(l.prim, &shapeF4, 0, &sxy[0]);
        storeXY(l.prim, &shapeF4, 1, &sxy[1]);
        storeXY(l.prim, &shapeF4, 2, &sxy[2]);
        storeXY(l.prim, &shapeF4, 3, &sxy[3]);
        PRIM_RGBC(l.prim) = (c & 0xFF000000) | (gteMfc2(GTE_RGB2) & 0xFFFFFF);
        linkPrim(&l, z, l.prim, 5);
    }
    currentModeBlock18 = inst;
    loopEnd(&l, sizeof(POLY_F4));
    return 0;
}

/* FT4 (row 9): face normal in the instance data (8 bytes per polygon), NCS,
 * average Z; the colour word keeps the primitive's code. */
s32 primD_1(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* v0;
    u8* inst;
    u32 sxy[4];
    u32 i3;
    s32 skip;
    u32 z;

    loopBegin(&l, sizeof(POLY_FT4), 0);
    inst = currentModeBlock18;
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_FT4);
        v0 = &l.verts[IDX0_13(e)];
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        inst += 8;
        skip = lzcrSkip();
        gte_nclip();
        if (skip) {
            continue;
        }
        i3 = e[-1];
        gteSwc2(GTE_SXY0, &sxy[0]);
        if (!frontFacing(&l, (s32)gteMfc2(GTE_MAC0))) {
            continue;
        }
        gteSwc2(GTE_SXY1, &sxy[1]);
        gte_ldv0(&l.verts[i3]);
        gteSwc2(GTE_SXY2, &sxy[2]);
        gte_rtps();
        skip = lzcrSkip();
        gteSwc2(GTE_SXY2, &sxy[3]);
        gte_avsz4();
        if (skip) {
            continue;
        }
        if (!onScreenY(&l, sxy, 4) || !onScreenX(&l, sxy, 4)) {
            continue;
        }
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        if (z == 0) {
            continue;
        }
        gte_ldv0(inst - 8);
        gte_ncs();
        storeXY(l.prim, &shapeFT4, 0, &sxy[0]);
        storeXY(l.prim, &shapeFT4, 1, &sxy[1]);
        storeXY(l.prim, &shapeFT4, 2, &sxy[2]);
        storeXY(l.prim, &shapeFT4, 3, &sxy[3]);
        PRIM_RGBC(l.prim) = (PRIM_CODE(l.prim) << 24) | (gteMfc2(GTE_RGB2) & 0xFFFFFF);
        linkPrim(&l, z, l.prim, 9);
    }
    currentModeBlock18 = inst;
    loopEnd(&l, sizeof(POLY_FT4));
    return 0;
}

/* ---- Modes 4 and 5: depth-cued towards the fog colour (DPCS) ------------ */

/* The fogged colour word: DPCS of fogNearColor at the polygon's depth, with
 * the primitive's code minus bit 0 (raw texture off, so the colour shades
 * the texture). */
static u32 foggedRGBC(u8* prim) {
    return ((PRIM_CODE(prim) << 24) & 0xFE000000) | (gteMfc2(GTE_RGB2) & 0xFFFFFF);
}

/* prim5_4 (FT3), func_8002EEF8 (F3): average Z. */
static s32 trisFogAverage(const u16* e, s32 n, const PrimShape* s) {
    Loop l;
    u32 sxy[3];
    s32 mac0;
    u32 z;

    loopBegin(&l, s->size, 0);
    gte_ldv0(&l.verts[IDX0(e)]);
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    gteMtc2(GTE_RGBC, *(u32*)&fogNearColor);
    for (;;) {
        wideRestore(&l);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += s->size;
        gte_ldv0(&l.verts[IDX0(e)]);
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        gte_avsz3();
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        storeXY(l.prim, s, 0, &sxy[0]);
        storeXY(l.prim, s, 1, &sxy[1]);
        storeXY(l.prim, s, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        l.polys++;
        gte_dpcs();
        if (z == 0) {
            continue;
        }
        PRIM_RGBC(l.prim) = foggedRGBC(l.prim);
        linkPrim(&l, z, l.prim, s->len);
    }
    loopEnd(&l, s->size);
    return 0;
}

s32 prim5_4(u8* indices, s32 count) {
    return trisFogAverage((u16*)indices, count, &shapeFT3);
}

s32 func_8002EEF8(u8* indices, s32 count) {
    return trisFogAverage((u16*)indices, count, &shapeF3);
}

/* FT3, mode 5: the farthest vertex's Z, OT shift + 2. */
s32 prim5_5(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    u32 sxy[3];
    s32 mac0;
    s32 onY;
    s32 z;

    loopBegin(&l, sizeof(POLY_FT3), 2);
    gte_ldv0(&l.verts[IDX0(e)]);
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    gteMtc2(GTE_RGBC, *(u32*)&fogNearColor);
    for (;;) {
        wideRestore(&l);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_FT3);
        gte_ldv0(&l.verts[IDX0(e)]);
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        onY = onScreenY(&l, sxy, 3);
        gte_dpcs();
        if (!onY || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        storeXY(l.prim, &shapeFT3, 0, &sxy[0]);
        storeXY(l.prim, &shapeFT3, 1, &sxy[1]);
        storeXY(l.prim, &shapeFT3, 2, &sxy[2]);
        z = pickZ(gteMfc2(GTE_SZ1), gteMfc2(GTE_SZ2), 0);
        z = pickZ(z, gteMfc2(GTE_SZ3), 0);
        l.polys++;
        if (z == 0) {
            continue;
        }
        PRIM_RGBC(l.prim) = foggedRGBC(l.prim);
        linkPrim(&l, z, l.prim, 7);
    }
    loopEnd(&l, sizeof(POLY_FT3));
    return 0;
}

/* FT4: primD_4 (mode 4, average Z) and primD_5 (mode 5, the farthest
 * vertex's Z with OT shift + 2). */
static s32 quadsFog(const u16* e, s32 n, s32 farthest) {
    Loop l;
    SVECTOR* v0;
    u32 sxy[4];
    u32 i3;
    s32 skip;
    s32 onY;
    s32 z;

    loopBegin(&l, sizeof(POLY_FT4), farthest ? 2 : 0);
    v0 = &l.verts[IDX0(e)];
    gte_ldv1(&l.verts[IDX1(e)]);
    gte_ldv2(&l.verts[IDX2(e)]);
    gteMtc2(GTE_RGBC, *(u32*)&fogNearColor);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(v0);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n--;
        e += 4;
        l.prim += sizeof(POLY_FT4);
        v0 = &l.verts[IDX0(e)];
        gte_ldv1(&l.verts[IDX1(e)]);
        gte_ldv2(&l.verts[IDX2(e)]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        i3 = e[-1];
        if (!frontFacing(&l, (s32)gteMfc2(GTE_MAC0))) {
            continue;
        }
        gte_ldv0(&l.verts[i3]);
        gte_rtps();
        skip = lzcrSkip();
        gteSwc2(GTE_SXY2, &sxy[3]);
        if (!farthest) {
            gte_avsz4();
        }
        if (skip) {
            continue;
        }
        onY = onScreenY(&l, sxy, 4);
        if (farthest) {
            gte_dpcs();
        }
        if (!onY) {
            continue;
        }
        storeXY(l.prim, &shapeFT4, 0, &sxy[0]);
        if (!onScreenX(&l, sxy, 4)) {
            continue;
        }
        storeXY(l.prim, &shapeFT4, 1, &sxy[1]);
        storeXY(l.prim, &shapeFT4, 2, &sxy[2]);
        storeXY(l.prim, &shapeFT4, 3, &sxy[3]);
        if (farthest) {
            z = pickZ(gteMfc2(GTE_SZ0), gteMfc2(GTE_SZ1), 0);
            z = pickZ(z, gteMfc2(GTE_SZ2), 0);
            z = pickZ(z, gteMfc2(GTE_SZ3), 0);
        } else {
            z = gteMfc2(GTE_OTZ);
        }
        l.polys++;
        if (!farthest) {
            gte_dpcs();
        }
        if (z == 0) {
            continue;
        }
        PRIM_RGBC(l.prim) = foggedRGBC(l.prim);
        linkPrim(&l, z, l.prim, 9);
    }
    loopEnd(&l, sizeof(POLY_FT4));
    return 0;
}

s32 primD_4(u8* indices, s32 count) {
    return quadsFog((u16*)indices, count, 0);
}

s32 primD_5(u8* indices, s32 count) {
    return quadsFog((u16*)indices, count, 1);
}

/* ---- The self-patched loop (polyRenderDefs row 0x10) -------------------- */

/* The PS1 rewrites the shift amounts and offsets of six instructions in
 * prim10_0 (patchSelfModifyingCode); the PC keeps them here. These are
 * the values assembled into the code. */
static s32 envShiftU = 6;
static s32 envShiftV = 6;
static s16 envOffsetU = 0x40;
static s16 envOffsetV = 0x40;

/* Sets the U and V shift amounts (srl, 5-bit fields) and offsets (addiu,
 * 16-bit immediates) of the mapping in prim10_0. */
void patchSelfModifyingCode(s32 shiftU, s32 shiftV, s32 offsetU, s32 offsetV) {
    envShiftU = shiftU & 31;
    envShiftV = shiftV & 31;
    envOffsetU = offsetU;
    envOffsetV = offsetV;
}

/* One vertex's texture coordinates: its normal rotated by the rotation
 * matrix (MVMVA RT * V0, sf=1), MAC1/MAC2 shifted down (logically) and offset. */
static void envUV(u8* u, u8* v) {
    *u = (gteMfc2(GTE_MAC1) >> envShiftU) + envOffsetU;
    *v = (gteMfc2(GTE_MAC2) >> envShiftV) + envOffsetV;
}

/* FT3 with environment-mapped UVs from the model's vertex normals. */
s32 prim10_0(u8* indices, s32 count) {
    const u16* e = (const u16*)indices;
    s32 n = count;
    Loop l;
    SVECTOR* normals = currentModelBlockNormals;
    u32 i0;
    u32 i1;
    u32 i2;
    SVECTOR* n0;
    SVECTOR* n1;
    SVECTOR* n2;
    POLY_FT3* p;
    u32 sxy[3];
    s32 mac0;
    u32 z;

    loopBegin(&l, sizeof(POLY_FT3), 0);
    i0 = IDX0(e);
    i1 = IDX1(e);
    i2 = IDX2(e);
    gte_ldv1(&l.verts[i1]);
    gte_ldv2(&l.verts[i2]);
    for (;;) {
        wideRestore(&l);
        gte_ldv0(&l.verts[i0]);
        gte_rtpt();
        if (n == 0) {
            break;
        }
        n0 = &normals[i0];
        n1 = &normals[i1];
        n2 = &normals[i2];
        n--;
        e += 4;
        l.prim += sizeof(POLY_FT3);
        i0 = IDX0(e);
        i1 = IDX1(e);
        i2 = IDX2(e);
        gte_ldv1(&l.verts[i1]);
        gte_ldv2(&l.verts[i2]);
        if (lzcrSkip()) {
            continue;
        }
        readSXY3(sxy);
        gte_nclip();
        if (!onScreenY(&l, sxy, 3) || !onScreenX(&l, sxy, 3)) {
            continue;
        }
        mac0 = gteMfc2(GTE_MAC0);
        p = (POLY_FT3*)l.prim;
        storeXY(l.prim, &shapeFT3, 0, &sxy[0]);
        storeXY(l.prim, &shapeFT3, 1, &sxy[1]);
        if (!frontFacing(&l, mac0)) {
            continue;
        }
        gte_avsz3();
        storeXY(l.prim, &shapeFT3, 2, &sxy[2]);
        z = gteMfc2(GTE_OTZ);
        gteLwc2(0, n0);
        if (z == 0) {
            continue;
        }
        gteLwc2(1, (u8*)n0 + 4);
        gte_rtv0();
        l.polys++;
        envUV(&p->u0, &p->v0);
        gte_ldv0(n1);
        gte_rtv0();
        envUV(&p->u1, &p->v1);
        gte_ldv0(n2);
        gte_rtv0();
        envUV(&p->u2, &p->v2);
        linkPrim(&l, z, l.prim, 7);
    }
    loopEnd(&l, sizeof(POLY_FT3));
    return 0;
}

/* ---- Models only the wide picture shows (src/main/model.c) ---------------- */

extern s32 objectClippingMask;
extern s32 fieldPolyCount2;
extern u8* g_currentModelBlockSubBlocks;
extern SVECTOR* currentModelBlockNormals;
s32 submitModelForRendering(void* b, u8* prims, OTag* ot, s32 renderMode);
s32 isObjectClippedWide(void* block, u8 mask);

/* A block the object test drops (off the 4:3 screen): if the widened test
 * keeps it, its polygons go to the wide picture only, and the GTE and the
 * renderer's globals are put back as the PS1 leaves them. */
void pcWideModel(void* b, u8* prims, OTag* ot, s32 renderMode) {
    GteState gte;
    u8* drawPrims;
    OTag* otEntry;
    u8* subBlocks;
    u8* block18;
    SVECTOR* normals;
    SVECTOR* verts;
    s32 polys;
    s32 polys2;
    s32 mask = objectClippingMask;

    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return;
    }
    gteSave(&gte);
    drawPrims = currentModelInstanceDrawPrims;
    otEntry = currentOTEntry;
    subBlocks = g_currentModelBlockSubBlocks;
    block18 = currentModeBlock18;
    normals = currentModelBlockNormals;
    verts = currentModelBlockVertices;
    polys = fieldPolyCount;
    polys2 = fieldPolyCount2;
    if (isObjectClippedWide(b, (u8)mask) == 0) {
        objectClippingMask = 0;
        pcWideOnlyScope = 1;
        submitModelForRendering(b, prims, ot, renderMode);
        pcWideOnlyScope = 0;
        objectClippingMask = mask;
    }
    currentModelInstanceDrawPrims = drawPrims;
    currentOTEntry = otEntry;
    g_currentModelBlockSubBlocks = subBlocks;
    currentModeBlock18 = block18;
    currentModelBlockNormals = normals;
    currentModelBlockVertices = verts;
    fieldPolyCount = polys;
    fieldPolyCount2 = polys2;
    gteLoad(&gte);
}
