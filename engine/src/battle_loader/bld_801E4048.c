/* Battle loader phases (Noah battleLoader.cpp): enemies, party members,
 * battle inventory, the loader tick. gcc-2.6.3-psx, ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/filesystem.h"
#include "battle/battle.h"
#include "battle_loader/battle_loader.h"
#include "pc_convert.h"
#include "pc_gameplay.h"

/* battle overlay */
void* allocateBattleHeap(s32 size, s32 flags);
void* allocateTextureRamForText(s32 size);

/* battle .bss */
extern u8 battleEventMoviePending;
extern u8 battleEventEndBattleFlag;
extern u8 battleEventIsRunningFlag;
extern u8 isBattleAnEvent;
extern u8 battleEventMenuEnabled;
extern u8 D_800D2D44;
extern u8 drawBattleMode1Disabled;
extern u8 battleRunningVar1;
extern u8 bBattleTickMode1;
extern u8 battleForceOnGear;

extern u8 battlePartyLayoutType;
extern sBattleMechaInitData* battleMechaInitData; /* Noah: battleLoadDataVar2Bis */
extern u8 battleGetSlotStatusSub_current28Index;
extern u8 battleMonsterMapping[11];
extern u8 battleSlotLayout[32][4];
void initPartyBattleStats(void);
void initForcedGearBattlePartyStats(void);
void battleGetSlotStatus(u8* slotStatus);
u8 randomInRange(s32 min, s32 max);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4* polys, s16 x, s16 y);
s32 battleSetupStringInPolyFT4Small(s32 character, POLY_FT4* polys, s16 x, s16 y);
void battleSetupTextPoly(POLY_FT4* p);
void computeMenuBorder(void* font, s32 glyphIndex, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpX, s32* tpY);
u16 bitmaskCharacterCheck(u16 bitmask, u8 characterId);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
s8 getEntityToFace(u8 entity);

extern u8* battleConfigFile2;
extern u8* currentBattleSpecificStrings;
extern u16 bBattleTickMode0;
extern u8* battleFont;
extern u8* battleItemNameStrings;
extern u8* battleAttackNameStrings;
extern u8* battleConfigFile3;
extern LoadingBatchCommand playerLoadingCommands[3];
extern u16 partyMemberSpritesOffset[3 * 3];           /* Noah: partyMemberSpritesOffset[layout][pc] */
extern s32 battleOddOrEven;
extern u8 needBattleEventOverlay;
/* Noah: battleCommandsSetups, per loadout; entries 16-18 are Noah's
 * batteLoaderPhase2_2_array, _array3 and _arrayFor7 */
extern u8* battleCommandsSetups[19];
extern u16 party1C_InitialValues[16];
extern sBattleItemList battleEffectBF;
extern u8 battleItemStartIds[0x30];             /* battle item list: ids (copy) */
extern u8 battleAmmoItemIds[0x30];             /* inventory 1 items 0x32-0x48: ids */
extern u8 battleAmmoItemCounts[0x30];             /* inventory 1 items 0x32-0x48: counts */

/* main */
extern void* battleCharacterConfigFile;                /* battle character config file (Noah: battleCharacterConfigFile) */
extern sBattleMechaInitData* battleLoadDataVar2;
extern u16 battleNonDefendTurnCount;
extern u8 battleEventReturnMode;
extern u8 battlePartyCharacterIds[]; /* party slot -> character */

void batteLoaderPhase1_1(void);
void loadEnemies(void);
void batteLoaderPhase1_3(void);
void initBattleInventory(void);
void batteLoaderPhase2_1(void);
void batteLoaderPhase2_2(void);
void loadPartyMembers(void);
void batteLoaderPhase3_0_sub0(void);
void setupBattleVar0Polys(void);
void renderItemTargetsLabels(void);
void batteLoaderPhase3_0_sub1(void);
void batteLoaderPhase3_0(void);
void batteLoaderPhase3_1(void);

