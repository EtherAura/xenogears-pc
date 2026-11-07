/*
 * Modes 17/18, world map models (init, render, collisions against model
 * triangles), sprite actors, FT4 decoration and cloud layers, particles, player and
 * party-member tasks.
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "pc_interpolation.h"
#include "gte.h"
#include "ot.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"
#include "pc_convert.h"

extern u16 worldmapWaypointShipX[8];
extern u16 worldmapWaypointShipZ[8];
extern s16 worldmapFlyingShipModelLinks[];   /* pairs of model indices, -1 terminated */
extern s16 worldmapEventModelChildren[];
extern u16 worldmapTranslucentSpinModelPairs[10][2];
extern u16 worldmapWaypointShipModels[];
extern u16 worldmapFlyingShipModelBases[];
extern u16 worldmapColliderAModels[];
extern u16 worldmapColliderCModels[];
extern sWorldmapParticle* worldmapFile1Buffer_18; /* particle emitters (0x200) */
extern sWorldmapParticle2* worldmapParticleInstances;
extern POLY_FT4* worldmapFT4_1[2];   /* Noah: worldmapFT4_1 (worldmapFT4_1[0], worldmapFT4_1[1]) */
typedef struct { POLY_FT4 m[256]; } PolyFT4Block;
extern s32 worldMapGearMode;
extern u32 worldmapGamestate1824;
extern s32 worldmapNumActivePartyMembers;
extern s16 currentWorldmapDynamicCollisionSlot;
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
extern u8 targetVehicleEntityIndex;
extern u8 adjustLocationAfterCollisionVar0;
extern u16 D_8009B180[];
extern SVECTOR worldmapParticleQuadVertices[10][4];         /* particle quad vertices per type */
extern SVECTOR D_8009B058[];              /* fourth vertex, stride four SVECTORs */
extern u16 D_8009AFF6[];                  /* fourth packed UV, stride four halfwords */
extern u16 worldmapParticleQuadUVs[10][4];             /* particle quad uvs per type */
extern s16 worldmapFollowLeaderDataIndex;
extern sWorldmapFollowLeader worldmapFollowLeaderData[32];
extern s16 worldmapPlayerDirection;                    /* Noah: worldmapVar_8009d52c */
extern AnimationBundle* worldmapPartySprites[3];
extern SVECTOR worldmapWaypointShipSteps[32];
void setWorldmapModelSemiTrans(sWorldmapModel* model, POLY_FT4* poly, s32 count, s32 abr);
s32 worldmap_taskTranslucentSpin_reinit(s32 index);
s32 worldmap_taskWaypointShip_reinit(s32 index);
s32 worldmap_taskFlyingShip_reinit(s32 index);
void attachWorldmapModels(s32 a, s32 b);
s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z);
void checkWorldmapPositionSub0(VECTOR* v);
void extractWorldmapEulerAngles(MATRIX* m, SVECTOR* r);
s32 SquareRoot0(s32 a);
s32 computeWorldmapDistance2D(VECTOR* a, VECTOR* b);
void selectYggdrasilExitDirection(VECTOR* pos, VECTOR* target, VECTOR* step, s16* angle);
s32 stepGearAwayFromYggdrasil(sWorldmapStateEntry* p);
void moveWorldmapParticles(void);
s32 worldmapUpdatePlayerControls(sWorldmapStateEntry* p);
s32 checkWorldmapPosition(VECTOR* pos, VECTOR* step, VECTOR* out, s32 scale, s32 mode);
void processWorldmapDynamicCollisions(VECTOR* pos, s32 a, s32 b, u8* out0, u8* out1);
void updateWorldmapEncounterTimers(void);                                /* Noah: followLeaderPostProcess */
s32 setupWorldmapExits(VECTOR* position, s32 mode);
void spawnParticlesForTerrainType3(s32 type, sWorldmapStateEntry* entry, sWorldmapScratch* scratch);
s32 changeWorldmapEntityState(s32 index, s32 state);
void addWorldmapTrackMark(s16 type, VECTOR* position);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
SpriteActor* createSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);


extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern s32 worldmapMode18Script[51]; /* mode 18's scene script (to worldmapModelsRenderModes) */
extern POLY_FT4* worldmapFT4_3[2]; /* worldmapFT4_3[0]/[1]: POLY_FT4 x0x200 per buffer */
extern SVECTOR* worldmapCloudSteps; /* cloud steps */
extern VECTOR* worldmapCloudPositions;  /* cloud positions */
extern POLY_FT4* worldmapFT4_2[2]; /* worldmapFT4_2[0]/[1]: POLY_FT4 x0x120 per buffer */
extern VECTOR worldmapPosition; /* Noah: worldmapPosition? */
extern VECTOR worldmapRadarPosition3;
extern VECTOR worldmapRadarPosition;
extern SVECTOR worldmapRotation; /* Noah: worldmapRotation? */
extern s32 worldmapMatrixMode;
extern s32 worldmapCameraVar0;
extern s32 worldmapGeometryOffsetY;
extern s16 worldmapNumModels;
void freeWorldmapChunks(void);
void setupInitialGrid(VECTOR* position);
s32 loadWorldmapChunk(void);
s32 isWorldStreamingDone(void);
s32 worldmapCheckCollisionsAgainstWorldmapModel(VECTOR* position, s32 modelIndex);
void pcWideWorldmapBillboards(void);
void pcWideWorldmapClouds(void);

/* Scratchpad (0x1F800000) layout used by this file's functions. */
typedef struct WorldmapScratch {
    /* 0x00 */ VECTOR m0_vertices[3];
    /* 0x30 */ s32 m30_sxy[3];
    /* 0x3C */ s32 m3C;
    /* 0x40 */ VECTOR m40_delta;
    /* 0x50 */ u8 m50_pad[0x50];
    /* 0xA0 */ SVECTOR mA0_particlePosition;
    /* 0xA8 */ SVECTOR mA8;
    /* 0xB0 */ u8 mB0_pad[0x40];
    /* 0xF0 */ MATRIX mF0_matrix;
} WorldmapScratch;
extern s16 worldmapModelCollisionHits[]; /* collision hits: (triangle index, mC) pairs */
extern MATRIX D_8009BE4C;
extern MATRIX worldMapIdentityMatrix;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern sWorldmapSeqHeader* fieldSeq;
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern s32 worldmapExitToFieldVar0;
void initWorldmapGraphics(void);
void worldmapFadeIn(s32, s32, s32, s32);
void finalizeWorldmapEventFileLoading(void);
void allocAndInitWorldmapState(void);
void worldmapDotInit(void);
void readWorldmapSequenceFile(void);
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
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
s32 worldmap_taskFader_init(s32);
s32 worldmap_taskFader_update(s32);
s32 worldmapMode18_taskScript_init(s32);
s32 worldmap_taskScript_update(s32);
s32 worldmapMode18_taskCamera_init(s32);
s32 worldmapMode18_taskCamera_update(s32);
s32 worldmapMode18_taskRisingModel_init(s32);
s32 worldmapMode18_taskRisingModel_update(s32);
s32 worldmap_taskSceneNoBillboards_init(s32);
s32 worldmap_taskSceneNoBillboards_update(s32);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void initWorldmapEncounterTimers(void);
void moveWorldmapClouds(void);
void freeWorldmapModels(void);
void freeWorldmapClouds(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapPolyFT4Array1(void);
void freeWorldmapState(void);
extern sWorldmapOffsetEntry* worldmapFile1Buffer_14; /* 0x100 entries, relocated by initWorldmapBillboards */
extern u16 worldmapBillboardCluts[16]; /* billboard CLUTs */
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
extern s32 initWorldMapVar0;
extern u16 worldmapSpinModelPairs[10][2];
#define setPolyFT4(p) (setlen(p, 9), setcode(p, 0x2C))
extern Model* worldmapModelFiles;
extern s16 worldmapFile1Buffer_8_numEntries;
extern s32* worldmapFile1Buffer_C; /* collision mesh offsets */
extern u16* pWorldmapModelsConfig;
extern MATRIX worldmapColorMatrix;
extern MATRIX worldmapLightMatrix;
extern s32 gDepthDivider;
extern s32 worldmapModelVar0;
extern s32 worldmapModelVar1;
extern void* worldmapFile3Buffer;
extern u16 worldmapFile3Cluts[];       /* Noah: SHORT_ARRAY_8009bce0 */
void loadMenuImageBundle(void* data);
void tweakWorldmapTexture(u16* src, u16* dst, s32 steps, u8* color);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void clearWorldmapParticles(s32 type);
s32 approachWorldmapValue(s32 value, s32 target, s32 step);
extern VECTOR worldmapGridInputPosition;
extern sWorldmapCameraState worldmapCameraVector;
void computeCameraVector(sWorldmapCameraState* camera, VECTOR* position, s32 distance, SVECTOR* rotation);
void smoothWorldmapCameraRotation(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
void smoothWorldmapCameraDistance(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
void smoothWorldmapCameraPosition(sWorldmapStateEntry* e, sWorldmapCameraDelta* d);
s32 rand(void);
extern VECTOR worldmapCloudOrigins[5];
typedef struct WorldmapSpriteScratch {
    /* 0x00 */ SVECTOR m0_position;
    /* 0x08 */ VECTOR m8_delta;
    /* 0x18 */ s32 m18_depths[0x40];
} WorldmapSpriteScratch;
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern MATRIX worldmapMainMatrix2;
void computeWormapSpritePosition(VECTOR* v);
void setCurrentRenderingMatrix(MATRIX* m);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
typedef struct WorldmapQuadScratch {
    /* 0x00 */ SVECTOR m0_quad[4];
    /* 0x20 */ u8 m20_pad[8];
    /* 0x28 */ MATRIX m28_matrix;
    /* 0x48 */ MATRIX m48_rotation;
    /* 0x68 */ u16 m68_cluts[16];
} WorldmapQuadScratch;
extern s16 worldmapBillboardPolyCount;
extern s16 megatilesVisibility[5][5];
extern s16 worldmapGrid[81];
extern SVECTOR worldmapGridPosition; /* current grid cell (vx, vz) */
extern s32 worldmapOddOrEven;
void drawWorldmapCellBillboards(s16* pos, s32 count, OTag* ot, POLY_FT4* polys);
typedef struct WorldmapModelScratch {
    /* 0x000 */ VECTOR m0_delta;
    /* 0x010 */ VECTOR m10_scale;
    /* 0x020 */ s32 m20_flag;
    /* 0x024 */ s32 m24;
    /* 0x028 */ s32 m28_depth;
    /* 0x02C */ u8 m2C_pad[0x74];
    /* 0x0A0 */ SVECTOR mA0_origin;
    /* 0x0A8 */ u8 mA8_pad[0x48];
    /* 0x0F0 */ MATRIX mF0_matrix;
    /* 0x110 */ MATRIX m110_matrix;
} WorldmapModelScratch;
extern s32 objectClippingMask;
extern s32 fieldPolyCount2;
extern s32 fieldPolyCount;
extern s16 worldmapModelsRenderModes[];
void adjustWorldModelLocation(VECTOR* v);
s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
typedef struct WorldmapTriScratch {
    /* 0x00 */ VECTOR m0_vertex0;
    /* 0x10 */ VECTOR m10_vertex1;
    /* 0x20 */ VECTOR m20_vertex2;
    /* 0x30 */ u8 m30_pad[0xC0];
    /* 0xF0 */ MATRIX mF0_matrix;
} WorldmapTriScratch;
void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);
void averageVector(VECTOR* position, VECTOR* point, VECTOR* normal);
typedef struct WorldmapPlaneScratch {
    /* 0x000 */ VECTOR m0_vertex0;
    /* 0x010 */ VECTOR m10_vertex1;
    /* 0x020 */ VECTOR m20_vertex2;
    /* 0x030 */ VECTOR m30_normal;
    /* 0x040 */ VECTOR m40_result;
    /* 0x050 */ u8 m50_pad[0xA0];
    /* 0x0F0 */ MATRIX mF0_matrix;
    /* 0x110 */ MATRIX m110_segment;
} WorldmapPlaneScratch;
typedef struct WorldmapEdgeScratch {
    /* 0x00 */ VECTOR m0_vertex0;
    /* 0x10 */ VECTOR m10_vertex1;
    /* 0x20 */ VECTOR m20_vertex2;
    /* 0x30 */ VECTOR m30_edgeDir0;
    /* 0x40 */ VECTOR m40_edgeDir1;
    /* 0x50 */ VECTOR m50_edgeDir2;
    /* 0x60 */ u8 m60_pad[0x90];
    /* 0xF0 */ MATRIX mF0_matrix;
} WorldmapEdgeScratch;

/* mode-17 task init (Noah: dummy task init, worldmapTask.cpp) */
s32 worldmapTaskDummy(s32 index) {
    return 1;
}

/* mode-17 task update: m4==1 -> re-stream grid (setupInitialGrid/loadWorldmapChunk loop) */
s32 worldmapMode17_taskRestream_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    if (e->m4_request == 1) {
        e->m4_request = 0;
        DrawSync(0);
        VSync(0);
        freeWorldmapChunks();
        setupInitialGrid(&worldmapPosition);
        do {
            loadWorldmapChunk();
            VSync(0);
        } while (isWorldStreamingDone() > 0);
    }
    return 1;
}

/* mode-18 main (worldmapModes[18].update; cf. Noah worldmapMode0_update) */
void worldmapMode18_update(void) {
    RECT rect;

    initWorldmapGraphics();
    rect.w = 0x140;
    rect.x = 0;
    rect.y = 0;
    rect.h = 0xD8;
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    /* The batch load's CD interrupts must come in (as in the other modes). */
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
    worldmapPosition.vx = 0x1800000;
    worldmapPosition.vy = 0xFFF00000;
    worldmapPosition.vz = 0x1A00000;
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
    setupWorldmapTask(worldmapMode18_taskScript_init, worldmap_taskScript_update);
    setupWorldmapTask(worldmapMode18_taskCamera_init, worldmapMode18_taskCamera_update);
    setupWorldmapTask(worldmapMode18_taskRisingModel_init, worldmapMode18_taskRisingModel_update);
    setupWorldmapTask(worldmap_taskSceneNoBillboards_init, worldmap_taskSceneNoBillboards_update);
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    initWorldmapEncounterTimers();
}

/* mode-18 free (cf. Noah worldmapMode0_free) */
void worldmapMode18_free(void) {
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
    gameState.m231A_fieldID = 0x269;
    gameState.m2320_worldmapMode = 4;
    worldmapExitToFieldVar0 = 1;
    gameState.m231C_cameraYaw = worldmapRotation.vy;
}

/* mode-18 task init (m50 = &worldmapMode18Script) */
s32 worldmapMode18_taskScript_init(s32 index) {
    sWorldmapStateEntry* e = gWorldmapState->m0_entries;
    e[index].m50 = (sptr)worldmapMode18Script;
    return 1;
}

s32 worldmapMode18_taskCamera_init(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];

    worldmapGeometryOffsetY = 0x78;
    e->m7C = 0x1000;
    e->m28_position.vx = worldmapRadarPosition.vx = worldmapRadarPosition3.vx = worldmapPosition.vx;
    e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = worldmapPosition.vy;
    e->m28_position.vz = worldmapRadarPosition.vz = worldmapRadarPosition3.vz = worldmapPosition.vz;
    e->m20_subState = 0;
    e->m4_request = 0;
    e->m5C = 0x960000;
    worldmapRotation.vx = -0x20;
    worldmapRotation.vy = 0x400;
    worldmapRotation.vz = 0;
    e->m38_step.vx = e->m50 = worldmapRotation.vx << 12;
    worldmapMatrixMode = 0;
    worldmapCameraVar0 = 0x960000;
    e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
    e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
    return 1;
}
/* mode-18 camera task update (cf. Noah worldmap_taskCamera_update); 2 jump tables 0x80070490/0x800704A8 */
void pcWm60Cut(void);

s32 worldmapMode18_taskCamera_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    WorldmapScratch* sp = (WorldmapScratch*)0x1F800000;
    s32 shake;

    switch (e->m4_request) {
    case 1:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 1;
        e->m4_request = 0;
        e->m5C = 0x960000;
        worldmapRotation.vx = -0x20;
        worldmapRotation.vy = 0x400;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0xFFFE0000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x960000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        break;
    case 2:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 2;
        e->m4_request = 0;
        e->m5C = 0x180000;
        worldmapRotation.vx = 0x40;
        worldmapRotation.vy = 0x1A0;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0x40000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x180000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = 0xFFF60000;
        break;
    case 3:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 3;
        e->m4_request = 0;
        e->m5C = 0x120000;
        worldmapRotation.vx = -0x2A0;
        worldmapRotation.vy = 0x6A0;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0xFFD60000;
        worldmapCameraVar0 = 0x120000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = worldmapRadarPosition3.vx = 0x1700000;
        worldmapGridInputPosition.vx = worldmapGridInputPosition.vx + e->m28_position.vx - worldmapPosition.vx;
        break;
    case 4:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 4;
        e->m4_request = 0;
        e->m5C = 0x180000;
        worldmapRotation.vx = -0x10;
        worldmapRotation.vy = 0xA50;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0xFFFF0000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        worldmapCameraVar0 = 0x180000;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vx = worldmapRadarPosition.vx = worldmapRadarPosition3.vx = 0x1800000;
        e->m28_position.vz = worldmapRadarPosition.vz = worldmapRadarPosition3.vz = 0x1900000;
        e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = 0xFFF80000;
        worldmapGridInputPosition.vx += 0x100;
        worldmapGridInputPosition.vz -= 0x100;
        break;
    case 5:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 5;
        e->m4_request = 0;
        e->m5C = 0x260000;
        worldmapRotation.vx = -0x100;
        worldmapRotation.vy = 0x670;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0xFFF00000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = 0xFFEF0000;
        e->m28_position.vz = worldmapRadarPosition.vz = worldmapRadarPosition3.vz = 0x1A80000;
        worldmapCameraVar0 = 0x260000;
        worldmapGridInputPosition.vz += 0x180;
        break;
    case 6:
        pcWm60Cut(); /* Enhanced, 60 Hz: a new shot: a cut (w_80070CFC.c) */
        e->m20_subState = 6;
        e->m4_request = 0;
        e->m5C = 0x620000;
        worldmapRotation.vx = -0x80;
        worldmapRotation.vy = 0xA70;
        worldmapRotation.vz = 0;
        e->m38_step.vx = e->m50 = 0xFFF80000;
        e->m38_step.vy = e->m54 = worldmapRotation.vy << 12;
        e->m38_step.vz = e->m58 = worldmapRotation.vz << 12;
        e->m28_position.vy = worldmapRadarPosition.vy = worldmapRadarPosition3.vy = 0xFFEE0000;
        e->m28_position.vz = worldmapRadarPosition.vz = worldmapRadarPosition3.vz = 0x1980000;
        worldmapCameraVar0 = 0x620000;
        worldmapGridInputPosition.vz -= 0x100;
        break;
    }
    if (worldmapMatrixMode == 0) {
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m50 = approachWorldmapValue((s32)e->m50, 0xFFC00000, -0x8000);
        e->m54 = approachWorldmapValue((s32)e->m54, 0, -0x8000);
        e->m5C = approachWorldmapValue(e->m5C, 0x380000, 0xFFFF3000);
        break;
    case 2:
        break;
    case 3:
        break;
    case 4:
        break;
    case 5:
        e->m50 = approachWorldmapValue((s32)e->m50, 0x200000, 0x2000);
        break;
    case 6:
        break;
    }
    smoothWorldmapCameraRotation(e, (sWorldmapCameraDelta*)sp);
    smoothWorldmapCameraDistance(e, (sWorldmapCameraDelta*)sp);
    smoothWorldmapCameraPosition(e, (sWorldmapCameraDelta*)sp);
    sp->mA0_particlePosition.vy = rand() % (e->m7C >> 12) - (e->m7C >> 13);
    worldmapCameraVector.m0_eye.vy += sp->mA0_particlePosition.vy;
    worldmapCameraVector.m8_target.vy += sp->mA0_particlePosition.vy;
    return 1;
}

