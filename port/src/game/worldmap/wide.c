/*
 * The world map's wide picture (include/pc_wide.h): what the margins beside
 * the 4:3 picture show. PC only, compiled like the game C.
 *
 * Each pass runs right after the game's own drawing of the same things and
 * adds only what the PS1's tests drop outside its 320x216 screen. The
 * display pass widens both X and Y visibility; a primitive already kept
 * by the native X and Y tests is never added again, so the 4:3
 * picture, and the centre of the wide one, keep exactly the PS1's content.
 * The added primitives come from the pools here (the game's buffers and
 * counters are untouched), are linked PC_WIDE_ONLY (never drawn into VRAM),
 * and the GTE is put back as the game left it; nothing writes the
 * scratchpad.
 *
 *   - Ground (after drawWorldmapGround): the tile frustum's side and vertical planes are
 *     widened to the cull margin, and the window of terrain around the
 *     camera grows from the PS1's 5 x 5 chunks (9 x 9 tiles of 0x400) to
 *     7 x 7 (13 x 13 tiles), trimmed the PS1's way so the camera stays in
 *     the middle tile: at wide aspects the side views reach the PS1
 *     window's edge before the far plane (0xF00), and every chunk of the
 *     bigger window is in the 9 x 9 streamed grid. Chunks still being read
 *     are skipped.
 *   - Billboards (after drawWorldmapBillboards): the same, per chunk.
 *   - Clouds (after drawWorldmapClouds): the yaw cone widened by the angle
 *     the margins add.
 *   - Sky (drawWorldmapSkyGradient, drawWorldmapSkyCloudBand): the PS1's
 *     bands are 0x2000 wide at depth 0x1000 (256 pixels each side of the
 *     centre): pieces beside them reach the widest margin.
 *
 * None of it calls libgte: every libgte function starts with a safe point
 * (port/src/system/hw.c), and under XG_CLOCK=step each one moves the clock on
 * by a microsecond, so work only the wide picture does would change when
 * interrupts arrive and so the game (a random encounter on another frame).
 * The GTE macros and the helpers below do the same work without that.
 */
#include "common.h"
#include "pc_interpolation.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
#include "worldmap/worldmap.h"

#define SCRATCH ((u8*)0x1F800000)
#define WM_SCRATCH ((sWorldmapScratch*)0x1F800000)

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
#define GTE_H 26
#define GTE_FLAG 31

extern s32 worldmapOddOrEven;
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern u8* worldmapChunks[256];
extern s16 worldmapGrid[81];
extern SVECTOR worldmapGridPosition;
extern s32 worldmapWaterWave;
extern s32 worldmapWaterWave2;
extern s16 rcossin_tbl[0x2000]; /* rcossin_tbl: (sin, cos) pairs */
extern VECTOR worldmapFrustumTopPlane;      /* the tile frustum's top and bottom planes */
extern VECTOR worldmapFrustumBottomPlane;
extern sWorldmapCellFromCD* loadedWorldmapCellsFromCD[16];
extern sWorldmapCellFromPC* loadedWorldmapCellsFromPC[16];
extern VECTOR worldmapRadarPosition3;
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;
extern sWorldmapOffsetEntry* worldmapFile1Buffer_14;
extern POLY_FT4* worldmapFT4_3[2];
extern POLY_FT4* worldmapFT4_2[2];
extern VECTOR* worldmapCloudPositions;  /* cloud positions */
extern SVECTOR* worldmapCloudSteps; /* cloud steps (vy: the UV set) */
extern SVECTOR worldmapCloudMidVertices[]; /* cloud quads, middle distance */
extern SVECTOR worldmapCloudNearVertices[]; /* cloud quads, near */
extern u16 worldmapCloudUVs[];     /* cloud UV origins */
extern SVECTOR worldmapRotation;
extern sWorldmapCameraState worldmapCameraVector;
extern MATRIX worldmapMainMatrix2;
extern MATRIX worldMapIdentityMatrix;
extern MATRIX streamingMatrixIdentity;
extern s32 gDepthDivider;
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);

/* ---- libgte without its safe points ------------------------------------------------ */

/* ratan2's arithmetic (the repository's matched C, port/src/gte/libgte.c). */
s32 libgteRatan2(s32 y, s32 x);

/* RotMatrixX / RotMatrixY (port/src/gte/libgte.c): m rotated about X / Y by
 * r, with libgte's sine and cosine (rcossin_tbl) and arithmetic. */
static void wideSinCos(s32 r, s32* s, s32* c) {
    u32 i = (r >= 0 ? (u32)r : -(u32)r) & 0xFFF;

    *s = r >= 0 ? rcossin_tbl[i * 2] : -rcossin_tbl[i * 2];
    *c = rcossin_tbl[i * 2 + 1];
}

