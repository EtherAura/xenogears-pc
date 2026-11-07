/*
 * Character submenu (status, list, skills). Provisional file 0x801E2BE4-0x801E3088;
 * owns .rodata 0x801C51B4-0x801C51DC. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "menu/menu.h"

/* character submenu: Status / list mode 0 / skills (JP skips the refresh after the sub-screens) */
u8 openStatusMenu(void) {
    u8 keepGoing;
    u8 prev;
    u8 first;
    s32 slot;
    s32 i;

    keepGoing = 1;
    prev = 0xF3;
    first = 1;
    slot = gMenuContext->m4DC_currentlySelectedCharacter;
    gMenuContext->m338_currentSelectedSubMenuEntry = 2;
    gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
    initStatusMenu();
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            slot = i;
            break;
        }
    }
    while (keepGoing) {
        menuDraw();
        if (slot != prev) {
            setupStatusMenuForCharacter1(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]);
            setupStatusMenuForCharacter2(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]);
            setupStatusMenuForCharacter3(slot, 0);
            prev = slot;
            if (first) {
                first = 0;
                processLoadSaveMenuSub1();
                processLoadSaveMenuSub2(0, 0);
                processLoadSaveMenuSub3(0);
            }
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            setupStatusMenuForCharacter4();
            processLoadSaveMenuSub8(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            resetCursorState();
            hideSubMenuLabels();
            gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 0;
            gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 0;
            gMenuContext->m33C_drawFlags->m4B_draw360 = 0;
            switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
            case 0:
                gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
                runEquipMenu(slot, 0, 0);
                freeEquipMenu();
                setupStatusMenuForCharacter3Sub2(slot, 0, 0, 0);
                setupStatusScreenEquipNames(slot, 0, 0, 0);
#if !VERSION_JP
                prev = 0xFF; /* JP does not refresh the character after the sub-screen */
#endif
                gMenuContext->m33C_drawFlags->mA_354Enabled = 1;
                break;
            case 1:
                runEtherListMenu(slot, 0, 0);
                freeEtherListMenu(0);
#if !VERSION_JP
                prev = 0xFF;
#endif
                break;
            case 2:
                runSkillMenu(slot);
                break;
            }
            initStatusMenuSub0(1);
            gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 1;
            gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 1;
            gMenuContext->m33C_drawFlags->m4B_draw360 = 1;
            updateCursorPolysLocation(gMenuContext->m338_currentSelectedSubMenuEntry + 7, 1);
            gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            initStatusMenuSub1(0, 0);
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
            slot = findNextPartySlot(slot, 0, 0);
            break;
        case 10:
            slot = findNextPartySlot(slot, 1, 0);
            break;
        }
    }
    initStatusMenuSub0(0);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    return 1;
}
