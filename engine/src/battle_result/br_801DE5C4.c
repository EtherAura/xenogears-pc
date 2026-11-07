/* Battle result overlay, after LIBSND SSINIT_C.OBJ: EXP and gold totals, item
 * drops, EXP split, level-ups with stat growth, deathblows and abilities, the
 * result windows, and the return to the next game mode. gcc-2.6.3-psx,
 * ASPSX 2.34. */
#include "common.h"
#include "main/sound.h"
#include "battle_result/battle_result.h"
#include "main/dialog.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pc_convert.h"
#include "pc_gameplay.h"
#include "battle_loader/battle_loader.h"

/* ---- main ---- */
extern u8 victoryFanfarePlayed;
extern u8 battleInitVar0;
extern u8 menuReturnState0;
extern u8 battleInitVar1;
void* loadGameModeOverlay(s32 mode);
void resetFieldAndPreloadPartyFiles(void);
void freeHeapForUser(u8 user);
void playSoundEffectInFreeSlot(s32 id);
void loadMenuImageBundle(u32* data);
void playSoundEffect(s32 id);
s32 rand(void);
void stopAllSounds(void);

/* ---- battle ----
 * Several battle globals are declared through a view whose name is the
 * address this overlay's code references, because JP's symbol file only
 * knows those names (battle.h: battleVisualEntities, battleEntities, previousEntityTurn, ...). */
extern u8 resultWindowLabelGlyphs[18];                /* result window labels: glyph */
extern s16 resultWindowLabelX[18];               /* result window labels: x */
extern s16 resultWindowLabelY[18];               /* result window labels: y */
extern u8 levelUpWindowLabelDefs[27 * 3];            /* level-up window text: glyph (0xFF: none), coloured, colour */
extern s16 levelUpWindowLabelX[27];               /* level-up window text: x */
extern s16 levelUpWindowLabelY[27];               /* level-up window text: y */
extern u8 rewardsWindowLabelGlyphs[8];                 /* gold window labels (setupRewardsWindowLabels): glyph */
extern s16 rewardsWindowLabelX[8];                /* ... x */
extern s16 rewardsWindowLabelY[8];                /* ... y */
/* battle's printIntegerToStringBattleBuffer: 9 digits, 0xFF = leading blank */
extern u8 printIntegerToStringBattleBuffer[9];
extern u8 needBattleEventOverlay;
extern u16 battleStartUnlockedAbilities[3][2];             /* unlocked ability bits [0], [1] of each slot at battle start */
extern u8 currentBattleMode;                    /* currentBattleMode */
/* BattleBss_800C48E8.m2_battleRunningVar1 (a struct member: retail may keep
 * its address in a register), as a one-element array */
extern u8 battleRunningVar1[1];
/* Noah: battleOddOrEven. Most functions read it as D_800CCB04.m30_battleOddOrEven
 * (battle.h); the plain symbol reproduces the reads that do not keep its address
 * in a register and are not reloaded after stores into structures. */
extern s32 battleOddOrEven;
extern sBattleItemList battleEffectBF;
extern u8 battleEventIsRunningFlag;
extern void* battleFont;                 /* battleFont */
extern u8 isBattleAnEvent;
extern u8 battleItemStartIds[0x30];              /* item ids to restore (copyBattleItemCountsToInventory) */
extern u8 battleInputButton;
extern void* battleEventHeapMarker;
extern void* battleEventOverlayHeap;
extern u8 battleForceOnGear;
extern void* battleItemNameStrings;
/* apConfigArray[i].m5[0..1] (apConfigArray, sApStruct): level / ether level
 * before the battle */
extern u8 battleEventMoviePending;
extern sBattleResultScreen* battleResultScreen;
extern u8* battleAttackNameStrings;
s32 battleRenderDebugAndMain(void);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
void repositionTextRenderingPlanePrim(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u8 width);
void battleInitPoly4(POLY_FT4* p, u8 odd, u8 mode);
void loadItemTargetsLabel(void);
void loadImageSync(RECT* rect, void* data);
u16 bitmaskCharacterCheckReverse(u16 bits, u8 index);
void* allocateBattleHeap(s32 size, s32 flags);
u16* allocateTextureRamForText(s32 n);
void idleBattleDuringLoading(void);
void shutdownBattleSpriteSystem(void);
void waitBattleAnimationSoundLoaded(void);

/* ---- this overlay's data (0x801E44C0-0x801E4500, stays assembly) ---- */
extern u8 resultFanfareSoundsPlayed;                    /* result sound effects started (playVictoryFanfare) */
extern GameState* pResultGameState;            /* &gameState */
extern BattleBss_800CCCE8* pResultBattleEntities;   /* &battleEntities */
extern s32 statBarValueBefore;                   /* stat bar: value before */
extern s32 statBarValueAfter;                   /* stat bar: value after */
extern s32 statBarValueDelta;                   /* stat bar: |after - before| */
extern s32 statBarBaseWidth;                   /* stat bar: width of the old value */
extern s32 statBarDeltaWidth;                   /* stat bar: width of the change */
extern u8 statBarDeltaColor;                    /* stat bar colour: 2 gain, 3 loss */
extern u8 statBarDeltaSignGlyph;                    /* sign glyph: 0xE3 '+', 0xE5 '-' */
extern sLevelUpData* pLevelUpData;
extern GameStateCharacter* pLevelUpCharacter;   /* character being levelled */
extern u32 characterExpGain;                   /* experience won */
extern u32 characterEtherExpGain;                   /* ether experience won */
extern u8 levelsBeforeExp[3][2];              /* level, ether level of each slot before the battle */

/* ---- this overlay's functions ---- */
void setupResultCharacterNames(void);
void setupResultWindowLabels(void);
void setupResultHpMpValues(void);
void setupResultLevelValues(u8 current);
void setupResultExpValues(void);
void setupResultExpGainedValues(void);
void setupStatsPolyG4(POLY_G4* p, u8 type);
void tintStatChangeDigits(POLY_FT4* p, u8 gain, u8 count, u8 oddOrEven);
void computeStatBarWidths(u8 before, u8 after, s32 max);
s32 getHighestLevelUpStat(u8 c);
void setupLevelUpWindowLabels(u8 c);
void setupLevelUpMaxHpValue(u8 c);
void setupLevelUpMaxMpValue(u8 c);
void setupLevelUpMaxHpGain(u8 c);
void setupLevelUpMaxMpGain(u8 c);
void setupLevelUpStatBars(u8 c);
void playResultSoundEffect(u8 id);
void waitForResultConfirm(void);
void showLearnedDeathblowsAndAbilities(u8 slot);
void setupRewardsWindowLabels(void);
void setupRewardsGoldValues(u32 gold);
void setupRewardsWindowTitles(void);
void addItemToInventoryList(u8 id, u8 count, u8* ids, u8* counts, u8 n);
void addItemDropsToInventory(u8* ids, u8* counts, u8* categories);
void collectItemDrops(u8* ids, u8* counts, u8* categories);
void setupRewardsItemList(void);
void showResultCharacterWindows(void);
void animateResultExpGain(void);
void showResultLevelUps(void);
void showResultRewards(u32 gold);
void runBattleResultScreen(u32 gold);
void loadBattleResultAssets(void);
void processBattleRewards(void);
void updatePartyAfterBattle(void);
void copyBattleItemCountsToInventory(void);
void transferCharStatsBackToField_result(void);
void distributeBattleExperience(void);
void computeExperienceToAdd(u32 exp, s16 slot, s16 mode);
void applyExperienceAndLevelUp(void);
void levelUpCharacterStats(void);
void levelUpCharacterEtherStats(void);
u8 rollStatGrowth(u8 stat, u8 goal, u8 maxLevel, u8 level);
u16 rollMaxHpGrowth(u16 hp, u8 level);
u8 rollMaxMpGrowth(u8 stat, u8 level);
void learnSkillsAfterBattle(void);
u8 learnDeathblow(u8 c);
u8 learnChiAbility(u8 c);
void unlockGearAttacksFromDeathblows(u8 c);
void unlockMariaGearAttacks(void);
void unlockGearChiAbilities(u8 c);
void updateChuChuGearStats(void);
void updateEnergyPerTurn(void);
void unlockGearAttacksByLevel(void);
void rollItemDrops(void);

#if VERSION_JP
#define RESULT_NAME_X 0x24
#else
#define RESULT_NAME_X 0x20
#endif

void setupResultCharacterNames(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15E0_nameCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            battleResultCharWindows[i]->m15E0_nameCount.m0_count +=
                battleSetupStringInPolyFT4Large(i + 0xFC, &battleResultCharWindows[i]->m0_name[battleResultCharWindows[i]->m15E0_nameCount.m0_count], RESULT_NAME_X,
                              i * 0x20 + 0x24);
        }
        battleResultCharWindows[i]->m15E0_nameCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    }
}

void setupResultWindowLabels(void) {
    s32 i;
    s32 j;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15E2_labelsCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            for (j = 0; j < 18; j++) {
                battleResultCharWindows[i]->m15E2_labelsCount.m0_count +=
                    battleSetupStringInPolyFT4Large(resultWindowLabelGlyphs[j], &battleResultCharWindows[i]->m140_labels[battleResultCharWindows[i]->m15E2_labelsCount.m0_count], resultWindowLabelX[j],
                                  resultWindowLabelY[j] + i * 32);
            }
            if (battleResultCharWindows[0]->m15F8_showEther) {
                battleResultCharWindows[i]->m15E2_labelsCount.m0_count +=
                    battleSetupStringInPolyFT4Large(0xE8, &battleResultCharWindows[i]->m140_labels[battleResultCharWindows[i]->m15E2_labelsCount.m0_count], 0x88, i * 32 + 0x20);
                battleResultCharWindows[i]->m15E2_labelsCount.m0_count +=
                    battleSetupStringInPolyFT4Large(0xE9, &battleResultCharWindows[i]->m140_labels[battleResultCharWindows[i]->m15E2_labelsCount.m0_count], 0x88, i * 32 + 0x28);
                SetShadeTex(
                    &battleResultCharWindows[i]->m140_labels[0][(battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 2) * 2 + D_800CCB04.m30_battleOddOrEven],
                    0);
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 2) * 2 + battleOddOrEven))->r0 = 0x80;
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 2) * 2 + battleOddOrEven))->g0 = 0x40;
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 2) * 2 + battleOddOrEven))->b0 = 0x40;
                SetShadeTex(
                    &battleResultCharWindows[i]->m140_labels[0][(battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 1) * 2 + D_800CCB04.m30_battleOddOrEven],
                    0);
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 1) * 2 + battleOddOrEven))->r0 = 0x40;
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 1) * 2 + battleOddOrEven))->g0 = 0x80;
                (battleResultCharWindows[i]->m140_labels[0] + ((battleResultCharWindows[i]->m15E2_labelsCount.m0_count - 1) * 2 + battleOddOrEven))->b0 = 0x40;
            }
        }
        battleResultCharWindows[i]->m15E2_labelsCount.m1_oddOrEven = battleOddOrEven;
    }
}

