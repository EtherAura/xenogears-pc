/* The gear shop's own code, first file: it follows the gear shop's build of
 * main-menu m_801E8018 (which ends with processLoadSaveMenuSub8) and calls
 * initMemoryCardPolyVerts without a prototype. gcc-2.6.3-psx, ASPSX 2.34, -G0. */
#include "common.h"
#define MENU_FAMILY_OVERLAY
#include "menu/menu.h"
#include "menu/shop.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pollhost.h"
#include "main/game_state.h"
#include "main/dialog.h"

extern u16 gearShopMenu2Bitmask[];
extern u32 gearShopMenu2Bitmask32[];
extern MenuContext* gMenuContext;
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode);
u8* getDialogParamPointer(u8* buf, s32 idx);
extern u8 gearShopInitMenuLabelConfig[];
void loadBlankImage(void);
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count);
void computeMenuBorder(void* font, s32 glyph, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpageX, s32* tpageY);
extern s32 gearShopCursorPosXTable[];
extern s32 gearShopCursorPosYTable[];
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

/* MF2 declarations */
#include "menu/shop.h"
#include "field_mecha/mecha.h"
#include "main/memory.h"
#include "main/filesystem.h"

#include "main/memory.h"
#include "menu/shop.h"
void countField33C_C(u8 count, u8* arr); /* = menu func_801E8044 (Noah countField33C_C) */
void clearShopScreen(u8 arg0);
void refuelGearShopGear(void);
void applyAllGearPartStats(MenuContext_330* ctx, u8 gearId);
extern u8 gearShopGearId;
void applyGearFrameStats(MenuContext_330* ctx, u8 gearId);
void applyGearEngineStats(MenuContext_330* ctx, u8 gearId); /* = menu func_801E42AC */
void applyGearArmorStats(MenuContext_330* ctx, u8 gearId); /* = menu func_801E4258 */
void applyGearAccessoryBonuses(MenuContext_330* ctx, u8 gearId); /* like menu func_801E433C */
void applyGearWeaponStats(MenuContext_330* ctx, u8 gearId); /* = menu func_801E4754 */
void mechaInitOverlay(s32 count); /* field_mecha overlay (Noah mechaInitOverlay) */
extern MATRIX* mechaFinalizeVar0;     /* field_mecha overlay: colour matrix pointer */
void loadGearModelFiles(u8 gearId, u8 arg1);
void countGearShopMembers(void);
extern u8 gearShopDebugValuesEnabled;
void freeMechaModule(void); /* field_mecha overlay (Noah freeMechaModule) */
extern u8 gearShopGearPilotIds[20]; /* gear -> pilot (party member) */
u8 computeGearAgilityPenalty(u8 gearId); /* = menu func_801E4928 */
u8 hideGearShopMainMenu(void);
void processLoadSaveMenuSub3(u8 arg0);
void processLoadSaveMenuSub8(u8 arg0);
void turnGearToSelectedPart(void);
void executeGearShopSellSelection(u8 arg0);
void resetGearShopCamera(void);
void setupGearShopStatsPanel(void);
void cycleGearShopGear(u8 arg0);
extern s32 gearShopAvailableCount;
extern u8 gearShopSubMenuLabelConfig[];  /* submenu label config */
extern s32 gearShopSubMenuLabelOffsetsX[]; /* submenu label x offsets */

/* MF3 declarations (G123-G139: the gear shop's buy/sell screens, 0x801D1304-0x801D573C) */
extern u16 textSpriteMode1;   /* textSpriteMode1 (CLUT) */
extern u8 gearShopSubMenuLabelConfig[];  /* sell screen submenu labels (j_setupMenuContext4E0 config) */
extern u8 gearShopBuyLabelConfig[];  /* buy screen submenu labels */
extern s32 gearShopSubMenuLabelOffsetsX[]; /* buy screen label x offsets (updateMenuSelection2) */
extern u16 gearShopOwnedCount;   /* how many of the selected item the party owns (setupGearShopOwnedCount) */
extern s32 gearShopStockCounts[]; /* buy list: entry count per kind (group * 3 + submenu entry) */


void setupMenuPolyFT4(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u16 w, u16 h); /* = menu func_801E920C */
u32 menu2CheckBitmask32(u32 mask, u8 bit);                                   /* = menu func_801C8678 */
void setupScrollBarThumb(s32 x, s32 y, s32 h, s32 count, s32 pos);
void allocMenuPointerGlyph(u8 arg0);
void updateMenuPointerGlyph(s32 row, s32 scroll, u8 arg2, u8 arg3);
void processLoadSaveMenuSub7Sub0(u8 index);
void iniMenuContext364And380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 animate, u8 transformDisabled, s32 arg7,
                   u8 arg8);
void setupDraw428(u8 mode);
void disableDraw428(); /* refuelGearShopGear passes it a 0, runGearShopSellMenu nothing */
void countField33C_C(u8 count, u8* arr); /* = menu func_801E8044 */
void showShopPartyMembers(void);
void setupGearShopTitle(u8 isPart);
void setupShopSellTitle(void);
void setupGearShopPriceNumbers(u32 value1, u32 value2, u32 value3, u32 value4, u8 row);
void setupShopTotalNumber(u32 value, u8 arg1);
void setupGearShopGoldNumber(u32 gold);
void clearShopScreen(u8 arg0);
s32 showGearShopSellItemInfo(u8 itemId, u8 kind);
void setupGearShopSellListRows(s32 start, u8* ids, u8* kinds, u8* amounts, u8* counts);
/* Sell: set the gold (capped at 9999999) and, when update, take the sold
 * amounts out of the inventory (ids/counts; an entry emptied loses its id) */
void completeShopSale(u32 gold, u8* boughtIds, u8* boughtCounts, s32 count, u8* ids, u8* counts, u8* arg6, u8 update,
                   u8 arg8);
void runGearShopSellMenu(s32 count, u8* ids, u8* counts, u8 category, u8 arg4, u8* arg5, u8 arg6);
void runGearShopSellAccessories(void);
void runGearShopSellWeapons(void);
void setupGearShopOwnedPartLabel(u8 kind);
s32 setupGearShopBuyListRows(s32 start, s32 gold, u8* flags);
void computeGearShopStatChange(s32* diffs, u8* gains, u8 itemId, u8 kind, u8 slot);
u16 findInventoryCount(u8* keys, u8* values, s32 count, u8 key);
void setupGearShopOwnedCount(u8 kind, u8 itemId);
s32 showGearShopBuyItemInfo(s32 row, s32 scroll, u8* flags);
void completeGearShopPurchase(u32 gold);
u8 isNewerGearPart(s32 idx);
void computeGearStatusStats(MenuContext_330* ctx, u8 gearId);
void applyAllGearPartStats(MenuContext_330* ctx, u8 gearId);


/* gear shop functions (twins of main-menu functions noted) */
void menuDraw(void);
void updateGearShopSparkles(void);
void updateGearShopSpark(void);
void updateGearShopTwinkle(void);

