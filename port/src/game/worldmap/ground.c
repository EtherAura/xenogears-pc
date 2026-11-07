/*
 * The world map's ground cells and billboards (hand-written assembly at
 * 0x8009980C in the PS1 build; config/handwritten.yaml). PC versions of the
 * same logic: primitives in the PC layout (fields by name, sizeof steps)
 * with host pointers.
 *
 * Both work from the scratchpad, where the game C (drawWorldmapGround,
 * drawWorldmapGroundCell, the billboard setup) leaves their inputs, through
 * the same address the game C uses (WM_SCRATCH in src/worldmap).
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
#include "pc_interpolation.h"

#define SCRATCH ((u8*)PSX_SCRATCH(0x1F800000))

extern s32 renderWorldmapGroundVar1; /* ground POLY_FT3 used this frame */
/* Billboard POLY_FT4 used this frame. The game C has it as an s16; the PS1
 * code here reads and writes the whole word, whose high half stays 0. */
extern s16 worldmapBillboardPolyCount;
extern VECTOR worldmapRadarPosition3;
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;
extern u8* worldmapChunks[256];
extern void* worldmapFile1Buffer_14; /* the billboards' table: an entry's offset in it is its identity */

/* The chunk the cells drawn next belong to: its slot in worldmapChunks
 * (static: the chunk's buffer comes and goes as the map streams) and its
 * buffer, so a ground triangle's identity for smooth motion is the chunk, the
 * cell's word in it and which of the cell's two triangles. */
static s32 groundChunk = -1;
static u32* groundBase;

void pcWorldmapGroundChunk(s32 id, u32* base) {
    groundChunk = id;
    groundBase = base;
}

#define GTE_SXY0 12
#define GTE_SXY1 13
#define GTE_SXY2 14
#define GTE_SZ1 17
#define GTE_SZ2 18
#define GTE_SZ3 19
#define GTE_MAC0 24
#define GTE_MAC1 25
#define GTE_MAC2 26
#define GTE_MAC3 27
#define GTE_IR0 8
#define GTE_FLAG 31

/* Both routines keep a projected polygon when some vertex has 0 <= x < 320
 * and some (maybe another) has 0 <= y < 216. */
static s32 onScreen3(u32 a, u32 b, u32 c) {
    if (!((a & 0xFFFF) < 0x140 || (b & 0xFFFF) < 0x140 || (c & 0xFFFF) < 0x140)) {
        return 0;
    }
    return (u32)((s32)a >> 16) < 0xD8 || (u32)((s32)b >> 16) < 0xD8 || (u32)((s32)c >> 16) < 0xD8;
}

static void linkPrim(OTag* entry, void* prim, u32 len) {
    OTag next = *entry;

    *entry = OT_LINK(prim);
    *(OTag*)prim = next | OT_LEN(len);
}

/* One ground triangle, V0-V2 loaded. 0: drawn; 1: dropped; -1: the GTE
 * flagged an error, which drops the rest of the cell. */
