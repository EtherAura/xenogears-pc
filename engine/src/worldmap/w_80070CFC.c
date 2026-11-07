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

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern sWorldmapRenderingStruct worldmapRenderingStructs[2];
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
void pcFieldVSync(void); /* port/src/system/libetc.c */
s32 pcWm60On(void);
s32 pcWm60OffTick(void);
s32 pcWm60Half(s32 step);
void pcWm60RenderBegin(void);
void pcWm60RenderEnd(void);
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

    worldmapFlushCacheAndSync();
    DrawSync(0);
    VSync(0);
    VSyncCallback(vsyncCallback);
    InitGeom();
    pcInterpolationScene(); /* nothing of the field before carries over */
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
    {
        /* XG_WM_* start options (port/src/game/worldmap/start.c) */
        void portWorldmapStart(void);
        portWorldmapStart();
    }
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

/* ---- Enhanced: the 60 Hz world map (xg-hbc.19.1, docs/ENGINE_PLAN.md "60 Hz world map") ----
 *
 * With worldmap60 the main loop of the walking world map (modes 0-7) runs
 * every vblank, end to end. Every other frame is a tick: the native frame,
 * every task with the input of both vblanks (the frame between carries its
 * samples), run whole from where the tick before left things, so the game's
 * state at every tick is the Original's. The frame between is the first half
 * of the next native frame: it reads its own vblank's input and runs the
 * motion tasks only (callAllWorlmapObjects), as a preview: the leader goes
 * half of the native step it checks, the Yggdrasil flies at half every rate,
 * the followers go halfway along the trail, the camera adds the second half
 * of the tick's native step (w_800907F4.c). Decisions and counters stay on the
 * tick (pcWm60OffTick in the tasks). The tick then puts the previewed motion
 * back (pcWm60NativeStart, pcWm60YggStart) and runs the native frame.
 *
 * What still moves on the tick only (sequences, later tasks such as the
 * ships, particles, the fader) is drawn halfway: the ground task, which draws
 * the scene, puts in the values halfway from the tick's start to where the
 * tasks took them (pcWm60RenderBegin), draws, and takes the game's values
 * back (pcWm60RenderEnd); what moved by its own logic on the frame between is
 * drawn as it is (pcWm60EntryLive, pcWm60ModelLive, pcWm60CameraLive). The
 * frame between also takes the second half of what is split: the sprites'
 * animation steps, the clouds, the water. The camera keeps the ground's
 * origin (worldmapRadarPosition3, which the terrain streaming and the ground
 * tables follow) and moves its eye by the origin's half step when drawn
 * halfway. */
#define PC_WM60_ENTRIES 64
#define PC_WM60_MODELS 128 /* (event 17 has 86 models: its glows are 81-85) */
typedef struct {
    VECTOR entry[PC_WM60_ENTRIES];
    s8 entryValid[PC_WM60_ENTRIES];
    VECTOR model[PC_WM60_MODELS];
    s16 modelRot[PC_WM60_MODELS][9];
    s8 modelValid[PC_WM60_MODELS];
    SVECTOR rotation;
    sWorldmapCameraState camera;
    VECTOR radar;
    VECTOR radar3;
    s32 geomY;
    s32 matrixMode; /* worldmapMatrixMode: a switch is a cut */
} PcWm60State;
static s32 pcWm60Active;    /* this main loop runs at 60 Hz */
static s32 pcWm60Frame;     /* frames since the loop started: even ones are ticks */
static s32 pcWm60InRender;  /* inside pcWm60RenderBegin/End */
static s32 pcWm60GroundTicked; /* the ground task ran on this tick: the frame between owes its halves */
static u16 pcWm60Carry[6];  /* the frame between's input samples, for the tick */
static PcWm60State pcWm60Start; /* at the tick's start (before its tasks) */
static PcWm60State pcWm60Drawn; /* at the tick's ground task */
static PcWm60State pcWm60Full;  /* the values taken back after the drawing */
static s32 pcWm60GeomStart;     /* worldmapGeometryOffsetY at the tick's start */
static s32 pcWm60FaderStart;    /* the fader's level at the tick's start */
static sWorldmapTrackMark pcWm60Marks[16]; /* the tick's track marks, drawn again on the frame between */
static VECTOR* pcWm60MarkSource[16];       /* the position each mark was taken from */
static VECTOR* pcWm60MarkSaved[16];
static s32 pcWm60MarkCount;
static s32 pcWm60CutTick;   /* the event camera cut on this tick: its picture drawn as reached */
static s32 pcWm60Event;     /* an event mode (8-18): no track marks (their buffer is the walking map's) */
extern s16 worldmapNumModels;
extern SVECTOR worldmapRotation;
extern VECTOR worldmapRadarPosition;
extern s32 worldmapGeometryOffsetY;
extern sWorldmapTrackMark* worldmapTrackMarks;
extern s32 worldmapTrackMarkIndex;
extern s32 currentWorldmapMode;

s32 pcWm60On(void) {
    return pcWm60Active;
}
s32 pcWm60FrameIndex(void) {
    return pcWm60Frame;
}
s32 pcWm60OffTick(void) {
    return pcWm60Active && (pcWm60Frame & 1);
}
/* The frame's half of a per-frame step: the whole step natively; at 60 Hz
 * the tick takes the first half and the frame between the rest. */
s32 pcWm60Half(s32 step) {
    if (!pcWm60Active) return step;
    return (pcWm60Frame & 1) ? step - step / 2 : step / 2;
}
/* The camera's yaw as the game's logic sees it (inside the drawing the
 * picture's is put in). */
