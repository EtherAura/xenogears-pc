/*
 * World map entry point, main loop, mode 0/8 init, update and free, file loading
 * and relocation (Noah: worldmap.cpp, worldmapTaskGround.cpp).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sound.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"
#include "pc_gameplay.h"
#include "pc_interpolation.h"
#include "pc_camera.h"
#include "pc_speedup.h"

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern sWorldmapRenderingStruct worldmapRenderingStructs[2] PORT_ALIAS;
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern LoadingBatchCommand worldmapLoadingCommands[16];
extern u8 worldMapFarColor[3];
extern s32 worldmapMinimapScale;
extern s32 exitWorldMapMode;
extern s32 worldMapGearMode;
extern VECTOR worldmapPosition;
extern u32 worldmapGamestate1824;
extern s16* worldmapFile1Buffer_4_0;                     /* Noah: worldmapFile1Buffer_4_0 (initial positions) */

/* initWorldMap */
extern u16 scenarioStateToWorldmapConfigsMapping[10];                    /* Noah: scenarioStateToWorldmapConfigsMapping (from index 1) */
extern sWorldmapConfig worldmapConfigs[20] PORT_ALIAS;  /* Noah: worldmapConfigs */
extern s32 initWorldMapVar0;
extern s32 worldmapFile1;
extern s32 worldmapFile2;
extern s32 worldmapFile3;
extern s32 worldmapFile4;
extern s32 worldmapFile5;
extern s32 worldmapFile6;
extern s32 worldmapFile7;
extern s32 currentBattleMusicId;                      /* Noah: currentBattleMusicId (file 8) */
extern s32 worldmapFile9;
extern s32 worldmapFile10;
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;

/* file buffers */
extern AnimationBundle* worldmapPartySprites[3];
extern AnimationBundle* worldmapPartyGearSprites[3];
extern s32 worldmapNumActivePartyMembers;
extern void* worldmapFile2Buffer;
extern void* worldmapFile3Buffer;
extern sWorldmapFile1Header* worldmapFile1Buffer;
extern void* menuSharedResources;
extern void* worldmapMenuOverlayFile;
extern s32 worldmapSoundFilesPendingCount;
extern void* worldmapWds;
extern void* worldmapSound1;
extern sWorldmapSeqHeader* fieldSeq;
extern void* worldmapSound2;
extern void* worldmapSound3;

/* finalizeWorldMapFileLoading */
extern s32* worldmapFile1Buffer_C; /* collision mesh offsets */
extern Model* worldmapModelFiles;
extern sWorldmapOffsetEntry* worldmapFile1Buffer_14; /* 0x100 entries, relocated by initWorldmapBillboards */
extern u16* pWorldmapModelsConfig;
extern u8* worldmapFile1Buffer_1C;
extern sWorldmapParticle* worldmapFile1Buffer_18; /* particle emitters (0x200) */
extern s32* worldmapFile1Buffer_24; /* animated texture set 2: count, then offsets */
extern s32* worldmapFile1Buffer_20; /* animated texture set 1: count, then offsets */
extern sWorldmapEncounterTable* worldmapFile1Buffer_2C_data[16]; /* encounter tables */
extern sWorldmapExitDef** worldmapExitsDefs; /* exit tables (4) */

/* worldmap_taskGround_update */
extern s32 worldmapMatrixMode;
extern sWorldmapCameraState worldmapCameraVector;
extern VECTOR worldmapGridInputPosition;
extern VECTOR worldmapRadarPosition3;
extern s16 worldmapGridUpdateMask;
extern s32 worldmapWaterWave;