/* mode-18 model task init */
s32 worldmapMode18_taskRisingModel_init(s32 index) {
    sWorldmapStateEntry* e = gWorldmapState->m0_entries;
    sWorldmapModel* m = worldmapModels;
    e += index;
    e->m28_position.vx = 0x1800000;
    e->m28_position.vy = 0x80000;
    e->m28_position.vz = 0x1A00000;
    e->m20_subState = 0;
    m[8].m0_hidden = 1;
    m[7].m0_hidden = 1;
    m[7].m8_position.vx = m[8].m8_position.vx = e->m28_position.vx >> 12;
    m[7].m8_position.vy = m[8].m8_position.vy = e->m28_position.vy >> 12;
    m[7].m8_position.vz = m[8].m8_position.vz = e->m28_position.vz >> 12;
    return 1;
}

/* mode-18 model/particle task update; jump table 0x800704C8 */
s32 worldmapMode18_taskRisingModel_update(s32 index) {
    sWorldmapState* st;
    sWorldmapModel* m;
    WorldmapScratch* sp;
    sWorldmapModel* p;
    sWorldmapStateEntry* e;
    st = gWorldmapState;
    m = worldmapModels;
    e = &st->m0_entries[index];
    p = &m[7];
    sp = (WorldmapScratch*)0x1F800000;

    switch (e->m4_request) {
    case 1:
        e->m4_request = 0;
        e->m20_subState = 1;
        m[8].m0_hidden = 0;
        m[7].m0_hidden = 0;
        break;
    case 2:
        e->m4_request = 0;
        e->m20_subState = 2;
        clearWorldmapParticles(3);
        clearWorldmapParticles(5);
        clearWorldmapParticles(6);
        clearWorldmapParticles(7);
        clearWorldmapParticles(10);
        break;
    case 3:
        e->m4_request = 0;
        e->m20_subState = 3;
        clearWorldmapParticles(7);
        clearWorldmapParticles(8);
        clearWorldmapParticles(9);
        break;
    case 4:
        e->m4_request = 0;
        e->m20_subState = 4;
        clearWorldmapParticles(5);
        clearWorldmapParticles(6);
        clearWorldmapParticles(7);
        clearWorldmapParticles(10);
        clearWorldmapParticles(11);
        clearWorldmapParticles(12);
        break;
    }
    switch (e->m20_subState) {
    case 0:
        break;
    case 1:
        e->m28_position.vy = approachWorldmapValue(e->m28_position.vy, 0xFFE80000, -0x400);
        sp->mA0_particlePosition.vx = e->m28_position.vx >> 12;
        sp->mA0_particlePosition.vz = e->m28_position.vz >> 12;
        sp->mA0_particlePosition.vy = e->m28_position.vy >> 12;
        spawnWorldmapParticles(3, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(5, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(6, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(7, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(10, &sp->mA0_particlePosition, NULL);
        break;
    case 2:
        e->m28_position.vy = approachWorldmapValue(e->m28_position.vy, 0xFFE80000, -0x400);
        sp->mA0_particlePosition.vx = e->m28_position.vx >> 12;
        sp->mA0_particlePosition.vz = e->m28_position.vz >> 12;
        sp->mA0_particlePosition.vy = e->m28_position.vy >> 12;
        spawnWorldmapParticles(7, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(8, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(9, &sp->mA0_particlePosition, NULL);
        break;
    case 3:
        e->m28_position.vy = approachWorldmapValue(e->m28_position.vy, 0xFFE80000, -0x400);
        sp->mA0_particlePosition.vx = e->m28_position.vx >> 12;
        sp->mA0_particlePosition.vz = e->m28_position.vz >> 12;
        sp->mA0_particlePosition.vy = e->m28_position.vy >> 12;
        spawnWorldmapParticles(5, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(6, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(7, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(10, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(11, &sp->mA0_particlePosition, NULL);
        spawnWorldmapParticles(12, &sp->mA0_particlePosition, NULL);
        break;
    case 4:
        e->m28_position.vy = approachWorldmapValue(e->m28_position.vy, 0xFFE80000, -0x400);
        sp->mA0_particlePosition.vx = e->m28_position.vx >> 12;
        sp->mA0_particlePosition.vz = e->m28_position.vz >> 12;
        sp->mA0_particlePosition.vy = e->m28_position.vy >> 12;
        spawnWorldmapParticles(13, &sp->mA0_particlePosition, NULL);
        sp->mA0_particlePosition.vy = 0;
        spawnWorldmapParticles(14, &sp->mA0_particlePosition, NULL);
        break;
    }
    p[0].m8_position.vx = p[1].m8_position.vx = e->m28_position.vx >> 12;
    p[0].m8_position.vy = p[1].m8_position.vy = e->m28_position.vy >> 12;
    p[0].m8_position.vz = p[1].m8_position.vz = e->m28_position.vz >> 12;
    return 1;
}

/* Noah loadWorldmapTextureFromFile3 (worldmap.cpp); owns rodata D_800704DC {E0 F5 FF 00} */
void loadWorldmapTextureFromFile3(void) {
    RECT rect;
    u8 color[4] = { 0xE0, 0xF5, 0xFF, 0x00 };
    s32 i;
    u16* buf0;
    u16* buf1;

    buf0 = (u16*)mallocAndDecompress(worldmapFile3Buffer, 1);
    loadMenuImageBundle(buf0);
    DrawSync(0);
    freeHeap(buf0);
    freeHeap(worldmapFile3Buffer);
    buf0 = allocateHeap(0x200, 1);
    buf1 = allocateHeap(0x2000, 1);
    rect.x = 0;
    rect.y = 0x1F0;
    rect.w = 0x100;
    rect.h = 1;
    StoreImage(&rect, (u32*)buf0);
    DrawSync(0);
    tweakWorldmapTexture(buf0, buf1, 0x10, color);
    rect.x = 0;
    rect.y = 0x1F0;
    rect.w = 0x100;
    rect.h = 0xF;
    LoadImage(&rect, buf1);
    DrawSync(0);
    for (i = 0; i < 0x10; i++) {
        worldmapFile3Cluts[i] = GetClut(rect.x, rect.y);
        rect.y++;
    }
    freeHeap(buf1);
    freeHeap(buf0);
}

/* worldmapModels[4].m0_hidden = 1 below is stored also when the mode has fewer
 * than five models (event mode 13 has three). The PS1 then writes the halfword
 * past the array, into the next heap block: model 0's first primitive buffer,
 * which initModel1 allocates right after the array (0x54-byte models, 4-byte
 * heap grain, 8-byte block header). Event 13: its second quad's uv3 becomes
 * (1, 0), so the lightning ring's streak quad samples its texture page from
 * u 0x39 back to 1 (Beetle RAM 0x800C4088, the picture; the quad keeps it,
 * setWorldmapModelSemiTransGrey copies buffer 0 to buffer 1). The PC's models
 * and primitives are bigger, so the same index lands elsewhere (event 13: the
 * quad's uv2). Store where the PS1 does, when the PC heap has model 0's buffer
 * right after the array too (as the PS1's, the same allocation order). With
 * four models the PS1 store hits that block's header: nothing is stored. */
static void pcHideModel4(void) {
    s32 n = worldmapNumModels;
    ModelBlock* b;
    u8* at;

    if (n > 4) {
        worldmapModels[4].m0_hidden = 1;
        return;
    }
    if (worldmapModels[0].m48_primBuffers[0] !=
        (u8*)worldmapModels + ((n * (s32)sizeof(sWorldmapModel) + HEAP_GRAIN - 1) & ~(HEAP_GRAIN - 1)) + HEAP_HEADER) {
        return;
    }
    b = worldmapModels[0].m40_modelBlock;
    at = pcModelPrimBufferAt(b->m10_meshBlocks, b->m6_numMeshBlocks, worldmapModels[0].m48_primBuffers[0],
                             4 * 0x54 - ((n * 0x54 + 3) & ~3) - 8);
    if (at != NULL) {
        *(s16*)at = 1;
    }
}

/* Noah worldmapInitAllModels (worldmap.cpp) */
void worldmapInitAllModels(void) {
    s32 i;
    s16 n;
    s32* base;
    s32* p;
    sWorldmapModelConfig* cfg;
    sWorldmapCollisionMesh* mesh;

    worldmapFile1Buffer_8_numEntries = n = fieldModelRelocation(worldmapModelFiles);
    base = worldmapFile1Buffer_C;
    p = worldmapFile1Buffer_C = base + 1;
    /* The offsets stay as loaded: the mesh is base + offset (read once below). */
    (void)p;
    i = 0;
    worldmapNumModels = *pWorldmapModelsConfig;
    worldmapModels = allocateHeap(worldmapNumModels * sizeof(sWorldmapModel), 0);
    cfg = (sWorldmapModelConfig*)(pWorldmapModelsConfig + 1);
    SetColorMatrix(&worldmapColorMatrix);
    SetLightMatrix(&worldmapLightMatrix);
    for (; i < worldmapNumModels; i++, cfg++) {
        worldmapModels[i].m0_hidden = 0;
        worldmapModels[i].m2_modelBlockIndex = cfg->m0_blockIndex;
        worldmapModels[i].m4_flags = cfg->m2_flags;
        worldmapModels[i].m8_position.vx = cfg->m4_x;
        worldmapModels[i].m8_position.vy = cfg->m6_y;
        worldmapModels[i].m8_position.vz = -cfg->m8_z;
        worldmapModels[i].m18_rotation.vx = cfg->mA_rotX;
        worldmapModels[i].m18_rotation.vy = cfg->mC_rotY;
        worldmapModels[i].m18_rotation.vz = cfg->mE_rotZ;
        RotMatrixYXZ(&worldmapModels[i].m18_rotation, &worldmapModels[i].m20_rotationMatrix);
        worldmapModels[i].m40_modelBlock = (ModelBlock*)pcModelBlocks(worldmapModelFiles) + worldmapModels[i].m2_modelBlockIndex;
        initModel1(worldmapModels[i].m40_modelBlock, &worldmapModels[i].m48_primBuffers[0], &worldmapModels[i].m48_primBuffers[1]);
        initModel2(worldmapModels[i].m40_modelBlock, worldmapModels[i].m48_primBuffers[0], 1);
        memcpy(worldmapModels[i].m48_primBuffers[1], worldmapModels[i].m48_primBuffers[0], worldmapModels[i].m40_modelBlock->m34_primBufferSize);
        mesh = worldmapModels[i].m44_collisionMesh =
            (sWorldmapCollisionMesh*)((u8*)(worldmapFile1Buffer_C - 1) + worldmapFile1Buffer_C[worldmapModels[i].m2_modelBlockIndex]);
        (void)mesh;
        worldmapModels[i].m50_parentModel = NULL;
    }
    gDepthDivider = 2;
    worldmapModelVar1 = worldmapModelVar0 = -1;
    pcHideModel4();
}

/* free all models (not in Noah) */
void freeWorldmapModels(void) {
    s32 i;
    for (i = 0; i < worldmapNumModels; i++) {
        freeHeap(worldmapModels[i].m48_primBuffers[0]);
        freeModelBlock(worldmapModels[i].m40_modelBlock);
    }
    freeHeap(worldmapModels);
}

/* Noah attachWorldmapModels (worldmapTaskYggdrasil.cpp) */
void attachWorldmapModels(s32 parent, s32 child) {
    sWorldmapModel* m = worldmapModels;
    m[child].m50_parentModel = &m[parent];
}

/* Noah renderWorldmapModels (worldmapTaskGround.cpp) */
void renderWorldmapModels(void) {
    WorldmapModelScratch* sp = (WorldmapModelScratch*)0x1F800000;
    sWorldmapModel* p;
    s32 worldX;
    s32 worldZ;
    s32 i;

    sp->m10_scale.vx = sp->m10_scale.vy = sp->m10_scale.vz = 0x800;
    objectClippingMask = 3;
    worldX = worldmapRadarPosition3.vx >> 12;
    worldZ = worldmapRadarPosition3.vz >> 12;
    fieldPolyCount2 = 0;
    fieldPolyCount = 0;
    sp->mA0_origin.vx = sp->mA0_origin.vy = sp->mA0_origin.vz = 0;
    for (i = 0; i < worldmapNumModels; i++) {
        if (worldmapModels[i].m0_hidden == 0) {
            sp->mF0_matrix = worldmapModels[i].m20_rotationMatrix;
            sp->mF0_matrix.t[0] = worldmapModels[i].m8_position.vx;
            sp->mF0_matrix.t[1] = worldmapModels[i].m8_position.vy;
            sp->mF0_matrix.t[2] = -worldmapModels[i].m8_position.vz;
            p = &worldmapModels[i];
            while (p->m50_parentModel != NULL) {
                p = p->m50_parentModel;
                p->m20_rotationMatrix.t[0] = p->m8_position.vx;
                p->m20_rotationMatrix.t[1] = p->m8_position.vy;
                p->m20_rotationMatrix.t[2] = -p->m8_position.vz;
                gte_CompMatrix(&p->m20_rotationMatrix, &sp->mF0_matrix, &sp->mF0_matrix);
            }
            sp->m0_delta.vx = sp->mF0_matrix.t[0] - worldX;
            sp->m0_delta.vz = -sp->mF0_matrix.t[2] - worldZ;
            adjustWorldModelLocation(&sp->m0_delta);
            sp->mF0_matrix.t[0] = sp->m0_delta.vx;
            sp->mF0_matrix.t[2] = -sp->m0_delta.vz;
            ScaleMatrix(&sp->mF0_matrix, &sp->m10_scale);
            CompMatrix(&worldmapMainMatrix2, &sp->mF0_matrix, &sp->m110_matrix);
            SetRotMatrix(&sp->m110_matrix);
            SetTransMatrix(&sp->m110_matrix);
            gte_ldv0(&sp->mA0_origin);
            gte_rtps();
            gte_stflg(&sp->m20_flag);
            if (sp->m20_flag >= 0) {
                gte_stsz(&sp->m28_depth);
                if (sp->m28_depth < 0xD80) {
                    pcInterpolationObject(&worldmapModels[i], worldmapModels[i].m40_modelBlock);
                    submitModelForRendering(worldmapModels[i].m40_modelBlock, worldmapModels[i].m48_primBuffers[worldmapOddOrEven], pCurrentWorldmapRenderingStruct->m70_OT, worldmapModelsRenderModes[worldmapModels[i].m4_flags]);
                    pcInterpolationObject(0, 0);
                }
            }
        }
    }
}

/* Noah worldmapCheckCollisionsAgainstWorldmapModels (worldmapPlayer.cpp) */
s32 worldmapCheckCollisionsAgainstWorldmapModels(VECTOR* position, s16* outIndex) {
    s16 i;
    s16 r;
    sWorldmapModel* m = worldmapModels;
    for (i = 0; i < worldmapNumModels; i++) {
        if (m->m4_flags & 1) {
            r = worldmapCheckCollisionsAgainstWorldmapModel(position, i);
            if (r != 0) {
                *outIndex = i;
                return r;
            }
        }
        m++;
    }
    return 0;
}

/* Noah worldmapCheckCollisionsAgainstWorldmapModel (worldmapPlayer.cpp) */
s32 worldmapCheckCollisionsAgainstWorldmapModel(VECTOR* position, s32 modelIndex) {
    sWorldmapModel* m = &worldmapModels[modelIndex];
    WorldmapScratch* s = (WorldmapScratch*)0x1F800000;
    sWorldmapCollisionMesh* mesh;
    sWorldmapCollisionTri* tri;
    SVECTOR* verts;
    s32 dx, dz;
    s32 i, n;
    s32 count;
    s32 flag;

    s->m40_delta.vx = (position->vx >> 12) - m->m8_position.vx;
    s->m40_delta.vz = m->m8_position.vz - (position->vz >> 12);
    if ((abs(s->m40_delta.vx) >= 0x800) | (abs(s->m40_delta.vz) >= 0x800)) {
        return 0;
    }
    s->mF0_matrix = m->m20_rotationMatrix;
    s->mF0_matrix.t[2] = 0;
    s->mF0_matrix.t[0] = 0;
    s->mF0_matrix.t[1] = m->m8_position.vy;
    s->m0_vertices[0].vx = s->m0_vertices[0].vy = s->m0_vertices[0].vz = 0x800;
    count = 0;
    ScaleMatrix(&s->mF0_matrix, &s->m0_vertices[0]);
    SetRotMatrix(&s->mF0_matrix);
    SetTransMatrix(&s->mF0_matrix);
    mesh = m->m44_collisionMesh;
    n = mesh->m0_numTriangles;
    verts = WM_MESH_VERTICES(mesh);
    s->m30_sxy[2] = (s->m40_delta.vz << 16) | (u16)s->m40_delta.vx;
    tri = mesh->m8_triangles;
    for (i = 0; i < n; i++, tri++) {
        gte_ldv0(&verts[tri->m0_vertices[0]]);
        gte_rt();
        gte_stlvnl(&s->m0_vertices[0]);
        gte_stflg(&flag);
        gte_ldv0(&verts[tri->m0_vertices[1]]);
        gte_rt();
        gte_stlvnl(&s->m0_vertices[1]);
        gte_stflg(&flag);
        gte_ldv0(&verts[tri->m0_vertices[2]]);
        gte_rt();
        gte_stlvnl(&s->m0_vertices[2]);
        gte_stflg(&flag);
        s->m30_sxy[0] = (s->m0_vertices[0].vz << 16) | (u16)s->m0_vertices[0].vx;
        s->m30_sxy[1] = (s->m0_vertices[1].vz << 16) | (s->m0_vertices[1].vx & 0xFFFF);
        if (NormalClip(s->m30_sxy[0], s->m30_sxy[1], s->m30_sxy[2]) > 0) continue;
        s->m30_sxy[0] = (s->m0_vertices[1].vz << 16) | (s->m0_vertices[1].vx & 0xFFFF);
        s->m30_sxy[1] = (s->m0_vertices[2].vz << 16) | (s->m0_vertices[2].vx & 0xFFFF);
        if (NormalClip(s->m30_sxy[0], s->m30_sxy[1], s->m30_sxy[2]) > 0) continue;
        s->m30_sxy[0] = (s->m0_vertices[2].vz << 16) | (s->m0_vertices[2].vx & 0xFFFF);
        s->m30_sxy[1] = (s->m0_vertices[0].vz << 16) | (u16)s->m0_vertices[0].vx;
        if (NormalClip(s->m30_sxy[0], s->m30_sxy[1], s->m30_sxy[2]) > 0) continue;
        worldmapModelCollisionHits[count] = i;
        worldmapModelCollisionHits[count + 1] = tri->mC_type;
        count += 2;
    }
    return count;
}

/* model triangle contact point + normal (not in Noah) */
void getWorldmapModelTriangleContact(VECTOR* position, VECTOR* out, VECTOR* normal, u16 modelIndex, u16 triIndex) {
    WorldmapTriScratch* sp = (WorldmapTriScratch*)0x1F800000;
    sWorldmapModel* m = &worldmapModels[modelIndex];
    sWorldmapCollisionMesh* mesh;
    sWorldmapCollisionTri* tri;
    SVECTOR* verts;
    s32 flag;

    out->vx = (position->vx >> 12) - m->m8_position.vx;
    out->vz = m->m8_position.vz - (position->vz >> 12);
    sp->mF0_matrix = m->m20_rotationMatrix;
    sp->mF0_matrix.t[2] = 0;
    sp->mF0_matrix.t[0] = 0;
    sp->mF0_matrix.t[1] = m->m8_position.vy;
    sp->m10_vertex1.vx = sp->m10_vertex1.vy = sp->m10_vertex1.vz = 0x800;
    ScaleMatrix(&sp->mF0_matrix, &sp->m10_vertex1);
    SetRotMatrix(&sp->mF0_matrix);
    SetTransMatrix(&sp->mF0_matrix);
    mesh = m->m44_collisionMesh;
    tri = mesh->m8_triangles;
    tri += triIndex;
    verts = WM_MESH_VERTICES(mesh);
    RotTrans(&verts[tri->m0_vertices[0]], &sp->m0_vertex0, &flag);
    RotTrans(&verts[tri->m0_vertices[1]], &sp->m10_vertex1, &flag);
    RotTrans(&verts[tri->m0_vertices[2]], &sp->m20_vertex2, &flag);
    sp->m10_vertex1.vx -= sp->m0_vertex0.vx;
    sp->m10_vertex1.vy -= sp->m0_vertex0.vy;
    sp->m10_vertex1.vz -= sp->m0_vertex0.vz;
    sp->m20_vertex2.vx -= sp->m0_vertex0.vx;
    sp->m20_vertex2.vy -= sp->m0_vertex0.vy;
    sp->m20_vertex2.vz -= sp->m0_vertex0.vz;
    OuterProduct0(&sp->m20_vertex2, &sp->m10_vertex1, &sp->m20_vertex2);
    sp->m20_vertex2.vx >>= 2;
    sp->m20_vertex2.vy >>= 2;
    sp->m20_vertex2.vz >>= 2;
    VectorNormal(&sp->m20_vertex2, normal);
    averageVector(out, &sp->m0_vertex0, normal);
}

/* model triangle plane crossing test (not in Noah) */
s32 isWorldmapModelTrianglePlaneCrossed(VECTOR* position, s32 height, u16 modelIndex, u16 triIndex) {
    WorldmapPlaneScratch* sp = (WorldmapPlaneScratch*)0x1F800000;
    sWorldmapModel* m = &worldmapModels[modelIndex];
    sWorldmapCollisionMesh* mesh;
    sWorldmapCollisionTri* tri;
    SVECTOR* verts;
    s32 flag;

    sp->mF0_matrix = m->m20_rotationMatrix;
    sp->mF0_matrix.t[2] = 0;
    sp->mF0_matrix.t[0] = 0;
    sp->mF0_matrix.t[1] = m->m8_position.vy;
    sp->m10_vertex1.vx = sp->m10_vertex1.vy = sp->m10_vertex1.vz = 0x800;
    ScaleMatrix(&sp->mF0_matrix, &sp->m10_vertex1);
    SetRotMatrix(&sp->mF0_matrix);
    SetTransMatrix(&sp->mF0_matrix);
    mesh = m->m44_collisionMesh;
    tri = mesh->m8_triangles;
    tri += triIndex;
    verts = WM_MESH_VERTICES(mesh);
    RotTrans(&verts[tri->m0_vertices[0]], &sp->m0_vertex0, &flag);
    RotTrans(&verts[tri->m0_vertices[1]], &sp->m10_vertex1, &flag);
    RotTrans(&verts[tri->m0_vertices[2]], &sp->m20_vertex2, &flag);
    sp->m10_vertex1.vx -= sp->m0_vertex0.vx;
    sp->m10_vertex1.vy -= sp->m0_vertex0.vy;
    sp->m10_vertex1.vz -= sp->m0_vertex0.vz;
    sp->m20_vertex2.vx -= sp->m0_vertex0.vx;
    sp->m20_vertex2.vy -= sp->m0_vertex0.vy;
    sp->m20_vertex2.vz -= sp->m0_vertex0.vz;
    OuterProduct0(&sp->m20_vertex2, &sp->m10_vertex1, &sp->m20_vertex2);
    sp->m20_vertex2.vx >>= 2;
    sp->m20_vertex2.vy >>= 2;
    sp->m20_vertex2.vz >>= 2;
    VectorNormal(&sp->m20_vertex2, &sp->m30_normal);
    sp->m110_segment.m[1][0] = sp->m110_segment.m[0][0] = (position->vx >> 12) - m->m8_position.vx - sp->m0_vertex0.vx;
    sp->m110_segment.m[0][1] = (position->vy >> 12) - sp->m0_vertex0.vy;
    sp->m110_segment.m[1][2] = sp->m110_segment.m[0][2] = m->m8_position.vz - (position->vz >> 12) - sp->m0_vertex0.vz;
    sp->m110_segment.m[1][1] = sp->m110_segment.m[0][1] - height;
    ApplyMatrixLV(&sp->m110_segment, &sp->m30_normal, &sp->m40_result);
    return (sp->m40_result.vx ^ sp->m40_result.vy) >> 31;
}

/* model triangle edge test (not in Noah) */
s32 getWorldmapModelTriangleEdgeFlags(VECTOR* target, VECTOR* position, s32 modelIndex, s32 triIndex) {
    WorldmapEdgeScratch* sp = (WorldmapEdgeScratch*)0x1F800000;
    sWorldmapModel* m = &worldmapModels[modelIndex];
    sWorldmapCollisionMesh* mesh;
    sWorldmapCollisionTri* tri;
    SVECTOR* verts;
    u32 flag;

    sp->m40_edgeDir1.vx = (position->vx >> 12) - m->m8_position.vx;
    sp->m40_edgeDir1.vz = m->m8_position.vz - (position->vz >> 12);
    sp->mF0_matrix = m->m20_rotationMatrix;
    sp->mF0_matrix.t[2] = 0;
    sp->mF0_matrix.t[0] = 0;
    sp->mF0_matrix.t[1] = m->m8_position.vy;
    sp->m0_vertex0.vx = sp->m0_vertex0.vy = sp->m0_vertex0.vz = 0x800;
    ScaleMatrix(&sp->mF0_matrix, &sp->m0_vertex0);
    SetRotMatrix(&sp->mF0_matrix);
    SetTransMatrix(&sp->mF0_matrix);
    mesh = m->m44_collisionMesh;
    tri = mesh->m8_triangles;
    tri += triIndex;
    verts = WM_MESH_VERTICES(mesh);
    gte_ldv0(&verts[tri->m0_vertices[0]]);
    gte_rt();
    gte_stlvnl(&sp->m0_vertex0);
    gte_stflg(&flag);
    gte_ldv0(&verts[tri->m0_vertices[1]]);
    gte_rt();
    gte_stlvnl(&sp->m10_vertex1);
    gte_stflg(&flag);
    gte_ldv0(&verts[tri->m0_vertices[2]]);
    gte_rt();
    gte_stlvnl(&sp->m20_vertex2);
    gte_stflg(&flag);
    flag = 0;
    sp->m30_edgeDir0.vx = (sp->m0_vertex0.vz << 16) | (u16)sp->m0_vertex0.vx;
    sp->m30_edgeDir0.vy = (sp->m10_vertex1.vz << 16) | (u16)sp->m10_vertex1.vx;
    sp->m30_edgeDir0.vz = (sp->m40_edgeDir1.vz << 16) | (u16)sp->m40_edgeDir1.vx;
    if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) > 0) {
        flag |= 1;
    }
    sp->m30_edgeDir0.vx = (sp->m10_vertex1.vz << 16) | (u16)sp->m10_vertex1.vx;
    sp->m30_edgeDir0.vy = (sp->m20_vertex2.vz << 16) | (u16)sp->m20_vertex2.vx;
    if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) > 0) {
        flag |= 2;
    }
    sp->m30_edgeDir0.vx = (sp->m20_vertex2.vz << 16) | (u16)sp->m20_vertex2.vx;
    sp->m30_edgeDir0.vy = (sp->m0_vertex0.vz << 16) | (u16)sp->m0_vertex0.vx;
    if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) > 0) {
        flag |= 4;
    }
    switch (flag) {
    case 3:
        sp->m30_edgeDir0.vx = ((m->m8_position.vz - (target->vz >> 12)) << 16) | (((target->vx >> 12) - m->m8_position.vx) & 0xFFFF);
        sp->m30_edgeDir0.vy = (sp->m10_vertex1.vz << 16) | (u16)sp->m10_vertex1.vx;
        if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) != 0) {
            flag = 1;
        }
        break;
    case 5:
        sp->m30_edgeDir0.vx = ((m->m8_position.vz - (target->vz >> 12)) << 16) | (((target->vx >> 12) - m->m8_position.vx) & 0xFFFF);
        sp->m30_edgeDir0.vy = (sp->m0_vertex0.vz << 16) | (u16)sp->m0_vertex0.vx;
        if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) != 0) {
            flag = 4;
        }
        break;
    case 6:
        sp->m30_edgeDir0.vx = ((m->m8_position.vz - (target->vz >> 12)) << 16) | (((target->vx >> 12) - m->m8_position.vx) & 0xFFFF);
        sp->m30_edgeDir0.vy = (sp->m20_vertex2.vz << 16) | (u16)sp->m20_vertex2.vx;
        if (NormalClip(sp->m30_edgeDir0.vx, sp->m30_edgeDir0.vy, sp->m30_edgeDir0.vz) != 0) {
            flag = 2;
        }
        break;
    }
    sp->m30_edgeDir0.vx = sp->m10_vertex1.vx - sp->m0_vertex0.vx;
    sp->m30_edgeDir0.vy = 0;
    sp->m30_edgeDir0.vz = sp->m10_vertex1.vz - sp->m0_vertex0.vz;
    VectorNormal(&sp->m30_edgeDir0, &sp->m30_edgeDir0);
    sp->m40_edgeDir1.vx = sp->m20_vertex2.vx - sp->m10_vertex1.vx;
    sp->m40_edgeDir1.vy = 0;
    sp->m40_edgeDir1.vz = sp->m20_vertex2.vz - sp->m10_vertex1.vz;
    VectorNormal(&sp->m40_edgeDir1, &sp->m40_edgeDir1);
    sp->m50_edgeDir2.vx = sp->m0_vertex0.vx - sp->m20_vertex2.vx;
    sp->m50_edgeDir2.vy = 0;
    sp->m50_edgeDir2.vz = sp->m0_vertex0.vz - sp->m20_vertex2.vz;
    VectorNormal(&sp->m50_edgeDir2, &sp->m50_edgeDir2);
    return flag;
}

