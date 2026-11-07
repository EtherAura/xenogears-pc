/*
 * Load/save submenu (Noah processLoadSaveMenu), then the items submenu helpers from
 * initItemMenu. Provisional file 0x801D9F98-0x801DBE54. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"

/* Noah processLoadSaveMenu (the switch needs a dead `case 3`) */
u8 processLoadSaveMenu(u8 arg0, u8 arg1) {
    u8 keepOpen;
    u8 first;
    u8 success;
    u8 config;

    keepOpen = 1;
    first = 1;
    success = 1;
    menuVarUnk0 = 0;
    gMenuContext->m338_currentSelectedSubMenuEntry = 2;
    config = 0;
    if (menuToEnter == 0 && g_menuOpenArg == 0) {
        gMenuContext->m338_currentSelectedSubMenuEntry = 1;
    }
    gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
    processLoadSaveMenuSub0();
    menuDraw();
    while (keepOpen) {
        gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
        gMenuContext->m4D8_cardTilesMode = 1;
        if (first) {
            initMenuContext34C();
            initMenuContext3A8_memoryCardDisplay();
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0] = 1;
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1] = 1;
            processLoadSaveMenuSub1();
            switch (menuToEnter) {
            case 0:
                processLoadSaveMenuSub2(0, 0);
                if (g_menuOpenArg == 0) {
                    config = 9;
                }
                break;
            case 2:
                config = 7;
                break;
            case 3:
                break;
            }
            processLoadSaveMenuSub3(config);
            gMenuContext->m33C_drawFlags->m6_drawPlayTime = 0;
            first = 0;
            gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 0;
            if (processLoadSaveMenuSub4()) {
                if (menuToEnter != 0) {
                    success = 0;
                }
                break;
            }
            gMenuContext->m33C_drawFlags->m68_drawCardLabels = 1;
        }
        if (needProcessLoadSaveMenuSub7) {
            processLoadSaveMenuSub5(0x20);
        }
        if (processLoadSaveMenuSub6()) {
            keepOpen = 0;
        }
        if (needProcessLoadSaveMenuSub7) {
            processLoadSaveMenuSub7();
            needProcessLoadSaveMenuSub7 = 0;
        }
        if (!keepOpen) {
            break;
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            updateMenuSelection2(6, gMenuContext->mDE0_saveMenuLabels, menuConfig2, saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels,
                          gMenuContext->m338_currentSelectedSubMenuEntry, 7, 0);
            if (menuToEnter != 2) {
                processLoadSaveMenuSub8(0);
            } else {
                processLoadSaveMenuSub8(7);
            }
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            resetCursorState();
            countField33C_C(6, gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels);
            keepOpen = updateSaveState(arg1);
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            processLoadSaveMenuSub0();
            if (menuToEnter == 2) {
                break;
            }
            /* fallthrough */
        case 5:
            keepOpen = 0;
            if (arg0) {
                success = 0;
            }
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
        }
    }
    if (*(u16*)gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent == 0 && menuToEnter != 0) {
        success = 0;
    }
    gMenuContext->m33C_drawFlags->m68_drawCardLabels = 0;
    gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    countField33C_C(6, gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels);
    closeMemoryCardsEvents();
    return success;
}

/* item menu setup: labels m10E0, allocate m42C, load the item data */
void initItemMenu(void) {
    setupDraw428(2);
    j_setupMenuContext4E0(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
    gMenuContext->m42C_itemMenu = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_42C), (s32)0x1198), 0);
    bzero(gMenuContext->m42C_itemMenu, (s32)sizeof(struct MenuContext_42C));
    loadSharedMenuData2(0);
}

/* item menu teardown */
void freeItemMenu(void) {
    freeScrollBarThumb();
    processLoadSaveMenuSub7Sub0(3);
    processLoadSaveMenuSub7Sub0(4);
    gMenuContext->m33C_drawFlags->m48_draw42C = 0;
    loadSharedMenuData2(0x10);
    freeHeap(gMenuContext->m42C_itemMenu->m1180_descriptions);
    freeHeap(gMenuContext->m42C_itemMenu);
    freeHeap(gMenuContext->m330_itemData->m1C_usableItems);
}