void setupResultHpMpValues(void) {
    s32 i;
    s32 j;
    u8 digit;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15E8_hpCount.m0_count = 0;
        battleResultCharWindows[i]->m15EC_mpCount.m0_count = 0;
        battleResultCharWindows[i]->m15EA_maxHpCount.m0_count = 0;
        battleResultCharWindows[i]->m15EE_maxMpCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            printIntegerToStringBattle(battleEntities.m0_entities[i].m0_base.m4C_HP);
            for (j = 0; j < 3; j++) {
                digit = printIntegerToStringBattleBuffer[j + 6];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15E8_hpCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->m960_hp[battleResultCharWindows[i]->m15E8_hpCount.m0_count], (j + 9) * 8, i * 32 + 32);
                }
            }
            battleResultCharWindows[i]->m15E8_hpCount.m1_oddOrEven = battleOddOrEven;
            printIntegerToStringBattle(battleEntities.m0_entities[i].m0_base.m50_MP);
            for (j = 0; j < 2; j++) {
                digit = printIntegerToStringBattleBuffer[j + 7];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15EC_mpCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->mB40_mp[battleResultCharWindows[i]->m15EC_mpCount.m0_count], (j + 10) * 8, i * 32 + 40);
                }
            }
            battleResultCharWindows[i]->m15EC_mpCount.m1_oddOrEven = battleOddOrEven;
            printIntegerToStringBattle(battleEntities.m1000_savedMaxHpMp[i][0]);
            for (j = 0; j < 3; j++) {
                digit = printIntegerToStringBattleBuffer[j + 6];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15EA_maxHpCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->mA50_maxHp[battleResultCharWindows[i]->m15EA_maxHpCount.m0_count], (j + 13) * 8, i * 32 + 32);
                }
            }
            battleResultCharWindows[i]->m15EA_maxHpCount.m1_oddOrEven = battleOddOrEven;
            printIntegerToStringBattle(battleEntities.m1000_savedMaxHpMp[i][1]);
            for (j = 0; j < 2; j++) {
                digit = printIntegerToStringBattleBuffer[j + 7];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15EE_maxMpCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->mBE0_maxMp[battleResultCharWindows[i]->m15EE_maxMpCount.m0_count], (j + 14) * 8, i * 32 + 40);
                }
            }
            battleResultCharWindows[i]->m15EE_maxMpCount.m1_oddOrEven = battleOddOrEven;
        }
    }
}

void setupResultLevelValues(u8 current) {
    s32 i;
    s32 j;
    u8 digit;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15E4_levelCount.m0_count = 0;
        battleResultCharWindows[i]->m15E6_etherLevelCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (!current) {
                printIntegerToStringBattle(apConfigArray[i].m5_savedLevels[0]);
            } else {
                printIntegerToStringBattle(gameState.m26C_party[battleCharacters[i]].m62_Level);
            }
            for (j = 0; j < 3; j++) {
                digit = printIntegerToStringBattleBuffer[j + 6];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15E4_levelCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->m780_level[battleResultCharWindows[i]->m15E4_levelCount.m0_count], (j + 0x12) * 8, i * 32 + 32);
                }
            }
            battleResultCharWindows[i]->m15E4_levelCount.m1_oddOrEven = battleOddOrEven;
            if (battleResultCharWindows[0]->m15F8_showEther) {
                if (!current) {
                    printIntegerToStringBattle(apConfigArray[i].m5_savedLevels[1]);
                } else {
                    printIntegerToStringBattle(gameState.m26C_party[battleCharacters[i]].m63_Level2);
                }
                for (j = 0; j < 3; j++) {
                    digit = printIntegerToStringBattleBuffer[j + 6];
                    if (digit != 0xFF) {
                        battleResultCharWindows[i]->m15E6_etherLevelCount.m0_count += battleSetupStringInPolyFT4Large(
                            digit, &battleResultCharWindows[i]->m870_etherLevel[battleResultCharWindows[i]->m15E6_etherLevelCount.m0_count], (j + 0x12) * 8, i * 32 + 40);
                    }
                }
                battleResultCharWindows[i]->m15E6_etherLevelCount.m1_oddOrEven = battleOddOrEven;
                for (j = 0; j < battleResultCharWindows[i]->m15E4_levelCount.m0_count; j++) {
                    SetShadeTex(&battleResultCharWindows[i]->m780_level[0][j * 2 + battleOddOrEven], 0);
                    (battleResultCharWindows[i]->m780_level[0] + (j * 2 + battleOddOrEven))->r0 = 0x80;
                    (battleResultCharWindows[i]->m780_level[0] + (j * 2 + battleOddOrEven))->g0 = 0x40;
                    (battleResultCharWindows[i]->m780_level[0] + (j * 2 + battleOddOrEven))->b0 = 0x40;
                }
                for (j = 0; j < battleResultCharWindows[i]->m15E6_etherLevelCount.m0_count; j++) {
                    SetShadeTex(&battleResultCharWindows[i]->m870_etherLevel[0][j * 2 + battleOddOrEven], 0);
                    (battleResultCharWindows[i]->m870_etherLevel[0] + (j * 2 + battleOddOrEven))->r0 = 0x40;
                    (battleResultCharWindows[i]->m870_etherLevel[0] + (j * 2 + battleOddOrEven))->g0 = 0x80;
                    (battleResultCharWindows[i]->m870_etherLevel[0] + (j * 2 + battleOddOrEven))->b0 = 0x40;
                }
            }
        }
    }
}

void setupResultExpValues(void) {
    s32 i;
    s32 j;
    u8 digit;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15F0_expCount.m0_count = 0;
        battleResultCharWindows[i]->m15F2_etherExpCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            printIntegerToStringBattle(battleEntities.mFD0_savedM3C[i][0]);
            for (j = 0; j < 8; j++) {
                digit = printIntegerToStringBattleBuffer[j + 1];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15F0_expCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->mC80_exp[battleResultCharWindows[i]->m15F0_expCount.m0_count], (j + 0x16) * 8, i * 32 + 32);
                }
            }
            battleResultCharWindows[i]->m15F0_expCount.m1_oddOrEven = battleOddOrEven;
            if (battleResultCharWindows[0]->m15F8_showEther) {
                printIntegerToStringBattle(battleEntities.mFD0_savedM3C[i][1]);
                for (j = 0; j < 8; j++) {
                    digit = printIntegerToStringBattleBuffer[j + 1];
                    if (digit != 0xFF) {
                        battleResultCharWindows[i]->m15F2_etherExpCount.m0_count += battleSetupStringInPolyFT4Large(
                            digit, &battleResultCharWindows[i]->mF00_etherExp[battleResultCharWindows[i]->m15F2_etherExpCount.m0_count], (j + 0x16) * 8, i * 32 + 40);
                    }
                }
                battleResultCharWindows[i]->m15F2_etherExpCount.m1_oddOrEven = battleOddOrEven;
            }
        }
    }
}

void setupResultExpGainedValues(void) {
    s32 i;
    s32 j;
    u8 digit;

    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i]->m15F4_expGainedCount.m0_count = 0;
        battleResultCharWindows[i]->m15F6_etherExpGainedCount.m0_count = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            printIntegerToStringBattle(battleEntities.mFE8_experienceToAdd[i][0]);
            for (j = 0; j < 7; j++) {
                digit = printIntegerToStringBattleBuffer[j + 2];
                if (digit != 0xFF) {
                    battleResultCharWindows[i]->m15F4_expGainedCount.m0_count += battleSetupStringInPolyFT4Large(
                        digit, &battleResultCharWindows[i]->m1180_expGained[battleResultCharWindows[i]->m15F4_expGainedCount.m0_count], (j + 0x1F) * 8, i * 32 + 32);
                }
            }
            battleResultCharWindows[i]->m15F4_expGainedCount.m1_oddOrEven = battleOddOrEven;
            if (battleResultCharWindows[0]->m15F8_showEther) {
                printIntegerToStringBattle(battleEntities.mFE8_experienceToAdd[i][1]);
                for (j = 0; j < 7; j++) {
                    digit = printIntegerToStringBattleBuffer[j + 2];
                    if (digit != 0xFF) {
                        battleResultCharWindows[i]->m15F6_etherExpGainedCount.m0_count += battleSetupStringInPolyFT4Large(
                            digit, &battleResultCharWindows[i]->m13B0_etherExpGained[battleResultCharWindows[i]->m15F6_etherExpGainedCount.m0_count], (j + 0x1F) * 8, i * 32 + 40);
                    }
                }
                battleResultCharWindows[i]->m15F6_etherExpGainedCount.m1_oddOrEven = battleOddOrEven;
            }
        }
    }
}

void setupStatsPolyG4(POLY_G4* p, u8 type) {
    u8 color[3];
    s32 i;

    switch (type) {
    case 0:
        color[0] = 0xFF;
        color[1] = 0x80;
        color[2] = 0x80;
        break;
    case 1:
        color[0] = 0x80;
        color[1] = 0xFF;
        color[2] = 0x80;
        break;
    case 2:
        color[0] = 0xFF;
        color[1] = 0;
        color[2] = 0;
        break;
    case 3:
        color[0] = 0;
        color[1] = 0;
        color[2] = 0xFF;
        break;
    }
    for (i = 0; i < 2; i++) {
        SetPolyG4(&p[i]);
        p[i].r0 = color[0];
        p[i].g0 = color[1];
        p[i].b0 = color[2];
        p[i].r1 = color[0];
        p[i].g1 = color[1];
        p[i].b1 = color[2];
        p[i].r2 = 0;
        p[i].g2 = 0;
        p[i].b2 = 0;
        p[i].r3 = 0;
        p[i].g3 = 0;
        p[i].b3 = 0;
    }
}

