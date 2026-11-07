/*
 * Items submenu main loop; ether/list pages (m430). Provisional file
 * 0x801DBE54-0x801DE29C; owns .rodata 0x801C50FC-0x801C515C. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"

extern u8 partyCardsAllocated; /* party info cards allocated (m1E08) */
extern u16 menuUsableEtherMask[]; /* per character: usable-ether mask (setupEtherListPage) */
extern s32 etherListValueX[]; /* list value x (setupEtherListPage) */
extern s32 etherListValueY[]; /* list value y (setupEtherListPage) */
void setupTargetPartyCards(u8 mode);
void applyEtherHeal(); /* called unprototyped here: the ether id (s32 cursor) is passed without andi */
void setupEtherListPage(u8 slot, u8 mode);
void useEtherOnParty(u8 slot, s32 cursor, u8 mode);

/* items submenu main loop: 2 columns x 8 rows, page scroll, swap, use */
u8 openItemMenu(void) {
    u8 keepGoing;
    u8 first;
    s32 page;
    s32 prevPage;
    s32 cursor;
    s32 prevCursor;
    s32 swap;

    keepGoing = 1;
    first = 1;
    page = 0;
    prevPage = 0xFF;
    cursor = 0;
    prevCursor = 0xFF;
    swap = 0xFF;
    initItemMenu();
    computeItemListScrollBar();
    allocMenuPointerGlyph(0);
    allocMenuPointerGlyph(1);
    while (keepGoing) {
        menuDraw();
        if (page != prevPage) {
            setupItemMenuPage(page);
            setupScrollBarThumb(0xC, (itemListScrollStep * page) / 100 + 0x12, itemListScrollThumbHeight);
            prevPage = page;
        }
        updateMenuPointerGlyph(cursor, page, 0, 0);
        if (cursor != prevCursor) {
            setupItemMenuSelection(cursor, page);
            prevCursor = cursor;
        }
        if (first) {
            iniMenuContext364And380(3, 0xC, 0xA, 0x124, 0x84, 0, 1, 4, 1);
            iniMenuContext364And380(4, 8, 0x8F, 0x130, 0x22, 0, 1, 4, 0);
            first = 0;
            processLoadSaveMenuSub1();
            processLoadSaveMenuSub2(0, 0);
        }
        updateMenuPointerGlyph(swap, page, 1, 1);
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (swap == 0xFF) {
                swap = page * 2 + cursor;
            } else {
                if (page * 2 + cursor == swap) {
                    if (useItemOnParty(page, cursor)) {
                        prevPage = 0xFF;
                        prevCursor = 0xFF;
                    }
                } else {
                    swapInventorySlots(page * 2 + cursor, swap);
                    prevPage = 0xFF;
                    prevCursor = 0xFF;
                }
                swap = 0xFF;
            }
            break;
        case 5:
            if (swap == 0xFF) {
                keepGoing = 0;
            } else {
                swap = 0xFF;
            }
            break;
        case 1:
            if (cursor + 2 >= 16) {
                page++;
                if (page > itemListMaxPage) {
                    page--;
                }
            } else {
                cursor += 2;
            }
            prevCursor = 0xFF;
            break;
        case 3:
            if (cursor - 2 < 0) {
                page--;
                if (page < 0) {
                    page++;
                }
            } else {
                cursor -= 2;
            }
            prevCursor = 0xFF;
            break;
        case 0:
            if (cursor + 1 >= 16) {
                page++;
                if (page > itemListMaxPage) {
                    page--;
                } else {
                    cursor = 14;
                }
            } else {
                cursor += 1;
            }
            prevCursor = 0xFF;
            break;
        case 2:
            if (cursor - 1 < 0) {
                page--;
                if (page < 0) {
                    page++;
                } else {
                    cursor = 1;
                }
            } else {
                cursor -= 1;
            }
            prevCursor = 0xFF;
            break;
        case 9:
            page += 8;
            if (page > itemListMaxPage) {
                page = itemListMaxPage;
            }
            prevCursor = 0xFF;
            break;
        case 10:
            page -= 8;
            if (page < 0) {
                page = 0;
            }
            prevCursor = 0xFF;
            break;
        }
    }
    disableDraw428();
    freeMenuPointerGlyph(0);
    freeMenuPointerGlyph(1);
    countField33C_C(8, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
    return 1;
}