static void wideRotMatrixX(s32 r, MATRIX* m) {
    s32 s;
    s32 c;
    s32 a;
    s32 b;
    s32 j;

    wideSinCos(r, &s, &c);
    for (j = 0; j < 3; j++) {
        a = m->m[1][j];
        b = m->m[2][j];
        m->m[1][j] = (s16)((s32)((u32)c * a - (u32)s * b) >> 12);
        m->m[2][j] = (s16)((s32)((u32)s * a + (u32)c * b) >> 12);
    }
}

static void wideRotMatrixY(s32 r, MATRIX* m) {
    s32 s;
    s32 c;
    s32 a;
    s32 b;
    s32 j;

    wideSinCos(r, &s, &c);
    for (j = 0; j < 3; j++) {
        a = m->m[0][j];
        b = m->m[2][j];
        m->m[0][j] = (s16)((s32)((u32)c * a + (u32)s * b) >> 12);
        m->m[2][j] = (s16)((s32)((u32)c * b - (u32)s * a) >> 12);
    }
}

/* RotTransPers4: four screen positions, the flags of both projections, SZ3 / 4. */
static s32 wideRotTransPers4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2,
                             s32* sxy3, s32* flag) {
    s32 f;
    s32 f2;
    s32 sz;

    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gte_stsxy3(sxy0, sxy1, sxy2);
    gte_stflg(&f);
    gte_ldv0(v3);
    gte_rtps();
    gte_stsxy(sxy3);
    gte_stflg(&f2);
    *flag = f2 | f;
    gte_stsz(&sz);
    return sz >> 2;
}

/* ---- the tests ------------------------------------------------------------------ */

/* Some vertex has 0 <= y < 216. */
static s32 inY3(u32 a, u32 b, u32 c) {
    return (u32)((s32)a >> 16) < 0xD8 || (u32)((s32)b >> 16) < 0xD8 || (u32)((s32)c >> 16) < 0xD8;
}

/* Some vertex has 0 <= x < 320: the PS1 keeps it (or drops it for its own
 * reasons, which the margins then share). */
static s32 inX3(u32 a, u32 b, u32 c) {
    return (a & 0xFFFF) < 0x140 || (b & 0xFFFF) < 0x140 || (c & 0xFFFF) < 0x140;
}

static s32 inWideX3(u32 a, u32 b, u32 c) {
    return PC_WIDE_UX(a, 0x140) || PC_WIDE_UX(b, 0x140) || PC_WIDE_UX(c, 0x140);
}

/* Only the display copy has it: visible in its enlarged X/Y bounds,
 * while the original screen's pair of vertex tests drops it. */
static s32 marginOnly3(u32 a, u32 b, u32 c) {
    s32 native = inX3(a, b, c) && inY3(a, b, c);
    s32 extraY = PC_WIDE_UY(a >> 16, 216) || PC_WIDE_UY(b >> 16, 216) || PC_WIDE_UY(c >> 16, 216);
    return !native && inWideX3(a, b, c) && extraY;
}

static void linkWideOnly(OTag* entry, void* prim, u32 len) {
    OTag next = *entry;

    *entry = OT_LINK(prim);
    *(OTag*)prim = next | OT_LEN(len);
    pcWideOnly(prim);
}

/* The half-angle (4096 units) a screen half margin pixels wider than the
 * PS1's 160 subtends at the GTE's projection distance. */
static s32 halfAngle(s32 margin) {
    s32 h = (s32)(gteCfc2(GTE_H) & 0xFFFF);

    if (h <= 0) {
        h = 0x100;
    }
    return libgteRatan2(0xA0 + margin, h);
}

/* ---- ground ---------------------------------------------------------------------- */

#define WIN 7               /* chunks per side of the wide window (the PS1's: 5) */
#define WIN_TILES (WIN * 2) /* tiles per side, before trimming to WIN_TILES - 1 */
#define GROUND_POOL 0x3000
#define BILLBOARD_POOL 0x800

static POLY_FT3 groundPool[2][GROUND_POOL];
static s32 groundUsed;
static POLY_FT4 billboardPool[2][BILLBOARD_POOL];
static s32 billboardUsed;

typedef struct {
    s32 nlx, nlz; /* the left side plane's normal (x, z) */
    s32 nrx, nrz; /* the right one's */
    s32 nty, ntz, nby, nbz; /* top/bottom, widened only for display drawing */
} WidePlanes;

