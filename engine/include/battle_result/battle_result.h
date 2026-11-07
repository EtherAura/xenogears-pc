#ifndef OVL_10_04_OVL_10_04_H
#define OVL_10_04_OVL_10_04_H

/*
 * Battle result overlay (disc 10/04, loaded at 0x801DE000 by battle; Noah:
 * "Battle_result", battleResultDraw calls 801DE048/801DE1C4/801DE408).
 * End-of-battle screens: experience, gold, items, level-up stat bars and
 * learned deathblows. Types are offset-tagged; unknown ranges are padding.
 */

#include "common.h"
#include "psyq.h"
#include "battle/battle.h"

/* Shared result screen (stat bars etc.), *battleResultScreen. Offsets are US: JP's
 * m140 is 20 entries shorter, so every later field is 0x640 lower in JP. */
typedef struct {
    /* 0x0000 */ POLY_FT4 m0_name[4][2];
#if VERSION_JP
    /* 0x0140 */ POLY_FT4 m140_levelUpLabels[47][2];
#else
    /* 0x0140 */ POLY_FT4 m140_levelUpLabels[67][2];
#endif
    /* 0x1630 */ POLY_FT4 m1630_maxHp[3][2];
    /* 0x1720 */ POLY_FT4 m1720_maxMp[2][2];
    /* 0x17C0 */ POLY_FT4 m17C0_maxHpGain[4][2];
    /* 0x1900 */ POLY_FT4 m1900_maxMpGain[3][2];
    /* 0x19F0 */ POLY_FT4 m19F0_statNames[7][3][2];
    /* 0x2080 */ POLY_FT4 m2080_statValues[7][4][2];
    /* 0x2940 */ POLY_G4 m2940_statBars[7][2];
    /* 0x2B38 */ POLY_G4 m2B38_statBarGains[7][2];
    /* 0x2D30 */ POLY_FT4 m2D30_rewardLabels[7][2];
    /* 0x2F60 */ POLY_FT4 m2F60_goldWon[6][2];
    /* 0x3140 */ POLY_FT4 m3140_totalGold[9][2];
    /* 0x3410 */ POLY_FT4 m3410_rewardTitles[2][2];
    /* 0x34B0 */ POLY_FT4 m34B0_learnedText[2][2];
    /* 0x3550 */ POLY_FT4 m3550_itemNames[8][2];
    /* 0x37D0 */ POLY_FT4 m37D0_itemCounts[8][2];
    /* 0x3A50 */ sBattleResultPolyCount m3A50_nameCount;  /* m0 */
    /* 0x3A52 */ sBattleResultPolyCount m3A52_levelUpLabelsCount;  /* m140 */
    /* 0x3A54 */ sBattleResultPolyCount m3A54_maxHpCount;  /* m1630 */
    /* 0x3A56 */ sBattleResultPolyCount m3A56_maxHpGainCount;  /* m17C0 */
    /* 0x3A58 */ sBattleResultPolyCount m3A58_maxMpCount;  /* m1720 */
    /* 0x3A5A */ sBattleResultPolyCount m3A5A_maxMpGainCount;  /* m1900 */
    /* 0x3A5C */ u8 m3A5C_statNameCount[7];
    /* 0x3A63 */ u8 m3A63_statNameOddOrEven[7];
    /* 0x3A6A */ u8 m3A6A_statValueCount[7];
    /* 0x3A71 */ u8 m3A71_statValueOddOrEven[7];
    /* 0x3A78 */ u8 m3A78_statBarOddOrEven[7];
    /* 0x3A7F */ u8 m3A7F_rewardLabelsCount;                      /* m2D30 count */
    /* 0x3A80 */ u8 m3A80_rewardLabelsOddOrEven;                      /* m2D30 oddOrEven */
    /* 0x3A81 */ sBattleResultPolyCount m3A81_goldWonCount;  /* m2F60 */
    /* 0x3A83 */ sBattleResultPolyCount m3A83_totalGoldCount;  /* m3140 */
    /* 0x3A85 */ u8 m3A85_numItems;                      /* m3550/m37D0 count */
    /* 0x3A86 */ u8 m3A86_itemsOddOrEven;                      /* m3550/m37D0 oddOrEven */
    /* 0x3A87 */ u8 m3A87_rewardTitlesOddOrEven;                      /* m3410 oddOrEven */
    /* 0x3A88 */ u8 m3A88_learnedTextOddOrEven[2];                   /* m34B0[i] oddOrEven */
} sBattleResultScreen;

/* Per-character level-up / learning data (0x110 bytes), in the file that
 * pLevelUpData points to. */
typedef struct {
    /* 0x000 */ u16 m0_deathblowUses[13][7]; /* uses of each attack needed per deathblow */
    /* 0x0B6 */ u8 mB6[2];
    /* 0x0B8 */ u16 mB8_maxHpGoal99;                  /* max HP goal (levels < 100) */
    /* 0x0BA */ u16 mBA_maxHpGoal200;                  /* max HP goal (levels 100+) */
    /* 0x0BC */ u8 mBC_statGoals[6][2];   /* [stat][level >= 100]: attack, defence, hit %,
                                           * evade %, ether, ether defence */
    /* 0x0C8 */ u8 mC8_maxMpGoal99;                   /* max MP goal (levels < 100) */
    /* 0x0C9 */ u8 mC9_maxMpGoal200;                   /* max MP goal (levels 100+) */
    /* 0x0CA */ u8 mCA[2];
    /* 0x0CC */ u8 mCC_energyLevels[3];   /* levels at which m16C0[c].m17_energyPerTurn goes 3->4->5->6 */
    /* 0x0CF */ u8 mCF;
    /* 0x0D0 */ u8 mD0_gearAttackRequirements[16];               /* 1-based bits of unlocked[0] that unlock unlocked[2] bit
                                           * i + 3 (0xFF-terminated); character 8: levels for bits 12 - i */
    /* 0x0E0 */ u8 mE0_gearChiRequirements[16];               /* 1-based bits of unlocked[1] that unlock unlocked[3] bit i
                                           * (0-terminated) */
    /* 0x0F0 */ u8 mF0_learnLevels[16];   /* level for unlocked[1] bit i (0xFF-terminated) */
    /* 0x100 */ u8 m100_deathblowLevels[16]; /* level for unlocked[0] bit i (deathblow i) */
} sLevelUpCharData; /* 0x110 */

typedef struct {
    /* 0x000 */ sLevelUpCharData m0_chars[11];
    /* 0xBB0 */ s32 mBB0_expTable[99];     /* [level - 1] */
} sLevelUpData;

#endif /* OVL_10_04_OVL_10_04_H */
