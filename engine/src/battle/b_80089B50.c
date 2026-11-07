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

int pcVersusRandom(int lo, int hi, int* out); /* port/src/game/battle/versus.c */
u16 getRandomValueInRange(s32 arg0, u16 arg1) {
    u16 lo = arg0;
    u16 hi = arg1;
    s32 range;

    if (lo == 0xFFFF) {
        return 0xFFFF;
    }
    if (hi == 0) {
        return 0;
    }
    if (lo == hi) {
        return lo;
    }
    {
        /* a versus battle's enemy turn: player 2's choice (port/src/game/battle/versus.c) */
        int v;
        if (pcVersusRandom(lo, hi, &v)) {
            return v;
        }
    }
    range = hi - lo;
    if (range >= 0xFFFF) {
        return rand() & 0xFFFF;
    }
    return lo + R3000_MOD(rand() & 0xFFFF, range + 1);
}

/* 72 263 260] */
u16 characterIdToReverseBitmask(u8 a) {
    return bitmaskCharacterReverse[a];
}

/* 72 263 260] */
u16 characterIdToTargetBitmask(u8 a) {
    return bitmaskCharacter[a];
}

/* 72 263 260] */
u16 characterIdToInverseReverseBitmask(u8 a) {
    return ~bitmaskCharacterReverse[a];
}

/* 72 263 260] */
u16 characterOdToInverseTargetBitmask(u8 a) {
    return ~bitmaskCharacter[a];
}

/* 72 263 260] */
u16 bitmaskCharacterCheckReverse(u16 flags, u8 i) {
    u16 r;
    if (i < 16) {
        r = bitmaskCharacterReverse[i] & flags;
    } else {
        r = 0;
    }
    return r;
}

/* 72 263 260] */
u16 bitmaskCharacterCheck(u16 flags, u8 i) {
    return (i < 16) ? (bitmaskCharacter[i] & flags) : 0;
}

void battleHandleInput(u8 arg0) {
    u8 button;
#if !VERSION_JP
    u8 waiting;
    u8 paused;
#endif
    s32 playTime;
    u16 pad;

    button = 8;
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
            PORT_YIELD(); /* pad unplugged: wait for the vsync interrupt to see it again */
        }
    } while (waiting);
