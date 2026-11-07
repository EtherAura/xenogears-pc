/* The shop's own code (buy, sell, equip preview), after the menu library
 * it links (whose last function is updateMenuSelection, the twin of the main menu's
 * updateMenuSelection). gcc-2.6.3-psx, ASPSX 2.34, -G0. */
#include "common.h"
#define MENU_FAMILY_OVERLAY
#include "menu/menu.h"
#include "menu/shop.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pollhost.h"
#include "main/game_state.h"
#include "main/dialog.h"

extern u16 shopMenu2Bitmask[];
extern MenuContext* gMenuContext;
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
u8* getDialogParamPointer(u8* buf, s32 idx);
extern u8 shopInitMenuLabelConfig[];
void loadBlankImage(void);
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count);
void computeMenuBorder(void* font, s32 glyph, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpageX, s32* tpageY);
extern s32 shopCursorPosXTable[];
extern s32 shopCursorPosYTable[];
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
void SetLineF3(void* p);
void setupPolyG4(POLY_G4* p, s32 r, s32 g, s32 b);
void resetCursorState(void);
void initMenuTiles1Sub(POLY_FT4* p);
void initMenuTiles6(u8 index, u16 x, u16 y, s32 width, s32 height);
void initMenuTiles1(u8 index, u16 x, u16 y, u16 width, s32 height);
void initMenuTilesTopFrame(u8 index, u16 x, u16 y, u16 width);
void initMenuTilesBottomFrame(u8 index, u16 x, u16 y, u16 width, s32 height);
void initMenuTiles4(u8 index, u16 x, u16 y, u16 height);
void initMenuTiles5(u8 index, u16 x, u16 y, u16 width, u16 height);
void draw380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 transformDisabled, s32 arg6, u8 arg7);
void setupMenuContext364(u8 boxId);
void transformAndAddPrim(s32 count, SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven);
void drawMultiPolys(s32 count, POLY_FT4* polys, s32 oddOrEven);
void playSoundEffect(s32);
void menuDraw(void);
void updateCursorPolysLocation(s32 idx, u8 draw);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void playMenuSoundEffect(u8 sfx);
s32 runYesNoPrompt(u8 arg0);
void processLoadSaveMenuSub5(u8 msg);
void processLoadSaveMenuSub7(void);
extern void drawMenu_380(void);
extern void drawMenu_428(void);
extern void drawMenu2_sub(void);
extern void drawMenu_348_cursor(void);
extern void drawMenuPointerGlyphs(void);
extern void drawShopMenu(void);
extern void drawScrollBarThumb(void);
extern void drawMainMenu(void);
extern void drawMenuContext354(void);
extern void drawMenuBoxes(void);
extern void drawMenuBackground(void);
void menuDraw(void); /* = menu func_801C7BF4 (Noah menuDraw) */
void initMenuContext32C(u8 arg0);
void initMenuContext33C(u8 arg0);
void initMenuContext350MainMenu(u8 arg0);
void initMenuContext354(u8 arg0);
void initMenuContext330(u8 arg0);
void initMenuContext348(u8 arg0);
void initMenuContext1E20(u8 arg0);
void initMenuContext450Shop(u8 arg0);
void loadShopData(u8 arg0);
void stopSequence(void*);
void unloadSequence(void*);
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count); /* = menu func_801E7E68 (Noah setupMenuContext4E0) */
u8 openShopSellMenu(void);
u8 runShopBuyMenu(void);
void cleanupShopMainMenuSelection(void);
extern u8 shopMainLabelConfig[];
void initMenuGlobals(void);
void resetMenuOddOrEven(void);
void initMenuContext4E0(void);
void initPrimitives_348_cursor(void);
void computeMenuBorders(void);
void setupShopStock(void);
void shopMainMenuExecute(void);
s32 isControllerConnected(s32 port);
void resumeMusic(void);
void pauseMusic(void);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField;
void checkSoftReboot(void);
extern s32* pRunningOnDTL;
void updateMenuInput(void);
void updateMenuTransforms(void); /* = menu func_801D1D40 (Noah updateMenuTransforms) */
void drawMenuSpecificElements(void);
void loadMenuSharedResources(void);
char* strcpy(char* dst, const char* src);
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
extern u32* menuSharedResources;
extern void* fieldSeq;
extern s32 shopStockCount; /* number of items on sale */
extern s32 shopMemberColumnX[]; /* column x */
extern s32 shopTotalNumberX;
extern s32 shopTotalNumberY;
void iniMenuContext364And380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 animate, u8 transformDisabled, s32 arg7, u8 arg8); /* = menu func_801D397C */
extern u32 shopSubMenuConfig[24]; /* submenu glyph pairs, [row][4][2] flattened */
u8 shopExecuteMainMenuSelection(); /* K&R definition: called without an argument */
extern u32 shopMainMenuConfig[][2]; /* shop main menu entries, two glyph ids each */
extern s32 shopMainLabelOffsetsX[];    /* label x offsets */
void setupMainMenuWithConfig(s32 count, u32 (*config)[2]); /* = menu func_801E8474 */
void updateMenuSelection(u8 count, u8 sel, u32 (*config)[2]); /* = menu func_801E8978 */
void updateMenuSelection2(u8 count, MenuContext_4E0* labels, void* config, s32* xOffsets, u8* flags, u8 sel, u8 base, u8 mode);
extern s32 shopGoldNumberX; /* number string positions (x, y) x3 */
extern s32 shopGoldNumberY;
extern s32 shopNewGoldNumberX;
extern s32 shopNewGoldNumberY;
extern u8 shopBuyTitleGlyphs[];  /* title glyphs */
extern s32 shopBuyTitleX[]; /* their x */
extern s32 shopBuyTitleY[]; /* their y */
void setMenuLabelsEnabled(u8 show, u8 count, MenuContext_4E0* labels, void* config, u8* flags);
void processLoadSaveMenuSub8(u8 arg0);
u8 runShopSellEquippedAccessories(void);
void executeShopSellSelection(void);
void runShopSellAccessories(void);
void runShopSellWeapons(void);
void runShopSellItems(void);
u16 findInventoryCount(u8* ids, u8* counts, s32 n, u8 id);
void completeShopSale(u32 gold, u8* ids, u8* counts, s32 count, u8* invIds, u8* invCounts, u8* kinds, u8 fromInventory, u8 character);
void runShopSellMenu(s32 count, u8* ids, u8* counts, u8 category, u8 arg4, u8* arg5, u8 arg6);
void clearShopScreen(u8 arg0, u8 arg1);
extern u16 shopOwnedCount; /* number of the selected item owned */
extern u8 shopSellLabelConfig[]; /* label config "{{{{" */
extern s32 shopSubMenuLabelOffsetsX[]; /* label x offsets */
u16 getPartyEquippingItem(u8 id, u8 kind);
extern u16 textSpriteMode1; /* textSpriteMode1 (CLUT) */
void setupShopSellListRows(s32 start, u8* ids, u8* kinds, u8* selCounts, u8* values);
void setupScrollBarThumb(s32 x, s32 y, s32 h, s32 n, s32 top); /* MF1: s32 (u16 x/h does not match) */
void updateMenuPointerGlyph(s32 row, s32 top, u8 arg2, u8 arg3);
void setupDraw428(u8 arg0);
void disableDraw428(void);
void setupShopGoldNumbers(s32 gold, s32 total, s32 newGold);
void setupShopTotalNumber(u32 value);
s32 showShopSellItemInfo(u8 id, u8 kind);
void computeShopPreviewStats(MenuContext_330* ctx, u8 character);
void setupShopBuyTitle(void);
void setupShopBuyListRows(s32 top, s32 gold, u8* rowFlags);
s32 showShopBuyItemInfo(s32 row, s32 top, u8* rowFlags);
void completeShopPurchase(u32 gold);
extern u8 shopBuyLabelConfig[]; /* label config "|{" */
void computeShopEquipStatChange(s32* out, u8* diffs, u8 id, u8 kind, u8 character);
void setupShopOwnedCount(u8 category, u8 id);
extern u8 shopSellTitleGlyphs[];
extern s32 shopSellTitleX[];
extern s32 shopSellTitleY[];
void menuDraw(void); /* = gear shop func_801CC1C4 */
extern s32 shopMemberColumnX[];  /* = gear shop D_801D6C44 */
extern u32 shopSubMenuConfig[24]; /* = gear shop D_801D69A0 */
void drawMenuBoxCorners(s32 box);
void drawMenuBoxInnerFrame(s32 box);
void drawMenuBoxTopFrame(s32 box);
void drawMenuBoxBottomFrame(s32 box);
void drawMenuBoxLeftFrame(s32 box);
void drawMenuBoxRightFrame(s32 box);
void drawMenuBoxBackground(s32 box);
extern s32 shopSaveCursorToSlot[];
extern s32 shopMemoryCardTilesPositionX[];
extern s32 shopMemoryCardTilesPositionY[];
extern s32 shopCardCursorStartX[];
extern s32 shopCardCursorStartY[];
void processLoadSaveMenuSub7Sub0(u8 index);

