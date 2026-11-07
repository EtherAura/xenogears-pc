/*
 * Position checks against the ground, world streaming (CD and PC reads, grids,
 * chunk cache), task system, camera matrices, ground rendering (Noah: worldmapPlayer.cpp,
 * worldmapWorldStreaming.cpp, worldmapTask.cpp, worldmapTaskGround.cpp, worldmap.cpp).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "worldmap/worldmap.h"

extern sWorldmapState* gWorldmapState;

/* worldmapWorldStreaming.cpp */
extern s32 worldmapCellLoadingPending;
extern s32 worldmapNextCellIndex;
extern s32 worldmapCellLoadSlot;
extern s32 worldmapStreamingStateMachineState;
extern s32 worldmapStreamingStateMachineNumPendingLoads;
extern sWorldmapCellFromCD* worldmapCellsFromCD;     /* Noah: worldmapCellsFromCD (16) */
extern sWorldmapCellFromPC* worldmapCellsFromPC;     /* Noah: worldmapCellsFromPC (16) */
extern u8* worldmapExtraData;                      /* Noah: worldmapExtraData (0x800) */
extern sWorldmapCellFromCD* loadedWorldmapCellsFromCD[16];
extern sWorldmapCellFromPC* loadedWorldmapCellsFromPC[16];
extern u8 worldmapStreamingCDSyncStatus[8];
extern sWorldmapCellFromCDSub* volatile worldmapStreamingNextRead; /* current CD read entry, advanced by the CdReady callback: volatile (retail keeps both stores in loadWorldmapChunkFromCD) */
extern CdlLOC worldmapStreamingCdLoc;                   /* current CD read position */
extern s32 worldmapStreamingReadSector;
extern s32 worldmapStreamingWantedSector;
extern s32 worldmapStreamingSectorsLeft;
extern s32 worldmapStreamingBytesLeft;
extern u8* worldmapStreamingDest;
extern s32 D_8009BE48;
extern s32 D_8009CCB0;
extern s32 worldmapStreamingShellOpenCount;
extern s32 worldmapStreamingReadErrorCount;
extern s32 worldmapStreamingSectorHeader[3]; /* CdGetSector reads 3 words here; its first is the sector position (CdlLOC) */

/* worldmap.cpp */
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern void* worldmapFile2Buffer;
extern u8 worldMapFarColor[3];
extern u16 worldmapGroundCluts[0x40];                /* Noah: USHORT_ARRAY_8009ccb4 (ground CLUTs) */
extern u16 worldmapGroundTpages[8];                   /* Noah: USHORT_ARRAY_8009cd54 (ground TPAGEs) */
void tweakWorldmapTexture(u16* src, u16* dst, s32 steps, u8* color);
void loadMenuImageBundle(void* data);

/* worldmapWorldStreaming.cpp (grid) */
extern u8* worldmapChunks[256];
extern s16 worldmapGrid[81];                  /* Noah: worldmapGrid (9x9 chunk ids) */
extern s16 worldmapGrid2[81];                  /* Noah: worldmapGrid2 (previous grid) */
extern s32 worldmapFile9;
extern s32 worldmapFile10;
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;
extern VECTOR worldmapFrustumUpAxis;                   /* Noah: VECTOR_worldmap__8009bb4c */
extern VECTOR worldmapFrustumSideAxis;
extern VECTOR worldmapFrustumLeftEdge;
extern VECTOR worldmapFrustumRightEdge;
extern VECTOR worldmapFrustumTopEdge;
extern VECTOR worldmapFrustumBottomEdge;
extern VECTOR worldmapFrustumLeftPlane;                   /* Noah: dots[0] */
extern VECTOR worldmapFrustumRightPlane;                   /* Noah: dots[1] */
extern VECTOR worldmapFrustumTopPlane;                   /* Noah: dots[2] */
extern VECTOR worldmapFrustumBottomPlane;                   /* Noah: dots[3] */
extern s16 worldmapGridUpdateMask;
extern s16 rcossin_tbl[0x2000];              /* main: rcossin_tbl (sin, cos pairs) */
extern SVECTOR worldmapGridPosition;
extern s32 renderWorldmapGroundVar1;                      /* Noah: renderWorldmapGroundVar1 (used POLY_FT3) */
extern s16 megatilesVisibility[5][5];
extern s16 tilesVisibility[5][5][4];
extern s16 worldmapGridCornerIndices[4];                   /* Noah: USHORT_ARRAY_worldmap__8009bbac (grid corners) */
extern s32 mapQuadrantTilesPatch[4][25][2];            /* Noah: mapQuadrantTilesPatch (s16 [4][5][5][4]) */
#define WM_SCRATCH ((sWorldmapScratch*)PSX_SCRATCH(0x1F800000))
extern MATRIX streamingMatrixIdentity;
extern VECTOR worldmapGridInputPosition;
extern s32 worldmapWaterWave;
extern s32 worldmapWaterWave2;

/* worldmapTaskGround.cpp / camera */
extern MATRIX worldMapIdentityMatrix;
extern MATRIX worldmapMainMatrix2;
extern SVECTOR worldmapRotation;

/* Scratchpad work areas (Noah names them after their addresses) */
#define SCRATCH_VECTOR_000 ((VECTOR*)PSX_SCRATCH(0x1F800000))
#define SCRATCH_VECTOR_010 ((VECTOR*)PSX_SCRATCH(0x1F800010))
#define SCRATCH_VECTOR_020 ((VECTOR*)PSX_SCRATCH(0x1F800020))
#define SCRATCH_VECTOR_030 ((VECTOR*)PSX_SCRATCH(0x1F800030))
#define SCRATCH_SVECTOR_040 ((SVECTOR*)PSX_SCRATCH(0x1F800040))
#define SCRATCH_MATRIX_048 ((MATRIX*)PSX_SCRATCH(0x1F800048))
#define SCRATCH_SVECTOR_0A0 ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))
#define SCRATCH_MATRIX_0F0 ((MATRIX*)PSX_SCRATCH(0x1F8000F0))
#define SCRATCH_MATRIX_110 ((MATRIX*)PSX_SCRATCH(0x1F800110))
#define SCRATCH_MATRIX_130 ((MATRIX*)PSX_SCRATCH(0x1F800130))
#define SCRATCH_MATRIX_150 ((MATRIX*)PSX_SCRATCH(0x1F800150))

void RotMatrixX(s32 r, MATRIX* m);

void deleteFieldEntitySub4(void* actor);
s32 getCdromFnd(void);                    /* main: file system mode (0 or -1 = CD, else PC/DTL) */
s32 PCclose(s32 fd);                  /* main: PCclose */
char* getFilenameFromPC(s32 fileIndex);         /* main: PC (DTL) file name */

s32 loadWorldmapChunk(void);
s32 updateWorldmapStreamingStateMachine(void);
void worldmapStreamingCdSyncCallback(s32 status, u8* result);
void worldmapStreamingCdReadyCallback(s32 status, u8* result);

void sortWorldmapStreamingEntries(sWorldmapCellFromCDSub* p);
void sortWorldmapStreamingEntriesFromPC(sWorldmapCellFromPCSub* p);
void loadWorldmapChunkFromPC(sWorldmapCellFromPCSub* p);
void loadWorldmapChunkFromCD(sWorldmapCellFromCD* p);
s32 isWorldStreamingDone(void);

void triggerInitialWorldLoading(void);
void updateWorldmapGrids(VECTOR* p);
void pcWideWorldmapGround(OTag* ot, VECTOR* pos);

s32 setWorldmapCellTriangleValue(u32 sector, s32 size, u8* buffer);
s32 setWorldmapCellQuadValue(char* filename, u32 offset, s32 size, u8* buffer);
s32 finalizeLoadingWorldmapCell(void);
s32 finalizeLoadingWorldmapCellFromPC(void);

void drawWorldmapGroundCellSub0(u32* cell, void* ot, void* prims);
/* port/src/game/worldmap/ground.c: the chunk the ground cells drawn next belong to (smooth motion). */
void pcWorldmapGroundChunk(s32 id, u32* base);

s32 checkWorldmapPositionSub1_0(VECTOR* position, VECTOR* step, s32 stepScale, s16 mode);
s32 checkWorldmapPositionSub1_1(VECTOR* collision, VECTOR* step, VECTOR* outputStep);

void drawWorldmapGroundCell(u32* cell, void* ot, POLY_FT3* prims, SVECTOR* start);

s16 computeTileVisibility(SVECTOR* a, SVECTOR* b, SVECTOR* c, SVECTOR* d);

void checkWorldmapPositionSub0(VECTOR* p);                /* Noah: checkWorldmapPositionSub0 (wrap) */
s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z);
s32 worldmapCheckCollisionsAgainstWorldmapModels(VECTOR* position, s16* model);
void getWorldmapModelTriangleContact(VECTOR* position, VECTOR* out, VECTOR* normal, u16 modelIndex, u16 triIndex);
s32 isWorldmapModelTrianglePlaneCrossed(VECTOR* position, s32 height, u16 modelIndex, u16 triIndex);
s32 getWorldmapModelTriangleEdgeFlags(VECTOR* target, VECTOR* position, s32 modelIndex, s32 triIndex);
extern s16 worldmapModelCollisionHits[];                    /* collision hits: (triangle index, mC) pairs */
extern s32 worldmapModelVar0;                       /* Noah: worldmapModelVar0 (model triangle) */
extern s32 worldmapModelVar1;                       /* Noah: worldmapModelVar1 (model index) */
extern sWorldmapModel* worldmapModels;

s16 getWorldmapGroundType(VECTOR* position);
s16 checkWorldmapPositionSub1_0_1(s16 a, s16 b);
s32 checkWorldmapPositionSub1_0_3(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type);
s32 checkWorldmapPositionSub1_0_4(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type);
s32 checkWorldmapPositionSub1_0_5(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type);
s32 checkWorldmapPositionSub1_0_2(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type);


