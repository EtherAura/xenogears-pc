#ifndef BATTLE_BATTLE_H
#define BATTLE_BATTLE_H

/*
 * Battle overlay (disc 01/10) types. Names and layouts follow Noah
 * (work/ref/Noah/NoahLib/battle/: battle.h, battleConfig.h,
 * battleRenderContext.h, enemyScript.h, damageDisplay.h, ...) where it has
 * them, otherwise the most descriptive name the matching agents gave; fields
 * are offset-tagged and unknown ranges are padding.
 *
 * Several .bss ranges are reached by retail from one base (GCC only does that
 * within one symbol), so they are described here as aggregates
 * (BattleBss_XXXXXXXX). A file uses either the aggregate or the plain D_
 * symbols of its members, whichever reproduces its code.
 */

#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/battle.h"
#include "main/sprite.h"
#include "main/game_state.h"
#include "main/sound.h"
#include "field_mecha/mecha.h"

/* ---- libgpu functions and macros missing from psyq.h */
void SetPolyG3(void* p);
void SetLineF2(void* p);
/* Not in psyq.h: main's libgpu sys.c defines it with a DR_PAIR view. */
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);

/* ---- main functions without a declaration in include/main yet */
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void setupOverrideTPage(s16 x, s16 y);
void setupOverrideClut(s16 x, s16 y);

#define setXY4(p, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3)                      \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1),       \
     (p)->x2 = (_x2), (p)->y2 = (_y2), (p)->x3 = (_x3), (p)->y3 = (_y3))

#define setUV4(p, _u0, _v0, _u1, _v1, _u2, _v2, _u3, _v3)                      \
    ((p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u1), (p)->v1 = (_v1),       \
     (p)->u2 = (_u2), (p)->v2 = (_v2), (p)->u3 = (_u3), (p)->v3 = (_v3))

typedef POLY_FT4 POLY_FT4x2[2];

/* Noah: sTaskHeaderPair (kernel/taskLists.h): a task plus its draw task. */
typedef struct TaskHeaderPair {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
} TaskHeaderPair;

/* ---- battle UI state (Noah: battle.h) ---- */

/* Noah: sBattleVar0Sub (size 0x1E4) */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_apLabel[2][2];
    /* 0x0A0 */ POLY_FT4 mA0_fuelLabel[2][2];
    /* 0x140 */ POLY_FT4 m140[2][2];
    /* 0x1E0 */ u8 m1E0_oddOrEven;
    /* 0x1E1 */ u8 m1E1_barMode;
    /* 0x1E2 */ u8 m1E2_apLabelLength;
    /* 0x1E3 */ u8 m1E3_fuelLabelLength;
} sBattleVar0Sub;

/* Noah: sBattleVar0Sub_tpage (size 0x18) */
typedef struct {
    /* 0x00 */ s32 m0_polyCount;
    /* 0x04 */ s32 m4_tpage_tp;
    /* 0x08 */ s32 m8_clutX;
    /* 0x0C */ s32 mC_clutY;
    /* 0x10 */ s32 m10_tpage_X;
    /* 0x14 */ s32 m14_tpage_Y;
} sBattleVar0Sub_tpage;

/* Noah: sBattleVar0_a230 (size 0x670; battleVar0->mA230) */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_polys[0x12][2];
    /* 0x5A0 */ POLY_FT4 m5A0_mpSlash[2];
    /* 0x5F0 */ POLY_G4 m5F0_apBarFill[2];
    /* 0x638 */ POLY_F4 m638_apBarEmpty[2];
    /* 0x668 */ u8 m668_oddOrEven;
    /* 0x669 */ u8 m669_drawEPCost;
    /* 0x66A */ u8 m66A_oddOrEven;
    /* 0x66B */ u8 m66B_drawSelection;
    /* 0x66C */ u8 m66C_epCostOddOrEven;
    /* 0x66D */ u8 m66D_drawDescription;
    /* 0x66E */ u8 m66E_costDigitCount;
    /* 0x66F */ u8 m66F_drawApBar;
} sBattleVar0_a230;

/* Noah: sBattleVar0 (size 0xA2B4; JP 0xA5D4); battleVar0 = battleVar0.
   Offsets are US; JP: m27C8..m6417 -0x320, m835C.. +0x320. */
typedef struct {
    /* 0x0000 */ POLY_FT4 m0_apBarFrame[3][6][2];
    /* 0x05A0 */ POLY_GT4 m5A0_timeBarFills[4][2];
    /* 0x0740 */ POLY_G4 m740_APOrFuelPoly[3][2];
    /* 0x0818 */ POLY_FT4 m818_portraits[3][2];
    /* 0x0908 */ LINE_F2 m908_apMarks[12];      /* Noah: [6][2]; retail indexes [i * 2 + oddOrEven] */
    /* 0x09C8 */ POLY_FT4 m9C8_APCounterDisplayPolys[6][2];
    /* 0x0BA8 */ POLY_FT4 mBA8_menuGlyphs[60][2];
#if VERSION_JP
    /* 0x1E68 */ POLY_FT4 m1E68_menuGlyphs2[20][2];  /* JP: 20 (US 30); later offsets -0x320 up to m641C */
#else
    /* 0x1E68 */ POLY_FT4 m1E68_menuGlyphs2[30][2];
#endif
    /* 0x27C8 */ POLY_FT4 m27C8_selectionCursor[20][2];
    /* 0x2E08 */ POLY_FT4 m2E08_timeBars[4][10][2];
    /* 0x3A88 */ POLY_FT4 m3A88_hpDigits[3][40][2];
    /* 0x6008 */ POLY_FT4 m6008_apFuelDigits[3][4][2];
    /* 0x63C8 */ POLY_F4 m63C8_portraitHighlight[2];
    /* 0x63F8 */ DR_MODE m63F8_portraitHighlightDrMode[2];
    /* 0x6410 */ s32 m6410_highlightLevel;             /* chi-menu fade level (0..0x80) */
    /* 0x6414 */ u8 m6414_highlightOddOrEven;
    /* 0x6415 */ u8 m6415_drawHighlight;
    /* 0x6416 */ u8 m6416_highlightFadingOut;
    /* 0x6417 */ u8 m6417[5];
#if VERSION_JP
    /* 0x641C */ POLY_FT4 m641C_commandGlyphs[2][120]; /* JP 0x60FC, [2][120]: later offsets +0x320 (size 0xA5D4) */
#else
    /* 0x641C */ POLY_FT4 m641C_commandGlyphs[2][100]; /* Noah: [2][50][2]; retail indexes [i][j * 2 + oddOrEven] */
#endif
    /* 0x835C */ sBattleVar0Sub m835C_apFuelLabels[3];
    /* 0x8908 */ DR_MODE m8908_drMode[2];
    /* 0x8920 */ DR_MODE m8920_fullWindowDrMode[2];
    /* 0x8938 */ u8 m8938[0x18];
    /* 0x8950 */ RECT m8950_clutRects[4];
    /* 0x8970 */ u16 m8970_cluts[4][4][0xC6];
    /* 0xA230 */ sBattleVar0_a230* mA230_menuPanel;
    /* 0xA234 */ sBattleVar0Sub_tpage mA234_glyphTextures[5];
    /* 0xA2AC */ u16 mA2AC_timeBarClut;
    /* 0xA2AE */ u16 mA2AE_fuelBarClut;
    /* 0xA2B0 */ u16 mA2B0_slowTimeBarClut;
    /* 0xA2B2 */ u16 mA2B2_hasteTimeBarClut;
} sBattleVar0;

/* Noah: sBattleVar1 (size 0x10C); battleVar1 = battleVar1 */
typedef struct {
    /* 0x00 */ RECT m0_textureWindow;
    /* 0x08 */ RECT m8_glyphTexture0Window;
    /* 0x10 */ RECT m10_glyphTexture1Window;
    /* 0x18 */ RECT m18_glyphTexture2Window;
    /* 0x20 */ RECT m20_glyphTexture3Window;
    /* 0x28 */ RECT m28_glyphTexture4Window;
    /* 0x30 */ u32 m30_clutScroll;                 /* chi-menu CLUT scroll */
    /* 0x34 */ s32 m34_timeBarScreenPositionX;
    /* 0x38 */ u8 m38[4];
    /* 0x3C */ u32 m3C_timeBarAnimationTargetPositionX;
    /* 0x40 */ u8 m40[4];
    /* 0x44 */ s32 m44_timeBarScreenPositionY;
    /* 0x48 */ u8 m48[4];
    /* 0x4C */ u32 m4C_timeBarAnimationTargetPositionY;
    /* 0x50 */ u8 m50[4];
    /* 0x54 */ u32 m54_timeBarAnimationXStep;
    /* 0x58 */ u8 m58[4];
    /* 0x5C */ u32 m5C_timeBarAnimationYStep;
    /* 0x60 */ u8 m60[4];
    /* 0x64 */ u32 m64_timeBarAnimationX;
    /* 0x68 */ u8 m68[4];
    /* 0x6C */ u32 m6C_timeBarAnimationY;
    /* 0x70 */ u8 m70[4];
    /* 0x74 */ u8 m74_timeBarsLengths[4];
    /* 0x78 */ u8 m78_apBarFrameLengths[3];
    /* 0x7B */ u8 m7B_apCounterLength;
    /* 0x7C */ u8 m7C_hpDisplayDirty[3];
    /* 0x7F */ u8 m7F_drawTimeBarFill[4];               /* [3] (0x82): the gear/fuel bar */
    /* 0x83 */ u8 m83_portraitsOddOrEven;
    /* 0x84 */ u8 m84_timeBarsOddOrEven[4];
    /* 0x88 */ u8 m88[6];
    /* 0x8E */ u8 m8E_isBillyWindowOpen;
    /* 0x8F */ u8 m8F_drawLearnedMessage;
    /* 0x90 */ u8 m90_perPCTimeBarStatus[3];
    /* 0x93 */ u8 m93_oddOrEven[3];
    /* 0x96 */ u8 m96_isGearCommandTextLoaded;
    /* 0x97 */ u8 m97_apMarkCount;
    /* 0x98 */ u8 m98_apMarksOddOrEven;
    /* 0x99 */ u8 m99_oddOrEven[3];
    /* 0x9C */ u8 m9C_renderBA8;
    /* 0x9D */ u8 m9D_render1E68;
    /* 0x9E */ u8 m9E_render27C8;
    /* 0x9F */ u8 m9F;
    /* 0xA0 */ u8 mA0_drawResultCharacterWindows;
    /* 0xA1 */ u8 mA1_drawLevelUpWindow;
    /* 0xA2 */ u8 mA2_apBarFrameOddOrEven;
    /* 0xA3 */ u8 mA3_641COddOrEven;
    /* 0xA4 */ u8 mA4_oddOrEven7B;
    /* 0xA5 */ u8 mA5_ba8OddOrEven;
    /* 0xA6 */ u8 mA6_1E68OddOrEven;
    /* 0xA7 */ u8 mA7_27C8OddOrEven;
    /* 0xA8 */ u8 mA8_drawComboInput;
    /* 0xA9 */ u8 mA9_timeBarNumMoveSteps;
    /* 0xAA */ u8 mAA_clutScrollTimer;
    /* 0xAB */ u8 mAB_timeBarNumAnimationSteps;
    /* 0xAC */ u8 mAC_drawRewardsWindow;
    /* 0xAD */ u8 mAD_drawGearHud;
    /* 0xAE */ u8 mAE_isChiMenuDataLoaded;
    /* 0xAF */ u8 mAF_drawApCounter;
    /* 0xB0 */ u8 mB0_isDialogWindowInitialized[7];
    /* 0xB7 */ u8 mB7_openMenuType;
    /* 0xB8 */ u8 mB8_isDialogWindowInitialized2[7];
    /* 0xBF */ u8 mBF_isDialogWindowInitialized3[7];
    /* 0xC6 */ u8 mC6_isTargetSelectionCursorVisible;
    /* 0xC7 */ u8 mC7_drawGearFuel;
    /* 0xC8 */ u8 mC8_dialogPortrait;
    /* 0xC9 */ u8 mC9_dialog;
    /* 0xCA */ u8 mCA_battleEventPoly;
    /* 0xCB */ u8 mCB_drawCommandRing;
    /* 0xCC */ u8 mCC_drawApFuel[3];
    /* 0xCF */ u8 mCF_isWaitingForInput;
    /* 0xD0 */ s32 mD0_battleCommandLength[2]; /* Noah: u8[2]; retail reads words */
    /* 0xD8 */ u8 mD8[8];
    /* 0xE0 */ s32 mE0_hpDigitCounts[3];
    /* 0xEC */ s32 mEC_apFuelDigitCounts[3];
    /* 0xF8 */ s32 mF8_countOfBA8;
    /* 0xFC */ s32 mFC_countOF1E68;
    /* 0x100 */ s32 m100_countOf27C8;
    /* 0x104 */ s16 m104_timeBarAnimationScale;
    /* 0x106 */ s16 m106_timeBarAnimationRotation;
    /* 0x108 */ u8 m108[4];
} sBattleVar1;

