/*
 * Battling's arena floor (hand-written assembly at 0x80072D18 in the PS1
 * build; config/handwritten.yaml). PC version of the same logic: primitives
 * in the PC layout (fields by name, sizeof steps) with host pointers.
 *
 * The floor is a 128 x 128 height map (battlingHeightMap, one word per cell: the
 * height in the low half, texture flags in the high half), drawn one row at a
 * time between the columns the caller put in the scratchpad, as two
 * depth-cued POLY_FT3 per cell. Scratchpad layout (shared with the game C,
 * which fills it through the same addresses):
 *   0x000 + row  u8: end column (exclusive), 0 = skip the row
 *   0x080 + row  u8: start column, 0xFF = skip the row
 *   0x100        the cell's four corners (SVECTOR x4), built here
 *   0x120        four UV sets of four u16 (u | v << 8), picked by flags & 3
 *   0x140        four words, clut << 16 | tpage, picked by (flags & 0xC) / 4
 *
 * pcWideBattlingGroundCells (PC only) draws the cells a wide picture needs
 * beside the 4:3 one (include/pc_wide.h; the cone in wide.c).
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"

/* The scratchpad where the game C has it (see include/battling/battling.h). */
#define SCRATCH ((u8*)0x1F800000)

extern u8 battlingBufferIndex;           /* display buffer index */
extern POLY_FT3* battlingGroundPolys[2]; /* floor primitives per display buffer */
extern u32* battlingHeightMap;         /* the height map */
extern s32 fieldPolyCount;
extern s32 fieldPolyCount2;
void copyBattlingWords(void* dst, void* src, s32 size);

#define GTE_SXY0 12
#define GTE_SXY1 13
#define GTE_SXY2 14
#define GTE_SZ1 17
#define GTE_SZ2 18
#define GTE_SZ3 19
#define GTE_RGB2 22
#define GTE_MAC0 24

/* Fill and link one triangle after RTPT/NCLIP passed: depth-cued colour, UVs
 * uvA, uvB, uvC of the cell's set, linked at the farthest vertex's Z / 16. */
static void groundTri(POLY_FT3* p, OTag* ot, u32 flags, u32 page, u32 uvA, u32 uvB, u32 uvC) {
    OTag* entry;
    OTag next;
    s32 z;
    s32 sz;

    gte_dpcs();
    *(u32*)&p->u0 = flags | uvA | (page & 0xFFFF0000);
    *(u32*)&p->u1 = flags | uvB | (page << 16);
    *(u16*)&p->u2 = flags | uvC;
    z = gteMfc2(GTE_SZ1);
    sz = gteMfc2(GTE_SZ2);
    gteSwc2(GTE_RGB2, &p->r0);
    if (!(sz < z)) {
        z = sz;
    }
    sz = gteMfc2(GTE_SZ3);
    if (!(sz < z)) {
        z = sz;
    }
    entry = &ot[(u32)z >> 4];
    next = *entry;
    gteSwc2(GTE_SXY2, &p->x2);
    gteSwc2(GTE_SXY0, &p->x0);
    gteSwc2(GTE_SXY1, &p->x1);
    *entry = OT_LINK(p);
    *(OTag*)p = next | OT_LEN(7);
}