s32 pcWm60On(void);
s32 pcWm60OffTick(void);
s32 pcWm60RotY(void);
static u8 pcWm60SpriteAnimate[0x40];
static FP_VEC3 pcWm60SpritePosition[0x40];
void executeSpriteBytecode2(SpriteActorCore* actor);
/* The facing's native turn (below), against the game's own yaw. */
static void pcWm60SpriteFacing(sWorldmapStateEntry* e) {
    s32 a = e->m48_direction;
    s32 b = e->m5C;
    s32 d = a - b;
    if (d < 0) {
        d += 0x1000;
    }
    if (d < 0x801) {
        if (d < 0x100) {
            d = a;
        } else {
            d = b + 0x100;
        }
    } else {
        d -= 0x1000;
        if (d >= -0xFF) {
            d = a;
        } else {
            d = b - 0x100;
        }
    }
    e->m5C = d;
    setSpriteActorAngle(e->m4C_sprite, (e->m5C - pcWm60RotY() - 0x400) & 0xFFF);
}
extern s32 fieldDrawEnvsInitialized;
/* OP_INIT_ENTITY_SCRIPT_sub0Sub9's fieldDrawEnvsInitialized + 1 steps of a
 * native frame (two on the world map), the tick taking the first half. */
static void pcWm60SpriteStep(SpriteActorCore* actor) {
    s32 n = fieldDrawEnvsInitialized + 1;
    n = pcWm60OffTick() ? n / 2 : n - n / 2;
    while (n-- > 0) {
        if (actor->m9E_wait != 0) {
            if (--actor->m9E_wait == 0) {
                executeSpriteBytecode2(actor);
            }
        }
    }
}

/* Noah worldmapDrawSpriteActors (worldmapTaskGround.cpp) */
void worldmapDrawSpriteActors(void) {
    WorldmapSpriteScratch* sp = (WorldmapSpriteScratch*)0x1F800000;
    sWorldmapStateEntry* e;
    s32 i;
    s32 a, b, d;

    e = gWorldmapState->m0_entries;
    for (i = 0; i < 0x40; i++, e++) {
        if ((e->m24_hidden == 0) & (e->m4C_sprite != NULL)) {
            sp->m8_delta.vx = e->m28_position.vx - worldmapRadarPosition3.vx;
            sp->m8_delta.vz = e->m28_position.vz - worldmapRadarPosition3.vz;
            computeWormapSpritePosition(&sp->m8_delta);
            e->m4C_sprite->m0_position.vx = sp->m8_delta.vx << 4;
            e->m4C_sprite->m0_position.vz = -sp->m8_delta.vz << 4;
            e->m4C_sprite->m0_position.vy = e->m28_position.vy << 4;
        }
    }
    SetRotMatrix(&worldmapMainMatrix2);
    SetTransMatrix(&worldmapMainMatrix2);
    e = gWorldmapState->m0_entries;
    for (i = 0; i < 0x40; i++, e++) {
        if ((e->m24_hidden == 0) & (e->m4C_sprite != NULL)) {
            sp->m0_position.vx = e->m4C_sprite->m0_position.vx >> 16;
            sp->m0_position.vy = e->m4C_sprite->m0_position.vy >> 16;
            sp->m0_position.vz = e->m4C_sprite->m0_position.vz >> 16;
            gte_ldv0(&sp->m0_position);
            gte_rtps();
            gte_stsz(&sp->m18_depths[i]);
        }
    }
    setCurrentRenderingMatrix(&worldmapMainMatrix2);
    e = gWorldmapState->m0_entries;
    for (i = 0; i < 0x40; i++, e++) {
        if ((e->m24_hidden == 0) & (e->m4C_sprite != NULL) & (sp->m18_depths[i] < 0xB00)) {
            renderSpriteActor(e->m4C_sprite, &pCurrentWorldmapRenderingStruct->m70_OT[sp->m18_depths[i] >> 4]);
            if (pcWm60On()) {
                continue; /* Enhanced, 60 Hz: the turn and the animation below, as the tick decided */
            }
            a = e->m48_direction;
            b = e->m5C;
            d = a - b;
            if (d < 0) {
                d += 0x1000;
            }
            if (d < 0x801) {
                if (d < 0x100) {
                    d = a;
                } else {
                    d = b + 0x100;
                }
            } else {
                d -= 0x1000;
                if (d >= -0xFF) {
                    d = a;
                } else {
                    d = b - 0x100;
                }
            }
            e->m5C = d;
            setSpriteActorAngle(e->m4C_sprite, (e->m5C - worldmapRotation.vy - 0x400) & 0xFFF);
            OP_INIT_ENTITY_SCRIPT_sub0Sub9(e->m4C_sprite);
        }
    }
    if (pcWm60On()) {
        /* Enhanced, 60 Hz (w_80070CFC.c): the sprites the tick found in view,
         * at their full positions, the facing turned on the tick against the
         * game's own yaw, the animation's steps split over the two frames */
        e = gWorldmapState->m0_entries;
        for (i = 0; i < 0x40; i++, e++) {
            if (!pcWm60SpriteAnimate[i] || e->m4C_sprite == NULL) {
                continue;
            }
            e->m4C_sprite->m0_position = pcWm60SpritePosition[i];
            if (!pcWm60OffTick()) {
                pcWm60SpriteFacing(e);
            }
            pcWm60SpriteStep(e->m4C_sprite);
        }
    }
}

/* Enhanced, 60 Hz: the frame between's animation steps without its picture
 * (the tick was followed by the pause, the menu or leaving: pcWm60Settle). */
void pcWm60SpriteFinish(void) {
    sWorldmapStateEntry* e = gWorldmapState->m0_entries;
    s32 i;
    for (i = 0; i < 0x40; i++, e++) {
        if (pcWm60SpriteAnimate[i] && e->m4C_sprite != NULL) {
            pcWm60SpriteStep(e->m4C_sprite);
        }
    }
}

/* Enhanced, 60 Hz: on the tick, before its picture's values go in, which
 * sprites the Original's frame animates (in view, nearer than 0xB00, by the
 * game's own camera: the ground task has just set up its matrix) and where
 * it puts them; the frame between repeats the tick's verdict. */
void pcWm60SpriteDecide(void) {
    WorldmapSpriteScratch* sp = (WorldmapSpriteScratch*)0x1F800000;
    sWorldmapStateEntry* e = gWorldmapState->m0_entries;
    s32 i;
    s32 depth;

    SetRotMatrix(&worldmapMainMatrix2);
    SetTransMatrix(&worldmapMainMatrix2);
    for (i = 0; i < 0x40; i++, e++) {
        pcWm60SpriteAnimate[i] = 0;
        if ((e->m24_hidden == 0) & (e->m4C_sprite != NULL)) {
            sp->m8_delta.vx = e->m28_position.vx - worldmapRadarPosition3.vx;
            sp->m8_delta.vz = e->m28_position.vz - worldmapRadarPosition3.vz;
            computeWormapSpritePosition(&sp->m8_delta);
            pcWm60SpritePosition[i] = e->m4C_sprite->m0_position;
            pcWm60SpritePosition[i].vx = sp->m8_delta.vx << 4;
            pcWm60SpritePosition[i].vz = -sp->m8_delta.vz << 4;
            pcWm60SpritePosition[i].vy = e->m28_position.vy << 4;
            sp->m0_position.vx = pcWm60SpritePosition[i].vx >> 16;
            sp->m0_position.vy = pcWm60SpritePosition[i].vy >> 16;
            sp->m0_position.vz = pcWm60SpritePosition[i].vz >> 16;
            gte_ldv0(&sp->m0_position);
            gte_rtps();
            gte_stsz(&depth);
            pcWm60SpriteAnimate[i] = depth < 0xB00;
        }
    }
}

/* relocate worldmapFile1Buffer_14 table + 16 CLUTs (not in Noah) */
void initWorldmapBillboards(void) {
    s32 i;
    sWorldmapOffsetEntry* p = worldmapFile1Buffer_14;
    sWorldmapOffsetEntry* base = p;
    /* The offsets stay as loaded (drawWorldmapBillboards adds the table). */
    (void)p;
    (void)base;
    for (i = 0; i < 0x10; i++) {
        worldmapBillboardCluts[i] = GetClut(0xF0, i + 0x1F0);
    }
}