/* Noah: sBattleVar2Sub (size 0x40) */
typedef struct {
    /* 0x00 */ u8 m0_circleMenuBattleCommandsMapping[8];
    /* 0x08 */ u8 m8[4];
    /* 0x0C */ u8 mC[4];
    /* 0x10 */ u8 m10[0xC];
    /* 0x1C */ u16 m1C_isCommandEnabled[16]; /* Noah: s16; retail reads lhu */
    /* 0x3C */ u8 m3C_currentTarget;         /* Noah: s8 */
    /* 0x3D */ u8 m3D[3];
} sBattleVar2Sub;

/* Noah: sBattleVar2 (size 0x2F8); battleVar2 = battleVar2 */
typedef struct {
    /* 0x000 */ sBattleVar2Sub m0_entityMenus[11];
    /* 0x2C0 */ u8 m2C0_circleMenuCommandsNames[4];
    /* 0x2C4 */ u8 m2C4[7];
    /* 0x2CB */ u8 m2CB;
    /* 0x2CC */ u8 m2CC_comboInput[7];             /* combo input (0xFF: none) */
    /* 0x2D3 */ u8 m2D3_currentEntityTurn;
    /* 0x2D4 */ u8 m2D4_remainingAP;
    /* 0x2D5 */ u8 m2D5_maxAP;
    /* 0x2D6 */ u8 m2D6_comboInputLength;
    /* 0x2D7 */ u8 m2D7[3];
    /* 0x2DA */ u8 m2DA_indexInBattleVar48;
    /* 0x2DB */ u8 m2DB_turnEnded;
    /* 0x2DC */ u8 m2DC_attackIndex;
    /* 0x2DD */ u8 m2DD_currentActiveBattleMenu;  /* Noah: s8 */
    /* 0x2DE */ u8 m2DE_actionDone;
    /* 0x2DF */ u8 m2DF_currentAttackNumOfApUsed;
    /* 0x2E0 */ u8 m2E0_isBattleMenuDisabled;
    /* 0x2E1 */ u8 m2E1_waitBeforeNextAttackInput;
    /* 0x2E2 */ u8 m2E2_previousActiveBattleMenu; /* Noah: s8; 0xFF: none */
    /* 0x2E3 */ u8 m2E3_hasAttacked;
    /* 0x2E4 */ u8 m2E4_deathblowCompleted;
    /* 0x2E5 */ u8 m2E5_deathblowPerformed;
    /* 0x2E6 */ u8 m2E6_menuSelection;
    /* 0x2E7 */ u8 m2E7_gearAttackCount;
    /* 0x2E8 */ u8 m2E8_currentTarget;
    /* 0x2E9 */ u8 m2E9_targetConfirmed;
    /* 0x2EA */ u8 m2EA_notDefending;
    /* 0x2EB */ u8 m2EB_hpChanged[11];
    /* 0x2F6 */ u8 m2F6_doublePressPending;
    /* 0x2F7 */ u8 m2F7;
} sBattleVar2;

/* Noah: sBattleVar48 (size 0x48); battleCurrentDamages[32] = battleCurrentDamages */
typedef struct {
    /* 0x00 */ u16 m0_damageValue[11];
    /* 0x16 */ u16 m16_targetBitMask;
    /* 0x18 */ u8 m18_damageType[11];  /* Noah: s8; 0xFF: none */
    /* 0x23 */ u8 m23_battleEntityIndex;
    /* 0x24 */ u16 m24_totalDamages[11];               /* total damage shown by the counter */
    /* 0x3A */ u16 m3A_animationParam;                 /* Noah: s16 */
    /* 0x3C */ u8 m3C_totalDamageColors[11];                /* counter colour type */
    /* 0x47 */ u8 m47_battleAnimationToPlay; /* Noah: s8; retail stores 0xFF/0xF5 with li */
} sBattleVar48;

/* Noah: sBattleEntity (size 0x170); battleEntities[11] = battleEntities */
typedef struct {
    /* 0x000 */ GameStateCharacter m0_base;
    /* 0x0A4 */ GameStateGear mA4_gear;
    /* 0x148 */ u8 m148_gearMode;
    /* 0x149 */ u8 m149[3];
    /* 0x14C */ s32 m14C_experience;                 /* enemies: experience (battle_result) */
    /* 0x150 */ u8 m150_dropChance1;                  /* enemies: drop chance 1 (%) */
    /* 0x151 */ u8 m151_dropChance2;                  /* enemies: drop chance 2 (%) */
    /* 0x152 */ u8 m152_dropItem1;                  /* enemies: drop 1 item id */
    /* 0x153 */ u8 m153_dropItem2;                  /* enemies: drop 2 item id */
    /* 0x154 */ u8 m154_dropCategory1;                  /* enemies: drop 1 inventory category */
    /* 0x155 */ u8 m155_dropCategory2;                  /* enemies: drop 2 inventory category */
    /* 0x156 */ u16 m156_gold;                 /* enemies: gold */
    /* 0x158 */ u8 m158_expShares[2];               /* party: experience / ether experience shares */
    /* 0x15A */ u8 m15A_flags;
    /* 0x15B */ u8 m15B;
    /* 0x15C */ u8 m15C_statusTimers[14]; /* Noah: m15C_statusTimer (first) */
    /* 0x16A */ u8 m16A[0x170 - 0x16A];
} sBattleEntity;

/* Noah: sApStruct (size 8); apConfigArray[11] = apConfigArray */
typedef struct {
    /* 0x0 */ u8 m0_storedAP;
    /* 0x1 */ u8 m1_gearState;                    /* != 0: the entity is in its gear */
    /* 0x2 */ u8 m2[2];
    /* 0x4 */ u8 m4_currentAP;
    /* 0x5 */ u8 m5_savedLevels[3];
} sApStruct;

/* Noah: sUnkMonsterStatus (size 4); unknownMonsterStatus0[8] = unknownMonsterStatus0 */
typedef struct {
    /* 0x0 */ u8 m0_hasWhenAttackedScript;                    /* has a "when attacked" script (Noah: s8) */
    /* 0x1 */ u8 m1_hasEndOfTurnScript;                    /* Noah: s8 */
    /* 0x2 */ u8 m2_wasHit;                    /* Noah: s8 */
    /* 0x3 */ u8 m3_removed;
} sUnkMonsterStatus;

/* Noah: sMonsterTurnSub (size 0x60) */
typedef struct {
    /* 0x00 */ POLY_FT4 m0_polys[2];
    /* 0x50 */ RECT m50_monsterNameVRamRect;
    /* 0x58 */ u16* m58_ramBufferFromMonsterName;
    /* 0x5C */ u8 m5C_useOddClut;
    /* 0x5D */ u8 m5D_monsterNameIsDisplayed;
    /* 0x5E */ u8 m5E_currentMonsterNameStringWidth;
    /* 0x5F */ u8 m5F;
} sMonsterTurnSub;

/* Noah: sMonsterTurnDisplay (size 0x308); previousEntityTurn = previousEntityTurn */
typedef struct {
    /* 0x000 */ s8 m0_lastEntityTurn;
    /* 0x001 */ u8 m1[7];
    /* 0x008 */ sMonsterTurnSub m8_nameLabels[8];
} sMonsterTurnDisplay;

/* Noah: sBattle800c3e24 (target selection cursor, size 0xEC); battleG3 = battleG3 */
typedef struct {
    /* 0x00 */ POLY_G3 m0_poly[4 * 2]; /* [direction * 2 + oddOrEven] */
    /* 0xE0 */ s32 mE0_fadeValue;
    /* 0xE4 */ u8 mE4_oddOrEven;
    /* 0xE5 */ u8 mE5_fadeDirection;
    /* 0xE6 */ u8 mE6_isDirectionEnabled[4];
} sBattle800c3e24;

/* Noah: sBattleDialogWindow (battleDialogWindow.h, size 0x5A8);
 * battleDialogWindows[7] = battleDialogWindows */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_corners[4][2];
    /* 0x140 */ POLY_FT4 m140_topEdge[2][2];
    /* 0x1E0 */ POLY_FT4 m1E0_bottomEdge[2][2];
    /* 0x280 */ POLY_FT4 m280_leftEdge[2][2];
    /* 0x320 */ POLY_FT4 m320_rightEdge[2][2];
    /* 0x3C0 */ POLY_G4 m3C0_polyG4[2];
    /* 0x408 */ DR_MODE m408_drawMode[2];
    /* 0x420 */ u8 m420[0x5A0 - 0x420];
    /* 0x5A0 */ s32 m5A0_cornerCount;
    /* 0x5A4 */ u8 m5A4_oddOrEven;
} sBattleDialogWindow;

/* Noah: sBattleDialogWindow2 (an opening dialog window, size 0xE);
 * battleDialogWindows2[7] = battleDialogWindows2 */
typedef struct {
    /* 0x0 */ u16 m0_x;
    /* 0x2 */ u16 m2_y;
    /* 0x4 */ u16 m4_width;
    /* 0x6 */ u16 m6_height;
    /* 0x8 */ u16 m8_currentWidth;
    /* 0xA */ u16 mA_currentHeight;
    /* 0xC */ u8 mC_windowIndex;
} sBattleDialogWindow2;

/* Noah: sBattleRenderStruct2 (battle result / overall UI polys, size 0x5DA4);
 * pBattleRenderStruct2 = pBattleRenderStruct2. Each mXXXX is a POLY_FT4 pair array
 * ([n * 2 + oddOrEven]), sized by the gap to the next one. */