s32 checkWorldmapPositionSub1_0(VECTOR* pos, VECTOR* step, s32 scale, s16 mode) {
    s32 nc;
    s32 r;
    sWorldmapScratch* s = WM_SCRATCH;
    u32 flag;

    flag = 0;
    s->m10.vx = pos->vx + ((step->vx * scale) >> 12);
    s->m10.vz = pos->vz + ((step->vz * scale) >> 12);
    s->mA0.vx = pos->vx >> 19;
    s->mA0.vz = pos->vz >> 19;
    s->mA8.vx = s->m10.vx >> 19;
    s->mA8.vz = s->m10.vz >> 19;
    r = 0;
    if (s->mA8.vx < s->mA0.vx) {
        flag = 2;
    } else if (s->mA0.vx < s->mA8.vx) {
        flag = 1;
    }
    if (s->mA0.vz > s->mA8.vz) {
        flag |= 8;
    } else if (s->mA0.vz < s->mA8.vz) {
        flag |= 4;
    }
    switch (flag) {
    case 0:
    case 3:
    case 7:
        break;
    case 1:
        r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
        break;
    case 2:
        r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
        break;
    case 4:
        r = checkWorldmapPositionSub1_0_5(pos, step, &s->m0, mode);
        break;
    case 8:
        r = checkWorldmapPositionSub1_0_2(pos, step, &s->m0, mode);
        break;
    case 5:
        s->m0.vx = (pos->vx & 0xFFF80000) + 0x80000;
        s->m0.vz = (pos->vz & 0xFFF80000) + 0x80000;
        s->m20.vx = (pos->vx - s->m0.vx) >> 12;
        s->m20.vz = (pos->vz - s->m0.vz) >> 12;
        s->m30.vx = (s->m10.vx - s->m0.vx) >> 12;
        s->m30.vz = (s->m10.vz - s->m0.vz) >> 12;
        s->m40.vy = 0;
        s->m40.vx = (s->m20.vz << 16) | (s->m20.vx & 0xFFFF);
        s->m40.vz = (s->m30.vz << 16) | (s->m30.vx & 0xFFFF);
        nc = NormalClip(s->m40.vx, s->m40.vy, s->m40.vz);
        if (nc < 0) {
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_5(pos, step, &s->m0, mode);
        } else if (nc > 0) {
            r = checkWorldmapPositionSub1_0_5(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
        } else {
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
        }
        break;
    case 6:
        s->m0.vx = (pos->vx & 0xFFF80000);
        s->m0.vz = (pos->vz & 0xFFF80000) + 0x80000;
        s->m20.vx = (pos->vx - s->m0.vx) >> 12;
        s->m20.vz = (pos->vz - s->m0.vz) >> 12;
        s->m30.vx = (s->m10.vx - s->m0.vx) >> 12;
        s->m30.vz = (s->m10.vz - s->m0.vz) >> 12;
        s->m40.vy = 0;
        s->m40.vx = (s->m20.vz << 16) | (s->m20.vx & 0xFFFF);
        s->m40.vz = (s->m30.vz << 16) | (s->m30.vx & 0xFFFF);
        nc = NormalClip(s->m40.vx, s->m40.vy, s->m40.vz);
        if (nc > 0) {
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_5(pos, step, &s->m0, mode);
        } else if (nc < 0) {
            r = checkWorldmapPositionSub1_0_5(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
        } else {
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
        }
        break;
    case 9:
        s->m0.vx = (pos->vx & 0xFFF80000) + 0x80000;
        s->m0.vz = (pos->vz & 0xFFF80000);
        s->m20.vx = (pos->vx - s->m0.vx) >> 12;
        s->m20.vz = (pos->vz - s->m0.vz) >> 12;
        s->m30.vx = (s->m10.vx - s->m0.vx) >> 12;
        s->m30.vz = (s->m10.vz - s->m0.vz) >> 12;
        s->m40.vy = 0;
        s->m40.vx = (s->m20.vz << 16) | (s->m20.vx & 0xFFFF);
        s->m40.vz = (s->m30.vz << 16) | (s->m30.vx & 0xFFFF);
        nc = NormalClip(s->m40.vx, s->m40.vy, s->m40.vz);
        if (nc > 0) {
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_2(pos, step, &s->m0, mode);
        } else if (nc < 0) {
            r = checkWorldmapPositionSub1_0_2(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
        } else {
            r = checkWorldmapPositionSub1_0_3(pos, step, &s->m0, mode);
        }
        break;
    case 10:
        s->m0.vx = (pos->vx & 0xFFF80000);
        s->m0.vz = (pos->vz & 0xFFF80000);
        s->m20.vx = (pos->vx - s->m0.vx) >> 12;
        s->m20.vz = (pos->vz - s->m0.vz) >> 12;
        s->m30.vx = (s->m10.vx - s->m0.vx) >> 12;
        s->m30.vz = (s->m10.vz - s->m0.vz) >> 12;
        s->m40.vy = 0;
        s->m40.vx = (s->m20.vz << 16) | (s->m20.vx & 0xFFFF);
        s->m40.vz = (s->m30.vz << 16) | (s->m30.vx & 0xFFFF);
        nc = NormalClip(s->m40.vx, s->m40.vy, s->m40.vz);
        if (nc < 0) {
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_2(pos, step, &s->m0, mode);
        } else if (nc > 0) {
            r = checkWorldmapPositionSub1_0_2(pos, step, &s->m0, mode);
            if (r != 0) {
                return r;
            }
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
        } else {
            r = checkWorldmapPositionSub1_0_4(pos, step, &s->m0, mode);
        }
        break;
    }
    if (r != 0) {
        return r;
    }
    checkWorldmapPositionSub0(&s->m10);
    if (checkWorldmapPositionSub1_0_1(mode, getWorldmapGroundType(&s->m10)) != 0) {
        return 0;
    }
    s->m0 = s->m10;
    return 1;
}

/* K&R definition: callers (checkWorldmapPositionYggdrasil) pass mode unconverted. */
s32 checkWorldmapPositionSub1(position, step, output, stepScale, mode)
    VECTOR* position;
    VECTOR* step;
    VECTOR* output;
    s32 stepScale;
    s16 mode;
{
    s32 result;
    s32 r;

    switch (checkWorldmapPositionSub1_0(position, step, stepScale, mode)) {
    case 0:
        result = 1;
        break;
    case 1:
        r = checkWorldmapPositionSub1_1(SCRATCH_VECTOR_000, step, output);
        result = 0;
        if (r == 0) {
            output->vz = 0;
            output->vy = 0;
            output->vx = 0;
        }
        break;
    case 2:
        output->vx = (step->vx < 0) ? -0x1000 : 0x1000;
        result = 0;
        output->vz = 0;
        output->vy = 0;
        break;
    case 3:
        output->vy = 0;
        output->vx = 0;
        output->vz = (step->vz < 0) ? -0x1000 : 0x1000;
        result = 0;
        break;
    }
    return result;
}

/* unnamed; slide step along a normal (worldmapPlayer.cpp) */
void computeWorldmapEdgeSlideStep(VECTOR* step, VECTOR* out, VECTOR* normal) {
    s32 d = normal->vx * step->vx + normal->vz * step->vz;

    if (d < 0) {
        out->vx = -normal->vx;
        out->vz = -normal->vz;
    } else if (d > 0) {
        out->vx = normal->vx;
        out->vz = normal->vz;
    } else {
        out->vz = 0;
        out->vx = 0;
    }
    out->vy = 0;
}

void computeWorldmapWallSlideStep(VECTOR* normal, VECTOR* step, VECTOR* out) {
    s32 d;
    SCRATCH_VECTOR_010->vz = 0;
    SCRATCH_VECTOR_010->vx = 0;
    SCRATCH_VECTOR_010->vy = -0x1000;
    OuterProduct12(normal, SCRATCH_VECTOR_010, SCRATCH_VECTOR_020);
    VectorNormal(SCRATCH_VECTOR_020, SCRATCH_VECTOR_000);
    d = (SCRATCH_VECTOR_000->vx * step->vx) + (SCRATCH_VECTOR_000->vz * step->vz);
    if (d < 0) {
        out->vx = -SCRATCH_VECTOR_000->vx;
        out->vz = -SCRATCH_VECTOR_000->vz;
    } else if (d > 0) {
        out->vx = SCRATCH_VECTOR_000->vx;
        out->vz = SCRATCH_VECTOR_000->vz;
    } else {
        out->vz = 0;
        out->vx = 0;
    }
    out->vy = 0;
}

/* Noah: checkWorldmapPosition (worldmapPlayer.cpp; Noah's version is incomplete).
 * Moves `position` by `step` and fills `output`. Off models (collisionMode 1):
 * if the new point is inside a model's collision triangles, either stands on
 * one (height within 10 units: remembers model worldmapModelVar1 / triangle
 * worldmapModelVar0, returns 1) or slides along the hit wall; otherwise the ground
 * check checkWorldmapPositionSub1. On a model (collisionMode 3): walks the triangle mesh
 * from the remembered triangle through the crossed edges' neighbours
 * (m6[edge], -1 = mesh border: fall back to the ground check; mC == 1 = wall:
 * slide along that edge's normal, s->m30/m40/m50 from getWorldmapModelTriangleEdgeFlags). */
s32 checkWorldmapPosition(VECTOR* position, VECTOR* step, VECTOR* output, s32 stepScale, s32 mode) {
    sWorldmapScratch* s = WM_SCRATCH;
    sWorldmapCollisionTri* triangles;
    s16 model;
    s32 collisionMode;
    s32 result;
    s32 numHits;
    s32 loop;
    s32 i;
    s32 numRejected;
    s16 tri;
    s32 neighbors;
    s32 floors;

    s->m60_probePosition.vx = position->vx + ((step->vx * stepScale) >> 12);
    s->m60_probePosition.vz = position->vz + ((step->vz * stepScale) >> 12);
    checkWorldmapPositionSub0(&s->m60_probePosition);
    s->m60_probePosition.vy = worldmapGetAltitudeFor2dPoint(s->m60_probePosition.vx, s->m60_probePosition.vz) - 0x4000;
    output->vx = position->vx + ((step->vx * stepScale) >> 12);
    output->vz = position->vz + ((step->vz * stepScale) >> 12);
    checkWorldmapPositionSub0(output);
    output->vy = worldmapGetAltitudeFor2dPoint(output->vx, output->vz) - 0x4000;
    collisionMode = 1;
    if (worldmapModelVar1 != -1) {
        collisionMode = 3;
    }
    switch (collisionMode) {
    case 0:
        result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
        worldmapModelVar0 = -1;
        worldmapModelVar1 = -1;
        break;
    case 1:
        numHits = worldmapCheckCollisionsAgainstWorldmapModels(&s->m60_probePosition, &model);
        if (numHits != 0) {
            numRejected = 0;
            for (i = 0; i < numHits; i += 2) {
                if (isWorldmapModelTrianglePlaneCrossed(&s->m60_probePosition, 0x70, model, worldmapModelCollisionHits[i]) == 0) {
                    worldmapModelCollisionHits[i] = -1;
                    numRejected += 2;
                }
            }
            if (numRejected != numHits) {
                result = 0;
                for (i = 0; i < numHits; i += 2) {
                    if (worldmapModelCollisionHits[i] != -1 && worldmapModelCollisionHits[i + 1] != 1) {
                        getWorldmapModelTriangleContact(&s->m60_probePosition, &s->m70_contactPoint, &s->m80_contactNormal, model, worldmapModelCollisionHits[i]);
                        if (abs(s->m70_contactPoint.vy - (position->vy >> 12)) < 11) {
                            output->vy = s->m70_contactPoint.vy << 12;
                            worldmapModelVar1 = model;
                            worldmapModelVar0 = worldmapModelCollisionHits[i];
                            result = 1;
                            break;
                        }
                    }
                }
                if (result == 0) {
                    computeWorldmapWallSlideStep(&s->m80_contactNormal, step, output);
                }
                break;
            }
        }
        result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
        worldmapModelVar0 = -1;
        worldmapModelVar1 = -1;
        break;
    case 2:
    case 3:
        loop = 1;
        tri = worldmapModelVar0;
        model = worldmapModelVar1;
        triangles = worldmapModels[model].m44_collisionMesh->m8_triangles;
        do {
            i = getWorldmapModelTriangleEdgeFlags(position, &s->m60_probePosition, model, tri);
            switch (i) {
            case 0:
                getWorldmapModelTriangleContact(&s->m60_probePosition, &s->m70_contactPoint, &s->m80_contactNormal, model, tri);
                result = 1;
                loop = 0;
                worldmapModelVar0 = tri;
                output->vy = s->m70_contactPoint.vy << 12;
                worldmapModelVar1 = model;
                break;
            case 1:
                if (triangles[tri].m6_neighbors[0] == -1) {
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                }
                tri = triangles[tri].m6_neighbors[0];
                if (triangles[tri].mC_type == 1) {
                    result = 0;
                    loop = 0;
                    computeWorldmapEdgeSlideStep(step, output, &s->m30);
                }
                break;
            case 2:
                if (triangles[tri].m6_neighbors[1] == -1) {
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                }
                tri = triangles[tri].m6_neighbors[1];
                if (triangles[tri].mC_type == 1) {
                    result = 0;
                    loop = 0;
                    computeWorldmapEdgeSlideStep(step, output, &s->m40);
                }
                break;
            case 4:
                if (triangles[tri].m6_neighbors[2] == -1) {
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                }
                tri = triangles[tri].m6_neighbors[2];
                if (triangles[tri].mC_type == 1) {
                    result = 0;
                    loop = 0;
                    computeWorldmapEdgeSlideStep(step, output, &s->m50_edgeDir2);
                }
                break;
            case 3:
                neighbors = 0;
                if (triangles[tri].m6_neighbors[0] != -1) {
                    neighbors |= 1;
                }
                if (triangles[tri].m6_neighbors[1] != -1) {
                    neighbors |= 2;
                }
                switch (neighbors) {
                case 0:
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                case 1:
                    tri = triangles[tri].m6_neighbors[0];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m30);
                    }
                    break;
                case 2:
                    tri = triangles[tri].m6_neighbors[1];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m40);
                    }
                    break;
                case 3:
                    floors = triangles[triangles[tri].m6_neighbors[0]].mC_type == 0;
                    if (triangles[triangles[tri].m6_neighbors[1]].mC_type == 0) {
                        floors |= 2;
                    }
                    switch (floors) {
                    case 0:
                        result = 0;
                        loop = 0;
                        output->vz = 0;
                        output->vy = 0;
                        output->vx = 0;
                        break;
                    case 1:
                    case 3:
                        tri = triangles[tri].m6_neighbors[0];
                        break;
                    case 2:
                        tri = triangles[tri].m6_neighbors[1];
                        break;

                    }
                    break;
                }
                break;
            case 5:
                neighbors = 0;
                if (triangles[tri].m6_neighbors[0] != -1) {
                    neighbors |= 1;
                }
                if (triangles[tri].m6_neighbors[2] != -1) {
                    neighbors |= 2;
                }
                switch (neighbors) {
                case 0:
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                case 1:
                    tri = triangles[tri].m6_neighbors[0];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m30);
                    }
                    break;
                case 2:
                    tri = triangles[tri].m6_neighbors[2];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m50_edgeDir2);
                    }
                    break;
                case 3:
                    floors = triangles[triangles[tri].m6_neighbors[0]].mC_type == 0;
                    if (triangles[triangles[tri].m6_neighbors[2]].mC_type == 0) {
                        floors |= 2;
                    }
                    switch (floors) {
                    case 0:
                        result = 0;
                        loop = 0;
                        output->vz = 0;
                        output->vy = 0;
                        output->vx = 0;
                        break;
                    case 1:
                    case 3:
                        tri = triangles[tri].m6_neighbors[0];
                        break;
                    case 2:
                        tri = triangles[tri].m6_neighbors[2];
                        break;

                    }
                    break;
                }
                break;
            case 6:
                neighbors = 0;
                if (triangles[tri].m6_neighbors[1] != -1) {
                    neighbors |= 1;
                }
                if (triangles[tri].m6_neighbors[2] != -1) {
                    neighbors |= 2;
                }
                switch (neighbors) {
                case 0:
                    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
                    worldmapModelVar0 = -1;
                    worldmapModelVar1 = -1;
                    loop = 0;
                    break;
                case 1:
                    tri = triangles[tri].m6_neighbors[1];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m40);
                    }
                    break;
                case 2:
                    tri = triangles[tri].m6_neighbors[2];
                    if (triangles[tri].mC_type == 1) {
                        result = 0;
                        loop = 0;
                        computeWorldmapEdgeSlideStep(step, output, &s->m50_edgeDir2);
                    }
                    break;
                case 3:
                    floors = triangles[triangles[tri].m6_neighbors[1]].mC_type == 0;
                    if (triangles[triangles[tri].m6_neighbors[2]].mC_type == 0) {
                        floors |= 2;
                    }
                    switch (floors) {
                    case 0:
                        result = 0;
                        loop = 0;
                        output->vz = 0;
                        output->vy = 0;
                        output->vx = 0;
                        break;
                    case 1:
                    case 3:
                        tri = triangles[tri].m6_neighbors[1];
                        break;
                    case 2:
                        tri = triangles[tri].m6_neighbors[2];
                        break;

                    }
                    break;
                }
                break;
            case 7:
                break;
            }
        } while (loop != 0);
        break;
    }
    return result;
}