/* The shop code calls these library functions without prototypes: retail
 * passes unmasked ints for their u8/u16 parameters and results. */
u16 getMenuBitMask();
void initMenuContext_1DE0();
void initMemoryCardPolyVerts();

/* defined in sh_801C5040.c */
void setupMenuPolyFT4(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u16 w, u16 h);
void fillStringDisplayBuffer(u32 value);
void freeScrollBarThumb(void);
void allocMenuPointerGlyph(u8 arg0);
void freeMenuPointerGlyph(u8 arg0);
void processLoadSaveMenuSub1(void);
void processLoadSaveMenuSub1Alt2(void);
void shutdownShopMenu(void);
void processLoadSaveMenuSub3(u8 row);

/* draw the submenu entries of row (like menu func_801E8B4C, Noah processLoadSaveMenuSub8) */
void processLoadSaveMenuSub8(u8 row) {
    s32 i;
    s32 glyph;

    gMenuContext->m354_subMenu->m1400_0Length = 0;
    gMenuContext->m354_subMenu->m1404_500Length = 0;
    for (i = 0; i < gMenuContext->m33A_numMenuEntries; i++) {
        if (i == gMenuContext->m338_currentSelectedSubMenuEntry) {
            glyph = shopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] + 0xD;
        } else {
            glyph = shopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2];
        }
        gMenuContext->m354_subMenu->m1400_0Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, glyph, &gMenuContext->m354_subMenu->m0_polys[gMenuContext->m354_subMenu->m1400_0Length],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
        gMenuContext->m354_subMenu->m1404_500Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, shopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2 + 1],
            &gMenuContext->m354_subMenu->m500_polys[gMenuContext->m354_subMenu->m1404_500Length], gMenuContext->m308_oddOrEven,
            0xA0, 0x96, 0x1000);
    }
    gMenuContext->m354_subMenu->m1408_0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m354_subMenu->m1409_500OddOrEven = gMenuContext->m308_oddOrEven;
    updateCursorPolysLocation(gMenuContext->m338_currentSelectedSubMenuEntry + 3, 1);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
}

/* like menu func_801C531C (Noah menu2_executeMainMenuSelection) for the shop */
u8 shopExecuteMainMenuSelection(arg0)
    u8 arg0; /* K&R: shopMainMenuExecute calls it with no argument (arg0 is then garbage) */
{
    u8 ret;

    ret = 1;
    switch (gMenuContext->m336_menuSelectedEntry) {
    case 0:
        ret = 0;
        break;
    case 1:
        arg0 = openShopSellMenu();
        break;
    case 2:
        arg0 = runShopBuyMenu();
        break;
    }
    if (arg0) {
        processLoadSaveMenuSub1Alt2();
        setMenuLabelsEnabled(1, 4, gMenuContext->m6E0_mainMenuLabels, shopMainLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    }
    cleanupShopMainMenuSelection();
    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 1;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
    return ret;
}

/* shop main loop (like menu func_801C55A0) */
void shopMainMenuExecute(void) {
    u8 cont;

    cont = 1;
    gMenuContext->m336_menuSelectedEntry = 2;
    setupMainMenuWithConfig(4, shopMainMenuConfig);
    setMenuLabelsEnabled(1, 4, gMenuContext->m6E0_mainMenuLabels, shopMainLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    do {
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 4:
            playMenuSoundEffect(2);
            gMenuContext->m350_mainMenu->m1192_dimmed = 1;
            resetCursorState();
            setMenuLabelsEnabled(0, 4, gMenuContext->m6E0_mainMenuLabels, shopMainLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x4C;
            /* The PS1 passes what setMenuLabelsEnabled leaves in a0: 0 (Redux). */
            cont = shopExecuteMainMenuSelection(0);
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
            break;
        case 5:
            cont = 0;
            break;
        case 1:
            if (gMenuContext->m336_menuSelectedEntry != 0) {
                gMenuContext->m336_menuSelectedEntry--;
            } else {
                gMenuContext->m336_menuSelectedEntry = 2;
            }
            break;
        case 3:
            if (++gMenuContext->m336_menuSelectedEntry >= 3) {
                gMenuContext->m336_menuSelectedEntry = 0;
            }
            break;
        }
        if (gMenuContext->m336_menuSelectedEntry != gMenuContext->m337_previousMenuSelectedEntry) {
            updateMenuSelection(3, gMenuContext->m336_menuSelectedEntry, shopMainMenuConfig);
            updateMenuSelection2(4, gMenuContext->m6E0_mainMenuLabels, shopMainLabelConfig, shopMainLabelOffsetsX, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m336_menuSelectedEntry, 0, 0);
            gMenuContext->m337_previousMenuSelectedEntry = gMenuContext->m336_menuSelectedEntry;
        }
    } while (cont);
}

/* shop entry: allocate, set up, run, tear down */
void shopMenuEntryPoint(void) {
    initMenuContext32C(1);
    initMenuContext33C(1);
    initMenuContext350MainMenu(1);
    initMenuContext354(1);
    initMenuContext330(1);
    initMenuContext348(1);
    initMenuContext1E20(1);
    initMenuContext450Shop(1);
    gMenuContext->m350_mainMenu->m1180_backgroundRect.x = 0x2C0;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.y = 0x100;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.w = 0x140;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.h = 0xE0;
    gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
    initMenuGlobals();
    resetMenuOddOrEven();
    initMenuContext4E0();
    initPrimitives_348_cursor();
    computeMenuBorders();
    setupShopStock();
    gMenuContext->m327_drawEnabled = 1;
    gMenuContext->m32A_soundEnabled = 1;
    shopMainMenuExecute();
    shutdownShopMenu();
}

/* a character's computed stats for the shop's equip preview (like menu
 * func_801E3A80, Noah setupStatusMenuForCharacter2; nine entries, clamped to
 * 999/99) */
void computeShopPreviewStats(MenuContext_330* ctx, u8 charId) {
    GameStateCharacter* c;

    c = &gameState.m26C_party[charId];
    if (c->m56_battleCommandLoadout == 4) {
        ctx->mB8_computedStats[0] = c->m4_weaponSlots[0].m0_weaponPower + c->m4_weaponSlots[3].m0_weaponPower;
    } else {
        ctx->mB8_computedStats[0] = c->m4_weaponSlots[0].m0_weaponPower + (c->m58_attack + c->m28_attackBonus);
    }
    ctx->mB8_computedStats[0] = c->m4_weaponSlots[0].m0_weaponPower + (c->m58_attack + c->m28_attackBonus); /* sic: overrides the branch above */
    ctx->mB8_computedStats[1] = c->m5E_hitPercentage + c->m2E_hitBonus;
    ctx->mB8_computedStats[2] = c->m2D_accessoryDefence + (c->m59_defence + c->m29_defenceBonus);
    ctx->mB8_computedStats[3] = c->m5F_evadePercentage + c->m2F_evadeBonus;
    ctx->mB8_computedStats[4] = c->m5B_ether + c->m2B_etherBonus;
    ctx->mB8_computedStats[5] = 99;
    ctx->mB8_computedStats[6] = c->m5C_etherDefence + c->m2C_etherDefenceBonus;
    ctx->mB8_computedStats[7] = 10;
    ctx->mB8_computedStats[8] = c->m5A_agility + c->m2A_agilityBonus;
    if (ctx->mB8_computedStats[0] > 999) {
        ctx->mB8_computedStats[0] = 999;
    }
    if (ctx->mB8_computedStats[1] > 99) {
        ctx->mB8_computedStats[1] = 99;
    }
    if (ctx->mB8_computedStats[2] > 999) {
        ctx->mB8_computedStats[2] = 999;
    }
    if (ctx->mB8_computedStats[3] > 99) {
        ctx->mB8_computedStats[3] = 99;
    }
    if (ctx->mB8_computedStats[4] > 999) {
        ctx->mB8_computedStats[4] = 999;
    }
    if (ctx->mB8_computedStats[5] > 99) {
        ctx->mB8_computedStats[5] = 99;
    }
    if (ctx->mB8_computedStats[6] > 999) {
        ctx->mB8_computedStats[6] = 999;
    }
    if (ctx->mB8_computedStats[7] > 99) {
        ctx->mB8_computedStats[7] = 99;
    }
    if (ctx->mB8_computedStats[8] > 99) {
        ctx->mB8_computedStats[8] = 99;
    }
}

/* shop draw (MenuShop polys, labels and frame lines) */
void drawShopMenu(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m5A_drawShop[0]) {
        for (i = 0; i < 9; i++) {
            if (gMenuContext->m450_shop->m469C_lineEnabled[i]) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m450_shop->m3890_lines[0][i * 2 + gMenuContext->m308_oddOrEven]);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m450_shop->m3A40_lines[0][i * 2 + gMenuContext->m308_oddOrEven]);
            }
        }
        drawMultiPolys(gMenuContext->m450_shop->m46AB_textLength, gMenuContext->m450_shop->m5A0_textPolys[0],
                      gMenuContext->m450_shop->m46AA_textOddOrEven);
        drawMultiPolys(gMenuContext->m450_shop->m46A9_2D0Length, gMenuContext->m450_shop->m2D0_polys[0],
                      gMenuContext->m450_shop->m46A8_2D0OddOrEven);
        drawMultiPolys(gMenuContext->m450_shop->m46A5_0Length, gMenuContext->m450_shop->m0_polys[0],
                      gMenuContext->m450_shop->m46A6_0OddOrEven);
        for (i = 0; i < 8; i++) {
            if (gMenuContext->m450_shop->m4684_labelEnabled[i]) {
                transformAndAddPrim(1, gMenuContext->m450_shop->m3C30_labels[i].m50_vertex,
                              gMenuContext->m450_shop->m3C30_labels[i].m0_polys,
                              gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven);
                transformAndAddPrim(1, gMenuContext->m450_shop->m4030_labels[i].m50_vertex,
                              gMenuContext->m450_shop->m4030_labels[i].m0_polys,
                              gMenuContext->m450_shop->m4030_labels[i].m7D_oddOrEven);
            }
        }
        if (gMenuContext->m450_shop->m46A7_4430Enabled) {
            transformAndAddPrim(1, gMenuContext->m450_shop->m4430_label.m50_vertex, gMenuContext->m450_shop->m4430_label.m0_polys,
                          gMenuContext->m450_shop->m4430_label.m7D_oddOrEven);
        }
        if (gMenuContext->m450_shop->m46B5_44B0Enabled) {
            transformAndAddPrim(1, gMenuContext->m450_shop->m44B0_label.m50_vertex, gMenuContext->m450_shop->m44B0_label.m0_polys,
                          gMenuContext->m450_shop->m44B0_label.m7D_oddOrEven);
        }
        if (gMenuContext->m450_shop->m4785_45B0Enabled) {
            transformAndAddPrim(1, gMenuContext->m450_shop->m45B0_label.m50_vertex, gMenuContext->m450_shop->m45B0_label.m0_polys,
                          gMenuContext->m450_shop->m45B0_label.m7D_oddOrEven);
        }
        if (gMenuContext->m450_shop->m46B2_numbersEnabled) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m450_shop->m3BF0_line[gMenuContext->m308_oddOrEven]);
            drawMultiPolys(gMenuContext->m450_shop->m46AD_C80Length, gMenuContext->m450_shop->mC80_number1[0],
                          gMenuContext->m450_shop->m46AC_C80OddOrEven);
            drawMultiPolys(gMenuContext->m450_shop->m46AF_F50Length, gMenuContext->m450_shop->mF50_number2[0],
                          gMenuContext->m450_shop->m46AE_F50OddOrEven);
            drawMultiPolys(gMenuContext->m450_shop->m46B1_1AE0Length, gMenuContext->m450_shop->m1AE0_number3[0],
                          gMenuContext->m450_shop->m46B0_1AE0OddOrEven);
        }
        for (i = 0; i < 8; i++) {
            drawMultiPolys(gMenuContext->m450_shop->m468C_rowLength[i], gMenuContext->m450_shop->m1DB0_rows[i][0],
                          gMenuContext->m450_shop->m4694_rowOddOrEven[i]);
        }
        for (i = 0; i < 9; i++) {
            drawMultiPolys(gMenuContext->m450_shop->m46BC_27B0Length[i], gMenuContext->m450_shop->m27B0_attackChange[i][0],
                          gMenuContext->m450_shop->m46CE_27B0OddOrEven[i]);
            drawMultiPolys(gMenuContext->m450_shop->m46C5_3020Length[i], gMenuContext->m450_shop->m3020_defenceChange[i][0],
                          gMenuContext->m450_shop->m46D7_3020OddOrEven[i]);
        }
    }
    if (gMenuContext->m33C_drawFlags->m5A_drawShop[1] == 1) {
        drawMultiPolys(gMenuContext->m450_shop->m46B4_1220Length, gMenuContext->m450_shop->m1220_number[0],
                      gMenuContext->m450_shop->m46B3_1220OddOrEven);
    }
}