/* field_mecha (loaded at 0x801DC000 by the gear shop) */


extern s32 gearShopAvailableCount;
extern u8 gearShopTitleGlyphs[];  /* glyph ids [row * 4 + i] (setupGearShopTitle) */
extern u8 gearShopSellTitleGlyphs[2];
extern s32 gearShopTitleX[]; /* x [row * 4 + i] */
extern s32 gearShopSellTitleX[2];
extern s32 gearShopTitleY[]; /* y [row * 4 + i] */
extern s32 gearShopSellTitleY[2];
u8 runGearShopSubmenu(void);
void processLoadSaveMenuSub1Alt2(void);
void j_setupMenuContext4E0(u8 count, MenuContext_4E0* p, void* config, u8* flags);
extern u8 gearShopMainLabelConfig[];
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
extern u16* gearShopNameImage;
extern u16 gearShopModelFiles[]; /* gear model file index (dir 4) per gear */
extern s32 gearShopMemberColumnX[]; /* x of the glyphs drawn by showShopPartyMembers */
void processLoadSaveMenuSub5(u8 arg0);
void processLoadSaveMenuSub7(void);
u8 runYesNoPrompt(u8 arg0, u8 arg1);
void playMenuSoundEffect(u8 arg0);
void stopSequence(void*);
void unloadSequence(void*);
void initMenuContext32C(u8 arg0);
void initMenuContext33C(u8 arg0);
void initMenuContext350MainMenu(u8 arg0);
void initMenuContext354(u8 arg0);
void initMenuContext330(u8 arg0);
void initMenuContext348(u8 arg0);
void initMenuContext1E20(u8 arg0);
void initMenuContext450Shop(u8 arg0);
void initMenuContext454GearShop(u8 arg0);
void loadGearShopData(u8 arg0);
extern s32 gearShopSwitchArrowX[]; /* x of the two arrows */
void initGearShopCameraTween(void);
/* gear model rotation animated by initGearShopCameraTween (from -> to); 0x801D9050 */
typedef struct {
    /* 0x00 */ s32 m0_from[3];
    /* 0x0C */ s32 mC_to[3];
    /* 0x18 */ s32 m18_step[3];
    /* 0x24 */ s32 m24_offset[3];
    /* 0x30 */ u8 m30_reversed[3];
    /* 0x33 */ u8 m33_stepsPerFrame;
} GearShopRotation;
extern GearShopRotation gearShopCameraTween;
extern s32 gearShopSelectedIndex; /* selected gear (index among the m30C flags), gearShopAvailableCount = count */
extern u8 gearShopGearId;  /* gear number shown */

void setupGearShopModel(u8 slot, u8 gear);
void loadGearModelFiles(u8 gear, u8 slot);
extern s32 gDepthDivider;
extern u16 gearShopModelGroundY[]; /* per gear: ground y */
extern u16 gearShopModelScales[]; /* per gear: scale */
extern u8 gearShopModelAnimations[];  /* per gear: animation id */
void setupGearShopStatsPanel(void);
void fillStringDisplayBuffer(u32 value); /* = menu func_801C80B8 (fillStringDisplayBuffer) */
u8 randomInRange(s32 arg0, s32 arg1);
extern u16 gearShopTwinkleX[]; /* blink x choices */
extern u16 gearShopTwinkleY[]; /* blink y choices */
void initMenuGlobals(void);
void resetMenuOddOrEven(void);
void initMenuContext4E0(void);
void computeMenuBorders(void);
void initPrimitives_348_cursor(void);
void setupGearShopStock(void);
void initGearShopView(void);
void gearShopMainMenuExecute(void);
void shutdownGearShopMenu(void);
void setupGearSwitchArrows(void);
extern s32 gearShopGoldNumberX; /* number panel positions (setupGearShopPriceNumbers) */
extern s32 gearShopGoldNumberY;
extern s32 gearShopTotalNumberX;
extern s32 gearShopTotalNumberY;
extern s32 gearShopNewGoldNumberX;
extern s32 gearShopNewGoldNumberY;
u32 getMenuBitMask32(u8 bit); /* gearShopMenu2Bitmask32[bit] */
extern s16 gearShopCameraDistances[];    /* per gear: rotation target x */
extern s16 gearShopCameraHeights[]; /* [(gear * 3 + m336) * 4 + m338] */
extern s16 gearShopGearYaws[]; /* [m336 * 4 + m338] */
extern u32 gearShopSubMenuConfig[32]; /* [(row + m336) * 8 + i * 2 + 0/1]: submenu entry glyph pairs (cf. menu D_801EA1EC) */
void updateMenuSelection2(s32 count, MenuContext_4E0* labels, void* unused, s32* xOffsets, u8* flags, u8 sel, u8 base, u8 mode);
void cycleGearShopGear(u8 dir);
void freeGearShopView(void);
void freeGearSwitchArrows(void);
u8 gearShopExecuteMainMenuSelection(void);
extern u32 gearShopMainMenuConfig[][2]; /* main menu entries (glyph pairs) */
extern s32 gearShopMainLabelOffsetsX[];    /* label x offsets */
extern u8 gearShopDebugValuesEnabled;
extern u8 gearShopSparkPositions[];  /* [m336 * 4 + m338]: spark position index */
extern u16 gearShopSparkX[]; /* spark x */
extern u16 gearShopSparkY[]; /* spark y */
extern u16 gearShopSparkleGlyphs[]; /* [slot * 20 + frame * 4 + j]: sparkle glyphs, 0xFFFF = none */
extern u16 gearShopSparkleX[]; /* per slot: sparkle x */
extern u16 gearShopSparkleY[]; /* [slot * 6 + k]: sparkle y */
extern u16 gearShopStatLabelGlyphs[]; /* gear stats screen: 14 fixed glyphs */
extern u16 gearShopStatLabelX[]; /* their x */
extern u16 gearShopStatLabelY[]; /* their y */
/* gear stats number positions (x, y): hp, max hp, fuel, max fuel, m68 */
extern u16 gearShopHpPositionX;
extern u16 gearShopHpPositionY;
extern u16 gearShopMaxHpPositionX;
extern u16 gearShopMaxHpPositionY;
extern u16 gearShopFuelPositionX;
extern u16 gearShopFuelPositionY;
extern u16 gearShopMaxFuelPositionX;
extern u16 gearShopMaxFuelPositionY;
extern u16 gearShopWeightPositionX;
extern u16 gearShopWeightPositionY;
void renderGearNameInVram(u8 slot);