/* Noah createWorlmapPolyFT4Array3 (worldmap.cpp) */
void createWorlmapPolyFT4Array3(void) {
    s32 i;
    POLY_FT4* p;
    worldmapFT4_3[0] = allocateHeap(0x200 * sizeof(POLY_FT4), 1);
    worldmapFT4_3[1] = allocateHeap(0x200 * sizeof(POLY_FT4), 1);
    p = worldmapFT4_3[0];
    for (i = 0; i < 0x200; i++, p++) {
        setPolyFT4(p);
        setRGB0(p, 0x80, 0x80, 0x80);
        p->u0 = 0;
        p->v0 = 0x40;
        p->u1 = 0x1F;
        p->v1 = 0x40;
        p->u2 = 0;
        p->v2 = 0x6F;
        p->u3 = 0x1F;
        p->v3 = 0x6F;
        p->clut = GetClut(0xF0, 0x1FF);
        p->tpage = GetTPage(0, 0, 0x380, 0x100);
    }
    __builtin_memcpy(worldmapFT4_3[1], worldmapFT4_3[0], 0x200 * sizeof(POLY_FT4));
}

/* free FT4 array 3 (not in Noah) */
void freeWorldmapPolyFT4Array3(void) {
    freeHeap(worldmapFT4_3[1]);
    freeHeap(worldmapFT4_3[0]);
}

/* draw grid decorations with FT4 array 3 (not in Noah) */
void drawWorldmapBillboards(void) {
    WorldmapQuadScratch* sp = (WorldmapQuadScratch*)0x1F800000;
    s32 i, j, k;

    sWorldmapOffsetEntry* table;

    sp->m0_quad[0].vx = -0x18;
    sp->m0_quad[0].vy = -0x48;
    sp->m0_quad[0].vz = 0;
    sp->m0_quad[1].vx = 0x18;
    sp->m0_quad[1].vy = -0x48;
    sp->m0_quad[1].vz = 0;
    sp->m0_quad[2].vx = -0x18;
    sp->m0_quad[2].vy = 0;
    sp->m0_quad[2].vz = 0;
    sp->m0_quad[3].vx = 0x18;
    sp->m0_quad[3].vy = 0;
    sp->m0_quad[3].vz = 0;
    sp->m28_matrix = worldmapMainMatrix2;
    sp->m48_rotation = worldMapIdentityMatrix;
    RotMatrixZ(-worldmapRotation.vz, &sp->m48_rotation);
    for (j = 0; j < 0x10; j++) {
        sp->m68_cluts[j] = worldmapBillboardCluts[j];
    }
    table = worldmapFile1Buffer_14;
    worldmapBillboardPolyCount = 0;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            if (((s16*)megatilesVisibility)[i * 5 + j] != -1) { /* flat index keeps retail code */
                k = worldmapGrid[(i + worldmapGridPosition.vz) * 9 + j + worldmapGridPosition.vx];
                if (table[k].m4_count != 0) {
                    drawWorldmapCellBillboards((s16*)((u8*)table + table[k].m0_offset), table[k].m4_count,
                                               pCurrentWorldmapRenderingStruct->m70_OT, &worldmapFT4_3[worldmapOddOrEven][worldmapBillboardPolyCount]);
                }
            }
        }
    }
    /* The wide picture's margins (port/src/game/worldmap/wide.c). */
    pcWideWorldmapBillboards();
}

/* clouds init (not in Noah) */
void initWorldmapClouds(void) {
    s32 i, j, k;
    VECTOR* pos;
    SVECTOR* step;

    worldmapCloudPositions = allocateHeap(0x50 * sizeof(VECTOR), 0);
    worldmapCloudSteps = allocateHeap(0x50 * sizeof(SVECTOR), 0);
    pos = worldmapCloudPositions;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 5; k++, pos++) {
                pos->vx = (worldmapCloudOrigins[k].vx + j * 0x800) << 12;
                pos->vz = (worldmapCloudOrigins[k].vz + i * 0x800) << 12;
                pos->vy = (-(rand() >> 10) * 8 - 0x200) << 12;
            }
        }
    }
    step = worldmapCloudSteps;
    for (k = 0; k < 0x50; k++) {
        j = (rand() & 3) + 1;
        step->vx = j * 3547;
        step->vz = -(j * 0x800);
        step->vy = rand() & 1;
        step++;
    }
}

/* clouds free (not in Noah) */
void freeWorldmapClouds(void) {
    freeHeap(worldmapCloudSteps);
    freeHeap(worldmapCloudPositions);
}

/* Noah createWorlmapPolyFT4Array2 (worldmap.cpp) */
void createWorlmapPolyFT4Array2(void) {
    s32 i;
    POLY_FT4* p;
    u8 len, code, c;
    worldmapFT4_2[0] = allocateHeap(0x120 * sizeof(POLY_FT4), 1);
    worldmapFT4_2[1] = allocateHeap(0x120 * sizeof(POLY_FT4), 1);
    p = worldmapFT4_2[0];
    c = 0x26;
    for (i = 0; i < 0x120; i++, p++) {
        len = 9;
        code = 0x2C;
        setlen(p, len);
        setcode(p, code);
        setRGB0(p, c, c, c);
        p->tpage = GetTPage(0, 1, 0x3C0, 0x100);
        p->clut = GetClut(0x130, 0x1FE);
        SetSemiTrans(p, 1);
    }
    __builtin_memcpy(worldmapFT4_2[1], worldmapFT4_2[0], 0x120 * sizeof(POLY_FT4));
}

/* free FT4 array 2 (not in Noah) */
void freeWorldmapPolyFT4Array2(void) {
    freeHeap(worldmapFT4_2[1]);
    freeHeap(worldmapFT4_2[0]);
}

/* clouds move (callback worldmapCloudsMoveCallback; not in Noah) */
s32 pcWm60Half(s32 step);
void moveWorldmapClouds(void) {
    s32 i;
    VECTOR* pos = worldmapCloudPositions;
    SVECTOR* step = worldmapCloudSteps;
    s32 x, z;
    for (i = 0; i < 0x50; i++) {
        /* Enhanced, 60 Hz (w_80070CFC.c): half the drift per 60 Hz frame, whole by the next tick */
        x = pos[i].vx + pcWm60Half(step[i].vx);
        z = pos[i].vz + pcWm60Half(step[i].vz);
        if (x > 0x1FFFFFF) x += 0xFE000000;
        if (x < 0) x += 0x2000000;
        if (z > 0x1FFFFFF) z += 0xFE000000;
        if (z < 0) z += 0x2000000;
        pos[i].vx = x;
        pos[i].vz = z;
    }
}

typedef struct WorldmapCloudScratch {
    /* 0x000 */ SVECTOR m0_near[12];
    /* 0x060 */ SVECTOR m60_mid[0x30];
    /* 0x1E0 */ u16 m1E0_uv[8];
    /* 0x1F0 */ SVECTOR m1F0_quad[4];
    /* 0x210 */ VECTOR m210_camera;
    /* 0x220 */ VECTOR m220_cell;
    /* 0x230 */ s32 m230_sxy[4];
    /* 0x240 */ VECTOR m240_center;
    /* 0x250 */ MATRIX m250_localMatrix;
    /* 0x270 */ MATRIX m270;
    /* 0x290 */ MATRIX m290_yawMatrix;
    /* 0x2B0 */ MATRIX m2B0;
    /* 0x2D0 */ s32 m2D0_uvIndex;
    /* 0x2D4 */ s32 m2D4_flag;
    /* 0x2D8 */ s32 m2D8_depth;
    /* 0x2DC */ s32 m2DC_sz[4];
    /* 0x2EC */ SVECTOR* m2EC_vertices;
    /* 0x2F0 */ s32 m2F0_count;
    /* 0x2F4 */ s32 m2F4_numQuads;
} WorldmapCloudScratch;
typedef struct WorldmapCloudPrim {
    /* 0x00 */ OTag m0_tag;
    /* 0x04 */ u8 m4_r0, m5_g0, m6_b0, m7_code;
    /* 0x08 */ u32 m8_xy0;
    /* 0x0C */ u16 mC_uv0;
    /* 0x0E */ u16 mE_clut;
    /* 0x10 */ u32 m10_xy1;
    /* 0x14 */ u16 m14_uv1;
    /* 0x16 */ u16 m16_tpage;
    /* 0x18 */ u32 m18_xy2;
    /* 0x1C */ u16 m1C_uv2;
    /* 0x1E */ u16 m1E_pad1;
    /* 0x20 */ u32 m20_xy3;
    /* 0x24 */ u16 m24_uv3;
    /* 0x26 */ u16 m26_pad2;
} WorldmapCloudPrim;
extern SVECTOR worldmapCloudMidVertices[];
extern SVECTOR worldmapCloudNearVertices[];
extern u16 worldmapCloudUVs[];
void RotMatrixX(s32 r, MATRIX* m);


/* Cloud renderer, three levels of detail. Its OT links are inline asm in
 * retail (ADD_PRIM_ASM, include/ot.h) and the projected SXY values live in
 * t6-t9 throughout, hence the register variables (user decision). */
void drawWorldmapClouds(void) {
    WorldmapCloudScratch* sp = (WorldmapCloudScratch*)0x1F800000;
    s32 i, j, k, l;
    WorldmapCloudPrim* prim;
    s32 uv;
    /* projected SXY0-3: retail keeps them in t6/t7/t8/t9 at all three LOD levels */
    u32 p0, p1, p2, p3;

    worldmapCloudsMoveCallback();
    for (i = 0; i < 0x30; i++) {
        sp->m60_mid[i] = worldmapCloudMidVertices[i];
    }
    for (i = 0; i < 0xC; i++) {
        sp->m0_near[i] = worldmapCloudNearVertices[i];
    }
    for (i = 0; i < 8; i++) {
        sp->m1E0_uv[i] = worldmapCloudUVs[i];
    }
    sp->m210_camera.vx = worldmapRadarPosition3.vx & 0x1FFFFFF;
    sp->m210_camera.vz = worldmapRadarPosition3.vz & 0x1FFFFFF;
    sp->m220_cell.vx = (worldmapRadarPosition3.vx >> 12) & 0x7FF;
    sp->m220_cell.vz = (worldmapRadarPosition3.vz >> 12) & 0x7FF;
    sp->m250_localMatrix = worldMapIdentityMatrix;
    sp->m270 = sp->m250_localMatrix;
    sp->m290_yawMatrix = sp->m250_localMatrix;
    RotMatrixX((worldmapRotation.vx + 0x400) / 8, &sp->m250_localMatrix);
    RotMatrixY(-worldmapRotation.vy, &sp->m270);
    RotMatrixY(worldmapRotation.vy, &sp->m290_yawMatrix);
    MulMatrix0(&sp->m250_localMatrix, &sp->m270, &sp->m2B0);
    MulMatrix0(&sp->m290_yawMatrix, &sp->m2B0, &sp->m250_localMatrix);
    sp->m230_sxy[0] = (getAngleSin(worldmapRotation.vy - 0x169) << 16) | (getAngleCos(worldmapRotation.vy - 0x169) & 0xFFFF);
    sp->m230_sxy[1] = (getAngleSin(worldmapRotation.vy + 0x169) << 16) | (getAngleCos(worldmapRotation.vy + 0x169) & 0xFFFF);
    sp->m240_center.vx = worldmapCameraVector.m0_eye.vx * 2 + (-getAngleCos(worldmapRotation.vy) >> 1);
    sp->m240_center.vz = worldmapCameraVector.m0_eye.vz * 2 + (-getAngleSin(worldmapRotation.vy) >> 1);
    prim = (WorldmapCloudPrim*)worldmapFT4_2[worldmapOddOrEven];
    sp->m2F4_numQuads = 0;
    sp->m2F0_count = 0;
    for (i = 0; i < 0x50; i++) {
        if (sp->m2F0_count > 0xF0) {
            break;
        }
        k = (worldmapCloudPositions[i].vx - sp->m210_camera.vx) >> 12;
        l = (worldmapCloudPositions[i].vz - sp->m210_camera.vz) >> 12;
        if (k < -0x1000) {
            k += 0x2000;
        } else if (k >= 0x1000) {
            k -= 0x2000;
        }
        if (l < -0x1000) {
            l += 0x2000;
        } else if (l >= 0x1000) {
            l -= 0x2000;
        }
        l = -l;
        j = ((l - sp->m240_center.vz) << 16) | ((k - sp->m240_center.vx) & 0xFFFF);
        gte_ldsxy3(j, sp->m230_sxy[1], 0);
        gte_nclip();
        gte_stopz(&sp->m2D4_flag);
        if (sp->m2D4_flag > 0) {
            continue;
        }
        j = ((l - sp->m240_center.vz) << 16) | ((k - sp->m240_center.vx) & 0xFFFF);
        gte_ldsxy3(0, sp->m230_sxy[0], j);
        gte_nclip();
        gte_stopz(&sp->m2D4_flag);
        if (sp->m2D4_flag > 0) {
            continue;
        }
        sp->m250_localMatrix.t[0] = k;
        sp->m250_localMatrix.t[1] = worldmapCloudPositions[i].vy >> 12;
        sp->m250_localMatrix.t[2] = l;
        gte_CompMatrix(&worldmapMainMatrix2, &sp->m250_localMatrix, &sp->m270);
        gte_SetRotMatrix(&sp->m270);
        gte_SetTransMatrix(&sp->m270);
        sp->m1F0_quad[0].vx = sp->m1F0_quad[0].vy = sp->m1F0_quad[0].vz = 0;
        gte_ldv0(&sp->m1F0_quad[0]);
        gte_rtps();
        sp->m2D0_uvIndex = worldmapCloudSteps[i].vy * 4;
        gte_stflg(&sp->m2D4_flag);
        if (sp->m2D4_flag & 0x7F85E000) {
            continue;
        }
        gte_stsz(&sp->m2DC_sz[0]);
        if (sp->m2DC_sz[0] > 0x580) {
            sp->m2EC_vertices = sp->m0_near;
            for (j = 0; j < 3; j++) {
                gte_ldv3c(sp->m2EC_vertices);
                gte_rtpt();
                uv = sp->m1E0_uv[sp->m2D0_uvIndex++];
                gte_stflg(&sp->m2D4_flag);
                if (!(sp->m2D4_flag & 0x80000000)) {
                    gte_stsxy3_r(p0, p1, p2);
                    gte_ldv0(&sp->m2EC_vertices[3]);
                    gte_rtps();
                    gte_stflg(&sp->m2D4_flag);
                    if (!(sp->m2D4_flag & 0x80000000)) {
                        gte_stsxy2_r(p3);
                        if (((p0 & 0xFFFF) < 0x140 || (p1 & 0xFFFF) < 0x140 || (p2 & 0xFFFF) < 0x140 || (p3 & 0xFFFF) < 0x140) &&
                            ((p0 >> 16) < 0xD8 || (p1 >> 16) < 0xD8 || (p2 >> 16) < 0xD8 || (p3 >> 16) < 0xD8)) {
                            gte_stsz4c(sp->m2DC_sz);
                            if (sp->m2DC_sz[0] > sp->m2DC_sz[1]) {
                                sp->m2D8_depth = sp->m2DC_sz[0];
                            } else {
                                sp->m2D8_depth = sp->m2DC_sz[1];
                            }
                            if (sp->m2DC_sz[2] > sp->m2D8_depth) {
                                sp->m2D8_depth = sp->m2DC_sz[2];
                            } else if (sp->m2DC_sz[3] > sp->m2D8_depth) {
                                sp->m2D8_depth = sp->m2DC_sz[3];
                            }
                            if (sp->m2D8_depth > 0xD00) {
                                break;
                            }
                            /* Smooth motion: the cloud, its detail level, layer and piece. */
                            pcInterpolationKeyType(worldmapCloudPositions, worldmapCloudPositions, i << 8 | 0 << 6 | j << 4, prim, PC_INTERP_PARTICLE);
                            ADD_PRIM_ASM(&pCurrentWorldmapRenderingStruct->m70_OT[sp->m2D8_depth >> 4], prim, 9);
                            prim->m14_uv1 = uv | 0x3F;
                            prim->m1C_uv2 = uv | 0x3F00;
                            prim->m8_xy0 = p0;
                            gte_copy_sxy(&prim->m8_xy0, &p0);
                            prim->m10_xy1 = p1;
                            gte_copy_sxy(&prim->m10_xy1, &p1);
                            prim->m18_xy2 = p2;
                            gte_copy_sxy(&prim->m18_xy2, &p2);
                            prim->m20_xy3 = p3;
                            gte_copy_sxy(&prim->m20_xy3, &p3);
                            prim->mC_uv0 = uv;
                            prim->m24_uv3 = uv | 0x3F3F;
                            prim++;
                            sp->m2F0_count++;
                            if (sp->m2D8_depth > 0xB00) {
                                break;
                            }
                        }
                    }
                }
                sp->m2EC_vertices += 4;
                sp->m2F4_numQuads++;
            }
        } else if (sp->m2DC_sz[0] > 0x400) {
            sp->m2EC_vertices = sp->m60_mid;
            for (j = 0; j < 3; j++) {
                for (k = 0; k < 4; k++) {
                    gte_ldv3c(sp->m2EC_vertices);
                    gte_rtpt();
                    uv = sp->m1E0_uv[j + sp->m2D0_uvIndex] + ((k & 2) << 12) + ((k & 1) << 5);
                    gte_stflg(&sp->m2D4_flag);
                    if (!(sp->m2D4_flag & 0x80000000)) {
                        gte_stsxy3_r(p0, p1, p2);
                        gte_ldv0(&sp->m2EC_vertices[3]);
                        gte_rtps();
                        gte_stflg(&sp->m2D4_flag);
                        if (!(sp->m2D4_flag & 0x80000000)) {
                            gte_stsxy2_r(p3);
                            if (((p0 & 0xFFFF) < 0x140 || (p1 & 0xFFFF) < 0x140 || (p2 & 0xFFFF) < 0x140 || (p3 & 0xFFFF) < 0x140) &&
                                ((p0 >> 16) < 0xD8 || (p1 >> 16) < 0xD8 || (p2 >> 16) < 0xD8 || (p3 >> 16) < 0xD8)) {
                                gte_stsz4c(sp->m2DC_sz);
                                if (sp->m2DC_sz[0] > sp->m2DC_sz[1]) {
                                    sp->m2D8_depth = sp->m2DC_sz[0];
                                } else {
                                    sp->m2D8_depth = sp->m2DC_sz[1];
                                }
                                if (sp->m2DC_sz[2] > sp->m2D8_depth) {
                                    sp->m2D8_depth = sp->m2DC_sz[2];
                                } else if (sp->m2DC_sz[3] > sp->m2D8_depth) {
                                    sp->m2D8_depth = sp->m2DC_sz[3];
                                }
                                pcInterpolationKeyType(worldmapCloudPositions, worldmapCloudPositions, i << 8 | 1 << 6 | j << 4 | k, prim, PC_INTERP_PARTICLE);
                                ADD_PRIM_ASM(&pCurrentWorldmapRenderingStruct->m70_OT[sp->m2D8_depth >> 4], prim, 9);
                                prim->m14_uv1 = uv | 0x1F;
                                prim->m1C_uv2 = uv | 0x1F00;
                                prim->m8_xy0 = p0;
                                gte_copy_sxy(&prim->m8_xy0, &p0);
                                prim->m10_xy1 = p1;
                                gte_copy_sxy(&prim->m10_xy1, &p1);
                                prim->m18_xy2 = p2;
                                gte_copy_sxy(&prim->m18_xy2, &p2);
                                prim->m20_xy3 = p3;
                                gte_copy_sxy(&prim->m20_xy3, &p3);
                                prim->mC_uv0 = uv;
                                prim->m24_uv3 = uv | 0x1F1F;
                                prim++;
                                sp->m2F0_count++;
                            }
                        }
                    }
                    sp->m2EC_vertices += 4;
                    sp->m2F4_numQuads++;
                }
            }
        } else {
            for (j = 0; j < 3; j++) {
                s16 rx, rz, ry;

                rx = sp->m0_near[0].vx;
                rz = sp->m0_near[0].vz;
                sp->m1F0_quad[0].vx = sp->m1F0_quad[2].vx = rx;
                sp->m1F0_quad[0].vz = sp->m1F0_quad[1].vz = rz;
                k = rx;
                l = rz;
                k += 0x180;
                l -= 0x180;
                sp->m1F0_quad[1].vx = sp->m1F0_quad[3].vx = k;
                sp->m1F0_quad[2].vz = sp->m1F0_quad[3].vz = l;
                ry = sp->m0_near[0].vy - j * 8;
                sp->m1F0_quad[0].vy = sp->m1F0_quad[1].vy = sp->m1F0_quad[2].vy = sp->m1F0_quad[3].vy = ry;
                gte_ldv0(&sp->m1F0_quad[0]);
                gte_rtps();
                gte_stflg(&sp->m2D4_flag);
                if (sp->m2D4_flag & 0x7F85E000) {
                    continue;
                }
                gte_ldv3c(&sp->m1F0_quad[1]);
                gte_rtpt();
                gte_stflg(&sp->m2D4_flag);
                if (sp->m2D4_flag & 0x7F85E000) {
                    continue;
                }
                for (k = 0; k < 4; k++) {
                    for (l = 0; l < 4; l++) {
                        s16 qx = sp->m0_near[0].vx + l * 0x60;
                        s16 qz = sp->m0_near[0].vz - k * 0x60;
                        s32 ex = qx + 0x60;
                        s32 ez = qz - 0x60;

                        sp->m1F0_quad[0].vx = sp->m1F0_quad[2].vx = qx;
                        sp->m1F0_quad[0].vz = sp->m1F0_quad[1].vz = qz;
                        sp->m1F0_quad[1].vx = sp->m1F0_quad[3].vx = ex;
                        sp->m1F0_quad[2].vz = sp->m1F0_quad[3].vz = ez;
                        gte_ldv3c(&sp->m1F0_quad[0]);
                        gte_rtpt();
                        uv = sp->m1E0_uv[j + sp->m2D0_uvIndex] + ((k << 12) + (l << 4));
                        gte_stflg(&sp->m2D4_flag);
                        if (!(sp->m2D4_flag & 0x7F85E000)) {
                            gte_stsxy3_r(p0, p1, p2);
                            gte_ldv0(&sp->m1F0_quad[3]);
                            gte_rtps();
                            gte_stflg(&sp->m2D4_flag);
                            if (!(sp->m2D4_flag & 0x7F85E000)) {
                                gte_stsxy2_r(p3);
                                if (((p0 & 0xFFFF) < 0x140 || (p1 & 0xFFFF) < 0x140 || (p2 & 0xFFFF) < 0x140 || (p3 & 0xFFFF) < 0x140) &&
                                    ((p0 >> 16) < 0xD8 || (p1 >> 16) < 0xD8 || (p2 >> 16) < 0xD8 || (p3 >> 16) < 0xD8)) {
                                    gte_stsz4c(sp->m2DC_sz);
                                    if (sp->m2DC_sz[0] > sp->m2DC_sz[1]) {
                                        sp->m2D8_depth = sp->m2DC_sz[0];
                                    } else {
                                        sp->m2D8_depth = sp->m2DC_sz[1];
                                    }
                                    if (sp->m2DC_sz[2] > sp->m2D8_depth) {
                                        sp->m2D8_depth = sp->m2DC_sz[2];
                                    } else if (sp->m2DC_sz[3] > sp->m2D8_depth) {
                                        sp->m2D8_depth = sp->m2DC_sz[3];
                                    }
                                    pcInterpolationKeyType(worldmapCloudPositions, worldmapCloudPositions, i << 8 | 2 << 6 | j << 4 | k << 2 | l, prim, PC_INTERP_PARTICLE);
                                    ADD_PRIM_ASM(&pCurrentWorldmapRenderingStruct->m70_OT[sp->m2D8_depth >> 4], prim, 9);
                                    prim->m14_uv1 = uv | 0xF;
                                    prim->m1C_uv2 = uv | 0xF00;
                                    prim->m8_xy0 = p0;
                                    gte_copy_sxy(&prim->m8_xy0, &p0);
                                    prim->m10_xy1 = p1;
                                    gte_copy_sxy(&prim->m10_xy1, &p1);
                                    prim->m18_xy2 = p2;
                                    gte_copy_sxy(&prim->m18_xy2, &p2);
                                    prim->m20_xy3 = p3;
                                    gte_copy_sxy(&prim->m20_xy3, &p3);
                                    prim->mC_uv0 = uv;
                                    prim->m24_uv3 = uv | 0xF0F;
                                    prim++;
                                    sp->m2F0_count++;
                                }
                            }
                        }
                        sp->m2F4_numQuads++;
                    }
                }
            }
        }
    }
    /* The wide picture's margins (port/src/game/worldmap/wide.c). */
    pcWideWorldmapClouds();
}