/* m430 alloc + setup for list mode 0/1/2 */
void initEtherListMenu(u8 mode) {
    u8 bg;
    s32 set;

    set = 0;
    gMenuContext->m430_etherMenu = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_430), (s32)0x1094), 0);
    bzero(gMenuContext->m430_etherMenu, (s32)sizeof(struct MenuContext_430));
    switch (mode) {
    case 0:
        bg = 2;
        break;
    case 1:
        bg = 5;
        break;
    case 2:
        bg = 6;
        set = 1;
        break;
    }
    j_setupMenuContext4E0(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig[set], gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
    loadSharedMenuData2(bg);
    setupDraw428(2);
    initStatusMenuSub1(2, mode);
}

/* m430 teardown */
void freeEtherListMenu(u8 mode) {
    u8 bg;

    processLoadSaveMenuSub7Sub0(3);
    processLoadSaveMenuSub7Sub0(4);
    processLoadSaveMenuSub7Sub0(5);
    processLoadSaveMenuSub7Sub0(6);
    disableDraw428();
    gMenuContext->m33C_drawFlags->m4A_draw430 = 0;
    menuDraw();
    switch (mode) {
    case 0:
        bg = 2;
        break;
    case 1:
        bg = 5;
        break;
    case 2:
        bg = 6;
        break;
    }
    loadSharedMenuData2(bg | 0x10);
    freeHeap(gMenuContext->m430_etherMenu);
    gMenuContext->m33C_drawFlags->m6_drawPlayTime = 1;
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 1;
}

