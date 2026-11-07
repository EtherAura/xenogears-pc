/*
 * Sky gradient, cloud layer, minimap, ground track marks, animated textures,
 * random encounters, menu hand-over, event script opcodes, camera smoothing and three
 * scripted world map modes with their tasks.
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "pc_interpolation.h"
#include "gte.h"
#include "main/game_state.h"
#include "main/dialog.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sound.h"
#include "worldmap/worldmap.h"
#include "field_mecha/mecha.h"
#include "pc_gameplay.h"

extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
typedef struct { POLY_FT4 m[16]; } MarkPolys16; /* 16 mark sprites, copied as one block */
extern MarkPolys16* worldmapTrackMarkPolys[2]; /* [odd/even] */
extern s32 worldmapTrackMarkIndex;
extern sWorldmapSeqHeader* fieldSeq;
extern sWorldmapTrackMark* worldmapTrackMarks;
s32 changeWorldmapEntityState(s32 index, s32 state);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void clearWorldmapParticles(s32 type);
void killWorldmapParticles(s32);
void playSoundEffectInFreeSlot(s32);
void fadeSoundEffectVolume(s32, s32, s32);
extern POLY_G4 worldmapSkyGradientPolys[4][2]; /* sky gradient quads [band][odd/even] */
extern SVECTOR worldmapSkyGradientVertices[4][4]; /* sky gradient vertices */
extern SVECTOR worldmapRotation;
extern MATRIX worldmapMainMatrix2;
extern s32 worldmapOddOrEven;
extern s32 gDepthDivider; /* OT depth shift */
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern sWorldmapState* gWorldmapState;
extern POLY_FT4 worldmapCloudBandPolys[2][2]; /* cloud layer quads [layer][odd/even] */
extern DR_TWIN worldmapCloudBandTexWindows[2];
extern POLY_FT4 worldmapMinimapBackgroundPoly[2];
extern DR_TPAGE worldmapMinimapDrTpage;
extern POLY_G3 worldmapMinimapRadar[2][4]; /* minimap cursor triangles [odd/even][4] */
extern TILE worldmapMinimapPOIsPolys[2][32]; /* minimap marker dots [odd/even][32] */
extern SVECTOR worldmapMinimapRadarShape[4][3];
extern VECTOR worldmapRadarPosition;
extern s32 worldmapMinimapScale;
extern s32 worldmapGeometryOffsetY;
extern s16 worldmapMinimapPOIsCoordinates[64]; /* minimap marker x,y pairs */
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
extern MATRIX worldMapIdentityMatrix;
extern VECTOR worldmapRadarPosition3;
s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z);
void interpolateWorldPosition(VECTOR* out, s32 x, s32 z);
extern s32 worldmapTexAnims1Count;
extern sWorldmapTexAnim* worldmapTexAnims1;
extern sWorldmapTexAnimDef worldmapTexAnims1Defs[2]; /* the worldmap file has 2 */
extern s32 worldmapTexAnims2Count;
extern sWorldmapTexAnim* worldmapTexAnims2;
extern sWorldmapTexAnimDef worldmapTexAnims2Defs[];
extern s16 worldmapEncounterCountdowns[16]; /* random encounter countdowns */
extern s32 worldmapEncounterTimer;
extern s32 worldmapEncounterPeriod;
extern s32 worldmapEncounterTimerCount;
extern s32 worldmapEncounterTriggered;
extern sWorldmapMenuBackup overlayStateSaveBlock;
extern VECTOR worldmapPosition;
extern s16 worldmapPlayerDirection;
extern sWorldmapFollowLeader worldmapFollowLeaderData[32];
extern s16 worldmapFollowLeaderDataIndex;
extern s32 worldmapCameraVar0;
extern VECTOR worldmapGridInputPosition;
extern SVECTOR worldmapGridPosition;
/* Copies an SVECTOR as two words. */
#define COPY_WORDS2(d, s) (((s32*)&(d))[0] = ((s32*)&(s))[0], ((s32*)&(d))[1] = ((s32*)&(s))[1])
extern u8 gearSwitchDisabled;
extern s32 worldmapSavedGearSwitchDisabled;
extern void* worldmapMenuOverlayBuffer;
extern void* worldmapMenuVramBackup1;
extern void* worldmapMenuVramBackup2;
extern void* worldmapMenuOverlayFile;
s16 getWorldmapGroundType(VECTOR* position);
void waitWorldmapStreamingDone(void);
void freeWorldmapPolyFT4Array3(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapPolyFT4Array1(void);
void startLoadingWorldmapMenuFiles(void);
void worldmapFadeIn(s32, s32, s32, s32);
extern void* menuSharedResources;
extern s32 worldmapFile3;
extern void* worldmapFile3Buffer;
extern s32 worldmapExitVar2;
void initWorldmapGraphics(void);
void loadWorldmapTextureFromFile3(void);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void createWorlmapPolyFT4Array3(void);
void applyWorldmapGearBoardingChanges(void);
void resetInputs(void);
extern s16 worldmapEncounterZoneRemap[];
extern u16 worldmapEncounterScenarioThresholds[5]; /* scenario thresholds */
extern sWorldmapEncounterTable* worldmapFile1Buffer_2C_data[16]; /* encounter tables */
extern sWorldmapBattleSetup battleConfigs;
extern u8 requestedBattleConfig;
s32 getWorldmapEncounterZone(VECTOR* pos);
extern void* mechaFinalizeVar0;
extern MATRIX worldmapColorMatrix;
extern MATRIX worldmapLightMatrix;
void adjustWorldModelLocation(VECTOR* pos);
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern u16 worldmapInput1_0;
extern u16 worldmapInput1_1;
extern u16 worldmapInput2_0;
extern u16 worldmapInput2_1;
extern u16 worldmapInput3_0;
extern u16 worldmapInput3_1;
extern u16 newPadButtonForScripts;
extern u16 newPadButtonForScripts2;
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForDialogs2;
extern u16 newPadButtonForField;
extern u16 newPadButtonForField2;
extern s32 g_playTimeInVsync;
void EnterCriticalSection(void);
void FlushCache(void);
void ExitCriticalSection(void);
void pauseMusic(void);
void resumeMusic(void);
void uploadBuiltinTexture(s32 x, s32 y);
s32 loadInputFromVSyncBuffer(void);
s32 isControllerConnected(s32 port);
extern u8 battleMusic[];
void stopAllMusicInstances(void);
void* createMusicInstance(void* seq);
void startMusicInstance(void* inst, s32 volume, s32 arg2);
/* World map file 1 (decompressed in place): header of offsets. */
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern Model* worldmapModelFiles;
extern u16* pWorldmapModelsConfig;
extern s32* worldmapFile1Buffer_C; /* collision mesh offsets */
extern sWorldmapParticle* worldmapFile1Buffer_18; /* particle emitters (0x200) */
extern sWorldmapOffsetEntry* worldmapFile1Buffer_14; /* 0x100 entries, relocated by initWorldmapBillboards */
extern s32* worldmapFile1Buffer_20; /* animated texture set 1: count, then offsets */
extern s32* worldmapFile1Buffer_24; /* animated texture set 2: count, then offsets */
extern s32 worldmapMatrixMode;
extern s16 worldmapGridUpdateMask;
extern s32 worldmapWaterWave;
extern sWorldmapCameraState worldmapCameraVector;
extern WorldmapScriptOp worldmapScriptOpcodes[]; /* script opcode table */
void setupWorldmapMatrix1(SVECTOR* eye);
void setupWorldmapMatrix2(sWorldmapCameraState* cam);
void updateWorldmapParticleEmitters(void);
void drawWorlmapPolyFT4Array1(void);
void drawWorldmapBillboards(void);
void renderWorldmapModels(void);
void setWorldmapGridUpdateMask(VECTOR* position);
void updateWorldmapGrids(VECTOR*);
void streamWorldmap0(void);
void streamWorldmap1(void);
void worldmapGroundPrepareRenderingTable(VECTOR*);
void drawWorldmapGround(OTag* ot, POLY_FT3* polys, VECTOR*);
void drawWorldmapSkyCloudBand(void);
void drawWorldmapSkyGradient(void);
void drawWorldmapClouds(void);
void computeWormapSpritePosition(VECTOR* v);
extern MATRIX D_8009BE4C;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern s32 worldmapExitToFieldVar0;
void finalizeWorldMapFileLoading(void);
void allocAndInitWorldmapState(void);
void moveWorldmapClouds(void);
void worldmapDotInit(void);
void worldmapInitAllModels(void);
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
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
s32 worldmap_taskFader_init(s32);
s32 worldmap_taskFader_update(s32);
s32 worldmapMode8_taskPadCamera_init(s32);
s32 worldmapMode8_taskPadCamera_update(s32);
s32 worldmap_taskSpinModels_init(s32);
s32 worldmap_taskSpinModels_update(s32);
s32 worldmap_taskGround_init(s32);
s32 worldmap_taskGround_update(s32);
void initWorldmapEncounterTimers(void);
void freeWorldmapModels(void);
void freeWorldmapClouds(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapChunks(void);
void freeWorldmapState(void);
void computeCameraVector(sWorldmapCameraState* lookAt, VECTOR* pos, s32 dist, SVECTOR* rot);
void finalizeWorldmapEventFileLoading(void);
void readWorldmapSequenceFile(void);
void loadSequence(void* data);
s32 worldmapMode9_taskPathCamera_init(s32);
s32 worldmapMode9_taskPathCamera_update(s32);
s32 worldmapMode9_taskAirship_init(s32);
s32 worldmapMode9_taskAirship_update(s32);
s32 worldmap_taskSceneNoBillboards_init(s32);
s32 worldmap_taskSceneNoBillboards_update(s32);
void stopAllSounds(void);
void unloadSequence(void* data);
void attachWorldmapModels(s32, s32);
s32 worldmapMode10_taskCamera_init(s32);
s32 worldmapMode10_taskCamera_update(s32);
s32 worldmapMode10_taskAirship_init(s32);
s32 worldmapMode10_taskAirship_update(s32);
s32 worldmapMode10_taskRings_init(s32);
s32 worldmapMode10_taskRings_update(s32);
s32 worldmapMode10_taskTrail_init(s32);
s32 worldmapMode10_taskTrail_update(s32);
s32 worldmapMode10_taskBurst_init(s32);
s32 worldmapMode10_taskBurst_update(s32);
s32 worldmapMode10_taskGroundModel_init(s32);
s32 worldmapMode10_taskGroundModel_update(s32);
typedef struct {
    /* 0x00 */ u8 m0[0xA0];
    /* 0xA0 */ SVECTOR mA0_shake;
} ShakeScratch;
extern sWorldmapModel* worldmapModels;
void checkWorldmapPositionSub0(VECTOR*);
s32 getWorldmapElevationWithWater(s32 x, s32 z);
typedef struct {
    /* 0x000 */ VECTOR m0_position;
    /* 0x010 */ u8 m10[0xA0 - 0x10];
    /* 0x0A0 */ SVECTOR mA0_rot[4];
    /* 0x0C0 */ u8 mC0[0xF0 - 0xC0];
    /* 0x0F0 */ MATRIX mF0_rotations[4];
} ShipScratch;
extern SVECTOR worldmapPathCameraPoints[]; /* camera flight path control points (pad = -1 ends) */
void extractWorldmapEulerAngles(MATRIX*, SVECTOR*);
s32 computeWorldmapDistance2D(VECTOR*, VECTOR*);
void setSoundEffectVolume(s32, s32);
typedef struct {
    /* 0x00 */ VECTOR m0;
    /* 0x10 */ VECTOR m10_eyePosition;
    /* 0x20 */ s32 m20_distance;
    /* 0x24 */ u8 m24[0xA0 - 0x24];
    /* 0xA0 */ SVECTOR mA0_controlPoints[3];
} PathScratch;
#define PATH_SCRATCH ((PathScratch*)0x1F800000)

#define POLY_G4_RGB(p, n) (*(u32*)&(p).r##n) /* r,g,b,pad/code written as one word */
void initWorldmapSkyGradient(void) {
    POLY_G4_RGB(worldmapSkyGradientPolys[0][0], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[0][0], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[0][1], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[0][1], 1) = 0xFF7A70;
    POLY_G4_RGB(worldmapSkyGradientPolys[0][0], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[0][0], 3) = POLY_G4_RGB(worldmapSkyGradientPolys[0][1], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[0][1], 3) = 0xFFF5E0;
    setlen(&worldmapSkyGradientPolys[0][0], 8);
    setcode(&worldmapSkyGradientPolys[0][0], 0x38);
    setlen(&worldmapSkyGradientPolys[0][1], 8);
    setcode(&worldmapSkyGradientPolys[0][1], 0x38);
    POLY_G4_RGB(worldmapSkyGradientPolys[1][0], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[1][0], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[1][1], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[1][1], 1) = 0xC03745;
    POLY_G4_RGB(worldmapSkyGradientPolys[1][0], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[1][0], 3) = POLY_G4_RGB(worldmapSkyGradientPolys[1][1], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[1][1], 3) = 0xFF7A70;
    setlen(&worldmapSkyGradientPolys[1][0], 8);
    setcode(&worldmapSkyGradientPolys[1][0], 0x38);
    setlen(&worldmapSkyGradientPolys[1][1], 8);
    setcode(&worldmapSkyGradientPolys[1][1], 0x38);
    POLY_G4_RGB(worldmapSkyGradientPolys[2][0], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[2][0], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[2][0], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[2][0], 3) =
        POLY_G4_RGB(worldmapSkyGradientPolys[2][1], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[2][1], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[2][1], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[2][1], 3) = 0xC03745;
    setlen(&worldmapSkyGradientPolys[2][0], 8);
    setcode(&worldmapSkyGradientPolys[2][0], 0x38);
    setlen(&worldmapSkyGradientPolys[2][1], 8);
    setcode(&worldmapSkyGradientPolys[2][1], 0x38);
    POLY_G4_RGB(worldmapSkyGradientPolys[3][0], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[3][0], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[3][0], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[3][0], 3) =
        POLY_G4_RGB(worldmapSkyGradientPolys[3][1], 0) = POLY_G4_RGB(worldmapSkyGradientPolys[3][1], 1) = POLY_G4_RGB(worldmapSkyGradientPolys[3][1], 2) = POLY_G4_RGB(worldmapSkyGradientPolys[3][1], 3) = 0xC03745;
    setlen(&worldmapSkyGradientPolys[3][0], 8);
    setcode(&worldmapSkyGradientPolys[3][0], 0x38);
    setlen(&worldmapSkyGradientPolys[3][1], 8);
    setcode(&worldmapSkyGradientPolys[3][1], 0x38);
}

/* Scratchpad layout of drawWorldmapSkyGradient. */
typedef struct {
    /* 0x00 */ SVECTOR m0_rot;
    /* 0x08 */ MATRIX m8_matrix;
    /* 0x28 */ MATRIX m28_rotMatrix;
    /* 0x48 */ s32 m48_p;
    /* 0x4C */ s32 m4C_flag;
} SkyScratch;

void pcWideWorldmapSkyBand(s32 band, SVECTOR* v, POLY_G4* p);
void pcWideWorldmapCloudBand(SVECTOR* v, POLY_FT4* left, POLY_FT4* right, OTag* ot);

/* sky gradient render (RotTransPers4) */
void drawWorldmapSkyGradient(void) {
    s32 i;
    SVECTOR* v;
    POLY_G4* p;
    s32 z;
    SkyScratch* sp = (SkyScratch*)0x1F800000;

    sp->m0_rot.vx = sp->m0_rot.vz = 0;
    sp->m0_rot.vy = worldmapRotation.vy;
    RotMatrixYXZ(&sp->m0_rot, &sp->m28_rotMatrix);
    sp->m28_rotMatrix.t[0] = sp->m28_rotMatrix.t[1] = sp->m28_rotMatrix.t[2] = 0;
    CompMatrix(&worldmapMainMatrix2, &sp->m28_rotMatrix, &sp->m8_matrix);
    SetRotMatrix(&sp->m8_matrix);
    SetTransMatrix(&sp->m8_matrix);
    for (v = worldmapSkyGradientVertices[0], i = 0; i < 4; i++, v += 4) {
        p = &worldmapSkyGradientPolys[i][worldmapOddOrEven];
        z = RotTransPers4(&v[0], &v[1], &v[2], &v[3], (s32*)&p->x0, (s32*)&p->x1, (s32*)&p->x2, (s32*)&p->x3,
                          &sp->m48_p, &sp->m4C_flag);
        if (sp->m4C_flag >= 0) {
            pcInterpolationKeyType(worldmapSkyGradientPolys, worldmapSkyGradientPolys, i, p, PC_INTERP_MODEL);
            addPrim(&pCurrentWorldmapRenderingStruct->m70_OT[z >> gDepthDivider], p);
            /* Pieces beside the band for the wide picture (port/src/game/worldmap/wide.c). */
            pcWideWorldmapSkyBand(i, v, p);
        }
    }
}

/* cloud layer POLY_FT4 + DR_TWIN init */
void initWorldmapSkyCloudBand(void) {
    RECT rect;
    u16 tpage;
    u16 clut;
    s32 i;
    POLY_FT4* p;

    tpage = GetTPage(0, 1, 0x380, 0x100);
    clut = GetClut(0x110, 0x1FE);
    for (p = worldmapCloudBandPolys[0], i = 0; i < 4; i++, p++) {
        setlen(p, 9);
        setcode(p, 0x2C);
        setRGB0(p, 0x30, 0x30, 0x30);
        setUVWH(p, 0, 0, 0xFF, 0x3F);
        p->tpage = tpage;
        p->clut = clut;
        SetSemiTrans(p, 1);
    }
    rect.x = 0;
    rect.y = 0;
    rect.w = 0x80;
    rect.h = 0;
    SetTexWindow(&worldmapCloudBandTexWindows[0], &rect);
    rect.x = 0;
    rect.y = 0;
    rect.w = 0;
    rect.h = 0;
    SetTexWindow(&worldmapCloudBandTexWindows[1], &rect);
}

extern SVECTOR worldmapCloudBandVertices[2][4]; /* cloud layer vertices */
#define UV(p, n) (*(u16*)&(p).u##n)
typedef struct {
    /* 0x00 */ SVECTOR m0_rot;
    /* 0x08 */ VECTOR m8;
    /* 0x18 */ MATRIX m18_matrix;
    /* 0x38 */ MATRIX m38_rotMatrix;
    /* 0x58 */ s32 m58_p;
    /* 0x5C */ s32 m5C_flag;
} CloudScratch;

void drawWorldmapSkyCloudBand(void) {
    s32 i;
    SVECTOR* v;
    s32 z;
    POLY_FT4* p;
    OTag* ot;
    s32 u;
    s32 u1;
    CloudScratch* sp = (CloudScratch*)0x1F800000;
    u16* rotY = (u16*)&worldmapRotation.vy;

    u = (*rotY >> 2) & 0x7F;
    u1 = u | 0x80;
    UV(worldmapCloudBandPolys[0][worldmapOddOrEven], 0) = UV(worldmapCloudBandPolys[1][worldmapOddOrEven], 0) = u;
    UV(worldmapCloudBandPolys[0][worldmapOddOrEven], 1) = UV(worldmapCloudBandPolys[1][worldmapOddOrEven], 1) = u1;
    UV(worldmapCloudBandPolys[0][worldmapOddOrEven], 2) = UV(worldmapCloudBandPolys[1][worldmapOddOrEven], 2) = u + 0x3F00;
    UV(worldmapCloudBandPolys[0][worldmapOddOrEven], 3) = UV(worldmapCloudBandPolys[1][worldmapOddOrEven], 3) = u1 + 0x3F00;
    sp->m0_rot.vx = sp->m0_rot.vz = 0;
    sp->m0_rot.vy = *rotY;
    RotMatrixYXZ(&sp->m0_rot, &sp->m38_rotMatrix);
    sp->m38_rotMatrix.t[0] = sp->m38_rotMatrix.t[1] = sp->m38_rotMatrix.t[2] = 0;
    CompMatrix(&worldmapMainMatrix2, &sp->m38_rotMatrix, &sp->m18_matrix);
    SetRotMatrix(&sp->m18_matrix);
    SetTransMatrix(&sp->m18_matrix);
    for (v = worldmapCloudBandVertices[0], i = 0; i < 2; i++, v += 4) {
        p = &worldmapCloudBandPolys[i][worldmapOddOrEven];
        z = RotTransPers4(&v[0], &v[1], &v[2], &v[3], (s32*)&p->x0, (s32*)&p->x1, (s32*)&p->x2, (s32*)&p->x3,
                          &sp->m58_p, &sp->m5C_flag);
    }
    if (sp->m5C_flag >= 0) {
        ot = &pCurrentWorldmapRenderingStruct->m70_OT[z >> gDepthDivider];
        addPrim(ot, &worldmapCloudBandTexWindows[1]);
        pcInterpolationKeyType(worldmapCloudBandPolys, worldmapCloudBandPolys, 0, &worldmapCloudBandPolys[0][worldmapOddOrEven], PC_INTERP_MODEL);
        addPrim(ot, &worldmapCloudBandPolys[0][worldmapOddOrEven]);
        pcInterpolationKeyType(worldmapCloudBandPolys, worldmapCloudBandPolys, 1, &worldmapCloudBandPolys[1][worldmapOddOrEven], PC_INTERP_MODEL);
        addPrim(ot, &worldmapCloudBandPolys[1][worldmapOddOrEven]);
        /* Pieces beside the band for the wide picture, inside its texture
         * window (port/src/game/worldmap/wide.c). */
        pcWideWorldmapCloudBand(worldmapCloudBandVertices[0], &worldmapCloudBandPolys[0][worldmapOddOrEven], &worldmapCloudBandPolys[1][worldmapOddOrEven], ot);
        addPrim(ot, &worldmapCloudBandTexWindows[0]);
    }
}

/* minimap (radar) primitives init */
void initWorldmapMinimap(void) {
    s32 i;
    POLY_G3* g;
    TILE* t;

    setlen(&worldmapMinimapBackgroundPoly[0], 9);
    setcode(&worldmapMinimapBackgroundPoly[0], 0x2C);
    setXYWH(&worldmapMinimapBackgroundPoly[0], 0xD0, 0x78, 0x67, 0x5F);
    setUVWH(&worldmapMinimapBackgroundPoly[0], 0, 0x80, 0x7F, 0x7F);
    setRGB0(&worldmapMinimapBackgroundPoly[0], 0x80, 0x80, 0x80);
    worldmapMinimapBackgroundPoly[0].tpage = GetTPage(0, 0, 0x380, 0x100);
    worldmapMinimapBackgroundPoly[0].clut = GetClut(0x100, 0x1FE);
    SetSemiTrans(&worldmapMinimapBackgroundPoly[0], 1);
    worldmapMinimapBackgroundPoly[1] = worldmapMinimapBackgroundPoly[0];
    SetDrawTPage(&worldmapMinimapDrTpage, 1, 0, GetTPage(0, 1, 0x380, 0x100));
    for (g = worldmapMinimapRadar[0], i = 0; i < 8; i++, g++) {
        setlen(g, 6);
        setcode(g, 0x30);
        setRGB0(g, 0xFF, 0x40, 0x40);
        setRGB1(g, 0, 0, 0);
        setRGB2(g, 0, 0, 0);
        SetSemiTrans(g, 1);
    }
    for (t = worldmapMinimapPOIsPolys[0], i = 0; i < 64; i++, t++) {
        setlen(t, 3);
        setcode(t, 0x60);
        setRGB0(t, 0x80, 0x80, 0x10);
        t->w = 2;
        t->h = 2;
    }
}

typedef struct {
    /* 0x000 */ u8 m0[0xB8];
    /* 0x0B8 */ SVECTOR mB8_rot;
    /* 0x0C0 */ u8 mC0[0xF0 - 0xC0];
    /* 0x0F0 */ MATRIX mF0_matrix;
} RadarScratch;

/* minimap render (GTE macros; markers from gameState) */
void worldmapRenderMinimap(void) {
    s32 i;
    SVECTOR* v;
    POLY_G3* p;
    TILE* t;
    u32 mask;
    RadarScratch* sp = (RadarScratch*)0x1F800000;

    p = worldmapMinimapRadar[worldmapOddOrEven];
    for (v = worldmapMinimapRadarShape[0], i = 0; i < 4; i++, v += 3, p++) {
        sp->mB8_rot.vx = sp->mB8_rot.vy = 0;
        sp->mB8_rot.vz = worldmapRotation.vy;
        createRotationMatrix(&sp->mB8_rot, &sp->mF0_matrix);
        sp->mF0_matrix.t[0] = (worldmapRadarPosition.vx >> 12) / 315 + 0x30;
        sp->mF0_matrix.t[2] = worldmapMinimapScale;
        sp->mF0_matrix.t[1] = 0x78 - worldmapGeometryOffsetY + (worldmapRadarPosition.vz >> 12) / 341;
        gte_SetRotMatrix(&sp->mF0_matrix);
        gte_SetTransMatrix(&sp->mF0_matrix);
        gte_ldv3(&v[0], &v[1], &v[2]);
        gte_rtpt();
        gte_stsxy3(&p->x0, &p->x1, &p->x2);
        pcInterpolationKeyType(worldmapMinimapRadar, worldmapMinimapRadar, i, p, PC_INTERP_MODEL);
        addPrim(pCurrentWorldmapRenderingStruct->m70_OT, p);
    }
    addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapMinimapDrTpage);
    mask = *(u32*)&gameState.m1930_fieldVarsBackup[254];
    for (t = worldmapMinimapPOIsPolys[worldmapOddOrEven], i = 0; i < 32; i++, t++) {
        if (mask & 1) {
            switch (i) {
            case 24:
                t->x0 = gameState.m182C_vehiclePosition[0] / 315 + 0xCF;
                t->y0 = gameState.m182C_vehiclePosition[2] / 341 + 0x77;
                break;
            case 25:
                t->x0 = gameState.m184E_flyingShipPosition[0] / 315 + 0xCF;
                t->y0 = gameState.m184E_flyingShipPosition[2] / 341 + 0x77;
                break;
            case 26:
                t->x0 = gameState.m1844_waypointShipPosition[0] / 315 + 0xCF;
                t->y0 = gameState.m1844_waypointShipPosition[1] / 341 + 0x77;
                break;
            default:
                t->x0 = worldmapMinimapPOIsCoordinates[i * 2] + 0xD0;
                t->y0 = worldmapMinimapPOIsCoordinates[i * 2 + 1] + 0x78;
                break;
            }
            addPrim(pCurrentWorldmapRenderingStruct->m70_OT, t);
        }
        mask >>= 1;
    }
    addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapMinimapBackgroundPoly[worldmapOddOrEven]);
    /* The wide picture: the minimap moves to its right edge, as far from it
     * as from the 4:3 edge. */
    if (pcWideView != 0) {
        for (p = worldmapMinimapRadar[worldmapOddOrEven], i = 0; i < 4; i++, p++) {
            pcWideAnchorRight(p);
        }
        for (t = worldmapMinimapPOIsPolys[worldmapOddOrEven], i = 0; i < 32; i++, t++) {
            pcWideAnchorRight(t);
        }
        pcWideAnchorRight(&worldmapMinimapBackgroundPoly[worldmapOddOrEven]);
    }
}

