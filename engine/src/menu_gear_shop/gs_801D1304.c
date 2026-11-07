/* The gear shop's own code, second file. It calls the menu library and
 * getGearsEquippingPart without prototypes (unmasked arguments; getGearsEquippingPart's u16
 * result used as an int), and its .rodata starts 8-aligned at 0x801C5050,
 * after 4 bytes of linker fill. gcc-2.6.3-psx, ASPSX 2.34, -G0. */
#include "common.h"
#define MENU_FAMILY_OVERLAY
#include "menu/menu.h"
#include "menu/shop.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pollhost.h"
#include "main/game_state.h"
#include "pc_gameplay.h"
#include "main/dialog.h"

extern u16 gearShopMenu2Bitmask[];
extern u32 gearShopMenu2Bitmask32[];
extern MenuContext* gMenuContext;
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
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
extern sLoadedMechas* loadedMechas[10]; /* field_mecha loadedMechas */
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
void initMenuContext_1DE0();
void initMemoryCardPolyVerts();

/* defined in earlier files */
void setShopDigitsColor(s32 count, POLY_FT4* polys, u8 color);

/* Sell screen detail: the selected item's name and sell price (half its price);
 * m469C marks the listed party members whose gear can equip it, glyph 0xE the
 * ones that have it equipped (getGearsEquippingPart). Returns the sell price. */