/* Returns the number of primitives drawn (also added to fieldPolyCount). */
s32 renderBattlingGround(OTag* ot, s32 camX, s32 camZ) {
    POLY_FT3* start = battlingGroundPolys[battlingBufferIndex];
    POLY_FT3* p = start;
    SVECTOR* c = (SVECTOR*)(SCRATCH + 0x100); /* corners A, B, C, D */
    u16* uv;
    u32* cell;
    u32* cellEnd;
    u32 word;
    u32 flags;
    u32 page;
    s32 row;
    s32 col;
    s32 endCol;
    s32 x;
    s32 z;

    for (row = 0; row < 0x7F; row++) {
        endCol = SCRATCH[row];
        if (endCol == 0) {
            continue;
        }
        col = SCRATCH[0x80 + row];
        if (col == 0xFF || col >= endCol) {
            continue;
        }
        z = (row << 8) - camZ;
        x = (col << 8) - camX;
        fieldPolyCount2 += endCol - col;
        cell = &battlingHeightMap[row * 128 + col];
        cellEnd = cell + (endCol - col);
        do {
            /* A (x, z), B (x + 256, z + 256), C (x + 256, z), D (x, z + 256),
             * at the heights of their map cells. */
            c[3].vx = x;
            c[0].vx = x;
            c[2].vz = z;
            c[0].vz = z;
            x += 0x100;
            c[2].vx = x;
            c[1].vx = x;
            c[3].vz = z + 0x100;
            c[1].vz = z + 0x100;
            word = cell[0];
            c[0].vy = word;
            c[1].vy = cell[129];
            c[2].vy = cell[1];
            c[3].vy = cell[128];
            gte_ldv0(&c[0]);
            gte_ldv1(&c[1]);
            gte_ldv2(&c[2]);
            flags = word >> 16;
            gte_rtpt();
            uv = (u16*)(SCRATCH + 0x120) + (flags & 3) * 4;
            page = *(u32*)(SCRATCH + 0x140 + (flags & 0xC));
            flags &= 0xF0F0;
            gte_nclip();
            gte_ldv1(&c[3]); /* the second triangle: A, D, B */
            gte_ldv2(&c[1]);
            if ((s32)gteMfc2(GTE_MAC0) > 0) {
                groundTri(p++, ot, flags, page, uv[0], uv[3], uv[1]);
            } else if (pcGeometryRescue()) {
                POLY_FT3* extra = pcGeometryAlloc(p, sizeof(*p));
                if (extra) {
                    GteState saved;
                    gteSave(&saved);
                    groundTri(extra, ot, flags, page, uv[0], uv[3], uv[1]);
                    gteLoad(&saved);
                }
            }
            gte_rtpt();
            gte_nclip();
            cell++;
            if ((s32)gteMfc2(GTE_MAC0) > 0) {
                groundTri(p++, ot, flags, page, uv[0], uv[2], uv[3]);
            } else if (pcGeometryRescue()) {
                POLY_FT3* extra = pcGeometryAlloc(p, sizeof(*p));
                if (extra) {
                    GteState saved;
                    gteSave(&saved);
                    groundTri(extra, ot, flags, page, uv[0], uv[2], uv[3]);
                    gteLoad(&saved);
                }
            }
        } while (cell != cellEnd);
    }
    fieldPolyCount += p - start;
    return p - start;
}

/* ---- The floor beside the 4:3 picture (include/pc_wide.h) ------------------ */

/* GTE FLAG: SZ/OTZ, divide, SX2 and SY2 saturated (a vertex at or behind the
 * camera, or far off the screen). */
#define FLAG_BAD_PROJECTION 0x66000

/* Cells [col, endCol) of row, drawn as renderBattlingGround draws them into
 * primitives only the wide picture has (pcWideAlloc); triangles whose
 * projection saturated are left out (the game's cone keeps such cells near the
 * camera; the wider one would reach them beside it). 0 when the pool is full. */
