/*
 * Battle main loop and UI rendering (Noah battle.cpp: loadBattleEventOverlay,
 * battleMain, battleRenderDebugAndMain, the time bars and HP displays).
 * 0x80070E2C-0x800769E8; gcc-2.6.3-psx, ASPSX 2.34.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "battle/battle.h"
#include "pc_gameplay.h"
#include "pc_bestiary.h"
#include "main/game_state.h"

s32 battlePreviewLoading(void);
/* The Battle arena's versus battles (port/src/game/battle/versus.c) */
void pcVersusBattleSetup(void);
void pcVersusMonsterTurn(u8 entity, s32 status);
void pcVersusBattleOver(void);
void pcVersusBattleEnd(void);

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
extern sBattleItemList battleEffectBF;
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

/* Loads the battle event overlay (file 1 of the battle directory) to 0x801E5000
 * and starts it. Noah: loadBattleEventOverlay */
void loadBattleEventOverlay(void) {
    if (needBattleEventOverlay) {
        setCurrentDir_20_0();
        /* The battle event overlay is linked in, so no padding up to its link
         * address: token blocks keep the frees balanced. */
        battleEventHeapMarker = allocateBattleHeap(4, 1);
        battleEventOverlayHeap = allocateBattleHeap(4, 1);
        if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
            readFile(1, portOverlayScratch(0, getFileSizeAligned(1)), 0, 0x80);
        }
        portResetModule("battle_event");
        idleBattleDuringLoading();
        battleEventEntry();
    }
}

/* battle event overlay call if loaded (battleEvent_update) */
void updateBattleEventOverlay(u8 arg0) {
    if (needBattleEventOverlay) {
        battleEvent_update();
    }
}

/* event overlay update; restarts battle music when battleRunningVar1 == 0x81 */
void unloadBattleEventOverlay(void) {
    u8 r = 0;

    if (needBattleEventOverlay) {
        r = battleEventFree();
    }
    if (r == 0 && performAttackSub3_var0.m2_battleRunningVar1 == 0x81) {
        playBattleMusic(pCurrentBattleMusic, 0, 0xF0);
    }
}

void pcBattleSelectorFont(void);
void battleMain(void);
/* The extras' bestiary (include/pc_bestiary.h; none without the extras):
 * the enemy types this battle showed, and at its end (end 1) those the party
 * defeated in a battle it won, as processBattleRewards counts them (filled,
 * shown, down, not removed; no event-only battle). The type is
 * battleMonsterMapping's (a copied slot's m2_characterId is stale). */
static void pcBestiaryBattle(s32 end) {
    s32 i;
    s32 down;
    s32 type;

    if (pcBestiaryNote == NULL) {
        return;
    }
    for (i = 3; i < 11; i++) {
        if (!isBattleSlotFilled.m0_isBattleSlotFilled[i] || battleMonsterMapping[i] == 0) {
            continue;
        }
        type = battleMonsterMapping[i] - 1;
        down = battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000;
        if (battleVisualEntities[i].m3_hidden == 0 || down) {
            pcBestiaryNote(currentBattleConfig[0], type, 0);
        }
        if (end && down && battleVisualEntities[i].m3_hidden == 0 && unknownMonsterStatus0[i - 3].m3_removed == 0 &&
            isBattleAnEvent == 0 && menuReturnState0 == 0) {
            pcBestiaryNote(currentBattleConfig[0], type, 1);
        }
    }
}

