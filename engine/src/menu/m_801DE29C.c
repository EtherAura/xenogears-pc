/*
 * Equip screen (character or gear equipment). Provisional file
 * 0x801DE29C-0x801E0F78; owns .rodata 0x801C515C-0x801C518C. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"

extern u8 equipMenuLabelConfig[];  /* equip menu label config, 6 bytes per slot */
extern s32 equipMenuCursorY[];
extern u8 equipCandidateIds[];  /* equip candidates list (item ids) */
extern u8 equipCandidateCounts[];  /* equip candidates list (counts) */
u32 menu2CheckBitmask32(u32 mask, u8 bit); /* m_801C65F4.c */

s32 buildEquipCandidateList(u8 slot, s32 page, s32 kind, u8 isSlot, u8 isGear);
u8 swapEquippedItem(u8 slot, u8 kind, u8 isSlot, u8 isGear);
void backupEquipment(u8 slot, u8 isGear);
void restoreEquipment(u8 slot, u8 isGear);
void setEquipmentSlotItem(u8 slot, s32 kind, s32 page, s32 index, u8 isSlot, u8 isGear);
void setupEquipItemDescription(s32 kind, s32 page, s32 index, u8 isSlot, u8 isGear, u8 current, u8 slot);
void unequipWeaponToInventory(u8 slot, u8 isGear);
void initEquipMenu(u8 slot);
void setupEquipSlotLabels(u8 mode, u8 slot);

/* equip submenu entry: runEtherListMenu(slot, arg1, 0) */
u8 openEtherMenu(u8 slot, u8 arg1) {
    runEtherListMenu(slot, arg1, 0);
    return 1;
}

/* equip screen setup (m434 work area, labels m14E0) */
void initEquipMenu(u8 slot) {
    gMenuContext->m434_equipMenu = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_434), (s32)0xa1c), 0);
    bzero(gMenuContext->m434_equipMenu, (s32)sizeof(struct MenuContext_434));
    j_setupMenuContext4E0(6, gMenuContext->m14E0_equipMenuLabels, &equipMenuLabelConfig[slot * 6], gMenuContext->m33C_drawFlags->m40_drawEquipMenuLabels);
    loadSharedMenuData2(7);
    setupDraw428(3);
    allocMenuPointerGlyph(0);
    initStatusMenuSub1(1, slot);
}

/* equip screen teardown */
void freeEquipMenu(void) {
    gMenuContext->m33C_drawFlags->m4C_draw434 = 0;
    processLoadSaveMenuSub7Sub0(2);
    processLoadSaveMenuSub7Sub0(3);
    processLoadSaveMenuSub7Sub0(4);
    processLoadSaveMenuSub7Sub0(5);
    menuDraw();
    countField33C_C(6, gMenuContext->m33C_drawFlags->m40_drawEquipMenuLabels);
    loadSharedMenuData2(0x17);
    freeHeap(gMenuContext->m434_equipMenu);
    freeScrollBarThumb();
}

/* free m35C/m360 (status detail + equip preview) */
void freeStatusStatsAndEquipNames(void) {
    gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 0;
    gMenuContext->m33C_drawFlags->m4B_draw360 = 0;
    menuDraw();
    freeHeap(gMenuContext->m35C_selectedStats);
    freeHeap(gMenuContext->m360_equipment);
}

/* equip slot labels + menu box 4 */
void setupEquipSlotLabels(u8 mode, u8 slot) {
    s32 i;
    s32 start;
    s32 end;
    s32 height;

    for (i = 0; i < 6; i++) {
        gMenuContext->m33C_drawFlags->m40_drawEquipMenuLabels[i] = 0;
    }
    if (mode) {
        start = 2;
        end = 6;
        height = 0x72;
    } else {
        start = 0;
        end = 2;
        height = 0x5A;
    }
    for (i = start; i < end; i++) {
        updateMenuSelection2(6, gMenuContext->m14E0_equipMenuLabels, &equipMenuLabelConfig[slot * 6], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m40_drawEquipMenuLabels, i, i,
                      3);
    }
    if (gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[4]) {
        processLoadSaveMenuSub7Sub0(4);
    }
    iniMenuContext364And380(4, 0x10, 0xC, 0x80, height, 0, 1, 4, 0);
}

