/*
 * Mode 12 tasks (0x8007CC6C-0x8007D7FC) and mode 15 (from 0x8007D918).
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
extern VECTOR worldmapRadarPosition;
extern VECTOR worldmapRadarPosition3;

void attachWorldmapModels(s32, s32);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void clearWorldmapParticles(s32 type);
void checkWorldmapPositionSub0(VECTOR*);
s32 getWorldmapElevationWithWater(s32, s32);

extern MATRIX D_8009BE4C;
extern MATRIX worldMapIdentityMatrix;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern s32 worldmapMatrixMode;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern s32 worldmapInitialPositionIndex;
extern SVECTOR worldmapMode15Positions[3];
extern VECTOR worldmapPosition;
extern void* worldmapWds;
extern void* worldmapSound1;
extern s32 worldmapFile5;
extern sWorldmapSeqHeader* fieldSeq;
extern u16 musicStatusFlag;
extern u8 battleMusic[0x3200];

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
s32 worldmapMode15_taskDirector_init(s32);
s32 worldmapMode15_taskDirector_update(s32);
s32 worldmapMode15_taskCamera_init(s32);
s32 worldmapMode15_taskCamera_update(s32);
s32 worldmapMode15_taskProjectile_init(s32);
s32 worldmapMode15_taskProjectile_update(s32);
s32 worldmapMode15_taskTrailRing_init(s32);
s32 worldmapMode15_taskTrailRing_update(s32);
s32 worldmapMode15_taskImpactRings_init(s32);
s32 worldmapMode15_taskImpactRings_update(s32);

void resetBattleAudioState(void);
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
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern u16 worldmapMode15NextWorldmapModes[4]; /* mode 15's three positions and a pad */
extern s32 worldmapExitToFieldVar0;

typedef struct {
    u16* m0;
    u16* m4;
} sWorldmapUnkPair;
extern sWorldmapUnkPair worldmapMode15DirectorLists[3];
extern u16 worldmapMode15SoundIds[3][3];
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
void playSoundEffectInFreeSlot(s32);
s32 changeWorldmapEntityState(s32 index, s32 state);
void killWorldmapParticles(s32);
s32 rand(void);
extern s32 worldmapGeometryOffsetY;
extern s32 worldmapCameraVar0;
extern SVECTOR worldmapRotation;
extern VECTOR worldmapGridInputPosition;
extern sWorldmapCameraState worldmapCameraVector;
extern SVECTOR worldmapMode15ProjectileStopPoints[3];
s32 worldmapGetAltitudeFor2dPoint(s32, s32);
void extractWorldmapEulerAngles(MATRIX*, SVECTOR*);
extern u16 worldmapMode15TrailRingScales[];
s32 setWorldmapModelSemiTransGrey(sWorldmapModel* model, POLY_FT4* poly, s32 count);

void setWorldmapModelSemiTransBlue(sWorldmapModel* m, POLY_FT4* p, s32 n, s32 abr);
void computeCameraVector(sWorldmapCameraState*, VECTOR*, s32, SVECTOR*);
void loadSequence(void*);
void* createMusicInstance(void* seq);
void startMusicInstance(void* inst, s32 volume, s32 arg2);

s32 worldmapMode12_taskShip4_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(4, 0);
    attachWorldmapModels(4, 2);
    attachWorldmapModels(4, 1);
    attachWorldmapModels(4, 3);
    m = &worldmapModels[4];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m28_position.vx = 0xD00000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0x400000;
    return 1;
}

s32 worldmapMode12_taskShip4_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[4];
    u8* scratch = (u8*)PSX_SCRATCH(0x1F800000);

    if (e->m4_request == 1) {
        e->m4_request = 0;
        e->m20_subState = 1;
    } else if (e->m4_request == 2) {
        e->m4_request = 0;
        e->m20_subState = 2;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    m->m8_position.vx = e->m28_position.vx >> 12;
    m->m8_position.vy = e->m28_position.vy >> 12;
    m->m8_position.vz = e->m28_position.vz >> 12;
    ((SVECTOR*)(scratch + 0xA0))->vx = e->m28_position.vx >> 12;
    ((SVECTOR*)(scratch + 0xA0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)(scratch + 0xA0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x13, (SVECTOR*)(scratch + 0xA0), 0);
    switch (e->m20_subState) {
    case 1:
        spawnWorldmapParticles(0x1F, (SVECTOR*)(scratch + 0xA0), 0);
    case 0:
        worldmapRadarPosition.vz = e->m28_position.vz;
        break;
    case 2:
        spawnWorldmapParticles(0x1F, (SVECTOR*)(scratch + 0xA0), 0);
        break;
    }
    return 1;
}

s32 worldmapMode12_taskShip5_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(5, 6);
    m = &worldmapModels[5];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m28_position.vx = 0xB00000;
    e->m20_subState = 0;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0x200000;
    return 1;
}

s32 worldmapMode12_taskShip5_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request == 1) {
        e->m4_request = 0;
        e->m20_subState = 1;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) - 0x4000;
    models[5].m8_position.vx = e->m28_position.vx >> 12;
    models[5].m8_position.vy = e->m28_position.vy >> 12;
    models[5].m8_position.vz = e->m28_position.vz >> 12;
    /* vx written as the vy of the SVECTOR 8 bytes lower: keeps the first
     * store's address out of the register that holds 0x1F8000A0 (retail). */
    ((SVECTOR*)PSX_SCRATCH(0x1F80009E))->vy = e->m28_position.vx >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x15, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    if (e->m20_subState == 1) {
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0x1E, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    }
    return 1;
}