/* checkWorldmapPosition, model collision variant (worldmapPlayer.cpp) */
s32 checkWorldmapPositionYggdrasil(VECTOR* position, VECTOR* step, VECTOR* output, s32 stepScale, s32 mode) {
    s32 alt;
    s32 n;
    s32 i;
    s32 k;
    s16 model;
    s32 result;
    sWorldmapScratch* s = WM_SCRATCH;

    s->m60_probePosition.vx = position->vx + ((step->vx * stepScale) >> 12);
    s->m60_probePosition.vy = position->vy + ((step->vy * stepScale) >> 12);
    s->m60_probePosition.vz = position->vz + ((step->vz * stepScale) >> 12);
    checkWorldmapPositionSub0(&s->m60_probePosition);
    output->vx = position->vx + ((step->vx * stepScale) >> 12);
    output->vy = position->vy + ((step->vy * stepScale) >> 12);
    output->vz = position->vz + ((step->vz * stepScale) >> 12);
    checkWorldmapPositionSub0(output);
    alt = worldmapGetAltitudeFor2dPoint(s->m60_probePosition.vx, s->m60_probePosition.vz) - 0x20000;
    if (alt > 0x20000) {
        alt = 0x20000;
    }
    if ((alt < s->m60_probePosition.vy) | (alt < -0x280000)) {
        s->m60_probePosition.vy = alt;
        output->vy = alt;
    }
    if ((alt > -0x280000) & (s->m60_probePosition.vy < -0x280000)) {
        s->m60_probePosition.vy = -0x280000;
        output->vy = -0x280000;
    }
    n = worldmapCheckCollisionsAgainstWorldmapModels(&s->m60_probePosition, &model);
    if (n != 0) {
        k = 0;
        for (i = 0; i < n; i += 2) {
            if (isWorldmapModelTrianglePlaneCrossed(&s->m60_probePosition, 0x70, model, worldmapModelCollisionHits[i]) == 0) {
                worldmapModelCollisionHits[i] = -1;
                k += 2;
            }
        }
        result = 0;
        if (k != n) {
            output->vx = -step->vx >> 1;
            output->vy = -step->vy >> 1;
            output->vz = -step->vz >> 1;
            goto end;
        }
    }
    result = checkWorldmapPositionSub1(position, step, output, stepScale, mode);
end:
    return result;
}

/* Noah: allocateWorldmapData (worldmapWorldStreaming.cpp) */
void allocateWorldmapData(void) {
    s32 a;
    s32 b;
    s32 i;
    s32 j;
    u8* ps;

    a = getCdromFnd();
    b = getCdromFnd();
    if ((a == 0) | (b == -1)) {
        worldmapCellLoadSlot = 0;
        worldmapNextCellIndex = 0;
        worldmapStreamingStateMachineState = 0;
        for (i = 0xF; i >= 0; i--) {
            loadedWorldmapCellsFromCD[i] = NULL;
        }
        worldmapCellsFromCD = allocateHeap(16 * (s32)sizeof(sWorldmapCellFromCD), 0);
        worldmapExtraData = allocateHeap(0x800, 0);
        worldmapCellLoadingPending = 0;
        for (i = 7, ps = &worldmapStreamingCDSyncStatus[7]; i >= 0; i--, ps--) {
            *ps = 0;
        }
    } else {
        worldmapCellLoadSlot = 0;
        worldmapNextCellIndex = 0;
        worldmapStreamingStateMachineState = 0;
        for (i = 0xF; i >= 0; i--) {
            loadedWorldmapCellsFromPC[i] = NULL;
        }
        worldmapCellsFromPC = allocateHeap(16 * (s32)sizeof(sWorldmapCellFromPC), 0);
        worldmapExtraData = allocateHeap(0x800, 0);
        worldmapCellLoadingPending = 0;
        for (i = 7, ps = &worldmapStreamingCDSyncStatus[7]; i >= 0; i--, ps--) {
            *ps = 0;
        }
    }
}

/* Noah: freeWorldmapData (worldmapWorldStreaming.cpp) */
void freeWorldmapData(void) {
    s32 a = getCdromFnd();
    s32 b = getCdromFnd();
    if ((a == 0) | (b == -1)) {
        freeHeap(worldmapCellsFromCD);
    } else {
        freeHeap(worldmapCellsFromPC);
    }
    freeHeap(worldmapExtraData);
}

/* Noah: streamWorldmap0 (worldmapWorldStreaming.cpp) */
void streamWorldmap0(void) {
    s32 a = getCdromFnd();
    s32 b = getCdromFnd();
    if ((a == 0) | (b == -1)) {
        while (loadedWorldmapCellsFromCD[worldmapNextCellIndex] != NULL) {
            VSync(0);
            loadWorldmapChunk();
        }
    } else {
        while (loadedWorldmapCellsFromPC[worldmapNextCellIndex] != NULL) {
            VSync(0);
            loadWorldmapChunk();
        }
    }
}

/* Noah: setWorldmapCellTriangleValue (worldmapWorldStreaming.cpp) */
s32 setWorldmapCellTriangleValue(u32 sector, s32 size, u8* buffer) {
    sWorldmapCellFromCDSub* p;

    if (worldmapCellLoadingPending < 0x58) {
        p = &worldmapCellsFromCD[worldmapNextCellIndex].m0_reads[worldmapCellLoadingPending++];
        p->m0_sector = sector;
        p->m4_size = size;
        p->m8_buffer = buffer;
        return 0;
    }
    return -1;
}

/* Noah: setWorldmapCellQuadValue (worldmapWorldStreaming.cpp) */
s32 setWorldmapCellQuadValue(char* filename, u32 offset, s32 size, u8* buffer) {
    sWorldmapCellFromPCSub* p;

    if (worldmapCellLoadingPending < 0x58) {
        p = &worldmapCellsFromPC[worldmapNextCellIndex].m0_reads[worldmapCellLoadingPending++];
        p->m0_filename = filename;
        p->m4_offset = offset;
        p->m8_size = size;
        p->mC_buffer = buffer;
        return 0;
    }
    return -1;
}

/* Noah: finalizeLoadingWorldmapCell (worldmapWorldStreaming.cpp) */
s32 finalizeLoadingWorldmapCell(void) {
    sWorldmapCellFromCD* p = &worldmapCellsFromCD[worldmapNextCellIndex];

    if (p->m0_reads[0].m0_sector != 0 && loadedWorldmapCellsFromCD[worldmapNextCellIndex] == NULL) {
        sortWorldmapStreamingEntries(p->m0_reads);
        worldmapCellLoadingPending = 0;
        loadedWorldmapCellsFromCD[worldmapNextCellIndex] = p;
        worldmapNextCellIndex = (worldmapNextCellIndex + 1) & 0xF;
        return 0;
    }
    worldmapCellLoadingPending = 0;
    return -1;
}