/* task init (model spin) */
s32 worldmap_taskSpinModels_init(s32 index) {
    gWorldmapState->m0_entries[index].m50 = 0;
    gWorldmapState->m0_entries[index].m54 = 8;
    return 1;
}

/* task update: spin models 12/14 */
s32 worldmap_taskSpinModels_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = worldmapModels;
    SVECTOR* r = (SVECTOR*)0x1F8000A0;
    e->m50 = (e->m50 + e->m54) & 0xFFF;
    r[1].vx = 0;
    r[0].vx = 0;
    r[0].vy = m[12].m18_rotation.vy;
    r[1].vy = m[14].m18_rotation.vy;
    r[0].vz = r[1].vz = e->m50;
    createRotationMatrix(&r[0], &m[12].m20_rotationMatrix);
    createRotationMatrix(&r[1], &m[14].m20_rotationMatrix);
    return 1;
}

/* task init (model spin) */
s32 worldmap_taskSpinModelPair_init(s32 index) {
    gWorldmapState->m0_entries[index].m50 = 0;
    gWorldmapState->m0_entries[index].m54 = 8;
    return 1;
}

/* task update: spin model pair worldmapSpinModelPairs[initWorldMapVar0] (unused SVECTOR local reserves 8 stack bytes) */
s32 worldmap_taskSpinModelPair_update(s32 index) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = worldmapModels;
    SVECTOR* r = (SVECTOR*)0x1F8000A0;
    SVECTOR unused; /* reserves retail's stack frame */
    sWorldmapModel* m0 = &m[worldmapSpinModelPairs[initWorldMapVar0][0]];
    sWorldmapModel* m1 = &m[worldmapSpinModelPairs[initWorldMapVar0][1]];
    e->m50 = (e->m50 + e->m54) & 0xFFF;
    r[1].vx = 0;
    r[0].vx = 0;
    r[0].vy = m0->m18_rotation.vy;
    r[1].vy = m1->m18_rotation.vy;
    r[0].vz = r[1].vz = e->m50;
    createRotationMatrix(&r[0], &m0->m20_rotationMatrix);
    createRotationMatrix(&r[1], &m1->m20_rotationMatrix);
    return 1;
}

void setWorldmapModelSemiTrans(sWorldmapModel* model, POLY_FT4* poly, s32 count, s32 abr) {
    s32 i;

    for (i = 0; i < count; i++) {
        poly->tpage = GetTPage(0, abr, 0x1A0, 0xA0);
        poly->code |= 2;
        poly++;
    }
    memcpy(model->m48_primBuffers[1], model->m48_primBuffers[0], count * sizeof(POLY_FT4));
}

s32 worldmap_taskTranslucentSpin_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    p->m50 = 0;
    p->m54 = 0x10;
    worldmap_taskTranslucentSpin_reinit(index);
    return 1;
}

s32 worldmap_taskTranslucentSpin_reinit(s32 index) {
    s32 pad[2]; /* reserves retail's stack frame */
    sWorldmapModel* a = &worldmapModels[worldmapTranslucentSpinModelPairs[initWorldMapVar0][0]];
    sWorldmapModel* b = &worldmapModels[worldmapTranslucentSpinModelPairs[initWorldMapVar0][1]];

    setWorldmapModelSemiTrans(a, (POLY_FT4*)a->m48_primBuffers[0], a->m40_modelBlock->m4_numPrims, 3);
    setWorldmapModelSemiTrans(b, (POLY_FT4*)b->m48_primBuffers[0], b->m40_modelBlock->m4_numPrims, 3);
    return 1;
}

s32 worldmap_taskTranslucentSpin_update(s32 index) {
    s32 pad[2]; /* reserves retail's stack frame */
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    SVECTOR* rot = (SVECTOR*)0x1F8000A0;
    sWorldmapModel* a = &worldmapModels[worldmapTranslucentSpinModelPairs[initWorldMapVar0][0]];
    sWorldmapModel* b = &worldmapModels[worldmapTranslucentSpinModelPairs[initWorldMapVar0][1]];

    p->m50 = (p->m50 + p->m54) & 0xFFF;
    rot[0].vx = rot[1].vx = rot[0].vz = rot[1].vz = 0;
    rot[0].vy = rot[1].vy = p->m50;
    createRotationMatrix(&rot[0], &a->m20_rotationMatrix);
    createRotationMatrix(&rot[1], &b->m20_rotationMatrix);
    return 1;
}

void computeWorldmapLookMatrix(VECTOR* forward, VECTOR* tmp, MATRIX* m) {
    tmp->vz = 0;
    tmp->vx = 0;
    tmp->vy = 0x1000;
    OuterProduct12(tmp, forward, tmp);
    VectorNormal(tmp, tmp);
    m->m[0][0] = tmp->vx;
    m->m[0][1] = tmp->vy;
    m->m[0][2] = tmp->vz;
    OuterProduct12(forward, tmp, tmp);
    VectorNormal(tmp, tmp);
    m->m[1][0] = tmp->vx;
    m->m[1][1] = tmp->vy;
    m->m[1][2] = tmp->vz;
    m->m[2][0] = forward->vx;
    m->m[2][1] = forward->vy;
    m->m[2][2] = forward->vz;
    TransposeMatrix(m, m);
}

s32 worldmap_taskWaypointShip_init(s32 index) {
    VECTOR* v = (VECTOR*)0x1F800000;
    s32 modelIdx = worldmapWaypointShipModels[initWorldMapVar0];
    sWorldmapStateEntry* p;
    sWorldmapModel* m;
    s32 d;
    s32 i;

    worldmap_taskWaypointShip_reinit(index);
    p = &gWorldmapState->m0_entries[index];
    p->m4A_stepScale = 0x4000;
    p->m20_subState = 0;
    m = &worldmapModels[modelIdx];
    if (gameState.m1930_fieldVarsBackup[0] < 0xCD) {
        p->m28_position.vx = 0xD80000;
        p->m28_position.vz = 0x7280000;
        m->m20_rotationMatrix = worldMapIdentityMatrix;
        p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    } else {
        if (gameState.m184A_waypointShipPlaced == 0) {
            gameState.m184A_waypointShipPlaced++;
            p->m50 = 0;
            p->m28_position.vx = worldmapWaypointShipX[p->m50] << 12;
            p->m28_position.vz = worldmapWaypointShipZ[p->m50] << 12;
            p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        } else {
            p->m50 = gameState.m1848_waypointShipWaypoint;
            p->m28_position.vx = gameState.m1844_waypointShipPosition[0] << 12;
            p->m28_position.vz = gameState.m1844_waypointShipPosition[1] << 12;
            p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        }
        v->vx = worldmapWaypointShipX[p->m50] << 12;
        v->vz = worldmapWaypointShipZ[p->m50] << 12;
        d = computeWorldmapDistance2D(&p->m28_position, v);
        if (d < 0) {
            d = -d;
        }
        if (d < 8) {
            p->m50 = (p->m50 + 1) & 7;
        }
        v->vx = worldmapWaypointShipX[p->m50] - (p->m28_position.vx >> 12);
        v->vz = worldmapWaypointShipZ[p->m50] - (p->m28_position.vz >> 12);
        v->vy = 0;
        adjustWorldModelLocation(v);
        VectorNormal(v, v);
        for (i = 0; i < 32; i++) {
            worldmapWaypointShipSteps[i].vx = v->vx;
            worldmapWaypointShipSteps[i].vz = v->vz;
        }
        p->m58 = 1;
        p->m54 = 0;
        p->m38_step.vx = worldmapWaypointShipSteps[1].vx;
        p->m38_step.vz = worldmapWaypointShipSteps[p->m58].vz;
        p->m38_step.vy = 0;
    }
    return 1;
}
s32 worldmap_taskWaypointShip_reinit(s32 index) {
    u16 base = worldmapWaypointShipModels[initWorldMapVar0];

    attachWorldmapModels(base, base - 4);
    attachWorldmapModels(base, base - 3);
    attachWorldmapModels(base, base - 2);
    attachWorldmapModels(base, base - 1);
    return 1;
}

s32 worldmap_taskWaypointShip_update(s32 index) {
    VECTOR* v = (VECTOR*)0x1F800000;
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = &worldmapModels[worldmapWaypointShipModels[initWorldMapVar0]];
    s32 d;
    s32 dist;

    if (gameState.m1930_fieldVarsBackup[0] < 0xCD) {
        p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        m->m8_position.vx = p->m28_position.vx >> 12;
        m->m8_position.vy = p->m28_position.vy >> 12;
        m->m8_position.vz = p->m28_position.vz >> 12;
    } else {
        v->vx = worldmapWaypointShipX[p->m50] << 12;
        v->vz = worldmapWaypointShipZ[p->m50] << 12;
        d = computeWorldmapDistance2D(&p->m28_position, v);
        if (d < 0) {
            d = -d;
        }
        if (d < 8) {
            p->m50 = (p->m50 + 1) & 7;
        }
        v->vx = worldmapWaypointShipX[p->m50] - (p->m28_position.vx >> 12);
        v->vz = worldmapWaypointShipZ[p->m50] - (p->m28_position.vz >> 12);
        v->vy = 0;
        adjustWorldModelLocation(v);
        VectorNormal(v, v);
        p->m38_step.vx = ((v->vx + p->m38_step.vx * 63) << 6) >> 12;
        p->m38_step.vz = ((v->vz + p->m38_step.vz * 63) << 6) >> 12;
        VectorNormal(&p->m38_step, &p->m38_step);
        worldmapWaypointShipSteps[p->m54].vx = p->m38_step.vx;
        worldmapWaypointShipSteps[p->m54].vz = p->m38_step.vz;
        p->m54 = (p->m54 + 1) & 0x1F;
        v->vx = p->m38_step.vx;
        v->vy = p->m38_step.vy;
        v->vz = -p->m38_step.vz;
        computeWorldmapLookMatrix(v, (VECTOR*)0x1F800010, (MATRIX*)0x1F8000F0);
        m->m20_rotationMatrix = *(MATRIX*)0x1F8000F0;
        p->m28_position.vx += worldmapWaypointShipSteps[p->m58].vx * (p->m4A_stepScale >> 12);
        p->m28_position.vz += worldmapWaypointShipSteps[p->m58].vz * (p->m4A_stepScale >> 12);
        p->m58 = (p->m58 + 1) & 0x1F;
        checkWorldmapPositionSub0(&p->m28_position);
        p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        m->m8_position.vx = p->m28_position.vx >> 12;
        m->m8_position.vy = p->m28_position.vy >> 12;
        m->m8_position.vz = p->m28_position.vz >> 12;
        v->vx = gameState.m182C_vehiclePosition[0] - (p->m28_position.vx >> 12);
        v->vz = gameState.m182C_vehiclePosition[2] - (p->m28_position.vz >> 12);
        v->vy = (s16)gameState.m182C_vehiclePosition[1];
        adjustWorldModelLocation(v);
        dist = SquareRoot0(v->vx * v->vx + v->vz * v->vz);
        if (dist < 0x100 && v->vy >= -0xBF) {
            p->m4A_stepScale = 0;
        } else if (dist < 0x300 && v->vy >= -0xBF) {
            p->m4A_stepScale -= 0x100;
            if (p->m4A_stepScale < 0) {
                p->m4A_stepScale = 0;
            }
        } else {
            p->m4A_stepScale += 0x200;
            if (p->m4A_stepScale > 0x4000) {
                p->m4A_stepScale = 0x4000;
            }
        }
        if (p->m4A_stepScale > 0x1000) {
            ((SVECTOR*)(v + 10))->vx = m->m8_position.vx;
            ((SVECTOR*)(v + 10))->vy = m->m8_position.vy;
            ((SVECTOR*)(v + 10))->vz = m->m8_position.vz;
            TransposeMatrix(&m->m20_rotationMatrix, (MATRIX*)(v + 21));
            extractWorldmapEulerAngles((MATRIX*)(v + 21), (SVECTOR*)(v + 10) + 1);
            spawnWorldmapParticles(0x13, (SVECTOR*)(v + 10), (SVECTOR*)(v + 10) + 1);
        } else {
            clearWorldmapParticles(0x13);
        }
    }
    gameState.m1844_waypointShipPosition[0] = p->m28_position.vx >> 12;
    gameState.m1844_waypointShipPosition[1] = p->m28_position.vz >> 12;
    gameState.m1848_waypointShipWaypoint = p->m50;
    v->vx = p->m28_position.vx;
    v->vy = p->m28_position.vy + 0x30000;
    v->vz = p->m28_position.vz;
    addWorldmapDynamicCollisions(index, v, 0x80, 0xB0);
    return 1;
}

s32 worldmap_taskFlyingShip_init(s32 index) {
    sWorldmapStateEntry* p;

    worldmap_taskFlyingShip_reinit(index);
    p = &gWorldmapState->m0_entries[index];
    p->m54 = 4;
    p->m20_subState = 0;
    p->m50 = 0;
    p->m58 = 0;
    p->m5C = 0xC;
    if (gameState.m184E_flyingShipPosition[3] == 0) {
        gameState.m184E_flyingShipPosition[3]++;
        p->m28_position.vy = -0x280000;
        p->m28_position.vx = 0;
        p->m28_position.vz = 0x4800000;
    } else {
        p->m28_position.vx = ((s16)gameState.m184E_flyingShipPosition[0] << 12) + gameState.m184C_flyingShipXLow;
        p->m28_position.vy = -0x280000;
        p->m28_position.vz = ((s16)gameState.m184E_flyingShipPosition[2] << 12) + gameState.m184E_flyingShipPosition[1];
    }
    p->m38_step.vz = 0xB50;
    p->m38_step.vx = 0xB50;
    p->m38_step.vy = 0;
    p->m4A_stepScale = 1;
    gameState.m184C_flyingShipXLow = p->m28_position.vx;
    gameState.m184E_flyingShipPosition[0] = p->m28_position.vx >> 12;
    gameState.m184E_flyingShipPosition[1] = p->m28_position.vz;
    gameState.m184E_flyingShipPosition[2] = p->m28_position.vz >> 12;
    return 1;
}

s32 worldmap_taskFlyingShip_reinit(s32 index) {
    s32 base = worldmapFlyingShipModelBases[initWorldMapVar0];
    s32 i;

    for (i = 0; worldmapFlyingShipModelLinks[i * 2] != -1; i++) {
        attachWorldmapModels(base + worldmapFlyingShipModelLinks[i * 2], base + worldmapFlyingShipModelLinks[i * 2 + 1]);
    }
    return 1;
}