s32 worldmapMode12_taskShip9_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(9, 7);
    attachWorldmapModels(9, 8);
    m = &worldmapModels[9];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vx = 0xC00000;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0;
    return 1;
}

s32 worldmapMode12_taskShip9_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request == 1) {
        e->m4_request = 0;
        models[9].m0_hidden = 1;
        models[7].m0_hidden = 1;
        models[8].m0_hidden = 1;
        clearWorldmapParticles(0x16);
        return 3;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) - 0x4000;
    models[9].m8_position.vx = e->m28_position.vx >> 12;
    models[9].m8_position.vy = e->m28_position.vy >> 12;
    models[9].m8_position.vz = e->m28_position.vz >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x16, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    return 1;
}

s32 worldmapMode12_taskShip10_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(0xA, 0xB);
    m = &worldmapModels[10];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m28_position.vx = 0xE00000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0x100000;
    return 1;
}

s32 worldmapMode12_taskShip10_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request == 1) {
        e->m4_request = 0;
        models[10].m0_hidden = 1;
        models[11].m0_hidden = 1;
        clearWorldmapParticles(0x17);
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0x1C, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
        return 3;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) - 0x4000;
    models[10].m8_position.vx = e->m28_position.vx >> 12;
    models[10].m8_position.vy = e->m28_position.vy >> 12;
    models[10].m8_position.vz = e->m28_position.vz >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x17, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    return 1;
}

s32 worldmapMode12_taskShip12_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(0xC, 0xF);
    m = &worldmapModels[12];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m28_position.vx = 0xE80000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0x280000;
    return 1;
}

s32 worldmapMode12_taskShip12_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    if (e->m4_request == 1) {
        e->m4_request = 0;
        models[12].m0_hidden = 1;
        models[15].m0_hidden = 1;
        clearWorldmapParticles(0x18);
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
        ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0x1D, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
        return 3;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) - 0x4000;
    models[12].m8_position.vx = e->m28_position.vx >> 12;
    models[12].m8_position.vy = e->m28_position.vy >> 12;
    models[12].m8_position.vz = e->m28_position.vz >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x18, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    return 1;
}

s32 worldmapMode12_taskShip13_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(0xD, 0xE);
    m = &worldmapModels[13];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m38_step.vz = -0x4000;
    e->m28_position.vx = 0xF80000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m28_position.vy = 0;
    e->m28_position.vz = 0x380000;
    return 1;
}

s32 worldmapMode12_taskShip13_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* models = worldmapModels;

    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) - 0x4000;
    models[13].m8_position.vx = e->m28_position.vx >> 12;
    models[13].m8_position.vy = e->m28_position.vy >> 12;
    models[13].m8_position.vz = e->m28_position.vz >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vx = e->m28_position.vx >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vy = e->m28_position.vy >> 12;
    ((SVECTOR*)PSX_SCRATCH(0x1F8000A0))->vz = e->m28_position.vz >> 12;
    spawnWorldmapParticles(0x19, (SVECTOR*)PSX_SCRATCH(0x1F8000A0), 0);
    return 1;
}

s32 worldmapMode12_taskAirship_init(s32 index) {
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    m = &worldmapModels[16];
    m->m0_hidden = 1;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e = &gWorldmapState->m0_entries[index];
    e->m28_position.vx = 0xD00000;
    e->m28_position.vz = 0x400000;
    e->m28_position.vy = -0x280000;
    e->m38_step.vy = 0;
    e->m38_step.vx = 0;
    e->m38_step.vz = -0x6000;
    return 3;
}

s32 worldmapMode12_taskAirship_update(s32 index) {
    sWorldmapModel* m = &worldmapModels[16];
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        worldmapModels[16].m0_hidden = 0;
        e->m28_position.vx = worldmapRadarPosition3.vx;
        e->m28_position.vz = worldmapRadarPosition3.vz + 0x400000;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 2;
        break;
    }
    e->m28_position.vz += e->m38_step.vz;
    checkWorldmapPositionSub0(&e->m28_position);
    m->m8_position.vx = e->m28_position.vx >> 12;
    m->m8_position.vy = e->m28_position.vy >> 12;
    m->m8_position.vz = e->m28_position.vz >> 12;
    if (e->m20_subState == 2) {
        worldmapRadarPosition.vx = e->m28_position.vx;
        worldmapRadarPosition.vy = e->m28_position.vy;
        worldmapRadarPosition.vz = e->m28_position.vz;
    }
    return 1;
}

