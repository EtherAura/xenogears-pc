/*
 * Party-member and gear tasks, dynamic collisions, follow-leader data
 * (Noah: worldmapPartyMember.cpp, worldmapPartyGear.cpp, worldmapPlayerGear.cpp,
 * worldmapDynamicCollisions.cpp).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"

extern sWorldmapState* gWorldmapState;
extern s32 worldMapGearMode;
extern sWorldmapDynamicCollision worldmapDynamicCollisions[32];
extern s16 currentWorldmapDynamicCollisionSlot;
extern AnimationBundle* worldmapPartySprites[3];
extern AnimationBundle* worldmapPartyGearSprites[3];
extern s16 worldmapGearSpriteClutX[4];                      /* Noah: SHORT_ARRAY_worldmap__8009b18c (gear clut x) */
extern s16 worldmapGearSpriteClutY[4];                      /* gear clut y */
extern s16 worldmapGearSpriteVramX[4];                      /* gear vram x */
extern s16 worldmapGearSpriteVramY[4];                      /* gear vram y */
extern VECTOR worldmapPosition;
extern u32 worldmapGamestate1824;
extern VECTOR worldmapRadarPosition;
extern s16 worldmapPlayerDirection;                         /* Noah: worldmapVar_8009d52c */
extern s16 worldmapFollowLeaderDataIndex;
extern sWorldmapFollowLeader worldmapFollowLeaderData[32];
extern s32 worldmapNumActivePartyMembers;
extern s32 continueWorldmapLoop;
extern s32 exitWorldMapMode;
extern u8 adjustLocationAfterCollisionVar0;
extern u8 targetVehicleEntityIndex;
extern u16 D_8009B180[];                       /* Noah: SHORT_ARRAY_worldmap__8009b180 */

SpriteActor* createSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
s32 createWorldmapGear(sWorldmapStateEntry* entry, s32 index);
void getGamestate182C(VECTOR* arg0);
s32 ratan2(s32 y, s32 x);
s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z);
void checkWorldmapPositionSub0(VECTOR* position);
void adjustWorldModelLocation(VECTOR* delta);
s16 getWorldmapGroundType(VECTOR* position);
void spawnWorldmapParticles(s32 type, SVECTOR* position, SVECTOR* rotation);
void clearWorldmapParticles(s32 type);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void addWorldmapTrackMark(s16 type, VECTOR* position); /* Noah: FUN_Worldmap__80074794 (shadow) */
void updateWorldmapEncounterTimers(void);                      /* Noah: followLeaderPostProcess */
s32 worldmapUpdatePlayerGearsControls(sWorldmapStateEntry* entry);
s16 checkWorldmapPositionSub1_0_1(s16 a, s16 b);
void selectYggdrasilExitDirection(VECTOR* from, VECTOR* to, VECTOR* step, s16* direction);
s32 setupWorldmapExits(VECTOR* position, s32 mode);
s32 checkWorldmapPosition(VECTOR* position, VECTOR* step, VECTOR* output, s32 stepScale, s32 mode);
s32 changeWorldmapEntityState(s32 index, s32 state);

/* Noah: worldmapPartyMember.cpp, init of task 3 (party member 2) in the alternate task set. */
s32 worldmapMode0_task_partyMember2_reinit(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result = 1;
    if (gameState.m1D34_currentParty[2] != 0xFF) {
        entry->m4C_sprite = &createSpriteActor(worldmapPartySprites[2], 0x120, 0x1E0, 0x160, 0x100, 0x40)->m0_spriteActorCore;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
        setGraphicEntityScale(entry->m4C_sprite, 0x1800);
        entry->m4C_sprite->m3C_x2 = 0;
    } else {
        result = 3;
    }
    return result;
}

