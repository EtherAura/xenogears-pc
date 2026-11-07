/*
 * Item list and command ring menus, battle dialog windows (Noah battle.cpp,
 * battleDialogWindow.cpp). 0x8008B478-0x800941A4; gcc-2.6.3-psx, ASPSX 2.34.
 */
#include "common.h"
#include "psyq.h"
#include "battle/battle.h"

#include "main/memory.h"
#include "main/dialog.h"
#include "main/game_state.h"

extern void battleRenderDebugAndMain(void);
extern void freeBattleVar0_A230(void);
extern void removeTargetSelectionCursors(void);
extern u8 currentBattleMode; /* currentBattleMode */
extern u16 party1C_InitialValues[16]; /* party1C_InitialValues */
extern sBattle800CEF10 mechaBattleStats[3] PORT_ALIAS; /* mechaBattleStats */
extern u8 healWhileOnGear;
extern u8 selectedItemRow;
extern u16 abilityTargetBitmask;
extern u8 abilitySelectedTarget;
extern u16 entitiesHitInCurrentAttackBF; /* entitiesHitInCurrentAttackBF */
extern u16 performAttack_type;
extern sBattleItemList battleEffectBF;
extern u8 startCharacterJumpToEnemyVar0;
extern u8 selectedItemColumn;
extern s32 computeFacingForJump(u8 entity);
extern void handleMenuSelectEnemy_cancel_sub0(u8 entity);
extern void executeAbilitySub0(u16 mask, u8 entity);
extern void performAttackSub2(void);
extern void performAttackSub6(u8 index);
extern u16 bitmaskCharacterCheck(u16 mask, u8 i);
extern void updateMonsterScriptEntitiesVarByAtttack(u8 entity, u8 target);
extern void initJumpData(u8 entity, u8 target);
extern void startCharacterJumpToEnemy(u8 entity);
extern void moveEntityToOtherEntity(u8 entity, u8 target);
extern u8 battleInputButton; /* battle menu input (1 down, 3 up, 4 confirm, 5 cancel) */
extern void initBattleG3Array(void);
extern void setupGearSpecialMenu(u8 entity, u8* ids, u16* costs);
extern void updateGearSpecialMenuCursor(u8 entity, u8 cursor);
extern void drawCircleMenuChi_updateSub4(s32 x, s32 y, s32* a, u8* b);
extern s32 battleMenuScrollOffset; /* item list scroll offset (pixels) */
extern u8 battleMenuScrollRequest; /* item list scroll request */
extern void drawCircleMenuChi_updateSub0(u8 entity);
extern void drawCircleMenuChi_updateSub1(u8 entity, u8 x, u8 row);
extern void drawCircleMenuChi_updateSub2(s32 offset);
extern void drawCircleMenuChi_updateSub3(s32 offset);
extern u8 useAbility(u8 entity, u8 x, u8 row);
extern void drawCircleMenuChi_updateSub5(u8 state);
extern void setupItemMenu(u8 entity, u8 mode, u8 first);
extern void updateItemMenuCursor(u8 x, u8 row, u8 mode);
extern void drawItemMenuScrollBar(s32 row);
extern void scrollItemMenuList(s32 offset);
extern u8 battlePartyCharacterIds[]; /* main: party slot -> character */
extern u8 comboSelectedSlots[7];
extern sBattle800cdd40 partyBattleStats[3] PORT_ALIAS; /* partyBattleStats */
extern void setupComboMenu(u8 entity, u8* moves, u8* costs);
extern void updateComboMenuApDisplay(u8 entity, u8 ap);
extern u16* battleMenuGraphicConfigs2[19]; /* layouts: {id, x, y, flags} * n, 0xFFFF-terminated */
extern sBattleCommandSet* battleMenuGraphicConfigs[];
extern u16 bitmaskCharacterCheckReverse(u16 a, u8 b);
extern u8 battleAmmoItemIds[];
extern sBattle800D2204 D_800D2204[];
extern u8 battleAmmoItemCounts[];
extern s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
extern void drawBattleDialogWindow_sub0Sub(POLY_FT4* p);
extern void* allocateBattleHeap(s32 size, s32 flags);
extern void setupBattleDialogWindow(u8 idx);
extern u8* battleItemNameStrings; /* item name strings */
extern u8* chiData; /* chi/ether ability name + description strings */
extern u8* gearCommandStrings; /* gear command name + description strings */
extern u16* blankTextImage; /* blank text image (clears VRAM text areas) */
void loadItemTargetsLabel(void);
void initBattleVar0_A230(void);
void setupComboMenuMoveGlyphs(u8 entity, u8* moves);
extern u8 comboMenuMoveGlyphs[16];  /* combo menu: glyph per command slot */
extern s32 comboMenuMoveGlyphX[16]; /* combo menu: glyph x per slot */
extern s32 comboMenuMoveGlyphY[16]; /* combo menu: glyph y per slot */
u16* allocateTextureRamForText(s32 n);
void loadImageSync(RECT* rect, void* data);
void repositionTextRenderingPlanePrim(POLY_FT4* p, u16 x, s16 y, u8 u, u8 v, u8 width);
extern u8 printIntegerToStringBattleBuffer[9];
void setupMenuPolyFT4(POLY_FT4* p, s16 x, s16 y, u8 u, u8 v, s32 w, s32 h);
void drawBattleDialogWindow_sub0(u8 idx, u16 x, u16 y, u16 w, u16 h);
void drawBattleDialogWindow_sub1(u8 idx, u16 x, u16 y, u16 w);
void drawBattleDialogWindow_sub2(u8 idx, u16 x, u16 y, u16 w, u16 h);
void drawBattleDialogWindow_sub3(u8 idx, u16 x, u16 y, u16 h);
void drawBattleDialogWindow_sub4(u8 idx, u16 x, u16 y, u16 w, u16 h);
void drawBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h);
u8 useGearSpecial(u8 entity, u8 slot);
void itemMenuHideWindows(u8 arg0);
void itemCancelSelection(u8 arg0);
void battleCleanupCommandMenu(u8 arg0);
void comboCancelSelection(u8 arg0);
void makeAllBattleCommandsDisabled(void);
void gearSpecialMenuHideWindows(u8 arg0);
void gearSpecialCancelSelection(u8 arg0);
u8 drawCircleMenuComboSelect(u8 entity);
u8 useItemInSlot(u8 entity, u8 col, u8 row, u8 mode);
u8 useCombo(u8 entity);
void renderItemMenuItemName(u8 x, u8 row);
void setItemMenuHighlight(u8 x, u8 row);
void setItemMenuTargetIcons(u8 x, u8 row);
void drawCircleMenuChi_updateSub0Sub3Sub0(u8 entity);
void setChiMenuTargetIcons(u8 entity, u8 x, u8 row);
void renderChiMenuDescription(u8 entity, u8 x, u8 row);
void setGearSpecialMenuIcons(u8 entity, u8 slot);
void renderGearSpecialDescription(u8 entity, u8 slot);
extern void freeBillyBattleStructs(u8 entity);

/* item list menu (2x6 grid, scrolls 16 items; jtbl_80070314) */
u8 drawCircleMenuChi_update(u8 entity) {
    s32 count;
    u8 flag;
    u8 x;
    u8 row;
    u8 prevX;
    u8 prevRow;
    s32 prevOffset;
    u8 state;
    s32 target;

    x = 0;
    row = 0;
    prevX = 0xFF;
    prevRow = 0xFF;
    prevOffset = 0xFFFF;
    state = 2;
    count = 4;
    flag = 0;
    battleMenuScrollOffset = 0;
    battleMenuScrollRequest = 0;
    battleVar1->mCB_drawCommandRing = 0;
    allocateAndSetupBattleDialogWindow(0, 0x1C, 0xA0, 0xAC, 0x38, 0, 1);
    allocateAndSetupBattleDialogWindow(1, 0x20, 0x2C, 0x118, 0x70, 0, 1);
    allocateAndSetupBattleDialogWindow(2, 0xD0, 0xA4, 0x50, 0x18, 0, 1);
    allocateAndSetupBattleDialogWindow(3, 0xE6, 0xC0, 0x4C, 0x18, 0, 1);
    drawCircleMenuChi_updateSub0(entity);
    initBattleG3Array();
    while (state == 2) {
        if (x != prevX || row != prevRow) {
            drawCircleMenuChi_updateSub1(entity, x, row);
            prevX = x;
            prevRow = row;
        }
        if (battleMenuScrollOffset != prevOffset) {
            drawCircleMenuChi_updateSub2(battleMenuScrollOffset);
            drawCircleMenuChi_updateSub3(battleMenuScrollOffset);
            prevOffset = battleMenuScrollOffset;
        }
        drawCircleMenuChi_updateSub4((x % 2) * 0x80 + (x % 2) * 4 + 0x2A, (x / 2) * 16 + 0x38, &count, &flag);
        battleRenderDebugAndMain();
        switch (battleInputButton) {
        case 5:
            state = 0;
            break;
        case 4:
            if (useAbility(entity, x, row) != 0) {
                state = 1;
                battleVar2->m2E6_menuSelection = x + row * 2;
            }
            break;
        case 0:
            if (row * 2 + x != 15) {
                if (x + 1 == 12) {
                    battleMenuScrollRequest = 1;
                } else {
                    x++;
                }
            }
            break;
        case 2:
            if (row * 2 + x > 0) {
                if (x == 0 && row != 0) {
                    battleMenuScrollRequest = 2;
                } else {
                    x--;
                }
            }
            break;
        case 1:
            if (row * 2 + x < 14) {
                if (x + 2 >= 12) {
                    battleMenuScrollRequest = 3;
                } else {
                    x += 2;
                }
            }
            break;
        case 3:
            if (row * 2 + x >= 2) {
                if (x < 2 && row != 0) {
                    battleMenuScrollRequest = 4;
                } else {
                    x -= 2;
                }
            }
            break;
        }
        switch (battleMenuScrollRequest) {
        case 1:
            target = (row + 1) * 16;
            if (battleMenuScrollOffset >= target) {
                row++;
                x--;
                battleMenuScrollOffset = target;
                battleMenuScrollRequest = 0;
            }
            break;
        case 2:
            target = (row - 1) * 16;
            if (battleMenuScrollOffset < target) {
                row--;
                x++;
                battleMenuScrollOffset = target;
                battleMenuScrollRequest = 0;
            }
            break;
        case 3:
            target = (row + 1) * 16;
            if (battleMenuScrollOffset >= target) {
                row++;
                battleMenuScrollOffset = target;
                battleMenuScrollRequest = 0;
            }
            break;
        case 4:
            target = (row - 1) * 16;
            if (battleMenuScrollOffset < target) {
                row--;
                battleMenuScrollOffset = target;
                battleMenuScrollRequest = 0;
            }
            break;
        }
    }
    drawCircleMenuChi_updateSub5(state);
    freeBattleVar0_A230();
    removeTargetSelectionCursors();
    clearBattleDialogWindow(0);
    clearBattleDialogWindow(1);
    clearBattleDialogWindow(2);
    clearBattleDialogWindow(3);
    return state;
}