void setFogNearColor(u8 r, u8 g, u8 b);
void SetFarColor(s32 r, s32 g, s32 b);
void SetFogNearFar(s32 a, s32 b, s32 h);
void getGamestate182C(VECTOR* v);
void setupWorldmapMatrix1(SVECTOR* eye);
void setupWorldmapMatrix2(sWorldmapCameraState* cam);
void updateWorldmapParticleEmitters(void);
void drawWorlmapPolyFT4Array1(void);
void worldmapDrawSpriteActors(void);
void drawWorldmapBillboards(void);
void drawWorldmapTrackMarks(void);
void renderWorldmapModels(void);
void setWorldmapGridUpdateMask(VECTOR* position);
void updateWorldmapGrids(VECTOR*);
void streamWorldmap0(void);
void streamWorldmap1(void);
void worldmapGroundPrepareRenderingTable(VECTOR*);
void drawWorldmapGround(OTag*, POLY_FT3*, VECTOR*);
void drawWorldmapSkyCloudBand(void);
void drawWorldmapSkyGradient(void);
void drawWorldmapClouds(void);
void worldmapRenderMinimap(void);
void stopAllSounds(void);
void loadSequence(void*);
void unloadSequence(void*);
void deleteFieldEntitySub4(void* actor);
void backupWorldmapStateForBattle(void);
void freeWorldmapExitDialog(void);
void freeWorldmapInfoDialog(void);
void freeWorldmapModels(void);
void freeWorldmapPolyFT4Array3(void);
void freeShapeTransfert(void);
void freeWorldmapClouds(void);
void freeWorldmapPolyFT4Array2(void);
void freeWorldmapTrackMarks(void);
void freeWorldmapTexAnims1(void);
void freeWorldmapTexAnims2(void);
void freeWorldmapParticles(void);
void freeWorldmapPolyFT4Array1(void);
void freeWorldmapChunks(void);
void freeWorldmapState(void);
void freeWorldmapData(void);
extern u8 battleMusic[];                     /* Noah: battleMusic (sequence buffer) */
extern MATRIX D_8009BE4C;
extern MATRIX worldMapIdentityMatrix;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapExitVar2;
extern s32 worldmapGearToggleHeld;
extern s32 worldmapGearTogglePrevHeld;
extern s32 worldmapGearTogglePressed;
extern s32 fieldDrawEnvsInitialized;
extern s32 worldmapFadeRunning;
extern void (*worldmapCloudsMoveCallback)(void); /* clouds callback */
extern s32 isWorldmapModeFlagSet;
extern s32 worldmapInitialPositionIndex;
extern u16 musicStatusFlag;
extern u8 gearSwitchDisabled;
/* 15 tasks and the NULL end; splat's D_80099E90 (= &[0].m4_update) is inside. */
extern sWorldTaskDefinition worldmapMode0_tasks[16];
extern sWorldTaskDefinition* worldmapConfigExtraTasks[];  /* extra tasks per world map config */
void worldmapFadeIn(s32 count, s32 start, s32 step, s32 abr);
void initWorldMap(s32 worldmapMode, s32 scenarioFlag);
void setupWorldmapGearMode(void);
void allocateWorldMapsOT(void);
void worldmapMode8_init(void);
void finalizeWorldMapFileLoading(void);
void allocAndInitWorldmapState(void);
void moveWorldmapClouds(void);
void worldmapDotInit(void);
void resetBattleAudioState(void);
void stopAllMusicInstances(void);
void setupWorldmapPositionFromField(void);
void setupWorldmapPositionFromFileData(s32);
void restoreWorldmapStateFromBattle(void);
void applyWorldmapGearBoardingChanges(void);
void loadWorldmapTextureFromFile3(void);
void loadWorldmapTextureFromFile2(void);
void worldmapInitAllModels(void);
void startLoadingWorldmapSoundFiles(void);
void initWorldmapSkyGradient(void);
void initWorldmapMinimap(void);
void initWorldmapBillboards(void);
void initWorldmapTrackMarks(void);
void initWorldmapClouds(void);
void initWorldmapTexAnims1(void);
void initWorldmapTexAnims2(void);
void initWorldmapSkyCloudBand(void);
void initWorldmapParticles(void);
void setupInitialGrid(VECTOR*);
s32 loadWorldmapChunk(void);
s32 isWorldStreamingDone(void);
void reloadWorldmapInitialGrid(VECTOR*);
void waitWorldmapStreamingDone(void);
void* createMusicInstance(void* seq);
void startMusicInstance(void* inst, s32 volume, s32 arg2);
void resumeMusicInstance(void* inst, s32 vol, s32 arg2);
void setupWorldmapTask(s32 (*init)(s32), s32 (*update)(s32));
void setWorldmapTaskInit(s32 (*init)(s32), s32 slot);
s32 worldmap_taskFader_init(s32);
s32 worldmapMode0_taskPlayer_reinit(s32);
s32 worldmapMode0_task_partyMember1_reinit(s32);
s32 worldmapMode0_task_partyMember2_reinit(s32);
s32 worldmapMode0_taskPlayerGear_reinit(s32);
s32 worldmapMode0_task_partyGear1_reinit(s32);
s32 worldmapMode0_task_partyGear2_reinit(s32);
s32 worldmap_taskYggdrasil_reinit(s32);
s32 worldmap_taskYggdrasilSubModels_reinit(s32 index);
s32 worldmap_taskExit_init(s32);
s32 worldmap_taskInfoDialog_init(s32);
s32 worldmap_taskWaypointShip_reinit(s32);
s32 worldmap_taskFlyingShip_reinit(s32);
s32 worldmap_taskTranslucentSpin_reinit(s32);
s32 worldmap_taskEventModel_reinit(s32);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void createWorlmapPolyFT3Array(void);
void createWorlmapPolyFT4Array1(void);
void createWorlmapPolyFT4Array2(void);
void createWorlmapPolyFT4Array3(void);
void initWorldmapEncounterTimers(void);
void initFontPalettes(s16 x, s16 y);
extern void vsyncCallback(void);               /* VSync callback (main) */
extern u8 isWorldMapOverlayLoaded;
extern u8 battleInitVar1;
extern s32 currentWorldmapMode;
extern s32 worldmapExitToFieldVar0;
extern s32 fieldBootModeFromWorldmap;
extern u16 worldmapWaypointShipX[8];
extern u16 worldmapWaypointShipZ[8];
extern sWorldmapModes worldmapModes[19];
extern sWorldmapExitDef* worldmapCurrentExit;
extern SVECTOR worldmapRotation;
extern VECTOR worldmapRadarPosition;
void worldmapFlushCacheAndSync(void);
void allocateWorldmapData(void);
void callAllWorlmapObjects(void);
void resetInputs(void);
void worldmapMainLoop(void);
void loadGameModeOverlay(s32 mode);
void setGameMode(s32 mode);
s32 findWorldmapExitOfType(VECTOR* position, s32 a1, s32 a2);
void bootGame(s32);
extern s32 worldmapOddOrEven;
extern s32 continueWorldmapLoop;
extern u16 worldmapInput1_0;
extern u16 worldmapInput1_1;
extern u16 worldmapInput2_0;
extern u16 worldmapInput2_1;
extern u16 worldmapInput3_0;
extern u16 worldmapInput3_1;
extern u16 newPadButtonForScripts;                      /* Noah: newPadButtonForScripts[0].m0_buttons */
extern u16 newPadButtonForScripts2;                      /* Noah: newPadButtonForScripts[1].m0_buttons */
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForDialogs2;
extern u16 newPadButtonForField;
extern u16 newPadButtonForField2;
extern u8 worldmapStreamingCDSyncStatus[8];
extern s16 worldmapExitVar0;
extern s16 worldmapExitVar1;
extern s32 worldmapEncounterTriggered;
extern s32 worldmapGeometryOffsetY;
extern u8 battleTransitionEffect;
extern u8 menuToEnter;
extern u8 useDebugMenuList;
extern u8 g_menuOpenArg;                       /* Noah: menuOpenArg */
extern sWorldmapExitDef worldmapExitToField290_yggdrasilDeck;
s32 loadInputFromVSyncBuffer(void);
s32 CdSync(s32 mode, u8* result);
void clearShapeTransfertTableEntry(s32);
void uploadCharacterSprites(void);
void checkSoftReboot(void);
s16 getWorldmapGroundType(VECTOR* position);
s32 isControllerConnected(s32);
void runWorldmapPause(void);
void waitWorldmapControllerReconnect(void);
s32 rollWorldmapRandomEncounter(VECTOR* pos, s32 flag);
void worldmapPrepareEnterMenu(void);
void enterMenu(void);
void worldmapRestoreAfterMenu(void);
void shapeTransfert(void);
void updateWorldmapTexAnims1(void);
void updateWorldmapTexAnims2(void);
void initWorldmapGraphics(void);