/* Sort the cell list by sector (insertion sort). `next` is first assigned at the
 * end of the loop body: GCC reduces it to the induction value current + 1 and
 * initialises that before the loop, exactly as retail does (initialising it in C
 * adds a register copy). */
void sortWorldmapStreamingEntries(sWorldmapCellFromCDSub* p) {
    sWorldmapCellFromCDSub* next;
    sWorldmapCellFromCDSub* current;
    sWorldmapCellFromCDSub* start;
    sWorldmapCellFromCDSub temp;

    current = p;
    start = current;
    next = current + 1; /* what retail's GCC sets up before the loop */
    if (current[1].m0_sector != 0) {
        do {
            if (current->m0_sector > next->m0_sector) {
                temp.m0_sector = current->m0_sector;
                temp.m4_size = current->m4_size;
                temp.m8_buffer = current->m8_buffer;
                current->m0_sector = next->m0_sector;
                current->m4_size = next->m4_size;
                current->m8_buffer = next->m8_buffer;
                next->m0_sector = temp.m0_sector;
                next->m4_size = temp.m4_size;
                next->m8_buffer = temp.m8_buffer;
                if (start < current) {
                    current--;
                }
            } else {
                current++;
            }
            next = current + 1;
        } while (next->m0_sector != 0);
    }
}

/* PC twin of sortWorldmapStreamingEntries (same loop shape, same note on `next`). */
void sortWorldmapStreamingEntriesFromPC(sWorldmapCellFromPCSub* p) {
    sWorldmapCellFromPCSub* next;
    sWorldmapCellFromPCSub* current;
    sWorldmapCellFromPCSub* start;
    sWorldmapCellFromPCSub temp;

    current = p;
    start = current;
    next = current + 1; /* what retail's GCC sets up before the loop */
    if (current[1].m0_filename != 0) {
        do {
            if (current->m4_offset > next->m4_offset) {
                temp.m0_filename = current->m0_filename;
                temp.m4_offset = current->m4_offset;
                temp.m8_size = current->m8_size;
                temp.mC_buffer = current->mC_buffer;
                current->m0_filename = next->m0_filename;
                current->m4_offset = next->m4_offset;
                current->m8_size = next->m8_size;
                current->mC_buffer = next->mC_buffer;
                next->m0_filename = temp.m0_filename;
                next->m4_offset = temp.m4_offset;
                next->m8_size = temp.m8_size;
                next->mC_buffer = temp.mC_buffer;
                if (start < current) {
                    current--;
                }
            } else {
                current++;
            }
            next = current + 1;
        } while (next->m0_filename != 0);
    }
}

/* Noah: finalizeLoadingWorldmapCellFromPC (worldmapWorldStreaming.cpp) */
s32 finalizeLoadingWorldmapCellFromPC(void) {
    sWorldmapCellFromPC* p = &worldmapCellsFromPC[worldmapNextCellIndex];

    if (p->m0_reads[0].m0_filename != NULL && loadedWorldmapCellsFromPC[worldmapNextCellIndex] == NULL) {
        sortWorldmapStreamingEntriesFromPC(p->m0_reads);
        worldmapCellLoadingPending = 0;
        loadedWorldmapCellsFromPC[worldmapNextCellIndex] = p;
        worldmapNextCellIndex = (worldmapNextCellIndex + 1) & 0xF;
        return 0;
    }
    worldmapCellLoadingPending = 0;
    return -1;
}

/* Noah: isWorldStreamingDone (worldmapWorldStreaming.cpp) */
s32 isWorldStreamingDone(void) {
    s32 n = worldmapNextCellIndex - worldmapCellLoadSlot;
    if (n < 0) {
        n += 0x10;
    }
    return n;
}

/* wait until streaming done (worldmapWorldStreaming.cpp) */
void waitWorldmapStreamingDone(void) {
    do {
        VSync(0);
        loadWorldmapChunk();
    } while (isWorldStreamingDone() != 0);
}

/* Noah: loadWorldmapChunkFromPC (worldmapWorldStreaming.cpp) */
void loadWorldmapChunkFromPC(sWorldmapCellFromPCSub* p) {
    s32 i;
    s32 fd;

    D_8009BE48 = 0;
    D_8009CCB0 = 0;
    worldmapStreamingShellOpenCount = 0;
    worldmapStreamingReadErrorCount = 0;
    while (p->m0_filename != NULL) {
        for (i = 0; i < 8; i++) {
            fd = PCopen(p->m0_filename, 0, 0);
            if (fd != -1) {
                break;
            }
        }
        if (fd != -1) {
            PClseek(fd, p->m4_offset, 0);
            for (i = 0; i < 8; i++) {
                if (PCread(fd, p->mC_buffer, p->m8_size) != 0) {
                    break;
                }
            }
            for (i = 0; i < 8; i++) {
                if (PCclose(fd) == 0) {
                    break;
                }
            }
        }
        p++;
    }
}

/* Noah: loadWorldmapChunk (worldmapWorldStreaming.cpp) */
s32 loadWorldmapChunk(void) {
    s32 result = 0;
    s32 a = getCdromFnd();
    s32 b = getCdromFnd();

    if ((a == 0) | (b == -1)) {
        result = updateWorldmapStreamingStateMachine();
        if (result == 0 && loadedWorldmapCellsFromCD[worldmapCellLoadSlot] != NULL) {
            loadWorldmapChunkFromCD(loadedWorldmapCellsFromCD[worldmapCellLoadSlot]);
        }
    } else if (loadedWorldmapCellsFromPC[worldmapCellLoadSlot] != NULL) {
        loadWorldmapChunkFromPC(loadedWorldmapCellsFromPC[worldmapCellLoadSlot]->m0_reads);
        loadedWorldmapCellsFromPC[worldmapCellLoadSlot] = NULL;
        worldmapCellLoadSlot = (worldmapCellLoadSlot + 1) & 0xF;
    }
    return result;
}

/* Noah: updateWorldmapStreamingStateMachine (worldmapWorldStreaming.cpp) */
s32 updateWorldmapStreamingStateMachine(void) {
    switch (worldmapStreamingStateMachineState) {
    case 0:
        return 0;
    case 4:
        if (--worldmapStreamingStateMachineNumPendingLoads == 0) {
            worldmapStreamingStateMachineState++;
        }
    case 1:
    case 2:
    case 3:
        return 1;
    case 5:
        worldmapStreamingStateMachineState = 0;
        loadedWorldmapCellsFromCD[worldmapCellLoadSlot] = NULL;
        worldmapCellLoadSlot = (worldmapCellLoadSlot + 1) & 0xF;
        return 2;
    default:
        return 3;
    }
}

/* Noah: loadWorldmapChunkFromCD (worldmapWorldStreaming.cpp); worldmapStreamingNextRead must be volatile:
 * retail stores it twice in a row and GCC drops the first, dead store otherwise */
void loadWorldmapChunkFromCD(sWorldmapCellFromCD* p) {
    s32 sector = p->m0_reads[0].m0_sector;

    worldmapStreamingStateMachineState = 1;
    worldmapStreamingNextRead = p->m0_reads;
    worldmapStreamingNextRead = &p->m0_reads[1];
    D_8009BE48 = 0;
    D_8009CCB0 = 0;
    worldmapStreamingShellOpenCount = 0;
    worldmapStreamingReadErrorCount = 0;
    worldmapStreamingReadSector = sector;
    worldmapStreamingWantedSector = sector;
    worldmapStreamingSectorsLeft = (u32)(p->m0_reads[0].m4_size + 0x7FF) >> 11;
    worldmapStreamingBytesLeft = p->m0_reads[0].m4_size;
    worldmapStreamingDest = p->m0_reads[0].m8_buffer;
    CdIntToPos(sector, &worldmapStreamingCdLoc);
    CdSyncCallback((CdlCB)worldmapStreamingCdSyncCallback); /* status is an s32 in the callback */
    CdControlF(2, (u8*)&worldmapStreamingCdLoc);
}

/* CdSyncCallback of the streaming reads (worldmapWorldStreaming.cpp) */
void worldmapStreamingCdSyncCallback(s32 status, u8* result) {
    if (status == 2) {
        switch (worldmapStreamingStateMachineState) {
        case 1:
            worldmapStreamingStateMachineState = 2;
            worldmapStreamingSectorHeader[2] = 0;
            worldmapStreamingSectorHeader[1] = 0;
            worldmapStreamingSectorHeader[0] = 0;
            CdReadyCallback((CdlCB)worldmapStreamingCdReadyCallback);
            CdControlF(0x1B, NULL);
            break;
        case 3:
            if (worldmapStreamingWantedSector == 0) {
                worldmapStreamingStateMachineState = 4;
                worldmapStreamingStateMachineNumPendingLoads = 1;
                CdSyncCallback(NULL);
            }
            break;
        case 10:
            if (*result & 0x10) {
                CdControlF(1, NULL);
            } else {
                CdControlF(0x13, NULL);
                worldmapStreamingStateMachineState = 11;
            }
            break;
        case 11:
            worldmapStreamingStateMachineState = 12;
            CdControlF(9, NULL);
            break;
        case 12:
            worldmapStreamingStateMachineState = 1;
            CdIntToPos(worldmapStreamingReadSector, &worldmapStreamingCdLoc);
            CdControlF(2, (u8*)&worldmapStreamingCdLoc);
            break;
        }
    } else if (*result & 0x10) {
        worldmapStreamingStateMachineState = 10;
        worldmapStreamingShellOpenCount++;
        CdControlF(1, NULL);
    } else {
        worldmapStreamingStateMachineState = 11;
        CdControlF(0x13, NULL);
    }
}

/* CdReadyCallback of the streaming reads (worldmapWorldStreaming.cpp) */
void worldmapStreamingCdReadyCallback(s32 status, u8* result) {
    s32 pos;
    sWorldmapCellFromCDSub* e;
    s32 sector;
    s32 count;

    if (status == 1) {
        CdGetSector(worldmapStreamingSectorHeader, 3);
        pos = CdPosToInt((CdlLOC*)worldmapStreamingSectorHeader);
        if (pos == worldmapStreamingReadSector) {
            if (worldmapStreamingWantedSector == pos) {
                if (worldmapStreamingBytesLeft < 0x800) {
                    CdGetSector(worldmapStreamingDest, worldmapStreamingBytesLeft / 4);
                    CdGetSector(worldmapExtraData, (0x800 - worldmapStreamingBytesLeft) / 4);
                } else {
                    CdGetSector(worldmapStreamingDest, 0x200);
                    worldmapStreamingBytesLeft -= 0x800;
                }
                if (--worldmapStreamingSectorsLeft != 0) {
                    worldmapStreamingWantedSector++;
                    worldmapStreamingDest += 0x800;
                } else {
                    e = worldmapStreamingNextRead;
                    sector = e->m0_sector;
                    count = (u32)(e->m4_size + 0x7FF) >> 11;
                    worldmapStreamingNextRead = e + 1;
                    worldmapStreamingWantedSector = sector;
                    worldmapStreamingSectorsLeft = count;
                    worldmapStreamingBytesLeft = e->m4_size;
                    worldmapStreamingDest = e->m8_buffer;
                    if (sector == 0) {
                        worldmapStreamingStateMachineState = 3;
                        CdReadyCallback(NULL);
                        CdControlF(9, NULL);
                    } else if (sector - worldmapStreamingReadSector >= 0x13) {
                        worldmapStreamingReadSector = sector;
                        worldmapStreamingStateMachineState = 1;
                        CdIntToPos(sector, &worldmapStreamingCdLoc);
                        CdControlF(2, (u8*)&worldmapStreamingCdLoc);
                        return;
                    }
                }
            }
            worldmapStreamingReadSector++;
        } else {
            worldmapStreamingReadErrorCount++;
            CdReadyCallback(NULL);
            if (*result & 0x10) {
                worldmapStreamingStateMachineState = 10;
                worldmapStreamingShellOpenCount++;
                CdControlF(1, NULL);
            } else {
                worldmapStreamingStateMachineState = 11;
                CdControlF(0x13, NULL);
            }
        }
    } else {
        worldmapStreamingReadErrorCount++;
        CdReadyCallback(NULL);
        if (*result & 0x10) {
            worldmapStreamingStateMachineState = 10;
            worldmapStreamingShellOpenCount++;
            CdControlF(1, NULL);
        } else {
            worldmapStreamingStateMachineState = 11;
            CdControlF(0x13, NULL);
        }
    }
}

