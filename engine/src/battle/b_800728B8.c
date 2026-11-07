/* Provisional file (split where JP links the code in another order). */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "battle/battle.h"
#include "pc_wide.h"
#define AddPrim pcTextureHudPrim


/* Globals (splat names; Noah names in comments) */
extern u8 needBattleEventOverlay;                /* battle event overlay loaded (Noah: needBattleEventOverlay) */
extern s32* pRunningOnDTL;
extern u8 battleDebugDisplay;
void battleEvent_update(void);
void drawBattleDebugTextPages(void);
void battleRender(void);
void drawBattleDialogWindows(void);
void battleResultDraw(void);
void renderBattleResultText(void);
s32 battleRenderDebugAndMain(void);
void updatePortraits(void);
void render_BA8_27C8_1E68(void);
void drawMonsterNames(void);
void drawDialogPortrait(void);
void battleDrawAPBar(void);
void battleDrawOverallUI(void);
void updatePolyArray3A88(void);
void renderGearHP(void);
void battleRenderPortraitSelection(void);
void battleRenderPlayerPortraits(void);
void battleRenderCommandRing(void);
void renderDeathBlowFlashPolys(void);
void renderOpenMenu(void);
void renderTargetSelectionCursor(void);
extern void* battleEventHeapMarker;
extern void* battleEventOverlayHeap;
extern u8 isEntityReadyForBattle[11];
extern BattleBss_800D2CA4 D_800D2CA4; /* battleTickMain_var3 / battleTickMain_var1 (0x800D2CAF) */
extern u16 entitiesHitInCurrentAttackBF;
extern u8* battleAttackNameStrings;
extern u8 battleInputButton;
extern u16 bBattleTickMode0;
extern u8 currentBattleMode;
extern u8 drawBattleMode1Disabled;
extern DialogWindow18* battleEventVar1;
void setCurrentDir_20_0(void);
void* allocateBattleHeap(s32, s32);
void idleBattleDuringLoading(void);
void battleEventEntry(void);
s32 battleEventFree(void);
u16 bitmaskCharacterCheck(u16 mask, u8 index);
void battleTickMain(u8);
void battleRenderPolyArray(POLY_FT4* p, s32 count, s32 oddOrEven);
void drawBattleMode1(void);
void drawBattleMode0(void);
void drawBattleMode2(void);
void setupPolyArray3A88_HP(s32);
s32 setupPolyArray3A88_HP2(s32, u8);
void tintHP(s32, u8);
void tintHpGear(s32, u8);
void repositionTextRenderingPlanePrim(POLY_FT4* p, u16 x, s16 y, u8 u, u8 v, u8 width);
extern u8 battlePartyLayoutType;
extern u16 partyMemberSpritesOffset[3 * 3];        /* Noah: partyMemberSpritesOffset[layout][pc]; retail indexes it flat */
extern u16 HP_positionTable[3 * 24];         /* Noah: HP_positionTable (per PC 24 entries: 7 HP, then gear at 7..); retail indexes it flat, idx * 24 + i */
extern u8 currentEntityHpForDisplayBuffer[3];
extern u8 currentEntityGearMaxHpForDisplayBuffer[5];
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4* polys, s16 x, s16 y);
void tintFT4(POLY_FT4* poly, s32 from, s32 to, u8 color);
extern u8 makeBattleTimeProgress;
extern u8 printIntegerToStringBattleBuffer[9];
extern void* battleFont;
s32 battleSetupStringInPolyFTRot(); /* main (font.c); battle calls it unprototyped: u16 args are passed zero-extended */
/* s32 battleSetupStringInPolyFTRot(void* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, s16 scaleX,
                                 s16 scaleY, s16 rot); */
s32 battleSetupStringInPolyFT4Small(s32 character, POLY_FT4* polys, s16 x, s16 y);
void battleSetupTextPoly(POLY_FT4* p);
void timerBarInitStartOfTurnAnimation(s32 idx);
void timerBarUpdateStartOfTurnAnimation(s32 idx);
extern u16 jumpAnimationActiveActorBF;
u16 characterOdToInverseTargetBitmask(u8 index);
extern s16 currentEntityMaxHpCopy;               /* max HP (copy) */
extern s16 currentEntityMaxHpForDisplay;
extern s16 currentEntityHpCopy;               /* HP (copy) */
extern s16 currentEntityHpForDisplay;
extern s32 currentEntityGearHpCopy;               /* gear HP (copy) */
extern s32 currentEntityGearMaxHpForDisplay;               /* Noah: currentEntityGearMaxHpForDisplay (gear HP digits) */
extern s32 currentEntityGearHpForDisplay;               /* gear max HP */
extern u8 currentEntityMaxHpForDisplayBuffer[3];
void drawTargetAndElementIcons(void);
void drawDescriptionText(void);
void renderOpenMenuSub0(void);
extern u8 bBattleTickMode1;
extern u8 currentEntryInRandomTurnOrder;
extern u8 randomTurnOrder[11];
extern u8 battleTickMain_var0;
extern u8 battleTickMain_var2;
extern u8* currentBattleSpecificStrings;
extern u8 battleMonsterMapping[11];
extern u16 battleNonDefendTurnCount;
void battleTickMainSub0(void);
void updateBattleEntityStatus(u8);
void loadImageSync(RECT*, void*);
void monsterJumpToTarget(u8);
void setupTurnRenderLoop(u8);
void performAutoPartyMemberTurn(u8);
void execMonsterScriptEndOfPlayerTurn(void);
s8 getEntityToFace(u8);
s32 computeFacingForJump(u8);
void clearAnimationsEndOfTurn(s32);
void updateRunningAnimations(void);
void checkWinConditions(void);
void resetBattleVar2_m2EB(void);
void copyBattleVar1_m7C(void);
void removeCurrentMonsterNameString(void);
void loadMonsterAttackName(void);
void displayAndWaitMonsterAttackName(void);
void applyBattleEffectBF7(void);
void updateTicksEndOfTurn(void);

/* battleMain (battleMain) */
extern u8 battleInitVar0;
extern u8 requestedBattleConfig;
extern u8 victoryFanfarePlayed;
extern u8 battleInitVar1;
extern u8 newBattleInputButton2;
extern u8 startCharacterJumpToEnemyVar0;
extern u8 currentBattleConfig[0x20];          /* Noah: currentBattleConfig (m1_flags at +1) */
extern u8 battleConfigs[][0x20];
extern u8 battleTransitionEffect;
extern void* battleConfigFile3;
extern u8 battleMusic[];
extern void* battleLoadDataVar2;
extern void* battleMechaInitData;             /* Noah: battleLoadDataVar2Bis */
extern u8 battleTimeEnabled;
extern void* battleStartSeq;
extern void* battleLoaderProbeBlock;
extern void* battleLoaderMemoryBlock;
extern u8 isBattleAnEvent;
extern s16 battleStartUnlockedAbilities[3][2];
extern u8 battleEventEndBattleFlag;
extern u8 battleEventMenuEnabled;
extern u8 menuReturnState0;
extern u8 battleEventIsRunningFlag;
extern void* battleResultHeapMarker;
extern void* battleResultOverlayHeap;
void battleDebugSelector(void);
void initBattleGraphics(void*);
void* realStartBattleMusic(void*, s32, s32);
void init8920(void);
void setupStringsForMonsterNames(void);
void stopSequence(void*);
void unloadSequence(void*);
void battleStartPartyFixups(void);
void battleTickGameplay(void);
void loadWdsDataIfNeeded(void);
void clearBattleAnimationData(void);
void playBattleEndAndReleaseEnemies(s32);
void finishBattle(void);
void loadBattleEventOverlay(void);
void unloadBattleEventOverlay(void);
/* (battleOddOrEven = D_800CCB04.m30_battleOddOrEven) */
void updateApFuelPolyBar(void);

void battleRenderPolyArray(POLY_FT4* p, s32 count, s32 oddOrEven) {
    s32 i;

    for (i = 0; i < count; i++) {
        pcTextureClassDefault(&p[i * 2 + oddOrEven], PC_TEX_TEXT);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &p[i * 2 + oddOrEven]);
    }
}

void tintFT4(POLY_FT4* poly, s32 from, s32 to, u8 color) {
    s32 i;

    for (i = from; i < to; i++) {
        SetShadeTex(&poly[i * 2 + D_800CCB04.m30_battleOddOrEven], 0);
        if (color != 1) {
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].r0 = 0x80;
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].g0 = 0;
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].b0 = 0;
        } else {
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].r0 = 0x80;
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].g0 = 0x80;
            poly[i * 2 + D_800CCB04.m30_battleOddOrEven].b0 = 0;
        }
    }
}

