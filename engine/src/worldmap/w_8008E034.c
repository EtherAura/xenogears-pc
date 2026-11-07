/*
 * Gamestate position save/restore, gear and Yggdrasil tasks
 * (Noah: worldmap.cpp, worldmapTaskYggdrasil.cpp, worldmapPlayerGear.cpp).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/sound.h"
#include "worldmap/worldmap.h"

extern sWorldmapState* gWorldmapState;
extern s32 worldMapGearMode;
extern sWorldmapModel* worldmapModels;
extern u8 adjustLocationAfterCollisionVar0;
extern u8 targetVehicleEntityIndex;
extern sWorldmapExitDef* worldmapCurrentExit;
extern s16 worldmapExitVar0;
extern sWorldmapExitDef worldmapExitToField312;
extern sWorldmapExitDef worldmapExitToField440;
extern VECTOR worldmapRadarPosition;
extern s16 worldmapPlayerDirection;                         /* Noah: worldmapVar_8009d52c */
extern VECTOR worldmapPosition;
extern u32 worldmapGamestate1824;
extern s32 worldmapNumActivePartyMembers;
extern s32 currentWorldmapMode;
extern SVECTOR worldmapRotation;
extern s16 worldmapExitVar1;
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
extern s16 currentWorldmapDynamicCollisionSlot;
extern sWorldmapSeqHeader* fieldSeq;
extern SVECTOR yggdrasilScriptedLandPath[5];                   /* yggdrasil take-off path (vx, vz used) */
extern SVECTOR yggdrasilScriptedSeaPath[12];                   /* yggdrasil sea path (vx, vz used) */
extern s32 worldmapExitToFieldVar0;
extern void* worldmapSound1;
extern void* worldmapSound2;
extern s32 worldmapFadePolyAbr;
extern s32 worldmapFadePolyStep;
extern s32 worldmapFile5;
extern s32 worldmapFile7;

#define SCRATCH_A0 (*(SVECTOR*)0x1F8000A0)
#define SCRATCH_A8 (*(SVECTOR*)0x1F8000A8)
#define ABS(x) ((x) < 0 ? -(x) : (x))

s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void attachWorldmapModels(s32 parent, s32 child);
void getGamestate182C(VECTOR* arg0);
void setGamestate182C(VECTOR* arg0);
s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z);
s32 changeWorldmapEntityState(s32 index, s32 state);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void clearWorldmapParticles(s32 type);
s16 getWorldmapGroundType(VECTOR* position);
s16 checkWorldmapPositionSub1_0_1(s16 a, s16 b);
void processWorldmapDynamicCollisions(VECTOR* position, s32 radius, s32 height, u8* outType, u8* outIndex);
s32 stepGearAwayFromYggdrasil(sWorldmapStateEntry* entry);
void checkWorldmapPositionSub0(VECTOR* position);
s32 getWorldmapElevationWithWater(s32 x, s32 z);
void selectYggdrasilExitDirection(VECTOR* from, VECTOR* to, VECTOR* step, s16* direction);
s32 setupWorldmapExits(VECTOR* position, s32 mode);
s32 processYggdrasilInputs_mode4(sWorldmapStateEntry* entry);
s32 processYggdrasilInputs(sWorldmapStateEntry* entry);
s32 checkWorldmapPositionYggdrasil(VECTOR* position, VECTOR* step, VECTOR* output, s32 stepScale, s32 mode);
void addWorldmapTrackMark(s16 type, VECTOR* position);
void initWorldmapEncounterTimers(void);
void updateWorldmapEncounterTimers(void);                      /* Noah: followLeaderPostProcess */
void startWorldmapMusicFromBuffer(void* data, s32 fileId);
void playSoundEffectInFreeSlot(s32 arg0);
s32 checkWorldmapPosition(VECTOR* position, VECTOR* step, VECTOR* output, s32 stepScale, s32 mode);

/* Noah: setGamestate182C (worldmap.cpp). */
void setGamestate182C(VECTOR* arg0) {
    gameState.m182C_vehiclePosition[0] = arg0->vx >> 12;
    gameState.m182C_vehiclePosition[1] = arg0->vy >> 12;
    gameState.m182C_vehiclePosition[2] = arg0->vz >> 12;
}

/* Noah: setupYggdrasilInternalExit (worldmapTaskYggdrasil.cpp). */
void setupYggdrasilInternalExit(void) {
    if (adjustLocationAfterCollisionVar0 != 0) {
        switch (targetVehicleEntityIndex) {
        case 15:
            worldmapCurrentExit = &worldmapExitToField312;
            worldmapExitVar0 = worldmapExitToField312.mC_newWorldmapMode;
            break;
        case 16:
            worldmapCurrentExit = &worldmapExitToField440;
            worldmapExitVar0 = worldmapExitToField440.mC_newWorldmapMode;
            break;
        }
    }
}

/* Noah: checkYggdrasilLandingPosition (worldmapTaskYggdrasil.cpp). */
s32 checkYggdrasilLandingPosition(VECTOR* position, VECTOR* unused, s32 stepScale) {
    s32 angle;
    VECTOR output;
    VECTOR step;

    angle = 0;
    do {
        step.vx = getAngleCos(angle);
        step.vz = -getAngleSin(angle);
        if (checkWorldmapPosition(position, &step, &output, stepScale, 2) == 1) {
            return angle;
        }
        angle += 0x100;
    } while (angle < 0x1000);
    return -1;
}