/* uses the selected item (Noah: useItem, not ported): damages queue, inventory decrement, waits m2DB */
void executeItem(u8 entity) {
    s32 i;
    u16 mask;

    startCharacterJumpToEnemyVar0 = 0;
    updateCharacterBlinkingTask(0);
    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
    }
    startJumpAnimation(1, entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget, computeFacingForJump(entity));
    if (D_800D2204[battleVar2->m2E6_menuSelection].m0_targetFlags & 7) {
        mask = abilityTargetBitmask;
    } else {
        mask = characterIdToTargetBitmask(abilitySelectedTarget);
    }
    executeAbilitySub0(mask, entity);
    performAttackSub2();
    performItemOnTargets(entity, mask, battleVar2->m2E6_menuSelection);
    if (!(battleEffectBF.m0_flags & 0x8000)) {
        if (--battleEffectBF.m12_counts[selectedItemRow * 2 + selectedItemColumn] == 0) {
            battleEffectBF.m42_ids[selectedItemRow * 2 + selectedItemColumn] = 0;
        }
    }
    healWhileOnGear = 1;
    performAttackSub6(battleVar2->m2DA_indexInBattleVar48);
    healWhileOnGear = 0;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF5;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = performAttack_type;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = entitiesHitInCurrentAttackBF;
    for (i = 3; i < 11; i++) {
        if (bitmaskCharacterCheck(entitiesHitInCurrentAttackBF, i) != 0) {
            updateMonsterScriptEntitiesVarByAtttack(entity, i);
        }
    }
    battleVar2->m2DA_indexInBattleVar48++;
    handleMenuSelectEnemy_cancel_sub0(entity);
    while (battleVar2->m2DB_turnEnded == 0) {
        battleRenderDebugAndMain();
    }
    for (i = 0; i < 3; i++) {
        if (bitmaskCharacterCheck(mask, i) != 0) {
            battleVar1->m7C_hpDisplayDirty[i] = 1;
        }
    }
}

/* closes 2-window menu (Noah drawCircleMenuChi_updateSub5 shape) */
void itemMenuHideWindows(u8 arg0) {
    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 0;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = 0;
    battleVar1->mB7_openMenuType = 0;
    if (arg0 == 0) {
        battleVar1->mCB_drawCommandRing = 1;
    }
}

/* reopens menu B7=2 (Noah abilityCancelSelection shape) */
void itemCancelSelection(u8 arg0) {
    u16 temp;

    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 1;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = 1;
    battleVar1->mB7_openMenuType = 2;
    temp = characterIdToTargetBitmask(arg0);
    setCameraVisibleEntities((temp | characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget)) & 0xFFFF);
    updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget) & 0xFFFF);
}

/* u8 return; uses item/ability slot (row,col): mode 0 = targeted command, else consumable */
u8 useItemInSlot(u8 entity, u8 col, u8 row, u8 mode) {
    u16 id;
    u8 item;
    u8 result;

    id = D_800D2204[battleEffectBF.m42_ids[row * 2 + col]].m0_targetFlags;
    result = 0;
    if (mode == 0) {
        item = battleEffectBF.m42_ids[row * 2 + col];
    } else {
        item = battleAmmoItemIds[row * 2 + col];
    }
    if (item != 0) {
        if (mode == 0) {
            itemMenuHideWindows(1);
            battleVar1->mC6_isTargetSelectionCursorVisible = 1;
            if (abilitySelectTarget(id, entity, 0) != 0) {
                result = 1;
            } else {
                battleVar1->mC6_isTargetSelectionCursorVisible = 0;
                itemCancelSelection(entity);
                return result;
            }
        } else {
            if (isBillyAmmoLoaded(item - 0x32) != 0) {
                loadBillyAmmo(item, 0); /* retail passes no slot; one of the gun's slots already holds this ammo */
                if (--battleAmmoItemCounts[row * 2 + col] == 0) {
                    battleAmmoItemIds[row * 2 + col] = 0;
                }
                result = 2;
            }
            return result;
        }
    } else {
        battleSoundEffect2(0x4F);
    }
    return result;
}

/* item selection menu (Noah: FUN_8008bed8; 2 columns, 48 items; jtbl_8007032C) */
u8 drawCircleMenuItem_update(u8 entity) {
    s32 count;
    u8 flag;
    u8 x;
    u8 row;
    u8 prevX;
    u8 prevRow;
    s32 prevOffset;
    u8 state;
    u8 pending;
    u8 mode;
    u8 first;

    x = 0;
    row = 0;
    prevRow = prevX = 0xFF;
    prevOffset = 0xFFFF;
    state = 2;
    pending = 1;
    mode = 0;
    first = 1;
    count = 4;
    flag = 0;
    battleMenuScrollOffset = 0;
    battleMenuScrollRequest = 0;
    battleVar1->mCB_drawCommandRing = 0;
    allocateAndSetupBattleDialogWindow(0, 0x20, 0x58, 0x118, 0x78, 0, 1);
    allocateAndSetupBattleDialogWindow(1, 0x10, 0x2C, 0x128, 0x28, 0, 1);
    initBattleG3Array();
    while (state == 2) {
        if (pending != 0) {
            setupItemMenu(entity, pending - 1, first);
            mode = pending - 1;
            pending = 0;
            first = 0;
        }
        if (x != prevX || row != prevRow) {
            updateItemMenuCursor(x, row, mode);
            prevX = x;
            prevRow = row;
        }
        if (battleMenuScrollOffset != prevOffset) {
            drawItemMenuScrollBar(battleMenuScrollOffset / 13 * 2);
            scrollItemMenuList(battleMenuScrollOffset);
            prevOffset = battleMenuScrollOffset;
        }
        drawCircleMenuChi_updateSub4((x % 2) * 0x80 | (x % 2) * 2 + 0x2A, (x / 2) * 13 + 0x63, &count, &flag);
        battleRenderDebugAndMain();
        switch (battleInputButton) {
        case 5:
            state = 0;
            break;
        case 4:
            switch (useItemInSlot(entity, x, row, mode)) {
            case 1:
                battleVar2->m2E6_menuSelection = battleEffectBF.m42_ids[row * 2 + x];
                selectedItemColumn = x;
                selectedItemRow = row;
                state = 1;
                break;
            case 2:
                pending = 2;
                break;
            }
            break;
        case 0:
            if (row * 2 + x != 0x2F) {
                if (x + 1 == 16) {
                    battleMenuScrollRequest = 1;
                } else {
                    x++;
                }
            }
            break;
        case 2:
            if (row * 2 + x > 0) {
                if (x == 0 && row != 0) {
                    battleMenuScrollRequest = 2;
                } else {
                    x--;
                }
            }
            break;
        case 1:
            if (row * 2 + x < 0x2E) {
                if (x + 2 >= 16) {
                    battleMenuScrollRequest = 3;
                } else {
                    x += 2;
                }
            }
            break;
        case 3:
            if (row * 2 + x >= 2) {
                if (x < 2 && row != 0) {
                    battleMenuScrollRequest = 4;
                } else {
                    x -= 2;
                }
            }
            break;
        }
        switch (battleMenuScrollRequest) {
        case 1:
            battleMenuScrollOffset = (row + 1) * 13;
            row++;
            x--;
            battleMenuScrollRequest = 0;
            break;
        case 2:
            battleMenuScrollOffset = (row - 1) * 13;
            row--;
            x++;
            battleMenuScrollRequest = 0;
            break;
        case 3:
            battleMenuScrollOffset = (row + 1) * 13;
            row++;
            battleMenuScrollRequest = 0;
            break;
        case 4:
            battleMenuScrollOffset = (row - 1) * 13;
            row--;
            battleMenuScrollRequest = 0;
            break;
        }
    }
    itemMenuHideWindows(state);
    freeBattleVar0_A230();
    removeTargetSelectionCursors();
    clearBattleDialogWindow(0);
    clearBattleDialogWindow(1);
    return state;
}

/* Noah battleCleanupCommandMenu */
void battleCleanupCommandMenu(u8 arg0) {
    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 0;
    battleVar1->mB7_openMenuType = 0;
    if (arg0 != 0) {
        battleVar1->mC6_isTargetSelectionCursorVisible = 0;
        clearBattleDialogWindow(0);
        clearBattleDialogWindow(1);
        battleRenderDebugAndMain();
        freeBattleVar0_A230();
        removeTargetSelectionCursors();
    } else {
        battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = 0;
    }
}

/* reopens menu B7=3 (abilityCancelSelection shape) */
void comboCancelSelection(u8 arg0) {
    u16 temp;

    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 1;
    battleVar1->mB7_openMenuType = 3;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = 1;
    temp = characterIdToTargetBitmask(arg0);
    setCameraVisibleEntities((temp | characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget)) & 0xFFFF);
    updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget) & 0xFFFF);
}

/* Noah useCombo */
u8 useCombo(u8 entity) {
    u8 result;
    s32 i;

    result = 1;
    if (drawCircleMenuComboSelect(entity) != 0) {
        stopBattleVibration(entity, 0x100);
        battleCleanupCommandMenu(1);
        battleVar1->mCB_drawCommandRing = result;
        result = 0;
    } else {
        stopBattleVibration(entity, 0x80);
        battleVar1->mAF_drawApCounter = 0;
        battleCleanupCommandMenu(1);
        startCharacterJumpToEnemyVar0 = 0;
        updateCharacterBlinkingTask(0);
        battleVar2->m0_entityMenus[entity].m3C_currentTarget = abilitySelectedTarget;
        initJumpData(entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
        battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 1;
        startCharacterJumpToEnemy(entity);
        if (battleEntities.m0_entities[battleVar2->m0_entityMenus[entity].m3C_currentTarget].m0_base.m34_monsterFlags & 0x800) {
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF3;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget);
            battleVar2->m2DA_indexInBattleVar48++;
        }
        for (i = 0; i < 7; i++) {
            if (battleVar2->m2C0_circleMenuCommandsNames[i + 4] != 0xFF) {
                battleVar2->m2DC_attackIndex = battleVar2->m2C0_circleMenuCommandsNames[i + 4] + 8;
                performAttackSub3(entity, characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget), battleVar2->m2DC_attackIndex - 1);
                performAttackSub6(battleVar2->m2DA_indexInBattleVar48);
                battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = battleVar2->m2DC_attackIndex - 1;
                battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
                battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = entitiesHitInCurrentAttackBF;
                battleVar2->m2DA_indexInBattleVar48++;
            }
        }
        handleMenuSelectEnemy_cancel_sub0(entity);
        moveEntityToOtherEntity(entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
        while (battleVar2->m2DB_turnEnded == 0) {
            battleRenderDebugAndMain();
        }
    }
    return result;
}