s32 showGearShopSellItemInfo(u8 itemId, u8 kind) {
    RECT rect;
    u8 unused[0x10]; /* only reserves the 0x10 stack bytes retail has between rect and codes */
    u8 codes[0xE];
    u8 text[0x10];
    s32 powers[5];
    u16* image;
    u32 mask;
    u32 mask2;
    s32 price;
    s32 value;
    s32 digit;
    s32 i;
    s32 j;
    s32 n;
    u8 started;
    u8 gearId;

    mask = 0;
    powers[0] = 1;
    powers[1] = 10;
    powers[2] = 100;
    powers[3] = 1000;
    powers[4] = 10000;
    image = allocateHeap(0x618, 0);
    bzero(image, 0x618);
    bzero(codes, 0xE);
    switch (kind) {
    case 4:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth = renderString(getGearWeaponName(itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m18_gearWeapons[itemId].m8_price >> 1;
        mask = gMenuContext->m330_itemData->m18_gearWeapons[itemId].m4_gearMask;
        value = price;
        break;
    case 3:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth = renderString(getGearAccessoryName(itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m14_gearAccessories[itemId].m4_price >> 1;
        mask = gMenuContext->m330_itemData->m14_gearAccessories[itemId].m0_gearMask;
        value = price;
        break;
    }
    mask2 = getGearsEquippingPart(itemId, kind);
    started = 0;
    for (n = 4, j = 0; n > 0; j++, n--) {
        digit = value / powers[n];
        if (digit != 0 || started) {
            codes[j * 2] = digit + 0x10;
            started = 1;
            value -= digit * powers[n];
        } else {
            codes[j * 2] = 0xC3;
        }
    }
    codes[j * 2] = value % 10 + 0x10;
    decodeCharacterCodes((u16*)codes, text, 5);
    gMenuContext->m450_shop->m44B0_label.m7E_stringWidth = renderString(text, image, 0x39, 1);
    rect.x = 0x140;
    rect.y = 0x4E;
    rect.w = 0x3C;
    rect.h = 0xD;
    LoadImage(&rect, image);
    DrawSync(0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m4430_label, 0, 0, 0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m44B0_label, 0, 0, 0);
    gMenuContext->m450_shop->m44B0_label.m0_polys[gMenuContext->m308_oddOrEven].clut = textSpriteMode1;
    setupMenuPolyFT4(&gMenuContext->m450_shop->m4430_label.m0_polys[gMenuContext->m308_oddOrEven], 0x2C, 0x12, 0,
                  0x4E, gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    setupMenuPolyFT4(&gMenuContext->m450_shop->m44B0_label.m0_polys[gMenuContext->m308_oddOrEven], 0x98, 0x12, 0,
                  0x4E, gMenuContext->m450_shop->m44B0_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m4430_label.m50_vertex, 0x2C, 0x12,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m44B0_label.m50_vertex, 0x98, 0x12,
                  gMenuContext->m450_shop->m44B0_label.m7E_stringWidth, 0xD);
    gMenuContext->m450_shop->m4430_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m44B0_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    freeHeap(image);
    if (itemId) {
        gMenuContext->m450_shop->m46A7_4430Enabled = 1;
        gMenuContext->m450_shop->m46B5_44B0Enabled = 1;
    } else {
        gMenuContext->m450_shop->m46A7_4430Enabled = 0;
        gMenuContext->m450_shop->m46B5_44B0Enabled = 0;
    }
    gMenuContext->m450_shop->m46A9_2D0Length = 0;
    for (i = 0, n = 0; i < 16; i++) {
        if (gMenuContext->m30C_characterAvailable[i]) {
            gearId = gameState.m26C_party[i].mA0_gearNum;
            if (menu2CheckBitmask32(mask, gearId)) {
                gMenuContext->m450_shop->m469C_lineEnabled[n] = 1;
            } else {
                gMenuContext->m450_shop->m469C_lineEnabled[n] = 0;
            }
            if (menu2CheckBitmask32(mask2, gearId)) {
                gMenuContext->m450_shop->m46A9_2D0Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, 0xE, gMenuContext->m450_shop->m2D0_polys[gMenuContext->m450_shop->m46A9_2D0Length],
                    gMenuContext->m308_oddOrEven, gearShopMemberColumnX[n] + 0xE, 0xB4, 0x1000);
            }
            n++;
        }
    }
    gMenuContext->m450_shop->m46A8_2D0OddOrEven = gMenuContext->m308_oddOrEven;
    return price;
}

/* Sell list: eight rows from start (name, owned count, "x amount") */
void setupGearShopSellListRows(s32 start, u8* ids, u8* kinds, u8* amounts, u8* counts) {
    RECT rect;
    u8 codes[0xE];
    u8 text[0x10];
    s32 powers[5];
    u16* image;
    s32 i;
    s32 k;
    s32 j;
    s32 value;
    s32 digit;
    u8 started;
    u8* name;
    u8* amount;
    u8 tens;

    powers[0] = 1;
    powers[1] = 10;
    powers[2] = 100;
    powers[3] = 1000;
    powers[4] = 10000;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 8; i++) {
        bzero(codes, 0xE);
        gMenuContext->m450_shop->m468C_rowLength[i] = 0;
        if (ids[start + i] != 0) {
            value = counts[start + i];
            switch (kinds[start + i]) {
            case 4:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getGearWeaponName(ids[start + i]), image, 0x24, 0);
                break;
            case 3:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getGearAccessoryName(ids[start + i]), image, 0x24, 0);
                break;
            }
            started = 0;
            for (k = 4, j = 0; k > 0; j++, k--) {
                digit = value / powers[k];
                if (digit != 0 || started) {
                    codes[j * 2] = digit + 0x10;
                    started = 1;
                    value -= digit * powers[k];
                } else {
                    codes[j * 2] = 0xC3;
                }
            }
            codes[j * 2] = value % 10 + 0x10;
            decodeCharacterCodes((u16*)codes, text, 5);
            gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth = renderString(text, image, 0x24, 1);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m3C30_labels[i], i, 0x80, 0x81);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m3C30_labels[i].m50_vertex, 0x24, i * 13 + 0x32,
                          gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth, 0xD);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            DrawSync(0);
            initMenuContext_1DE0(&gMenuContext->m450_shop->m4030_labels[i], i, 0x80, 0x82);
            initMemoryCardPolyVerts(gMenuContext->m450_shop->m4030_labels[i].m50_vertex, 0x8C, i * 13 + 0x32,
                          gMenuContext->m450_shop->m4030_labels[i].m7E_stringWidth, 0xD);
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 1;
            amount = &amounts[start + i];
            if (*amount != 0) {
                gMenuContext->m450_shop->m468C_rowLength[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE5, gMenuContext->m450_shop->m1DB0_rows[i],
                                  gMenuContext->m308_oddOrEven, 0xB4, i * 13 + 0x36, 0x1000);
                tens = *amount / 10;
                if (tens) {
                    gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                        gMenuContext->m2DC_font, tens,
                        gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                        gMenuContext->m308_oddOrEven, 0xBC, i * 13 + 0x36, 0x1000);
                }
                gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, (u8)(*amount % 10),
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

void completeShopSale(u32 gold, u8* boughtIds, u8* boughtCounts, s32 count, u8* ids, u8* counts, u8* arg6, u8 update,
                   u8 arg8) {
    s32 i;
    s32 j;

    playMenuSoundEffect(0xD1);
    gameState.m1924_gold = gold;
    if (gold > 9999999) {
        gameState.m1924_gold = 9999999;
    }
    if (update) {
        for (i = 0; i < count; i++) {
            if (boughtIds[i] != 0) {
                for (j = 0; j < count; j++) {
                    if (boughtIds[i] == ids[j]) {
                        counts[j] -= boughtCounts[i];
                        if (counts[j] == 0) {
                            ids[j] = 0;
                        }
                    }
                }
            }
        }
    }
}

/* Gear shop sell screen for one inventory (count entries of ids/counts) */
void runGearShopSellMenu(s32 count, u8* ids, u8* counts, u8 category, u8 arg4, u8* arg5, u8 arg6) {
    u8 running = 1;
    u8 redraw = 1;
    u8 dirty = 1;
    s32 row = 0;
    s32 prevRow = 0xFF;
    s32 scroll = 0;
    s32 prevScroll = 0xFF;
    u8 listIds[count];
    u8 listKinds[count];
    u8 listAmounts[count];
    u8 listValid[count];
    u8 listCounts[count];
    s32 gold0 = gameState.m1924_gold;
    s32 gold;
    s32 total = 0;
    s32 price;
    s32 numEntries;
    s32 i;
    s32 j;
    s32 idx;

    gold = gold0;
    for (i = 0; i < count; i++) {
        listCounts[i] = 0;
        listValid[i] = 0;
        listAmounts[i] = 0;
        listIds[i] = 0;
        listKinds[i] = category;
    }
    j = 0;
    for (i = 0; i < count; i++) {
        if (ids[i] != 0 && counts[i] != 0) {
            listIds[j] = ids[i];
            listCounts[j] = counts[i];
            listValid[j] = 1;
            j++;
        }
    }
    numEntries = j;
    allocMenuPointerGlyph(0);
    while (running) {
        menuDraw();
        if (scroll != prevScroll || dirty) {
            setupGearShopSellListRows(scroll, listIds, listKinds, listAmounts, listCounts);
            setupScrollBarThumb(0xC, 0x32, 0x3C, numEntries, scroll);
        }
        if (row != prevRow || scroll != prevScroll) {
            prevRow = row;
            price = showGearShopSellItemInfo(listIds[scroll + row], listKinds[scroll + row]);
            gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 1;
            prevScroll = scroll;
        }
        updateMenuPointerGlyph(row, scroll, 0, 0);
        if (redraw) {
            iniMenuContext364And380(2, 0xC, 0x2A, 0xC4, 0x74, 0, 1, 4, 1);
            iniMenuContext364And380(3, 0x20, 0xE, 0xFC, 0x14, 0, 1, 4, 0);
            showShopPartyMembers();
            setupShopSellTitle();
            redraw = 0;
        }
        if (dirty) {
            setupGearShopPriceNumbers(gold0, total, gold, 0, 0);
            dirty = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (total != 0) {
                playMenuSoundEffect(2);
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                running = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                setupShopTotalNumber(total, 0);
                if (askYesNoQuestion(0x95, 0xFF, 1)) {
                    completeShopSale(gold, listIds, listAmounts, count, ids, counts, listKinds, arg4, arg6);
                } else {
                    running = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = running;
                    gMenuContext->m33C_drawFlags->m49_draw43C = running;
                    prevScroll = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = running;
                }
                disableDraw428();
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 5:
            running = 0;
            if (total != 0) {
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                if (askYesNoQuestion(0x92, 0xFF, 1) == 0) {
                    running = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = running;
                    gMenuContext->m33C_drawFlags->m49_draw43C = running;
                    prevScroll = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = running;
                }
                disableDraw428();
            }
            break;
        case 1:
            row++;
            if (row >= 8) {
                row = 7;
                scroll++;
                if (numEntries - 8 < scroll) {
                    scroll--;
                }
            }
            break;
        case 3:
            row--;
            if (row < 0) {
                row = 0;
                scroll--;
                if (scroll < 0) {
                    scroll = 0;
                }
            }
            break;
        case 0:
            idx = scroll + row;
            if (listValid[idx] && listCounts[idx] - 1 >= 0) {
                dirty = 1;
                listAmounts[idx]++;
                total += price;
                listCounts[idx]--;
                gold += price;
            }
            break;
        case 2:
            idx = scroll + row;
            if (listValid[idx] && listAmounts[idx] - 1 >= 0) {
                listCounts[idx]++;
                listAmounts[idx]--;
                total -= price;
                gold -= price;
                dirty = 1;
            }
            break;
        }
    }
}

/* Sell accessories (inventory 4, kind 3) */
void runGearShopSellAccessories(void) {
    runGearShopSellMenu(150, gameState.m221A_inventory4Ids, gameState.m2184_inventory4Counts, 3, 1,
                  gameState.m2184_inventory4Counts, 0);
}

/* Sell weapons (inventory 3, kind 4) */
void runGearShopSellWeapons(void) {
    runGearShopSellMenu(100, gameState.m2120_inventory3Ids, gameState.m20BC_inventory3Counts, 4, 1,
                  gameState.m20BC_inventory3Counts, 0);
}

/* Sell menu entry: hide the cursor and labels, run the sell screen of submenu
 * entry m338 + arg0 * 3 (3 accessories, 4 weapons), then show them again */
void executeGearShopSellSelection(u8 arg0) {
    u8 show = 1;

    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
    countField33C_C(4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    switch (gMenuContext->m338_currentSelectedSubMenuEntry + arg0 * 3) {
    case 3:
        runGearShopSellAccessories();
        break;
    case 4:
        runGearShopSellWeapons();
        break;
    }
    clearShopScreen(show);
    gMenuContext->m33C_drawFlags->mA_354Enabled = 1;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    j_setupMenuContext4E0(4, gMenuContext->m6E0_mainMenuLabels, gearShopSubMenuLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
}

/* Owned-part label (m4530): the name of the selected gear's current armor/frame/engine */
void setupGearShopOwnedPartLabel(u8 kind) {
    RECT rect;
    u16* image;

    image = ALLOC_TEXT_IMAGE(0x3F6);
    switch (kind) {
    case 0:
        gMenuContext->m450_shop->m4530_label.m7E_stringWidth = renderString(
            getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[6], gameState.m978_gears[gearShopGearId].m8_armor),
            image, 0x24, 0);
        break;
    case 1:
        gMenuContext->m450_shop->m4530_label.m7E_stringWidth = renderString(
            getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[7], gameState.m978_gears[gearShopGearId].m2_frame),
            image, 0x24, 0);
        break;
    case 2:
        gMenuContext->m450_shop->m4530_label.m7E_stringWidth = renderString(
            getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[8], gameState.m978_gears[gearShopGearId].m3_engine),
            image, 0x24, 0);
        break;
    }
    rect.x = 0x198;
    rect.y = 0xB4;
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, image);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m4530_label, 9, 0x80, 0x81);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m4530_label.m50_vertex, 0xD4, 0x8E,
                  gMenuContext->m450_shop->m4530_label.m7E_stringWidth, 0xD);
    DrawSync(0);
    gMenuContext->m450_shop->m4530_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m450_shop->m46B6_4530Enabled = 1;
    freeHeap(image);
}

/* Buy list: eight rows from start (name, price, "x quantity"); flags[i] gets
 * 0x80 when the row is affordable and fits the selected gear. Returns the
 * trade-in value (half price) of the selected gear's current part. */