static void widePlanes(WidePlanes* w) {
    s32 a = halfAngle(pcWideCull) & 0xFFF;
    s32 s = rcossin_tbl[a * 2];
    s32 c = rcossin_tbl[a * 2 + 1];
    s32 h = (s32)(gteCfc2(GTE_H) & 0xFFFF);
    s32 top;
    s32 bottom;

    w->nlx = c;
    w->nlz = s;
    w->nrx = -c;
    w->nrz = s;
    if (h <= 0) h = 256;
    w->nty = worldmapFrustumTopPlane.vy;
    w->nby = worldmapFrustumBottomPlane.vy;
    top = w->nty < 0 ? -w->nty : w->nty;
    bottom = w->nby < 0 ? -w->nby : w->nby;
    /* Projection Y = H*y/z: add a positive Z term to move each clip
     * plane outward by the render-only vertical guard. Native planes and
     * the streamed grid remain unchanged. */
    w->ntz = worldmapFrustumTopPlane.vz + top * pcVerticalCull / h;
    w->nbz = worldmapFrustumBottomPlane.vz + bottom * pcVerticalCull / h;
}

/* Whether a ground-level square (four corners, PS1 order: top left, top
 * right, bottom left, bottom right) is outside the widened frustum: all
 * four corners behind one plane (computeTileVisibility's -1). The ground
 * matrix is loaded. */
static s32 outsideWide(const WidePlanes* w, SVECTOR* c) {
    VECTOR p[4];
    s32 i;
    s32 n[4] = {0, 0, 0, 0};

    for (i = 0; i < 4; i++) {
        gte_ldv0(&c[i]);
        gte_rt();
        gte_stlvnl(&p[i]);
    }
    for (i = 0; i < 4; i++) {
        n[0] += (s32)(((s64)p[i].vx * w->nlx + (s64)p[i].vz * w->nlz) >> 12) < 0;
        n[1] += (s32)(((s64)p[i].vx * w->nrx + (s64)p[i].vz * w->nrz) >> 12) < 0;
        n[2] += (s32)(((s64)p[i].vy * w->nty + (s64)p[i].vz * w->ntz) >> 12) < 0;
        n[3] += (s32)(((s64)p[i].vy * w->nby + (s64)p[i].vz * w->nbz) >> 12) < 0;
    }
    return n[0] == 4 || n[1] == 4 || n[2] == 4 || n[3] == 4;
}

/* A chunk buffer a queued streaming read has not filled yet. */
static s32 chunkPending(const u8* buf) {
    s32 s;
    s32 i;

    for (s = 0; s < 16; s++) {
        sWorldmapCellFromCD* cd = loadedWorldmapCellsFromCD[s];
        sWorldmapCellFromPC* pc = loadedWorldmapCellsFromPC[s];

        for (i = 0; cd != NULL && i < 88 && cd->m0_reads[i].m0_sector != 0; i++) {
            if (cd->m0_reads[i].m8_buffer == buf) {
                return 1;
            }
        }
        for (i = 0; pc != NULL && i < 88 && pc->m0_reads[i].m0_filename != NULL; i++) {
            if (pc->m0_reads[i].mC_buffer == buf) {
                return 1;
            }
        }
    }
    return 0;
}

/* The chunk of window square (i, j), -1..5 around the PS1's 0..4; NULL if
 * it is not there yet. */
/* The chunk id of the window's tile (i, j), for smooth motion's identities (windowChunk's). */
static s32 windowChunkId;

static u32* windowChunk(s32 i, s32 j) {
    s32 r = i + worldmapGridPosition.vz;
    s32 c = j + worldmapGridPosition.vx;
    u8* chunk;

    if (r < 0 || r > 8 || c < 0 || c > 8) {
        return NULL;
    }
    windowChunkId = worldmapGrid[r * 9 + c];
    chunk = worldmapChunks[windowChunkId];
    if (chunk == NULL || chunkPending(chunk)) {
        return NULL;
    }
    return (u32*)chunk;
}

/* One ground triangle, V0-V2 loaded (groundTri's steps): -1 on a GTE error
 * (the PS1 then skips the quad's second triangle). */
/* A margin triangle has the identity it has in the PS1's picture (ground.c):
 * the chunk, the cell's word in it and which triangle, so one crossing the
 * screen's edge between frames keeps moving. */
static s32 groundChunkId;
static u32* groundChunkBase;