void worldmapEntryPoint(void) {
    RECT rect;
    s32 i;
    void* buf;

#ifdef PLATFORM_PC
    /* a stackless load: the frame loop goes on where its state was saved (include/pc_state.h) */
    if (PC_SAFE_MODE(pcResumeAt()) == PC_SAFE_MODE(PC_SAFE_WORLDMAP)) {
        goto resumeFrames;
    }
#endif
    worldmapFlushCacheAndSync();
    DrawSync(0);
    VSync(0);
    VSyncCallback(vsyncCallback);
    InitGeom();
#ifdef PLATFORM_PC
    pcInterpolationScene(); /* nothing of the field before carries over */
#endif
    isWorldMapOverlayLoaded = 1;
    if (gameState.m2320_worldmapMode == 0) {
        gameState.m231A_fieldID = 0x400;
        gameState.m231E_worldmapInitialPositionIndex = 0xFFF;
        gameState.m231C_cameraYaw = 0xC00;
        gameState.m2320_worldmapMode = 1;
        gameState.m1834_vehicleMode = 0x4003;
        gameState.m182C_vehiclePosition[0] = 0x6680;
        gameState.m1836_restoreWorldmapPosition = 1;
        gameState.m182C_vehiclePosition[1] = -0x100;
        gameState.m182C_vehiclePosition[2] = 0x2A00;
        gameState.m182C_vehiclePosition[3] = 0;
        gameState.m1820_worldmapPosition[0] = 0x7580;
        gameState.m1820_worldmapPosition[1] = 0x2C00;
        gameState.m1D34_currentParty[1] = 10;
        gameState.m1820_worldmapPosition[2] = 0;
        gameState.m1D34_currentParty[2] = 5;
        gameState.m26C_party[0].mA0_gearNum = 0xF;
        gameState.m26C_party[4].mA0_gearNum = 5;
        gameState.m26C_party[1].mA0_gearNum = 2;
        gameState.m26C_party[2].mA0_gearNum = 3;
        gameState.m26C_party[3].mA0_gearNum = 4;
        gameState.m1D34_currentParty[0] = 0;
        gameState.m22B1_isOnGear[0] = 0;
        gameState.m26C_party[5].mA0_gearNum = 6;
        gameState.m22B1_isOnGear[1] = 0;
        gameState.m22B1_isOnGear[2] = 0;
        gameState.m26C_party[6].mA0_gearNum = 9;
        gameState.m26C_party[7].mA0_gearNum = 7;
        gameState.m26C_party[9].mA0_gearNum = 3;
        gameState.m26C_party[10].mA0_gearNum = 9;
        gameState.m26C_party[8].mA0_gearNum = 8;
        gameState.m1930_fieldVarsBackup[21] = 0x400;
        gameState.m1930_fieldVarsBackup[22] = 0x7500;
        gameState.m1930_fieldVarsBackup[23] = 0x2E58;
        gameState.m1930_fieldVarsBackup[24] = 0x400;
        gameState.m1930_fieldVarsBackup[25] = 0x7580;
        gameState.m1930_fieldVarsBackup[26] = 0x2E58;
        gameState.m1930_fieldVarsBackup[27] = 0x400;
        gameState.m1930_fieldVarsBackup[28] = 0x7600;
        gameState.m1930_fieldVarsBackup[29] = 0x2E58;
        gameState.m1848_waypointShipWaypoint = 1;
        gameState.m1844_waypointShipPosition[0] = worldmapWaypointShipX[gameState.m1844_waypointShipPosition[1]];
        *(u32*)&gameState.m1930_fieldVarsBackup[254] = 0x7FFFFFF;
        gameState.m1844_waypointShipPosition[1] = worldmapWaypointShipZ[gameState.m1844_waypointShipPosition[1]];
    }
#ifdef PLATFORM_PC
    {
        /* XG_WM_* start options (port/src/game/worldmap/start.c) */
        void portWorldmapStart(void);
        portWorldmapStart();
    }
#endif
    resetMemoryAllocStats(3, 0);
    setCurrentDirectory(0x24, 0);
    allocateWorldmapData();
    allocateWorldMapsOT();
    setupWorldmapGearMode();
    if (gameState.m2320_worldmapMode & 0x8000) {
        isWorldmapModeFlagSet = 1;
    } else {
        isWorldmapModeFlagSet = 0;
    }
    worldmapExitToFieldVar0 = 0;
    fieldBootModeFromWorldmap = (gameState.m231A_fieldID & 0x3FFF) - 0x400;
    worldmapInitialPositionIndex = (u16)gameState.m231E_worldmapInitialPositionIndex;
    worldmapGamestate1824 = gameState.m231C_cameraYaw;
    currentWorldmapMode = gameState.m2320_worldmapMode & 0x7FFF;
    gameState.m2320_worldmapMode = currentWorldmapMode;
    initWorldMap(currentWorldmapMode, gameState.m1930_fieldVarsBackup[0]);
    if (worldmapModes[currentWorldmapMode].m0_init != NULL) {
        worldmapModes[currentWorldmapMode].m0_init();
    }
    while (exitWorldMapMode >= 2) {
        worldmapModes[currentWorldmapMode].m4_update();
        callAllWorlmapObjects();
        DrawSync(0);
        VSync(0);
        resetInputs();
        isWorldmapModeFlagSet = exitWorldMapMode;
#ifdef PLATFORM_PC
    resumeFrames:
#endif
        worldmapMainLoop();
        worldmapModes[currentWorldmapMode].m8_free();
    }
    switch (exitWorldMapMode) {
    case 0:
        loadGameModeOverlay(1);
        setGameMode(1);
        if (worldmapExitToFieldVar0 == 0) {
            if (worldmapCurrentExit->mE_type == 3) {
                findWorldmapExitOfType(&worldmapRadarPosition, 3, gameState.m1930_fieldVarsBackup[0]);
            }
            gameState.m231A_fieldID = worldmapCurrentExit->m8_destinationField;
            gameState.m231C_cameraYaw = worldmapRotation.vy;
            gameState.m2320_worldmapMode = worldmapCurrentExit->mA_returnWorldmapMode;
        }
        gameState.m1930_fieldVarsBackup[2] = fieldBootModeFromWorldmap + 0x400;
        break;
    case 1:
        loadGameModeOverlay(2);
        setGameMode(2);
        battleInitVar1 = 0;
        for (i = 0; i < 3; i++) {
            gameState.m183C_isOnGearBackup[i] = gameState.m22B1_isOnGear[i];
        }
        stopAllMusicInstances();
        buf = worldmapSound3;
        memcpy(battleMusic, buf, getFileSizeAligned(currentBattleMusicId));
        savedMusicInstance = pMusic;
        pMusic = createMusicInstance(battleMusic);
        startMusicInstance(pMusic, 0x7F, 0);
        break;
    default:
        setGameMode(0);
        rect.w = 0x13F;
        rect.x = 0;
        rect.y = 0;
        rect.h = 0x1AF;
        ClearImage(&rect, 0, 0, 0x40);
        DrawSync(0);
        break;
    }
    isWorldMapOverlayLoaded = 0;
    worldmapFlushCacheAndSync();
    bootGame(0);
}

/* worldmapMainLoop (US and JP: the waitWorldmapControllerReconnect block is #if !VERSION_JP) */
void worldmapMainLoop(void) {
    RECT rect;
    s32 i;
    sWorldmapRenderingStruct* rs;

#ifdef PLATFORM_PC
    if (pcResumeAt() == PC_SAFE_WORLDMAP) {
        pcResumeDone(); /* every frame's state is the module's: nothing to take back */
        goto frameTop;
    }
    if (PC_SAFE_MODE(pcResumeAt()) == PC_SAFE_MODE(PC_SAFE_WORLDMAP) && pcResumeMenu()) {
        goto worldmapMenu; /* a menu loop's safe point: the frame's menu call goes on */
    }
#endif
    pCurrentWorldmapRenderingStruct = &worldmapRenderingStructs[1];
    worldmapOddOrEven = 1;
    continueWorldmapLoop = 1;
    do {
#ifdef PLATFORM_PC
    frameTop:
        pcSafePoint(PC_SAFE_WORLDMAP); /* between frames: a canonical state's place */
#endif
        worldmapInput3_1 = 0;
        worldmapInput2_1 = 0;
        worldmapInput1_1 = 0;
        worldmapInput3_0 = 0;
        worldmapInput2_0 = 0;
        worldmapInput1_0 = 0;
        while (loadInputFromVSyncBuffer()) {
            worldmapInput1_0 |= newPadButtonForScripts;
            worldmapInput1_1 |= newPadButtonForScripts2;
            worldmapInput2_0 |= newPadButtonForDialogs;
            worldmapInput2_1 |= newPadButtonForDialogs2;
            worldmapInput3_0 |= newPadButtonForField;
            worldmapInput3_1 |= newPadButtonForField2;
        }
        while (loadWorldmapChunk() == 3) {
            VSync(0);
        }
        CdSync(1, worldmapStreamingCDSyncStatus);
        rs = &worldmapRenderingStructs[0];
        if (pCurrentWorldmapRenderingStruct == rs) {
            rs++;
        }
        pCurrentWorldmapRenderingStruct = rs;
        worldmapOddOrEven = worldmapOddOrEven == 0;
        ClearOTagR(rs->m70_OT, 0x400);
#ifdef PLATFORM_PC
        /* Smooth motion (port/src/gpu/interpolation.c): the frame's pictures
         * from here to its DrawOTag, the buffer drawn its area. */
        pcInterpolationFrameBegin(PC_INTERP_WORLDMAP, rs->m0_DrawEnv.clip.x, rs->m0_DrawEnv.clip.y,
                                  rs->m0_DrawEnv.clip.w, rs->m0_DrawEnv.clip.h);
#endif
        clearShapeTransfertTableEntry(worldmapOddOrEven);
        uploadCharacterSprites();
        callAllWorlmapObjects();
        DrawSync(0);
        VSync(2);
        checkSoftReboot();
        PutDispEnv(&pCurrentWorldmapRenderingStruct->m5C_DispEnv);
        PutDrawEnv(&pCurrentWorldmapRenderingStruct->m0_DrawEnv);
        if (gearSwitchDisabled == 0 && worldmapGearTogglePressed != 0 && worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && worldmapExitVar0 == -1 &&
            worldmapExitVar1 == worldmapExitVar0 && continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0) {
            worldmapGearTogglePressed = 0;
            if (getWorldmapGroundType(&worldmapRadarPosition) != 4) {
                {
                    s32 j;
                    for (j = 0; j < 3; j++) {
                        gameState.m183C_isOnGearBackup[j] = gameState.m22B1_isOnGear[j];
                    }
                }
                if (gameState.m22B1_isOnGear[0] != 0) {
                    gameState.m22B1_isOnGear[2] = 0;
                    gameState.m22B1_isOnGear[1] = 0;
                    gameState.m22B1_isOnGear[0] = 0;
                } else {
                    if (gameState.m26C_party[gameState.m1D34_currentParty[0]].mA0_gearNum != 0xFF) {
                        gameState.m22B1_isOnGear[0] = 1;
                    }
                    if (gameState.m26C_party[gameState.m1D34_currentParty[1]].mA0_gearNum != 0xFF) {
                        gameState.m22B1_isOnGear[1] = 1;
                    }
                    if (gameState.m26C_party[gameState.m1D34_currentParty[2]].mA0_gearNum != 0xFF) {
                        gameState.m22B1_isOnGear[2] = 1;
                    }
                }
                applyWorldmapGearBoardingChanges();
            }
        } else {
            worldmapGearTogglePressed = 0;
        }
        if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0 && (worldmapInput2_0 & 0x800)) {
            runWorldmapPause();
        }
#if !VERSION_JP
        if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0 && isControllerConnected(0) == 0) {
            waitWorldmapControllerReconnect();
        }