s32 pcWm60RotY(void) {
    return pcWm60InRender ? pcWm60Full.rotation.vy : worldmapRotation.vy;
}
/* addWorldmapTrackMark: where slot `index`'s mark was taken from. */
void pcWm60MarkFrom(s32 index, VECTOR* position) {
    pcWm60MarkSource[index & 0xF] = position;
}
/* ---- end to end: the frame between's motion ---- */
static s32 pcWm60Pending;              /* the leader moved on the frame between: the followers go halfway on */
static u8 pcWm60EntryLive[PC_WM60_ENTRIES]; /* moved by its own logic on the latest frame between */
static u8 pcWm60ModelLive[PC_WM60_MODELS];
static s32 pcWm60CameraLive;           /* the camera tasks ran on the latest frame between */
/* The motion's rate: half a native frame's on the frame between; the tick
 * runs the native frame whole from where the tick before left things
 * (pcWm60NativeStart, pcWm60YggStart), so its result is the Original's. */
s32 pcWm60Rate(void) {
    return pcWm60Active && (pcWm60Frame & 1) ? 2 : 1;
}
/* The camera tasks this frame between (the camera is split in halves, w_800907F4.c). */
void pcWm60CameraRun(void) {
    pcWm60CameraLive = 1;
}
void pcWm60Live(s32 index) {
    if (pcWm60Active && (pcWm60Frame & 1) && index >= 0 && index < PC_WM60_ENTRIES) pcWm60EntryLive[index] = 1;
}
static u8 pcWm60Previewed[8]; /* the frame between took a half step from where the tick left it */
/* The frame between's half step of a walking leader (pcWm60NativeStart takes it back on the tick). */
void pcWm60Preview(s32 index) {
    if (pcWm60Active && (pcWm60Frame & 1) && index >= 0 && index < 8) pcWm60Previewed[index] = 1;
}
void pcWm60LiveModel(s32 index) {
    if (pcWm60Active && (pcWm60Frame & 1) && index >= 0 && index < PC_WM60_MODELS) pcWm60ModelLive[index] = 1;
}
/* The frame between's tasks start: nothing live yet. */
void pcWm60FrameBetween(void) {
    pcWm60Pending = 0; /* a move the last tick did not count is not carried further */
    memset(pcWm60Previewed, 0, sizeof pcWm60Previewed);
    memset(pcWm60EntryLive, 0, sizeof pcWm60EntryLive);
    memset(pcWm60ModelLive, 0, sizeof pcWm60ModelLive);
    pcWm60CameraLive = 0;
}
/* The leader's preview step on the frame between (the followers then go
 * halfway along the trail); the tick, which counts its own native move,
 * clears it. */
void pcWm60Moved(void) {
    pcWm60Pending = 1;
}
s32 pcWm60MovePending(void) {
    return pcWm60Pending;
}
s32 pcWm60TakeMove(void) {
    s32 r = pcWm60Pending;
    pcWm60Pending = 0;
    return r;
}
static void pcWm60MidWorld(const VECTOR* a, const VECTOR* b, VECTOR* out, s32 shift);
void pcWm60MidPosition(const VECTOR* a, const VECTOR* b, VECTOR* out) {
    pcWm60MidWorld(a, b, out, 0);
}

/* Where the tick (and its decisions) left things: the native frame's start,
 * from which the next tick runs it whole. */
static VECTOR pcWm60TickPos[8];
static sWorldmapStateEntry pcWm60TickYgg; /* the Yggdrasil's entry (7) */
static sWorldmapStateEntry pcWm60TickEntry[8];
static SVECTOR pcWm60TickRotation;
static void pcWm60TickEnd(void) {
    s32 i;
    for (i = 0; i < 8; i++) pcWm60TickPos[i] = gWorldmapState->m0_entries[i].m28_position;
    pcWm60TickYgg = gWorldmapState->m0_entries[7];
    memcpy(pcWm60TickEntry, gWorldmapState->m0_entries, sizeof pcWm60TickEntry);
    pcWm60TickRotation = worldmapRotation;
}
/* On the tick, another entry as the native frame finds it: where the tick
 * before left it, when the frame between moved it (the rider copies the Gear
 * or the Yggdrasil it rides, which runs later in the frame). */
sWorldmapStateEntry* pcWm60NativeEntry(s32 index) {
    if (pcWm60Active && !(pcWm60Frame & 1) && pcWm60Frame != 0 && index >= 0 && index < 8 && pcWm60EntryLive[index]) {
        return &pcWm60TickEntry[index];
    }
    return &gWorldmapState->m0_entries[index];
}
/* The tick, the Yggdrasil in flight or at sea: its motion as the tick before
 * left it (the frame between's half was a preview); the native frame whole
 * follows. Its requests and sub-state are the tick's. */
void pcWm60YggStart(sWorldmapStateEntry* e) {
    const sWorldmapStateEntry* t = &pcWm60TickYgg;
    if (!pcWm60Active || (pcWm60Frame & 1) || pcWm60Frame == 0 || e != &gWorldmapState->m0_entries[7] ||
        t->m20_subState != e->m20_subState) {
        return;
    }
    e->m28_position = t->m28_position;
    e->m38_step = t->m38_step;
    e->m48_direction = t->m48_direction;
    e->m58 = t->m58;
    e->m5C = t->m5C;
    e->m60 = t->m60;
    e->m64 = t->m64;
    e->m6C = t->m6C;
    e->m70 = t->m70;
    worldmapRotation.vy = pcWm60TickRotation.vy;
    worldmapRotation.vz = pcWm60TickRotation.vz;
}
/* On the tick: the entry where the native frame started (swapped with where
 * the frame between took it; swap again to put that back). */