/* not attempted: build the equip candidate list (equipCandidateIds/equipCandidateCounts) from the inventory */
s32 buildEquipCandidateList(u8 slot, s32 page, s32 kind, u8 isSlot, u8 isGear) {
    RECT rect;
    u8 codes[4];
    u8 str[8];
    s32 count;
    u8 type;
    s32 limit;
    MenuWeaponStats* wpn;
    MenuAccessoryStats* acc;
    MenuWeaponStats* curWpn;
    MenuGearWeapon* gw;
    MenuGearWeapon* curGw;
    u16 mask1;
    u16 mask2;
    MenuGearAccessory* ga;
    u8* image;
    s32 i;
    u8 ok;
    u8 tens;
    u16 y;

    codes[1] = 0;
    codes[3] = 0;
    if (isSlot) {
        if (isGear == 0) {
            type = kind + 1;
            limit = 100;
            curWpn = &gMenuContext->m330_itemData->m0_weaponStats[gMenuContext->m360_equipment->m29C_backup6A[kind]];
        } else {
            type = kind + 1;
            limit = 100;
            curGw = &gMenuContext->m330_itemData->m18_gearWeapons[gMenuContext->m360_equipment->m29C_backup6A[kind]];
        }
    } else if (isGear == 0) {
        if (kind != 0) {
            type = 5;
            limit = 200;
            acc = &gMenuContext->m330_itemData->m4_accessoryData[gMenuContext->m360_equipment->m2A6_backup74[kind - 1]];
            mask1 = acc->mE_exclusiveGroup;
            for (i = 0, mask2 = 0; i < 3; i++) {
                acc = &gMenuContext->m330_itemData->m4_accessoryData[gMenuContext->m360_equipment->m2A6_backup74[i]];
                mask2 |= acc->mE_exclusiveGroup;
            }
        } else {
            type = 0;
            limit = 100;
        }
    } else if (kind != 0) {
        type = 5;
        limit = 150;
        ga = &gMenuContext->m330_itemData->m14_gearAccessories[gMenuContext->m360_equipment->m2A6_backup74[kind - 1]];
        mask1 = ga->m8_exclusiveGroup;
        for (i = 0, mask2 = 0; i < 3; i++) {
            ga = &gMenuContext->m330_itemData->m14_gearAccessories[gMenuContext->m360_equipment->m2A6_backup74[i]];
            mask2 |= ga->m8_exclusiveGroup;
        }
    } else {
        type = 0;
        limit = 100;
    }
    for (i = 0; i < 200; i++) {
        equipCandidateIds[i] = 0;
        equipCandidateCounts[i] = 0;
    }
    count = type == 5;
    for (i = 0; i < limit; i++) {
        if (isGear == 0) {
            acc = &gMenuContext->m330_itemData->m4_accessoryData[gameState.m1EC8_inventory2Ids[i]];
            wpn = &gMenuContext->m330_itemData->m0_weaponStats[gameState.m1D9C_inventory1Ids[i]];
        } else {
            gw = &gMenuContext->m330_itemData->m18_gearWeapons[gameState.m2120_inventory3Ids[i]];
            ga = &gMenuContext->m330_itemData->m14_gearAccessories[gameState.m221A_inventory4Ids[i]];
        }
        ok = 0;
        if (isGear == 0) {
            switch (type) {
            case 0:
                if (menu2CheckBitmask(wpn->m0_equipMask, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]) && wpn->m6_slotType[0] < 5 &&
                    gameState.m1D9C_inventory1Ids[i] < 50) {
                    ok = 1;
                }
                break;
            case 1:
            case 2:
            case 3:
            case 4:
                if (menu2CheckBitmask(wpn->m0_equipMask, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]) &&
                    wpn->m6_slotType[0] == curWpn->m6_slotType[0] && gameState.m1D9C_inventory1Ids[i] >= 50) {
                    ok = 1;
                }
                break;
            case 5:
                if (menu2CheckBitmask(acc->m0_equipMask, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot])) {
                    if (acc->mE_exclusiveGroup == 0 || (mask1 & acc->mE_exclusiveGroup) || !(mask2 & acc->mE_exclusiveGroup)) {
                        ok = 1;
                    }
                }
                break;
            }
        } else {
            switch (type) {
            case 0:
                if (menu2CheckBitmask32(gw->m4_gearMask, gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum) &&
                    gw->mF_slotType < 5 && gameState.m2120_inventory3Ids[i] < 50) {
                    ok = 1;
                }
                break;
            case 1:
            case 2:
            case 3:
            case 4:
                if (menu2CheckBitmask32(gw->m4_gearMask, gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum) &&
                    gw->mF_slotType == curGw->mF_slotType && gameState.m2120_inventory3Ids[i] >= 50) {
                    ok = 1;
                }
                break;
            case 5:
                if (menu2CheckBitmask32(ga->m0_gearMask, gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum)) {
                    if (ga->m8_exclusiveGroup == 0) {
                        ok = 1;
                    } else if (mask1 & ga->m8_exclusiveGroup) {
                        ok = 1;
                    } else if (!(mask2 & ga->m8_exclusiveGroup)) {
                        ok = 1;
                    }
                }
                break;
            }
        }
        if (ok) {
            if (isGear == 0) {
                if (type != 5) {
                    equipCandidateIds[count] = gameState.m1D9C_inventory1Ids[i];
                    equipCandidateCounts[count] = gameState.m1D38_inventory1Counts[i];
                } else {
                    equipCandidateIds[count] = gameState.m1EC8_inventory2Ids[i];
                    equipCandidateCounts[count] = gameState.m1E00_inventory2Counts[i];
                }
            } else if (type != 5) {
                equipCandidateIds[count] = gameState.m2120_inventory3Ids[i];
                equipCandidateCounts[count] = gameState.m20BC_inventory3Counts[i];
            } else {
                equipCandidateIds[count] = gameState.m221A_inventory4Ids[i];
                equipCandidateCounts[count] = gameState.m2184_inventory4Counts[i];
            }
            count++;
        }
    }
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 8; i++) {
        if (equipCandidateIds[page + i] != 0) {
            if (isGear == 0) {
                if (type != 5) {
                    gMenuContext->m434_equipMenu->m0_names[i].m7E_stringWidth = renderString(getWeaponName(equipCandidateIds[page + i]), image, 0x24, 0);
                } else {
                    gMenuContext->m434_equipMenu->m0_names[i].m7E_stringWidth = renderString(getAccessoryName(equipCandidateIds[page + i]), image, 0x24, 0);
                }
            } else if (type != 5) {
                gMenuContext->m434_equipMenu->m0_names[i].m7E_stringWidth = renderString(getGearWeaponName(equipCandidateIds[page + i]), image, 0x24, 0);
            } else {
                gMenuContext->m434_equipMenu->m0_names[i].m7E_stringWidth = renderString(getGearAccessoryName(equipCandidateIds[page + i]), image, 0x24, 0);
            }
            tens = equipCandidateCounts[page + i] / 10;
            codes[0] = tens ? tens + 0x10 : 0xC3;
            codes[2] = equipCandidateCounts[page + i] % 10 + 0x10;
            decodeCharacterCodes((u16*)codes, str, 2);
            gMenuContext->m434_equipMenu->m400_counts[i].m7E_stringWidth = renderString(str, image, 0x24, 1);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            DrawSync(0);
            initMenuContext_1DE0(&gMenuContext->m434_equipMenu->m0_names[i], i, 0x80, 0x81);
            initMenuContext_1DE0(&gMenuContext->m434_equipMenu->m400_counts[i], i, 0x80, 0x82);
            y = i * 13 + 0x12;
            initMemoryCardPolyVerts(gMenuContext->m434_equipMenu->m0_names[i].m50_vertex, 0xA8, y, gMenuContext->m434_equipMenu->m0_names[i].m7E_stringWidth, 0xD);
            initMemoryCardPolyVerts(gMenuContext->m434_equipMenu->m400_counts[i].m50_vertex, 0x10C, y, gMenuContext->m434_equipMenu->m400_counts[i].m7E_stringWidth, 0xD);
            gMenuContext->m434_equipMenu->m0_names[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m434_equipMenu->m400_counts[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m434_equipMenu->mA10_enabled[i] = 1;
        } else {
            gMenuContext->m434_equipMenu->mA10_enabled[i] = 0;
        }
    }
    freeHeap(image);
    setupCharacterNameLabel(&gMenuContext->m434_equipMenu->m800_characterName, slot, isGear, 0);
    count -= 8;
    gMenuContext->m33C_drawFlags->m4C_draw434 = 1;
    if (count < 0) {
        count = 0;
    }
    return count;
}


/* unequip/re-equip one slot: swap the old and new item through the inventory lists (ids/counts);
 * returns 1 when party member id 4 swaps a non-empty weapon slot. new is read before p/old: that order
 * gives the zero-extended kind its early place in sched1 (register a0 as in retail). */
u8 swapEquippedItem(u8 slot, u8 kind, u8 isSlot, u8 isGear) {
    u8* ids;
    u8* counts;
    u8* p;
    u8 old;
    u8 new;
    s32 n;
    u8 proceed;
    u8 notFound;
    u8 ret;
    s32 i;

    notFound = 1;
    proceed = 1;
    ret = 0;
    if (!isGear) {
        ids = gameState.m1D9C_inventory1Ids;
        counts = ids - 100;
    } else {
        ids = gameState.m2120_inventory3Ids;
        counts = ids - 100;
    }
    n = 100;
    if (isSlot) {
        if (!isGear) {
            new = gMenuContext->m360_equipment->m2A1_backup6F[kind];
            p = &gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[kind];
            old = *p;
            if (old == 0) {
                *p = new;
                proceed = 0;
            } else {
                if (gameStateAmmoByWeaponItem[*p] < 100) {
                    new = 0;
                }
                gameStateAmmoByWeaponItem[*p] = 100;
            }
        } else {
            new = gMenuContext->m360_equipment->m2A1_backup6F[kind];
            p = &gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                     .m4_slotItem[kind];
            old = *p;
            if (old == 0) {
                *p = new;
                proceed = 0;
            } else {
                if (gameStateAmmoByGearItem[*p] < 100) {
                    new = 0;
                }
                gameStateAmmoByGearItem[*p] = 100;
            }
        }
    } else if (!isGear) {
        if (kind == 0) {
            old = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[0];
            new = gMenuContext->m360_equipment->m29C_backup6A[0];
            if (old == 0) {
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[0] = new;
                proceed = 0;
            } else if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 4) {
                ret = 1;
            }
        } else {
            old = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots[kind - 1];
            new = gMenuContext->m360_equipment->m2A6_backup74[kind - 1];
            ids = gameState.m1EC8_inventory2Ids;
            counts = gameState.m1E00_inventory2Counts;
            n = 200;
        }
    } else {
        if (kind == 0) {
            old = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                      .mC_weapon[0];
            new = gMenuContext->m360_equipment->m29C_backup6A[0];
            if (old == 0) {
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .mC_weapon[0] = new;
                proceed = 0;
            } else if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 4) {
                ret = 1;
            }
        } else {
            old = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                      .m9_accessorySlots[kind - 1];
            new = gMenuContext->m360_equipment->m2A6_backup74[kind - 1];
            ids = gameState.m221A_inventory4Ids;
            counts = gameState.m2184_inventory4Counts;
            n = 150;
        }
    }
    if (proceed) {
        if (old != 0) {
            for (i = 0; i < n; i++) {
                if (ids[i] == old) {
                    counts[i]--;
                    break;
                }
            }
        }
        if (new != 0) {
            for (i = 0; i < n; i++) {
                if (ids[i] == new) {
                    notFound = 0;
                    counts[i]++;
                    break;
                }
            }
            if (notFound) {
                for (i = 0; i < n; i++) {
                    if (ids[i] == 0) {
                        ids[i] = new;
                        counts[i] = 1;
                        break;
                    }
                }
            }
        }
        for (i = 0; i < n; i++) {
            if (counts[i] == 0) {
                ids[i] = 0;
            } else if (counts[i] >= 100) {
                counts[i] = 99;
            }
        }
    }
    return ret;
}