#endif
        if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && worldmapExitVar0 == -1 && worldmapExitVar1 == worldmapExitVar0 &&
            continueWorldmapLoop != 0 && worldmapEncounterTriggered != 0) {
            i = rollWorldmapRandomEncounter(&worldmapRadarPosition, gameState.m1930_fieldVarsBackup[0]);
            if (i == 1) {
                continueWorldmapLoop = 0;
                exitWorldMapMode = i;
                battleTransitionEffect = 0;
                gameState.m183C_isOnGearBackup[0] = gameState.m22B1_isOnGear[0];
                gameState.m183C_isOnGearBackup[1] = gameState.m22B1_isOnGear[1];
                gameState.m183C_isOnGearBackup[2] = gameState.m22B1_isOnGear[2];
            }
        }
#ifdef PLATFORM_PC
        /* A gauntlet's first fight (F1 > Gameplay > Gauntlet), where an
         * encounter could start, on foot or in Gears: as an encounter does
         * (port/src/game/main/gauntlet.c puts the fight's configs in). */
        if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && worldmapExitVar0 == -1 && worldmapExitVar1 == worldmapExitVar0 &&
            continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0 && gameState.m1834_vehicleMode == 0 &&
            pcGauntletState() == 1 && pcGauntletStartBattle()) {
            continueWorldmapLoop = 0;
            exitWorldMapMode = 1;
            battleTransitionEffect = 0;
            gameState.m183C_isOnGearBackup[0] = gameState.m22B1_isOnGear[0];
            gameState.m183C_isOnGearBackup[1] = gameState.m22B1_isOnGear[1];
            gameState.m183C_isOnGearBackup[2] = gameState.m22B1_isOnGear[2];
        }
#endif
        worldmapEncounterTriggered = 0;
        if (worldmapInput2_0 & 0x100) {
            gameState.m1842_disableWorldmapMinimap ^= 1;
        }
        if (worldmapFadeRunning == 0 && worldmapExitVar2 != 0 && continueWorldmapLoop != 0) {
            if (worldMapGearMode > 0) {
                if (worldMapGearMode < 4) {
#ifdef PLATFORM_PC
                    pcTransitionMenu(1); /* Speedup transitions: the main menu's way in */
#endif
                    worldmapPrepareEnterMenu();
                    menuToEnter = 0;
                    useDebugMenuList = 0;
                    g_menuOpenArg = 1;
                    worldmapFlushCacheAndSync();
#ifdef PLATFORM_PC
                worldmapMenu:
                    {
                        /* The menu's own kind for the wide picture (include/pc_wide.h). */
                        s32 wideKind = pcWideSetMode(PC_WIDE_MENU);
                        pcMenuHost(PC_SAFE_MODE(PC_SAFE_WORLDMAP));
                        enterMenu();
                        pcMenuHost(-1);
                        pcWideSetMode(wideKind);
                    }
#else
                    enterMenu();
#endif
                    worldmapFlushCacheAndSync();
                    worldmapRestoreAfterMenu();
#ifdef PLATFORM_PC
                    pcTransitionMenu(0); /* the world map has the screen back */
#endif
                } else if (worldMapGearMode < 8) {
                    continueWorldmapLoop = 0;
                    exitWorldMapMode = 0;
                    worldmapCurrentExit = &worldmapExitToField290_yggdrasilDeck;
                    gameState.m1834_vehicleMode |= 0x2000;
                }
            }
        } else {
            worldmapExitVar2 = 0;
        }
        shapeTransfert();
        updateWorldmapTexAnims1();
        updateWorldmapTexAnims2();
        SetGeomOffset(0xA0, worldmapGeometryOffsetY);
        DrawOTag(&pCurrentWorldmapRenderingStruct->m70_OT[0x3FF]);
#ifdef PLATFORM_PC
        pcInterpolationFrameEnd();
#endif
    } while (continueWorldmapLoop != 0);
    ResetGraph(1);
    if (worldmapOddOrEven == 0) {
        rect.x = 0;
        rect.y = 0xD8;
        rect.w = 0x140;
        rect.h = 0xD8;
        MoveImage(&rect, 0, 0);
    }
    waitWorldmapStreamingDone();
    DrawSync(0);
    VSync(0);
    PutDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv);
}

s32 worldmap_taskGround_init(s32 index) {
    return 1;
}

s32 worldmap_taskGround_update(s32 index) {
    SVECTOR unused; /* retail frame keeps 8 unused bytes */

    if (worldmapMatrixMode == 0) {
#ifdef PLATFORM_PC
        /* the free camera's tilt (freecam.c): the view matrix from its pitch, the game's kept */
        s16 pitch = worldmapRotation.vx;
        worldmapRotation.vx = pcWorldCameraPitch(pitch);
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
        worldmapRotation.vx = pitch;
#else
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
#endif
    } else {
        setupWorldmapMatrix2(&worldmapCameraVector);
    }
    updateWorldmapParticleEmitters();
    drawWorlmapPolyFT4Array1();
    worldmapDrawSpriteActors();
    drawWorldmapBillboards();
    drawWorldmapTrackMarks();
    renderWorldmapModels();
    setWorldmapGridUpdateMask(&worldmapGridInputPosition);
    if (worldmapGridUpdateMask != 0) {
        updateWorldmapGrids(&worldmapRadarPosition3);
        streamWorldmap0();
        streamWorldmap1();
    }
    worldmapGroundPrepareRenderingTable(&worldmapRadarPosition3);
    drawWorldmapGround(pCurrentWorldmapRenderingStruct->m70_OT, pCurrentWorldmapRenderingStruct->m74_polyFT3, &worldmapRadarPosition3);
    worldmapWaterWave += 0x40;
    drawWorldmapSkyCloudBand();
    drawWorldmapSkyGradient();
    drawWorldmapClouds();
    if (gameState.m1842_disableWorldmapMinimap == 0) {
        worldmapRenderMinimap();
    }
    return 1;
}