void batteLoaderPhase1_0(void) {
    s32 i;

    battleEventMoviePending = 0;
    battleEventEndBattleFlag = 0;
    battleEventIsRunningFlag = 0;
    isBattleAnEvent = 0;
    battleEventMenuEnabled = 0;
    D_800D2D44 = 0;
    drawBattleMode1Disabled = 0;
    battleNonDefendTurnCount = 0;
    battleEventReturnMode = 0;
    for (i = 0; i < 3; i++) {
        battlePartyCharacterIds[i] = battleCharacters[i];
        battleVisualEntities[i].m2_characterId = battleCharacters[i];
    }
    for (i = 0; i < 11; i++) {
        battleVisualEntities[i].m3_hidden = 0;
        if (battleForceOnGear == 0) {
            battleVisualEntities[i].m4_isGear = gameState.m22B1_isOnGear[i];
        } else {
            battleVisualEntities[i].m4_isGear = 1;
        }
        battleVisualEntities[i].m5 = 0;
    }
    battleRunningVar1 = 0;
    bBattleTickMode1 = 0;
}

void batteLoaderPhase1_1(void) {
    s32 i;

    battlePartyLayoutType = 0;
    battleBssBlock.m0_battleLoadDataVar2Ter = (u8*)(battleMechaInitData = battleLoadDataVar2);
    for (i = 0; i < 3; i++) {
        if ((battleVisualEntities[i].m2_characterId & 0x7F) != 0x7F) {
            isBattleSlotFilled.m0_isBattleSlotFilled[i] = 1;
            battlePartyLayoutType++;
        } else {
            isBattleSlotFilled.m0_isBattleSlotFilled[i] = 0;
        }
        if (battleVisualEntities[i].m4_isGear == 0) {
            battleVisualEntities[i].m0_positionSlot = currentBattleConfig.m4[i] & 0x7F;
        } else {
            battleVisualEntities[i].m0_positionSlot = i;
        }
    }
    battlePartyLayoutType += 0xFF;
    for (i = 0; i < 8; i++) {
        u8 id = currentBattleConfig.m4[i + 4] & 0x7F;
        if (id != 0x7F) {
            battleVisualEntities[i + 3].m2_characterId = id;
            battleVisualEntities[i + 3].m3_hidden = currentBattleConfig.m10_monsterFlags[i] & 0x80;
            battleVisualEntities[i + 3].m4_isGear = currentBattleConfig.m4[i + 4] & 0x80;
            battleVisualEntities[i + 3].m5 = currentBattleConfig.m10_monsterFlags[i] & 1;
            isBattleSlotFilled.m0_isBattleSlotFilled[i + 3] = 1;
            battleVisualEntities[i + 3].m0_positionSlot = currentBattleConfig.m18_monsterPositions[i] & 0x7F;
        } else {
            battleEntities.m0_entities[i + 3].m0_base.m4E_MaxHP = 0;
            battleEntities.m0_entities[i + 3].m0_base.m4C_HP = 0;
            battleVisualEntities[i + 3].m2_characterId = 0x7F;
            battleVisualEntities[i + 3].m3_hidden = 0;
            battleVisualEntities[i + 3].m4_isGear = 0;
            isBattleSlotFilled.m0_isBattleSlotFilled[i + 3] = 0;
        }
        battleMonsterMapping[i + 3] = battleVisualEntities[i + 3].m2_characterId + 1;
    }
    for (i = 0; i < 32; i++) {
        battleSlotLayout[i][0] = 0;
        battleSlotLayout[i][1] = 0;
    }
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleVisualEntities[i].m4_isGear == 0) {
                battleVisualEntities[i].m1_indexInSlot = battleSlotLayout[battleVisualEntities[i].m0_positionSlot][0];
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot][1] |= characterIdToTargetBitmask(battleVisualEntities[i].m1_indexInSlot);
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot][0]++;
            } else {
                battleVisualEntities[i].m1_indexInSlot = 0;
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 0x10][1] = 1;
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 0x10][0] = 1;
            }
        }
    }
    for (i = 3; i < 11; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleVisualEntities[i].m4_isGear == 0) {
                battleVisualEntities[i].m1_indexInSlot = battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 8][0];
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 8][1] |= characterIdToTargetBitmask(battleVisualEntities[i].m1_indexInSlot);
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 8][0]++;
            } else {
                battleVisualEntities[i].m1_indexInSlot = 0;
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 0x18][1] = 1;
                battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 0x18][0] = 1;
            }
        }
    }
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleVisualEntities[i].m4_isGear == 0) {
                battleVisualEntities[i].mA_X = battleMechaInitData->m4_slotPositions[battleVisualEntities[i].m0_positionSlot].m0_party[battleVisualEntities[i].m1_indexInSlot].vx;
                battleVisualEntities[i].mC_Z = battleMechaInitData->m4_slotPositions[battleVisualEntities[i].m0_positionSlot].m0_party[battleVisualEntities[i].m1_indexInSlot].vy;
            } else {
                battleVisualEntities[i].mA_X = battleMechaInitData->m100_gearPositions[battleVisualEntities[i].m0_positionSlot][0].vx;
                battleVisualEntities[i].mC_Z = battleMechaInitData->m100_gearPositions[battleVisualEntities[i].m0_positionSlot][0].vy;
            }
        }
    }
    for (i = 3; i < 11; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleVisualEntities[i].m4_isGear == 0) {
                battleVisualEntities[i].mA_X = battleMechaInitData->m4_slotPositions[battleVisualEntities[i].m0_positionSlot].mC_monsters[battleVisualEntities[i].m1_indexInSlot].vx;
                battleVisualEntities[i].mC_Z = battleMechaInitData->m4_slotPositions[battleVisualEntities[i].m0_positionSlot].mC_monsters[battleVisualEntities[i].m1_indexInSlot].vy;
                battleVisualEntities[i].m6_direction = currentBattleConfig.m18_monsterPositions[i] & 0x80;
            } else {
                battleVisualEntities[i].mA_X = battleMechaInitData->m100_gearPositions[battleVisualEntities[i].m0_positionSlot][1].vx;
                battleVisualEntities[i].mC_Z = battleMechaInitData->m100_gearPositions[battleVisualEntities[i].m0_positionSlot][1].vy;
                battleVisualEntities[i].m6_direction = currentBattleConfig.m18_monsterPositions[i] & 0x80;
            }
        }
    }
}