typedef struct {
    /* 0x0000 */ POLY_FT4 m0[24];
    /* 0x03C0 */ POLY_FT4 m3C0_markerXFrame[60];
    /* 0x0D20 */ POLY_FT4 mD20_marker[16];
    /* 0x0FA0 */ POLY_FT4 mFA0_markerOverlay[48];
    /* 0x1720 */ POLY_FT4 m1720_frame[90];
    /* 0x2530 */ POLY_FT4 m2530_frameShadow[86];
    /* 0x32A0 */ POLY_FT4 m32A0_markerYFrame[52];
    /* 0x3AC0 */ POLY_FT4 m3AC0_boosterLabel[22];
    /* 0x3E30 */ POLY_FT4 m3E30[2];
    /* 0x3E80 */ POLY_FT4 m3E80_modeLabel[34];
    /* 0x43D0 */ POLY_FT4 m43D0_modeLabelShadow[18];
    /* 0x46A0 */ POLY_FT4 m46A0_randomDigits[40];
    /* 0x4CE0 */ POLY_FT4 m4CE0_statusGlyphs[10];
    /* 0x4E70 */ POLY_FT4 m4E70_attackDigits[8];
    /* 0x4FB0 */ POLY_FT4 m4FB0_infinityChanceDigits[6];
    /* 0x50A0 */ POLY_FT4 m50A0_defenceDownDigits[8];
    /* 0x51E0 */ POLY_FT4 m51E0_agilityDigits[4];
    /* 0x5280 */ POLY_FT4 m5280_fuelDigits[8];
    /* 0x53C0 */ POLY_FT4 m53C0_maxFuelDigits[10];
    /* 0x5550 */ POLY_FT4 m5550_commandNames[6];
    /* 0x5640 */ POLY_FT4 m5640_comboInputIcons[40];
    /* 0x5C80 */ POLY_FT4 m5C80_fuelCosts[6];
    /* 0x5D70 */ u8 m5D70_count[0x13];
    /* 0x5D83 */ u8 m5D83_oddOrEven[0x19];
    /* 0x5D9C */ u16 m5D9C_markerX;
    /* 0x5D9E */ u16 m5D9E_markerY;
    /* 0x5DA0 */ u8 m5DA0_oddOrEven;
    /* 0x5DA1 */ u8 m5DA1_count;
    /* 0x5DA2 */ s16 m5DA2_blinkTimer;
} sBattleRenderStruct2;

/* Noah: sBattleRunningVar0Sub (one script entity, size 0x38) */
typedef struct {
    /* 0x00 */ u16 m0_scriptBytecodeOffset[8];
    /* 0x10 */ u8 m10_isScriptSlotActive[8];
    /* 0x18 */ u8 m18_slotEntryPoints[8];
    /* 0x20 */ u8 m20_currentActiveEntry;
    /* 0x21 */ u8 m21_currentEntryPoint;
    /* 0x22 */ u8 m22_priority;
    /* 0x23 */ u8 m23_calledEntryPoint;
    /* 0x24 */ u8 m24_portrait;
    /* 0x25 */ u8 m25;
    /* 0x26 */ s16 m26_waitTimer;
    /* 0x28 */ u8 m28_isWaiting;
    /* 0x29 */ u8 m29[3];
    /* 0x2C */ void* m2C_animationBundle;
    /* 0x30 */ void* m30_spriteTask;
    /* 0x34 */ u8 m34_mechaSetupState;
    /* 0x35 */ u8 m35_hasSprite;
    /* 0x36 */ u8 m36[2];
} sBattleRunningVar0Sub;

/* Noah: sBattleRunningVar0 (size 0x828); battleEventVar0 = battleEventVar0 */
typedef struct {
    /* 0x000 */ sBattleRunningVar0Sub m0_scriptEntities[0x10];
    /* 0x380 */ u16 m380_currentArgs[8];
    /* 0x390 */ u8* m390_scriptByteCode;
    /* 0x394 */ s16 m394_vars[512];
    /* 0x794 */ u8 m794_executionOrder[0x10];
    /* 0x7A4 */ POLY_FT4 m7A4_portraitPolys[2];
    /* 0x7F4 */ u8 m7F4_battleDialogPortraitOddOrEven;
    /* 0x7F5 */ u8 m7F5;
    /* 0x7F6 */ u16 m7F6_dialogParams[5]; /* Noah sDialogParams: x, y, width, height, flags */
    /* 0x800 */ u8 m800_isEventDone;
    /* 0x801 */ u8 m801_pauseCountdown;
    /* 0x802 */ u8 m802_dialogBoxInitialized;
    /* 0x803 */ u8 m803;
    /* 0x804 */ u8 m804_isMoving[0x10];
    /* 0x814 */ void* m814_pLoadedMusic;
    /* 0x818 */ SeqFile* m818_eventSeq;
    /* 0x81C */ s16 m81C_loadedMusicId;
    /* 0x81E */ u8 m81E_loadedMusicVol;
    /* 0x81F */ u8 m81F_hasLoadedMusic;
    /* 0x820 */ u8 m820_isSeqLoaded;
    /* 0x821 */ u8 m821[7];
} sBattleRunningVar0;

extern sBattleRunningVar0* battleEventVar0; /* Noah: battleEventVar0 (the battle event overlay's state) */
u8 startBattleAttackAnimation(void);

/* Heap text image handle (a 4-byte struct holding the image pointer). */
typedef struct {
    /* 0x0 */ u16* m0_image;
} sBattleTextImage;

/* Byte offset of entry n of a table of pointers (or of pointer-sized structs
 * such as sBattleTextImage): retail scales n by hand (n << 2); the PC's
 * entries are 8 bytes. */
#define PTR_TABLE_OFS(n, T) ((n) * (s32)sizeof(T))

/* Small glyph table at 0x800D2F68 (4-byte entries). */
typedef struct {
    /* 0x0 */ u8 m0_width;
    /* 0x1 */ u8 m1_clut; /* != 0: odd-line CLUT (textSpriteMode1) */
    /* 0x2 */ u8 m2_u;
    /* 0x3 */ u8 m3_v;
} sBattleGlyphRect;
extern sBattleGlyphRect battleLabelGlyphRects[22]; /* 11 target names, then their odd-line copies */

/* ---- monster AI scripts (Noah: enemyScript.h) ---- */

/* Noah: sBattleScriptExecutionContext */
typedef struct {
    /* 0x0 */ u8* m0_scriptPtr;
} sBattleScriptExecutionContext;

/* Noah: sBattleScriptEntity (size 0x40); monstersScriptsEntities[8] = monstersScriptsEntities */
typedef struct {
    /* 0x00 */ u8* m0_scripts[4];
    /* 0x10 */ s32 m10_s32Vars[4];
    /* 0x20 */ u16 m20_s16Vars[8];      /* Noah: s16; retail reads lhu */
    /* 0x30 */ u8 m30_s8Vars[16];
} sBattleScriptEntity;

/* Noah: sBattleScriptContext (size 8); battleScriptContext[32] = battleScriptContext */
typedef struct {
    /* 0x0 */ u8 m0_attackType;
    /* 0x1 */ u8 m1_operand;
    /* 0x2 */ u8 m2_animationId;                    /* Noah: s8 */
    /* 0x3 */ u8 m3_nameIndex;
    /* 0x4 */ u8 m4_valueLow;                    /* Noah: s16 m4; retail reads the bytes */
    /* 0x5 */ u8 m5_valueHigh;
    /* 0x6 */ u16 m6_mask;
} sBattleScriptContext;

/* ---- battle commands / abilities ---- */

/* Noah: sBattle800CDD40Sub (one command / ability entry, size 0x28) */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ s16 m2_attackType;
    /* 0x04 */ u8 m4[4];
    /* 0x08 */ u16 m8_elements;                  /* element bits (0x8000 >> element) */
    /* 0x0A */ s16 mA_attackFlags;
    /* 0x0C */ u8 mC[4];
    /* 0x10 */ u8 m10_weaponMask;
    /* 0x11 */ u8 m11_power;
    /* 0x12 */ u8 m12;
    /* 0x13 */ u8 m13_cost;
    /* 0x14 */ u8 m14_etherHitBonus;
    /* 0x15 */ u8 m15_hitBonus;
    /* 0x16 */ u8 m16_effectType;
    /* 0x17 */ u8 m17_apCost;
    /* 0x18 */ u8 m18_chanceSource;
    /* 0x19 */ u8 m19;
    /* 0x1A */ u8 m1A_attackFormula;
    /* 0x1B */ u8 m1B_defenceFormula;
    /* 0x1C */ u8 m1C_chance;
    /* 0x1D */ u8 m1D_statusType;
    /* 0x1E */ u16 m1E_statusMask;
    /* 0x20 */ u8 m20;
    /* 0x21 */ u8 m21;
    /* 0x22 */ u8 m22_elementFlags;
    /* 0x23 */ u8 m23;
    /* 0x24 */ u16 m24_fuelCost;
    /* 0x26 */ u8 m26[2];
} sBattle800CDD40Sub;

/* Noah: sBattle800cdd40 (size 0x5F0); partyBattleStats[3] = partyBattleStats */
typedef struct {
    /* 0x000 */ sBattle800CDD40Sub m0_commands[38];
} sBattle800cdd40;

/* Noah: sBattle800CEF10 (size 0x690); mechaBattleStats[3] = mechaBattleStats */
typedef struct {
    /* 0x000 */ sBattle800CDD40Sub m0_commands[42];
} sBattle800CEF10;

/* 16-byte entries at 0x800D2204, indexed by an item/ability id */
typedef struct {
    /* 0x0 */ u16 m0_targetFlags;
    /* 0x2 */ u8 m2[8];
    /* 0xA */ u16 mA_attackType;
    /* 0xC */ u8 mC[4];
} sBattle800D2204;

/* Item menu list at 0x800D2C9E: flags, then per-slot counts and ids
 * (slot index = row * 2 + col). */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u8 m2[0x10];
    /* 0x12 */ u8 m12_counts[0x30];
    /* 0x42 */ u8 m42_ids[0x30];
} sBattleItemList;

/* One glyph of a battle command ring layout (battleMenuGraphicConfigs2 tables), size 8,
 * terminated by m0_id == 0xFFFF. m0_id >= 0x4000: the glyph comes from the
 * entity's command mapping (low byte) and names slot (m0_id & 3). */
typedef struct {
    /* 0x0 */ u16 m0_id;
    /* 0x2 */ s16 m2_x;
    /* 0x4 */ s16 m4_y;
    /* 0x6 */ u16 m6_flags;
} sBattleMenuGlyph;

/* Command ring set (battleMenuGraphicConfigs[] entries): the two pages drawn and the
 * layout (battleMenuGraphicConfigs2 index) of each. */
typedef struct {
    /* 0x0 */ u8 m0_page[2];
    /* 0x2 */ u8 m2_layout[2];
} sBattleCommandSet;

/* Jump path points built by initJumpData (battleBssBlock.mA3C_jumpPoints, 9 entries) */
typedef struct {
    /* 0x0 */ u16 m0_x;
    /* 0x2 */ u16 m2_z;
    /* 0x4 */ u8 m4_flags;
    /* 0x5 */ u8 m5;
} sBattleJumpPoint;

/* Noah: battleSlotLayout (battleSlotLayout) */
typedef struct {
    /* 0x0 */ u8 m0_count;
    /* 0x1 */ u8 m1_mask;
    /* 0x2 */ u8 m2;
    /* 0x3 */ u8 m3;
} sBattleSlotLayout;

/* A camera preset of the battle config (target, then eye). */
typedef struct {
    /* 0x0 */ s16 m0_at[3];
    /* 0x6 */ s16 m6_eye[3];
} sBattleCameraPreset;