void initWorldMap(s32 worldmapMode, s32 scenarioFlag) {
    sWorldmapConfig* pConfig;
    s32 i;

    if (worldmapMode < 8) {
        i = 1;
        while (scenarioStateToWorldmapConfigsMapping[i] <= scenarioFlag) {
            i++;
        }
        pConfig = &worldmapConfigs[i - 1];
        initWorldMapVar0 = i - 1;
    } else {
        pConfig = &worldmapConfigs[worldmapMode + 1];
    }
    worldmapFile1 = pConfig->m0_baseFileId + 1;
    worldmapFile2 = pConfig->m0_baseFileId + 2;
    worldmapFile3 = pConfig->m0_baseFileId + 3;
    worldmapFile4 = pConfig->m0_baseFileId + 4;
    worldmapFile5 = pConfig->m0_baseFileId + 5;
    worldmapFile6 = pConfig->m0_baseFileId + 6;
    worldmapFile7 = pConfig->m0_baseFileId + 7;
    currentBattleMusicId = pConfig->m0_baseFileId + 8;
    worldmapFile9 = pConfig->m0_baseFileId + 9;
    worldmapFile10 = pConfig->m0_baseFileId + 10;
    worldmapSizeX = pConfig->m2_sizeX;
    worldmapSizeY = pConfig->m4_sizeZ;
    exitWorldMapMode = pConfig->m6_exitMode;
}

void worldmapMode0_init(void) {
    s32 i;
    s32 j;
    s32 member;
    u8 gear;

    for (i = 0; i < 3; i++) {
        member = gameState.m1D34_currentParty[i];
        if (member != 0xFF) {
            worldmapPartySprites[i] = allocateHeap(getFileSizeAligned(member + 2), 0);
            j = gameState.m26C_party[member].mA0_gearNum;
            if (j != 0xFF) {
                worldmapPartyGearSprites[i] = allocateHeap(getFileSizeAligned(j + 0x13), 0);
            } else {
                worldmapPartyGearSprites[i] = NULL;
            }
        } else {
            worldmapPartyGearSprites[i] = NULL;
            worldmapPartySprites[i] = NULL;
        }
    }
    i = 0;
    worldmapNumActivePartyMembers = 0;
    for (j = 0; j < 3; j++) {
        member = gameState.m1D34_currentParty[j];
        if (member != 0xFF) {
            worldmapLoadingCommands[i].m0_fileIndex = member + 2;
            worldmapLoadingCommands[i].m4_loadPtr = worldmapPartySprites[j];
            i++;
            worldmapNumActivePartyMembers++;
            gear = gameState.m26C_party[member].mA0_gearNum;
            if (gear != 0xFF) {
                worldmapLoadingCommands[i].m0_fileIndex = gear + 0x13;
                worldmapLoadingCommands[i].m4_loadPtr = worldmapPartyGearSprites[j];
                i++;
            }
        }
    }
    worldmapLoadingCommands[i].m0_fileIndex = 0;
    worldmapLoadingCommands[i].m4_loadPtr = NULL;
    batchStartLoadingFiles(worldmapLoadingCommands, 0, 0);
}

void worldmapMode8_init(void) {
    worldmapFile2Buffer = allocateHeap(getFileSizeAligned(worldmapFile2), 1);
    worldmapFile3Buffer = allocateHeap(getFileSizeAligned(worldmapFile3), 1);
    worldmapFile1Buffer = allocateHeap(getFileSizeAligned(worldmapFile1), 1);
    worldmapLoadingCommands[0].m0_fileIndex = worldmapFile1;
    worldmapLoadingCommands[1].m0_fileIndex = worldmapFile2;
    worldmapLoadingCommands[2].m0_fileIndex = worldmapFile3;
    worldmapLoadingCommands[3].m0_fileIndex = 0;
    worldmapLoadingCommands[0].m4_loadPtr = worldmapFile1Buffer;
    worldmapLoadingCommands[1].m4_loadPtr = worldmapFile2Buffer;
    worldmapLoadingCommands[2].m4_loadPtr = worldmapFile3Buffer;
    worldmapLoadingCommands[3].m4_loadPtr = NULL;
    batchStartLoadingFiles(worldmapLoadingCommands, 0, 0);
}

/* loads files 0x25/0x26 */
void startLoadingWorldmapMenuFiles(void) {
    s32 i;

    menuSharedResources = allocateHeap(getFileSizeAligned(0x26), 1);
    worldmapMenuOverlayFile = allocateHeap(getFileSizeAligned(0x25), 1);
    i = 0;
    worldmapLoadingCommands[i].m0_fileIndex = 0x25;
    worldmapLoadingCommands[i].m4_loadPtr = worldmapMenuOverlayFile;
    worldmapLoadingCommands[i + 1].m0_fileIndex = 0x26;
    worldmapLoadingCommands[i + 2].m0_fileIndex = 0;
    worldmapLoadingCommands[i + 2].m4_loadPtr = NULL;
    worldmapLoadingCommands[i + 1].m4_loadPtr = menuSharedResources;
    batchStartLoadingFiles(worldmapLoadingCommands, 0, 0);
}

void startLoadingWorldmapSoundFiles(void) {
    s32 i;

    worldmapSoundFilesPendingCount++;
    i = 0;
    worldmapLoadingCommands[i].m0_fileIndex = worldmapFile4;
    worldmapLoadingCommands[i].m4_loadPtr = worldmapWds = allocateHeap(getFileSizeAligned(worldmapFile4), 1);
    i++;
    worldmapLoadingCommands[i].m0_fileIndex = worldmapFile5;
    worldmapLoadingCommands[i].m4_loadPtr = worldmapSound1 = allocateHeap(getFileSizeAligned(worldmapFile5), 0);
    i++;
    worldmapLoadingCommands[i].m0_fileIndex = worldmapFile6;
    worldmapLoadingCommands[i].m4_loadPtr = fieldSeq = allocateHeap(getFileSizeAligned(worldmapFile6), 0);
    i++;
    worldmapLoadingCommands[i].m0_fileIndex = worldmapFile7;
    worldmapLoadingCommands[i].m4_loadPtr = worldmapSound2 = allocateHeap(getFileSizeAligned(worldmapFile7), 0);
    i++;
    worldmapLoadingCommands[i].m0_fileIndex = currentBattleMusicId;
    worldmapLoadingCommands[i].m4_loadPtr = worldmapSound3 = allocateHeap(getFileSizeAligned(currentBattleMusicId), 0);
    i++;
    worldmapLoadingCommands[i].m0_fileIndex = 0;
    worldmapLoadingCommands[i].m4_loadPtr = NULL;
    batchStartLoadingFiles(worldmapLoadingCommands, 0, 0);
}

/* reads worldmapFile6 into fieldSeq */
void readWorldmapSequenceFile(void) {
    fieldSeq = allocateHeap(getFileSizeAligned(worldmapFile6), 0);
    readFile(worldmapFile6, fieldSeq, 0, 0);
}

