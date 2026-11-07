/*
 * Lighting and clipping helpers: light direction/colour matrices, back
 * colour, on-screen tests for triangles and bounding boxes (Noah:
 * isObjectClipped).
 *
 * File 0x80030A30-0x800315A0 (main executable), between hand-written
 * render code and the hand-written add-to-OT stubs. Built with GCC 2.7.2
 * (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"

/* isObjectClippedWide: the same test widened by onScreenMargin to the wide
 * picture's margins (include/pc_wide.h); 0 is the PS1's test. */
static s32 onScreenMargin;
static s32 onScreenVertical;
#define ON_SCREEN(sz, sxy)                                                    \
    ((s16)(sz) != 0 && (s16)(sz) != -1 && \
     (onScreenVertical ? PC_WIDE_UY((u32)(sxy) >> 16, (u32)screenClippingY >> 16) : (u32)(sxy) < (u32)screenClippingY) && \
     (((sxy) + (u32)onScreenMargin) & 0xFFFF) < (u32)screenClippingX + 2 * (u32)onScreenMargin)
/* Midpoint of two bounding-box coordinates, from a towards b. */
#define MID(a, b) ((a) + ((b) - (a)) / 2)

/* Light parameters passed to MulRotationMatrix. */
typedef struct {
    /* 0x00 */ s32 m0_x;  /* direction */
    /* 0x04 */ s32 m4_y;
    /* 0x08 */ s32 m8_z;
    /* 0x0C */ u16 mC_r;  /* colour */
    /* 0x0E */ u16 mE_g;
    /* 0x10 */ u16 m10_b;
} LightDef;

/* ---- dynamic (morphing) vertices: Noah kernel/modelDynamicVertices.cpp ---- */

/* One vertex offset of a morph target. */
typedef struct {
    /* 0x00 */ s16 m0_offset[3];
    /* 0x06 */ s16 m6_index;
} DynVertOffset;

typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ DynVertOffset* m4_positions;
    /* 0x08 */ DynVertOffset* m8_normals;
} ModelDynVertEntry;

/* ModelBlock.m1C: count, count entries, then { index count, s16* indices }. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ ModelDynVertEntry m4_entries[1];
} ModelDynVerts;

/* Noah: sModelBlock (same layout as src/main/model.c) */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u16 m2_numVertices;
    /* 0x04 */ u16 m4_numPrims;
    /* 0x06 */ u16 m6_numMeshBlocks;
    /* 0x08 */ SVECTOR* m8_vertices;
    /* 0x0C */ SVECTOR* mC_normals;
    /* 0x10 */ u8* m10_meshBlocks;
    /* 0x14 */ u8* m14_displayList;
    /* 0x18 */ u8* m18_instanceData;
    /* 0x1C */ ModelDynVerts* m1C_dynamicVertices;
    /* 0x20 */ s16 m20_boundingBox[8];
    /* 0x30 */ s32 m30_instanceDataSize;
    /* 0x34 */ s32 m34_primBufferSize;
} ModelBlock; /* 0x38 */

static MATRIX lightDirectionMatrix; /* light direction matrix (one row per light) */
static MATRIX lightColorMatrix; /* light colour matrix (one column per light) */
extern s32 screenClippingX;
extern s32 screenClippingY;    /* Noah: screenClippingY << 16 */
/* ---- model clipping test (Noah: isObjectClipped) ---- */

s32 isProjectedTriangleOnScreen(void);

/* Set light `index`: direction = -light dir (normalised), colour into
 * column `index` of the colour matrix, then load the colour matrix. */
void MulRotationMatrix(u16 index, LightDef* light) {
    VECTOR v;

    v.vx = -light->m0_x;
    v.vy = -light->m4_y;
    v.vz = -light->m8_z;
    VectorNormalS(&v, (SVECTOR*)lightDirectionMatrix.m[index]);
    lightColorMatrix.m[0][index] = light->mC_r;
    lightColorMatrix.m[1][index] = light->mE_g;
    lightColorMatrix.m[2][index] = light->m10_b;
    gte_SetColorMatrix(&lightColorMatrix);
}

/* Load the light matrix = light directions x m (m: the object rotation). */
void setupLightMatrix(MATRIX* m) {
    MATRIX lm;

    gte_MulMatrix0(&lightDirectionMatrix, m, &lm);
    gte_SetLightMatrix(&lm);
}

/* Ambient (back) colour from 8.4 fixed-point components. */
void setAmbientColorFixed(u16 r, u16 g, u16 b) {
    gte_SetBackColor(r >> 4, g >> 4, b >> 4);
}