void worldmapMode15_update(void) {
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
    worldmapPosition.vx = worldmapMode15Positions[worldmapInitialPositionIndex].vx << 12;
    worldmapPosition.vy = worldmapMode15Positions[worldmapInitialPositionIndex].vy << 12;
    worldmapPosition.vz = worldmapMode15Positions[worldmapInitialPositionIndex].vz << 12;
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
    setupWorldmapTask(worldmapMode15_taskDirector_init, worldmapMode15_taskDirector_update);
    setupWorldmapTask(worldmapMode15_taskCamera_init, worldmapMode15_taskCamera_update);
    setupWorldmapTask(worldmapMode15_taskProjectile_init, worldmapMode15_taskProjectile_update);
    setupWorldmapTask(worldmapMode15_taskTrailRing_init, worldmapMode15_taskTrailRing_update);
    setupWorldmapTask(worldmapMode15_taskTrailRing_init, worldmapMode15_taskTrailRing_update);
    setupWorldmapTask(worldmapMode15_taskTrailRing_init, worldmapMode15_taskTrailRing_update);
    setupWorldmapTask(worldmapMode15_taskTrailRing_init, worldmapMode15_taskTrailRing_update);
    setupWorldmapTask(worldmapMode15_taskTrailRing_init, worldmapMode15_taskTrailRing_update);
    setupWorldmapTask(worldmapMode15_taskImpactRings_init, worldmapMode15_taskImpactRings_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

void worldmapMode15_free(void) {
    playBattleMusic(pMusic, 0, 0xF0);
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
    gameState.m231A_fieldID = 0x1A1;
    gameState.m2320_worldmapMode = worldmapMode15NextWorldmapModes[worldmapInitialPositionIndex];
    gameState.m231C_cameraYaw = worldmapRotation.vy;
    worldmapExitToFieldVar0 = 1;
}

/* (8), worldmapMode15_taskDirector_update (16), worldmapMode15_taskTrailRing_update (8). */
s32 worldmapMode15_taskDirector_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 unused[2]; /* reserves retail's stack frame */

    e->m54 = (sptr)worldmapMode15DirectorLists[worldmapInitialPositionIndex].m0;
    e->m58 = (sptr)worldmapMode15DirectorLists[worldmapInitialPositionIndex].m4;
    e->m50 = 0;
    e->m20_subState = *(u16*)e->m54;
    e->m22_timer = ((u16*)e->m58)[e->m50];
    e->m50++;
    return 1;
}

s32 worldmapMode15_taskDirector_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 unused[4]; /* reserves retail's stack frame */

    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m22_timer--;
        if (e->m22_timer < 0) {
            e->m20_subState = ((u16*)e->m54)[e->m50];
            e->m22_timer = ((u16*)e->m58)[e->m50];
            e->m50++;
        }
        break;
    case 2:
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x1C);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x1D);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x1E);
        changeWorldmapEntityState(2, 2);
        changeWorldmapEntityState(3, 2);
        changeWorldmapEntityState(4, 3);
        changeWorldmapEntityState(5, 3);
        changeWorldmapEntityState(6, 3);
        changeWorldmapEntityState(7, 3);
        changeWorldmapEntityState(8, 3);
        e->m20_subState = 1;
        break;
    case 3:
        killWorldmapParticles(0x22);
        killWorldmapParticles(0x23);
        killWorldmapParticles(0x24);
        changeWorldmapEntityState(2, 3);
        changeWorldmapEntityState(3, 3);
        changeWorldmapEntityState(4, 3);
        changeWorldmapEntityState(5, 3);
        changeWorldmapEntityState(6, 3);
        changeWorldmapEntityState(7, 3);
        changeWorldmapEntityState(8, 3);
        e->m20_subState = 1;
        break;
    case 4:
        changeWorldmapEntityState(2, 4);
        changeWorldmapEntityState(3, 4);
        changeWorldmapEntityState(4, 3);
        changeWorldmapEntityState(5, 3);
        changeWorldmapEntityState(6, 3);
        changeWorldmapEntityState(7, 3);
        changeWorldmapEntityState(8, 3);
        e->m20_subState = 1;
        break;
    case 5:
        changeWorldmapEntityState(2, 5);
        e->m20_subState = 1;
        break;
    case 6:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x40;
        e->m20_subState = 1;
        break;
    case 7:
        changeWorldmapEntityState(3, 5);
        changeWorldmapEntityState(4, 4);
        changeWorldmapEntityState(5, 4);
        changeWorldmapEntityState(6, 4);
        changeWorldmapEntityState(7, 4);
        changeWorldmapEntityState(8, 4);
        changeWorldmapEntityState(0, 0xC);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x40;
        e->m20_subState = 1;
        break;
    case 8:
        changeWorldmapEntityState(2, 6);
        e->m20_subState = 1;
        break;
    case 9:
        changeWorldmapEntityState(0, 0xD);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x80;
        e->m20_subState = 1;
        break;
    case 10:
        changeWorldmapEntityState(0, 0xC);
        worldmapFadePolyAbr = 1;
        worldmapFadePolyStep = 0x80;
        changeWorldmapEntityState(9, 1);
        changeWorldmapEntityState(2, 7);
        e->m20_subState = 1;
        break;
    case 16:
        changeWorldmapEntityState(2, 4);
        changeWorldmapEntityState(3, 0x10);
        changeWorldmapEntityState(4, 3);
        changeWorldmapEntityState(5, 3);
        changeWorldmapEntityState(6, 3);
        changeWorldmapEntityState(7, 3);
        changeWorldmapEntityState(8, 3);
        e->m20_subState = 1;
        break;
    case 17:
        changeWorldmapEntityState(2, 0x10);
        e->m20_subState = 1;
        break;
    case 18:
        changeWorldmapEntityState(2, 0x11);
        e->m20_subState = 1;
        break;
    case 24:
        changeWorldmapEntityState(2, 4);
        changeWorldmapEntityState(3, 0x18);
        changeWorldmapEntityState(4, 3);
        changeWorldmapEntityState(5, 3);
        changeWorldmapEntityState(6, 3);
        changeWorldmapEntityState(7, 3);
        changeWorldmapEntityState(8, 3);
        e->m20_subState = 1;
        break;
    case 25:
        changeWorldmapEntityState(2, 0x18);
        e->m20_subState = 1;
        break;
    case 61:
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | worldmapMode15SoundIds[worldmapInitialPositionIndex][0]);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | worldmapMode15SoundIds[worldmapInitialPositionIndex][1]);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | worldmapMode15SoundIds[worldmapInitialPositionIndex][2]);
        e->m20_subState = 1;
        break;
    case 62:
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x19);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x1A);
        playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x1B);
        e->m20_subState = 1;
        break;
    case 63:
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