static s32 groundTri(POLY_FT3** pp, OTag* ot, u32 word, u32 uv0, u32 uv1, u32 uv2, u32 part) {
    POLY_FT3* p = *pp;
    int rescue = 0;
    u32 sxy0;
    u32 sxy1;
    u32 sxy2;
    s32 z;
    s32 sz;
    u32 depth;
    u32 clut;
    u32 tpage;
    OTag* entry;

    gte_rtpt();
    if ((s32)gteCfc2(GTE_FLAG) < 0) {
        return -1;
    }
    gteSwc2(GTE_SXY0, &sxy0);
    gteSwc2(GTE_SXY1, &sxy1);
    gteSwc2(GTE_SXY2, &sxy2);
    if (!onScreen3(sxy0, sxy1, sxy2)) {
        return 1;
    }
    z = gteMfc2(GTE_SZ1);
    sz = gteMfc2(GTE_SZ2);
    if (!(sz < z)) {
        z = sz;
    }
    sz = gteMfc2(GTE_SZ3);
    if (!(sz < z)) {
        z = sz;
    }
    if (!((u32)z < 0xF00)) {
        return 1;
    }
    gte_nclip();
    entry = &ot[z >> 4];
    depth = gteMfc2(GTE_IR0);
    if ((s32)gteMfc2(GTE_MAC0) <= 0) {
        if (!pcGeometryRescue() || !(p = pcGeometryAlloc(p, sizeof(*p)))) return 1;
        rescue = 1;
    }
    if (!(depth < 0x1000)) {
        depth = 0xFFF;
    }
    /* The CLUT by depth (32 steps, bit 11 of the cell picks the second set of
     * 32), the texture page by bits 8-10. */
    tpage = *(u16*)(SCRATCH + 0x308 + ((word >> 8) & 7) * 2);
    clut = *(u16*)(SCRATCH + 0x288 + (depth >> 7) * 2 + ((word >> 5) & 0x40));
    if (!rescue) renderWorldmapGroundVar1++;
    gteCopySxy(&p->x0, &sxy0);
    gteCopySxy(&p->x1, &sxy1);
    gteCopySxy(&p->x2, &sxy2);
    *(u16*)&p->u2 = uv2;
    *(u32*)&p->u0 = clut << 16 | uv0;
    *(u32*)&p->u1 = tpage << 16 | uv1;
    if (groundChunk >= 0) {
        pcInterpolationKeyType(&worldmapChunks[groundChunk], &worldmapChunks[groundChunk], part, p, PC_INTERP_TERRAIN);
    }
    linkPrim(entry, p, 7);
    if (!rescue) *pp = p + 1;
    return rescue ? 1 : 0;
}

/* Draws the 8 x 8 cells of a ground block as two POLY_FT3 each. The 9 x 9
 * corner grid (SVECTOR, rows of 9) is at the start of the scratchpad; cell
 * words come in rows of 9 (the ninth is the next block's edge): bits 16-23
 * the UV origin, 13-14 the UV rotation, 15 which diagonal splits the cell,
 * 8-10 the texture page, 11 the CLUT set. Stops once 0x7FE primitives are
 * used (renderWorldmapGroundVar1). */
void drawWorldmapGroundCellSub0(u32* cell, void* ot, void* prims) {
    POLY_FT3* p = (POLY_FT3*)prims;
    SVECTOR* grid = (SVECTOR*)SCRATCH;
    u32 word;
    u32 base;
    u32 uv[4]; /* top left, top right, bottom left, bottom right */
    s32 flip;
    s32 row;
    s32 col;
    s32 r;

    for (row = 0; row < 8; row++, cell++, grid++) {
        for (col = 0; col < 8; col++, cell++, grid++) {
            word = *cell;
            flip = (word & 0x8000) != 0;
            base = ((word >> 12) & 0xF0) | ((word >> 8) & 0xF000);
            switch ((word >> 13) & 3) {
            case 0:
                uv[0] = base;
                uv[1] = base + 0xF;
                uv[2] = base + 0xF00;
                uv[3] = base + 0xF0F;
                break;
            case 1:
                uv[0] = base + 0xF;
                uv[1] = base;
                uv[2] = base + 0xF0F;
                uv[3] = base + 0xF00;
                break;
            case 2:
                uv[0] = base + 0xF00;
                uv[1] = base + 0xF0F;
                uv[2] = base;
                uv[3] = base + 0xF;
                break;
            default:
                uv[0] = base + 0xF0F;
                uv[1] = base + 0xF00;
                uv[2] = base + 0xF;
                uv[3] = base;
                break;
            }
            if (!((u32)renderWorldmapGroundVar1 < 0x7FE)) {
                return;
            }
            /* Top left, bottom left, and the bottom right or top right corner. */
            gte_ldv0(&grid[0]);
            gte_ldv2(&grid[9]);
            gte_ldv1(flip ? &grid[10] : &grid[1]);
            r = groundTri(&p, ot, word, uv[0], flip ? uv[3] : uv[1], uv[2], (groundBase ? (u32)(cell - groundBase) : 0) * 2);
            if (r < 0) {
                continue;
            }
            /* Top right, bottom right, and the top left or bottom left corner. */
            gte_ldv0(&grid[1]);
            gte_ldv1(&grid[10]);
            gte_ldv2(flip ? &grid[0] : &grid[9]);
            groundTri(&p, ot, word, uv[1], uv[3], flip ? uv[0] : uv[2], (groundBase ? (u32)(cell - groundBase) : 0) * 2 + 1);
        }
    }
}