/* tint `count` double-buffered polys: 0 red, 1 blue */
void setShopDigitsColor(s32 count, POLY_FT4* polys, u8 color) {
    s32 i;

    for (i = 0; i < count; i++) {
        SetShadeTex(&polys[i * 2 + gMenuContext->m308_oddOrEven], 0);
        switch (color) {
        case 0:
            polys[i * 2 + gMenuContext->m308_oddOrEven].r0 = 0x80;
            polys[i * 2 + gMenuContext->m308_oddOrEven].g0 = 0x40;
            polys[i * 2 + gMenuContext->m308_oddOrEven].b0 = 0x40;
            break;
        case 1:
            polys[i * 2 + gMenuContext->m308_oddOrEven].r0 = 0x40;
            polys[i * 2 + gMenuContext->m308_oddOrEven].g0 = 0x40;
            polys[i * 2 + gMenuContext->m308_oddOrEven].b0 = 0x80;
            break;
        }
    }
}

void showShopPartyMembers(void) {
    s32 n;
    s32 i;
    s32 j;

    gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 1;
    for (n = 1; n < 12; n++) {
        gMenuContext->m450_shop->m46A5_0Length = 0;
        for (i = 0, j = 0; i < n; i++) {
            if (gMenuContext->m30C_characterAvailable[i]) {
                gMenuContext->m450_shop->m46A5_0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, i + 0x14E,
                                                                gMenuContext->m450_shop->m0_polys[j],
                                                                gMenuContext->m308_oddOrEven, shopMemberColumnX[j], 0xA6, 0x1000);
                j++;
            }
        }
        gMenuContext->m450_shop->m46A6_0OddOrEven = gMenuContext->m308_oddOrEven;
        menuDraw();
    }
}

/* the shop title (four glyphs) into m5A0_textPolys */
void setupShopBuyTitle(void) {
    s32 i;

    gMenuContext->m450_shop->m46AB_textLength = 0;
    for (i = 0; i < 4; i++) {
        gMenuContext->m450_shop->m46AB_textLength +=
            setupStringInPolyFT4(gMenuContext->m2DC_font, shopBuyTitleGlyphs[i],
                          &gMenuContext->m450_shop->m5A0_textPolys[gMenuContext->m450_shop->m46AB_textLength],
                          gMenuContext->m308_oddOrEven, shopBuyTitleX[i], shopBuyTitleY[i], 0x1000);
    }
    gMenuContext->m450_shop->m46AA_textOddOrEven = gMenuContext->m308_oddOrEven;
}

void setupShopSellTitle(void) {
    s32 i;

    gMenuContext->m450_shop->m46AB_textLength = 0;
    for (i = 0; i < 2; i++) {
        gMenuContext->m450_shop->m46AB_textLength += setupStringInPolyFT4(
            gMenuContext->m2DC_font, shopSellTitleGlyphs[i],
            gMenuContext->m450_shop->m5A0_textPolys[gMenuContext->m450_shop->m46AB_textLength],
            gMenuContext->m308_oddOrEven, shopSellTitleX[i], shopSellTitleY[i], 0x1000);
    }
    gMenuContext->m450_shop->m46AA_textOddOrEven = gMenuContext->m308_oddOrEven;
}

/* the gold, total and new-gold numbers into the shop's number strings (mC80, mF50, m1AE0) */
void setupShopGoldNumbers(s32 gold, s32 total, s32 newGold) {
    s32 i;

    fillStringDisplayBuffer(gold);
    gMenuContext->m450_shop->m46AD_C80Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46AD_C80Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                &gMenuContext->m450_shop->mC80_number1[gMenuContext->m450_shop->m46AD_C80Length],
                gMenuContext->m308_oddOrEven, i * 8 + shopGoldNumberX, shopGoldNumberY, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46AC_C80OddOrEven = gMenuContext->m308_oddOrEven;
    fillStringDisplayBuffer(total);
    gMenuContext->m450_shop->m46AF_F50Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46AF_F50Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                &gMenuContext->m450_shop->mF50_number2[gMenuContext->m450_shop->m46AF_F50Length],
                gMenuContext->m308_oddOrEven, i * 8 + shopTotalNumberX, shopTotalNumberY, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46AE_F50OddOrEven = gMenuContext->m308_oddOrEven;
    fillStringDisplayBuffer(newGold);
    gMenuContext->m450_shop->m46B1_1AE0Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46B1_1AE0Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                &gMenuContext->m450_shop->m1AE0_number3[gMenuContext->m450_shop->m46B1_1AE0Length],
                gMenuContext->m308_oddOrEven, i * 8 + shopNewGoldNumberX, shopNewGoldNumberY, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46B0_1AE0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m46B2_numbersEnabled = 1;
}