void loadEnemies(void) {
    s32 i, j;
    sBattleEntity* entities;

    battleBssBlock.mA38_performAttackSub3_var0 = 0;
    bBattleTickMode0 = 0;
    currentBattleSpecificStrings = battleConfigFile2 + *(u16*)(battleConfigFile2 + 0x30);
    entities = (sBattleEntity*)(battleConfigFile2 + 0x32);
    for (i = 3; i < 11; i++) {
        unknownMonsterStatus0[i - 3].m3_removed = 0;
        if (battleBssBlock.m4_visualEntities[i].m2_characterId != 0x7F) {
            u8* p;
            memmove(&battleEntities.m0_entities[i], &entities[battleBssBlock.m4_visualEntities[i].m2_characterId], sizeof(sBattleEntity));
            pcGameplayEnemyLoaded(&battleEntities.m0_entities[i], i, currentBattleConfig.m0_battleId,
                                  battleBssBlock.m4_visualEntities[i].m2_characterId);
            p = battleConfigFile2 + ((u16*)battleConfigFile2)[battleBssBlock.m4_visualEntities[i].m2_characterId];
            monstersScriptsEntities[i - 3].m0_scripts[0] = p + ((u16*)p)[0];
            monstersScriptsEntities[i - 3].m0_scripts[1] = p + ((u16*)p)[1];
            if (((u16*)p)[2] != 0xFFFF) {
                monstersScriptsEntities[i - 3].m0_scripts[2] = p + ((u16*)p)[2];
                unknownMonsterStatus0[i - 3].m0_hasWhenAttackedScript = 1;
            } else {
                unknownMonsterStatus0[i - 3].m0_hasWhenAttackedScript = 0;
            }
            if (((u16*)p)[3] != 0xFFFF) {
                monstersScriptsEntities[i - 3].m0_scripts[3] = p + ((u16*)p)[3];
                unknownMonsterStatus0[i - 3].m1_hasEndOfTurnScript = 1;
            } else {
                unknownMonsterStatus0[i - 3].m1_hasEndOfTurnScript = 0;
            }
            for (j = 0; j < 4; j++) {
                monstersScriptsEntities[i - 3].m10_s32Vars[j] = 0;
            }
            for (j = 0; j < 8; j++) {
                monstersScriptsEntities[i - 3].m20_s16Vars[j] = 0;
            }
            for (j = 0; j < 16; j++) {
                monstersScriptsEntities[i - 3].m30_s8Vars[j] = 0;
            }
        } else {
            bzero(&battleEntities.m0_entities[i], sizeof(sBattleEntity));
            unknownMonsterStatus0[i - 3].m0_hasWhenAttackedScript = 0;
            unknownMonsterStatus0[i - 3].m1_hasEndOfTurnScript = 0;
        }
    }
}