/* list page fill for modes 0-2: ether names (character / gear) or gear skills, their MP/fuel\n * costs as digit strings, the MP/fuel lines (entries 12, 13), selectability flags */
void setupEtherListPage(u8 slot, u8 mode) {
    u8 codes[10];
    u8 str[16];
    RECT rect;
    s32 pow10[5];
    u8* image;
    s32 i;
    s32 j;
    s32 k;
    s32 q;
    s32 v;
    s32 value;
    s32 limit;
    s32 digits;
    s32 half;
    u8 learned;
    u8 lead;
    u8 flag;
    u8 extra;

    extra = 0;
    limit = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m50_MP;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    pow10[0] = 1;
    pow10[1] = 10;
    pow10[2] = 100;
    pow10[3] = 1000;
    pow10[4] = 10000;
    for (i = 4; i >= 0; i--) {
        codes[i * 2 + 1] = 0;
    }
    for (i = 0; i < 14; i++) {
        bzero(codes, 8);
        learned = 0;
        switch (mode) {
        case 0:
            if (menu2CheckBitmaskMsbFirst(gameState.m16C0_abilities[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m0_unlockedAbilitiesBitField[1], i) ||
                i >= 12) {
                learned = 1;
            }
            break;
        case 1:
            if (menu2CheckBitmaskMsbFirst(gameState.m16C0_abilities[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m0_unlockedAbilitiesBitField[3], i) ||
                i >= 12) {
                learned = 1;
            }
            break;
        case 2:
            if (!(i & 1)) {
                if (menu2CheckBitmaskMsbFirst(gameState.m16C0_abilities[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m1A_unlockedGearAbilities, i / 2) || i >= 12) {
                    learned = 1;
                }
            }
            break;
        }
        if (learned) {
            digits = 2;
            if (i < 12) {
                switch (mode) {
                case 0:
                    gMenuContext->m430_etherMenu->m0_names[i].m7E_stringWidth = renderString(
                        drawCircleMenuChi_updateSub0Sub1(gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] * 16 + i), image, 0x24, 0);
                    value = ((MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]] + 0x370))[i].m13_cost;
                    break;
                case 1:
                    gMenuContext->m430_etherMenu->m0_names[i].m7E_stringWidth = renderString(
                        drawCircleMenuChi_updateSub0Sub2(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum * 16 + i), image, 0x24, 0);
                    value = ((MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] + 0x348))[i].m13_cost;
                    break;
                case 2:
                    half = i / 2;
                    gMenuContext->m430_etherMenu->m0_names[i].m7E_stringWidth = renderString(
                        getGearFuelAbilityName(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum * 4 + half), image, 0x24, 0);
                    limit = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m38_fuel;
                    value = ((MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] + 0x5C8))[half].m24_fuelCost;
                    digits = 4;
                    break;
                }
            } else if (i == 12) {
                if (mode != 2) {
                    value = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m50_MP;
                } else {
                    extra = 3;
                    value = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m38_fuel;
                    digits = 5;
                }
            } else {
                value = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m52_MaxMP;
            }
            v = value;
            lead = 0;
            for (j = digits - 1, k = 0; j > 0; k++, j--) {
                q = v / pow10[j];
                if (q != 0 || lead) {
                    codes[k * 2] = q + 0x10;
                    lead = 1;
                    v -= q * pow10[j];
                } else {
                    codes[k * 2] = 0xC3;
                }
            }
            codes[k * 2] = v % 10 + 0x10;
            decodeCharacterCodes((u16*)codes, str, digits);
            gMenuContext->m430_etherMenu->m700_values[i].m7E_stringWidth = renderString(str, image, 0x24, 1);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            DrawSync(0);
            flag = 0x80;
            if (i < 12) {
                switch (mode) {
                case 0:
                    if (menu2CheckBitmask(menuUsableEtherMask[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]], i)) {
                        if (limit < value) {
                            flag = 0;
                        }
                    } else {
                        flag = 0;
                    }
                    break;
                case 1:
                    flag = 0;
                    break;
                case 2:
                    if (i != 0) {
                        flag = 0;
                    } else if (limit < value) {
                        flag = 0;
                    }
                    break;
                }
            }
            if (i < 12) {
                initMenuContext_1DE0(&gMenuContext->m430_etherMenu->m0_names[i], i, 0x80, flag | 1);
                initMemoryCardPolyVerts(gMenuContext->m430_etherMenu->m0_names[i].m50_vertex, ((i % 2) * 0x88 + 0x24) & 0xFFFC,
                              ((i / 2) * 16 + 0x12) & 0xFFFE, gMenuContext->m430_etherMenu->m0_names[i].m7E_stringWidth, 0xD);
            }
            initMenuContext_1DE0(&gMenuContext->m430_etherMenu->m700_values[i], i, 0x80, flag | 2);
            initMemoryCardPolyVerts(gMenuContext->m430_etherMenu->m700_values[i].m50_vertex, etherListValueX[i], etherListValueY[i],
                          gMenuContext->m430_etherMenu->m700_values[i].m7E_stringWidth, 0xD);
            gMenuContext->m430_etherMenu->m0_names[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m430_etherMenu->m700_values[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m430_etherMenu->m1084_flags[i] = flag | 1;
        } else {
            gMenuContext->m430_etherMenu->m1084_flags[i] = 0;
        }
    }
    freeHeap(image);
    updateMenuSelection2(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, 6, 1, extra + 2);
    updateMenuSelection2(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, 7, 1, extra + 2);
    setupCharacterNameLabel(&gMenuContext->m430_etherMenu->m1000_characterName, slot, mode, 1);
    gMenuContext->m33C_drawFlags->m4A_draw430 = 1;
}

