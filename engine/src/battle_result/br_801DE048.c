/* Battle result overlay (Noah Battle_result), loaded by battle at 0x801DE000:
 * the three result-screen draw entry points. gcc-2.6.3-psx, ASPSX 2.34. */
#include "common.h"
#include "battle_result/battle_result.h"
#include "pc_wide.h"
/* Stat bars use the HUD choice; learned/reward text uses the text choice.
 * Font arrays already get their text class in battleRenderPolyArray. */
#define AddPrim pcTextureHudPrim
#define RESULT_TEXT_PRIM pcTextureTextPrim

extern sBattleResultScreen* battleResultScreen;

void battleRenderPolyArray(POLY_FT4* p, s32 count, s32 oddOrEven);

/* Name records mix a face and letter quads in each of the three windows. */
static void tagResultName(POLY_FT4 (*polys)[2], s32 count, s32 odd) {
    s32 j;
    for (j = 0; j < count; j++) {
        POLY_FT4* p = &polys[j][odd];
        if ((p->tpage & 0x180) == 0x80 && p->u1-p->u0 >= 23 && p->u1-p->u0 <= 24 &&
            p->v2-p->v0 >= 23 && p->v2-p->v0 <= 24)
            pcTextureClass(p, PC_TEX_BATTLE_PORTRAIT);
    }
}

void drawBattleResultCharacterWindows(void) {
    s32 i;

    if (battleVar1->mA0_drawResultCharacterWindows != 0) {
        for (i = 0; i < 3; i++) {
            /* The result font's FC..FE records (one per party slot,
             * setupResultCharacterNames) include a 24x24 face next to the
             * name: an 8-bit TIM (0x460 bytes) that loadBattleCharacterPortraits
             * uploads at the texture place computeMenuBorder reads from the
             * record's first piece. Tag the face apart from the text pieces:
             * a piece on an 8-bit texture page whose UV extent is 23..24
             * texels each way. A heuristic, not the record's own word: a text
             * piece of that size and depth would be classed a portrait too.
             * Tagging m0_name[0] by its place would be exact if the face is
             * always the record's first piece (not measured). */
            tagResultName(battleResultCharWindows[i]->m0_name, battleResultCharWindows[i]->m15E0_nameCount.m0_count,
                          battleResultCharWindows[i]->m15E0_nameCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m0_name[0], battleResultCharWindows[i]->m15E0_nameCount.m0_count, battleResultCharWindows[i]->m15E0_nameCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m140_labels[0], battleResultCharWindows[i]->m15E2_labelsCount.m0_count, battleResultCharWindows[i]->m15E2_labelsCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m960_hp[0], battleResultCharWindows[i]->m15E8_hpCount.m0_count, battleResultCharWindows[i]->m15E8_hpCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->mB40_mp[0], battleResultCharWindows[i]->m15EC_mpCount.m0_count, battleResultCharWindows[i]->m15EC_mpCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->mA50_maxHp[0], battleResultCharWindows[i]->m15EA_maxHpCount.m0_count, battleResultCharWindows[i]->m15EA_maxHpCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->mBE0_maxMp[0], battleResultCharWindows[i]->m15EE_maxMpCount.m0_count, battleResultCharWindows[i]->m15EE_maxMpCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m780_level[0], battleResultCharWindows[i]->m15E4_levelCount.m0_count, battleResultCharWindows[i]->m15E4_levelCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m870_etherLevel[0], battleResultCharWindows[i]->m15E6_etherLevelCount.m0_count, battleResultCharWindows[i]->m15E6_etherLevelCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->mC80_exp[0], battleResultCharWindows[i]->m15F0_expCount.m0_count, battleResultCharWindows[i]->m15F0_expCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->mF00_etherExp[0], battleResultCharWindows[i]->m15F2_etherExpCount.m0_count, battleResultCharWindows[i]->m15F2_etherExpCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m1180_expGained[0], battleResultCharWindows[i]->m15F4_expGainedCount.m0_count, battleResultCharWindows[i]->m15F4_expGainedCount.m1_oddOrEven);
            battleRenderPolyArray(battleResultCharWindows[i]->m13B0_etherExpGained[0], battleResultCharWindows[i]->m15F6_etherExpGainedCount.m0_count, battleResultCharWindows[i]->m15F6_etherExpGainedCount.m1_oddOrEven);
        }
    }
}

