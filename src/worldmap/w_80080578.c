/*
 * Mode 13 (to 0x80080AC4), mode 16 (0x80080D00-0x80081FD8) and mode 17
 * (from 0x80082324).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/sound.h"
#include "worldmap/worldmap.h"
#include "main/game_state.h"

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern VECTOR worldmapPosition;
extern VECTOR worldmapRadarPosition3;
extern VECTOR worldmapRadarPosition;
extern s32 worldmapMatrixMode;
extern s32 worldmapGeometryOffsetY;
extern s32 worldmapCameraVar0;
extern SVECTOR worldmapRotation;
extern sWorldmapCameraState worldmapCameraVector;
extern s32 worldmapMode17Script[];

s32 rand(void);
void computeCameraVector(sWorldmapCameraState*, VECTOR*, s32, SVECTOR*);
void setWorldmapMode17ModelSemiTransFT3(sWorldmapModel* m, POLY_FT3* p, u32 n, s32 abr);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
s32 setWorldmapModelSemiTransGrey(sWorldmapModel* model, POLY_FT4* poly, s32 count);
void setWorldmapPolysColor(POLY_FT4* p, s32 n, s32 r, s32 g, s32 b);
extern MATRIX worldMapIdentityMatrix;
extern s32 worldmapOddOrEven;
extern MATRIX D_8009BE4C;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern void* worldmapWds;
extern void* worldmapSound1;
extern s32 worldmapFile5;
extern sWorldmapSeqHeader* fieldSeq;
extern u16 musicStatusFlag;
extern u8 battleMusic[];
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern s32 worldmapExitToFieldVar0;
extern u16 worldmapMode16DirectorStates[];
extern u16 worldmapMode16DirectorTimers[];
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;

s32 changeWorldmapEntityState(s32 index, s32 state);
void playSoundEffectInFreeSlot(s32);
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
void initWorldmapGraphics(void);
void worldmapFadeIn(s32, s32, s32, s32);
void finalizeWorldmapEventFileLoading(void);
void allocAndInitWorldmapState(void);
void moveWorldmapClouds(void);
void worldmapDotInit(void);
void worldmapInitAllModels(void);
void loadWorldmapTextureFromFile3(void);
void loadWorldmapTextureFromFile2(void);
void startLoadingWorldmapSoundFiles(void);
void initWorldmapSkyGradient(void);
void initWorldmapClouds(void);
void initWorldmapTexAnims1(void);
void initWorldmapTexAnims2(void);
void initWorldmapSkyCloudBand(void);
void initWorldmapParticles(void);
void setupInitialGrid(VECTOR*);
s32 loadWorldmapChunk(void);
s32 isWorldStreamingDone(void);
s32 worldmap_taskFader_init(s32);
s32 worldmap_taskFader_update(s32);
s32 worldmap_taskSceneNoBillboards_init(s32);
s32 worldmap_taskSceneNoBillboards_update(s32);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void initWorldmapEncounterTimers(void);
s32 worldmapMode16_taskDirector_init(s32);
s32 worldmapMode16_taskDirector_update(s32);
s32 worldmapMode16_taskCamera_init(s32);
s32 worldmapMode16_taskCamera_update(s32);
s32 worldmapMode16_taskFadeInModel_init(s32);
s32 worldmapMode16_taskFadeInModel_update(s32);
s32 worldmapMode16_taskLightPillars_init(s32);
s32 worldmapMode16_taskLightPillars_update(s32);
s32 worldmapMode16_taskScreenWarp_init(s32);
s32 worldmapMode16_taskScreenWarp_update(s32);
s32 worldmapMode16_taskWarpControl_init(s32);
s32 worldmapMode16_taskWarpControl_update(s32);
void resetBattleAudioState(void);
void loadSequence(void*);
void* createMusicInstance(void* seq);
void startMusicInstance(void* inst, s32 volume, s32 arg2);
void stopAllSounds(void);
void unloadSequence(void*);
void freeWorldmapModels(void);
void freeWorldmapClouds(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapPolyFT4Array1(void);
void freeWorldmapChunks(void);
void freeWorldmapState(void);
void setWorldmapMode16ModelSemiTrans(sWorldmapModel* m, POLY_FT4* p, s32 n, s32 abr);
void initWorldmapBillboards(void);
void createWorlmapPolyFT4Array3(void);
void freeWorldmapPolyFT4Array3(void);
s32 worldmapMode17_taskScript_init(s32);
s32 worldmap_taskScript_update(s32);
s32 worldmapMode17_taskCamera_init(s32);
s32 worldmapMode17_taskCamera_update(s32);
s32 worldmapMode17_taskGlow_init(s32);
s32 worldmapMode17_taskGlow_update(s32);
s32 worldmapTaskDummy(s32);
s32 worldmapMode17_taskRestream_update(s32);
s32 worldmap_taskScene_init(s32);
s32 worldmap_taskScene_update(s32);
s32 approachWorldmapValue(s32, s32, s32);
void checkWorldmapPositionSub0(VECTOR*);
void smoothWorldmapCameraRotation(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
void smoothWorldmapCameraDistance(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
void smoothWorldmapCameraPosition(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
s32 getAngleCos(s32);
void createRotationMatrix(SVECTOR*, MATRIX*);
extern s16 worldmapMode17GlowParams1[5 * 14];
extern s16 worldmapMode17GlowParams2[5 * 14];
extern s16 worldmapMode17GlowParams3[5 * 14];
void setWorldmapPolysColorFT3(POLY_FT3* p, u32 n, s32 r, s32 g, s32 b);

void updateWorldmapMode17Glow(sWorldmapStateEntry* e, sWorldmapModel* m, sWorldmapScratch* sp);

typedef struct {
    POLY_FT4 m0[0xC0];
} sWorldmapPolyBuf;
extern POLY_FT4* worldmapMode16WarpPolys[2];
extern u16* worldmapMode16WarpAmplitudes;
extern DR_MOVE worldmapMode16WarpDrawMoves[2];
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;

s32 worldmapMode13_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapMatrixMode = 0;
    e->m7C = 0x1000;
    worldmapGeometryOffsetY = 0x78;
    worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m4_request = 1;
    e->m20_subState = 0;
    return 1;
}

s32 worldmapMode13_taskCamera_update(s32 index) {
    u8* scratch = (u8*)PSX_SCRATCH(0x1F800000);
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 r;
    s32 d;

    switch (e->m4_request) {
    case 1:
        e->m20_subState = 1;
        e->m4_request = 0;
        worldmapRotation.vx = 0x10;
        worldmapRotation.vy = 0x8E0;
        worldmapRotation.vz = 0;
        e->m50 = 0x800000;
        e->m54 = e->m58 = worldmapRotation.vx << 12;
        worldmapCameraVar0 = 0x800000;
        e->m5C = e->m60 = worldmapRotation.vy << 12;
        break;
    case 2:
        e->m20_subState = 2;
        e->m4_request = 0;
        e->m7C = 0x8000;
        break;
    case 3:
        e->m20_subState = 3;
        e->m4_request = 0;
        e->m7C = 0x80000;
        break;
    }
    if (worldmapMatrixMode == 0) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m50 += -0x10000;
        if (e->m50 < 0x300000) {
            e->m50 = 0x300000;
        }
        d = (s32)(e->m50 - worldmapCameraVar0);
        if (d != 0) {
            worldmapCameraVar0 += d >> 5;
        }
        e->m54 -= 0x4000;
        if (e->m54 < -0x100000) {
            e->m54 = -0x100000;
        }
        d = (s32)(e->m54 - e->m58);
        if (d != 0) {
            e->m58 += d >> 4;
            worldmapRotation.vx = e->m58 >> 12;
        }
        e->m5C += -0x10000;
        if (e->m5C < 0x2E0000) {
            e->m5C = 0x2E0000;
        }
        d = e->m5C - e->m60;
        if (d != 0) {
            e->m60 += d >> 5;
            worldmapRotation.vy = e->m60 >> 12;
        }
        break;
    case 2:
        break;
    case 3:
        e->m7C -= 0x4000;
        if (e->m7C < 0x1000) {
            e->m7C = 0x1000;
            e->m20_subState = 0;
        }
        break;
    }
    r = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    ((SVECTOR*)(scratch + 0xA0))->vy = r;
    worldmapCameraVector.m0_eye.vy += r;
    worldmapCameraVector.m8_target.vy += ((SVECTOR*)(scratch + 0xA0))->vy;
    return 1;
}

s32 worldmapMode13_taskExplosion_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m28_position.vx = worldmapPosition.vx;
    e->m28_position.vy = worldmapPosition.vy;
    e->m28_position.vz = worldmapPosition.vz;
    return 1;
}

s32 worldmapMode13_taskExplosion_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    if (e->m4_request == 1) {
        e->m4_request = 0;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0x28, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
        spawnWorldmapParticles(0x29, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
        spawnWorldmapParticles(0x2A, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    }
    return 1;
}

void setWorldmapPolysColor(POLY_FT4* p, s32 n, s32 r, s32 g, s32 b) {
    s32 i;

    for (i = 0; i < n; i++, p++) {
        p->r0 = r;
        p->g0 = g;
        p->b0 = b;
    }
}

s32 worldmapMode13_taskRings_init(s32 index) {
    sWorldmapModel* m = worldmapModels;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m28_position.vx = worldmapPosition.vx;
    e->m28_position.vy = -0x80000;
    e->m28_position.vz = worldmapPosition.vz;
    e->m50 = 0;
    e->m54 = -0x800;
    e->m58 = 0x80;
    setWorldmapModelSemiTransGrey(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims);
    m++;
    setWorldmapModelSemiTransGrey(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims);
    return 3;
}


s32 worldmapMode13_taskRings_update(s32 index) {
    sWorldmapModel* models = worldmapModels;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    if (e->m4_request == 1) {
        e->m4_request = 0;
    }
    models[0].m8_position.vx = models[1].m8_position.vx = e->m28_position.vx >> 12;
    models[0].m8_position.vy = models[1].m8_position.vy = e->m28_position.vy >> 12;
    models[0].m8_position.vz = models[1].m8_position.vz = e->m28_position.vz >> 12;
    models[1].m20_rotationMatrix = worldMapIdentityMatrix;
    models[0].m20_rotationMatrix = models[1].m20_rotationMatrix;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vx = ((VECTOR*)PSX_SCRATCH(0x1F800000))->vz = (s32)e->m50;
    ((VECTOR*)PSX_SCRATCH(0x1F800010))->vx = ((VECTOR*)PSX_SCRATCH(0x1F800010))->vz = (s32)e->m54;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vy = ((VECTOR*)PSX_SCRATCH(0x1F800010))->vy = 0x1000;
    ScaleMatrix(&models[0].m20_rotationMatrix, (VECTOR*)PSX_SCRATCH(0x1F800000));
    ScaleMatrix(&models[1].m20_rotationMatrix, (VECTOR*)PSX_SCRATCH(0x1F800010));
    e->m50 += 0x180;
    if (e->m50 > 0x7FFF) {
        e->m50 = 0x7FFF;
    }
    e->m54 += 0x180;
    if (e->m54 > 0x7FFF) {
        e->m54 = 0x7FFF;
    }
    setWorldmapPolysColor((POLY_FT4*)models->m48_primBuffers[worldmapOddOrEven], models->m40_modelBlock->m4_numPrims, (s32)e->m58, (s32)e->m58, (s32)e->m58);
    models++;
    setWorldmapPolysColor((POLY_FT4*)models->m48_primBuffers[worldmapOddOrEven], models->m40_modelBlock->m4_numPrims, (s32)e->m58, (s32)e->m58, (s32)e->m58);
    e->m58 -= 4;
    if (e->m58 < 0) {
        e->m58 = 0;
        return 3;
    }
    return 1;
}

void worldmapMode16_update(void) {
    RECT r;
    void* src;

    initWorldmapGraphics();
    r.x = 0;
    r.y = 0;
    r.w = 0x140;
    r.h = 0xD8;
    MoveImage(&r, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    while (isCDBusy() >= 3) {
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
    resetBattleAudioState();
    worldmapPosition.vx = 0x4000000;
    worldmapPosition.vy = -0xC0000;
    worldmapPosition.vz = 0x4000000;
    worldmapInitAllModels();
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    startLoadingWorldmapSoundFiles();
    initWorldmapSkyGradient();
    initWorldmapClouds();
    initWorldmapTexAnims1();
    initWorldmapTexAnims2();
    initWorldmapSkyCloudBand();
    initWorldmapParticles();
    waitReadCompletion(0);
    worldmapWdsResult = loadWds(worldmapWds, 0);
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&worldmapPosition);
    do {
        loadWorldmapChunk();
        VSync(0);
    } while (isWorldStreamingDone() > 0);
    while (musicStatusFlag & 0x10) {
        PORT_YIELD(); /* until the SPU transfer interrupt clears it (the wds upload) */
    }
    freeHeap(worldmapWds);
    loadSequence(fieldSeq);
    src = worldmapSound1;
    memcpy(battleMusic, src, getFileSizeAligned(worldmapFile5));
    pMusic = createMusicInstance(battleMusic);
    startMusicInstance(pMusic, 0x7F, 0);
    setupWorldmapTask(worldmap_taskFader_init, worldmap_taskFader_update);
    setupWorldmapTask(worldmapMode16_taskDirector_init, worldmapMode16_taskDirector_update);
    setupWorldmapTask(worldmapMode16_taskCamera_init, worldmapMode16_taskCamera_update);
    setupWorldmapTask(worldmapMode16_taskFadeInModel_init, worldmapMode16_taskFadeInModel_update);
    setupWorldmapTask(worldmapMode16_taskLightPillars_init, worldmapMode16_taskLightPillars_update);
    setupWorldmapTask(worldmapMode16_taskScreenWarp_init, worldmapMode16_taskScreenWarp_update);
    setupWorldmapTask(worldmapMode16_taskWarpControl_init, worldmapMode16_taskWarpControl_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

void worldmapMode16_free(void) {
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
    gameState.m231A_fieldID = 0x1FA;
    gameState.m2320_worldmapMode = 0;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
    worldmapExitToFieldVar0 = 1;
}

s32 worldmapMode16_taskDirector_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m50 = 0;
    e->m20_subState = worldmapMode16DirectorStates[0];
    e->m22_timer = worldmapMode16DirectorTimers[e->m50];
    e->m50++;
    return 1;
}