/* footprint/track marks init */
void initWorldmapTrackMarks(void) {
    s32 i;
    sWorldmapTrackMark* m;
    POLY_FT4* p;

    worldmapTrackMarks = allocateHeap(0x80, 0);
    worldmapTrackMarkPolys[0] = allocateHeap((s32)sizeof(MarkPolys16), 0);
    worldmapTrackMarkPolys[1] = allocateHeap((s32)sizeof(MarkPolys16), 0);
    for (m = worldmapTrackMarks, i = 15; i != -1; i--) {
        m->m0_x = m->m2_type = m->m4_z = 0;
        m++;
    }
    for (p = worldmapTrackMarkPolys[0]->m, i = 15; i != -1; i--) {
        setlen(p, 9);
        setcode(p, 0x2C);
        setRGB0(p, 0x40, 0x40, 0x48);
        setUVWH(p, 0x80, 0xF0, 0xF, 0xF);
        p->clut = GetClut(0x120, 0x1FE);
        p->tpage = GetTPage(0, 0, 0x380, 0x100);
        SetSemiTrans(p, 1);
        p++;
    }
    *worldmapTrackMarkPolys[1] = *worldmapTrackMarkPolys[0];
    worldmapTrackMarkIndex = 0;
}

/* track marks free */
void freeWorldmapTrackMarks(void) {
    freeHeap(worldmapTrackMarkPolys[1]);
    freeHeap(worldmapTrackMarkPolys[0]);
    freeHeap(worldmapTrackMarks);
}

