/*
 * World map mode 14 (event scene; modes table 0x8009A058[14] = {worldmapMode8_init,
 * worldmapMode14_update, worldmapMode14_free}) and its tasks. No Noah equivalent.
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
extern u16 worldmapMode14DirectorStates[];
extern u16 worldmapMode14DirectorTimers[];
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
void evalWorldmapBezier(s32 t, SVECTOR* p0, SVECTOR* p1, SVECTOR* p2, VECTOR* out);
void freeWorldmapChunks(void);
void freeWorldmapState(void);

s32 worldmapMode14_taskDirector_init(s32);
s32 worldmapMode14_taskDirector_update(s32);
s32 worldmapMode14_taskCamera_init(s32);
s32 worldmapMode14_taskCamera_update(s32);
s32 worldmapMode14_taskRingsA_init(s32);
s32 worldmapMode14_taskRingsA_update(s32);
s32 worldmapMode14_taskRingsB_init(s32);
s32 worldmapMode14_taskRingsB_update(s32);
s32 worldmapMode14_taskTrail_init(s32);
s32 worldmapMode14_taskTrail_update(s32);
s32 worldmapMode14_taskFlightPath_init(s32);
s32 worldmapMode14_taskFlightPath_update(s32);

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
extern MATRIX worldmapMainMatrix2;
void computeCameraVector(sWorldmapCameraState* cam, VECTOR* pos, s32 dist, SVECTOR* rot);
void setupWorldmapMatrix2(sWorldmapCameraState* cam);
void extractWorldmapEulerAngles(MATRIX* m, SVECTOR* rot);
extern s32 exitWorldMapMode;
void clearWorldmapParticles(s32 type);
void attachWorldmapModels(s32 a0, s32 a1);
/* Model 0's path (event 14): 9 points, but the last segments read [9] and
 * [10], which on the PS1 are the next table, worldmapMode12DirectorStates. The full extent
 * keeps those bytes on the PC (pc_link aliases worldmapMode12DirectorStates inside it). */
extern SVECTOR worldmapMode14FlightPath[11];

s32 setWorldmapModelSemiTransGrey(sWorldmapModel* model, POLY_FT4* poly, s32 count) {
    s32 i;

    for (i = 0; i < count; i++, poly++) {
        setlen(poly, 9);
        poly->code = 0x2C;
        poly->tpage = GetTPage(1, 3, 0x340, 0x100);
        poly->clut = GetClut(0x100, 0x1FF);
        poly->r0 = 0x80;
        poly->g0 = 0x80;
        poly->code |= 2;
        poly->b0 = 0x80;
    }
    memcpy(model->m48_primBuffers[1], model->m48_primBuffers[0], count * sizeof(POLY_FT4));
    FALLOFF_RETURN(0); /* $v0: memcpy's destination; unused */
}

/* task init (models 14/15 polys) */
s32 worldmapMode10_taskRings_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m20_subState = 0;
    e->m70 = 0;
    e->m6C = 0;
    setWorldmapModelSemiTransGrey(&worldmapModels[14], (POLY_FT4*)worldmapModels[14].m48_primBuffers[0], worldmapModels[14].m40_modelBlock->m4_numPrims);
    setWorldmapModelSemiTransGrey(&worldmapModels[15], (POLY_FT4*)worldmapModels[15].m48_primBuffers[0], worldmapModels[15].m40_modelBlock->m4_numPrims);
    return 3;
}

