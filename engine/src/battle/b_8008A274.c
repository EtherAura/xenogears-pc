/* Provisional file (split where JP links the code in another order). */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "battle/battle.h"

extern sBattle800cdd40 partyBattleStats[3];
extern sBattle800CEF10 mechaBattleStats[3];
extern BattleBss_800D2C0A D_800D2C0A;            /* gear fuel costs ... */
extern sBattleMechaInitData* battleMechaInitData;
extern s32 battleOddOrEven;
extern sBattleSlotLayout battleSlotLayout[];
extern u8 battleInputButton;
extern u8 startCharacterJumpToEnemyVar0;

/* ---- used by 0x80086028-0x8008B478 ---- */
extern u16 bitmaskCharacter[];
extern u16 bitmaskCharacterReverse[];                      /* Noah: bitmaskCharacterReverse {0x8000 .. 1} */
void playSoundEffect(s32);
void repositionTextRenderingPlanePrim(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u8 w);
void battleInitPoly4(POLY_FT4* p, u8 odd, u8 mode);
extern u8 gearDeathblowAbilityIds[];
extern u8 currentBattleMode;
void battleUpdateInputs_mode1(u8);
void battleUpdateInputs_mode2(u8);
void battleUpdateInputs_mode0(u8);
void setupGearHudFrame(void);
void setupGearHudStatusIcons(void);
void setupGearHudBoosterLabel(void);
void setupGearHudModeLabels(void);
void setupGearHudAttackValue(void);
void setupGearHudInfinityChance(void);
void setupGearHudPercentValue(void);
void setupGearHudAgilityValue(void);
extern u8 printIntegerToStringBattleBuffer[9];
void battleSetupTextPolyAdditive(POLY_FT4* p);
extern u8 gearHudMarkerPathDone;
extern s32 gearHudMarkerOffsetX;
extern s32 gearHudMarkerOffsetY;
extern s32 gearHudMarkerStartX;
extern s32 gearHudMarkerStartY;
extern s32 gearHudMarkerEndX;
extern s32 gearHudMarkerEndY;
extern s32 gearHudMarkerStepX;
extern s32 gearHudMarkerStepY;
extern u8 gearHudMarkerReverseX;
extern u8 gearHudMarkerReverseY;
extern s32 gearHudMarkerSpeed;
void battleSetupTextPolySubtractive(POLY_FT4* p);
extern u8 battleTimeEnabled;
extern s32 battleMenuScrollOffset;
extern u8 battleMenuScrollRequest;
void battleTimeProgress(void);
void battleHandleInput(u8);
void battleChiMenuTextScrollTick(void);
extern s32* pRunningOnDTL;
extern s32 g_playTimeInVsync;                        /* play time in vsyncs */
extern u16 newPadButtonForDialogs;                        /* pad 0 newly pressed */
extern u16 newPadButtonForField;                        /* pad 0 pressed with auto-repeat */
extern u8 battleIsPaused;
extern u8 battleInputButton;
s32 isControllerConnected(s32);
void resumeMusic(void);
void pauseMusic(void);
void uploadBuiltinTexture(s32, s32);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void drawCircleMenuChi_updateSub5(u8 flag);
void abilityCancelSelection(u8 entity);
void updateMonsterScriptEntitiesVarByAtttack(u8, u8);
void performAttackSub6(u8);
void executeAbilitySub0(u16 mask, u8 entity);
void loadImageSync(RECT* rect, void* data);
extern u8 perApTable[8][3]; /* 8 rows up to the next label; its first byte is pBattleEntities's top byte */
void moveEntityToOtherEntity(u8 entity, u8 target);
void moveEntityToShootEntity(u8 entity, u8 target);
extern u8 battleDebugFontInitialized;
extern u8 battleDebugDisplay;
extern s32 gearHudMarkerTargets[];

void setupResultExpValues(void);
void setupResultExpGainedValues(void);
extern u16* deathblowNameImages[3];
s32 addGearDeathblowNamePoly(u8 entity, s32 index, s32 x, u8 cmd, u16** image);
u16* allocateTextureRamForText(s32 n);
s32 addDeathblowNamePoly(u8 slot, s32 index, s32 x, u8 textIdx, u16** image, u8 offset);
extern u8 gearHudFrameGlyphs[4];