/* Character positions of one battle position slot (8 * 4 bytes). */
typedef struct {
    /* 0x00 */ Vec2s16 m0_party[3];     /* [m1] */
    /* 0x0C */ Vec2s16 mC_monsters[5];  /* [m1] */
} sBattleSlotPositions;

/* Noah: sBattleMechaInitData: the battle config file seen from its data + 4
 * (battleMechaInitData = battleMechaInitData; main's battleEnvMechaInitData). */
typedef struct sBattleMechaInitData {
    /* 0x000 */ s32 m0;
    /* 0x004 */ sBattleSlotPositions m4_slotPositions[7]; /* [positionSlot] (Noah: Vec2s16 m4[8][8], [slot][m1] for
                                             * the party, [slot][m1 + 3] for monsters; its last row
                                             * overlaps m100) */
    /* 0x0E4 */ u8 mE4[0x100 - 0xE4];
    /* 0x100 */ Vec2s16 m100_gearPositions[8][2];
    /* 0x140 */ u8 m140_jumpPaths[8][8][8];       /* [attacker slot][target slot][] */
    /* 0x340 */ u8 m340_terrainAgility[4];
    /* 0x344 */ s16 m344_mechaScale;               /* battle mecha scale (x/0x1000) */
    /* 0x346 */ s16 m346;
    /* 0x348 */ s16 m348_trackPoolSize;               /* track pool size */
    /* 0x34A */ s16 m34A_trailPoolSize;               /* sMechaInitVar3 count */
    /* 0x34C */ s16 m34C_maxX;          /* arena bounds */
    /* 0x34E */ s16 m34E_minX;
    /* 0x350 */ s16 m350_minZ;
    /* 0x352 */ s16 m352_maxZ;
    /* 0x354 */ u8 m354[0x35F - 0x354];
    /* 0x35F */ u8 m35F_soundMode;      /* sound events: 3 and event m7 != 2 scale the volume by 60/107 */
    /* 0x360 */ u8 m360[0x464 - 0x360];  /* background layers (battle_loader sBattleEnvConfig) */
    /* 0x464 */ s16 m464_light[3];      /* light direction (-> light matrix row 0) */
    /* 0x46A */ u8 m46A[2];
    /* 0x46C */ s16 m46C_color[3];      /* light colour (-> colour matrix column 0) */
    /* 0x472 */ u8 m472[2];
    /* 0x474 */ u8 m474_backR;          /* back colour (SetBackColor) */
    /* 0x475 */ u8 m475_backG;
    /* 0x476 */ u8 m476_backB;
    /* 0x477 */ u8 m477;
    /* 0x478 */ u8 m478_r;              /* shadow poly colour */
    /* 0x479 */ u8 m479_g;
    /* 0x47A */ u8 m47A_b;
    /* 0x47B */ u8 m47B;
    /* 0x47C */ sBattleCameraPreset m47C_cameraPresets[8]; /* [0].at/eye: the opening camera */
    /* 0x4DC */ s16 m4DC_effectGround[3]; /* effect events (mode 2): terrain height probe */
    /* 0x4E2 */ u8 m4E2[0x50C - 0x4E2];
    /* 0x50C */ s32 m50C_terrainVertices;   /* offsets from this struct (0: none) */
    /* 0x510 */ s32 m510_terrainTriangles;  /* -> s32 count, then the triangles */
    /* 0x514 */ s32 m514_animation;         /* relocatable environment gear animation */
} sBattleMechaInitData;

/* Battle views of a mecha animation event (sMechaAnimEvent, field_mecha/
 * mecha.h) by type, as updateMechAnimSub4_battle reads them. */

/* Type 1: a sprite effect on a mecha bone; from m5 on it is the
 * sMechaBoneAttachmentConfig that spawnMechaBoneAttachedSprite_battle reads. */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_animationId;
    /* 0x04 */ u8 m4_flags;        /* 0x80: m3 indexes the gear's effect table; also the damage mask */
    /* 0x05 */ u8 m5_boneIndex;    /* 0: root matrix */
    /* 0x06 */ s16 m6_x;           /* position in bone space */
    /* 0x08 */ s16 m8_y;
    /* 0x0A */ s16 mA_z;
    /* 0x0C */ u8 mC_mode;         /* & 0x7F: 1 mecha ground y, 2 terrain height; 0x80: at the target */
    /* 0x0D */ u8 mD_absoluteDirection;
    /* 0x0E */ s16 mE_direction;
    /* 0x10 */ s16 m10_scale;      /* * mecha scale >> 8 */
    /* 0x12 */ u8 m12_vramData;    /* 0: createSavePointMeshData_mode5, else createSavePointMeshData_mode6 */
    /* 0x13 */ u8 m13_attach;
} sMechaAnimEventEffect; /* 0x14 */

/* Types 3/4: a bone trail emitter (initMechaTrailEmitter_battle arguments). */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;         /* - 3: emitter mode */
    /* 0x03 */ u8 m3_emitter;      /* sLoadedMechas::m110 index */
    /* 0x04 */ u8 m4_start;        /* 0: stop only */
    /* 0x05 */ u8 m5_boneIndex;
    /* 0x06 */ u8 m6_maxCount;
    /* 0x07 */ u8 m7_lifetime;
    /* 0x08 */ u8 m8_colors[6];           /* colour, colour step */
    /* 0x0E */ s16 mE_edgePoints[6];          /* the two edge points */
    /* 0x1A */ u8 m1A_semiTrans;
} sMechaAnimEventTrail; /* 0x1C */

/* Type 5: a sound effect. */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_sound;
    /* 0x04 */ u8 m4_mask;         /* damage mask (isMechaEventAllowedForHitResult) */
    /* 0x05 */ u8 m5_bank;         /* getMechaSoundEffectIdBase_battle */
    /* 0x06 */ u8 m6_sound2;       /* 0: none */
    /* 0x07 */ u8 m7;
} sMechaAnimEventSound; /* 0x08 */

/* Type 8: play an animation on the mechas of m10A (per target damage type). */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_mask;         /* damage mask (isMechaEventAllowedForHitResult) */
    /* 0x04 */ u8 m4_useOther;     /* mechaPlayAnimationFromSource_battle instead of mechaPlayAnimationPreserveState_battle */
    /* 0x05 */ u8 m5_anim[5];      /* by damage type: 0/1 (m6 for mechas with m4A 2), 5, 4, 2/3 */
} sMechaAnimEventAnim; /* 0x0A */

/* Battle view of the extra animation file's sound/texture table
 * (sLoadedMechas::mB4 = sMechaDataTable2::m8 of sLoadedMechas::mC), with
 * the fields processMechaAnimData_battle reads beyond sMechaDataTable2_8. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ void* m4_spriteBundle; /* == m8_seq: none */
    /* 0x08 */ u8* m8_seq;
    /* 0x0C */ u8* mC_seqEnd;                /* == m8_seq: no sequence */
    /* 0x10 */ u8* m10;
    /* 0x14 */ void* m14_textures;
    /* 0x18 */ u8* m18_texturesEnd;               /* == m14_textures: no textures */
} sBattleMechaSoundData;

/* The battle's sLoadedMechas is 0x11C bytes (no m11C/m128 translation
 * history); processMechaAnimData_battle copies one as a whole. */
typedef struct {
    /* 0x000 */ s32 m0[0x11C / 4];
} sBattleLoadedMechasCopy;

/* Noah: sBattleStartEffectSub (size 0x7C) */
typedef struct {
    /* 0x00 */ SVECTOR m0_rotation;
    /* 0x08 */ SVECTOR m8_rotationStep;
    /* 0x10 */ VECTOR m10_position;
    /* 0x20 */ s32 m20;
    /* 0x24 */ POLY_FT3 m24_poly[2];
    /* 0x64 */ s32 m64_delay;
    /* 0x68 */ VECTOR m68_velocity;     /* 16.16 */
    /* 0x78 */ s32 m78_gravity;
} sBattleStartEffectSub;

/* Noah: sBattleStartEffect (a task pair, size 0x10F7C) */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_frameCounter;
    /* 0x3C */ sBattleStartEffectSub m3C_triangleData[2][14][20];
} sBattleStartEffect;

/* ---- sprites, animation, camera (Noah: battle.h, battleSpriteLoader.h) ---- */

/* Noah: sJumpAnimationControlStruct (size 0x50); jumpAnimationControlStruct = jumpAnimationControlStruct */
typedef struct sJumpAnimationControlStruct {
    /* 0x00 */ void* m0;
    /* 0x04 */ SpriteActorCore* m4_spriteCore;
    /* 0x08 */ void (*m8)(struct sJumpAnimationControlStruct*);
    /* 0x0C */ u8 mC[0x1C - 0xC];
    /* 0x1C */ u32 m1C_mode;
    /* 0x20 */ u32 m20_actor;
    /* 0x24 */ u32 m24_entity;
    /* 0x28 */ u32 m28_targetSlot;
    /* 0x2C */ u32 m2C_jumpPointIndex;
    /* 0x30 */ u32 m30;
    /* 0x34 */ s32 m34_pendingSteps;
    /* 0x38 */ u8 m38[0x40 - 0x38];
    /* 0x40 */ u32 m40_facing;
    /* 0x44 */ s32 m44_distanceToTarget;
    /* 0x48 */ u8 m48_isStepDone;
    /* 0x49 */ u8 m49_completedMode;
    /* 0x4A */ u8 m4A;
    /* 0x4B */ u8 m4B;
    /* 0x4C */ SpriteActorCore* m4C_target;
} sJumpAnimationControlStruct;

/* Noah: sBattleMechaLoaderTask (task + battle entity index, size 0x20) */
typedef struct {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ s32 m1C_entity;
} sBattleMechaLoaderTask;

/* Character blinking task (Noah: updateCharacterBlinkingTask), task + 0xC */
typedef struct {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ SpriteActorCore* m1C_actor;
    /* 0x20 */ s32 m20_phase;
    /* 0x24 */ s32 m24_entity;
} BattleBlinkTask;

/* Noah: sBattleSpinningEnemySelectionCursor (task pair + cursor, size 0x68) */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ SpriteActorCore* m38_actor;
    /* 0x3C */ VECTOR m3C_translation;
    /* 0x4C */ SVECTOR m4C_rotation;
    /* 0x54 */ s32 m54_actorHeight;
    /* 0x58 */ s32 m58_tick;
    /* 0x5C */ void* m5C_buffer[2];
    /* 0x64 */ void* m64_customPolySubBuffer;
} sBattleSpinningEnemySelectionCursor;

/* Noah: sBattleVisualBuffer (size 0xC); battleVisualBuffers[11] = D_800CCBD4 */
typedef struct {
    /* 0x0 */ void* m0_spriteData;
    /* 0x4 */ s16 m4_vramX;
    /* 0x6 */ s16 m6_vramY;
    /* 0x8 */ u32 m8_vramParam;
} sBattleVisualBuffer;

/* Noah: sBattleSpriteActor (task pair + sprite actor, size 0x19C) */
typedef struct sBattleSpriteActor {
    /* 0x000 */ TaskHeader m0_updateTask;
    /* 0x01C */ TaskHeader m1C_drawTask;
    /* 0x038 */ SpriteActor m38_spriteActor;
} sBattleSpriteActor;