/* item page render */
void setupItemMenuPage(s32 page) {
    u8 codes[4];
    u8 str[8];
    RECT rect;
    u8* image;
    s32 i;
    u8 tens;
    u8 attr;
    u8 flags;
    s16 x; /* HImode: its add is not a 'birthing' insn for sched1 */

    image = ALLOC_TEXT_IMAGE(0x3F6);
    codes[1] = 0;
    codes[3] = 0;
    for (i = 0; i < 16; i++) {
        if (gameState.m2026_inventory0Ids[page * 2 + i] != 0) {
            if (gameState.m1F90_inventory0Counts[page * 2 + i] != 0) {
                if (gameState.m1F90_inventory0Counts[page * 2 + i] >= 100) {
                    gameState.m1F90_inventory0Counts[page * 2 + i] = 99;
                }
                gMenuContext->m42C_itemMenu->m0_names[i].m7E_stringWidth =
                    renderString(getUsableItemName(gameState.m2026_inventory0Ids[page * 2 + i]), image, 0x24, 0);
                tens = gameState.m1F90_inventory0Counts[page * 2 + i] / 10;
                if (tens != 0) {
                    codes[0] = tens + 0x10;
                } else {
                    codes[0] = 0xC3;
                }
                codes[2] = gameState.m1F90_inventory0Counts[page * 2 + i] % 10 + 0x10;
                decodeCharacterCodes((u16*)codes, str, 2);
                gMenuContext->m42C_itemMenu->m800_counts[i].m7E_stringWidth = renderString(str, image, 0x24, 1);
                rect.x = (i & 1) * 24 + 0x180;
                rect.y = (i / 2) * 13 + 0x80;
                rect.w = 0x28;
                rect.h = 0xD;
                LoadImage(&rect, image);
                DrawSync(0);
                attr = gMenuContext->m330_itemData->m1C_usableItems[gameState.m2026_inventory0Ids[page * 2 + i]].m6_flags;
                if ((attr & 0x20) && g_menuOpenArg == 0) {
                    flags = 0;
                } else {
                    flags = attr & 0x80;
                }
                initMenuContext_1DE0(&gMenuContext->m42C_itemMenu->m0_names[i], i, 0x80, flags | 1);
                initMenuContext_1DE0(&gMenuContext->m42C_itemMenu->m800_counts[i], i, 0x80, flags | 2);
                x = (i % 2) * 0x88 + 0x28;
                initMemoryCardPolyVerts(gMenuContext->m42C_itemMenu->m0_names[i].m50_vertex, x & ~7, (i / 2) * 16 | 0xE,
                              gMenuContext->m42C_itemMenu->m0_names[i].m7E_stringWidth, 0xD);
                initMemoryCardPolyVerts(gMenuContext->m42C_itemMenu->m800_counts[i].m50_vertex, (u16)((i % 2) * 0x88 + 0x90) & ~7, (i / 2) * 16 | 0xE,
                              gMenuContext->m42C_itemMenu->m800_counts[i].m7E_stringWidth, 0xD);
                gMenuContext->m42C_itemMenu->m0_names[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
                gMenuContext->m42C_itemMenu->m800_counts[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
                gMenuContext->m42C_itemMenu->m1184_enabled[i] = 1;
            } else {
                gameState.m2026_inventory0Ids[page * 2 + i] = 0;
                gMenuContext->m42C_itemMenu->m1184_enabled[i] = 0;
            }
        } else {
            gameState.m1F90_inventory0Counts[page * 2 + i] = 0;
            gMenuContext->m42C_itemMenu->m1184_enabled[i] = 0;
        }
    }
    freeHeap(image);
    gMenuContext->m33C_drawFlags->m48_draw42C = 1;
}

/* selected item: name/count copies, description, target menu */
void setupItemMenuSelection(s32 sel, s32 page);
void setupItemMenuSelection(s32 sel, s32 page) {
    u8* id;
    u16* image;
    RECT rect;
    s32 dx;
    u8 a;
    u8 b;
    MenuUsableItem* item;
    u16 target;
    u8* ids;

    ids = gameState.m2026_inventory0Ids;
    id = &ids[page * 2 + sel];
    if (*id != 0) {
        image = allocateHeap(PC_HEAP_BYTES(0x618), 0);
        bzero(image, 0x618);
        gMenuContext->m42C_itemMenu->m1100_description.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m42C_itemMenu->m1180_descriptions, *id), image, 0x39, 0);
        rect.x = 0x140;
        rect.y = 0x4E;
        rect.w = 0x3C;
        rect.h = 0xD;
        LoadImage(&rect, image);
        DrawSync(0);
        initMenuContext_1DE0(&gMenuContext->m42C_itemMenu->m1100_description, 0, 0, 0);
        setupMenuPolyFT4(&gMenuContext->m42C_itemMenu->m1100_description.m0_polys[gMenuContext->m308_oddOrEven], 0x1C, 0xA1, 0, 0x4E,
                      gMenuContext->m42C_itemMenu->m1100_description.m7E_stringWidth, 0xD);
        initMemoryCardPolyVerts(gMenuContext->m42C_itemMenu->m1100_description.m50_vertex, 0x1C, 0xA1,
                      gMenuContext->m42C_itemMenu->m1100_description.m7E_stringWidth, 0xD);
        freeHeap(image);
        dx = (gMenuContext->m42C_itemMenu->m800_counts[sel].m7E_stringWidth == 0x10) * 4;
        memmove(&gMenuContext->m42C_itemMenu->m1000_selectedName, &gMenuContext->m42C_itemMenu->m0_names[sel], (s32)sizeof(MenuContext_4E0));
        memmove(&gMenuContext->m42C_itemMenu->m1080_selectedCount, &gMenuContext->m42C_itemMenu->m800_counts[sel], (s32)sizeof(MenuContext_4E0));
        initMemoryCardPolyVerts(gMenuContext->m42C_itemMenu->m1000_selectedName.m50_vertex, 0x10, 0x93,
                      gMenuContext->m42C_itemMenu->m0_names[sel].m7E_stringWidth, 0xD);
        initMemoryCardPolyVerts(gMenuContext->m42C_itemMenu->m1080_selectedCount.m50_vertex, dx | 0x78, 0x93,
                      gMenuContext->m42C_itemMenu->m800_counts[sel].m7E_stringWidth, 0xD);
        (gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->r0 = 0x80;
        (gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->g0 = 0x80;
        (gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys + gMenuContext->m308_oddOrEven)->b0 = 0x80;
        SetSemiTrans(&gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys[gMenuContext->m308_oddOrEven], 0);
        (gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys + gMenuContext->m308_oddOrEven)->r0 = 0x80;
        (gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys + gMenuContext->m308_oddOrEven)->g0 = 0x80;
        (gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys + gMenuContext->m308_oddOrEven)->b0 = 0x80;
        SetSemiTrans(&gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys[gMenuContext->m308_oddOrEven], 0);
        countField33C_C(8, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
        item = &gMenuContext->m330_itemData->m1C_usableItems[*id];
        if (item->m6_flags & 0xC0) {
            target = item->m4_target;
            if (target & 0x4000) {
                a = 2;
            } else if (target & 0x1000) {
                a = 0;
            } else {
                a = 1;
            }
            updateMenuSelection2(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, a, 0, 1);
            b = (item->m4_target & 3) + 3;
            updateMenuSelection2(8, gMenuContext->m10E0_listMenuLabels, listMenuLabelConfig[1], saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels, b, 0, 1);
#if !VERSION_JP
            ((gMenuContext->m10E0_listMenuLabels + a)->m0_polys + gMenuContext->m308_oddOrEven)->r0 = 0x80;
            ((gMenuContext->m10E0_listMenuLabels + a)->m0_polys + gMenuContext->m308_oddOrEven)->g0 = 0x80;
            ((gMenuContext->m10E0_listMenuLabels + a)->m0_polys + gMenuContext->m308_oddOrEven)->b0 = 0x80;
            SetSemiTrans(&gMenuContext->m10E0_listMenuLabels[a].m0_polys[gMenuContext->m308_oddOrEven], 0);
            ((gMenuContext->m10E0_listMenuLabels + b)->m0_polys + gMenuContext->m308_oddOrEven)->r0 = 0x80;
            ((gMenuContext->m10E0_listMenuLabels + b)->m0_polys + gMenuContext->m308_oddOrEven)->g0 = 0x80;
            ((gMenuContext->m10E0_listMenuLabels + b)->m0_polys + gMenuContext->m308_oddOrEven)->b0 = 0x80;
            SetSemiTrans(&gMenuContext->m10E0_listMenuLabels[b].m0_polys[gMenuContext->m308_oddOrEven], 0);
#endif
        }
        gMenuContext->m42C_itemMenu->m1000_selectedName.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m42C_itemMenu->m1080_selectedCount.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m42C_itemMenu->m1100_description.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m42C_itemMenu->m1194_showSelected = 1;
    } else {
        countField33C_C(8, gMenuContext->m33C_drawFlags->m38_drawListMenuLabels);
        gMenuContext->m42C_itemMenu->m1194_showSelected = 0;
    }
}

/* allocate the pointer glyph m444[slot] */
void allocMenuPointerGlyph(u8 arg0) {
    gMenuContext->m444_pointerGlyphs[arg0] = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_444), (s32)0x78), 0);
    bzero(gMenuContext->m444_pointerGlyphs[arg0], (s32)sizeof(struct MenuContext_444));
    gMenuContext->m444_pointerGlyphs[arg0]->m70_frame = 4;
    gMenuContext->m444_pointerGlyphs[arg0]->m74_timer = 0;
}