s32 worldmapMode16_taskDirector_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m22_timer--;
        if (e->m22_timer < 0) {
            e->m20_subState = worldmapMode16DirectorStates[e->m50];
            e->m22_timer = worldmapMode16DirectorTimers[e->m50];
            e->m50++;
        }
        break;
    case 2:
        changeWorldmapEntityState(6, 1);
        e->m20_subState = 1;
        break;
    case 3:
        changeWorldmapEntityState(6, 0);
        e->m20_subState = 1;
        break;
    case 4:
        changeWorldmapEntityState(6, 2);
        e->m20_subState = 1;
        break;
    case 5:
        changeWorldmapEntityState(6, 3);
        e->m20_subState = 1;
        break;
    case 6:
        changeWorldmapEntityState(6, 4);
        e->m20_subState = 1;
        break;
    case 7:
        changeWorldmapEntityState(6, 5);
        e->m20_subState = 1;
        break;
    case 8:
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x2E);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x2F);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x30);
        e->m20_subState = 1;
        break;
    case 16:
        changeWorldmapEntityState(3, 1);
        changeWorldmapEntityState(4, 1);
        e->m20_subState = 1;
        break;
    case 17:
        changeWorldmapEntityState(3, 2);
        e->m20_subState = 1;
        break;
    case 63:
        playBattleMusic(pMusic, 0, 0xF0);
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 2;
        worldmapFadePolyStep = 4;
        e->m20_subState = 1;
        break;
    case 64:
        continueWorldmapLoop = 0;
        exitWorldMapMode = 0;
        e->m20_subState = 0;
        break;
    }
    return 1;
}