s32 worldmapMode15_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapMatrixMode = 0;
    e->m7C = 0x1000;
    worldmapGeometryOffsetY = 0x78;
    worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m4_request = 1;
    e->m58 = 0x40;
    e->m20_subState = 0;
    e->m50 = 0;
    return 1;
}

s32 worldmapMode15_taskCamera_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    u8* scratch = (u8*)PSX_SCRATCH(0x1F800000);
    s32 r;

    switch (e->m4_request) {
    case 1:
        worldmapCameraVar0 = 0x500000;
        e->m4_request = 0;
        worldmapRotation.vx = 0x150;
        worldmapRotation.vy = 0xF50;
        worldmapRotation.vz = 0;
        break;
    case 2:
        worldmapCameraVar0 = 0x4D0000;
        e->m4_request = 0;
        worldmapRotation.vx = 0xC60;
        worldmapRotation.vy = 0x380;
        worldmapRotation.vz = 0;
        break;
    case 3:
        worldmapCameraVar0 = 0x400000;
        e->m4_request = 0;
        worldmapRotation.vx = 0x30;
        worldmapRotation.vy = 0x160;
        worldmapRotation.vz = 0;
        break;
    case 4:
        worldmapCameraVar0 = 0x400000;
        e->m4_request = 0;
        worldmapRotation.vx = 0x30;
        worldmapRotation.vy = 0x960;
        worldmapRotation.vz = 0;
        break;
    case 5:
        e->m20_subState = 1;
        e->m4_request = 0;
        e->m5C = -0x8000;
        break;
    case 6:
        e->m20_subState = 3;
        e->m7C = 0x1000;
        worldmapCameraVar0 = 0x200000;
        e->m4_request = 0;
        worldmapRotation.vx = -0x58;
        worldmapRotation.vy = 0xE58;
        worldmapRotation.vz = 0;
        break;
    case 7:
        e->m20_subState = 5;
        e->m4_request = 0;
        e->m7C = 0x40000;
        break;
    case 16:
        e->m20_subState = 0x10;
        e->m4_request = 0;
        e->m5C = -0x8000;
        break;
    case 17:
        e->m20_subState = 0x10;
        e->m7C = 0x1000;
        worldmapCameraVar0 = 0x400000;
        e->m4_request = 0;
        worldmapRotation.vx = 0xE0;
        worldmapRotation.vy = 0x798;
        worldmapRotation.vz = 0;
        break;
    case 24:
        e->m20_subState = 0x18;
        e->m4_request = 0;
        e->m5C = -0x8000;
        break;
    }
    if (worldmapMatrixMode == 0) {
        checkWorldmapPositionSub0(&e->m28_position);
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        worldmapRadarPosition3.vx += -0x42D00;
        worldmapRadarPosition3.vz += 0x6D300;
        if (worldmapRadarPosition3.vx < 0x1F9E000 && worldmapRadarPosition3.vz > 0x5998000) {
            worldmapRadarPosition3.vx = 0x1F9E000;
            worldmapRadarPosition3.vz = 0x5998000;
            e->m20_subState = 2;
            e->m7C = 0x10000;
        } else {
            worldmapGridInputPosition.vx += -0x42D00;
            worldmapGridInputPosition.vz += 0x6D300;
        }
        checkWorldmapPositionSub0(&e->m28_position);
        worldmapRotation.vy -= 8;
        worldmapRotation.vx += e->m5C >> 12;
        e->m5C += 0x300;
        worldmapCameraVar0 += 0x8000;
        if (worldmapRotation.vx > 0x80) {
            worldmapRotation.vx = 0x80;
        }
        break;
    case 2:
        e->m7C -= 0x80;
        if (e->m7C < 0x1000) {
            e->m7C = 0x1000;
        }
        break;
    case 3:
        worldmapRadarPosition3.vx += -0x4C400;
        worldmapRadarPosition3.vy += 0xFF80;
        worldmapRadarPosition3.vz += -0x65580;
        if (worldmapRadarPosition3.vx < 0x1498000 && worldmapRadarPosition3.vz < 0x4AF2000) {
            worldmapRadarPosition3.vx = 0x1498000;
            worldmapRadarPosition3.vz = 0x4AF2000;
            e->m20_subState = 4;
            e->m5C = 0x20000;
        } else {
            worldmapGridInputPosition.vx += -0x4C400;
            worldmapGridInputPosition.vz += -0x65580;
        }
        checkWorldmapPositionSub0(&e->m28_position);
        worldmapCameraVar0 += 0x20000;
        worldmapRotation.vy += 0x20;
        worldmapRotation.vx -= 4;
        break;
    case 4:
        worldmapRotation.vy += e->m5C >> 12;
        e->m5C -= 0x800;
        if (e->m5C < 0) {
            e->m5C = 0;
            e->m20_subState = 0;
        }
        break;
    case 5:
        e->m7C -= 0x2000;
        if (e->m7C < 0x1000) {
            e->m7C = 0x1000;
            e->m20_subState = 0;
        }
        break;
    case 16:
        worldmapRadarPosition3.vx += -0x42D00;
        worldmapRadarPosition3.vz += 0x6D300;
        if (worldmapRadarPosition3.vx < 0x1F9E000 && worldmapRadarPosition3.vz > 0x5998000) {
            worldmapRadarPosition3.vx = 0x1F9E000;
            worldmapRadarPosition3.vz = 0x5998000;
            e->m20_subState = 0x11;
            e->m7C = 0x8000;
        } else {
            worldmapGridInputPosition.vx += -0x42D00;
            worldmapGridInputPosition.vz += 0x6D300;
        }
        checkWorldmapPositionSub0(&e->m28_position);
        worldmapRotation.vy -= 8;
        worldmapRotation.vx += e->m5C >> 12;
        e->m5C += 0x300;
        worldmapCameraVar0 += 0x8000;
        if (worldmapRotation.vx > 0x80) {
            worldmapRotation.vx = 0x80;
        }
        break;
    case 17:
        e->m7C -= 0x200;
        if (e->m7C < 0x1000) {
            e->m7C = 0x1000;
        }
        break;
    case 24:
        worldmapRadarPosition3.vx += -0x42D00;
        worldmapRadarPosition3.vz += 0x6D300;
        if (worldmapRadarPosition3.vx < 0x2152000 && worldmapRadarPosition3.vz > 0x5AA3000) {
            worldmapRadarPosition3.vx = 0x2152000;
            worldmapRadarPosition3.vz = 0x5AA3000;
            e->m20_subState = 0x19;
            e->m60 = 0x10000;
        } else {
            worldmapGridInputPosition.vx += -0x42D00;
            worldmapGridInputPosition.vz += 0x6D300;
        }
        checkWorldmapPositionSub0(&e->m28_position);
        worldmapRotation.vy -= 8;
        worldmapRotation.vx += e->m5C >> 12;
        e->m5C += 0x300;
        worldmapCameraVar0 += 0x8000;
        if (worldmapRotation.vx > 0x80) {
            worldmapRotation.vx = 0x80;
        }
        break;
    case 25:
        worldmapRotation.vy -= e->m60 >> 12;
        e->m60 -= 0x200;
        if (e->m60 < 0) {
            e->m60 = 0;
        }
        break;
    }
    r = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    ((SVECTOR*)(scratch + 0xA0))->vy = r;
    worldmapCameraVector.m0_eye.vy += r;
    worldmapCameraVector.m8_target.vy += ((SVECTOR*)(scratch + 0xA0))->vy;
    return 1;
}