/* track marks add (Noah: FUN_Worldmap__80074794 call in worldmapPartyGear.cpp) */
void addWorldmapTrackMark(s16 type, VECTOR* pos) {
    sWorldmapTrackMark* p = (sWorldmapTrackMark*)(worldmapTrackMarkIndex * 8 + (sptr)worldmapTrackMarks);
    p->m0_x = pos->vx >> 12;
    p->m4_z = pos->vz >> 12;
    p->m2_type = type;
    worldmapTrackMarkIndex = (worldmapTrackMarkIndex + 1) & 0xF;
}

typedef struct {
    /* 0x000 */ u8 m0[0x30];
    /* 0x030 */ VECTOR m30_normal;
    /* 0x040 */ VECTOR m40_up;
    /* 0x050 */ VECTOR m50_side;
    /* 0x060 */ VECTOR m60_front;
    /* 0x070 */ VECTOR m70_position;
    /* 0x080 */ VECTOR m80_scale;
    /* 0x090 */ u8 m90[0x10];
    /* 0x0A0 */ SVECTOR mA0_vertices[8];
    /* 0x0E0 */ u8 mE0[0x10];
    /* 0x0F0 */ MATRIX mF0_local;
    /* 0x110 */ MATRIX m110_final;
    /* 0x130 */ MATRIX m130_camera;
    /* 0x150 */ MATRIX m150_rotation;
} MarkScratch;

/* track marks render (GTE CompMatrix macros) */
void drawWorldmapTrackMarks(void) {
    s32 flag;
    s32 i;
    s32 z;
    s32 x0;
    s32 z0;
    sWorldmapTrackMark* mark;
    POLY_FT4* p;
    MarkScratch* sp;

    if (worldmapTrackMarkIndex != 0) {
        sp = (MarkScratch*)0x1F800000;
        sp->mA0_vertices[0].vx = -16;
        sp->mA0_vertices[0].vz = 16;
        sp->mA0_vertices[1].vx = 16;
        sp->mA0_vertices[1].vz = 16;
        sp->mA0_vertices[2].vx = -16;
        sp->mA0_vertices[2].vz = -16;
        sp->mA0_vertices[7].vx = 16;
        sp->mA0_vertices[7].vz = -16;
        sp->mA0_vertices[0].vy = sp->mA0_vertices[1].vy = sp->mA0_vertices[2].vy = sp->mA0_vertices[7].vy = 0;
        sp->m130_camera = worldmapMainMatrix2;
        mark = worldmapTrackMarks;
        i = worldmapTrackMarkIndex;
        sp->m40_up.vz = 0x1000;
        sp->m40_up.vy = sp->m40_up.vx = 0;
        p = worldmapTrackMarkPolys[worldmapOddOrEven]->m;
        x0 = worldmapRadarPosition3.vx;
        z0 = worldmapRadarPosition3.vz;
        for (i--; i != -1; i--, mark++) {
            sp->m70_position.vx = mark->m0_x << 12;
            sp->m70_position.vz = mark->m4_z << 12;
            sp->m70_position.vy = worldmapGetAltitudeFor2dPoint(sp->m70_position.vx, sp->m70_position.vz);
            interpolateWorldPosition(&sp->m30_normal, sp->m70_position.vx, sp->m70_position.vz);
            OuterProduct12(&sp->m40_up, &sp->m30_normal, &sp->m50_side);
            VectorNormal(&sp->m50_side, &sp->m60_front);
            OuterProduct12(&sp->m30_normal, &sp->m60_front, &sp->m80_scale);
            VectorNormal(&sp->m80_scale, &sp->m50_side);
            sp->mF0_local.m[0][0] = sp->m60_front.vx;
            sp->mF0_local.m[0][1] = sp->m60_front.vy;
            sp->mF0_local.m[0][2] = sp->m60_front.vz;
            sp->mF0_local.m[1][0] = sp->m30_normal.vx;
            sp->mF0_local.m[1][1] = sp->m30_normal.vy;
            sp->mF0_local.m[1][2] = sp->m30_normal.vz;
            sp->mF0_local.m[2][0] = sp->m50_side.vx;
            sp->mF0_local.m[2][1] = sp->m50_side.vy;
            sp->mF0_local.m[2][2] = sp->m50_side.vz;
            switch (mark->m2_type) {
            case 0:
                break;
            case 1:
                sp->m80_scale.vx = sp->m80_scale.vy = sp->m80_scale.vz = 0x1800;
                ScaleMatrix(&sp->mF0_local, &sp->m80_scale);
                break;
            case 2:
                sp->m150_rotation = worldMapIdentityMatrix;
                RotMatrixY(gameState.m182C_vehiclePosition[3], &sp->m150_rotation);
                MulMatrix(&sp->mF0_local, &sp->m150_rotation);
                sp->m80_scale.vx = 0x1800;
                sp->m80_scale.vy = 0x1000;
                sp->m80_scale.vz = 0x4800;
                ScaleMatrix(&sp->mF0_local, &sp->m80_scale);
                break;
            }
            sp->mF0_local.t[0] = (sp->m70_position.vx - x0) >> 12;
            sp->mF0_local.t[2] = (z0 - sp->m70_position.vz) >> 12;
            sp->mF0_local.t[1] = sp->m70_position.vy >> 12;
            gte_CompMatrix(&sp->m130_camera, &sp->mF0_local, &sp->m110_final);
            gte_SetRotMatrix(&sp->m110_final);
            gte_SetTransMatrix(&sp->m110_final);
            gte_ldv3(&sp->mA0_vertices[0], &sp->mA0_vertices[1], &sp->mA0_vertices[2]);
            gte_rtpt();
            gte_stflg(&flag);
            if (flag >= 0) {
                gte_stsxy3(&p->x0, &p->x1, &p->x2);
                gte_stsz3(&sp->m80_scale.vx, &sp->m80_scale.vy, &sp->m80_scale.vz);
                z = sp->m80_scale.vx;
                if (sp->m80_scale.vy < z) {
                    z = sp->m80_scale.vy;
                }
                if (sp->m80_scale.vz < z) {
                    z = sp->m80_scale.vz;
                }
                gte_ldv0(&sp->mA0_vertices[7]);
                gte_rtps();
                gte_stsxy(&p->x3);
                gte_stsz(&sp->m80_scale.vx);
                if (sp->m80_scale.vx < z) {
                    z = sp->m80_scale.vx;
                }
                if (z < 0x1000) {
                    /* The marks are added again every frame in the same order: the slot and kind are theirs. */
                    pcInterpolationKeyType(&worldmapTrackMarkIndex, &worldmapTrackMarkIndex,
                                           (u32)(mark - worldmapTrackMarks) << 2 | mark->m2_type, p, PC_INTERP_SHADOW);
                    addPrim(&pCurrentWorldmapRenderingStruct->m70_OT[z >> 4], p);
                    p++;
                }
            }
        }
        worldmapTrackMarkIndex = 0;
    }
}

/* animated texture set 1 init */
void initWorldmapTexAnims1(void) {
    s32 i;
    sWorldmapTexAnim* a;

    worldmapTexAnims1Count = worldmapFile1Buffer_20[0];
    worldmapTexAnims1 = allocateHeap(worldmapTexAnims1Count * sizeof(sWorldmapTexAnim), 0);
    for (a = worldmapTexAnims1, i = 0; i < worldmapTexAnims1Count; i++, a++) {
        a->m0_pixels = (u8*)worldmapFile1Buffer_20 + worldmapFile1Buffer_20[i + 1];
        a->m4_def = &worldmapTexAnims1Defs[i];
        a->m8_frame = 0;
        a->mA_timer = 1;
    }
}

/* animated texture set 1 free */
void freeWorldmapTexAnims1(void) {
    freeHeap(worldmapTexAnims1);
}

/* animated texture set 1 update (LoadImage) */
void updateWorldmapTexAnims1(void) {
    s32 i;
    sWorldmapTexAnim* a;

    for (a = worldmapTexAnims1, i = 0; i < worldmapTexAnims1Count; i++, a++) {
        if (--a->mA_timer == 0) {
            a->m8_frame++;
            a->mA_timer = a->m4_def->mC_frames[a->m8_frame * 2 + 1];
            if (a->mA_timer < 0) {
                a->m8_frame = 0;
                a->mA_timer = a->m4_def->mC_frames[1];
            }
            LoadImage(&a->m4_def->m0_rect, a->m0_pixels + (a->m4_def->mC_frames[a->m8_frame * 2] << 4));
        }
    }
}

/* animated texture set 2 init */
void initWorldmapTexAnims2(void) {
    s32 i;
    sWorldmapTexAnim* a;

    worldmapTexAnims2Count = worldmapFile1Buffer_24[0];
    worldmapTexAnims2 = allocateHeap(worldmapTexAnims2Count * sizeof(sWorldmapTexAnim), 0);
    for (a = worldmapTexAnims2, i = 0; i < worldmapTexAnims2Count; i++, a++) {
        a->m0_pixels = (u8*)worldmapFile1Buffer_24 + worldmapFile1Buffer_24[i + 1];
        a->m4_def = &worldmapTexAnims2Defs[i];
        a->m8_frame = 0;
        a->mA_timer = 1;
    }
}

/* animated texture set 2 free */
void freeWorldmapTexAnims2(void) {
    freeHeap(worldmapTexAnims2);
}

/* animated texture set 2 update */
void updateWorldmapTexAnims2(void) {
    s32 i;
    sWorldmapTexAnim* a;

    for (a = worldmapTexAnims2, i = 0; i < worldmapTexAnims2Count; i++, a++) {
        if (--a->mA_timer == 0) {
            a->m8_frame++;
            a->mA_timer = a->m4_def->mC_frames[a->m8_frame * 2 + 1];
            if (a->mA_timer < 0) {
                a->m8_frame = 0;
                a->mA_timer = a->m4_def->mC_frames[1];
            }
            LoadImage(&a->m4_def->m0_rect, a->m0_pixels + a->m4_def->mC_frames[a->m8_frame * 2] * ((a->m4_def->m0_rect.h * a->m4_def->m0_rect.w) << 1));
        }
    }
}

/* random encounter init (Noah: FUN_Worldmap__80075228 in worldmapTaskYggdrasil.cpp) */
void initWorldmapEncounterTimers(void) {
    s32 i;

    for (i = 15; i >= 0; i--) {
        worldmapEncounterCountdowns[i] = 0;
    }
    worldmapEncounterTimer = 1;
    if (gameState.m1834_vehicleMode & 0x4000) {
        worldmapEncounterPeriod = 0x300;
    } else {
        worldmapEncounterPeriod = 0x180;
    }
    worldmapEncounterTimerCount = 1;
    worldmapEncounterTriggered = 0;
}