char* strcpy(char* dst, const char* src);
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
extern u32* menuSharedResources;
extern void* fieldSeq;
u16 menu2CheckBitmask(u16 mask, u8 bit);
void loadMenuSharedResources(void);
extern s32 gearShopMemberColumnX[];
extern s32 gearShopStockCounts[5];
void drawMenuBoxCorners(s32 box);
void drawMenuBoxInnerFrame(s32 box);
void drawMenuBoxTopFrame(s32 box);
void drawMenuBoxBottomFrame(s32 box);
void drawMenuBoxLeftFrame(s32 box);
void drawMenuBoxRightFrame(s32 box);
void drawMenuBoxBackground(s32 box);
extern s32 gearShopSaveCursorToSlot[];
extern s32 gearShopMemoryCardTilesPositionX[];
extern s32 gearShopMemoryCardTilesPositionY[];
void drawMenu_380(void);
void updateGearShopEffects(void);
void drawMenu_428(void);
void drawMenu2_sub(void);
void drawMenu_348_cursor(void);
void drawMenuPointerGlyphs(void);
void drawShopMenu(void);
void drawCharacterSwitchArrows(void);
void drawScrollBarThumb(void);
void drawMainMenu(void);
void drawMenuContext354(void);
void drawMenuBoxes(void);
void drawGearShopPolys(void);
void drawGearShopMechas(void);
void drawMenuBackground(void);
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField;
s32 isControllerConnected(s32 port);
void resumeMusic(void);
void pauseMusic(void);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void playMenuSoundEffect(u8 sfx);
void updateGearShopCameraTween(void);
extern POLY_FT4 gearShopDebugPolys[][2];
extern s32 gearShopDebugPolyCount;
void updateMenuInput(void);
void checkSoftReboot(void);
void updateMenuTransforms(void);
void drawGearShopDebugValues(void);
void drawMenuSpecificElements(void);
void AddPrims(void* ot, void* p0, void* p1);
extern s32* pRunningOnDTL;
extern s32 gearShopCardCursorStartX[];
extern s32 gearShopCardCursorStartY[];

/* This file calls these without prototypes: retail passes unmasked ints
 * for their u8/u16 parameters and uses their results unmasked. */
void initMemoryCardPolyVerts();

/* defined in earlier files */
void freeScrollBarThumb(void);
void freeMenuPointerGlyph(u8 arg0);
void setupMainMenuWithConfig(s32 count, u32 (*config)[2]);
void updateMenuSelection(u8 count, u8 sel, u32 (*config)[2]);