/* worldmapMode0_update (global init store order found by search; buf local + memcpy in both branches) */
void worldmapMode0_update(void) {
    RECT rect;
    sWorldTaskDefinition* task;
    s32 i;
    s32 file;
    s32 size;
    void* buf;

    initWorldmapGraphics();
    rect.w = 0x140;
    rect.x = 0;
    rect.y = 0;
    rect.h = 0xD8;
    MoveImage(&rect, 0x2C0, 0x100);
    DrawSync(0);
    worldmapFadeIn(0x40, 0, 4, 2);
    waitReadCompletion(0);
    worldmapMode8_init();
    while (isCDBusy() > 2) {
        PORT_YIELD();
    }
    finalizeWorldMapFileLoading();
    allocAndInitWorldmapState();
    D_8009BE4C = worldMapIdentityMatrix;
    worldmapExitVar2 = 0;
    worldmapGearToggleHeld = 0;
    worldmapGearTogglePrevHeld = 0;
    worldmapGearTogglePressed = 0;
    worldmapMatrixMode = 0;
    fieldDrawEnvsInitialized = 1;
    worldmapFadePolyAbr = 2;
    worldmapFadePolyStep = 4;
    worldmapFadeRunning = 1;
    worldmapCloudsMoveCallback = moveWorldmapClouds;
    worldmapDotInit();
    if (isWorldmapModeFlagSet == 0) {
        resetBattleAudioState();
    } else {
        stopAllMusicInstances();
        freeSoundInstance(pMusic);
        pMusic = savedMusicInstance;
        savedMusicInstance = NULL;
    }
    if (gameState.m1836_restoreWorldmapPosition != 0) {
        setupWorldmapPositionFromField();
    } else if (isWorldmapModeFlagSet == 0) {
        setupWorldmapPositionFromFileData(worldmapInitialPositionIndex);
    } else {
        restoreWorldmapStateFromBattle();
        applyWorldmapGearBoardingChanges();
    }
    waitReadCompletion(0);
    loadWorldmapTextureFromFile3();
    loadWorldmapTextureFromFile2();
    worldmapInitAllModels();
    startLoadingWorldmapSoundFiles();
    initWorldmapSkyGradient();
    initWorldmapMinimap();
    initWorldmapBillboards();
    allocateShapeTransfert(0x1400, 0);
    initWorldmapTrackMarks();
    initWorldmapClouds();
    initWorldmapTexAnims1();
    initWorldmapTexAnims2();
    initWorldmapSkyCloudBand();
    initWorldmapParticles();
    waitReadCompletion(0);
    if (isWorldmapModeFlagSet == 0) {
        worldmapWdsResult = loadWds(worldmapWds, 0);
    }
    setCurrentDirectory(0x24, 0);
    if (isWorldmapModeFlagSet == 0) {
        setupInitialGrid(&worldmapPosition);
        do {
            loadWorldmapChunk();
            VSync(0);
        } while (isWorldStreamingDone() > 1);
    } else {
        reloadWorldmapInitialGrid(&worldmapRadarPosition3);
        waitWorldmapStreamingDone();
    }
    if (isWorldmapModeFlagSet == 0) {
        while (musicStatusFlag & 0x10) {
            PORT_YIELD(); /* until the SPU transfer interrupt clears it (the wds upload) */
        }
        freeHeap(worldmapWds);
        loadSequence(fieldSeq);
        if (worldMapGearMode == 7) {
            buf = worldmapSound2;
            memcpy(battleMusic, buf, getFileSizeAligned(worldmapFile7));
        } else {
            buf = worldmapSound1;
            memcpy(battleMusic, buf, getFileSizeAligned(worldmapFile5));
        }
        pMusic = createMusicInstance(battleMusic);
        startMusicInstance(pMusic, 0x7F, 0);
    } else {
        freeHeap(worldmapWds);
        loadSequence(fieldSeq);
        if (worldMapGearMode == 7) {
            buf = worldmapSound2;
            memcpy(battleMusic, buf, getFileSizeAligned(worldmapFile7));
        } else {
            buf = worldmapSound1;
            memcpy(battleMusic, buf, getFileSizeAligned(worldmapFile5));
        }
        resumeMusicInstance(pMusic, 0x7F, 0xF0);
    }
    switch (isWorldmapModeFlagSet) {
    case 0:
        for (i = 0; worldmapMode0_tasks[i].m0_init != NULL; i++) {
            setupWorldmapTask(worldmapMode0_tasks[i].m0_init, worldmapMode0_tasks[i].m4_update);
        }
        task = worldmapConfigExtraTasks[initWorldMapVar0];
        for (i = 0; task[i].m0_init != NULL; i++) {
            setupWorldmapTask(task[i].m0_init, task[i].m4_update);
        }
        break;
    case 1:
        setWorldmapTaskInit(worldmap_taskFader_init, 0);
        setWorldmapTaskInit(worldmapMode0_taskPlayer_reinit, 1);
        setWorldmapTaskInit(worldmapMode0_task_partyMember1_reinit, 2);
        setWorldmapTaskInit(worldmapMode0_task_partyMember2_reinit, 3);
        setWorldmapTaskInit(worldmapMode0_taskPlayerGear_reinit, 4);
        setWorldmapTaskInit(worldmapMode0_task_partyGear1_reinit, 5);
        setWorldmapTaskInit(worldmapMode0_task_partyGear2_reinit, 6);
        setWorldmapTaskInit(worldmap_taskYggdrasil_reinit, 7);
        setWorldmapTaskInit(worldmap_taskYggdrasilSubModels_reinit, 8);
        setWorldmapTaskInit(worldmap_taskExit_init, 0xC);
        setWorldmapTaskInit(worldmap_taskInfoDialog_init, 0xD);
        setWorldmapTaskInit(worldmap_taskGround_init, 0xE);
        switch (initWorldMapVar0) {
        case 3:
            setWorldmapTaskInit(worldmap_taskWaypointShip_reinit, 0xF);
            setWorldmapTaskInit(worldmap_taskFlyingShip_reinit, 0x10);
            break;
        case 4:
            setWorldmapTaskInit(worldmap_taskWaypointShip_reinit, 0xF);
            setWorldmapTaskInit(worldmap_taskFlyingShip_reinit, 0x10);
            setWorldmapTaskInit(worldmap_taskTranslucentSpin_reinit, 0x11);
            setWorldmapTaskInit(worldmap_taskEventModel_reinit, 0x13);
            break;
        case 5:
        case 6:
        case 7:
            setWorldmapTaskInit(worldmap_taskWaypointShip_reinit, 0xF);
            setWorldmapTaskInit(worldmap_taskFlyingShip_reinit, 0x10);
        case 8:
            setWorldmapTaskInit(worldmap_taskTranslucentSpin_reinit, 0x11);
            break;
        }
        break;
    }
    gearSwitchDisabled = 0;
    if (initWorldMapVar0 == 0) {
        spawnWorldmapParticles(0xE, NULL, NULL);
        gearSwitchDisabled = 1;
    }
    createWorlmapPolyFT3Array();
    createWorlmapPolyFT4Array1();
    createWorlmapPolyFT4Array2();
    createWorlmapPolyFT4Array3();
    if (isWorldmapModeFlagSet == 0) {
        initWorldmapEncounterTimers();
    }
    initFontPalettes(0x130, 0x1E0);
}

/* worldmapMode0_free (entry-pointer cast in the task loop) */
void worldmapMode0_free(void) {
    s32 i;

    if (exitWorldMapMode == 0) {
        playBattleMusic(pMusic, 0, 0xF0);
    }
    stopAllSounds();
    unloadSequence(fieldSeq);
    freeHeap(fieldSeq);
    for (i = 0; i < 0x40; i++) {
        /* entry-pointer view: retail adds (i << 7) + base here, base + (i << 7) for ->m0[i] */
        if (((sWorldmapStateEntry*)gWorldmapState)[i].m4C_sprite != NULL) {
            deleteFieldEntitySub4(((sWorldmapStateEntry*)gWorldmapState)[i].m4C_sprite);
            ((sWorldmapStateEntry*)gWorldmapState)[i].m4C_sprite = NULL;
        }
    }
    if (exitWorldMapMode == 1) {
        backupWorldmapStateForBattle();
        gameState.m2320_worldmapMode |= 0x8000;
    }
    freeWorldmapExitDialog();
    freeWorldmapInfoDialog();
    freeWorldmapModels();
    freeWorldmapPolyFT4Array3();
    freeShapeTransfert();
    freeWorldmapClouds();
    freeWorldmapPolyFT4Array2();
    freeWorldmapTrackMarks();
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
    for (i = 0; i < 3; i++) {
        if (worldmapPartySprites[i] != NULL) {
            freeHeap(worldmapPartySprites[i]);
        }
        if (worldmapPartyGearSprites[i] != NULL) {
            freeHeap(worldmapPartyGearSprites[i]);
        }
    }
    freeWorldmapState();
    freeWorldmapData();
}