static s32 wideGroundTri(OTag* ot, u32 word, u32 uv0, u32 uv1, u32 uv2, u32 part) {
    sWorldmapScratch* s = WM_SCRATCH;
    POLY_FT3* p;
    u32 sxy0;
    u32 sxy1;
    u32 sxy2;
    s32 z;
    s32 sz;
    u32 depth;

    gte_rtpt();
    if ((s32)gteCfc2(GTE_FLAG) < 0) {
        return -1;
    }
    gteSwc2(GTE_SXY0, &sxy0);
    gteSwc2(GTE_SXY1, &sxy1);
    gteSwc2(GTE_SXY2, &sxy2);
    if (!marginOnly3(sxy0, sxy1, sxy2)) {
        return 0;
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
        return 0;
    }
    gte_nclip();
    depth = gteMfc2(GTE_IR0);
    if (((s32)gteMfc2(GTE_MAC0) <= 0 && !pcGeometryRescue()) || groundUsed >= GROUND_POOL) {
        return 0;
    }
    if (!(depth < 0x1000)) {
        depth = 0xFFF;
    }
    p = &groundPool[worldmapOddOrEven][groundUsed++];
    setlen(p, 7);
    setcode(p, 0x24);
    p->r0 = p->g0 = p->b0 = 0x80;
    gteCopySxy(&p->x0, &sxy0);
    gteCopySxy(&p->x1, &sxy1);
    gteCopySxy(&p->x2, &sxy2);
    *(u16*)&p->u2 = uv2;
    *(u32*)&p->u0 = (u32)s->m288_clut[(depth >> 7) + (((word >> 5) & 0x40) >> 1)] << 16 | uv0;
    *(u32*)&p->u1 = (u32)s->m308_tpage[(word >> 8) & 7] << 16 | uv1;
    pcInterpolationKeyType(&worldmapChunks[groundChunkId], &worldmapChunks[groundChunkId], part, p, PC_INTERP_TERRAIN);
    linkWideOnly(&ot[z >> 4], p, 7);
    return 0;
}

/* A tile of 8 x 8 cells (drawWorldmapGroundCell and its Sub0): the corner
 * grid with the water's waves, then two triangles per cell. */
static void wideGroundTile(u32* cell, OTag* ot, s32 x0, s32 z) {
    SVECTOR grid[81];
    SVECTOR* out = grid;
    u32* src = cell;
    u32 wave2 = worldmapWaterWave2;
    u32 wave;
    u32 word;
    u32 base;
    u32 uv[4];
    s32 flip;
    s32 row;
    s32 col;
    s32 x;
    s32 c;
    s32 i;
    s32 j;

    for (i = 8; i != -1; i--) {
        x = x0;
        c = rcossin_tbl[(wave2 & 0xFFF) * 2] * 2;
        wave = worldmapWaterWave;
        for (j = 8; j != -1; j--) {
            u32 v = *src;
            if (v & 0x1000) {
                s32 t = (s32)(v << 24) >> 21;
                *(s32*)out = ((((rcossin_tbl[(wave & 0xFFF) * 2] * c) >> 20) + t) << 16) | (x & 0xFFFF);
            } else {
                *(s32*)out = ((s32)(v << 24) >> 5) | (x & 0xFFFF);
            }
            out->vz = z;
            x += 0x80;
            out++;
            src++;
            wave += 0x200;
        }
        z -= 0x80;
        wave2 += 0x200;
    }
    out = grid;
    for (row = 0; row < 8; row++, cell++, out++) {
        for (col = 0; col < 8; col++, cell++, out++) {
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
            gte_ldv0(&out[0]);
            gte_ldv2(&out[9]);
            gte_ldv1(flip ? &out[10] : &out[1]);
            if (wideGroundTri(ot, word, uv[0], flip ? uv[3] : uv[1], uv[2], (u32)(cell - groundChunkBase) * 2) < 0) {
                continue;
            }
            gte_ldv0(&out[1]);
            gte_ldv1(&out[10]);
            gte_ldv2(flip ? &out[0] : &out[9]);
            wideGroundTri(ot, word, uv[1], uv[3], flip ? uv[0] : uv[2], (u32)(cell - groundChunkBase) * 2 + 1);
        }
    }
}

/* Whether tile (r, c) of the wide window (0..13 each, the PS1's 9 x 9 at
 * 2..10 or 3..11) is in it: one row and one column are trimmed, as
 * mapQuadrantTilesPatch does, so the camera's tile is the middle one. */
static s32 inWindow(s32 r, s32 c, s32 quadrant) {
    if (c == ((quadrant & 1) ? 0 : WIN_TILES - 1)) {
        return 0;
    }
    if (r == ((quadrant & 2) ? 0 : WIN_TILES - 1)) {
        return 0;
    }
    return 1;
}

static s32 cameraQuadrant(VECTOR* pos) {
    s32 q = ((pos->vx >> 12) & 0x7FF) < 0x400 ? 0 : 1;

    if (((pos->vz >> 12) & 0x7FF) >= 0x400) {
        q |= 2;
    }
    return q;
}