u8 gearShopExecuteMainMenuSelection(void) {
    u8 ret;
    u8 r;

    /* Entry 0 (the shop closes) tests r unset: retail's $v1, which the caller
     * has just loaded with the m348_cursor block's address (PCSX-Redux:
     * 0x800D47E4), so the main menu labels are set up once more first. */
    r = 1;
    ret = 1;
    if (gMenuContext->m336_menuSelectedEntry) {
        r = runGearShopSubmenu();
    } else {
        ret = 0;
    }
    if (r) {
        processLoadSaveMenuSub1Alt2();
        j_setupMenuContext4E0(4, gMenuContext->m6E0_mainMenuLabels, gearShopMainLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    }
    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 1;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
    return ret;
}

/* gear shop main loop: the top menu (4 entries) with gear switching (buttons 9/10) */
void gearShopMainMenuExecute(void) {
    u8 loop;

    loop = 1;
    setupGearShopModel(1, gearShopGearId);
    gMenuContext->m33C_drawFlags->m63_drawGearShop[0] = 1;
    gMenuContext->m336_menuSelectedEntry = 2;
    setupMainMenuWithConfig(5, gearShopMainMenuConfig);
    j_setupMenuContext4E0(4, gMenuContext->m6E0_mainMenuLabels, gearShopMainLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    if (gearShopAvailableCount >= 2) {
        gMenuContext->m33C_drawFlags->m53_draw440 = 1;
    }
    do {
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 4:
            playMenuSoundEffect(2);
            gMenuContext->m350_mainMenu->m1192_dimmed = 1;
            resetCursorState();
            countField33C_C(4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x4C;
            loop = gearShopExecuteMainMenuSelection();
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
            break;
        case 5:
            loop = 0;
            break;
        case 1:
            if (gMenuContext->m336_menuSelectedEntry) {
                gMenuContext->m336_menuSelectedEntry--;
            } else {
                gMenuContext->m336_menuSelectedEntry = 3;
            }
            break;
        case 3:
            if (++gMenuContext->m336_menuSelectedEntry >= 4) {
                gMenuContext->m336_menuSelectedEntry = 0;
            }
            break;
        case 9:
            cycleGearShopGear(0);
            break;
        case 10:
            cycleGearShopGear(1);
            break;
        }
        if (gMenuContext->m336_menuSelectedEntry != gMenuContext->m337_previousMenuSelectedEntry) {
            updateMenuSelection(4, gMenuContext->m336_menuSelectedEntry, gearShopMainMenuConfig);
            updateMenuSelection2(4, gMenuContext->m6E0_mainMenuLabels, gearShopMainLabelConfig, gearShopMainLabelOffsetsX, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m336_menuSelectedEntry, 0, 0);
            gMenuContext->m337_previousMenuSelectedEntry = gMenuContext->m336_menuSelectedEntry;
        }
    } while (loop);
    gearShopDebugValuesEnabled = 0;
    freeGearShopView();
    freeGearSwitchArrows();
}

/* gear shop entry point: allocate the contexts, set up the gear view, run the shop, tear down */
void gearShopMenuEntryPoint(void) {
    initMenuContext32C(1);
    initMenuContext33C(1);
    initMenuContext350MainMenu(1);
    initMenuContext354(1);
    initMenuContext330(1);
    initMenuContext348(1);
    initMenuContext1E20(1);
    initMenuContext450Shop(1);
    initMenuContext454GearShop(1);
    gMenuContext->m350_mainMenu->m1180_backgroundRect.x = 0x2C0;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.y = 0x100;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.w = 0x140;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.h = 0xE0;
    gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
    gMenuContext->m1E0_menuTranslation.vz = 0x200;
    gMenuContext->m1D8_menuRotation.vz = 0;
    gMenuContext->m1D8_menuRotation.vx = 0;
    gMenuContext->m1D8_menuRotation.vy = 0;
    gMenuContext->m220_menuTranslation2.vz = 0x400;
    gMenuContext->m218_menuRotation2.vz = 0;
    gMenuContext->m218_menuRotation2.vx = 0;
    gMenuContext->m218_menuRotation2.vy = 0x400;
    gMenuContext->m329_animationState = 0;
    gearShopCameraTween.m0_from[2] = -0x400;
    gearShopCameraTween.mC_to[2] = -0x400;
    gearShopCameraTween.m0_from[0] = 0x400;
    gearShopCameraTween.m0_from[1] = 0;
    gearShopCameraTween.mC_to[0] = 0x400;
    gearShopCameraTween.mC_to[1] = 0;
    gearShopCameraTween.m33_stepsPerFrame = 0x10;
    initMenuGlobals();
    resetMenuOddOrEven();
    initMenuContext4E0();
    initPrimitives_348_cursor();
    computeMenuBorders();
    setupGearShopStock();
    initGearShopView();
    gMenuContext->m440_arrows = allocateHeap((s32)sizeof(struct MenuContext_440), 0);
    bzero(gMenuContext->m440_arrows, (s32)sizeof(struct MenuContext_440));
    setupGearSwitchArrows();
    gMenuContext->m327_drawEnabled = 1;
    gMenuContext->m32A_soundEnabled = 1;
    gearShopMainMenuExecute();
    shutdownGearShopMenu();
}

/* left/right arrows 0x164/0x165 in m440 at y 0x64 (cf. menu func_801D3488). Retail passes
 * initMemoryCardPolyVerts (u16 params) unmasked ints: this file has no prototype for it. */
void setupGearSwitchArrows(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x164 + i, &gMenuContext->m440_arrows->m0_polys[i * 4],
                      gMenuContext->m308_oddOrEven, gearShopSwitchArrowX[i], 0x64, 0x1000);
    }
    for (i = 0; i < 4; i++) {
        initMemoryCardPolyVerts(gMenuContext->m440_arrows->m140_vertex[i], gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    gMenuContext->m440_arrows->m1C0_oddOrEven = gMenuContext->m308_oddOrEven;
}

void freeGearSwitchArrows(void) {
    gMenuContext->m33C_drawFlags->m53_draw440 = 0;
    menuDraw();
    freeHeap(gMenuContext->m440_arrows);
}

/* draw the shop's (m450) lines, labels and numbers */
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
        if (gMenuContext->m450_shop->m46B6_4530Enabled) {
            transformAndAddPrim(1, gMenuContext->m450_shop->m4530_label.m50_vertex, gMenuContext->m450_shop->m4530_label.m0_polys,
                          gMenuContext->m450_shop->m4530_label.m7D_oddOrEven);
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
            if (gMenuContext->m450_shop->m46B9_14F0Enabled) {
                drawMultiPolys(gMenuContext->m450_shop->m46B8_14F0Length, gMenuContext->m450_shop->m14F0_number4[0],
                              gMenuContext->m450_shop->m46B7_14F0OddOrEven);
            }
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
    if (gMenuContext->m33C_drawFlags->m63_drawGearShop[1]) {
        drawMultiPolys(gMenuContext->m450_shop->m46BB_17C0Length, gMenuContext->m450_shop->m17C0_polys[0],
                      gMenuContext->m450_shop->m46BA_17C0OddOrEven);
    }
}

/* draw the gear models (field_mecha renderMechas). Retail calls it with four arguments (no
 * fifth on the stack): the gear shop's view of renderMechas had four parameters. */
void drawGearShopMechas(void) {
    if (gMenuContext->m33C_drawFlags->m63_drawGearShop[0]) {
        ((void (*)(MATRIX*, MATRIX*, OTag*, s32))renderMechas)(
            &gMenuContext->m230_gearMatrix, &gMenuContext->m298_lightMatrix,
            (OTag*)gMenuContext->m1D4_currentDrawContext->mB0, gMenuContext->m308_oddOrEven);
    }
}

/* draw the gear shop's (m454) polys */
void drawGearShopPolys(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m63_drawGearShop[2]) {
        if (gMenuContext->m454_gearShop->m1ED8_mode[0]) {
            drawMultiPolys(gMenuContext->m454_gearShop->m1ED0_length[0], gMenuContext->m454_gearShop->mD0_blinkPolys[0],
                          gMenuContext->m454_gearShop->m1ED4_oddOrEven[0]);
            drawMultiPolys(gMenuContext->m454_gearShop->m1ED0_length[0], gMenuContext->m454_gearShop->mD0_blinkPolys[1],
                          gMenuContext->m454_gearShop->m1ED4_oddOrEven[0]);
            drawMultiPolys(gMenuContext->m454_gearShop->m1ED0_length[0], gMenuContext->m454_gearShop->mD0_blinkPolys[2],
                          gMenuContext->m454_gearShop->m1ED4_oddOrEven[0]);
        }
        for (i = 0; i < 2; i++) {
            if (gMenuContext->m454_gearShop->m1ED8_mode[i + 1]) {
                drawMultiPolys(gMenuContext->m454_gearShop->m1ED0_length[i + 1], &gMenuContext->m454_gearShop->m2B0_sparkles[i * 22],
                              gMenuContext->m454_gearShop->m1ED4_oddOrEven[i + 1]);
            }
        }
        if (gMenuContext->m454_gearShop->m1ED8_mode[3]) {
            drawMultiPolys(gMenuContext->m454_gearShop->m1ED0_length[3], gMenuContext->m454_gearShop->mD00_spark[0],
                          gMenuContext->m454_gearShop->m1ED4_oddOrEven[3]);
        }
    }
    if (gMenuContext->m33C_drawFlags->m63_drawGearShop[3]) {
        drawMultiPolys(1, gMenuContext->m454_gearShop->m0_poly, gMenuContext->m308_oddOrEven);
        drawMultiPolys(14, gMenuContext->m454_gearShop->m12A0_statLabels[0], gMenuContext->m454_gearShop->m1EED_oddOrEven);
        drawMultiPolys(gMenuContext->m454_gearShop->m1EE8_length[0], gMenuContext->m454_gearShop->m1700_statDigits[0][0],
                      gMenuContext->m454_gearShop->m1EED_oddOrEven);
        drawMultiPolys(gMenuContext->m454_gearShop->m1EE8_length[1], gMenuContext->m454_gearShop->m1700_statDigits[1][0],
                      gMenuContext->m454_gearShop->m1EED_oddOrEven);
        drawMultiPolys(gMenuContext->m454_gearShop->m1EE8_length[2], gMenuContext->m454_gearShop->m1700_statDigits[2][0],
                      gMenuContext->m454_gearShop->m1EED_oddOrEven);
        drawMultiPolys(gMenuContext->m454_gearShop->m1EE8_length[3], gMenuContext->m454_gearShop->m1700_statDigits[3][0],
                      gMenuContext->m454_gearShop->m1EED_oddOrEven);
        drawMultiPolys(gMenuContext->m454_gearShop->m1EE8_length[4], gMenuContext->m454_gearShop->m1700_statDigits[4][0],
                      gMenuContext->m454_gearShop->m1EED_oddOrEven);
    }
}

/* sparkle animations of slots 1 and 2 (glyph strings from gearShopSparkleGlyphs) */
void updateGearShopSparkles(void) {
    s32 i;
    s32 j;
    u8 set;
    u16 glyph;
    u16 pos[2][4];

    for (i = 0; i < 2; i++) {
        set = 0;
        if (gMenuContext->m454_gearShop->m1ED8_mode[i + 1] == 0) {
            continue;
        }
        switch (gMenuContext->m454_gearShop->m1ED8_mode[i + 1]) {
        case 1:
            pos[0][i] = gearShopSparkleX[i];
            pos[1][i] = gearShopSparkleY[i * 6 + gearShopSparkPositions[gMenuContext->m336_menuSelectedEntry * 4 +
                                                     gMenuContext->m338_currentSelectedSubMenuEntry]];
            set = 1;
            break;
        case 2:
            if (randomInRange(0, 0xFF) < 0x10) {
                pos[0][i] = gearShopSparkleX[i];
                pos[1][i] = gearShopSparkleY[i * 6 + randomInRange(0, 5)];
                set = 1;
            }
            break;
        }
        if (set) {
            gMenuContext->m454_gearShop->m1EF4_x[i] = pos[0][i];
            gMenuContext->m454_gearShop->m1EF8_y[i] = pos[1][i];
        }
        if (gMenuContext->m454_gearShop->m1EDC_timer[i + 1]) {
            gMenuContext->m454_gearShop->m1EDC_timer[i + 1]--;
            continue;
        }
        gMenuContext->m454_gearShop->m1ED0_length[i + 1] = 0;
        for (j = 0; j < 4; j++) {
            glyph = gearShopSparkleGlyphs[i * 20 + (gMenuContext->m454_gearShop->m1EE4_frame[i] * 4 + j)];
            if (glyph != 0xFFFF) {
                gMenuContext->m454_gearShop->m1ED0_length[i + 1] += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, glyph,
                    &gMenuContext->m454_gearShop->m2B0_sparkles[i * 22 + gMenuContext->m454_gearShop->m1ED0_length[i + 1] * 2],
                    gMenuContext->m308_oddOrEven, gMenuContext->m454_gearShop->m1EF4_x[i],
                    gMenuContext->m454_gearShop->m1EF8_y[i], 0x1000);
            }
        }
        gMenuContext->m454_gearShop->m1ED4_oddOrEven[i + 1] = gMenuContext->m308_oddOrEven;
        switch (gMenuContext->m454_gearShop->m1ED8_mode[i + 1]) {
        case 1:
            if (++gMenuContext->m454_gearShop->m1EE4_frame[i] == 4) {
                gMenuContext->m454_gearShop->m1ED8_mode[i + 1] = 2;
            }
            gMenuContext->m454_gearShop->m1EDC_timer[i + 1] = 2;
            break;
        case 2:
            if (++gMenuContext->m454_gearShop->m1EE4_frame[i] >= 5) {
                gMenuContext->m454_gearShop->m1EE4_frame[i] = 3;
            }
            gMenuContext->m454_gearShop->m1EDC_timer[i + 1] = 8;
            break;
        case 3:
            if (--gMenuContext->m454_gearShop->m1EE4_frame[i] < 0) {
                gMenuContext->m454_gearShop->m1ED8_mode[i + 1] = 0;
                gMenuContext->m454_gearShop->m1ED8_mode[0] = 0;
            }
            gMenuContext->m454_gearShop->m1EDC_timer[i + 1] = 2;
            break;
        }
    }
}

