/*
 * World map mode 12 (event scene; modes table 0x8009A058[12] = {worldmapMode8_init,
 * worldmapMode12_update, worldmapMode12_free}). Its tasks are in w_8007CC6C.c.
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern MATRIX worldMapIdentityMatrix;                   /* identity-like base matrix (Noah: worldMapIdentityMatrix?) */
extern VECTOR worldmapPosition;
extern SVECTOR worldmapRotation;
extern VECTOR worldmapRadarPosition3;
extern VECTOR worldmapRadarPosition;
extern s32 worldmapGeometryOffsetY;
extern s32 worldmapCameraVar0;
extern u16 worldmapMode12DirectorStates[];
extern u16 worldmapMode12DirectorTimers[];
extern u8 worldmapMode14TrailParticleRotation[];

extern MATRIX D_8009BE4C;                   /* Noah: worldmapMainMatrix2? */
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern s32 worldmapMatrixMode;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern sWorldmapSeqHeader* fieldSeq;
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern s32 worldmapExitToFieldVar0;

void initWorldmapGraphics(void);
void worldmapFadeIn(s32 a0, s32 a1, s32 a2, s32 a3);
void readWorldmapSequenceFile(void);
void finalizeWorldmapEventFileLoading(void);
void allocAndInitWorldmapState(void);
void moveWorldmapClouds(void);
void worldmapDotInit(void);
void worldmapInitAllModels(void);
void loadWorldmapTextureFromFile3(void);
void loadWorldmapTextureFromFile2(void);
void initWorldmapSkyGradient(void);
void initWorldmapClouds(void);
void initWorldmapTexAnims1(void);
void initWorldmapTexAnims2(void);
void initWorldmapSkyCloudBand(void);
void initWorldmapParticles(void);
void loadSequence(void*);
void unloadSequence(void*);
void stopAllSounds(void);
void setupInitialGrid(VECTOR*);
s32 loadWorldmapChunk(void);
s32 isWorldStreamingDone(void);
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
s32 worldmap_taskFader_init(s32);
s32 worldmap_taskFader_update(s32);
s32 worldmap_taskSceneNoBillboards_init(s32);
s32 worldmap_taskSceneNoBillboards_update(s32);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void initWorldmapEncounterTimers(void);
void freeWorldmapModels(void);
void freeWorldmapClouds(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapPolyFT4Array1(void);
void freeWorldmapChunks(void);
void freeWorldmapState(void);

s32 worldmapMode12_taskDirector_init(s32);
s32 worldmapMode12_taskDirector_update(s32);
s32 worldmapMode12_taskCamera_init(s32);
s32 worldmapMode12_taskCamera_update(s32);
s32 worldmapMode12_taskShip4_init(s32);
s32 worldmapMode12_taskShip4_update(s32);
s32 worldmapMode12_taskShip5_init(s32);
s32 worldmapMode12_taskShip5_update(s32);
s32 worldmapMode12_taskShip9_init(s32);
s32 worldmapMode12_taskShip9_update(s32);
s32 worldmapMode12_taskShip10_init(s32);
s32 worldmapMode12_taskShip10_update(s32);
s32 worldmapMode12_taskShip12_init(s32);
s32 worldmapMode12_taskShip12_update(s32);
s32 worldmapMode12_taskShip13_init(s32);
s32 worldmapMode12_taskShip13_update(s32);
s32 worldmapMode12_taskAirship_init(s32);
s32 worldmapMode12_taskAirship_update(s32);

#define SCRATCH_MATRIX0 ((MATRIX*)0x1F8000F0)
#define SCRATCH_MATRIX1 ((MATRIX*)0x1F800110)
#define SCRATCH_VECTOR0 ((VECTOR*)0x1F800000)
#define SCRATCH_VECTOR1 ((VECTOR*)0x1F800010)
#define SCRATCH_SVECTOR ((SVECTOR*)0x1F8000A0)

void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
s32 changeWorldmapEntityState(s32 index, s32 state);
void playSoundEffectInFreeSlot(s32 id);
void playSoundEffectFixedSlot(s32 id);
extern s32 continueWorldmapLoop;
extern sWorldmapCameraState worldmapCameraVector;
extern VECTOR worldmapGridInputPosition;
extern MATRIX worldmapMainMatrix2;
extern SVECTOR worldmapEvent12CameraPath1[];                /* camera path (event 12, part 1) */
extern SVECTOR worldmapEvent12CameraPath2[];                /* camera path (event 12, part 2) */
void computeCameraVector(sWorldmapCameraState* cam, VECTOR* pos, s32 dist, SVECTOR* rot);
void computeWormapSpritePosition(VECTOR* v);
void evalWorldmapBezier(s32 t, SVECTOR* p0, SVECTOR* p1, SVECTOR* p2, VECTOR* out);
void setupWorldmapMatrix2(sWorldmapCameraState* cam);
void extractWorldmapEulerAngles(MATRIX* m, SVECTOR* rot);
extern s32 exitWorldMapMode;
void clearWorldmapParticles(s32 type);
void attachWorldmapModels(s32 a0, s32 a1);

/* mode 12 update (same shape as worldmapMode14_update) */
void worldmapMode12_update(void) {
    RECT rect;

    initWorldmapGraphics();
    rect.w = 0x140;
    rect.x = 0;
    rect.y = 0;
    rect.h = 0xD8;
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    while (isCDBusy() > 2) {
        PORT_YIELD();
    }
    finalizeWorldmapEventFileLoading();
    allocAndInitWorldmapState();
    D_8009BE4C = worldMapIdentityMatrix;
    worldmapFadePolyAbr = 2;
    worldmapFadePolyStep = 0x10;
    worldmapExitVar2 = 0;
    worldmapMatrixMode = 0;
    worldmapCloudsMoveCallback = moveWorldmapClouds;
    worldmapDotInit();
    waitReadCompletion(0);
    readWorldmapSequenceFile();
    worldmapPosition.vx = 0xD00000;
    worldmapPosition.vy = 0xFFF60000;
    worldmapPosition.vz = 0x400000;
    worldmapInitAllModels();
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    initWorldmapSkyGradient();
    initWorldmapClouds();
    initWorldmapTexAnims1();
    initWorldmapTexAnims2();
    initWorldmapSkyCloudBand();
    initWorldmapParticles();
    waitReadCompletion(0);
    loadSequence(fieldSeq);
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&worldmapPosition);
    do {
        loadWorldmapChunk();
        VSync(0);
    } while (isWorldStreamingDone() > 0);
    setupWorldmapTask(worldmap_taskFader_init, worldmap_taskFader_update);
    setupWorldmapTask(worldmapMode12_taskDirector_init, worldmapMode12_taskDirector_update);
    setupWorldmapTask(worldmapMode12_taskCamera_init, worldmapMode12_taskCamera_update);
    setupWorldmapTask(worldmapMode12_taskShip4_init, worldmapMode12_taskShip4_update);
    setupWorldmapTask(worldmapMode12_taskShip5_init, worldmapMode12_taskShip5_update);
    setupWorldmapTask(worldmapMode12_taskShip9_init, worldmapMode12_taskShip9_update);
    setupWorldmapTask(worldmapMode12_taskShip10_init, worldmapMode12_taskShip10_update);
    setupWorldmapTask(worldmapMode12_taskShip12_init, worldmapMode12_taskShip12_update);
    setupWorldmapTask(worldmapMode12_taskShip13_init, worldmapMode12_taskShip13_update);
    setupWorldmapTask(worldmapMode12_taskAirship_init, worldmapMode12_taskAirship_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

/* mode 12 free (same shape as worldmapMode14_free) */
void worldmapMode12_free(void) {
    stopAllSounds();
    unloadSequence(fieldSeq);
    freeHeap(fieldSeq);
    freeWorldmapModels();
    freeWorldmapClouds();
    freeWorldmapPolyFT4Array2();
    freeWorldmapTexAnims1();
    freeWorldmapTexAnims2();
    freeWorldmapParticles();
    freeWorldmapPolyFT4Array1();
    freeWorldmapChunks();
    freeHeap(worldmapRenderingStructs[0].m70_OT);
    freeHeap(worldmapRenderingStructs[1].m70_OT);
    freeHeap(worldmapRenderingStructs[0].m74_polyFT3);
    freeHeap(worldmapRenderingStructs[1].m74_polyFT3);
    freeHeap(worldmapFile1Buffer);
    freeWorldmapState();
    gameState.m231A_fieldID = 0x111;
    gameState.m2320_worldmapMode = 2;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

/* task init (worldmapMode12DirectorStates is an array: keeps its load after the store) */
s32 worldmapMode12_taskDirector_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m50 = 0;
    e->m20_subState = worldmapMode12DirectorStates[0];
    e->m22_timer = worldmapMode12DirectorTimers[e->m50];
    e->m50++;
    return 1;
}

/* task update (event sequencer; scratch pointer local) */
s32 worldmapMode12_taskDirector_update(s32 index) {
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        if (--e->m22_timer < 0) {
            e->m20_subState = worldmapMode12DirectorStates[e->m50];
            e->m22_timer = worldmapMode12DirectorTimers[e->m50];
            e->m50++;
        }
        break;
    case 2:
        changeWorldmapEntityState(2, 1);
        e->m20_subState = 1;
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0xD);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0xE);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0xF);
        break;
    case 0x10:
        s->mA0.vy = 0;
        s->mA0.vx = worldmapRadarPosition3.vx >> 12;
        s->mA0.vz = worldmapRadarPosition3.vz >> 12;
        spawnWorldmapParticles(0x14, &s->mA0, NULL);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x10);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x11);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x12);
        e->m20_subState = 1;
        break;
    case 0x11:
        changeWorldmapEntityState(2, 2);
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 1;
        e->m20_subState = 1;
        worldmapFadePolyStep = 0x40;
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x13);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x14);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x15);
        break;
    case 0x12:
        changeWorldmapEntityState(2, 3);
        changeWorldmapEntityState(3, 1);
        changeWorldmapEntityState(4, 1);
        changeWorldmapEntityState(5, 1);
        changeWorldmapEntityState(6, 1);
        changeWorldmapEntityState(7, 1);
        changeWorldmapEntityState(0, 0xC);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 1;
        e->m20_subState = 1;
        break;
    case 0x13:
        changeWorldmapEntityState(2, 4);
        changeWorldmapEntityState(9, 1);
        e->m20_subState = 1;
        break;
    case 0x14:
        changeWorldmapEntityState(2, 6);
        changeWorldmapEntityState(3, 2);
        changeWorldmapEntityState(9, 2);
        e->m20_subState = 1;
        break;
    case 0x16:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 2;
        worldmapFadePolyStep = 4;
        e->m20_subState = 1;
        break;
    case 0x40:
        continueWorldmapLoop = 0;
        exitWorldMapMode = 0;
        e->m20_subState = 1;
        break;
    }
    return 1;
}