/* back up equipment (m360) and stats before editing */
void backupEquipment(u8 slot, u8 isGear) {
    s32 i;

    for (i = 0; i < 9; i++) {
        gMenuContext->m360_equipment->m280_stats[i] = gMenuContext->m330_itemData->mB8_computedStats[i];
    }
    if (!isGear) {
        for (i = 0; i < 5; i++) {
            gMenuContext->m360_equipment->m29C_backup6A[i] =
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[i];
            gMenuContext->m360_equipment->m2A1_backup6F[i] =
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[i];
            gMenuContext->m360_equipment->m2A6_backup74[i] =
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots[i];
        }
    } else {
        for (i = 0; i < 4; i++) {
            gMenuContext->m360_equipment->m29C_backup6A[i] =
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .mC_weapon[i];
            gMenuContext->m360_equipment->m2A1_backup6F[i] =
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .m4_slotItem[i];
            gMenuContext->m360_equipment->m2A6_backup74[i] =
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .m9_accessorySlots[i];
        }
    }
}

/* restore equipment from m360 (cancel) */
void restoreEquipment(u8 slot, u8 isGear) {
    s32 i;

    if (!isGear) {
        for (i = 0; i < 5; i++) {
            gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[i] =
                gMenuContext->m360_equipment->m29C_backup6A[i];
            gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[i] =
                gMenuContext->m360_equipment->m2A1_backup6F[i];
        }
        for (i = 0; i < 3; i++) {
            gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots[i] =
                gMenuContext->m360_equipment->m2A6_backup74[i];
        }
    } else {
        for (i = 0; i < 4; i++) {
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                .mC_weapon[i] = gMenuContext->m360_equipment->m29C_backup6A[i];
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                .m4_slotItem[i] = gMenuContext->m360_equipment->m2A1_backup6F[i];
        }
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                .m9_accessorySlots[i] = gMenuContext->m360_equipment->m2A6_backup74[i];
        }
    }
}