void setupEtherListSelection(u8 slot, u8 cursor, u8 mode);
/* selected list entry: description, target menu and the two option labels */
void setupEtherListSelection(u8 slot, u8 cursor, u8 mode) {
    u16 idx;
    u8* image;
    RECT rect;
    u16* flags;
    u16 t;
    u8 sel;
    u8 second;

    switch (mode) {
    case 0:
        idx = (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] << 5) + cursor * 2;
        break;
    case 1:
        idx = (gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum << 5) + cursor * 2;
        break;
    case 2:
        idx = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum * 8 + cursor / 2 * 2;
        break;
    }
    if (gMenuContext->m430_etherMenu->m1084_flags[cursor] != 0) {
        image = allocateHeap(PC_HEAP_BYTES(0x618), 0);
        bzero(image, 0x618);
        gMenuContext->m430_etherMenu->mF00_description[0].m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m430_etherMenu->m1080_descriptions, idx), image, 0x39, 0);
        gMenuContext->m430_etherMenu->mF00_description[1].m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m430_etherMenu->m1080_descriptions, idx + 1), image, 0x39, 1);
        rect.x = 0x140;
        rect.y = 0x4E;
        rect.w = 0x3C;
        rect.h = 0xD;
        LoadImage(&rect, image);
        DrawSync(0);
        initMenuContext_1DE0(&gMenuContext->m430_etherMenu->mF00_description[0], 0, 0, 0);
        setupMenuPolyFT4(&gMenuContext->m430_etherMenu->mF00_description[0].m0_polys[gMenuContext->m308_oddOrEven], 0x1C, 0x9E, 0, 0x4E,
                      gMenuContext->m430_etherMenu->mF00_description[0].m7E_stringWidth, 0xD);
        initMemoryCardPolyVerts(gMenuContext->m430_etherMenu->mF00_description[0].m50_vertex, 0x1C, 0x9E, gMenuContext->m430_etherMenu->mF00_description[0].m7E_stringWidth, 0xD);
        initMenuContext_1DE0(&gMenuContext->m430_etherMenu->mF00_description[1], 1, 0, 0);
        setupMenuPolyFT4(&gMenuContext->m430_etherMenu->mF00_description[1].m0_polys[gMenuContext->m308_oddOrEven], 0x1C, 0xAE, 0, 0x4E,
                      gMenuContext->m430_etherMenu->mF00_description[1].m7E_stringWidth, 0xD);
        initMemoryCardPolyVerts(gMenuContext->m430_etherMenu->mF00_description[1].m50_vertex, 0x1C, 0xAE, gMenuContext->m430_etherMenu->mF00_description[1].m7E_stringWidth, 0xD);
        freeHeap(image);
        memmove(&gMenuContext->m430_etherMenu->mE00_selectedName, &gMenuContext->m430_etherMenu->m0_names[cursor], (s32)sizeof(MenuContext_4E0));
        initMemoryCardPolyVerts(gMenuContext->m430_etherMenu->mE00_selectedName.m50_vertex, 0x12, 0x8E, gMenuContext->m430_etherMenu->m0_names[cursor].m7E_stringWidth, 0xD);
        (gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->r0 = 0x80;
        (gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->g0 = 0x80;
        (gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->b0 = 0x80;
        SetSemiTrans(&gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys[gMenuContext->m308_oddOrEven], 0);
        countField33C_C(8, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
        switch (mode) {
        case 0:
            flags = (u16*)((u8*)gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]] + cursor * 0x28 + 0x370);
            break;
        case 1:
            flags = (u16*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] + cursor * 0x28 + 0x348);
            break;
        case 2:
            flags = (u16*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] + (cursor / 2) * 0x28 + 0x5C8);
            break;
        }
        t = *flags;
        if (t & 0x4000) {
            sel = 2;
        } else if (t & 0x1000) {
            sel = 0;
        } else {
            sel = 1;
        }
        updateMenuSelection2(8, &gMenuContext->m4E0_labels[24], listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, sel, 0, 2);
        second = (*flags & 3) + 3;
        updateMenuSelection2(8, &gMenuContext->m4E0_labels[24], listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, second, 0, 2);
        gMenuContext->m430_etherMenu->mE00_selectedName.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m430_etherMenu->mE80_selectedValue.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m430_etherMenu->mF00_description[0].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m430_etherMenu->mF00_description[1].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m430_etherMenu->m1092_showSelected = 1;
        gMenuContext->m33C_drawFlags->m38_drawListMenuLabels[6] = 1;
        gMenuContext->m33C_drawFlags->m38_drawListMenuLabels[7] = 1;
    } else {
        s32 i;

        gMenuContext->m430_etherMenu->m1092_showSelected = 0;
        for (i = 0; i < 6; i++) {
            gMenuContext->m33C_drawFlags->m38_drawListMenuLabels[i] = 0;
        }
    }
}