void tintHP(s32 idx, u8 color) {
    s32 i;
    s32 j;
    s32 start = battleVar1->mE0_hpDigitCounts[idx];

    for (i = 0; i < 3; i++) {
        if (currentEntityHpForDisplayBuffer[i] != 0xFF) {
            battleVar1->mE0_hpDigitCounts[idx] += battleSetupStringInPolyFT4Large(currentEntityHpForDisplayBuffer[i] + 0x67, battleVar0->m3A88_hpDigits[idx][battleVar1->mE0_hpDigitCounts[idx]],
                                                  HP_positionTable[idx * 24 + i] + partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx], 0x10);
        }
    }
    battleVar1->mE0_hpDigitCounts[idx] += battleSetupStringInPolyFT4Large(0x71, battleVar0->m3A88_hpDigits[idx][battleVar1->mE0_hpDigitCounts[idx]],
                                          HP_positionTable[idx * 24 + 3] + partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx], 0x10);
    i = 4;
    j = 4;
    for (; i < 7; i++) {
        if (currentEntityMaxHpForDisplayBuffer[i - 4] != 0xFF) {
            battleVar1->mE0_hpDigitCounts[idx] += battleSetupStringInPolyFT4Large(currentEntityMaxHpForDisplayBuffer[i - 4] + 0x67, battleVar0->m3A88_hpDigits[idx][battleVar1->mE0_hpDigitCounts[idx]],
                                                  HP_positionTable[idx * 24 + j] + partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx], 0x10);
            j++;
        }
    }
    if (color) {
        tintFT4(battleVar0->m3A88_hpDigits[idx][0], start, battleVar1->mE0_hpDigitCounts[idx], color);
    }
}

void tintHpGear(s32 idx, u8 color) {
    s32 i;
    s32 start = battleVar1->mE0_hpDigitCounts[idx];

    for (i = 0; i < 5; i++) {
        if (currentEntityGearMaxHpForDisplayBuffer[i] != 0xFF) {
            battleVar1->mE0_hpDigitCounts[idx] += battleSetupStringInPolyFT4Large(currentEntityGearMaxHpForDisplayBuffer[i] + 0x67, battleVar0->m3A88_hpDigits[idx][battleVar1->mE0_hpDigitCounts[idx]],
                                                  HP_positionTable[idx * 24 + i + 7] + partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx], 0x10);
        }
    }
    if (color) {
        tintFT4(battleVar0->m3A88_hpDigits[idx][0], start, battleVar1->mE0_hpDigitCounts[idx], color);
    }
}

s32 setupPolyArray3A88_HP2(s32 idx, u8 isGear) {
    s32 ret = 0;
    s32 k;
    s32 b;

    currentEntityMaxHpForDisplay = currentEntityMaxHpCopy = battleEntities.m0_entities[idx].m0_base.m4E_MaxHP;
    currentEntityHpForDisplay = currentEntityHpCopy = battleEntities.m0_entities[idx].m0_base.m4C_HP;
    currentEntityGearMaxHpForDisplay = currentEntityGearHpCopy = battleEntities.m0_entities[idx].mA4_gear.m60_hp;
    currentEntityGearHpForDisplay = battleEntities.m0_entities[idx].mA4_gear.m64_maxHp;
    if (isGear) {
        if (!(currentEntityGearHpForDisplay / 8 < currentEntityGearMaxHpForDisplay)) {
            goto two;
        }
        b = currentEntityGearHpForDisplay / 4 < currentEntityGearMaxHpForDisplay;
        goto check;
    }
    if (!(currentEntityMaxHpForDisplay / 8 < currentEntityHpForDisplay)) {
    two:
        ret = 2;
    } else {
        b = currentEntityMaxHpForDisplay / 4 < currentEntityHpForDisplay;
    check:
        if (!b) {
            ret = 1;
        }
    }
    currentEntityHpForDisplayBuffer[0] = currentEntityHpForDisplay / 100;
    currentEntityHpForDisplay -= currentEntityHpForDisplay / 100 * 100;
    currentEntityHpForDisplayBuffer[1] = currentEntityHpForDisplay / 10;
    currentEntityHpForDisplay -= currentEntityHpForDisplay / 10 * 10;
    currentEntityHpForDisplayBuffer[2] = currentEntityHpForDisplay;
    currentEntityMaxHpForDisplayBuffer[0] = currentEntityMaxHpForDisplay / 100;
    currentEntityMaxHpForDisplay -= currentEntityMaxHpForDisplay / 100 * 100;
    currentEntityMaxHpForDisplayBuffer[1] = currentEntityMaxHpForDisplay / 10;
    currentEntityMaxHpForDisplay -= currentEntityMaxHpForDisplay / 10 * 10;
    currentEntityMaxHpForDisplayBuffer[2] = currentEntityMaxHpForDisplay;
    currentEntityGearMaxHpForDisplayBuffer[0] = currentEntityGearMaxHpForDisplay / 10000;
    currentEntityGearMaxHpForDisplay -= currentEntityGearMaxHpForDisplay / 10000 * 10000;
    currentEntityGearMaxHpForDisplayBuffer[1] = currentEntityGearMaxHpForDisplay / 1000;
    currentEntityGearMaxHpForDisplay -= currentEntityGearMaxHpForDisplay / 1000 * 1000;
    currentEntityGearMaxHpForDisplayBuffer[2] = currentEntityGearMaxHpForDisplay / 100;
    currentEntityGearMaxHpForDisplay -= currentEntityGearMaxHpForDisplay / 100 * 100;
    currentEntityGearMaxHpForDisplayBuffer[3] = currentEntityGearMaxHpForDisplay / 10;
    currentEntityGearMaxHpForDisplay -= currentEntityGearMaxHpForDisplay / 10 * 10;
    currentEntityGearMaxHpForDisplayBuffer[4] = currentEntityGearMaxHpForDisplay;
    for (k = 0; k < 2; k++) {
        if (currentEntityHpForDisplayBuffer[k]) {
            break;
        }
        currentEntityHpForDisplayBuffer[k] = 0xFF;
    }
    for (k = 0; k < 2; k++) {
        if (currentEntityMaxHpForDisplayBuffer[k]) {
            break;
        }
        currentEntityMaxHpForDisplayBuffer[k] = 0xFF;
    }
    for (k = 0; k < 4; k++) {
        if (currentEntityGearMaxHpForDisplayBuffer[k]) {
            break;
        }
        currentEntityGearMaxHpForDisplayBuffer[k] = 0xFF;
    }
    return ret;
}

void setupPolyArray3A88_HP(s32 idx) {
    s16 value;
    s32 i;

    if (battleVar0->m835C_apFuelLabels[idx].m1E1_barMode == 1) {
        value = apConfigArray[idx].m0_storedAP;
    } else {
        value = battleEntities.m0_entities[idx].mA4_gear.m38_fuel;
    }
    printIntegerToStringBattle(value);
    for (i = 0; i < 4; i++) {
        if (printIntegerToStringBattleBuffer[i + 5] != 0xFF) {
            battleVar1->mEC_apFuelDigitCounts[idx] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 5] + 0x83, battleVar0->m6008_apFuelDigits[idx][battleVar1->mEC_apFuelDigitCounts[idx]],
                                                  idx * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx] + 0x4A) + i * 6, 0x25);
        }
    }
    battleVar1->m99_oddOrEven[idx] = D_800CCB04.m30_battleOddOrEven;
}