void battleMain(void) {
    s32 i;
    s32 pcChained; /* a battle event (op 0x24) or the selector chained this battle */
    /* A lost battle with an event overlay but no event menu sets no result:
     * the PS1 switches on what its caller left in the register (s2 = -1 from
     * the field, 0x8007EBF0 from the kernel menu; PCSX-Redux), so no case. */
    u8 result = 0xFF;
    s32 mode = 1;
    u8 saved;
    void* a;
    void* b;

    battleVar0 = (sBattleVar0*)allocateBattleHeap(sizeof(sBattleVar0), 0);
    battleVar1 = (sBattleVar1*)allocateBattleHeap(0x10C, 0);
    battleVar2 = (sBattleVar2*)allocateBattleHeap(0x2F8, 0);
    bzero(battleVar0, sizeof(sBattleVar0));
    bzero(battleVar1, 0x10C);
    bzero(battleVar2, 0x2F8);
    pcChained = battleInitVar0 != 0;
    battleDebugDisplay = 0;
    newBattleInputButton2 = 0xFF;
    previousNewBattleInputButton[0] = 0xFF;
    startCharacterJumpToEnemyVar0 = 0;
    pCurrentBattleMusic = pMusic;
    if (battleInitVar0 != 0) {
        requestedBattleConfig = battleInitVar0 - 1;
        if (victoryFanfarePlayed) {
            victoryFanfarePlayed = 0;
            /* A chained battle after a won one where no music instance is
             * playing (the kernel menu's selector): the PS1 writes the fade
             * into low RAM through NULL (0x74..0x7B, unused there). */
            if (pMusic != NULL)
            playBattleMusic(pMusic, 0x7F, 0x3C);
        }
        if (battleInitVar1) {
            goto load;
        }
        battleInitVar0 = 0;
    }
    if (battleInitVar1) {
    load:
        setCurrentDirectory(0x10, 2);
        if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
            readFile(1, portOverlayScratch(0, getFileSizeAligned(1)), 0, 0x80);
            waitReadCompletion(0);
        }
        portResetModule("battle_debug_selector"); /* linked in */
        if (battleInitVar0 == 0) {
            pcBattleSelectorFont(); /* port/src/game/battle/debug.c */
            battleDebugSelector();
        } else {
            requestedBattleConfig = battleInitVar0 - 1;
            battleInitVar0 = 0;
        }
    }
    if (*pRunningOnDTL != -1) {
        setCurrentDirectory(0x10, 2);
        PORT_TODO("overlay load at its link address");
    }
    if (!battlePreviewLoading()) {
        pcVersusBattleSetup(); /* a versus battle: its checkpoint and formation */
    }
    if (!battlePreviewLoading()) {
        pcGauntletBattleSetup(); /* a gauntlet's fight: its configs, party and Gears (port/src/game/main/gauntlet.c) */
        pcRetryCheckpoint(); /* the retry setting's checkpoint (off by default); never an extras preview */
    }
    pcGameplayBattleStart(requestedBattleConfig, pcChained); /* random or scripted, for rewards */
    memmove(currentBattleConfig, battleConfigs[requestedBattleConfig], 0x20);
    startBattleLoader(battleTransitionEffect);
    checkWinConditions();
    currentBattleMode = 2;
    initBattleGraphics(battleConfigFile3);
    battleCreateFader(0, 2, 0xFF, 0xFF, 0xFF);
    if (!needBattleEventOverlay) {
        battleCreateFader(0x14, 2, 0, 0, 0);
    }
    if (battleInitVar1) {
        pCurrentBattleMusic = realStartBattleMusic(battleMusic, 0x7F, 0);
    }
    battleBssBlock.m0_battleLoadDataVar2Ter = battleMechaInitData = battleLoadDataVar2;
    init8920();
    makeBattleTimeProgress = 1;
    setCameraVisibleEntities(allPlayerCharacterBitmask);
    battleRenderDebugAndMain();
    setupStringsForMonsterNames();
    while (battleTimeEnabled == 0) {
        battleRenderDebugAndMain();
    }
    freeHeap(battleStartSeq);
    if (battleTransitionEffect != 4) {
        stopSequence(battleStartSeq);
    }
    unloadSequence(battleStartSeq);
    freeHeap(battleLoaderProbeBlock);
    freeHeap(battleLoaderMemoryBlock);
    /* An extras world retains this fully loaded native battle. Its owner
     * steps battleRender explicitly; it must never enter the gameplay loop. */
    if (battlePreviewLoading()) {
        currentBattleMode = 1;
        battleStartPartyFixups();
        return;
    }
    pcBestiaryBattle(0);
    loadBattleEventOverlay();
    updateBattleEventOverlay(1);
    if (isBattleAnEvent == 0) {
        currentBattleMode = 1;
    }
    battleStartPartyFixups();
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] != 0x7F) {
            battleStartUnlockedAbilities[i][0] = gameState.m16C0_abilities[battleCharacters[i]].m0_unlockedAbilitiesBitField[0];
            battleStartUnlockedAbilities[i][1] = gameState.m16C0_abilities[battleCharacters[i]].m0_unlockedAbilitiesBitField[1];
        }
    }
    while (performAttackSub3_var0.m2_battleRunningVar1 == 0 && isBattleAnEvent == 0) {
        if (battleTimeEnabled) {
            battleTickGameplay();
        }
        battleRenderDebugAndMain();
    }
    pcVersusBattleOver(); /* a versus battle: who won; an end without rewards or game over */
    /* A wipe that battle_boot turns into the game over (no battle event menu
     * makes it the story's outcome): the retry setting's choice. */
    if (performAttackSub3_var0.m2_battleRunningVar1 == 0x81 && !(needBattleEventOverlay && battleEventMenuEnabled)) {
        pcBattleRetryOffer();
    }
    if (!(performAttackSub3_var0.m2_battleRunningVar1 & 0xC0)) {
        result = 0;
    } else if (performAttackSub3_var0.m2_battleRunningVar1 & 0x40) {
        result = 1;
    } else if (!needBattleEventOverlay) {
        result = 2;
    } else if (battleEventMenuEnabled) {
        result = 3;
        menuReturnState0 = result;
        performAttackSub3_var0.m2_battleRunningVar1 = 1;
    }
    switch (result) {
    case 0:
        menuReturnState0 = 0;
        mode = 0;
    case 3:
        if (needBattleEventOverlay) {
            battleEventVar0->m394_vars[0] = 0xFF;
            if (battleEventEndBattleFlag || !isBattleAnEvent) {
                battleEventVar0->m800_isEventDone = 0;
                saved = performAttackSub3_var0.m2_battleRunningVar1;
                performAttackSub3_var0.m2_battleRunningVar1 = 0;
                for (i = 0; i < 3; i++) {
                    battleEventVar0->m394_vars[0x10 + i] = battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000;
                    battleEventVar0->m394_vars[0x10 + i] |= battleEntities.m0_entities[i].mA4_gear.m7C_statusFlags & 0x8000;
                }
                loadWdsDataIfNeeded();
                updateBattleEventOverlay(1);
                performAttackSub3_var0.m2_battleRunningVar1 = saved;
            }
        }
        break;
    case 1:
        menuReturnState0 = 2;
        mode = 2;
        break;
    case 2:
        menuReturnState0 = 1;
        mode = 1;
        break;
    }
    pcBestiaryBattle(1);
    clearBattleAnimationData();
    if (battleEventIsRunningFlag) {
        mode = 1;
    }
    setCurrentDirectory(0x10, 0);
    /* The battle result overlay is linked in: token blocks for the frees. */
    battleResultHeapMarker = (void*)allocateBattleHeap(4, 1);
    battleResultOverlayHeap = (void*)allocateBattleHeap(4, 1);
    if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
        readFile(4, portOverlayScratch(0, getFileSizeAligned(4)), 0, 0x80);
    }
    portResetModule("battle_result");
    playBattleEndAndReleaseEnemies(mode);
    while (battleTimeEnabled) {
        battleRenderDebugAndMain();
    }
    if (!needBattleEventOverlay && !(performAttackSub3_var0.m2_battleRunningVar1 & 0x40) && !(currentBattleConfig[1] & 8)) {
        battleCreateFader(0x40, 2, 0x40, 0x40, 0x40);
    }
    /* After the battle event's end (its chain, op 0x24, and its movie request
     * are set by now): a gauntlet's next fight or the run's end
     * (port/src/game/battle/gauntlet.c). */
    pcGauntletBattleOverHook();
    unloadBattleEventOverlay();
    finishBattle();
    freeHeap(battleResultHeapMarker);
    freeHeap(battleResultOverlayHeap);
    pcVersusBattleEnd(); /* a versus battle: everything back as the battle found it */
}