/* store the chosen item (equipCandidateIds[index + page]) into an equipment slot */
void setEquipmentSlotItem(u8 slot, s32 kind, s32 page, s32 index, u8 isSlot, u8 isGear) {
    if (!isGear) {
        if (!isSlot) {
            if (kind == 0) {
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[0] =
                    equipCandidateIds[index + page];
            } else if (kind >= 0) {
                if (kind < 4) {
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots[kind - 1] =
                    equipCandidateIds[index + page];
                }
            }
        } else {
            gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[kind] =
                equipCandidateIds[index + page];
        }
    } else {
        if (!isSlot) {
            if (kind == 0) {
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .mC_weapon[0] = equipCandidateIds[index + page];
            } else if (kind >= 0) {
                if (kind < 4) {
                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                    .m9_accessorySlots[kind - 1] = equipCandidateIds[index + page];
                }
            }
        } else {
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                .m4_slotItem[kind] = equipCandidateIds[index + page];
        }
    }
}

/* recompute gear stats into m330->mB8 */
void computeGearMenuStats(u8 slot) {
    refreshGearEquipment(gMenuContext->m330_itemData,
                  gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum);
    computeGearStatusStats(gMenuContext->m330_itemData,
                  gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum);
    gMenuContext->m330_itemData->mB8_computedStats[0] = gMenuContext->m330_itemData->m9C_gearStats.m14_attack;
    gMenuContext->m330_itemData->mB8_computedStats[1] = gMenuContext->m330_itemData->m9C_gearStats.m8_defence;
    gMenuContext->m330_itemData->mB8_computedStats[2] = gMenuContext->m330_itemData->m9C_gearStats.mA_etherDefence;
    gMenuContext->m330_itemData->mB8_computedStats[3] = gMenuContext->m330_itemData->m9C_gearStats.m16_response;
    gMenuContext->m330_itemData->mB8_computedStats[4] = gMenuContext->m330_itemData->m9C_gearStats.m17_agility;
    gMenuContext->m330_itemData->mB8_computedStats[5] = gMenuContext->m330_itemData->m9C_gearStats.m18_etherAmp;
}