/* Ambient (back) colour; same as libgte SetBackColor. */
void setAmbientColor(s32 r, s32 g, s32 b) {
    gte_SetBackColor(r, g, b);
}

typedef struct {
    /* 0x00 */ u32 m0_v01;  /* vertex indices 0 (low) and 1 (high) */
    /* 0x04 */ u16 m4_v2;
    /* 0x06 */ u16 m6_v3;
} QuadIndices;

extern u8* currentModelInstanceDrawPrims;
extern u8* currentModeBlock18;
extern SVECTOR* currentModelBlockVertices;
extern OTag* currentOTEntry;
extern s32 fieldPolyCount;
extern s32 gDepthDivider;           /* Noah: gDepthDivider (OT z shift) */

/* Lit flat quads (POLY_F4, code 0x28): back-face cull, average Z, NCCS
 * colour from the per-face normal/colour stream at currentModeBlock18. */
void renderLitFlatQuads(QuadIndices* idx, s32 count) {
    s32 flag;
    s32 sz0;
    s32 sz1;
    s32 sz2;
    s32 sz3;
    s32 otz;
    POLY_F4* p;
    /* Never used: last keeps a load of idx->m4_v2 after the m6 load alive
     * through CSE (acc reads it at the top of the next iteration), so
     * GCC's combine_givs bases both halfword loads on idx+4 like retail;
     * flow deletes both after loop optimisation (no code). */
    s32 last = 0;
    s32 acc = 0;

    for (count--; count != -1; count--) {
        acc += last;
        p = (POLY_F4*)currentModelInstanceDrawPrims;
        gte_ldv3(&currentModelBlockVertices[idx->m0_v01 & 0xFFFF], &currentModelBlockVertices[idx->m0_v01 >> 16], &currentModelBlockVertices[idx->m4_v2]);
        gte_rtpt();
        gte_stflg(&flag);
        if (flag >= 0) {
            gte_nclip();
            gte_stopz(&flag);
            GteState saved;
            int rescue = 0;
            if (flag <= 0 && pcGeometryRescue()) {
                POLY_F4* extra = pcGeometryAlloc(p, sizeof(*p));
                if (extra) { p = extra; rescue = 1; gteSave(&saved); }
            }
            if (flag > 0 || rescue) {
                gte_stsxy3(&p->x0, &p->x1, &p->x2);
                gte_stsz3(&sz0, &sz1, &sz2);
                gte_ldv0(&currentModelBlockVertices[idx->m6_v3]);
                gte_rtps();
                gte_stsxy(&p->x3);
                gte_stsz(&sz3);
                gte_ldsz4(sz0, sz1, sz2, sz3);
                gte_avsz4();
                gte_stotz(&otz);
                gte_ldrgb(currentModeBlock18);
                gte_ldv0(currentModeBlock18 + 4);
                gte_nccs();
                gte_strgb(&p->r0);
                setlen(p, 5);
                setcode(p, 0x28);
                otz >>= gDepthDivider;
                addPrim(&currentOTEntry[otz], p);
                if (rescue) gteLoad(&saved);
                else
                fieldPolyCount++;
            }
        }
        last = idx->m4_v2;
        idx++;
        currentModelInstanceDrawPrims += sizeof(POLY_F4);
        currentModeBlock18 += 0xC;
    }
}

/* RTPT the three vertices already in V0-V2; 1 if any lands on screen. */
s32 isProjectedTriangleOnScreen(void) {
    s32 sz0;
    s32 sz1;
    s32 sz2;
    s32 sxy;

    gte_rtpt();
    gte_stsz3(&sz0, &sz1, &sz2);
    gte_stsxy0(&sxy);
    if (ON_SCREEN(sz0, sxy)) {
        return 1;
    }
    gte_stsxy1(&sxy);
    if (ON_SCREEN(sz1, sxy)) {
        return 1;
    }
    gte_stsxy2(&sxy);
    if (ON_SCREEN(sz2, sxy)) {
        return 1;
    }
    return 0;
}

/* Noah: isObjectClipped (stubbed there). m20[0..2] / m20[4..6] are the
 * block's bounding-box min / max. Projects triangles over the box corners
 * and midpoints (bit 0: corners, bit 1: edge midpoints); returns 0 as soon
 * as one of them is on screen, else 1. The mask is really a small type: an
 * s32 parameter makes the first test use the saved copy (s2) instead of a1.
 * model.c calls it through an s32 prototype, which is harmless (the callee
 * ignores the upper bits). */