void updatePortraits(void) {
    s32 i;
    s32 j;
    s32 rot = 0; /* a variable (not REG_EQUIV) keeps 0xC00 in s0 across the two calls, as in retail */

    battleVar1->m74_timeBarsLengths[3] = 0;
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            switch (battleVar1->m90_perPCTimeBarStatus[i]) {
            case 0:
                battleVar1->m74_timeBarsLengths[i] = 0;
                battleVar1->m74_timeBarsLengths[i] += battleSetupStringInPolyFT4Large(
                    0x8F, battleVar0->m2E08_timeBars[i][battleVar1->m74_timeBarsLengths[i]],
                    i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x44), 0x1F);
                battleVar1->m74_timeBarsLengths[i] += battleSetupStringInPolyFT4Small(
                    0x52, battleVar0->m2E08_timeBars[i][battleVar1->m74_timeBarsLengths[i]],
                    i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x48), 0x1C);
                j = battleVar1->m74_timeBarsLengths[i] * 2;
                battleVar1->m74_timeBarsLengths[i] += battleSetupStringInPolyFT4Small(
                    0x53, battleVar0->m2E08_timeBars[i][battleVar1->m74_timeBarsLengths[i]],
                    i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x48), 0x1C);
                for (; j < battleVar1->m74_timeBarsLengths[i] * 2; j += 2) {
                    battleSetupTextPoly(&battleVar0->m2E08_timeBars[i][0][j + D_800CCB04.m30_battleOddOrEven]);
                }
                battleVar1->m84_timeBarsOddOrEven[i] = D_800CCB04.m30_battleOddOrEven;
                battleVar1->mCC_drawApFuel[i] = 1;
                break;
            case 1:
                rot = 0xC00;
                battleVar1->m74_timeBarsLengths[3] = battleSetupStringInPolyFTRot(
                    battleFont, 0x52, battleVar0->m2E08_timeBars[3], D_800CCB04.m30_battleOddOrEven,
                    0x10, 0x98, 0x1000, 0x1000, rot);
                j = battleVar1->m74_timeBarsLengths[3] * 2;
                battleVar1->m74_timeBarsLengths[3] += battleSetupStringInPolyFTRot(
                    battleFont, 0x53, &battleVar0->m2E08_timeBars[3][battleVar1->m74_timeBarsLengths[3]],
                    D_800CCB04.m30_battleOddOrEven, 0x10, 0x98, 0x1000, 0x1000, rot);
                for (; j < battleVar1->m74_timeBarsLengths[3] * 2; j += 2) {
                    battleSetupTextPoly(&battleVar0->m2E08_timeBars[3][0][j + D_800CCB04.m30_battleOddOrEven]);
                }
                battleVar1->m84_timeBarsOddOrEven[3] = D_800CCB04.m30_battleOddOrEven;
                break;
            case 3:
                if (apConfigArray[i].m1_gearState != 0) {
                    if (battleCharacters[i] != 7) {
                        battleVar1->mCC_drawApFuel[i] = 0;
                    } else {
                        battleVar1->m74_timeBarsLengths[i] = 0;
                    }
                } else {
                    battleVar1->m74_timeBarsLengths[i] = 0;
                }
                timerBarInitStartOfTurnAnimation(i);
            case 2:
                timerBarUpdateStartOfTurnAnimation(i);
                if (apConfigArray[i].m1_gearState == 0 || battleCharacters[i] == 7) {
                    battleVar1->m7F_drawTimeBarFill[i] = 0;
                }
                break;
            }
        }
    }
}

/*
 * The wide picture (include/pc_wide.h): the party's status boxes spread to
 * its edges, the first at the left edge and the last at the right (one box
 * stays in the middle). Only the wide picture moves; the 4:3 one is as on
 * the PS1. The example of anchoring 2D to the wide edges.
 */
static s32 widePartyBoxDx(s32 i) {
    s32 n = 0;
    s32 k;

    for (k = 0; k < 3; k++) {
        n += battleVisualEntities[k].m2_characterId != 0x7F;
    }
    return n > 1 ? (2 * i - (n - 1)) * pcWideView / (n - 1) : 0;
}

static void widePolyArray(POLY_FT4* p, s32 count, s32 oddOrEven, s32 dx) {
    s32 i;

    for (i = 0; i < count; i++) {
        pcWidePrim(&p[i * 2 + oddOrEven], dx, 0);
    }
}

static void wideAnchorPartyBoxes(void) {
    s32 i, dx;

    for (i = 0; i < 3; i++) {
        dx = widePartyBoxDx(i);
        widePolyArray(battleVar0->m2E08_timeBars[i][0], battleVar1->m74_timeBarsLengths[i],
                      battleVar1->m84_timeBarsOddOrEven[i], dx);
        widePolyArray(battleVar0->m835C_apFuelLabels[i].m0_apLabel[0], battleVar0->m835C_apFuelLabels[i].m1E2_apLabelLength,
                      battleVar0->m835C_apFuelLabels[i].m1E0_oddOrEven, dx);
        widePolyArray(battleVar0->m835C_apFuelLabels[i].mA0_fuelLabel[0],
                      battleVar0->m835C_apFuelLabels[i].m1E3_fuelLabelLength,
                      battleVar0->m835C_apFuelLabels[i].m1E0_oddOrEven, dx);
        widePolyArray(battleVar0->m6008_apFuelDigits[i][0], battleVar1->mEC_apFuelDigitCounts[i],
                      battleVar1->m99_oddOrEven[i], dx);
        widePolyArray(battleVar0->m3A88_hpDigits[i][0], battleVar1->mE0_hpDigitCounts[i], battleVar1->m93_oddOrEven[i],
                      dx);
        widePolyArray(battleVar0->m818_portraits[i], 1, battleVar1->m83_portraitsOddOrEven, dx);
        widePolyArray(battleVar0->m0_apBarFrame[i][0], battleVar1->m78_apBarFrameLengths[i],
                      battleVar1->mA2_apBarFrameOddOrEven, dx);
        pcWidePrim(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], dx, 0);
        pcWidePrim(&battleVar0->m5A0_timeBarFills[0][i * 2 + battleVar1->m84_timeBarsOddOrEven[i]], dx, 0);
    }
}

void battleRenderPortraitSelection(void) {
    if (battleVar0->m6415_drawHighlight) {
        setRGB0(&battleVar0->m63C8_portraitHighlight[battleVar0->m6414_highlightOddOrEven], battleVar0->m6410_highlightLevel, battleVar0->m6410_highlightLevel, battleVar0->m6410_highlightLevel);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m63C8_portraitHighlight[battleVar0->m6414_highlightOddOrEven]);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m63F8_portraitHighlightDrMode[battleVar0->m6414_highlightOddOrEven]);
        /* With the portrait it is on (wideAnchorPartyBoxes). */
        if (pcWideView != 0) {
            POLY_F4* hl = &battleVar0->m63C8_portraitHighlight[battleVar0->m6414_highlightOddOrEven];
            s32 k, bx;

            for (k = 0; k < 3; k++) {
                bx = k * 0x60 + partyMemberSpritesOffset[battlePartyLayoutType * 3 + k];
                if (hl->x0 >= bx && hl->x0 < bx + 0x60) {
                    pcWidePrim(hl, widePartyBoxDx(k), 0);
                }
            }
        }
    }
}


void battleRenderPlayerPortraits(void) {
    s32 i;

    for (i = 0; i < battleVar1->m97_apMarkCount; i++) {
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m908_apMarks[battleVar1->m98_apMarksOddOrEven + i * 2]);
    }
    for (i = 0; i < 4; i++) {
        battleRenderPolyArray(battleVar0->m2E08_timeBars[i][0], battleVar1->m74_timeBarsLengths[i], battleVar1->m84_timeBarsOddOrEven[i]);
    }
    for (i = 0; i < 3; i++) {
        if (battleVar1->mCC_drawApFuel[i]) {
            switch (battleVar0->m835C_apFuelLabels[i].m1E1_barMode) {
            case 1:
                battleRenderPolyArray(battleVar0->m835C_apFuelLabels[i].m0_apLabel[0], battleVar0->m835C_apFuelLabels[i].m1E2_apLabelLength, battleVar0->m835C_apFuelLabels[i].m1E0_oddOrEven);
                break;
            case 2:
                battleRenderPolyArray(battleVar0->m835C_apFuelLabels[i].mA0_fuelLabel[0], battleVar0->m835C_apFuelLabels[i].m1E3_fuelLabelLength, battleVar0->m835C_apFuelLabels[i].m1E0_oddOrEven);
                break;
            }
            battleRenderPolyArray(battleVar0->m6008_apFuelDigits[i][0], battleVar1->mEC_apFuelDigitCounts[i], battleVar1->m99_oddOrEven[i]);
        }
    }
    if (battleVar1->mAF_drawApCounter) {
        battleRenderPolyArray(battleVar0->m9C8_APCounterDisplayPolys[0], battleVar1->m7B_apCounterLength, battleVar1->mA4_oddOrEven7B);
    }
    for (i = 0; i < 3; i++) {
        battleRenderPolyArray(battleVar0->m3A88_hpDigits[i][0], battleVar1->mE0_hpDigitCounts[i], battleVar1->m93_oddOrEven[i]);
    }
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
            { int k; for (k = 0; k < 1; ++k) pcTextureClass(&(battleVar0->m818_portraits[i])[k * 2 + battleVar1->m83_portraitsOddOrEven], PC_TEX_BATTLE_PORTRAIT); }
            battleRenderPolyArray(battleVar0->m818_portraits[i], 1, battleVar1->m83_portraitsOddOrEven);
            if (battleVar1->mCC_drawApFuel[i]) {
                { int k; for (k = 0; k < battleVar1->m78_apBarFrameLengths[i]; ++k) pcTextureClass(&(battleVar0->m0_apBarFrame[i][0])[k * 2 + battleVar1->mA2_apBarFrameOddOrEven], PC_TEX_HUD); }
                battleRenderPolyArray(battleVar0->m0_apBarFrame[i][0], battleVar1->m78_apBarFrameLengths[i], battleVar1->mA2_apBarFrameOddOrEven);
                AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven]);
            }
        }
    }
    if (pcWideView != 0) {
        wideAnchorPartyBoxes();
    }
}