s32 worldmap_taskFlyingShip_update(s32 index) {
    VECTOR* v = (VECTOR*)0x1F800000;
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 base = worldmapFlyingShipModelBases[initWorldMapVar0];
    sWorldmapModel* m;

    p->m50 = (p->m50 + p->m54) & 0xFFF;
    p->m58 = (p->m58 + p->m5C) & 0xFFF;
    ((SVECTOR*)0x1F8000A0)->vz = 0;
    ((SVECTOR*)0x1F8000A0)->vx = 0;
    ((SVECTOR*)0x1F8000A0)->vy = p->m50;
    ((SVECTOR*)0x1F8000A8)->vz = 0;
    ((SVECTOR*)0x1F8000A8)->vx = 0;
    ((SVECTOR*)0x1F8000A8)->vy = p->m58;
    RotMatrixYXZ((SVECTOR*)0x1F8000A0, (MATRIX*)0x1F8000F0);
    RotMatrixYXZ((SVECTOR*)0x1F8000A8, (MATRIX*)0x1F800110);
    (&worldmapModels[base])[5].m20_rotationMatrix = *(MATRIX*)0x1F8000F0;
    m = (sWorldmapModel*)(base * sizeof(sWorldmapModel) + (uptr)worldmapModels); /* offset first: (plus offset D) ties m to the offset qty */
    m[3].m20_rotationMatrix = *(MATRIX*)0x1F800110;
    m[2].m20_rotationMatrix = m[3].m20_rotationMatrix;
    m[1].m20_rotationMatrix = m[2].m20_rotationMatrix;
    m[0].m20_rotationMatrix = m[1].m20_rotationMatrix;
    v->vx = gameState.m182C_vehiclePosition[0] - (p->m28_position.vx >> 12);
    v->vz = gameState.m182C_vehiclePosition[2] - (p->m28_position.vz >> 12);
    v->vy = (s16)gameState.m182C_vehiclePosition[1];
    adjustWorldModelLocation(v);
    if (SquareRoot0(v->vx * v->vx + v->vz * v->vz) < 0x300 && v->vy < -0x240) {
        p->m4A_stepScale = 0;
    } else {
        p->m4A_stepScale = 1;
    }
    p->m28_position.vx += p->m38_step.vx * p->m4A_stepScale;
    p->m28_position.vz += p->m38_step.vz * p->m4A_stepScale;
    checkWorldmapPositionSub0(&p->m28_position);
    v->vx = p->m28_position.vx >> 12;
    v->vy = p->m28_position.vy >> 12;
    v->vz = p->m28_position.vz >> 12;
    worldmapModels[base + 12].m8_position = *v;
    addWorldmapDynamicCollisions(index, &p->m28_position, 0x180, 0xC0);
    gameState.m184C_flyingShipXLow = p->m28_position.vx;
    gameState.m184E_flyingShipPosition[0] = p->m28_position.vx >> 12;
    gameState.m184E_flyingShipPosition[1] = p->m28_position.vz;
    gameState.m184E_flyingShipPosition[2] = p->m28_position.vz >> 12;
    return 1;
}
s32 worldmap_taskEventModel_init(s32 index) {
    sWorldmapStateEntry* p;
    sWorldmapModel* m;
    s32 i;
    s32 ret;

    for (i = 0; worldmapEventModelChildren[i] != -1; i++) {
        attachWorldmapModels(0x45, worldmapEventModelChildren[i]);
    }
    p = &gWorldmapState->m0_entries[index]; p->m20_subState = 0; m = worldmapModels;
    if (gameState.m1930_fieldVarsBackup[0] == 0x99) {
        ret = 1;
        p->m28_position.vx = 0x2000000;
        p->m28_position.vz = 0x4120000;
        p->m28_position.vy = 0;
    } else {
        m[0x45].m0_hidden = 1;
        for (i = 0; worldmapEventModelChildren[i] != -1; i++) {
            m[worldmapEventModelChildren[i]].m0_hidden = 1;
        }
        ret = 3;
    }
    m += 0x45;
    m->m8_position.vx = p->m28_position.vx >> 12;
    m->m8_position.vy = p->m28_position.vy >> 12;
    m->m8_position.vz = p->m28_position.vz >> 12;
    return ret;
}

s32 worldmap_taskEventModel_reinit(s32 index) {
    sWorldmapModel* models = worldmapModels;
    s32 i;

    models[0x45].m0_hidden = 1;
    for (i = 0; worldmapEventModelChildren[i] != -1; i++) {
        models[worldmapEventModelChildren[i]].m0_hidden = 1;
    }
    return 3;
}

s32 worldmap_taskEventModel_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m = worldmapModels;

    m += 0x45;
    if (gameState.m1930_fieldVarsBackup[0] == 0x99) {
        m->m8_position.vx = p->m28_position.vx >> 12;
        m->m8_position.vy = p->m28_position.vy >> 12;
        m->m8_position.vz = p->m28_position.vz >> 12;
    }
    return 1;
}

s32 worldmap_taskModelColliderA_init(s32 index) {
    sWorldmapModel* m = &worldmapModels[worldmapColliderAModels[initWorldMapVar0]];
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m28_position.vx = m->m8_position.vx << 12;
    p->m28_position.vy = (m->m8_position.vy << 12) + 0x40000;
    p->m28_position.vz = m->m8_position.vz << 12;
    return 1;
}

s32 worldmap_taskModelColliderA_update(s32 index) {
    addWorldmapDynamicCollisions(index, &gWorldmapState->m0_entries[index].m28_position, 0x68, 0x60);
    return 1;
}

s32 worldmap_taskModelColliderB_init(s32 index) {
    sWorldmapModel* m = worldmapModels;
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    p->m28_position.vx = m[0x4B].m8_position.vx << 12;
    p->m28_position.vy = m[0x4B].m8_position.vy << 12;
    p->m28_position.vz = m[0x4B].m8_position.vz << 12;
    return 1;
}

s32 worldmap_taskModelColliderB_update(s32 index) {
    addWorldmapDynamicCollisions(index, &gWorldmapState->m0_entries[index].m28_position, 0x10C, 0x1A6);
    return 1;
}

s32 worldmap_taskModelColliderC_init(s32 index) {
    sWorldmapModel* m = &worldmapModels[worldmapColliderCModels[initWorldMapVar0]];
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m28_position.vx = m->m8_position.vx << 12;
    p->m28_position.vy = m->m8_position.vy << 12;
    p->m28_position.vz = m->m8_position.vz << 12;
    return 1;
}

s32 worldmap_taskModelColliderC_update(s32 index) {
    addWorldmapDynamicCollisions(index, &gWorldmapState->m0_entries[index].m28_position, 0xC9, 0x392);
    return 1;
}

s32 worldmap_taskIdle_init(s32 index) {
    return 3;
}

s32 worldmap_taskIdle_update(s32 index) {
    return 3;
}

void initWorldmapParticles(void) {
    s32 i;
    sWorldmapParticle* p;
    sWorldmapParticle2* q;

    p = worldmapFile1Buffer_18;
    for (i = 0x1FF; i != -1; i--) {
        p->m4_timer = 0;
        p->mA_numParticles = 0;
        p->m12_periodTimer = 0;
        p->m14_position.vz = 0;
        p->m14_position.vy = 0;
        p->m14_position.vx = 0;
        p->m1C_rotation.vz = 0;
        p->m1C_rotation.vy = 0;
        p->m1C_rotation.vx = 0;
        p++;
    }
    worldmapParticleInstances = allocateHeap(0x4C00, 0);
    q = worldmapParticleInstances;
    for (i = 0xFF; i != -1; i--) {
        q->m6_active = 0;
        q->m4_life = 0;
        q++;
    }
}

void freeWorldmapParticles(void) {
    freeHeap(worldmapParticleInstances);
}

void createWorlmapPolyFT4Array1(void) {
    POLY_FT4* p;
    s32 i;

    worldmapFT4_1[0] = allocateHeap(256 * (s32)sizeof(POLY_FT4), 1);
    worldmapFT4_1[1] = allocateHeap(256 * (s32)sizeof(POLY_FT4), 1);
    p = worldmapFT4_1[0];
    for (i = 255; i != -1; i--) {
        setlen(p, 9);
        setcode(p, 0x2C);
        p->tpage = GetTPage(1, 1, 0x340, 0x100);
        p->clut = GetClut(0x100, 0x1FF);
        p->code |= 2;
        p++;
    }
    *(PolyFT4Block*)worldmapFT4_1[1] = *(PolyFT4Block*)worldmapFT4_1[0];
}

void freeWorldmapPolyFT4Array1(void) {
    freeHeap(worldmapFT4_1[0]);
    freeHeap(worldmapFT4_1[1]);
}

void spawnWorldmapParticles(s32 group, SVECTOR* pos, SVECTOR* rot) {
    sWorldmapParticle* e;
    s32 i;
    s32 found = 0;

    e = &worldmapFile1Buffer_18[group * 8];
    for (i = 7; i != -1; i--) {
        if (e->m4C_color.m0_c.m3_flags & 0x80) {
            found++;
            break;
        }
    }
    e = &worldmapFile1Buffer_18[group * 8];
    if ((pos == NULL) & (rot == NULL)) {
        for (i = 7; i != -1; i--) {
            if (found == 0) {
                e->m4C_color.m0_c.m3_flags |= 0x80;
                e->m4_timer = e->m0_timerInit;
                e->m12_periodTimer = e->m10_period;
                e->mA_numParticles = 0;
            }
            *(s32*)&e->m1C_rotation.vx = 0;
            *(s32*)&e->m14_position.vx = 0;
            e->m1C_rotation.vz = 0;
            e->m14_position.vz = 0;
            e++;
        }
    } else if ((pos != NULL) & (rot == NULL)) {
        for (i = 7; i != -1; i--) {
            if (found == 0) {
                e->m4C_color.m0_c.m3_flags |= 0x80;
                e->m4_timer = e->m0_timerInit;
                e->m12_periodTimer = e->m10_period;
                e->mA_numParticles = 0;
            }
            e->m14_position = *pos;
            *(s32*)&e->m1C_rotation.vx = 0;
            e->m1C_rotation.vz = 0;
            e++;
        }
    } else if ((pos == NULL) & (rot != NULL)) {
        for (i = 7; i != -1; i--) {
            if (found == 0) {
                e->m4C_color.m0_c.m3_flags |= 0x80;
                e->m4_timer = e->m0_timerInit;
                e->m12_periodTimer = e->m10_period;
                e->mA_numParticles = 0;
            }
            *(s32*)&e->m14_position.vx = 0;
            e->m14_position.vz = 0;
            e->m1C_rotation.vx = -rot->vx;
            e->m1C_rotation.vy = -rot->vy;
            e->m1C_rotation.vz = -rot->vz;
            e++;
        }
    } else {
        for (i = 7; i != -1; i--) {
            if (found == 0) {
                e->m4C_color.m0_c.m3_flags |= 0x80;
                e->m4_timer = e->m0_timerInit;
                e->m12_periodTimer = e->m10_period;
                e->mA_numParticles = 0;
            }
            e->m14_position = *pos;
            e->m1C_rotation.vx = -rot->vx;
            e->m1C_rotation.vy = -rot->vy;
            e->m1C_rotation.vz = -rot->vz;
            e++;
        }
    }
}

void spawnWorldmapParticlesDirected(s32 group, sWorldmapSVecXYZ* dir, sWorldmapSVecXYZ* rot) {
    sWorldmapParticle* e;
    s32 i;
    s32 found = 0;

    e = &worldmapFile1Buffer_18[group * 8];
    for (i = 7; i != -1; i--) {
        if (e->m4C_color.m0_c.m3_flags & 0x80) {
            found++;
            break;
        }
    }
    e = &worldmapFile1Buffer_18[group * 8];
    for (i = 7; i != -1; i--) {
        if (found == 0) {
            e->m4C_color.m0_c.m3_flags |= 0x80;
            e->m4_timer = e->m0_timerInit;
            e->m12_periodTimer = e->m10_period;
            e->mA_numParticles = 0;
        }
        ((sWorldmapSVecXYZ*)&e->m2C_targetDir)->m0_vxvy = dir->m0_vxvy;
        ((sWorldmapSVecXYZ*)&e->m14_position)->m0_vxvy = 0;
        ((sWorldmapSVecXYZ*)&e->m1C_rotation)->m0_vxvy = rot->m0_vxvy;
        ((sWorldmapSVecXYZ*)&e->m2C_targetDir)->m4_vz = dir->m4_vz;
        ((sWorldmapSVecXYZ*)&e->m14_position)->m4_vz = 0;
        ((sWorldmapSVecXYZ*)&e->m1C_rotation)->m4_vz = rot->m4_vz;
        e++;
    }
}

void clearWorldmapParticles(s32 group) {
    s32 i;
    sWorldmapParticle* p = &worldmapFile1Buffer_18[group * 8];

    for (i = 7; i != -1; i--) {
        p->m4C_color.m0_c.m3_flags &= 0x7F;
        p++;
    }
}

void killWorldmapParticles(s32 group) {
    sWorldmapParticle2* p = worldmapParticleInstances;
    s32 i;
    s32 j;

    for (i = 0xFF; i != -1; i--) {
        for (j = 0; j < 8; j++) {
            if (p->m0_emitter == group * 8 + j && p->m6_active != 0) {
                p->m4_life = 0;
                break;
            }
        }
        p++;
    }
}

/* Enhanced, 60 Hz (w_80070CFC.c): the tick draws the particles halfway
 * through their native step (position, size, colour), the frame between
 * where the step took them. A particle born, ended or reborn in the step is
 * drawn where it is. */
static sWorldmapParticle2 pcWm60ParticleBefore[0x100];
static sWorldmapParticle2 pcWm60ParticleAfter[0x100];
void pcWm60ParticlesBefore(void) {
    if (worldmapParticleInstances != NULL) {
        memcpy(pcWm60ParticleBefore, worldmapParticleInstances, sizeof pcWm60ParticleBefore);
    }
}
static s32 pcWm60ParticleMid(s32 a, s32 b) {
    s32 d = b - a;
    if (d > 0x1000000 || d < -0x1000000) return b;
    return a + d / 2;
}
void pcWm60ParticlesMid(void) {
    sWorldmapParticle2* p = worldmapParticleInstances;
    sWorldmapParticle2* a = pcWm60ParticleBefore;
    s32 i;
    s32 k;
    u8* ca;
    u8* cb;
    if (p == NULL) return;
    memcpy(pcWm60ParticleAfter, p, sizeof pcWm60ParticleAfter);
    for (i = 0; i < 0x100; i++, p++, a++) {
        if (p->m6_active == 0 || a->m6_active == 0 || p->m0_emitter != a->m0_emitter || p->m4_life != a->m4_life - 1) {
            continue;
        }
        p->m8_position.vx = pcWm60ParticleMid(a->m8_position.vx, p->m8_position.vx);
        p->m8_position.vy = pcWm60ParticleMid(a->m8_position.vy, p->m8_position.vy);
        p->m8_position.vz = pcWm60ParticleMid(a->m8_position.vz, p->m8_position.vz);
        p->m38_scaleX = a->m38_scaleX + (s16)(p->m38_scaleX - a->m38_scaleX) / 2;
        p->m3A_scaleY = a->m3A_scaleY + (s16)(p->m3A_scaleY - a->m3A_scaleY) / 2;
        ca = (u8*)&a->m40_color;
        cb = (u8*)&p->m40_color;
        for (k = 0; k < 3; k++) cb[k] = (ca[k] + cb[k]) / 2;
    }
}
void pcWm60ParticlesAfter(void) {
    if (worldmapParticleInstances != NULL) {
        memcpy(worldmapParticleInstances, pcWm60ParticleAfter, sizeof pcWm60ParticleAfter);
    }
}

void moveWorldmapParticles(void) {
    sWorldmapParticle2* p = worldmapParticleInstances;
    s32 pad[2]; /* reserves retail's stack frame */
    s32 i;
    s32 x, y;
    s32 px, py, pz;
    s32 vx, vy, vz;
    s32 r, g, b;

    for (i = 0xFF; i != -1; i--) {
        x = *(s32*)&p->m4_life;
        y = x >> 16;
        x = (s16)x;
        if (y != 0) {
            if (x > 0) {
                px = p->m8_position.vx;
                py = p->m8_position.vy;
                pz = p->m8_position.vz;
                vx = p->m18_velocity.vx;
                vy = p->m18_velocity.vy;
                vz = p->m18_velocity.vz;
                y = p->m40_color;
                x = p->m44_colorStep;
                p->m4_life--;
                px += vx;
                py += vy;
                pz += vz;
                vx += p->m28_accel.vx;
                vy += p->m28_accel.vy;
                vz += p->m28_accel.vz;
                p->m38_scaleX += p->m3C_scaleXStep;
                p->m3A_scaleY += p->m3E_scaleYStep;
                r = (y & 0xFF) + *(s8*)&p->m44_colorStep;
                g = ((y >> 8) & 0xFF) + (s8)(x >> 8);
                b = ((y >> 16) & 0xFF) + (s8)(x >> 16);
                if (r < 0) {
                    r = 0;
                }
                if (r >= 0x100) {
                    r = 0xFF;
                }
                if (g < 0) {
                    g = 0;
                }
                if (g >= 0x100) {
                    g = 0xFF;
                }
                if (b < 0) {
                    b = 0;
                }
                if (b >= 0x100) {
                    b = 0xFF;
                }
                p->m40_color = (y & 0xFF000000) | (b << 16) | (g << 8) | r;
                p->m8_position.vx = px;
                p->m8_position.vy = py;
                p->m8_position.vz = pz;
                p->m18_velocity.vx = vx;
                p->m18_velocity.vy = vy;
                p->m18_velocity.vz = vz;
            } else {
                worldmapFile1Buffer_18[p->m0_emitter].mA_numParticles--;
                p->m0_emitter = 0;
                *(s32*)&p->m4_life = 0;
            }
        }
        p++;
    }
}