/* Noah: worldmap_taskYggdrasil_init (worldmapTaskYggdrasil.cpp). */
s32 worldmap_taskYggdrasil_init(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    sWorldmapScratch* scratch = (sWorldmapScratch*)0x1F800000;
    s32 result;
    s16 direction;

    entry->m24_hidden = 0;
    getGamestate182C(&entry->m28_position);
    result = 1;
    direction = gameState.m182C_vehiclePosition[3];
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m64 = 0;
    entry->m60 = 0;
    entry->m74 = 0;
    entry->m68 = 0xFFD80000;
    entry->m70 = 0;
    entry->m6C = 0;
    entry->m48_direction = direction;
    switch (gameState.m1834_vehicleMode & 0x1FFF) {
    case 0:
        result = 3;
        break;
    case 1:
    case 2:
        entry->m4A_stepScale = 0xC;
        break;
    case 3:
    case 4:
        entry->m4A_stepScale = 0x20;
        break;
    }
    switch (worldMapGearMode) {
    case 1:
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        break;
    case 2:
    case 3:
        entry->m20_subState = 1;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        break;
    case 4:
    case 5:
        entry->m20_subState = 3;
        entry->m28_position.vx = worldmapPosition.vx;
        entry->m28_position.vz = worldmapPosition.vz;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) - 0x40000;
        entry->m48_direction = worldmapGamestate1824;
        entry->m74 = worldmapNumActivePartyMembers;
        worldmapRadarPosition = entry->m28_position;
        {
            s16 dir = entry->m48_direction;
            worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 1;
            worldmapPlayerDirection = dir;
        }
        break;
    case 6:
        break;
    case 7:
        entry->m20_subState = 2;
        entry->m28_position.vx = worldmapPosition.vx;
        entry->m28_position.vz = worldmapPosition.vz;
        entry->m28_position.vy = worldmapPosition.vy;
        entry->m48_direction = worldmapGamestate1824;
        entry->m74 = worldmapNumActivePartyMembers;
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    }
    worldmapModels[0].m8_position = entry->m28_position;
    worldmapModels[0].m0_hidden = entry->m24_hidden;
    scratch->mA0.vx = -(entry->m70 >> 12);
    scratch->mA0.vy = entry->m48_direction;
    scratch->mA0.vz = worldmapRotation.vz;
    RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
    RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
    setGamestate182C(&entry->m28_position);
    gameState.m182C_vehiclePosition[3] = entry->m48_direction;
    switch (currentWorldmapMode) {
    case 2:
        entry->m20_subState = 0x24;
        break;
    case 3:
        entry->m20_subState = 0x28;
        entry->m7C = 0;
        break;
    case 4:
        entry->m20_subState = 0x30;
        entry->m7C = 0;
        break;
    case 5:
        entry->m20_subState = 0x34;
        entry->m7C = 0;
        break;
    }
    return result;
}

/* Noah: worldmapTaskYggdrasil.cpp, init of task 7 (Yggdrasil) in the alternate task set. */
s32 worldmap_taskYggdrasil_reinit(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    sWorldmapScratch* scratch = (sWorldmapScratch*)0x1F800000;
    s32 result;

    worldmapModels[0].m8_position = entry->m28_position;
    worldmapModels[0].m0_hidden = entry->m24_hidden;
    result = 1;
    if ((gameState.m1834_vehicleMode & 0x1FFF) == 0) {
        result = 3;
    }
    switch (worldMapGearMode) {
    case 1:
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        break;
    case 2:
    case 3:
        entry->m20_subState = 1;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        break;
    case 4:
    case 5:
        worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 1;
        break;
    case 6:
    case 7:
        worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 0;
        break;
    }
    scratch->mA0.vx = -(entry->m70 >> 12);
    scratch->mA0.vy = entry->m48_direction;
    scratch->mA0.vz = worldmapRotation.vz;
    RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
    RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
    return result;
}

/* Noah: worldmapTaskYggdrasil.cpp, Yggdrasil init variant (not referenced). */
s32 worldmap_taskYggdrasil_initSaved(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];

    getGamestate182C(&entry->m28_position);
    entry->m28_position.vy = 0x80000;
    entry->m48_direction = gameState.m182C_vehiclePosition[3];
    entry->m4A_stepScale = 0x20;
    entry->m74 = 3;
    entry->m68 = 0xFFD80000;
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m24_hidden = 0;
    entry->m60 = 0;
    entry->m64 = 0;
    entry->m6C = 0;
    entry->m20_subState = 8;
    worldmapRadarPosition = entry->m28_position;
    worldmapPlayerDirection = entry->m48_direction;
    worldmapModels[0].m8_position = entry->m28_position;
    return 1;
}