s32 worldmapMode16_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapMatrixMode = 0;
    e->m7C = 0x1000;
    worldmapGeometryOffsetY = 0x78;
    worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m4_request = 1;
    e->m20_subState = 0;
    return 1;
}

s32 worldmapMode16_taskCamera_update(s32 index) {
    u8* scratch = (u8*)PSX_SCRATCH(0x1F800000);
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 r;
    s32 d;

    switch (e->m4_request) {
    case 1:
        e->m20_subState = 1;
        e->m4_request = 0;
        worldmapRotation.vx = -0x80;
        worldmapRotation.vy = -0x200;
        worldmapRotation.vz = 0;
        e->m50 = 0x980000;
        e->m54 = e->m58 = worldmapRotation.vx << 12;
        worldmapCameraVar0 = 0x980000;
        e->m5C = e->m60 = worldmapRotation.vy << 12;
        break;
    case 2:
        break;
    case 3:
        break;
    }
    if (worldmapMatrixMode == 0) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    if (e->m20_subState == 1) {
        e->m50 += -0x10000;
        if (e->m50 < 0x630000) {
            e->m50 = 0x630000;
        }
        d = (s32)(e->m50 - worldmapCameraVar0);
        if (d != 0) {
            worldmapCameraVar0 += d >> 5;
        }
        e->m54 += 0x1000;
        if (e->m54 > 0x10000) {
            e->m54 = 0x10000;
        }
        d = (s32)(e->m54 - e->m58);
        if (d != 0) {
            e->m58 += d >> 4;
            worldmapRotation.vx = e->m58 >> 12;
        }
        e->m5C += 0x10000;
        if (e->m5C > 0x4B0000) {
            e->m5C = 0x4B0000;
        }
        d = e->m5C - e->m60;
        if (d != 0) {
            e->m60 += d >> 5;
            worldmapRotation.vy = e->m60 >> 12;
        }
    }
    r = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    ((SVECTOR*)(scratch + 0xA0))->vy = r;
    worldmapCameraVector.m0_eye.vy += r;
    worldmapCameraVector.m8_target.vy += ((SVECTOR*)(scratch + 0xA0))->vy;
    return 1;
}