void batteLoaderPhase1_3(void) {
    s32 i;

    initPartyBattleStats();
    if (battleForceOnGear != 0) {
        initForcedGearBattlePartyStats();
    }
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            battleVar0->m835C_apFuelLabels[i].m1E1_barMode = 1;
            if (battleVisualEntities[i].m4_isGear != 0) {
                apConfigArray[i].m1_gearState = 1;
                battleEntities.m0_entities[i].m15A_flags |= 0x80;
                if (battleVisualEntities[i].m2_characterId != 7) {
                    battleVar0->m835C_apFuelLabels[i].m1E1_barMode = 2;
                }
            } else {
                apConfigArray[i].m1_gearState = 0;
                battleEntities.m0_entities[i].m15A_flags &= 0x7F;
            }
        } else {
            apConfigArray[i].m1_gearState = 0;
            battleEntities.m0_entities[i].m15A_flags &= 0x7F;
            battleVar0->m835C_apFuelLabels[i].m1E1_barMode = 0;
        }
    }
    for (i = 3; i < 11; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleVisualEntities[i].m4_isGear != 0) {
                apConfigArray[i].m1_gearState = 1;
            } else {
                apConfigArray[i].m1_gearState = 0;
            }
        } else {
            apConfigArray[i].m1_gearState = 0;
        }
    }
    for (i = 0; i < 3; i++) {
        apConfigArray[i].m5_savedLevels[0] = battleEntities.m0_entities[i].m0_base.m62_Level;
        apConfigArray[i].m5_savedLevels[1] = battleEntities.m0_entities[i].m0_base.m63_Level2;
    }
}

/* Battle inventory setup (Noah: initBattleInventory, MissingCode there) */
void initBattleInventory(void) {
    s32 i;
    u8 n;

    for (i = 0; i < 0x30; i++) {
        battleEffectBF.m42_ids[i] = 0;
        battleEffectBF.m12_counts[i] = 0;
        battleItemStartIds[i] = 0;
    }
    for (i = 0; i < 150; i++) {
        if (gameState.m1F90_inventory0Counts[i] >= 100) {
            gameState.m1F90_inventory0Counts[i] = 99;
        }
        if (gameState.m1F90_inventory0Counts[i] == 0) {
            gameState.m2026_inventory0Ids[i] = 0;
        }
    }
    n = 0;
    for (i = 0; i < 150; i++) {
        if (n >= 0x30) {
            break;
        }
        if (gameState.m2026_inventory0Ids[i] != 0 && gameState.m2026_inventory0Ids[i] < 0x31) {
            battleEffectBF.m42_ids[n] = gameState.m2026_inventory0Ids[i];
            battleEffectBF.m12_counts[n] = gameState.m1F90_inventory0Counts[i];
            battleItemStartIds[n] = gameState.m2026_inventory0Ids[i];
            n++;
        }
    }
    battleVar2->m2D7[2] = 0x2F;
    for (i = 0, n = 0; i < 100; i++) {
        if (gameState.m1D9C_inventory1Ids[i] >= 0x32 && gameState.m1D9C_inventory1Ids[i] < 0x49) {
            battleAmmoItemIds[n] = gameState.m1D9C_inventory1Ids[i];
            battleAmmoItemCounts[n] = gameState.m1D38_inventory1Counts[i];
            n++;
        }
    }
    for (; n < 0x30; n++) {
        battleAmmoItemIds[n] = 0;
        battleAmmoItemCounts[n] = 0;
    }
}