/* Noah: computeCameraVector (worldmapTaskCamera.cpp) */
void computeCameraVector(sWorldmapCameraState* cam, VECTOR* target, s32 distance, SVECTOR* angles) {
    s32 y;
    sWorldmapScratch* s = WM_SCRATCH;

    cam->m8_target.vx = 0;
    y = target->vy;
    cam->m8_target.vz = 0;
    cam->m8_target.vy = y >> 12;
    s->mA0.vx = angles->vx;
    s->mA0.vy = angles->vy;
    s->mA0.vz = 0;
    RotMatrixYXZ(&s->mA0, &s->mF0_rotation);
    s->m0.vx = 0;
    s->m0.vy = 0;
    s->m0.vz = -(distance >> 12);
    ApplyMatrixLV(&s->mF0_rotation, &s->m0, &s->m10);
    cam->m0_eye.vx = s->m10.vx;
    cam->m0_eye.vy = cam->m8_target.vy + s->m10.vy;
    cam->m0_eye.vz = s->m10.vz;
    s->mA0.vx = 0;
    s->mA0.vy = angles->vy;
    s->mA0.vz = angles->vz;
    RotMatrixYXZ(&s->mA0, &s->mF0_rotation);
    s->mA0.vx = 0;
    s->mA0.vy = -0x1000;
    s->mA0.vz = 0;
    ApplyMatrix(&s->mF0_rotation, &s->mA0, &cam->m10_up);
}

/* matrix to Euler angles (worldmapTaskGround.cpp?) */
void extractWorldmapEulerAngles(MATRIX* m, SVECTOR* angles) {
    if (m->m[2][0] | m->m[2][2]) {
        angles->vy = ratan2(m->m[2][0], m->m[2][2]) & 0xFFF;
        *SCRATCH_MATRIX_0F0 = *m;
        *SCRATCH_MATRIX_110 = worldMapIdentityMatrix;
        RotMatrixY(angles->vy, SCRATCH_MATRIX_110);
        MulMatrix0(SCRATCH_MATRIX_0F0, SCRATCH_MATRIX_110, SCRATCH_MATRIX_130);
        angles->vx = ratan2(SCRATCH_MATRIX_130->m[1][2], SCRATCH_MATRIX_130->m[1][1]);
        *SCRATCH_MATRIX_110 = worldMapIdentityMatrix;
        RotMatrixX(angles->vx, SCRATCH_MATRIX_110);
        MulMatrix0(SCRATCH_MATRIX_130, SCRATCH_MATRIX_110, SCRATCH_MATRIX_0F0);
        angles->vz = -ratan2(SCRATCH_MATRIX_0F0->m[1][0], SCRATCH_MATRIX_0F0->m[1][1]);
    }
}

/* Noah: setupWorldmapMatrix2 (look-at from worldmapCameraVector) (worldmapTaskGround.cpp) */
void setupWorldmapMatrix2(sWorldmapCameraState* cam) {
    SCRATCH_VECTOR_000->vx = -cam->m0_eye.vx + cam->m8_target.vx;
    SCRATCH_VECTOR_000->vy = -cam->m0_eye.vy + cam->m8_target.vy;
    SCRATCH_VECTOR_000->vz = -cam->m0_eye.vz + cam->m8_target.vz;
    VectorNormal(SCRATCH_VECTOR_000, SCRATCH_VECTOR_030);
    OuterProduct12(SCRATCH_VECTOR_030, &cam->m10_up, SCRATCH_VECTOR_000);
    VectorNormal(SCRATCH_VECTOR_000, SCRATCH_VECTOR_010);
    OuterProduct12(SCRATCH_VECTOR_030, SCRATCH_VECTOR_010, SCRATCH_VECTOR_000);
    VectorNormal(SCRATCH_VECTOR_000, SCRATCH_VECTOR_020);
    worldmapMainMatrix2.m[0][0] = SCRATCH_VECTOR_010->vx;
    worldmapMainMatrix2.m[0][1] = SCRATCH_VECTOR_010->vy;
    worldmapMainMatrix2.m[0][2] = SCRATCH_VECTOR_010->vz;
    worldmapMainMatrix2.m[1][0] = SCRATCH_VECTOR_020->vx;
    worldmapMainMatrix2.m[1][1] = SCRATCH_VECTOR_020->vy;
    worldmapMainMatrix2.m[1][2] = SCRATCH_VECTOR_020->vz;
    worldmapMainMatrix2.m[2][0] = SCRATCH_VECTOR_030->vx;
    worldmapMainMatrix2.m[2][1] = SCRATCH_VECTOR_030->vy;
    worldmapMainMatrix2.m[2][2] = SCRATCH_VECTOR_030->vz;
    SCRATCH_SVECTOR_040->vx = -cam->m0_eye.vx;
    SCRATCH_SVECTOR_040->vy = -cam->m0_eye.vy;
    SCRATCH_SVECTOR_040->vz = -cam->m0_eye.vz;
    *SCRATCH_MATRIX_048 = worldmapMainMatrix2;
    ApplyMatrix(SCRATCH_MATRIX_048, SCRATCH_SVECTOR_040, SCRATCH_VECTOR_000);
    TransMatrix(&worldmapMainMatrix2, SCRATCH_VECTOR_000);
}

/* Noah: setupWorldmapMatrix1 (worldmapTaskGround.cpp) */
void setupWorldmapMatrix1(SVECTOR* p) {
    *SCRATCH_MATRIX_0F0 = worldMapIdentityMatrix;
    *SCRATCH_MATRIX_110 = *SCRATCH_MATRIX_0F0;
    *SCRATCH_MATRIX_130 = *SCRATCH_MATRIX_0F0;
    RotMatrixX(-worldmapRotation.vx, SCRATCH_MATRIX_0F0);
    RotMatrixY(-worldmapRotation.vy, SCRATCH_MATRIX_110);
    RotMatrixZ(-worldmapRotation.vz, SCRATCH_MATRIX_130);
    MulMatrix0(SCRATCH_MATRIX_0F0, SCRATCH_MATRIX_110, SCRATCH_MATRIX_150);
    MulMatrix0(SCRATCH_MATRIX_130, SCRATCH_MATRIX_150, &worldmapMainMatrix2);
    SCRATCH_SVECTOR_0A0->vx = -p->vx;
    SCRATCH_SVECTOR_0A0->vy = -p->vy;
    SCRATCH_SVECTOR_0A0->vz = -p->vz;
    *SCRATCH_MATRIX_0F0 = worldmapMainMatrix2;
    ApplyMatrix(SCRATCH_MATRIX_0F0, SCRATCH_SVECTOR_0A0, SCRATCH_VECTOR_000);
    TransMatrix(&worldmapMainMatrix2, SCRATCH_VECTOR_000);
}

void initWorldmapState(void);

/* Noah: allocAndInitWorldmapState (worldmap.cpp) */
void allocAndInitWorldmapState(void) {
    gWorldmapState = allocateHeap((s32)sizeof(sWorldmapState), 0);
    initWorldmapState();
}

/* free gWorldmapState (worldmap.cpp) */
void freeWorldmapState(void) {
    freeHeap(gWorldmapState);
}

/* Noah: initWorldmapState (worldmap.cpp) */
void initWorldmapState(void) {
    s32 i;
    for (i = 0; i < 0x40; i++) {
        gWorldmapState->m0_entries[i].m4C_sprite = NULL;
        gWorldmapState->m0_entries[i].m18_init = NULL;
        gWorldmapState->m0_entries[i].m1C_update = NULL;
    }
}

/* set a task's init function (worldmapTask.cpp) */
void setWorldmapTaskInit(s32 (*init)(s32), s32 index) {
    sWorldmapStateEntry* p = gWorldmapState->m0_entries;
    p[index].m0_state = 0;
    p[index].m18_init = init;
}

/* Noah: setupWorldmapTask (worldmapTask.cpp) */
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32)) {
    s32 i;
    for (i = 0; i < 0x40; i++) {
        if (gWorldmapState->m0_entries[i].m1C_update == NULL) {
            gWorldmapState->m0_entries[i].m0_state = 0;
            gWorldmapState->m0_entries[i].m2_waitTimer = 0;
            gWorldmapState->m0_entries[i].m4_request = 0;
            gWorldmapState->m0_entries[i].m18_init = init;
            gWorldmapState->m0_entries[i].m1C_update = update;
            gWorldmapState->m0_entries[i].m20_subState = 0;
            gWorldmapState->m0_entries[i].m22_timer = 0;
            return;
        }
    }
}

/* Noah: changeWorldmapEntityState (worldmap.cpp) */
s32 changeWorldmapEntityState(s32 index, s32 value) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    if (p->m4_request == 0) {
        p->m0_state = 1;
        p->m4_request = value;
        return 1;
    }
    return 0;
}

/* task state 3 = idle (worldmapTask.cpp) */
void setWorldmapTaskIdle(s32 index) {
    gWorldmapState->m0_entries[index].m0_state = 3;
}

/* task state 4 = delete sprite (worldmapTask.cpp) */
void setWorldmapTaskDeleteSprite(s32 index) {
    gWorldmapState->m0_entries[index].m0_state = 4;
}

/* task state 2 = wait m2 frames (worldmapTask.cpp) */
void setWorldmapTaskWait(s32 index, s16 value) {
    gWorldmapState->m0_entries[index].m0_state = 2;
    gWorldmapState->m0_entries[index].m2_waitTimer = value;
}

s32 pcWm60OffTick(void);
s32 pcWm60GroundTick(void);
void pcWm60FaderDraw(s32 index);
void pcWm60DialogDraw(s32 index, s32 info);
s32 worldmap_taskFader_update(s32);
s32 worldmap_taskExit_update(s32);
s32 worldmap_taskInfoDialog_update(s32);
s32 worldmap_taskGround_update(s32);

/* Enhanced, the 60 Hz world map's frame between (w_80070CFC.c): the motion
 * tasks run (below); what only draws draws the tick's state again: the ground
 * task (its scene), the fader (its level halfway on) and the dialogs (not
 * stepped). */