/* The 4-bit battle slot of a sprite actor. US packs it across 0xA8 bit 30 ..
 * 0xAC bit 1 (a packed bitfield; cdk reads it as lw 0xA8 / lw 0xAC, srl 30,
 * andi 3, sll 2, or). SpriteActorCore splits it into mA8_x1E + mAC_x0; this
 * view reads it the way the original code did. */
/* The PC's SpriteActorCore holds 8-byte pointers before 0xA8, so the view
 * would read pointer bits (always slot 0): read the fields themselves. Some
 * battle scripts ask for the slot of a missing actor (a NULL target): the
 * PS1 then reads BIOS RAM, 0x01000008 at 0xA8 and 0 at 0xAC, so slot 0. */
static inline s32 pcBattleSlot(const void* a) {
    const SpriteActorCore* s = (const SpriteActorCore*)a;
    if (s == NULL) {
        return 0;
    }
#if VERSION_JP
    return s->mAC_x0;
#else
    return s->mA8_x1E | (s->mAC_x0 << 2);
#endif
}
#define BATTLE_SLOT(a) pcBattleSlot(a)

/* One entry of the pad change history (copied as a struct: lwl/lwr pairs). */
typedef struct {
    /* 0x0 */ u16 m0_held;
    /* 0x2 */ u16 m2_pressed;
    /* 0x4 */ u16 m4_released;
    /* 0x6 */ u16 m6_time;              /* low half of startOfBattleTime */
} BattlePadHistory;

/* Pad state kept at 0x800CCB08 (battleRender's input sampler updateBattlePadState). */
typedef struct {
    /* 0x00 */ u16 m0_held;             /* 0x800CCB08 */
    /* 0x02 */ u16 m2_held2;            /* 0x800CCB0A (pad 1) */
    /* 0x04 */ u16 m4_pressed;          /* 0x800CCB0C */
    /* 0x06 */ u16 m6_pressed2;         /* 0x800CCB0E */
    /* 0x08 */ u16 m8_released;         /* 0x800CCB10 */
    /* 0x0A */ u16 mA_released2;        /* 0x800CCB12 */
    /* 0x0C */ BattlePadHistory mC_history[4]; /* 0x800CCB14: last 4 changes, newest first */
} BattlePadState; /* 0x2C */

/* ---- custom poly models, shake, fader, ramp (0x800B1720-0x800B3E04) ---- */

/* Noah: sCustomPolySubBuffer (battle/customPolyBuffer.h): a model with its
 * vertices at m0 bytes and m14 packets at m10 bytes from its start. */
typedef struct sCustomPolySubBuffer {
    /* 0x00 */ s32 m0_offsetToVertices;
    /* 0x04 */ s32 m4_numVertices;
    /* 0x08 */ s32 m8_offsetToNormals;
    /* 0x0C */ s32 mC;
    /* 0x10 */ s32 m10_offsetToPackets;
    /* 0x14 */ s32 m14_count;
    /* 0x18 */ s32 m18_flags;           /* 0x8000: vertices already scaled */
} sCustomPolySubBuffer;

/* Noah: sCustomPolyBuffer: a header, then m8_count sub buffers. */
typedef struct sCustomPolyBuffer {
    /* 0x00 */ s32 m0;
    /* 0x04 */ s32 m4_flags;            /* 1: the sub buffers' offsets are already pointers */
    /* 0x08 */ s32 m8_count;
    /* 0x0C */ sCustomPolySubBuffer mC_buffers[1];
} sCustomPolyBuffer;

/* Screen shake task (0x800B3358..0x800B3658): allocateUpdateTask(0, 0x24).
 * m1C is the current offset, m24 the start, m2C the target; all three are
 * SVECTORs with the pad unused. */
typedef struct BattleShake {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ SVECTOR m1C_current;
    /* 0x24 */ SVECTOR m24_start;
    /* 0x2C */ SVECTOR m2C_target;
    /* 0x34 */ s32 m34_frame;
    /* 0x38 */ s32 m38_remaining;
    /* 0x3C */ s32 m3C_duration;
} BattleShake; /* 0x40 */

/* Noah: sBattleFader (battle/battleFader.cpp), size 0x4C */
typedef struct sBattleFader {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_duration;
    /* 0x3C */ s32 m3C_remaining;
    /* 0x40 */ s8 m40;
    /* 0x41 */ u8 m41_tpageABE;
    /* 0x42 */ u8 m42_targetColor[3];
    /* 0x45 */ u8 m45_startColor[3];
    /* 0x48 */ u8 m48_currentColor[3];
} sBattleFader;

/* Volume/pitch ramp task (0x800B3B94..0x800B3E04): allocateTaskPair(0x50,..). */
typedef struct BattleRamp {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_duration;
    /* 0x3C */ s32 m3C_remaining;
    /* 0x40 */ s16 m40_start;
    /* 0x42 */ s16 m42_target;
    /* 0x44 */ s16 m44_applied;
    /* 0x46 */ s16 m46_current;
    /* 0x48 */ s16 m48_mode;
    /* 0x4A */ s16 m4A_rOffset;
    /* 0x4C */ s16 m4C_gOffset;
    /* 0x4E */ s16 m4E_bOffset;
} BattleRamp; /* 0x50 */

/* ---- battle sprite bytecode extension (0x800B3E04-0x800B6F0C) ---- */

/* Sprite-trail task (battleSpriteEffect_1): 5 subgroup points of an actor, the
 * drawn copy (m78) easing towards the actor's (m50). allocateTaskPair(0xB8). */
typedef struct BattleSpriteTrail {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ SpriteActorCore* m38_actor;
    /* 0x3C */ s32 m3C_frame;
    /* 0x40 */ s32 m40_animationId;
    /* 0x44 */ s32 m44_blendMode;
    /* 0x48 */ s32 m48_width;
    /* 0x4C */ u8* m4C_args;
    /* 0x50 */ SVECTOR m50_target[5];
    /* 0x78 */ SVECTOR m78_current[5];
    /* 0xA0 */ u8 mA0[0x18];
} BattleSpriteTrail; /* 0xB8 */

/* Distance watch task (battleSpriteOpcode_FB), allocateUpdateTask(owner, 0x18). */
typedef struct BattleSpriteDistanceTask {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ SpriteActorCore* m1C_actor;
    /* 0x20 */ s32 m20;
    /* 0x24 */ s32 m24_animationId;
    /* 0x28 */ s32 m28_distance;
    /* 0x2C */ s32 m2C_minDistance;
    /* 0x30 */ u8* m30_bytecode;
} BattleSpriteDistanceTask; /* 0x34 */

/* Attach task (battleSpriteEffect_3): keeps the actor's target glued to one of the
 * actor's subgroups. allocateUpdateTask(owner, 0x18). */
typedef struct BattleSpriteAttachTask {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ SpriteActorCore* m1C_actor;
    /* 0x20 */ SpriteActorCore* m20_target;
    /* 0x24 */ s32 m24_actorSubgroup;
    /* 0x28 */ s32 m28_targetSubgroup;
    /* 0x2C */ s32 m2C_frame;
    /* 0x30 */ s32 m30_animationId;
} BattleSpriteAttachTask; /* 0x34 */

/* Distance watch task (startSpriteDistanceWatch): calls m2C(actor) when the actor's
 * distance to its target grows or drops below m28. allocateUpdateTask(owner, 0x14). */
typedef struct {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ SpriteActorCore* m1C_actor;
    /* 0x20 */ s32 m20_animationId;
    /* 0x24 */ s32 m24_distance;
    /* 0x28 */ s32 m28_minDistance;
    /* 0x2C */ void (*m2C_callback)(SpriteActorCore*);
} BattleDistanceWatchTask; /* 0x30 */

/* ---- damage display (Noah: damageDisplay.cpp, damageDisplay2.cpp) ---- */

/* Noah: sColorAndCode */
typedef struct {
    /* 0x0 */ u8 m0_r;
    /* 0x1 */ u8 m1_g;
    /* 0x2 */ u8 m2_b;
    /* 0x3 */ u8 m3_code;
} sColorAndCode;

/* Noah: sDamageDisplaySub0 (one glyph quad, size 0x18) */
typedef struct {
    /* 0x00 */ s16 m0_screenPosition[2];
    /* 0x04 */ u8 m4_U;
    /* 0x05 */ u8 m5_V;
    /* 0x06 */ u8 m6_width;
    /* 0x07 */ u8 m7_height;
    /* 0x08 */ u8 m8;
    /* 0x09 */ u8 m9;
    /* 0x0A */ u16 mA_tpage;
    /* 0x0C */ u16 mC_clut;
    /* 0x0E */ u8 mE[2];
    /* 0x10 */ sColorAndCode m10;
    /* 0x14 */ u32 m14;
} sDamageDisplaySub0;

/* Noah: sDamageString */
typedef struct {
    /* 0x0 */ u8 m0_length;
    /* 0x1 */ u8 m1_string[7];
} sDamageString;

/* Noah: sDamageDisplayTask (size 0x1B0) */
typedef struct sDamageDisplayTask {
    /* 0x000 */ TaskHeader m0_updateTask;
    /* 0x01C */ TaskHeader m1C_drawTask;
    /* 0x038 */ struct sDamageDisplayTask* m38_pNext;
    /* 0x03C */ u8 m3C[4];
    /* 0x040 */ SVECTOR m40_rotation;
    /* 0x048 */ VECTOR m48_position;
    /* 0x058 */ VECTOR m58_scale;
    /* 0x068 */ u8 m68[0x10];
    /* 0x078 */ SpriteActorCore* m78_actor;
    /* 0x07C */ u8 m7C_direction;
    /* 0x07D */ u8 m7D[3];
    /* 0x080 */ sColorAndCode m80_colorAndCode;
    /* 0x084 */ u8 m84[4];
    /* 0x088 */ s32 m88_timer;
    /* 0x08C */ s32 m8C_damageStringLength;
    /* 0x090 */ sDamageDisplaySub0 m90_glyphs[12];
} sDamageDisplayTask;

/* Total-damage counter task (Noah: sDamageDisplayPoly, damageDisplay2.cpp);
 * damageDisplayPolyTask2, one instance. */
typedef struct sDamageDisplayPoly {
    /* 0x000 */ TaskHeader m0_updateTask;
    /* 0x01C */ TaskHeader m1C_drawTask;
    /* 0x038 */ SVECTOR m38_rotation;
    /* 0x040 */ VECTOR m40_scale;
    /* 0x050 */ s16 m50_x;
    /* 0x052 */ s16 m52_y;
    /* 0x054 */ u8 m54[0xC];
    /* 0x060 */ sColorAndCode m60_colorAndCode;
    /* 0x064 */ s32 m64_count;
    /* 0x068 */ sDamageDisplaySub0 m68_glyphs[12]; /* the instance is 0x188 bytes: numValidTarget follows */
} sDamageDisplayPoly;

/* Floating total-damage particle task (createDamageDisplayTask3, size 0x130) */
typedef struct sDamageParticleTask {
    /* 0x000 */ TaskHeader m0_updateTask;
    /* 0x01C */ TaskHeader m1C_drawTask;
    /* 0x038 */ SVECTOR m38_rotation;
    /* 0x040 */ VECTOR m40_scale;
    /* 0x050 */ u8 m50[0x10];
    /* 0x060 */ s32 m60_timer;
    /* 0x064 */ s32 m64_rotationStep;
    /* 0x068 */ sColorAndCode m68_colorAndCode;
    /* 0x06C */ s32 m6C_count;
    /* 0x070 */ sDamageDisplaySub0 m70_glyphs[11];
} sDamageParticleTask;