/* Noah: worldmap_taskYggdrasil_update (worldmapTaskYggdrasil.cpp). */
/* Enhanced, the 60 Hz world map end to end (w_80070CFC.c) */
s32 pcWm60OffTick(void);
s32 pcWm60Rate(void);
void pcWm60Moved(void);
s32 pcWm60TakeMove(void);
void pcWm60Live(s32 index);
void pcWm60LiveModel(s32 index);
void pcWm60YggStart(sWorldmapStateEntry* entry);
void pcWm60MidPosition(const VECTOR* a, const VECTOR* b, VECTOR* out);

s32 worldmap_taskYggdrasil_update(s32 index) {
    s32 result = 1;
    sWorldmapScratch* scratch = (sWorldmapScratch*)0x1F800000;
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 r;
    s32 d;
    s32 mode;
    s16 state;
    VECTOR unused;                    /* keeps the retail frame size */

    state = entry->m4_request;
    if (!pcWm60OffTick() && state != 4 && entry->m20_subState >= 2 && entry->m20_subState <= 4) {
        pcWm60YggStart(entry); /* the tick: flight and sea from where the tick before left them, the native frame whole */
    }
    if (pcWm60OffTick()) {
        /* Enhanced, 60 Hz end to end (w_80070CFC.c): flight and the sea every
         * frame from that vblank's input; requests, sequences, timers,
         * particles and the encounter count on the tick */
        if (entry->m20_subState <= 1) {
            goto pcWm60Done; /* parked */
        }
        if (entry->m20_subState > 4) {
            goto pcWm60Done;
        }
        pcWm60Live(index);
        pcWm60LiveModel(0);
        pcWm60LiveModel(1);
    } else if (state == 4) {
        entry->m4_request = 0;
        entry->m74++;
        if (worldmapNumActivePartyMembers == entry->m74) {
            changeWorldmapEntityState(8, 9);
            worldmapRadarPosition = entry->m28_position;
            worldmapPlayerDirection = entry->m48_direction;
            gameState.m1834_vehicleMode |= 0x4000;
            mode = gameState.m1834_vehicleMode & 0x1FFF;
            switch (mode) {
            case 1:
                entry->m20_subState = 0xC;
                worldMapGearMode = state;
                entry->m68 = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
                SCRATCH_A0.vx = -(entry->m70 >> 12);
                SCRATCH_A0.vy = entry->m48_direction;
                SCRATCH_A0.vz = worldmapRotation.vz;
                SCRATCH_A8.vx = entry->m28_position.vx >> 12;
                SCRATCH_A8.vy = entry->m28_position.vy >> 12;
                SCRATCH_A8.vz = entry->m28_position.vz >> 12;
                spawnWorldmapParticles(2, &SCRATCH_A8, &SCRATCH_A0);
                currentWorldmapDynamicCollisionSlot = 0;
                break;
            case 2:
                entry->m20_subState = 0xC;
                worldMapGearMode = 5;
                entry->m68 = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
                SCRATCH_A0.vx = -(entry->m70 >> 12);
                SCRATCH_A0.vy = entry->m48_direction;
                SCRATCH_A0.vz = worldmapRotation.vz;
                SCRATCH_A8.vx = entry->m28_position.vx >> 12;
                SCRATCH_A8.vy = entry->m28_position.vy >> 12;
                SCRATCH_A8.vz = entry->m28_position.vz >> 12;
                spawnWorldmapParticles(2, &SCRATCH_A8, &SCRATCH_A0);
                currentWorldmapDynamicCollisionSlot = 0;
                break;
            case 3:
                playBattleMusic(pMusic, 0, 0xF0);
                SCRATCH_A0.vx = entry->m28_position.vx >> 12;
                SCRATCH_A0.vy = entry->m28_position.vy >> 12;
                SCRATCH_A0.vz = entry->m28_position.vz >> 12;
                if (getWorldmapGroundType(&entry->m28_position) == 3) {
                    spawnWorldmapParticles(0x3E, &SCRATCH_A0, NULL);
                } else {
                    spawnWorldmapParticles(0x3D, &SCRATCH_A0, NULL);
                }
                changeWorldmapEntityState(9, 9);
                changeWorldmapEntityState(10, 9);
                changeWorldmapEntityState(11, 9);
                result = 2;
                entry->m20_subState = 8;
                entry->m2_waitTimer = 0x3C;
                worldMapGearMode = 7;
                currentWorldmapDynamicCollisionSlot = 0;
                break;
            }
            gameState.m22B1_isOnGear[0] = 1;
            if (gameState.m1D34_currentParty[1] != 0xFF) {
                gameState.m22B1_isOnGear[1] = 1;
            }
            if (gameState.m1D34_currentParty[2] != 0xFF) {
                gameState.m22B1_isOnGear[2] = 1;
            }
            initWorldmapEncounterTimers();
            worldmapCurrentExit = (sWorldmapExitDef*)-1;
            worldmapExitVar0 = -1;
            worldmapExitVar1 = -1;
        }
    }

    switch (entry->m20_subState) {
    case 0:
    case 1:
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) - 0x1000;
        addWorldmapDynamicCollisions(index, &entry->m28_position, 0x40, 0x400);
        break;
    case 2:
        switch (processYggdrasilInputs(entry)) {
        case 1:
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
            break;
        case 4:
            if (checkWorldmapPositionSub1_0_1(2, getWorldmapGroundType(&entry->m28_position)) != 0) {
                r = checkYggdrasilLandingPosition(&entry->m28_position, &entry->m38_step, 0x68000);
                if (r != -1) {
                    entry->m20_subState = 0x10;
                    entry->m78 = r;
                    entry->m68 = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
                    changeWorldmapEntityState(11, 10);
                    playBattleMusic(pMusic, 0, 0xF0);
                    entry->m7C = 1;
                    clearWorldmapParticles(0x3C);
                    clearWorldmapParticles(0x3F);
                    worldmapCurrentExit = (sWorldmapExitDef*)-1;
                    worldmapExitVar0 = -1;
                    worldmapExitVar1 = -1;
                    targetVehicleEntityIndex = 0;
                    adjustLocationAfterCollisionVar0 = 0;
                    currentWorldmapDynamicCollisionSlot = 0;
                }
            }
            break;
        default:
            r = checkWorldmapPositionYggdrasil(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m60 / pcWm60Rate(), worldMapGearMode);
            if (r == 0) {
                entry->m60 = 0;
                entry->m38_step.vz = 0;
                entry->m38_step.vx = 0;
            }
            if (r == 1) {
                processWorldmapDynamicCollisions(&scratch->m90_checkResult, 0x40, 0x20, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                if (adjustLocationAfterCollisionVar0 != 2) {
                    entry->m28_position = scratch->m90_checkResult;
                } else {
                    entry->m60 = 0;
                }
            }
            worldmapRadarPosition = entry->m28_position;
            worldmapPlayerDirection = entry->m48_direction;
            setupWorldmapExits(&entry->m28_position, 2);
            setupYggdrasilInternalExit();
            scratch->mA0.vx = -(entry->m70 >> 12);
            scratch->mA0.vy = entry->m48_direction;
            scratch->mA0.vz = worldmapRotation.vz;
            RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
            RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
            if (pcWm60OffTick()) {
                /* the wake and the spray: the tick's */
            } else if (abs(worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) - entry->m28_position.vy) < 0x60000 && entry->m60 != 0) {
                scratch->mA8.vx = entry->m28_position.vx >> 12;
                scratch->mA8.vy = entry->m28_position.vy >> 12;
                scratch->mA8.vz = entry->m28_position.vz >> 12;
                r = getWorldmapGroundType(&entry->m28_position);
                if (r == 2) {
                    spawnWorldmapParticles(0x3F, &scratch->mA8, &scratch->mA0);
                    clearWorldmapParticles(0x3C);
                } else if (r == 3) {
                    spawnWorldmapParticles(0x3C, &scratch->mA8, &scratch->mA0);
                    clearWorldmapParticles(0x3F);
                } else {
                    clearWorldmapParticles(0x3C);
                    clearWorldmapParticles(0x3F);
                }
            } else {
                clearWorldmapParticles(0x3C);
                clearWorldmapParticles(0x3F);
            }
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            if (!pcWm60OffTick()) currentWorldmapDynamicCollisionSlot = 0;
            break;
        }
        break;
    case 3:
        switch (processYggdrasilInputs_mode4(entry)) {
        case 1:
            if (worldmapCurrentExit->mE_type == 2) {
                goto takeOff;
            }
            goto stopLoop;
        case 4:
            r = checkYggdrasilLandingPosition(&entry->m28_position, &entry->m38_step, 0x68000);
            if (r != -1) {
                entry->m20_subState = 0x14;
                entry->m78 = r;
                entry->m68 = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
                scratch->mA0.vx = -(entry->m70 >> 12);
                scratch->mA0.vy = entry->m48_direction;
                scratch->mA0.vz = worldmapRotation.vz;
                scratch->mA8.vx = entry->m28_position.vx >> 12;
                scratch->mA8.vy = entry->m28_position.vy >> 12;
                scratch->mA8.vz = entry->m28_position.vz >> 12;
                spawnWorldmapParticles(2, &scratch->mA8, &scratch->mA0);
                clearWorldmapParticles(1);
                worldmapCurrentExit = (sWorldmapExitDef*)-1;
                worldmapExitVar0 = -1;
                worldmapExitVar1 = -1;
            }
            break;
        default:
            {
                s32 a = entry->m48_direction - gameState.m182C_vehiclePosition[3];
                if (ABS(a) > 0x800) {
                    if (a < 0) {
                        a += 0x1000;
                    } else {
                        a -= 0x1000;
                    }
                }
                if (ABS(a) > 0x80) {
                    if (a < 0) {
                        entry->m48_direction = gameState.m182C_vehiclePosition[3] - 0x80;
                    } else {
                        entry->m48_direction = gameState.m182C_vehiclePosition[3] + 0x80;
                    }
                }
            }
            entry->m48_direction &= 0xFFF;
            r = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
            if (r == 0) {
                entry->m38_step = scratch->m90_checkResult;
                r = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
                if (r == 0) {
                    entry->m38_step.vz = 0;
                    entry->m38_step.vx = 0;
                }
            }
            if (r == 1) {
                scratch->m90_checkResult.vy = worldmapGetAltitudeFor2dPoint(scratch->m90_checkResult.vx, scratch->m90_checkResult.vz) + 0x18000;
                processWorldmapDynamicCollisions(&scratch->m90_checkResult, 0x40, 0x20, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                if (adjustLocationAfterCollisionVar0 != 2) {
                    if (pcWm60OffTick()) {
                        pcWm60MidPosition(&entry->m28_position, &scratch->m90_checkResult, &entry->m28_position); /* half the native step */
                    } else {
                        entry->m28_position = scratch->m90_checkResult;
                    }
                } else {
                    entry->m60 = 0;
                }
            }
            worldmapRadarPosition = entry->m28_position;
            worldmapPlayerDirection = entry->m48_direction;
            setupWorldmapExits(&entry->m28_position, 2);
            scratch->mA0.vx = -(entry->m70 >> 12);
            scratch->mA0.vy = entry->m48_direction;
            scratch->mA0.vz = worldmapRotation.vz;
            RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
            RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
            if (pcWm60OffTick()) {
                entry->m38_step.vz = 0; /* the wake and the shore: the tick's */
                entry->m38_step.vy = 0;
                entry->m38_step.vx = 0;
                break;
            }
            if ((entry->m38_step.vx | entry->m38_step.vz) != 0) {
                scratch->mA8.vx = entry->m28_position.vx >> 12;
                scratch->mA8.vy = entry->m28_position.vy >> 12;
                scratch->mA8.vz = entry->m28_position.vz >> 12;
                spawnWorldmapParticles(1, &scratch->mA8, &scratch->mA0);
            } else {
                clearWorldmapParticles(1);
            }
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            if (getWorldmapGroundType(&entry->m28_position) != 3) {
                clearWorldmapParticles(1);
                spawnWorldmapParticles(3, &scratch->mA8, &scratch->mA0);
                entry->m20_subState = 4;
                entry->m22_timer = 0x28;
            }
            break;
        }
        break;
    case 4:
        if (processYggdrasilInputs_mode4(entry) == 1) {
            if (worldmapCurrentExit->mE_type == 2) {
            takeOff:
                entry->m20_subState = 0x20;
                entry->m68 = 0x30000;
                scratch->mA8.vx = entry->m28_position.vx >> 12;
                scratch->mA8.vy = entry->m28_position.vy >> 12;
                scratch->mA8.vz = entry->m28_position.vz >> 12;
                spawnWorldmapParticles(2, &scratch->mA8, &scratch->mA0);
                break;
            }
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
            break;
        }
        {
            s32 a = entry->m48_direction - gameState.m182C_vehiclePosition[3];
            if (ABS(a) > 0x800) {
                if (a < 0) {
                    a += 0x1000;
                } else {
                    a -= 0x1000;
                }
            }
            if (ABS(a) > 0x80) {
                if (a < 0) {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] - 0x80;
                } else {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] + 0x80;
                }
            }
        }
        entry->m48_direction &= 0xFFF;
        r = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
        if (r == 0) {
            entry->m38_step = scratch->m90_checkResult;
            r = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
            if (r == 0) {
                entry->m38_step.vz = 0;
                entry->m38_step.vx = 0;
            }
        }
        if (r == 1) {
            scratch->m90_checkResult.vy = getWorldmapElevationWithWater(scratch->m90_checkResult.vx, scratch->m90_checkResult.vz) + 0x18000;
            processWorldmapDynamicCollisions(&scratch->m90_checkResult, 0x40, 0x20, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
            if (adjustLocationAfterCollisionVar0 != 2) {
                if (pcWm60OffTick()) {
                    pcWm60MidPosition(&entry->m28_position, &scratch->m90_checkResult, &entry->m28_position); /* half the native step */
                } else {
                    entry->m28_position = scratch->m90_checkResult;
                }
                if ((entry->m38_step.vx | entry->m38_step.vz) != 0) {
                    if (pcWm60OffTick()) {
                        pcWm60Moved(); /* the encounter count: the tick's */
                    } else {
                        pcWm60TakeMove();
                        updateWorldmapEncounterTimers();
                    }
                }
            } else {
                entry->m28_position.vy = getWorldmapElevationWithWater(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
                entry->m60 = 0;
            }
        } else {
            entry->m28_position.vy = getWorldmapElevationWithWater(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        setupWorldmapExits(&entry->m28_position, 2);
        setupYggdrasilInternalExit();
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
        if (pcWm60OffTick()) {
            entry->m38_step.vz = 0; /* the timer, the wake and the shore: the tick's */
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            break;
        }
        if (--entry->m22_timer > 0) {
            scratch->mA8.vx = entry->m28_position.vx >> 12;
            scratch->mA8.vy = entry->m28_position.vy >> 12;
            scratch->mA8.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(3, &scratch->mA8, &scratch->mA0);
        } else {
            entry->m22_timer = 0;
            clearWorldmapParticles(3);
        }
        if ((entry->m38_step.vx | entry->m38_step.vz) != 0) {
            scratch->mA8.vx = entry->m28_position.vx >> 12;
            scratch->mA8.vy = entry->m28_position.vy >> 12;
            scratch->mA8.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(4, &scratch->mA8, &scratch->mA0);
        } else {
            clearWorldmapParticles(4);
            entry->m22_timer = 0;
        }
        entry->m38_step.vz = 0;
        entry->m38_step.vy = 0;
        entry->m38_step.vx = 0;
        if (getWorldmapGroundType(&scratch->m90_checkResult) == 3) {
            clearWorldmapParticles(3);
            clearWorldmapParticles(4);
            entry->m20_subState = 3;
        }
        currentWorldmapDynamicCollisionSlot = 0;
        break;
    case 8:
        entry->m28_position.vy -= 0x8000;
        if (entry->m28_position.vy <= -0x1E0000) {
            entry->m28_position.vy = -0x1E0000;
            if (entry->m4_request == 11) {
                entry->m64 = 0;
                entry->m20_subState = 2;
                entry->m4_request = 0;
                startWorldmapMusicFromBuffer(worldmapSound2, worldmapFile7);
            }
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0xC:
        entry->m28_position.vy += 0x800;
        if (entry->m28_position.vy >= entry->m68) {
            entry->m28_position.vy = (s32)entry->m68;
            entry->m20_subState = 3;
            worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 1;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x10:
        entry->m28_position.vy += 0x8000;
        if (entry->m28_position.vy >= entry->m68) {
            entry->m28_position.vy = (s32)entry->m68;
            entry->m20_subState = 1;
            startWorldmapMusicFromBuffer(worldmapSound1, worldmapFile5);
            changeWorldmapEntityState(8, 10);
            changeWorldmapEntityState(9, 10);
            changeWorldmapEntityState(10, 10);
            changeWorldmapEntityState(4, 3);
            changeWorldmapEntityState(1, 3);
            gameState.m22B1_isOnGear[0] = 1;
            if (gameState.m1D34_currentParty[1] != 0xFF) {
                changeWorldmapEntityState(5, 3);
                changeWorldmapEntityState(2, 3);
                gameState.m22B1_isOnGear[1] = 1;
            }
            if (gameState.m1D34_currentParty[2] != 0xFF) {
                changeWorldmapEntityState(6, 3);
                changeWorldmapEntityState(3, 3);
                gameState.m22B1_isOnGear[2] = 1;
            }
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            entry->m74 = 0;
            gameState.m1834_vehicleMode &= 0x3FFF;
            clearWorldmapParticles(0);
            clearWorldmapParticles(1);
            initWorldmapEncounterTimers();
        } else if (entry->m28_position.vy > -0x200000 && entry->m7C != 0) {
            entry->m7C = 0;
            scratch->mA0.vx = entry->m28_position.vx >> 12;
            scratch->mA0.vy = entry->m68 >> 12;
            scratch->mA0.vz = entry->m28_position.vz >> 12;
            if (getWorldmapGroundType(&entry->m28_position) == 3) {
                spawnWorldmapParticles(0x3E, &scratch->mA0, NULL);
            } else {
                spawnWorldmapParticles(0x3D, &scratch->mA0, NULL);
            }
        }
        worldmapRadarPosition.vy = entry->m28_position.vy;
        break;
    case 0x14:
        entry->m28_position.vy -= 0x800;
        if (entry->m28_position.vy <= entry->m68) {
            entry->m28_position.vy = (s32)entry->m68;
            entry->m20_subState = 1;
            changeWorldmapEntityState(8, 10);
            changeWorldmapEntityState(1, 3);
            changeWorldmapEntityState(4, 3);
            gameState.m22B1_isOnGear[0] = 1;
            if (gameState.m1D34_currentParty[1] != 0xFF) {
                changeWorldmapEntityState(2, 3);
                changeWorldmapEntityState(5, 3);
                gameState.m22B1_isOnGear[1] = 1;
            }
            if (gameState.m1D34_currentParty[2] != 0xFF) {
                changeWorldmapEntityState(3, 3);
                changeWorldmapEntityState(6, 3);
                gameState.m22B1_isOnGear[2] = 1;
            }
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            entry->m74 = 0;
            gameState.m1834_vehicleMode &= 0x3FFF;
            worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 0;
            initWorldmapEncounterTimers();
        }
        worldmapRadarPosition.vy = entry->m28_position.vy;
        break;
    case 0x20:
        entry->m28_position.vy += 0x2000;
        if (entry->m28_position.vy >= entry->m68) {
            entry->m28_position.vy = (s32)entry->m68;
            clearWorldmapParticles(2);
            worldmapModels[0].m0_hidden = worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 1;
            entry->m20_subState++;
            scratch->m0.vx = 0x4B40000;
            scratch->m0.vz = 0x1C00000;
            scratch->m0.vy = (s32)entry->m68;
            selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
            entry->m50 = scratch->m0.vx >> 12;
            entry->m54 = scratch->m0.vz >> 12;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x21:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState++;
        }
        entry->m28_position.vy = (s32)entry->m68;
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x22:
        entry->m20_subState = 0x40;
    stopLoop:
        continueWorldmapLoop = 0;
        exitWorldMapMode = 0;
        break;
    case 0x24:
        entry->m28_position.vy = 0x30000;
        entry->m20_subState++;
        scratch->m0.vx = 0x4937000;
        scratch->m0.vz = 0x21E2000;
        scratch->m0.vy = 0x30000;
        selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
        entry->m50 = scratch->m0.vx >> 12;
        entry->m54 = scratch->m0.vz >> 12;
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        worldmapModels[0].m0_hidden = worldmapModels[1].m0_hidden = worldmapModels[2].m0_hidden = worldmapModels[3].m0_hidden = 1;
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
        break;
    case 0x25:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m28_position.vy = 0x30000;
            entry->m20_subState++;
            entry->m68 = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
            scratch->mA8.vx = entry->m28_position.vx >> 12;
            scratch->mA8.vy = entry->m68 >> 12;
            scratch->mA8.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(2, &scratch->mA8, &scratch->mA0);
            worldmapModels[0].m0_hidden = 0;
        }
        entry->m28_position.vy = 0x30000;
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x26:
        entry->m28_position.vy -= 0x2000;
        if (entry->m28_position.vy <= entry->m68) {
            entry->m28_position.vy = (s32)entry->m68;
            clearWorldmapParticles(2);
            entry->m20_subState = 3;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x28:
        entry->m20_subState++;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
        scratch->m0.vx = yggdrasilScriptedLandPath[entry->m7C].vx << 12;
        scratch->m0.vz = yggdrasilScriptedLandPath[entry->m7C].vz << 12;
        selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
        entry->m50 = scratch->m0.vx >> 12;
        entry->m54 = scratch->m0.vz >> 12;
        entry->m78 = entry->m48_direction;
        if (++entry->m7C == 2) {
            changeWorldmapEntityState(9, 0xF);
        }
        if (entry->m7C == 4) {
            entry->m22_timer = 0x40;
            entry->m20_subState++;
            changeWorldmapEntityState(0, 0xD);
            worldmapFadePolyAbr = 2;
            worldmapFadePolyStep = 4;
        }
    case 0x29:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState--;
        }
    turnFly:
        {
            s32 a = entry->m78 - gameState.m182C_vehiclePosition[3];
            if (ABS(a) > 0x800) {
                if (a < 0) {
                    a += 0x1000;
                } else {
                    a -= 0x1000;
                }
            }
            if (ABS(a) > 0x10) {
                if (a < 0) {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] - 0x10;
                } else {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] + 0x10;
                }
            }
        }
        entry->m48_direction &= 0xFFF;
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        scratch->mA8.vx = entry->m28_position.vx >> 12;
        scratch->mA8.vy = entry->m28_position.vy >> 12;
        scratch->mA8.vz = entry->m28_position.vz >> 12;
        spawnWorldmapParticles(1, &scratch->mA8, &scratch->mA0);
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x2A:
        if (--entry->m22_timer < 0) {
            gameState.m231A_fieldID = 0x50;
            gameState.m2320_worldmapMode = 1;
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
            worldmapExitToFieldVar0 = 1;
            gameState.m231C_cameraYaw = worldmapRotation.vy;
        }
        goto turnFly;
    case 0x30:
        entry->m20_subState++;
        entry->m28_position.vy = getWorldmapElevationWithWater(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
        scratch->m0.vx = yggdrasilScriptedSeaPath[entry->m7C].vx << 12;
        scratch->m0.vz = yggdrasilScriptedSeaPath[entry->m7C].vz << 12;
        selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
        entry->m50 = scratch->m0.vx >> 12;
        entry->m54 = scratch->m0.vz >> 12;
        entry->m78 = entry->m48_direction;
        if (++entry->m7C == 2) {
            changeWorldmapEntityState(9, 0x10);
        }
        if (entry->m7C == 7) {
            entry->m22_timer = 0x40;
            entry->m20_subState++;
            changeWorldmapEntityState(0, 0xD);
            worldmapFadePolyAbr = 2;
            worldmapFadePolyStep = 4;
        }
    case 0x31:
    turnSail:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState--;
        }
        {
            s32 a = entry->m78 - gameState.m182C_vehiclePosition[3];
            if (ABS(a) > 0x800) {
                if (a < 0) {
                    a += 0x1000;
                } else {
                    a -= 0x1000;
                }
            }
            if (ABS(a) > 0x10) {
                if (a < 0) {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] - 0x10;
                } else {
                    entry->m48_direction = gameState.m182C_vehiclePosition[3] + 0x10;
                }
            }
        }
        entry->m48_direction &= 0xFFF;
        entry->m28_position.vy = getWorldmapElevationWithWater(entry->m28_position.vx, entry->m28_position.vz) + 0x18000;
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        scratch->mA8.vx = entry->m28_position.vx >> 12;
        scratch->mA8.vy = entry->m28_position.vy >> 12;
        scratch->mA8.vz = entry->m28_position.vz >> 12;
        spawnWorldmapParticles(4, &scratch->mA8, &scratch->mA0);
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x32:
        if (--entry->m22_timer < 0) {
            gameState.m231A_fieldID = 0x120;
            gameState.m2320_worldmapMode = 6;
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
            worldmapExitToFieldVar0 = 1;
            gameState.m231C_cameraYaw = worldmapRotation.vy;
        }
        goto turnSail;
    case 0x34:
        entry->m20_subState++;
        changeWorldmapEntityState(10, 0x11);
        entry->m28_position.vy = -0x100000;
        entry->m48_direction = 0x200;
        entry->m38_step.vx = getAngleCos(0x200);
        entry->m38_step.vz = -getAngleSin(entry->m48_direction);
        entry->m22_timer = 0x5A;
        entry->m78 = 0;
        scratch->mA0.vx = -(entry->m70 >> 12);
        scratch->mA0.vy = entry->m48_direction;
        scratch->mA0.vz = worldmapRotation.vz;
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[0].m20_rotationMatrix);
        RotMatrixYXZ(&scratch->mA0, &worldmapModels[1].m20_rotationMatrix);
    case 0x35:
        if (--entry->m22_timer < 0) {
            entry->m22_timer = 0xB4;
            entry->m20_subState++;
            changeWorldmapEntityState(9, 0x11);
        }
        goto sink;
    case 0x36:
        if (--entry->m22_timer < 0) {
            entry->m22_timer = 0x78;
            entry->m20_subState++;
            playSoundEffectInFreeSlot((fieldSeq->m14_id << 16) | 0x38);
        }
    sink:
        entry->m28_position.vx += entry->m38_step.vx * 12;
        entry->m28_position.vz += entry->m38_step.vz * 12;
        checkWorldmapPositionSub0(&entry->m28_position);
        entry->m28_position.vy += 0x1000;
        if (entry->m28_position.vy > -0x10000 && entry->m28_position.vy < 0x10000) {
            scratch->mA0.vx = -(entry->m70 >> 12);
            scratch->mA0.vy = entry->m48_direction;
            scratch->mA0.vz = worldmapRotation.vz;
            scratch->mA8.vx = entry->m28_position.vx >> 12;
            scratch->mA8.vy = entry->m28_position.vy >> 12;
            scratch->mA8.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(0x3F, &scratch->mA8, &scratch->mA0);
        } else {
            clearWorldmapParticles(0x3F);
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x37:
        if (--entry->m22_timer < 0) {
            entry->m22_timer = 0x5A;
            entry->m20_subState++;
            clearWorldmapParticles(0x2B);
        }
        entry->m28_position.vx += entry->m38_step.vx * 8;
        entry->m28_position.vz += entry->m38_step.vz * 8;
        checkWorldmapPositionSub0(&entry->m28_position);
        entry->m28_position.vy += 0x1000;
        if (entry->m20_subState == 0x38) {
            clearWorldmapParticles(0x2B);
        } else {
            scratch->mA0.vx = -(entry->m70 >> 12);
            scratch->mA0.vy = entry->m48_direction;
            scratch->mA0.vz = worldmapRotation.vz;
            scratch->mA8.vx = entry->m28_position.vx >> 12;
            scratch->mA8.vy = 0;
            scratch->mA8.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(0x2B, &scratch->mA8, &scratch->mA0);
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x38:
        if (--entry->m22_timer < 0) {
            entry->m22_timer = 0x80;
            entry->m20_subState++;
            changeWorldmapEntityState(0, 0xD);
            worldmapFadePolyAbr = 2;
            worldmapFadePolyStep = 4;
        }
        goto drift;
    case 0x39:
        if (--entry->m22_timer < 0) {
            gameState.m231A_fieldID = 0x1F0;
            continueWorldmapLoop = 0;
            exitWorldMapMode = 0;
            gameState.m2320_worldmapMode = 0;
            worldmapExitToFieldVar0 = 1;
            gameState.m231C_cameraYaw = worldmapRotation.vy;
        }
    drift:
        entry->m28_position.vx += entry->m38_step.vx * 8;
        entry->m28_position.vz += entry->m38_step.vz * 8;
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x40:
        break;
    }
pcWm60Done:
    worldmapModels[0].m8_position.vx = worldmapModels[1].m8_position.vx = entry->m28_position.vx >> 12;
    worldmapModels[0].m8_position.vy = worldmapModels[1].m8_position.vy = entry->m28_position.vy >> 12;
    worldmapModels[0].m8_position.vz = worldmapModels[1].m8_position.vz = entry->m28_position.vz >> 12;
    setGamestate182C(&entry->m28_position);
    gameState.m182C_vehiclePosition[3] = entry->m48_direction;
    switch (entry->m20_subState) {
    case 2:
    case 8:
    case 0x10:
        addWorldmapTrackMark(2, &entry->m28_position);
        break;
    }
    return result;
}

/* Noah: worldmap_taskYggdrasilSubModels_init (worldmapTaskYggdrasil.cpp). */
s32 worldmap_taskYggdrasilSubModels_init(s32 index) {
    sWorldmapStateEntry* entry;

    attachWorldmapModels(0, 2);
    attachWorldmapModels(0, 3);
    entry = &gWorldmapState->m0_entries[index];
    entry->m20_subState = 0;
    entry->m28_position = worldmapModels[2].m8_position;
    entry->m38_step = worldmapModels[3].m8_position;
    entry->m50 = 0;
    entry->m54 = 0x40;
    if (worldMapGearMode < 8) {
        if (worldMapGearMode >= 4) {
            entry->m20_subState = 3;
            entry->m5C = 0x80;
            entry->m58 = 0x80;
        }
    }
    return 1;
}

/* Noah: worldmapTaskYggdrasil.cpp, init of task 8 (sub models) in the alternate task set. */
s32 worldmap_taskYggdrasilSubModels_reinit(s32 index) {
    attachWorldmapModels(0, 2);
    attachWorldmapModels(0, 3);
    return 1;
}