s32 setupGearShopBuyListRows(s32 start, s32 gold, u8* flags) {
    RECT rect;
    u8 codes[0xE];
    u8 text[0x10];
    s32 powers[5];
    s32 i;
    s32 j;
    s32 k;
    s32 value;
    s32 price;
    s32 digit;
    u32 mask;
    s32 tradeIn;
    MenuGearArmor* armor;
    MenuGearFrame* frame;
    MenuGearEngine* engine;
    u16* image;
    u8 started;
    u8 tens;

    powers[0] = 1;
    powers[1] = 10;
    powers[2] = 100;
    powers[3] = 1000;
    powers[4] = 10000;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 8; i++) {
        flags[i] = 0;
        bzero(codes, 0xE);
        gMenuContext->m450_shop->m468C_rowLength[i] = 0;
        if (gMenuContext->m1E30_shopItemIds[start + i] != 0) {
            switch (gMenuContext->m1E60_shopItemKinds[start + i]) {
            case 0:
                armor = &gMenuContext->m330_itemData->m10_gearArmors[gameState.m978_gears[gearShopGearId].m8_armor];
                tradeIn = armor->m4_price >> 1;
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth = renderString(
                    getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[6], gMenuContext->m1E30_shopItemIds[start + i]),
                    image, 0x24, 0);
                armor = &gMenuContext->m330_itemData->m10_gearArmors[gMenuContext->m1E30_shopItemIds[start + i]];
                value = armor->m4_price;
                mask = armor->m0_gearMask;
                price = value;
                break;
            case 1:
                frame = &gMenuContext->m330_itemData->m8_gearFrames[gameState.m978_gears[gearShopGearId].m2_frame];
                tradeIn = frame->mA_price >> 1;
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth = renderString(
                    getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[7], gMenuContext->m1E30_shopItemIds[start + i]),
                    image, 0x24, 0);
                frame = &gMenuContext->m330_itemData->m8_gearFrames[gMenuContext->m1E30_shopItemIds[start + i]];
                value = frame->mA_price;
                mask = frame->m0_gearMask;
                price = value;
                break;
            case 2:
                engine = &gMenuContext->m330_itemData->mC_gearEngines[gameState.m978_gears[gearShopGearId].m3_engine];
                tradeIn = engine->mA_price >> 1;
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth = renderString(
                    getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[8], gMenuContext->m1E30_shopItemIds[start + i]),
                    image, 0x24, 0);
                engine = &gMenuContext->m330_itemData->mC_gearEngines[gMenuContext->m1E30_shopItemIds[start + i]];
                value = engine->mA_price;
                mask = engine->m0_gearMask;
                price = value;
                break;
            case 4:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getGearWeaponName(gMenuContext->m1E30_shopItemIds[start + i]), image, 0x24, 0);
                value = gMenuContext->m330_itemData->m18_gearWeapons[gMenuContext->m1E30_shopItemIds[start + i]].m8_price;
                mask = -1;
                price = value;
                break;
            case 3:
                gMenuContext->m450_shop->m3C30_labels[i].m7E_stringWidth =
                    renderString(getGearAccessoryName(gMenuContext->m1E30_shopItemIds[start + i]), image, 0x24, 0);
                value = gMenuContext->m330_itemData->m14_gearAccessories[gMenuContext->m1E30_shopItemIds[start + i]].m4_price;
                mask = -1;
                price = value;
                break;
            }
            if (gold >= price) {
                if (menu2CheckBitmask32(mask, gearShopGearId)) {
                    flags[i] = 0x80;
                }
            }
            started = 0;
            for (k = 4, j = 0; k > 0; j++, k--) {
                digit = value / powers[k];
                if (digit != 0 || started) {
                    codes[j * 2] = digit + 0x10;
                    started = 1;
                    value -= digit * powers[k];
                } else {
                    codes[j * 2] = 0xC3;
                }
            }
            codes[j * 2] = value % 10 + 0x10;
            decodeCharacterCodes((u16*)codes, text, 5);
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
            gMenuContext->m450_shop->m3C30_labels[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 1;
            if (gMenuContext->m450_shop->m4654_buyQuantities[start + i] != 0) {
                gMenuContext->m450_shop->m468C_rowLength[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xF1, gMenuContext->m450_shop->m1DB0_rows[i],
                                  gMenuContext->m308_oddOrEven, 0xB4, i * 13 + 0x36, 0x1000);
                tens = gMenuContext->m450_shop->m4654_buyQuantities[start + i] / 10;
                if (tens) {
                    gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                        gMenuContext->m2DC_font, tens,
                        gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                        gMenuContext->m308_oddOrEven, 0xBC, i * 13 + 0x36, 0x1000);
                }
                gMenuContext->m450_shop->m468C_rowLength[i] += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, (u8)(gMenuContext->m450_shop->m4654_buyQuantities[start + i] % 10),
                    gMenuContext->m450_shop->m1DB0_rows[i][gMenuContext->m450_shop->m468C_rowLength[i]],
                    gMenuContext->m308_oddOrEven, 0xC4, i * 13 + 0x36, 0x1000);
                gMenuContext->m450_shop->m4694_rowOddOrEven[i] = gMenuContext->m308_oddOrEven;
            }
        } else {
            gMenuContext->m450_shop->m4684_labelEnabled[i] = 0;
        }
    }
    freeHeap(image);
    return tradeIn;
}

/* Stat change (attack, defence) if the party member's gear equipped item
 * itemId (kind 4 weapon, 3 accessory): differences and signs (1 = gain). */
void computeGearShopStatChange(s32* diffs, u8* gains, u8 itemId, u8 kind, u8 slot) {
    s16 after[2];
    s16 before[2];
    u8 backup[8];
    u8 gearId;
    u8 isNew;
    MenuGearWeapon* weapon;
    MenuGearAccessory* accessory;
    u8 min;
    s32 i;
    s32 minIdx;
    MenuGearAccessory* p;

    gearId = gameState.m26C_party[slot].mA0_gearNum;
    backup[0] = gameState.m978_gears[gearId].mC_weapon[0];
    backup[1] = gameState.m978_gears[gearId].m4_slotItem[0];
    backup[2] = gameState.m978_gears[gearId].m4_slotItem[1];
    backup[3] = gameState.m978_gears[gearId].m4_slotItem[2];
    backup[4] = gameState.m978_gears[gearId].m4_slotItem[3];
    backup[5] = gameState.m978_gears[gearId].m9_accessorySlots[0];
    backup[6] = gameState.m978_gears[gearId].m9_accessorySlots[1];
    backup[7] = gameState.m978_gears[gearId].m9_accessorySlots[2];
    switch (kind) {
    case 4:
        if (itemId < 0x32) {
            gameState.m978_gears[gearId].mC_weapon[0] = itemId;
        } else {
            weapon = &gMenuContext->m330_itemData->m18_gearWeapons[itemId];
            for (i = 0; i < 4; i++) {
                if (gMenuContext->m330_itemData->m18_gearWeapons[gameState.m978_gears[gearId].m4_slotItem[i]].mF_slotType == weapon->mF_slotType) {
                    gameState.m978_gears[gearId].m4_slotItem[i] = itemId;
                }
            }
        }
        break;
    case 3:
        isNew = 1;
        accessory = &gMenuContext->m330_itemData->m14_gearAccessories[itemId];
        for (i = 0; i < 3; i++) {
            p = &gMenuContext->m330_itemData->m14_gearAccessories[backup[5 + i]];
            if (p->m8_exclusiveGroup != 0 && p->m8_exclusiveGroup == accessory->m8_exclusiveGroup) {
                isNew = 0;
                gameState.m978_gears[gearId].m9_accessorySlots[i] = itemId;
            }
        }
        if (isNew) {
            min = 0xFF;
            for (i = 0; i < 3; i++) {
                p = &gMenuContext->m330_itemData->m14_gearAccessories[backup[5 + i]];
                if (min >= p->mD_defence) {
                    min = p->mD_defence;
                    minIdx = i;
                }
            }
            gameState.m978_gears[gearId].m9_accessorySlots[minIdx] = itemId;
        }
        break;
    }
    applyAllGearPartStats(gMenuContext->m330_itemData, gameState.m26C_party[slot].mA0_gearNum);
    computeGearStatusStats(gMenuContext->m330_itemData, gameState.m26C_party[slot].mA0_gearNum);
    after[0] = gMenuContext->m330_itemData->m9C_gearStats.m14_attack;
    after[1] = gMenuContext->m330_itemData->m9C_gearStats.m8_defence;
    before[0] = gMenuContext->m450_shop->m46E0_attack[slot];
    before[1] = gMenuContext->m450_shop->m4700_defence[slot];
    for (i = 0; i < 2; i++) {
        if (after[i] >= before[i]) {
            diffs[i] = after[i] - before[i];
            gains[i] = 0;
        } else {
            diffs[i] = before[i] - after[i];
            gains[i] = 1;
        }
    }
    gameState.m978_gears[gearId].mC_weapon[0] = backup[0];
    gameState.m978_gears[gearId].m4_slotItem[0] = backup[1];
    gameState.m978_gears[gearId].m4_slotItem[1] = backup[2];
    gameState.m978_gears[gearId].m4_slotItem[2] = backup[3];
    gameState.m978_gears[gearId].m4_slotItem[3] = backup[4];
    gameState.m978_gears[gearId].m9_accessorySlots[0] = backup[5];
    gameState.m978_gears[gearId].m9_accessorySlots[1] = backup[6];
    gameState.m978_gears[gearId].m9_accessorySlots[2] = backup[7];
    applyAllGearPartStats(gMenuContext->m330_itemData, gameState.m26C_party[slot].mA0_gearNum);
}