void drawBattleResultLevelUpWindow(void) {
    s32 i;

    if (battleVar1->mA1_drawLevelUpWindow != 0) {
        tagResultName(battleResultScreen->m0_name, battleResultScreen->m3A50_nameCount.m0_count,
                      battleResultScreen->m3A50_nameCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m0_name[0], battleResultScreen->m3A50_nameCount.m0_count, battleResultScreen->m3A50_nameCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m140_levelUpLabels[0], battleResultScreen->m3A52_levelUpLabelsCount.m0_count, battleResultScreen->m3A52_levelUpLabelsCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m1630_maxHp[0], battleResultScreen->m3A54_maxHpCount.m0_count, battleResultScreen->m3A54_maxHpCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m1720_maxMp[0], battleResultScreen->m3A58_maxMpCount.m0_count, battleResultScreen->m3A58_maxMpCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m17C0_maxHpGain[0], battleResultScreen->m3A56_maxHpGainCount.m0_count, battleResultScreen->m3A56_maxHpGainCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m1900_maxMpGain[0], battleResultScreen->m3A5A_maxMpGainCount.m0_count, battleResultScreen->m3A5A_maxMpGainCount.m1_oddOrEven);
        for (i = 0; i < 7; i++) {
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1,
                    &battleResultScreen->m2B38_statBarGains[i][battleResultScreen->m3A78_statBarOddOrEven[i]]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1,
                    &battleResultScreen->m2940_statBars[i][battleResultScreen->m3A78_statBarOddOrEven[i]]);
            battleRenderPolyArray(battleResultScreen->m19F0_statNames[i][0], battleResultScreen->m3A5C_statNameCount[i],
                          battleResultScreen->m3A63_statNameOddOrEven[i]);
            battleRenderPolyArray(battleResultScreen->m2080_statValues[i][0], battleResultScreen->m3A6A_statValueCount[i],
                          battleResultScreen->m3A71_statValueOddOrEven[i]);
        }
    }
    if (battleVar1->m8F_drawLearnedMessage != 0) {
        tagResultName(battleResultScreen->m0_name, battleResultScreen->m3A50_nameCount.m0_count,
                      battleResultScreen->m3A50_nameCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m0_name[0], battleResultScreen->m3A50_nameCount.m0_count, battleResultScreen->m3A50_nameCount.m1_oddOrEven);
        for (i = 0; i < 2; i++) {
            RESULT_TEXT_PRIM(D_800CCB04.m0_pCurrentBattleOT + 1, &battleResultScreen->m34B0_learnedText[i][battleResultScreen->m3A88_learnedTextOddOrEven[i]]);
        }
    }
}

void drawBattleResultRewardsWindow(void) {
    s32 i;

    if (battleVar1->mAC_drawRewardsWindow != 0) {
        battleRenderPolyArray(battleResultScreen->m2D30_rewardLabels[0], 7, battleResultScreen->m3A80_rewardLabelsOddOrEven);
        battleRenderPolyArray(battleResultScreen->m2F60_goldWon[0], battleResultScreen->m3A81_goldWonCount.m0_count, battleResultScreen->m3A81_goldWonCount.m1_oddOrEven);
        battleRenderPolyArray(battleResultScreen->m3140_totalGold[0], battleResultScreen->m3A83_totalGoldCount.m0_count, battleResultScreen->m3A83_totalGoldCount.m1_oddOrEven);
        for (i = 0; i < battleResultScreen->m3A85_numItems; i++) {
            RESULT_TEXT_PRIM(D_800CCB04.m0_pCurrentBattleOT + 1, &battleResultScreen->m3550_itemNames[i][battleResultScreen->m3A86_itemsOddOrEven]);
            RESULT_TEXT_PRIM(D_800CCB04.m0_pCurrentBattleOT + 1, &battleResultScreen->m37D0_itemCounts[i][battleResultScreen->m3A86_itemsOddOrEven]);
        }
        for (i = 0; i < 2; i++) {
            RESULT_TEXT_PRIM(D_800CCB04.m0_pCurrentBattleOT + 1, &battleResultScreen->m3410_rewardTitles[i][battleResultScreen->m3A87_rewardTitlesOddOrEven]);
        }
    }
}

/* draws the three result windows; called by battle drawBattleMode0 (Noah battle.cpp) */
void battleResultDraw(void) {
    drawBattleResultCharacterWindows();
    drawBattleResultLevelUpWindow();
    drawBattleResultRewardsWindow();
}