/* Noah: worldmapPartyMember.cpp, like worldmapMode0_task_partyMember2_init (not referenced). */
s32 worldmapMode0_task_partyMember2_initSaved(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s16 direction;

    entry->m4C_sprite = &createSpriteActor(worldmapPartySprites[2], 0x120, 0x1E0, 0x160, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
    setGraphicEntityScale(entry->m4C_sprite, 0x1800);
    entry->m4C_sprite->m3C_x2 = 0;
    entry->m28_position.vx = (u16)gameState.m1820_worldmapPosition[0] << 12;
    entry->m28_position.vz = (u16)gameState.m1820_worldmapPosition[1] << 12;
    entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
    direction = gameState.m1820_worldmapPosition[2];
    entry->m24_hidden = 1;
    entry->m4A_stepScale = 8;
    entry->m20_subState = 2;
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m58 = 0x1E;
    entry->m48_direction = direction;
    return 1;
}

/* Noah: stepGearAwayFromYggdrasil (worldmapPlayerGear.cpp). */
s32 stepGearAwayFromYggdrasil(sWorldmapStateEntry* entry) {
    s32 result;
    s32 x;

    x = entry->m28_position.vx;
    result = 0;
    if (abs((s32)entry->m50 - (x >> 12)) >= 5) {
        entry->m28_position.vx = x + entry->m38_step.vx * (entry->m4A_stepScale / 2);
    } else {
        result = 1;
    }
    x = entry->m28_position.vz;
    if (abs((s32)entry->m54 - (x >> 12)) >= 5) {
        entry->m28_position.vz = x + entry->m38_step.vz * (entry->m4A_stepScale / 2);
    } else {
        result |= 2;
    }
    checkWorldmapPositionSub0(&entry->m28_position);
    entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
    return result;
}

/* Noah: addWorldmapDynamicCollisions (worldmapDynamicCollisions.cpp). */
void addWorldmapDynamicCollisions(s16 arg0, VECTOR* arg1, s16 arg2, s16 arg3) {
    sWorldmapDynamicCollision* slot = &worldmapDynamicCollisions[currentWorldmapDynamicCollisionSlot];
    slot->m0_entityIndex = arg0;
    slot->m4_position[0] = arg1->vx;
    slot->m4_position[1] = arg1->vy;
    slot->m4_position[2] = arg1->vz;
    slot->m10_height = arg3;
    slot->m14_radius = arg2;
    currentWorldmapDynamicCollisionSlot = (currentWorldmapDynamicCollisionSlot + 1) & 0x1F;
}

/* Noah: processWorldmapDynamicCollisions (worldmapDynamicCollisions.cpp). */
void processWorldmapDynamicCollisions(VECTOR* position, s32 radius, s32 height, u8* outType, u8* outIndex) {
    s32 i;
    s32 top;
    s32 bottom;
    s32 dist;
    sWorldmapDynamicCollision* entry;
    VECTOR delta;
    VECTOR delta2;                 /* unused: keeps the retail frame size */

    *outType = 0;
    *outIndex = 0;
    entry = worldmapDynamicCollisions;
    if (currentWorldmapDynamicCollisionSlot > 0) {
    for (i = 0; i < currentWorldmapDynamicCollisionSlot; i++, entry++) {
        top = position->vy;
        if (top < entry->m4_position[1]) {
            bottom = top - (height << 12);
            top = entry->m4_position[1];
        } else {
            bottom = entry->m4_position[1] - (entry->m10_height << 12);
        }
        if ((top - bottom) >> 12 < height + entry->m10_height) {
            delta.vx = (entry->m4_position[0] - position->vx) >> 12;
            delta.vz = (entry->m4_position[2] - position->vz) >> 12;
            adjustWorldModelLocation(&delta);
            dist = SquareRoot0(delta.vx * delta.vx + delta.vz * delta.vz);
            if (dist < entry->m14_radius + radius) {
                *outType = 2;
                *outIndex = entry->m0_entityIndex;
                return;
            }
            if (dist < entry->m14_radius + radius + 0x10) {
                *outType = 1;
                *outIndex = entry->m0_entityIndex;
                return;
            }
        }
    }
    }
}

/* Noah: spawnParticlesForTerrainType3 (worldmapParticles.cpp). */
void spawnParticlesForTerrainType3(s32 type, sWorldmapStateEntry* entry, sWorldmapScratch* scratch) {
    if (getWorldmapGroundType(&entry->m28_position) == 3) {
        scratch->mA0.vx = entry->m28_position.vx >> 12;
        scratch->mA0.vy = entry->m28_position.vy >> 12;
        scratch->mA0.vz = entry->m28_position.vz >> 12;
        scratch->mA8.vz = 0;
        scratch->mA8.vx = 0;
        scratch->mA8.vy = entry->m5C;
        spawnWorldmapParticles(type, &scratch->mA0, &scratch->mA8);
    } else {
        clearWorldmapParticles(type);
    }
}

/* Noah: createGearSpriteActor (worldmapPlayerGear.cpp). */
void createGearSpriteActor(sWorldmapStateEntry* entry, s32 index) {
    SpriteActorCore* actor;

    actor = &createSpriteActor(worldmapPartyGearSprites[index], worldmapGearSpriteClutX[index], worldmapGearSpriteClutY[index], worldmapGearSpriteVramX[index], worldmapGearSpriteVramY[index], 0x40)->m0_spriteActorCore;
    entry->m4C_sprite = actor;
    if (gameState.m22B1_isOnGear[index] == 1) {
        spriteActorSetPlayingAnimation(actor, 0);
    } else {
        spriteActorSetPlayingAnimation(actor, 3);
    }
    setGraphicEntityScale(entry->m4C_sprite, 0x2000);
    entry->m4C_sprite->m3C_x2 = 0;
}

/* Noah: createWorldmapGear (worldmapPlayerGear.cpp). */
s32 createWorldmapGear(sWorldmapStateEntry* entry, s32 index) {
    s32 result = 1;
    u32 state;

    state = gameState.m1D34_currentParty[index] != 0xFF;
    if ((gameState.m1930_fieldVarsBackup[index * 3 + 0x15] & 0x3FFF) >= 0x400) {
        state |= 2;
    }
    if (gameState.m22B1_isOnGear[index] == 1) {
        state |= 4;
    }
    switch (state) {
    case 0:
    case 2:
    case 4:
    case 6:
    noGear:
        result = 3;
        goto hidden;
    case 1:
        if (gameState.m26C_party[gameState.m1D34_currentParty[index]].mA0_gearNum == 0xFF) {
            goto noGear;
        }
        createGearSpriteActor(entry, index);
    hidden:
        entry->m24_hidden = 1;
        entry->m28_position.vy = 0;
        entry->m28_position.vz = 0;
        entry->m28_position.vx = 0;
        break;
    case 3:
        createGearSpriteActor(entry, index);
        entry->m24_hidden = 0;
        entry->m28_position.vx = (u16)gameState.m1930_fieldVarsBackup[index * 3 + 0x16] << 12;
        entry->m28_position.vz = (u16)gameState.m1930_fieldVarsBackup[index * 3 + 0x17] << 12;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        break;
    case 5:
    case 7:
        createGearSpriteActor(entry, index);
        entry->m24_hidden = 0;
        entry->m28_position.vx = worldmapPosition.vx;
        entry->m28_position.vz = worldmapPosition.vz;
        entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
        gameState.m1930_fieldVarsBackup[index * 3 + 0x15] = 0x400;
        break;
    }
    return result;
}

s32 worldmapMode0_taskPlayerGear_init(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result;
    s32 i;
    sWorldmapFollowLeader* p;
    result = createWorldmapGear(entry, 0);
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m48_direction = gameState.m1820_worldmapPosition[3];
    entry->m4A_stepScale = 0xC;
    entry->m5C = entry->m48_direction;
    switch (worldMapGearMode) {
    case 1:
    case 2:
    case 3:
        if (gameState.m22B1_isOnGear[0] == 1) {
            entry->m20_subState = 1;
            entry->m28_position.vx = worldmapPosition.vx;
            entry->m28_position.vz = worldmapPosition.vz;
            entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
            entry->m48_direction = worldmapGamestate1824;
            worldmapRadarPosition = entry->m28_position;
            worldmapPlayerDirection = entry->m48_direction;
            worldmapFollowLeaderDataIndex = 0;
            for (i = 0, p = worldmapFollowLeaderData; i < 32; i++, p++) {
                p->m0_position = entry->m28_position;
                p->m10_direction = entry->m48_direction;
            }
        }
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        entry->m20_subState = 2;
        entry->m24_hidden = 1;
        break;
    }
    gameState.m1930_fieldVarsBackup[0x16] = entry->m28_position.vx >> 12;
    gameState.m1930_fieldVarsBackup[0x17] = entry->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[3] = entry->m48_direction;
    return result;
}

/* Noah: worldmapPlayerGear.cpp, init of task 4 (player gear) in the alternate task set. */
s32 worldmapMode0_taskPlayerGear_reinit(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result = createWorldmapGear(entry, 0);
    if (worldMapGearMode > 0) {
        if (worldMapGearMode < 4) {
        } else if (worldMapGearMode < 8) {
            entry->m24_hidden = 1;
        }
    }
    return result;
}

/* Noah: worldmapPlayerGear.cpp / worldmapPartyGear.cpp, gear 0 init without follow logic (not referenced). */
s32 worldmapMode0_taskPlayerGear_initSaved(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s16 direction;

    entry->m4C_sprite = &createSpriteActor(worldmapPartyGearSprites[0], 0x100, 0x1FD, 0x140, 0x140, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
    setGraphicEntityScale(entry->m4C_sprite, 0x2000);
    entry->m4C_sprite->m3C_x2 = 0;
    entry->m28_position.vx = (u16)gameState.m1930_fieldVarsBackup[0x16] << 12;
    entry->m28_position.vz = (u16)gameState.m1930_fieldVarsBackup[0x17] << 12;
    entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
    direction = gameState.m1820_worldmapPosition[3];
    entry->m24_hidden = 1;
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m4A_stepScale = 0xC;
    entry->m48_direction = direction;
    return 1;
}

/* Noah: worldmapMode0_taskPlayerGear_update (worldmapPlayerGear.cpp). */
s32 worldmapMode0_taskPlayerGear_update(s32 index) {
    s32 result = 1;
    sWorldmapScratch* scratch = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 i;
    sWorldmapFollowLeader* p;
    VECTOR unused[2];                 /* keeps the retail frame size */

    switch (entry->m4_request) {
    case 4:
        entry->m20_subState = 0x10;
        entry->m4_request = 0;
        entry->m58 = 1;
        gameState.m22B1_isOnGear[0] = 1;
        break;
    case 3:
        entry->m4_request = 0;
        entry->m20_subState = 0x30;
        break;
    case 7:
        entry->m4_request = 0;
        entry->m58++;
        if (worldmapNumActivePartyMembers == entry->m58) {
            entry->m20_subState = 1;
            worldMapGearMode = 2;
            currentWorldmapDynamicCollisionSlot = 0;
        }
        break;
    case 8:
        entry->m4_request = 0;
        entry->m20_subState = 0x18;
        break;
    }

    switch (entry->m20_subState) {
    case 0:
    case 1:
        if (gameState.m22B1_isOnGear[0] == 1) {
            switch (worldmapUpdatePlayerGearsControls(entry)) {
            case 1:
                continueWorldmapLoop = 0;
                exitWorldMapMode = 0;
                break;
            case 3:
                entry->m20_subState = 8;
                break;
            case 4:
                if (checkWorldmapPositionSub1_0_1(1, getWorldmapGroundType(&entry->m28_position)) != 0) {
                    entry->m20_subState = 0x20;
                }
                break;
            default:
                if ((entry->m38_step.vx == 0) & (entry->m38_step.vy == 0) & (entry->m38_step.vz == 0)) {
                    if ((s8)entry->m4C_sprite->mAF_animationId != 0) {
                        spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
                        clearWorldmapParticles(0x2C);
                    }
                } else {
                    if ((s8)entry->m4C_sprite->mAF_animationId != 1) {
                        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
                    }
                    spawnParticlesForTerrainType3(0x2C, entry, scratch);
                }
                i = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
                if (i == 0) {
                    entry->m38_step = scratch->m90_checkResult;
                    i = checkWorldmapPosition(&entry->m28_position, &entry->m38_step, &scratch->m90_checkResult, entry->m4A_stepScale << 12, worldMapGearMode);
                    if (i == 0) {
                        entry->m38_step.vz = 0;
                        entry->m38_step.vx = 0;
                    }
                }
                if (i == 1) {
                    processWorldmapDynamicCollisions(&scratch->m90_checkResult, 0x18, 0x30, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                    i = adjustLocationAfterCollisionVar0;
                    if (targetVehicleEntityIndex == 7) {
                        i += 3;
                    }
                    if (D_8009B180[i] != 0) {
                        entry->m28_position = scratch->m90_checkResult;
                        if ((entry->m38_step.vx | entry->m38_step.vz) != 0) {
                            worldmapFollowLeaderDataIndex = (worldmapFollowLeaderDataIndex + 1) & 0x1F;
                            p = &worldmapFollowLeaderData[worldmapFollowLeaderDataIndex];
                            p->m0_position = entry->m28_position;
                            p->m10_direction = entry->m48_direction;
                            updateWorldmapEncounterTimers();
                        }
                    }
                } else {
                    processWorldmapDynamicCollisions(&entry->m28_position, 0x18, 0x30, &adjustLocationAfterCollisionVar0, &targetVehicleEntityIndex);
                }
                setupWorldmapExits(&entry->m28_position, 1);
                entry->m38_step.vz = 0;
                entry->m38_step.vy = 0;
                entry->m38_step.vx = 0;
                worldmapRadarPosition = entry->m28_position;
                worldmapPlayerDirection = entry->m48_direction;
                break;
            }
            currentWorldmapDynamicCollisionSlot = 0;
        } else if ((s8)entry->m4C_sprite->mAF_animationId != 3) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 3);
            clearWorldmapParticles(0x2C);
        }
        break;
    case 2:
        entry->m28_position.vx = gWorldmapState->m0_entries[7].m28_position.vx;
        entry->m28_position.vz = gWorldmapState->m0_entries[7].m28_position.vz;
        entry->m48_direction = gWorldmapState->m0_entries[7].m48_direction;
        break;
    case 8:
        if (gameState.m1D34_currentParty[1] != 0xFF) {
            if (gameState.m22B1_isOnGear[1] == 1) {
                if (changeWorldmapEntityState(5, 1) != 0) {
                    entry[1].m6_targetEntity = targetVehicleEntityIndex;
                    entry->m20_subState++;
                }
                break;
            }
            changeWorldmapEntityState(2, 1);
            gWorldmapState->m0_entries[2].m6_targetEntity = targetVehicleEntityIndex;
            changeWorldmapEntityState(5, 8);
        }
        entry->m20_subState++;
        break;
    case 9:
        if (gameState.m1D34_currentParty[2] != 0xFF) {
            if (gameState.m22B1_isOnGear[2] == 1) {
                if (changeWorldmapEntityState(6, 1) != 0) {
                    entry[2].m6_targetEntity = targetVehicleEntityIndex;
                    entry->m20_subState++;
                }
                break;
            }
            changeWorldmapEntityState(3, 1);
            gWorldmapState->m0_entries[3].m6_targetEntity = targetVehicleEntityIndex;
            changeWorldmapEntityState(6, 8);
        }
        entry->m20_subState++;
        break;
    case 10:
        selectYggdrasilExitDirection(&entry->m28_position, &gWorldmapState->m0_entries[targetVehicleEntityIndex].m28_position, &entry->m38_step, &entry->m48_direction);
        entry->m50 = (gWorldmapState->m0_entries + targetVehicleEntityIndex)->m28_position.vx >> 12;
        entry->m54 = (gWorldmapState->m0_entries + targetVehicleEntityIndex)->m28_position.vz >> 12;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
        entry->m20_subState++;
    case 11:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState++;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        spawnParticlesForTerrainType3(0x2C, entry, scratch);
        break;
    case 12:
        if (changeWorldmapEntityState(targetVehicleEntityIndex, 4) != 0) {
            entry->m24_hidden = 1;
            entry->m20_subState = 2;
            clearWorldmapParticles(0x2C);
        }
        break;
    case 0x10:
        switch (worldmapNumActivePartyMembers) {
        case 1:
            entry->m20_subState = 1;
            break;
        case 2:
            if (gameState.m22B1_isOnGear[1] != 0) {
                entry->m20_subState++;
            }
            break;
        case 3:
            if (gameState.m22B1_isOnGear[1] & gameState.m22B1_isOnGear[2]) {
                entry->m20_subState++;
            }
            break;
        }
        break;
    case 0x11:
        if (gameState.m1D34_currentParty[1] == 0xFF || changeWorldmapEntityState(5, 5) != 0) {
            entry->m20_subState++;
        }
        break;
    case 0x12:
        if (gameState.m1D34_currentParty[2] == 0xFF || changeWorldmapEntityState(6, 5) != 0) {
            entry->m20_subState = 0x40;
        }
        break;
    case 0x18:
        if (gameState.m1D34_currentParty[index - 4] == 7) {
            entry->m22_timer = 1;
            entry->m20_subState = 0x1A;
        } else {
            scratch->mA0.vx = entry->m28_position.vx >> 12;
            scratch->mA0.vy = entry->m28_position.vy >> 12;
            scratch->mA0.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(5, &scratch->mA0, NULL);
            entry->m22_timer = 8;
            entry->m20_subState++;
        }
        break;
    case 0x19:
        if (--entry->m22_timer <= 0) {
            entry->m24_hidden = 1;
            entry->m22_timer = 0x10;
            entry->m20_subState++;
        }
        break;
    case 0x1A:
        if (--entry->m22_timer <= 0) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 3);
            entry->m24_hidden = 1;
            entry->m20_subState = 2;
        }
        break;
    case 0x20:
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
        if (gameState.m1D34_currentParty[1] != 0xFF) {
            changeWorldmapEntityState(5, 2);
        }
        if (gameState.m1D34_currentParty[2] != 0xFF) {
            changeWorldmapEntityState(6, 2);
        }
        entry->m38_step.vz = 0;
        entry->m38_step.vy = 0;
        entry->m38_step.vx = 0;
        entry->m20_subState++;
    case 0x21:
        if (changeWorldmapEntityState(1, 2) != 0) {
            gWorldmapState->m0_entries[1].m6_targetEntity = index;
            entry->m20_subState++;
        }
        break;
    case 0x22:
        entry->m20_subState = 0;
        break;
    case 0x30:
        getGamestate182C(&entry->m28_position);
        entry->m24_hidden = 0;
        entry->m48_direction = gWorldmapState->m0_entries[7].m78;
        scratch->m0.vx = entry->m28_position.vx + getAngleCos(entry->m48_direction) * 0x60;
        scratch->m0.vz = entry->m28_position.vz + -getAngleSin(entry->m48_direction) * 0x60;
        selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
        entry->m50 = scratch->m0.vx >> 12;
        entry->m54 = scratch->m0.vz >> 12;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
        entry->m20_subState++;
    case 0x31:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            entry->m20_subState++;
        }
        worldmapRadarPosition = entry->m28_position;
        worldmapPlayerDirection = entry->m48_direction;
        break;
    case 0x32:
        p = worldmapFollowLeaderData;
        worldmapFollowLeaderDataIndex = 0;
        scratch->m30 = entry->m28_position;
        scratch->mA0.vx = entry->m48_direction;
        for (i = 31; i != -1; i--) {
            p->m0_position = scratch->m30;
            p->m10_direction = scratch->mA0.vx;
            p++;
        }
        entry->m20_subState = 1;
        worldMapGearMode = 2;
        break;
    case 0x40:
        break;
    }
    if (entry->m20_subState != 2 && (worldMapGearMode == 2 || gameState.m1D34_currentParty[0] != 7)) {
        addWorldmapTrackMark(1, &entry->m28_position);
    }
    gameState.m1930_fieldVarsBackup[0x16] = entry->m28_position.vx >> 12;
    gameState.m1930_fieldVarsBackup[0x17] = entry->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[3] = entry->m48_direction;
    return result;
}