/* random encounter tick (Noah: FUN_worldmap__8007528c in worldmapTaskYggdrasil.cpp) */
static void updateWorldmapEncounterTimerTick(void) {
    s32 i;
    s32 j;
    s32 r;

    if (--worldmapEncounterTimer == 0) {
        for (i = 0; i < worldmapEncounterTimerCount; i++) {
            if (i != 0) {
                do {
                    r = rand() % worldmapEncounterPeriod + 1;
                    for (j = 0; j < i; j++) {
                        if (worldmapEncounterCountdowns[j] == r) {
                            break;
                        }
                    }
                } while (j < i);
            } else {
                r = rand() % worldmapEncounterPeriod + 1;
            }
            worldmapEncounterCountdowns[i] = r;
        }
        worldmapEncounterTimer = worldmapEncounterPeriod;
    }
    worldmapEncounterTriggered = 0;
    for (i = 0; i < worldmapEncounterTimerCount; i++) {
        if (--worldmapEncounterCountdowns[i] == 0) {
            worldmapEncounterTriggered++;
        }
    }
}

void updateWorldmapEncounterTimers(void) {
    static int fraction;
    int i;
    int ticks = pcGameplayEncounterTicks(&fraction);
    worldmapEncounterTriggered = 0;
    for (i = 0; i < ticks; i++) {
        updateWorldmapEncounterTimerTick();
        /* Keep a trigger until the main loop handles it. Extra ticks may
         * otherwise clear it, or skip past an encounter while moving. */
        if (worldmapEncounterTriggered != 0) break;
    }
}

/* save worldmap state to main buffer 0x8005A4E4 before a menu (Noah: worldmapPrepareEnterMenu, commented out in worldmap.cpp) */
void backupWorldmapStateForBattle(void) {
    sWorldmapMenuBackup* b = &overlayStateSaveBlock;

    b->m0_tasks = *gWorldmapState;
    b->m2000_position.vx = worldmapRadarPosition.vx;
    b->m2000_position.vy = worldmapRadarPosition.vy;
    b->m2000_position.vz = worldmapRadarPosition.vz;
    b->m2010_direction = worldmapPlayerDirection;
    b->m2014_encounterPeriod = worldmapEncounterPeriod;
    b->m2018_encounterCount = worldmapEncounterTimerCount;
    b->m201C_encounterTimer = worldmapEncounterTimer;
    b->m2020_encounters = *(sWorldmapEncounterTimers*)worldmapEncounterCountdowns;
    b->m2040_followLeader = *(sWorldmapFollowLeaderBlock*)worldmapFollowLeaderData;
    b->m22C0_followLeaderIndex = worldmapFollowLeaderDataIndex;
    COPY_WORDS2(b->m22C4_rotation, worldmapRotation);
    b->m22CC_cameraDistance = worldmapCameraVar0;
    b->m22D0_geometryOffsetY = worldmapGeometryOffsetY;
    COPY_WORDS2(b->m22E4_gridPosition, worldmapGridPosition);
    b->m22D4_gridInputPosition.vx = worldmapGridInputPosition.vx;
    b->m22D4_gridInputPosition.vy = worldmapGridInputPosition.vy;
    b->m22D4_gridInputPosition.vz = worldmapGridInputPosition.vz;
    b->m22EC_cameraTarget.vx = worldmapRadarPosition3.vx;
    b->m22EC_cameraTarget.vy = worldmapRadarPosition3.vy;
    b->m22EC_cameraTarget.vz = worldmapRadarPosition3.vz;
}

/* restore worldmap state after a menu (Noah: worldmapRestoreAfterMenu) */
void restoreWorldmapStateFromBattle(void) {
    sWorldmapMenuBackup* b = &overlayStateSaveBlock;

    *gWorldmapState = b->m0_tasks;
    worldmapPosition = b->m2000_position;
    worldmapRadarPosition = b->m2000_position;
    *(sWorldmapEncounterTimers*)worldmapEncounterCountdowns = b->m2020_encounters;
    worldmapPlayerDirection = b->m2010_direction;
    worldmapEncounterPeriod = b->m2014_encounterPeriod;
    worldmapEncounterTimerCount = b->m2018_encounterCount;
    worldmapEncounterTimer = b->m201C_encounterTimer;
    *(sWorldmapFollowLeaderBlock*)worldmapFollowLeaderData = b->m2040_followLeader;
    worldmapRotation = b->m22C4_rotation;
    worldmapGridInputPosition = b->m22D4_gridInputPosition;
    worldmapGridPosition = b->m22E4_gridPosition;
    worldmapRadarPosition3 = b->m22EC_cameraTarget;
    worldmapFollowLeaderDataIndex = b->m22C0_followLeaderIndex;
    worldmapCameraVar0 = b->m22CC_cameraDistance;
    worldmapGeometryOffsetY = b->m22D0_geometryOffsetY;
}

/* enter-menu preparation: save VRAM, free/alloc heap, load menu (Noah: worldmapPrepareEnterMenu) */
void worldmapPrepareEnterMenu(void) {
    RECT rect;
    s32 i;
    u32 p;

    gameState.m1836_restoreWorldmapPosition = 1;
    gameState.m231C_cameraYaw = (worldmapRotation.vy + 0x2000) & 0x3FFF;
    for (i = 0; i < 3; i++) {
        gameState.m183C_isOnGearBackup[i] = gameState.m22B1_isOnGear[i];
    }
    worldmapSavedGearSwitchDisabled = gearSwitchDisabled;
    if ((s16)getWorldmapGroundType(&worldmapRadarPosition) == 4) {
        gearSwitchDisabled = 1;
    }
    waitWorldmapStreamingDone();
    freeWorldmapPolyFT4Array3();
    freeWorldmapPolyFT4Array2();
    freeWorldmapPolyFT4Array1();
    freeHeap(worldmapRenderingStructs[0].m74_polyFT3);
    freeHeap(worldmapRenderingStructs[1].m74_polyFT3);
    /* The menu overlay is linked in, so no padding up to its link address:
     * a token block keeps the frees balanced. */
    worldmapMenuOverlayBuffer = allocateHeap(4, 1);
    (void)p;
    startLoadingWorldmapMenuFiles();
    worldmapMenuVramBackup1 = allocateHeap(0x10000, 0);
    worldmapMenuVramBackup2 = allocateHeap(0xC800, 0);
    setRECT(&rect, 0x180, 0x100, 0x80, 0x100);
    StoreImage(&rect, worldmapMenuVramBackup1);
    setRECT(&rect, 0, 0x1B0, 0x140, 0x50);
    StoreImage(&rect, worldmapMenuVramBackup2);
    if (worldmapOddOrEven == 0) {
        setRECT(&rect, 0, 0xD8, 0x140, 0xD8);
        MoveImage(&rect, 0, 0);
    }
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x10, 0, 8, 2);
    while (isCDBusy() >= 2) {
        PORT_YIELD();
    }
    portResetModule("menu"); /* file 0x25 is a copy of the menu overlay, linked in */
    freeHeap(worldmapMenuOverlayFile);
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0, 0xE0);
    DrawSync(0);
    waitReadCompletion(0);
}

/* leave-menu restore: reload world map files/VRAM (Noah: worldmapRestoreAfterMenu part) */
void worldmapRestoreAfterMenu(void) {
    RECT rect;

    resetMemoryAllocStats(3, 0);
    setCurrentDirectory(0x24, 0);
    freeHeap(menuSharedResources);
    freeHeap(worldmapMenuOverlayBuffer);
    initWorldmapGraphics();
    worldmapFile3Buffer = allocateHeap(getFileSizeAligned(worldmapFile3), 1);
    readFile(worldmapFile3, worldmapFile3Buffer, 0, 0);
    worldmapFadeIn(0x10, 0x80, -8, 2);
    setRECT(&rect, 0x2C0, 0x100, 0x140, 0xD8);
    MoveImage(&rect, 0, 0);
    MoveImage(&rect, 0, 0xD8);
    ClearOTagR(pCurrentWorldmapRenderingStruct->m70_OT, 0x400);
    waitReadCompletion(0);
    loadWorldmapTextureFromFile3();
    setRECT(&rect, 0x180, 0x100, 0x80, 0x100);
    LoadImage(&rect, worldmapMenuVramBackup1);
    setRECT(&rect, 0, 0x1B0, 0x140, 0x50);
    LoadImage(&rect, worldmapMenuVramBackup2);
    DrawSync(0);
    freeHeap(worldmapMenuVramBackup1);
    freeHeap(worldmapMenuVramBackup2);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    initFontPalettes(0x130, 0x1E0);
    VSync(0);
    resetInputs();
    worldmapExitVar2 = 0;
    gameState.m1836_restoreWorldmapPosition = 0;
    gearSwitchDisabled = worldmapSavedGearSwitchDisabled;
    applyWorldmapGearBoardingChanges();
}

extern s32 worldMapGearMode;

void applyWorldmapGearBoardingChanges(void) {
    s32 i;
    s32 n;
    sWorldmapStateEntry* e;

    for (i = 0, e = gWorldmapState->m0_entries; i < 3; i++, e = &gWorldmapState->m0_entries[i]) {
        if (gameState.m22B1_isOnGear[i] != (u8)gameState.m183C_isOnGearBackup[i]) {
            if (gameState.m22B1_isOnGear[i] == 0) {
                e[1].m28_position.vx = e[4].m28_position.vx;
                e[1].m28_position.vy = e[4].m28_position.vy;
                e[1].m28_position.vz = e[4].m28_position.vz;
                e[1].m58 = e[4].m58;
            } else {
                gameState.m1930_fieldVarsBackup[21 + i * 3] = 0x400;
                e[4].m24_hidden = 0;
                e[4].m28_position.vx = e[1].m28_position.vx;
                e[4].m28_position.vy = e[1].m28_position.vy;
                e[4].m28_position.vz = e[1].m28_position.vz;
                e[4].m58 = e[1].m58;
            }
        }
    }
    n = 0;
    for (i = 0; i < 3; i++) {
        if (gameState.m1D34_currentParty[i] != 0xFF && gameState.m22B1_isOnGear[i] == 1) {
            n++;
        }
    }
    if (!(gameState.m1834_vehicleMode & 0x4000)) {
        if (n != 0) {
            worldMapGearMode = 2;
        } else {
            worldMapGearMode = 1;
        }
    }
}

/* pick a random encounter group for the current zone (weights table, copies battle setup to 0x800658DC) */
s32 rollWorldmapRandomEncounter(VECTOR* pos, s32 scenario) {
    s32 zone;
    s32 j;
    s32 total;
    s32 r;
    s32 k;
    u8* src;
    u8 weights[16];

    zone = (s16)getWorldmapEncounterZone(pos);
    if ((s16)getWorldmapGroundType(pos) == 4) {
        zone = worldmapEncounterZoneRemap[zone];
    }
    for (j = 1; worldmapEncounterScenarioThresholds[j] <= scenario; j++) {
    }
    j--;
    total = 0;
    src = worldmapFile1Buffer_2C_data[zone]->m200_weights[0];
    src += j * 16;
    for (k = 0; k < 16; k++) {
        weights[k] = src[k];
        total += src[k];
    }
    if (total <= 0) {
        return 0;
    }
    r = rand() % total + 1;
    k = 0;
    do {
        r--;
    again:
        if (weights[k] == 0) {
            k++;
            goto again;
        }
        weights[k]--;
    } while (r > 0);
    battleConfigs = worldmapFile1Buffer_2C_data[zone]->m0_battleSetup;
    requestedBattleConfig = k;
    pcGameplayRandomBattle(k); /* a random battle, for the reward settings */
    return 1;
}

typedef struct {
    /* 0x000 */ VECTOR m0_position;
    /* 0x010 */ u8 m10[0x10];
    /* 0x020 */ s32 m20_flag;
    /* 0x024 */ u8 m24[4];
    /* 0x028 */ s32 m28_sz;
    /* 0x02C */ u8 m2C[0xA0 - 0x2C];
    /* 0x0A0 */ SVECTOR mA0_origin;
    /* 0x0A8 */ u8 mA8[0xF0 - 0xA8];
    /* 0x0F0 */ MATRIX mF0_local;
    /* 0x110 */ MATRIX m110_final;
} GearScratch;