void pcWm60NativeStart(s32 index) {
    VECTOR t;
    if (!pcWm60Active || (pcWm60Frame & 1) || pcWm60Frame == 0 || index < 0 || index >= 8 || !pcWm60Previewed[index]) return;
    t = gWorldmapState->m0_entries[index].m28_position;
    gWorldmapState->m0_entries[index].m28_position = pcWm60TickPos[index];
    pcWm60TickPos[index] = t;
}

s32 pcWm60GroundTick(void) {
    return pcWm60GroundTicked;
}
s32 pcWm60FaderLevel(s32 now) {
    s32 v = (pcWm60FaderStart + now) / 2;
    return v < 0 ? 0 : v > 0xFF ? 0xFF : v;
}

static void pcWm60Capture(PcWm60State* s) {
    s32 i;
    s32 n = worldmapNumModels < PC_WM60_MODELS ? worldmapNumModels : PC_WM60_MODELS;
    for (i = 0; i < PC_WM60_ENTRIES; i++) {
        sWorldmapStateEntry* e = &gWorldmapState->m0_entries[i];
        s->entry[i] = e->m28_position;
        s->entryValid[i] = e->m1C_update != NULL && e->m0_state != 0 && e->m24_hidden == 0; /* one that appears is drawn where it is */
    }
    for (i = 0; i < n; i++) {
        s->model[i] = worldmapModels[i].m8_position;
        memcpy(s->modelRot[i], worldmapModels[i].m20_rotationMatrix.m, sizeof s->modelRot[i]);
        s->modelValid[i] = worldmapModels[i].m0_hidden == 0;
    }
    for (; i < PC_WM60_MODELS; i++) s->modelValid[i] = 0;
    s->rotation = worldmapRotation;
    s->camera = worldmapCameraVector;
    s->radar = worldmapRadarPosition;
    s->radar3 = worldmapRadarPosition3;
    s->geomY = worldmapGeometryOffsetY;
    s->matrixMode = worldmapMatrixMode;
}
static void pcWm60Put(const PcWm60State* s) {
    s32 i;
    s32 n = worldmapNumModels < PC_WM60_MODELS ? worldmapNumModels : PC_WM60_MODELS;
    for (i = 0; i < PC_WM60_ENTRIES; i++) gWorldmapState->m0_entries[i].m28_position = s->entry[i];
    for (i = 0; i < n; i++) {
        worldmapModels[i].m8_position = s->model[i];
        memcpy(worldmapModels[i].m20_rotationMatrix.m, s->modelRot[i], sizeof s->modelRot[i]);
    }
    worldmapRotation = s->rotation;
    worldmapCameraVector = s->camera;
    worldmapRadarPosition = s->radar;
    worldmapGeometryOffsetY = s->geomY;
}
/* Halfway from a to b; a jump (a teleport, a respawn) is not split. */
static s32 pcWm60Mid(s32 a, s32 b, s32 jump) {
    s32 d = b - a;
    if (d > jump || d < -jump) return b;
    return a + d / 2;
}
/* An angle (4096 a turn), halfway the short way round. */
static s32 pcWm60MidAngle(s32 a, s32 b) {
    s32 d = ((b - a + 0x800) & 0xFFF) - 0x800;
    s32 m;
    if (d > 0x400 || d < -0x400) return b;
    m = a + d / 2;
    if (a >= 0 && a < 0x1000 && b >= 0 && b < 0x1000) m &= 0xFFF;
    return m;
}
/* An eye coordinate (whole units) halfway, plus an offset in 20.12, rounded. */
static s32 pcWm60MidEye(s32 a, s32 b, s32 offset) {
    s32 d = b - a;
    if (d > 0x400 || d < -0x400 || offset > 0x400000 || offset < -0x400000) return b;
    return (((a + b) << 11) + offset + 0x800) >> 12;
}
/* A world position (20.12, wrapping round the map). */
static void pcWm60MidWorldJump(const VECTOR* a, const VECTOR* b, VECTOR* out, s32 shift, s32 jump);
static void pcWm60MidWorld(const VECTOR* a, const VECTOR* b, VECTOR* out, s32 shift) {
    pcWm60MidWorldJump(a, b, out, shift, 0x100000 >> shift); /* 256 units: no step is that long */
}
/* A model's move over the native frame (whole units, the map's wrap). */
static void pcWm60ModelDelta(const VECTOR* a, const VECTOR* b, VECTOR* d) {
    s32 sx = worldmapSizeX << 11;
    s32 sz = worldmapSizeY << 11;
    d->vx = b->vx - a->vx;
    d->vy = b->vy - a->vy;
    d->vz = b->vz - a->vz;
    if (d->vx > sx / 2) d->vx -= sx; else if (d->vx < -sx / 2) d->vx += sx;
    if (d->vz > sz / 2) d->vz -= sz; else if (d->vz < -sz / 2) d->vz += sz;
}
/* The models' moves on the tick before: a move longer than a jump (256 units)
 * that continues it (the event modes' fly-bys speeding up: event 14's
 * Yggdrasil leaves at 350 to 1400 units a frame) is drawn halfway too; one
 * from rest or elsewhere (a respawn: event 15's rings) whole. */