/* task update (scale models 14/15) */
s32 worldmapMode10_taskRings_update(s32 index) {
    sWorldmapModel* models = worldmapModels;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 ret;

    models[14].m8_position = models[0].m8_position;
    models[15].m8_position = models[14].m8_position;
    e->m6C += 0xC0;
    ret = 1;
    if (e->m6C >= 0x800) {
        e->m70 = (e->m70 + 0x100) & 0x7FFF;
    }
    if (e->m70 >= 0x6000) {
        ret = 3;
        e->m4_request = 0;
        e->m70 = 0;
        e->m6C = 0;
    }
    *SCRATCH_MATRIX0 = worldMapIdentityMatrix;
    *SCRATCH_MATRIX1 = *SCRATCH_MATRIX0;
    SCRATCH_VECTOR0->vx = SCRATCH_VECTOR0->vz = e->m6C;
    SCRATCH_VECTOR1->vx = SCRATCH_VECTOR1->vz = e->m70;
    SCRATCH_VECTOR0->vy = SCRATCH_VECTOR1->vy = 0x1000;
    ScaleMatrix(SCRATCH_MATRIX0, SCRATCH_VECTOR0);
    ScaleMatrix(SCRATCH_MATRIX1, SCRATCH_VECTOR1);
    models[14].m20_rotationMatrix = *SCRATCH_MATRIX0;
    models[15].m20_rotationMatrix = *SCRATCH_MATRIX1;
    return ret;
}

/* task init */
s32 worldmapMode10_taskTrail_init(s32 index) {
    gWorldmapState->m0_entries[index].m22_timer = 0x60;
    return 3;
}

s32 worldmapMode10_taskTrail_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 ret = 1;

    if (e->m4_request == ret) {
        e->m4_request = 0;
        e->m28_position.vx = worldmapModels->m8_position.vx << 12;
        e->m28_position.vy = worldmapModels->m8_position.vy << 12;
        e->m28_position.vz = worldmapModels->m8_position.vz << 12;
    }
    if (--e->m22_timer > 0) {
        e->m28_position.vz += 0x20000;
        SCRATCH_SVECTOR->vx = e->m28_position.vx >> 12;
        SCRATCH_SVECTOR->vy = e->m28_position.vy >> 12;
        SCRATCH_SVECTOR->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(9, SCRATCH_SVECTOR, NULL);
    } else {
        e->m22_timer = 0x60;
        e->m28_position = worldmapPosition;
        clearWorldmapParticles(9);
        ret = 3;
    }
    return ret;
}

/* task init (idle) */
s32 worldmapMode10_taskBurst_init(s32 index) {
    return 3;
}

/* task init (particle 10 at model 0) */
s32 worldmapMode10_taskBurst_update(s32 index) {
    gWorldmapState->m0_entries[index].m4_request = 0;
    SCRATCH_SVECTOR->vx = worldmapModels->m8_position.vx;
    SCRATCH_SVECTOR->vy = worldmapModels->m8_position.vy;
    SCRATCH_SVECTOR->vz = worldmapModels->m8_position.vz;
    spawnWorldmapParticles(10, SCRATCH_SVECTOR, NULL);
    return 3;
}