void pcWideWorldmapGround(OTag* ot, VECTOR* pos) {
    sWorldmapScratch* s = WM_SCRATCH;
    GteState gte;
    WidePlanes w;
    SVECTOR c[4];
    s32 quadrant;
    s32 ox;
    s32 oz;
    s32 i;
    s32 j;
    s32 t;
    u32* chunk;

    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return;
    }
    gteSave(&gte);
    groundUsed = 0;
    gte_SetRotMatrix(&s->m370_groundMatrix);
    gte_SetTransMatrix(&s->m370_groundMatrix);
    widePlanes(&w);
    quadrant = cameraQuadrant(pos);
    ox = -((pos->vx >> 12) & 0x7FF) - 0x1000;
    oz = ((pos->vz >> 12) & 0x7FF) + 0x1000;
    c[0].vy = c[1].vy = c[2].vy = c[3].vy = 0;
    for (i = -1; i < WIN - 1; i++) {
        for (j = -1; j < WIN - 1; j++) {
            chunk = windowChunk(i, j);
            if (chunk == NULL) {
                continue;
            }
            for (t = 0; t < 4; t++) {
                s32 x = ox + j * 0x800 + (t & 1) * 0x400;
                s32 z = oz - i * 0x800 - (t >> 1) * 0x400;
                if (!inWindow((i + 1) * 2 + (t >> 1), (j + 1) * 2 + (t & 1), quadrant)) {
                    continue;
                }
                c[0].vx = c[2].vx = x;
                c[1].vx = c[3].vx = x + 0x400;
                c[0].vz = c[1].vz = z;
                c[2].vz = c[3].vz = z - 0x400;
                if (outsideWide(&w, c)) {
                    continue;
                }
                groundChunkId = windowChunkId;
                groundChunkBase = chunk;
                wideGroundTile(chunk + t * 0x51, ot, x, z);
            }
        }
    }
    gteLoad(&gte);
}

/* ---- billboards ------------------------------------------------------------------ */

/* drawWorldmapCellBillboards (port/src/game/worldmap/ground.c) for the
 * margins: the scratchpad still holds drawWorldmapBillboards' setup. */
static void wideCellBillboards(s16* pos, s32 count, OTag* ot) {
    u32* s = (u32*)SCRATCH;
    POLY_FT4* tmpl = &worldmapFT4_3[worldmapOddOrEven][0];
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
    POLY_FT4* p;
    s32 i;

    for (; count > 0; count--, pos += 4) {
        if (billboardUsed >= BILLBOARD_POOL) {
            return;
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
        if (!marginOnly3(sxy0, sxy1, sxy2)) {
            continue;
        }
        sz = gteMfc2(GTE_SZ3);
        if (!(sz < 0xE00)) {
            continue;
        }
        gte_ldv0(SCRATCH + 0x18);
        gte_rtps();
        depth = gteMfc2(GTE_IR0);
        if (!(depth < 0x1000)) {
            depth = 0xFFF;
        }
        p = &billboardPool[worldmapOddOrEven][billboardUsed++];
        *p = *tmpl;
        gteCopySxy(&p->x0, &sxy0);
        gteCopySxy(&p->x1, &sxy1);
        gteCopySxy(&p->x2, &sxy2);
        gteSwc2(GTE_SXY2, &p->x3);
        p->clut = *(u16*)(SCRATCH + 0x68 + (depth >> 8) * 2);
        pcInterpolationKeyType(&worldmapFile1Buffer_14, &worldmapFile1Buffer_14,
                               (u32)((u8*)pos - (u8*)worldmapFile1Buffer_14) >> 3, p, PC_INTERP_TERRAIN);
        linkWideOnly(&ot[(s32)sz >> 4], p, 9);
    }
}

void pcWideWorldmapBillboards(void) {
    sWorldmapOffsetEntry* table = worldmapFile1Buffer_14;
    GteState gte;
    WidePlanes w;
    MATRIX m;
    SVECTOR c[4];
    VECTOR* pos = &worldmapRadarPosition3;
    s32 ox;
    s32 oz;
    s32 i;
    s32 j;
    s32 k;

    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return;
    }
    gteSave(&gte);
    billboardUsed = 0;
    gte_CompMatrix(&worldmapMainMatrix2, &streamingMatrixIdentity, &m);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    widePlanes(&w);
    ox = -((pos->vx >> 12) & 0x7FF) - 0x1000;
    oz = ((pos->vz >> 12) & 0x7FF) + 0x1000;
    c[0].vy = c[1].vy = c[2].vy = c[3].vy = 0;
    for (i = -1; i < WIN - 1; i++) {
        for (j = -1; j < WIN - 1; j++) {
            s32 r = i + worldmapGridPosition.vz;
            s32 col = j + worldmapGridPosition.vx;

            if (r < 0 || r > 8 || col < 0 || col > 8) {
                continue;
            }
            k = worldmapGrid[r * 9 + col];
            if (table[k].m4_count == 0) {
                continue;
            }
            c[0].vx = c[2].vx = ox + j * 0x800;
            c[1].vx = c[3].vx = ox + j * 0x800 + 0x800;
            c[0].vz = c[1].vz = oz - i * 0x800;
            c[2].vz = c[3].vz = oz - i * 0x800 - 0x800;
            gte_SetRotMatrix(&m);
            gte_SetTransMatrix(&m);
            if (outsideWide(&w, c)) {
                continue;
            }
            wideCellBillboards((s16*)((u8*)table + table[k].m0_offset), table[k].m4_count,
                               pCurrentWorldmapRenderingStruct->m70_OT);
        }
    }
    gteLoad(&gte);
}