s32 battleRenderDebugAndMain(void) {
    if (*pRunningOnDTL != -1) {
        drawBattleDebugTextPages();
    }
    battleRender();
    return 0;
}

void battleTimeProgress(void) {
    s32 i;

    if (makeBattleTimeProgress) {
        for (i = 0; i < 11; i++) {
            if (isBattleSlotFilled.m0_isBattleSlotFilled[i] && !isEntityReadyForBattle[i]) {
                s32 speed = 1;
                if ((battleEntities.m0_entities[i].m0_base.m84_statusFlags2[0] | battleEntities.m0_entities[i].m0_base.m84_statusFlags2[1]) & 0x8000) {
                    speed = 2;
                }
                if (!(battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x1000) || !(battleSlotStatusVar0.m2C_battleSlotHalfSpeedToggle[i] ^= 1)) {
                    if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x2000) {
                        if (!(battleEntities.m0_entities[i].m15C_statusTimers[0] -= speed)) {
                            battleEntities.m0_entities[i].m15C_statusTimers[0] = 0;
                            battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] &= ~0x2000;
                        }
                    } else if (!(battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x80) && !(battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] & 0x1000)) {
                        if ((battleSlotStatusVar0.m16_numTicksBeforeReady[i] -= speed) <= 0) {
                            isEntityReadyForBattle[i] = 1;
                            battleSlotStatusVar0.m16_numTicksBeforeReady[i] = 0;
                        }
                    }
                }
            }
        }
    }
}