void battleRenderCommandRing(void) {
    s32 i;

    if (battleVar1->mCB_drawCommandRing) {
        for (i = 0; i < 2; i++) {
            /* This shared font contains both command lettering and wheel
             * artwork. The wheel's measured atlas/CLUT is HUD, not text. */
            {
                s32 k;
                for (k = 0; k < battleVar1->mD0_battleCommandLength[i]; k++) {
                    POLY_FT4* p = &battleVar0->m641C_commandGlyphs[i][k * 2 + battleVar1->mA3_641COddOrEven];
                    /* Font drawing moves flipped UV starts back one texel.
                     * Those guard texels are part of the same wheel art. */
                    if ((p->tpage & 0x19F) == 15 && p->clut == GetClut(176, 497) &&
                        p->u0 >= 143 && p->u0 <= 244 && p->v0 >= 216 && p->v0 <= 241) {
                        pcTextureClass(p, PC_TEX_BATTLE_HUD);
                    } else if ((p->tpage & 0x19F) == 0x8F && p->clut == GetClut(0, 502) &&
                               p->u0 >= 80 && p->u0 <= 117 && p->v0 >= 140 && p->v0 <= 141) {
                        /* The point/cancel prompts share this command array.
                         * Their colored 8-bit buttons use the icon artwork. */
                        pcTextureClass(p, PC_TEX_ICON);
                    }
                }
            }
            battleRenderPolyArray(battleVar0->m641C_commandGlyphs[i], battleVar1->mD0_battleCommandLength[i], battleVar1->mA3_641COddOrEven);
        }
    }
}

void render_BA8_27C8_1E68(void) {
    if (battleVar1->m9C_renderBA8) {
        battleRenderPolyArray(battleVar0->mBA8_menuGlyphs[0], battleVar1->mF8_countOfBA8, battleVar1->mA5_ba8OddOrEven);
    }
    if (battleVar1->m9E_render27C8) {
        { int k; for (k = 0; k < battleVar1->m100_countOf27C8; ++k) pcTextureClass(&(battleVar0->m27C8_selectionCursor[0])[k * 2 + battleVar1->mA7_27C8OddOrEven], PC_TEX_ICON); }
        battleRenderPolyArray(battleVar0->m27C8_selectionCursor[0], battleVar1->m100_countOf27C8, battleVar1->mA7_27C8OddOrEven);
    }
    if (battleVar1->m9D_render1E68) {
        battleRenderPolyArray(battleVar0->m1E68_menuGlyphs2[0], battleVar1->mFC_countOF1E68, battleVar1->mA6_1E68OddOrEven);
    }
}