/* mode 14 update (worldmapDotInit takes no arguments) */
void worldmapMode14_update(void) {
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
    worldmapPosition.vx = 0x05BED000;
    worldmapPosition.vy = 0xFFF60000;
    worldmapPosition.vz = 0x062A8000;
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
    setupWorldmapTask(worldmapMode14_taskDirector_init, worldmapMode14_taskDirector_update);
    setupWorldmapTask(worldmapMode14_taskCamera_init, worldmapMode14_taskCamera_update);
    setupWorldmapTask(worldmapMode14_taskRingsA_init, worldmapMode14_taskRingsA_update);
    setupWorldmapTask(worldmapMode14_taskRingsB_init, worldmapMode14_taskRingsB_update);
    setupWorldmapTask(worldmapMode14_taskTrail_init, worldmapMode14_taskTrail_update);
    setupWorldmapTask(worldmapMode14_taskFlightPath_init, worldmapMode14_taskFlightPath_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

/* mode 14 free */
void worldmapMode14_free(void) {
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
    gameState.m231A_fieldID = 0x11A;
    gameState.m2320_worldmapMode = 0;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

/* task init */
s32 worldmapMode14_taskDirector_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m50 = 0;
    e->m20_subState = worldmapMode14DirectorStates[0];
    e->m22_timer = worldmapMode14DirectorTimers[e->m50];
    return 1;
}

/* task update (event sequencer: m20 = step, m22 = timer) */
s32 worldmapMode14_taskDirector_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        if (--e->m22_timer < 0) {
            e->m20_subState = worldmapMode14DirectorStates[e->m50];
            e->m22_timer = worldmapMode14DirectorTimers[e->m50];
            e->m50++;
        }
        break;
    case 2:
        spawnWorldmapParticles(0x11, NULL, NULL);
        e->m20_subState = 1;
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 1);
        break;
    case 3:
        changeWorldmapEntityState(2, 2);
        e->m20_subState = 1;
        spawnWorldmapParticles(0xF, NULL, NULL);
        spawnWorldmapParticles(0x10, NULL, NULL);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 4);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 5);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 6);
        break;
    case 4:
        changeWorldmapEntityState(2, 3);
        changeWorldmapEntityState(3, 1);
        e->m20_subState = 1;
        break;
    case 5:
        changeWorldmapEntityState(2, 6);
        changeWorldmapEntityState(4, 1);
        changeWorldmapEntityState(5, 1);
        e->m20_subState = 1;
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 7);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 8);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 9);
        break;
    case 6:
        changeWorldmapEntityState(2, 4);
        e->m20_subState = 1;
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0xA);
        playSoundEffectFixedSlot((fieldSeq->m14_id << 16) | 0xB);
        playSoundEffectFixedSlot((fieldSeq->m14_id << 16) | 0xC);
        break;
    case 7:
        changeWorldmapEntityState(2, 5);
        e->m20_subState = 1;
        break;
    case 8:
        changeWorldmapEntityState(2, 1);
        e->m20_subState = 1;
        break;
    case 9:
        changeWorldmapEntityState(6, 1);
        changeWorldmapEntityState(2, 7);
        e->m20_subState = 1;
        break;
    case 10:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyStep = 4;
        e->m20_subState = 1;
        break;
    case 11:
        continueWorldmapLoop = 0;
        exitWorldMapMode = 0;
        e->m20_subState = 1;
        break;
    }
    return 1;
}

/* task init (camera) */
s32 worldmapMode14_taskCamera_init(s32 index) {
    worldmapGeometryOffsetY = 0x78;
    worldmapCameraVar0 = 0x1C0000;
    worldmapRotation.vx = 0x40;
    worldmapRotation.vy = 0x480;
    worldmapRotation.vz = 0;
    worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    gWorldmapState->m0_entries[index].m50 = 0x1000;
    return 1;
}

/* task update (event camera) */
void pcWm60Cut(void);