void initWorldmapGraphics(void) {
    s32 a;

    ResetGraph(1);
    worldmapMinimapScale = 0x100;
    SetGeomScreen(0x100);
    SetDefDrawEnv(&worldmapRenderingStructs[0].m0_DrawEnv, 0, 0, 0x140, 0xD8);
    SetDefDrawEnv(&worldmapRenderingStructs[1].m0_DrawEnv, 0, 0xD8, 0x140, 0xD8);
    SetDefDispEnv(&worldmapRenderingStructs[0].m5C_DispEnv, 0, 0xD8, 0x140, 0xD8);
    SetDefDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv, 0, 0, 0x140, 0xD8);
    worldmapRenderingStructs[1].m0_DrawEnv.isbg = 1;
    worldmapRenderingStructs[0].m0_DrawEnv.isbg = 1;
    worldmapRenderingStructs[1].m0_DrawEnv.dtd = 1;
    worldmapRenderingStructs[0].m0_DrawEnv.dtd = 1;
    if (exitWorldMapMode == 2) {
        setRGB0(&worldmapRenderingStructs[0].m0_DrawEnv, 0, 0, 0);
        setRGB0(&worldmapRenderingStructs[1].m0_DrawEnv, 0, 0, 0);
    } else {
        setRGB0(&worldmapRenderingStructs[0].m0_DrawEnv, 0, 0, 0x70);
        setRGB0(&worldmapRenderingStructs[1].m0_DrawEnv, 0, 0, 0x70);
    }
    worldmapRenderingStructs[1].m5C_DispEnv.screen.y = 10;
    worldmapRenderingStructs[0].m5C_DispEnv.screen.y = 10;
    worldmapRenderingStructs[1].m5C_DispEnv.screen.w = 0x100;
    worldmapRenderingStructs[0].m5C_DispEnv.screen.w = 0x100;
    worldmapRenderingStructs[1].m5C_DispEnv.screen.x = 0;
    worldmapRenderingStructs[0].m5C_DispEnv.screen.x = 0;
    worldmapRenderingStructs[1].m5C_DispEnv.screen.h = 0xD8;
    worldmapRenderingStructs[0].m5C_DispEnv.screen.h = 0xD8;
    setFogNearColor(0x80, 0x80, 0x80);
    SetBackColor(0x80, 0x80, 0x80);
    SetFarColor(worldMapFarColor[0], worldMapFarColor[1], worldMapFarColor[2]);
    a = 0x800;
    if (exitWorldMapMode == 2) {
        a = 0xB00;
    }
    SetFogNearFar(a, 0xE80, worldmapMinimapScale);
}

#ifdef PLATFORM_PC
/*
 * The wide picture of worldmapFadeIn (include/pc_wide.h): its strips show
 * the screen captured at (0x2C0, 0x100). When that capture has a wide copy
 * with this frame's margins, the strips are drawn from it and pieces beside
 * them show its margins (the fade overlay reaches them by itself); without
 * one (the screen before was 4:3) the frame stays 4:3.
 */
static POLY_FT4 worldmapFadeWidePieces[2][9];

static void worldmapFadeWide(OTag* ot, POLY_FT4* ft4, s32 buf) {
    POLY_FT4* p = worldmapFadeWidePieces[buf];
    s32 m = pcWideView;
    s32 side;
    s32 end;
    s32 x0;
    s32 x1;
    s32 i;
    s32 top;
    s32 bottom;

    if (!pcWideCaptureLines(0x2C0, 0x100, &top, &bottom) ||
        (m == 0 && top == 0 && bottom == 0) || pcWideCaptureMargin(0x2C0, 0x100) != m) {
        return;
    }
    for (i = 0; i < 3; i++) {
        pcWidePrim(&ft4[i], 0, PC_WIDE_HANDLED);
    }
    for (side = 0; side < 2; side++) {
        end = side == 0 ? 0 : 0x140 + m;
        for (x0 = side == 0 ? -m : 0x140; x0 < end; x0 = x1, p++) {
            x1 = x0 + 0x80 < end ? x0 + 0x80 : end;
            *p = ft4[0];
            p->x0 = p->x2 = x0;
            p->x1 = p->x3 = x1;
            p->u0 = p->u2 = 0;
            p->u1 = p->u3 = x1 - x0;
            p->y0 = p->y1 = -top;
            p->v0 = p->v1 = 0;
            p->v2 = p->v3 = ft4[0].v2 + top;
            addPrim(ot, p);
            /* Texel u of the piece is the capture's column x0 + u. */
            pcWidePrimTex2D(p, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, x0, -top);
        }
    }
    for (i = 0; top > 0 && i < 3; i++, p++) {
        *p = ft4[i];
        p->y0 = p->y1 = -top;
        p->y2 = p->y3 = 0;
        p->v0 = p->v1 = 0;
        p->v2 = p->v3 = top;
        addPrim(ot, p);
        pcWidePrimTex2D(p, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, 0, -top);
    }
}
#endif

/* worldmapFadeIn (FT4 xy store order found by search; count loop is while (--count != -1)) */
void worldmapFadeIn(s32 count, s32 start, s32 step, s32 abr) {
    POLY_FT4* ft4;
    POLY_G4* g4;
    DR_TPAGE* tpage;
    sWorldmapRenderingStruct* rs;
    sWorldmapRenderingStruct* next;
    s32 i;
    s32 odd;

    ft4 = allocateHeap(PC_HEAP_OF(POLY_FT4, sizeof(POLY_FT4) * 3), 1);
    g4 = allocateHeap(PC_HEAP_OF(POLY_G4, sizeof(POLY_G4) * 2), 1);
    tpage = allocateHeap(PC_HEAP_OF(DR_TPAGE, sizeof(DR_TPAGE)), 1);
    for (i = 0; i < 3; i++) {
        setlen(&ft4[i], 9);
        setcode(&ft4[i], 0x2C);
        ft4[i].r0 = 0x80;
        ft4[i].g0 = 0x80;
        ft4[i].b0 = 0x80;
        ft4[i].code |= 1;
    }
    ft4[0].x0 = 0;
    ft4[0].y0 = 0;
    ft4[0].y1 = 0;
    ft4[0].x2 = 0;
    ft4[0].x1 = 0x80;
    ft4[0].y2 = 0xEF;
    ft4[0].x3 = 0x80;
    ft4[1].x1 = 0x100;
    ft4[1].x0 = 0x80;
    ft4[0].y3 = 0xEF;
    ft4[1].y0 = 0;
    ft4[1].y1 = 0;
    ft4[1].y2 = 0xEF;
    ft4[1].x2 = 0x80;
    ft4[1].x3 = 0x100;
    ft4[1].y3 = 0xEF;
    ft4[2].x0 = 0x100;
    ft4[2].x2 = 0x100;
    ft4[2].y0 = 0;
    ft4[2].x1 = 0x140;
    ft4[2].y1 = 0;
    ft4[2].y2 = 0xEF;
    ft4[2].x3 = 0x140;
    ft4[2].y3 = 0xEF;
    ft4[0].u0 = 0;
    ft4[0].v0 = 0;
    ft4[0].u1 = 0x80;
    ft4[0].v1 = 0;
    ft4[0].u2 = 0;
    ft4[0].v2 = 0xEF;
    ft4[0].u3 = 0x80;
    ft4[0].v3 = 0xEF;
    ft4[1].u0 = 0;
    ft4[1].v0 = 0;
    ft4[1].u1 = 0x80;
    ft4[1].u3 = 0x80;
    ft4[1].v1 = 0;
    ft4[1].u2 = 0;
    ft4[1].v2 = 0xEF;
    ft4[1].v3 = 0xEF;
    ft4[2].u0 = 0;
    ft4[2].v0 = 0;
    ft4[2].u1 = 0x40;
    ft4[2].v1 = 0;
    ft4[2].u2 = 0;
    ft4[2].v2 = 0xEF;
    ft4[2].u3 = 0x40;
    ft4[2].v3 = 0xEF;
    ft4[0].tpage = GetTPage(2, 0, 0x2C0, 0x100);
    ft4[1].tpage = GetTPage(2, 0, 0x340, 0x100);
    ft4[2].tpage = GetTPage(2, 0, 0x3C0, 0x100);
    SetDrawTPage(tpage, 0, 1, GetTPage(0, abr, 0, 0));
    setlen(&g4[0], 8);
    setcode(&g4[0], 0x38);
    g4[0].x0 = 0;
    g4[0].y0 = 0;
    g4[0].x1 = 0x140;
    g4[0].y1 = 0;
    g4[0].x2 = 0;
    g4[0].y2 = 0xF0;
    g4[0].x3 = 0x140;
    g4[0].y3 = 0xF0;
    setRGB0(&g4[0], 0, 0, 0);
    setRGB1(&g4[0], 0, 0, 0);
    setRGB2(&g4[0], 0, 0, 0);
    setRGB3(&g4[0], 0, 0, 0);
    SetSemiTrans(&g4[0], 1);
    g4[0].code |= 1;
    g4[1] = g4[0];
    DrawSync(0);
    VSync(0);
    PutDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv);
    PutDrawEnv(&worldmapRenderingStructs[1].m0_DrawEnv);
    rs = &worldmapRenderingStructs[0];
    odd = 0;
    i = start;
    while (--count != -1) {
        next = &worldmapRenderingStructs[0];
        if (rs == next) {
            next = rs + 1;
        }
        rs = next;
        ClearOTagR(rs->m70_OT, 0x400);
        addPrim(rs->m70_OT + 1, &ft4[0]);
        addPrim(rs->m70_OT + 1, &ft4[1]);
        addPrim(rs->m70_OT + 1, &ft4[2]);
#ifdef PLATFORM_PC
        worldmapFadeWide(rs->m70_OT + 1, ft4, odd);
#endif
        odd ^= 1;
        setRGB0(&g4[odd], i, i, i);
        setRGB1(&g4[odd], i, i, i);
        setRGB2(&g4[odd], i, i, i);
        setRGB3(&g4[odd], i, i, i);
        addPrim(rs->m70_OT, &g4[odd]);
        addPrim(rs->m70_OT, tpage);
        DrawSync(0);
        VSync(0);
        PutDispEnv(&rs->m5C_DispEnv);
        PutDrawEnv(&rs->m0_DrawEnv);
        i += step;
        DrawOTag(rs->m70_OT + 0x3FF);
    }
    DrawSync(0);
    VSync(0);
    PutDispEnv(&worldmapRenderingStructs[1].m5C_DispEnv);
    freeHeap(ft4);
    freeHeap(g4);
    freeHeap(tpage);
}

