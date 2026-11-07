/*
 * The map viewer's way into the world map overlay (mapsview.h; PC only,
 * called inside an extras sandbox, never by the game).
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sound.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"
#include "mapsview.h"

/* port/src/game/main/mapsview.c */
void mapsStartKernel(void);

/* main */
void allocateShapeTransfert(s32 size, s32 flags);
extern s32 gDepthDivider;
extern u8 isWorldMapOverlayLoaded;

/* worldmap */
void allocateWorldmapData(void);
void allocateWorldMapsOT(void);
void setupWorldmapGearMode(void);
void initWorldMap(s32 worldmapMode, s32 scenarioFlag);
void initWorldmapGraphics(void);
void worldmapMode8_init(void);
void finalizeWorldMapFileLoading(void);
void allocAndInitWorldmapState(void);
void setupWorldmapPositionFromFileData(s32 index);
void loadWorldmapTextureFromFile3(void);
void loadWorldmapTextureFromFile2(void);
void worldmapInitAllModels(void);
void initWorldmapSkyGradient(void);
void initWorldmapBillboards(void);
void initWorldmapClouds(void);
void initWorldmapTexAnims1(void);
void initWorldmapTexAnims2(void);
void updateWorldmapTexAnims1(void);
void updateWorldmapTexAnims2(void);
void initWorldmapSkyCloudBand(void);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void createWorlmapPolyFT4Array3(void);
void setupInitialGrid(VECTOR* p);
s32 loadWorldmapChunk(void);
s32 isWorldStreamingDone(void);
void freeWorldmapChunks(void);
void worldmapGroundPrepareRenderingTable(VECTOR* pos);
void drawWorldmapGround(OTag* ot, POLY_FT3* prims, VECTOR* pos);
void renderWorldmapModels(void);
void drawWorldmapBillboards(void);
void drawWorldmapSkyGradient(void);
void drawWorldmapSkyCloudBand(void);
void drawWorldmapClouds(void);
void worldmapDotInit(void);
void moveWorldmapClouds(void);
extern void (*worldmapCloudsMoveCallback)(void); /* the clouds' motion (worldmapMode0_update) */
void SetFogNearFar(s32 a, s32 b, s32 h);
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern MATRIX worldmapMainMatrix2;
extern MATRIX D_8009BE4C;
extern MATRIX worldMapIdentityMatrix;
extern VECTOR worldmapRadarPosition3;
extern VECTOR worldmapRadarPosition;
extern VECTOR worldmapPosition;
extern VECTOR worldmapGridInputPosition;
extern u8* worldmapChunks[256];
extern s16 worldmapNumModels;
extern sWorldmapModel* worldmapModels;
extern sWorldmapExitDef** worldmapExitsDefs;
extern s16 megatilesVisibility[5][5];
extern s16 tilesVisibility[5][5][4];
extern s32 worldmapOddOrEven;
extern s32 exitWorldMapMode;
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;
extern s32 isWorldmapModeFlagSet;
extern s32 currentWorldmapMode;
extern s32 worldmapInitialPositionIndex;
extern s32 initWorldMapVar0;
extern s32 worldmapMatrixMode;
extern u32 worldmapGamestate1824;
extern s32 worldmapExitToFieldVar0;
extern s32 fieldBootModeFromWorldmap;
extern s32 worldmapMinimapScale;
extern s32 worldmapFile9;

#define MAPS_WORLD_OT 0x400
#define MAPS_WORLD_OT_SLACK 0x400

static OTag* mapsWorldOt;
static s32 mapsWorldFlat;
static u8 mapsChunkFlat[256];

extern u16 scenarioStateToWorldmapConfigsMapping[10];

/* The queued cells read: through the host-file path each loadWorldmapChunk
 * reads one at once (the queue holds 16). */
static void mapsWorldStream(void) {
    s32 i;

    for (i = 0; i < 16 && isWorldStreamingDone() != 0; i++) {
        loadWorldmapChunk();
    }
}

s32 mapsWorldStages(s32* out, s32 max) {
    s32 n = 0;
    s32 mode;

    /* configuration k from mapping[k] (k = 0: from 0) to mapping[k + 1] */
    out[n++] = 0;
    while (n < max && n < 16 && scenarioStateToWorldmapConfigsMapping[n] != 0xFFFF) {
        out[n] = scenarioStateToWorldmapConfigsMapping[n];
        n++;
    }
    /* The remaining table rows are selected directly by modes 8..15,
     * rather than story progress. Negative entries encode those modes. */
    for (mode = 8; mode < 16 && n < max; mode++) {
        out[n++] = -mode;
    }
    return n;
}