/* draw a gear (field_mecha model) at a fixed world position (GTE macros) */
void drawWorldmapMechaModel(void) {
    GearScratch* sp = (GearScratch*)0x1F800000;

    sp->m0_position.vy = 0xA0;
    sp->m0_position.vx = 0x4E0E - (worldmapRadarPosition3.vx >> 12);
    sp->m0_position.vz = 0x1B68 - (worldmapRadarPosition3.vz >> 12);
    adjustWorldModelLocation(&sp->m0_position);
    loadedMechas[0]->m4_bones->m5C_translation[0] = sp->m0_position.vx;
    loadedMechas[0]->m4_bones->m5C_translation[1] = 0;
    loadedMechas[0]->m4_bones->m5C_translation[2] = -sp->m0_position.vz;
    loadedMechas[0]->m4_bones->m54_rotationAngles.vx = loadedMechas[0]->m4_bones->m54_rotationAngles.vy = loadedMechas[0]->m4_bones->m54_rotationAngles.vz = 0;
    *(s16*)&loadedMechas[0]->m5C_attachMecha = -0x100; /* m5C and m5D in one store */
    loadedMechas[0]->m1C_scale = 0x40;
    sp->mF0_local = worldMapIdentityMatrix;
    sp->mF0_local.t[0] = sp->m0_position.vx;
    sp->mF0_local.t[1] = sp->m0_position.vy;
    sp->mF0_local.t[2] = -sp->m0_position.vz;
    CompMatrix(&worldmapMainMatrix2, &sp->mF0_local, &sp->m110_final);
    SetRotMatrix(&sp->m110_final);
    SetTransMatrix(&sp->m110_final);
    sp->mA0_origin.vx = sp->mA0_origin.vy = sp->mA0_origin.vz = 0;
    gte_ldv0(&sp->mA0_origin);
    gte_rtps();
    gte_stflg(&sp->m20_flag);
    if (sp->m20_flag >= 0) {
        gte_stsz(&sp->m28_sz);
        if (sp->m28_sz < 0xD00) {
            mechaFinalizeVar0 = &worldmapColorMatrix;
            SetBackColor(0x40, 0x40, 0x40);
            renderMechas(&worldmapMainMatrix2, &worldmapLightMatrix, pCurrentWorldmapRenderingStruct->m70_OT, worldmapOddOrEven, 1);
        }
    }
}

/* stop callbacks (DrawSync/VSync + main 800404D4/80040454/800404E4) */
void worldmapFlushCacheAndSync(void) {
    DrawSync(0);
    VSync(0);
    EnterCriticalSection();
    DrawSync(0);
    VSync(0);
    FlushCache();
    ExitCriticalSection();
}

/* pause: wait for Start (worldmapInput2_0 & 0x800), Noah worldmap.cpp input loop */
void runWorldmapPause(void) {
    RECT rect;
    s32 vsync;

    vsync = g_playTimeInVsync;
    DrawSync(0);
    VSync(0);
    if (worldmapOddOrEven == 0) {
        setRECT(&rect, 0, 0xD8, 0x140, 0xD8);
        MoveImage(&rect, 0, 0);
    }
    PutDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv);
    PutDrawEnv(&worldmapRenderingStructs[1].m0_DrawEnv);
    pauseMusic();
    /* The wide picture: the frozen frame keeps its margins while the text
     * is uploaded over it each frame (include/pc_wide.h). */
    pcWideKeepMargins(1);
    do {
        DrawSync(0);
        VSync(0);
        uploadBuiltinTexture(0x88, 0x64);
        worldmapInput1_0 = worldmapInput2_0 = worldmapInput3_0 = worldmapInput1_1 = worldmapInput2_1 = worldmapInput3_1 = 0;
        while (loadInputFromVSyncBuffer()) {
            worldmapInput1_0 |= newPadButtonForScripts;
            worldmapInput1_1 |= newPadButtonForScripts2;
            worldmapInput2_0 |= newPadButtonForDialogs;
            worldmapInput2_1 |= newPadButtonForDialogs2;
            worldmapInput3_0 |= newPadButtonForField;
            worldmapInput3_1 |= newPadButtonForField2;
        }
    } while (!(worldmapInput2_0 & 0x800));
    resumeMusic();
    DrawSync(0);
    VSync(0);
    pcWideKeepMargins(0);
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0, 0xD8);
    PutDispEnv(&worldmapRenderingStructs[worldmapOddOrEven].m5C_DispEnv);
    PutDrawEnv(&worldmapRenderingStructs[worldmapOddOrEven].m0_DrawEnv);
    g_playTimeInVsync = vsync;
}

#if !VERSION_JP
/* US only: JP has no counterpart (it is a near-twin of runWorldmapPause) */
void waitWorldmapControllerReconnect(void) {
    RECT rect;
    s32 vsync;

    vsync = g_playTimeInVsync;
    DrawSync(0);
    VSync(0);
    if (worldmapOddOrEven == 0) {
        setRECT(&rect, 0, 0xD8, 0x140, 0xD8);
        MoveImage(&rect, 0, 0);
    }
    PutDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv);
    PutDrawEnv(&worldmapRenderingStructs[1].m0_DrawEnv);
    pauseMusic();
    /* The wide picture: the frozen frame keeps its margins while the text
     * is uploaded over it each frame (include/pc_wide.h). */
    pcWideKeepMargins(1);
    do {
        DrawSync(0);
        VSync(0);
        uploadBuiltinTexture(0x88, 0x64);
        worldmapInput1_0 = worldmapInput2_0 = worldmapInput3_0 = worldmapInput1_1 = worldmapInput2_1 = worldmapInput3_1 = 0;
        while (loadInputFromVSyncBuffer()) {
            worldmapInput1_0 |= newPadButtonForScripts;
            worldmapInput1_1 |= newPadButtonForScripts2;
            worldmapInput2_0 |= newPadButtonForDialogs;
            worldmapInput2_1 |= newPadButtonForDialogs2;
            worldmapInput3_0 |= newPadButtonForField;
            worldmapInput3_1 |= newPadButtonForField2;
        }
    } while (isControllerConnected(0) == 0);
    resumeMusic();
    DrawSync(0);
    VSync(0);
    pcWideKeepMargins(0);
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0, 0xD8);
    PutDispEnv(&worldmapRenderingStructs[worldmapOddOrEven].m5C_DispEnv);
    PutDrawEnv(&worldmapRenderingStructs[worldmapOddOrEven].m0_DrawEnv);
    g_playTimeInVsync = vsync;
}
#endif

/* load and start a music sequence from a buffer (field sound pattern) */
void startWorldmapMusicFromBuffer(void* data, s32 fileIndex) {
    stopAllMusicInstances();
    freeSoundInstance(pMusic);
    memcpy(battleMusic, data, getFileSizeAligned(fileIndex));
    pMusic = createMusicInstance(battleMusic);
    startMusicInstance(pMusic, 0x7F, 0);
}

/* quadratic Bezier (SVECTOR control points, 0x1000 = 1.0) into a VECTOR */
void evalWorldmapBezier(s32 t, SVECTOR* p0, SVECTOR* p1, SVECTOR* p2, VECTOR* out) {
    s32 u = 0x1000 - t;
    s32 b0 = (u * u * 8) >> 12;
    s32 b1 = ((u * t) >> 8) + 0x8000;
    s32 b2 = (t * t * 8) >> 12;

    out->vx = p0->vx * b0 + p1->vx * b1 + p2->vx * b2;
    out->vy = p0->vy * b0 + p1->vy * b1 + p2->vy * b2;
    out->vz = p0->vz * b0 + p1->vz * b1 + p2->vz * b2;
}

/* decompress world map file 1 and set its section pointers (Noah: finalizeWorldMapFileLoading, other mode) */
void finalizeWorldmapEventFileLoading(void) {
    sWorldmapFile1Header* old = worldmapFile1Buffer;

    worldmapFile1Buffer = (sWorldmapFile1Header*)mallocAndDecompress(old, 0);
    freeHeap(old);
    worldmapModelFiles = (Model*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m8_modelsOffset);
    worldmapFile1Buffer_C = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->mC_collisionMeshesOffset);
    pWorldmapModelsConfig = (u16*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m10_modelsConfigOffset);
    worldmapFile1Buffer_14 = (sWorldmapOffsetEntry*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m14_billboardsOffset);
    worldmapFile1Buffer_18 = (sWorldmapParticle*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m18_particlesOffset);
    worldmapFile1Buffer_20 = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m20_texAnims1Offset);
    worldmapFile1Buffer_24 = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m24_texAnims2Offset);
}

/* returns 1 (Noah worldmapTask.cpp dummy) */
s32 worldmap_taskScene_init(s32 index) {
    return 1;
}

/* Enhanced, the 60 Hz world map's event modes (w_80070CFC.c): the scene
 * tasks draw as the ground task does. The tick: the picture's values put in
 * halfway (pcWm60RenderBegin), the particles' spawning and moving (the RNG)
 * the tick's, drawn halfway through their step; the frame between (called
 * only after a tick that drew) draws where the tick took things, with the
 * second halves of the clouds' drift and the water's wave. */
s32 pcWm60On(void);
s32 pcWm60OffTick(void);
s32 pcWm60Half(s32 step);
s32 pcWm60FrameIndex(void);
void pcWm60RenderBegin(void);
void pcWm60RenderEnd(void);
void pcWm60SceneTicked(void);
void pcWm60CameraRun(void);
void pcWm60Cut(void);
void pcWm60ParticlesBefore(void);
void pcWm60ParticlesMid(void);
void pcWm60ParticlesAfter(void);
void portWorldmapSweepRender(s32 frame, s32 tick);
static void pcWm60SceneBegin(s32 offTick) {
    if (!offTick) {
        pcWm60SceneTicked();
    }
    pcWm60RenderBegin();
}
static void pcWm60SceneParticles(s32 offTick) {
    if (!offTick) {
        if (pcWm60On()) pcWm60ParticlesBefore();
        updateWorldmapParticleEmitters(); /* spawns (the RNG) and moves: the tick's */
        if (pcWm60On()) pcWm60ParticlesMid();
    }
    drawWorlmapPolyFT4Array1();
    if (pcWm60On() && !offTick) pcWm60ParticlesAfter();
}

/* per-frame render/update task of a world map mode (ground, sky, clouds, sprites) */
s32 worldmap_taskScene_update(s32 index) {
    s32 offTick = pcWm60OffTick();

    pcWm60SceneBegin(offTick);
    if (worldmapMatrixMode == 0) {
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
    } else {
        setupWorldmapMatrix2(&worldmapCameraVector);
    }
    portWorldmapSweepRender(pcWm60FrameIndex(), !offTick); /* XG_SWEEP_LOG: the picture's values */
    pcWm60SceneParticles(offTick);
    drawWorldmapBillboards();
    renderWorldmapModels();
    setWorldmapGridUpdateMask(&worldmapGridInputPosition);
    if (worldmapGridUpdateMask != 0) {
        updateWorldmapGrids(&worldmapRadarPosition3);
        streamWorldmap0();
        streamWorldmap1();
    }
    worldmapGroundPrepareRenderingTable(&worldmapRadarPosition3);
    drawWorldmapGround(pCurrentWorldmapRenderingStruct->m70_OT, pCurrentWorldmapRenderingStruct->m74_polyFT3, &worldmapRadarPosition3);
    worldmapWaterWave += pcWm60Half(0x40); /* the water's wave: half a step per 60 Hz frame */
    drawWorldmapSkyCloudBand();
    drawWorldmapSkyGradient();
    drawWorldmapClouds();
    pcWm60RenderEnd();
    return 1;
}

/* event script interpreter task (opcode table worldmapScriptOpcodes); unused 8-byte local reserves retail's stack slot */
s32 worldmap_taskScript_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    s32 n = 0;
    sWorldmapScriptIns* pc;
    s32 w;
    sWorldmapScriptIns unused; /* unused: reserves the 8-byte stack slot retail has */

    do {
        pc = (sWorldmapScriptIns*)((s16*)e->m50 + n);
        e->m50 = (sptr)pc;
        w = *(s32*)pc;
        n = worldmapScriptOpcodes[w & 0xFFFF](e, w >> 16, pc->m4_arg2, pc->m6_arg3);
    } while (n != 0);
    return 1;
}

/* script op */
s32 worldmapOP_0_exit(void) {
    continueWorldmapLoop = 0;
    exitWorldMapMode = 0;
    return 0;
}

/* script op: wait */
s32 worldmapOP_1_wait(sWorldmapStateEntry* e, s32 a1) {
    if (e->m22_timer == 0) {
        e->m22_timer = a1;
        return 0;
    }
    e->m22_timer--;
    return (e->m22_timer <= 0) * 2;
}

/* script op */
s32 worldmapOP_2_signalTask(sWorldmapStateEntry* e, s32 a1, s32 a2) {
    changeWorldmapEntityState(a1, a2);
    return 4;
}

/* script op */
s32 worldmapOP_3_setPosition(sWorldmapStateEntry* e, s32 a1, s32 a2, s32 a3) {
    worldmapPosition.vx = a1 << 12;
    worldmapPosition.vy = a2 << 12;
    worldmapPosition.vz = a3 << 12;
    return 4;
}

/* script op */
s32 worldmapOP_4_setParticlePosition(sWorldmapStateEntry* e, s16 a1, s16 a2, s16 a3) {
    *(s16*)0x1F8000A0 = a1;
    *(s16*)0x1F8000A2 = a2;
    *(s16*)0x1F8000A4 = a3;
    return 4;
}

/* script op */
s32 worldmapOP_5_spawnParticles(sWorldmapStateEntry* e, s32 a1) {
    spawnWorldmapParticles(a1, (void*)0x1F8000A0, 0);
    return 2;
}

/* script op */
s32 worldmapOP_6_clearParticles(sWorldmapStateEntry* e, s32 a1) {
    clearWorldmapParticles(a1);
    return 2;
}

/* script op */
s32 worldmapOP_7_killParticles(sWorldmapStateEntry* e, s32 a1) {
    killWorldmapParticles(a1);
    return 2;
}

/* script op: playBattleMusic */
s32 worldmapOP_8_playBattleMusic(sWorldmapStateEntry* e, s32 a1, s32 a2) {
    playBattleMusic(pMusic, a1, a2);
    return 4;
}

/* script op: sound */
s32 worldmapOP_9_playSound(sWorldmapStateEntry* e, s32 a1) {
    playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | a1);
    return 2;
}