/* spark animation (glyphs 0x172 + frame) at the selected part (mode 1) or at random (mode 2) */
void updateGearShopSpark(void) {
    u8 set;
    u16 x;
    u16 y;
    u8 k;

    if (gMenuContext->m454_gearShop->m1ED8_mode[3]) {
        if (gMenuContext->m454_gearShop->m1EDC_timer[3]) {
            gMenuContext->m454_gearShop->m1EDC_timer[3]--;
            return;
        }
        set = 0;
        switch (gMenuContext->m454_gearShop->m1ED8_mode[3]) {
        case 1:
            k = gearShopSparkPositions[gMenuContext->m336_menuSelectedEntry * 4 + gMenuContext->m338_currentSelectedSubMenuEntry];
            x = gearShopSparkX[k];
            set = 1;
            y = gearShopSparkY[k];
            break;
        case 2:
            if (randomInRange(0, 0xFF) < 4) {
                x = gearShopSparkX[randomInRange(0, 5)];
                k = randomInRange(0, 5);
                set = 1;
                y = gearShopSparkY[k];
            }
            break;
        }
        if (set) {
            gMenuContext->m454_gearShop->m1EFC_sparkX = x;
            gMenuContext->m454_gearShop->m1EFE_sparkY = y;
        }
        gMenuContext->m454_gearShop->m1ED0_length[3] = setupStringInPolyFT4(
            gMenuContext->m2DC_font, gMenuContext->m454_gearShop->m1EE2_frame + 0x172, gMenuContext->m454_gearShop->mD00_spark,
            gMenuContext->m308_oddOrEven, gMenuContext->m454_gearShop->m1EFC_sparkX, gMenuContext->m454_gearShop->m1EFE_sparkY,
            0x1000);
        gMenuContext->m454_gearShop->m1ED4_oddOrEven[3] = gMenuContext->m308_oddOrEven;
        gMenuContext->m454_gearShop->m1EDC_timer[3] = 1;
        switch (gMenuContext->m454_gearShop->m1ED8_mode[3]) {
        case 1:
            if (++gMenuContext->m454_gearShop->m1EE2_frame >= 3) {
                gMenuContext->m454_gearShop->m1ED8_mode[3] = 2;
            }
            break;
        case 2:
            if (++gMenuContext->m454_gearShop->m1EE2_frame >= 11) {
                gMenuContext->m454_gearShop->m1EE2_frame = 3;
            }
            break;
        case 3:
            gMenuContext->m454_gearShop->m1EE2_frame = 2;
            gMenuContext->m454_gearShop->m1ED8_mode[3] = 4;
            break;
        case 4:
            if (--gMenuContext->m454_gearShop->m1EE2_frame < 0) {
                gMenuContext->m454_gearShop->m1ED8_mode[3] = 0;
            }
            break;
        }
    }
}

/* twinkle: every 4 frames redraw three glyphs (0x167/0x168 alternating), sometimes at a new place */
void updateGearShopTwinkle(void) {
    s32 i;
    s32 dy;
    u8 r;
    u16 nx;
    u16 ny;

    if (gMenuContext->m454_gearShop->m1ED8_mode[0]) {
        if (gMenuContext->m454_gearShop->m1EDC_timer[0]) {
            gMenuContext->m454_gearShop->m1EDC_timer[0]--;
        } else {
            r = randomInRange(0, 0xFF);
            dy = 0;
            if (r < 8) {
                nx = gearShopTwinkleX[randomInRange(0, 5)];
                ny = gearShopTwinkleY[randomInRange(0, 5)];
                gMenuContext->m454_gearShop->m1EF0_blinkX = nx;
                gMenuContext->m454_gearShop->m1EF2_blinkY = ny;
            }
            for (i = 0; i < 3; i++) {
                gMenuContext->m454_gearShop->m1ED0_length[0] = setupStringInPolyFT4(
                    gMenuContext->m2DC_font, gMenuContext->m454_gearShop->m1EE0_blinkFrame + 0x167,
                    gMenuContext->m454_gearShop->mD0_blinkPolys[i], gMenuContext->m308_oddOrEven,
                    gMenuContext->m454_gearShop->m1EF0_blinkX + i * 8, gMenuContext->m454_gearShop->m1EF2_blinkY + dy,
                    0x1000);
                dy += 10;
            }
            gMenuContext->m454_gearShop->m1EE0_blinkFrame ^= 1;
            gMenuContext->m454_gearShop->m1ED4_oddOrEven[0] = gMenuContext->m308_oddOrEven;
            gMenuContext->m454_gearShop->m1EDC_timer[0] = 4;
        }
    }
}