s32 mapsWorldStart(s32 progress) {
    s32 mode = progress < 0 ? -progress : 1;
    if (mode < 8 || mode > 15) {
        mode = 1;
    }
    if (progress < 0) {
        progress = 0;
    }
    mapsStartKernel();

    /* worldmapEntryPoint: its defaults for mode 0 (a new game) */
    InitGeom();
    isWorldMapOverlayLoaded = 1;
    gameState.m231A_fieldID = 0x400;
    gameState.m231E_worldmapInitialPositionIndex = 0xFFF;
    gameState.m231C_cameraYaw = 0xC00;
    gameState.m2320_worldmapMode = mode;
    gameState.m1834_vehicleMode = 0;
    gameState.m1836_restoreWorldmapPosition = 0;
    gameState.m22B1_isOnGear[0] = gameState.m22B1_isOnGear[1] = gameState.m22B1_isOnGear[2] = 0;
    gameState.m1930_fieldVarsBackup[0] = progress;
    resetMemoryAllocStats(3, 0);
    setCurrentDirectory(0x24, 0);
    allocateWorldmapData();
    allocateWorldMapsOT();
    setupWorldmapGearMode();
    isWorldmapModeFlagSet = 0;
    worldmapExitToFieldVar0 = 0;
    fieldBootModeFromWorldmap = (gameState.m231A_fieldID & 0x3FFF) - 0x400;
    worldmapInitialPositionIndex = (u16)gameState.m231E_worldmapInitialPositionIndex;
    worldmapGamestate1824 = gameState.m231C_cameraYaw;
    currentWorldmapMode = gameState.m2320_worldmapMode & 0x7FFF;
    initWorldMap(currentWorldmapMode, gameState.m1930_fieldVarsBackup[0]);
    if (mode >= 8) {
        initWorldMapVar0 = mode + 1; /* initWorldMap sets it only for story stages */
    }

    /* worldmapMode0_update's set-up, without the party, the fade-in, the
     * sound and the tasks */
    initWorldmapGraphics();
    worldmapMode8_init();
    finalizeWorldMapFileLoading();
    allocAndInitWorldmapState();
    D_8009BE4C = worldMapIdentityMatrix;
    worldmapMatrixMode = 0;
    worldmapCloudsMoveCallback = moveWorldmapClouds;
    worldmapDotInit();
    setupWorldmapPositionFromFileData(worldmapInitialPositionIndex);
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    worldmapInitAllModels();
    initWorldmapSkyGradient();
    initWorldmapBillboards();
    allocateShapeTransfert(0x1400, 0);
    initWorldmapClouds();
    initWorldmapTexAnims1();
    initWorldmapTexAnims2();
    initWorldmapSkyCloudBand();
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&worldmapPosition);
    mapsWorldStream();
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    worldmapRadarPosition3 = worldmapPosition;
    worldmapRadarPosition = worldmapPosition;
    /* the animated tiles (water...) in their first frame */
    updateWorldmapTexAnims1();
    updateWorldmapTexAnims2();

    mapsWorldOt = (OTag*)allocateHeap((MAPS_WORLD_OT + MAPS_WORLD_OT_SLACK) * (s32)sizeof(OTag), 0);
    mapsWorldFlat = 0;
    return mapsWorldOt != NULL && worldmapRenderingStructs[0].m74_polyFT3 != NULL && worldmapModels != NULL;
}

void mapsWorldView(MapsWorldView* out) {
    s32 t;
    s32 n;
    sWorldmapExitDef* e;

    out->sizeX = worldmapSizeX;
    out->sizeZ = worldmapSizeY;
    out->config = initWorldMapVar0;
    out->baseFile = worldmapFile9 - 9;
    out->startX = worldmapPosition.vx >> 12;
    out->startZ = worldmapPosition.vz >> 12;
    out->models = worldmapNumModels;
    for (t = 0; t < 4; t++) {
        n = 0;
        for (e = worldmapExitsDefs != NULL ? worldmapExitsDefs[t] : NULL; e != NULL && e->m8_destinationField != -1 && n < 256;
             e++) {
            n++;
        }
        out->exits[t] = n;
    }
}

s32 mapsWorldModel(s32 i, s32* x, s32* y, s32* z, s32* hidden) {
    if (i < 0 || i >= worldmapNumModels) {
        return 0;
    }
    *x = worldmapModels[i].m8_position.vx;
    *y = worldmapModels[i].m8_position.vy;
    *z = -worldmapModels[i].m8_position.vz; /* stored negated (worldmapInitAllModels) */
    *hidden = worldmapModels[i].m0_hidden != 0;
    return 1;
}

s32 mapsWorldExit(s32 t, s32 i, s32* x, s32* z, s32* w, s32* d, s32* field, s32* type) {
    sWorldmapExitDef* e;

    if (t < 0 || t >= 4 || worldmapExitsDefs == NULL || worldmapExitsDefs[t] == NULL || i < 0) {
        return 0;
    }
    e = worldmapExitsDefs[t];
    while (i > 0 && e->m8_destinationField != -1) {
        e++;
        i--;
    }
    if (e->m8_destinationField == -1) {
        return 0;
    }
    *x = e->m0_X;
    *z = e->m2_Y;
    *w = e->m4_width;
    *d = e->m6_height;
    *field = e->m8_destinationField;
    *type = e->mE_type;
    return 1;
}