s32 worldmapMode14_taskCamera_update(s32 index) {
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 t;

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 1;
        break;
    case 2:
        e->m20_subState = 2;
        e->m4_request = 0;
        e->m50 = 0x40000;
        break;
    case 3:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        worldmapCameraVar0 = 0x640000;
        e->m4_request = 0;
        e->m20_subState = 0;
        worldmapRotation.vx = -0x1E0;
        worldmapRotation.vy = 0x480;
        worldmapRotation.vz = 0;
        break;
    case 4:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m4_request = 0;
        e->m20_subState = 0;
        worldmapCameraVar0 = 0x1E0000;
        worldmapRotation.vx = 0x40;
        worldmapRotation.vy = 0x418;
        worldmapRotation.vz = 0;
        worldmapMatrixMode = 0;
        worldmapRadarPosition3.vz = worldmapPosition.vz;
        break;
    case 5:
        e->m4_request = 0;
        e->m20_subState = 3;
        break;
    case 6:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m4_request = 0;
        e->m20_subState = 4;
        e->m58 = worldmapPosition.vx + 0xC7C00;
        worldmapMatrixMode = 1;
        e->m5C = worldmapPosition.vz + 0x3EC400;
        break;
    case 7:
        e->m4_request = 0;
        e->m20_subState = 5;
        break;
    }
    switch (e->m20_subState) {
    case 0:
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 1:
        e->m50 += 0x200;
        if (e->m50 > 0x8000) {
            e->m50 = 0x8000;
            e->m20_subState = 0;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 2:
        e->m50 -= 0x200;
        if (e->m50 < 0x8000) {
            e->m50 = 0x8000;
            e->m20_subState = 0;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 3:
        e->m50 -= 0x100;
        if (e->m50 < 0x1000) {
            e->m50 = 0x1000;
            e->m20_subState = 0;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 4:
        worldmapRadarPosition3.vz += 0x3A000;
        if (worldmapRadarPosition3.vz > 0x77FFFFF) {
            e->m20_subState = 0;
        }
        worldmapCameraVector.m0_eye.vx = (e->m58 - worldmapRadarPosition3.vx) >> 12;
        worldmapCameraVector.m0_eye.vz = (worldmapRadarPosition3.vz - e->m5C) >> 12;
        worldmapCameraVector.m8_target.vz = 0;
        worldmapCameraVector.m8_target.vx = 0;
        t = worldmapRadarPosition3.vy >> 12;
        worldmapCameraVector.m8_target.vy = t;
        worldmapCameraVector.m0_eye.vy = t - 0x40;
        setupWorldmapMatrix2(&worldmapCameraVector);
        extractWorldmapEulerAngles(&worldmapMainMatrix2, &worldmapRotation);
        break;
    case 5:
        worldmapRotation.vy += 4;
        if (worldmapRotation.vy > 0x600) {
            e->m20_subState = 0;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    }
    s->mA0.vx = rand() % (e->m50 >> 12) - (e->m50 >> 13);
    s->mA0.vy = rand() % (e->m50 >> 12) - (e->m50 >> 13);
    worldmapCameraVector.m0_eye.vx += s->mA0.vx;
    worldmapCameraVector.m8_target.vx += s->mA0.vx;
    worldmapCameraVector.m0_eye.vy += s->mA0.vy;
    worldmapCameraVector.m8_target.vy += s->mA0.vy;
    return 1;
}

/* task init (models 4/5) */
s32 worldmapMode14_taskRingsA_init(s32 index) {
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    s32 i;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[4];

    for (i = 0; i < 2; i++) {
        setWorldmapModelSemiTransGrey(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims);
        m++;
    }
    e->m54 = 0;
    e->m50 = 0;
    s->mF0_rotation = worldMapIdentityMatrix;
    s->m0.vx = s->m0.vz = (s32)e->m50;
    s->m0.vy = 0x1000;
    ScaleMatrix(&s->mF0_rotation, &s->m0);
    m[0].m20_rotationMatrix = s->mF0_rotation;
    m[1].m20_rotationMatrix = s->mF0_rotation;
    m[1].m0_hidden = 1;
    m[0].m0_hidden = 1;
    return 3;
}

/* task update (models 4/5) */
s32 worldmapMode14_taskRingsA_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request != 0) {
        e->m4_request = 0;
        models[4].m0_hidden = models[5].m0_hidden = 0;
    }
    models[4].m8_position.vx = models[5].m8_position.vx = worldmapPosition.vx >> 12;
    models[4].m8_position.vy = models[5].m8_position.vy = -0x40;
    models[4].m8_position.vz = models[5].m8_position.vz = worldmapPosition.vz >> 12;
    e->m50 += 0x180;
    if (e->m50 > 0x800) {
        e->m54 += 0x180;
    }
    if (e->m50 > 0x7F00) {
        e->m50 = 0x7F00;
    }
    if (e->m54 > 0x7F00) {
        e->m54 = 0x7F00;
    }
    *SCRATCH_MATRIX0 = worldMapIdentityMatrix;
    *SCRATCH_MATRIX1 = *SCRATCH_MATRIX0;
    SCRATCH_VECTOR0->vx = SCRATCH_VECTOR0->vz = (s32)e->m50;
    SCRATCH_VECTOR1->vx = SCRATCH_VECTOR1->vz = (s32)e->m54;
    SCRATCH_VECTOR0->vy = SCRATCH_VECTOR1->vy = 0x1000;
    ScaleMatrix(SCRATCH_MATRIX0, SCRATCH_VECTOR0);
    ScaleMatrix(SCRATCH_MATRIX1, SCRATCH_VECTOR1);
    models[4].m20_rotationMatrix = *SCRATCH_MATRIX0;
    models[5].m20_rotationMatrix = *SCRATCH_MATRIX1;
    return 1;
}

/* task init (models 6/7) */
s32 worldmapMode14_taskRingsB_init(s32 index) {
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    s32 i;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[6];

    for (i = 0; i < 2; i++) {
        setWorldmapModelSemiTransGrey(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims);
        m++;
    }
    e->m54 = 0;
    e->m50 = 0;
    s->mF0_rotation = worldMapIdentityMatrix;
    s->m0.vx = s->m0.vz = (s32)e->m50;
    s->m0.vy = 0x1000;
    ScaleMatrix(&s->mF0_rotation, &s->m0);
    m[0].m20_rotationMatrix = s->mF0_rotation;
    m[1].m20_rotationMatrix = s->mF0_rotation;
    m[1].m0_hidden = 1;
    m[0].m0_hidden = 1;
    return 3;
}

/* task update (models 6/7) */
s32 worldmapMode14_taskRingsB_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request != 0) {
        e->m4_request = 0;
        models[6].m0_hidden = models[7].m0_hidden = 0;
    }
    models[6].m8_position.vx = models[7].m8_position.vx = worldmapPosition.vx >> 12;
    models[6].m8_position.vy = models[7].m8_position.vy = -0x40;
    models[6].m8_position.vz = models[7].m8_position.vz = worldmapPosition.vz >> 12;
    e->m50 += 0x180;
    if (e->m50 > 0x800) {
        e->m54 += 0x180;
    }
    if (e->m50 > 0x7F00) {
        e->m50 = 0x7F00;
    }
    if (e->m54 > 0x7F00) {
        e->m54 = 0x7F00;
    }
    *SCRATCH_MATRIX0 = worldMapIdentityMatrix;
    *SCRATCH_MATRIX1 = *SCRATCH_MATRIX0;
    SCRATCH_VECTOR0->vx = SCRATCH_VECTOR0->vz = (s32)e->m50;
    SCRATCH_VECTOR1->vx = SCRATCH_VECTOR1->vz = (s32)e->m54;
    SCRATCH_VECTOR0->vy = SCRATCH_VECTOR1->vy = 0x1000;
    ScaleMatrix(SCRATCH_MATRIX0, SCRATCH_VECTOR0);
    ScaleMatrix(SCRATCH_MATRIX1, SCRATCH_VECTOR1);
    models[6].m20_rotationMatrix = *SCRATCH_MATRIX0;
    models[7].m20_rotationMatrix = *SCRATCH_MATRIX1;
    return 1;
}