/* Noah drawCircleMenuComboSelect (jtbl_80070344); unk28 volatile is a matching hack, see its comment */
u8 drawCircleMenuComboSelect(u8 entity) {
    u8 moves[7];
    volatile u8 unk28[8]; /* only cleared; volatile keeps loop.c from reversing the clear loop (retail counts up) */
    u8 costs[7];
    u8 unk38[8];
    u8 used[7];
    s32 count;
    u8 flag;
    u8 state;
    u8 cursor;
    u8 selected;
    u8 refresh;
    u8 ap;
    s32 n;
    u8* p;
    s32 i;

    state = 0;
    cursor = 0;
    refresh = 1;
    selected = 0;
    ap = apConfigArray[entity].m0_storedAP;
    count = 4;
    flag = 0;
    battleVar1->mCB_drawCommandRing = 0;
    for (i = 0; i < 7; i++) {
        moves[i] = 0xFF;
        costs[i] = 0;
        battleVar2->m2C0_circleMenuCommandsNames[i + 4] = 0xFF;
#if !VERSION_JP
        comboSelectedSlots[i] = 0xFF;
#endif
    }
    for (i = 0; i < 8; i++) {
        unk28[i] = 0;
    }
    n = 0;
    for (i = 0; i < 7; i++) {
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battlePartyCharacterIds[entity]].m0_unlockedAbilitiesBitField[0], i) != 0) {
            moves[n] = i;
            costs[n] = partyBattleStats[entity].m0_commands[i + 7].m17_apCost;
            n++;
        }
    }
    allocateAndSetupBattleDialogWindow(0, 0x16, 0x5C, 0x122, 0x4C, 1, 1);
    allocateAndSetupBattleDialogWindow(1, 0x10, 0x2C, 0xE8, 0x2C, 1, 1);
    while (battleVar1->mBF_isDialogWindowInitialized3[0] == 0 || battleVar1->mBF_isDialogWindowInitialized3[1] == 0) {
        battleRenderDebugAndMain();
    }
    setupComboMenu(entity, moves, costs);
    initBattleG3Array();
    while (state == 0) {
        if (refresh) {
            updateComboMenuApDisplay(entity, ap);
            refresh = 0;
        }
        {
            s32 x = (cursor % 2) * 0x88;
            s32 dx = (cursor % 2) * 4 + 0x1E;

            drawCircleMenuChi_updateSub4(x + dx, (cursor / 2) * 16 + 0x64, &count, &flag);
        }
        battleRenderDebugAndMain();
        switch (battleInputButton) {
        case 5:
            if (selected == 0) {
                state = 2;
            } else {
                selected--;
                refresh = 1;
                ap += used[selected];
                battleVar2->m2C0_circleMenuCommandsNames[selected + 4] = 0xFF;
#if !VERSION_JP
                comboSelectedSlots[selected] = 0xFF;
#endif
            }
            break;
        case 4:
            if (cursor == 7) {
                if (battleVar2->m2C0_circleMenuCommandsNames[4] == 0xFF) {
                    state = 2;
                } else {
                    battleCleanupCommandMenu(0);
                    battleVar1->mC6_isTargetSelectionCursorVisible = 1;
                    if (abilitySelectTarget(0x1000, entity, 1) != 0) {
                        apConfigArray[entity].m0_storedAP = ap;
                        state = 1;
                    } else {
                        battleVar1->mC6_isTargetSelectionCursorVisible = 0;
                        comboCancelSelection(entity);
                    }
                }
            } else if (selected != 7 && moves[cursor] != 0xFF && ap >= costs[cursor]) {
                battleVar2->m2C0_circleMenuCommandsNames[selected + 4] = moves[cursor];
#if !VERSION_JP
                comboSelectedSlots[selected] = cursor;
#endif
                ap -= costs[cursor];
                used[selected] = costs[cursor];
                selected++;
                refresh = 1;
            }
            break;
        case 0:
            cursor++;
            if (cursor >= 8) {
                cursor--;
            }
            break;
        case 1:
            cursor += 2;
            if (cursor >= 8) {
                cursor -= 2;
            }
            break;
        case 2:
            if (cursor != 0) {
                cursor--;
            }
            break;
        case 3:
            if (cursor >= 2) {
                cursor -= 2;
            }
            break;
        case 6:
        case 7:
        case 9:
        case 10:
            cursor = 7;
            break;
        case 8:
            break;
        }
    }
    return state - 1;
}

/* closes 3-window menu */
void gearSpecialMenuHideWindows(u8 arg0) {
    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 0;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = battleVar1->mB0_isDialogWindowInitialized[2] = 0;
    battleVar1->mB7_openMenuType = 0;
    if (arg0 == 0) {
        battleVar1->mCB_drawCommandRing = 1;
    }
}

/* reopens menu B7=4 */
void gearSpecialCancelSelection(u8 arg0) {
    u16 temp;

    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 1;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] = battleVar1->mB0_isDialogWindowInitialized[2] = 1;
    battleVar1->mB7_openMenuType = 4;
    temp = characterIdToTargetBitmask(arg0);
    setCameraVisibleEntities((temp | characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget)) & 0xFFFF);
    updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget) & 0xFFFF);
}

/* u8 return; gear command: checks fuel cost + availability, then targets; returns 1 on use */
u8 useGearSpecial(u8 entity, u8 slot) {
    u16 mask[4];
    u8 result;
    u8 fail;
    u16 cost;
    u16 id;

    mask[0] = party1C_InitialValues[13];
    mask[1] = party1C_InitialValues[14];
    mask[2] = party1C_InitialValues[15];
    mask[3] = party1C_InitialValues[3];
    result = 0;
    fail = 1;
    cost = mechaBattleStats[entity].m0_commands[slot + 37].m24_fuelCost;
    id = mechaBattleStats[entity].m0_commands[slot + 37].m0_flags;

    if (battleEntities.m0_entities[entity].mA4_gear.m38_fuel >= cost
        && bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m1A_unlockedGearAbilities, slot) != 0
        && (battleEntities.m0_entities[entity].m0_base.m7A_commandEnabledBF & mask[slot]) == 0) {
        gearSpecialMenuHideWindows(1);
        battleVar1->mC6_isTargetSelectionCursorVisible = 1;
        if (abilitySelectTarget(id, entity, 0) != 0) {
            battleEntities.m0_entities[entity].mA4_gear.m38_fuel -= cost;
            result = 1;
        } else {
            battleVar1->mC6_isTargetSelectionCursorVisible = 0;
            gearSpecialCancelSelection(entity);
        }
        fail = 0;
    }
    if (fail) {
        battleSoundEffect2(0x4F);
    }
    return result;
}

/* gear command sub-menu (4 slots, fuel cost; returns 1 used, 0 cancelled) */
u8 drawCircleMenuGearSpecial_update(u8 entity) {
    u8 ids[4];
    u16 costs[4];
    u16 mask[4];
    s32 count;
    u8 flag;
    s32 cursor;
    s32 prev;
    u8 state;
    s32 i;

    cursor = 0;
    prev = 0xFF;
    state = 2;
    mask[0] = party1C_InitialValues[13];
    mask[1] = party1C_InitialValues[14];
    mask[2] = party1C_InitialValues[15];
    mask[3] = party1C_InitialValues[3];
    count = 4;
    flag = 0;
    for (i = 0; i < 4; i++) {
        ids[i] = 0xFF;
        costs[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m1A_unlockedGearAbilities, i) != 0
            && (battleEntities.m0_entities[entity].m0_base.m7A_commandEnabledBF & mask[i]) == 0) {
            ids[i] = battleEntities.m0_entities[entity].m0_base.mA0_gearNum * 4 + i;
            costs[i] = mechaBattleStats[entity].m0_commands[i + 37].m24_fuelCost;
        }
    }
    battleVar1->mCB_drawCommandRing = 0;
    allocateAndSetupBattleDialogWindow(0, 0x74, 0xA0, 0xAC, 0x38, 0, 1);
    allocateAndSetupBattleDialogWindow(1, 0x84, 0x4C, 0x9C, 0x50, 0, 1);
    allocateAndSetupBattleDialogWindow(2, 0x1C, 0xA0, 0x50, 0x18, 0, 1);
    setupGearSpecialMenu(entity, ids, costs);
    initBattleG3Array();
    while (state == 2) {
        if (cursor != prev) {
            updateGearSpecialMenuCursor(entity, cursor);
            prev = cursor;
        }
        drawCircleMenuChi_updateSub4(0x8C, cursor * 16 + 0x58, &count, &flag);
        battleRenderDebugAndMain();
        switch (battleInputButton) {
        case 5:
            state = 0;
            break;
        case 4:
            if (useGearSpecial(entity, cursor) != 0) {
                state = 1;
                battleVar2->m2E6_menuSelection = cursor + 0x10;
            }
            break;
        case 1:
            cursor++;
            if (cursor >= 4) {
                cursor = 3;
            }
            break;
        case 3:
            cursor--;
            if (cursor < 0) {
                cursor = 0;
            }
            break;
        }
    }
    gearSpecialMenuHideWindows(state);
    freeBattleVar0_A230();
    removeTargetSelectionCursors();
    clearBattleDialogWindow(0);
    clearBattleDialogWindow(1);
    clearBattleDialogWindow(2);
    return state;
}

/* dims the battleVar0->m641C command polys by battleVar2->m2E0 (clears battleVar1->mCB when none lit) */
void makeAllBattleCommandsDisabled(void) {
    u8 active;
    s32 i;
    s32 j;
    POLY_FT4* p;
    u8 r;

    active = 0;
    for (i = 0; i < 2; i++) {
        if (battleVar1->mD0_battleCommandLength[i] != 0) {
            for (j = 0; j < battleVar1->mD0_battleCommandLength[i] * 2; j += 2) {
                SetSemiTrans(&battleVar0->m641C_commandGlyphs[i][j + battleVar1->mA3_641COddOrEven], 1);
                SetShadeTex(&battleVar0->m641C_commandGlyphs[i][j + battleVar1->mA3_641COddOrEven], 0);
                battleVar0->m641C_commandGlyphs[i][j + battleVar1->mA3_641COddOrEven].tpage |= 0x20;
                r = battleVar0->m641C_commandGlyphs[i][j + battleVar1->mA3_641COddOrEven].r0;
                if (r != 0) {
                    active = 1;
                    (battleVar0->m641C_commandGlyphs[i] + (j + battleVar1->mA3_641COddOrEven))->r0 = r - battleVar2->m2E0_isBattleMenuDisabled * 16;
                    (battleVar0->m641C_commandGlyphs[i] + (j + battleVar1->mA3_641COddOrEven))->g0 = r - battleVar2->m2E0_isBattleMenuDisabled * 16;
                    (battleVar0->m641C_commandGlyphs[i] + (j + battleVar1->mA3_641COddOrEven))->b0 = r - battleVar2->m2E0_isBattleMenuDisabled * 16;
                }
            }
        }
    }
    if (!active) {
        battleVar1->mCB_drawCommandRing = 0;
    }
}

/* builds the command ring glyph polys (battleVar0->m641C) for a command set; dimOnly -> makeAllBattleCommandsDisabled */
s32 updateBattleMenuSpriteForMenu(u8 entity, u8 set, u8 dimOnly) {
    u16 page[2];
    u8 layout[2];
    s32 i;
    s32 j;
    s32 k;
    s32 start;
    s32 id;
    u8 color;

    if (dimOnly != 0) {
        makeAllBattleCommandsDisabled();
        return battleVar1->mA3_641COddOrEven;
    }
    for (i = 0; i < 2; i++) {
        page[i] = battleMenuGraphicConfigs[set]->m0_page[i];
        layout[i] = battleMenuGraphicConfigs[set]->m2_layout[i];
        battleVar1->mD0_battleCommandLength[i] = 0;
    }
    for (i = 0; i < 2; i++) {
        if (page[i] == 0xFF) {
            break;
        }
        j = 0;
        while ((id = battleMenuGraphicConfigs2[layout[i]][j]) != 0xFFFF) {
            if (id >= 0x4000) {
                battleVar2->m2C0_circleMenuCommandsNames[id & 3] = battleVar2->m0_entityMenus[entity].m0_circleMenuBattleCommandsMapping[id & 0xFF];
                id = battleVar2->m0_entityMenus[entity].m0_circleMenuBattleCommandsMapping[id & 0xFF];
                if (id == 0xFF) {
                    j += 4;
                    continue;
                }
            }
            start = battleVar1->mD0_battleCommandLength[page[i]] * 2;
            battleVar1->mD0_battleCommandLength[page[i]] += battleSetupStringInPolyFT4Large(id,
                (POLY_FT4 (*)[2])&battleVar0->m641C_commandGlyphs[page[i]][battleVar1->mD0_battleCommandLength[page[i]] * 2],
                battleMenuGraphicConfigs2[layout[i]][j + 1], battleMenuGraphicConfigs2[layout[i]][j + 2]);
            color = (battleMenuGraphicConfigs2[layout[i]][j + 3] & 0xFF) >> 1;
            if (battleMenuGraphicConfigs2[layout[i]][j + 3] >= 0x2000) {
                if (battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[battleMenuGraphicConfigs2[layout[i]][j + 3] & 0xF] != 0) {
                    color = 0x10;
                } else {
                    color = (battleMenuGraphicConfigs2[layout[i]][j + 3] & 0xF0) >> 1;
                }
            }
            if (color != 0) {
                for (k = start; k < battleVar1->mD0_battleCommandLength[page[i]] * 2; k += 2) {
                    SetSemiTrans(&battleVar0->m641C_commandGlyphs[page[i]][k + D_800CCB04.m30_battleOddOrEven], 1);
                    SetShadeTex(&battleVar0->m641C_commandGlyphs[page[i]][k + D_800CCB04.m30_battleOddOrEven], 0);
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->r0 = color;
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->g0 = color;
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->b0 = color;
                    battleVar0->m641C_commandGlyphs[page[i]][k + D_800CCB04.m30_battleOddOrEven].tpage |= 0x20;
                }
            } else {
                for (k = start; k < battleVar1->mD0_battleCommandLength[page[i]] * 2; k += 2) {
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->r0 = 0x80;
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->g0 = 0x80;
                    (battleVar0->m641C_commandGlyphs[page[i]] + (k + D_800CCB04.m30_battleOddOrEven))->b0 = 0x80;
                }
            }
            j += 4;
        }
    }
    return D_800CCB04.m30_battleOddOrEven;
}