/* animate + place the pointer glyph of slot m444[slot]; modes 0-3 */
void updateMenuPointerGlyph(s32 index, s32 page, u8 mode, u8 slot) {
    MenuContext_444* p;
    POLY_FT4* poly;
    u8 show;
    s32 x;
    s32 y;

    p = gMenuContext->m444_pointerGlyphs[slot];
    show = 1;
    if (++p->m74_timer >= 6) {
        if (--p->m70_frame < 0) {
            p->m70_frame = 4;
        }
        p->m74_timer = 0;
    }
    switch (mode) {
    case 0:
        x = (index % 2) * 0x88 + 0x1C;
        y = (index / 2) * 16 + 0x11;
        break;
    case 1:
        if (index >= page * 2 && index < page * 2 + 16) {
            x = (index % 2) * 0x88 + 0x18;
            y = ((index - page * 2) / 2) * 16 + 0x11;
        } else {
            show = 0;
        }
        break;
    case 2:
        x = (index % 2) * 0x88 + 0x18;
        y = (index / 2) * 16 + 0x14;
        break;
    case 3:
        x = 0xA0;
        y = index * 13 + 0x14;
        show = 2;
        break;
    }
    if (show) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, p->m70_frame + 0x15B, p->m0_polys, gMenuContext->m308_oddOrEven, 0, 0, 0x1000);
        poly = &p->m0_polys[gMenuContext->m308_oddOrEven];
        initMemoryCardPolyVerts(p->m50_vertex, poly->x0 + x, poly->y0 + y, poly->x1 - poly->x0, poly->y3 - poly->y0);
        p->m75_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m50_draw444[slot] = 1;
    } else {
        gMenuContext->m33C_drawFlags->m50_draw444[slot] = 0;
    }
}