/* script op: sound */
s32 worldmapOP_A_fadeSound(sWorldmapStateEntry* e, s32 a1, s32 a2, s32 a3) {
    fadeSoundEffectVolume((fieldSeq->m14_id << 16) | a1, a2, a3);
    return 4;
}

/* script op */
s32 worldmapOP_B_setFade(sWorldmapStateEntry* e, s32 a1, s32 a2) {
    worldmapFadePolyAbr = a1;
    worldmapFadePolyStep = a2;
    return 4;
}

/* camera rotation smoothing towards the target (m50..m58), uses builtin abs() */
void smoothWorldmapCameraRotation(sWorldmapStateEntry* e, sWorldmapCameraDelta* d) {
    if ((e->m38_step.vx != e->m50) | (e->m38_step.vy != e->m54) | (e->m38_step.vz != e->m58)) {
        d->m0_delta.vx = (s32)e->m50 - e->m38_step.vx;
        d->m0_delta.vy = (s32)e->m54 - e->m38_step.vy;
        d->m0_delta.vz = (s32)e->m58 - e->m38_step.vz;
        computeWormapSpritePosition(&d->m0_delta);
        d->m10_step.vx = d->m0_delta.vx >> 3;
        d->m10_step.vy = d->m0_delta.vy >> 3;
        d->m10_step.vz = d->m0_delta.vz >> 3;
        if (abs(d->m10_step.vx) < 0x40) {
            e->m38_step.vx = (s32)e->m50;
        } else {
            e->m38_step.vx += d->m10_step.vx;
        }
        if (abs(d->m10_step.vy) < 0x40) {
            e->m38_step.vy = (s32)e->m54;
        } else {
            e->m38_step.vy += d->m10_step.vy;
        }
        if (abs(d->m10_step.vz) < 0x40) {
            e->m38_step.vz = (s32)e->m58;
        } else {
            e->m38_step.vz += d->m10_step.vz;
        }
    }
    worldmapRotation.vx = e->m38_step.vx >> 12;
    worldmapRotation.vy = e->m38_step.vy >> 12;
    worldmapRotation.vz = e->m38_step.vz >> 12;
}

/* camera distance smoothing (worldmapCameraVar0 towards m5C) */
void smoothWorldmapCameraDistance(sWorldmapStateEntry* e, sWorldmapCameraDelta* unused) { /* callers pass the scratchpad like its siblings */
    s32 t = e->m5C;
    s32 d;

    if (t != worldmapCameraVar0) {
        d = (t - worldmapCameraVar0) >> 3;
        if (abs(d) < 0x40) {
            worldmapCameraVar0 = t;
        } else {
            worldmapCameraVar0 += d;
        }
    }
}

/* camera position smoothing towards the player position */
void smoothWorldmapCameraPosition(sWorldmapStateEntry* e, sWorldmapCameraDelta* d) {
    if ((e->m28_position.vx != worldmapRadarPosition.vx) | (e->m28_position.vy != worldmapRadarPosition.vy) | (e->m28_position.vz != worldmapRadarPosition.vz)) {
        d->m0_delta.vx = worldmapRadarPosition.vx - e->m28_position.vx;
        d->m0_delta.vy = worldmapRadarPosition.vy - e->m28_position.vy;
        d->m0_delta.vz = worldmapRadarPosition.vz - e->m28_position.vz;
        computeWormapSpritePosition(&d->m0_delta);
        d->m10_step.vx = d->m0_delta.vx >> 3;
        d->m10_step.vy = d->m0_delta.vy >> 3;
        d->m10_step.vz = d->m0_delta.vz >> 3;
        if (abs(d->m10_step.vx) < 0x200) {
            worldmapGridInputPosition.vx += d->m0_delta.vx;
            e->m28_position.vx = worldmapRadarPosition.vx;
        } else {
            worldmapGridInputPosition.vx += d->m10_step.vx;
            e->m28_position.vx += d->m10_step.vx;
        }
        if (abs(d->m10_step.vy) < 0x200) {
            e->m28_position.vy = worldmapRadarPosition.vy;
        } else {
            e->m28_position.vy += d->m10_step.vy;
        }
        if (abs(d->m10_step.vz) < 0x200) {
            worldmapGridInputPosition.vz += d->m0_delta.vz;
            e->m28_position.vz = worldmapRadarPosition.vz;
        } else {
            worldmapGridInputPosition.vz += d->m10_step.vz;
            e->m28_position.vz += d->m10_step.vz;
        }
    }
    worldmapRadarPosition3 = e->m28_position;
}

/* step a value towards a target (builtin abs) */
s32 approachWorldmapValue(s32 cur, s32 target, s32 step) {
    if (cur != target) {
        if (abs(target - cur) < abs(step)) {
            cur = target;
        } else {
            cur += step;
        }
    }
    return cur;
}

/* world map mode init (loads data, sets up tasks via setupWorldmapTask = Noah setupWorldmapTask) */
void worldmapMode8_update(void) {
    RECT rect;

    initWorldmapGraphics();
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    while (isCDBusy() >= 3) {
        PORT_YIELD();
    }
    finalizeWorldMapFileLoading();
    allocAndInitWorldmapState();
    D_8009BE4C = worldMapIdentityMatrix;
    worldmapFadePolyAbr = 2;
    worldmapFadePolyStep = 4;
    worldmapExitVar2 = 0;
    worldmapMatrixMode = 0;
    worldmapCloudsMoveCallback = moveWorldmapClouds;
    worldmapDotInit();
    waitReadCompletion(0);
    worldmapPosition.vx = 0x7702000;
    worldmapPosition.vy = -0x300000;
    worldmapPosition.vz = 0x27C0000;
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
    setCurrentDirectory(0x24, 0);
    setupInitialGrid(&worldmapPosition);
    do {
        loadWorldmapChunk();
        VSync(0);
    } while (isWorldStreamingDone() > 0);
    setupWorldmapTask(worldmap_taskFader_init, worldmap_taskFader_update);
    setupWorldmapTask(worldmapMode8_taskPadCamera_init, worldmapMode8_taskPadCamera_update);
    setupWorldmapTask(worldmap_taskSpinModels_init, worldmap_taskSpinModels_update);
    setupWorldmapTask(worldmap_taskGround_init, worldmap_taskGround_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    initWorldmapEncounterTimers();
    spawnWorldmapParticles(0xE, 0, 0);
}

/* world map mode free; exits to field 0x11 with worldmap mode 7 */
void worldmapMode8_free(void) {
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
    gameState.m231A_fieldID = 0x11;
    gameState.m2320_worldmapMode = 7;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

typedef struct {
    /* 0x00 */ VECTOR m0_delta;
    /* 0x10 */ u8 m10[0xA0 - 0x10];
    /* 0xA0 */ SVECTOR mA0_tmp;
} DemoScratch;

/* task init: free-look camera (sets rotation/distance, camera from worldmapPosition) */
s32 worldmapMode8_taskPadCamera_init(s32 index) {
    sWorldmapStateEntry* e;

    worldmapRotation.vx = -0x80;
    worldmapRotation.vy = 0x200;
    worldmapCameraVar0 = 0x400000;
    worldmapRotation.vz = 0;
    e = &gWorldmapState->m0_entries[index];
    e->m28_position.vx = -0x80000;
    worldmapGeometryOffsetY = 0x78;
    e->m28_position.vy = worldmapRotation.vy << 12;
    e->m38_step = e->m28_position;
    worldmapMatrixMode = 0;
    worldmapRadarPosition3.vx = worldmapPosition.vx;
    worldmapRadarPosition3.vy = worldmapPosition.vy;
    worldmapRadarPosition3.vz = worldmapPosition.vz;
    computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    ((DemoScratch*)0x1F800000)->mA0_tmp = worldmapCameraVector.m0_eye;
    worldmapCameraVector.m0_eye = worldmapCameraVector.m8_target;
    worldmapCameraVector.m8_target = ((DemoScratch*)0x1F800000)->mA0_tmp;
    return 1;
}

/* Enhanced, the 60 Hz world map (w_80070CFC.c): the frame between previews
 * half of the native step the pad camera would take from that vblank's pad,
 * on copies (the tick runs the native frame from where the tick before left
 * it, with both vblanks' input, as the Original's frame collects them); Cross
 * leaving is the tick's. Its camera is drawn as it is (pcWm60CameraRun). */
static void pcWm60PadCameraPreview(sWorldmapStateEntry* e) {
    DemoScratch* sp = (DemoScratch*)0x1F800000;
    VECTOR target = e->m28_position;
    VECTOR step = e->m38_step;

    if (worldmapInput1_0 & 0x1000) {
        target.vx += 0x8000;
    }
    if (worldmapInput1_0 & 0x4000) {
        target.vx -= 0x8000;
    }
    if (target.vx < -0x180000) {
        target.vx = -0x180000;
    } else if (target.vx > 0x18000) {
        target.vx = 0x18000;
    }
    if (worldmapInput1_0 & 0x8004) {
        target.vy -= 0x10000;
    }
    if (worldmapInput1_0 & 0x2008) {
        target.vy += 0x10000;
    }
    if ((step.vx != target.vx) | (step.vy != target.vy)) {
        step.vx += ((target.vx - step.vx) >> 3) / 2;
        step.vy += ((target.vy - step.vy) >> 3) / 2;
    }
    worldmapRotation.vx = step.vx >> 12;
    worldmapRotation.vy = step.vy >> 12;
    computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    sp->mA0_tmp = worldmapCameraVector.m0_eye;
    worldmapCameraVector.m0_eye = worldmapCameraVector.m8_target;
    worldmapCameraVector.m8_target = sp->mA0_tmp;
    worldmapRotation.vy = (worldmapRotation.vy + 0x800) & 0xFFF;
    SetGeomScreen(worldmapMinimapScale);
}

/* task update: free-look camera driven by pad (debug/demo view) */
s32 worldmapMode8_taskPadCamera_update(s32 index) {
    DemoScratch* sp = (DemoScratch*)0x1F800000;
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    if (pcWm60On()) {
        pcWm60CameraRun(); /* every frame moves the camera: drawn as it is */
        if (pcWm60OffTick()) {
            pcWm60PadCameraPreview(e);
            return 1;
        }
    }
    if (worldmapInput2_0 & 0x40) {
        continueWorldmapLoop = 0;
        exitWorldMapMode = 0;
    }
    if (worldmapInput1_0 & 0x1000) {
        e->m28_position.vx += 0x8000;
    }
    if (worldmapInput1_0 & 0x4000) {
        e->m28_position.vx -= 0x8000;
    }
    if (e->m28_position.vx < -0x180000) {
        e->m28_position.vx = -0x180000;
    } else if (e->m28_position.vx > 0x18000) {
        e->m28_position.vx = 0x18000;
    }
    if (worldmapInput1_0 & 0x8004) {
        e->m28_position.vy -= 0x10000;
    }
    if (worldmapInput1_0 & 0x2008) {
        e->m28_position.vy += 0x10000;
    }
    if ((e->m38_step.vx != e->m28_position.vx) | (e->m38_step.vy != e->m28_position.vy)) {
        sp->m0_delta.vx = e->m28_position.vx - e->m38_step.vx;
        sp->m0_delta.vy = e->m28_position.vy - e->m38_step.vy;
        e->m38_step.vx += sp->m0_delta.vx >> 3;
        e->m38_step.vy += sp->m0_delta.vy >> 3;
    }
    worldmapRotation.vx = e->m38_step.vx >> 12;
    worldmapRotation.vy = e->m38_step.vy >> 12;
    computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    sp->mA0_tmp = worldmapCameraVector.m0_eye;
    worldmapCameraVector.m0_eye = worldmapCameraVector.m8_target;
    worldmapCameraVector.m8_target = sp->mA0_tmp;
    worldmapRotation.vy = (worldmapRotation.vy + 0x800) & 0xFFF;
    SetGeomScreen(worldmapMinimapScale);
    return 1;
}

/* returns 1 (empty task) */
s32 worldmap_taskSceneNoParticles_init(s32 index) {
    return 1;
}

/* per-frame render task (variant of worldmap_taskScene_update without two calls) */
s32 worldmap_taskSceneNoParticles_update(s32 index) {
    s32 offTick = pcWm60OffTick();

    pcWm60SceneBegin(offTick);
    if (worldmapMatrixMode == 0) {
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
    } else {
        setupWorldmapMatrix2(&worldmapCameraVector);
    }
    portWorldmapSweepRender(pcWm60FrameIndex(), !offTick); /* XG_SWEEP_LOG: the picture's values */
    drawWorldmapBillboards();
    renderWorldmapModels();
    setWorldmapGridUpdateMask(&worldmapGridInputPosition);
    if (worldmapGridUpdateMask != 0) {
        updateWorldmapGrids(&worldmapRadarPosition3);
        streamWorldmap0();
        streamWorldmap1();
    }
    worldmapGroundPrepareRenderingTable(&worldmapRadarPosition3);
    drawWorldmapGround(pCurrentWorldmapRenderingStruct->m70_OT, pCurrentWorldmapRenderingStruct->m74_polyFT3, &worldmapRadarPosition3);
    worldmapWaterWave += pcWm60Half(0x40); /* the water's wave: half a step per 60 Hz frame */
    drawWorldmapSkyCloudBand();
    drawWorldmapSkyGradient();
    drawWorldmapClouds();
    pcWm60RenderEnd();
    return 1;
}

/* world map mode init (another mode; tasks worldmapMode9_taskPathCamera_init/E68, worldmapMode9_taskAirship_init/83E8, worldmap_taskSceneNoBillboards_init/8950) */
void worldmapMode9_update(void) {
    RECT rect;

    initWorldmapGraphics();
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 1);
    while (isCDBusy() >= 3) {
        PORT_YIELD();
    }
    finalizeWorldmapEventFileLoading();
    allocAndInitWorldmapState();
    D_8009BE4C = worldMapIdentityMatrix;
    worldmapFadePolyAbr = 1;
    worldmapFadePolyStep = 4;
    worldmapExitVar2 = 0;
    worldmapMatrixMode = 0;
    worldmapCloudsMoveCallback = moveWorldmapClouds;
    worldmapDotInit();
    waitReadCompletion(0);
    readWorldmapSequenceFile();
    worldmapPosition.vx = 0x2000000;
    worldmapPosition.vy = -0x200000;
    worldmapPosition.vz = 0x2000000;
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
    setupWorldmapTask(worldmapMode9_taskPathCamera_init, worldmapMode9_taskPathCamera_update);
    setupWorldmapTask(worldmapMode9_taskAirship_init, worldmapMode9_taskAirship_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
}

/* world map mode free (other mode); exits to field 0x10E, worldmap mode 0 */
void worldmapMode9_free(void) {
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
    gameState.m231A_fieldID = 0x10E;
    gameState.m2320_worldmapMode = 0;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

/* task init: camera flight (fieldSeq typed as a struct so the id load is MEM_IN_STRUCT) */
s32 worldmapMode9_taskPathCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapGeometryOffsetY = 0x78;
    worldmapCameraVar0 = 0x200000;
    e->m20_subState = 0;
    e->m50 = e->m54 = e->m58 = 0;
    worldmapRotation.vx = worldmapRotation.vy = worldmapRotation.vz = 0;
    worldmapMatrixMode = 1;
    playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0xA4);
    e->m22_timer = 0x18;
    return 1;
}

/* task update: camera flight along a Bezier path (jump table jtbl_8006FB40); p = table; p += index */
s32 worldmapMode9_taskPathCamera_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    SVECTOR* p;
    s32 d;
    PathScratch* sp = PATH_SCRATCH;

    e->m54 = (s32)e->m50 >> 12;
    p = worldmapPathCameraPoints;
    p += e->m54;
    if (p[2].pad != -1) {
        sp->mA0_controlPoints[0].vx = p[0].vx;
        sp->mA0_controlPoints[0].vz = p[0].vz;
        sp->mA0_controlPoints[1].vx = p[1].vx;
        sp->mA0_controlPoints[1].vz = p[1].vz;
        sp->mA0_controlPoints[2].vx = p[2].vx;
        sp->mA0_controlPoints[2].vz = p[2].vz;
        sp->mA0_controlPoints[0].vy = p[0].vy;
        sp->mA0_controlPoints[1].vy = p[1].vy;
        sp->mA0_controlPoints[2].vy = p[2].vy;
        evalWorldmapBezier(e->m50 & 0xFFF, &sp->mA0_controlPoints[0], &sp->mA0_controlPoints[1], &sp->mA0_controlPoints[2], &sp->m0);
        worldmapCameraVector.m8_target.vx = worldmapCameraVector.m8_target.vz = 0;
        worldmapCameraVector.m10_up.vx = worldmapCameraVector.m10_up.vz = 0;
        worldmapCameraVector.m10_up.vy = -0x1000;
        worldmapCameraVector.m0_eye.vx = sp->m0.vx >> 16;
        worldmapCameraVector.m0_eye.vz = -(s16)(sp->m0.vz >> 16);
        worldmapCameraVector.m0_eye.vy = sp->m0.vy >> 16;
        worldmapCameraVector.m8_target.vy = worldmapRadarPosition3.vy >> 12;
    }
    setupWorldmapMatrix2(&worldmapCameraVector);
    extractWorldmapEulerAngles(&worldmapMainMatrix2, &worldmapRotation);
    switch (e->m20_subState) {
    case 0:
        e->m58 += 5;
        if (--e->m22_timer < 0) {
            e->m58 = 0x80;
            e->m20_subState++;
            sp->mA0_controlPoints[0].vy = -0x20D;
            sp->mA0_controlPoints[0].vx = (worldmapRadarPosition3.vx >> 12) + 0x32;
            sp->mA0_controlPoints[0].vz = (worldmapRadarPosition3.vz >> 12) - 0x70;
            spawnWorldmapParticles(8, &sp->mA0_controlPoints[0], 0);
        }
        break;
    case 1:
        if (e->m50 >= 0x6000) {
            e->m20_subState++;
        }
        break;
    case 2:
        e->m58 -= 2;
        if (e->m58 < 0x19) {
            e->m58 = 0x18;
            e->m20_subState++;
        }
        break;
    case 3:
        if (e->m50 > 0x7FFF) {
            e->m20_subState++;
        }
        break;
    case 4:
        e->m58 -= 1;
        if (e->m58 < 5) {
            e->m58 = 4;
            e->m20_subState++;
        }
        break;
    case 5:
        if (e->m50 > 0x83FF) {
            fadeSoundEffectVolume((fieldSeq->m14_id << 16) | 0xA4, 0, 0x100);
            changeWorldmapEntityState(0, 0xD);
            worldmapFadePolyAbr = 2;
            worldmapFadePolyStep = 4;
            e->m22_timer = 0x50;
            e->m58 = 0;
            e->m20_subState++;
        }
        break;
    case 6:
        if (--e->m22_timer <= 0) {
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
        }
        break;
    }
    e->m50 += e->m58;
    if (e->m20_subState < 6) {
        sp->m0.vx = worldmapCameraVector.m8_target.vx << 12;
        sp->m0.vy = worldmapCameraVector.m8_target.vy << 12;
        sp->m0.vz = worldmapCameraVector.m8_target.vz << 12;
        sp->m10_eyePosition.vx = worldmapCameraVector.m0_eye.vx << 12;
        sp->m10_eyePosition.vy = worldmapCameraVector.m0_eye.vy << 12;
        sp->m10_eyePosition.vz = worldmapCameraVector.m0_eye.vz << 12;
        d = computeWorldmapDistance2D(&sp->m0, &sp->m10_eyePosition) >> 3;
        sp->m20_distance = d;
        setSoundEffectVolume((fieldSeq->m14_id << 16) | 0xA4, 0x87 - d);
    }
    return 1;
}