/* ---- terrain, VRAM moves (Noah: battleTerrain.h) ---- */

/* Noah: sBattleImageMoveCommand (main: filesystem.c ImageMoveCommand), 0x18 */
typedef struct {
    /* 0x00 */ u16 m0_x;
    /* 0x02 */ u16 m2_y;
    /* 0x04 */ u16 m4_width;
    /* 0x06 */ s16 m6_totalHeight;
    /* 0x08 */ u16 m8_height;
    /* 0x0A */ u16 mA_count;
    /* 0x0C */ u16 mC_destX;
    /* 0x0E */ u16 mE_destY;
    /* 0x10 */ u8* m10_speeds;
    /* 0x14 */ u16* m14_offsets;
} sBattleImageMoveCommand;

/* Noah: sBattleTerrainTriangle (0xE bytes); battleTerrainTriangles = battleTerrainTriangles */
typedef struct {
    /* 0x0 */ s16 m0_vertexIndices[3];
    /* 0x6 */ s16 m6_neighborIndices[3];
    /* 0xC */ u8 mC_flags;
    /* 0xD */ u8 mD_visited;
} sBattleTerrainTriangle;

/* ---- environment ---- */

/* The battle horizon backdrop (battle800c3ea0: built by battle_loader's
 * createBattleHorizon, allocateHeap(0x17CC), drawn by
 * drawBattleHorizonBackdrop): a 9x9 vertex grid with 128 textured quads, plus
 * flat / gradient sky quads. */
typedef struct {
    /* 0x0000 */ s16 m0_u;
    /* 0x0002 */ s16 m2_v;
    /* 0x0004 */ s16 m4_du;
    /* 0x0006 */ s16 m6_dv;
    /* 0x0008 */ u16 m8_texSize;
    /* 0x000A */ s16 mA_height;
    /* 0x000C */ s16 mC_skyBandScale;
    /* 0x000E */ s16 mE_groundBandScale;
    /* 0x0010 */ s16 m10_geomScreen;
    /* 0x0012 */ s16 m12_tiltScale;
    /* 0x0014 */ s16 m14_horizonX;
    /* 0x0016 */ s16 m16_horizonY;
    /* 0x0018 */ s16 m18_horizonDistance;
    /* 0x001A */ u8 m1A[2];
    /* 0x001C */ CVECTOR m1C_colors[2];
    /* 0x0024 */ DR_MODE m24_drawModes[2];  /* per buffer: the texture window */
    /* 0x003C */ DR_MODE m3C_drawModes[2];  /* per buffer: the default window */
    /* 0x0054 */ SVECTOR m54_grid[9 * 9];
    /* 0x02DC */ POLY_FT4 m2DC_quads[128];
    /* 0x16DC */ POLY_F4 m16DC_flat[4];
    /* 0x173C */ POLY_G4 m173C_gradient[4];
} sBattleHorizon; /* 0x17CC */

/* ---- result screen (drawn by the battle_result overlay) ---- */

/* A POLY_FT4 list drawn by battleRenderPolyArray (battleRenderPolyArray):
 * polys[i][oddOrEven], count and oddOrEven kept next to each other. */
typedef struct {
    u8 m0_count;
    u8 m1_oddOrEven;
} sBattleResultPolyCount;

/* Per-character result window, battleResultCharWindows[3] (0x15FC bytes each). */
typedef struct {
    /* 0x0000 */ POLY_FT4 m0_name[4][2];
    /* 0x0140 */ POLY_FT4 m140_labels[20][2];
    /* 0x0780 */ POLY_FT4 m780_level[3][2];
    /* 0x0870 */ POLY_FT4 m870_etherLevel[3][2];
    /* 0x0960 */ POLY_FT4 m960_hp[3][2];
    /* 0x0A50 */ POLY_FT4 mA50_maxHp[3][2];
    /* 0x0B40 */ POLY_FT4 mB40_mp[2][2];
    /* 0x0BE0 */ POLY_FT4 mBE0_maxMp[2][2];
    /* 0x0C80 */ POLY_FT4 mC80_exp[8][2];
    /* 0x0F00 */ POLY_FT4 mF00_etherExp[8][2];
    /* 0x1180 */ POLY_FT4 m1180_expGained[7][2];
    /* 0x13B0 */ POLY_FT4 m13B0_etherExpGained[7][2];
    /* 0x15E0 */ sBattleResultPolyCount m15E0_nameCount;  /* m0_name */
    /* 0x15E2 */ sBattleResultPolyCount m15E2_labelsCount;  /* m140 */
    /* 0x15E4 */ sBattleResultPolyCount m15E4_levelCount;  /* m780 */
    /* 0x15E6 */ sBattleResultPolyCount m15E6_etherLevelCount;  /* m870 */
    /* 0x15E8 */ sBattleResultPolyCount m15E8_hpCount;  /* m960 */
    /* 0x15EA */ sBattleResultPolyCount m15EA_maxHpCount;  /* mA50 */
    /* 0x15EC */ sBattleResultPolyCount m15EC_mpCount;  /* mB40 */
    /* 0x15EE */ sBattleResultPolyCount m15EE_maxMpCount;  /* mBE0 */
    /* 0x15F0 */ sBattleResultPolyCount m15F0_expCount;  /* mC80 */
    /* 0x15F2 */ sBattleResultPolyCount m15F2_etherExpCount;  /* mF00 */
    /* 0x15F4 */ sBattleResultPolyCount m15F4_expGainedCount;  /* m1180 */
    /* 0x15F6 */ sBattleResultPolyCount m15F6_etherExpGainedCount;  /* m13B0 */
    /* 0x15F8 */ u8 m15F8_showEther;                     /* [0] only: show ether values (!(gameState.m22B6 & 0x8000)) */
    /* 0x15F9 */ u8 m15F9_isCountingExp;                     /* [0] only: EXP count-up running */
    /* 0x15FA */ u8 m15FA_expCountDone;
    /* 0x15FB */ u8 m15FB_etherExpCountDone;
} sBattleResultChar; /* 0x15FC */

/* ---- .bss aggregates ---- */

/* Turn timers at 0x800D2DF0: retail addresses the three arrays from one
 * base. Noah: battleSlotStatusVar0, numTicksBeforeReady,
 * battleSlotHalfSpeedToggle (separate arrays there). */
typedef struct {
    /* 0x00 */ s16 m0_battleSlotStatusVar0[11];       /* 0x800D2DF0 */
    /* 0x16 */ s16 m16_numTicksBeforeReady[11];       /* 0x800D2E06 */
    /* 0x2C */ s16 m2C_battleSlotHalfSpeedToggle[11]; /* 0x800D2E1C */
} BattleSlotTicks;

/* Battle .bss block at 0x800C3EB0 (Noah: battleBssBlock,
 * battleVisualEntities, battleCurrentDamages, ... battleEntities). Retail
 * reaches its members from one base (+0x54 for battleVisualEntities[3],
 * +0x8C8C, +0xB88, +0x4BF9, 0x8000+0xC50/0xDA8, the entities at +0x8E38),
 * which GCC only does within one symbol. Absolute addresses in the comments. */
typedef struct {
    /* 0x0000 */ u8* m0_battleLoadDataVar2Ter;                    /* 0x800C3EB0 */
    /* 0x0004 */ sBattleVisualEntity m4_visualEntities[11];      /* 0x800C3EB4 Noah: battleVisualEntities */
    /* 0x0138 */ sBattleVar48 m138_currentDamages[32];           /* 0x800C3FE8 Noah: battleCurrentDamages */
    /* 0x0A38 */ u16 mA38_performAttackSub3_var0;                /* 0x800C48E8 */
    /* 0x0A3A */ u8 mA3A_battleRunningVar1;                      /* 0x800C48EA */
    /* 0x0A3B */ u8 mA3B;
    /* 0x0A3C */ sBattleJumpPoint mA3C_jumpPoints[9];            /* 0x800C48EC */
    /* 0x0A72 */ u8 mA72_currentEntity;                          /* 0x800C4922 (entity whose sprite blinks) */
    /* 0x0A73 */ u8 mA73;                                        /* 0x800C4923 */
    /* 0x0A74 */ u8 mA74[0xA78 - 0xA74];
    /* 0x0A78 */ u8 mA78_startPlayingAttackAnimationVar0;        /* 0x800C4928 */
    /* 0x0A79 */ u8 mA79;                                        /* 0x800C4929 (cleared by createDamageDisplay) */
    /* 0x0A7A */ u8 mA7A[0xB70 - 0xA7A];
    /* 0x0B70 */ BattleRenderStruct mB70_renderStructs[2];       /* 0x800C4A20 Noah: battleRenderStructs */
    /* 0x8C50 */ BattleRenderStruct* m8C50_currentRenderStruct;  /* 0x800CCB00 Noah: pCurrentBattleRenderStruct */
    /* 0x8C54 */ OTag* m8C54_currentOT;                          /* 0x800CCB04 Noah: pCurrentBattleOT */
    /* 0x8C58 */ BattlePadState m8C58_pad;                       /* 0x800CCB08 */
    /* 0x8C84 */ s32 m8C84_oddOrEven;                            /* 0x800CCB34 Noah: battleOddOrEven */
    /* 0x8C88 */ s32 m8C88;
    /* 0x8C8C */ SpriteActorCore* m8C8C_spriteActorCores[11];    /* 0x800CCB3C Noah: battleSpriteActorCores */
    /* 0x8CB8 */ sBattleSpriteActor* m8CB8_spriteActors[11];     /* 0x800CCB68 Noah: battleSpriteActors */
    /* 0x8CE4 */ MATRIX m8CE4_matrix800CCB94;                    /* 0x800CCB94 Noah: battleMatrix800CCB94 */
    /* 0x8D04 */ MATRIX m8D04_matrix800CCBB4;                    /* 0x800CCBB4 Noah: battleMatrix800CCBB4 */
    /* 0x8D24 */ sBattleVisualBuffer m8D24_visualBuffers[11];    /* 0x800CCBD4 Noah: battleVisualBuffers */
    /* 0x8DA8 */ u8 m8DA8_timeEnabled;                                       /* 0x800CCC58 Noah: battleTimeEnabled */
    /* 0x8DA9 */ u8 m8DA9[3];
    /* 0x8DAC */ s32 m8DAC_frameRate;                                      /* 0x800CCC5C Noah: battle800ccc5c */
    /* 0x8DB0 */ u8 m8DB0[0x8E38 - 0x8DB0];
    /* 0x8E38 */ sBattleEntity m8E38_entities[11];               /* 0x800CCCE8 Noah: battleEntities */
    /* 0x9E08 */ s32 m9E08_shownExperience[3][2];                                /* 0x800CDCB8 */
    /* 0x9E20 */ s32 m9E20_experienceToAdd[3][2];                                /* 0x800CDCD0 */
} BattleBss_800C3EB0;


/* Battle data block at 0x800CCCE8 (battleEntities first). Retail reaches it
 * both through the initialised pointer pBattleEntities (= &battleEntities) and
 * through plain symbols (damageDonePerAttack, initBattleAttackStatusArray0, ...). */
typedef struct {
    /* 0x00 */ u8 m0[0xC];
    /* 0x0C */ u8 mC_ammoCount;
    /* 0x0D */ u8 mD;
    /* 0x0E */ u8 mE_power;
    /* 0x0F */ u8 mF_id;
    /* 0x10 */ u8 m10_statusChance;
    /* 0x11 */ u8 m11_statusType;
    /* 0x12 */ u8 m12[2];
} sBattleData5750; /* 0x14 */