void battleDrawAPBar(void) {
    s32 i;

    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m8920_fullWindowDrMode[D_800CCB04.m30_battleOddOrEven]);
    for (i = 0; i < 4; i++) {
        if (battleVar1->m7F_drawTimeBarFill[i] && currentBattleMode == 1) {
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m5A0_timeBarFills[0][i * 2 + battleVar1->m84_timeBarsOddOrEven[i]]);
        }
    }
    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->m8908_drMode[D_800CCB04.m30_battleOddOrEven]);
    for (i = 0; i < 7; i++) {
        if (battleVar1->mB0_isDialogWindowInitialized[i]) {
            sBattleDialogWindow* w = battleDialogWindows[i];
            { int k; for (k = 0; k < w->m5A0_cornerCount; ++k) pcTextureClass(&(w->m0_corners[0])[k * 2 + w->m5A4_oddOrEven], PC_TEX_HUD); }
            battleRenderPolyArray(w->m0_corners[0], w->m5A0_cornerCount, w->m5A4_oddOrEven);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m280_leftEdge[0][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m280_leftEdge[1][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m320_rightEdge[0][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m320_rightEdge[1][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m140_topEdge[0][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m140_topEdge[1][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m1E0_bottomEdge[0][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m1E0_bottomEdge[1][w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m3C0_polyG4[w->m5A4_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &w->m408_drawMode[w->m5A4_oddOrEven]);
        }
    }
}

void updatePolyArray3A88(void) {
    s32 i;
    s32 tint;

    for (i = 0; i < 3; i++) {
        if (battleVar1->m7C_hpDisplayDirty[i]) {
            battleVar1->mE0_hpDigitCounts[i] = 0;
            battleVar1->mEC_apFuelDigitCounts[i] = 0;
            if (battleVisualEntities[i].m2_characterId != 0x7F) {
                setupPolyArray3A88_HP(i);
                tint = setupPolyArray3A88_HP2(i, apConfigArray[i].m1_gearState);
                if (apConfigArray[i].m1_gearState != 0) {
                    tintHpGear(i, tint);
                } else {
                    tintHP(i, tint);
                }
                battleVar1->m93_oddOrEven[i] = D_800CCB04.m30_battleOddOrEven;
                battleVar1->m7C_hpDisplayDirty[i] = 0;
            }
        }
    }
}

void renderOpenMenuSub0(void) {
    if (battleVar0->mA230_menuPanel->m669_drawEPCost) {
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0][battleVar0->mA230_menuPanel->m668_oddOrEven]);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[1][battleVar0->mA230_menuPanel->m668_oddOrEven]);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[2][battleVar0->mA230_menuPanel->m668_oddOrEven]);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[3][battleVar0->mA230_menuPanel->m668_oddOrEven]);
    }
}

/* draws polys 10 and 11 of battleVar0->mA230 (sibling of drawDescriptionText) */
void drawTargetAndElementIcons(void) {
    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[10][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[11][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
}

void drawDescriptionText(void) {
    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[8][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
    AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[9][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
}

void renderOpenMenu(void) {
    if (battleVar1->mB7_openMenuType) {
        s32 k, odd;
        for (k = 0; k < 18; k++) for (odd = 0; odd < 2; odd++)
            pcTextureClass(&battleVar0->mA230_menuPanel->m0_polys[k][odd], PC_TEX_TEXT);
        for (odd = 0; odd < 2; odd++)
            pcTextureClass(&battleVar0->mA230_menuPanel->m5A0_mpSlash[odd], PC_TEX_TEXT);
    }
    switch (battleVar1->mB7_openMenuType) {
    case 1:
        renderOpenMenuSub0();
        if (battleVar0->mA230_menuPanel->m669_drawEPCost) {
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0xD][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0xE][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0xF][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0x10][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0x11][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m5A0_mpSlash[battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
        }
        if (battleVar0->mA230_menuPanel->m66B_drawSelection) {
            if (battleVar0->mA230_menuPanel->m66E_costDigitCount) {
                AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0xC][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            }
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[4][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
            battleRenderPolyArray(battleVar0->mA230_menuPanel->m0_polys[5], battleVar0->mA230_menuPanel->m66E_costDigitCount, battleVar0->mA230_menuPanel->m66A_oddOrEven);
            drawDescriptionText();
            drawTargetAndElementIcons();
        }
        break;
    case 2:
        renderOpenMenuSub0();
        if (battleVar0->mA230_menuPanel->m66B_drawSelection) {
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[4][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[5][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
        }
        if (battleVar0->mA230_menuPanel->m66D_drawDescription) {
            drawTargetAndElementIcons();
            drawDescriptionText();
        }
        break;
    case 3:
        renderOpenMenuSub0();
        if (battleVar0->mA230_menuPanel->m66F_drawApBar) {
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m5F0_apBarFill[battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m638_apBarEmpty[battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
        }
        break;
    case 4:
        renderOpenMenuSub0();
        if (battleVar0->mA230_menuPanel->m66B_drawSelection) {
            if (battleVar0->mA230_menuPanel->m66E_costDigitCount) {
                AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[0xC][battleVar0->mA230_menuPanel->m66C_epCostOddOrEven]);
            }
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleVar0->mA230_menuPanel->m0_polys[4][battleVar0->mA230_menuPanel->m66A_oddOrEven]);
            drawDescriptionText();
            drawTargetAndElementIcons();
        }
        break;
    case 5:
        renderOpenMenuSub0();
        break;
    }
}

void battleDrawOverallUI(void) {
    if (battleVar1->mC7_drawGearFuel) {
        battleRenderPolyArray(pBattleRenderStruct2->m3E80_modeLabel, pBattleRenderStruct2->m5D70_count[7], pBattleRenderStruct2->m5D83_oddOrEven[3]);
        battleRenderPolyArray(pBattleRenderStruct2->m5280_fuelDigits, pBattleRenderStruct2->m5D70_count[0xC], pBattleRenderStruct2->m5D83_oddOrEven[8]);
        battleRenderPolyArray(pBattleRenderStruct2->m53C0_maxFuelDigits, pBattleRenderStruct2->m5D70_count[0xD], pBattleRenderStruct2->m5D83_oddOrEven[9]);
        battleRenderPolyArray(pBattleRenderStruct2->m43D0_modeLabelShadow, pBattleRenderStruct2->m5D70_count[0xF], pBattleRenderStruct2->m5D83_oddOrEven[0xB]);
    }
    if (battleVar1->mAD_drawGearHud) {
        battleRenderPolyArray(pBattleRenderStruct2->m46A0_randomDigits, 0x14, pBattleRenderStruct2->m5D83_oddOrEven[0x15]);
        battleRenderPolyArray(pBattleRenderStruct2->m0, pBattleRenderStruct2->m5D70_count[0], pBattleRenderStruct2->m5D83_oddOrEven[0xF]);
        { int k; for (k = 0; k < pBattleRenderStruct2->m5D70_count[4]; ++k) pcTextureClass(&(pBattleRenderStruct2->m1720_frame)[k * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0]], PC_TEX_HUD); }
        battleRenderPolyArray(pBattleRenderStruct2->m1720_frame, pBattleRenderStruct2->m5D70_count[4], pBattleRenderStruct2->m5D83_oddOrEven[0]);
        if (++pBattleRenderStruct2->m5DA2_blinkTimer < 15) {
            battleRenderPolyArray(pBattleRenderStruct2->m4CE0_statusGlyphs, pBattleRenderStruct2->m5DA1_count, pBattleRenderStruct2->m5DA0_oddOrEven);
        } else if (pBattleRenderStruct2->m5DA2_blinkTimer >= 21) {
            pBattleRenderStruct2->m5DA2_blinkTimer = 0;
        }
        battleRenderPolyArray(pBattleRenderStruct2->m3AC0_boosterLabel, pBattleRenderStruct2->m5D70_count[5], pBattleRenderStruct2->m5D83_oddOrEven[1]);
        battleRenderPolyArray(pBattleRenderStruct2->m3E30, pBattleRenderStruct2->m5D70_count[6], pBattleRenderStruct2->m5D83_oddOrEven[2]);
        battleRenderPolyArray(pBattleRenderStruct2->m4E70_attackDigits, pBattleRenderStruct2->m5D70_count[8], pBattleRenderStruct2->m5D83_oddOrEven[4]);
        battleRenderPolyArray(pBattleRenderStruct2->m4FB0_infinityChanceDigits, pBattleRenderStruct2->m5D70_count[9], pBattleRenderStruct2->m5D83_oddOrEven[5]);
        battleRenderPolyArray(pBattleRenderStruct2->m50A0_defenceDownDigits, pBattleRenderStruct2->m5D70_count[0xA], pBattleRenderStruct2->m5D83_oddOrEven[6]);
        battleRenderPolyArray(pBattleRenderStruct2->m51E0_agilityDigits, pBattleRenderStruct2->m5D70_count[0xB], pBattleRenderStruct2->m5D83_oddOrEven[7]);
        { int k; for (k = 0; k < pBattleRenderStruct2->m5D70_count[0xE]; ++k) pcTextureClass(&(pBattleRenderStruct2->m2530_frameShadow)[k * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0xA]], PC_TEX_HUD); }
        battleRenderPolyArray(pBattleRenderStruct2->m2530_frameShadow, pBattleRenderStruct2->m5D70_count[0xE], pBattleRenderStruct2->m5D83_oddOrEven[0xA]);
        battleRenderPolyArray(pBattleRenderStruct2->m32A0_markerYFrame, pBattleRenderStruct2->m5D83_oddOrEven[0x13], pBattleRenderStruct2->m5D83_oddOrEven[0x14]);
        battleRenderPolyArray(pBattleRenderStruct2->m3C0_markerXFrame, pBattleRenderStruct2->m5D70_count[1], pBattleRenderStruct2->m5D83_oddOrEven[0x10]);
        battleRenderPolyArray(pBattleRenderStruct2->mD20_marker, pBattleRenderStruct2->m5D70_count[2], pBattleRenderStruct2->m5D83_oddOrEven[0x11]);
        battleRenderPolyArray(pBattleRenderStruct2->mFA0_markerOverlay, pBattleRenderStruct2->m5D70_count[3], pBattleRenderStruct2->m5D83_oddOrEven[0x12]);
    }
}

void renderTargetSelectionCursor(void) {
    s32 i;

    if (battleVar1->mC6_isTargetSelectionCursorVisible) {
        if (battleG3->mE5_fadeDirection) {
            if ((battleG3->mE0_fadeValue -= 4) < 0x40) {
                battleG3->mE5_fadeDirection = 0;
                battleG3->mE0_fadeValue = 0x40;
            }
        } else {
            if ((battleG3->mE0_fadeValue += 4) >= 0x100) {
                battleG3->mE5_fadeDirection = 1;
                battleG3->mE0_fadeValue = 0xFC;
            }
        }
        for (i = 0; i < 4; i++) {
            if (battleG3->mE6_isDirectionEnabled[i]) {
                setRGB0(&battleG3->m0_poly[i * 2 + battleG3->mE4_oddOrEven], battleG3->mE0_fadeValue, 0, 0);
                AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleG3->m0_poly[i * 2 + battleG3->mE4_oddOrEven]);
            }
        }
    }
}

void renderDeathBlowFlashPolys(void) {
    if (battleVar1->mA8_drawComboInput) {
        battleRenderPolyArray(pBattleRenderStruct2->m5550_commandNames, pBattleRenderStruct2->m5D70_count[0x10], pBattleRenderStruct2->m5D83_oddOrEven[0xC]);
        { int k; for (k = 0; k < pBattleRenderStruct2->m5D70_count[0x11]; ++k) pcTextureClass(&(pBattleRenderStruct2->m5640_comboInputIcons)[k * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0xD]], PC_TEX_ICON); }
        battleRenderPolyArray(pBattleRenderStruct2->m5640_comboInputIcons, pBattleRenderStruct2->m5D70_count[0x11], pBattleRenderStruct2->m5D83_oddOrEven[0xD]);
        battleRenderPolyArray(pBattleRenderStruct2->m5C80_fuelCosts, pBattleRenderStruct2->m5D70_count[0x12], pBattleRenderStruct2->m5D83_oddOrEven[0xE]);
    }
}

void drawDialogPortrait(void) {
    if (battleVar1->mC8_dialogPortrait) {
        pcTextureClass(&battleEventVar0->m7A4_portraitPolys[battleEventVar0->m7F4_battleDialogPortraitOddOrEven], PC_TEX_PORTRAIT);
        AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &battleEventVar0->m7A4_portraitPolys[battleEventVar0->m7F4_battleDialogPortraitOddOrEven]);
    }
    if (battleVar1->mC9_dialog) {
        updateAndRenderTextForDialogWindow(battleEventVar1, D_800CCB04.m0_pCurrentBattleOT + 1, D_800CCB04.m30_battleOddOrEven);
    }
}

/* Noah: drawMonsterNames */
#if VERSION_JP
#define MONSTER_NAME_CENTER_X 0x3A
#else
#define MONSTER_NAME_CENTER_X 0x40
#endif
void drawMonsterNames(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (previousEntityTurn.m8_nameLabels[i].m5D_monsterNameIsDisplayed) {
            if (i == 0) {
                repositionTextRenderingPlanePrim(&previousEntityTurn.m8_nameLabels[i].m0_polys[D_800CCB04.m30_battleOddOrEven],
                              MONSTER_NAME_CENTER_X - (previousEntityTurn.m8_nameLabels[i].m5E_currentMonsterNameStringWidth >> 1), 0x2C, 0, 0,
                              previousEntityTurn.m8_nameLabels[i].m5E_currentMonsterNameStringWidth);
            } else {
                repositionTextRenderingPlanePrim(&previousEntityTurn.m8_nameLabels[i].m0_polys[D_800CCB04.m30_battleOddOrEven],
                              0x9A - (previousEntityTurn.m8_nameLabels[i].m5E_currentMonsterNameStringWidth >> 1), 0xCA, 0,
                              (i / 2) * 0xD, previousEntityTurn.m8_nameLabels[i].m5E_currentMonsterNameStringWidth);
            }
            pcTextureClass(&previousEntityTurn.m8_nameLabels[i].m0_polys[D_800CCB04.m30_battleOddOrEven], PC_TEX_TEXT);
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, &previousEntityTurn.m8_nameLabels[i].m0_polys[D_800CCB04.m30_battleOddOrEven]);
        }
    }
}

void updateApFuelPolyBar(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        switch (battleVar0->m835C_apFuelLabels[i].m1E1_barMode) {
        case 1:
            setXY4(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven],
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28), 0x22,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28) + apConfigArray[i].m0_storedAP * 2, 0x22,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28), 0x26,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28) + apConfigArray[i].m0_storedAP * 2, 0x26);
            setRGB0(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0, 0xFF, 0);
            setRGB1(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0, 0xFF, 0);
            break;
        case 2: {
            u32 fuel = battleEntities.m0_entities[i].mA4_gear.m38_fuel;
            u32 maxFuel = battleEntities.m0_entities[i].mA4_gear.m3A_maxFuel;
            u32 ratio = R3000_DIV(fuel * 100, maxFuel) * 5600 / 10000;

            setXY4(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven],
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28), 0x22,
                   i * 0x60 + 0x28 + partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + ratio, 0x22,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x28), 0x26,
                   i * 0x60 + 0x28 + partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + ratio, 0x26);
            if (fuel >= maxFuel / 4) {
                setRGB0(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0, 0, 0xFF);
                setRGB1(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0, 0, 0xFF);
            } else if (fuel >= maxFuel / 8) {
                setRGB0(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0xFF, 0xFF, 0);
                setRGB1(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0xFF, 0xFF, 0);
            } else {
                setRGB0(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0xFF, 0x7F, 0x7F);
                setRGB1(&battleVar0->m740_APOrFuelPoly[0][i * 2 + D_800CCB04.m30_battleOddOrEven], 0xFF, 0x7F, 0x7F);
            }
            break;
        }
        }
    }
}