/* 1oah: worldmapMode0_task_partyGear1_init (worldmapPartyGear.cpp). */
s32 worldmapMode0_task_partyGear1_init(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result;

    result = createWorldmapGear(entry, 1);
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m48_direction = gameState.m1820_worldmapPosition[3 + 1];
    entry->m4A_stepScale = 0xC;
    entry->m58 = 0xF;
    entry->m5C = entry->m48_direction;
    if (worldMapGearMode > 0) {
        if (worldMapGearMode < 4) {
            if (gameState.m22B1_isOnGear[1] == 1) {
                entry->m20_subState = 1;
                entry->m28_position.vx = worldmapPosition.vx;
                entry->m28_position.vz = worldmapPosition.vz;
                entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
                entry->m48_direction = worldmapGamestate1824;
            }
        } else if (worldMapGearMode < 8) {
            entry->m20_subState = 2;
            entry->m24_hidden = 1;
        }
    }
    gameState.m1930_fieldVarsBackup[1 * 3 + 0x16] = entry->m28_position.vx >> 12;
    gameState.m1930_fieldVarsBackup[1 * 3 + 0x17] = entry->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[3 + 1] = entry->m48_direction;
    return result;
}

/* 1oah: worldmapPartyGear.cpp, init of task 5 (party gear 1) in the alternate task set. */
s32 worldmapMode0_task_partyGear1_reinit(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result = createWorldmapGear(entry, 1);
    if (worldMapGearMode > 0) {
        if (worldMapGearMode < 4) {
        } else if (worldMapGearMode < 8) {
            entry->m24_hidden = 1;
        }
    }
    return result;
}