void tintStatChangeDigits(POLY_FT4* p, u8 gain, u8 count, u8 oddOrEven) {
    u8 color[3];
    s32 i;

    color[1] = 0x40;
    if (gain == 0) {
        color[0] = 0x80;
        color[2] = 0x40;
    } else {
        color[0] = 0x40;
        color[2] = 0x80;
    }
    for (i = 0; i < count; i++) {
        SetShadeTex(&p[i * 2 + oddOrEven], 0);
        p[i * 2 + oddOrEven].r0 = color[0];
        p[i * 2 + oddOrEven].g0 = color[1];
        p[i * 2 + oddOrEven].b0 = color[2];
    }
}

void computeStatBarWidths(u8 before, u8 after, s32 max) {
    statBarValueBefore = before;
    statBarValueAfter = after;
    statBarValueDelta = after - before;
    statBarBaseWidth = before * 100 / max * 6400 / 10000;
    if (statBarValueDelta >= 0) {
        statBarDeltaColor = 2;
        statBarDeltaSignGlyph = 0xE3;
    } else {
        statBarDeltaColor = 3;
        statBarDeltaSignGlyph = 0xE5;
        statBarValueDelta = before - after;
    }
    statBarDeltaWidth = statBarValueDelta * 100 / max * 6400 / 10000;
}

s32 getHighestLevelUpStat(u8 arg0) {
    s32 i;
    u8 max = 0;

    for (i = 0; i < 7; i++) {
        if (battleEntities.m1028_savedStats[arg0][i] >= max) {
            max = battleEntities.m1028_savedStats[arg0][i];
        }
        if (battleEntities.m1040_newStats[arg0][i] >= max) {
            max = battleEntities.m1040_newStats[arg0][i];
        }
    }
    return max;
}

#if VERSION_JP
#define LEVELUP_NAME_X 0x44
#else
#define LEVELUP_NAME_X 0x3E
#endif

void setupLevelUpWindowLabels(u8 c) {
    s16 colors[6];
    s32 i;
    s32 k;
    s32 start;

    colors[0] = 0x80;
    colors[1] = 0x40;
    colors[2] = 0x40;
    colors[3] = 0x40;
    colors[4] = 0x40;
    colors[5] = 0x40;
    battleResultScreen->m3A50_nameCount.m0_count = battleSetupStringInPolyFT4Large(c + 0xFC, battleResultScreen->m0_name, LEVELUP_NAME_X, 0xA4);
    battleResultScreen->m3A50_nameCount.m1_oddOrEven = battleOddOrEven;
    battleResultScreen->m3A52_levelUpLabelsCount.m0_count = 0;
    for (i = 0; i < 27; i++) {
        start = battleResultScreen->m3A52_levelUpLabelsCount.m0_count;
        if (levelUpWindowLabelDefs[i * 3] != 0xFF) {
            battleResultScreen->m3A52_levelUpLabelsCount.m0_count +=
                battleSetupStringInPolyFT4Large(levelUpWindowLabelDefs[i * 3], &battleResultScreen->m140_levelUpLabels[start], levelUpWindowLabelX[i], levelUpWindowLabelY[i]);
            if (levelUpWindowLabelDefs[i * 3 + 1] != 0) {
                for (k = start; k < battleResultScreen->m3A52_levelUpLabelsCount.m0_count; k++) {
                    SetShadeTex(&battleResultScreen->m140_levelUpLabels[0][k * 2 + battleOddOrEven], 0);
                    (battleResultScreen->m140_levelUpLabels[0] + (k * 2 + battleOddOrEven))->r0 = colors[levelUpWindowLabelDefs[i * 3 + 2] * 3];
                    (battleResultScreen->m140_levelUpLabels[0] + (k * 2 + battleOddOrEven))->g0 = colors[levelUpWindowLabelDefs[i * 3 + 2] * 3 + 1];
                    (battleResultScreen->m140_levelUpLabels[0] + (k * 2 + battleOddOrEven))->b0 = colors[levelUpWindowLabelDefs[i * 3 + 2] * 3 + 2];
                }
            }
        }
    }
    battleResultScreen->m3A52_levelUpLabelsCount.m1_oddOrEven = battleOddOrEven;
}

void setupLevelUpMaxHpValue(u8 c) {
    s32 i;
    u8 digit;

    battleResultScreen->m3A54_maxHpCount.m0_count = 0;
    battleResultScreen->m3A58_maxMpCount.m0_count = 0;
    battleResultScreen->m3A56_maxHpGainCount.m0_count = 0;
    battleResultScreen->m3A5A_maxMpGainCount.m0_count = 0;
    printIntegerToStringBattle(battleEntities.m1000_savedMaxHpMp[c][0]);
    for (i = 0; i < 3; i++) {
        digit = printIntegerToStringBattleBuffer[i + 6];
        if (digit != 0xFF) {
            battleResultScreen->m3A54_maxHpCount.m0_count +=
                battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m1630_maxHp[battleResultScreen->m3A54_maxHpCount.m0_count], (i + 0x17) * 8, 0x80);
        }
    }
    battleResultScreen->m3A54_maxHpCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void setupLevelUpMaxMpValue(u8 c) {
    s32 i;
    u8 digit;

    printIntegerToStringBattle(battleEntities.m1000_savedMaxHpMp[c][1]);
    for (i = 0; i < 2; i++) {
        digit = printIntegerToStringBattleBuffer[i + 7];
        if (digit != 0xFF) {
            battleResultScreen->m3A58_maxMpCount.m0_count +=
                battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m1720_maxMp[battleResultScreen->m3A58_maxMpCount.m0_count], (i + 0x18) * 8, 0x88);
        }
    }
    battleResultScreen->m3A58_maxMpCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void setupLevelUpMaxHpGain(u8 c) {
    s32 diff;
    s32 i;
    u8 sign;
    u8 digit;
    s32 n;
    s32 before;
    s32 after;

    sign = 0xE3;
    before = battleEntities.m1000_savedMaxHpMp[c][0];
    after = gameState.m26C_party[battleCharacters[c]].m4E_MaxHP;
    diff = after - before;
    if (diff < 0) {
        sign = 0xE5;
        diff = before - after;
    }
    if (diff != 0) {
        battleResultScreen->m3A56_maxHpGainCount.m0_count = battleSetupStringInPolyFT4Large(sign, battleResultScreen->m17C0_maxHpGain, 0xD8, 0x80);
        printIntegerToStringBattle(diff);
        n = 0;
        for (i = 0; i < 3; i++) {
            digit = printIntegerToStringBattleBuffer[i + 6];
            if (digit != 0xFF) {
                battleResultScreen->m3A56_maxHpGainCount.m0_count +=
                    battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m17C0_maxHpGain[battleResultScreen->m3A56_maxHpGainCount.m0_count], n * 8 + 0xE0, 0x80);
                n++;
            }
        }
        for (i = 0; i < battleResultScreen->m3A56_maxHpGainCount.m0_count; i++) {
            SetShadeTex(&battleResultScreen->m17C0_maxHpGain[0][i * 2 + battleOddOrEven], 0);
            (battleResultScreen->m17C0_maxHpGain[0] + (i * 2 + battleOddOrEven))->r0 = 0x80;
            (battleResultScreen->m17C0_maxHpGain[0] + (i * 2 + battleOddOrEven))->g0 = 0x40;
            (battleResultScreen->m17C0_maxHpGain[0] + (i * 2 + battleOddOrEven))->b0 = 0x40;
        }
        battleResultScreen->m3A56_maxHpGainCount.m1_oddOrEven = battleOddOrEven;
    }
}

void setupLevelUpMaxMpGain(u8 c) {
    s32 diff;
    s32 i;
    u8 sign;
    u8 digit;
    s32 n;
    s32 before;
    s32 after;

    sign = 0xE3;
    before = battleEntities.m1000_savedMaxHpMp[c][1];
    after = gameState.m26C_party[battleCharacters[c]].m52_MaxMP;
    diff = after - before;
    if (diff < 0) {
        sign = 0xE5;
        diff = before - after;
    }
    if (diff != 0) {
        battleResultScreen->m3A5A_maxMpGainCount.m0_count = battleSetupStringInPolyFT4Large(sign, battleResultScreen->m1900_maxMpGain, 0xD8, 0x88);
        printIntegerToStringBattle(diff);
        n = 0;
        for (i = 0; i < 2; i++) {
            digit = printIntegerToStringBattleBuffer[i + 7];
            if (digit != 0xFF) {
                battleResultScreen->m3A5A_maxMpGainCount.m0_count +=
                    battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m1900_maxMpGain[battleResultScreen->m3A5A_maxMpGainCount.m0_count], n * 8 + 0xE0, 0x88);
                n++;
            }
        }
        for (i = 0; i < battleResultScreen->m3A5A_maxMpGainCount.m0_count; i++) {
            SetShadeTex(&battleResultScreen->m1900_maxMpGain[0][i * 2 + battleOddOrEven], 0);
            (battleResultScreen->m1900_maxMpGain[0] + (i * 2 + battleOddOrEven))->r0 = 0x80;
            (battleResultScreen->m1900_maxMpGain[0] + (i * 2 + battleOddOrEven))->g0 = 0x40;
            (battleResultScreen->m1900_maxMpGain[0] + (i * 2 + battleOddOrEven))->b0 = 0x40;
        }
        battleResultScreen->m3A5A_maxMpGainCount.m1_oddOrEven = battleOddOrEven;
    }
}

void setupLevelUpMaxHpMp(u8 arg0) {
    setupLevelUpMaxHpValue(arg0);
    setupLevelUpMaxMpValue(arg0);
    setupLevelUpMaxHpGain(arg0);
    setupLevelUpMaxMpGain(arg0);
}