#endif
    do {
        if (getInputOverflowed()) {
            resetInputs();
        } else {
            while (loadInputFromVSyncBuffer()) {
                if (battleRunningVar1[0] != 0 || battleVar2->m2DB_turnEnded != 0) {
                    button = 0xFF;
                    break;
                }
                if (battleIsPaused != 0) {
                    if (*pRunningOnDTL != -1 && (newPadButtonForField & 4) && (newPadButtonForField & 8)) {
                        battleRunningVar1[0] = 1;
                        resumeMusic();
                        g_playTimeInVsync = playTime;
                        battleIsPaused = 0;
                        break;
                    }
                } else {
                    pad = newPadButtonForField;
                    if (pad & 0x2000) {
                        battleSoundEffect2(0x4C);
                        button = 0;
                        previousNewBattleInputButton[0] = previousNewBattleInputButton[1];
                        previousNewBattleInputButton[1] = button;
                        break;
                    }
                    if (pad & 0x4000) {
                        battleSoundEffect2(0x4C);
                        button = 1;
                        previousNewBattleInputButton[0] = previousNewBattleInputButton[1];
                        previousNewBattleInputButton[1] = button;
                        break;
                    }
                    if (pad & 0x8000) {
                        battleSoundEffect2(0x4C);
                        button = 2;
                        previousNewBattleInputButton[0] = previousNewBattleInputButton[1];
                        previousNewBattleInputButton[1] = button;
                        break;
                    }
                    if (pad & 0x1000) {
                        battleSoundEffect2(0x4C);
                        button = 3;
                        previousNewBattleInputButton[0] = previousNewBattleInputButton[1];
                        previousNewBattleInputButton[1] = button;
                        break;
                    }
                    pad = newPadButtonForDialogs;
                    if (pad & 0x20) {
                        button = 4;
                        battleSoundEffect2(0x4D);
                        break;
                    }
                    if (pad & 0x40) {
                        button = 5;
                        battleSoundEffect2(0x4E);
                        break;
                    }
                    if (pad & 0x80) {
                        button = 6;
                        battleSoundEffect2(0x4D);
                        break;
                    }
                    if (pad & 0x10) {
                        button = 7;
                        battleSoundEffect2(0x4D);
                        break;
                    }
                    if (pad & 1) {
#if VERSION_JP
                        battleSoundEffect2(0x4D);
#endif
                        button = 0xC;
                        if (*pRunningOnDTL != -1) {
                            apConfigArray[0].m0_storedAP = 0x1C;
                            apConfigArray[1].m0_storedAP = 0x1C;
                            apConfigArray[2].m0_storedAP = 0x1C;
                            battleEntities.m0_entities[0].m148_gearMode = 4;
                            battleEntities.m0_entities[1].m148_gearMode = 4;
                            battleEntities.m0_entities[2].m148_gearMode = 4;
                            battleEntities.m0_entities[0].m15C_statusTimers[6] = 0xFF;
                            battleEntities.m0_entities[1].m15C_statusTimers[6] = 0xFF;
                            battleEntities.m0_entities[2].m15C_statusTimers[6] = 0xFF;
                        }
                        break;
                    }
                    if (pad & 2) {
                        if (*pRunningOnDTL != -1) {
#if VERSION_JP
                            battleSoundEffect2(0x4D);
#endif
                            if (++battleDebugDisplay >= 5) {
                                battleDebugDisplay = 0;
                            }
                            button = 0xB;
                            if (battleDebugFontInitialized == 0) {
                                setDebugFontBuffer(0x80200000);
                                initDebugFont(0x10, 0x10, 0x140, 0x100, 0x3E8, 0, 0x340, 0, 0x340, 0x20, 0);
                                battleDebugFontInitialized++;
                            }
                        }
                        break;
                    }
                    if (pad & 0x100) {
                        button = 0xD;
                        break;
                    }
                }
                if (newPadButtonForDialogs & 0x800) {
                    button = 0xE;
                    if (battleTimeEnabled != 0) {
                        if (battleIsPaused == 0) {
                            uploadBuiltinTexture(0x88, 0x64);
                            uploadBuiltinTexture(0x88, 0x144);
                            pauseMusic();
                            playTime = g_playTimeInVsync;
                            battleIsPaused = 1;
                        } else {
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
            /* Paused: the PS1 spins here until the vsync interrupt brings a
             * Start press; nothing above is an SDK call, so offer the PC's
             * interrupts a safe point (without it the pads, the picture and
             * the sound all stop for good). */
            PORT_YIELD();
        }
    } while (battleIsPaused != 0);
    battleInputButton = button;
}

void battleChiMenuTextScrollTick(void) {
    battleVar1->mAA_clutScrollTimer++;
    if (battleVar1->mAA_clutScrollTimer & 1) {
        /* the CLUT buffers scroll by words (battleVar1->m30) */
        LoadImage(&battleVar0->m8950_clutRects[0], &((u32*)battleVar0->m8970_cluts[0])[battleVar1->m30_clutScroll]);
        LoadImage(&battleVar0->m8950_clutRects[1], &((u32*)battleVar0->m8970_cluts[1])[battleVar1->m30_clutScroll]);
        LoadImage(&battleVar0->m8950_clutRects[2], &((u32*)battleVar0->m8970_cluts[2])[battleVar1->m30_clutScroll]);
        LoadImage(&battleVar0->m8950_clutRects[3], &((u32*)battleVar0->m8970_cluts[3])[battleVar1->m30_clutScroll]);
        battleVar1->m30_clutScroll += 4;
        if (battleVar1->m30_clutScroll >= 0xC7) {
            battleVar1->m30_clutScroll = 0;
        }
    }
}