/* task init (idle) */
s32 worldmapMode14_taskTrail_init(s32 index) {
    return 3;
}

s32 worldmapMode14_taskTrail_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 ret = 1;

    if (e->m4_request == ret) {
        e->m4_request = 0;
        e->m22_timer = 0x3C;
        e->m28_position.vx = worldmapPosition.vx;
        e->m28_position.vy = worldmapPosition.vy;
        e->m28_position.vz = worldmapPosition.vz;
    }
    if (--e->m22_timer > 0) {
        e->m28_position.vx += -0x95D0;
        e->m28_position.vz += 0x2F130;
        SCRATCH_SVECTOR->vx = e->m28_position.vx >> 12;
        SCRATCH_SVECTOR->vy = e->m28_position.vy >> 12;
        SCRATCH_SVECTOR->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(9, SCRATCH_SVECTOR, worldmapMode14TrailParticleRotation);
    } else {
        e->m22_timer = 0x3C;
        e->m28_position = worldmapPosition;
        clearWorldmapParticles(9);
        ret = 3;
    }
    return ret;
}

/* task init */
s32 worldmapMode14_taskFlightPath_init(s32 index) {
    attachWorldmapModels(0, 1);
    attachWorldmapModels(0, 2);
    attachWorldmapModels(0, 3);
    gWorldmapState->m0_entries[index].m50 = 0;
    gWorldmapState->m0_entries[index].m54 = 0x100;
    gWorldmapState->m0_entries[index].m58 = 0x100;
    worldmapModels->m18_rotation.vx = 0;
    worldmapModels->m18_rotation.vy = 0;
    worldmapModels->m18_rotation.vz = 0;
    RotMatrixYXZ(&worldmapModels->m18_rotation, &worldmapModels->m20_rotationMatrix);
    return 3;
}