static VECTOR pcWm60ModelStep[PC_WM60_MODELS];
static s8 pcWm60ModelStepValid[PC_WM60_MODELS];
static s32 pcWm60ModelJump(s32 i, const VECTOR* a, const VECTOR* b) {
    VECTOR d;
    s32 ex;
    s32 ey;
    s32 ez;
    s32 m;
    if (!pcWm60ModelStepValid[i]) return 0x100;
    pcWm60ModelDelta(a, b, &d);
    ex = d.vx - pcWm60ModelStep[i].vx;
    ey = d.vy - pcWm60ModelStep[i].vy;
    ez = d.vz - pcWm60ModelStep[i].vz;
    m = abs(d.vx) > abs(d.vz) ? abs(d.vx) : abs(d.vz);
    m = m > abs(d.vy) ? m : abs(d.vy);
    if (abs(ex) <= (m >> 2) + 0x40 && abs(ey) <= (m >> 2) + 0x40 && abs(ez) <= (m >> 2) + 0x40) {
        return 0x8000; /* continues the move before */
    }
    return 0x100;
}
static void pcWm60MidWorldJump(const VECTOR* a, const VECTOR* b, VECTOR* out, s32 shift, s32 jump) {
    s32 sx = (worldmapSizeX << 23) >> shift;
    s32 sz = (worldmapSizeY << 23) >> shift;
    s32 dx = b->vx - a->vx;
    s32 dz = b->vz - a->vz;
    if (dx > sx / 2) dx -= sx; else if (dx < -sx / 2) dx += sx;
    if (dz > sz / 2) dz -= sz; else if (dz < -sz / 2) dz += sz;
    if (dx > jump || dx < -jump || dz > jump || dz < -jump) {
        *out = *b;
        return;
    }
    out->vx = b->vx - (dx - dx / 2);
    out->vz = b->vz - (dz - dz / 2);
    out->vy = pcWm60Mid(a->vy, b->vy, jump);
}
/* The picture halfway from `a` to the full state `b` (the live values). */
static void pcWm60Halfway(const PcWm60State* a, const PcWm60State* b) {
    s32 i;
    s32 j;
    s32 n = worldmapNumModels < PC_WM60_MODELS ? worldmapNumModels : PC_WM60_MODELS;
    VECTOR r3;
    s32 ox;
    s32 oz;
    for (i = 0; i < PC_WM60_ENTRIES; i++) {
        if (a->entryValid[i] && b->entryValid[i] && !pcWm60EntryLive[i]) {
            pcWm60MidWorld(&a->entry[i], &b->entry[i], &gWorldmapState->m0_entries[i].m28_position, 0);
        }
    }
    for (i = 0; i < n; i++) {
        s16* m;
        if (!a->modelValid[i] || !b->modelValid[i] || pcWm60ModelLive[i]) continue;
        pcWm60MidWorldJump(&a->model[i], &b->model[i], &worldmapModels[i].m8_position, 12, pcWm60ModelJump(i, &a->model[i], &b->model[i]));
        for (j = 0; j < 9; j++) {
            s32 d = b->modelRot[i][j] - a->modelRot[i][j];
            if (d > 0x800 || d < -0x800) break; /* a turn too fast to split: whole */
        }
        if (j < 9) continue;
        m = &worldmapModels[i].m20_rotationMatrix.m[0][0];
        for (j = 0; j < 9; j++) m[j] = (s16)pcWm60Mid(a->modelRot[i][j], b->modelRot[i][j], 0x800);
    }
    if (pcWm60CameraLive) {
        goto marks; /* the camera moved every frame: drawn as it is */
    }
    worldmapRotation.vx = pcWm60MidAngle(a->rotation.vx, b->rotation.vx);
    worldmapRotation.vy = pcWm60MidAngle(a->rotation.vy, b->rotation.vy);
    worldmapRotation.vz = pcWm60MidAngle(a->rotation.vz, b->rotation.vz);
    pcWm60MidWorld(&a->radar, &b->radar, &worldmapRadarPosition, 0);
    worldmapGeometryOffsetY = pcWm60Mid(a->geomY, b->geomY, 0x40);
    /* The eye halfway, then moved by the origin's half step (the ground is
     * drawn round the full origin), rounded once: the eye is whole units. */
    pcWm60MidWorld(&a->radar3, &b->radar3, &r3, 0);
    ox = r3.vx - b->radar3.vx;
    oz = r3.vz - b->radar3.vz;
    worldmapCameraVector.m0_eye.vx = pcWm60MidEye(a->camera.m0_eye.vx, b->camera.m0_eye.vx, ox);
    worldmapCameraVector.m0_eye.vy = pcWm60MidEye(a->camera.m0_eye.vy, b->camera.m0_eye.vy, 0);
    worldmapCameraVector.m0_eye.vz = pcWm60MidEye(a->camera.m0_eye.vz, b->camera.m0_eye.vz, -oz);
    worldmapCameraVector.m8_target.vx = pcWm60MidEye(a->camera.m8_target.vx, b->camera.m8_target.vx, ox);
    worldmapCameraVector.m8_target.vy = pcWm60MidEye(a->camera.m8_target.vy, b->camera.m8_target.vy, 0);
    worldmapCameraVector.m8_target.vz = pcWm60MidEye(a->camera.m8_target.vz, b->camera.m8_target.vz, -oz);
    worldmapCameraVector.m10_up.vx = pcWm60Mid(a->camera.m10_up.vx, b->camera.m10_up.vx, 0x800);
    worldmapCameraVector.m10_up.vy = pcWm60Mid(a->camera.m10_up.vy, b->camera.m10_up.vy, 0x800);
    worldmapCameraVector.m10_up.vz = pcWm60Mid(a->camera.m10_up.vz, b->camera.m10_up.vz, 0x800);
marks:
    /* The track marks (the characters' shadows) under the drawn positions. */
    for (i = 0; !pcWm60Event && i < worldmapTrackMarkIndex && i < 16; i++) {
        sWorldmapTrackMark* mk = &worldmapTrackMarks[i];
        VECTOR* src = pcWm60MarkSource[i];
        if (src == NULL) continue;
        for (j = 0; j < PC_WM60_ENTRIES; j++) {
            if (src == &gWorldmapState->m0_entries[j].m28_position) break;
        }
        if (j == PC_WM60_ENTRIES) continue;
        if (mk->m0_x != (s16)(b->entry[j].vx >> 12) || mk->m4_z != (s16)(b->entry[j].vz >> 12)) continue;
        mk->m0_x = src->vx >> 12;
        mk->m4_z = src->vz >> 12;
    }
}