/* 1oah: worldmapPlayerGear.cpp / worldmapPartyGear.cpp, gear 1 init without follow logic (not referenced). */
s32 worldmapMode0_task_partyGear1_initSaved(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s16 direction;

    entry->m4C_sprite = &createSpriteActor(worldmapPartyGearSprites[1], 0x100, 0x1FC, 0x160, 0x140, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
    setGraphicEntityScale(entry->m4C_sprite, 0x2000);
    entry->m4C_sprite->m3C_x2 = 0;
    entry->m28_position.vx = (u16)gameState.m1930_fieldVarsBackup[1 * 3 + 0x16] << 12;
    entry->m28_position.vz = (u16)gameState.m1930_fieldVarsBackup[1 * 3 + 0x17] << 12;
    entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
    direction = gameState.m1820_worldmapPosition[3 + 1];
    entry->m24_hidden = 1;
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m4A_stepScale = 0xC;
    entry->m48_direction = direction;
    return 1;
}

/* Noah: worldmapMode0_task_partyGear_update (worldmapPartyGear.cpp). */
s32 worldmapMode0_task_partyGear_update(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    sWorldmapScratch* scratch = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    sWorldmapStateEntry* target;
    sWorldmapFollowLeader* p;

    switch (entry->m4_request) {
    case 1:
        entry->m20_subState = 8;
        entry->m4_request = 0;
        target = &gWorldmapState->m0_entries[entry->m6_targetEntity];
        break;
    case 4:
        entry->m20_subState = 0x40;
        entry->m4_request = 0;
        gameState.m22B1_isOnGear[index - 4] = 1;
        break;
    case 5:
        entry->m4_request = 0;
        entry->m20_subState = 0x10;
        break;
    case 2:
        entry->m4_request = 0;
        entry->m20_subState = 0x20;
        break;
    case 8:
        entry->m4_request = 0;
        entry->m20_subState = 0x18;
        break;
    case 3:
        entry->m4_request = 0;
        entry->m20_subState = 0x30;
        break;
    }

    switch (entry->m20_subState) {
    case 0:
    case 1:
        if (gameState.m22B1_isOnGear[index - 4] == 1) {
            p = &worldmapFollowLeaderData[(worldmapFollowLeaderDataIndex - entry->m58) & 0x1F];
            if ((entry->m28_position.vx == p->m0_position.vx) & (entry->m28_position.vy == p->m0_position.vy) & (entry->m28_position.vz == p->m0_position.vz)) {
                if ((s8)entry->m4C_sprite->mAF_animationId != 0) {
                    spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
                    clearWorldmapParticles(index + 0x28);
                }
            } else {
                if ((s8)entry->m4C_sprite->mAF_animationId != 1) {
                    spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
                }
                spawnParticlesForTerrainType3(index + 0x28, entry, scratch);
            }
            entry->m28_position.vx = p->m0_position.vx;
            entry->m28_position.vy = p->m0_position.vy;
            entry->m28_position.vz = p->m0_position.vz;
            entry->m48_direction = p->m10_direction;
        } else if ((s8)entry->m4C_sprite->mAF_animationId != 3) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 3);
            clearWorldmapParticles(index + 0x28);
        }
        break;
    case 2:
        entry->m28_position.vx = gWorldmapState->m0_entries[7].m28_position.vx;
        entry->m28_position.vz = gWorldmapState->m0_entries[7].m28_position.vz;
        entry->m48_direction = gWorldmapState->m0_entries[7].m48_direction;
        break;
    case 8:
        selectYggdrasilExitDirection(&entry->m28_position, &target->m28_position, &entry->m38_step, &entry->m48_direction);
        entry->m50 = target->m28_position.vx >> 12;
        entry->m54 = target->m28_position.vz >> 12;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
        entry->m20_subState++;
    case 9:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState++;
        }
        spawnParticlesForTerrainType3(index + 0x28, entry, scratch);
        break;
    case 10:
        if (changeWorldmapEntityState(entry->m6_targetEntity, 4) != 0) {
            entry->m24_hidden = 1;
            entry->m20_subState = 2;
            clearWorldmapParticles(index + 0x28);
        }
        break;
    case 0x10:
        entry->m50 = gWorldmapState->m0_entries[4].m28_position.vx;
        entry->m54 = gWorldmapState->m0_entries[4].m28_position.vz;
        scratch->m0.vx = (s32)entry->m50 - entry->m28_position.vx;
        scratch->m0.vz = (s32)entry->m54 - entry->m28_position.vz;
        entry->m48_direction = (ratan2(scratch->m0.vz, scratch->m0.vx) + 0x400) & 0xFFF;
        entry->m38_step.vx = getAngleCos(entry->m48_direction);
        entry->m38_step.vz = -getAngleSin(entry->m48_direction);
        entry->m50 >>= 12;
        entry->m54 >>= 12;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
        entry->m20_subState++;
    case 0x11:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            entry->m20_subState++;
        }
        break;
    case 0x12:
        if (changeWorldmapEntityState(4, 7) != 0) {
            entry->m20_subState = 1;
        }
        break;
    case 0x18:
        if (gameState.m1D34_currentParty[index - 4] == 7) {
            entry->m22_timer = 1;
            entry->m20_subState = 0x1A;
        } else {
            scratch->mA0.vx = entry->m28_position.vx >> 12;
            scratch->mA0.vy = entry->m28_position.vy >> 12;
            scratch->mA0.vz = entry->m28_position.vz >> 12;
            spawnWorldmapParticles(index + 1, &scratch->mA0, NULL);
            entry->m22_timer = 8;
            entry->m20_subState++;
        }
        break;
    case 0x19:
        if (--entry->m22_timer <= 0) {
            entry->m24_hidden = 1;
            entry->m22_timer = 0x10;
            entry->m20_subState++;
        }
        break;
    case 0x1A:
        if (--entry->m22_timer <= 0) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 3);
            entry->m24_hidden = 1;
            entry->m20_subState = 2;
        }
        break;
    case 0x20:
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
        entry->m38_step.vz = 0;
        entry->m38_step.vy = 0;
        entry->m38_step.vx = 0;
        entry->m20_subState++;
        break;
    case 0x21:
        if (changeWorldmapEntityState(index - 3, 2) != 0) {
            (gWorldmapState->m0_entries + (index - 3))->m6_targetEntity = index;
            entry->m20_subState++;
        }
        break;
    case 0x22:
        entry->m20_subState = 0;
        break;
    case 0x30:
        getGamestate182C(&entry->m28_position);
        entry->m24_hidden = 0;
        entry->m48_direction = gWorldmapState->m0_entries[7].m78;
        scratch->m0.vx = entry->m28_position.vx + getAngleCos(entry->m48_direction) * 0x60;
        scratch->m0.vz = entry->m28_position.vz + -getAngleSin(entry->m48_direction) * 0x60;
        selectYggdrasilExitDirection(&entry->m28_position, &scratch->m0, &entry->m38_step, &entry->m48_direction);
        entry->m50 = scratch->m0.vx >> 12;
        entry->m54 = scratch->m0.vz >> 12;
        spriteActorSetPlayingAnimation(entry->m4C_sprite, 1);
        entry->m20_subState++;
        break;
    case 0x31:
        if (stepGearAwayFromYggdrasil(entry) == 3) {
            spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
            entry->m38_step.vz = 0;
            entry->m38_step.vy = 0;
            entry->m38_step.vx = 0;
            entry->m20_subState++;
        }
        break;
    case 0x32:
        entry->m20_subState = 1;
        break;
    case 0x40:
        break;
    }
    if (entry->m20_subState != 2 && (worldMapGearMode == 2 || gameState.m1D34_currentParty[index - 4] != 7)) {
        addWorldmapTrackMark(1, &entry->m28_position);
    }
    {
        s32 gear = index - 4;
        gameState.m1930_fieldVarsBackup[gear * 3 + 0x16] = entry->m28_position.vx >> 12;
        gameState.m1930_fieldVarsBackup[gear * 3 + 0x17] = entry->m28_position.vz >> 12;
    }
    gameState.m1820_worldmapPosition[index - 1] = entry->m48_direction;
    return 1;
}