/* Noah drawBattleDialogWindow_sub0 */
void drawBattleDialogWindow_sub0(u8 idx, u16 x, u16 y, u16 w, u16 h) {
    u8 sprite[4];
    sBattleDialogWindow* win = battleDialogWindows[idx];
    s32 i;

    if (currentBattleMode != 0) {
        sprite[0] = 0x4A;
        sprite[1] = 0x4C;
        sprite[2] = 0x4F;
        sprite[3] = 0x51;
    } else {
        sprite[0] = 0xF0;
        sprite[1] = 0xF2;
        sprite[2] = 0xF5;
        sprite[3] = 0xF7;
    }
    win->m5A0_cornerCount = 0;
    win->m5A0_cornerCount += battleSetupStringInPolyFT4Large(sprite[0], &win->m0_corners[win->m5A0_cornerCount], x, y);
    win->m5A0_cornerCount += battleSetupStringInPolyFT4Large(sprite[1], &win->m0_corners[win->m5A0_cornerCount], x + w - 8, y);
    win->m5A0_cornerCount += battleSetupStringInPolyFT4Large(sprite[2], &win->m0_corners[win->m5A0_cornerCount], x, y + h - 8);
    win->m5A0_cornerCount += battleSetupStringInPolyFT4Large(sprite[3], &win->m0_corners[win->m5A0_cornerCount], x + w - 8, y + h - 8);
    for (i = 0; i < 4; i++) {
        drawBattleDialogWindow_sub0Sub(&win->m0_corners[0][0] + (i * 2 + D_800CCB04.m30_battleOddOrEven));
    }
}

/* Noah drawBattleDialogWindow_sub1 */
void drawBattleDialogWindow_sub1(u8 idx, u16 x, u16 y, u16 w) {
    sBattleDialogWindow* win = battleDialogWindows[idx];
    s32 i;

    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->x0 = x + 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->y0 = y - 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8 + (w - 0x10) / 2;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->y1 = y - 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->x2 = x + 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8 + (w - 0x10) / 2;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->x0 = x + 8 + (w - 0x10) / 2;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->y0 = y - 8;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8 + (w - 0x10) / 2 + (w - 0x10) / 2;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->y1 = y - 8;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->x2 = x + 8 + (w - 0x10) / 2;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8 + (w - 0x10) / 2 + (w - 0x10) / 2;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->u0 = (battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1) + 7;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->u2 = (battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y + 0x10;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1) + 7;
    (win->m140_topEdge[0] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y + 0x10;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->u0 = (battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1) + 7;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->u2 = (battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y + 0x10;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) << 1) + 7;
    (win->m140_topEdge[1] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[1].m14_tpage_Y + 0x10;
    for (i = 0; i < 2; i++) {
        drawBattleDialogWindow_sub0Sub(&win->m140_topEdge[0][0] + (i * 2 + D_800CCB04.m30_battleOddOrEven));
    }
}

/* Noah drawBattleDialogWindow_sub2 */
void drawBattleDialogWindow_sub2(u8 idx, u16 x, u16 y, u16 w, u16 h) {
    sBattleDialogWindow* win = battleDialogWindows[idx];
    s32 i;

    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->x0 = x + 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->y0 = y + h - 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->y1 = y + h - 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->x2 = x + 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->y2 = y + h + 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->y3 = y + h + 8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->x0 = x + 8 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->y0 = y + h - 8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8 + (w - 0x10) / 2 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->y1 = y + h - 8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->x2 = x + 8 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->y2 = y + h + 8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8 + (w - 0x10) / 2 + (w - 0x10) / 2;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->y3 = y + h + 8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xF8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xFF;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xF8;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y + 0x10;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xFF;
    (win->m1E0_bottomEdge[0] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y + 0x10;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xF8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xFF;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xF8;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y + 0x10;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) << 1) + 0xFF;
    (win->m1E0_bottomEdge[1] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[2].m14_tpage_Y + 0x10;
    for (i = 0; i < 2; i++) {
        drawBattleDialogWindow_sub0Sub(&win->m1E0_bottomEdge[0][0] + (i * 2 + D_800CCB04.m30_battleOddOrEven));
    }
}

/* Noah drawBattleDialogWindow_sub3 */
void drawBattleDialogWindow_sub3(u8 idx, u16 x, u16 y, u16 h) {
    sBattleDialogWindow* win = battleDialogWindows[idx];
    s32 i;

    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->x0 = x - 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->y0 = y + 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->y1 = y + 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->x2 = x - 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8 + (h - 0x10) / 2;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8 + (h - 0x10) / 2;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->x0 = x - 8;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->y0 = y + 8 + (h - 0x10) / 2;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->x1 = x + 8;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->y1 = y + 8 + (h - 0x10) / 2;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->x2 = x - 8;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8 + (h - 0x10) / 2 + (h - 0x10) / 2;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->x3 = x + 8;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8 + (h - 0x10) / 2 + (h - 0x10) / 2;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y + 7;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m280_leftEdge[0] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y + 7;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y + 7;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m280_leftEdge[1] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[3].m14_tpage_Y + 7;
    for (i = 0; i < 2; i++) {
        drawBattleDialogWindow_sub0Sub(&win->m280_leftEdge[0][0] + (i * 2 + D_800CCB04.m30_battleOddOrEven));
    }
}

/* Noah drawBattleDialogWindow_sub4 */
void drawBattleDialogWindow_sub4(u8 idx, u16 x, u16 y, u16 w, u16 h) {
    sBattleDialogWindow* win = battleDialogWindows[idx];
    s32 i;

    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->x0 = x + w - 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->y0 = y + 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->x1 = x + w + 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->y1 = y + 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->x2 = x + w - 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8 + (h - 0x10) / 2;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->x3 = x + w + 8;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8 + (h - 0x10) / 2;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->x0 = x + w - 8;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->y0 = y + 8 + (h - 0x10) / 2;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->x1 = x + w + 8;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->y1 = y + 8 + (h - 0x10) / 2;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->x2 = x + w - 8;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->y2 = y + 8 + (h - 0x10) / 2 + (h - 0x10) / 2;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->x3 = x + w + 8;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->y3 = y + 8 + (h - 0x10) / 2 + (h - 0x10) / 2;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y + 7;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m320_rightEdge[0] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y + 7;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->u0 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->v0 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->u1 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->v1 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->u2 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0xE;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->v2 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y + 7;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->u3 = ((battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) << 1) + 0x1E;
    (win->m320_rightEdge[1] + D_800CCB04.m30_battleOddOrEven)->v3 = battleVar0->mA234_glyphTextures[4].m14_tpage_Y + 7;
    for (i = 0; i < 2; i++) {
        drawBattleDialogWindow_sub0Sub(&win->m320_rightEdge[0][0] + (i * 2 + D_800CCB04.m30_battleOddOrEven));
    }
}

/* Noah drawBattleDialogWindow */
void drawBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h) {
    sBattleDialogWindow* win = battleDialogWindows[idx];

    battleVar1->mB0_isDialogWindowInitialized[idx] = 0;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->x0 = x;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->y0 = y;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->x1 = x + w;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->y1 = y;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->x2 = x;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->y2 = y + h;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->x3 = x + w;
    (win->m3C0_polyG4 + D_800CCB04.m30_battleOddOrEven)->y3 = y + h;
    drawBattleDialogWindow_sub0(idx, x, y, w, h);
    drawBattleDialogWindow_sub1(idx, x, y, w);
    drawBattleDialogWindow_sub2(idx, x, y, w, h);
    drawBattleDialogWindow_sub3(idx, x, y, h);
    drawBattleDialogWindow_sub4(idx, x, y, w, h);
    win->m5A4_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->mB0_isDialogWindowInitialized[idx] = 1;
}

/* Noah allocateAndSetupBattleDialogWindow */
void allocateAndSetupBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h, u8 deferred, u8 render) {
    sBattleDialogWindow2* win2;

    if (battleVar1->mB0_isDialogWindowInitialized[idx] == 0) {
        battleDialogWindows[idx] = allocateBattleHeap(sizeof(sBattleDialogWindow), 0);
        bzero(battleDialogWindows[idx], sizeof(sBattleDialogWindow));
        battleDialogWindows2[idx] = allocateBattleHeap(sizeof(sBattleDialogWindow2), 0);
        bzero(battleDialogWindows2[idx], sizeof(sBattleDialogWindow2));
        setupBattleDialogWindow(idx);
    }
    if (deferred != 0) {
        win2 = battleDialogWindows2[idx];
        win2->mC_windowIndex = idx;
        win2->m0_x = x;
        win2->m2_y = y;
        win2->m4_width = w;
        win2->m6_height = h;
        win2->m8_currentWidth = 0;
        win2->mA_currentHeight = 0;
        battleVar1->mBF_isDialogWindowInitialized3[idx] = 0;
        battleVar1->mB8_isDialogWindowInitialized2[idx] = 1;
    } else {
        drawBattleDialogWindow(idx, x, y, w, h);
        if (render != 0) {
            battleRenderDebugAndMain();
        }
    }
}

/* Noah clearBattleDialogWindow */
void clearBattleDialogWindow(u8 arg0) {
    battleVar1->mB0_isDialogWindowInitialized[arg0] = 0;
    battleVar1->mB8_isDialogWindowInitialized2[arg0] = 0;
    battleRenderDebugAndMain();
    freeHeap(battleDialogWindows[arg0]);
    freeHeap(battleDialogWindows2[arg0]);
}