s32 worldmapMode0_taskPlayer_update(s32);
s32 worldmapMode0_task_partyMember_update(s32);
s32 worldmapMode0_taskPlayerGear_update(s32);
s32 worldmapMode0_task_partyGear_update(s32);
s32 worldmap_taskYggdrasil_update(s32);
s32 worldmap_taskYggdrasilSubModels_update(s32);
s32 worldmap_taskWorldCamera_update(s32);
s32 worldmap_taskCamera_update(s32);
s32 worldmap_taskMinimapY_update(s32);
s32 worldmapMode8_taskPadCamera_update(s32);
s32 worldmap_taskScene_update(s32);
s32 worldmap_taskSceneNoBillboards_update(s32);
s32 worldmap_taskSceneNoParticles_update(s32);
s32 worldmapMode16_taskScreenWarp_update(s32);
void pcWm60Mode16WarpDraw(void);
void pcWm60FrameBetween(void);
extern s32 continueWorldmapLoop;
extern s16 worldmapExitVar0;
extern s16 worldmapExitVar1;
extern sWorldmapExitDef* worldmapCurrentExit;
extern u8 adjustLocationAfterCollisionVar0;
extern u8 targetVehicleEntityIndex;
/* The tasks that move things every vblank (end to end): on the frame between
 * they run their motion only (pcWm60OffTick inside), their result ignored
 * (a task's state changes on the tick). */
static s32 pcWm60MotionTask(s32 (*update)(s32)) {
    return update == worldmapMode0_taskPlayer_update || update == worldmapMode0_task_partyMember_update ||
           update == worldmapMode0_taskPlayerGear_update || update == worldmapMode0_task_partyGear_update ||
           update == worldmap_taskYggdrasil_update || update == worldmap_taskYggdrasilSubModels_update ||
           update == worldmap_taskWorldCamera_update || update == worldmap_taskCamera_update ||
           update == worldmap_taskMinimapY_update || update == worldmapMode8_taskPadCamera_update;
}
/* The tasks that draw the scene (the ground task; the event modes' scene tasks). */
static s32 pcWm60SceneTask(s32 (*update)(s32)) {
    return update == worldmap_taskGround_update || update == worldmap_taskScene_update ||
           update == worldmap_taskSceneNoBillboards_update || update == worldmap_taskSceneNoParticles_update;
}
static void pcWm60DrawTasks(void) {
    s32 i;
    sWorldmapStateEntry* p = gWorldmapState->m0_entries;

    pcWm60FrameBetween();
    for (i = 0; i < 0x40; i++, p++) {
        if (p->m1C_update == NULL || p->m0_state != 1) {
            continue;
        }
        if (pcWm60MotionTask(p->m1C_update)) {
            if (continueWorldmapLoop != 0) {
                /* what the tick's decisions read (the exit under the player, the
                 * vehicle beside it, the model it walks on) stays the tick's */
                s16 exit0 = worldmapExitVar0;
                s16 exit1 = worldmapExitVar1;
                sWorldmapExitDef* exit = worldmapCurrentExit;
                u8 adjust = adjustLocationAfterCollisionVar0;
                u8 vehicle = targetVehicleEntityIndex;
                s32 model0 = worldmapModelVar0;
                s32 model1 = worldmapModelVar1;
                p->m1C_update(i); /* (leaving the map: nothing moves after the tick) */
                worldmapExitVar0 = exit0;
                worldmapExitVar1 = exit1;
                worldmapCurrentExit = exit;
                adjustLocationAfterCollisionVar0 = adjust;
                targetVehicleEntityIndex = vehicle;
                worldmapModelVar0 = model0;
                worldmapModelVar1 = model1;
            }
        } else if (p->m1C_update == worldmap_taskFader_update) {
            pcWm60FaderDraw(i);
        } else if (p->m1C_update == worldmap_taskExit_update || p->m1C_update == worldmap_taskInfoDialog_update) {
            pcWm60DialogDraw(i, p->m1C_update == worldmap_taskInfoDialog_update);
        } else if (pcWm60SceneTask(p->m1C_update) && pcWm60GroundTick()) {
            p->m1C_update(i); /* only after a tick that drew: its second halves */
        } else if (p->m1C_update == worldmapMode16_taskScreenWarp_update) {
            pcWm60Mode16WarpDraw(); /* event 16's warp strips again, the tick's */
        }
    }
}

/* Noah: callAllWorlmapObjects (worldmapTask.cpp) */
void callAllWorlmapObjects(void) {
    s32 i;
    sWorldmapStateEntry* p = gWorldmapState->m0_entries;

    if (pcWm60OffTick()) {
        pcWm60DrawTasks();
        return;
    }
    for (i = 0; i < 0x40; i++, p++) {
        if (p->m1C_update != NULL) {
            switch (p->m0_state) {
            case 0:
                p->m0_state = p->m18_init(i);
                break;
            case 1:
                p->m0_state = p->m1C_update(i);
                break;
            case 2:
                if (--p->m2_waitTimer <= 0) {
                    p->m0_state = 1;
                }
                break;
            case 3:
                break;
            case 4:
                if (p->m4C_sprite != NULL) {
                    deleteFieldEntitySub4(p->m4C_sprite);
                }
                break;
            }
        }
    }
}

/* Noah: createWorlmapPolyFT3Array (worldmap.cpp) */
void createWorlmapPolyFT3Array(void) {
    s32 i;
    POLY_FT3* p;

    worldmapRenderingStructs[0].m74_polyFT3 = allocateHeap(2048 * (s32)sizeof(POLY_FT3), 1);
    worldmapRenderingStructs[1].m74_polyFT3 = allocateHeap(2048 * (s32)sizeof(POLY_FT3), 1);
    for (i = 0, p = worldmapRenderingStructs[0].m74_polyFT3; i < 0x800; i++, p++) {
        setlen(p, 7);
        setcode(p, 0x24);
        p->r0 = 0x80;
        p->g0 = 0x80;
        p->b0 = 0x80;
    }
    *(sWorldmapPolyFT3Array*)worldmapRenderingStructs[1].m74_polyFT3 = *(sWorldmapPolyFT3Array*)worldmapRenderingStructs[0].m74_polyFT3;
}

/* Noah: loadWorldmapTextureFromFile2 (worldmap.cpp) */
void loadWorldmapTextureFromFile2(void) {
    RECT rect;
    s32 i;
    s32 x;
    s32 y;
    u16* buf0;
    u16* buf1;

    buf0 = (u16*)mallocAndDecompress(worldmapFile2Buffer, 1);
    loadMenuImageBundle((u32*)buf0);
    DrawSync(0);
    freeHeap(buf0);
    freeHeap(worldmapFile2Buffer);
    buf0 = allocateHeap(0x400, 1);
    buf1 = allocateHeap(0x8000, 1);
    rect.y = 0x1E0;
    rect.x = 0;
    rect.w = 0x100;
    rect.h = 2;
    StoreImage(&rect, (u32*)buf0);
    DrawSync(0);
    tweakWorldmapTexture(buf0, buf1, 0x20, worldMapFarColor);
    tweakWorldmapTexture(buf0 + 0x100, buf1 + 0x2000, 0x20, worldMapFarColor);
    rect.y = 0x1B0;
    rect.x = 0;
    rect.w = 0x100;
    rect.h = 0x40;
    LoadImage(&rect, buf1);
    DrawSync(0);
    for (i = 0; i < 0x40; i++) {
        worldmapGroundCluts[i] = GetClut(rect.x, rect.y);
        rect.y++;
    }
    x = 0x200;
    y = 0;
    for (i = 0; i < 4; i++) {
        worldmapGroundTpages[i] = GetTPage(1, 0, x, y);
        x += 0x80;
    }
    x = 0x180;
    y = 0x100;
    for (i = 4; i < 7; i++) {
        worldmapGroundTpages[i] = GetTPage(1, 0, x, y);
        x += 0x80;
    }
    freeHeap(buf1);
    freeHeap(buf0);
}

/* Noah: setupInitialGrid (worldmapWorldStreaming.cpp) */
void setupInitialGrid(VECTOR* p) {
    s32 i;

    streamingMatrixIdentity = worldMapIdentityMatrix;
    for (i = 0xFF; i >= 0; i--) {
        worldmapChunks[i] = NULL;
    }
    worldmapGridInputPosition.vx = p->vx & 0x7FFFFF;
    worldmapGridInputPosition.vy = 0;
    worldmapGridInputPosition.vz = p->vz & 0x7FFFFF;
    worldmapWaterWave = 0;
    worldmapWaterWave2 = 0x400;
    worldmapGridPosition.vx = 2;
    worldmapGridPosition.vy = 0;
    worldmapGridPosition.vz = 2;
    updateWorldmapGrids(p);
    triggerInitialWorldLoading();
}

/* setupInitialGrid without grid-input reset (worldmapWorldStreaming.cpp) */
void reloadWorldmapInitialGrid(VECTOR* p) {
    s32 i;

    streamingMatrixIdentity = worldMapIdentityMatrix;
    for (i = 0xFF; i >= 0; i--) {
        worldmapChunks[i] = NULL;
    }
    worldmapWaterWave = 0;
    worldmapWaterWave2 = 0x400;
    updateWorldmapGrids(p);
    triggerInitialWorldLoading();
}

/* free worldmapChunks (worldmapWorldStreaming.cpp) */
void freeWorldmapChunks(void) {
    s32 i;

    for (i = 0; i < 0x100; i++) {
        if (worldmapChunks[i] != NULL) {
            freeHeap(worldmapChunks[i]);
        }
    }
}

/* Noah: triggerInitialWorldLoading (worldmapWorldStreaming.cpp) */
void triggerInitialWorldLoading(void) {
    s32 a;
    s32 b;
    s32 i;
    s32 j;
    s32 sector;
    s16 id;
    char* name;

    a = getCdromFnd();
    b = getCdromFnd();
    if ((a == 0) | (b == -1)) {
        sector = getFileStartSector(worldmapFile9);
        for (i = 3; i < 6; i++) {
            for (j = 3; j < 6; j++) {
                id = worldmapGrid[i * 9 + j];
                if (worldmapChunks[id] == NULL) {
                    worldmapChunks[id] = allocateHeap(0x710, 0);
                    setWorldmapCellTriangleValue(sector + id, 0x710, worldmapChunks[id]);
                }
            }
        }
        setWorldmapCellTriangleValue(0, 0, 0);
        finalizeLoadingWorldmapCell();
        for (i = 0; i < 9; i++) {
            for (j = 0; j < 9; j++) {
                id = worldmapGrid[i * 9 + j];
                if (worldmapChunks[id] == NULL) {
                    worldmapChunks[id] = allocateHeap(0x710, 0);
                    setWorldmapCellTriangleValue(sector + id, 0x710, worldmapChunks[id]);
                }
            }
        }
        setWorldmapCellTriangleValue(0, 0, 0);
        finalizeLoadingWorldmapCell();
    } else {
        name = getFilenameFromPC(worldmapFile9);
        for (i = 0; i < 9; i++) {
            for (j = 0; j < 9; j++) {
                id = worldmapGrid[i * 9 + j];
                if (worldmapChunks[id] == NULL) {
                    worldmapChunks[id] = allocateHeap(0x710, 0);
                    setWorldmapCellQuadValue(name, id << 11, 0x710, worldmapChunks[id]);
                }
            }
        }
        setWorldmapCellQuadValue(0, 0, 0, 0);
        finalizeLoadingWorldmapCellFromPC();
    }
}