void setupLevelUpStatBars(u8 c) {
    s32 max;
    s32 i;
    s32 j;
    s32 n;
    u8 digit;
    s32 x;
    s32 w;

    max = getHighestLevelUpStat(c);
    for (i = 0; i < 7; i++) {
        battleResultScreen->m3A5C_statNameCount[i] = 0;
        battleResultScreen->m3A6A_statValueCount[i] = 0;
        computeStatBarWidths(battleEntities.m1028_savedStats[c][i], battleEntities.m1040_newStats[c][i], max);
        setupStatsPolyG4(battleResultScreen->m2940_statBars[i], 0);
        setupStatsPolyG4(battleResultScreen->m2B38_statBarGains[i], statBarDeltaColor);
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->x0 = 0x78;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->y0 = i * 8 + 0x92;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->x1 = statBarBaseWidth + 0x78;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->y1 = i * 8 + 0x92;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->x2 = 0x78;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->y2 = i * 8 + 0x98;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->x3 = statBarBaseWidth + 0x78;
        (battleResultScreen->m2940_statBars[i] + D_800CCB04.m30_battleOddOrEven)->y3 = i * 8 + 0x98;
        if (statBarDeltaColor == 2) {
            x = statBarBaseWidth + 0x78;
        } else {
            w = statBarDeltaWidth - 0x78;
            x = statBarBaseWidth - w;
        }
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->x0 = x;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->y0 = i * 8 + 0x92;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->x1 = x + statBarDeltaWidth;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->y1 = i * 8 + 0x92;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->x2 = x;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->y2 = i * 8 + 0x98;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->x3 = x + statBarDeltaWidth;
        (battleResultScreen->m2B38_statBarGains[i] + D_800CCB04.m30_battleOddOrEven)->y3 = i * 8 + 0x98;
        battleResultScreen->m3A78_statBarOddOrEven[i] = battleOddOrEven;
        printIntegerToStringBattle(statBarValueBefore);
        for (j = 0; j < 3; j++) {
            digit = printIntegerToStringBattleBuffer[j + 6];
            if (digit != 0xFF) {
                battleResultScreen->m3A5C_statNameCount[i] += battleSetupStringInPolyFT4Large(
                    digit, &battleResultScreen->m19F0_statNames[i][battleResultScreen->m3A5C_statNameCount[i]], (j + 0x17) * 8,
                    i * 8 + 0x90);
            }
        }
        battleResultScreen->m3A63_statNameOddOrEven[i] = battleOddOrEven;
        if (statBarValueDelta != 0) {
            battleResultScreen->m3A6A_statValueCount[i] =
                battleSetupStringInPolyFT4Large(statBarDeltaSignGlyph, battleResultScreen->m2080_statValues[i], 0xD8, i * 8 + 0x90);
            printIntegerToStringBattle(statBarValueDelta);
            n = 0;
            for (j = 0; j < 3; j++) {
                digit = printIntegerToStringBattleBuffer[j + 6];
                if (digit != 0xFF) {
                    battleResultScreen->m3A6A_statValueCount[i] += battleSetupStringInPolyFT4Large(
                        digit, &battleResultScreen->m2080_statValues[i][battleResultScreen->m3A6A_statValueCount[i]], n * 8 + 0xE0,
                        i * 8 + 0x90);
                    n++;
                }
            }
            tintStatChangeDigits(battleResultScreen->m2080_statValues[i][0], statBarDeltaColor - 2, battleResultScreen->m3A6A_statValueCount[i],
                          D_800CCB04.m30_battleOddOrEven);
            battleResultScreen->m3A71_statValueOddOrEven[i] = D_800CCB04.m30_battleOddOrEven;
        }
    }
}

void playResultSoundEffect(u8 arg0) {
    playSoundEffectInFreeSlot((gBattleCurrentSoundEffectSeq->u14.m14_id << 16) | arg0);
}

void playVictoryFanfare(void) {
    if (resultFanfareSoundsPlayed == 0) {
        victoryFanfarePlayed = 1;
        playResultSoundEffect(0x5C);
        playResultSoundEffect(0x5D);
        playResultSoundEffect(0x5E);
        resultFanfareSoundsPlayed = 1;
    }
}

void waitForResultConfirm(void) {
    playVictoryFanfare();
    battleRenderDebugAndMain();
    battleVar1->mCF_isWaitingForInput = 1;
    while (battleInputButton != 4) {
        battleRenderDebugAndMain();
    }
    battleVar1->mCF_isWaitingForInput = 0;
}

#if VERSION_JP
#define LEARNED_NAME_X 0x44
#else
#define LEARNED_NAME_X 0x40
#endif

void showLearnedDeathblowsAndAbilities(u8 slot) {
    RECT rect;
    u16 newDeathblows;
    u16 newAbilities;
    u16* image;
    u8 width;
    s32 i;

    newDeathblows = gameState.m16C0_abilities[battleCharacters[slot]].m0_unlockedAbilitiesBitField[0] & ~battleStartUnlockedAbilities[slot][0];
    newAbilities = gameState.m16C0_abilities[battleCharacters[slot]].m0_unlockedAbilitiesBitField[1] & ~battleStartUnlockedAbilities[slot][1];
    if (newDeathblows || newAbilities) {
        if (!battleVar1->mB0_isDialogWindowInitialized[1]) {
            allocateAndSetupBattleDialogWindow(1, 0x28, 0x78, 0xE8, 0x58, 0, 1);
        }
        battleResultScreen->m3A50_nameCount.m0_count = battleSetupStringInPolyFT4Large(slot + 0xFC, battleResultScreen->m0_name, LEARNED_NAME_X, 0xA4);
        battleResultScreen->m3A50_nameCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
        battleVar1->mCF_isWaitingForInput = 0;
        image = allocateTextureRamForText(0x1B);
        battleInitPoly4(battleResultScreen->m34B0_learnedText[0], 1, 2);
        battleInitPoly4(battleResultScreen->m34B0_learnedText[1], 0, 3);
        for (i = 0; i < 16; i++) {
            if (bitmaskCharacterCheckReverse(newDeathblows, i)) {
                width = renderString(getDeathblowName(battleCharacters[slot], i), image, 0x1B, 0);
                rect.x = 0x3C0;
                rect.y = 0x1A;
                rect.w = 0x1E;
                rect.h = 13;
                loadImageSync(&rect, image);
                repositionTextRenderingPlanePrim(&battleResultScreen->m34B0_learnedText[1][D_800CCB04.m30_battleOddOrEven], 0x52, 0x9C, 0, 0x1A, width);
                SetShadeTex(&battleResultScreen->m34B0_learnedText[1][D_800CCB04.m30_battleOddOrEven], 0);
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->r0 = 0;
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->g0 = 0x80;
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->b0 = 0;
                battleResultScreen->m3A88_learnedTextOddOrEven[1] = D_800CCB04.m30_battleOddOrEven;
                repositionTextRenderingPlanePrim(&battleResultScreen->m34B0_learnedText[0][D_800CCB04.m30_battleOddOrEven], width + 0x5A, 0x9C, battleLabelGlyphRects[14].m2_u,
                              battleLabelGlyphRects[14].m3_v, battleLabelGlyphRects[14].m0_width);
                battleResultScreen->m3A88_learnedTextOddOrEven[0] = D_800CCB04.m30_battleOddOrEven;
                battleVar1->m8F_drawLearnedMessage = 1;
                waitForResultConfirm();
            }
        }
        for (i = 0; i < 16; i++) {
            if (bitmaskCharacterCheckReverse(newAbilities, i)) {
                width = renderString(drawCircleMenuChi_updateSub0Sub1(battleCharacters[slot] * 16 + i), image, 0x1B, 0);
                rect.x = 0x3C0;
                rect.y = 0x1A;
                rect.w = 0x1E;
                rect.h = 13;
                loadImageSync(&rect, image);
                repositionTextRenderingPlanePrim(&battleResultScreen->m34B0_learnedText[1][D_800CCB04.m30_battleOddOrEven], 0x52, 0x9C, 0, 0x1A, width);
                SetShadeTex(&battleResultScreen->m34B0_learnedText[1][D_800CCB04.m30_battleOddOrEven], 0);
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->r0 = 0x80;
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->g0 = 0x80;
                (battleResultScreen->m34B0_learnedText[1] + battleOddOrEven)->b0 = 0;
                battleResultScreen->m3A88_learnedTextOddOrEven[1] = D_800CCB04.m30_battleOddOrEven;
                repositionTextRenderingPlanePrim(&battleResultScreen->m34B0_learnedText[0][D_800CCB04.m30_battleOddOrEven], width + 0x5A, 0x9C, battleLabelGlyphRects[14].m2_u,
                              battleLabelGlyphRects[14].m3_v, battleLabelGlyphRects[14].m0_width);
                battleResultScreen->m3A88_learnedTextOddOrEven[0] = D_800CCB04.m30_battleOddOrEven;
                battleVar1->m8F_drawLearnedMessage = 1;
                waitForResultConfirm();
            }
        }
        battleVar1->m8F_drawLearnedMessage = 0;
    }
}