/* ---- The event modes 8-18 (the story's world map scenes), drawn halfway ----
 *
 * Their tasks are a director (a timed list of requests), a camera (eases,
 * Bezier paths, cuts on requests), the scene's models and effects, and a scene
 * task that draws (worldmap_taskScene / NoBillboards / NoParticles, or the
 * ground task in event 8/11). The tick runs them all as the Original; the
 * frame between runs only what draws: the scene task (pcWm60RenderBegin/End:
 * the models, camera and particles halfway, the clouds, water and sprites
 * their second halves), the fader, event 16's screen warp strips again
 * (pcWm60Mode16WarpDraw). Event 8/11's pad camera is input: the frame between
 * previews half of its native step from that vblank's pad (w_800736DC.c).
 * Three things only the event modes need: */

/* A camera cut (a request that puts the camera somewhere new, called by the
 * event cameras on the tick): that tick's picture is drawn as the tick left
 * it, not halfway from before the cut. */
void pcWm60Cut(void) {
    if (pcWm60Active && !(pcWm60Frame & 1)) pcWm60CutTick = 1;
}
/* A scene task drew on this tick: the frame between owes its halves (as the ground task). */
void pcWm60SceneTicked(void) {
    if (pcWm60Active && !(pcWm60Frame & 1)) pcWm60GroundTicked = 1;
}

/* A model's colour that a task sets every native frame (the fades: rings,
 * pillars, glows). The Original writes the frame's primitive buffer
 * (worldmapOddOrEven), so each buffer has the colour of the frame that last
 * drew it; at 60 Hz the frame between draws the other buffer, so the tick
 * writes both: its own halfway from the previous tick's colour (the tick's
 * picture is halfway), the frame between's where the tick took it. The next
 * tick puts the full colour back in its own first (pcWm60ColourSettle), so a
 * fade that stops rests where the Original's does. */
typedef struct {
    u8 set;
    u8 half;  /* the tick's buffer holds the halfway colour */
    u8 ft3;
    u8 rgb[3];
    u8* halfBuf;
    s32 n;
} PcWm60Colour;
static PcWm60Colour pcWm60Colours[PC_WM60_MODELS];
void setWorldmapPolysColor(POLY_FT4* p, s32 n, s32 r, s32 g, s32 b);
void setWorldmapPolysColorFT3(POLY_FT3* p, u32 n, s32 r, s32 g, s32 b);
static void pcWm60Paint(u8* buf, s32 n, s32 ft3, s32 r, s32 g, s32 b) {
    if (ft3) {
        setWorldmapPolysColorFT3((POLY_FT3*)buf, n, r, g, b);
    } else {
        setWorldmapPolysColor((POLY_FT4*)buf, n, r, g, b);
    }
}
void pcWm60ModelColour(sWorldmapModel* m, s32 r, s32 g, s32 b, s32 ft3) {
    s32 i = m - worldmapModels;
    s32 n = m->m40_modelBlock->m4_numPrims;
    u8* own = m->m48_primBuffers[worldmapOddOrEven];
    PcWm60Colour* c;

    if (!pcWm60Active || (pcWm60Frame & 1) || i < 0 || i >= PC_WM60_MODELS) {
        pcWm60Paint(own, n, ft3, r, g, b); /* the Original's */
        return;
    }
    c = &pcWm60Colours[i];
    pcWm60Paint(m->m48_primBuffers[!worldmapOddOrEven], n, ft3, r, g, b); /* the frame between's */
    if (c->set && pcWm60Frame != 0 && !pcWm60CutTick) {
        pcWm60Paint(own, n, ft3, (c->rgb[0] + (r & 0xFF)) / 2, (c->rgb[1] + (g & 0xFF)) / 2, (c->rgb[2] + (b & 0xFF)) / 2);
        c->half = 1;
        c->halfBuf = own;
        c->n = n;
        c->ft3 = ft3;
    } else {
        pcWm60Paint(own, n, ft3, r, g, b);
        c->half = 0;
    }
    c->set = 1;
    c->rgb[0] = r;
    c->rgb[1] = g;
    c->rgb[2] = b;
}
static void pcWm60ColourSettle(void) {
    s32 i;
    for (i = 0; i < PC_WM60_MODELS; i++) {
        PcWm60Colour* c = &pcWm60Colours[i];
        if (c->half) {
            pcWm60Paint(c->halfBuf, c->n, c->ft3, c->rgb[0], c->rgb[1], c->rgb[2]);
            c->half = 0;
        }
    }
}