/* 2oah: worldmapMode0_task_partyGear2_init (worldmapPartyGear.cpp). */
s32 worldmapMode0_task_partyGear2_init(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result;

    result = createWorldmapGear(entry, 2);
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m48_direction = gameState.m1820_worldmapPosition[3 + 2];
    entry->m4A_stepScale = 0xC;
    entry->m58 = 0x1F;
    entry->m5C = entry->m48_direction;
    if (worldMapGearMode > 0) {
        if (worldMapGearMode < 4) {
            if (gameState.m22B1_isOnGear[2] == 1) {
                entry->m20_subState = 1;
                entry->m28_position.vx = worldmapPosition.vx;
                entry->m28_position.vz = worldmapPosition.vz;
                entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
                entry->m48_direction = worldmapGamestate1824;
            }
        } else if (worldMapGearMode < 8) {
            entry->m20_subState = 2;
            entry->m24_hidden = 1;
        }
    }
    gameState.m1930_fieldVarsBackup[2 * 3 + 0x16] = entry->m28_position.vx >> 12;
    gameState.m1930_fieldVarsBackup[2 * 3 + 0x17] = entry->m28_position.vz >> 12;
    gameState.m1820_worldmapPosition[3 + 2] = entry->m48_direction;
    return result;
}

/* 2oah: worldmapPartyGear.cpp, init of task 6 (party gear 2) in the alternate task set. */
s32 worldmapMode0_task_partyGear2_reinit(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s32 result = createWorldmapGear(entry, 2);
    if (worldMapGearMode > 0) {
        if (worldMapGearMode < 4) {
        } else if (worldMapGearMode < 8) {
            entry->m24_hidden = 1;
        }
    }
    return result;
}