/* Noah: worldmapDotInit (worldmapWorldStreaming.cpp) */
void worldmapDotInit(void) {
    OuterProduct0(&worldmapFrustumLeftEdge, &worldmapFrustumUpAxis, &worldmapFrustumLeftPlane);
    OuterProduct0(&worldmapFrustumUpAxis, &worldmapFrustumRightEdge, &worldmapFrustumRightPlane);
    OuterProduct0(&worldmapFrustumTopEdge, &worldmapFrustumSideAxis, &worldmapFrustumTopPlane);
    OuterProduct0(&worldmapFrustumSideAxis, &worldmapFrustumBottomEdge, &worldmapFrustumBottomPlane);
}

/* Noah: setWorldmapGridUpdateMask (worldmapWorldStreaming.cpp) */
void setWorldmapGridUpdateMask(VECTOR* p) {
    s32 x = p->vx;
    s32 z = p->vz;

    worldmapGridUpdateMask = 0;
    if (x < -0x800000) {
        p->vx = x + 0x800000;
        worldmapGridUpdateMask = 4;
    } else if (x > 0x800000) {
        p->vx = x - 0x800000;
        worldmapGridUpdateMask = 8;
    }
    if (z < -0x800000) {
        p->vz += 0x800000;
        worldmapGridUpdateMask |= 1;
    } else if (z > 0x800000) {
        p->vz -= 0x800000;
        worldmapGridUpdateMask |= 2;
    }
    worldmapGridPosition.vx = (p->vx >> 23) + 2;
    worldmapGridPosition.vz = (p->vz >> 23) + 2;
}

/* Noah: updateWorldmapGrids (worldmapWorldStreaming.cpp) */
void updateWorldmapGrids(VECTOR* pos) {
    s32 sizeX = worldmapSizeX;
    s32 sizeY = worldmapSizeY;
    s32 x;
    s32 z;
    s32 xx;
    s32 base;
    s32 i;
    s32 j;
    s16* p;

    x = (pos->vx >> 12) / 8 - ((worldmapGridPosition.vx + 2) << 8);
    z = (pos->vz >> 12) / 8 - ((worldmapGridPosition.vz + 2) << 8);
    if (x < 0) {
        x += sizeX << 8;
    } else if (x > sizeX << 8) {
        x -= sizeX << 8;
    }
    if (z < 0) {
        z += sizeY << 8;
    } else if (z > sizeY << 8) {
        z -= sizeY << 8;
    }
    x >>= 8;
    z >>= 8;
    *(sWorldmapGrid*)worldmapGrid2 = *(sWorldmapGrid*)worldmapGrid;
    xx = x;
    p = worldmapGrid;
    for (i = 8; i != -1; i--) {
        if (z >= sizeY) {
            z = 0;
        }
        x = xx;
        base = z * sizeX;
        for (j = 8; j != -1; j--) {
            if (x >= sizeX) {
                x = 0;
            }
            *p++ = base + x;
            x++;
        }
        z++;
    }
}

void worldmapGroundPrepareRenderingTable(VECTOR* pos) {
    s32 x;
    s32 z;
    s32 vis;
    u32 w;
    s16* mega;
    s32* tiles;
    sWorldmapScratch* s = WM_SCRATCH;

    s->mF0_rotation = streamingMatrixIdentity;
    CompMatrix(&worldmapMainMatrix2, &s->mF0_rotation, &s->m110);
    SetRotMatrix(&s->m110);
    SetTransMatrix(&s->m110);
    mega = (s16*)megatilesVisibility;
    tiles = (s32*)tilesVisibility;
    s->m40.vx = -((pos->vx >> 12) & 0x7FF) - 0x1000;
    s->m40.vz = -((pos->vz >> 12) & 0x7FF) - 0x1000;
    s->mE0_quadCenter.vy = 0;
    s->mD8_quadMidpoint23.vy = 0;
    s->mD0_quadMidpoint13.vy = 0;
    s->mC8_quadMidpoint02.vy = 0;
    s->mC0_quadMidpoint01.vy = 0;
    s->mB8_quadCorner3.vy = 0;
    s->mB0_quadCorner2.vy = 0;
    s->mA8.vy = 0;
    s->mA0.vy = 0;
    s->mA0.vz = -s->m40.vz;
    for (x = 0; x < 5; x++) {
        s->mA0.vx = s->m40.vx;
        for (z = 0; z < 5; z++) {
            s->mA8.vx = s->mA0.vx + 0x800;
            s->mA8.vz = s->mA0.vz;
            s->mB0_quadCorner2.vx = s->mA0.vx;
            s->mB0_quadCorner2.vz = s->mA0.vz - 0x800;
            s->mB8_quadCorner3.vx = s->mA0.vx + 0x800;
            s->mB8_quadCorner3.vz = s->mA0.vz - 0x800;
            vis = *mega = computeTileVisibility(&s->mA0, &s->mA8, &s->mB0_quadCorner2, &s->mB8_quadCorner3);
            if (vis == 0) {
                s->mC0_quadMidpoint01.vz = s->mA0.vz;
                s->mC0_quadMidpoint01.vx = s->mA0.vx + 0x400;
                s->mC8_quadMidpoint02.vz = s->mA0.vz - 0x400;
                s->mC8_quadMidpoint02.vx = s->mA0.vx;
                s->mD0_quadMidpoint13.vx = s->mA8.vx;
                s->mD0_quadMidpoint13.vz = s->mC8_quadMidpoint02.vz;
                s->mD8_quadMidpoint23.vx = s->mC0_quadMidpoint01.vx;
                s->mD8_quadMidpoint23.vz = s->mB0_quadCorner2.vz;
                s->mE0_quadCenter.vx = s->mC0_quadMidpoint01.vx;
                s->mE0_quadCenter.vz = s->mC8_quadMidpoint02.vz;
                w = (u16)computeTileVisibility(&s->mA0, &s->mC0_quadMidpoint01, &s->mC8_quadMidpoint02, &s->mE0_quadCenter);
                w |= computeTileVisibility(&s->mC0_quadMidpoint01, &s->mA8, &s->mE0_quadCenter, &s->mD0_quadMidpoint13) << 16;
                tiles[0] = w;
                w = (u16)computeTileVisibility(&s->mC8_quadMidpoint02, &s->mE0_quadCenter, &s->mB0_quadCorner2, &s->mD8_quadMidpoint23);
                w |= computeTileVisibility(&s->mE0_quadCenter, &s->mD0_quadMidpoint13, &s->mD8_quadMidpoint23, &s->mB8_quadCorner3) << 16;
            } else {
                w = (u16)vis;
                w |= w << 16;
                tiles[0] = w;
            }
            tiles[1] = w;
            tiles += 2;
            s->mA0.vx += 0x800;
            mega++;
        }
        s->mA0.vz -= 0x800;
    }
    if (((pos->vx >> 12) & 0x7FF) < 0x400) {
        z = 0;
    } else {
        z = 1;
    }
    if (((pos->vz >> 12) & 0x7FF) >= 0x400) {
        z |= 2;
    }
    mega = (s16*)mapQuadrantTilesPatch[z][0];
    tiles = (s32*)tilesVisibility;
    for (x = 0; x < 25; x++) {
        tiles[0] |= ((s32*)mega)[0];
        tiles[1] |= ((s32*)mega)[1];
        mega += 4;
        tiles += 2;
    }
}

/* Noah: computeTileVisibility (worldmapWorldStreaming.cpp); splat says Handwritten, it is C + gte.h macros */
s16 computeTileVisibility(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3) {
    VECTOR* p = (VECTOR*)PSX_SCRATCH(0x1F800000);
    s32 a;
    s32 b;
    s32 c;
    s32 d;
    s16 r0;
    s16 r1;
    s16 r2;
    s16 r3;

    gte_ldv0(v0);
    gte_rt();
    gte_stlvnl(&p[0]);
    gte_ldv0(v1);
    gte_rt();
    gte_stlvnl(&p[1]);
    gte_ldv0(v2);
    gte_rt();
    gte_stlvnl(&p[2]);
    gte_ldv0(v3);
    gte_rt();
    gte_stlvnl(&p[3]);

    r0 = 0;
    a = (p[0].vx * worldmapFrustumLeftPlane.vx + p[0].vz * worldmapFrustumLeftPlane.vz) >> 12;
    b = (p[1].vx * worldmapFrustumLeftPlane.vx + p[1].vz * worldmapFrustumLeftPlane.vz) >> 12;
    c = (p[2].vx * worldmapFrustumLeftPlane.vx + p[2].vz * worldmapFrustumLeftPlane.vz) >> 12;
    d = (p[3].vx * worldmapFrustumLeftPlane.vx + p[3].vz * worldmapFrustumLeftPlane.vz) >> 12;
    if (a < 0 && b < 0) {
        r0++;
    }
    if (b < 0 && d < 0) {
        r0++;
    }
    if (d < 0 && c < 0) {
        r0++;
    }
    if (c < 0 && a < 0) {
        r0++;
    }
    if (r0 == 4) {
        return -1;
    }

    r1 = 0;
    a = (p[0].vx * worldmapFrustumRightPlane.vx + p[0].vz * worldmapFrustumRightPlane.vz) >> 12;
    b = (p[1].vx * worldmapFrustumRightPlane.vx + p[1].vz * worldmapFrustumRightPlane.vz) >> 12;
    c = (p[2].vx * worldmapFrustumRightPlane.vx + p[2].vz * worldmapFrustumRightPlane.vz) >> 12;
    d = (p[3].vx * worldmapFrustumRightPlane.vx + p[3].vz * worldmapFrustumRightPlane.vz) >> 12;
    if (a < 0 && b < 0) {
        r1++;
    }
    if (b < 0 && d < 0) {
        r1++;
    }
    if (d < 0 && c < 0) {
        r1++;
    }
    if (c < 0 && a < 0) {
        r1++;
    }
    if (r1 == 4) {
        return -1;
    }

    r2 = 0;
    a = (p[0].vy * worldmapFrustumTopPlane.vy + p[0].vz * worldmapFrustumTopPlane.vz) >> 12;
    b = (p[1].vy * worldmapFrustumTopPlane.vy + p[1].vz * worldmapFrustumTopPlane.vz) >> 12;
    c = (p[2].vy * worldmapFrustumTopPlane.vy + p[2].vz * worldmapFrustumTopPlane.vz) >> 12;
    d = (p[3].vy * worldmapFrustumTopPlane.vy + p[3].vz * worldmapFrustumTopPlane.vz) >> 12;
    if (a < 0 && b < 0) {
        r2++;
    }
    if (b < 0 && d < 0) {
        r2++;
    }
    if (d < 0 && c < 0) {
        r2++;
    }
    if (c < 0 && a < 0) {
        r2++;
    }
    if (r2 == 4) {
        return -1;
    }

    r3 = 0;
    a = (p[0].vy * worldmapFrustumBottomPlane.vy + p[0].vz * worldmapFrustumBottomPlane.vz) >> 12;
    b = (p[1].vy * worldmapFrustumBottomPlane.vy + p[1].vz * worldmapFrustumBottomPlane.vz) >> 12;
    c = (p[2].vy * worldmapFrustumBottomPlane.vy + p[2].vz * worldmapFrustumBottomPlane.vz) >> 12;
    d = (p[3].vy * worldmapFrustumBottomPlane.vy + p[3].vz * worldmapFrustumBottomPlane.vz) >> 12;
    if (a < 0 && b < 0) {
        r3++;
    }
    if (b < 0 && d < 0) {
        r3++;
    }
    if (d < 0 && c < 0) {
        r3++;
    }
    if (c < 0 && a < 0) {
        r3++;
    }
    if (r3 != 4) {
        return (s16)(r1 | r0 | r2 | r3) == 0;
    }
    return -1;
}