void updateGearShopEffects(void) {
    if (gMenuContext->m33C_drawFlags->m63_drawGearShop[2]) {
        updateGearShopSparkles();
        updateGearShopSpark();
        updateGearShopTwinkle();
    }
}

/* render a character's name into VRAM (0x180, 0x48); cf. menu func_801E8DA8 */
void renderGearNameInVram(u8 slot) {
    RECT rect;

    gearShopNameImage = ALLOC_TEXT_IMAGE(0x3F6);
    bzero(gearShopNameImage, 0x3F6);
    renderString(gameState.m0_names[slot], gearShopNameImage, 0x24, 0);
    rect.x = 0x180;
    rect.y = 0x48;
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, gearShopNameImage);
    DrawSync(0);
    freeHeap(gearShopNameImage);
}

/* gear stats panel (m454 m12A0/m1700): labels, HP/max HP, fuel/max fuel, m68, and the gear's name */
void setupGearShopStatsPanel(void) {
    s32 i;
    s32 n;

    for (n = 0; n < 14; n++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, gearShopStatLabelGlyphs[n], gMenuContext->m454_gearShop->m12A0_statLabels[n],
                      gMenuContext->m308_oddOrEven, gearShopStatLabelX[n], gearShopStatLabelY[n], 0x1000);
    }
    fillStringDisplayBuffer(gameState.m978_gears[gearShopGearId].m60_hp);
    gMenuContext->m454_gearShop->m1EE8_length[0] = 0;
    for (i = 0; i < 5; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 4] != 0xFF) {
            gMenuContext->m454_gearShop->m1EE8_length[0] += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 4],
                gMenuContext->m454_gearShop->m1700_statDigits[0][gMenuContext->m454_gearShop->m1EE8_length[0]],
                gMenuContext->m308_oddOrEven, gearShopHpPositionX + i * 8, gearShopHpPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m978_gears[gearShopGearId].m64_maxHp);
    gMenuContext->m454_gearShop->m1EE8_length[1] = 0;
    for (i = 0, n = 0; i < 5; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 4] != 0xFF) {
            gMenuContext->m454_gearShop->m1EE8_length[1] += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 4],
                gMenuContext->m454_gearShop->m1700_statDigits[1][gMenuContext->m454_gearShop->m1EE8_length[1]],
                gMenuContext->m308_oddOrEven, gearShopMaxHpPositionX + n * 8, gearShopMaxHpPositionY, 0x1000);
            n++;
        }
    }
    fillStringDisplayBuffer(gameState.m978_gears[gearShopGearId].m38_fuel);
    gMenuContext->m454_gearShop->m1EE8_length[2] = 0;
    for (i = 0; i < 4; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 5] != 0xFF) {
            gMenuContext->m454_gearShop->m1EE8_length[2] += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 5],
                gMenuContext->m454_gearShop->m1700_statDigits[2][gMenuContext->m454_gearShop->m1EE8_length[2]],
                gMenuContext->m308_oddOrEven, gearShopFuelPositionX + i * 8, gearShopFuelPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m978_gears[gearShopGearId].m3A_maxFuel);
    gMenuContext->m454_gearShop->m1EE8_length[3] = 0;
    for (i = 0, n = 0; i < 4; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 5] != 0xFF) {
            gMenuContext->m454_gearShop->m1EE8_length[3] += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 5],
                gMenuContext->m454_gearShop->m1700_statDigits[3][gMenuContext->m454_gearShop->m1EE8_length[3]],
                gMenuContext->m308_oddOrEven, gearShopMaxFuelPositionX + n * 8, gearShopMaxFuelPositionY, 0x1000);
            n++;
        }
    }
    fillStringDisplayBuffer(gameState.m978_gears[gearShopGearId].m68_weight);
    gMenuContext->m454_gearShop->m1EE8_length[4] = 0;
    for (i = 0; i < 5; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 4] != 0xFF) {
            gMenuContext->m454_gearShop->m1EE8_length[4] += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 4],
                gMenuContext->m454_gearShop->m1700_statDigits[4][gMenuContext->m454_gearShop->m1EE8_length[4]],
                gMenuContext->m308_oddOrEven, gearShopWeightPositionX + i * 8, gearShopWeightPositionY, 0x1000);
        }
    }
    renderGearNameInVram(gearShopGearId + 11);
    gMenuContext->m454_gearShop->m1EED_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* load a gear's two model files (dir 4, files n and n+1) into m458_files[slot] */
void loadGearModelFiles(u8 gear, u8 slot) {
    setCurrentDirectory(4, 0);
    gMenuContext->m458_files[slot]->m0_mechaData2 = allocateHeap(getFileSizeAligned(gearShopModelFiles[gear]), 0);
    readFile(gearShopModelFiles[gear], gMenuContext->m458_files[slot]->m0_mechaData2, 0, 0x80);
    waitReadCompletion(0);
    gMenuContext->m458_files[slot]->m4_mechaData1 = allocateHeap(getFileSizeAligned(gearShopModelFiles[gear] + 1), 0);
    readFile(gearShopModelFiles[gear] + 1, gMenuContext->m458_files[slot]->m4_mechaData1, 0, 0x80);
    waitReadCompletion(0);
    setCurrentDirectory(0x10, 0);
}

/* set up gear model `slot` (files loaded by loadGearModelFiles) and play its idle animation.
 * The gear shop's view of mechaInitNewMecha (mechaInitNewMecha) takes tpageX as an int: retail
 * passes (slot << 6) + 0x200 unnarrowed (field_mecha's definition says s16). */
void setupGearShopModel(u8 slot, u8 gear) {
    u8 anim;

    anim = 0;
    ((void (*)(s32, u16, sMechaDataTable2*, sMechaDataTable1*, s32, s16, s16, s16, SVECTOR*))mechaInitNewMecha)(slot, 0, gMenuContext->m458_files[slot]->m0_mechaData2, gMenuContext->m458_files[slot]->m4_mechaData1, (slot << 6) + 0x200,
                  0, 0, slot + 0x1C0, &gMenuContext->m458_files[slot]->m8_position);
    loadedMechas[slot]->m60_groundY = gearShopModelGroundY[gear];
    loadedMechas[slot]->m1C_scale = gearShopModelScales[gear];
    loadedMechas[1]->m4_bones->m54_rotationAngles.vy -= 0x400;
    gDepthDivider = 0;
    loadedMechas[1]->m4_bones->m54_rotationAngles.vx -= 0x20;
    if (gear != 0xFF) {
        anim = gearShopModelAnimations[gear];
    }
    mechaPlayAnimation(slot, 0, anim);
    freeHeap(gMenuContext->m458_files[slot]->m4_mechaData1);
    gMenuContext->m458_files[slot]->m12_loaded = 1;
    setupGearShopStatsPanel();
    gMenuContext->m33C_drawFlags->m63_drawGearShop[3] = 1;
}