void setWorldmapMode16ModelSemiTrans(sWorldmapModel* m, POLY_FT4* p, s32 n, s32 abr) {
    s32 i;

    for (i = 0; i < n; i++, p++) {
        setlen(p, 9);
        setcode(p, 0x2C);
        p->tpage = GetTPage(0, abr, 0x180, 0);
        p->code |= 2;
    }
    memcpy(m->m48_primBuffers[1], m->m48_primBuffers[0], n * sizeof(POLY_FT4));
}

s32 worldmapMode16_taskFadeInModel_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models;

    e->m20_subState = 0;
    e->m28_position.vx = worldmapPosition.vx;
    e->m28_position.vy = worldmapPosition.vy - 0x100000;
    e->m28_position.vz = worldmapPosition.vz;
    e->m50 = 0;
    e->m54 = 0;
    e->m58 = 0;
    models = worldmapModels;
    setWorldmapMode16ModelSemiTrans(&models[2], (POLY_FT4*)models[2].m48_primBuffers[0], models[2].m40_modelBlock->m4_numPrims, 1);
    models[2].m8_position.vx = e->m28_position.vx >> 12;
    models[2].m8_position.vy = e->m28_position.vy >> 12;
    models[2].m8_position.vz = e->m28_position.vz >> 12;
    return 1;
}

s32 worldmapMode16_taskFadeInModel_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;
    sWorldmapModel* m = &models[2];
    POLY_FT4* p;
    s32 i;

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 1;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 0;
        p = (POLY_FT4*)m->m48_primBuffers[worldmapOddOrEven];
        for (i = 0; i < m->m40_modelBlock->m4_numPrims; i++, p++) {
            p->r0 = 0x80;
            p->g0 = 0x80;
            p->code &= ~2;
            p->b0 = 0x80;
        }
        break;
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m50++;
        e->m54++;
        e->m58++;
        if (e->m50 >= 0xFF) {
            e->m50 = e->m54 = e->m58 = 0xFF;
            e->m20_subState = 0;
        }
        break;
    }
    setWorldmapPolysColor((POLY_FT4*)m->m48_primBuffers[worldmapOddOrEven], m->m40_modelBlock->m4_numPrims, (s32)e->m50, (s32)e->m54, (s32)e->m58);
    return 1;
}

s32 worldmapMode16_taskLightPillars_init(s32 index) {
    sWorldmapModel* m = worldmapModels;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m20_subState = 0;
    e->m28_position.vx = worldmapPosition.vx;
    e->m28_position.vy = worldmapPosition.vy;
    e->m28_position.vz = worldmapPosition.vz;
    e->m50 = 0;
    e->m54 = 0;
    e->m58 = 0;
    m->m20_rotationMatrix = worldMapIdentityMatrix;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vx = ((VECTOR*)PSX_SCRATCH(0x1F800000))->vz = 0x1000;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vy = 0x7000;
    ScaleMatrix(&m->m20_rotationMatrix, (VECTOR*)PSX_SCRATCH(0x1F800000));
    m[1].m20_rotationMatrix = m[0].m20_rotationMatrix;
    setWorldmapMode16ModelSemiTrans(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims, 3);
    m++;
    setWorldmapMode16ModelSemiTrans(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims, 3);
    return 1;
}