/* The grid stores signed chunk IDs in 16-bit cells. */
static __inline__ s32 decodeWorldmapGridId(u16 cell) {
    return (s16)cell;
}

static __inline__ s32 loadHorizontalCd(u8** chunk, s32 sector, s16 id, s32 flags) {
    if (*chunk != NULL) {
        return flags;
    }
    *chunk = allocateHeap(0x710, 0);
    setWorldmapCellTriangleValue(sector + id, 0x710, *chunk);
    flags = (u8)(flags | 2);
    return flags;
}

static __inline__ s32 loadHorizontalPc(u8** chunk, sptr sector, s16 id, s32 flags) {
    if (*chunk != NULL) {
        return flags;
    }
    *chunk = allocateHeap(0x710, 0);
    setWorldmapCellQuadValue((char*)sector, id << 11, 0x710, *chunk);
    flags = (u8)(flags | 2);
    return flags;
}

/* Reconcile cached grid chunks, then load missing edges and corners. */
void streamWorldmap1(void) {
    u32 rawId;
    s32 phaseIndex;
    s32 alternate;
    s32 i;
    s32 j;
    s32 k;
    s32 id;
    s32 flags;
    sptr sector;
    u8** chunk;
    u8* buf;
    s32 a;
    s16* grid;
    u8** chunks;

    for (j = 0; j < 0x51; j++) {
        id = worldmapGrid2[j];
        if (worldmapChunks[id] != NULL) {
            for (phaseIndex = 0; phaseIndex < 0x51; phaseIndex++) {
                if (worldmapGrid[phaseIndex] == id) {
                    break;
                }
            }
            if (phaseIndex == 0x51) {
                freeHeap(worldmapChunks[id]);
                worldmapChunks[id] = NULL;
            }
        }
    }
    flags = 0;
    a = getCdromFnd();
    if ((a == 0) | (getCdromFnd() == -1)) {
        sector = getFileStartSector(worldmapFile9);
        grid = worldmapGrid;
        chunks = worldmapChunks;
        id = 1;
        i = 1;
        do {
            j = 6;
            k = id;
            do {
                /* Single-pass scope preserves the grid cursor's allocation
                 * weight in GCC 2.7.2. */
                do {
                    rawId = ((u16*)grid)[k];
                } while (0);
                alternate = decodeWorldmapGridId(rawId);
                chunk = (u8**)(alternate * (s32)sizeof(u8*) + (uptr)chunks);
                flags = loadHorizontalCd(chunk, (s32)sector, alternate, flags);
                j--;
                k++;
            } while (j != -1);
            i--;
            id = 0x49;
        } while (i != j);
        sector = getFileStartSector(worldmapFile10);
        grid = worldmapGrid;
        chunks = worldmapChunks;
        id = 9;
        i = 1;
        do {
            j = 6;
            k = id;
            do {
                alternate = grid[k];
                chunk = (u8**)(alternate * (s32)sizeof(u8*) + (uptr)chunks);
                if (*chunk == NULL) {
                    buf = allocateHeap(0x710, 0);
                    setWorldmapCellTriangleValue((s32)sector + ((alternate % worldmapSizeX) * worldmapSizeY + alternate / worldmapSizeX), 0x710, *chunk = buf);
                    flags = (u8)(flags | 1);
                }
                j--;
                k += 9;
            } while (j != -1);
            i--;
            id = 0x11;
        } while (i != j);
        for (j = 0; j < 4; j++) {
            i = worldmapGrid[worldmapGridCornerIndices[j]];
            if (worldmapChunks[i] == NULL) {
                worldmapChunks[i] = allocateHeap(0x710, 0);
                if ((u8)(flags & 2)) {
                    sector = getFileStartSector(worldmapFile9) + i;
                    setWorldmapCellTriangleValue((s32)sector, 0x710, worldmapChunks[i]);
                } else if ((u8)(flags & 1)) {
                    sector = getFileStartSector(worldmapFile10) + ((i % worldmapSizeX) * worldmapSizeY + i / worldmapSizeX);
                    setWorldmapCellTriangleValue((s32)sector, 0x710, worldmapChunks[i]);
                }
            }
        }
        setWorldmapCellTriangleValue(0, 0, 0);
        finalizeLoadingWorldmapCell();
    } else {
        sector = (sptr)getFilenameFromPC(worldmapFile9);
        grid = worldmapGrid;
        chunks = worldmapChunks;
        id = 1;
        phaseIndex = 1;
        do {
            j = 6;
            k = id;
            do {
                rawId = ((u16*)grid)[k];
                alternate = (s16)rawId;
                chunk = (u8**)(alternate * (s32)sizeof(u8*) + (uptr)chunks);
                flags = loadHorizontalPc(chunk, sector, alternate, flags);
                j--;
                k++;
            } while (j != -1);
            phaseIndex--;
            id = 0x49;
        } while (phaseIndex != j);
        sector = (sptr)getFilenameFromPC(worldmapFile10);
        grid = worldmapGrid;
        chunks = worldmapChunks;
        id = 9;
        phaseIndex = 1;
        do {
            j = 6;
            k = id;
            do {
                alternate = grid[k];
                chunk = (u8**)(alternate * (s32)sizeof(u8*) + (uptr)chunks);
                if (*chunk == NULL) {
                    buf = allocateHeap(0x710, 0);
                    setWorldmapCellQuadValue((char*)sector, ((alternate % worldmapSizeX) << 11) * worldmapSizeY + ((alternate / worldmapSizeX) << 11), 0x710, *chunk = buf);
                    flags = (u8)(flags | 1);
                }
                j--;
                k += 9;
            } while (j != -1);
            phaseIndex--;
            id = 0x11;
        } while (phaseIndex != j);
        for (j = 0; j < 4; j++) {
            i = worldmapGrid[worldmapGridCornerIndices[j]];
            if (worldmapChunks[i] == NULL) {
                worldmapChunks[i] = allocateHeap(0x710, 0);
                if ((u8)(flags & 2)) {
                    setWorldmapCellQuadValue(getFilenameFromPC(worldmapFile9), i << 11, 0x710, worldmapChunks[i]);
                } else if ((u8)(flags & 1)) {
                    setWorldmapCellQuadValue(getFilenameFromPC(worldmapFile10), ((i % worldmapSizeX) << 11) * worldmapSizeY + ((i / worldmapSizeX) << 11), 0x710, worldmapChunks[i]);
                }
            }
        }
        setWorldmapCellQuadValue(0, 0, 0, 0);
        finalizeLoadingWorldmapCellFromPC();
    }
}

void drawWorldmapGround(void* ot, POLY_FT3* prims, VECTOR* pos) {
    s32 i;
    s32 j;
    s32 k;
    s16 vis;
    u32* cell;
    sWorldmapScratch* s = WM_SCRATCH;

    for (j = 0; j < 0x40; j++) {
        s->m288_clut[j] = worldmapGroundCluts[j];
    }
    for (j = 0; j < 7; j++) {
        s->m308_tpage[j] = worldmapGroundTpages[j];
    }
    s->m350_groundModelMatrix = streamingMatrixIdentity;
    CompMatrix(&worldmapMainMatrix2, &s->m350_groundModelMatrix, &s->m370_groundMatrix);
    SetRotMatrix(&s->m370_groundMatrix);
    SetTransMatrix(&s->m370_groundMatrix);
    s->m318_groundOrigin.vx = -((pos->vx >> 12) & 0x7FF) - 0x1000;
    s->m318_groundOrigin.vz = -((pos->vz >> 12) & 0x7FF) - 0x1000;
    renderWorldmapGroundVar1 = 0;
    s->m328_cellCorners[0].vz = -s->m318_groundOrigin.vz;
    for (k = 0, i = 0; i < 5; i++) {
        s->m328_cellCorners[0].vx = s->m318_groundOrigin.vx;
        for (j = 0; j < 5; j++, k++) {
            if (((s16*)megatilesVisibility)[k] != -1) {
                cell = (u32*)worldmapChunks[worldmapGrid[(i + worldmapGridPosition.vz) * 9 + j + worldmapGridPosition.vx]];
                pcWorldmapGroundChunk(worldmapGrid[(i + worldmapGridPosition.vz) * 9 + j + worldmapGridPosition.vx], cell);
                s->m328_cellCorners[3].vx = s->m328_cellCorners[1].vx = s->m328_cellCorners[0].vx + 0x400;
                s->m328_cellCorners[1].vz = s->m328_cellCorners[0].vz;
                s->m328_cellCorners[2].vx = s->m328_cellCorners[0].vx;
                s->m328_cellCorners[3].vz = s->m328_cellCorners[2].vz = s->m328_cellCorners[0].vz - 0x400;
                vis = tilesVisibility[0][k][0] | tilesVisibility[0][k][1] | tilesVisibility[0][k][2] | tilesVisibility[0][k][3];
                if (vis != -1) {
                    drawWorldmapGroundCell(cell, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[0]);
                    drawWorldmapGroundCell(cell + 0x51, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[1]);
                    drawWorldmapGroundCell(cell + 0xA2, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[2]);
                    drawWorldmapGroundCell(cell + 0xF3, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[3]);
                } else {
                    if (tilesVisibility[0][k][0] != vis) {
                        drawWorldmapGroundCell(cell, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[0]);
                    }
                    if (tilesVisibility[0][k][1] != vis) {
                        drawWorldmapGroundCell(cell + 0x51, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[1]);
                    }
                    if (tilesVisibility[0][k][2] != vis) {
                        drawWorldmapGroundCell(cell + 0xA2, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[2]);
                    }
                    if (tilesVisibility[0][k][3] != vis) {
                        drawWorldmapGroundCell(cell + 0xF3, ot, prims + renderWorldmapGroundVar1, &s->m328_cellCorners[3]);
                    }
                }
            }
            s->m328_cellCorners[0].vx += 0x800;
        }
        s->m328_cellCorners[0].vz -= 0x800;
    }
    pcWorldmapGroundChunk(-1, NULL);
    /* The wide picture's margins (port/src/game/worldmap/wide.c). */
    pcWideWorldmapGround(ot, pos);
}

void drawWorldmapGroundCell(u32* cell, void* ot, POLY_FT3* prims, SVECTOR* start) {
    s32 i;
    s32 j;
    u32* src;
    SVECTOR* out;
    s32 x;
    s32 z;
    s32 x0;
    s32 c;
    u32 v;
    u32 wave;
    u32 wave2;

    out = (SVECTOR*)PSX_SCRATCH(0x1F800000);
    src = cell;
    wave2 = worldmapWaterWave2;
    x0 = start->vx;
    z = start->vz;
    for (i = 8; i != -1; i--) {
        x = x0;
        c = rcossin_tbl[(wave2 & 0xFFF) * 2] * 2;
        wave = worldmapWaterWave;
        for (j = 8; j != -1; j--) {
            v = *src;
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
    drawWorldmapGroundCellSub0(cell, ot, prims);
}