/* free the pointer glyph m444[slot] */
void freeMenuPointerGlyph(u8 arg0) {
    freeHeap(gMenuContext->m444_pointerGlyphs[arg0]);
    gMenuContext->m33C_drawFlags->m50_draw444[arg0] = 0;
}

/* item menu: set the fade mode of all its prims (needs the separate m10E0 array) */
void setItemMenuShadeMode(u8 mode) {
    s32 i;

    setMenuBoxShadeMode(3, mode);
    setMenuBoxShadeMode(4, mode);
    setMenuPolyShadeMode(&gMenuContext->m43C_scrollBarThumb->m0_polys[gMenuContext->m43C_scrollBarThumb->m70_oddOrEven], mode);
    for (i = 0; i < 16; i++) {
        if (gMenuContext->m42C_itemMenu->m0_names[i].m0_polys[gMenuContext->m42C_itemMenu->m0_names[i].m7D_oddOrEven].r0 != 0x20) {
            setMenuPolyShadeMode(&gMenuContext->m42C_itemMenu->m0_names[i].m0_polys[gMenuContext->m42C_itemMenu->m0_names[i].m7D_oddOrEven], mode);
            setMenuPolyShadeMode(&gMenuContext->m42C_itemMenu->m800_counts[i].m0_polys[gMenuContext->m42C_itemMenu->m800_counts[i].m7D_oddOrEven], mode);
        }
    }
    for (i = 0; i < 2; i++) {
        setMenuPolyShadeMode(&gMenuContext->m444_pointerGlyphs[i]->m0_polys[gMenuContext->m444_pointerGlyphs[i]->m75_oddOrEven], mode);
    }
    setMenuPolyShadeMode(&gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys[gMenuContext->m42C_itemMenu->m1000_selectedName.m7D_oddOrEven], mode);
    setMenuPolyShadeMode(&gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys[gMenuContext->m42C_itemMenu->m1080_selectedCount.m7D_oddOrEven], mode);
    setMenuPolyShadeMode(&gMenuContext->m42C_itemMenu->m1100_description.m0_polys[gMenuContext->m42C_itemMenu->m1100_description.m7D_oddOrEven], mode);
    for (i = 0; i < 8; i++) {
        setMenuPolyShadeMode(&(gMenuContext->m10E0_listMenuLabels + i)->m0_polys[gMenuContext->m10E0_listMenuLabels[i].m7D_oddOrEven], mode);
    }
}