void setWorldmapModelSemiTransBlue(sWorldmapModel* m, POLY_FT4* p, s32 n, s32 abr) {
    s32 i;

    for (i = 0; i < n; i++, p++) {
        setlen(p, 9);
        setcode(p, 0x2C);
        p->tpage = GetTPage(0, abr, 0x300, 0x100);
        p->clut = GetClut(0, 0x1FF);
        p->r0 = 0x3C;
        p->g0 = 0x3C;
        p->code |= 2;
        p->b0 = 0xC0;
    }
    memcpy(m->m48_primBuffers[1], m->m48_primBuffers[0], n * sizeof(POLY_FT4));
}

s32 worldmapMode15_taskProjectile_init(s32 index) {
    sWorldmapModel* models;
    sWorldmapModel* m;
    sWorldmapStateEntry* e;

    attachWorldmapModels(1, 2);
    attachWorldmapModels(1, 3);
    models = worldmapModels;
    e = &gWorldmapState->m0_entries[index];
    setWorldmapModelSemiTransBlue(&models[1], (POLY_FT4*)models[1].m48_primBuffers[0], models[1].m40_modelBlock->m4_numPrims, 3);
    setWorldmapModelSemiTransBlue(&models[2], (POLY_FT4*)models[2].m48_primBuffers[0], models[2].m40_modelBlock->m4_numPrims, 3);
    setWorldmapModelSemiTransBlue(&models[3], (POLY_FT4*)models[3].m48_primBuffers[0], models[3].m40_modelBlock->m4_numPrims, 1);
    m = &worldmapModels[1];
    m->m0_hidden = 0;
    m->m18_rotation.vz = 0;
    m->m18_rotation.vy = 0;
    m->m18_rotation.vx = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    e->m38_step.vx = -0x85A;
    e->m20_subState = 0;
    e->m38_step.vy = 0;
    e->m38_step.vz = 0xDA6;
    e->m50 = worldmapMode15ProjectileStopPoints[worldmapInitialPositionIndex].vx << 12;
    e->m54 = worldmapMode15ProjectileStopPoints[worldmapInitialPositionIndex].vz << 12;
    e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680;
    e->m28_position.vy = worldmapPosition.vy;
    e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680;
    return 1;
}

