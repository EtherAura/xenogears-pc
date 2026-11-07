/*
 * Character submenu (status, lists, board or leave the gear). Provisional file
 * 0x801E23CC-0x801E2BE4; owns .rodata 0x801C518C-0x801C51B4. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"

extern u8 gearSwitchDisabled;
extern s32 gearMenuLabelOffsetsX[10]; /* [8..9] is gearMenuLabelX */

/* character submenu: status / lists / board-leave gear (owns jtbl_801C518C).
 * JP drops the redraw resets of cases 0/1 (#if !VERSION_JP around them). */
/* Every `prev = 0xFF` sits in two one-shot loops: the loop depth weights prev's references so
 * that global alloc ranks it above slot (retail prev s1, slot s2). */
#if VERSION_JP
/* JP has no resets in cases 0/1 (prev stays live there), so its three remaining resets need
 * more weight: eight one-shot loops (weighted refs 37 vs slot's 40 over a longer life). */
#define RESET_PREV() do { do { do { do { do { do { do { do { prev = 0xFF; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0)
#else
#define RESET_PREV() do { do { prev = 0xFF; } while (0); } while (0)
#endif
u8 openGearMenu(void) {
    u8 keepGoing;
    u8 first;
    u8 prev;
    u8 set;
    s32 slot;

    keepGoing = 1;
    /* Unset in retail too: $a0 is the caller's $s2, menu2_executeMainMenuSelection's
     * arg0, which is 0 whenever it opens this menu. The result is unused, but a
     * garbage index walks findNextPartySlot's table out of bounds. */
    slot = 0;
    findNextPartySlot(slot, 0, 1);
    prev = 0xF3;
    first = 1;
    gMenuContext->m338_currentSelectedSubMenuEntry = 0;
    gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
    slot = initGearStatusMenu();
    while (keepGoing) {
        menuDraw();
        if (slot != prev) {
            prev = slot;
            computeGearMenuStats(prev);
            setupStatusMenuForCharacter3(prev, 1);
            set = gameState.m22B1_isOnGear[slot] ? 6 : 0;
            setupGearMenuLabels(set);
            updateMenuSelection2(6, &gMenuContext->m4E0_labels[40], &gearMenuLabelConfig[6], gearMenuLabelOffsetsX, gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels, 4, 7, 6);
            updateMenuSelection2(6, &gMenuContext->m4E0_labels[40], &gearMenuLabelConfig[6], gearMenuLabelOffsetsX, gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels, 5, 7, 6);
            gMenuContext->m4E0_labels[44].m7F_transformEnabled = 1;
            gMenuContext->m4E0_labels[45].m7F_transformEnabled = 1;
            if (first) {
                first = 0;
                processLoadSaveMenuSub1();
                processLoadSaveMenuSub2(0, 0);
                processLoadSaveMenuSub3(0);
            }
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            updateMenuSelection2(4, &gMenuContext->m4E0_labels[40], &gearMenuLabelConfig[6], &gearMenuLabelOffsetsX[set], gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels,
                          gMenuContext->m338_currentSelectedSubMenuEntry, 7, 0);
            processLoadSaveMenuSub8(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            resetCursorState();
            countField33C_C(4, gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels);
            gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 0;
            gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 0;
            gMenuContext->m33C_drawFlags->m4B_draw360 = 0;
            gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels[4] = 0;
            gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels[5] = 0;
            switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
            case 0:
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] != 7) {
                    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
                    runEquipMenu(slot, 0, 1);
#if !VERSION_JP
                    RESET_PREV();
#endif
                    freeEquipMenu();
                    setupStatusMenuForCharacter3Sub2(slot, 0, 0, 1);
                    setupStatusScreenEquipNames(slot, 0, 0, 1);
                    gMenuContext->m33C_drawFlags->mA_354Enabled = 1;
                } else {
                    playMenuSoundEffect(4);
#if !VERSION_JP
                    RESET_PREV();
#endif
                }
                break;
            case 1:
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] != 7) {
                    runEtherListMenu(slot, 0, 1);
                    freeEtherListMenu(1);
#if !VERSION_JP
                    RESET_PREV();
#endif
                } else {
                    playMenuSoundEffect(4);
#if !VERSION_JP
                    RESET_PREV();
#endif
                }
                break;
            case 2:
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] != 7) {
                    runEtherListMenu(slot, 0, 2);
                    freeEtherListMenu(2);
                    RESET_PREV();
                } else {
                    playMenuSoundEffect(4);
                    RESET_PREV();
                }
                break;
            case 3:
                if (gearSwitchDisabled == 0) {
                    if (gameState.m22B1_isOnGear[slot]) {
                        gameState.m22B1_isOnGear[slot] = 0;
                        set = 0;
                    } else if (gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum != 0xFF) {
                        set = 6;
                        gameState.m22B1_isOnGear[slot] = 1;
                    }
                    ((void (*)())setupStatusMenuForCharacter3Sub1)(slot, 1, gameState.m22B1_isOnGear[slot]);
                } else {
                    playMenuSoundEffect(4);
                }
                RESET_PREV();
                break;
            }
            gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels[4] = 1;
            gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels[5] = 1;
            j_setupMenuContext4E0(6, &gMenuContext->m4E0_labels[40], &gearMenuLabelConfig[set], gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels);
            gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 1;
            gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 1;
            gMenuContext->m33C_drawFlags->m4B_draw360 = 1;
            updateCursorPolysLocation(gMenuContext->m338_currentSelectedSubMenuEntry + 7, 1);
            gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            initStatusMenuSub1(0, 1);
            break;
        case 5:
            keepGoing = 0;
            break;
        case 1:
            if (gMenuContext->m338_currentSelectedSubMenuEntry != 0) {
                gMenuContext->m338_currentSelectedSubMenuEntry--;
            } else {
                gMenuContext->m338_currentSelectedSubMenuEntry = gMenuContext->m33A_numMenuEntries - 1;
            }
            break;
        case 3:
            if (++gMenuContext->m338_currentSelectedSubMenuEntry >= gMenuContext->m33A_numMenuEntries) {
                gMenuContext->m338_currentSelectedSubMenuEntry = 0;
            }
            break;
        case 9:
            slot = findNextPartySlot(slot, 0, 1);
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            break;
        case 10:
            slot = findNextPartySlot(slot, 1, 1);
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            break;
        }
    }
    countField33C_C(6, gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    return 1;
}
#undef RESET_PREV

/* alloc m358/m35C/m360 for openStatusMenu */
void initStatusMenu(void);
void initStatusMenu(void) {
    initStatusMenuSub0(1);
    gMenuContext->m358_selectedInfoCard = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_358), 0x2af0), 0);
    bzero(gMenuContext->m358_selectedInfoCard, sizeof(MenuContext_358));
    gMenuContext->m35C_selectedStats = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_35C), 0x32f4), 0);
    bzero(gMenuContext->m35C_selectedStats, sizeof(MenuContext_35C));
    gMenuContext->m360_equipment = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_360), (s32)0x2ac), 0);
    bzero(gMenuContext->m360_equipment, (s32)sizeof(struct MenuContext_360));
    loadSharedMenuData2(3);
    initStatusMenuSub1(0, 0);
}

/* free m358/m35C/m360 */
void freeStatusMenu(void) {
    freeHeap(gMenuContext->m358_selectedInfoCard);
    freeHeap(gMenuContext->m35C_selectedStats);
    freeHeap(gMenuContext->m360_equipment);
    loadSharedMenuData2(0x13);
}