/* resets the current entity's ready flag and turn ticks (battleGetSlotStatusSub) */
void updateTicksEndOfTurn(void) {
    u8 t = battleVar2->m2D3_currentEntityTurn;

    if (isEntityReadyForBattle[t] != 0xFF) {
        isEntityReadyForBattle[t] = 0;
    }
    battleSlotStatusVar0.m16_numTicksBeforeReady[battleVar2->m2D3_currentEntityTurn] = battleGetSlotStatusSub(battleVar2->m2D3_currentEntityTurn, 0);
    battleSlotStatusVar0.m0_battleSlotStatusVar0[battleVar2->m2D3_currentEntityTurn] = battleSlotStatusVar0.m16_numTicksBeforeReady[battleVar2->m2D3_currentEntityTurn];
}

void loadMonsterAttackName(void) {
    s32 i;

    if (D_800D2CA4.mB_battleTickMain_var1 != 0 && (entitiesHitInCurrentAttackBF & performAttackSub3_var0.m0_performAttackSub3_var0) == 0) {
        u8* s = getDialogParamPointer(battleAttackNameStrings, D_800D2CA4.mB_battleTickMain_var1);
        previousEntityTurn.m8_nameLabels[7].m5E_currentMonsterNameStringWidth =
            renderString(s, previousEntityTurn.m8_nameLabels[7].m58_ramBufferFromMonsterName, 0x39, 1);
        LoadImage(&previousEntityTurn.m8_nameLabels[7].m50_monsterNameVRamRect, previousEntityTurn.m8_nameLabels[7].m58_ramBufferFromMonsterName);
        for (i = 3; i != 0; i--) {
            battleRenderDebugAndMain();
        }
    }
}

void resetBattleVar2_m2EB(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        battleVar2->m2EB_hpChanged[i] = 0;
    }
}

void copyBattleVar1_m7C(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        battleVar1->m7C_hpDisplayDirty[i] = battleVar2->m2EB_hpChanged[i];
    }
    battleRenderDebugAndMain();
}

void removeCurrentMonsterNameString(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        previousEntityTurn.m8_nameLabels[i].m5D_monsterNameIsDisplayed = 0;
    }
    battleVar1->mB0_isDialogWindowInitialized[5] = 0;
    battleVar1->mB0_isDialogWindowInitialized[4] = 0;
    battleRenderDebugAndMain();
}