s32 worldmapMode16_taskLightPillars_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = worldmapModels;

    if (e->m4_request == 1) {
        e->m4_request = 0;
        e->m20_subState = 1;
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m50 += 4;
        e->m54 += 2;
        e->m58 += 1;
        if (e->m50 >= 0xFC) {
            e->m50 = 0xFC;
            e->m20_subState = 0;
        }
        break;
    }
    setWorldmapPolysColor((POLY_FT4*)m->m48_primBuffers[worldmapOddOrEven], m->m40_modelBlock->m4_numPrims, (s32)e->m50, (s32)e->m54, (s32)e->m58);
    m++;
    setWorldmapPolysColor((POLY_FT4*)m->m48_primBuffers[worldmapOddOrEven], m->m40_modelBlock->m4_numPrims, (s32)e->m50, (s32)e->m54, (s32)e->m58);
    return 1;
}

s32 worldmapMode16_taskScreenWarp_init(s32 index) {
    POLY_FT4* p;
    u16* q;
    s32 i;

    worldmapMode16WarpPolys[0] = allocateHeap(192 * (s32)sizeof(POLY_FT4), 0);
    worldmapMode16WarpPolys[1] = allocateHeap(192 * (s32)sizeof(POLY_FT4), 0);
    worldmapMode16WarpAmplitudes = allocateHeap(0x180, 0);
    p = worldmapMode16WarpPolys[0];
    for (i = 0; i < 0xC0; i++, p++) {
        setlen(p, 9);
        setcode(p, 0x2C);
        p->r0 = 0x80;
        p->g0 = 0x80;
        p->b0 = 0x80;
        p->code |= 1;
        p->tpage = GetTPage(2, 0, 0x280, 0x100);
    }
    *(sWorldmapPolyBuf*)worldmapMode16WarpPolys[1] = *(sWorldmapPolyBuf*)worldmapMode16WarpPolys[0];
    q = worldmapMode16WarpAmplitudes;
    for (i = 0; i < 0xC0; i++) {
        *q++ = 1;
    }
    return 1;
}

s32 worldmapMode16_taskScreenWarp_update(s32 index) {
    POLY_FT4* p = worldmapMode16WarpPolys[worldmapOddOrEven];
    u16* q = worldmapMode16WarpAmplitudes;
    s32 i;
    s32 x;
    RECT r;

    for (i = 0; i < 0xC0; i++, p++) {
        x = rand() % *q - (*q >> 1);
        q++;
        p->x0 = x + 0x40;
        p->y0 = i;
        p->x1 = x + 0x100;
        p->y1 = i;
        p->x2 = x + 0x40;
        p->y2 = i + 1;
        p->x3 = x + 0x100;
        p->y3 = i + 1;
        p->u0 = 0;
        p->v0 = i;
        p->u1 = 0xC0;
        p->v1 = i;
        p->u2 = 0;
        p->v2 = i + 1;
        p->u3 = 0xC0;
        p->v3 = i + 1;
        addPrim(pCurrentWorldmapRenderingStruct->m70_OT, p);
#ifdef PLATFORM_PC
        /* The wide picture (include/pc_wide.h): the strips stay inside the
         * 4:3 picture and take the capture's centre, so the frame stays
         * wide. */
        pcWidePrim(p, 0, PC_WIDE_HANDLED);
#endif
    }
    r.x = 0x40;
    r.w = 0xC0;
    r.h = 0xD8;
    r.y = worldmapOddOrEven * 0xD8;
    SetDrawMove(&worldmapMode16WarpDrawMoves[worldmapOddOrEven], &r, 0x280, 0x100);
    addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapMode16WarpDrawMoves[worldmapOddOrEven]);
    return 1;
}

s32 worldmapMode16_taskWarpControl_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m20_subState = 0;
    e->m50 = 1;
    return 1;
}