/* item description (3 lines of m434->mA00[cat]) for the highlighted or equipped item (the item id reuses param page) */
void setupEquipItemDescription(s32 kind, s32 page, s32 index, u8 isSlot, u8 isGear, u8 current, u8 slot) {
    u8* bundle;
    u16* image;
    u16 item;
    u8 cat;
    s32 i;
    RECT rect;

    cat = 0;
    page = equipCandidateIds[index + page];
    if (current) {
        page = 0xFF;
    }
    if (page != 0) {
        if (!isSlot && kind != 0) {
            cat = 1;
        }
        cat += isGear * 2;
        switch (cat) {
        case 0:
            bundle = gMenuContext->m434_equipMenu->mA00_textBundles[0];
            if (current) {
                if (!isSlot) {
                    page = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier[0];
                } else {
                    page = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[kind];
                }
            }
            break;
        case 1:
            bundle = gMenuContext->m434_equipMenu->mA00_textBundles[1];
            if (current) {
                page = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots[kind - 1];
            }
            break;
        case 2:
            bundle = gMenuContext->m434_equipMenu->mA00_textBundles[2];
            if (current) {
                if (!isSlot) {
                    page = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]]
                                                  .mA0_gearNum]
                             .mC_weapon[0];
                } else {
                    page = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]]
                                                  .mA0_gearNum]
                             .m4_slotItem[kind];
                }
            }
            break;
        case 3:
            bundle = gMenuContext->m434_equipMenu->mA00_textBundles[3];
            if (current) {
                page = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]]
                                              .mA0_gearNum]
                         .m9_accessorySlots[kind - 1];
            }
            break;
        }
        item = page;
        if (item != 0) {
            image = ALLOC_TEXT_IMAGE(0x3F6);
            bzero(image, 0x3F6);
            for (i = 0; i < 3; i++) {
                gMenuContext->m434_equipMenu->m880_description[i].m7E_stringWidth =
                    renderString(getDialogParamPointer(bundle, item * 3 + i), image, 0x24, 0);
                rect.x = ((i + 8) & 1) * 24 + 0x180;
                rect.y = ((i + 8) / 2) * 13 + 0x80;
                rect.w = 0x28;
                rect.h = 0xD;
                LoadImage(&rect, image);
                DrawSync(0);
                initMenuContext_1DE0(&gMenuContext->m434_equipMenu->m880_description[i], i + 8, 0x80, 0x81);
                initMemoryCardPolyVerts(gMenuContext->m434_equipMenu->m880_description[i].m50_vertex, 0x10, i * 16 + 0x96,
                              gMenuContext->m434_equipMenu->m880_description[i].m7E_stringWidth, 0xD);
                gMenuContext->m434_equipMenu->m880_description[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            }
            gMenuContext->m434_equipMenu->mA18_drawDescription = 1;
            freeHeap(image);
            return;
        }
    }
    gMenuContext->m434_equipMenu->mA18_drawDescription = 0;
}