/* The heights (bits 0-7 of each 32-bit sample) of a loaded chunk to 0. */
static void mapsFlattenChunk(s32 id) {
    u32* p;
    s32 i;

    if (worldmapChunks[id] == NULL || mapsChunkFlat[id]) {
        return;
    }
    p = (u32*)worldmapChunks[id];
    for (i = 0; i < 0x710 / 4; i++) {
        p[i] &= ~0xFF;
    }
    mapsChunkFlat[id] = 1;
}

void mapsWorldFlatten(s32 on) {
    s32 i;

    mapsWorldFlat = on;
    for (i = 0; on && i < 256; i++) {
        mapsFlattenChunk(i);
    }
}

void mapsWorldGoTo(s32 x, s32 z) {
    VECTOR p;
    s32 i;
    s32 sx = worldmapSizeX << 11;
    s32 sz = worldmapSizeY << 11;

    x = sx > 0 ? ((x % sx) + sx) % sx : x;
    z = sz > 0 ? ((z % sz) + sz) % sz : z;
    p.vx = x << 12;
    p.vy = 0;
    p.vz = z << 12;
    p.pad = 0;
    /* setupInitialGrid forgets the loaded chunks: free them first */
    freeWorldmapChunks();
    for (i = 0; i < 256; i++) {
        mapsChunkFlat[i] = 0;
    }
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&p);
    mapsWorldStream();
    worldmapRadarPosition3 = p;
    worldmapRadarPosition = p;
    worldmapPosition = p;
    if (mapsWorldFlat) {
        mapsWorldFlatten(1);
    }
}

void mapsWorldDraw(MATRIX* rotation, s32 eyeX, s32 eyeY, s32 eyeZ, s32 H, u32 layers) {
    sWorldmapRenderingStruct* rs = &worldmapRenderingStructs[0];
    OTag* keepOt = rs->m70_OT;
    s32 r;
    s32 t;
    s32 y;
    s32 x;

    if (mapsWorldOt == NULL) {
        return;
    }
    /* the camera: worldmapMainMatrix2 as setupWorldmapMatrix1/2 make it */
    worldmapMainMatrix2 = *rotation;
    for (r = 0; r < 3; r++) {
        t = (rotation->m[r][0] * eyeX + rotation->m[r][1] * eyeY + rotation->m[r][2] * eyeZ) >> 12;
        worldmapMainMatrix2.t[r] = -t;
    }
    rs->m70_OT = mapsWorldOt;
    pCurrentWorldmapRenderingStruct = rs;
    worldmapOddOrEven = 0;
    ClearOTagR(mapsWorldOt, MAPS_WORLD_OT);
    SetGeomOffset(0xA0, (layers & 0x80) ? 0x6C : 0x78); /* the walking map's (worldmapGeometryOffsetY), or centred */
    SetGeomScreen(H);
    worldmapMinimapScale = H;
    if (layers & 0x20) {
        SetFogNearFar(0x800, 0xE80, H); /* initWorldmapGraphics' */
    } else {
        SetFogNearFar(0x7000, 0x7FF0, H); /* past every depth drawn: the near CLUTs */
    }
    if (layers & 0x40) {
        /* a frame of the animated tiles, as the main loop steps them */
        updateWorldmapTexAnims1();
        updateWorldmapTexAnims2();
    }
    if (layers & 1) {
        worldmapGroundPrepareRenderingTable(&worldmapRadarPosition3);
        if (layers & 0x10) {
            for (y = 0; y < 5; y++) {
                for (x = 0; x < 5; x++) {
                    megatilesVisibility[y][x] = 0;
                    tilesVisibility[y][x][0] = tilesVisibility[y][x][1] = 0;
                    tilesVisibility[y][x][2] = tilesVisibility[y][x][3] = 0;
                }
            }
        }
        /* the ground's own primitives (createWorlmapPolyFT3Array set them up) */
        drawWorldmapGround(mapsWorldOt, rs->m74_polyFT3, &worldmapRadarPosition3);
    }
    if (layers & 2) {
        gDepthDivider = 2;
        renderWorldmapModels();
    }
    if (layers & 4) {
        drawWorldmapBillboards();
    }
    if (layers & 8) {
        drawWorldmapSkyCloudBand();
        drawWorldmapSkyGradient();
        drawWorldmapClouds();
    }
    DrawOTag(&mapsWorldOt[MAPS_WORLD_OT - 1]);
    rs->m70_OT = keepOt;
}