/* party members (bit mask) that have item equipped (mode 0: weapon/armor slots, 1: accessories) */
u16 getPartyEquippingItem(u8 item, u8 mode) {
    u16 mask;
    s32 i;
    s32 j;
    u8 found;

    mask = 0;
    if (item != 0 && mode != 2) {
        for (i = 0; i < 16; i++) {
            found = 0;
            if (gMenuContext->m30C_characterAvailable[i]) {
                switch (mode) {
                case 0:
                    for (j = 0; j < 5; j++) {
                        if (item < 0x32) {
                            if (gameState.m26C_party[i].m6A_weaponModifier[j] == item) {
                                found = 1;
                                break;
                            }
                        } else {
                            if (gameState.m26C_party[i].m6F_equipment[j] == item) {
                                found = 1;
                                break;
                            }
                        }
                    }
                    break;
                case 1:
                    for (j = 0; j < 3; j++) {
                        if (gameState.m26C_party[i].m74_accessorySlots[j] == item) {
                            found = 1;
                            break;
                        }
                    }
                    break;
                }
            }
            if (found) {
                mask |= getMenuBitMask(i);
            }
        }
    }
    return mask;
}

/* the eight visible lines of the shop list from item first on: name and price
 * labels (greyed out when gold < price), quantity being bought "x NN" */
void setupShopBuyListRows(s32 first, s32 gold, u8* flags) {
    RECT rect;
    u8 digits[0xE];
    u8 text[0x10];
    s32 div[5];
    u16* image;
    s32 price;
    s32 cost;
    s32 i;
    s32 j;
    s32 k;
    s32 d;
    u8 leading;

    div[0] = 1;
    div[1] = 10;
    div[2] = 100;
    div[3] = 1000;
    div[4] = 10000;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 8; i++) {
        flags[i] = 0;
        bzero(digits, 0xE);
        gMenuContext->m450_shop->m468C_rowLength[i] = 0;
        if (gMenuContext->m1E30_shopItemIds[first + i]) {
            switch (gMenuContext->m1E60_shopItemKinds[first + i]) {
            case 0:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getWeaponName(gMenuContext->m1E30_shopItemIds[first + i]), image, 0x24, 0);
                cost = price = gMenuContext->m330_itemData->m0_weaponStats[gMenuContext->m1E30_shopItemIds[first + i]].m4_price;
                break;
            case 1:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getAccessoryName(gMenuContext->m1E30_shopItemIds[first + i]), image, 0x24, 0);
                cost = price = gMenuContext->m330_itemData->m4_accessoryData[gMenuContext->m1E30_shopItemIds[first + i]].m2_price;
                break;
            case 2:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getUsableItemName(gMenuContext->m1E30_shopItemIds[first + i]), image, 0x24, 0);
                cost = price = gMenuContext->m330_itemData->m1C_usableItems[gMenuContext->m1E30_shopItemIds[first + i]].m2_price;
                break;
            }
            if (gold >= cost) {
                flags[i] = 0x80;
            } else {
                flags[i] = 0;
            }
            leading = 0;
            k = 0;
            for (j = 4; j > 0; j--) {
                d = price / div[j];
                if (d != 0 || leading) {
                    digits[k * 2] = d + 0x10;
                    price -= d * div[j];
                    leading = 1;
                } else {
                    digits[k * 2] = 0xC3;
                }
                k++;
            }
            digits[k * 2] = price % 10 + 0x10;
            decodeCharacterCodes((u16*)digits, text, 5);
            gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth = renderString(text, image, 0x24, 1);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m3C30_labels[i], i, 0x80, flags[i] + 1);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m3C30_labels[i].m50_vertex, 0x24, i * 13 + 0x32,
                          gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth, 0xD);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            DrawSync(0);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m4030_labels[i], i, 0x80, flags[i] + 2);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m4030_labels[i].m50_vertex, 0x8C, i * 13 + 0x32,
                          gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth, 0xD);
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven; /* sic (m4030's?) */
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 1;
            if (gMenuContext->m450_shop->m4654_buyQuantities[first + i]) {
                gMenuContext->m450_shop->m468C_rowLength[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xF1, gMenuContext->m450_shop->m1DB0_rows[i],
                                  gMenuContext->m308_oddOrEven, 0xB4, i * 13 + 0x36, 0x1000);
                if ((u8)(gMenuContext->m450_shop->m4654_buyQuantities[first + i] / 10)) {
                    gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                        gMenuContext->m2DC_font, (u8)(gMenuContext->m450_shop->m4654_buyQuantities[first + i] / 10),
                        gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                        gMenuContext->m308_oddOrEven, 0xBC, i * 13 + 0x36, 0x1000);
                }
                gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, (u8)(gMenuContext->m450_shop->m4654_buyQuantities[first + i] % 10),
                    gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                    gMenuContext->m308_oddOrEven, 0xC4, i * 13 + 0x36, 0x1000);
                gMenuContext->m450_shop->m4694_rowOddOrEven[i] = gMenuContext->m308_oddOrEven;
            }
        } else {
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 0;
        }
    }
    freeHeap(image);
}

/* equip preview: attack (mode 0, weapon) or defence (mode 1, accessory) of
 * character charId now and with item; |difference| into diffs, 1 into signs
 * when it drops */
void computeShopEquipStatChange(s32* diffs, u8* signs, u8 item, u8 mode, u8 charId) {
    s16 newStats[4];
    s16 curStats[4];
    s32 i;
    s32 minIndex;
    u8 min;
    u8 add;
    MenuWeaponStats* weapon;
    MenuWeaponStats* newWeapon;
    MenuAccessoryStats* acc;
    MenuAccessoryStats* newAcc;

    curStats[1] = 0;
    curStats[0] = 0;
    newStats[1] = 0;
    newStats[0] = 0;
    switch (mode) {
    case 0:
        if (charId != 4) {
            weapon = &gMenuContext->m330_itemData->m0_weaponStats[gameState.m26C_party[charId].m6A_weaponModifier[0]];
            curStats[0] = weapon->mC_power + gameState.m26C_party[charId].m58_attack;
            weapon = &gMenuContext->m330_itemData->m0_weaponStats[item];
            newStats[0] = weapon->mC_power + gameState.m26C_party[charId].m58_attack;
        } else {
            for (i = 0; i < 4; i++) {
                weapon = &gMenuContext->m330_itemData->m0_weaponStats[gameState.m26C_party[charId].m6F_equipment[i]];
                curStats[0] += weapon->mC_power;
            }
            newWeapon = &gMenuContext->m330_itemData->m0_weaponStats[item];
            for (i = 0; i < 4; i++) {
                weapon = &gMenuContext->m330_itemData->m0_weaponStats[gameState.m26C_party[charId].m6F_equipment[i]];
                if (weapon->m6_slotType[0] == newWeapon->m6_slotType[0] && weapon->m6_slotType[0] != 5) {
                    newStats[0] += newWeapon->mC_power;
                } else {
                    newStats[0] += weapon->mC_power;
                }
            }
        }
        break;
    case 1:
        add = 1;
        curStats[1] = newStats[1] = gameState.m26C_party[charId].m59_defence;
        for (i = 0; i < 3; i++) {
            acc = &gMenuContext->m330_itemData->m4_accessoryData[gameState.m26C_party[charId].m74_accessorySlots[i]];
            curStats[1] += acc->m8_defence;
        }
        newAcc = &gMenuContext->m330_itemData->m4_accessoryData[item];
        for (i = 0; i < 3; i++) {
            acc = &gMenuContext->m330_itemData->m4_accessoryData[gameState.m26C_party[charId].m74_accessorySlots[i]];
            if (acc->mE_exclusiveGroup != 0 && acc->mE_exclusiveGroup == newAcc->mE_exclusiveGroup) {
                newStats[1] += newAcc->m8_defence;
                add = 0;
            } else {
                newStats[1] += acc->m8_defence;
            }
        }
        if (add) {
            min = 0xFF;
            for (i = 0; i < 3; i++) {
                acc = &gMenuContext->m330_itemData->m4_accessoryData[gameState.m26C_party[charId].m74_accessorySlots[i]];
                if (acc->m8_defence <= min) {
                    min = acc->m8_defence;
                    minIndex = i;
                }
            }
            newStats[1] = gameState.m26C_party[charId].m59_defence + newAcc->m8_defence;
            for (i = 0; i < 3; i++) {
                if (i != minIndex) {
                    acc = &gMenuContext->m330_itemData->m4_accessoryData[gameState.m26C_party[charId].m74_accessorySlots[i]];
                    newStats[1] += acc->m8_defence;
                }
            }
        }
        break;
    }
    for (i = 0; i < 2; i++) {
        if (newStats[i] >= curStats[i]) {
            diffs[i] = newStats[i] - curStats[i];
            signs[i] = 0;
        } else {
            diffs[i] = curStats[i] - newStats[i];
            signs[i] = 1;
        }
    }
}