s32 worldmapMode16_taskWarpControl_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 i;
    u16* q;
    s32 base;

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 1;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 2;
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 3;
        break;
    case 4:
        e->m4_request = 0;
        e->m20_subState = 4;
        break;
    case 5:
        e->m4_request = 0;
        e->m20_subState = 5;
        break;
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        q = worldmapMode16WarpAmplitudes;
        for (i = 0; i < 0xC0; i++) {
            *q++ = e->m50;
        }
        base = rand() & 0x3F;
        q = &worldmapMode16WarpAmplitudes[base];
        for (i = base; i < base + (rand() & 0x1F) + 1; i++, q++) {
            if ((rand() & 3) == 0) {
                *q = (rand() & 0x3F) + 1;
            }
        }
        base = (rand() & 0x3F) + 0x40;
        q = &worldmapMode16WarpAmplitudes[base];
        for (i = base; i < base + (rand() & 0x1F) + 1; i++, q++) {
            if ((rand() & 3) == 0) {
                *q = (rand() & 0x3F) + 1;
            }
        }
        base = (rand() & 0x1F) + 0x80;
        q = &worldmapMode16WarpAmplitudes[base];
        for (i = base; i < base + (rand() & 0x1F) + 1; i++, q++) {
            if ((rand() & 3) == 0) {
                *q = (rand() & 0x3F) + 1;
            }
        }
        break;
    case 2:
        q = worldmapMode16WarpAmplitudes;
        e->m50++;
        if (e->m50 > 0x40) {
            e->m50 = 0x40;
        }
        for (i = 0; i < 0xC0; i++) {
            *q++ = e->m50;
        }
        break;
    case 3:
        q = worldmapMode16WarpAmplitudes;
        e->m50--;
        if (e->m50 < 2) {
            e->m50 = 2;
        }
        for (i = 0; i < 0xC0; i++) {
            *q++ = e->m50;
        }
        break;
    case 4:
        q = worldmapMode16WarpAmplitudes;
        for (i = 0; i < 0xC0; i++, q++) {
            if ((rand() & 3) == 0) {
                *q = (rand() & 0x3F) + 1;
            }
        }
        break;
    case 5:
        q = worldmapMode16WarpAmplitudes;
        for (i = 0; i < 0xC0; i++) {
            *q++ = 2;
        }
        break;
    }
    return 1;
}