void setupRewardsWindowLabels(void) {
    s32 i;

    for (i = 0; i < 7; i++) {
        battleResultScreen->m3A7F_rewardLabelsCount += battleSetupStringInPolyFT4Large(rewardsWindowLabelGlyphs[i], &battleResultScreen->m2D30_rewardLabels[battleResultScreen->m3A7F_rewardLabelsCount], rewardsWindowLabelX[i],
                                           rewardsWindowLabelY[i]);
    }
    battleResultScreen->m3A80_rewardLabelsOddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void setupRewardsGoldValues(u32 gold) {
    s32 i;
    u8 digit;

    printIntegerToStringBattle(gold);
    for (i = 0; i < 6; i++) {
        digit = printIntegerToStringBattleBuffer[i + 3];
        if (digit != 0xFF) {
            battleResultScreen->m3A81_goldWonCount.m0_count +=
                battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m2F60_goldWon[battleResultScreen->m3A81_goldWonCount.m0_count], (i + 0x1B) * 8, 0x50);
        }
    }
    battleResultScreen->m3A81_goldWonCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    printIntegerToStringBattle(gameState.m1924_gold);
    for (i = 0; i < 9; i++) {
        digit = printIntegerToStringBattleBuffer[i];
        if (digit != 0xFF) {
            battleResultScreen->m3A83_totalGoldCount.m0_count +=
                battleSetupStringInPolyFT4Large(digit, &battleResultScreen->m3140_totalGold[battleResultScreen->m3A83_totalGoldCount.m0_count], (i + 0x18) * 8, 0x60);
        }
    }
    battleResultScreen->m3A83_totalGoldCount.m1_oddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void setupRewardsWindowTitles(void) {
    battleInitPoly4(battleResultScreen->m3410_rewardTitles[0], 0, 2);
    battleInitPoly4(battleResultScreen->m3410_rewardTitles[1], 1, 2);
    repositionTextRenderingPlanePrim(&battleResultScreen->m3410_rewardTitles[0][D_800CCB04.m30_battleOddOrEven], 0x20, 0x20, battleLabelGlyphRects[10].m2_u, battleLabelGlyphRects[10].m3_v, battleLabelGlyphRects[10].m0_width);
    repositionTextRenderingPlanePrim(&battleResultScreen->m3410_rewardTitles[1][D_800CCB04.m30_battleOddOrEven], 0xB8, 0x40, battleLabelGlyphRects[11].m2_u, battleLabelGlyphRects[11].m3_v, battleLabelGlyphRects[11].m0_width);
    battleResultScreen->m3A87_rewardTitlesOddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void addItemToInventoryList(u8 id, u8 count, u8* ids, u8* counts, u8 n) {
    s32 i;

    for (i = 0; i < n; i++) {
        if (ids[i] == id) {
            if (counts[i] + count >= 100) {
                counts[i] = 99;
            } else {
                counts[i] += count;
            }
            break;
        }
    }
    if (i == n) {
        for (i = 0; i < n; i++) {
            if (ids[i] == 0) {
                ids[i] = id;
                counts[i] = count;
                return;
            }
        }
    }
}

void addItemDropsToInventory(u8* ids, u8* counts, u8* categories) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (ids[i] != 0) {
            switch (categories[i]) {
            case 0:
                addItemToInventoryList(ids[i], counts[i], gameState.m1D9C_inventory1Ids, gameState.m1D38_inventory1Counts, 100);
                break;
            case 1:
                addItemToInventoryList(ids[i], counts[i], gameState.m1EC8_inventory2Ids, gameState.m1E00_inventory2Counts, 200);
                break;
            case 2:
                addItemToInventoryList(ids[i], counts[i], gameState.m2026_inventory0Ids, gameState.m1F90_inventory0Counts, 150);
                break;
            case 3:
                addItemToInventoryList(ids[i], counts[i], gameState.m2120_inventory3Ids, gameState.m20BC_inventory3Counts, 100);
                break;
            case 4:
                addItemToInventoryList(ids[i], counts[i], gameState.m221A_inventory4Ids, gameState.m2184_inventory4Counts, 150);
                break;
            }
        }
    }
}

void collectItemDrops(u8* ids, u8* counts, u8* categories) {
    s32 i;
    s32 j;
    s32 k;

    for (i = 0; i < 8; i++) {
        ids[i] = 0;
        counts[i] = 0;
    }
    k = 0;
    for (i = 0; i < 8; i++) {
        if (battleEntities.m1014_itemDropIds[i] != 0) {
            for (j = 0; j < 8; j++) {
                if (battleEntities.m100C_itemDropCategories[i] == categories[j] && battleEntities.m1014_itemDropIds[i] == ids[j]) {
                    counts[j]++;
                    break;
                }
            }
            if (j == 8) {
                categories[k] = battleEntities.m100C_itemDropCategories[i];
                ids[k] = battleEntities.m1014_itemDropIds[i];
                counts[k]++;
                k++;
            }
        }
    }
}

void setupRewardsItemList(void) {
    u8 ids[8];
    u8 categories[8];
    u8 counts[8];
    RECT rect;
    u16* images[8];
    s32 i;
    s32 n;
    u8 width;
    u16** img;

    collectItemDrops(ids, counts, categories);
    for (i = 0, n = 0; i < 8; i++) {
        img = images;
        if (ids[i] != 0) {
            battleInitPoly4(battleResultScreen->m3550_itemNames[n], 0, 1);
            battleInitPoly4(battleResultScreen->m37D0_itemCounts[n], 0, 2);
            img[n] = allocateTextureRamForText(0x1B);
            switch (categories[i]) {
            case 0:
                width = renderString(getWeaponName(ids[i]), img[n], 0x1B, 0);
                break;
            case 1:
                width = renderString(getAccessoryName(ids[i]), img[n], 0x1B, 0);
                break;
            case 2:
                width = renderString(getUsableItemName(ids[i]), img[n], 0x1B, 0);
                break;
            case 3:
                width = renderString(getGearWeaponName(ids[i]), img[n], 0x1B, 0);
                break;
            case 4:
                width = renderString(getGearAccessoryName(ids[i]), img[n], 0x1B, 0);
                break;
            }
            rect.x = 0x380;
            rect.y = n * 13 + 0x100;
            rect.w = 0x1E;
            rect.h = 13;
            loadImageSync(&rect, img[n]);
            repositionTextRenderingPlanePrim(&battleResultScreen->m3550_itemNames[n][battleOddOrEven], 0x2C, n * 16 + 0x30, 0, n * 13, width);
            repositionTextRenderingPlanePrim(&battleResultScreen->m37D0_itemCounts[n][battleOddOrEven], 0x94, n * 16 + 0x30, counts[i] * 8 + 0x78, 0, 8);
            n++;
        }
    }
    addItemDropsToInventory(ids, counts, categories);
    for (i = 0; i < n; i++) {
        freeHeap(images[i]);
    }
    battleResultScreen->m3A85_numItems = n;
    battleResultScreen->m3A86_itemsOddOrEven = battleOddOrEven;
}

void showResultCharacterWindows(void) {
    u32 step;
    u8 running;

    step = 0;
    running = 1;
    do {
        battleRenderDebugAndMain();
        switch (step) {
        case 0:
            setupResultCharacterNames();
            battleVar1->mA0_drawResultCharacterWindows = 1;
            battleVar2->m2DB_turnEnded = 0;
            break;
        case 1:
            setupResultWindowLabels();
            break;
        case 2:
            setupResultHpMpValues();
            break;
        case 3:
            setupResultLevelValues(0);
            break;
        case 4:
            setupResultExpValues();
            break;
        case 5:
            setupResultExpGainedValues();
            running = 0;
            break;
        }
        step++;
    } while (running);
    battleVar1->mCF_isWaitingForInput = 1;
    while (battleInputButton != 4) {
        battleRenderDebugAndMain();
    }
    battleVar1->mCF_isWaitingForInput = 0;
}

void animateResultExpGain(void) {
    s32 i;

    battleRenderDebugAndMain();
    battleVar1->mCF_isWaitingForInput = 1;
    battleResultCharWindows[0]->m15F9_isCountingExp = 1;
    while (battleResultCharWindows[0]->m15F9_isCountingExp != 0) {
        if (battleInputButton == 4) {
            break;
        }
        playResultSoundEffect(0x5B);
        battleRenderDebugAndMain();
    }
    battleVar1->mCF_isWaitingForInput = 0;
    battleResultCharWindows[0]->m15F9_isCountingExp = 0;
    for (i = 0; i < 3; i++) {
        battleEntities.mFD0_savedM3C[i][0] = gameState.m26C_party[battleCharacters[i]].m3C_experience;
        battleEntities.mFD0_savedM3C[i][1] = gameState.m26C_party[battleCharacters[i]].m40_experience2;
        battleEntities.mFE8_experienceToAdd[i][0] = 0;
        battleEntities.mFE8_experienceToAdd[i][1] = 0;
    }
    setupResultExpValues();
    setupResultExpGainedValues();
}

void showResultLevelUps(void) {
    s32 i;
    u8 wait;

    setupResultLevelValues(1);
    battleRenderDebugAndMain();
    battleVar1->mCF_isWaitingForInput = 1;
    while (battleInputButton != 4) {
        battleRenderDebugAndMain();
    }
    battleVar1->mCF_isWaitingForInput = 0;
    battleRenderDebugAndMain();
    allocateAndSetupBattleDialogWindow(1, 0x28, 0x78, 0xE8, 0x58, 0, 0);
    battleVar1->mB0_isDialogWindowInitialized[1] = 0;
    for (i = 0; i < 3; i++) {
        wait = 0;
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            if (battleEntities.m1022_levelsGained[i][0] != 0) {
                battleVar1->mB0_isDialogWindowInitialized[1] = 1;
                setupLevelUpWindowLabels(i);
                setupLevelUpMaxHpMp(i);
                setupLevelUpStatBars(i);
                battleVar1->mA1_drawLevelUpWindow = 1;
                playVictoryFanfare();
                battleVar1->mCF_isWaitingForInput = 0;
                wait = 1;
                battleRenderDebugAndMain();
            }
            battleVar1->mCF_isWaitingForInput = 1;
            while (battleInputButton != 4) {
                if (!wait) {
                    break;
                }
                battleRenderDebugAndMain();
            }
            battleVar1->mA1_drawLevelUpWindow = 0;
            battleRenderDebugAndMain();
            showLearnedDeathblowsAndAbilities(i);
            battleVar1->mCF_isWaitingForInput = 0;
            battleVar1->mB0_isDialogWindowInitialized[1] = 0;
        }
    }
}

void showResultRewards(u32 gold) {
    battleVar1->mA0_drawResultCharacterWindows = 0;
    battleVar1->mA1_drawLevelUpWindow = 0;
    battleVar1->m8F_drawLearnedMessage = 0;
    battleRenderDebugAndMain();
    allocateAndSetupBattleDialogWindow(0, 0x18, 0x18, 0x90, 0xA0, 0, 1);
    allocateAndSetupBattleDialogWindow(2, 0xB0, 0x38, 0x70, 0x38, 0, 1);
    battleRenderDebugAndMain();
    setupRewardsWindowLabels();
    setupRewardsGoldValues(gold);
    setupRewardsWindowTitles();
    setupRewardsItemList();
    battleVar1->mAC_drawRewardsWindow = 1;
    playSoundEffect((gBattleCurrentSoundEffectSeq->u14.m14_id << 16) | 0x5B);
    battleVar1->mCF_isWaitingForInput = 1;
    while (battleInputButton != 4) {
        battleRenderDebugAndMain();
    }
    battleVar1->mCF_isWaitingForInput = 0;
    battleVar1->mAC_drawRewardsWindow = 0;
    battleVar1->mB0_isDialogWindowInitialized[0] = 0;
    battleVar1->mB0_isDialogWindowInitialized[1] = 0;
    battleVar1->mB0_isDialogWindowInitialized[2] = 0;
    battleRenderDebugAndMain();
    clearBattleDialogWindow(0);
    clearBattleDialogWindow(1);
    clearBattleDialogWindow(2);
}