/* Noah drawBattleDialogWindows */
void drawBattleDialogWindows(void) {
    s32 i;
    u8 done;
    u16 v;
    sBattleDialogWindow2* win2;

    for (i = 0; i < 7; i++) {
        win2 = battleDialogWindows2[i];
        if (battleVar1->mB8_isDialogWindowInitialized2[i] != 0 && battleVar1->mBF_isDialogWindowInitialized3[i] == 0) {
            done = 0;
            if (win2->m8_currentWidth + 0x20 >= win2->m4_width) {
                v = win2->m4_width;
                done = 1;
            } else {
                v = win2->m8_currentWidth + 0x20;
            }
            win2->m8_currentWidth = v;
            if (win2->mA_currentHeight + 0x20 >= win2->m6_height) {
                v = win2->m6_height;
                done++;
            } else {
                v = win2->mA_currentHeight + 0x20;
            }
            win2->mA_currentHeight = v;
            if (done == 2) {
                battleVar1->mBF_isDialogWindowInitialized3[i] = 1;
            }
            drawBattleDialogWindow(win2->mC_windowIndex, win2->m0_x + (win2->m4_width >> 1) - (win2->m8_currentWidth >> 1),
                          win2->m2_y + (win2->m6_height >> 1) - (win2->mA_currentHeight >> 1), win2->m8_currentWidth, win2->mA_currentHeight);
        }
    }
}

void drawCircleMenuChi_updateSub0Sub0(s32 x, s32 y, s32 x2, s32 y2, s32 n);
extern s32 initMenuTiles6Sub(void* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, u16 scale,
                             u8 flipX, u8 flipY);
extern void* battleFont; /* battleFont */

/* Noah: drawCircleMenuChi_updateSub0Sub0 (glyph 0x65 rows, 0x64 cap, title).
 * The coordinates are s16 locals initialised from the int parameters:
 * that is what puts retail's i = 0 ahead of the parameter copies. */
void drawCircleMenuChi_updateSub0Sub0(s32 arg0, s32 arg1, s32 arg2, s32 y2, s32 n) {
    s16 x = arg0;
    s16 y = arg1;
    s16 x2 = arg2;
    u8 count = n;
    s32 i;

    battleVar1->mFC_countOF1E68 = 0;
    for (i = 0; i < count; i++) {
        battleVar1->mFC_countOF1E68 += battleSetupStringInPolyFT4Large(0x65, &battleVar0->m1E68_menuGlyphs2[battleVar1->mFC_countOF1E68], x, y2 + i * 8);
    }
    battleVar1->mFC_countOF1E68 += battleSetupStringInPolyFT4Large(0x64, &battleVar0->m1E68_menuGlyphs2[battleVar1->mFC_countOF1E68], x, y);
    battleVar1->mFC_countOF1E68 += initMenuTiles6Sub(battleFont, 0x64, &battleVar0->m1E68_menuGlyphs2[battleVar1->mFC_countOF1E68],
                                                     D_800CCB04.m30_battleOddOrEven, x, x2, 0x1000, 0, 1);
    battleVar1->mA6_1E68OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9D_render1E68 = 1;
}

/* drawCircleMenuChi_updateSub0Sub0 with the fixed layout */
void drawItemMenuScrollTrack(void) {
    drawCircleMenuChi_updateSub0Sub0(0x20, 0x5C, 0xCC, 0x60, 0xE);
}

/* item menu (drawCircleMenuItem_update) setup: uploads the 32 item names and two-digit counts of both item pages to VRAM */
void setupItemMenu(u8 entity, u8 mode, u8 first) {
    u16* images[32];
    RECT rectName;
    RECT rectTens0;
    RECT rectOnes0;
    RECT rectTens1;
    RECT rectOnes1;
    RECT rectBlank;
    RECT unused;
    u8 counts[48];
    u8 ids[48];
    s32 i;
    u16** p;
    s32 off;
    u16* buf;
    s32 t;
    u16* img;

    if (first) {
        initBattleVar0_A230();
        loadItemTargetsLabel();
        drawCircleMenuChi_updateSub0Sub0(0x20, 0x5C, 0xCC, 0x60, 0xE);
    }
    blankTextImage = allocateTextureRamForText(0x39);
    bzero(blankTextImage, 0x618);
    rectBlank.x = 0x3C0;
    rectBlank.y = 0;
    rectBlank.w = 0x3C;
    rectBlank.h = 0xD;
    loadImageSync(&rectBlank, blankTextImage);
    for (i = 0; i < 48; i++) {
        ids[i] = battleEffectBF.m42_ids[i];
        counts[i] = battleEffectBF.m12_counts[i];
    }
    for (i = 0; i < 32; i++) {
        buf = allocateTextureRamForText(0x1B);
        off = i * (s32)sizeof(u16*);
        *(p = (u16**)((u8*)images + off)) = buf;
        bzero(*p, 0x30C);
        rectName.x = (i % 2) * 30 + 0x380;
        rectName.y = (i / 2) * 13 + 0x100;
        rectName.w = 0x1E;
        rectName.h = 0xD;
        rectTens0.x = (i % 2) * 16 + 0x3C0;
        rectTens0.y = (i / 2) * 13 + 0x100;
        rectTens0.w = 6;
        rectTens0.h = 0xD;
        rectOnes0.x = (i % 2) * 16 + 0x3C2;
        rectOnes0.y = (i / 2) * 13 + 0x100;
        rectOnes0.w = 6;
        rectOnes0.h = 0xD;
        rectTens1.x = (i % 2) * 16 + 0x3C4;
        rectTens1.y = (i / 2) * 13 + 0x100;
        rectTens1.w = 6;
        rectTens1.h = 0xD;
        rectOnes1.x = (i % 2) * 16 + 0x3C6;
        rectOnes1.y = (i / 2) * 13 + 0x100;
        rectOnes1.w = 6;
        rectOnes1.h = 0xD;
        if (ids[i] != 0) {
            renderString(getUsableItemName(ids[i]), *p, 0x1B, 0);
        }
        if (ids[i + 16] != 0) {
            renderString(getUsableItemName(ids[i + 16]), *p, 0x1B, 1);
        }
        loadImageSync(&rectName, *p);
        if ((u8)(counts[i] / 10) != 0) {
            img = *(u16**)((u8*)itemLabelsIds + (t = (u8)(counts[i] / 10) * (s32)sizeof(sBattleTextImage)));
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectTens0, img);
        if (ids[i] != 0) {
            img = itemLabelsIds[(u8)(counts[i] % 10)].m0_image;
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectOnes0, img);
        if ((u8)(battleEffectBF.m12_counts[i + 16] / 10) != 0) {
            img = *(u16**)((u8*)itemLabelsIds + (t = (u8)(counts[i + 16] / 10) * (s32)sizeof(sBattleTextImage)));
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectTens1, img);
        if (ids[i + 16] != 0) {
            img = itemLabelsIds[(u8)(counts[i + 16] % 10)].m0_image;
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectOnes1, img);
    }
    p = images;
    for (i = 0; i < 32; i++) {
        freeHeap(p[i]);
    }
    freeHeap(blankTextImage);
    battleRenderDebugAndMain();
    battleVar0->mA230_menuPanel->m669_drawEPCost = 1;
    battleVar1->mB7_openMenuType = 2;
}

void drawItemMenuScrollBar(s32 row) {
    s32 i;
    s32 y;

    battleVar1->mF8_countOfBA8 = 0;
    for (i = 0, y = 0x68; i < 9; i++) {
        battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(0x66, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], 0x20, y - 4 + row);
        y += 8;
    }
    battleVar1->mA5_ba8OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9C_renderBA8 = 1;
}

/* item menu: highlight plates for slot (x, row) (battleVar0->mA230 polys 4 and 5; second page from slot 17) */
void setItemMenuHighlight(u8 x, u8 row) {
    s32 idx;
    s32 u;

    idx = row * 2 + x;
    u = 0;
    if (idx > 0x10) {
        idx -= 0x10;
        battleVar0->mA230_menuPanel->m0_polys[4][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode1;
        u = 0x10;
    } else {
        battleVar0->mA230_menuPanel->m0_polys[4][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[4][D_800CCB04.m30_battleOddOrEven], 0x18, 0x33, (idx % 2) * 0x78, (idx / 2) * 13, 0x60);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[5][D_800CCB04.m30_battleOddOrEven], 0x84, 0x33, (idx % 2) << 6 | u, (idx / 2) * 13, 0x10);
}

/* item menu: target-type and element icons for slot (x, row) (battleVar0->mA230 polys 10 and 11) */
void setItemMenuTargetIcons(u8 x, u8 row) {
    u16 flags;
    u8 target;
    u8 icon;

    flags = D_800D2204[battleEffectBF.m42_ids[row * 2 + x]].m0_targetFlags;
    if (flags & 0x4000) {
        target = 9;
    } else if (flags & 0x1000) {
        target = 7;
    } else {
        target = 8;
    }
    switch (flags & 0xF) {
    case 0:
        icon = 0xC;
        break;
    case 1:
        icon = 0xD;
        break;
    case 2:
        icon = 0x15;
        break;
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven], 0xA8, 0x33, battleLabelGlyphRects[target].m2_u,
                  battleLabelGlyphRects[target].m3_v, battleLabelGlyphRects[target].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[target].m1_clut ? textSpriteMode1 : textSpriteMode0;
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven], 0xD0, 0x33, battleLabelGlyphRects[icon].m2_u,
                  battleLabelGlyphRects[icon].m3_v, battleLabelGlyphRects[icon].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[icon].m1_clut ? textSpriteMode1 : textSpriteMode0;
}

/* item menu: renders the name of item slot (x, row) into the item name plate (battleVar0->mA230 poly 8) */
void renderItemMenuItemName(u8 x, u8 row) {
    RECT rect;
    u8 id;
    u16* image;
    u8 width;

    id = battleEffectBF.m42_ids[row * 2 + x];
    image = allocateTextureRamForText(0x39);
    bzero(image, 0x618);
    width = renderString(getDialogParamPointer(battleItemNameStrings, id), image, 0x39, 0);
    rect.x = 0x3C0;
    rect.y = 0;
    rect.w = 0x3C;
    rect.h = 0xD;
    loadImageSync(&rect, image);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[8][D_800CCB04.m30_battleOddOrEven], 0x30, 0x42, 0, 0, width);
    freeHeap(image);
}

/* item menu (drawCircleMenuItem_update): cursor moved to (x, row); mode 0 = items, 1 = second list (battleAmmoItemIds) */
void updateItemMenuCursor(u8 x, u8 row, u8 mode) {
    u8 id;

    if (mode == 0) {
        id = battleEffectBF.m42_ids[row * 2 + x];
    } else {
        id = battleAmmoItemIds[row * 2 + x];
    }
    battleVar0->mA230_menuPanel->m66D_drawDescription = 0;
    setItemMenuHighlight(x, row);
    if (id != 0 && mode == 0) {
        renderItemMenuItemName(x, row);
        setItemMenuTargetIcons(x, row);
        battleVar0->mA230_menuPanel->m66D_drawDescription = 1;
    }
    battleVar0->mA230_menuPanel->m66A_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar0->mA230_menuPanel->m66B_drawSelection = 1;
}

/* item menu (drawCircleMenuItem_update): list text planes scrolled to offset (battleVar0->mA230 polys 0-3; second page from 0x68) */
void scrollItemMenuList(s32 offset) {
    s32 u;

    u = 0;
    if (offset >= 0x68) {
        offset -= 0x68;
        battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode1;
        battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode1;
        u = 0x10;
    } else {
        battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
        battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    }
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven], 0x30, 0x60, 0, offset, 0x60, 0x68);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven], 0xB4, 0x60, 0x78, offset, 0x60, 0x68);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[2][D_800CCB04.m30_battleOddOrEven], 0x98, 0x60, u, offset, 0x10, 0x68);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[3][D_800CCB04.m30_battleOddOrEven], 0x11C, 0x60, u | 0x40, offset, 0x10, 0x68);
    battleVar0->mA230_menuPanel->m668_oddOrEven = D_800CCB04.m30_battleOddOrEven;
}