/* task init (camera) */
s32 worldmapMode12_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e;

    worldmapGeometryOffsetY = 0x78;
    worldmapCameraVar0 = 0x400000;
    worldmapMatrixMode = 1;
    e = &gWorldmapState->m0_entries[index];
    e->m7C = 0x1000;
    worldmapRotation.vx = -0x40;
    worldmapRotation.vy = 0;
    worldmapRotation.vz = 0;
    worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m58 = 0x40;
    e->m50 = 0;
    return 1;
}

void pcWm60Cut(void);

/* task update (event camera; path tail written in both branches, cross-jumped) */
s32 worldmapMode12_taskCamera_update(s32 index) {
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    SVECTOR* p;

    switch (e->m4_request) {
    case 1:
        e->m20_subState = 1;
        e->m5C = 0x8200;
        e->m4_request = 0;
        e->m60 = 1;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 3;
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 4;
        break;
    case 4:
        e->m20_subState = 5;
        e->m5C = 0xA800;
        e->m4_request = 0;
        e->m60 = 8;
        break;
    case 6:
        pcWm60Cut(); /* Enhanced, 60 Hz: the second path, a cut (w_80070CFC.c) */
        e->m20_subState = 6;
        e->m4_request = 0;
        e->m50 = 0;
        e->m58 = 0x40;
        break;
    }
    if (worldmapMatrixMode == 0) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    switch (e->m20_subState) {
    case 2:
        e->m58 -= e->m60;
        if (e->m58 < 0) {
            e->m58 = 0;
            e->m20_subState = 0;
        }
    case 1:
        e->m50 += e->m58;
        if (e->m5C < e->m50) {
            e->m20_subState = 2;
        }
    case 0:
    block_a:
        s->m0.vy = 0;
        s->m0.vx = 0;
        s->m0.vz = worldmapRadarPosition.vz - worldmapRadarPosition3.vz;
        computeWormapSpritePosition(&s->m0);
        worldmapGridInputPosition.vz += s->m0.vz;
        worldmapRadarPosition3.vz = worldmapRadarPosition.vz;
        p = &worldmapEvent12CameraPath1[e->m50 >> 12];
        if (p[2].pad != -1) {
            evalWorldmapBezier(e->m50 & 0xFFF, &p[0], &p[1], &p[2], &s->m0);
            worldmapCameraVector.m8_target.vz = 0;
            worldmapCameraVector.m8_target.vx = 0;
            worldmapCameraVector.m10_up.vz = 0;
            worldmapCameraVector.m10_up.vx = 0;
            worldmapCameraVector.m10_up.vy = -0x1000;
            worldmapCameraVector.m0_eye.vx = s->m0.vx >> 16;
            worldmapCameraVector.m0_eye.vz = -(s->m0.vz >> 16);
            worldmapCameraVector.m0_eye.vy = s->m0.vy >> 16;
            worldmapCameraVector.m8_target.vy = worldmapRadarPosition3.vy >> 12;
        }
        setupWorldmapMatrix2(&worldmapCameraVector);
        extractWorldmapEulerAngles(&worldmapMainMatrix2, &worldmapRotation);
        break;
    case 3:
        e->m7C = 0xF000;
        e->m20_subState = 0;
        goto block_a;
    case 4:
        s->mA0.vy = 0;
        s->mA0.vx = worldmapRadarPosition3.vx >> 12;
        s->mA0.vz = worldmapRadarPosition3.vz >> 12;
        spawnWorldmapParticles(0x1A, &s->mA0, NULL);
        spawnWorldmapParticles(0x1B, &s->mA0, NULL);
        e->m7C -= 0x120;
        if (e->m7C < 0x1000) {
            e->m7C = 0x1000;
            e->m20_subState = 0;
            clearWorldmapParticles(0x1A);
            clearWorldmapParticles(0x1B);
        }
        goto block_a;
    case 5:
        e->m58 = 0x100;
        e->m20_subState = 1;
        goto block_a;
    case 6:
        s->m0.vy = 0;
        s->m0.vx = 0;
        s->m0.vz = worldmapRadarPosition.vz - worldmapRadarPosition3.vz;
        computeWormapSpritePosition(&s->m0);
        worldmapGridInputPosition.vz += s->m0.vz;
        worldmapRadarPosition3.vx = worldmapRadarPosition.vx;
        worldmapRadarPosition3.vy = worldmapRadarPosition.vy;
        worldmapRadarPosition3.vz = worldmapRadarPosition.vz;
        e->m50 += e->m58;
        if (e->m50 > 0x4800) {
            if (--e->m58 < 0) {
                e->m58 = 0;
            }
        }
        p = &worldmapEvent12CameraPath2[e->m50 >> 12];
        if (p[2].pad != -1) {
            evalWorldmapBezier(e->m50 & 0xFFF, &p[0], &p[1], &p[2], &s->m0);
            worldmapCameraVector.m8_target.vz = 0;
            worldmapCameraVector.m8_target.vx = 0;
            worldmapCameraVector.m10_up.vz = 0;
            worldmapCameraVector.m10_up.vx = 0;
            worldmapCameraVector.m10_up.vy = -0x1000;
            worldmapCameraVector.m0_eye.vx = s->m0.vx >> 16;
            worldmapCameraVector.m0_eye.vz = -(s->m0.vz >> 16);
            worldmapCameraVector.m0_eye.vy = s->m0.vy >> 16;
            worldmapCameraVector.m8_target.vy = worldmapRadarPosition3.vy >> 12;
        }
        setupWorldmapMatrix2(&worldmapCameraVector);
        extractWorldmapEulerAngles(&worldmapMainMatrix2, &worldmapRotation);
        break;
    }
    s->mA0.vy = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    worldmapCameraVector.m0_eye.vy += s->mA0.vy;
    worldmapCameraVector.m8_target.vy += s->mA0.vy;
    return 1;
}