void displayAndWaitMonsterAttackName(void) {
    s32 i;

    if (D_800D2CA4.mB_battleTickMain_var1 != 0 && (entitiesHitInCurrentAttackBF & performAttackSub3_var0.m0_performAttackSub3_var0) == 0) {
        processBattleAnimation_subType_minus6(7);
        battleVar2->m2DB_turnEnded = 0;
        for (i = 0x3B; i != 0; i--) {
            battleRenderDebugAndMain();
            if (battleInputButton != 8) {
                break;
            }
        }
        battleVar2->m2DB_turnEnded = 1;
        hideMonsterAttackName(7);
        battleRenderDebugAndMain();
    }
}

void battleTickMain(u8 arg0) {
    s32 i;
    u8* p;

    battleTickMain_var0 = 1;
    D_800D2CA4.mB_battleTickMain_var1 = 0;
    if (battleVar2->m2D3_currentEntityTurn == 0) {
        return;
    }
    battleTickMainSub0();
    resetBattleVar2_m2EB();
    makeBattleTimeProgress = 0;
    battleVar2->m2D3_currentEntityTurn--;
    battleTickMain_var2 = battleVar2->m2D3_currentEntityTurn;
    battleVar2->m2DA_indexInBattleVar48 = 0;
    battleVar2->m2DB_turnEnded = 0;
    for (i = 4, p = &D_800D2CA4.m0_battleTickMain_var3[4]; i >= 0; p--, i--) {
        *p = 0;
    }
    entitiesHitInCurrentAttackBF = 0;
    updateBattleEntityStatus(battleVar2->m2D3_currentEntityTurn);
    if (battleVar2->m2D3_currentEntityTurn >= 3) {
        if (arg0 == 0) {
            /* a versus battle: player 2 makes the script's choices (versus.c); else as below */
            pcVersusMonsterTurn(battleVar2->m2D3_currentEntityTurn, battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[2] & 0x2000);
            if (!(battleVisualEntities[battleVar2->m2D3_currentEntityTurn].m3_hidden & 0x80) &&
                !(battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m34_monsterFlags & 0x400)) {
                battleVar1->mB0_isDialogWindowInitialized[5] = 1;
                previousEntityTurn.m8_nameLabels[0].m5E_currentMonsterNameStringWidth = renderString(
                    getDialogParamPointer(currentBattleSpecificStrings, battleMonsterMapping[battleVar2->m2D3_currentEntityTurn]),
                    previousEntityTurn.m8_nameLabels[0].m58_ramBufferFromMonsterName, 0x39, 0);
                loadImageSync(&previousEntityTurn.m8_nameLabels[0].m50_monsterNameVRamRect, previousEntityTurn.m8_nameLabels[0].m58_ramBufferFromMonsterName);
                previousEntityTurn.m8_nameLabels[0].m5D_monsterNameIsDisplayed = 1;
            }
        }
        if ((battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[0] & 0x2080) == 0 &&
            (battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[2] & 0x1000) == 0) {
            monsterJumpToTarget(battleVar2->m2D3_currentEntityTurn);
            removeCurrentMonsterNameString();
            loadMonsterAttackName();
            displayAndWaitMonsterAttackName();
        }
        removeCurrentMonsterNameString();
        updateBattleEventOverlay(0);
    
    } else {
        for (i = 0; i < 8; i++) {
            unknownMonsterStatus0[i].m2_wasHit = 0;
        }
        setXYWH(&battleVar0->m63C8_portraitHighlight[D_800CCB04.m30_battleOddOrEven],
                battleVar2->m2D3_currentEntityTurn * 0x60 + 0x10 + partyMemberSpritesOffset[battlePartyLayoutType * 3 + battleVar2->m2D3_currentEntityTurn],
                8, 0x18, 0x18);
        battleVar0->m6414_highlightOddOrEven = D_800CCB04.m30_battleOddOrEven;
        battleVar0->m6415_drawHighlight = 1;
        if ((battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[0] & 0x2080) == 0 &&
            (battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[2] & 0x1000) == 0) {
            if ((battleEntities.m0_entities[battleVar2->m2D3_currentEntityTurn].m0_base.m7C_statusFlags[2] & 0x2000) == 0) {
                setupTurnRenderLoop(battleVar2->m2D3_currentEntityTurn);
            } else {
                performAutoPartyMemberTurn(battleVar2->m2D3_currentEntityTurn);
            }
            if (battleVar2->m2EA_notDefending) {
                battleNonDefendTurnCount++;
            }
            previousEntityTurn.m0_lastEntityTurn = battleVar2->m2D3_currentEntityTurn;
            battleVar1->m97_apMarkCount = 0;
            updateCharacterBlinkingTask(0);
        }
        loadMonsterAttackName();
        displayAndWaitMonsterAttackName();
        removeCurrentMonsterNameString();
        updateBattleEventOverlay(0);
        for (i = 0; i < 8; i++) {
            if (bitmaskCharacterCheck(entitiesHitInCurrentAttackBF, i + 3)) {
                unknownMonsterStatus0[i].m2_wasHit = 1;
            }
        }
        execMonsterScriptEndOfPlayerTurn();
    }
    copyBattleVar1_m7C();
    applyBattleEffectBF7();
    battleVar0->m6415_drawHighlight = 0;
    for (i = 0; i < 11; i++) {
        battleVar2->m0_entityMenus[i].m3C_currentTarget = getEntityToFace(i);
        battleVisualEntities[i].m6_direction = getDirectionBetween2BattleEntities(i, battleVar2->m0_entityMenus[i].m3C_currentTarget);
    }
    clearAnimationsEndOfTurn(computeFacingForJump(battleVar2->m2D3_currentEntityTurn));
    resetBattleVar2_m2EB();
    checkWinConditions();
    if (performAttackSub3_var0.m2_battleRunningVar1 == 0) {
        applyPassiveModifiersAtEndOfTurn(battleVar2->m2D3_currentEntityTurn);
    }
    checkWinConditions();
    copyBattleVar1_m7C();
    updateRunningAnimations();
    updateTicksEndOfTurn();
    makeBattleTimeProgress = 1;
}