/* unequip weapon / gear slot 0 back into inventory 1 / 3 */
void unequipWeaponToInventory(u8 slot, u8 isGear) {
    u8* ids;
    u8* counts;
    u8 item;
    u8 n;
    u8 notFound;
    s32 i;

    notFound = 1;
    if (!isGear) {
        ids = gameState.m1D9C_inventory1Ids;
        counts = ids - 100;
        item = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[0];
        gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment[0] = 0;
        n = 100;
    } else {
        ids = gameState.m2120_inventory3Ids;
        counts = ids - 100;
        item = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                   .m4_slotItem[0];
        gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
            .m4_slotItem[0] = 0;
        n = 100;
    }
    for (i = 0; i < n; i++) {
        if (ids[i] == item) {
            if (++counts[i] >= 100) {
                counts[i] = 99;
            }
            notFound = 0;
        }
    }
    if (notFound) {
        for (i = 0; i < n; i++) {
            if (ids[i] == 0) {
                ids[i] = item;
                counts[i] = 1;
                return;
            }
        }
    }
}

/* equip screen main loop (owns jtbl_801C515C) */
void runEquipMenu(u8 slot, u8 arg1, u8 isGear) {
    u8 gear;
    u8 redraw;
    u8 isSlot;
    u8 prevSlot;
    s32 kind;
    s32 pos;
    s32 page;
    u8 changed;
    u8 ret;
    u8 cont;
    u8 boxes;
    u8 described;
    u8 drawList;
    u8 pressEnter;
    u8 listMode;
    s32 prevKind;
    s32 prevPos;
    s32 prevPage;
    s32 count;
    s32 y;
    s32 h;

    cont = 1;
    boxes = 1;
    redraw = 1;
    isSlot = 0;
    prevSlot = 0xFF;
    kind = 0;
    prevKind = 0xFF;
    pos = 0;
    prevPos = 0xFF;
    gear = isGear;
    described = 0;
    drawList = 0;
    pressEnter = 0;
    listMode = 0;
    prevPage = 0xFF;
    gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 0;
    page = 0;
    initEquipMenu(gear);
    backupEquipment(slot, gear);
    do {
        menuDraw();
        if (redraw || slot != prevSlot) {
            setupStatusScreenEquipNames(slot, isSlot + 1, listMode, gear);
            setupEquipSlotLabels(isSlot, gear);
            redraw = 0;
        }
        if (page != prevPage || slot != prevSlot) {
            count = buildEquipCandidateList(slot, page, kind, isSlot, gear);
            if (count != 0) {
                y = page * 100 / count; /* two statements: the quotient goes to a1 like y */
                y /= 2;
                h = 0x32;
            } else {
                y = 0;
                h = 0x64;
            }
            setupScrollBarThumb(0x94, y + 0x12, h);
        }
        if (drawList) {
            updateMenuPointerGlyph(pos, page, 3, 0);
        } else {
            gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
        }
        if (pos != prevPos || slot != prevSlot || page != prevPage) {
            if (described) {
                setEquipmentSlotItem(slot, kind, pos, page, isSlot, gear);
            }
            described = 1;
            if (!gear) {
                setupStatusMenuForCharacter1(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]);
                setupStatusMenuForCharacter2(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]);
            } else {
                computeGearMenuStats(slot);
            }
            setupEquipItemDescription(kind, pos, page, isSlot, gear, 0, slot);
            setupStatusMenuForCharacter3Sub2(slot, 1, listMode, gear);
            prevPos = pos;
            prevPage = page;
        }
        if (kind != prevKind || slot != prevSlot) {
            setupEquipItemDescription(kind, pos, page, isSlot, gear, 1, slot);
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->x0 = 0x8C;
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->y0 = equipMenuCursorY[isSlot * 4 + kind];
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->x1 = 0x9C;
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->y1 = equipMenuCursorY[isSlot * 4 + kind];
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->x2 = 0x8C;
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->y2 =
                equipMenuCursorY[isSlot * 4 + kind] + 0x10;
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->x3 = 0x9C;
            (gMenuContext->m428_cardCursors->m0_polys[0] + gMenuContext->m428_cardCursors->m148_oddOrEven[0])->y3 =
                equipMenuCursorY[isSlot * 4 + kind] + 0x10;
            prevKind = kind;
            prevSlot = slot;
        }
        if (boxes) {
            iniMenuContext364And380(2, 0x94, 0xA, 0x94, 0x74, 0, 1, 4, 1);
            iniMenuContext364And380(3, 0x6C, 0x87, 0xC4, 0x48, 0, 1, 4, 0);
            iniMenuContext364And380(5, 8, 0x8E, 0x60, 0x40, 0, 1, 4, 0);
            if (arg1) {
                processLoadSaveMenuSub1();
                processLoadSaveMenuSub2(0, 0);
                while (gMenuContext->m329_animationState != 0) {
                    menuDraw();
                }
            }
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
            gMenuContext->m33C_drawFlags->m6_drawPlayTime = 0;
            boxes = 0;
            gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 0;
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        if (pressEnter) {
            pressEnter = 0;
            gMenuContext->m325_menuButton = 4;
        }
        if (!listMode) {
            switch (gMenuContext->m325_menuButton) {
            case 5:
                cont = 0;
                break;
            case 4:
                backupEquipment(slot, isGear);
                listMode = 1;
                page = 0;
                prevPage = 0xFF;
                prevPos = 0xFF;
                gMenuContext->m428_cardCursors->m140_enabled[0] = 0;
                redraw = 1;
                drawList = 1;
                pos = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = redraw;
                break;
            case 0:
            case 2:
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 4) {
                    isSlot ^= 1;
                    prevKind = 0xFF;
                    redraw = 1;
                    kind = 0;
                }
                break;
            case 1:
                kind++;
                if (kind >= 4) {
                    kind = 0;
                }
                if (isSlot && gear && (u32)(kind - 1) < 2) {
                    kind = 3;
                }
                break;
            case 3:
                kind--;
                if (kind < 0) {
                    kind = 3;
                }
                if (isSlot && gear && (u32)(kind - 1) < 2) {
                    kind = 0;
                }
                break;
            case 9:
#if VERSION_JP
                slot = findNextPartySlot(slot, 0, isGear);
                isSlot = 0;
                described = 0;
#else
                if (findNextPartySlot(slot, 0, gear) != slot) {
                    slot = findNextPartySlot(slot, 0, gear);
                    isSlot = 0;
                    described = 0;
                }
#endif
                break;
            case 10:
#if VERSION_JP
                slot = findNextPartySlot(slot, 1, isGear);
                isSlot = 0;
                described = 0;
#else
                if (findNextPartySlot(slot, 1, gear) != slot) {
                    slot = findNextPartySlot(slot, 1, gear);
                    isSlot = 0;
                    described = 0;
                }
#endif
                break;
            }
        } else {
            changed = 0;
            switch (gMenuContext->m325_menuButton) {
            case 5:
                restoreEquipment(slot, isGear);
                drawList = 0;
                gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
                changed = 1;
                updateMenuPointerGlyph(0, page, 3, 0);
                pos = 0;
                prevSlot = 0xFF;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                break;
            case 4:
                ret = swapEquippedItem(slot, kind, isSlot, gear);
                prevSlot = 0xFF;
                if (ret) {
                    isSlot ^= 1;
                    redraw = 1;
                    pressEnter = 1;
                    unequipWeaponToInventory(slot, gear);
                }
                /* one-shot loop: its notes keep changed = 1 above the updateMenuPointerGlyph call (sched1) */
                do {
                    backupEquipment(slot, gear);
                    drawList = 0;
                    gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
                    changed = 1;
                    updateMenuPointerGlyph(0, page, 3, 0);
                } while (0);
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                pos = 0;
                break;
            case 1:
                pos++;
                if (pos >= 8) {
                    pos = 7;
                    page++;
                    if (page > count) {
                        page = count;
                    }
                }
                break;
            case 3:
                pos--;
                if (pos < 0) {
                    page--;
                    pos = 0;
                    if (page < 0) {
                        page = 0;
                    }
                }
                break;
            }
            if (changed) {
                pos = 0;
                page = 0;
                listMode = 0;
                prevPos = 0xFF;
                redraw = 1;
                described = 0;
                gMenuContext->m35C_selectedStats->m32F2_drawSecondGauge = 0;
            }
        }
    } while (cont);
    disableDraw428();
    freeMenuPointerGlyph(0);
}