void worldmapMode17_update(void) {
    RECT r;
    void* src;

    initWorldmapGraphics();
    r.x = 0;
    r.y = 0;
    r.w = 0x140;
    r.h = 0xD8;
    MoveImage(&r, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    while (isCDBusy() >= 3) {
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
    resetBattleAudioState();
    worldmapPosition.vx = 0x70A9000;
    worldmapPosition.vy = -0x148000;
    worldmapPosition.vz = 0x42AA000;
    worldmapInitAllModels();
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    startLoadingWorldmapSoundFiles();
    initWorldmapSkyGradient();
    initWorldmapBillboards();
    initWorldmapClouds();
    initWorldmapTexAnims1();
    initWorldmapTexAnims2();
    initWorldmapSkyCloudBand();
    initWorldmapParticles();
    waitReadCompletion(0);
    worldmapWdsResult = loadWds(worldmapWds, 0);
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&worldmapPosition);
    do {
        loadWorldmapChunk();
        VSync(0);
    } while (isWorldStreamingDone() > 0);
    while (musicStatusFlag & 0x10) {
        PORT_YIELD(); /* until the SPU transfer interrupt clears it (the wds upload) */
    }
    freeHeap(worldmapWds);
    loadSequence(fieldSeq);
    src = worldmapSound1;
    memcpy(battleMusic, src, getFileSizeAligned(worldmapFile5));
    pMusic = createMusicInstance(battleMusic);
    startMusicInstance(pMusic, 0x7F, 0);
    setupWorldmapTask(worldmap_taskFader_init, worldmap_taskFader_update);
    setupWorldmapTask(worldmapMode17_taskScript_init, worldmap_taskScript_update);
    setupWorldmapTask(worldmapMode17_taskCamera_init, worldmapMode17_taskCamera_update);
    setupWorldmapTask(worldmapMode17_taskGlow_init, worldmapMode17_taskGlow_update);
    setupWorldmapTask(worldmapMode17_taskGlow_init, worldmapMode17_taskGlow_update);
    setupWorldmapTask(worldmapMode17_taskGlow_init, worldmapMode17_taskGlow_update);
    setupWorldmapTask(worldmapMode17_taskGlow_init, worldmapMode17_taskGlow_update);
    setupWorldmapTask(worldmapMode17_taskGlow_init, worldmapMode17_taskGlow_update);
    setupWorldmapTask(worldmapTaskDummy, worldmapMode17_taskRestream_update);
    setupWorldmapTask(worldmap_taskScene_init, worldmap_taskScene_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    initWorldmapEncounterTimers();
}

void worldmapMode17_free(void) {
    stopAllSounds();
    unloadSequence(fieldSeq);
    freeHeap(fieldSeq);
    freeWorldmapModels();
    freeWorldmapPolyFT4Array3();
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
    gameState.m231A_fieldID = 0x269;
    gameState.m2320_worldmapMode = 2;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
    worldmapExitToFieldVar0 = 1;
}

s32 worldmapMode17_taskScript_init(s32 index) {
    (gWorldmapState->m0_entries + index)->m50 = (sptr)worldmapMode17Script;
    return 1;
}

s32 worldmapMode17_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapGeometryOffsetY = 0x78;
    e->m7C = 0x1000;
    e->m28_position.vx = worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    e->m28_position.vz = worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m20_subState = 0;
    e->m4_request = 0;
    e->m5C = 0x500000;
    worldmapRotation.vx = -0x220;
    worldmapRotation.vy = 0;
    worldmapRotation.vz = 0;
    e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
    worldmapMatrixMode = 0;
    worldmapCameraVar0 = 0x500000;
    e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
    e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
    return 1;
}

s32 worldmapMode17_taskCamera_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    u8* scratch = (u8*)PSX_SCRATCH(0x1F800000);
    s32 r;

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 1;
        e->m50 = e->m38_step.vx;
        e->m54 = e->m38_step.vy;
        e->m58 = e->m38_step.vz;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 0;
        e->m5C = 0x3E0000;
        worldmapRotation.vx = -0x20;
        worldmapRotation.vy = 0xD70;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x3E0000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 2;
        break;
    case 4:
        e->m4_request = 0;
        e->m20_subState = 3;
        worldmapRadarPosition.vx = 0x7499000;
        worldmapRadarPosition.vy = -0x18C000;
        worldmapRadarPosition.vz = 0x408A000;
        break;
    case 5:
        e->m4_request = 0;
        e->m20_subState = 4;
        break;
    case 6:
        e->m20_subState = 5;
        e->m7C = 0x1000;
        e->m4_request = 0;
        e->m5C = 0x320000;
        worldmapRotation.vx = 0x40;
        worldmapRotation.vy = 0xD40;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x320000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = 0x1379000;
        e->m28_position.vy = worldmapRadarPosition.vy = -0x120000;
        e->m28_position.vz = worldmapRadarPosition.vz = 0x4B2A000;
        break;
    case 7:
        e->m4_request = 0;
        e->m20_subState = 6;
        break;
    case 8:
        e->m20_subState = 7;
        e->m4_request = 0;
        e->m5C = 0x100000;
        worldmapRotation.vx = -0x90;
        worldmapRotation.vy = -0x510;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x100000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = 0x7529000;
        e->m28_position.vy = worldmapRadarPosition.vy = -0x18C000;
        e->m28_position.vz = worldmapRadarPosition.vz = 0x2A3A000;
        break;
    case 9:
        e->m4_request = 0;
        e->m20_subState = 8;
        break;
    case 10:
        e->m20_subState = 9;
        e->m4_request = 0;
        e->m5C = 0x320000;
        worldmapRotation.vx = -0x160;
        worldmapRotation.vy = 0xDE0;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x320000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = 0x2659000;
        e->m28_position.vy = worldmapRadarPosition.vy = -0x110000;
        e->m28_position.vz = worldmapRadarPosition.vz = 0x6D8A000;
        break;
    case 11:
        e->m20_subState = 10;
        e->m4_request = 0;
        e->m5C = 0x290000;
        worldmapRotation.vx = -0x1A0;
        worldmapRotation.vy = 0x160;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x290000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = 0x4BA9000;
        e->m28_position.vy = worldmapRadarPosition.vy = -0x288000;
        e->m28_position.vz = worldmapRadarPosition.vz = 0x1DDA000;
        break;
    case 12:
        e->m4_request = 0;
        e->m20_subState = 11;
        break;
    }
    if (worldmapMatrixMode == 0) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m50 = approachWorldmapValue((s32)e->m50, -0x20000, 0x5D17);
        e->m54 = approachWorldmapValue((s32)e->m54, 0x580000, 0x10000);
        break;
    case 2:
        e->m50 = approachWorldmapValue((s32)e->m50, -0x1F0000, -0x10000);
        e->m54 = approachWorldmapValue((s32)e->m54, 0x1290000, 0x10000);
        e->m5C = approachWorldmapValue(e->m5C, 0x490000, 0x10000);
        break;
    case 3:
        e->m50 = approachWorldmapValue((s32)e->m50, -0x10000, 0x10000);
        e->m54 = approachWorldmapValue((s32)e->m54, 0x1290000, 0x10000);
        e->m5C = approachWorldmapValue(e->m5C, 0x2B0000, -0x10000);
        break;
    case 4:
        e->m7C += 0x200;
        if (e->m7C > 0x8000) {
            e->m7C = 0x8000;
            e->m20_subState = 0;
        }
        break;
    case 6:
        e->m54 = approachWorldmapValue((s32)e->m54, 0xF40000, 0x4000);
        break;
    case 8:
        e->m50 = approachWorldmapValue((s32)e->m50, 0x50000, 0x2000);
        e->m54 = approachWorldmapValue((s32)e->m54, 0x10000, 0x8000);
        break;
    case 11:
        e->m50 = approachWorldmapValue((s32)e->m50, -0x80000, 0x2400);
        e->m54 = approachWorldmapValue((s32)e->m54, 0x2C0000, 0x2C00);
        e->m5C = approachWorldmapValue(e->m5C, 0x100000, -0x3200);
        worldmapRadarPosition.vx = approachWorldmapValue(worldmapRadarPosition.vx, 0x4CD9000, 0x2600);
        worldmapRadarPosition.vy = approachWorldmapValue(worldmapRadarPosition.vy, -0x15C000, 0x2580);
        worldmapRadarPosition.vz = approachWorldmapValue(worldmapRadarPosition.vz, 0x1C8A000, -0x2A00);
        checkWorldmapPositionSub0(&worldmapRadarPosition);
        break;
    }
    smoothWorldmapCameraRotation(e, (sWorldmapCameraDelta*)scratch);
    smoothWorldmapCameraDistance(e, (sWorldmapCameraDelta*)scratch);
    smoothWorldmapCameraPosition(e, (sWorldmapCameraDelta*)scratch);
    r = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    ((SVECTOR*)(scratch + 0xA0))->vy = r;
    worldmapCameraVector.m0_eye.vy += r;
    worldmapCameraVector.m8_target.vy += ((SVECTOR*)(scratch + 0xA0))->vy;
    return 1;
}