/* Draws count billboards (POLY_FT4 prepared by the caller) at the 8-byte
 * positions (x, y, z): each is wrapped around the map edges relative to the
 * camera, placed with the scratchpad's two matrices and offset, and its four
 * scratchpad corners projected. Stops once 0x200 billboards are used
 * (worldmapBillboardPolyCount). count >= 1. */
void drawWorldmapCellBillboards(s16* pos, s32 count, OTag* ot, POLY_FT4* p) {
    u32* s = (u32*)SCRATCH;
    s32 used = (u16)worldmapBillboardPolyCount;
    s32 camX = worldmapRadarPosition3.vx >> 12;
    s32 camZ = worldmapRadarPosition3.vz >> 12;
    s32 sizeX = worldmapSizeX << 11;
    s32 sizeZ = worldmapSizeY << 11;
    s32 x;
    s32 z;
    s32 tx;
    s32 ty;
    s32 tz;
    u32 sxy0;
    u32 sxy1;
    u32 sxy2;
    u32 sz;
    u32 depth;
    OTag* entry;
    s32 i;

    do {
        if (!(used < 0x200)) {
            break;
        }
        x = pos[0] - camX;
        z = pos[2] - camZ;
        if (x < -0x4000) {
            x += sizeX;
        }
        if (!(x < 0x4000)) {
            x -= sizeX;
        }
        if (z < -0x4000) {
            z += sizeZ;
        }
        if (!(z < 0x4000)) {
            z -= sizeZ;
        }
        for (i = 0; i < 5; i++) {
            gteCtc2(i, s[0x28 / 4 + i]);
        }
        gteMtc2(0, (x & 0xFFFF) | ((u32)pos[1] << 16));
        gteMtc2(1, -z & 0xFFFF);
        gte_rtv0();
        tx = gteMfc2(GTE_MAC1) + s[0x3C / 4];
        ty = gteMfc2(GTE_MAC2) + s[0x40 / 4];
        tz = gteMfc2(GTE_MAC3) + s[0x44 / 4];
        for (i = 0; i < 5; i++) {
            gteCtc2(i, s[0x48 / 4 + i]);
        }
        gteCtc2(5, tx);
        gteCtc2(6, ty);
        gteCtc2(7, tz);
        gte_ldv3c(SCRATCH);
        gte_rtpt();
        if ((s32)gteCfc2(GTE_FLAG) < 0) {
            continue;
        }
        gteSwc2(GTE_SXY0, &sxy0);
        gteSwc2(GTE_SXY1, &sxy1);
        gteSwc2(GTE_SXY2, &sxy2);
        if (!onScreen3(sxy0, sxy1, sxy2)) {
            continue;
        }
        sz = gteMfc2(GTE_SZ3);
        if (!(sz < 0xE00)) {
            continue;
        }
        gte_ldv0(SCRATCH + 0x18);
        entry = &ot[(s32)sz >> 4];
        gte_rtps();
        depth = gteMfc2(GTE_IR0);
        if (!(depth < 0x1000)) {
            depth = 0xFFF;
        }
        gteCopySxy(&p->x0, &sxy0);
        gteCopySxy(&p->x1, &sxy1);
        gteCopySxy(&p->x2, &sxy2);
        gteSwc2(GTE_SXY2, &p->x3);
        pcInterpolationKeyType(&worldmapFile1Buffer_14, &worldmapFile1Buffer_14,
                               (u32)((u8*)pos - (u8*)worldmapFile1Buffer_14) >> 3, p, PC_INTERP_TERRAIN);
        linkPrim(entry, p, 9);
        p->clut = *(u16*)(SCRATCH + 0x68 + (depth >> 8) * 2);
        p++;
        used++;
    } while (pos += 4, --count != 0);
    worldmapBillboardPolyCount = used;
}