/* task init: follow camera (clears 13 slots via attachWorldmapModels, camera from worldmapPosition) */
s32 worldmapMode9_taskAirship_init(s32 index) {
    sWorldmapStateEntry* e;

    attachWorldmapModels(0, 1);
    attachWorldmapModels(0, 2);
    attachWorldmapModels(0, 3);
    attachWorldmapModels(0, 4);
    attachWorldmapModels(0, 5);
    attachWorldmapModels(0, 6);
    attachWorldmapModels(0, 7);
    attachWorldmapModels(0, 8);
    attachWorldmapModels(0, 9);
    attachWorldmapModels(0, 10);
    attachWorldmapModels(0, 11);
    attachWorldmapModels(0, 12);
    attachWorldmapModels(0, 13);
    e = &gWorldmapState->m0_entries[index];
    e->m20_subState = 0;
    e->m28_position = worldmapPosition;
    e->m54 = 0x40;
    e->m58 = 0x200;
    e->m5C = 0x60;
    e->m60 = 0x300;
    e->m50 = 0;
    e->m64 = 0x50;
    worldmapRadarPosition3 = e->m28_position;
    return 1;
}

/* task update: ship parts rotation (Noah sWorldmapModel matrices); needs RotMatrixYXZ declared returning MATRIX* (SDK prototype) */
s32 worldmapMode9_taskAirship_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    ShipScratch* sp = (ShipScratch*)0x1F800000;

    e->m28_position.vz -= 0x4000;
    worldmapGridInputPosition.vz -= 0x4000;
    checkWorldmapPositionSub0(&e->m28_position);
    sp->m0_position.vx = e->m28_position.vx >> 12;
    sp->m0_position.vy = e->m28_position.vy >> 12;
    sp->m0_position.vz = e->m28_position.vz >> 12;
    worldmapModels[0].m8_position = sp->m0_position;
    worldmapRadarPosition3 = e->m28_position;
    e->m50 = (e->m50 + e->m54) & 0xFFF;
    e->m58 = (e->m58 + e->m5C) & 0xFFF;
    e->m60 = (e->m60 + e->m64) & 0xFFF;
    sp->mA0_rot[0].vx = sp->mA0_rot[0].vz = sp->mA0_rot[1].vx = sp->mA0_rot[1].vz = sp->mA0_rot[2].vx = sp->mA0_rot[2].vz = 0;
    sp->mA0_rot[0].vy = e->m50;
    sp->mA0_rot[1].vy = e->m58;
    sp->mA0_rot[2].vy = e->m60;
    sp->mA0_rot[3].vx = sp->mA0_rot[3].vy = 0;
    sp->mA0_rot[3].vz = e->m60;
    RotMatrixYXZ(&sp->mA0_rot[0], &sp->mF0_rotations[0]);
    RotMatrixYXZ(&sp->mA0_rot[1], &sp->mF0_rotations[1]);
    RotMatrixYXZ(&sp->mA0_rot[2], &sp->mF0_rotations[2]);
    RotMatrixYXZ(&sp->mA0_rot[3], &sp->mF0_rotations[3]);
    worldmapModels[1].m20_rotationMatrix = worldmapModels[2].m20_rotationMatrix = worldmapModels[3].m20_rotationMatrix = sp->mF0_rotations[3];
    worldmapModels[4].m20_rotationMatrix = worldmapModels[5].m20_rotationMatrix = sp->mF0_rotations[2];
    worldmapModels[6].m20_rotationMatrix = worldmapModels[10].m20_rotationMatrix = worldmapModels[8].m20_rotationMatrix = worldmapModels[12].m20_rotationMatrix = sp->mF0_rotations[0];
    worldmapModels[7].m20_rotationMatrix = worldmapModels[11].m20_rotationMatrix = worldmapModels[9].m20_rotationMatrix = worldmapModels[13].m20_rotationMatrix = sp->mF0_rotations[1];
    return 1;
}

/* returns 1 (empty task) */
s32 worldmap_taskSceneNoBillboards_init(s32 index) {
    return 1;
}

/* per-frame render task (variant without drawWorldmapBillboards) */
s32 worldmap_taskSceneNoBillboards_update(s32 index) {
    s32 offTick = pcWm60OffTick();

    pcWm60SceneBegin(offTick);
    if (worldmapMatrixMode == 0) {
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
    } else {
        setupWorldmapMatrix2(&worldmapCameraVector);
    }
    portWorldmapSweepRender(pcWm60FrameIndex(), !offTick); /* XG_SWEEP_LOG: the picture's values */
    pcWm60SceneParticles(offTick);
    renderWorldmapModels();
    setWorldmapGridUpdateMask(&worldmapGridInputPosition);
    if (worldmapGridUpdateMask != 0) {
        updateWorldmapGrids(&worldmapRadarPosition3);
        streamWorldmap0();
        streamWorldmap1();
    }
    worldmapGroundPrepareRenderingTable(&worldmapRadarPosition3);
    drawWorldmapGround(pCurrentWorldmapRenderingStruct->m70_OT, pCurrentWorldmapRenderingStruct->m74_polyFT3, &worldmapRadarPosition3);
    worldmapWaterWave += pcWm60Half(0x40); /* the water's wave: half a step per 60 Hz frame */
    drawWorldmapSkyCloudBand();
    drawWorldmapSkyGradient();
    drawWorldmapClouds();
    pcWm60RenderEnd();
    return 1;
}