/* values[i] for the first keys[i] == key (0 if none): an item's inventory count */
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

/* Owned-count label (m45B0): how many of the item the party has (inventory 4
 * for kind 3, inventory 3 for kind 4), also kept in gearShopOwnedCount */
void setupGearShopOwnedCount(u8 kind, u8 itemId) {
    RECT rect;
    u8 codes[4];
    u8 text[8];
    u8* ids;
    u8* counts;
    s32 n;
    u8 found;
    u16 count;
    u16* image;

    found = 0;
    switch (kind) {
    case 4:
        ids = gameState.m2120_inventory3Ids;
        counts = gameState.m20BC_inventory3Counts;
        n = 100;
        found = 1;
        break;
    case 3:
        ids = gameState.m221A_inventory4Ids;
        counts = gameState.m2184_inventory4Counts;
        n = 150;
        found = 1;
        break;
    }
    if (found) {
        count = findInventoryCount(ids, counts, n, itemId);
        gearShopOwnedCount = count;
        image = ALLOC_TEXT_IMAGE(0x3F6);
        codes[1] = 0;
        codes[3] = 0;
        codes[0] = (u16)(count / 10) ? count / 10 + 0x10 : 0xC3;
        codes[2] = count % 10 + 0x10;
        decodeCharacterCodes((u16*)codes, text, 2);
        gMenuContext->m450_shop->m45B0_label.m7E_stringWidth = renderString(text, image, 0x24, 1);
        rect.x = 0x198;
        rect.y = 0xB4;
        rect.w = 0x28;
        rect.h = 0xD;
        LoadImage(&rect, image);
        DrawSync(0);
        initMenuContext_1DE0(&gMenuContext->m450_shop->m45B0_label, 9, 0x80, 0x82);
        initMemoryCardPolyVerts(gMenuContext->m450_shop->m45B0_label.m50_vertex, 0xF8, 0x8E,
                      gMenuContext->m450_shop->m45B0_label.m7E_stringWidth, 0xD);
        gMenuContext->m450_shop->m45B0_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m450_shop->m4785_45B0Enabled = 1;
        freeHeap(image);
    }
}

/* Buy screen: the selected entry's name and which party members' gears can
 * equip it, with the attack/defence change for parts; returns its price */
s32 showGearShopBuyItemInfo(s32 row, s32 scroll, u8* flags) {
    RECT rect;
    s32 diffs[2];
    u8 gains[2];
    s32 price;
    u32 mask2;
    u8 itemId;
    u8 kind;
    u16* image;
    u32 mask;
    s32 i;
    s32 k;
    s32 n;
    u8 gearId;
    u8 c;

    itemId = gMenuContext->m1E30_shopItemIds[scroll + row];
    kind = gMenuContext->m1E60_shopItemKinds[scroll + row];
    image = allocateHeap(0x618, 0);
    bzero(image, 0x618);
    mask = 0;
    switch (kind) {
    case 0:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[3], itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m10_gearArmors[itemId].m4_price;
        mask = gMenuContext->m330_itemData->m10_gearArmors[itemId].m0_gearMask;
        break;
    case 1:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[4], itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m8_gearFrames[itemId].mA_price;
        mask = gMenuContext->m330_itemData->m8_gearFrames[itemId].m0_gearMask;
        break;
    case 2:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[5], itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->mC_gearEngines[itemId].mA_price;
        mask = gMenuContext->m330_itemData->mC_gearEngines[itemId].m0_gearMask;
        break;
    case 4:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[1], itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m18_gearWeapons[itemId].m8_price;
        mask = gMenuContext->m330_itemData->m18_gearWeapons[itemId].m4_gearMask;
        break;
    case 3:
        gMenuContext->m450_shop->m4430_label.m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m450_shop->m4630_textBundles[2], itemId), image, 0x39, 0);
        price = gMenuContext->m330_itemData->m14_gearAccessories[itemId].m4_price;
        mask = gMenuContext->m330_itemData->m14_gearAccessories[itemId].m0_gearMask;
        break;
    }
    mask2 = getGearsEquippingPart(itemId, kind);
    rect.x = 0x140;
    rect.y = 0x4E;
    rect.w = 0x3C;
    rect.h = 0xD;
    LoadImage(&rect, image);
    DrawSync(0);
    initMenuContext_1DE0(&gMenuContext->m450_shop->m4430_label, 0, 0, 0);
    setupMenuPolyFT4(&gMenuContext->m450_shop->m4430_label.m0_polys[gMenuContext->m308_oddOrEven], 0x2C, 0x12, 0,
                  0x4E, gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m450_shop->m4430_label.m50_vertex, 0x2C, 0x12,
                  gMenuContext->m450_shop->m4430_label.m7E_stringWidth, 0xD);
    gMenuContext->m450_shop->m4430_label.m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    freeHeap(image);
    if (itemId) {
        gMenuContext->m450_shop->m46A7_4430Enabled = 1;
    } else {
        gMenuContext->m450_shop->m46A7_4430Enabled = 0;
    }
    gMenuContext->m450_shop->m46A9_2D0Length = 0;
    for (i = 0, n = 0; i < 16; i++) {
        if (gMenuContext->m30C_characterAvailable[i]) {
            gearId = gameState.m26C_party[i].mA0_gearNum;
            if (menu2CheckBitmask32(mask, gearId)) {
                gMenuContext->m450_shop->m469C_lineEnabled[n] = 1;
            } else {
                gMenuContext->m450_shop->m469C_lineEnabled[n] = 0;
            }
            if (menu2CheckBitmask32(mask2, gearId)) {
                gMenuContext->m450_shop->m46A9_2D0Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, 0xE, gMenuContext->m450_shop->m2D0_polys[gMenuContext->m450_shop->m46A9_2D0Length],
                    gMenuContext->m308_oddOrEven, gearShopMemberColumnX[n] + 0xE, 0xB4, 0x1000);
            }
            gMenuContext->m450_shop->m46BC_27B0Length[n] = 0;
            gMenuContext->m450_shop->m46C5_3020Length[n] = 0;
            if ((kind == 3 || kind == 4) && gMenuContext->m450_shop->m469C_lineEnabled[n]) {
                diffs[1] = 0;
                diffs[0] = 0;
                computeGearShopStatChange(diffs, gains, itemId, kind, i);
                if (diffs[0]) {
                    fillStringDisplayBuffer(diffs[0]);
                    for (k = 0; k < 3; k++) {
                        c = gMenuContext->m31C_stringDisplayBuffer[k + 6];
                        if (c != 0xFF) {
                            gMenuContext->m450_shop->m46BC_27B0Length[n] += setupStringInPolyFT4(
                                gMenuContext->m2DC_font, c,
                                gMenuContext->m450_shop->m27B0_attackChange[n][gMenuContext->m450_shop->m46BC_27B0Length[n]],
                                gMenuContext->m308_oddOrEven, k * 8 + 0x49 + n * 0x1A, 0xBE, 0x1000);
                        }
                    }
                    setShopDigitsColor(gMenuContext->m450_shop->m46BC_27B0Length[n],
                                  gMenuContext->m450_shop->m27B0_attackChange[n][0], gains[0]);
                    gMenuContext->m450_shop->m46CE_27B0OddOrEven[n] = gMenuContext->m308_oddOrEven;
                }
                if (diffs[1]) {
                    fillStringDisplayBuffer(diffs[1]);
                    for (k = 0; k < 3; k++) {
                        c = gMenuContext->m31C_stringDisplayBuffer[k + 6];
                        if (c != 0xFF) {
                            gMenuContext->m450_shop->m46C5_3020Length[n] += setupStringInPolyFT4(
                                gMenuContext->m2DC_font, c,
                                gMenuContext->m450_shop->m3020_defenceChange[n][gMenuContext->m450_shop->m46C5_3020Length[n]],
                                gMenuContext->m308_oddOrEven, k * 8 + 0x49 + n * 0x1A, 0xC6, 0x1000);
                        }
                    }
                    setShopDigitsColor(gMenuContext->m450_shop->m46C5_3020Length[n],
                                  gMenuContext->m450_shop->m3020_defenceChange[n][0], gains[1]);
                    gMenuContext->m450_shop->m46D7_3020OddOrEven[n] = gMenuContext->m308_oddOrEven;
                }
            }
            n++;
        }
    }
    gMenuContext->m450_shop->m46A8_2D0OddOrEven = gMenuContext->m308_oddOrEven;
    setupGearShopOwnedCount(kind, itemId);
    return price;
}