void updateWorldmapParticleEmitters(void) {
    sWorldmapParticle* e = worldmapFile1Buffer_18;
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapParticle2* part;
    s32 i;
    s32 j;
    s32 flags;
    s32 delay;
    s32 repeat;
    s32 spd;
    s32 per;

    /* Retail keeps both in saved registers, from one emitter to the next
     * (an emitter without the timer flag stores the last timed one's). */
    delay = 0;
    repeat = 0;
    for (i = 0; i < 0x200; i++, e++) {
        flags = e->m4C_color.m0_c.m3_flags;
        if (!(flags & 0x80)) {
            continue;
        }
        if (flags & 0x10) {
            delay = e->m4_timer;
            repeat = delay >> 16;
            delay = (s16)delay;
            if (delay != 0) {
                delay--;
                goto store;
            }
            if (repeat == 0) {
                goto toggle;
            }
            repeat--;
        }
        if (e->m12_periodTimer != 0) {
            e->m12_periodTimer--;
            goto store;
        }
        per = (u16)e->m10_period;
        e->m12_periodTimer = per;
        j = e->m8_maxParticles;
        per = e->mE_active;
        if ((per != 0) & (j > 0) & (e->mA_numParticles < j)) {
            for (part = worldmapParticleInstances, j = 0xFF; j != -1; j--) {
                if (part->m6_active == 0) {
                    pcInterpolationLifetime(part); /* a new particle in the slot (smooth motion) */
                    part->m0_emitter = i;
                    *(s32*)&part->m4_life = *(s32*)&e->mC_life;
                    RotMatrixYXZ(&e->m1C_rotation, &s->mF0_rotation);
                    ApplyMatrix(&s->mF0_rotation, &e->m24_spawnDir, &s->m20);
                    if (!(flags & 0x20)) {
                        s->m10.vy = (rand() & 0xFFF) - 0x800;
                    } else {
                        s->m10.vy = 0;
                    }
                    s->m10.vx = (rand() & 0xFFF) - 0x800;
                    s->m10.vz = (rand() & 0xFFF) - 0x800;
                    VectorNormal(&s->m10, &s->m0);
                    if (flags & 4) {
                        spd = e->m40_spawnRadius;
                    } else {
                        spd = rand() % e->m40_spawnRadius;
                    }
                    part->m8_position.vx = (s->m20.vx << 12) + s->m0.vx * spd + (e->m14_position.vx << 12);
                    part->m8_position.vy = (s->m20.vy << 12) + s->m0.vy * spd + (e->m14_position.vy << 12);
                    part->m8_position.vz = (s->m20.vz << 12) + s->m0.vz * spd + (e->m14_position.vz << 12);
                    ApplyMatrix(&s->mF0_rotation, &e->m2C_targetDir, &s->m20);
                    if (!(flags & 0x40)) {
                        s->m10.vy = (rand() & 0xFFF) - 0x800;
                    } else {
                        s->m10.vy = 0;
                    }
                    s->m10.vx = (rand() & 0xFFF) - 0x800;
                    s->m10.vz = (rand() & 0xFFF) - 0x800;
                    VectorNormal(&s->m10, &s->m0);
                    if (flags & 8) {
                        spd = e->m42_targetRadius;
                    } else {
                        spd = rand() % e->m42_targetRadius;
                    }
                    s->m0.vx = (s->m20.vx << 12) + s->m0.vx * spd + (e->m14_position.vx << 12);
                    s->m0.vy = (s->m20.vy << 12) + s->m0.vy * spd + (e->m14_position.vy << 12);
                    s->m0.vz = (s->m20.vz << 12) + s->m0.vz * spd + (e->m14_position.vz << 12);
                    s->m0.vx = (s->m0.vx - part->m8_position.vx) >> 12;
                    s->m0.vy = (s->m0.vy - part->m8_position.vy) >> 12;
                    s->m0.vz = (s->m0.vz - part->m8_position.vz) >> 12;
                    VectorNormal(&s->m0, &s->m10);
                    part->m18_velocity.vx = (s->m10.vx * e->m34_speed) >> 12;
                    part->m18_velocity.vy = (s->m10.vy * e->m34_speed) >> 12;
                    part->m18_velocity.vz = (s->m10.vz * e->m34_speed) >> 12;
                    flags &= 3;
                    part->m2_angle = ratan2(s->m10.vy, s->m10.vx);
                    part->m28_accel.vx = e->m38_accel.vx;
                    part->m28_accel.vy = e->m38_accel.vy;
                    part->m28_accel.vz = e->m38_accel.vz;
                    part->m40_color = e->m4C_color.m0_raw;
                    part->m44_colorStep = e->m50_colorStep;
                    *(s32*)&part->m38_scaleX = e->m44_scale;
                    *(s32*)&part->m3C_scaleXStep = e->m48_scaleStep;
                    part->m48_tpage = (flags << 5) | 0x9D;
                    e->mA_numParticles++;
                    goto store;
                }
                part++;
            }
        }
        goto store;
    toggle:
        e->m4C_color.m0_c.m3_flags ^= 0x80;
    store:
        e->m4_timer = (repeat << 16) | delay;
    }
    moveWorldmapParticles();
}

typedef struct sWorldmapParticleRenderScratch {
    /* 0x00 */ SVECTOR m0_quad[4];
    /* 0x20 */ SVECTOR m20_position;
    /* 0x28 */ MATRIX m28_view;
    /* 0x48 */ MATRIX m48_local;
    /* 0x68 */ MATRIX m68_identity;
    /* 0x88 */ VECTOR m88_position;
    /* 0x98 */ VECTOR m98_scale;
    /* 0xA8 */ s32 mA8;
    /* 0xAC */ s32 mAC_flag;
    /* 0xB0 */ s32 mB0_sz;
} sWorldmapParticleRenderScratch;

/* Render the second-stage particles as projected textured quads. The
 * matrix/table pointer reuse and buffer-pointer advance retain retail
 * allocation and stack layout. Address temporaries use MIPS32 pointers. */
#define RS ((sWorldmapParticleRenderScratch*)0x1F800000)
void drawWorlmapPolyFT4Array1(void) {
    s32 cx, cz;

    sWorldmapParticle2* p;
    POLY_FT4* poly;
    sWorldmapParticleRenderScratch* s;
    void* data;
    SVECTOR* vertexSource;
    void* uv;
    MATRIX* lm;
    VECTOR* pos;
    s32 i;
    uptr mask;
    uptr high;
    uptr bits;
    s32 scaleY;
    POLY_FT4** buffers;
    s32 index;
    void* out;
    u16* lastUV;
    SVECTOR* lastQuad;
    u16* uvSource;

    s = RS;
    data = &worldmapMainMatrix2;
    s->m28_view = *(MATRIX*)data;
    uv = &worldMapIdentityMatrix;
    s->m68_identity = *(MATRIX*)uv;
    i = 0;
    lm = (MATRIX*)0x1F800048;
    data = worldmapParticleQuadVertices;
    pos = (VECTOR*)0x1F800088;
    uv = worldmapParticleQuadUVs;
    buffers = worldmapFT4_1;
    lastQuad = D_8009B058;
    lastUV = D_8009AFF6;
    cx = worldmapRadarPosition3.vx >> 12;
    cz = worldmapRadarPosition3.vz >> 12;
    index = worldmapOddOrEven;
    buffers += index;
    poly = *buffers;
    p = worldmapParticleInstances;
    for (; i < 0x100; i++, p++) {
        if (p->m6_active == 0) {
            continue;
        }
        s->m98_scale.vx = p->m38_scaleX;
        scaleY = p->m3A_scaleY;
        s->m98_scale.vz = 0x1000;
        s->m98_scale.vy = scaleY;
        s->m48_local = s->m68_identity;
        if (((u8*)p)[0x47] & 1) {
            RotMatrixZ(p->m2_angle, lm);
        }
        ScaleMatrix(lm, &s->m98_scale);
        vertexSource = (SVECTOR*)data + 0;
        s->m0_quad[0] = vertexSource[p->m6_active * 4];
        vertexSource = (SVECTOR*)data + 1;
        s->m0_quad[1] = vertexSource[p->m6_active * 4];
        vertexSource = (SVECTOR*)data + 2;
        s->m0_quad[2] = vertexSource[p->m6_active * 4];
        bits = (uptr)lastQuad;
        s->m0_quad[3] = ((SVECTOR*)bits)[p->m6_active * 4];
        s->m88_position.vx = (p->m8_position.vx >> 12) - cx;
        s->m88_position.vz = (p->m8_position.vz >> 12) - cz;
        adjustWorldModelLocation(pos);
        s->m20_position.vx = s->m88_position.vx;
        s->m20_position.vy = p->m8_position.vy >> 12;
        s->m20_position.vz = -s->m88_position.vz;
        gte_SetRotMatrix(&s->m28_view);
        gte_ldv0(&s->m20_position);
        gte_rtv0();
        gte_stlvnl(pos);
        s->m48_local.t[0] = s->m88_position.vx + s->m28_view.t[0];
        s->m48_local.t[1] = s->m88_position.vy + s->m28_view.t[1];
        s->m48_local.t[2] = s->m88_position.vz + s->m28_view.t[2];
        mask = (uptr)lm;
        gte_SetRotMatrix((MATRIX*)mask);
        gte_SetTransMatrix(lm);
        gte_ldv3(&s->m0_quad[0], &s->m0_quad[1], &s->m0_quad[2]);
        gte_rtpt();
        out = &s->mAC_flag;
        gte_stflg(out);
        if (s->mAC_flag < 0) {
            continue;
        }
        mask = 0x80000000;
        gte_stsxy3(&poly->x0, &poly->x1, &poly->x2);
        gte_ldv0(&s->m0_quad[3]);
        gte_rtps();
        gte_stflg(out);
        if (s->mAC_flag & mask) {
            continue;
        }
        gte_stsxy(&poly->x3);
        pcInterpolationKeyType(p, p, 0, poly, PC_INTERP_PARTICLE);
        /* With the wide picture also past the right edge (the signed test
         * already keeps the left margin); those only the wide picture has
         * are marked below. The buffer has a quad per particle. */
        if ((poly->x0 < PC_WIDE_X1(0x140) || poly->x1 < PC_WIDE_X1(0x140) || poly->x2 < PC_WIDE_X1(0x140) ||
             poly->x3 < PC_WIDE_X1(0x140)) &&
            (poly->y0 < 0xD8 + pcVerticalCull || poly->y1 < 0xD8 + pcVerticalCull ||
             poly->y2 < 0xD8 + pcVerticalCull || poly->y3 < 0xD8 + pcVerticalCull)) {
            gte_stsz(&s->mB0_sz);
            if (s->mB0_sz < 0xC00) {
                poly->r0 = ((u8*)&p->m40_color)[0];
                poly->g0 = ((u8*)&p->m40_color)[1];
                poly->b0 = ((u8*)&p->m40_color)[2];
                poly->tpage = p->m48_tpage;
                uvSource = (u16*)uv + 0;
                *(u16*)&poly->u0 = uvSource[p->m6_active * 4];
                uvSource = (u16*)uv + 1;
                *(u16*)&poly->u1 = uvSource[p->m6_active * 4];
                uvSource = (u16*)uv + 2;
                *(u16*)&poly->u2 = uvSource[p->m6_active * 4];
                *(u16*)&poly->u3 = lastUV[p->m6_active * 4];
                mask = OT_LINK_MASK;
                high = poly->tag;
                out = pCurrentWorldmapRenderingStruct;
                bits = ~OT_LINK_MASK;
                high &= bits;
                high |= ((sWorldmapRenderingStruct*)out)->m70_OT[s->mB0_sz >> 4] & mask;
                poly->tag = high;
                mask &= (uptr)poly;
                ((sWorldmapRenderingStruct*)out)->m70_OT[s->mB0_sz >> 4] =
                    (((sWorldmapRenderingStruct*)out)->m70_OT[s->mB0_sz >> 4] & bits) | mask;
                if (!(poly->x0 < 0x140 || poly->x1 < 0x140 || poly->x2 < 0x140 || poly->x3 < 0x140) ||
                    !(poly->y0 < 0xD8 || poly->y1 < 0xD8 || poly->y2 < 0xD8 || poly->y3 < 0xD8)) {
                    pcWideOnly(poly);
                }
                poly++;
            }
        }
    }
}
#undef RS

/* Enhanced, the 60 Hz world map end to end (w_80070CFC.c) */
s32 pcWm60Rate(void);
void pcWm60Moved(void);
s32 pcWm60MovePending(void);
s32 pcWm60TakeMove(void);
void pcWm60Live(s32 index);
void pcWm60MidPosition(const VECTOR* a, const VECTOR* b, VECTOR* out);
void pcWm60NativeStart(s32 index);
sWorldmapStateEntry* pcWm60NativeEntry(s32 index);
void pcWm60Preview(s32 index);

s32 worldmapMode0_taskPlayer_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    sWorldmapFollowLeader* f;
    s32 i;
    s16 mode;

    p->m4C_sprite = &createSpriteActor(worldmapPartySprites[0], 0x100, 0x1E0, 0x140, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
    setGraphicEntityScale(p->m4C_sprite, 0x1800);
    p->m4C_sprite->m3C_x2 = 0;
    p->m24_hidden = 0;
    p->m28_position.vx = gameState.m1930_fieldVarsBackup[0] << 12;
    p->m28_position.vz = gameState.m1930_fieldVarsBackup[1] << 12;
    p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    p->m38_step.vz = 0;
    p->m38_step.vy = 0;
    p->m38_step.vx = 0;
    p->m48_direction = worldmapGamestate1824;
    p->m4A_stepScale = 8;
    p->m5C = p->m48_direction;
    switch (worldMapGearMode) {
    case 1:
    case 2:
    case 3:
        if (gameState.m22B1_isOnGear[0] == 0) {
            p->m28_position.vx = worldmapPosition.vx;
            p->m28_position.vz = worldmapPosition.vz;
            p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
            p->m48_direction = worldmapGamestate1824;
            worldmapRadarPosition = p->m28_position;
            worldmapPlayerDirection = p->m48_direction;
        } else {
            p->m20_subState = 1;
            p->m24_hidden = 1;
        }
        break;
    case 4:
    case 5:
    case 6:
        mode = 3;
        goto set_mode;
    case 7:
        mode = 2;
    set_mode:
        p->m20_subState = mode;
        p->m24_hidden = 1;
        if (gameState.m1D34_currentParty[0] != 0xFF) {
            gameState.m22B1_isOnGear[0] = 1;
        }
        break;
    case 13:
        break;
    }
    worldmapFollowLeaderDataIndex = 0;
    for (i = 0, f = worldmapFollowLeaderData; i < 32; i++) {
        f->m0_position = p->m28_position;
        f->m10_direction = p->m48_direction;
        f++;
    }
    gameState.m1820_worldmapPosition[0] = p->m28_position.vx >> 12;
    gameState.m1820_worldmapPosition[1] = p->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[2] = p->m48_direction;
    return 1;
}