void batteLoaderPhase2_1(void) {
    u8 slotStatus[11];
    s32 i;
    s32 min;

    battleGetSlotStatusSub_current28Index = 0;
    battleGetSlotStatus(slotStatus);
    for (i = 0; i < 11;) {
        u8 r = randomInRange(0, 10);
        if (slotStatus[r] == 0) {
            slotStatus[r] = 1;
            isBattleSlotFilled.mC_randomTurnOrder[i] = r;
            i++;
        }
    }
    isBattleSlotFilled.mB_currentEntryInRandomTurnOrder = 0;
    for (i = 3; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i] && (battleEntities.m0_entities[i].m0_base.m34_monsterFlags & 0x200)) {
            isBattleSlotFilled.m24_battleSlotStatusVar0[i] = isBattleSlotFilled.m3A_numTicksBeforeReady[i] = 1;
        }
    }
    min = 0xFFFF;
    for (i = 0; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i]) {
            if (isBattleSlotFilled.m3A_numTicksBeforeReady[i] < min) {
                min = isBattleSlotFilled.m3A_numTicksBeforeReady[i];
            }
        }
    }
    for (i = 0; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i]) {
            isBattleSlotFilled.m3A_numTicksBeforeReady[i] -= (s16)(min - 1);
        }
    }
}

void batteLoaderPhase2_2(void) {
    s32 i;
    s32 j;
    u8 k;

    if (currentBattleConfig.m1_flags & 0x20) {
        needBattleEventOverlay = 1;
    }
    for (i = 0; i < 3; i++) {
        apConfigArray[i].m4_currentAP = gameState.m16C0_abilities[battleCharacters[i]].m17_energyPerTurn;
        for (j = 0; j < 8; j++) {
            battleVar2->m0_entityMenus[i].m0_circleMenuBattleCommandsMapping[j] = battleCommandsSetups[battleEntities.m0_entities[i].m0_base.m56_battleCommandLoadout][j];
        }
        for (j = 0; j < 4; j++) {
            if (battleCharacters[i] != 7) {
                battleVar2->m0_entityMenus[i].m8[j] = battleCommandsSetups[16][j];
            } else {
                battleVar2->m0_entityMenus[i].m8[j] = battleCommandsSetups[18][j];
            }
            battleVar2->m0_entityMenus[i].mC[j] = battleCommandsSetups[17][j];
        }
        battleVar2->m0_entityMenus[i].m3C_currentTarget = getEntityToFace(i);
        battleVisualEntities[i].m6_direction = getDirectionBetween2BattleEntities(i, battleVar2->m0_entityMenus[i].m3C_currentTarget);
        for (k = 0; k < 16; k++) {
            battleVar2->m0_entityMenus[i].m1C_isCommandEnabled[k] = battleEntities.m0_entities[i].m0_base.m7A_commandEnabledBF & party1C_InitialValues[k];
        }
        if (battleEntities.m0_entities[i].m0_base.mA0_gearNum == 0xFF || (currentBattleConfig.m1_flags & 0x40)) {
            battleVar2->m0_entityMenus[i].m1C_isCommandEnabled[7] = party1C_InitialValues[7];
            battleEntities.m0_entities[i].m0_base.m7A_commandEnabledBF |= party1C_InitialValues[7];
        }
        if (currentBattleConfig.m1_flags & 0x80) {
            battleVar2->m0_entityMenus[i].m1C_isCommandEnabled[8] = party1C_InitialValues[8];
            battleEntities.m0_entities[i].m0_base.m7A_commandEnabledBF |= party1C_InitialValues[8];
        }
    }
    for (i = 3; i < 11; i++) {
        battleVar2->m0_entityMenus[i].m3C_currentTarget = getEntityToFace(i);
        battleVisualEntities[i].m6_direction = getDirectionBetween2BattleEntities(i, battleVar2->m0_entityMenus[i].m3C_currentTarget);
    }
}