/* Gear fuel costs ... selection marker mode at 0x800D2C0A. Retail reads
 * the fuel costs relative to m2A (handleGearMenuSelectEnemy). */
typedef struct {
    /* 0x00 */ u16 m0_fuelCosts[15];    /* 0x800D2C0A: [row * 3 + numAP] */
    /* 0x1E */ u16 m1E;
    /* 0x20 */ u16 m20_attackPower;                 /* 0x800D2C2A */
    /* 0x22 */ u8 m22[4];
    /* 0x26 */ u16 m26_statusFlags;     /* 0x800D2C30 */
    /* 0x28 */ u16 m28_refuel;          /* 0x800D2C32: fuel regained on gear defend */
    /* 0x2A */ u8 m2A_mode;                  /* 0x800D2C34: Noah updateTargetSelectionMarkerVar0 */
    /* 0x2B */ u8 m2B;
    /* 0x2C */ u8 m2C;
    /* 0x2D */ u8 m2D;
    /* 0x2E */ u8 m2E;
    /* 0x2F */ u8 m2F;
    /* 0x30 */ u16 m30_infinityChance;                 /* 0x800D2C3A */
} BattleBss_800D2C0A;

/* Item records (16 bytes) at 0x800D2200 (BattleBss_800CCCE8 + 0x5518),
 * indexed by item id (applyBattleItemToEntity). sBattle800D2204 is this table seen
 * from + 4 (its mA is mE here). Billy's ammo uses the same records with
 * ammo id k = record k - 2 (loadBillyAmmo, isBillyAmmoLoaded) and reads bytes
 * 0xA-0xC one by one (union mA). */
typedef struct {
    /* 0x0 */ u8 m0[3];
    /* 0x3 */ u8 m3_ammoCount;          /* ammo: -> gameStateAmmoByWeaponItem[id] */
    /* 0x4 */ u16 m4_targetFlags;
    /* 0x6 */ u8 m6_id;                 /* ammo: compared with a gun slot's id (m4[k].m1[1]) */
    /* 0x7 */ u8 m7;
    /* 0x8 */ u8 m8_power;              /* amount / percentage / sub-type (flags == 1) */
    /* 0x9 */ u8 m9_duration;
    /* 0xA */ union {
        struct {
            /* 0x0 */ s16 mA_flags;     /* 1: special item, switch on m8 */
            /* 0x2 */ s16 mC_status;    /* status bits added / removed */
        } item;
        struct {
            /* 0x0 */ u8 mA_statusChance;            /* -> gun slot byte 2 (m4[k].m1[-3]) */
            /* 0x1 */ u8 mB_statusType;            /* -> gun slot byte 3 (m4[k].m1[-2]) */
            /* 0x2 */ u8 mC_power;            /* -> gun slot power (m4[k].m0) */
            /* 0x3 */ u8 mD;
        } ammo;
    } mA;
    /* 0xE */ u16 mE_attackType;        /* -> performAttack_type (applyBattleItemToTargets) */
} sBattleItemData;

/* The same bytes seen from 0x800D2C0C (BattleBss_800CCCE8 + 0x5F24), which
 * is where setupGearTurnState points: the fuel costs are 1-based, so the code
 * indexing them through D_800D2C0A (b_80080AE4) reads [n] = [n - 1] here.
 * Field k here is BattleBss_800D2C0A field k + 2. */
typedef struct {
    /* 0x00 */ u16 m0_fuelCosts[15];    /* mechaBattleStats[idx][i].m24_fuelCost */
    /* 0x1E */ u16 m1E_attackPower;                 /* 0x800D2C2A: gear m3C * m3F + weapon powers */
    /* 0x20 */ u8 m20[4];
    /* 0x24 */ u16 m24_statusFlags;     /* 0x800D2C30 */
    /* 0x26 */ u16 m26_refuel;          /* 0x800D2C32: fuel regained on gear defend */
    /* 0x28 */ u8 m28_mode;             /* 0x800D2C34: entity m148 (4: infinity) */
    /* 0x29 */ u8 m29_agility;                  /* gear m98 */
    /* 0x2A */ u8 m2A_defenceDown;                  /* gear m99 */
    /* 0x2B */ u8 m2B;
    /* 0x2C */ u8 m2C_boosterActive;                  /* gear m80 & 0x8000 */
    /* 0x2D */ u8 m2D;
    /* 0x2E */ u16 m2E_chance;          /* 0x800D2C3A: infinity chance (%) */
} BattleGearTurn; /* 0x30 */

/* .bss block at 0x800CCB04 (Noah: pCurrentBattleOT, battleOddOrEven): the
 * region-A files reach battleOddOrEven (+0x30) from pCurrentBattleOT's
 * address, so they see it as this aggregate (same bytes as
 * BattleBss_800C3EB0 from +0x8C54). */
typedef struct {
    /* 0x00 */ OTag* m0_pCurrentBattleOT;                        /* 0x800CCB04 */
    /* 0x04 */ BattlePadState m4_pad;                            /* 0x800CCB08 */
    /* 0x30 */ s32 m30_battleOddOrEven;                          /* 0x800CCB34 */
    /* 0x34 */ u8 m34[0x6108 - 0x34];
    /* 0x6108 */ BattleGearTurn m6108_gearTurn;                  /* 0x800D2C0C */
} BattleBss_800CCB04;

typedef struct {
    /* 0x0000 */ sBattleEntity m0_entities[11];                          /* 0x800CCCE8 */
    /* 0x0FD0 */ s32 mFD0_savedM3C[3][2];                                /* 0x800CDCB8: m3C/m40 of each PC at battle start */
    /* 0x0FE8 */ s32 mFE8_experienceToAdd[3][2];                                          /* 0x800CDCD0 */
    /* 0x1000 */ u16 m1000_savedMaxHpMp[3][2];                            /* 0x800CDCE8: MaxHP/MaxMP at battle start */
    /* 0x100C */ u8 m100C_itemDropCategories[8];                        /* 0x800CDCF4: items won (battle_result): inventory 0-4 */
    /* 0x1014 */ u8 m1014_itemDropIds[8];                               /* 0x800CDCFC: 0 = none */
    /* 0x101C */ u8 m101C_newDeathblow[3];                             /* per party slot, 0: none (battle_result) */
    /* 0x101F */ u8 m101F_newAbility[3];
    /* 0x1022 */ u8 m1022_levelsGained[3][2];                          /* [slot]: level, ether level (battle_result) */
    /* 0x1028 */ u8 m1028_savedStats[3][8];                               /* 0x800CDD10: attack, hit, defence, evade, ether, ether defence, agility at battle start */
    /* 0x1040 */ u8 m1040_newStats[3][8];                               /* 0x800CDD28: m1028_savedStats after level-up (battle_result) */
    /* 0x1058 */ sBattle800CDD40Sub m1058_partyBattleStats[3][38];       /* 0x800CDD40 */
    /* 0x2228 */ sBattle800CDD40Sub m2228_mechaBattleStats[3][42];       /* 0x800CEF10 */
    /* 0x35D8 */ sBattle800CDD40Sub m35D8_enemiesBattleStats[200];       /* 0x800D02C0 */
    /* 0x5518 */ sBattleItemData m5518_items[35];                        /* 0x800D2200 */
    /* 0x5748 */ u8 m5748[0x5750 - 0x5748];
    /* 0x5750 */ sBattleData5750 m5750_gearItems[50];                              /* 0x800D2438 */
    /* 0x5B38 */ sBattleData5750 m5B38_gearAmmoItems[50];                              /* 0x800D2820 */
    /* 0x5F20 */ void* m5F20_levelUpData;                               /* 0x800D2C08: battle_result reads it as a pointer */
    /* 0x5F24 */ BattleGearTurn m5F24_gearTurn;                          /* 0x800D2C0C */
    /* 0x5F54 */ s32 m5F54_damageTaken[3];
    /* 0x5F60 */ s32 m5F60_gearDamageTaken[3];
    /* 0x5F6C */ s32 m5F6C_damageDonePerAttack[11];                      /* 0x800D2C54 */
    /* 0x5F98 */ u8 m5F98[4];
    /* 0x5F9C */ u32 m5F9C_experience;                                   /* 0x800D2C84: experience won (battle_result) */
    /* 0x5FA0 */ u8 m5FA0_initBattleAttackStatusArray0[11];             /* 0x800D2C88 */
    /* 0x5FAB */ u8 m5FAB;
    /* 0x5FAC */ u16 m5FAC_entitiesHitInCurrentAttackBF;                /* 0x800D2C94 */
    /* 0x5FAE */ u16 m5FAE_targetableMask;
    /* 0x5FB0 */ u16 m5FB0_performAttack_type;                          /* 0x800D2C98 */
    /* 0x5FB2 */ u8 m5FB2[2];
    /* 0x5FB4 */ u16 m5FB4_defeatedEnemies;                             /* 0x800D2C9C: bit i: enemy entity 3 + i */
    /* 0x5FB6 */ u16 m5FB6_battleEffectBF;                              /* 0x800D2C9E */
    /* 0x5FB8 */ u8 m5FB8[0x5FBC - 0x5FB8];
    /* 0x5FBC */ u8 m5FBC;
    /* 0x5FBD */ u8 m5FBD;
    /* 0x5FBE */ u8 m5FBE_lastAttackElements;
    /* 0x5FBF */ u8 m5FBF;
    /* 0x5FC0 */ u8 m5FC0_lastAttackIndex;
    /* 0x5FC1 */ u8 m5FC1_performAttackSub3_attacker;                   /* 0x800D2CA9 */
    /* 0x5FC2 */ u8 m5FC2_battleGetSlotStatusSub_current28Index;        /* 0x800D2CAA */
    /* 0x5FC3 */ u8 m5FC3;
    /* 0x5FC4 */ s8 m5FC4_expPenalty;                                    /* 0x800D2CAC: experience -= experience / 4 * n (battle_result) */
    /* 0x5FC5 */ u8 m5FC5[2];
    /* 0x5FC7 */ u8 m5FC7_battleTickMain_var1;                          /* 0x800D2CAF */
    /* 0x5FC8 */ u8 m5FC8[0x6028 - 0x5FC8];
    /* 0x6028 */ u8 m6028_terrainAgility[4];                         /* 0x800D2D10: per gear slot, from the arena (battle_loader); taken off m98 at the end */
    /* 0x602C */ u8 m602C[0x603C - 0x602C];
    /* 0x603C */ u8 m603C_battleCharacters[4];                          /* 0x800D2D24 = battleCharacters (Noah: battleCharacters) */
} BattleBss_800CCCE8;

/* .bss block at 0x800D2CA4: retail derives &battleTickMain_var3[4] from
 * &battleTickMain_var1 (0x800D2CAF - 7), so they are one aggregate. */
typedef struct {
    /* 0x0 */ u8 m0_battleTickMain_var3[5];
    /* 0x5 */ u8 m5[6];
    /* 0xB */ u8 mB_battleTickMain_var1; /* D_800D2CAF: attack name index */
} BattleBss_800D2CA4;

/* .bss block at 0x800C48E8: retail keeps &battleRunningVar1 in a register
 * where it is read and written in one block (struct-member behaviour). */
typedef struct {
    /* 0x0 */ u16 m0_performAttackSub3_var0;
    /* 0x2 */ u8 m2_battleRunningVar1;
} BattleBss_800C48E8;