u16 findInventoryCount(u8* keys, u8* values, s32 count, u8 key) {
    s32 i;
    u16 result = 0;

    for (i = 0; i < count; i++) {
        if (keys[i] == key) {
            result = values[i];
            break;
        }
    }
    return result;
}

void setupShopOwnedCount(u8 category, u8 id) {
    u8* ids;
    u8* counts;
    s32 n;
    u16 owned;
    u16* image;
    RECT rect;
    u8 codes[4];
    u8 text[8];

    switch (category) {
    case 0:
        ids = gameState.m1D9C_inventory1Ids;
        counts = gameState.m1D38_inventory1Counts;
        n = 100;
        break;
    case 1:
        ids = gameState.m1EC8_inventory2Ids;
        counts = gameState.m1E00_inventory2Counts;
        n = 200;
        break;
    case 2:
        ids = gameState.m2026_inventory0Ids;
        counts = gameState.m1F90_inventory0Counts;
        n = 150;
        break;
    }
    owned = findInventoryCount(ids, counts, n, id);
    shopOwnedCount = owned;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    codes[1] = 0;
    codes[3] = 0;
    if (owned / 10) {
        codes[0] = owned / 10 + 0x10;
    } else {
        codes[0] = 0xC3;
    }
    codes[2] = owned % 10 + 0x10;
    decodeCharacterCodes((u16*)codes, text, 2);
    gMenuContext->m450_shop->m45B0_label.m7E_stringWidth = renderString(text, image, 0x24, 1);
    rect.x = 0x198;
    rect.y = 0xB4;
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, (u32*)image);
    DrawSync(0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m45B0_label, 9, 0x80, 0x82);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m45B0_label.m50_vertex, 0xF8, 0x8E,
                  gMenuContext->m450_shop->m45B0_label.m7E_stringWidth, 0xD);
    gMenuContext->m450_shop->m45B0_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m4785_45B0Enabled = 1;
    freeHeap(image);
}

/* show the item on sale at list row 'row' (name, who can equip it and each
 * member's attack/defence change); returns its price. rowFlags is unused. */
s32 showShopBuyItemInfo(s32 row, s32 top, u8* rowFlags) {
    RECT rect;
    s32 stats[2];
    u8 diffs[2];
    s32 price;
    u16 mask2;
    u16 equipMask;
    u8 id;
    u8 kind;
    u16* image;
    s32 i;
    s32 j;
    s32 k;

    equipMask = 0;
    id = gMenuContext->m1E30_shopItemIds[top + row];
    kind = gMenuContext->m1E60_shopItemKinds[top + row];
    image = allocateHeap(0x618, 0);
    bzero(image, 0x618);
    switch (kind) {
    case 0:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[1], id), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m0_weaponStats[id].m4_price;
        equipMask = gMenuContext->m330_itemData->m0_weaponStats[id].m0_equipMask;
        break;
    case 1:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[2], id), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m4_accessoryData[id].m2_price;
        equipMask = gMenuContext->m330_itemData->m4_accessoryData[id].m0_equipMask;
        break;
    case 2:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[0], id), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m1C_usableItems[id].m2_price;
        break;
    }
    mask2 = getPartyEquippingItem(id, kind);
    rect.x = 0x140;
    rect.y = 0x4E;
    rect.w = 0x3C;
    rect.h = 0xD;
    LoadImage(&rect, (u32*)image);
    DrawSync(0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m4430_label, 0, 0, 0);
    setupMenuPolyFT4(&gMenuContext->m450_shop->m4430_label.m0_polys[gMenuContext->m308_oddOrEven], 0x2C, 0x12, 0, 0x4E,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m4430_label.m50_vertex, 0x2C, 0x12,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    gMenuContext->m450_shop->m4430_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    freeHeap(image);
    if (id) {
        gMenuContext->m450_shop->m46A7_4430Enabled = 1;
    } else {
        gMenuContext->m450_shop->m46A7_4430Enabled = 0;
    }
    gMenuContext->m450_shop->m46A9_2D0Length = 0;
    for (i = 0, j = 0; i < 16; i++) {
        if (gMenuContext->m30C_characterAvailable[i]) {
            if (menu2CheckBitmask(equipMask, i)) {
                gMenuContext->m450_shop->m469C_lineEnabled[j] = 1;
            } else {
                gMenuContext->m450_shop->m469C_lineEnabled[j] = 0;
            }
            if (menu2CheckBitmask(mask2, i)) {
                gMenuContext->m450_shop->m46A9_2D0Length +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE,
                                  gMenuContext->m450_shop->m2D0_polys[gMenuContext->m450_shop->m46A9_2D0Length],
                                  gMenuContext->m308_oddOrEven, shopMemberColumnX[j] + 0xE, 0xB4, 0x1000);
            }
            gMenuContext->m450_shop->m46BC_27B0Length[j] = 0;
            gMenuContext->m450_shop->m46C5_3020Length[j] = 0;
            if (gMenuContext->m450_shop->m469C_lineEnabled[j]) {
                stats[1] = 0;
                stats[0] = 0;
                computeShopEquipStatChange(stats, diffs, id, kind, i);
                if (stats[0]) {
                    fillStringDisplayBuffer(stats[0]);
                    for (k = 0; k < 3; k++) {
                        if (gMenuContext->m31C_stringDisplayBuffer[k + 6] != 0xFF) {
                            gMenuContext->m450_shop->m46BC_27B0Length[j] += setupStringInPolyFT4(
                                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[k + 6],
                                gMenuContext->m450_shop->m27B0_attackChange[j][gMenuContext->m450_shop->m46BC_27B0Length[j]],
                                gMenuContext->m308_oddOrEven, 0x49 + k * 8 + j * 0x1A, 0xBE, 0x1000);
                        }
                    }
                    setShopDigitsColor(gMenuContext->m450_shop->m46BC_27B0Length[j],
                                  gMenuContext->m450_shop->m27B0_attackChange[j][0], diffs[0]);
                    gMenuContext->m450_shop->m46CE_27B0OddOrEven[j] = gMenuContext->m308_oddOrEven;
                }
                if (stats[1]) {
                    fillStringDisplayBuffer(stats[1]);
                    for (k = 0; k < 3; k++) {
                        if (gMenuContext->m31C_stringDisplayBuffer[k + 6] != 0xFF) {
                            gMenuContext->m450_shop->m46C5_3020Length[j] += setupStringInPolyFT4(
                                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[k + 6],
                                gMenuContext->m450_shop->m3020_defenceChange[j][gMenuContext->m450_shop->m46C5_3020Length[j]],
                                gMenuContext->m308_oddOrEven, 0x49 + k * 8 + j * 0x1A, 0xC6, 0x1000);
                        }
                    }
                    setShopDigitsColor(gMenuContext->m450_shop->m46C5_3020Length[j],
                                  gMenuContext->m450_shop->m3020_defenceChange[j][0], diffs[1]);
                    gMenuContext->m450_shop->m46D7_3020OddOrEven[j] = gMenuContext->m308_oddOrEven;
                }
            }
            j++;
        }
    }
    gMenuContext->m450_shop->m46A8_2D0OddOrEven = gMenuContext->m308_oddOrEven;
    setupShopOwnedCount(kind, id);
    return price;
}