s32 worldmapMode15_taskProjectile_update(s32 index) {
    s32 ret = 1;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[1];
    sWorldmapScratch* sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);

    if (e->m4_request != 0) {
        e->m38_step.vx = -0x85A;
        e->m38_step.vy = 0;
        e->m38_step.vz = 0xDA6;
        e->m50 = worldmapMode15ProjectileStopPoints[worldmapInitialPositionIndex].vx << 12;
        e->m54 = worldmapMode15ProjectileStopPoints[worldmapInitialPositionIndex].vz << 12;
    }
    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 0;
        e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680;
        e->m28_position.vy = worldmapPosition.vy;
        e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 0;
        e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680 + e->m38_step.vx * 0x2D00;
        e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680 + e->m38_step.vz * 0x2D00;
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 0;
        e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680 + e->m38_step.vx * 0x1E00;
        e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680 + e->m38_step.vz * 0x1E00;
        break;
    case 4:
        e->m4_request = 0;
        e->m20_subState = 1;
        e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680 + e->m38_step.vx * 0x3300;
        e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680 + e->m38_step.vz * 0x3300;
        break;
    case 5:
        e->m20_subState = 2;
        e->m38_step.vx = -0x988;
        e->m38_step.vy = 0x227;
        e->m4_request = 0;
        e->m38_step.vz = -0xCAB;
        e->m28_position.vx = 0x1F9E000;
        e->m28_position.vz = 0x5998000;
        break;
    case 16:
        e->m4_request = 0;
        e->m20_subState = 0x10;
        e->m28_position.vx = e->m38_step.vx * 0x3300 + 0x56F2000;
        e->m28_position.vz = e->m38_step.vz * 0x3300 + 0x7F2C000;
        break;
    case 24:
        e->m4_request = 0;
        e->m20_subState = 0x18;
        e->m28_position.vx = worldmapPosition.vx - e->m38_step.vx * 0x3680 + e->m38_step.vx * 0x3300;
        e->m28_position.vz = worldmapPosition.vz - e->m38_step.vz * 0x3680 + e->m38_step.vz * 0x3300;
        break;
    }
    e->m28_position.vx += e->m38_step.vx << 7;
    e->m28_position.vy += e->m38_step.vy << 7;
    e->m28_position.vz += e->m38_step.vz << 7;
    switch (e->m20_subState) {
    case 0:
        if (e->m28_position.vx < e->m50 && e->m28_position.vz > e->m54) {
            e->m28_position.vx = (s32)e->m50;
            e->m28_position.vz = (s32)e->m54;
            changeWorldmapEntityState(4, 2);
            changeWorldmapEntityState(5, 2);
            changeWorldmapEntityState(6, 2);
            changeWorldmapEntityState(7, 2);
            changeWorldmapEntityState(8, 2);
        }
        break;
    case 1:
        if (e->m28_position.vx < 0x1F9E000 && e->m28_position.vz > 0x5998000) {
            e->m28_position.vx = 0x1F9E000;
            e->m28_position.vz = 0x5998000;
            e->m20_subState = 0x40;
            sp->mA0.vx = e->m28_position.vx >> 12;
            sp->mA0.vy = e->m28_position.vy >> 12;
            sp->mA0.vz = e->m28_position.vz >> 12;
            spawnWorldmapParticles(0x20, &sp->mA0, 0);
        }
        break;
    case 2:
        if (e->m28_position.vy > -0x80000) {
            sp->mA0.vx = 0x1498;
            sp->mA0.vz = 0x4AF2;
            sp->mA0.vy = worldmapGetAltitudeFor2dPoint(0x1498000, 0x4AF2000) >> 12;
            spawnWorldmapParticles(0x25, &sp->mA0, 0);
            spawnWorldmapParticles(0x26, &sp->mA0, 0);
            spawnWorldmapParticles(0x27, &sp->mA0, 0);
            e->m20_subState = 3;
        }
        break;
    case 3:
        if (e->m28_position.vy > 0x100000) {
            e->m28_position.vx -= e->m38_step.vx << 7;
            e->m28_position.vy -= e->m38_step.vy << 7;
            e->m28_position.vz -= e->m38_step.vz << 7;
            m[2].m0_hidden = 1;
            m[1].m0_hidden = 1;
            m[0].m0_hidden = 1;
            e->m20_subState = 0x41;
        }
        break;
    case 16:
        if (e->m28_position.vx < 0x1F9E000 && e->m28_position.vz > 0x5998000) {
            e->m20_subState = 0x11;
            sp->mA0.vx = 0x1F9E;
            sp->mA0.vz = 0x5998;
            sp->mA0.vy = e->m28_position.vy >> 12;
            spawnWorldmapParticles(0x20, &sp->mA0, 0);
            spawnWorldmapParticles(0x21, &sp->mA0, 0);
        }
        break;
    case 17:
        if (e->m28_position.vx < 0x199D000 && e->m28_position.vz > 0x6367000) {
            e->m28_position.vx = 0x199D000;
            e->m28_position.vz = 0x6367000;
            m[2].m0_hidden = 1;
            m[1].m0_hidden = 1;
            m[0].m0_hidden = 1;
            changeWorldmapEntityState(4, 5);
            changeWorldmapEntityState(5, 5);
            changeWorldmapEntityState(6, 5);
            changeWorldmapEntityState(7, 5);
            changeWorldmapEntityState(8, 5);
            ret = 3;
        }
        break;
    case 24:
        if (e->m28_position.vx < 0x1EB5000 && e->m28_position.vz > 0x5EE6000) {
            m[2].m0_hidden = 1;
            m[1].m0_hidden = 1;
            m[0].m0_hidden = 1;
            changeWorldmapEntityState(4, 2);
            changeWorldmapEntityState(5, 2);
            changeWorldmapEntityState(6, 2);
            changeWorldmapEntityState(7, 2);
            changeWorldmapEntityState(8, 2);
            e->m20_subState = 0x41;
        }
        break;
    case 64:
        e->m28_position.vx = 0x1F9E000;
        e->m28_position.vz = 0x5998000;
        break;
    case 65:
        ret = 3;
        break;
    }
    checkWorldmapPositionSub0(&e->m28_position);
    m->m8_position.vx = e->m28_position.vx >> 12;
    m->m8_position.vy = e->m28_position.vy >> 12;
    m->m8_position.vz = e->m28_position.vz >> 12;
    switch (e->m20_subState) {
    case 0:
    case 1:
    case 16:
    case 24:
        sp->mF0_rotation.m[0][0] = 0xDA6;
        sp->mF0_rotation.m[0][1] = 0;
        sp->mF0_rotation.m[0][2] = 0x85A;
        sp->mF0_rotation.m[1][0] = 0;
        sp->mF0_rotation.m[1][1] = -0x1000;
        sp->mF0_rotation.m[1][2] = 0;
        sp->mF0_rotation.m[2][0] = -0x85A;
        sp->mF0_rotation.m[2][1] = 0;
        sp->mF0_rotation.m[2][2] = 0xDA6;
        m->m18_rotation.vz = (m->m18_rotation.vz + 0x100) & 0xFFF;
        RotMatrixYXZ(&m->m18_rotation, &sp->m110);
        MulMatrix0(&sp->mF0_rotation, &sp->m110, &m->m20_rotationMatrix);
        sp->m0.vy = 0x800;
        sp->m0.vx = 0x800;
        sp->m0.vz = 0x1800;
        ScaleMatrix(&m->m20_rotationMatrix, &sp->m0);
        sp->mA0.vx = e->m28_position.vx >> 12;
        sp->mA0.vy = e->m28_position.vy >> 12;
        sp->mA0.vz = e->m28_position.vz >> 12;
        sp->mA8.vz = 0;
        sp->mA8.vx = 0;
        sp->mA8.vy = ratan2(0x85A, 0xDA6) & 0xFFF;
        spawnWorldmapParticles(0x22, &sp->mA0, &sp->mA8);
        break;
    case 4:
    case 17:
    case 65:
        clearWorldmapParticles(0x22);
        break;
    case 2:
    case 3:
        sp->m0.vx = 0x988;
        sp->m0.vy = -0x227;
        sp->m0.vz = -0xCAB;
        sp->m10.vz = 0;
        sp->m10.vx = 0;
        sp->m10.vy = 0x1000;
        OuterProduct12(&sp->m10, &sp->m0, &sp->m20);
        VectorNormal(&sp->m20, &sp->m20);
        OuterProduct12(&sp->m0, &sp->m20, &sp->m10);
        VectorNormal(&sp->m10, &sp->m10);
        sp->m150_directionBasis.m[0][0] = sp->m20.vx;
        sp->m150_directionBasis.m[0][1] = sp->m20.vy;
        sp->m150_directionBasis.m[0][2] = sp->m20.vz;
        sp->m150_directionBasis.m[1][0] = sp->m10.vx;
        sp->m150_directionBasis.m[1][1] = sp->m10.vy;
        sp->m150_directionBasis.m[1][2] = sp->m10.vz;
        sp->m150_directionBasis.m[2][0] = sp->m0.vx;
        sp->m150_directionBasis.m[2][1] = sp->m0.vy;
        sp->m150_directionBasis.m[2][2] = sp->m0.vz;
        extractWorldmapEulerAngles(&sp->m150_directionBasis, &sp->mA8);
        TransposeMatrix(&sp->m150_directionBasis, &sp->mF0_rotation);
        m->m18_rotation.vz = (m->m18_rotation.vz + 0x100) & 0xFFF;
        RotMatrixYXZ(&m->m18_rotation, &sp->m110);
        MulMatrix0(&sp->mF0_rotation, &sp->m110, &m->m20_rotationMatrix);
        sp->m0.vy = 0x800;
        sp->m0.vx = 0x800;
        sp->m0.vz = 0x1800;
        ScaleMatrix(&m->m20_rotationMatrix, &sp->m0);
        sp->mA0.vx = e->m28_position.vx >> 12;
        sp->mA0.vy = e->m28_position.vy >> 12;
        sp->mA0.vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0x22, &sp->mA0, &sp->mA8);
        break;
    }
    return ret;
}