s32 worldmapMode0_taskPlayer_reinit(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m4C_sprite = &createSpriteActor(worldmapPartySprites[0], 0x100, 0x1E0, 0x140, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
    setGraphicEntityScale(p->m4C_sprite, 0x1800);
    p->m4C_sprite->m3C_x2 = 0;
    return 1;
}

s32 worldmapMode0_taskPlayer_initSaved(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    sWorldmapFollowLeader* f;
    s32 i;

    p->m4C_sprite = &createSpriteActor(worldmapPartySprites[0], 0x100, 0x1E0, 0x140, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
    setGraphicEntityScale(p->m4C_sprite, 0x1800);
    p->m4C_sprite->m3C_x2 = 0;
    p->m28_position.vx = gameState.m1820_worldmapPosition[0] << 12;
    p->m28_position.vz = gameState.m1820_worldmapPosition[1] << 12;
    p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    p->m48_direction = gameState.m1820_worldmapPosition[2];
    p->m24_hidden = 1;
    p->m4A_stepScale = 8;
    p->m38_step.vz = 0;
    p->m38_step.vy = 0;
    p->m38_step.vx = 0;
    p->m20_subState = 2;
    worldmapFollowLeaderDataIndex = 0;
    for (i = 0, f = worldmapFollowLeaderData; i < 32; i++) {
        f->m0_position = p->m28_position;
        f->m10_direction = p->m48_direction;
        f++;
    }
    worldmapRadarPosition = p->m28_position;
    worldmapPlayerDirection = p->m48_direction;
    return 1;
}

s32 worldmapMode0_taskPlayer_update(s32 index) {
    s32 ret = 1;
    sWorldmapScratch* s = (sWorldmapScratch*)0x1F800000;
    sWorldmapStateEntry* ws = gWorldmapState->m0_entries;
    sWorldmapStateEntry* p = &ws[index];
    sWorldmapFollowLeader* f;
    s16 i;           /* loop counter, also the collision result */
    u16 dir;

    if (pcWm60OffTick()) {
        /* Enhanced, 60 Hz end to end (w_80070CFC.c): on the frame between only
         * the walk (and riding along) moves, from that vblank's input; requests,
         * sequences and counters are the tick's */
        if (p->m20_subState > 3) {
            goto pcWm60Done;
        }
    } else switch (p->m4_request) {
    case 2:
        p->m4_request = 0;
        p->m20_subState = 0x28;
        break;
    case 3:
        p->m4_request = 0;
        p->m20_subState = 1;
        break;
    case 6:
        p->m4_request = 0;
        p->m58++;
        if (worldmapNumActivePartyMembers == p->m58) {
            p->m20_subState = 0;
            worldMapGearMode = 1;
            currentWorldmapDynamicCollisionSlot = 0;
        }
        break;
    }
    switch (p->m20_subState) {
    case 0:
    case 1:
        /* Enhanced, 60 Hz: the tick runs the native frame whole from where the
         * tick before left the leader (the frame between's half step was a
         * preview from its own vblank's input) */
        pcWm60NativeStart(index);
        if (gameState.m22B1_isOnGear[0] == 0) {
            switch (worldmapUpdatePlayerControls(p)) {
            case 1:
                p->m20_subState = 0x40;
                continueWorldmapLoop = 0;
                exitWorldMapMode = 0;
                break;
            case 3:
                if (targetVehicleEntityIndex == 7) {
                    p->m20_subState = 8;
                } else {
                    targetVehicleEntityIndex = 4;
                    p->m20_subState = 0x10;
                }
                break;
            case 2:
            case 4:
            case 5:
                break;
            default:
                if (pcWm60OffTick()) {
                    /* the walk animation and the dust follow on the tick */
                } else if ((p->m38_step.vx == 0) & (p->m38_step.vy == 0) & (p->m38_step.vz == 0)) {
                    if ((s8)p->m4C_sprite->mAF_animationId != 0) {
                        spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
                        clearWorldmapParticles(0x2F);
                    }
                } else {
                    if ((s8)p->m4C_sprite->mAF_animationId != 1) {
                        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
                    }
                    spawnParticlesForTerrainType3(0x2F, p, s);
                }
                /* Enhanced, 60 Hz: the frame between checks the native step too and goes half of it */
                i = checkWorldmapPosition(&p->m28_position, &p->m38_step, &s->m90_checkResult, p->m4A_stepScale << 12, worldMapGearMode);
                if (i == 0) {
                    p->m38_step = s->m90_checkResult;
                    i = checkWorldmapPosition(&p->m28_position, &p->m38_step, &s->m90_checkResult, p->m4A_stepScale << 12, worldMapGearMode);
                    if (i == 0) {
                        p->m38_step.vz = 0;
                        p->m38_step.vx = 0;
                    }
                }
                if (i == 1) {
                    processWorldmapDynamicCollisions(&s->m90_checkResult, 0x10, 0x20, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                    if (targetVehicleEntityIndex == 7) {
                        i = adjustLocationAfterCollisionVar0 + 3;
                    } else {
                        i = adjustLocationAfterCollisionVar0;
                    }
                    if (D_8009B180[i] != 0) {
                        if (pcWm60OffTick()) {
                            pcWm60MidPosition(&p->m28_position, &s->m90_checkResult, &p->m28_position);
                        } else {
                            p->m28_position = s->m90_checkResult;
                        }
                        if ((p->m38_step.vx | p->m38_step.vz) != 0) {
                            if (pcWm60OffTick()) {
                                pcWm60Moved(); /* the follow trail and the encounter count: the tick's */
                            } else {
                                pcWm60TakeMove();
                                f = &worldmapFollowLeaderData[worldmapFollowLeaderDataIndex = (worldmapFollowLeaderDataIndex + 1) & 0x1F];
                                f->m0_position = p->m28_position;
                                f->m10_direction = p->m48_direction;
                                updateWorldmapEncounterTimers();
                            }
                        }
                    }
                } else {
                    processWorldmapDynamicCollisions(&p->m28_position, 0x10, 0x20, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                }
                setupWorldmapExits(&p->m28_position, 0);
                p->m38_step.vz = 0;
                p->m38_step.vy = 0;
                p->m38_step.vx = 0;
                worldmapRadarPosition = p->m28_position;
                worldmapPlayerDirection = p->m48_direction;
                pcWm60Live(index);
                pcWm60Preview(index); /* (the tick takes the step again whole) */
                break;
            }
            if (!pcWm60OffTick()) currentWorldmapDynamicCollisionSlot = 0; /* the volumes stay for the tick */
            p->m24_hidden = 0;
        } else {
            p->m24_hidden = 1;
            p->m28_position.vx = pcWm60NativeEntry(4)->m28_position.vx; /* (the Gear as the native frame finds it) */
            p->m28_position.vy = pcWm60NativeEntry(4)->m28_position.vy;
            p->m28_position.vz = pcWm60NativeEntry(4)->m28_position.vz;
            p->m48_direction = pcWm60NativeEntry(4)->m48_direction;
            if (!pcWm60OffTick()) clearWorldmapParticles(0x2F);
            pcWm60Live(index);
        }
        break;
    case 2:
    case 3:
        p->m28_position.vx = pcWm60NativeEntry(7)->m28_position.vx;
        p->m28_position.vy = pcWm60NativeEntry(7)->m28_position.vy;
        p->m28_position.vz = pcWm60NativeEntry(7)->m28_position.vz;
        p->m48_direction = pcWm60NativeEntry(7)->m48_direction;
        pcWm60Live(index);
        break;
    case 8:
        if (gameState.m1D34_currentParty[1] != 0xFF) {
            if (gameState.m22B1_isOnGear[1] == 0) {
                changeWorldmapEntityState(2, 1);
                p[1].m6_targetEntity = targetVehicleEntityIndex;
                changeWorldmapEntityState(5, 8);
            } else {
                changeWorldmapEntityState(5, 1);
                gWorldmapState->m0_entries[5].m6_targetEntity = targetVehicleEntityIndex;
            }
        }
        p->m20_subState++;
        break;
    case 9:
        if (gameState.m1D34_currentParty[2] != 0xFF) {
            if (gameState.m22B1_isOnGear[2] == 0) {
                changeWorldmapEntityState(3, 1);
                p[2].m6_targetEntity = targetVehicleEntityIndex;
                changeWorldmapEntityState(6, 8);
            } else {
                changeWorldmapEntityState(6, 1);
                gWorldmapState->m0_entries[6].m6_targetEntity = targetVehicleEntityIndex;
            }
        }
        p->m20_subState++;
        break;
    case 10:
        if (gameState.m1D34_currentParty[0] != 0xFF) {
            if (changeWorldmapEntityState(4, 8)) {
                p->m20_subState = 0xD;
            }
        } else {
            p->m20_subState = 0xD;
        }
        break;
    case 13:
        selectYggdrasilExitDirection(&p->m28_position, &gWorldmapState->m0_entries[targetVehicleEntityIndex].m28_position, &p->m38_step, &p->m48_direction);
        p->m50 = (gWorldmapState->m0_entries + targetVehicleEntityIndex)->m28_position.vx >> 12; /* pointer form, not m0[i]: keeps retail register order */
        p->m54 = (gWorldmapState->m0_entries + targetVehicleEntityIndex)->m28_position.vz >> 12;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
        p->m20_subState++;
    case 14:
        if (stepGearAwayFromYggdrasil(p) == 3) {
            p->m20_subState++;
        }
        worldmapRadarPosition = p->m28_position;
        worldmapPlayerDirection = p->m48_direction;
        spawnParticlesForTerrainType3(index + 0x2E, p, s);
        break;
    case 15:
        if (changeWorldmapEntityState(targetVehicleEntityIndex, 4)) {
            p->m24_hidden = 1;
            p->m20_subState = 2;
            clearWorldmapParticles(0x2F);
        }
        break;
    case 16:
        if (gameState.m1D34_currentParty[1] != 0xFF) {
            if (changeWorldmapEntityState(2, 1)) {
                p[1].m6_targetEntity = 5;
                p->m20_subState++;
            }
        } else {
            p->m20_subState++;
        }
        break;
    case 17:
        if (gameState.m1D34_currentParty[2] != 0xFF) {
            if (changeWorldmapEntityState(3, 1)) {
                p[2].m6_targetEntity = 6;
                p->m20_subState++;
            }
        } else {
            p->m20_subState++;
        }
        break;
    case 18:
        f = worldmapFollowLeaderData;
        worldmapFollowLeaderDataIndex = 0;
        s->m30.vx = gameState.m1930_fieldVarsBackup[0x16] << 12;
        s->m30.vz = gameState.m1930_fieldVarsBackup[0x17] << 12;
        s->m30.vy = worldmapGetAltitudeFor2dPoint(s->m30.vx, s->m30.vz);
        s->mA0.vx = gameState.m1820_worldmapPosition[3];
        for (i = 0x1F; i != -1; i--) {
            f->m0_position = s->m30;
            f->m10_direction = s->mA0.vx;
            f++;
        }
        p->m20_subState = 0xD;
        break;
    case 0x28:
        p->m28_position.vx = gameState.m1930_fieldVarsBackup[0x16] << 12;
        p->m28_position.vz = gameState.m1930_fieldVarsBackup[0x17] << 12;
        p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        dir = gameState.m1820_worldmapPosition[3];
        p->m5C = dir;
        p->m48_direction = dir;
        s->m0.vx = p->m28_position.vx + getAngleCos(p->m48_direction) * 0x30;
        s->m0.vz = p->m28_position.vz + -getAngleSin(p->m48_direction) * 0x30;
        selectYggdrasilExitDirection(&p->m28_position, &s->m0, &p->m38_step, &p->m48_direction);
        p->m50 = s->m0.vx >> 12;
        p->m54 = s->m0.vz >> 12;
        p->m24_hidden = 0;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
        p->m20_subState++;
        break;
    case 0x29:
        if (stepGearAwayFromYggdrasil(p) == 3) {
            spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
            p->m38_step.vz = 0;
            p->m38_step.vy = 0;
            p->m38_step.vx = 0;
            p->m20_subState++;
        }
        worldmapRadarPosition = p->m28_position;
        worldmapPlayerDirection = p->m48_direction;
        break;
    case 0x2A:
        f = worldmapFollowLeaderData;
        gameState.m22B1_isOnGear[0] = 0;
        p->m58 = 1;
        worldmapFollowLeaderDataIndex = 0;
        s->m0 = p->m28_position;
        s->mA0.vx = p->m48_direction;
        for (i = 0x1F; i != -1; i--) {
            f->m0_position = s->m0;
            f->m10_direction = s->mA0.vx;
            f++;
        }
        p->m20_subState++;
    case 0x2B:
        switch (worldmapNumActivePartyMembers) {
        case 1:
            p->m20_subState = 1;
            break;
        case 2:
            if (gameState.m22B1_isOnGear[1] == 0) {
                p->m20_subState++;
            }
            break;
        case 3:
            if ((gameState.m22B1_isOnGear[1] | gameState.m22B1_isOnGear[2]) == 0) {
                p->m20_subState++;
            }
            break;
        }
        break;
    case 0x2C:
        if (gameState.m1D34_currentParty[1] != 0xFF) {
            if (changeWorldmapEntityState(2, 5)) {
                p->m20_subState++;
            }
        } else {
            p->m20_subState++;
        }
        break;
    case 0x2D:
        if (gameState.m1D34_currentParty[2] == 0xFF || changeWorldmapEntityState(3, 5)) {
            p->m20_subState = 0x40;
        }
        break;
    case 0x40:
        break;
    }
pcWm60Done:
    gameState.m1820_worldmapPosition[0] = p->m28_position.vx >> 12;
    gameState.m1820_worldmapPosition[1] = p->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[2] = p->m48_direction;
    if (p->m24_hidden == 0) {
        addWorldmapTrackMark(0, &p->m28_position);
    }
    return ret;
}

s32 worldmapMode0_task_partyMember1_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 ret = 1;
    s16 mode;

    if (gameState.m1D34_currentParty[1] != 0xFF) {
        p->m4C_sprite = &createSpriteActor(worldmapPartySprites[1], 0x110, 0x1E0, 0x150, 0x100, 0x40)->m0_spriteActorCore;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
        setGraphicEntityScale(p->m4C_sprite, 0x1800);
        p->m4C_sprite->m3C_x2 = 0;
        p->m24_hidden = 0;
    } else {
        p->m24_hidden = 1;
        ret = 3;
    }
    p->m28_position.vx = gameState.m1820_worldmapPosition[0] << 12;
    p->m28_position.vz = gameState.m1820_worldmapPosition[1] << 12;
    p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    p->m38_step.vz = 0;
    p->m38_step.vy = 0;
    p->m38_step.vx = 0;
    p->m48_direction = gameState.m1820_worldmapPosition[2];
    p->m4A_stepScale = 8;
    p->m58 = 0xF;
    p->m5C = p->m48_direction;
    switch (worldMapGearMode) {
    case 1:
    case 2:
    case 3:
        if (gameState.m22B1_isOnGear[1] == 0) {
            p->m28_position.vx = worldmapPosition.vx;
            p->m28_position.vz = worldmapPosition.vz;
            p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
            p->m48_direction = worldmapGamestate1824;
        } else {
            p->m20_subState = 1;
            p->m24_hidden = 1;
        }
        return ret;
    case 4:
    case 5:
    case 6:
        mode = 3;
        break;
    case 7:
        mode = 2;
        break;
    case 13:
    default:
        return ret;
    }
    p->m20_subState = mode;
    p->m24_hidden = 1;
    if (gameState.m1D34_currentParty[1] != 0xFF) {
        gameState.m22B1_isOnGear[1] = 1;
    }
    return ret;
}

s32 worldmapMode0_task_partyMember1_reinit(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 ret = 1;

    if (gameState.m1D34_currentParty[1] != 0xFF) {
        p->m4C_sprite = &createSpriteActor(worldmapPartySprites[1], 0x110, 0x1E0, 0x150, 0x100, 0x40)->m0_spriteActorCore;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
        setGraphicEntityScale(p->m4C_sprite, 0x1800);
        p->m4C_sprite->m3C_x2 = 0;
    } else {
        ret = 3;
    }
    return ret;
}

s32 worldmapMode0_task_partyMember1_initSaved(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m4C_sprite = &createSpriteActor(worldmapPartySprites[1], 0x110, 0x1E0, 0x150, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
    setGraphicEntityScale(p->m4C_sprite, 0x1800);
    p->m4C_sprite->m3C_x2 = 0;
    p->m28_position.vx = gameState.m1820_worldmapPosition[0] << 12;
    p->m28_position.vz = gameState.m1820_worldmapPosition[1] << 12;
    p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    p->m48_direction = gameState.m1820_worldmapPosition[2];
    p->m24_hidden = 1;
    p->m4A_stepScale = 8;
    p->m20_subState = 2;
    p->m38_step.vz = 0;
    p->m38_step.vy = 0;
    p->m38_step.vx = 0;
    p->m58 = 0xF;
    return 1;
}

s32 worldmapMode0_task_partyMember_update(s32 index) {
    s32 ret = 1;
    VECTOR* tmp = (VECTOR*)0x1F800000;
    sWorldmapStateEntry* ws = gWorldmapState->m0_entries;
    sWorldmapStateEntry* p = &ws[index];
    sWorldmapStateEntry* parent;
    sWorldmapFollowLeader* f;
    s16 angle;
    u16 dir;

    if (pcWm60OffTick()) {
        /* Enhanced, 60 Hz end to end (w_80070CFC.c): following (and riding)
         * every frame; requests and sequences on the tick */
        if (p->m20_subState > 2) {
            goto pcWm60Done;
        }
        if (p->m20_subState <= 1 && gameState.m22B1_isOnGear[index - 1] == 0) {
            /* the trail grows once a native frame: halfway to its next point
             * while the leader is moving */
            f = &worldmapFollowLeaderData[(worldmapFollowLeaderDataIndex - p->m58) & 0x1F];
            if (pcWm60MovePending()) {
                pcWm60MidPosition(&f->m0_position, &worldmapFollowLeaderData[(worldmapFollowLeaderDataIndex - p->m58 + 1) & 0x1F].m0_position,
                                  &p->m28_position);
            } else {
                p->m28_position = f->m0_position;
            }
            p->m48_direction = f->m10_direction;
            p->m24_hidden = 0;
            pcWm60Live(index);
            pcWm60Preview(index);
            goto pcWm60Done;
        }
    } else switch (p->m4_request) {
    case 1:
        p->m4_request = 0;
        p->m20_subState = 8;
        parent = &ws[p->m6_targetEntity];
        break;
    case 2:
        p->m4_request = 0;
        p->m20_subState = 0x28;
        break;
    case 3:
        p->m4_request = 0;
        p->m20_subState = 1;
        break;
    case 5:
        p->m4_request = 0;
        p->m20_subState = 0x30;
        break;
    }
    switch (p->m20_subState) {
    case 0:
    case 1:
        if (gameState.m22B1_isOnGear[index - 1] == 0) {
            f = &worldmapFollowLeaderData[(worldmapFollowLeaderDataIndex - p->m58) & 0x1F];
            pcWm60NativeStart(index); /* (compared and checked where the native frame started) */
            if ((p->m28_position.vx == f->m0_position.vx) & (p->m28_position.vy == f->m0_position.vy) & (p->m28_position.vz == f->m0_position.vz)) {
                if ((s8)p->m4C_sprite->mAF_animationId != 0) {
                    spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
                    clearWorldmapParticles(index + 0x2E);
                }
            } else {
                if ((s8)p->m4C_sprite->mAF_animationId != 1) {
                    spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
                }
                spawnParticlesForTerrainType3(index + 0x2E, p, (sWorldmapScratch*)tmp);
            }
            pcWm60NativeStart(index);
            p->m28_position.vx = f->m0_position.vx;
            p->m28_position.vy = f->m0_position.vy;
            p->m28_position.vz = f->m0_position.vz;
            dir = f->m10_direction;
            p->m24_hidden = 0;
            p->m48_direction = dir;
        } else {
            parent = pcWm60NativeEntry(index + 3); /* (the Gear as the native frame finds it) */
            p->m28_position.vx = parent->m28_position.vx;
            p->m28_position.vy = parent->m28_position.vy;
            p->m28_position.vz = parent->m28_position.vz;
            angle = parent->m48_direction;
            p->m24_hidden = 1;
            p->m48_direction = angle;
            if (!pcWm60OffTick()) clearWorldmapParticles(index + 0x2E);
            pcWm60Live(index);
        }
        break;
    case 2:
        p->m28_position.vx = pcWm60NativeEntry(7)->m28_position.vx;
        p->m28_position.vy = pcWm60NativeEntry(7)->m28_position.vy;
        p->m28_position.vz = pcWm60NativeEntry(7)->m28_position.vz;
        p->m48_direction = pcWm60NativeEntry(7)->m48_direction;
        pcWm60Live(index);
        break;
    case 8:
        selectYggdrasilExitDirection(&p->m28_position, &parent->m28_position, &p->m38_step, &p->m48_direction);
        p->m50 = parent->m28_position.vx >> 12;
        p->m54 = parent->m28_position.vz >> 12;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
        p->m20_subState++;
    case 9:
        if (stepGearAwayFromYggdrasil(p) == 3) {
            p->m20_subState++;
        }
        spawnParticlesForTerrainType3(index + 0x2E, p, (sWorldmapScratch*)tmp);
        break;
    case 10:
        if (changeWorldmapEntityState(p->m6_targetEntity, 4)) {
            p->m24_hidden = 1;
            p->m20_subState = 2;
            clearWorldmapParticles(index + 0x2E);
        }
        break;
    case 0x28:
        p->m28_position.vx = gameState.m1930_fieldVarsBackup[0x13 + index * 3] << 12;
        p->m28_position.vz = gameState.m1930_fieldVarsBackup[0x14 + index * 3] << 12;
        p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
        dir = gameState.m1820_worldmapPosition[2 + index];
        p->m5C = dir;
        p->m48_direction = dir;
        tmp->vx = p->m28_position.vx + getAngleCos(p->m48_direction) * 0x30;
        tmp->vz = p->m28_position.vz + -getAngleSin(p->m48_direction) * 0x30;
        selectYggdrasilExitDirection(&p->m28_position, tmp, &p->m38_step, &p->m48_direction);
        p->m50 = tmp->vx >> 12;
        p->m54 = tmp->vz >> 12;
        p->m24_hidden = 0;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
        p->m20_subState++;
    case 0x29:
        if (stepGearAwayFromYggdrasil(p) == 3) {
            p->m20_subState++;
        }
        break;
    case 0x2A:
        gameState.m22B1_isOnGear[index - 1] = 0;
        p->m20_subState = 0x40;
        break;
    case 0x30:
        selectYggdrasilExitDirection(&p->m28_position, &gWorldmapState->m0_entries[1].m28_position, &p->m38_step, &p->m48_direction);
        p->m50 = gWorldmapState->m0_entries[1].m28_position.vx >> 12;
        p->m54 = gWorldmapState->m0_entries[1].m28_position.vz >> 12;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 1);
        p->m20_subState++;
    case 0x31:
        if (stepGearAwayFromYggdrasil(p) == 3) {
            p->m20_subState++;
        }
        break;
    case 0x32:
        if (changeWorldmapEntityState(1, 6)) {
            p->m20_subState = 0;
        }
        break;
    case 0x40:
        break;
    }
pcWm60Done:
    if (p->m24_hidden == 0) {
        addWorldmapTrackMark(0, &p->m28_position);
    }
    return ret;
}

s32 worldmapMode0_task_partyMember2_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 ret = 1;
    s16 mode;

    if (gameState.m1D34_currentParty[2] != 0xFF) {
        p->m4C_sprite = &createSpriteActor(worldmapPartySprites[2], 0x120, 0x1E0, 0x160, 0x100, 0x40)->m0_spriteActorCore;
        spriteActorSetPlayingAnimation(p->m4C_sprite, 0);
        setGraphicEntityScale(p->m4C_sprite, 0x1800);
        p->m4C_sprite->m3C_x2 = 0;
        p->m24_hidden = 0;
    } else {
        p->m24_hidden = 1;
        ret = 3;
    }
    p->m28_position.vx = gameState.m1820_worldmapPosition[0] << 12;
    p->m28_position.vz = gameState.m1820_worldmapPosition[1] << 12;
    p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
    p->m38_step.vz = 0;
    p->m38_step.vy = 0;
    p->m38_step.vx = 0;
    p->m48_direction = gameState.m1820_worldmapPosition[2];
    p->m4A_stepScale = 8;
    p->m58 = 0x1E;
    p->m5C = p->m48_direction;
    switch (worldMapGearMode) {
    case 1:
    case 2:
    case 3:
        if (gameState.m22B1_isOnGear[2] == 0) {
            p->m28_position.vx = worldmapPosition.vx;
            p->m28_position.vz = worldmapPosition.vz;
            p->m28_position.vy = worldmapGetAltitudeFor2dPoint(p->m28_position.vx, p->m28_position.vz);
            p->m48_direction = worldmapGamestate1824;
        } else {
            p->m20_subState = 1;
            p->m24_hidden = 1;
        }
        return ret;
    case 4:
    case 5:
    case 6:
        mode = 3;
        break;
    case 7:
        mode = 2;
        break;
    case 13:
    default:
        return ret;
    }
    p->m20_subState = mode;
    p->m24_hidden = 1;
    if (gameState.m1D34_currentParty[2] != 0xFF) {
        gameState.m22B1_isOnGear[2] = 1;
    }
    return ret;
}