/* The tick's start: the state its tasks start from. */
static void pcWm60TickStart(void) {
    sWorldmapStateEntry* e = &gWorldmapState->m0_entries[0];
    s32 worldmap_taskFader_update(s32);
    pcWm60ColourSettle();
    pcWm60Capture(&pcWm60Start);
    pcWm60GroundTicked = 0;
    pcWm60CutTick = 0;
    pcWm60GeomStart = worldmapGeometryOffsetY;
    pcWm60FaderStart = e->m1C_update == worldmap_taskFader_update ? (s32)e->m50 : 0;
}
/* The ground task's drawing: the picture's values in. */
void pcWm60RenderBegin(void) {
    s32 i;
    s32 j;
    s32 n;
    if (!pcWm60Active) return;
    pcWm60Capture(&pcWm60Full);
    if (!(pcWm60Frame & 1)) {
        /* the tick: halfway from its start; its marks kept for the frame between */
        pcWm60Drawn = pcWm60Full;
        if (!pcWm60Event) {
            pcWm60MarkCount = worldmapTrackMarkIndex;
            memcpy(pcWm60Marks, worldmapTrackMarks, sizeof pcWm60Marks);
            for (i = 0; i < 16; i++) pcWm60MarkSaved[i] = pcWm60MarkSource[i];
        }
        if (pcWm60Frame != 0 && !pcWm60CutTick && pcWm60Start.matrixMode == pcWm60Full.matrixMode) {
            pcWm60Halfway(&pcWm60Start, &pcWm60Full); /* (the loop's first frame has nothing to come from; a cut neither) */
        }
        n = worldmapNumModels < PC_WM60_MODELS ? worldmapNumModels : PC_WM60_MODELS;
        for (i = 0; i < PC_WM60_MODELS; i++) {
            pcWm60ModelStepValid[i] = i < n && pcWm60Frame != 0 && pcWm60Start.modelValid[i] && pcWm60Full.modelValid[i];
            if (pcWm60ModelStepValid[i]) pcWm60ModelDelta(&pcWm60Start.model[i], &pcWm60Full.model[i], &pcWm60ModelStep[i]);
        }
    } else {
        /* the frame between: the marks its own tasks added, then the tick's of
         * those that did not run (drawn halfway on) */
        n = worldmapTrackMarkIndex;
        for (i = 0; !pcWm60Event && i < pcWm60MarkCount && n < 16; i++) {
            for (j = 0; j < PC_WM60_ENTRIES; j++) {
                if (pcWm60MarkSaved[i] == &gWorldmapState->m0_entries[j].m28_position) break;
            }
            if (j < PC_WM60_ENTRIES && pcWm60EntryLive[j]) continue;
            worldmapTrackMarks[n] = pcWm60Marks[i];
            pcWm60MarkSource[n] = pcWm60MarkSaved[i];
            n++;
        }
        if (!pcWm60Event) worldmapTrackMarkIndex = n;
        pcWm60Halfway(&pcWm60Drawn, &pcWm60Full);
    }
    pcWm60InRender = 1;
}
/* ... and the game's values back. */
void pcWm60RenderEnd(void) {
    if (!pcWm60Active) return;
    pcWm60Put(&pcWm60Full);
    if (!(pcWm60Frame & 1) && !pcWm60Event) {
        memcpy(worldmapTrackMarks, pcWm60Marks, sizeof pcWm60Marks); /* as the draw left them, full */
    }
    pcWm60InRender = 0;
}
/* Before the pause, the controller wait, the menu or leaving the map, decided
 * on the tick: the frame between's vblank passes without its frame (the
 * Original's frame took two vblanks, and what comes next is the next native
 * frame), with its halves: the camera's second half, the sprites' second
 * animation steps, the clouds' and the water's. Its input stays buffered for
 * what comes next, as the Original's did. */
void pcWm60SpriteFinish(void);
void pcWm60CamFinish(void);
/* The stepped clock (port/src/system/hw.c), so the game's timing (its sound
 * among it: an effect keyed against the music sequencer's interrupts) stays
 * the Original's: the frame between is work the PS1 does not do
 * (hwClockHold: no time); the tick's work after its vblank (the decisions,
 * the picture's DrawOTag) the PS1 does after the second vblank, so its time
 * is spent there (hwClockDefer, hwClockSettle at the next tick's start or
 * when the frame between's vblank is spent here). */
void hwClockHold(int on);
void hwClockDefer(int on);
void hwClockSettle(void);
static s32 pcWm60Deferring;
static void pcWm60DeferEnd(void) {
    if (pcWm60Deferring) {
        hwClockDefer(0);
        pcWm60Deferring = 0;
    }
}
static void pcWm60Settle(void) {
    if (!pcWm60Active || (pcWm60Frame & 1)) return;
    pcWm60DeferEnd();
    VSync(0);
    hwClockSettle(); /* the tick's work after its vblank, as the Original's after its second */
    pcWm60Frame++; /* the frame between, spent */
    pcWm60CamFinish();
    if (pcWm60GroundTicked) {
        worldmapWaterWave += pcWm60Half(0x40);
        worldmapCloudsMoveCallback();
        pcWm60SpriteFinish();
        pcWm60GroundTicked = 0;
    }
}