/* animated selection cursor (frames 0xE0-0xE4, stepped every 3 calls) at (x, y) into battleVar0->m27C8 */
void drawCircleMenuChi_updateSub4(s32 x, s32 y, s32* frame, u8* timer) {
    if (++*timer >= 3) {
        if (--*frame < 0) {
            *frame = 4;
        }
        *timer = 0;
    }
    battleVar1->m100_countOf27C8 = battleSetupStringInPolyFT4Large(*frame + 0xE0, battleVar0->m27C8_selectionCursor, x, y);
    battleVar1->mA7_27C8OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9E_render27C8 = 1;
}

/* EP cost display: entity's MP / max MP as two-digit glyphs (battleVar0->mA230 polys 14-17) */
void drawCircleMenuChi_updateSub0Sub3Sub0(u8 entity) {
    u16 tens;

    tens = battleEntities.m0_entities[entity].m0_base.m50_MP / 10;
    if (tens != 0) {
        repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[14][D_800CCB04.m30_battleOddOrEven], 0x104, 0xC6, tens * 8 + 0x78, 0, 8);
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[15][D_800CCB04.m30_battleOddOrEven], 0x10C, 0xC6,
                  (u16)(battleEntities.m0_entities[entity].m0_base.m50_MP % 10) * 8 + 0x78, 0, 8);
    tens = battleEntities.m0_entities[entity].m0_base.m52_MaxMP / 10;
    if (tens != 0) {
        repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[16][D_800CCB04.m30_battleOddOrEven], 0x11C, 0xC6, tens * 8 + 0x78, 0, 8);
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[17][D_800CCB04.m30_battleOddOrEven], 0x124, 0xC6,
                  (u16)(battleEntities.m0_entities[entity].m0_base.m52_MaxMP % 10) * 8 + 0x78, 0, 8);
}

/* EP cost panel: "EP" labels (glyph table 6 and 5), MP digits, and the label glyph 0x71 */
void drawCircleMenuChi_updateSub0Sub3(u8 entity);
void drawCircleMenuChi_updateSub0Sub3(u8 entity) {
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[12][D_800CCB04.m30_battleOddOrEven], 0xA8, 0xA6, battleLabelGlyphRects[6].m2_u,
                  battleLabelGlyphRects[6].m3_v, battleLabelGlyphRects[6].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[12][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[6].m1_clut ? textSpriteMode1 : textSpriteMode0;
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[13][D_800CCB04.m30_battleOddOrEven], 0xEC, 0xC6, battleLabelGlyphRects[5].m2_u,
                  battleLabelGlyphRects[5].m3_v, battleLabelGlyphRects[5].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[13][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[5].m1_clut ? textSpriteMode1 : textSpriteMode0;
    drawCircleMenuChi_updateSub0Sub3Sub0(entity);
    battleSetupStringInPolyFT4Large(0x71, &battleVar0->mA230_menuPanel->m5A0_mpSlash, 0x118, 0xD1);
    battleVar0->mA230_menuPanel->m66C_epCostOddOrEven = D_800CCB04.m30_battleOddOrEven;
}

/* chi/ether menu (drawCircleMenuChi_update) setup: unlocked flags and ether costs of the 16 abilities, names + costs to VRAM */
void drawCircleMenuChi_updateSub0(u8 entity) {
    RECT rectName;
    RECT rectTens;
    RECT rectOnes;
    RECT rectClear;
    RECT rectClearCost;
    RECT rectBlank;
    u16* images[16];
    u8 unlocked[16];
    u8 costs[16];
    s32 i;
    u16* img;
    u16** p;
    s32 off;
    s32 off2;
    u16* buf;

    initBattleVar0_A230();
    loadItemTargetsLabel();
    drawCircleMenuChi_updateSub0Sub0(0x20, 0x30, 0x98, 0x38, 0xC);
    blankTextImage = allocateTextureRamForText(0x39);
    bzero(blankTextImage, 0x618);
    rectBlank.x = 0x3C0;
    rectBlank.y = 0;
    rectBlank.w = 0x3C;
    rectBlank.h = 0xD;
    loadImageSync(&rectBlank, blankTextImage);
    if (apConfigArray[entity].m1_gearState == 0) {
        for (i = 0; i < 16; i++) {
            if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[1], i) != 0) {
                unlocked[i] = 1;
                costs[i] = partyBattleStats[entity].m0_commands[i + 22].m13_cost;
            } else {
                unlocked[i] = 0;
                costs[i] = 0;
            }
        }
    } else {
        for (i = 0; i < 16; i++) {
            if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[3], i) != 0) {
                unlocked[i] = 1;
                costs[i] = mechaBattleStats[entity].m0_commands[i + 21].m13_cost;
            } else {
                unlocked[i] = 0;
                costs[i] = 0;
            }
        }
    }
    for (i = 0; i < 16; i++) {
        buf = allocateTextureRamForText(0x1B);
        off = i * (s32)sizeof(u16*);
        *(u16**)((u8*)images + off) = buf;
        bzero(buf, 0x30C);
        rectClear.x = (i % 2) * 30 + 0x380;
        rectClear.y = (i / 2) * 16 + 0x100;
        rectClear.w = 0x1B;
        rectClear.h = 0x10;
        loadImageSync(&rectClear, blankTextImage);
        if (unlocked[i] != 0) {
            if (apConfigArray[entity].m1_gearState == 0) {
                renderString(drawCircleMenuChi_updateSub0Sub1(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout * 16 + i), *(u16**)((u8*)images + off), 0x1B, 0);
            } else {
                renderString(drawCircleMenuChi_updateSub0Sub2(battleEntities.m0_entities[entity].m0_base.mA0_gearNum * 16 + i), *(u16**)((u8*)images + off), 0x1B, 0);
            }
            rectName.x = (i % 2) * 30 + 0x380;
            rectName.y = (i / 2) * 16 + 0x102;
            rectName.w = 0x1E;
            rectName.h = 0xD;
            off2 = i * (s32)sizeof(u16*);
            loadImageSync(&rectName, *(u16**)((u8*)images + off2));
        }
        if ((i & 1) == 0) {
            rectClearCost.x = 0x3C0;
            rectClearCost.y = (i / 2) * 16 + 0x100;
            rectClearCost.w = 0x1B;
            rectClearCost.h = 0x10;
            loadImageSync(&rectClearCost, blankTextImage);
        }
        rectTens.x = (i % 2) * 16 + 0x3C0;
        rectTens.y = (i / 2) * 16 + 0x102;
        rectTens.w = 6;
        rectTens.h = 0xD;
        if ((u8)(costs[i] / 10) != 0) {
            img = itemLabelsIds[(u8)(costs[i] / 10)].m0_image;
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectTens, img);
        rectOnes.x = (i % 2) * 16 + 0x3C2;
        rectOnes.y = (i / 2) * 16 + 0x102;
        rectOnes.w = 6;
        rectOnes.h = 0xD;
        if (unlocked[i] != 0) {
            img = itemLabelsIds[(u8)(costs[i] % 10)].m0_image;
        } else {
            img = blankTextImage;
        }
        loadImageSync(&rectOnes, img);
    }
    drawCircleMenuChi_updateSub0Sub3(entity);
    p = images;
    for (i = 0; i < 16; i++) {
        freeHeap(p[i]);
    }
    freeHeap(blankTextImage);
    battleRenderDebugAndMain();
    battleVar0->mA230_menuPanel->m0_polys[4][0].clut = textSpriteMode0;
    battleVar0->mA230_menuPanel->m0_polys[4][1].clut = textSpriteMode0;
    battleVar0->mA230_menuPanel->m669_drawEPCost = 1;
    battleVar1->mB7_openMenuType = 1;
}

/* item list (drawCircleMenuChi_update) scroll bar glyphs into battleVar0->mBA8 */
void drawCircleMenuChi_updateSub2(s32 offset) {
    s32 i;
    s32 y;

    battleVar1->mF8_countOfBA8 = 0;
    for (i = 0, y = 0x38; i < 8; i++) {
        battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(0x66, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], 0x20, y + offset);
        y += 8;
    }
    battleVar1->mA5_ba8OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9C_renderBA8 = 1;
}

/* chi/ether list: highlight plate for slot (x, row) and, for battleCharacters[entity] == 1, the ether cost digits */
void setChiMenuHighlight(u8 x, u8 row, u8 entity);
void setChiMenuHighlight(u8 x, u8 row, u8 entity) {
    s32 idx;
    s32 i;
    s32 px;

    idx = row * 2 + x;
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[4][D_800CCB04.m30_battleOddOrEven], 0x20, 0xA4, (idx % 2) * 0x78, (idx / 2) * 16, 0x60, 0x10);
    battleVar0->mA230_menuPanel->m66E_costDigitCount = 0;
    if (battleCharacters[entity] == 1) {
        printIntegerToStringBattle(getAbilityHitChance(entity, idx + 0x16));
        for (i = 0, px = 0x8C; i < 3; i++) {
            if (printIntegerToStringBattleBuffer[i + 6] != 0xFF) {
                repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[5][battleVar0->mA230_menuPanel->m66E_costDigitCount * 2 + D_800CCB04.m30_battleOddOrEven], px, 0xA6,
                              printIntegerToStringBattleBuffer[i + 6] * 8 + 0x78, 0, 8);
                battleVar0->mA230_menuPanel->m66E_costDigitCount++;
            }
            px += 8;
        }
    }
}

/* chi/ether list: target-type and element icons of the ability in slot (x, row) (party or gear stats) */
void setChiMenuTargetIcons(u8 entity, u8 x, u8 row) {
    u16 flags;
    u8 target;
    u8 icon;

    if (apConfigArray[entity].m1_gearState == 0) {
        flags = partyBattleStats[entity].m0_commands[row * 2 + x + 0x16].m0_flags;
    } else {
        flags = mechaBattleStats[entity].m0_commands[row * 2 + x + 0x15].m0_flags;
    }
    if (flags & 0x4000) {
        target = 9;
    } else if (flags & 0x1000) {
        target = 7;
    } else {
        target = 8;
    }
    switch (flags & 0xF) {
    case 0:
        icon = 0xC;
        break;
    case 1:
        icon = 0xD;
        break;
    case 2:
        icon = 0x15;
        break;
    default:
        icon = 0xC;
        break;
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven], 0xDA, 0xAA, battleLabelGlyphRects[target].m2_u,
                  battleLabelGlyphRects[target].m3_v, battleLabelGlyphRects[target].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[target].m1_clut ? textSpriteMode1 : textSpriteMode0;
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven], 0xFE, 0xAA, battleLabelGlyphRects[icon].m2_u,
                  battleLabelGlyphRects[icon].m3_v, battleLabelGlyphRects[icon].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[icon].m1_clut ? textSpriteMode1 : textSpriteMode0;
}

/* chi/ether list: name and description text of the ability in slot (x, row) (battleVar0->mA230 polys 8 and 9) */
void renderChiMenuDescription(u8 entity, u8 x, u8 row) {
    RECT rect;
    u16 v;
    s32 t;
    u16* image;
    u8 w0;
    u8 w1;

    if (apConfigArray[entity].m1_gearState == 0) {
        v = battleCharacters[entity] * 32 + (row * 2 + x) * 2;
    } else {
        v = gameState.m26C_party[battleCharacters[entity]].mA0_gearNum * 32 + (row * 2 + x) * 2;
    }
    image = allocateTextureRamForText(0x39);
    bzero(image, 0x618);
    t = v;
    w0 = renderString(getDialogParamPointer(chiData, t), image, 0x39, 0);
    w1 = renderString(getDialogParamPointer(chiData, t | 1), image, 0x39, 1);
    rect.x = 0x3C0;
    rect.y = 0;
    rect.w = 0x3C;
    rect.h = 0xD;
    loadImageSync(&rect, image);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[8][D_800CCB04.m30_battleOddOrEven], 0x30, 0xB6, 0, 0, w0);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[9][D_800CCB04.m30_battleOddOrEven], 0x30, 0xC6, 0, 0, w1);
    freeHeap(image);
}