/* task update (model 0 follows a path, orientation from the path tangent) */
s32 worldmapMode14_taskFlightPath_update(s32 index) {
    s32 ret = 1;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapModel* model;
    SVECTOR* p;
    s32 t;

    switch (e->m50 >> 12) {
    case 0:
    case 1:
        e->m50 += e->m58;
        e->m54 += 4;
        break;
    case 2:
    case 3:
    case 4:
        e->m50 += e->m58;
        e->m58 -= 8;
        if (e->m58 < 0x80) {
            e->m58 = 0x80;
        }
        e->m54 += 4;
        break;
    case 5:
    case 6:
    case 7:
        e->m50 += e->m58;
        e->m58 += 8;
        if (e->m58 > 0x100) {
            e->m58 = 0x100;
        }
        e->m54 -= 0x10;
        break;
    case 8:
        ret = 3;
        break;
    }
    p = &worldmapMode14FlightPath[e->m50 >> 12];
    if (p[2].pad != -1) {
        evalWorldmapBezier(e->m50 & 0xFFF, &p[0], &p[1], &p[2], &s->m0);
    }
    t = (s32)e->m50 + 0x80;
    p = &worldmapMode14FlightPath[t >> 12];
    evalWorldmapBezier(t & 0xFFF, &p[0], &p[1], &p[2], &s->m10);
    model = worldmapModels;
    model->m8_position.vx = s->m0.vx >> 16;
    model->m8_position.vy = s->m0.vy >> 16;
    model->m8_position.vz = s->m0.vz >> 16;
    s->m10.vx = (s->m10.vx - s->m0.vx) >> 12;
    s->m10.vy = (s->m10.vy - s->m0.vy) >> 12;
    s->m10.vz = -((s->m10.vz - s->m0.vz) >> 12);
    VectorNormal(&s->m10, &s->m0);
    s->mA0.vx = 0;
    s->mA0.vy = ratan2(s->m0.vx, s->m0.vz) & 0xFFF;
    s->mA0.vz = e->m54;
    RotMatrixYXZ(&s->mA0, &s->mF0_rotation);
    s->mA0.vx = 0;
    s->mA0.vy = -0x1000;
    s->mA0.vz = 0;
    ApplyMatrix(&s->mF0_rotation, &s->mA0, &s->m10);
    OuterProduct12(&s->m0, &s->m10, &s->m30);
    VectorNormal(&s->m30, &s->m20);
    OuterProduct12(&s->m0, &s->m20, &s->m30);
    VectorNormal(&s->m30, &s->m10);
    s->mF0_rotation.m[0][0] = s->m20.vx;
    s->mF0_rotation.m[0][1] = s->m20.vy;
    s->mF0_rotation.m[0][2] = s->m20.vz;
    s->mF0_rotation.m[1][0] = s->m10.vx;
    s->mF0_rotation.m[1][1] = s->m10.vy;
    s->mF0_rotation.m[1][2] = s->m10.vz;
    s->mF0_rotation.m[2][0] = s->m0.vx;
    s->mF0_rotation.m[2][1] = s->m0.vy;
    s->mF0_rotation.m[2][2] = s->m0.vz;
    TransposeMatrix(&s->mF0_rotation, &model->m20_rotationMatrix);
    extractWorldmapEulerAngles(&s->mF0_rotation, &s->mA8);
    if (model->m8_position.vy >= -0x7F) {
        s->mA0.vx = model->m8_position.vx;
        s->mA0.vy = model->m8_position.vy;
        s->mA0.vz = model->m8_position.vz;
        s->mA8.vz = -s->mA8.vz;
        spawnWorldmapParticles(0x12, &s->mA0, &s->mA8);
    }
    return ret;
}