/* ---- clouds ---------------------------------------------------------------------- */

#define CLOUD_POOL 0x300

static POLY_FT4 cloudPool[2][CLOUD_POOL];

/* Whether the 2D vector d is inside the cone between yaw - a and yaw + a
 * (drawWorldmapClouds' two NormalClip tests). */
static s32 inCone(u32 d, s32 yaw, s32 a) {
    u32 r = (getAngleSin(yaw + a) << 16) | (getAngleCos(yaw + a) & 0xFFFF);
    u32 l = (getAngleSin(yaw - a) << 16) | (getAngleCos(yaw - a) & 0xFFFF);
    s32 op;

    gte_ldsxy3(d, r, 0);
    gte_nclip();
    gte_stopz(&op);
    if (op > 0) {
        return 0;
    }
    gte_ldsxy3(0, l, d);
    gte_nclip();
    gte_stopz(&op);
    return op <= 0;
}

/* One cloud quad, V0-V2 loaded from v (and v[3]): projected, and added when
 * only the margins have it. 1 when the PS1's loop would stop after it (far
 * near-LOD clouds keep one layer), -1 when it stops before. */
static s32 wideCloudQuad(SVECTOR* v, u32 uv, u32 hi, s32* used, s32 lod, u32 errMask, u32 part) {
    POLY_FT4* p;
    u32 p0;
    u32 p1;
    u32 p2;
    u32 p3;
    s32 sz[4];
    s32 depth;
    s32 inPs1;

    gte_ldv0(&v[0]);
    gte_ldv1(&v[1]);
    gte_ldv2(&v[2]);
    gte_rtpt();
    if (gteCfc2(GTE_FLAG) & errMask) {
        return 0;
    }
    gteSwc2(GTE_SXY0, &p0);
    gteSwc2(GTE_SXY1, &p1);
    gteSwc2(GTE_SXY2, &p2);
    gte_ldv0(&v[3]);
    gte_rtps();
    if (gteCfc2(GTE_FLAG) & errMask) {
        return 0;
    }
    gteSwc2(GTE_SXY2, &p3);
    if (!(PC_WIDE_UY(p0 >> 16, 216) || PC_WIDE_UY(p1 >> 16, 216) ||
          PC_WIDE_UY(p2 >> 16, 216) || PC_WIDE_UY(p3 >> 16, 216))) {
        return 0;
    }
    inPs1 = ((p0 & 0xFFFF) < 0x140 || (p1 & 0xFFFF) < 0x140 || (p2 & 0xFFFF) < 0x140 || (p3 & 0xFFFF) < 0x140) &&
            ((p0 >> 16) < 216 || (p1 >> 16) < 216 || (p2 >> 16) < 216 || (p3 >> 16) < 216);
    if (!inPs1 && !(PC_WIDE_UX(p0, 0x140) || PC_WIDE_UX(p1, 0x140) || PC_WIDE_UX(p2, 0x140) ||
                    PC_WIDE_UX(p3, 0x140))) {
        return 0;
    }
    sz[0] = gteMfc2(16);
    sz[1] = gteMfc2(GTE_SZ1);
    sz[2] = gteMfc2(GTE_SZ2);
    sz[3] = gteMfc2(GTE_SZ3);
    /* The PS1's (odd) depth: the larger of the first two, then the third if
     * larger, else the fourth if larger. */
    depth = sz[0] > sz[1] ? sz[0] : sz[1];
    if (sz[2] > depth) {
        depth = sz[2];
    } else if (sz[3] > depth) {
        depth = sz[3];
    }
    if (lod == 0 && depth > 0xD00) {
        return -1;
    }
    if (!inPs1 && *used < CLOUD_POOL) {
        p = &cloudPool[worldmapOddOrEven][(*used)++];
        *p = worldmapFT4_2[worldmapOddOrEven][0];
        gteCopySxy(&p->x0, &p0);
        gteCopySxy(&p->x1, &p1);
        gteCopySxy(&p->x2, &p2);
        gteCopySxy(&p->x3, &p3);
        *(u16*)&p->u0 = uv;
        *(u16*)&p->u1 = uv | hi;
        *(u16*)&p->u2 = uv | (hi << 8);
        *(u16*)&p->u3 = uv | hi | (hi << 8);
        /* drawWorldmapClouds' identity: the cloud, its detail level, layer and piece. */
        pcInterpolationKeyType(worldmapCloudPositions, worldmapCloudPositions, part, p, PC_INTERP_PARTICLE);
        linkWideOnly(&pCurrentWorldmapRenderingStruct->m70_OT[depth >> 4], p, 9);
    }
    return lod == 0 && depth > 0xB00 ? 1 : 0;
}