extern u8 partyCardsAllocated;
extern s32 partyCardLayoutX[];
extern s32 gearCardLayoutX[];
extern s32 partyCardLayoutY[];
extern s32 gearCardLayoutY[];

/* party info cards for the item target (the mode parameter is reused as the gear flag) */
void setupTargetPartyCards(u8 mode) {
    s32 i;
    s32* table1;
    s32* table2;
    u8 id;
    u8 ok;
    MenuContext_1E08* card;

    if (partyCardsAllocated == 0) {
        for (i = 0; i < 3; i++) {
            gMenuContext->m1E08_partyCards[i] = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_1E08), (s32)0xbec), 0);
            bzero(gMenuContext->m1E08_partyCards[i], (s32)sizeof(struct MenuContext_1E08));
        }
        partyCardsAllocated = 1;
    }
    for (i = 0; i < 3; i++) {
        bzero(gMenuContext->m1E08_partyCards[i], (s32)sizeof(struct MenuContext_1E08));
    }
    if (mode != 2) {
        mode = 0;
        table1 = partyCardLayoutX;
        table2 = partyCardLayoutY;
    } else {
        table1 = gearCardLayoutX;
        table2 = gearCardLayoutY;
        mode = 1;
    }
    for (i = 0; i < 3; i++) {
        card = gMenuContext->m1E08_partyCards[i];
        id = gMenuContext->m33C_drawFlags->m30_partyMemberId[i];
        ok = 1;
        if (id != 0xFF) {
            if (mode) {
                ok = gameState.m26C_party[id].mA0_gearNum != 0xFF;
            }
            if (ok) {
                setupPartyCard(card, id, i, table1, table2, mode);
            }
        } else {
            card->mBE7_enabled = 0;
        }
    }
    gMenuContext->m33C_drawFlags->m46_draw1E08 = 1;
    for (i = 0; i < 3; i++) {
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->x0 = 0x90;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->y0 = i * 0x38 + 0x30;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->x1 = 0xA0;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->y1 = i * 0x38 + 0x30;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->x2 = 0x90;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->y2 = i * 0x38 + 0x40;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->x3 = 0xA0;
        (gMenuContext->m428_cardCursors->m0_polys[0] + (i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]))->y3 = i * 0x38 + 0x40;
    }
}