/* m430 list: set fade mode of its prims */
void setEtherListShadeMode(u8 mode) {
    s32 i;

    setMenuBoxShadeMode(5, mode);
    setMenuBoxShadeMode(6, mode);
    for (i = 0; i < 12; i++) {
        if (gMenuContext->m430_etherMenu->m0_names[i].m0_polys[gMenuContext->m430_etherMenu->m0_names[i].m7D_oddOrEven].r0 != 0x20) {
            setMenuPolyShadeMode(&gMenuContext->m430_etherMenu->m0_names[i].m0_polys[gMenuContext->m430_etherMenu->m0_names[i].m7D_oddOrEven], mode);
            setMenuPolyShadeMode(&gMenuContext->m430_etherMenu->m700_values[i].m0_polys[gMenuContext->m430_etherMenu->m700_values[i].m7D_oddOrEven], mode);
        }
    }
    setMenuPolyShadeMode(&gMenuContext->m444_pointerGlyphs[0]->m0_polys[gMenuContext->m444_pointerGlyphs[0]->m75_oddOrEven], mode);
    setMenuPolyShadeMode(&gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys[gMenuContext->m430_etherMenu->mE00_selectedName.m7D_oddOrEven], mode);
    for (i = 0; i < 2; i++) {
        setMenuPolyShadeMode(&gMenuContext->m430_etherMenu->mF00_description[i].m0_polys[gMenuContext->m430_etherMenu->mF00_description[i].m7D_oddOrEven], mode);
    }
}

/* use an ether/skill on party members: MP (or gear fuel) cost, heal targets (like useItemOnParty) */
void useEtherOnParty(u8 slot, s32 cursor, u8 mode) {
    MenuEtherEntry* ether;
    u8 mask;
    u8 redraw;
    u8 used;
    u8 heal;
    s32 target;
    s32 extra;
    s32 all;
    s32 i;

    mask = 1;
    redraw = 1;
    extra = 0;
    target = gMenuContext->m4DC_currentlySelectedCharacter;
    switch (mode) {
    case 0:
        ether = (MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]] +
                                  cursor * 0x28 + 0x370);
        break;
    case 1:
        ether = (MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] +
                                  cursor * 0x28 + 0x348);
        break;
    case 2:
        extra = 0x18;
        ether = (MenuEtherEntry*)((u8*)gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum] +
                                  cursor * 0x28 + 0x5C8);
        target = slot;
        break;
    }
    iniMenuContext364And380(2, 0x10, 0xE, extra + 0x90, 0xB0, 0, 0, 4, 0);
    all = ether->m0_flags & 1;
    while (mask) {
        menuDraw();
        if (redraw) {
            setupEtherListPage(slot, mode);
            setupEtherListSelection(slot, cursor, mode);
            setEtherListShadeMode(1);
            setupTargetPartyCards(mode);
            redraw = 0;
        }
        mask = 0;
        gMenuContext->m428_cardCursors->m140_enabled[0] = gMenuContext->m428_cardCursors->m140_enabled[1] = gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
        if (all) {
            for (i = 0; i < 3; i++) {
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                    mask |= 1 << i;
                    gMenuContext->m428_cardCursors->m140_enabled[i] = 1;
                }
            }
        } else {
            mask = 1 << target;
            gMenuContext->m428_cardCursors->m140_enabled[target] = 1;
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        if (mode != 2) {
            if (gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m50_MP - ether->m13_cost < 0) {
                mask = 0;
            }
        } else if (gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m38_fuel - ether->m24_fuelCost < 0) {
            mask = 0;
        }
        if (mask) {
            switch (gMenuContext->m325_menuButton) {
            case 5:
                mask = 0;
                break;
            case 4:
                used = 0;
                for (i = 0; i < 3; i++) {
                    heal = 0;
                    if (menu2CheckBitmask(mask, i)) {
                        if (mode != 2) {
                            if (gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m4C_HP !=
                                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m4E_MaxHP) {
                                used = 1;
                                heal = 1;
                            }
                        } else {
                            if (gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum].m60_hp !=
                                gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[cursor]].mA0_gearNum].m64_maxHp) {
                                used = 1;
                                heal = 1;
                            }
                        }
                        if (heal) {
                            applyEtherHeal(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[slot],
                                          gMenuContext->m33C_drawFlags->m30_partyMemberId[i], cursor, mode);
                        }
                    }
                }
                if (used) {
                    if (mode != 2) {
                        gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m50_MP -= ether->m13_cost;
                    } else {
                        gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m38_fuel -=
                            ether->m24_fuelCost;
                    }
                    playMenuSoundEffect(0x37);
                } else {
                    playMenuSoundEffect(4);
                }
                redraw = 1;
                break;
            case 1:
                if (mode == 0) {
                    target = findNextPartySlot(target, 0, 0);
                }
                break;
            case 3:
                if (mode == 0) {
                    target = findNextPartySlot(target, 1, 0);
                }
                break;
            }
        }
    }
    gMenuContext->m428_cardCursors->m140_enabled[0] = gMenuContext->m428_cardCursors->m140_enabled[1] = gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
    setEtherListShadeMode(0);
    gMenuContext->m33C_drawFlags->m46_draw1E08 = 0;
    menuDraw();
    if (partyCardsAllocated != 0) {
        for (i = 0; i < 3; i++) {
            freeHeap(gMenuContext->m1E08_partyCards[i]);
        }
        partyCardsAllocated = 0;
    }
    processLoadSaveMenuSub7Sub0(2);
}