/* Battle turn-order block at 0x800D2DCC. Retail derives numTicksBeforeReady's
 * address from currentEntryInRandomTurnOrder's (computeFacingForJump: addiu
 * t3,v0,0x2F). */
typedef struct {
    /* 0x00 */ u8 m0_isBattleSlotFilled[11];        /* 0x800D2DCC */
    /* 0x0B */ u8 mB_currentEntryInRandomTurnOrder; /* 0x800D2DD7 */
    /* 0x0C */ u8 mC_randomTurnOrder[11];           /* 0x800D2DD8 */
    /* 0x17 */ u8 m17;
    /* 0x18 */ u8 m18_isEntityReadyForBattle[11];   /* 0x800D2DE4 */
    /* 0x23 */ u8 m23;
    /* 0x24 */ s16 m24_battleSlotStatusVar0[11];    /* 0x800D2DF0 */
    /* 0x3A */ s16 m3A_numTicksBeforeReady[11];     /* 0x800D2E06 */
    /* 0x50 */ s16 m50_battleSlotHalfSpeedToggle[11]; /* 0x800D2E1C */
} BattleBss_800D2DCC;

/* Battle camera block at 0x800D309C. Retail reaches the rendering matrix's
 * translation from the block's base (renderBattleScene: addiu a0,s0,-0x20 then
 * 0x34(a0)), and updateBattleCamera addresses the eye/at targets and orientation
 * from one base. */
typedef struct {
    /* 0x00 */ s32 m0;                          /* 0x800D309C */
    /* 0x04 */ SVECTOR m4_eyeTarget;            /* 0x800D30A0 Noah: battleCameraEyeTarget */
    /* 0x0C */ SVECTOR mC_atTarget;             /* 0x800D30A8 Noah: battleCameraAtTarget */
    /* 0x14 */ SVECTOR m14_orientation;         /* 0x800D30B0 Noah: battleCameraSpriteOrientation */
    /* 0x1C */ s32 m1C_distance;                /* 0x800D30B8 Noah: battleCameraSpriteDistance */
    /* 0x20 */ MATRIX m20_renderingMatrix;      /* 0x800D30BC Noah: battleRenderingMatrix */
    /* 0x40 */ s32 m40_drawTiming0;             /* 0x800D30DC Noah: battleDrawTimming0 */
    /* 0x44 */ s32 m44_drawTiming1;             /* 0x800D30E0 Noah: battleDrawTimming1 */
    /* 0x48 */ s32 m48_startOfBattleTime;       /* 0x800D30E4 Noah: startOfBattleTime */
} BattleCamera_800D309C;

/* ---- globals every file sees the same way ---- */

/* The dev-kit flag word at the start of main's data (runningOnDTL; -1 on a
 * retail console), which the battle reads through its constant address. */
extern s32 runningOnDTL;
#define BATTLE_DTL_WORD runningOnDTL


extern sBattleVar0* battleVar0;
extern sBattleVar2* battleVar2;
extern sBattleVar1* battleVar1;
extern sBattle800c3e24* battleG3;
extern sBattleVar48 battleCurrentDamages[32] PORT_ALIAS;
extern sApStruct apConfigArray[11];
extern sUnkMonsterStatus unknownMonsterStatus0[8];
extern sBattleScriptEntity monstersScriptsEntities[8];
extern sBattleDialogWindow* battleDialogWindows[7];
extern sBattleDialogWindow2* battleDialogWindows2[7];
extern sBattleRenderStruct2* pBattleRenderStruct2;
extern sMonsterTurnDisplay previousEntityTurn;
extern u8 battleCharacters[4];
extern u16 allPlayerCharacterBitmask;
extern BattleCamera_800D309C battleCameraBlock;
extern BattleBss_800C3EB0 battleBssBlock;
extern BattleBss_800CCB04 D_800CCB04;          /* the same bytes as battleBssBlock + 0x8C54 */
extern SVECTOR* battleTerrainVertices;
extern s32 battleTerrainTriangleCount;
extern sBattleTerrainTriangle* battleTerrainTriangles;
extern u8 battleTerrainSearchGeneration;
extern SVECTOR battleSceneRenderingMatrixOffset;                           /* screen shake offset (BattleShake) */
extern u8 previousNewBattleInputButton[2];           /* Noah: previousNewBattleInputButton, newBattleInputButton2 */
extern SoundInstance* pCurrentBattleMusic;
extern sBattleTextImage itemLabelsIds[10];           /* digit glyph images (retail indexes them as structs) */
extern sBattleHorizon* battle800c3ea0;
extern BattleBss_800C48E8 performAttackSub3_var0;    /* Noah: performAttackSub3_var0, battleRunningVar1 (0x800C48EA) */
extern SeqFile* gCurrentBattleEventSeq;              /* the battle event's sequence (sound source 3) */
extern BattleBss_800CCCE8 battleEntities;            /* Noah: battleEntities, ... */
extern void* battleSpriteBundle;                             /* the battle sprite bundle (battle_loader) */
extern BattleBss_800D2DCC isBattleSlotFilled;        /* Noah: isBattleSlotFilled, randomTurnOrder, ... */
extern BattleSlotTicks battleSlotStatusVar0;
extern sBattleScriptContext battleScriptContext[32];
extern sBattleResultChar* battleResultCharWindows[3];
extern sLoadedMechas* battleMechas[32];
extern u16 currentJumpAnimationBitMask;

/* ---- functions called across battle files (the definitions' prototypes) ---- */
u8 abilitySelectTarget(u16 flags, u8 self, u8 useSelfTargets);
u8 accessBattleEntityByteStat(u8 entity, u8 fieldId, u8 value, u8 read);
u16 accessBattleEntityWordStat(u8 entity, u8 fieldId, u16 value, u8 read);
void allocateAndSetupBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h, u8 deferred, u8 render);
void applyPassiveModifiersAtEndOfTurn(s32 arg0);
void battleCreateFader(s32 duration, s32 abe, u8 r, u8 g, u8 b);
void battleSoundEffect2(u8 id);
void callGearSwapSprite(s32 arg0);
u16 characterIdToTargetBitmask(u8 a);
void chooseAutoBattleCommand(u8 idx, u8* out, s16* arg2);
void clearBattleDialogWindow(u8 arg0);
void copyCustomPolySubBuffer(sCustomPolySubBuffer* buf, u8* dst, s32 semiTrans, u8 shadeTex);
s32 countBattleActorCoresActiveForBitmask(u16 mask, SpriteActorCore** list, SpriteActorCore* target);
void drawBattleMenuEscape(s32 arg0);
void drawCircleMenuAttackActive(s32 arg0);
void drawCircleMenuCallGear(s32 arg0);
void drawCircleMenuChi(s32 arg0);
void drawCircleMenuCombo(s32 arg0);
void drawCircleMenuDefend(s32 arg0);
void drawCircleMenuItemActive(s32 arg0);
void drawCircleMenuItemSecondaryActive(s32 arg0);
void drawGearBattleMenuEscape(s32 arg0);
void drawGearCircleMenuAttackActive(s32 arg0);
void drawGearCircleMenuBooster(s32 arg0);
void drawGearCircleMenuCharge(s32 arg0);
void drawGearCircleMenuChi(s32 arg0);
void drawGearCircleMenuItemActive(s32 arg0);
void drawGearCircleMenuItemSecondaryActive(s32 arg0);
void drawGearCircleMenuSpecial(s32 arg0);
void executeMonsterScript(u8 entity, s32 unused);
void finalizeBattleMechaLoadingCommands(s32 index, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY);
s32 getDirectionBetween2BattleEntities(u8 a, u8 b);
u16 getRandomValueInRange(s32 arg0, u16 arg1);
void hideMonsterAttackName(u8 entity);
void loadBattleCharacterPortraits(u8* data, u8 glyph);
void loadGearCommandText(u8 entity);
void mechaPlayAnimation_battle(u16 index, u16 arg1, s32 animationId);
void performAttackSub3(u8 arg0, u16 arg1, u16 arg2);
void printIntegerToStringBattle(u32 value);
void processBattleAnimation_subType_minus6(u8 entity);
void removeBillyWindow(u8 entity, u8 arg1);
void setCameraVisibleEntities(s32 arg0);
void setupGearHudTexts(u8 entity);
s16 spriteActorCoreRatan2(SpriteActorCore* a, SpriteActorCore* b);
void startBattleLoader(s32 arg0);
void startGearInfinityMode(u8 idx);
void startJumpAnimation(s32 isBilly, s32 actor, s32 target, s32 facing);
void stopBattleVibration(u8 entity, s32 mask);
s32 tryToEscape(u8 entity);
void updateBattleEventOverlay(u8 arg0);
s32 updateBattleMenuSpriteForMenu(u8 entity, u8 set, u8 dimOnly);
void updateCharacterBlinkingTask(u16 arg0);

/* ---- PS1 views ----
 * Retail code in some files reads these globals through another type (a
 * plain halfword, its low byte, a one-field struct) or calls these functions
 * through other prototypes (unmasked arguments or return values, missing
 * arguments); it matches only that way, so on the PS1 those files declare
 * their own view and the PC sees the true declarations here. */
extern sBattleItemList battleEffectBF;
extern s32 battleOddOrEven;
sBattleSpriteActor* allocateBattleSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5, s16 x, s16 y, s16 z, s16 animationId, s16 direction, s32 arg11, s32 arg12, s32 vramParam);
void applyBattleItemToTargets(u8 arg0);
u8 battleGetSlotStatusSub(u8 entity, s32 unused);
void displayPassiveModifiers(s32 entity, s32 damage, s32 mpDamage, s32 fuelDamage);
s32 executeMonsterScriptWhenAttacked(u8 entity);
u8 getAbilityHitChance(u8 entity, u8 command);
u8 getEntityStatusAnimationIndex(u8 idx);
u16 getEntityStatusEffectBitmask(u8 idx);
u8 getGearAnimStatusBits(u8 idx);
u8 hasPassiveModifierEndOfTurn(u8 idx, s32* out);
u8 initFxFragments(void);
void initMechaCloth_battle(sLoadedMechas_114* obj, u16* data, s32 gravity, s32 scale, s16 x, s16 y, s16 z, s32 numColliders, s16 tpx, s16 tpy, s16 uSpan, s16 vSpan, s16 clutX, s16 clutY, u8 frontR, u8 frontG, u8 frontB, u8 backR, u8 backG, u8 backB);
s32 isBillyAmmoLoaded(u8 idx);
void loadBillyAmmo(u8 id, u8 slot);
void performItemOnTargets(s32 arg0, s16 arg1, u16 arg2);
void restoreBattleEntityHpAndStatus(s32 arg0);
void setBattleMechaIdleAnimation(s32 index, u8 arg1);

/* The battle's gear files (port/src/game/battle/convert.c), converted with
 * field_mecha's pcConvertMechaData1/2. The PS1 relocates a gear file in place
 * once and later mechas built from the same bytes (mechaInitEnvironmentMechaMesh
 * flags 1 and 4) read it as relocated: pcBattleMechaData2 converts a file the
 * first time (fresh: always) and gives the same tables to later users;
 * pcBattleFreeMechaData2 frees a mecha's own tables and file. */
sMechaDataTable2* pcBattleMechaData2(void* raw, s32 fresh);
void pcBattleFreeMechaData2(sMechaDataTable2* table);

#endif /* BATTLE_BATTLE_H */