/* ---- used by 0x80080AE4-0x80086028 ---- */
extern u8 autoPartyMemberTurnActive;
extern u8 battleTickMainSub0Var1[11];   /* accumulated damage type per entity */
extern s16 battleTickMainSub0Var0[11];  /* accumulated damage value per entity */
extern s32 damageDonePerAttack[11];  /* damage value per entity (current hit) */
extern u8 initBattleAttackStatusArray0[11];   /* damage type per entity (current hit) */
extern u16 entitiesHitInCurrentAttackBF[1];
extern u16 performAttack_type;
extern u8 battleGetSlotStatusSub_current28Index;
extern u8 newBattleInputButton2;
void freeBillyBattleStructs(u8);
void initBattleG3Array(void);
void selectDefaultTarget(u8);
void startCharacterJumpToEnemy(u8);
u8 drawCircleMenuItem_update(u8);
void executeItem(u8);
void characterPerformDefendAction(u8);
void drawCircleMenuChi_loadData(u8);
u8 drawCircleMenuChi_update(u8);
void executeAbility(u8);
u8 drawCircleMenuGearSpecial_update(u8);
u8 useCombo(u8);
extern u8 battleRunningVar1[1]; /* Noah: battleRunningVar1 (an array: retail keeps its address in a register) */
extern u8 gearSwitchDisabled;
extern u8 targetsPerPriority[12];
extern u8 numValidTarget;
u8 getEntityToFace(u8);
u8 isTargetValid(u8, u8);
u8 abilityTargetSelectionInitSub0Sub0(u8, u8);
u8 abilityTargetSelectionInitSub0(u8, u8, u8);
u8 abilityTargetSelectionInitSub1(u8);
u16 abilityTargetSelectionInit(u16, u16, u8, u8, u8);
u16 abilitySelectTargetSub0(void);
void updateTargetSelectionMarkerSub1(u8);
void removeTargetSelectionCursors(void);
s32 battleSetupStringInPolyFT4Large(s32, POLY_FT4 (*)[2], s16, s16);
extern u8* deathblowInputSequences[13];  /* deathblow input sequences (7 buttons each) */
extern u8* deathblowAbilityIdsByLoadout[];    /* per command loadout: deathblow ability index per sequence */
u16 bitmaskCharacterCheckReverse(u16, u8);
u8 randomInRange(s32, s32);
void clearEntityFlag15A_1(u8);
void initAnimSeqFromCharacterToCharacter(s32 entity, s32 target);
s32 initJumpData(u8, u8);
s32 performAttack(u8, u8);
void handleMenuSelectEnemy_cancel_sub0(u8);
s32 computeFacingForJump(u8);
extern u8 startCharacterJumpToEnemyVar1;
void cancelJumpAnim(void);
void updateTargetSelectionMarker(u8);
void removeBattleCursorDuringAttack(u8);
void updateAPCounterDisplay(void);
void updateDeathblowComboInput(u8, u8);
u8 handleMenuSelectEnemySub(u8, u8);
void battleSoundEffect(u8);         /* Noah: battleSoundEffect2 */
u8 updateGearDeathblowComboInput(u8, u8);
s32 isGearComboAttackAvailable(s32 cmd, u8 entity);
void updateGearHudFuel(u8);
extern u8 healWhileOnGear;
extern u8 apTurnDelayTicks[6][8];      /* ticks table [maxAP][remainingAP]; its last row runs into apMarkOffsetsY */
void performAttackSub2(void);
u8 selectNewSlotByDirection(u8, u8);
void battleRenderDebugAndMain(void);
extern u16 abilityTargetBitmask;
extern u8 abilitySelectedTarget;
u16 characterOdToInverseTargetBitmask(u8);
u16 bitmaskCharacterCheck(u16, u8);
void callGearRelocateSlot(u8);
void callGear(u8);
void callGearClearStatus(u8);
void markEnemyDead(u8);
void freeBattleVar1_AE(void);
void freeBattleVar1_96(void);
void freeBattleRenderStruct2(u8);
void setDamageDone(u8);
void applyChangeToHpOrMp(u8);