/* worldmapMainLoop (US and JP: the waitWorldmapControllerReconnect block is #if !VERSION_JP) */
void worldmapMainLoop(void) {
    RECT rect;
    s32 i;
    s32 tick;
    s32 decide;
    sWorldmapRenderingStruct* rs;
    u16* in[6];
    void portWorldmapSweepFrame(s32 frame, s32 tick);

    pCurrentWorldmapRenderingStruct = &worldmapRenderingStructs[1];
    worldmapOddOrEven = 1;
    continueWorldmapLoop = 1;
    /* Enhanced: the world map at 60 Hz (above): the walking map end to end, the event modes drawn halfway */
    pcWm60Active = pcGameplayWorldmap60();
    pcWm60Event = currentWorldmapMode >= 8;
    pcWm60Frame = 0;
    pcWm60Pending = 0;
    memset(pcWm60Colours, 0, sizeof pcWm60Colours);
    pcWm60FrameBetween();
    in[0] = &worldmapInput1_0;
    in[1] = &worldmapInput1_1;
    in[2] = &worldmapInput2_0;
    in[3] = &worldmapInput2_1;
    in[4] = &worldmapInput3_0;
    in[5] = &worldmapInput3_1;
    for (i = 0; i < 6; i++) {
        pcWm60Carry[i] = 0;
    }
    do {
        if (!pcWm60Active || !(pcWm60Frame & 1)) {
            /* the F1 setting, live: switched between native frames */
            s32 want = pcGameplayWorldmap60();
            if (want != pcWm60Active) {
                pcWm60Active = want;
                pcWm60Frame = 0;
                pcWm60ColourSettle(); /* (the buffers keep the full colours) */
                memset(pcWm60Colours, 0, sizeof pcWm60Colours);
            }
        }
        tick = !pcWm60Active || !(pcWm60Frame & 1);
        if (tick) {
            hwClockSettle(); /* (the tick before's work after its vblank: now, as the Original's) */
        } else {
            hwClockHold(1); /* the frame between: no time on the stepped clock */
        }
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
        /* the frame between's samples reach the tick, as the native frame's two vblanks did */
        for (i = 0; i < 6; i++) {
            pcWm60Carry[i] |= *in[i];
            if (tick) {
                *in[i] = pcWm60Carry[i];
                pcWm60Carry[i] = 0;
            }
        }
        portWorldmapSweepFrame(pcWm60Frame, tick); /* XG_SWEEP_LOG (port/src/game/worldmap/sweep.c) */
        if (tick) {
            while (loadWorldmapChunk() == 3) {
                VSync(0);
            }
            CdSync(1, worldmapStreamingCDSyncStatus);
        }
        rs = &worldmapRenderingStructs[0];
        if (pCurrentWorldmapRenderingStruct == rs) {
            rs++;
        }
        pCurrentWorldmapRenderingStruct = rs;
        worldmapOddOrEven = worldmapOddOrEven == 0;
        ClearOTagR(rs->m70_OT, 0x400);
        /* Smooth motion (port/src/gpu/interpolation.c): the frame's pictures
         * from here to its DrawOTag, the buffer drawn its area. */
        pcInterpolationFrameBegin(PC_INTERP_WORLDMAP, rs->m0_DrawEnv.clip.x, rs->m0_DrawEnv.clip.y,
                                  rs->m0_DrawEnv.clip.w, rs->m0_DrawEnv.clip.h);
        clearShapeTransfertTableEntry(worldmapOddOrEven);
        uploadCharacterSprites();
        if (pcWm60Active && tick) pcWm60TickStart();
        callAllWorlmapObjects();
        DrawSync(0);
        if (pcWm60Active) {
            /* one vblank, as the 60 Hz field's flip: with Speedup a vblank already passed while drawing ends the wait */
            if (!tick) hwClockHold(0);
            pcFieldVSync();
            if (!tick) {
                hwClockHold(1);
            } else {
                hwClockDefer(1);
                pcWm60Deferring = 1;
            }
        } else {
            VSync(2);
        }
        /* What the Original decides after its frame (the gear switch, the
         * pause, the encounter roll, the menu, the minimap, the texture
         * animations) the 60 Hz loop decides after the tick, from the tick's
         * input and state; what blocks or leaves the map first waits the
         * frame between's vblank (pcWm60Settle), as the Original's frame took
         * two. */
        decide = tick;
        if (decide) checkSoftReboot();
        PutDispEnv(&pCurrentWorldmapRenderingStruct->m5C_DispEnv);
        PutDrawEnv(&pCurrentWorldmapRenderingStruct->m0_DrawEnv);
        if (!decide) {
            /* the frame between: nothing to decide */
        } else if (gearSwitchDisabled == 0 && worldmapGearTogglePressed != 0 && worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && worldmapExitVar0 == -1 &&
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
        if (decide) {
            if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0 && (worldmapInput2_0 & 0x800)) {
                pcWm60Settle();
                runWorldmapPause();
            }
#if !VERSION_JP
            if (worldmapFadeRunning == 0 && worldmapExitVar2 == 0 && continueWorldmapLoop != 0 && worldmapEncounterTriggered == 0 && isControllerConnected(0) == 0) {
                pcWm60Settle();
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
            worldmapEncounterTriggered = 0;
            if (worldmapInput2_0 & 0x100) {
                gameState.m1842_disableWorldmapMinimap ^= 1;
            }
            if (worldmapFadeRunning == 0 && worldmapExitVar2 != 0 && continueWorldmapLoop != 0) {
                if (worldMapGearMode > 0) {
                    if (worldMapGearMode < 4) {
                        pcWm60Settle();
                        worldmapPrepareEnterMenu();
                        menuToEnter = 0;
                        useDebugMenuList = 0;
                        g_menuOpenArg = 1;
                        worldmapFlushCacheAndSync();
                        {
                            /* The menu's own kind for the wide picture (include/pc_wide.h). */
                            s32 wideKind = pcWideSetMode(PC_WIDE_MENU);
                            enterMenu();
                            pcWideSetMode(wideKind);
                        }
                        worldmapFlushCacheAndSync();
                        worldmapRestoreAfterMenu();
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
        }
        shapeTransfert();
        if (decide) {
            updateWorldmapTexAnims1();
            updateWorldmapTexAnims2();
        }
        /* At 60 Hz the frame between projects halfway in the geometry offset's ease (the minimap's slide) */
        SetGeomOffset(0xA0, pcWm60Active && tick && !pcWm60CameraLive && !pcWm60CutTick && pcWm60Frame != 0 ? pcWm60Mid(pcWm60GeomStart, worldmapGeometryOffsetY, 0x40) : worldmapGeometryOffsetY);
        DrawOTag(&pCurrentWorldmapRenderingStruct->m70_OT[0x3FF]);
        pcInterpolationFrameEnd();
        if (continueWorldmapLoop == 0) {
            pcWm60Settle(); /* leaving: nothing moves on, and the Original's frame took two vblanks */
        }
        if (pcWm60Active && tick) pcWm60TickEnd(); /* (after the tick's decisions too) */
        if (pcWm60Active && !tick) hwClockHold(0);
        pcWm60DeferEnd();
        pcWm60Frame++;
    } while (continueWorldmapLoop != 0);
    hwClockSettle();
    pcWm60Active = 0;
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

static void worldmapGroundMatrix(void) {
    if (worldmapMatrixMode == 0) {
        /* the free camera's tilt (freecam.c): the view matrix from its pitch, the game's kept */
        s16 pitch = worldmapRotation.vx;
        worldmapRotation.vx = pcWorldCameraPitch(pitch);
        setupWorldmapMatrix1(&worldmapCameraVector.m0_eye);
        worldmapRotation.vx = pitch;
    } else {
        setupWorldmapMatrix2(&worldmapCameraVector);
    }
}

void pcWm60SpriteDecide(void);
void pcWm60CamCommit(void);
void pcWm60ParticlesBefore(void);
void pcWm60ParticlesMid(void);
void pcWm60ParticlesAfter(void);

s32 worldmap_taskGround_update(s32 index) {
    SVECTOR unused; /* retail frame keeps 8 unused bytes */
    void portWorldmapSweepRender(s32 frame, s32 tick);
    s32 offTick = pcWm60OffTick();

    if (pcWm60On() && !offTick) {
        pcWm60CamCommit(); /* (the world camera's halves, if the camera task did not take them) */
    }
    if (pcWm60On() && !offTick) {
        /* Enhanced, 60 Hz: the Original's camera first, for the tick's verdicts (which sprites animate) */
        worldmapGroundMatrix();
        pcWm60SpriteDecide();
        pcWm60GroundTicked = 1;
    }
    pcWm60RenderBegin(); /* Enhanced, 60 Hz: the picture's values in (above) */
    worldmapGroundMatrix();
    portWorldmapSweepRender(pcWm60Frame, !offTick); /* XG_SWEEP_LOG: the picture's values */
    if (!offTick) {
        if (pcWm60On()) pcWm60ParticlesBefore();
        updateWorldmapParticleEmitters(); /* spawns (the RNG) and moves: the tick's */
        if (pcWm60On()) pcWm60ParticlesMid();
    }
    drawWorlmapPolyFT4Array1();
    if (pcWm60On() && !offTick) pcWm60ParticlesAfter();
    worldmapDrawSpriteActors();
    drawWorldmapBillboards();
    drawWorldmapTrackMarks();
    renderWorldmapModels();
    /* streaming follows the camera's own position (never put in), every frame
     * the camera moves (end to end: every vblank) */
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
    if (gameState.m1842_disableWorldmapMinimap == 0) {
        worldmapRenderMinimap();
    }
    pcWm60RenderEnd();
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

/* worldmapFadeIn (FT4 xy store order found by search; count loop is while (--count != -1)) */
void worldmapFadeIn(s32 count, s32 start, s32 step, s32 abr) {
    POLY_FT4* ft4;
    POLY_G4* g4;
    DR_TPAGE* tpage;
    sWorldmapRenderingStruct* rs;
    sWorldmapRenderingStruct* next;
    s32 i;
    s32 odd;

    ft4 = allocateHeap(sizeof(POLY_FT4) * 3, 1);
    g4 = allocateHeap(sizeof(POLY_G4) * 2, 1);
    tpage = allocateHeap(sizeof(DR_TPAGE), 1);
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
        worldmapFadeWide(rs->m70_OT + 1, ft4, odd);
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

static sWorldmapExitDef* pcWorldmapExitsDefs[4];

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
    /* The PS1 writes the four pointers over their offsets; 8-byte pointers
     * would overwrite the next offset before it is read. */
    worldmapExitsDefs = pcWorldmapExitsDefs;
    worldmapExitsDefs[0] = (sWorldmapExitDef*)(p + q[0]);
    worldmapExitsDefs[1] = (sWorldmapExitDef*)(p + q[1]);
    worldmapExitsDefs[2] = (sWorldmapExitDef*)(p + q[2]);
    worldmapExitsDefs[3] = (sWorldmapExitDef*)(p + q[3]);
}

void allocateWorldMapsOT(void) {
    worldmapRenderingStructs[0].m70_OT = allocateHeap(1024 * (s32)sizeof(OTag), 0);
    worldmapRenderingStructs[1].m70_OT = allocateHeap(1024 * (s32)sizeof(OTag), 0);
}