void setupWorldmapGearMode(void) {
    if (gameState.m1834_vehicleMode & 0x4000) {
        switch (gameState.m1834_vehicleMode & 0x1FFF) {
        case 0:
            break;
        case 1:
            worldMapGearMode = 4;
            break;
        case 2:
            worldMapGearMode = 5;
            break;
        case 3:
            worldMapGearMode = 7;
            break;
        case 4:
            worldMapGearMode = 7;
            break;
        }
    } else if ((gameState.m22B1_isOnGear[0] | gameState.m22B1_isOnGear[1] | gameState.m22B1_isOnGear[2]) != 0) {
        worldMapGearMode = 2;
    } else {
        worldMapGearMode = 1;
    }
}

void setupWorldmapPositionFromField(void) {
    gameState.m1836_restoreWorldmapPosition = 0;
    switch (worldMapGearMode) {
    case 1:
    case 2:
        worldmapPosition.vx = gameState.m1820_worldmapPosition[0] << 12;
        worldmapPosition.vz = gameState.m1820_worldmapPosition[1] << 12;
        worldmapGamestate1824 = gameState.m1820_worldmapPosition[2];
        break;
    case 4:
    case 5:
    case 7:
        getGamestate182C(&worldmapPosition);
        worldmapGamestate1824 = gameState.m182C_vehiclePosition[3];
        break;
    }
}

void setupWorldmapPositionFromFileData(s32 index) {
    s16* p;

    if (gameState.m1834_vehicleMode & 0x2000) {
        gameState.m1834_vehicleMode &= 0xDFFF;
        getGamestate182C(&worldmapPosition);
        worldmapGamestate1824 = gameState.m182C_vehiclePosition[3];
        return;
    }
    p = worldmapFile1Buffer_4_0;
    while (p[1] != -1) {
        if ((s16)(u16)p[1] == index) {
            worldmapPosition.vx = p[0] << 12;
            worldmapPosition.vy = 0;
            worldmapPosition.vz = p[2] << 12;
            return;
        }
        p += 4;
    }
    worldmapPosition.vx = 0;
    worldmapPosition.vy = 0;
    worldmapPosition.vz = 0;
}

#ifdef PLATFORM_PC
static sWorldmapExitDef* pcWorldmapExitsDefs[4];
#endif

void finalizeWorldMapFileLoading(void) {
    u8* p;
    s32* q;
    s32 i;

    p = (u8*)worldmapFile1Buffer;
    worldmapFile1Buffer = (sWorldmapFile1Header*)mallocAndDecompress(p, 0);
    freeHeap(p);
    p = (u8*)worldmapFile1Buffer + worldmapFile1Buffer->m4_locationsOffset;
    worldmapModelFiles = (Model*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m8_modelsOffset);
    worldmapFile1Buffer_C = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->mC_collisionMeshesOffset);
    pWorldmapModelsConfig = (u16*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m10_modelsConfigOffset);
    worldmapFile1Buffer_14 = (sWorldmapOffsetEntry*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m14_billboardsOffset);
    worldmapFile1Buffer_18 = (sWorldmapParticle*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m18_particlesOffset);
    worldmapFile1Buffer_1C = (u8*)worldmapFile1Buffer + worldmapFile1Buffer->m1C_textOffset;
    worldmapFile1Buffer_20 = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m20_texAnims1Offset);
    worldmapFile1Buffer_24 = (s32*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m24_texAnims2Offset);
    for (i = 0; i < 16; i++) {
        worldmapFile1Buffer_2C_data[i] = (sWorldmapEncounterTable*)((u8*)worldmapFile1Buffer + worldmapFile1Buffer->m2C_encounterTableOffsets[i]);
    }
    worldmapFile1Buffer_4_0 = (s16*)(p + ((sWorldmapFile1Sub4*)p)->m0_initialPositionsOffset);
    q = (s32*)(p + ((sWorldmapFile1Sub4*)p)->m4_exitGroupsOffset);
#ifndef PLATFORM_PC
    worldmapExitsDefs = (sWorldmapExitDef**)q;
#else
    /* The PS1 writes the four pointers over their offsets; 8-byte pointers
     * would overwrite the next offset before it is read. */
    worldmapExitsDefs = pcWorldmapExitsDefs;
#endif
    worldmapExitsDefs[0] = (sWorldmapExitDef*)(p + q[0]);
    worldmapExitsDefs[1] = (sWorldmapExitDef*)(p + q[1]);
    worldmapExitsDefs[2] = (sWorldmapExitDef*)(p + q[2]);
    worldmapExitsDefs[3] = (sWorldmapExitDef*)(p + q[3]);
}

void allocateWorldMapsOT(void) {
    worldmapRenderingStructs[0].m70_OT = allocateHeap(PC_HEAP_OF(OTag, 1024 * (s32)sizeof(OTag)), 0);
    worldmapRenderingStructs[1].m70_OT = allocateHeap(PC_HEAP_OF(OTag, 1024 * (s32)sizeof(OTag)), 0);
}