void loadPartyMembers(void) {
    u16 mask;
    s32 i;
    s32 n;
    u8 gear;
    u8* p;
    u8** reloc;

    if (currentBattleConfig.m1_flags & 0x10) {
        battleForceOnGear = 1;
    } else {
        battleForceOnGear = 0;
    }
    mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x7FF;
    if (battleForceOnGear == 0) {
        n = 0;
        for (i = 0; i < 3; i++) {
            if (bitmaskCharacterCheck(mask, gameState.m1D34_currentParty[i])) {
                battleCharacters[n] = gameState.m1D34_currentParty[i] & 0x7F;
                n++;
            }
        }
        for (; n < 3; n++) {
            battleCharacters[n] = 0x7F;
        }
    } else {
        battleCharacters[1] = 10;
        battleCharacters[2] = 10;
        battleCharacters[0] = gameState.m1D34_currentParty[0] & 0x7F;
    }
    reloc = (u8**)pcRelocTable(battleCharacterConfigFile);
    for (i = 0; i < 3; i++) {
        if (battleEntities.m603C_battleCharacters[i] != 0x7F) {
            memmove(&battleEntities.m0_entities[i].m0_base, &gameState.m26C_party[battleEntities.m603C_battleCharacters[i]], sizeof(GameStateCharacter));
            if (battleForceOnGear && i - 1U < 2) {
                battleEntities.m0_entities[i].m0_base.mA0_gearNum = 0x11;
            }
            gear = battleEntities.m0_entities[i].m0_base.mA0_gearNum;
            if (gear == 0xFF) {
                gear = 0;
            }
            memmove(&battleEntities.m0_entities[i].mA4_gear, &gameState.m978_gears[gear], sizeof(GameStateGear));
            p = mallocAndDecompress(reloc[battleEntities.m603C_battleCharacters[i] + 5], 1);
            memmove(battleEntities.m1058_partyBattleStats[i], p, sizeof(battleEntities.m1058_partyBattleStats[i]));
            freeHeap(p);
            p = mallocAndDecompress(reloc[gear + 17], 1);
            memmove(battleEntities.m2228_mechaBattleStats[i], p, sizeof(battleEntities.m2228_mechaBattleStats[i]));
            freeHeap(p);
        }
    }
    p = mallocAndDecompress(reloc[4], 1);
    memmove(battleEntities.m35D8_enemiesBattleStats, p, sizeof(battleEntities.m35D8_enemiesBattleStats));
    freeHeap(p);
    p = mallocAndDecompress(reloc[3], 1);
    memmove(battleEntities.m5518_items, p, 0x300);
    freeHeap(p);
    p = mallocAndDecompress(reloc[2], 1);
    uploadTextureToVram((s32*)p, 0, 0, 0, 0, 0, 0);
    freeHeap(p);
    battleFont = mallocAndDecompress(reloc[1], 0);
    initFontPalettes(0, 0x1F0);
    battleItemNameStrings = mallocAndDecompress(reloc[16], 0);
    p = mallocAndDecompress(reloc[36], 1);
    loadBattleCharacterPortraits(p, 0x61);
    freeHeap(p);
    p = mallocAndDecompress(reloc[37], 1);
    memmove(&battleEntities.m5750_gearItems[10], p + 0x320, 0x300); /* 0x800D2500 (Noah: battle800D2500) */
    freeHeap(p);
    battleAttackNameStrings = mallocAndDecompress(reloc[38], 0);
    freeHeap(reloc);
    freeHeap(battleCharacterConfigFile);
    setCurrentDirectory(0xC, 1);
    playerLoadingCommands->m4_loadPtr = battleConfigFile2 = allocateBattleHeap(getFileSizeAligned(currentBattleConfig.m0_battleId * 2 + 2), 0);
    playerLoadingCommands->m0_fileIndex = currentBattleConfig.m0_battleId * 2 + 2;
    playerLoadingCommands[1].m4_loadPtr = battleConfigFile3 = allocateBattleHeap(getFileSizeAligned(currentBattleConfig.m0_battleId * 2 + 3), 1);
    playerLoadingCommands[2].m0_fileIndex = 0;
    playerLoadingCommands[2].m4_loadPtr = NULL;
    playerLoadingCommands[1].m0_fileIndex = currentBattleConfig.m0_battleId * 2 + 3;
    batchStartLoadingFiles(playerLoadingCommands, 0, 0x80);
}