s32 worldmapMode15_taskTrailRing_init(s32 index) {
    s32 k = index - 4;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[k + 4];
    s32 abr;

    abr = 3;
    if (k == 4) {
        abr = 1;
    }
    setWorldmapModelSemiTransBlue(m, (POLY_FT4*)m->m48_primBuffers[0], m->m40_modelBlock->m4_numPrims, abr);
    e->m38_step.vx = -0x85A;
    e->m38_step.vz = 0xDA6;
    e->m20_subState = 0;
    e->m38_step.vy = 0;
    e->m5C = worldmapMode15TrailRingScales[k];
    e->m22_timer = 0x3C;
    return 1;
}

s32 worldmapMode15_taskTrailRing_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapStateEntry* ship = &gWorldmapState->m0_entries[3];
    sWorldmapModel* m = &worldmapModels[index];
    sWorldmapScratch* sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    VECTOR* scale;
    s32 unused[2]; /* reserves retail's stack frame */

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 1;
        break;
    case 3:
        e->m22_timer = 0x3C;
        e->m38_step.vx = -0x85A;
        e->m38_step.vz = 0xDA6;
        e->m4_request = 0;
        e->m20_subState = 0;
        e->m38_step.vy = 0;
        e->m5C = worldmapMode15TrailRingScales[index - 4];
        m->m0_hidden = 0;
        m->m18_rotation.vz = 0;
        m->m18_rotation.vy = 0;
        m->m18_rotation.vx = 0;
        RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
        clearWorldmapParticles(0x23);
        clearWorldmapParticles(0x24);
        break;
    case 4:
        e->m22_timer = 4;
        e->m20_subState = 1;
        e->m4_request = 0;
        e->m5C = worldmapMode15TrailRingScales[index - 4];
        m->m0_hidden = 0;
        m->m18_rotation.vz = 0;
        m->m18_rotation.vy = 0;
        m->m18_rotation.vx = 0;
        RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
        clearWorldmapParticles(0x23);
        clearWorldmapParticles(0x24);
        break;
    case 5:
        e->m20_subState = 1;
        e->m4_request = 0;
        e->m22_timer = 0x1E;
        break;
    }
    switch (e->m20_subState) {
    case 0:
        e->m28_position.vx = ship->m28_position.vx;
        e->m28_position.vy = ship->m28_position.vy;
        e->m28_position.vz = ship->m28_position.vz;
        sp->mA0.vx = (e->m28_position.vx + 0x42D000) >> 12;
        sp->mA0.vy = e->m28_position.vy >> 12;
        sp->mA0.vz = (e->m28_position.vz - 0x6D3000) >> 12;
        sp->mA8.vz = 0;
        sp->mA8.vx = 0;
        sp->mA8.vy = ratan2(0x85A, 0xDA6) & 0xFFF;
        spawnWorldmapParticles(0x23, &sp->mA0, &sp->mA8);
        spawnWorldmapParticles(0x24, &sp->mA0, &sp->mA8);
        break;
    case 1:
        e->m22_timer--;
        if (e->m22_timer < 0) {
            e->m22_timer = 0;
            e->m5C -= 0x80;
        }
        break;
    }
    sp->mF0_rotation.m[0][0] = 0xDA6;
    sp->mF0_rotation.m[0][1] = 0;
    sp->mF0_rotation.m[0][2] = 0x85A;
    sp->mF0_rotation.m[1][0] = 0;
    sp->mF0_rotation.m[1][1] = -0x1000;
    sp->mF0_rotation.m[1][2] = 0;
    sp->mF0_rotation.m[2][0] = -0x85A;
    sp->mF0_rotation.m[2][1] = 0;
    sp->mF0_rotation.m[2][2] = 0xDA6;
    m->m18_rotation.vz = (m->m18_rotation.vz + 0x100) & 0xFFF;
    RotMatrixYXZ(&m->m18_rotation, &sp->m110);
    MulMatrix0(&sp->mF0_rotation, &sp->m110, &m->m20_rotationMatrix);
    m->m8_position.vx = e->m28_position.vx >> 12;
    m->m8_position.vy = e->m28_position.vy >> 12;
    m->m8_position.vz = e->m28_position.vz >> 12;
    scale = &sp->m0;
    scale->vx = scale->vy = e->m5C;
    scale->vz = 0x2000;
    ScaleMatrix(&m->m20_rotationMatrix, scale);
    if (e->m5C < 0) {
        e->m5C = 0;
        m->m0_hidden = 1;
        return 3;
    }
    return 1;
}

/* needs setWorldmapModelSemiTransGrey declared returning s32: the call then sets v0, so the scheduler does not tie `li v0,3` to the end of the block */
s32 worldmapMode15_taskImpactRings_init(s32 index) {
    sWorldmapModel* models = worldmapModels;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    setWorldmapModelSemiTransGrey(&models[9], (POLY_FT4*)models[9].m48_primBuffers[0], models[9].m40_modelBlock->m4_numPrims);
    setWorldmapModelSemiTransGrey(&models[10], (POLY_FT4*)models[10].m48_primBuffers[0], models[10].m40_modelBlock->m4_numPrims);
    e->m28_position.vx = 0x1498000;
    e->m28_position.vy = -0x80000;
    e->m54 = -0x800;
    e->m20_subState = 0;
    e->m28_position.vz = 0x4AF2000;
    e->m50 = 0;
    e->m58 = 0x80;
    return 3;
}