void runBattleResultScreen(u32 gold) {
    u8 savedRunning;
    s32 i;

    savedRunning = battleRunningVar1[0];
    battleVar1->mCF_isWaitingForInput = 0;
    for (i = 0; i < 3; i++) {
        battleResultCharWindows[i] = allocateBattleHeap(sizeof(sBattleResultChar), 0);
        bzero(battleResultCharWindows[i], sizeof(sBattleResultChar));
    }
    battleResultScreen = allocateBattleHeap(sizeof(sBattleResultScreen), 0);
    bzero(battleResultScreen, sizeof(sBattleResultScreen));
    battleResultCharWindows[0]->m15F8_showEther = !(gameState.m22B6_gameFlags & 0x8000);
    battleRenderDebugAndMain();
    battleRunningVar1[0] = 0;
    showResultCharacterWindows();
    animateResultExpGain();
    showResultLevelUps();
    showResultRewards(gold);
    battleVar1->mA0_drawResultCharacterWindows = 0;
    battleVar1->mA1_drawLevelUpWindow = 0;
    battleVar1->m8F_drawLearnedMessage = 0;
    battleRenderDebugAndMain();
    for (i = 0; i < 3; i++) {
        freeHeap(battleResultCharWindows[i]);
    }
    freeHeap(battleResultScreen);
    battleRunningVar1[0] = savedRunning;
    stopAllSounds();
}

void loadBattleResultAssets(void) {
    u8 unused[0x60]; /* unused: reproduces the frame size */
    uptr* data;
    u32* image;

    currentBattleMode = 0;
    battleVar1->mA0_drawResultCharacterWindows = 0;
    battleVar1->mA1_drawLevelUpWindow = 0;
    battleVar1->m8F_drawLearnedMessage = 0;
    battleVar1->mAC_drawRewardsWindow = 0;
    battleVar1->m7F_drawTimeBarFill[0] = battleVar1->m7F_drawTimeBarFill[1] = battleVar1->m7F_drawTimeBarFill[2] = 0;
    battleRenderDebugAndMain();
    battleRenderDebugAndMain();
    freeHeap(battleFont);
    setCurrentDirectory(0x10, 2);
    data = allocateBattleHeap(getFileSizeAligned(2), 1);
    readFile(2, data, 0, 0x80);
    idleBattleDuringLoading();
    /* The table stays as loaded; each entry is read once. */
    battleEntities.m5F20_levelUpData = mallocAndDecompress(PC_RELOC_ENTRY(data, 1), 0);
    image = (u32*)mallocAndDecompress(PC_RELOC_ENTRY(data, 3), 0);
    loadMenuImageBundle(image);
    freeHeap(image);
    battleFont = mallocAndDecompress(PC_RELOC_ENTRY(data, 2), 0);
    image = (u32*)mallocAndDecompress(PC_RELOC_ENTRY(data, 4), 0);
    loadBattleCharacterPortraits((u8*)image, 0xFC);
    freeHeap(image);
    freeHeap(data);
    loadItemTargetsLabel();
}

void processBattleRewards(void) {
    s32 i;
    u32 gold;

    battleEntities.m5F9C_experience = 0;
    battleEntities.m5FB4_defeatedEnemies = 0;
    gold = 0;
    if (isBattleAnEvent == 0) {
        for (i = 0; i < 8; i++) {
            if (isBattleSlotFilled.m0_isBattleSlotFilled[i + 3] && !battleVisualEntities[i + 3].m3_hidden && (battleEntities.m0_entities[i + 3].m0_base.m7C_statusFlags[0] & 0x8000) && !unknownMonsterStatus0[i].m3_removed) {
                battleEntities.m5F9C_experience += battleEntities.m0_entities[i + 3].m14C_experience;
                gold += battleEntities.m0_entities[i + 3].m156_gold;
                battleEntities.m5FB4_defeatedEnemies |= characterIdToTargetBitmask(i);
            }
        }
        gold = pcGameplayGold(gold);
        gameState.m1924_gold += gold;
        if ((u32)gameState.m1924_gold > 9999999) {
            gameState.m1924_gold = 9999999;
        }
        updateCharacterBlinkingTask(0);
    }
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] == 0x7F) {
            battleCharacters[i] = 0xFF;
        }
    }
    if (battleForceOnGear) {
        battleCharacters[2] = 0xFF;
        battleCharacters[1] = 0xFF;
        battleVisualEntities[2].m2_characterId = 0x7F;
        battleVisualEntities[1].m2_characterId = 0x7F;
    }
    updatePartyAfterBattle();
    if (!battleEventIsRunningFlag && !(currentBattleConfig.m1_flags & 8)) {
        runBattleResultScreen(gold);
    }
}

void copyBattleItemCountsToInventory(void) {
    u8* p;
    s32 i, j;

    for (i = 0, p = battleItemStartIds; i < 0x30; i++, p++) {
        if (*p != 0) {
            for (j = 0; j < 0x96; j++) {
                if (*p == gameState.m2026_inventory0Ids[j]) {
                    gameState.m1F90_inventory0Counts[j] = battleEffectBF.m12_counts[i];
                }
            }
        }
    }
}

void finishBattle(void) {
    s32 i;
    s32 j;

    loadBattleResultAssets();
    if (needBattleEventOverlay) {
        freeHeap(battleEventHeapMarker);
        freeHeap(battleEventOverlayHeap);
    }
    if (!battleInitVar1) {
        if (battleEventMoviePending) {
            loadGameModeOverlay(6);
        } else if (battleInitVar0) {
            loadGameModeOverlay(2);
        } else if ((gameState.m231A_fieldID & 0x7FF) >= 0x400) {
            loadGameModeOverlay(3);
        } else {
            resetFieldAndPreloadPartyFiles();
            idleBattleDuringLoading();
            loadGameModeOverlay(1);
        }
    }
    idleBattleDuringLoading();
    copyBattleItemCountsToInventory();
    if (!(battleRunningVar1[0] & 0xC0) && battleRunningVar1[0] != 0x21 && !isBattleAnEvent && menuReturnState0 != 3) {
        waitBattleAnimationSoundLoaded();
        processBattleRewards();
    }
    for (i = 0; i < 8; i += 2) {
        freeHeap(previousEntityTurn.m8_nameLabels[i].m58_ramBufferFromMonsterName);
    }
    for (j = 0; j < 10; j++) {
        freeHeap(itemLabelsIds[j].m0_image);
    }
    freeHeap(battleItemNameStrings);
    freeHeap(battleG3);
    clearBattleDialogWindow(5);
    clearBattleDialogWindow(4);
    freeHeap(battleAttackNameStrings);
    freeHeap(battleVar0);
    freeHeap(battleVar1);
    freeHeap(battleVar2);
    freeHeap(battleEntities.m5F20_levelUpData);
    freeHeap(battleFont);
    freeHeapForUser(2);
    if (battleInitVar1) {
        startMusicInstanceSub0(pCurrentBattleMusic);
        freeSoundInstance(pCurrentBattleMusic);
    }
    shutdownBattleSpriteSystem();
}

void updatePartyAfterBattle(void) {
    u8 i;
    u8 dead = 0;

    for (i = 0; i < 3; i++) {
        if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000) {
            dead++;
        }
    }
    if (dead != 3) {
        pLevelUpData = pResultBattleEntities->m5F20_levelUpData;
        distributeBattleExperience();
        learnSkillsAfterBattle();
        updateEnergyPerTurn();
        unlockGearAttacksByLevel();
        transferCharStatsBackToField_result();
        rollItemDrops();
        if (gameState.m26C_party[3].mA0_gearNum == 0x12) {
            gameState.m16C0_abilities[3].m1A_unlockedGearAbilities = 0x4000;
            if (gameState.m978_gears[12].m4F_frameRepairLevel) {
                gameState.m16C0_abilities[3].m1A_unlockedGearAbilities = 0xC000;
            }
        }
    }
}

void transferCharStatsBackToField_result(void) {
    u8 i;
    u8 j;
    sBattleEntity* e;
    GameStateCharacter* ch;
    GameStateGear* gear;
    GameStateGear* battleGear;
    s32 unused[2]; /* unused: reproduces the 8-byte frame */

    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] == 0xFF) {
            continue;
        }
        e = &pResultBattleEntities->m0_entities[i];
        ch = &pResultGameState->m26C_party[e->m0_base.m56_battleCommandLoadout];
        gear = &pResultGameState->m978_gears[e->m0_base.mA0_gearNum];
        battleGear = &e->mA4_gear;
        if (e->m0_base.m56_battleCommandLoadout == 7 && (battleEntities.m0_entities[i].m15A_flags & 0x80)) {
            e->m0_base.m4C_HP = (u32)(e->mA4_gear.m60_hp + 1) / 50;
            if (e->m0_base.m4C_HP == 0) {
                e->m0_base.m4C_HP = 1;
            }
        }
        ch->m4C_HP = e->m0_base.m4C_HP;
        ch->m50_MP = e->m0_base.m50_MP;
        if (ch->m4C_HP > ch->m4E_MaxHP) {
            ch->m4C_HP = ch->m4E_MaxHP;
        }
        if (ch->m50_MP > ch->m52_MaxMP) {
            ch->m50_MP = ch->m52_MaxMP;
        }
        for (j = 0; j < 7; j++) {
            ch->m90_deathblowProgress[j] = e->m0_base.m90_deathblowProgress[j];
        }
        ch->m3A_killCount = e->m0_base.m3A_killCount;
        if (e->m0_base.m7C_statusFlags[0] & 0xC000) {
            ch->m4C_HP = 1;
        }
        switch (e->m0_base.mA0_gearNum) {
        case 0 ... 6:
        case 8 ... 16:
            gear->m60_hp = battleGear->m60_hp;
            gear->m38_fuel = battleGear->m38_fuel;
            if ((u32)gear->m60_hp > (u32)gear->m64_maxHp) {
                gear->m60_hp = gear->m64_maxHp;
            }
            if (gear->m38_fuel > gear->m3A_maxFuel) {
                gear->m38_fuel = gear->m3A_maxFuel;
            }
            if (battleGear->m7C_statusFlags & 0x8000) {
                gear->m60_hp = (u32)gear->m64_maxHp / 10;
            }
            break;
        }
    }
}