void battleLoaderTick(u8 step) {
    switch (step) {
    case 0:
        loadPartyMembers();
        break;
    case 1:
        batteLoaderPhase1_0();
        batteLoaderPhase1_1();
        loadEnemies();
        batteLoaderPhase1_3();
        break;
    case 2:
        initBattleInventory();
        batteLoaderPhase2_1();
        batteLoaderPhase2_2();
        battleG3 = allocateBattleHeap(sizeof(sBattle800c3e24), 0);
        bzero(battleG3, sizeof(sBattle800c3e24));
        break;
    case 3:
        batteLoaderPhase3_0();
        batteLoaderPhase3_1();
        break;
    }
}

void batteLoaderPhase3_0_sub0(void) {
    s32 i;

    battleVar1->m7C_hpDisplayDirty[0] = 1;
    battleVar1->m7C_hpDisplayDirty[1] = 1;
    battleVar1->m7C_hpDisplayDirty[2] = 1;
    computeMenuBorder(battleFont, 0x5C, &battleVar0->mA234_glyphTextures[0].m0_polyCount, &battleVar0->mA234_glyphTextures[0].m4_tpage_tp, &battleVar0->mA234_glyphTextures[0].m8_clutX, &battleVar0->mA234_glyphTextures[0].mC_clutY, &battleVar0->mA234_glyphTextures[0].m10_tpage_X, &battleVar0->mA234_glyphTextures[0].m14_tpage_Y);
    battleVar0->mA2AE_fuelBarClut = GetClut(battleVar0->mA234_glyphTextures[0].m8_clutX, battleVar0->mA234_glyphTextures[0].mC_clutY);
    battleVar0->mA2AC_timeBarClut = GetClut(battleVar0->mA234_glyphTextures[0].m8_clutX, battleVar0->mA234_glyphTextures[0].mC_clutY - 1);
    battleVar0->mA2B2_hasteTimeBarClut = GetClut(battleVar0->mA234_glyphTextures[0].m8_clutX, battleVar0->mA234_glyphTextures[0].mC_clutY - 2);
    battleVar0->mA2B0_slowTimeBarClut = GetClut(battleVar0->mA234_glyphTextures[0].m8_clutX, battleVar0->mA234_glyphTextures[0].mC_clutY - 3);
    for (i = 0; i < 8; i++) {
        SetPolyGT4(&battleVar0->m5A0_timeBarFills[0][i]);
        SetShadeTex(&battleVar0->m5A0_timeBarFills[0][i], 0);
        setRGB0(&battleVar0->m5A0_timeBarFills[0][i], 0x80, 0x80, 0x80);
        setRGB1(&battleVar0->m5A0_timeBarFills[0][i], 0x80, 0x80, 0x80);
        setRGB2(&battleVar0->m5A0_timeBarFills[0][i], 0, 0, 0);
        setRGB3(&battleVar0->m5A0_timeBarFills[0][i], 0, 0, 0);
        battleVar0->m5A0_timeBarFills[0][i].tpage = GetTPage(battleVar0->mA234_glyphTextures[0].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y);
        SetPolyG4(&battleVar0->m740_APOrFuelPoly[0][i]);
        setRGB2(&battleVar0->m740_APOrFuelPoly[0][i], 0x4F, 0x4F, 0x4F);
        setRGB3(&battleVar0->m740_APOrFuelPoly[0][i], 0x4F, 0x4F, 0x4F);
    }
    for (i = 0; i < 12; i++) {
        SetLineF2(&battleVar0->m908_apMarks[i]);
        setRGB0(&battleVar0->m908_apMarks[i], 0xFF, 0xFF, 0xFF);
    }
}