void completeShopPurchase(u32 gold) {
    s32 i;
    s32 j;
    u8 notFound;

    playMenuSoundEffect(0xD1);
    gameState.m1924_gold = gold;
    if (gold > 9999999) {
        gameState.m1924_gold = 9999999;
    }
    for (i = 0; i < 48; i++) {
        if (gMenuContext->m1E30_shopItemIds[i] != 0 && gMenuContext->m450_shop->m4654_buyQuantities[i] != 0) {
            switch (gMenuContext->m1E60_shopItemKinds[i]) {
            case 0:
                notFound = 1;
                for (j = 0; j < 100; j++) {
                    if (gameState.m1D9C_inventory1Ids[j] == gMenuContext->m1E30_shopItemIds[i]) {
                        notFound = 0;
                        if ((gameState.m1D38_inventory1Counts[j] += gMenuContext->m450_shop->m4654_buyQuantities[i]) >= 100) {
                            gameState.m1D38_inventory1Counts[j] = 99;
                        }
                    }
                }
                if (notFound) {
                    for (j = 0; j < 100; j++) {
                        if (gameState.m1D9C_inventory1Ids[j] == 0) {
                            gameState.m1D9C_inventory1Ids[j] = gMenuContext->m1E30_shopItemIds[i];
                            gameState.m1D38_inventory1Counts[j] = gMenuContext->m450_shop->m4654_buyQuantities[i];
                            break;
                        }
                    }
                }
                break;
            case 1:
                notFound = 1;
                for (j = 0; j < 200; j++) {
                    if (gameState.m1EC8_inventory2Ids[j] == gMenuContext->m1E30_shopItemIds[i]) {
                        notFound = 0;
                        if ((gameState.m1E00_inventory2Counts[j] += gMenuContext->m450_shop->m4654_buyQuantities[i]) >= 100) {
                            gameState.m1E00_inventory2Counts[j] = 99;
                        }
                    }
                }
                if (notFound) {
                    for (j = 0; j < 200; j++) {
                        if (gameState.m1EC8_inventory2Ids[j] == 0) {
                            gameState.m1EC8_inventory2Ids[j] = gMenuContext->m1E30_shopItemIds[i];
                            gameState.m1E00_inventory2Counts[j] = gMenuContext->m450_shop->m4654_buyQuantities[i];
                            break;
                        }
                    }
                }
                break;
            case 2:
                notFound = 1;
                for (j = 0; j < 150; j++) {
                    if (gameState.m2026_inventory0Ids[j] == gMenuContext->m1E30_shopItemIds[i]) {
                        notFound = 0;
                        if ((gameState.m1F90_inventory0Counts[j] += gMenuContext->m450_shop->m4654_buyQuantities[i]) >= 100) {
                            gameState.m1F90_inventory0Counts[j] = 99;
                        }
                    }
                }
                if (notFound) {
                    for (j = 0; j < 150; j++) {
                        if (gameState.m2026_inventory0Ids[j] == 0) {
                            gameState.m2026_inventory0Ids[j] = gMenuContext->m1E30_shopItemIds[i];
                            gameState.m1F90_inventory0Counts[j] = gMenuContext->m450_shop->m4654_buyQuantities[i];
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
}

void setupShopTotalNumber(u32 value) {
    s32 i;
    s32 x;
    u8 c;

    fillStringDisplayBuffer(value);
    i = 0;
    gMenuContext->m450_shop->m46B4_1220Length = 0;
    x = 0x6B;
    do {
        c = gMenuContext->m31C_stringDisplayBuffer[i];
        i++;
        if (c != 0xFF) {
            gMenuContext->m450_shop->m46B4_1220Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, c,
                              gMenuContext->m450_shop->m1220_number[gMenuContext->m450_shop->m46B4_1220Length],
                              gMenuContext->m308_oddOrEven, x, 0x54, 0x1000);
        }
        x += 8;
    } while (i < 9);
    gMenuContext->m450_shop->m46B3_1220OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m5A_drawShop[1] = 2;
}

/* buy menu: pick quantities of the items on sale, confirm and pay */
u8 runShopBuyMenu(void) {
    u8 rowFlags[8];
    u8 loop = 1;
    u8 first = 1;
    u8 redraw = 1;
    s32 row = 0;
    s32 prevRow = 0xFF;
    s32 top = 0;
    s32 prevTop = 0xFF;
    s32 i;
    s32 gold = gameState.m1924_gold;
    s32 total = 0;
    s32 newGold = gold;
    s32 price;

    for (i = 0; i < 11; i++) {
        computeShopPreviewStats(gMenuContext->m330_itemData, i);
        gMenuContext->m450_shop->m46E0_attack[i] = gMenuContext->m330_itemData->mB8_computedStats[0];
        gMenuContext->m450_shop->m4700_defence[i] = gMenuContext->m330_itemData->mB8_computedStats[2];
    }
    bzero(gMenuContext->m450_shop->m4654_buyQuantities, 0x30);
    gMenuContext->m350_mainMenu->m1192_dimmed = 1;
    allocMenuPointerGlyph(0);
    while (loop) {
        menuDraw();
        if (top != prevTop || redraw) {
            setupShopBuyListRows(top, newGold, rowFlags);
            setupScrollBarThumb(0xC, 0x32, 0x3C, shopStockCount, top);
        }
        if (row != prevRow || top != prevTop) {
            price = showShopBuyItemInfo(row, top, rowFlags);
            prevRow = row;
            gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 1;
            prevTop = top;
        }
        updateMenuPointerGlyph(row, top, 0, 0);
        if (first) {
            setMenuLabelsEnabled(1, 2, gMenuContext->m6E0_mainMenuLabels, shopBuyLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            updateMenuSelection2(2, gMenuContext->m6E0_mainMenuLabels, shopBuyLabelConfig, shopSubMenuLabelOffsetsX, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels, 0, 0, 1);
            gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 0;
            iniMenuContext364And380(2, 0xC, 0x2A, 0xC4, 0x74, 0, 1, 4, 1);
            iniMenuContext364And380(3, 0x20, 0xE, 0xFC, 0x14, 0, 1, 4, 0);
            iniMenuContext364And380(5, 0xE0, 0x7A, 0x40, 0x24, 0, 1, 4, 0);
            processLoadSaveMenuSub1();
            showShopPartyMembers();
            setupShopBuyTitle();
            first = 0;
            while (gMenuContext->m329_animationState != 0) {
                menuDraw();
            }
            gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 1;
        }
        if (redraw) {
            setupShopGoldNumbers(gold, total, newGold);
            redraw = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (total != 0) {
                playMenuSoundEffect(2);
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                loop = 0;
                gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 0;
                setupDraw428(0);
                setupShopTotalNumber(total);
                if (askYesNoQuestion(0x8F, 0xFF, 1)) {
                    completeShopPurchase(newGold);
                } else {
                    loop = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 1;
                    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = 1;
                    prevTop = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 1;
                }
                disableDraw428();
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 5:
            gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 0;
            loop = 0;
            if (total != 0) {
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                if (askYesNoQuestion(0x8C, 0xFF, 1) == 0) {
                    loop = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 1;
                    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = 1;
                    prevTop = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = 1;
                }
                disableDraw428();
            }
            menuDraw();
            break;
        case 1:
            row++;
            if (row >= 8) {
                row = 7;
                top++;
                if (shopStockCount - 8 < top) {
                    top--;
                }
            }
            break;
        case 3:
            row--;
            if (row < 0) {
                top--;
                row = 0;
                if (top < 0) {
                    top = 0;
                }
            }
            break;
        case 0:
            if (rowFlags[row]) {
                if (gMenuContext->m450_shop->m4654_buyQuantities[top + row] + 1 + shopOwnedCount < 100) {
                    total += price;
                    newGold -= price;
                    redraw = 1;
                    gMenuContext->m450_shop->m4654_buyQuantities[top + row] += 1;
                }
            }
            break;
        case 2:
            if (gMenuContext->m450_shop->m4654_buyQuantities[top + row]) {
                total -= price;
                newGold += price;
                redraw = 1;
                gMenuContext->m450_shop->m4654_buyQuantities[top + row] -= 1;
            }
            break;
        }
    }
    gMenuContext->m450_shop->m4785_45B0Enabled = 0;
    return 1;
}

/* show the selected item's name, sell price (half the list price) and who
 * can equip it; returns the sell price */
s32 showShopSellItemInfo(u8 id, u8 kind) {
    RECT rect;
    u8 unused[16]; /* never used; keeps retail's frame layout */
    u8 codes[14];
    u8 text[16];
    s32 divisors[5];
    u16* image;
    u16 equipMask;
    u16 mask2;
    s32 price;
    s32 value;
    s32 digit;
    s32 i;
    s32 j;
    u8 started;

    divisors[0] = 1;
    divisors[1] = 10;
    divisors[2] = 100;
    divisors[3] = 1000;
    divisors[4] = 10000;
    equipMask = 0;
    image = allocateHeap(0x618, 0);
    bzero(image, 0x618);
    bzero(codes, 0xE);
    switch (kind) {
    case 0:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth = renderString(getWeaponName(id), image, 0x39, 0);
        equipMask = gMenuContext->m330_itemData->m0_weaponStats[id].m0_equipMask;
        price = gMenuContext->m330_itemData->m0_weaponStats[id].m4_price >> 1;
        value = price;
        break;
    case 1:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth = renderString(getAccessoryName(id), image, 0x39, 0);
        equipMask = gMenuContext->m330_itemData->m4_accessoryData[id].m0_equipMask;
        price = gMenuContext->m330_itemData->m4_accessoryData[id].m2_price >> 1;
        value = price;
        break;
    case 2:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth = renderString(getUsableItemName(id), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m1C_usableItems[id].m2_price >> 1;
        value = price;
        break;
    }
    mask2 = getPartyEquippingItem(id, kind);
    started = 0;
    for (j = 0, i = 4; i > 0; j++, i--) {
        digit = value / divisors[i];
        if (digit != 0 || started) {
            codes[j * 2] = digit + 0x10;
            value -= digit * divisors[i];
            started = 1;
        } else {
            codes[j * 2] = 0xC3;
        }
    }
    codes[8] = value % 10 + 0x10;
    decodeCharacterCodes((u16*)codes, text, 5);
    gMenuContext->m450_shop->m44B0_label.m7E_stringWidth = renderString(text, image, 0x39, 1);
    rect.x = 0x140;
    rect.y = 0x4E;
    rect.w = 0x3C;
    rect.h = 0xD;
    LoadImage(&rect, (u32*)image);
    DrawSync(0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m4430_label, 0, 0, 0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m44B0_label, 0, 0, 0);
    gMenuContext->m450_shop->m44B0_label.m0_polys[gMenuContext->m308_oddOrEven].clut = textSpriteMode1;
    setupMenuPolyFT4(&gMenuContext->m450_shop->m4430_label.m0_polys[gMenuContext->m308_oddOrEven], 0x2C, 0x12, 0, 0x4E,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    setupMenuPolyFT4(&gMenuContext->m450_shop->m44B0_label.m0_polys[gMenuContext->m308_oddOrEven], 0x98, 0x12, 0, 0x4E,
                  gMenuContext->m450_shop->m44B0_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m4430_label.m50_vertex, 0x2C, 0x12,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m44B0_label.m50_vertex, 0x98, 0x12,
                  gMenuContext->m450_shop->m44B0_label.m7E_stringWidth, 0xD);
    gMenuContext->m450_shop->m4430_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m44B0_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    freeHeap(image);
    if (id) {
        gMenuContext->m450_shop->m46A7_4430Enabled = 1;
        gMenuContext->m450_shop->m46B5_44B0Enabled = 1;
    } else {
        gMenuContext->m450_shop->m46A7_4430Enabled = 0;
        gMenuContext->m450_shop->m46B5_44B0Enabled = 0;
    }
    gMenuContext->m450_shop->m46A9_2D0Length = 0;
    for (j = 0, i = 0; j < 16; j++) {
        if (gMenuContext->m30C_characterAvailable[j]) {
            if (menu2CheckBitmask(equipMask, j)) {
                gMenuContext->m450_shop->m469C_lineEnabled[i] = 1;
            } else {
                gMenuContext->m450_shop->m469C_lineEnabled[i] = 0;
            }
            if (menu2CheckBitmask(mask2, j)) {
                gMenuContext->m450_shop->m46A9_2D0Length +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE,
                                  gMenuContext->m450_shop->m2D0_polys[gMenuContext->m450_shop->m46A9_2D0Length],
                                  gMenuContext->m308_oddOrEven, shopMemberColumnX[i] + 0xE, 0xB4, 0x1000);
            }
            i++;
        }
    }
    gMenuContext->m450_shop->m46A8_2D0OddOrEven = gMenuContext->m308_oddOrEven;
    return price;
}

/* draw the 8 visible rows of the item list from entry 'start': name, a
 * 5-digit value and, when some are selected, 'x' and the selected count */
void setupShopSellListRows(s32 start, u8* ids, u8* kinds, u8* selCounts, u8* values) {
    RECT rect;
    u8 codes[14];
    u8 text[16];
    s32 divisors[5];
    u16* image;
    s32 i;
    s32 j;
    s32 k;
    s32 value;
    s32 digit;
    u8 started;

    divisors[0] = 1;
    divisors[1] = 10;
    divisors[2] = 100;
    divisors[3] = 1000;
    divisors[4] = 10000;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 8; i++) {
        bzero(codes, 0xE);
        gMenuContext->m450_shop->m468C_rowLength[i] = 0;
        if (ids[start + i] != 0) {
            value = values[start + i];
            switch (kinds[start + i]) {
            case 0:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getWeaponName(ids[start + i]), image, 0x24, 0);
                break;
            case 1:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getAccessoryName(ids[start + i]), image, 0x24, 0);
                break;
            case 2:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getUsableItemName(ids[start + i]), image, 0x24, 0);
                break;
            }
            started = 0;
            for (j = 0, k = 4; k > 0; j++, k--) {
                digit = value / divisors[k];
                if (digit != 0 || started) {
                    codes[j * 2] = digit + 0x10;
                    value -= digit * divisors[k];
                    started = 1;
                } else {
                    codes[j * 2] = 0xC3;
                }
            }
            codes[j * 2] = value % 10 + 0x10;
            decodeCharacterCodes((u16*)codes, text, 5);
            gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth = renderString(text, image, 0x24, 1);
            rect.x = (i & 1) * 0x18 + 0x180;
            rect.y = (i / 2) * 0xD + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, (u32*)image);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m3C30_labels[i], i, 0x80, 0x81);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m3C30_labels[i].m50_vertex, 0x24, i * 0xD + 0x32,
                          gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth, 0xD);
            rect.x = (i & 1) * 0x18 + 0x180;
            rect.y = (i / 2) * 0xD + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, (u32*)image);
            DrawSync(0);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m4030_labels[i], i, 0x80, 0x82);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m4030_labels[i].m50_vertex, 0x8C, i * 0xD + 0x32,
                          gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth, 0xD);
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven; /* sic: not m4030_labels */
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 1;
            if (selCounts[start + i] != 0) {
                gMenuContext->m450_shop->m468C_rowLength[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE5, gMenuContext->m450_shop->m1DB0_rows[i],
                                  gMenuContext->m308_oddOrEven, 0xB4, i * 0xD + 0x36, 0x1000);
                if (selCounts[start + i] / 10) {
                    gMenuContext->m450_shop->m468C_rowLength[i] +=
                        setupStringInPolyFT4(gMenuContext->m2DC_font, selCounts[start + i] / 10,
                                      gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                                      gMenuContext->m308_oddOrEven, 0xBC, i * 0xD + 0x36, 0x1000);
                }
                gMenuContext->m450_shop->m468C_rowLength[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, selCounts[start + i] % 10,
                                  gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                                  gMenuContext->m308_oddOrEven, 0xC4, i * 0xD + 0x36, 0x1000);
                gMenuContext->m450_shop->m4694_rowOddOrEven[i] = gMenuContext->m308_oddOrEven;
            }
        } else {
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 0;
        }
    }
    freeHeap(image);
}