/* turn the gear model to the angle of the selected part and wait for the twinkles */
void turnGearToSelectedPart(void) {
    gearShopCameraTween.m0_from[0] = gearShopCameraTween.mC_to[0];
    gearShopCameraTween.m0_from[1] = gearShopCameraTween.mC_to[1];
    gearShopCameraTween.m0_from[2] = gearShopCameraTween.mC_to[2];
    gearShopCameraTween.mC_to[0] = gearShopCameraDistances[gearShopGearId];
    gearShopCameraTween.mC_to[1] = gearShopCameraHeights[gMenuContext->m336_menuSelectedEntry * 4 + gearShopGearId * 12 + gMenuContext->m338_currentSelectedSubMenuEntry];
    gearShopCameraTween.mC_to[2] = gearShopGearYaws[gMenuContext->m336_menuSelectedEntry * 4 + gMenuContext->m338_currentSelectedSubMenuEntry];
    initGearShopCameraTween();
    gMenuContext->m329_animationState = 7;
    gMenuContext->m454_gearShop->m1ED8_mode[0] = 1;
    gMenuContext->m454_gearShop->m1ED8_mode[1] = 1;
    gMenuContext->m454_gearShop->m1ED8_mode[2] = 1;
    gMenuContext->m454_gearShop->m1ED8_mode[3] = 1;
    gMenuContext->m454_gearShop->m1EDC_timer[0] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[1] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[2] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[3] = 0;
    gMenuContext->m454_gearShop->m1EE0_blinkFrame = 0;
    gMenuContext->m454_gearShop->m1EE4_frame[0] = 0;
    gMenuContext->m454_gearShop->m1EE4_frame[1] = 0;
    gMenuContext->m454_gearShop->m1EE2_frame = 0;
    gMenuContext->m454_gearShop->m1EF0_blinkX = 0x18;
    gMenuContext->m454_gearShop->m1EF2_blinkY = 0x6E;
    gMenuContext->m33C_drawFlags->m63_drawGearShop[2] = 1;
    while (gMenuContext->m454_gearShop->m1ED8_mode[1] != 2) {
        menuDraw();
    }
    while (gMenuContext->m454_gearShop->m1ED8_mode[3] != 2) {
        menuDraw();
    }
}

void resetGearShopCamera(void) {
    gMenuContext->m454_gearShop->m1ED8_mode[1] = 3;
    gMenuContext->m454_gearShop->m1ED8_mode[2] = 3;
    gMenuContext->m454_gearShop->m1ED8_mode[3] = 3;
    gMenuContext->m454_gearShop->m1EDC_timer[0] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[1] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[2] = 0;
    gMenuContext->m454_gearShop->m1EDC_timer[3] = 0;
    gMenuContext->m33C_drawFlags->m63_drawGearShop[2] = 1;
    gearShopCameraTween.m0_from[0] = gearShopCameraTween.mC_to[0];
    gearShopCameraTween.m0_from[1] = gearShopCameraTween.mC_to[1];
    gearShopCameraTween.m0_from[2] = gearShopCameraTween.mC_to[2];
    gearShopCameraTween.mC_to[0] = 0x400;
    gearShopCameraTween.mC_to[1] = 0;
    gearShopCameraTween.mC_to[2] = -0x400;
    initGearShopCameraTween();
    gMenuContext->m329_animationState = 7;
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
                                                                gMenuContext->m308_oddOrEven, gearShopMemberColumnX[j], 0xA6, 0x1000);
                j++;
            }
        }
        gMenuContext->m450_shop->m46A6_0OddOrEven = gMenuContext->m308_oddOrEven;
        menuDraw();
    }
}

void countGearShopMembers(void) {
    s32 i;

    for (i = 0; i < 11; i++) {
        if (gMenuContext->m30C_characterAvailable[i]) {
            gearShopAvailableCount++;
        }
    }
}

/* cycle the displayed gear (dir 0: next, else previous) and reload its model */
void cycleGearShopGear(u8 dir) {
    s32 n;
    s32 i;

    if (gMenuContext->m329_animationState == 0 && gearShopAvailableCount >= 2) {
        n = gearShopSelectedIndex;
        if (dir == 0) {
            n++;
            if (n >= gearShopAvailableCount) {
                n = 0;
            }
        } else {
            n--;
            if (n < 0) {
                n = gearShopAvailableCount - 1;
            }
        }
        gearShopSelectedIndex = n;
        n++;
        i = -1;
        while (n != 0) {
            i++;
            if (gMenuContext->m30C_characterAvailable[i]) {
                n--;
            }
        }
        menuDraw();
        gMenuContext->m458_files[1]->m12_loaded = 0;
        freeMecha(1);
        menuDraw();
        loadGearModelFiles(gameState.m26C_party[i].mA0_gearNum, 1);
        gearShopGearId = gameState.m26C_party[i].mA0_gearNum;
        setupGearShopModel(1, gameState.m26C_party[i].mA0_gearNum);
        menuDraw();
    }
}

void setupGearShopTitle(u8 row) {
    s32 i;

    gMenuContext->m450_shop->m46AB_textLength = 0;
    for (i = 0; i < 4; i++) {
        gMenuContext->m450_shop->m46AB_textLength += setupStringInPolyFT4(
            gMenuContext->m2DC_font, gearShopTitleGlyphs[row * 4 + i],
            gMenuContext->m450_shop->m5A0_textPolys[gMenuContext->m450_shop->m46AB_textLength],
            gMenuContext->m308_oddOrEven, gearShopTitleX[row * 4 + i], gearShopTitleY[row * 4 + i], 0x1000);
    }
    gMenuContext->m450_shop->m46AA_textOddOrEven = gMenuContext->m308_oddOrEven;
}

void setupShopSellTitle(void) {
    s32 i;

    gMenuContext->m450_shop->m46AB_textLength = 0;
    for (i = 0; i < 2; i++) {
        gMenuContext->m450_shop->m46AB_textLength += setupStringInPolyFT4(
            gMenuContext->m2DC_font, gearShopSellTitleGlyphs[i],
            gMenuContext->m450_shop->m5A0_textPolys[gMenuContext->m450_shop->m46AB_textLength],
            gMenuContext->m308_oddOrEven, gearShopSellTitleX[i], gearShopSellTitleY[i], 0x1000);
    }
    gMenuContext->m450_shop->m46AA_textOddOrEven = gMenuContext->m308_oddOrEven;
}

/* (the (u16) casts on the line coordinates are needed: stored narrowed without reassociation)
 * build the price panel: a separator line and up to four numbers (mC80, mF50, m1AE0, m14F0);
 * `row` shifts it up (by 16 or 8 pixels per row) */