void pcWideWorldmapClouds(void) {
    GteState gte;
    MATRIX local;
    MATRIX yawNeg;
    MATRIX yaw;
    MATRIX tmp;
    MATRIX m;
    SVECTOR q[4];
    VECTOR cam;
    VECTOR center;
    s32 extra;
    s32 used = 0;
    s32 i;
    s32 j;
    s32 k;
    s32 l;
    s32 x;
    s32 z;
    s32 uvIndex;
    s32 flag;
    s32 sz;
    u32 d;
    u32 uv;

    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return;
    }
    gteSave(&gte);
    /* The cone's half-angle grows by what the margins add to the screen's. */
    extra = halfAngle(pcWideCull) - halfAngle(0);
    cam.vx = worldmapRadarPosition3.vx & 0x1FFFFFF;
    cam.vz = worldmapRadarPosition3.vz & 0x1FFFFFF;
    local = worldMapIdentityMatrix;
    yawNeg = local;
    yaw = local;
    wideRotMatrixX((worldmapRotation.vx + 0x400) / 8, &local);
    wideRotMatrixY(-worldmapRotation.vy, &yawNeg);
    wideRotMatrixY(worldmapRotation.vy, &yaw);
    gte_MulMatrix0(&local, &yawNeg, &tmp);
    gte_MulMatrix0(&yaw, &tmp, &local);
    center.vx = worldmapCameraVector.m0_eye.vx * 2 + (-getAngleCos(worldmapRotation.vy) >> 1);
    center.vz = worldmapCameraVector.m0_eye.vz * 2 + (-getAngleSin(worldmapRotation.vy) >> 1);
    for (i = 0; i < 0x50; i++) {
        x = (worldmapCloudPositions[i].vx - cam.vx) >> 12;
        z = (worldmapCloudPositions[i].vz - cam.vz) >> 12;
        if (x < -0x1000) {
            x += 0x2000;
        } else if (x >= 0x1000) {
            x -= 0x2000;
        }
        if (z < -0x1000) {
            z += 0x2000;
        } else if (z >= 0x1000) {
            z -= 0x2000;
        }
        z = -z;
        d = ((z - center.vz) << 16) | ((x - center.vx) & 0xFFFF);
        if (!inCone(d, worldmapRotation.vy, 0x169 + extra)) {
            continue;
        }
        local.t[0] = x;
        local.t[1] = worldmapCloudPositions[i].vy >> 12;
        local.t[2] = z;
        gte_CompMatrix(&worldmapMainMatrix2, &local, &m);
        gte_SetRotMatrix(&m);
        gte_SetTransMatrix(&m);
        q[0].vx = q[0].vy = q[0].vz = 0;
        gte_ldv0(&q[0]);
        gte_rtps();
        uvIndex = worldmapCloudSteps[i].vy * 4;
        gte_stflg(&flag);
        if (flag & 0x7F85E000) {
            continue;
        }
        gte_stsz(&sz);
        if (sz > 0x580) {
            /* Near: three layers; a far one keeps the first drawn. */
            for (j = 0; j < 3; j++) {
                uv = worldmapCloudUVs[uvIndex++];
                if (wideCloudQuad(&worldmapCloudNearVertices[j * 4], uv, 0x3F, &used, 0, 0x80000000, i << 8 | 0 << 6 | j << 4) != 0) {
                    break;
                }
            }
        } else if (sz > 0x400) {
            for (j = 0; j < 3; j++) {
                for (k = 0; k < 4; k++) {
                    uv = worldmapCloudUVs[j + uvIndex] + ((k & 2) << 12) + ((k & 1) << 5);
                    wideCloudQuad(&worldmapCloudMidVertices[(j * 4 + k) * 4], uv, 0x1F, &used, 1, 0x80000000, i << 8 | 1 << 6 | j << 4 | k);
                }
            }
        } else {
            for (j = 0; j < 3; j++) {
                s16 rx = worldmapCloudNearVertices[0].vx;
                s16 rz = worldmapCloudNearVertices[0].vz;
                s16 ry = worldmapCloudNearVertices[0].vy - j * 8;

                /* The PS1 first checks the whole 0x180 square's corners. */
                q[0].vx = q[2].vx = rx;
                q[0].vz = q[1].vz = rz;
                q[1].vx = q[3].vx = rx + 0x180;
                q[2].vz = q[3].vz = rz - 0x180;
                q[0].vy = q[1].vy = q[2].vy = q[3].vy = ry;
                gte_ldv0(&q[0]);
                gte_rtps();
                gte_stflg(&flag);
                if (flag & 0x7F85E000) {
                    continue;
                }
                gte_ldv3c(&q[1]);
                gte_rtpt();
                gte_stflg(&flag);
                if (flag & 0x7F85E000) {
                    continue;
                }
                for (k = 0; k < 4; k++) {
                    for (l = 0; l < 4; l++) {
                        s16 qx = rx + l * 0x60;
                        s16 qz = rz - k * 0x60;

                        q[0].vx = q[2].vx = qx;
                        q[0].vz = q[1].vz = qz;
                        q[1].vx = q[3].vx = qx + 0x60;
                        q[2].vz = q[3].vz = qz - 0x60;
                        uv = worldmapCloudUVs[j + uvIndex] + ((k << 12) + (l << 4));
                        wideCloudQuad(q, uv, 0xF, &used, 2, 0x7F85E000, i << 8 | 2 << 6 | j << 4 | k << 2 | l);
                    }
                }
            }
        }
    }
    gteLoad(&gte);
}