/* 72 263 260] */
void battleUpdateInputs_mode1(u8 arg0) {
    sBattleVar0* p;

#if VERSION_JP
    battleChiMenuTextScrollTick();
#endif
    if (battleTimeEnabled != 0) {
        battleTimeProgress();
    }
    if (arg0 == 0) {
        battleHandleInput(0);
    }
    battleVar1->mA9_timeBarNumMoveSteps += 6;
    battleVar1->mAB_timeBarNumAnimationSteps++;
    p = battleVar0;
    if (p->m6415_drawHighlight != 0) {
        if (p->m6416_highlightFadingOut == 0) {
            p->m6410_highlightLevel += 4;
            if (p->m6410_highlightLevel > 0x80) {
                p->m6410_highlightLevel = 0x7C;
                p->m6416_highlightFadingOut = 1;
            }
        } else {
            p->m6410_highlightLevel -= 4;
            if (p->m6410_highlightLevel < 0) {
                p->m6410_highlightLevel = 4;
                p->m6416_highlightFadingOut = 0;
            }
        }
    }
    switch (battleMenuScrollRequest) {
    case 1:
    case 3:
        battleMenuScrollOffset++;
        break;
    case 2:
    case 4:
        battleMenuScrollOffset--;
        break;
    }
#if !VERSION_JP
    battleChiMenuTextScrollTick();
#endif
}

/* 263 260] */
void battleUpdateInputs_mode2(u8 arg0) {
#if !VERSION_JP
    u8 paused;
    u8 waiting;
#endif
    s32 playTime;
    s32 i;

#if !VERSION_JP
    waiting = 1;
    paused = 0;
    do {
        if (isControllerConnected(0) == 0) {
            if (paused == 0) {
                uploadBuiltinTexture(0x88, 0x64);
                uploadBuiltinTexture(0x88, 0x144);
                pauseMusic();
                paused++;
                playTime = g_playTimeInVsync;
            }
        } else {
            waiting = 0;
            if (paused != 0) {
                resumeMusic();
                g_playTimeInVsync = playTime;
            }
        }
        if (waiting) {
            PORT_YIELD(); /* pad unplugged: see battleHandleInput */
        }
    } while (waiting);
#endif
    battleChiMenuTextScrollTick();
    if (battleVar1->mCA_battleEventPoly != 0) {
        for (i = 0; i < 16; i++) {
            if (battleEventVar0->m0_scriptEntities[i].m28_isWaiting != 0) {
                if (--battleEventVar0->m0_scriptEntities[i].m26_waitTimer < 0) {
                    battleEventVar0->m0_scriptEntities[i].m26_waitTimer = 0;
                }
            }
        }
    }
    do {
        if (battleVar1->mCF_isWaitingForInput == 0) {
            battleInputButton = 0xFF;
        }
        if (getInputOverflowed()) {
            resetInputs();
        } else {
            while (loadInputFromVSyncBuffer()) {
                if (battleIsPaused != 0) {
                    if (*pRunningOnDTL != -1 && (newPadButtonForField & 4) && (newPadButtonForField & 8)) {
                        battleRunningVar1[0] = 1;
                        goto resume;
                    }
                } else if (newPadButtonForDialogs & 0x20) {
                    battleInputButton = 4;
                    break;
                }
                if (newPadButtonForDialogs & 0x800) {
                    if (battleTimeEnabled != 0) {
                        if (battleIsPaused == 0) {
                            pauseMusic();
                            uploadBuiltinTexture(0x88, 0x64);
                            uploadBuiltinTexture(0x88, 0x144);
                            playTime = g_playTimeInVsync;
                            battleIsPaused = 1;
                        } else {
                        resume:
                            resumeMusic();
                            g_playTimeInVsync = playTime;
                            battleIsPaused = 0;
                        }
                    }
                    break;
                }
            }
        }
        if (battleIsPaused != 0) {
            PORT_YIELD(); /* paused: see battleHandleInput */
        }
    } while (battleIsPaused != 0);
}