s32 isObjectClipped(ModelBlock* block, u8 mask) {
    SVECTOR v;

    if (mask & 1) {
        gte_ldv0(&block->m20_boundingBox[0]);
        gte_ldv1(&block->m20_boundingBox[4]);
        v.vx = MID(block->m20_boundingBox[0], block->m20_boundingBox[4]);
        v.vy = MID(block->m20_boundingBox[1], block->m20_boundingBox[5]);
        v.vz = MID(block->m20_boundingBox[2], block->m20_boundingBox[6]);
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
        v.vx = block->m20_boundingBox[4];
        v.vy = block->m20_boundingBox[1];
        v.vz = block->m20_boundingBox[2];
        gte_ldv0(&v);
        v.vx = block->m20_boundingBox[0];
        v.vy = block->m20_boundingBox[5];
        v.vz = block->m20_boundingBox[2];
        gte_ldv1(&v);
        v.vx = block->m20_boundingBox[4];
        v.vy = block->m20_boundingBox[5];
        v.vz = block->m20_boundingBox[2];
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
        v.vx = block->m20_boundingBox[0];
        v.vy = block->m20_boundingBox[1];
        v.vz = block->m20_boundingBox[6];
        gte_ldv0(&v);
        v.vx = block->m20_boundingBox[4];
        v.vy = block->m20_boundingBox[1];
        v.vz = block->m20_boundingBox[6];
        gte_ldv1(&v);
        v.vx = block->m20_boundingBox[0];
        v.vy = block->m20_boundingBox[5];
        v.vz = block->m20_boundingBox[6];
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
    }
    if (mask & 2) {
        v.vx = MID(block->m20_boundingBox[0], block->m20_boundingBox[4]);
        v.vy = block->m20_boundingBox[1];
        v.vz = block->m20_boundingBox[2];
        gte_ldv0(&v);
        v.vx = block->m20_boundingBox[0];
        v.vy = block->m20_boundingBox[1];
        v.vz = MID(block->m20_boundingBox[2], block->m20_boundingBox[6]);
        gte_ldv1(&v);
        v.vx = block->m20_boundingBox[0];
        v.vy = MID(block->m20_boundingBox[1], block->m20_boundingBox[5]);
        v.vz = block->m20_boundingBox[2];
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
        v.vx = block->m20_boundingBox[4];
        v.vy = MID(block->m20_boundingBox[5], block->m20_boundingBox[1]);
        v.vz = block->m20_boundingBox[2];
        gte_ldv0(&v);
        v.vx = MID(block->m20_boundingBox[4], block->m20_boundingBox[0]);
        v.vy = block->m20_boundingBox[5];
        v.vz = block->m20_boundingBox[2];
        gte_ldv1(&v);
        v.vx = block->m20_boundingBox[4];
        v.vy = block->m20_boundingBox[5];
        v.vz = MID(block->m20_boundingBox[2], block->m20_boundingBox[6]);
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
        v.vx = block->m20_boundingBox[0];
        v.vy = block->m20_boundingBox[5];
        v.vz = MID(block->m20_boundingBox[6], block->m20_boundingBox[2]);
        gte_ldv0(&v);
        v.vx = block->m20_boundingBox[0];
        v.vy = MID(block->m20_boundingBox[5], block->m20_boundingBox[1]);
        v.vz = block->m20_boundingBox[6];
        gte_ldv1(&v);
        v.vx = MID(block->m20_boundingBox[0], block->m20_boundingBox[4]);
        v.vy = block->m20_boundingBox[5];
        v.vz = block->m20_boundingBox[6];
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
        v.vx = block->m20_boundingBox[4];
        v.vy = block->m20_boundingBox[1];
        v.vz = MID(block->m20_boundingBox[6], block->m20_boundingBox[2]);
        gte_ldv0(&v);
        v.vx = MID(block->m20_boundingBox[4], block->m20_boundingBox[0]);
        v.vy = block->m20_boundingBox[1];
        v.vz = block->m20_boundingBox[6];
        gte_ldv1(&v);
        v.vx = block->m20_boundingBox[4];
        v.vy = MID(block->m20_boundingBox[1], block->m20_boundingBox[5]);
        v.vz = block->m20_boundingBox[6];
        gte_ldv2(&v);
        if (isProjectedTriangleOnScreen()) {
            return 0;
        }
    }
    return 1;
}

/* Whether the block is off the wide picture too (pcWideModel). */
s32 isObjectClippedWide(ModelBlock* block, u8 mask) {
    s32 r;

    onScreenMargin = pcWideCull;
    onScreenVertical = pcVerticalCull;
    r = isObjectClipped(block, mask);
    onScreenMargin = 0;
    onScreenVertical = 0;
    return r;
}