extern s32 etherListBox6Widths[];
extern s32 etherListBox4X[];
extern s32 etherListBox4Widths[];
void setupEtherListPage(u8 slot, u8 mode);
void useEtherOnParty(u8 slot, s32 cursor, u8 mode);

/* m430 list page loop, modes 0-2 (the slot parameter is copied to a local) */
void runEtherListMenu(u8 slotArg, u8 flag, u8 mode) {
    u8 slot;
    u8 keepGoing;
    u8 first;
    s32 cursor;
    s32 prevCursor;
    u8 prevSlot;

    keepGoing = 1;
    first = 1;
    cursor = 0;
    prevCursor = 0xFF;
    slot = slotArg;
    prevSlot = 0xFF;
    initEtherListMenu(mode);
    allocMenuPointerGlyph(0);
    do {
        menuDraw();
        if (slot != prevSlot) {
            setupEtherListPage(slot, mode);
            prevSlot = slot;
            prevCursor = 0xFF;
        }
        updateMenuPointerGlyph(cursor, 0, 2, 0);
        if (cursor != prevCursor) {
            setupEtherListSelection(slot, cursor, mode);
            prevCursor = cursor;
        }
        if (first) {
            iniMenuContext364And380(6, 0x10, 0xA, etherListBox6Widths[mode], 0x70, 0, 1, 4, 0);
            iniMenuContext364And380(5, 0xC, 0x86, 0xAC, 0x38, 0, 1, 4, 0);
            iniMenuContext364And380(4, etherListBox4X[mode], 0xA6, etherListBox4Widths[mode], 0x18, 0, 1, 4, 0);
            iniMenuContext364And380(3, 0xC8, 0x86, 0x50, 0x18, 0, 1, 4, 0);
            if (flag) {
                processLoadSaveMenuSub1();
                processLoadSaveMenuSub2(0, 0);
                menuDraw();
            }
            gMenuContext->m33C_drawFlags->m6_drawPlayTime = 0;
            gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 0;
            first = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (gMenuContext->m430_etherMenu->m1084_flags[cursor] & 0x80) {
                useEtherOnParty(slot, cursor, mode);
                prevSlot = 0xFF;
                prevCursor = 0xFF;
            }
            break;
        case 5:
            keepGoing = 0;
            break;
        case 0:
            if (mode != 2) {
                if (++cursor >= 12) {
                    cursor = 11;
                }
            }
            break;
        case 2:
            if (mode != 2) {
                if (--cursor < 0) {
                    cursor = 0;
                }
            }
            break;
        case 1:
            if (cursor + 2 < 12) {
                cursor += 2;
            }
            break;
        case 3:
            if (cursor - 2 >= 0) {
                cursor -= 2;
            }
            break;
        case 9:
            slot = findNextPartySlot(slot, 0, mode);
            break;
        case 10:
            slot = findNextPartySlot(slot, 1, mode);
            break;
        }
    } while (keepGoing);
    countField33C_C(8, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
    freeMenuPointerGlyph(0);
}

/* The zero word after runEtherListMenu's jump table (0x801C5158, end of this object's .rodata;
 * m_801DE29C's .rodata ends the same way). Unreferenced. */
const s32 D_801C5158 = 0;