void completeShopSale(u32 gold, u8* ids, u8* counts, s32 count, u8* invIds, u8* invCounts, u8* kinds, u8 fromInventory,
                   u8 character) {
    s32 i;
    s32 j;

    playMenuSoundEffect(0xD1);
    gameState.m1924_gold = gold;
    if (gold > 9999999) {
        gameState.m1924_gold = 9999999;
    }
    if (fromInventory) {
        for (i = 0; i < count; i++) {
            if (ids[i] != 0) {
                for (j = 0; j < count; j++) {
                    if (ids[i] == invIds[j]) {
                        invCounts[j] -= counts[i];
                        if (invCounts[j] == 0) {
                            invIds[j] = 0;
                        }
                    }
                }
            }
        }
    } else {
        for (i = 0; i < count; i++) {
            if (ids[i] != 0 && counts[i] != 0) {
                switch (kinds[i]) {
                case 0:
                    if (ids[i] < 0x32) {
                        gameState.m26C_party[character].m6A_weaponModifier[0] = 0;
                    } else {
                        for (j = 0; j < 4; j++) {
                            if (ids[i] == gameState.m26C_party[character].m6F_equipment[j]) {
                                gameState.m26C_party[character].m6F_equipment[j] = 0;
                                break;
                            }
                        }
                    }
                    break;
                case 1:
                    for (j = 0; j < 3; j++) {
                        if (ids[i] == gameState.m26C_party[character].m74_accessorySlots[j]) {
                            gameState.m26C_party[character].m74_accessorySlots[j] = 0;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
}

/* sell menu: list the sellable entries of an inventory (or a character's
 * equipment), pick quantities, confirm and pay */
void runShopSellMenu(s32 count, u8* invIds, u8* invCounts, u8 category, u8 fromInventory, u8* kindsIn, u8 character) {
    u8 loop = 1;
    u8 first = 1;
    u8 redraw = 1;
    s32 row = 0;
    s32 prevRow = 0xFF;
    s32 top = 0;
    s32 prevTop = 0xFF;
    u8 ids[count];
    u8 kinds[count];
    u8 sel[count];
    u8 flags[count];
    u8 avail[count];
    s32 gold = gameState.m1924_gold;
    s32 total = 0;
    s32 newGold = gold;
    s32 price;
    s32 n;
    s32 k;
    s32 i;
    u8 ok;
    u8 t;

    /* Set only when an entry passes (an empty category leaves retail's stack
     * slot): the scroll bar and the cursor limit read it. 0 is the empty list. */
    n = 0;
    for (i = 0; i < count; i++) {
        avail[i] = 0;
        flags[i] = 0;
        sel[i] = 0;
        ids[i] = 0;
        if (fromInventory) {
            kinds[i] = category;
        } else {
            kinds[i] = kindsIn[i];
        }
    }
    k = 0;
    for (i = 0; i < count; i++) {
        if (invIds[i] != 0 && invCounts[i] != 0) {
            ok = 1;
            if (kinds[i] == 2) {
                t = gMenuContext->m330_itemData->m1C_usableItems[invIds[i]].m6_flags & 0x10;
                ok = t == 0;
            }
            if (ok) {
                ids[k] = invIds[i];
                avail[k] = invCounts[i];
                flags[k] = 1;
                k++;
                n = k;
            }
        }
    }
    allocMenuPointerGlyph(0);
    while (loop) {
        menuDraw();
        if (top != prevTop || redraw) {
            setupShopSellListRows(top, ids, kinds, sel, avail);
            setupScrollBarThumb(0xC, 0x32, 0x3C, n, top);
        }
        if (row != prevRow || top != prevTop) {
            prevRow = row;
            price = showShopSellItemInfo(ids[top + row], kinds[top + row]);
            prevTop = top;
            gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 1;
        }
        updateMenuPointerGlyph(row, top, 0, 0);
        if (first) {
            iniMenuContext364And380(2, 0xC, 0x2A, 0xC4, 0x74, 0, 1, 4, 1);
            iniMenuContext364And380(3, 0x20, 0xE, 0xFC, 0x14, 0, 1, 4, 0);
            processLoadSaveMenuSub1();
            if (fromInventory) {
                showShopPartyMembers();
            }
            while (gMenuContext->m329_animationState != 0) {
                menuDraw();
            }
            setupShopSellTitle();
            first = 0;
        }
        if (redraw) {
            setupShopGoldNumbers(gold, total, newGold);
            redraw = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (total != 0) {
                playMenuSoundEffect(2);
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                loop = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                setupShopTotalNumber(total);
                if (askYesNoQuestion(0x95, 0xFF, 1)) {
                    completeShopSale(newGold, ids, sel, count, invIds, invCounts, kinds, fromInventory, character);
                } else {
                    loop = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 1;
                    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
                    prevTop = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = 1;
                }
                disableDraw428();
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 5:
            loop = 0;
            if (total != 0) {
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                if (askYesNoQuestion(0x92, 0xFF, 1) == 0) {
                    loop = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 1;
                    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
                    prevTop = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = 1;
                }
                disableDraw428();
            }
            break;
        case 1:
            row++;
            if (row >= 8) {
                row = 7;
                top++;
                if (n - 8 < top) {
                    top--;
                }
            }
            break;
        case 3:
            row--;
            if (row < 0) {
                top--;
                row = 0;
                if (top < 0) {
                    top = 0;
                }
            }
            break;
        case 0:
            if (flags[top + row]) {
                if (avail[top + row] - 1 >= 0) {
                    redraw = 1;
                    total += price;
                    sel[top + row]++;
                    newGold += price;
                    avail[top + row]--;
                }
            }
            break;
        case 2:
            if (flags[top + row]) {
                if (sel[top + row] - 1 >= 0) {
                    total -= price;
                    redraw = 1;
                    avail[top + row]++;
                    newGold -= price;
                    sel[top + row]--;
                }
            }
            break;
        }
    }
}

u8 runShopSellEquippedAccessories(void) {
    s32 sel;
    s32 prev;
    u8 result;
    s32 i;
    s32 j;
    s32 k;
    u8 ids[8];
    u8 counts[8];
    u8 flags[8];

    sel = 0;
    prev = 1;
    result = 2;
    showShopPartyMembers();
    processLoadSaveMenuSub5(0x98);
    do {
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 5:
            result = 0;
            break;
        case 4:
            playMenuSoundEffect(2);
            result = 1;
            break;
        case 2:
            sel--;
            if (sel < 0) {
                sel = gMenuContext->m450_shop->m46A5_0Length - 1;
            }
            break;
        case 0:
            sel++;
            if (sel >= gMenuContext->m450_shop->m46A5_0Length) {
                sel = 0;
            }
            break;
        }
        if (sel != prev) {
            gMenuContext->m450_shop->m469C_lineEnabled[sel] = 1;
            gMenuContext->m450_shop->m469C_lineEnabled[prev] = 0;
            prev = sel;
        }
    } while (result == 2);
    processLoadSaveMenuSub7();
    if (result != 0) {
        i = 0;
        while (sel != 0) {
            i++;
            if (gMenuContext->m30C_characterAvailable[i] != 0) {
                sel--;
            }
        }
        for (j = 0; j < 8; j++) {
            ids[j] = 0;
            counts[j] = 1;
        }
        k = 0;
        for (j = 0; j < 3; j++) {
            if (gameState.m26C_party[i].m74_accessorySlots[j] != 0) {
                ids[k] = gameState.m26C_party[i].m74_accessorySlots[j];
                flags[k] = 1;
                k++;
            }
        }
        runShopSellMenu(8, ids, counts, 1, 0, flags, i);
    }
    return result;
}

void runShopSellAccessories(void) {
    runShopSellMenu(200, gameState.m1EC8_inventory2Ids, gameState.m1E00_inventory2Counts, 1, 1,
                  gameState.m1E00_inventory2Counts, 0);
}

void runShopSellWeapons(void) {
    runShopSellMenu(100, gameState.m1D9C_inventory1Ids, gameState.m1D38_inventory1Counts, 0, 1,
                  gameState.m1D38_inventory1Counts, 0);
}

void runShopSellItems(void) {
    runShopSellMenu(150, gameState.m2026_inventory0Ids, gameState.m1F90_inventory0Counts, 2, 1,
                  gameState.m1F90_inventory0Counts, 0);
}

void clearShopScreen(u8 arg0, u8 arg1) {
    s32 i;

    gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
    gMenuContext->m450_shop->m46A7_4430Enabled = 0;
    gMenuContext->m450_shop->m46B5_44B0Enabled = 0;
    gMenuContext->m450_shop->m46B2_numbersEnabled = 0;
    gMenuContext->m450_shop->m46AB_textLength = 0;
    gMenuContext->m450_shop->m46A9_2D0Length = 0;
    gMenuContext->m450_shop->m46A5_0Length = 0;
    for (i = 0; i < 9; i++) {
        gMenuContext->m450_shop->m469C_lineEnabled[i] = 0;
        gMenuContext->m450_shop->m46BC_27B0Length[i] = 0;
        gMenuContext->m450_shop->m46C5_3020Length[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        gMenuContext->m450_shop->m4684_labelEnabled[i] = 0;
        gMenuContext->m450_shop->m468C_rowLength[i] = 0;
    }
    if (arg0) {
        processLoadSaveMenuSub7Sub0(2);
        processLoadSaveMenuSub7Sub0(3);
        if (arg1) {
            processLoadSaveMenuSub7Sub0(5);
        }
        freeScrollBarThumb();
        freeMenuPointerGlyph(0);
    }
}

void executeShopSellSelection(void) {
    u8 ret;

    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
    setMenuLabelsEnabled(0, 4, gMenuContext->m6E0_mainMenuLabels, shopSellLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    ret = 1;
    switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
    case 0:
        ret = runShopSellEquippedAccessories();
        break;
    case 1:
        runShopSellAccessories();
        break;
    case 2:
        runShopSellWeapons();
        break;
    case 3:
        runShopSellItems();
        break;
    }
    clearShopScreen(ret, 0);
    gMenuContext->m33C_drawFlags->mA_354Enabled = 1;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    setMenuLabelsEnabled(1, 4, gMenuContext->m6E0_mainMenuLabels, shopSellLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
}

u8 openShopSellMenu(void) {
    u8 loop;
    u8 first;

    loop = 1;
    first = 1;
    gMenuContext->m338_currentSelectedSubMenuEntry = 3;
    gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
    do {
        menuDraw();
        if (first) {
            setMenuLabelsEnabled(1, 4, gMenuContext->m6E0_mainMenuLabels, shopSellLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            first = 0;
            processLoadSaveMenuSub1();
            processLoadSaveMenuSub3(0);
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            updateMenuSelection2(4, gMenuContext->m6E0_mainMenuLabels, shopSellLabelConfig, shopSubMenuLabelOffsetsX, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m338_currentSelectedSubMenuEntry, 3, 0);
            processLoadSaveMenuSub8(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            playMenuSoundEffect(2);
            executeShopSellSelection();
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            break;
        case 5:
            loop = 0;
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
    } while (loop);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    setMenuLabelsEnabled(0, 4, gMenuContext->m6E0_mainMenuLabels, shopSellLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    return 1;
}

void cleanupShopMainMenuSelection(void) {
    switch (gMenuContext->m336_menuSelectedEntry) {
    case 1:
        break;
    case 2:
        clearShopScreen(1, 1);
        break;
    }
}
