/*
 * End of mode 15 and mode 13 (from 0x8007FF70).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "worldmap/worldmap.h"
#include "main/game_state.h"

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern MATRIX worldMapIdentityMatrix;
extern s32 worldmapOddOrEven;
extern u16 worldmapMode13DirectorStates[];
extern u16 worldmapMode13DirectorTimers[];
extern sWorldmapSeqHeader* fieldSeq;
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern s32 worldmapMatrixMode;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern VECTOR worldmapPosition;
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern SVECTOR worldmapRotation;
extern s32 worldmapExitToFieldVar0;
extern MATRIX D_8009BE4C;

void setWorldmapPolysColor(POLY_FT4* p, s32 n, s32 r, s32 g, s32 b);
void pcWm60ModelColour(sWorldmapModel* m, s32 r, s32 g, s32 b, s32 ft3);
s32 changeWorldmapEntityState(s32 index, s32 state);
void playSoundEffectInFreeSlot(s32);
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
void initWorldmapGraphics(void);
void worldmapFadeIn(s32, s32, s32, s32);
void finalizeWorldmapEventFileLoading(void);
void allocAndInitWorldmapState(void);
void moveWorldmapClouds(void);
void worldmapDotInit(void);
void readWorldmapSequenceFile(void);
void worldmapInitAllModels(void);
void loadWorldmapTextureFromFile3(void);
void loadWorldmapTextureFromFile2(void);
void initWorldmapSkyGradient(void);
void initWorldmapBillboards(void);
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
s32 worldmapMode13_taskDirector_init(s32);
s32 worldmapMode13_taskDirector_update(s32);
s32 worldmapMode13_taskCamera_init(s32);
s32 worldmapMode13_taskCamera_update(s32);
s32 worldmapMode13_taskExplosion_init(s32);
s32 worldmapMode13_taskExplosion_update(s32);
s32 worldmapMode13_taskRings_init(s32);
s32 worldmapMode13_taskRings_update(s32);
s32 worldmap_taskScene_init(s32);
s32 worldmap_taskScene_update(s32);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void createWorlmapPolyFT4Array3(void);
void initWorldmapEncounterTimers(void);
void loadSequence(void*);
void stopAllSounds(void);
void unloadSequence(void*);
void freeWorldmapModels(void);
void freeWorldmapPolyFT4Array3(void);
void freeWorldmapClouds(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapPolyFT4Array1(void);
void freeWorldmapChunks(void);
void freeWorldmapState(void);

s32 worldmapMode15_taskImpactRings_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;
    sWorldmapModel* m = &models[9];

    if (e->m4_request == 1) {
        e->m4_request = 0;
    }
    models[9].m8_position.vx = models[10].m8_position.vx = e->m28_position.vx >> 12;
    models[9].m8_position.vy = models[10].m8_position.vy = e->m28_position.vy >> 12;
    models[9].m8_position.vz = models[10].m8_position.vz = e->m28_position.vz >> 12;
    models[10].m20_rotationMatrix = worldMapIdentityMatrix;
    models[9].m20_rotationMatrix = models[10].m20_rotationMatrix;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vx = ((VECTOR*)PSX_SCRATCH(0x1F800000))->vz = (s32)e->m50;
    ((VECTOR*)PSX_SCRATCH(0x1F800010))->vx = ((VECTOR*)PSX_SCRATCH(0x1F800010))->vz = (s32)e->m54;
    ((VECTOR*)PSX_SCRATCH(0x1F800000))->vy = ((VECTOR*)PSX_SCRATCH(0x1F800010))->vy = 0x1000;
    ScaleMatrix(&models[9].m20_rotationMatrix, (VECTOR*)PSX_SCRATCH(0x1F800000));
    ScaleMatrix(&models[10].m20_rotationMatrix, (VECTOR*)PSX_SCRATCH(0x1F800010));
    e->m50 += 0x180;
    if (e->m50 > 0x7FFF) {
        e->m50 = 0x7FFF;
    }
    e->m54 += 0x180;
    if (e->m54 > 0x7FFF) {
        e->m54 = 0x7FFF;
    }
    pcWm60ModelColour(m, (s32)e->m58, (s32)e->m58, (s32)e->m58, 0); /* Enhanced, 60 Hz: both buffers (w_80070CFC.c) */
    m++;
    pcWm60ModelColour(m, (s32)e->m58, (s32)e->m58, (s32)e->m58, 0); /* Enhanced, 60 Hz: both buffers (w_80070CFC.c) */
    e->m58 -= 3;
    if (e->m58 < 0) {
        e->m58 = 0;
        return 3;
    }
    return 1;
}

void worldmapMode13_update(void) {
    RECT r;

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
    readWorldmapSequenceFile();
    worldmapPosition.vx = 0x4100000;
    worldmapPosition.vy = -0x80000;
    worldmapPosition.vz = 0x13C0000;
    worldmapInitAllModels();
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    initWorldmapSkyGradient();
    initWorldmapBillboards();
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
    setupWorldmapTask(worldmapMode13_taskDirector_init, worldmapMode13_taskDirector_update);
    setupWorldmapTask(worldmapMode13_taskCamera_init, worldmapMode13_taskCamera_update);
    setupWorldmapTask(worldmapMode13_taskExplosion_init, worldmapMode13_taskExplosion_update);
    setupWorldmapTask(worldmapMode13_taskRings_init, worldmapMode13_taskRings_update);
    setupWorldmapTask(worldmap_taskScene_init, worldmap_taskScene_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    initWorldmapEncounterTimers();
}

void worldmapMode13_free(void) {
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
    gameState.m231A_fieldID = 0x84;
    gameState.m2320_worldmapMode = 2;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
    worldmapExitToFieldVar0 = 1;
}

s32 worldmapMode13_taskDirector_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    e->m50 = 0;
    e->m20_subState = worldmapMode13DirectorStates[0];
    e->m22_timer = worldmapMode13DirectorTimers[e->m50];
    return 1;
}

s32 worldmapMode13_taskDirector_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m22_timer--;
        if (e->m22_timer < 0) {
            e->m20_subState = worldmapMode13DirectorStates[e->m50];
            e->m22_timer = worldmapMode13DirectorTimers[e->m50];
            e->m50++;
        }
        break;
    case 2:
        changeWorldmapEntityState(3, 1);
        e->m20_subState = 1;
        break;
    case 3:
        changeWorldmapEntityState(2, 2);
        e->m20_subState = 1;
        break;
    case 4:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x80;
        e->m20_subState = 1;
        break;
    case 5:
        changeWorldmapEntityState(0, 0xC);
        changeWorldmapEntityState(4, 1);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x80;
        e->m20_subState = 1;
        break;
    case 6:
        changeWorldmapEntityState(2, 3);
        e->m20_subState = 1;
        break;
    case 7:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 2;
        worldmapFadePolyStep = 4;
        e->m20_subState = 1;
        break;
    case 8:
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x16);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x17);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x18);
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