static s32 wideCells(OTag* ot, s32 row, s32 col, s32 endCol, s32 camX, s32 camZ) {
    SVECTOR* c = (SVECTOR*)(SCRATCH + 0x100);
    POLY_FT3* p;
    u16* uv;
    u32* cell;
    u32* cellEnd;
    u32 word;
    u32 flags;
    u32 page;
    u32 bad;
    s32 x;
    s32 z;

    z = (row << 8) - camZ;
    x = (col << 8) - camX;
    cell = &battlingHeightMap[row * 128 + col];
    cellEnd = cell + (endCol - col);
    do {
        c[3].vx = x;
        c[0].vx = x;
        c[2].vz = z;
        c[0].vz = z;
        x += 0x100;
        c[2].vx = x;
        c[1].vx = x;
        c[3].vz = z + 0x100;
        c[1].vz = z + 0x100;
        word = cell[0];
        c[0].vy = word;
        c[1].vy = cell[129];
        c[2].vy = cell[1];
        c[3].vy = cell[128];
        gte_ldv0(&c[0]);
        gte_ldv1(&c[1]);
        gte_ldv2(&c[2]);
        flags = word >> 16;
        gte_rtpt();
        bad = gteCfc2(31) & FLAG_BAD_PROJECTION;
        uv = (u16*)(SCRATCH + 0x120) + (flags & 3) * 4;
        page = *(u32*)(SCRATCH + 0x140 + (flags & 0xC));
        flags &= 0xF0F0;
        gte_nclip();
        gte_ldv1(&c[3]);
        gte_ldv2(&c[1]);
        int rescue0 = (s32)gteMfc2(GTE_MAC0) <= 0 && pcGeometryRescue();
        if (((s32)gteMfc2(GTE_MAC0) > 0 || rescue0) && !bad) {
            GteState saved;
            if (rescue0) gteSave(&saved);
            p = pcWideAlloc(sizeof(POLY_FT3));
            if (p == NULL) {
                return 0;
            }
            groundTri(p, ot, flags, page, uv[0], uv[3], uv[1]);
            pcWideOnly(p);
            if (rescue0) gteLoad(&saved);
        }
        gte_rtpt();
        bad = gteCfc2(31) & FLAG_BAD_PROJECTION;
        gte_nclip();
        cell++;
        int rescue1 = (s32)gteMfc2(GTE_MAC0) <= 0 && pcGeometryRescue();
        if (((s32)gteMfc2(GTE_MAC0) > 0 || rescue1) && !bad) {
            GteState saved;
            if (rescue1) gteSave(&saved);
            p = pcWideAlloc(sizeof(POLY_FT3));
            if (p == NULL) {
                return 0;
            }
            groundTri(p, ot, flags, page, uv[0], uv[2], uv[3]);
            pcWideOnly(p);
            if (rescue1) gteLoad(&saved);
        }
    } while (cell != cellEnd);
    return 1;
}

/* After renderBattlingGround: the cells of the wider view cone (start/end per
 * row as the scratchpad's spans; port/src/game/battling/wide.c) that the
 * game's cone left out, for the wide picture only. The GTE, the scratchpad
 * and the counters are left as the game left them. */
void pcWideBattlingGroundCells(OTag* ot, s32 camX, s32 camZ, const u8* wideStart, const u8* wideEnd) {
    GteState gte;
    u32 corners[8];
    s32 row;
    s32 ws;
    s32 we;
    s32 gs;
    s32 ge;
    s32 ok = 1;

    if (pcWideCull == 0) {
        return;
    }
    gteSave(&gte);
    copyBattlingWords(corners, SCRATCH + 0x100, sizeof(corners));
    for (row = 0; row < 0x7F && ok; row++) {
        we = wideEnd[row];
        ws = wideStart[row];
        if (we == 0 || ws == 0xFF || ws >= we) {
            continue;
        }
        ge = SCRATCH[row];
        gs = SCRATCH[0x80 + row];
        if (ge == 0 || gs == 0xFF || gs >= ge) {
            ok = wideCells(ot, row, ws, we, camX, camZ);
            continue;
        }
        if (ws < gs) {
            ok = wideCells(ot, row, ws, gs < we ? gs : we, camX, camZ);
        }
        if (ok && ge < we) {
            ok = wideCells(ot, row, ge > ws ? ge : ws, we, camX, camZ);
        }
    }
    copyBattlingWords(SCRATCH + 0x100, corners, sizeof(corners));
    gteLoad(&gte);
}