void setupBattleVar0Polys(void) {
    RECT r;
    s32 i;

    r.y = 0;
    r.x = 0;
    r.h = 0x100;
    r.w = 0x100;
    for (i = 0; i < 2; i++) {
        SetPolyF4(&battleVar0->m63C8_portraitHighlight[i]);
        setRGB0(&battleVar0->m63C8_portraitHighlight[i], 0xFF, 0xFF, 0xFF);
        SetSemiTrans(&battleVar0->m63C8_portraitHighlight[i], 1);
        SetDrawMode(&battleVar0->m63F8_portraitHighlightDrMode[i], 0, 0, GetTPage(0, 2, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y), &r);
    }
    battleVar0->m6415_drawHighlight = 0;
    battleVar0->m6410_highlightLevel = 0xFF;
    battleVar0->m6416_highlightFadingOut = 0;
}

void renderItemTargetsLabels(void) {
    s32 i;
    RECT unused; /* reserves 8 bytes of stack like retail */

    for (i = 0; i < 10; i++) {
        itemLabelsIds[i].m0_image = allocateTextureRamForText(4);
        renderString(getItemLabelString(i), itemLabelsIds[i].m0_image, 2, 0);
    }
}

void batteLoaderPhase3_0_sub1(void) {
    s32 i;
    s32 j;
    s32 length;

    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            battleVar1->m78_apBarFrameLengths[i] += battleSetupStringInPolyFT4Small(0x52, battleVar0->m0_apBarFrame[i][battleVar1->m78_apBarFrameLengths[i]], i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x44), 0x24);
            length = battleVar1->m78_apBarFrameLengths[i] * 2; /* single-set temporary: sched1 keeps the doubling next to the call (retail delay slot) */
            battleVar1->m78_apBarFrameLengths[i] += battleSetupStringInPolyFT4Small(0x53, battleVar0->m0_apBarFrame[i][battleVar1->m78_apBarFrameLengths[i]], i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x44), 0x24);
            for (j = length; j < battleVar1->m78_apBarFrameLengths[i] * 2; j += 2) {
                battleSetupTextPoly(&battleVar0->m0_apBarFrame[i][0][j + battleOddOrEven]);
            }
            battleSetupStringInPolyFT4Large(i + 0x61, battleVar0->m818_portraits[i], i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x1C), 0x14);
            battleVar0->m835C_apFuelLabels[i].m1E2_apLabelLength = battleSetupStringInPolyFT4Large(0x90, battleVar0->m835C_apFuelLabels[i].m0_apLabel[0], i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x38), 0x27);
            battleVar0->m835C_apFuelLabels[i].m1E3_fuelLabelLength = battleSetupStringInPolyFT4Large(0x91, battleVar0->m835C_apFuelLabels[i].mA0_fuelLabel[0], i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x3C), 0x27);
            battleVar0->m835C_apFuelLabels[i].m1E0_oddOrEven = battleOddOrEven;
        }
    }
    battleVar1->mA2_apBarFrameOddOrEven = battleOddOrEven;
    battleVar1->m83_portraitsOddOrEven = battleOddOrEven;
}

void batteLoaderPhase3_0(void) {
    batteLoaderPhase3_0_sub0();
    batteLoaderPhase3_0_sub1();
}

void batteLoaderPhase3_1(void) {
    setupBattleVar0Polys();
    renderItemTargetsLabels();
}