void updateWorldmapMode17Glow(sWorldmapStateEntry* e, sWorldmapModel* m, sWorldmapScratch* sp) {
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        sp->mA0.vz = 0;
        sp->mA0.vx = 0;
        sp->mA0.vy = e->m68;
        sp->m0.vx = e->m70 & 0x7FFF;
        sp->m0.vy = 0x1000;
        sp->m0.vz = getAngleCos(e->m6C) * 6;
        createRotationMatrix(&sp->mA0, &sp->mF0_rotation);
        ScaleMatrix(&sp->mF0_rotation, &sp->m0);
        m->m20_rotationMatrix = sp->mF0_rotation;
        e->m50 += e->m5C;
        e->m54 += e->m60;
        if (e->m50 >= 0x100) {
            e->m50 = 0xFF;
            e->m5C = -e->m5C;
        } else if (e->m50 < 0x40) {
            e->m50 = 0x40;
            e->m5C = -e->m5C;
        }
        if (e->m54 >= 0x100) {
            e->m54 = 0xFF;
            e->m60 = -e->m60;
        } else if (e->m54 < 0x80) {
            e->m54 = 0x80;
            e->m60 = -e->m60;
        }
        e->m6C = (e->m6C + 8) & 0xFFF;
        e->m70 += e->m74;
        break;
    }
}

void setWorldmapMode17ModelSemiTransFT3(sWorldmapModel* m, POLY_FT3* p, u32 n, s32 abr) {
    u32 i = n;

    while (i--) {
        setlen(p, 7);
        setcode(p, 0x24);
        p->tpage = GetTPage(0, abr, 0x2C0, 0x100);
        p->r0 = 0;
        p->g0 = 0;
        p->b0 = 0;
        p->code |= 2;
        p++;
    }
    memcpy(m->m48_primBuffers[1], m->m48_primBuffers[0], n * sizeof(POLY_FT3));
}

void setWorldmapPolysColorFT3(POLY_FT3* p, u32 n, s32 r, s32 g, s32 b) {
    while (n--) {
        p->r0 = r;
        p->g0 = g;
        p->b0 = b;
        p++;
    }
}

s32 worldmapMode17_taskGlow_init(s32 index) {
    sWorldmapModel* m = &worldmapModels[78 + index];

    setWorldmapMode17ModelSemiTransFT3(m, (POLY_FT3*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims, 3);
    return 1;
}

#ifdef PLATFORM_PC
/* The PS1 copies a glow's 11 parameters as consecutive words from m50 on
 * (m50-m78); some of those fields are pointer-sized on the PC. */
static void pcSetGlowParams(sWorldmapStateEntry* e, s16* src) {
    e->m50 = src[0];
    e->m54 = src[1];
    e->m58 = src[2];
    e->m5C = src[3];
    e->m60 = src[4];
    e->m64 = src[5];
    e->m68 = src[6];
    e->m6C = src[7];
    e->m70 = src[8];
    e->m74 = src[9];
    e->m78 = src[10];
}
#endif

s32 worldmapMode17_taskGlow_update(s32 index) {
    s32 k = index - 3;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[k + 81];
    s32* p;
    s32 i;

    switch (e->m4_request) {
    case 0:
        break;
    case 1:
        e->m20_subState = 1;
        e->m4_request = 0;
        m->m8_position.vx = worldmapMode17GlowParams1[k * 14 + 0];
        m->m8_position.vy = worldmapMode17GlowParams1[k * 14 + 1];
        m->m8_position.vz = worldmapMode17GlowParams1[k * 14 + 2];
#ifndef PLATFORM_PC
        p = &e->m50;
        for (i = 3; i < 14; i++) {
            *p++ = worldmapMode17GlowParams1[k * 14 + i];
        }
#else
        pcSetGlowParams(e, &worldmapMode17GlowParams1[k * 14 + 3]);
#endif
        break;
    case 2:
        e->m20_subState = 1;
        e->m4_request = 0;
        m->m8_position.vx = worldmapMode17GlowParams2[k * 14 + 0];
        m->m8_position.vy = worldmapMode17GlowParams2[k * 14 + 1];
        m->m8_position.vz = worldmapMode17GlowParams2[k * 14 + 2];
#ifndef PLATFORM_PC
        p = &e->m50;
        for (i = 3; i < 14; i++) {
            *p++ = worldmapMode17GlowParams2[k * 14 + i];
        }
#else
        pcSetGlowParams(e, &worldmapMode17GlowParams2[k * 14 + 3]);
#endif
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 0;
        break;
    case 4:
        e->m20_subState = 1;
        e->m4_request = 0;
        m->m8_position.vx = worldmapMode17GlowParams3[k * 14 + 0];
        m->m8_position.vy = worldmapMode17GlowParams3[k * 14 + 1];
        m->m8_position.vz = worldmapMode17GlowParams3[k * 14 + 2];
#ifndef PLATFORM_PC
        p = &e->m50;
        for (i = 3; i < 14; i++) {
            *p++ = worldmapMode17GlowParams3[k * 14 + i];
        }
#else
        pcSetGlowParams(e, &worldmapMode17GlowParams3[k * 14 + 3]);
#endif
        break;
    }
    updateWorldmapMode17Glow(e, m, (sWorldmapScratch*)PSX_SCRATCH(0x1F800000));
    setWorldmapPolysColorFT3((POLY_FT3*)m->m48_primBuffers[worldmapOddOrEven], m->m40_modelBlock->m4_numPrims, (s32)e->m50, (s32)e->m54, (s32)e->m58);
    return 1;
}