void distributeBattleExperience(void) {
    s16 slotOf[11];
    u16 i;
    u16 dead;
    u32 exp;
    s32 penalty;

    for (i = 0; i < 11; i++) {
        slotOf[i] = 100;
    }
    dead = 0;
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] == 0xFF) {
            dead++;
            continue;
        }
        pLevelUpCharacter = &pResultBattleEntities->m0_entities[i].m0_base;
        if (pLevelUpCharacter->m7C_statusFlags[0] & 0xC000) {
            dead++;
            slotOf[pLevelUpCharacter->m56_battleCommandLoadout] = 0xFF;
        } else {
            slotOf[pLevelUpCharacter->m56_battleCommandLoadout] = i;
        }
        levelsBeforeExp[i][0] = pLevelUpCharacter->m62_Level;
        levelsBeforeExp[i][1] = pLevelUpCharacter->m63_Level2;
    }
    exp = pResultBattleEntities->m5F9C_experience;
    penalty = pResultBattleEntities->m5FC4_expPenalty;
    if (penalty != 0) {
        exp -= exp / 4 * penalty;
    }
    for (i = 0; i < 11; i++) {
        if (slotOf[i] != 0xFF) {
            pLevelUpCharacter = &pResultGameState->m26C_party[i];
            if (slotOf[i] < 3) {
                computeExperienceToAdd(exp / (3 - dead), slotOf[i], 0);
            } else {
                computeExperienceToAdd(exp / 3, 0xFF, 1);
            }
            applyExperienceAndLevelUp();
        }
    }
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] != 0xFF) {
            pLevelUpCharacter = &pResultGameState->m26C_party[battleCharacters[i]];
            pResultBattleEntities->m1022_levelsGained[i][0] = pLevelUpCharacter->m62_Level - levelsBeforeExp[i][0];
            pResultBattleEntities->m1022_levelsGained[i][1] = pLevelUpCharacter->m63_Level2 - levelsBeforeExp[i][1];
            if (pLevelUpCharacter->m56_battleCommandLoadout == 4) {
                pResultBattleEntities->m1040_newStats[i][0] = pLevelUpCharacter->m4_weaponSlots[0].m0_weaponPower + pLevelUpCharacter->m4_weaponSlots[3].m0_weaponPower;
            } else {
                pResultBattleEntities->m1040_newStats[i][0] = pLevelUpCharacter->m58_attack + pLevelUpCharacter->m4_weaponSlots[0].m0_weaponPower;
            }
            pResultBattleEntities->m1040_newStats[i][1] = pLevelUpCharacter->m5E_hitPercentage;
            pResultBattleEntities->m1040_newStats[i][2] = pLevelUpCharacter->m59_defence + pLevelUpCharacter->m2D_accessoryDefence;
            pResultBattleEntities->m1040_newStats[i][3] = pLevelUpCharacter->m5F_evadePercentage;
            pResultBattleEntities->m1040_newStats[i][4] = pLevelUpCharacter->m5B_ether;
            pResultBattleEntities->m1040_newStats[i][5] = pLevelUpCharacter->m5C_etherDefence;
            pResultBattleEntities->m1040_newStats[i][6] = pLevelUpCharacter->m5A_agility;
        }
    }
}

void computeExperienceToAdd(u32 exp, s16 slot, s16 mode) {
    s16 a;
    s16 b;

    if (mode == 1) {
        exp = exp * 3 / 4;
        characterExpGain = exp;
        characterEtherExpGain = exp;
        characterExpGain = pcGameplayExperience(characterExpGain);
        characterEtherExpGain = pcGameplayExperience(characterEtherExpGain);
        return;
    }
    a = pResultBattleEntities->m0_entities[slot].m158_expShares[0];
    b = pResultBattleEntities->m0_entities[slot].m158_expShares[1];
    if (a < 2) {
        a = 1;
    }
    if (b < 2) {
        b = 1;
    }
    characterExpGain = exp * a / (a + b);
    characterEtherExpGain = exp * b / (a + b);
    if (pResultGameState->m22B6_gameFlags & 0x8000) {
        characterExpGain = exp;
        characterEtherExpGain = exp;
    }
    if (characterExpGain == 0) {
        characterExpGain = 1;
    }
    if (characterEtherExpGain == 0) {
        characterEtherExpGain = 1;
    }
    if (pLevelUpCharacter->m32_accessoryFlags & 0x2000) {
        characterExpGain += characterExpGain / 2;
        characterEtherExpGain += characterEtherExpGain / 2;
    }
    if (pLevelUpCharacter->m32_accessoryFlags & 0x1000) {
        characterEtherExpGain += characterEtherExpGain / 2;
    }
    /* Scale the final award once, including the values animated by results. */
    characterExpGain = pcGameplayExperience(characterExpGain);
    characterEtherExpGain = pcGameplayExperience(characterEtherExpGain);
    pResultBattleEntities->mFE8_experienceToAdd[slot][0] = characterExpGain;
    pResultBattleEntities->mFE8_experienceToAdd[slot][1] = characterEtherExpGain;
}

void applyExperienceAndLevelUp(void) {
    s32 rest;

    pLevelUpCharacter->m3C_experience += characterExpGain;
    pLevelUpCharacter->m40_experience2 += characterEtherExpGain;
    if (pLevelUpCharacter->m62_Level == 99 && (pResultGameState->m22B6_gameFlags & 0x8000)) {
        characterExpGain = 0;
    }
    if (pLevelUpCharacter->m63_Level2 == 99 && (pResultGameState->m22B6_gameFlags & 0x8000)) {
        characterEtherExpGain = 0;
    }
    if (pLevelUpCharacter->m62_Level == 99) {
        characterExpGain = 0;
    }
    if (pLevelUpCharacter->m63_Level2 == 99) {
        characterEtherExpGain = 0;
    }
    rest = pLevelUpCharacter->m44_expToNextLevel - characterExpGain;
    if (rest > 0) {
        pLevelUpCharacter->m44_expToNextLevel = rest;
    } else {
        do {
            if (++pLevelUpCharacter->m62_Level >= 100 && (pResultGameState->m22B6_gameFlags & 0x8000)) {
                pLevelUpCharacter->m62_Level--;
            }
            pLevelUpCharacter->m44_expToNextLevel = pLevelUpData->mBB0_expTable[pLevelUpCharacter->m62_Level - 1];
            levelUpCharacterStats();
            rest += pLevelUpCharacter->m44_expToNextLevel;
        } while (rest <= 0);
    }
    pLevelUpCharacter->m44_expToNextLevel = rest;
    rest = pLevelUpCharacter->m48_expToNextLevel2 - characterEtherExpGain;
    if (rest > 0) {
        pLevelUpCharacter->m48_expToNextLevel2 = rest;
    } else {
        do {
            if (++pLevelUpCharacter->m63_Level2 >= 100 && (pResultGameState->m22B6_gameFlags & 0x8000)) {
                pLevelUpCharacter->m63_Level2--;
            }
            pLevelUpCharacter->m48_expToNextLevel2 = pLevelUpData->mBB0_expTable[pLevelUpCharacter->m63_Level2 - 1];
            levelUpCharacterEtherStats();
            rest += pLevelUpCharacter->m48_expToNextLevel2;
        } while (rest <= 0);
    }
    pLevelUpCharacter->m48_expToNextLevel2 = rest;
}

void levelUpCharacterStats(void) {
    u8 hi;
    u8 maxLevel;
    u8 level;

    hi = 0;
    maxLevel = 100;
    if (pLevelUpCharacter->m62_Level >= 100) {
        hi = 1;
        maxLevel = 200;
    }
    level = pLevelUpCharacter->m62_Level;
    pLevelUpCharacter->m4E_MaxHP = rollMaxHpGrowth(pLevelUpCharacter->m4E_MaxHP, pLevelUpCharacter->m62_Level);
    pLevelUpCharacter->m58_attack = rollStatGrowth(
        pLevelUpCharacter->m58_attack, pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[0][hi],
        maxLevel, level);
    pLevelUpCharacter->m59_defence = rollStatGrowth(
        pLevelUpCharacter->m59_defence, pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[1][hi],
        maxLevel, level);
    pLevelUpCharacter->m5E_hitPercentage = rollStatGrowth(
        pLevelUpCharacter->m5E_hitPercentage,
        pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[2][hi], maxLevel, level);
    pLevelUpCharacter->m5F_evadePercentage = rollStatGrowth(
        pLevelUpCharacter->m5F_evadePercentage,
        pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[3][hi], maxLevel, level);
}

void levelUpCharacterEtherStats(void) {
    u8 hi;
    u8 maxLevel;
    u8 level;

    hi = 0;
    maxLevel = 100;
    if (pLevelUpCharacter->m63_Level2 >= 100) {
        hi = 1;
        maxLevel = 200;
    }
    level = pLevelUpCharacter->m63_Level2;
    pLevelUpCharacter->m52_MaxMP = rollMaxMpGrowth(pLevelUpCharacter->m52_MaxMP, pLevelUpCharacter->m63_Level2);
    pLevelUpCharacter->m5B_ether = rollStatGrowth(
        pLevelUpCharacter->m5B_ether, pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[4][hi],
        maxLevel, level);
    pLevelUpCharacter->m5C_etherDefence = rollStatGrowth(
        pLevelUpCharacter->m5C_etherDefence,
        pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBC_statGoals[5][hi], maxLevel, level);
}

u8 rollStatGrowth(u8 stat, u8 goal, u8 maxLevel, u8 level) {
    s16 chance;
    s32 up;

    chance = (s16)(R3000_DIV((goal - stat) * 100, maxLevel - level) - 50) + rand() % 100;
    up = chance > 49;
    stat = stat + up;
    if (stat > 200) {
        stat = 200;
    }
    return stat;
}