/* use an item on the party (Noah: none); returns the target mask, 0 when cancelled */
u8 useItemOnParty(s32 page, s32 sel) {
    u16 mask;
    u8 keepGoing;
    s32 target;
    u8 redraw;
    u8 used;
    s32 all;
    s32 i;
    MenuUsableItem* item;

    mask = 0;
    keepGoing = 1;
    target = gMenuContext->m4DC_currentlySelectedCharacter;
    partyCardsAllocated = 0;
    item = &gMenuContext->m330_itemData->m1C_usableItems[gameState.m2026_inventory0Ids[page * 2 + sel]];
    redraw = 1;
    if (item->m6_flags & 0x80) {
        mask = 1;
        if (item->m6_flags & 0x20) {
            mask = g_menuOpenArg != 0;
        }
    }
    all = item->m4_target & 1;
    if (mask) {
        iniMenuContext364And380(2, 0x10, 0xE, 0x90, 0xB0, 0, 0, 4, 0);
        while (keepGoing) {
            menuDraw();
            if (redraw) {
                redraw = 0;
                setupItemMenuPage(page);
                setupItemMenuSelection(sel, page);
                setItemMenuShadeMode(1);
                setupTargetPartyCards(0);
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
            if (gameState.m1F90_inventory0Counts[page * 2 + sel] == 0) {
                keepGoing = 0;
            }
            if (!keepGoing) {
                break;
            }
            switch (gMenuContext->m325_menuButton) {
            case 4:
                used = 0;
                for (i = 0; i < 3; i++) {
                    if (menu2CheckBitmask(mask, i) &&
                        applyItemToCharacter(gMenuContext->m330_itemData, gMenuContext->m33C_drawFlags->m30_partyMemberId[i],
                                      gameState.m2026_inventory0Ids[page * 2 + sel]) == 0) {
                        used |= 1;
                    }
                }
                if (used) {
                    playMenuSoundEffect(0x37);
                    redraw = 1;
                    if (--gameState.m1F90_inventory0Counts[page * 2 + sel] == 0) {
                        gameState.m2026_inventory0Ids[page * 2 + sel] = 0;
                    }
                } else {
                    playMenuSoundEffect(4);
                    redraw = 1;
                }
                break;
            case 5:
                keepGoing = 0;
                mask = 0;
                break;
            case 1:
                target = findNextPartySlot(target, 0, 0);
                break;
            case 3:
                target = findNextPartySlot(target, 1, 0);
                break;
            }
        }
        gMenuContext->m428_cardCursors->m140_enabled[0] = gMenuContext->m428_cardCursors->m140_enabled[1] = gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
        setItemMenuShadeMode(0);
        gMenuContext->m33C_drawFlags->m46_draw1E08 = 0;
        menuDraw();
        for (i = 0; i < 3; i++) {
            freeHeap(gMenuContext->m1E08_partyCards[i]);
        }
        partyCardsAllocated = 0;
        processLoadSaveMenuSub7Sub0(2);
    }
    return mask;
}

/* swap two inventory0 slots */
void swapInventorySlots(s32 a, s32 b) {
    u8 t;
    u8 u;

    t = gameState.m2026_inventory0Ids[b];
    u = gameState.m2026_inventory0Ids[a];
    gameState.m2026_inventory0Ids[a] = t;
    gameState.m2026_inventory0Ids[b] = u;
    t = gameState.m1F90_inventory0Counts[b];
    u = gameState.m1F90_inventory0Counts[a];
    gameState.m1F90_inventory0Counts[a] = t;
    gameState.m1F90_inventory0Counts[b] = u;
}

/* item list scroll bar from the last used inventory0 slot (the parameters are its uninitialised locals) */
void computeItemListScrollBar(s32 arg0, s32 last) {
    s32 i;

    /* With no item, the PS1 keeps the a1 openItemMenu happens to leave, a
     * KSEG0 pointer: negative, so the short list's bar (seen in Redux). */
    last = -1;
    for (i = 0; i < 150; i++) {
        if (gameState.m2026_inventory0Ids[i] != 0) {
            last = i;
        }
    }
    if (last < 16) {
        itemListScrollThumbHeight = 0x74;
        itemListMaxPage = 0;
        itemListScrollStep = 0;
    } else {
        itemListScrollThumbHeight = 0x4A;
        itemListMaxPage = (last - 16) / 2 + 1;
        itemListScrollStep = 0x1068 / itemListMaxPage;
    }
}