void battleUpdateInputs_mode0(u8 arg0);
void battleUpdateInputs_mode0(u8 arg0) {
    u8 paused;
    u8 waiting;
    s32 playTime;
    u8 done;
    s32 i;

    done = 1;
#if !VERSION_JP /* JP has no pause-while-waiting loop here */
    waiting = 1;
    paused = 0;
    do {
        if (isControllerConnected(0) == 0) {
            if (paused == 0) {
                uploadBuiltinTexture(0x88, 0x64);
                uploadBuiltinTexture(0x88, 0x144);
                pauseMusic();
                paused++;
                playTime = g_playTimeInVsync;
            }
        } else {
            waiting = 0;
            if (paused != 0) {
                resumeMusic();
                g_playTimeInVsync = playTime;
            }
        }
        if (waiting) {
            PORT_YIELD(); /* pad unplugged: see battleHandleInput */
        }
    } while (waiting);
#endif
    if (battleVar1->mCF_isWaitingForInput == 0) {
        battleInputButton = 0xFF;
    }
    do {
        if (getInputOverflowed()) {
            resetInputs();
        } else {
            while (loadInputFromVSyncBuffer()) {
                if (newPadButtonForDialogs & 0x20) {
                    battleInputButton = 4;
                    break;
                }
                if (newPadButtonForDialogs & 0x800) {
                    if (battleIsPaused == 0) {
                        pauseMusic();
                        uploadBuiltinTexture(0x88, 0x64);
                        uploadBuiltinTexture(0x88, 0x144);
                        playTime = g_playTimeInVsync;
                        battleIsPaused = 1;
                    } else {
                        resumeMusic();
                        g_playTimeInVsync = playTime;
                        battleIsPaused = 0;
                    }
                    break;
                }
            }
        }
        if (battleIsPaused != 0) {
            PORT_YIELD(); /* paused: see battleHandleInput */
        }
    } while (battleIsPaused != 0);
    if (battleVar1->mA0_drawResultCharacterWindows != 0 && battleResultCharWindows[0]->m15F9_isCountingExp != 0) {
        for (i = 0; i < 3; i++) {
            if (battleResultCharWindows[i]->m15FA_expCountDone == 0) {
                if (battleBssBlock.m9E20_experienceToAdd[i][0] == 0) {
                    battleResultCharWindows[i]->m15FA_expCountDone = 1;
                } else {
                    battleBssBlock.m9E20_experienceToAdd[i][0]--;
                    battleBssBlock.m9E08_shownExperience[i][0]++;
                }
            }
            if (battleResultCharWindows[i]->m15FB_etherExpCountDone == 0) {
                if (battleBssBlock.m9E20_experienceToAdd[i][1] == 0) {
                    battleResultCharWindows[i]->m15FB_etherExpCountDone = 1;
                } else {
                    battleBssBlock.m9E20_experienceToAdd[i][1]--;
                    battleBssBlock.m9E08_shownExperience[i][1]++;
                }
            }
            done &= battleResultCharWindows[i]->m15FA_expCountDone;
            if (battleResultCharWindows[0]->m15F8_showEther != 0) {
                done &= battleResultCharWindows[i]->m15FB_etherExpCountDone;
            }
        }
        if (done == 0) {
            setupResultExpValues();
            setupResultExpGainedValues();
        } else {
            battleResultCharWindows[0]->m15F9_isCountingExp = 0;
        }
    }
}

/* 72 263 260] */
void battleUpdateInputs(s32 arg0) {
    switch (currentBattleMode) {
    case 0:
        battleUpdateInputs_mode0(arg0);
        break;
    case 1:
        battleUpdateInputs_mode1(arg0);
        break;
    case 2:
        battleUpdateInputs_mode2(arg0);
        break;
    }
}

void battleSoundEffect(u8 id) {
    playSoundEffect((gBattleCurrentSoundEffectSeq->u14.m14_id << 16) | id);
}

void battleSoundEffect2(u8 id) {
    if (startCharacterJumpToEnemyVar0 != 0) {
        battleSoundEffect(id);
    }
}

void printIntegerToStringBattle(u32 value) {
    s32 i;
    u32 divisor;

    divisor = 100000000;
    for (i = 0; i < 9; i++) {
        printIntegerToStringBattleBuffer[i] = value / divisor;
        value = value % divisor;
        divisor = divisor / 10;
    }
    for (i = 1; i < 9; i++) {
        if (printIntegerToStringBattleBuffer[i] != 0) {
            if (printIntegerToStringBattleBuffer[i - 1] == 0) {
                printIntegerToStringBattleBuffer[i - 1] = 0xFF;
            }
            return;
        }
        printIntegerToStringBattleBuffer[i - 1] = 0xFF;
    }
}

void setCurrentDir_20_0(void) {
    setCurrentDirectory(0x20, 0);
}

/* 72 263 260] */
void openMusicBattleFolder(void) {
    setCurrentDirectory(0x20, 2);
}

/* 72 263 260] */
void setFolderChiData(void) {
    setCurrentDirectory(0x20, 3);
}

/* 72 263 260] */
void* allocateBattleHeap(s32 size, s32 flags) {
    resetMemoryAllocStats(2, 0);
    return allocateHeap(size, flags);
}

/* 72 263 260] */
u16* allocateTextureRamForText(s32 n) {
    resetMemoryAllocStats(2, 0);
    return allocateHeap((n + 3) * 26, 0);
}

/* 72 263 260] */
void idleBattleDuringLoading(void) {
    while (isCDBusy()) {
        battleRenderDebugAndMain();
    }
}