u16 rollMaxHpGrowth(u16 hp, u8 level) {
    s16 gain;
    s16 newHp;
    s32 maxLevel;

    if (level < 100) {
        maxLevel = 99;
        gain = rand() % 100 *
                   (pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mB8_maxHpGoal99 - (level - maxLevel) - hp) /
                   ((maxLevel + 1 - level) * 100) +
               2;
        if (gain < 0) {
            newHp = hp;
        } else {
            newHp = gain + hp;
        }
    } else {
        maxLevel = 200;
        gain = rand() % 100 *
                   ((pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mBA_maxHpGoal200 - hp) /
                    ((maxLevel + 1 - level) * 100)) *
                   2 +
               2;
        if (gain < 0) {
            newHp = hp;
        } else {
            newHp = gain + hp;
        }
    }
    if (newHp >= 1000) {
        newHp = 999;
    }
    return newHp;
}

u8 rollMaxMpGrowth(u8 stat, u8 level) {
    u8 goal;
    u8 maxLevel;
    s16 chance;
    s32 up;

    if (level < 100) {
        goal = pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mC8_maxMpGoal99;
        maxLevel = 99;
    } else {
        maxLevel = 200;
        goal = pLevelUpData->m0_chars[pLevelUpCharacter->m56_battleCommandLoadout].mC9_maxMpGoal200;
    }
    chance = (s16)(R3000_DIV((goal - stat) * 100, maxLevel - level) - 50) + rand() % 100;
    up = chance > 49;
    stat = stat + up;
    if (stat >= 100) {
        stat = 99;
    }
    return stat;
}

void learnSkillsAfterBattle(void) {
    u8 i;
    u8 id;

    for (i = 0; i < 3; i++) {
        pLevelUpCharacter = &pResultBattleEntities->m0_entities[i].m0_base;
        if (!(pLevelUpCharacter->m7C_statusFlags[0] & 0x8000)) {
            switch (pLevelUpCharacter->m56_battleCommandLoadout) {
            case 7:
            case 8:
                break;
            default:
                id = learnDeathblow(pLevelUpCharacter->m56_battleCommandLoadout);
                if (id != 0) {
                    pResultBattleEntities->m101C_newDeathblow[i] = id;
                }
                break;
            }
            switch (pLevelUpCharacter->m56_battleCommandLoadout) {
            case 10:
                break;
            default:
                id = learnChiAbility(pLevelUpCharacter->m56_battleCommandLoadout);
                if (id != 0) {
                    pResultBattleEntities->m101F_newAbility[i] = id;
                }
                break;
            }
            switch (pLevelUpCharacter->m56_battleCommandLoadout) {
            case 7:
            case 8:
            case 10:
                break;
            default:
                unlockGearAttacksFromDeathblows(pLevelUpCharacter->m56_battleCommandLoadout);
                break;
            }
            switch (pLevelUpCharacter->m56_battleCommandLoadout) {
            case 8:
            case 9:
            case 10:
                break;
            default:
                unlockGearChiAbilities(pLevelUpCharacter->m56_battleCommandLoadout);
                break;
            }
            if (pLevelUpCharacter->m56_battleCommandLoadout == 8) {
                unlockMariaGearAttacks();
            }
            if (pLevelUpCharacter->m56_battleCommandLoadout == 7) {
                updateChuChuGearStats();
            }
        }
    }
}

u8 learnDeathblow(u8 c) {
    u8 n;
    u8 found;
    u8 i;
    u8 j;
    s32 bit;

    n = 7;
    if (gameState.m22B6_gameFlags & 0x4000) {
        n = 13;
    }
    found = 0xFF;
    for (i = 0; i < n; i++) {
        bit = 0x8000;
        if (!(gameState.m16C0_abilities[c].m0_unlockedAbilitiesBitField[0] & (bit >> i))) {
            if (pLevelUpData->m0_chars[c].m100_deathblowLevels[i] > gameState.m26C_party[c].m62_Level) {
                break;
            }
            for (j = 0; j < 7; j++) {
                if (pLevelUpData->m0_chars[c].m0_deathblowUses[i][j] > pLevelUpCharacter->m90_deathblowProgress[j]) {
                    break;
                }
            }
            if (j == 7) {
                found = i;
                break;
            }
        }
    }
    if (found == 0xFF) {
        return 0;
    }
    gameState.m16C0_abilities[c].m0_unlockedAbilitiesBitField[0] |= 0x8000 >> found;
    return found;
}

u8 learnChiAbility(u8 c) {
    u32 flags;
    u32 bit;
    u8 i;

    bit = 0x8000;

    for (i = 0; i < 12; i++, bit >>= 1) {
        if (pLevelUpData->m0_chars[c].mF0_learnLevels[i] == 0xFF) {
            return 0;
        }
        if (gameState.m26C_party[c].m62_Level >= pLevelUpData->m0_chars[c].mF0_learnLevels[i]) {
            flags = pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[1];
            if (!(bit & flags)) {
                pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[1] = bit | flags;
                return i + 1;
            }
        }
    }
    return 0;
}

void unlockGearAttacksFromDeathblows(u8 c) {
    u8 i;
    u8 id;

    for (i = 0; i < 9; i++) {
        id = pLevelUpData->m0_chars[c].mD0_gearAttackRequirements[i];
        if (id == 0xFF) {
            return;
        }
        if (pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[0] & (0x8000 >> (id - 1))) {
            pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[2] |= 0x8000 >> (i + 3);
        }
    }
}

void unlockMariaGearAttacks(void) {
    u8 i;
    u8 level;
    u32 flags;

    for (i = 0; i < 9; i++) {
        level = pLevelUpData->m0_chars[8].mD0_gearAttackRequirements[i];
        if (level == 0xFF) {
            return;
        }
        if (gameState.m26C_party[8].m62_Level >= level) {
            flags = pResultGameState->m16C0_abilities[8].m0_unlockedAbilitiesBitField[2];
            if (!(flags & (0x1000 >> i))) {
                pResultGameState->m16C0_abilities[8].m0_unlockedAbilitiesBitField[2] = (0x1000 >> i) | flags;
            }
        }
    }
}

void unlockGearChiAbilities(u8 c) {
    u8 i;
    u8 id;

    for (i = 0; i < 13; i++) {
        id = pLevelUpData->m0_chars[c].mE0_gearChiRequirements[i];
        if (id == 0) {
            return;
        }
        if (pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[1] & (0x8000 >> (id - 1))) {
            pResultGameState->m16C0_abilities[c].m0_unlockedAbilitiesBitField[3] |= 0x8000 >> i;
        }
    }
}

void updateChuChuGearStats(void) {
    GameStateGear* gear = &pResultGameState->m978_gears[7];

    gear->m64_maxHp = pLevelUpCharacter->m4E_MaxHP * 200;
    gear->m3C_engineOutput = pLevelUpCharacter->m58_attack / 5 + 1;
    gear->m70_defence = pLevelUpCharacter->m4E_MaxHP * 10;
    gear->m72_etherDefence = pLevelUpCharacter->m4E_MaxHP * 10;
}

void updateEnergyPerTurn(void) {
    u8 i;
    GameStateCharacter* ch;
    GameStateSize20* s;

    for (i = 0; i < 11; i++) {
        ch = &pResultGameState->m26C_party[i];
        s = &pResultGameState->m16C0_abilities[i];
        if (s->m17_energyPerTurn == 7) {
            continue;
        }
        switch (s->m17_energyPerTurn) {
        case 3:
            if (ch->m62_Level >= pLevelUpData->m0_chars[i].mCC_energyLevels[0]) {
                s->m17_energyPerTurn = 4;
            }
            break;
        case 4:
            if (ch->m62_Level >= pLevelUpData->m0_chars[i].mCC_energyLevels[1]) {
                s->m17_energyPerTurn = 5;
            }
            break;
        case 5:
            if (ch->m62_Level >= pLevelUpData->m0_chars[i].mCC_energyLevels[2]) {
                s->m17_energyPerTurn = 6;
            }
            break;
        case 6:
            if (ch->m62_Level >= 50 && (gameState.m22B6_gameFlags & 0x4000)) {
                s->m17_energyPerTurn = 7;
            }
            break;
        }
    }
}

void unlockGearAttacksByLevel(void) {
    u8 i;
    u8 c;

    for (i = 0; i < 3; i++) {
        c = battleEntities.m0_entities[i].m0_base.m56_battleCommandLoadout;
        if (gameState.m26C_party[c].m62_Level >= 50) {
            gameState.m16C0_abilities[c].m0_unlockedAbilitiesBitField[2] |= 8;
        }
        if (gameState.m26C_party[c].m62_Level >= 60) {
            gameState.m16C0_abilities[c].m0_unlockedAbilitiesBitField[2] |= 4;
        }
        if (gameState.m26C_party[c].m62_Level >= 70) {
            gameState.m16C0_abilities[c].m0_unlockedAbilitiesBitField[2] |= 2;
        }
    }
}

void rollItemDrops(void) {
    u8 i;
    u8 thief;
    sBattleEntity* e;
    s32 bit;

    thief = 0;
    for (i = 0; i < 3; i++) {
        pLevelUpCharacter = &pResultBattleEntities->m0_entities[i].m0_base;
        if (pLevelUpCharacter->m32_accessoryFlags & 0x800) {
            thief = 1;
        }
    }
    bit = 1;
    for (i = 0; i < 8; i++) {
        pResultBattleEntities->m1014_itemDropIds[i] = 0;
        if (pResultBattleEntities->m5FB4_defeatedEnemies & (bit << i)) {
            e = &pResultBattleEntities->m0_entities[i + 3];
            pLevelUpCharacter = &e->m0_base;
            if (rand() % 100 < e->m150_dropChance1 || thief == 1) {
                pResultBattleEntities->m100C_itemDropCategories[i] = e->m154_dropCategory1;
                pResultBattleEntities->m1014_itemDropIds[i] = e->m152_dropItem1;
            } else if (rand() % 100 < e->m151_dropChance2) {
                pResultBattleEntities->m100C_itemDropCategories[i] = e->m155_dropCategory2;
                pResultBattleEntities->m1014_itemDropIds[i] = e->m153_dropItem2;
            }
        }
    }
}