/* chi/ether list: cursor moved to (x, row); draws the plates/cost/description when the slot is unlocked */
void drawCircleMenuChi_updateSub1(u8 entity, u8 x, u8 row) {
    u8 enabled;

    enabled = 0;
    if (apConfigArray[entity].m1_gearState == 0) {
        enabled = bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[1], x + row * 2) != 0;
    } else if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[3], x + row * 2) != 0) {
        enabled = 1;
    }
    if (enabled) {
        setChiMenuHighlight(x, row, entity);
        setChiMenuTargetIcons(entity, x, row);
        renderChiMenuDescription(entity, x, row);
        battleVar0->mA230_menuPanel->m66A_oddOrEven = D_800CCB04.m30_battleOddOrEven;
        battleVar0->mA230_menuPanel->m66B_drawSelection = 1;
    } else {
        battleVar0->mA230_menuPanel->m66B_drawSelection = 0;
    }
}

/* chi/ether list (drawCircleMenuChi_update): list text planes scrolled to offset (battleVar0->mA230 polys 0-3) */
void drawCircleMenuChi_updateSub3(s32 offset) {
    battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven], 0x34, 0x34, 0, offset, 0x60, 0x60);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven], 0xB8, 0x34, 0x78, offset, 0x60, 0x60);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[2][D_800CCB04.m30_battleOddOrEven], 0x9C, 0x34, 0, offset, 0x10, 0x60);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[3][D_800CCB04.m30_battleOddOrEven], 0x120, 0x34, 0x40, offset, 0x10, 0x60);
    battleVar0->mA230_menuPanel->m668_oddOrEven = D_800CCB04.m30_battleOddOrEven;
}

/* combo menu (drawCircleMenuComboSelect): combo list text planes (battleVar0->mA230 polys 0-3), menu state B7 = 3 */
void setupComboMenuListPolys(void);
void setupComboMenuListPolys(void) {
    battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven], 0x36, 0x62, 0, 0, 0x60, 0x40);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[1][D_800CCB04.m30_battleOddOrEven], 0xC2, 0x62, 0x78, 0, 0x60, 0x40);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[2][D_800CCB04.m30_battleOddOrEven], 0x96, 0x62, 0, 0, 0x10, 0x40);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[3][D_800CCB04.m30_battleOddOrEven], 0x122, 0x62, 0x40, 0, 0x10, 0x40);
    battleVar0->mA230_menuPanel->m668_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar0->mA230_menuPanel->m669_drawEPCost = 1;
    battleVar1->mB7_openMenuType = 3;
}

/* combo menu: resets the highlight polys (battleVar0->mA230 m5F0/m638) and draws the command glyphs of the chosen moves */
void setupComboMenuMoveGlyphs(u8 entity, u8* moves) {
    s32 i;
    s32 j;

    for (i = 0; i < 2; i++) {
        SetPolyG4(&battleVar0->mA230_menuPanel->m5F0_apBarFill[i]);
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->r0 = 0xFF;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->g0 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->b0 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->r1 = 0xFF;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->g1 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->b1 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->r2 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->g2 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->b2 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->r3 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->g3 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->b3 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->x0 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->y0 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->x1 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->y1 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->x2 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->y2 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->x3 = 0;
        (battleVar0->mA230_menuPanel->m5F0_apBarFill + i)->y3 = 0;
    }
    for (i = 0; i < 2; i++) {
        SetPolyF4(&battleVar0->mA230_menuPanel->m638_apBarEmpty[i]);
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->r0 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->g0 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->b0 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->x0 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->y0 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->x1 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->y1 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->x2 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->y2 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->x3 = 0;
        (battleVar0->mA230_menuPanel->m638_apBarEmpty + i)->y3 = 0;
    }
    battleVar1->mFC_countOF1E68 = 0;
    for (j = 0; j < 16; j++) {
        if (moves[j] != 0xFF) {
            battleVar1->mFC_countOF1E68 += battleSetupStringInPolyFT4Large(comboMenuMoveGlyphs[j], &battleVar0->m1E68_menuGlyphs2[battleVar1->mFC_countOF1E68],
                                                         comboMenuMoveGlyphX[j], comboMenuMoveGlyphY[j]);
        }
    }
    battleVar1->mA6_1E68OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9D_render1E68 = 1;
}

/* combo menu (drawCircleMenuComboSelect) setup: uploads the 7 move names + AP costs and the "end" entry to VRAM */
void setupComboMenu(u8 entity, u8* moves, u8* costs) {
    RECT rectName;
    RECT rectTens;
    RECT rectOnes;
    RECT rectClear;
    RECT rectClearCost;
    RECT rectBlank;
    u16* images[8];
    s32 i;
    s32 nameW;
    u16** p;
    s32 off;
    u16* buf;
    u8 tens;
    u16* img;

    initBattleVar0_A230();
    loadItemTargetsLabel();
    blankTextImage = allocateTextureRamForText(0x39);
    bzero(blankTextImage, 0x618);
    rectBlank.x = 0x3C0;
    rectBlank.y = 0;
    rectBlank.w = 0x3C;
    rectBlank.h = 0xD;
    loadImageSync(&rectBlank, blankTextImage);
    nameW = 0x1E;
    for (i = 0; i < 8; i++) {
        buf = allocateTextureRamForText(0x1B);
        off = i * (s32)sizeof(u16*);
        *(p = (u16**)((u8*)images + off)) = buf;
        bzero(*p, 0x30C);
        rectClear.x = (i % 2) * 30 + 0x380;
        rectClear.y = (i / 2) * 16 + 0x100;
        rectClear.w = 0x1B;
        rectClear.h = 0x10;
        loadImageSync(&rectClear, blankTextImage);
        if (i != 7) {
            if (moves[i] != 0xFF) {
                renderString(getDeathblowName(battleCharacters[entity], moves[i]), *p, 0x1B, 0);
                rectName.x = (i % 2) * 30 + 0x380;
                rectName.y = (i / 2) * 16 + 0x102;
                rectName.w = nameW;
                rectName.h = 0xD;
                loadImageSync(&rectName, *p);
            }
        } else {
            renderString(getItemLabelString(0xA), images[7], 0x1B, 0);
            rectName.x = 0x39E;
            rectName.y = 0x132;
            rectName.w = nameW;
            rectName.h = 0xD;
            loadImageSync(&rectName, images[7]);
        }
        if ((i & 1) == 0) {
            rectClearCost.x = 0x3C0;
            rectClearCost.y = (i / 2) * 16 + 0x100;
            rectClearCost.w = 0x1B;
            rectClearCost.h = 0x10;
            loadImageSync(&rectClearCost, blankTextImage);
        }
        rectTens.x = (i % 2) * 16 + 0x3C0;
        rectTens.y = (i / 2) * 16 + 0x102;
        rectTens.w = 6;
        rectTens.h = 0xD;
        if (i != 7) {
            tens = costs[i] / 10;
            if (tens != 0) {
                img = itemLabelsIds[tens].m0_image;
            } else {
                img = blankTextImage;
            }
            loadImageSync(&rectTens, img);
            rectOnes.x = (i % 2) * 16 + 0x3C2;
            rectOnes.y = (i / 2) * 16 + 0x102;
            rectOnes.w = 6;
            rectOnes.h = 0xD;
            if (moves[i] != 0xFF) {
                loadImageSync(&rectOnes, itemLabelsIds[(u8)(costs[i] % 10)].m0_image);
            } else {
                loadImageSync(&rectOnes, blankTextImage);
            }
        } else {
            loadImageSync(&rectTens, blankTextImage);
            rectOnes.x = 0x3D2;
            rectOnes.y = 0x132;
            rectOnes.w = 6;
            rectOnes.h = 0xD;
            loadImageSync(&rectOnes, blankTextImage);
        }
    }
    setupComboMenuMoveGlyphs(entity, moves);
    p = images;
    for (i = 0; i < 8; i++) {
        freeHeap(p[i]);
    }
    freeHeap(blankTextImage);
    setupComboMenuListPolys();
}

/* combo menu: remaining AP digits, the chosen combo sequence glyphs, and the AP bar (battleVar0->mA230 m5F0/m638) */
void updateComboMenuApDisplay(u8 entity, u8 ap) {
    u8 digits[2];
    s32 i;

    digits[0] = ap / 10;
    digits[1] = ap - (u8)(ap / 10) * 10;
    battleVar1->mF8_countOfBA8 = 0;
    if (digits[0] != 0) {
        battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(digits[0] + 0x67, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], 0x56, 0x38);
    }
    battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(digits[1] + 0x67, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], 0x5E, 0x38);
    for (i = 0; i < 7; i++) {
        if (battleVar2->m2C0_circleMenuCommandsNames[i + 4] == 0xFF) {
            break;
        }
#if VERSION_JP
        battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(battleVar2->m2C0_circleMenuCommandsNames[i + 4] + 0x39, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], i * 0x20 + 0x2A, 0x4A);
#else
        battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(comboSelectedSlots[i] + 0x39, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], i * 0x20 + 0x2A, 0x4A);
#endif
        if (i != 0) {
            battleVar1->mF8_countOfBA8 += battleSetupStringInPolyFT4Large(0xA, &battleVar0->mBA8_menuGlyphs[battleVar1->mF8_countOfBA8], (i - 1) * 0x20 + 0x3A, 0x4A);
        }
    }
    battleVar1->mA5_ba8OddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->m9C_renderBA8 = 1;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->x0 = 0x80;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->y0 = 0x34;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->x1 = ap * 4 + 0x80;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->y1 = 0x34;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->x2 = 0x80;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->y2 = 0x3C;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->x3 = ap * 4 + 0x80;
    (battleVar0->mA230_menuPanel->m5F0_apBarFill + D_800CCB04.m30_battleOddOrEven)->y3 = 0x3C;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->x0 = ap * 4 + 0x80;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->y0 = 0x34;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->x1 = 0xF0;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->y1 = 0x34;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->x2 = ap * 4 + 0x80;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->y2 = 0x3C;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->x3 = 0xF0;
    (battleVar0->mA230_menuPanel->m638_apBarEmpty + D_800CCB04.m30_battleOddOrEven)->y3 = 0x3C;
    battleVar0->mA230_menuPanel->m66C_epCostOddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar0->mA230_menuPanel->m66F_drawApBar = 1;
}