/* Noah: renderGearHP (updateApFuelPolyBar + renderGearHPSub0, inlined) */
void renderGearHP(void) {
    s32 fill[4];
    s32 i;
    u16 clut;

    updateApFuelPolyBar();
    battleVar1->m7F_drawTimeBarFill[3] = 0;
    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F) {
        if (fill[i] < 0) {
            fill[i] = 0;
        }
        if (battleVar1->m90_perPCTimeBarStatus[i] == 0) {
            fill[i] = (100 - R3000_DIV(battleSlotStatusVar0.m16_numTicksBeforeReady[i] * 100, battleSlotStatusVar0.m0_battleSlotStatusVar0[i])) * 5600 / 10000;
            setXY4(&battleVar0->m5A0_timeBarFills[0][i * 2 + D_800CCB04.m30_battleOddOrEven],
                   partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + i * 0x60 + 0x2D, 0x1A,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x2C) + (s16)(fill[i] + 1), 0x1A,
                   partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + i * 0x60 + 0x2D, 0x1E,
                   i * 0x60 + (partyMemberSpritesOffset[battlePartyLayoutType * 3 + i] + 0x2C) + (s16)(fill[i] + 1), 0x1E);
            setUV4(&battleVar0->m5A0_timeBarFills[0][i * 2 + D_800CCB04.m30_battleOddOrEven],
                   (battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1, battleVar0->mA234_glyphTextures[0].m14_tpage_Y,
                   ((battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1) + fill[i] * 2, battleVar0->mA234_glyphTextures[0].m14_tpage_Y,
                   (battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1, battleVar0->mA234_glyphTextures[0].m14_tpage_Y + 4,
                   ((battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1) + fill[i] * 2, battleVar0->mA234_glyphTextures[0].m14_tpage_Y + 4);
            if ((battleEntities.m0_entities[i].m0_base.m84_statusFlags2[0] | battleEntities.m0_entities[i].m0_base.m84_statusFlags2[1]) & 0x8000) {
                clut = battleVar0->mA2B2_hasteTimeBarClut;
            } else if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x1000) {
                clut = battleVar0->mA2B0_slowTimeBarClut;
            } else {
                clut = battleVar0->mA2AC_timeBarClut;
            }
            battleVar0->m5A0_timeBarFills[0][i * 2 + D_800CCB04.m30_battleOddOrEven].clut = clut;
            battleVar1->m7F_drawTimeBarFill[i] = 1;
        } else {
            fill[i] = (100 - battleSlotStatusVar0.m16_numTicksBeforeReady[i]) * 5600 / 10000;
            if (apConfigArray[i].m1_gearState != 0 && battleCharacters[i] != 7) {
                fill[i] = R3000_DIV(battleEntities.m0_entities[i].mA4_gear.m38_fuel * 100, battleEntities.m0_entities[i].mA4_gear.m3A_maxFuel) * 5600 / 10000;
            }
            setXY4(&battleVar0->m5A0_timeBarFills[3][D_800CCB04.m30_battleOddOrEven],
                   0xC, 0xCE, 0xC, 0xCE - fill[i] * 2, 0x14, 0xCE, 0x14, 0xCE - fill[i] * 2);
            setUV4(&battleVar0->m5A0_timeBarFills[3][D_800CCB04.m30_battleOddOrEven],
                   (battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1, battleVar0->mA234_glyphTextures[0].m14_tpage_Y,
                   ((battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1) + fill[i] * 2, battleVar0->mA234_glyphTextures[0].m14_tpage_Y,
                   (battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1, battleVar0->mA234_glyphTextures[0].m14_tpage_Y + 4,
                   ((battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1) + fill[i] * 2, battleVar0->mA234_glyphTextures[0].m14_tpage_Y + 4);
            battleVar0->m5A0_timeBarFills[0][D_800CCB04.m30_battleOddOrEven + 6].clut = battleVar0->mA2AE_fuelBarClut;
            if (battleVar1->mCB_drawCommandRing) {
                battleVar1->m7F_drawTimeBarFill[3] = 1;
            }
        }
        } else {
            battleVar1->m7F_drawTimeBarFill[i] = 0;
        }
    }
}

void pcBattleEnemyInfo(void); /* port/src/game/battle/enemyinfo.c: an option, off by default */
void pcBattleRetryDraw(void); /* port/src/game/battle/retryprompt.c: the retry setting's choice */
void pcVersusDraw(void); /* port/src/game/battle/versus.c: player 2's window in a versus battle */

void drawBattleMode1(void) {
    if (!drawBattleMode1Disabled) {
        pcBattleEnemyInfo(); /* first: AddPrim order puts it on top of the HUD */
        drawBattleDialogWindows();
        pcBattleRetryDraw(); /* its text, where drawDialogPortrait draws a dialogue's */
        pcVersusDraw();
        updatePolyArray3A88();
        renderGearHP();
        updatePortraits();
        battleRenderPortraitSelection();
        battleRenderPlayerPortraits();
        battleRenderCommandRing();
        render_BA8_27C8_1E68();
        drawMonsterNames();
        renderDeathBlowFlashPolys();
        renderOpenMenu();
        renderTargetSelectionCursor();
        battleDrawAPBar();
        renderBattleResultText();
        battleDrawOverallUI();
    }
}

void drawBattleMode0(void) {
    battleDebugDisplay = 0;
    drawBattleDialogWindows();
    battleResultDraw();
    battleDrawAPBar();
}

void drawBattleMode2(void) {
    drawBattleDialogWindows();
    pcBattleRetryDraw();
    pcVersusDraw();
    updatePortraits();
    render_BA8_27C8_1E68();
    drawMonsterNames();
    drawDialogPortrait();
    battleDrawAPBar();
    renderBattleResultText();
    battleDrawOverallUI();
}

/*
 * The rest of the wide HUD (wideAnchorPartyBoxes above has the party's
 * boxes): what sits at the 4:3 picture's left goes to the wide picture's
 * left edge, what sits at its right to the right edge, and the middle stays.
 *   - left: the turn gauge and its "Time" label (which flies there from its
 *     owner's box: it follows that box, then the edge), the AP marks and
 *     counter, the deathblow input list, the command ring, and the target's
 *     name window under the first box;
 *   - the gear cockpit (battleDrawOverallUI): its left and right panels go to
 *     the edges, its top and bottom bands open up from the middle and the gap
 *     is filled with copies of their middle piece; the scope and its marker
 *     stay round the target;
 *   - menus, dialogs and the enemies' attack names stay in the middle; the
 *     target cursor and damage numbers follow the actors.
 * Only the wide picture moves (include/pc_wide.h); the 4:3 one is the PS1's.
 */
static void wideHudArray(POLY_FT4* p, s32 count, s32 oddOrEven, s32 dx) {
    s32 i;

    for (i = 0; dx != 0 && i < count; i++) {
        pcWidePrim(&p[i * 2 + oddOrEven], dx, 0);
    }
}

static s32 wideMinX(POLY_FT4* p) {
    s32 a = p->x0 < p->x1 ? p->x0 : p->x1;
    s32 b = p->x2 < p->x3 ? p->x2 : p->x3;

    return a < b ? a : b;
}

static s32 wideMaxX(POLY_FT4* p) {
    s32 a = p->x0 > p->x1 ? p->x0 : p->x1;
    s32 b = p->x2 > p->x3 ? p->x2 : p->x3;

    return a > b ? a : b;
}

/* A cockpit piece by where it is: a left or right panel, else the middle. */
static void wideHudSides(POLY_FT4* p, s32 count, s32 oddOrEven) {
    s32 i;

    for (i = 0; i < count; i++) {
        POLY_FT4* q = &p[i * 2 + oddOrEven];
        if (wideMaxX(q) <= 0x80) {
            pcWidePrim(q, -pcWideView, 0);
        } else if (wideMinX(q) >= 0xC0) {
            pcWidePrim(q, pcWideView, 0);
        }
    }
}

/* A piece of a band across the top or the bottom (y above or below band). */
static s32 wideInBand(POLY_FT4* q, s32 band) {
    s32 y = q->y0 < q->y3 ? q->y0 : q->y3;
    s32 y1 = q->y0 > q->y3 ? q->y0 : q->y3;

    return band == 0 ? y1 <= 0x20 : y >= 0xB8;
}

/* The cockpit's top and bottom bands: each half moves out to its edge, and
 * copies of the left half's innermost piece (16 pixels: the band repeats)
 * fill the middle, drawn only in the wide picture. Pieces off the bands go by
 * wideHudSides. */
static void wideHudBands(POLY_FT4* p, s32 count, s32 oddOrEven) {
    s32 i, band, x, w, l1, r0;
    POLY_FT4* fill;
    POLY_FT4* c;

    for (i = 0; i < count; i++) {
        POLY_FT4* q = &p[i * 2 + oddOrEven];
        if (!wideInBand(q, 0) && !wideInBand(q, 1)) {
            /* The status panel's pieces (top right); a lone mark below it
             * belongs to the scope. */
            if (q->y0 < 0x78 || wideMaxX(q) <= 0x80) {
                wideHudSides(q, 1, 0);
            }
        } else {
            pcWidePrim(q, wideMinX(q) + wideMaxX(q) < 0x140 ? -pcWideView : pcWideView, 0);
        }
    }
    for (band = 0; band < 2; band++) {
        fill = NULL;
        l1 = -0x1000;
        r0 = 0x1000;
        for (i = 0; i < count; i++) {
            POLY_FT4* q = &p[i * 2 + oddOrEven];
            if (!wideInBand(q, band)) {
                continue;
            }
            if (wideMinX(q) + wideMaxX(q) < 0x140) {
                if (wideMaxX(q) > l1) {
                    l1 = wideMaxX(q);
                    fill = q;
                }
            } else if (wideMinX(q) < r0) {
                r0 = wideMinX(q);
            }
        }
        /* Only a plain piece: 16 pixels of 16 texels, not mirrored. */
        if (!pcWideShifts() || fill == NULL || r0 == 0x1000 || fill->x1 - fill->x0 != 16 || fill->x3 - fill->x2 != 16 ||
            fill->u1 - fill->u0 != 16) {
            continue;
        }
        for (x = l1 - pcWideView; x < r0 + pcWideView; x += 16) {
            w = r0 + pcWideView - x < 16 ? r0 + pcWideView - x : 16;
            c = pcWideAlloc(sizeof(POLY_FT4));
            if (c == NULL) {
                return;
            }
            *c = *fill;
            c->x0 = c->x2 = x;
            c->x1 = c->x3 = x + w;
            c->u1 = c->u0 + w;
            c->u3 = c->u2 + w;
            AddPrim(D_800CCB04.m0_pCurrentBattleOT + 1, c);
            pcWideOnly(c);
        }
    }
}

static void wideHudCockpit(void) {
    sBattleRenderStruct2* r = pBattleRenderStruct2;

    if (battleVar1->mC7_drawGearFuel) {
        wideHudSides(r->m3E80_modeLabel, r->m5D70_count[7], r->m5D83_oddOrEven[3]);
        wideHudSides(r->m5280_fuelDigits, r->m5D70_count[0xC], r->m5D83_oddOrEven[8]);
        wideHudSides(r->m53C0_maxFuelDigits, r->m5D70_count[0xD], r->m5D83_oddOrEven[9]);
        wideHudSides(r->m43D0_modeLabelShadow, r->m5D70_count[0xF], r->m5D83_oddOrEven[0xB]);
    }
    if (battleVar1->mAD_drawGearHud) {
        /* Linked first, so drawn under the rest (as the bands are). */
        wideHudBands(r->m2530_frameShadow, r->m5D70_count[0xE], r->m5D83_oddOrEven[0xA]);
        wideHudBands(r->m3C0_markerXFrame, r->m5D70_count[1], r->m5D83_oddOrEven[0x10]);
        wideHudSides(r->m0, r->m5D70_count[0], r->m5D83_oddOrEven[0xF]);
        wideHudSides(r->m1720_frame, r->m5D70_count[4], r->m5D83_oddOrEven[0]);
        if (r->m5DA2_blinkTimer < 15) {
            wideHudSides(r->m4CE0_statusGlyphs, r->m5DA1_count, r->m5DA0_oddOrEven);
        }
        wideHudSides(r->m3AC0_boosterLabel, r->m5D70_count[5], r->m5D83_oddOrEven[1]);
        wideHudSides(r->m3E30, r->m5D70_count[6], r->m5D83_oddOrEven[2]);
        wideHudSides(r->m4E70_attackDigits, r->m5D70_count[8], r->m5D83_oddOrEven[4]);
        wideHudSides(r->m4FB0_infinityChanceDigits, r->m5D70_count[9], r->m5D83_oddOrEven[5]);
        wideHudSides(r->m50A0_defenceDownDigits, r->m5D70_count[0xA], r->m5D83_oddOrEven[6]);
        wideHudSides(r->m51E0_agilityDigits, r->m5D70_count[0xB], r->m5D83_oddOrEven[7]);
    }
}

/* The "Time" label's dx: at the gauge, the left edge; flying there from a
 * box, from that box's dx to the edge as it goes. */
static s32 wideTimeLabelDx(void) {
    s32 i, total, t, from;

    for (i = 0; i < 3; i++) {
        if (battleVisualEntities[i].m2_characterId != 0x7F &&
            (battleVar1->m90_perPCTimeBarStatus[i] == 2 || battleVar1->m90_perPCTimeBarStatus[i] == 3)) {
            from = widePartyBoxDx(i);
            total = battleVar1->m4C_timeBarAnimationTargetPositionY - (battleVar1->m44_timeBarScreenPositionY + 5);
            t = total > 0 ? (battleVar1->m6C_timeBarAnimationY >> 8) : 0;
            t = t > total ? total : t < 0 ? 0 : t;
            return total > 0 ? from + (-pcWideView - from) * t / total : from;
        }
    }
    return -pcWideView;
}

static void wideBattleHud(void) {
    s32 i, left = -pcWideView;
    sBattleDialogWindow* w;

    if (pcWideView == 0) {
        return;
    }
    if (currentBattleMode == 1) {
        wideHudArray(battleVar0->m2E08_timeBars[3][0], battleVar1->m74_timeBarsLengths[3],
                     battleVar1->m84_timeBarsOddOrEven[3], wideTimeLabelDx());
        pcWidePrim(&battleVar0->m5A0_timeBarFills[0][6 + battleVar1->m84_timeBarsOddOrEven[3]], left, 0);
        for (i = 0; i < battleVar1->m97_apMarkCount; i++) {
            pcWidePrim(&battleVar0->m908_apMarks[battleVar1->m98_apMarksOddOrEven + i * 2], left, 0);
        }
        wideHudArray(battleVar0->m9C8_APCounterDisplayPolys[0], battleVar1->m7B_apCounterLength,
                     battleVar1->mA4_oddOrEven7B, left);
        for (i = 0; i < 2; i++) {
            wideHudArray(battleVar0->m641C_commandGlyphs[i], battleVar1->mD0_battleCommandLength[i],
                         battleVar1->mA3_641COddOrEven, left);
        }
        if (battleVar1->mA8_drawComboInput) {
            wideHudArray(pBattleRenderStruct2->m5550_commandNames, pBattleRenderStruct2->m5D70_count[0x10],
                         pBattleRenderStruct2->m5D83_oddOrEven[0xC], left);
            wideHudArray(pBattleRenderStruct2->m5640_comboInputIcons, pBattleRenderStruct2->m5D70_count[0x11],
                         pBattleRenderStruct2->m5D83_oddOrEven[0xD], left);
            wideHudArray(pBattleRenderStruct2->m5C80_fuelCosts, pBattleRenderStruct2->m5D70_count[0x12],
                         pBattleRenderStruct2->m5D83_oddOrEven[0xE], left);
        }
    }
    /* The target's name, in its window under the first box. */
    if (previousEntityTurn.m8_nameLabels[0].m5D_monsterNameIsDisplayed) {
        pcWidePrim(&previousEntityTurn.m8_nameLabels[0].m0_polys[D_800CCB04.m30_battleOddOrEven], left, 0);
    }
    if (battleVar1->mB0_isDialogWindowInitialized[5] && battleDialogWindows[5] != NULL) {
        w = battleDialogWindows[5];
        wideHudArray(w->m0_corners[0], w->m5A0_cornerCount, w->m5A4_oddOrEven, left);
        for (i = 0; i < 2; i++) {
            pcWidePrim(&w->m280_leftEdge[i][w->m5A4_oddOrEven], left, 0);
            pcWidePrim(&w->m320_rightEdge[i][w->m5A4_oddOrEven], left, 0);
            pcWidePrim(&w->m140_topEdge[i][w->m5A4_oddOrEven], left, 0);
            pcWidePrim(&w->m1E0_bottomEdge[i][w->m5A4_oddOrEven], left, 0);
        }
        pcWidePrim(&w->m3C0_polyG4[w->m5A4_oddOrEven], left, 0);
    }
    wideHudCockpit();
}

void drawBattleFromModes(void) {
    switch (currentBattleMode) {
    case 0:
        drawBattleMode0();
        break;
    case 1:
        drawBattleMode1();
        break;
    case 2:
        drawBattleMode2();
        break;
    }
    if (currentBattleMode != 0 && !(currentBattleMode == 1 && drawBattleMode1Disabled)) {
        wideBattleHud();
    }
}

void timerBarInitStartOfTurnAnimation(s32 idx) {
    s32 x;
    s32 y;
    s32 off = partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx] + 0x48;

    battleVar1->m44_timeBarScreenPositionY = 0x1C;
    battleVar1->m34_timeBarScreenPositionX = idx * 0x60 + off;
    if (apConfigArray[idx].m1_gearState != 0 && battleCharacters[idx] != 7) {
        s32 off2 = partyMemberSpritesOffset[battlePartyLayoutType * 3 + idx] + 0x44;
        battleVar1->m44_timeBarScreenPositionY = 0x24;
        battleVar1->m34_timeBarScreenPositionX = idx * 0x60 + off2;
    }
    x = battleVar1->m34_timeBarScreenPositionX - 5;
    battleVar1->m3C_timeBarAnimationTargetPositionX = 0x10;
    battleVar1->m4C_timeBarAnimationTargetPositionY = 0x98;
    y = battleVar1->m44_timeBarScreenPositionY + 5;
    battleVar1->m54_timeBarAnimationXStep = x - battleVar1->m3C_timeBarAnimationTargetPositionX;
    battleVar1->m104_timeBarAnimationScale = 0x800;
    battleVar1->m5C_timeBarAnimationYStep = battleVar1->m4C_timeBarAnimationTargetPositionY - y;
    battleVar1->m54_timeBarAnimationXStep = R3000_DIV(battleVar1->m54_timeBarAnimationXStep << 8, battleVar1->m5C_timeBarAnimationYStep);
    battleVar1->m5C_timeBarAnimationYStep = 0x100;
    battleVar1->m64_timeBarAnimationX = 0;
    battleVar1->m6C_timeBarAnimationY = 0;
    battleVar1->m106_timeBarAnimationRotation = 0;
    battleVar1->mA9_timeBarNumMoveSteps = 6;
    battleVar1->mAB_timeBarNumAnimationSteps = 1;
    battleVar1->m90_perPCTimeBarStatus[idx]--;
}

/* Noah: timerBarUpdateStartOfTurnAnimation. barX/barY live on the stack in
 * retail (sh 0x28(sp)/0x30(sp)): u16 bar[2][4]. The second string's poly
 * pointer p is taken from len before k = len * 2 (len dies there, so
 * local-alloc ties it to k's register and k, set once, is a birthing insn
 * that sched1 keeps just ahead of the call); the loop starts from k. */
void timerBarUpdateStartOfTurnAnimation(s32 idx) {
    u16 bar[2][4];
    s32 i;
    s32 j;
    s32 len;
    s32 k;
    POLY_FT4 (*p)[2];

    for (i = 0; i < battleVar1->mA9_timeBarNumMoveSteps; i++) {
        battleVar1->m64_timeBarAnimationX -= battleVar1->m54_timeBarAnimationXStep;
        battleVar1->m6C_timeBarAnimationY += battleVar1->m5C_timeBarAnimationYStep;
        bar[0][0] = (battleVar1->m64_timeBarAnimationX >> 8) + battleVar1->m34_timeBarScreenPositionX;
        bar[1][0] = (battleVar1->m6C_timeBarAnimationY >> 8) + battleVar1->m44_timeBarScreenPositionY;
    }
    battleVar1->mA9_timeBarNumMoveSteps = 0;
    battleVar1->m74_timeBarsLengths[3] = 0;
    battleVar1->m74_timeBarsLengths[3] = battleSetupStringInPolyFTRot(battleFont, 0x52, battleVar0->m2E08_timeBars[3], D_800CCB04.m30_battleOddOrEven,
        bar[0][0], bar[1][0], (u16)battleVar1->m104_timeBarAnimationScale, (u16)battleVar1->m104_timeBarAnimationScale, (u16)battleVar1->m106_timeBarAnimationRotation);
    len = battleVar1->m74_timeBarsLengths[3];
    p = &battleVar0->m2E08_timeBars[3][len];
    k = len * 2;
    battleVar1->m74_timeBarsLengths[3] += battleSetupStringInPolyFTRot(battleFont, 0x53, p, D_800CCB04.m30_battleOddOrEven,
        bar[0][0], bar[1][0], (u16)battleVar1->m104_timeBarAnimationScale, (u16)battleVar1->m104_timeBarAnimationScale, (u16)battleVar1->m106_timeBarAnimationRotation);
    for (j = k; j < battleVar1->m74_timeBarsLengths[3] * 2; j += 2) {
        SetSemiTrans(&battleVar0->m2E08_timeBars[3][0][j + D_800CCB04.m30_battleOddOrEven], 1);
    }
    battleVar1->m84_timeBarsOddOrEven[3] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < battleVar1->mAB_timeBarNumAnimationSteps; i++) {
        battleVar1->m104_timeBarAnimationScale += 0x66;
        battleVar1->m106_timeBarAnimationRotation += 0x80;
    }
    battleVar1->mAB_timeBarNumAnimationSteps = 0;
    if (bar[0][0] <= battleVar1->m3C_timeBarAnimationTargetPositionX) {
        battleVar1->m90_perPCTimeBarStatus[idx] = 1;
    }
    if (battleVar1->m4C_timeBarAnimationTargetPositionY <= bar[1][0]) {
        battleVar1->m90_perPCTimeBarStatus[idx] = 1;
    }
}