/* Buy: set the gold (capped at 9999999) and apply the purchases: parts go on
 * the selected gear, items into inventory 3/4 (counts capped at 99) */
void completeGearShopPurchase(u32 gold) {
    s32 i;
    s32 j;
    u8 isNew;

    playMenuSoundEffect(0xD1);
    gameState.m1924_gold = gold;
    if (gold > 9999999) {
        gameState.m1924_gold = 9999999;
    }
    for (i = 0; i < 0x30; i++) {
        if (gMenuContext->m1E30_shopItemIds[i] == 0) {
            continue;
        }
        if (gMenuContext->m450_shop->m4654_buyQuantities[i] == 0) {
            continue;
        }
        switch (gMenuContext->m1E60_shopItemKinds[i]) {
        case 0:
            gameState.m978_gears[gearShopGearId].m8_armor = gMenuContext->m1E30_shopItemIds[i];
            break;
        case 1:
            gameState.m978_gears[gearShopGearId].m2_frame = gMenuContext->m1E30_shopItemIds[i];
            break;
        case 2:
            gameState.m978_gears[gearShopGearId].m3_engine = gMenuContext->m1E30_shopItemIds[i];
            break;
        case 4:
            isNew = 1;
            for (j = 0; j < 100; j++) {
                if (gameState.m2120_inventory3Ids[j] == gMenuContext->m1E30_shopItemIds[i]) {
                    isNew = 0;
                    if ((gameState.m20BC_inventory3Counts[j] += gMenuContext->m450_shop->m4654_buyQuantities[i]) >= 100) {
                        gameState.m20BC_inventory3Counts[j] = 99;
                    }
                }
            }
            if (isNew) {
                for (j = 0; j < 100; j++) {
                    if (gameState.m2120_inventory3Ids[j] == 0) {
                        gameState.m2120_inventory3Ids[j] = gMenuContext->m1E30_shopItemIds[i];
                        gameState.m20BC_inventory3Counts[j] = gMenuContext->m450_shop->m4654_buyQuantities[i];
                        break;
                    }
                }
            }
            break;
        case 3:
            isNew = 1;
            for (j = 0; j < 150; j++) {
                if (gameState.m221A_inventory4Ids[j] == gMenuContext->m1E30_shopItemIds[i]) {
                    isNew = 0;
                    if ((gameState.m2184_inventory4Counts[j] += gMenuContext->m450_shop->m4654_buyQuantities[i]) >= 100) {
                        gameState.m2184_inventory4Counts[j] = 99;
                    }
                }
            }
            if (isNew) {
                for (j = 0; j < 150; j++) {
                    if (gameState.m221A_inventory4Ids[j] == 0) {
                        gameState.m221A_inventory4Ids[j] = gMenuContext->m1E30_shopItemIds[i];
                        gameState.m2184_inventory4Counts[j] = gMenuContext->m450_shop->m4654_buyQuantities[i];
                        break;
                    }
                }
            }
            break;
        }
    }
}

/* Is list entry idx a newer part than the selected gear's current one (the
 * equipped id below the entry's)? 1 for items (kinds 3/4). */
u8 isNewerGearPart(s32 idx) {
    u8 ret = 1;

    switch (gMenuContext->m1E60_shopItemKinds[idx]) {
    case 0:
        if (gameState.m978_gears[gearShopGearId].m8_armor >= gMenuContext->m1E30_shopItemIds[idx]) {
            ret = 0;
        }
        break;
    case 1:
        if (gameState.m978_gears[gearShopGearId].m2_frame >= gMenuContext->m1E30_shopItemIds[idx]) {
            ret = 0;
        }
        break;
    case 2:
        if (gameState.m978_gears[gearShopGearId].m3_engine >= gMenuContext->m1E30_shopItemIds[idx]) {
            ret = 0;
        }
        break;
    }
    return ret;
}

/* Gear shop buy screen: group 0 = gear parts (armor/frame/engine, one at a
 * time with trade-in), group 1 = items (quantities) */