/* ---- sky ------------------------------------------------------------------------- */

#define SKY_PIECES 3 /* per side and band, each 0x800 wide */

static POLY_G4 skyPool[2][4 * 2 * SKY_PIECES];
static POLY_FT4 cloudBandPool[2][2 * 2];

/* Pieces beside the sky gradient's band v (four corners: top left, top
 * right, bottom left, bottom right; x -0x1000..0x1000), copies of p, with
 * the rotation and translation loaded. */
void pcWideWorldmapSkyBand(s32 band, SVECTOR* v, POLY_G4* p) {
    POLY_G4* out = &skyPool[worldmapOddOrEven][band * 2 * SKY_PIECES];
    SVECTOR c[4];
    GteState gte;
    s32 side;
    s32 n;
    s32 x0;
    s32 x1;
    s32 z;
    s32 flag;

    if (pcWideView == 0) {
        return;
    }
    gteSave(&gte);
    for (side = 0; side < 2; side++) {
        for (n = 0; n < SKY_PIECES; n++, out++) {
            x0 = 0x1000 + n * 0x800;
            x1 = x0 + 0x800;
            if (side == 0) {
                x0 = -x0;
                x1 = -x1;
            }
            c[0] = v[0];
            c[1] = v[1];
            c[2] = v[2];
            c[3] = v[3];
            /* Left: from x1 to x0 (the outer edge first); right: x0 to x1. */
            c[0].vx = c[2].vx = side == 0 ? x1 : x0;
            c[1].vx = c[3].vx = side == 0 ? x0 : x1;
            *out = *p;
            z = wideRotTransPers4(&c[0], &c[1], &c[2], &c[3], (s32*)&out->x0, (s32*)&out->x1, (s32*)&out->x2,
                                  (s32*)&out->x3, &flag);
            if (flag < 0) {
                continue;
            }
            pcInterpolationKeyType(skyPool, skyPool, (u32)(out - skyPool[worldmapOddOrEven]), out, PC_INTERP_MODEL);
            linkWideOnly(&pCurrentWorldmapRenderingStruct->m70_OT[z >> gDepthDivider], out, 8);
        }
    }
    gteLoad(&gte);
}

/* Pieces beside the cloud band (two textured quads of 0x1000, each with
 * the texture's 0x80-texel window once): one more of each on each side,
 * linked into ot between the band's texture-window packets. */
void pcWideWorldmapCloudBand(SVECTOR* v, POLY_FT4* left, POLY_FT4* right, OTag* ot) {
    POLY_FT4* out = &cloudBandPool[worldmapOddOrEven][0];
    SVECTOR c[4];
    GteState gte;
    s32 side;
    s32 n;
    s32 flag;

    if (pcWideView == 0) {
        return;
    }
    gteSave(&gte);
    for (side = 0; side < 2; side++) {
        for (n = 1; n <= 2; n++, out++) {
            s32 x0 = side == 0 ? -0x1000 - n * 0x1000 : n * 0x1000;

            c[0] = v[side * 4 + 0];
            c[1] = v[side * 4 + 1];
            c[2] = v[side * 4 + 2];
            c[3] = v[side * 4 + 3];
            c[0].vx = c[2].vx = x0;
            c[1].vx = c[3].vx = x0 + 0x1000;
            *out = side == 0 ? *left : *right;
            wideRotTransPers4(&c[0], &c[1], &c[2], &c[3], (s32*)&out->x0, (s32*)&out->x1, (s32*)&out->x2,
                              (s32*)&out->x3, &flag);
            if (flag < 0) {
                continue;
            }
            pcInterpolationKeyType(cloudBandPool, cloudBandPool, (u32)(out - cloudBandPool[worldmapOddOrEven]), out, PC_INTERP_MODEL);
            linkWideOnly(ot, out, 9);
        }
    }
    gteLoad(&gte);
}