void applyBattleEffectBF7(void) {
    s32 i;

    if (battleEffectBF.m0_flags & 7) {
        for (i = 0; i < 3; i++) {
            if (bitmaskCharacterCheck(battleEffectBF.m0_flags, i)) {
                isEntityReadyForBattle[i] = 0;
                battleSlotStatusVar0.m16_numTicksBeforeReady[i] = battleSlotStatusVar0.m0_battleSlotStatusVar0[i];
                battleVar1->m7C_hpDisplayDirty[i] = 1;
            }
        }
    }
    battleEffectBF.m0_flags = 0;
}

/* clears/restarts monster slots flagged in bBattleTickMode0 (restoreBattleEntityHpAndStatus + battleTickMain(1)) */
void battleTickRestoreFlaggedMonsters(void) {
    s32 i;
    s32 j;
    u32 k;
    u8* p; /* never initialised in retail either */

    for (i = 3; i < 11; i++) {
        if (bitmaskCharacterCheck(bBattleTickMode0, i)) {
            for (k = 0; k < 0x100; k++) {
                p[k] = 0;
            }
            for (j = 0; j < 32; j++) {
                battleCurrentDamages[j].m47_battleAnimationToPlay = 0xFF;
            }
            battleScriptContext[0].m0_attackType = 4;
            battleScriptContext[0].m4_valueLow = 0x17;
            restoreBattleEntityHpAndStatus(i);
            battleTickMain(1);
        }
    }
}