/* world map mode init (third mode; registers tasks from w_8007A06C too: worldmapMode10_taskRings_init.., so no file boundary evidence at 0x8007A06C from calls) */
void worldmapMode10_update(void) {
    RECT rect;

    initWorldmapGraphics();
    setRECT(&rect, 0, 0, 0x140, 0xD8);
    MoveImage(&rect, 0x2C0, 0x100);
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
    worldmapPosition.vx = 0x2000000;
    worldmapPosition.vy = -0x300000;
    worldmapPosition.vz = 0x2000000;
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
    setupWorldmapTask(worldmapMode10_taskCamera_init, worldmapMode10_taskCamera_update);
    setupWorldmapTask(worldmapMode10_taskAirship_init, worldmapMode10_taskAirship_update);
    setupWorldmapTask(worldmapMode10_taskRings_init, worldmapMode10_taskRings_update);
    setupWorldmapTask(worldmapMode10_taskTrail_init, worldmapMode10_taskTrail_update);
    setupWorldmapTask(worldmapMode10_taskBurst_init, worldmapMode10_taskBurst_update);
    setupWorldmapTask(worldmapMode10_taskGroundModel_init, worldmapMode10_taskGroundModel_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

/* world map mode free (third mode); exits to field 0x110, worldmap mode 0 */
void worldmapMode10_free(void) {
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
    gameState.m231A_fieldID = 0x110;
    gameState.m2320_worldmapMode = 0;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

/* task init: scripted camera (state 0x10) */
s32 worldmapMode10_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapGeometryOffsetY = 0x78;
    worldmapCameraVar0 = 0x200000;
    worldmapMatrixMode = 0;
    e->m20_subState = 0x10;
    e->m50 = e->m54 = e->m58 = 0;
    worldmapRotation.vx = -0xC0;
    worldmapRotation.vy = 0x680;
    worldmapRotation.vz = 0;
    e->m22_timer = 0x40;
    e->m74 = 0;
    return 1;
}

/* task update: scripted camera with shake (19-case switch, jump table jtbl_8006FB60) */
s32 worldmapMode10_taskCamera_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    ShakeScratch* sp = (ShakeScratch*)0x1F800000;

    switch (e->m20_subState) {
    case 0:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0x20;
            e->m20_subState++;
            changeWorldmapEntityState(5, 1);
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x62);
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x63);
        }
        break;
    case 1:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0x20;
            e->m20_subState++;
            changeWorldmapEntityState(3, 1);
        }
        break;
    case 2:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0x30;
            e->m20_subState++;
            changeWorldmapEntityState(4, 1);
            changeWorldmapEntityState(2, 1);
            changeWorldmapEntityState(6, 1);
        }
        break;
    case 3:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0x28;
            e->m20_subState++;
        }
        break;
    case 4:
        e->m22_timer--;
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        sp->mA0_shake.vx = rand() % 12 - 6;
        sp->mA0_shake.vy = rand() % 12 - 6;
        worldmapCameraVector.m0_eye.vx += sp->mA0_shake.vx;
        worldmapCameraVector.m8_target.vx += sp->mA0_shake.vx;
        worldmapCameraVector.m0_eye.vy += sp->mA0_shake.vy;
        worldmapCameraVector.m8_target.vy += sp->mA0_shake.vy;
        if (e->m22_timer <= 0) {
            e->m22_timer = 0x78;
            e->m20_subState++;
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x79);
            fadeSoundEffectVolume((fieldSeq->m14_id << 16) | 0x62, 0, 0x100);
            fadeSoundEffectVolume((fieldSeq->m14_id << 16) | 0x63, 0, 0x100);
        }
        break;
    case 5:
        e->m22_timer--;
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        sp->mA0_shake.vx = rand() % 4 - 2;
        sp->mA0_shake.vy = rand() % 4 - 2;
        worldmapCameraVector.m0_eye.vx += sp->mA0_shake.vx;
        worldmapCameraVector.m0_eye.vy += sp->mA0_shake.vy;
        worldmapCameraVector.m8_target.vx += sp->mA0_shake.vx;
        worldmapCameraVector.m8_target.vy += sp->mA0_shake.vy;
        if (e->m22_timer <= 0) {
            e->m22_timer = 0x5A;
            e->m20_subState++;
        }
        break;
    case 6:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0x50;
            e->m20_subState++;
            changeWorldmapEntityState(0, 0xD);
            fadeSoundEffectVolume((fieldSeq->m14_id << 16) | 0x79, 0, 0x100);
            worldmapFadePolyAbr = 2;
            worldmapFadePolyStep = 4;
        }
        break;
    case 7:
        if (--e->m22_timer <= 0) {
            e->m22_timer = 0;
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
        }
        break;
    case 16:
        if (e->m4_request == 1) {
            fadeSoundEffectVolume((fieldSeq->m14_id << 16) | 0x36, 0, 8);
            changeWorldmapEntityState(0, 0xD);
            worldmapFadePolyStep = 0x20;
            worldmapFadePolyAbr = 1;
            e->m22_timer = 8;
            e->m4_request = 0;
            e->m20_subState++;
        }
        break;
    case 17:
        if (--e->m22_timer <= 0) {
            pcWm60Cut(); /* Enhanced, 60 Hz: a cut (w_80070CFC.c) */
            worldmapCameraVar0 = 0x960000;
            worldmapRotation.vx = -0x30;
            worldmapRotation.vy = 0x40;
            worldmapRotation.vz = 0;
            e->m22_timer = 0x10;
            e->m20_subState++;
        }
        break;
    case 18:
        if (--e->m22_timer <= 0) {
            changeWorldmapEntityState(0, 0xC);
            worldmapFadePolyAbr = 1;
            worldmapFadePolyStep = 0x80;
            e->m20_subState = 0;
            e->m22_timer = 1;
        }
        break;
    }
    if (e->m20_subState != 4 && e->m20_subState != 5) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    return 1;
}

/* task init: place world model 16 (Noah sWorldmapModel) */
s32 worldmapMode10_taskGroundModel_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[16];

    e->m28_position.vx = 0x2000000;
    e->m28_position.vz = 0x1500000;
    e->m20_subState = 0;
    m->m18_rotation.vx = 0;
    m->m18_rotation.vy = 0x780;
    m->m18_rotation.vz = 0;
    RotMatrixYXZ(&m->m18_rotation, &m->m20_rotationMatrix);
    return 1;
}

/* task update: keep model 16 on the ground (getWorldmapElevationWithWater = altitude) */
s32 worldmapMode10_taskGroundModel_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    if (e->m4_request != 0) {
        e->m4_request = 0;
        worldmapModels[16].m0_hidden = 1;
    }
    checkWorldmapPositionSub0(&e->m28_position);
    e->m28_position.vy = getWorldmapElevationWithWater(e->m28_position.vx, e->m28_position.vz) + 0x18000;
    worldmapModels[16].m8_position.vx = e->m28_position.vx >> 12;
    worldmapModels[16].m8_position.vy = e->m28_position.vy >> 12;
    worldmapModels[16].m8_position.vz = e->m28_position.vz >> 12;
    return 1;
}

/* task init: follow camera + model 0 rotation, plays sound 0x36 */
s32 worldmapMode10_taskAirship_init(s32 index) {
    sWorldmapStateEntry* e;

    attachWorldmapModels(0, 1);
    attachWorldmapModels(0, 2);
    attachWorldmapModels(0, 3);
    attachWorldmapModels(0, 4);
    attachWorldmapModels(0, 5);
    attachWorldmapModels(0, 6);
    attachWorldmapModels(0, 7);
    attachWorldmapModels(0, 8);
    attachWorldmapModels(0, 9);
    attachWorldmapModels(0, 10);
    attachWorldmapModels(0, 11);
    attachWorldmapModels(0, 12);
    attachWorldmapModels(0, 13);
    e = &gWorldmapState->m0_entries[index];
    e->m20_subState = 0;
    e->m28_position = worldmapPosition;
    e->m54 = 0x40;
    e->m58 = 0x200;
    e->m5C = 0x60;
    e->m60 = 0x300;
    e->m50 = 0;
    e->m64 = 0x50;
    worldmapRadarPosition3 = e->m28_position;
    worldmapModels[0].m18_rotation.vx = -0x100;
    worldmapModels[0].m18_rotation.vy = 0;
    worldmapModels[0].m18_rotation.vz = -0x40;
    playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x36);
    return 1;
}

/* task update: ship take-off/landing sequence (jump table jtbl_8006FBB0) */
s32 worldmapMode10_taskAirship_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = worldmapModels;
    ShipScratch* sp = (ShipScratch*)0x1F800000;

    switch (e->m20_subState) {
    case 0:
        sp->mA0_rot[0].vx = e->m28_position.vx >> 12;
        sp->mA0_rot[0].vy = e->m28_position.vy >> 12;
        sp->mA0_rot[0].vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0xB, &sp->mA0_rot[0], 0);
        e->m28_position.vy += 0x4000;
        e->m28_position.vz += -0x8000;
        worldmapGridInputPosition.vz += -0x8000;
        if (e->m28_position.vy >= -0x18000) {
            e->m38_step.vy = -0x4000;
            e->m20_subState++;
            spawnWorldmapParticles(0xC, &sp->mA0_rot[0], 0);
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x71);
            e->m22_timer = 0x20;
        }
        break;
    case 1:
        sp->mA0_rot[0].vx = e->m28_position.vx >> 12;
        sp->mA0_rot[0].vy = e->m28_position.vy >> 12;
        sp->mA0_rot[0].vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0xB, &sp->mA0_rot[0], 0);
        spawnWorldmapParticles(0xC, &sp->mA0_rot[0], 0);
        if (--e->m22_timer <= 0) {
            clearWorldmapParticles(0xC);
        }
        m->m18_rotation.vx += 4;
        m->m18_rotation.vz += 2;
        e->m28_position.vy += e->m38_step.vy;
        e->m38_step.vy += 0x100;
        e->m28_position.vz += -0x8000;
        worldmapGridInputPosition.vz += -0x8000;
        if (e->m28_position.vy >= -0x18000) {
            e->m38_step.vz = -0x8000;
            e->m20_subState++;
            spawnWorldmapParticles(0xC, &sp->mA0_rot[0], 0);
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x71);
        }
        break;
    case 2:
        sp->mA0_rot[0].vx = e->m28_position.vx >> 12;
        sp->mA0_rot[0].vy = e->m28_position.vy >> 12;
        sp->mA0_rot[0].vz = e->m28_position.vz >> 12;
        spawnWorldmapParticles(0xB, &sp->mA0_rot[0], 0);
        spawnWorldmapParticles(0xC, &sp->mA0_rot[0], 0);
        m->m18_rotation.vx -= 2;
        m->m18_rotation.vz -= 1;
        e->m28_position.vz += e->m38_step.vz;
        worldmapGridInputPosition.vz += e->m38_step.vz;
        e->m38_step.vz += 0x100;
        if (e->m28_position.vz <= 0x1580000) {
            e->m22_timer = 8;
            e->m20_subState++;
            clearWorldmapParticles(0xC);
            changeWorldmapEntityState(1, 1);
        }
        break;
    case 3:
        m->m18_rotation.vx -= 2;
        m->m18_rotation.vz -= 1;
        e->m28_position.vz += e->m38_step.vz;
        worldmapGridInputPosition.vz += e->m38_step.vz;
        e->m38_step.vz += 0x100;
        if (--e->m22_timer <= 0) {
            changeWorldmapEntityState(1, 1);
            e->m20_subState++;
        }
        break;
    case 4:
        if (e->m4_request != 0) {
            e->m4_request = 0;
            m[0].m0_hidden = m[1].m0_hidden = m[2].m0_hidden = m[3].m0_hidden = m[4].m0_hidden = m[5].m0_hidden =
                m[6].m0_hidden = m[7].m0_hidden = m[8].m0_hidden = m[9].m0_hidden = m[10].m0_hidden =
                    m[11].m0_hidden = m[12].m0_hidden = m[13].m0_hidden = 1;
            clearWorldmapParticles(0xB);
        }
        break;
    }
    checkWorldmapPositionSub0(&e->m28_position);
    worldmapModels[0].m8_position.vx = e->m28_position.vx >> 12;
    worldmapModels[0].m8_position.vy = e->m28_position.vy >> 12;
    worldmapModels[0].m8_position.vz = e->m28_position.vz >> 12;
    worldmapRadarPosition3 = e->m28_position;
    e->m50 = (e->m50 + e->m54) & 0xFFF;
    e->m58 = (e->m58 + e->m5C) & 0xFFF;
    e->m60 = (e->m60 + e->m64) & 0xFFF;
    sp->mA0_rot[0].vx = sp->mA0_rot[0].vz = sp->mA0_rot[1].vx = sp->mA0_rot[1].vz = sp->mA0_rot[2].vx = sp->mA0_rot[2].vz = 0;
    sp->mA0_rot[0].vy = e->m50;
    sp->mA0_rot[1].vy = e->m58;
    sp->mA0_rot[2].vy = e->m60;
    sp->mA0_rot[3].vx = sp->mA0_rot[3].vy = 0;
    sp->mA0_rot[3].vz = e->m60;
    RotMatrixYXZ(&sp->mA0_rot[0], &sp->mF0_rotations[0]);
    RotMatrixYXZ(&sp->mA0_rot[1], &sp->mF0_rotations[1]);
    RotMatrixYXZ(&sp->mA0_rot[2], &sp->mF0_rotations[2]);
    RotMatrixYXZ(&sp->mA0_rot[3], &sp->mF0_rotations[3]);
    worldmapModels[1].m20_rotationMatrix = worldmapModels[2].m20_rotationMatrix = worldmapModels[3].m20_rotationMatrix = sp->mF0_rotations[3];
    worldmapModels[4].m20_rotationMatrix = worldmapModels[5].m20_rotationMatrix = sp->mF0_rotations[2];
    worldmapModels[6].m20_rotationMatrix = worldmapModels[10].m20_rotationMatrix = worldmapModels[8].m20_rotationMatrix = worldmapModels[12].m20_rotationMatrix = sp->mF0_rotations[0];
    worldmapModels[7].m20_rotationMatrix = worldmapModels[11].m20_rotationMatrix = worldmapModels[9].m20_rotationMatrix = worldmapModels[13].m20_rotationMatrix = sp->mF0_rotations[1];
    RotMatrixYXZ(&worldmapModels[0].m18_rotation, &sp->mF0_rotations[0]);
    worldmapModels[0].m20_rotationMatrix = sp->mF0_rotations[0];
    return 1;
}