/* gear command sub-menu (drawCircleMenuGearSpecial_update) setup: uploads the 4 command names and 4-digit fuel costs to VRAM */
void setupGearSpecialMenu(u8 entity, u8* ids, u16* costs) {
    RECT rectName;
    RECT rectLast;
    RECT rectDigit;
    RECT rectClear;
    RECT rectClearCost;
    RECT rectBlank;
    u16* images[4];
    u16 div[3];
    s32 i;
    s32 j;
    u8 d;
    u8 started;
    u16** p;
    s32 off;
    u16* img;

    div[0] = 1000;
    div[1] = 100;
    div[2] = 10;
    initBattleVar0_A230();
    loadItemTargetsLabel();
    blankTextImage = allocateTextureRamForText(0x39);
    bzero(blankTextImage, 0x618);
    rectBlank.x = 0x3C0;
    rectBlank.y = 0;
    rectBlank.w = 0x3C;
    rectBlank.h = 0xD;
    loadImageSync(&rectBlank, blankTextImage);
    for (i = 0; i < 4; i++) {
        img = allocateTextureRamForText(0x1B);
        off = i * (s32)sizeof(u16*);
        *(u16**)((u8*)images + off) = img;
        bzero(img, 0x30C);
        rectClear.x = 0x380;
        rectClear.y = i * 16 + 0x100;
        rectClear.w = 0x1B;
        rectClear.h = 0x10;
        loadImageSync(&rectClear, blankTextImage);
        if (ids[i] != 0xFF) {
            renderString(getGearFuelAbilityName(battleEntities.m0_entities[entity].m0_base.mA0_gearNum * 4 + i), *(u16**)((u8*)images + off), 0x1B, 0);
            rectName.x = 0x380;
            rectName.y = i * 16 + 0x102;
            rectName.w = 0x1E;
            rectName.h = 0xD;
            loadImageSync(&rectName, *(u16**)((u8*)images + off));
        }
        rectClearCost.x = 0x3C0;
        rectClearCost.y = i * 16 + 0x100;
        rectClearCost.w = 0x1B;
        rectClearCost.h = 0x10;
        loadImageSync(&rectClearCost, blankTextImage);
        started = 0;
        for (j = 0; j < 3; j++) {
            rectDigit.x = j * 2 + 0x3C0;
            rectDigit.y = i * 16 + 0x102;
            rectDigit.w = 6;
            rectDigit.h = 0xD;
            d = costs[i] / div[j];
            if (d != 0 || started) {
                loadImageSync(&rectDigit, itemLabelsIds[d].m0_image);
                started = 1;
                costs[i] -= d * div[j];
            } else {
                loadImageSync(&rectDigit, blankTextImage);
            }
        }
        rectLast.x = 0x3C6;
        rectLast.y = i * 16 + 0x102;
        rectLast.w = 6;
        rectLast.h = 0xD;
        if (ids[i] != 0xFF) {
            loadImageSync(&rectLast, itemLabelsIds[(u16)(costs[i] % 10)].m0_image);
        } else {
            loadImageSync(&rectLast, blankTextImage);
        }
    }
    p = images;
    for (i = 0; i < 4; i++) {
        freeHeap(p[i]);
    }
    freeHeap(blankTextImage);
    battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven], 0x94, 0x54, 0, 0, 0x60, 0x40);
    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[2][D_800CCB04.m30_battleOddOrEven], 0xFC, 0x54, 0, 0, 0x20, 0x40);
    battleVar0->mA230_menuPanel->m668_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar0->mA230_menuPanel->m669_drawEPCost = 1;
    battleVar1->mB7_openMenuType = 4;
}

/* gear command sub-menu: highlight plate and target/element icons of the gear command in slot */
void setGearSpecialMenuIcons(u8 entity, u8 slot) {
    u16 flags;
    u8 target;
    u8 icon;

    setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[4][D_800CCB04.m30_battleOddOrEven], 0x7C, 0xA4, 0, slot * 16, 0x60, 0x10);
    flags = mechaBattleStats[entity].m0_commands[slot + 37].m0_flags;
    if (flags & 0x4000) {
        target = 9;
    } else if (flags & 0x1000) {
        target = 7;
    } else {
        target = 8;
    }
    switch (flags & 0xF) {
    case 0:
        icon = 0xC;
        break;
    case 1:
        icon = 0xD;
        break;
    case 2:
        icon = 0x15;
        break;
    default:
        icon = 0xC;
        break;
    }
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven], 0x24, 0xA6, battleLabelGlyphRects[target].m2_u,
                  battleLabelGlyphRects[target].m3_v, battleLabelGlyphRects[target].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[10][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[target].m1_clut ? textSpriteMode1 : textSpriteMode0;
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven], 0x48, 0xA6, battleLabelGlyphRects[icon].m2_u,
                  battleLabelGlyphRects[icon].m3_v, battleLabelGlyphRects[icon].m0_width);
    battleVar0->mA230_menuPanel->m0_polys[11][D_800CCB04.m30_battleOddOrEven].clut = battleLabelGlyphRects[icon].m1_clut ? textSpriteMode1 : textSpriteMode0;
}

/* gear command sub-menu: name and description text of the gear's command in slot (battleVar0->mA230 polys 8 and 9) */
void renderGearSpecialDescription(u8 entity, u8 slot) {
    RECT rect;
    u16 v;
    s32 t;
    u16* image;
    u8 w0;
    u8 w1;

    v = (battleEntities.m0_entities[entity].m0_base.mA0_gearNum * 4 + slot) * 2;
    image = allocateTextureRamForText(0x39);
    bzero(image, 0x618);
    t = v;
    w0 = renderString(getDialogParamPointer(gearCommandStrings, t), image, 0x39, 0);
    w1 = renderString(getDialogParamPointer(gearCommandStrings, t | 1), image, 0x39, 1);
    rect.x = 0x3C0;
    rect.y = 0;
    rect.w = 0x3C;
    rect.h = 0xD;
    loadImageSync(&rect, image);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[8][D_800CCB04.m30_battleOddOrEven], 0x88, 0xB6, 0, 0, w0);
    repositionTextRenderingPlanePrim(&battleVar0->mA230_menuPanel->m0_polys[9][D_800CCB04.m30_battleOddOrEven], 0x88, 0xC6, 0, 0, w1);
    freeHeap(image);
}

/* gear command sub-menu (drawCircleMenuGearSpecial_update): cursor moved to slot; draws the slot's info when it is usable */
void updateGearSpecialMenuCursor(u8 entity, u8 cursor) {
    u16 mask[4];

    mask[0] = party1C_InitialValues[13];
    mask[1] = party1C_InitialValues[14];
    mask[2] = party1C_InitialValues[15];
    mask[3] = party1C_InitialValues[3];
    if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m1A_unlockedGearAbilities, cursor) != 0
        && (battleEntities.m0_entities[entity].m0_base.m7A_commandEnabledBF & mask[cursor]) == 0) {
        setGearSpecialMenuIcons(entity, cursor);
        renderGearSpecialDescription(entity, cursor);
        battleVar0->mA230_menuPanel->m66A_oddOrEven = D_800CCB04.m30_battleOddOrEven;
        battleVar0->mA230_menuPanel->m66B_drawSelection = 1;
    } else {
        battleVar0->mA230_menuPanel->m66B_drawSelection = 0;
    }
}

/* Billy (battleCharacters[entity] == 4): ammo window with the two ammo types and 3-digit counts */
void updateTargetSelectionMarkerSub1(u8 entity);
void updateTargetSelectionMarkerSub1(u8 entity) {
    RECT rectName;
    RECT rectLast;
    RECT rectDigit;
    RECT rectClear;
    RECT rectClearCost;
    RECT unused;
    u16* images[2];
    u16 div[2];
    u8 ids[2];
    u16 counts[2];
    s32 i;
    s32 j;
    u8 d;
    u8 started;
    u16** p;
    s32 off;
    u16* img;

    if (battleCharacters[entity] == 4 && battleVar1->mB7_openMenuType == 0) {
        div[0] = 100;
        div[1] = 10;
        if (apConfigArray[entity].m1_gearState == 0) {
            ids[0] = gameState.m26C_party[battleCharacters[entity]].m6F_equipment[0];
            ids[1] = gameState.m26C_party[battleCharacters[entity]].m6F_equipment[3];
            counts[0] = gameStateAmmoByWeaponItem[ids[0]];
            counts[1] = gameStateAmmoByWeaponItem[ids[1]];
        } else {
            ids[0] = gameState.m978_gears[gameState.m26C_party[battleCharacters[entity]].mA0_gearNum].m4_slotItem[0];
            ids[1] = gameState.m978_gears[gameState.m26C_party[battleCharacters[entity]].mA0_gearNum].m4_slotItem[3];
            counts[0] = gameStateAmmoByGearItem[ids[0]];
            counts[1] = gameStateAmmoByGearItem[ids[1]];
        }
        initBattleVar0_A230();
        loadItemTargetsLabel();
        blankTextImage = allocateTextureRamForText(0x39);
        bzero(blankTextImage, 0x618);
        for (i = 0; i < 2; i++) {
            img = allocateTextureRamForText(0x1B);
            off = i * (s32)sizeof(u16*);
            *(p = (u16**)((u8*)images + off)) = img;
            bzero(*p, 0x30C);
            rectClear.x = 0x380;
            rectClear.y = i * 16 + 0x100;
            rectClear.w = 0x1B;
            rectClear.h = 0x10;
            loadImageSync(&rectClear, blankTextImage);
            started = 0;
            if (ids[i] != 0) {
                renderString(getWeaponName(ids[i]), *p, 0x1B, 0);
                rectName.x = 0x380;
                rectName.y = i * 16 + 0x102;
                rectName.w = 0x1E;
                rectName.h = 0xD;
                loadImageSync(&rectName, *p);
            }
            rectClearCost.x = 0x3C0;
            rectClearCost.y = i * 16 + 0x100;
            rectClearCost.w = 0x1B;
            rectClearCost.h = 0x10;
            loadImageSync(&rectClearCost, blankTextImage);
            for (j = 0; j < 2; j++) {
                rectDigit.x = j * 2 + 0x3C0;
                rectDigit.y = i * 16 + 0x102;
                rectDigit.w = 6;
                rectDigit.h = 0xD;
                d = counts[i] / div[j];
                if (d != 0 || started) {
                    loadImageSync(&rectDigit, itemLabelsIds[d].m0_image);
                    started = 1;
                    counts[i] -= d * div[j];
                } else {
                    loadImageSync(&rectDigit, blankTextImage);
                }
            }
            rectLast.x = 0x3C4;
            rectLast.y = i * 16 + 0x102;
            rectLast.w = 6;
            rectLast.h = 0xD;
            if (ids[i] != 0) {
                loadImageSync(&rectLast, itemLabelsIds[(u16)(counts[i] % 10)].m0_image);
            } else {
                loadImageSync(&rectLast, blankTextImage);
            }
        }
        p = images;
        for (i = 0; i < 2; i++) {
            freeHeap(p[i]);
        }
        freeHeap(blankTextImage);
        battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven].clut = textSpriteMode0;
        setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[0][D_800CCB04.m30_battleOddOrEven], 0xA0, 0xAC, 0, 0, 0x60, 0x20);
        setupMenuPolyFT4(&battleVar0->mA230_menuPanel->m0_polys[2][D_800CCB04.m30_battleOddOrEven], 0x104, 0xAC, 0, 0, 0x18, 0x20);
        battleVar0->mA230_menuPanel->m668_oddOrEven = D_800CCB04.m30_battleOddOrEven;
        battleVar0->mA230_menuPanel->m669_drawEPCost = 1;
        allocateAndSetupBattleDialogWindow(0, 0x98, 0xA8, 0x8C, 0x28, 0, 1);
        battleVar1->mB7_openMenuType = 5;
        battleVar1->m8E_isBillyWindowOpen = 1;
    }
}

void removeBillyWindow(u8 entity, u8 arg1) {
    if (battleCharacters[entity] == 4) {
        battleVar1->mB7_openMenuType = 0;
        battleVar1->mB0_isDialogWindowInitialized[0] = 0;
        if (arg1 != 0) {
            freeBillyBattleStructs(entity);
        }
    }
}