/* Noah: battleTickGameplay (picks the next ready entity in the random turn order) */
void battleTickGameplay(void) {
    s32 i;
    s32 t;
    u8 n;

    if (bBattleTickMode0 == 0) {
        if (bBattleTickMode1 != 0) {
            battleVar2->m2D3_currentEntityTurn = bBattleTickMode1;
            isEntityReadyForBattle[bBattleTickMode1 - 1] = 1;
            {
                u8 x = bBattleTickMode1;
                bBattleTickMode1 = 0;
                battleSlotStatusVar0.m16_numTicksBeforeReady[x - 1] = 0;
            }
        } else {
            battleVar2->m2D3_currentEntityTurn = 0;
            i = isBattleSlotFilled.mB_currentEntryInRandomTurnOrder;
            for (;;) {
                if (isEntityReadyForBattle[t = randomTurnOrder[i]] == 1) {
                    n = i + 1;
                    battleVar2->m2D3_currentEntityTurn = t + 1;
                    isBattleSlotFilled.mB_currentEntryInRandomTurnOrder = n;
                    if ((u8)(i + 1) == 11) {
                        isBattleSlotFilled.mB_currentEntryInRandomTurnOrder = 0;
                        break;
                    }
                }
                i++;
                if (i == 11) {
                    i = 0;
                }
                if (i == isBattleSlotFilled.mB_currentEntryInRandomTurnOrder) {
                    break;
                }
            }
        }
        battleTickMain(0);
    } else {
        battleTickRestoreFlaggedMonsters();
    }
}

/* Noah: checkWinConditions. Everything indexed by the entity number i
 * (unknownMonsterStatus0[i - 3]): loop.c builds retail's extra i - 3 register itself.
 * mask is an int (the C48 result is zero-extended before the and). */
void checkWinConditions(void) {
    s32 i;
    s32 mask;

    allPlayerCharacterBitmask = 0;
    for (i = 0; i < 3; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i]) {
            if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000) {
                if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000) {
                    battleEntities.m0_entities[i].m0_base.m4C_HP = 0;
                }
                isEntityReadyForBattle[i] = 0xFF;
            } else if (battleVisualEntities[i].m3_hidden == 0) {
                allPlayerCharacterBitmask |= characterIdToTargetBitmask(i);
            }
        }
    }
    for (i = 3; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i]) {
            if (battleVisualEntities[i].m4_isGear == 0) {
                if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000) {
                    if (bitmaskCharacterCheck(jumpAnimationActiveActorBF, i)) {
                        allPlayerCharacterBitmask |= characterIdToTargetBitmask(i);
                    } else {
                        battleEntities.m0_entities[i].m0_base.m4C_HP = 0;
                        isEntityReadyForBattle[i] = 0xFF;
                    }
                } else if (battleVisualEntities[i].m3_hidden == 0 && unknownMonsterStatus0[i - 3].m3_removed == 0) {
                    allPlayerCharacterBitmask |= characterIdToTargetBitmask(i);
                }
            } else {
                if (battleEntities.m0_entities[i].mA4_gear.m7C_statusFlags & 0xC000) {
                    if (bitmaskCharacterCheck(jumpAnimationActiveActorBF, i)) {
                        allPlayerCharacterBitmask |= characterIdToTargetBitmask(i);
                    } else {
                        battleEntities.m0_entities[i].mA4_gear.m60_hp = 0;
                        isEntityReadyForBattle[i] = 0xFF;
                    }
                } else if (battleVisualEntities[i].m3_hidden == 0 && unknownMonsterStatus0[i - 3].m3_removed == 0) {
                    allPlayerCharacterBitmask |= characterIdToTargetBitmask(i);
                }
            }
        }
    }
    if ((allPlayerCharacterBitmask & 0x7F8) == 0) {
        performAttackSub3_var0.m2_battleRunningVar1 = 1;
    }
    if ((allPlayerCharacterBitmask & 7) == 0) {
        performAttackSub3_var0.m2_battleRunningVar1 = 0x81;
    }
    if (performAttackSub3_var0.m2_battleRunningVar1 == 0) {
        mask = allPlayerCharacterBitmask;
        for (i = 0; i < 11; i++) {
            if (bitmaskCharacterCheck(mask, i)) {
                if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] & 0x1000) {
                    mask &= characterOdToInverseTargetBitmask(i);
                }
            }
        }
        if (mask == 0) {
            for (i = 0; i < 11; i++) {
                if (bitmaskCharacterCheck(allPlayerCharacterBitmask, i)) {
                    if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] & 0x1000) {
                        battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] &= ~0x1000;
                        return;
                    }
                }
            }
        }
    }
}