void setupGearShopPriceNumbers(u32 value1, u32 value2, u32 value3, u32 value4, u8 row) {
    s32 i;

    for (i = 0; i < 2; i++) {
        SetLineF2(&gMenuContext->m450_shop->m3BF0_line[i]);
        (gMenuContext->m450_shop->m3BF0_line + i)->r0 = 0xFF;
        (gMenuContext->m450_shop->m3BF0_line + i)->g0 = 0xFF;
        (gMenuContext->m450_shop->m3BF0_line + i)->b0 = 0xFF;
        (gMenuContext->m450_shop->m3BF0_line + i)->x0 = (u16)(gearShopTotalNumberX - (row * 16 + 8));
        (gMenuContext->m450_shop->m3BF0_line + i)->y0 = (u16)(gearShopTotalNumberY - (row * 8 - 9));
        (gMenuContext->m450_shop->m3BF0_line + i)->x1 = (u16)(gearShopTotalNumberX - (row * 16 - 0x4E));
        (gMenuContext->m450_shop->m3BF0_line + i)->y1 = (u16)(gearShopTotalNumberY - (row * 8 - 9));
    }
    fillStringDisplayBuffer(value1);
    gMenuContext->m450_shop->m46AD_C80Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46AD_C80Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                gMenuContext->m450_shop->mC80_number1[gMenuContext->m450_shop->m46AD_C80Length],
                gMenuContext->m308_oddOrEven, i * 8 + gearShopGoldNumberX - row * 16, gearShopGoldNumberY - row * 16, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46AC_C80OddOrEven = gMenuContext->m308_oddOrEven;
    fillStringDisplayBuffer(value2);
    gMenuContext->m450_shop->m46AF_F50Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46AF_F50Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                gMenuContext->m450_shop->mF50_number2[gMenuContext->m450_shop->m46AF_F50Length],
                gMenuContext->m308_oddOrEven, i * 8 + gearShopTotalNumberX - row * 16, gearShopTotalNumberY - row * 16, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46AE_F50OddOrEven = gMenuContext->m308_oddOrEven;
    fillStringDisplayBuffer(value3);
    gMenuContext->m450_shop->m46B1_1AE0Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46B1_1AE0Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                gMenuContext->m450_shop->m1AE0_number3[gMenuContext->m450_shop->m46B1_1AE0Length],
                gMenuContext->m308_oddOrEven, i * 8 + gearShopNewGoldNumberX - row * 16, gearShopNewGoldNumberY - row * 8, 0x1000);
        }
    }
    gMenuContext->m450_shop->m46B0_1AE0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m46B8_14F0Length = 0;
    gMenuContext->m450_shop->m46B9_14F0Enabled = 0;
    if (row) {
        fillStringDisplayBuffer(value4);
        for (i = 0; i < 9; i++) {
            if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
                gMenuContext->m450_shop->m46B8_14F0Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                    gMenuContext->m450_shop->m14F0_number4[gMenuContext->m450_shop->m46B8_14F0Length],
                    gMenuContext->m308_oddOrEven, i * 8 + gearShopTotalNumberX - row * 16, gearShopTotalNumberY - row * 8, 0x1000);
            }
        }
        gMenuContext->m450_shop->m46B7_14F0OddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m450_shop->m46B9_14F0Enabled = 1;
    }
    gMenuContext->m450_shop->m46B2_numbersEnabled = 1;
}

/* print a number (m31C digit buffer) into m1220_number, at one of two positions */
void setupShopTotalNumber(u32 value, u8 alt) {
    s32 x;
    s32 y;
    s32 i;

    x = 0x68;
    y = 0x54;
    if (alt) {
        x = 0x50;
        y = 0x64;
    }
    fillStringDisplayBuffer(value);
    gMenuContext->m450_shop->m46B4_1220Length = 0;
    for (i = 0; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46B4_1220Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                gMenuContext->m450_shop->m1220_number[gMenuContext->m450_shop->m46B4_1220Length],
                gMenuContext->m308_oddOrEven, x + 3, y, 0x1000);
        }
        x += 8;
    }
    gMenuContext->m450_shop->m46B3_1220OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m5A_drawShop[1] = 2;
}

/* print a number (gold) plus glyph 0x10 into m17C0 at y 0xAE */
void setupGearShopGoldNumber(u32 value) {
    s32 x;
    s32 i;

    fillStringDisplayBuffer(value);
    gMenuContext->m450_shop->m46BB_17C0Length = 0;
    for (i = 0, x = 0xAA; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0xFF) {
            gMenuContext->m450_shop->m46BB_17C0Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i],
                gMenuContext->m450_shop->m17C0_polys[gMenuContext->m450_shop->m46BB_17C0Length],
                gMenuContext->m308_oddOrEven, x, 0xAE, 0x1000);
        }
        x += 8;
    }
    gMenuContext->m450_shop->m46BB_17C0Length += setupStringInPolyFT4(
        gMenuContext->m2DC_font, 0x10, gMenuContext->m450_shop->m17C0_polys[gMenuContext->m450_shop->m46BB_17C0Length],
        gMenuContext->m308_oddOrEven, 0xF2, 0xAE, 0x1000);
    gMenuContext->m450_shop->m46BA_17C0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m63_drawGearShop[1] = 1;
}

/* hide the shop's labels and numbers; with `close`, also close boxes 2/3 and the arrows */
void clearShopScreen(u8 close) {
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
    gMenuContext->m450_shop->m46B6_4530Enabled = 0;
    if (close) {
        processLoadSaveMenuSub7Sub0(2);
        processLoadSaveMenuSub7Sub0(3);
        freeScrollBarThumb();
        freeMenuPointerGlyph(0);
    }
}

/* mask (getMenuBitMask32 bits) of the gears of the listed party members that have `item`
 * equipped in `category` (0 armor, 1 frame, 2 engine, 3 accessory, 4 weapon / slot item) */
u16 getGearsEquippingPart(u8 item, u8 category) {
    u32 mask;
    s32 i;
    s32 j;
    u8 found;

    mask = 0;
    if (item) {
        for (i = 0; i < 16; i++) {
            found = 0;
            if (gMenuContext->m30C_characterAvailable[i]) {
                switch (category) {
                case 1:
                    if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].m2_frame == item) {
                        found = 1;
                    }
                    break;
                case 0:
                    if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].m8_armor == item) {
                        found = 1;
                    }
                    break;
                case 2:
                    if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].m3_engine == item) {
                        found = 1;
                    }
                    break;
                case 4:
                    for (j = 0; j < 4; j++) {
                        if (item < 0x32) {
                            if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].mC_weapon[j] == item) {
                                found = 1;
                                break;
                            }
                        } else {
                            if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].m4_slotItem[j] == item) {
                                found = 1;
                                break;
                            }
                        }
                    }
                    break;
                case 3:
                    for (j = 0; j < 3; j++) {
                        if (gameState.m978_gears[gameState.m26C_party[i].mA0_gearNum].m9_accessorySlots[j] == item) {
                            found = 1;
                            break;
                        }
                    }
                    break;
                }
            }
            if (found) {
                mask |= getMenuBitMask32(gameState.m26C_party[i].mA0_gearNum);
            }
        }
    }
    return mask;
}