/* 2oah: worldmapPlayerGear.cpp / worldmapPartyGear.cpp, gear 2 init without follow logic (not referenced). */
s32 worldmapMode0_task_partyGear2_initSaved(s32 index) {
    sWorldmapStateEntry* entry = &gWorldmapState->m0_entries[index];
    s16 direction;

    entry->m4C_sprite = &createSpriteActor(worldmapPartyGearSprites[2], 0x100, 0x1FB, 0x280, 0x100, 0x40)->m0_spriteActorCore;
    spriteActorSetPlayingAnimation(entry->m4C_sprite, 0);
    setGraphicEntityScale(entry->m4C_sprite, 0x2000);
    entry->m4C_sprite->m3C_x2 = 0;
    entry->m28_position.vx = (u16)gameState.m1930_fieldVarsBackup[2 * 3 + 0x16] << 12;
    entry->m28_position.vz = (u16)gameState.m1930_fieldVarsBackup[2 * 3 + 0x17] << 12;
    entry->m28_position.vy = worldmapGetAltitudeFor2dPoint(entry->m28_position.vx, entry->m28_position.vz);
    direction = gameState.m1820_worldmapPosition[3 + 2];
    entry->m24_hidden = 1;
    entry->m38_step.vz = 0;
    entry->m38_step.vy = 0;
    entry->m38_step.vx = 0;
    entry->m4A_stepScale = 0xC;
    entry->m48_direction = direction;
    return 1;
}

/* Noah: getGamestate182C (worldmap.cpp). */
void getGamestate182C(VECTOR* arg0) {
    arg0->vx = (s16)gameState.m182C_vehiclePosition[0] << 12;
    arg0->vy = (s16)gameState.m182C_vehiclePosition[1] << 12;
    arg0->vz = (s16)gameState.m182C_vehiclePosition[2] << 12;
}