s32 runGearShopBuyMenu(u8 group, u8 isPart) {
    u8 flags[8];
    u8 running = 1;
    u8 redraw = 1;
    u8 dirty = 1;
    s32 row = 0;
    s32 prevRow = 0xFF;
    s32 scroll = 0;
    s32 prevScroll = 0xFF;
    s32 total = 0;
    s32 gold0 = gameState.m1924_gold;
    s32 goldStart;
    s32 tradeInNew;
    s32 tradeIn = 0;
    u8 msg = 0x8F;
    u8 arg = 0xFF;
    s32 boxX = 0xE0;
    s32 boxW = 0x40;
    s32 numEntries;
    s32 gold;
    s32 price;
    s32 i;

    gold = gold0;
    goldStart = gold;
    for (i = 0; i < 11; i++) {
        if (gameState.m26C_party[i].mA0_gearNum != 0xFF) {
            applyAllGearPartStats(gMenuContext->m330_itemData, gameState.m26C_party[i].mA0_gearNum);
            computeGearStatusStats(gMenuContext->m330_itemData, gameState.m26C_party[i].mA0_gearNum);
            gMenuContext->m450_shop->m46E0_attack[i] = gMenuContext->m330_itemData->m9C_gearStats.m14_attack;
            gMenuContext->m450_shop->m4700_defence[i] = gMenuContext->m330_itemData->m9C_gearStats.m8_defence;
        }
    }
    bzero(gMenuContext->m1E30_shopItemIds, 0x30);
    bzero(gMenuContext->m1E60_shopItemKinds, 0x30);
    bzero(gMenuContext->m450_shop->m4654_buyQuantities, 0x30);
    for (i = 0; i < 20; i++) {
        gMenuContext->m1E30_shopItemIds[i] =
            gMenuContext->m450_shop->m4720_stock[(group * 3 + gMenuContext->m338_currentSelectedSubMenuEntry) * 20 + i];
        gMenuContext->m1E60_shopItemKinds[i] = group * 3 + gMenuContext->m338_currentSelectedSubMenuEntry;
    }
    numEntries = gearShopStockCounts[group * 3 + gMenuContext->m338_currentSelectedSubMenuEntry];
    gMenuContext->m350_mainMenu->m1192_dimmed = 1;
    allocMenuPointerGlyph(0);
    while (running) {
        menuDraw();
        if (scroll != prevScroll || dirty) {
            if (isPart) {
                tradeInNew = setupGearShopBuyListRows(scroll, goldStart, flags);
            } else {
                tradeInNew = setupGearShopBuyListRows(scroll, gold, flags);
            }
            setupScrollBarThumb(0xC, 0x32, 0x3C, numEntries, scroll);
        }
        if (row != prevRow || scroll != prevScroll) {
            price = showGearShopBuyItemInfo(row, scroll, flags);
            prevRow = row;
            prevScroll = scroll;
            gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 1;
            if (isPart) {
                boxX = 0xC8;
                boxW = 0x70;
                dirty = 1;
                gold = goldStart;
                total = 0;
                tradeIn = 0;
                if (flags[row]) {
                    total = price;
                    gold -= price;
                    tradeIn = tradeInNew;
                    gold += tradeInNew;
                }
            }
        }
        updateMenuPointerGlyph(row, scroll, 0, 0);
        if (redraw) {
            iniMenuContext364And380(2, 0xC, 0x2A, 0xC4 - isPart * 20, 0x74, 0, 1, 4, 1);
            iniMenuContext364And380(3, 0x20, 0xE, 0xFC, 0x14, 0, 1, 4, 0);
            iniMenuContext364And380(5, boxX, 0x7A, boxW, 0x24, 0, 1, 4, 0);
            j_setupMenuContext4E0(2, gMenuContext->m6E0_mainMenuLabels, gearShopBuyLabelConfig, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            updateMenuSelection2(2, gMenuContext->m6E0_mainMenuLabels, gearShopBuyLabelConfig, gearShopSubMenuLabelOffsetsX, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels, isPart, 0, 1);
            if (isPart) {
                setupGearShopOwnedPartLabel(gMenuContext->m338_currentSelectedSubMenuEntry + group * 3);
            }
            showShopPartyMembers();
            setupGearShopTitle(isPart);
            redraw = 0;
        }
        if (dirty) {
            setupGearShopPriceNumbers(gold0, total, gold, tradeIn, isPart);
            dirty = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            if (total != 0) {
                playMenuSoundEffect(2);
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 0;
                running = 0;
                gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[isPart] = 0;
                if (isPart) {
                    if (isNewerGearPart(scroll + row)) {
                        msg = 0xA3;
                    } else {
                        msg = 0xAF;
                        arg = 0xB2;
                    }
                } else {
                    setupShopTotalNumber(total, 0);
                }
                setupDraw428(0);
                if (askYesNoQuestion(msg, arg, 1)) {
                    if (isPart) {
                        gMenuContext->m450_shop->m4654_buyQuantities[scroll + row] = 1;
                    }
                    completeGearShopPurchase(gold);
                } else {
                    running = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = running;
                    gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[isPart] = running;
                    gMenuContext->m33C_drawFlags->m49_draw43C = running;
                    prevScroll = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = running;
                }
                disableDraw428();
                arg = 0xFF;
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 5:
            gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = 0;
            running = 0;
            gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[isPart] = 0;
            if (total != 0 && !isPart) {
                gMenuContext->m33C_drawFlags->m5A_drawShop[0] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = 0;
                gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = 0;
                gMenuContext->m33C_drawFlags->m49_draw43C = 0;
                gMenuContext->m33C_drawFlags->m50_draw444[0] = 0;
                setupDraw428(0);
                if (askYesNoQuestion(0x8C, 0xFF, 1) == 0) {
                    running = 1;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[3] = running;
                    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[5] = running;
                    gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[0] = running;
                    gMenuContext->m33C_drawFlags->m49_draw43C = running;
                    prevScroll = 0xFF;
                    prevRow = 0xFF;
                    gMenuContext->m33C_drawFlags->m50_draw444[0] = running;
                }
                disableDraw428();
            }
            break;
        case 1:
            row++;
            if (row >= 8) {
                row = 7;
                scroll++;
                if (numEntries - 8 < scroll) {
                    scroll--;
                }
            }
            break;
        case 3:
            row--;
            if (row < 0) {
                row = 0;
                scroll--;
                if (scroll < 0) {
                    scroll = 0;
                }
            }
            break;
        case 0:
            if (flags[row] && !isPart) {
                if (gMenuContext->m450_shop->m4654_buyQuantities[scroll + row] + 1 + gearShopOwnedCount < 100) {
                    total += price;
                    gold -= price;
                    dirty = 1;
                    gMenuContext->m450_shop->m4654_buyQuantities[scroll + row]++;
                }
            }
            break;
        case 2:
            if (!isPart && gMenuContext->m450_shop->m4654_buyQuantities[scroll + row] != 0) {
                total -= price;
                gold += price;
                dirty = 1;
                gMenuContext->m450_shop->m4654_buyQuantities[scroll + row] += 0xFF;
            }
            break;
        }
    }
    gMenuContext->m450_shop->m4785_45B0Enabled = 0;
    processLoadSaveMenuSub7Sub0(5);
    countField33C_C(2, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    clearShopScreen(1);
    return 1;
}

/* Gear shop: refuel (and repair) the selected gear */
void refuelGearShopGear(void) {
    u8 msg;
    u8 mode;
    u8 arg;
    u8 done;
    u16 units;
    u32 cost;

    done = 0;
    msg = 0xA6;
    mode = 1;
    arg = 1;
    if (gameState.m978_gears[gearShopGearId].m3A_maxFuel == gameState.m978_gears[gearShopGearId].m38_fuel) {
        if (gameState.m978_gears[gearShopGearId].m60_hp == gameState.m978_gears[gearShopGearId].m64_maxHp) {
            msg = 0xB8;
            mode = 0;
            arg = 0;
        } else {
            msg = 0xB5;
            mode = 2;
        }
    }
    units = (gameState.m978_gears[gearShopGearId].m3A_maxFuel - gameState.m978_gears[gearShopGearId].m38_fuel) / 100;
    if (units == 0) {
        units = 1;
    }
    cost = units * 10;
    iniMenuContext364And380(5, 0xA2, 0xA6, 0x60, 0x14, 0, 1, 4, 0);
    setupGearShopGoldNumber((u32)gameState.m1924_gold);
    if (mode) {
        setupShopTotalNumber(cost, 1);
    }
    menuDraw();
    setupDraw428(0);
    if (askYesNoQuestion(msg, 0xFF, arg)) {
        gMenuContext->m33C_drawFlags->m5A_drawShop[1] = 0;
        switch (mode) {
        case 1:
            if ((u32)gameState.m1924_gold < cost) {
                if (askYesNoQuestion(0xA9, 0xFF, 1)) {
                    if ((u32)gameState.m1924_gold != 0) {
                        done = 1;
                    }
                    cost = (u32)gameState.m1924_gold / 10;
                    gameState.m978_gears[gearShopGearId].m38_fuel += cost * 100;
                    gameState.m1924_gold = (u32)gameState.m1924_gold % 10;
                    if (gameState.m978_gears[gearShopGearId].m3A_maxFuel < gameState.m978_gears[gearShopGearId].m38_fuel) {
                        gameState.m978_gears[gearShopGearId].m38_fuel = gameState.m978_gears[gearShopGearId].m3A_maxFuel;
                    }
                }
            } else {
                gameState.m1924_gold -= cost;
                gameState.m978_gears[gearShopGearId].m38_fuel = gameState.m978_gears[gearShopGearId].m3A_maxFuel;
                done = 1;
            }
            break;
        case 2:
            done = 1;
            break;
        }
    }
    if (done) {
        gameState.m978_gears[gearShopGearId].m60_hp = gameState.m978_gears[gearShopGearId].m64_maxHp;
        playMenuSoundEffect(0xD1);
    }
    disableDraw428(0);
    gMenuContext->m33C_drawFlags->m63_drawGearShop[1] = 0;
    processLoadSaveMenuSub7Sub0(5);
}

u8 hideGearShopMainMenu(void) {
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    countField33C_C(4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    clearShopScreen(0);
    return 2;
}

void executeGearShopPartsSelection(void) {
    switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
    case 0:
    case 1:
    case 2:
        runGearShopBuyMenu(0, 1);
        applyAllGearPartStats(gMenuContext->m330_itemData, gearShopGearId);
        break;
    case 3:
        refuelGearShopGear();
        break;
    }
}

/* gear shop submenu loop (four entries; entry 4 on m336 == 3) */
u8 runGearShopSubmenu(void) {
    u8 loop;
    u8 redraw;
    u8 base;

    loop = 1;
    redraw = 1;
    base = 0;
    if (gMenuContext->m336_menuSelectedEntry == 3) {
        if (hideGearShopMainMenu() == 0) {
            return 1;
        }
    }
    gMenuContext->m338_currentSelectedSubMenuEntry = 0;
    gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
    if (gMenuContext->m336_menuSelectedEntry == 3) {
        gMenuContext->m338_currentSelectedSubMenuEntry = 3;
        base = 4;
    }
    while (loop) {
        menuDraw();
        if (gMenuContext->m329_animationState == 0 && gearShopAvailableCount >= 2) {
            gMenuContext->m33C_drawFlags->m53_draw440 = 1;
        }
        if (redraw) {
            j_setupMenuContext4E0(4, gMenuContext->m6E0_mainMenuLabels, &gearShopSubMenuLabelConfig[base], gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            processLoadSaveMenuSub3(0);
            redraw = 0;
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            updateMenuSelection2(4, gMenuContext->m6E0_mainMenuLabels, &gearShopSubMenuLabelConfig[base], &gearShopSubMenuLabelOffsetsX[base], gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m338_currentSelectedSubMenuEntry, 4, 0);
            processLoadSaveMenuSub8(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        if (gMenuContext->m454_gearShop->m1ED8_mode[1] == 0) {
            gMenuContext->m33C_drawFlags->m63_drawGearShop[3] = 1;
            gMenuContext->m33C_drawFlags->m63_drawGearShop[2] = 0;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
            gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
            gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
            gMenuContext->m33C_drawFlags->m63_drawGearShop[3] = 0;
            countField33C_C(4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            playMenuSoundEffect(2);
            turnGearToSelectedPart();
            gMenuContext->m33C_drawFlags->m53_draw440 = 0;
            switch (gMenuContext->m336_menuSelectedEntry) {
            case 1:
                executeGearShopSellSelection(1);
                break;
            case 2:
                runGearShopBuyMenu(1, 0);
                break;
            case 3:
                executeGearShopPartsSelection();
                break;
            }
            resetGearShopCamera();
            menuDraw();
            setupGearShopStatsPanel();
            menuDraw();
            gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
            gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
            gMenuContext->m33C_drawFlags->mA_354Enabled = 1;
            j_setupMenuContext4E0(4, gMenuContext->m6E0_mainMenuLabels, &gearShopSubMenuLabelConfig[base], gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
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
        case 9:
            cycleGearShopGear(0);
            break;
        case 10:
            cycleGearShopGear(1);
            break;
        }
    }
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    countField33C_C(4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    return 1;
}

/* gear shop: gear view setup (mecha overlay, light/colour matrices, first party member's gear) */
void initGearShopView(void) {
    s32 i;

    i = 0;
    mechaInitOverlay(0x40);
    gMenuContext->m298_lightMatrix.m[0][0] = 0x546;
    gMenuContext->m298_lightMatrix.m[0][1] = -0xE39;
    gMenuContext->m298_lightMatrix.m[0][2] = 0x546;
    gMenuContext->m298_lightMatrix.m[1][0] = 0;
    gMenuContext->m298_lightMatrix.m[1][1] = 0;
    gMenuContext->m298_lightMatrix.m[1][2] = 0;
    gMenuContext->m298_lightMatrix.m[2][0] = 0;
    gMenuContext->m298_lightMatrix.m[2][1] = 0;
    gMenuContext->m298_lightMatrix.m[2][2] = 0;
    gMenuContext->m2B8_colorMatrix.m[0][0] = 0x600;
    gMenuContext->m2B8_colorMatrix.m[0][1] = 0;
    gMenuContext->m2B8_colorMatrix.m[0][2] = 0;
    gMenuContext->m2B8_colorMatrix.m[1][0] = 0x600;
    gMenuContext->m2B8_colorMatrix.m[1][1] = 0;
    gMenuContext->m2B8_colorMatrix.m[1][2] = 0;
    gMenuContext->m2B8_colorMatrix.m[2][0] = 0x600;
    gMenuContext->m2B8_colorMatrix.m[2][1] = 0;
    gMenuContext->m2B8_colorMatrix.m[2][2] = 0;
    mechaFinalizeVar0 = &gMenuContext->m2B8_colorMatrix;
    SetBackColor(0x3C, 0x3C, 0x3C);
    gMenuContext->m6C_drawContexts[0].mB0 = gearShopOrderingTables[0];
    gMenuContext->m6C_drawContexts[1].mB0 = gearShopOrderingTables[1];
    gMenuContext->m458_files[0] = allocateHeap((s32)sizeof(struct MenuShopFiles), 0);
    bzero(gMenuContext->m458_files[0], (s32)sizeof(struct MenuShopFiles));
    gMenuContext->m458_files[1] = allocateHeap((s32)sizeof(struct MenuShopFiles), 0);
    bzero(gMenuContext->m458_files[1], (s32)sizeof(struct MenuShopFiles));
    while (gMenuContext->m30C_characterAvailable[i] == 0) {
        i++;
    }
    gearShopGearId = gameState.m26C_party[i].mA0_gearNum;
    loadGearModelFiles(gearShopGearId, 1);
    countGearShopMembers();
}

/* gear shop teardown of the gear view */
void freeGearShopView(void) {
    while (gMenuContext->m329_animationState) {
        menuDraw();
    }
    gearShopDebugValuesEnabled = 0;
    gMenuContext->m33C_drawFlags->m63_drawGearShop[0] = 0;
    menuDraw();
    freeMechaModule();
    gMenuContext->m458_files[0]->m12_loaded = 0;
    gMenuContext->m458_files[1]->m12_loaded = 0;
    freeHeap(gMenuContext->m458_files[0]);
    freeHeap(gMenuContext->m458_files[1]);
}

/* gear status numbers into m330->m9C (like menu func_801E3C2C, without its gear 7
 * block; m18 without m54) */
void computeGearStatusStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    GameStateCharacter* pilot;
    s32 power;

    if (gameState.m22B6_gameFlags & 0x1000) {
        gearShopGearPilotIds[9] = 10;
    }
    gear = &gameState.m978_gears[gearId];
    pilot = &gameState.m26C_party[gearShopGearPilotIds[gearId]];
    ctx->m9C_gearStats.m0_hp = gear->m60_hp;
    ctx->m9C_gearStats.m4_maxHp = gear->m64_maxHp;
    ctx->m9C_gearStats.m8_defence = gear->m70_defence + gear->m40_accessoryDefence;
    ctx->m9C_gearStats.mA_etherDefence = pilot->m5C_etherDefence + pilot->m2C_etherDefenceBonus + gear->m42_accessoryEtherDefence + gear->m72_etherDefence;
    ctx->m9C_gearStats.mC_totalWeight = gear->m68_weight + gear->m44_accessoryWeight;
    ctx->m9C_gearStats.mE = gear->m6A;
    ctx->m9C_gearStats.m10_fuel = gear->m38_fuel;
    ctx->m9C_gearStats.m12_maxFuel = gear->m3A_maxFuel;
    power = gear->m3C_engineOutput * (gear->m74_baseAttackMultiplier + gear->m55_accessoryBonuses[1]);
    ctx->m9C_gearStats.m14_attack = ((gearId == 5 || gearId == 13) ? (gear->m12_slots[0].m0_weaponPower + gear->m12_slots[2].m0_weaponPower) * 6 / 10 : gear->m12_slots[0].m0_weaponPower) + power;
    ctx->m9C_gearStats.m16_response = gear->m9F_response + gear->m4D_accessoryResponse;
    ctx->m9C_gearStats.m17_agility = gear->m98_agility - gear->m4A_agilityPenalty;
    ctx->m9C_gearStats.m18_etherAmp = gear->m9E_etherAmp;
    if (pcGameplayOriginalBugFixes()) {
        ctx->m9C_gearStats.m18_etherAmp += gear->m54_accessoryEtherAmp;
    }
    ctx->m9C_gearStats.m19 = gear->m9D;
    ctx->m9C_gearStats.m1A = gear->m9C;
}

/* recompute all of a gear's part stats */
void applyAllGearPartStats(MenuContext_330* ctx, u8 gearId) {
    applyGearFrameStats(ctx, gearId);
    applyGearEngineStats(ctx, gearId);
    applyGearArmorStats(ctx, gearId);
    applyGearAccessoryBonuses(ctx, gearId);
    applyGearWeaponStats(ctx, gearId);
}

/* gear frame stats (max HP, m68, m98/m9D-m9F) from m330->m8 table; like menu func_801E41C0 */
void applyGearFrameStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearFrame* frame;

    gear = &gameState.m978_gears[gearId];
    frame = ctx->m8_gearFrames;
    frame += gear->m2_frame;
    gear->m64_maxHp = frame->m4_hp;
    gear->m68_weight = frame->m8_weight;
    gear->m98_agility = frame->m14_agility;
    gear->m9E_etherAmp = frame->m15_etherAmp;
    gear->m9D = frame->m16;
    gear->m9F_response = frame->m17_response;
    if ((u32)gear->m60_hp > (u32)gear->m64_maxHp) {
        gear->m60_hp = gear->m64_maxHp;
    }
}

/* gear armor stats (m70/m72) from m330->m10 table */
void applyGearArmorStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearArmor* armor;

    gear = &gameState.m978_gears[gearId];
    armor = ctx->m10_gearArmors;
    armor += gear->m8_armor;
    gear->m70_defence = armor->m8_defence;
    gear->m72_etherDefence = armor->mA_etherDefence;
}

/* gear engine stats (max fuel, m3C-m3F) from m330->mC table */
void applyGearEngineStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearEngine* engine;

    gear = &gameState.m978_gears[gearId];
    engine = ctx->mC_gearEngines;
    engine += gear->m3_engine;
    gear->m3A_maxFuel = engine->m6_fuel;
    gear->m3C_engineOutput = engine->mC_output;
    gear->m3D[0] = engine->mD;
    gear->m3D[1] = engine->mE_attackMultiplier;
    gear->m3F_attackMultiplier = engine->mE_attackMultiplier;
    if (gear->m38_fuel > gear->m3A_maxFuel) {
        gear->m38_fuel = gear->m3A_maxFuel;
    }
}

/* recompute a gear's accessory bonuses (like menu func_801E433C: no m1B -> m54,
 * ability mask 0xDB7F) */
void applyGearAccessoryBonuses(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    u16* abilities;
    u16* flags;
    MenuGearAccessory* acc;
    u8 i;
    u8 j;

    gear = &gameState.m978_gears[gearId];
    abilities = &gameState.m16C0_abilities[gearShopGearPilotIds[gearId]].m0_unlockedAbilitiesBitField[2];
    flags = &gameState.m16C0_abilities[gearShopGearPilotIds[gearId]].m1A_unlockedGearAbilities;
    gear->m40_accessoryDefence = 0;
    gear->m42_accessoryEtherDefence = 0;
    gear->m44_accessoryWeight = 0;
    gear->m48 = 0;
    gear->m4C = 0;
    gear->m4D_accessoryResponse = 0;
    gear->m4E_accessoryAgility = 0;
    gear->m4F_frameRepairLevel = 0;
    gear->m6E_elementResistanceMask = 0;
    for (j = 0; j < 16; j++) {
        gear->m88_elementResistances[j] = 0;
    }
    for (j = 0; j < 4; j++) {
        gear->m50_terrainAgility[j] = 0;
    }
    for (j = 0; j < 3; j++) {
        gear->m55_accessoryBonuses[j] = 0;
    }
    gear->m54_accessoryEtherAmp = 0;
    gear->m7E_statusFlags1 = 0;
    gear->m82_statusFlags3 = 0;
    gear->m86_statusFlags5 &= 0xF000;
    *abilities &= 0xDB7F;
    for (i = 0; i < 3; i++) {
        acc = ctx->m14_gearAccessories;
        acc += gear->m9_accessorySlots[i];
        gear->m40_accessoryDefence += acc->mD_defence;
        gear->m42_accessoryEtherDefence += acc->mE_etherDefence;
        gear->m44_accessoryWeight += acc->m6_weight;
        gear->m4C += acc->m18;
        gear->m4D_accessoryResponse += acc->m14_response;
        if (pcGameplayOriginalBugFixes()) {
            gear->m54_accessoryEtherAmp += acc->m1B_etherAmp;
        }
        for (j = 0; j < 4; j++) {
            gear->m50_terrainAgility[j] += acc->m10_terrainAgility[j];
        }
        switch (acc->m15_type) {
        case 1:
            gear->m7E_statusFlags1 |= acc->m16_value;
            break;
        case 2:
            gear->m82_statusFlags3 |= acc->m16_value;
            break;
        case 3:
            gear->m86_statusFlags5 |= acc->m16_value;
            break;
        case 4:
            for (j = 0; j < 16; j++) {
                gear->m6E_elementResistanceMask |= acc->m16_value; /* sic: inside the loop */
                if (acc->m16_value & (0x8000 >> j)) {
                    gear->m88_elementResistances[j] += acc->m1A_resistanceAmount;
                }
            }
            break;
        case 5:
            gear->m4F_frameRepairLevel += (u8)acc->m16_value;
            break;
        case 6:
            if ((*abilities & 0x1000) && (*abilities & 0x800)) {
                *abilities |= 0x400;
            }
            break;
        case 7:
            if ((*abilities & 0x200) && (*abilities & 0x100)) {
                *abilities |= 0x80;
            }
            break;
        case 8:
            if ((*abilities & 0x40) && (*abilities & 0x20)) {
                *abilities |= 0x10;
            }
            break;
        case 9:
            gear->m48 |= acc->m16_value;
        case 10:
            gear->m55_accessoryBonuses[1] += (u8)acc->m16_value;
            break;
        case 11:
            gear->m55_accessoryBonuses[2] += (u8)acc->m16_value;
            break;
        }
    }
    gear->m4A_agilityPenalty = computeGearAgilityPenalty(gearId);
    if (gear->m4F_frameRepairLevel) {
        *flags |= 0x8000;
    } else if (gearId == gameState.m26C_party[gearShopGearPilotIds[gearId]].mA0_gearNum) {
        *flags &= 0x7FFF;
    }
}

/* gear weapon stats from m330->m18 (three weapon slots for gears 5/13) */
void applyGearWeaponStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearWeapon* weapon;

    gear = &gameState.m978_gears[gearId];
    weapon = ctx->m18_gearWeapons;
    weapon += gear->mC_weapon[0];
    gear->m12_slots[0].m0_weaponPower = weapon->mE_power;
    gear->m10_weaponStatusMask = weapon->m12_statusMask;
    gear->m12_slots[0].m1_weaponStatusChance = weapon->m10_statusChance;
    gear->m12_slots[0].m2_weaponStatusType = weapon->m11_statusType;
    gear->m5C_extraFileIndex = weapon->m0_extraFileIndex;
    gear->m5D_weaponAnimOffsets[0] = weapon->m1_weaponAnimOffset0;
    gear->m5D_weaponAnimOffsets[1] = weapon->m2_weaponAnimOffset1;
    gear->m5D_weaponAnimOffsets[2] = weapon->m3_weaponAnimOffset2;
    if (gear->m12_slots[0].m2_weaponStatusType == 100) {
        gear->m86_statusFlags5 &= 0xFFF;
        gear->m86_statusFlags5 |= gear->m10_weaponStatusMask;
    }
    if (gearId == 5 || gearId == 13) {
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[0];
        gear->m12_slots[0].m0_weaponPower = weapon->mE_power;
        gear->m10_weaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[0].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[0].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[0] = weapon->m1_weaponAnimOffset0;
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[1];
        gear->m12_slots[1].m0_weaponPower = weapon->mE_power;
        gear->m12_slots[0].m6_nextWeaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[1].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[1].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[1] = weapon->m2_weaponAnimOffset1;
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[3];
        gear->m12_slots[2].m0_weaponPower = weapon->mE_power;
        gear->m12_slots[1].m6_nextWeaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[2].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[2].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[2] = weapon->m3_weaponAnimOffset2;
    }
}

/* gear value from accessory weight: max(0, (m44/120 - m75) / 2) */
u8 computeGearAgilityPenalty(u8 gearId) {
    GameStateGear* gear;
    s16 value;

    gear = &gameState.m978_gears[gearId];
    value = ((u16)(gear->m44_accessoryWeight / 120) - gear->m75_weightAllowance) / 2;
    if (value < 0) {
        value = 0;
    }
    return value;
}
