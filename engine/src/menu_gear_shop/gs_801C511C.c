/* The gear shop (disc 10/0A) menu overlay: the main menu's library (same code,
 * own addresses: MENU_FAMILY_OVERLAY keeps menu/menu.h's main-menu data and
 * prototypes out), ending with the gear shop's build of m_801E8018
 * (0x801CCE90-0x801CDC68: updateMenuSelection's twin updateMenuSelection, then
 * processLoadSaveMenuSub8). gcc-2.6.3-psx, ASPSX 2.34, -G0. */
#include "common.h"
#include "pc_convert.h"
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
void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h);
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

u16 randomU16InRange(u16 lo, u16 hi) {
    s32 range;

    if (lo == 0xFFFF) {
        return 0xFFFF;
    }
    if (hi == 0) {
        return 0;
    }
    if (lo == hi) {
        return lo;
    }
    range = hi - lo;
    if (range >= 0xFFFF) {
        return rand() & 0xFFFF;
    }
    return lo + R3000_MOD(rand() & 0xFFFF, range + 1);
}

/* Noah setupMenuPolyFT4 */
void setupMenuPolyFT4(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u16 w, u16 h) {
    p->x0 = x;
    p->y0 = y;
    p->y1 = y;
    p->x2 = x;
    p->u0 = u;
    p->u2 = u;
    p->x1 = x + w;
    p->y2 = y + h;
    p->x3 = x + w;
    p->y3 = y + h;
    p->v0 = v;
    p->u1 = u + w;
    p->v1 = v;
    p->v2 = v + h;
    p->u3 = u + w;
    p->v3 = v + h;
}

/* Noah menu2CheckBitmask family: mask & gearShopMenu2Bitmask[bit] */
u16 menu2CheckBitmask(u16 arg0, u8 arg1) {
    return gearShopMenu2Bitmask[arg1] & arg0;
}

/* Noah menu2CheckBitmask family: bit mask gearShopMenu2Bitmask[bit] */
u16 getMenuBitMask(u8 arg0) {
    return gearShopMenu2Bitmask[arg0];
}

u32 getMenuBitMask32(u8 arg0) {
    return gearShopMenu2Bitmask32[arg0];
}

/* Noah menu2CheckBitmask family: mask & gearShopMenu2Bitmask32[bit] (32-bit) */
u32 menu2CheckBitmask32(u32 mask, u8 bit) {
    return mask & gearShopMenu2Bitmask32[bit];
}

/* Noah fillStringDisplayBuffer */
void fillStringDisplayBuffer(u32 value) {
    u32 div;
    s32 i;
    u8 digit;

    div = 100000000;
    for (i = 0; i < 9; i++) {
        digit = value / div;
        value %= div;
        div /= 10;
        gMenuContext->m31C_stringDisplayBuffer[i] = digit;
    }
    for (i = 1; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0) {
            if (gMenuContext->m31C_stringDisplayBuffer[i - 1] == 0) {
                gMenuContext->m31C_stringDisplayBuffer[i - 1] = 0xFF;
            }
            return;
        }
        gMenuContext->m31C_stringDisplayBuffer[i - 1] = 0xFF;
    }
}

/* Noah initMenuContext32C */
void initMenuContext32C(u8 arg0) {
    if (arg0) {
        gMenuContext->m32C_memoryCardContext = allocateHeap(sizeof(MenuContext_32C), 0);
        bzero(gMenuContext->m32C_memoryCardContext, sizeof(MenuContext_32C));
    } else {
        freeHeap(gMenuContext->m32C_memoryCardContext);
    }
}

/* Noah initMenuContext33C */
void initMenuContext33C(u8 arg0) {
    if (arg0) {
        gMenuContext->m33C_drawFlags = allocateHeap(sizeof(MenuContext_33C), 0);
        bzero(gMenuContext->m33C_drawFlags, sizeof(MenuContext_33C));
    } else {
        freeHeap(gMenuContext->m33C_drawFlags);
    }
}

/* Noah initMenuContext350MainMenu */
void initMenuContext350MainMenu(u8 arg0) {
    if (arg0) {
        gMenuContext->m350_mainMenu = allocateHeap(sizeof(MenuContext_350), 0);
        bzero(gMenuContext->m350_mainMenu, sizeof(MenuContext_350));
    } else {
        freeHeap(gMenuContext->m350_mainMenu);
    }
}

/* Noah initMenuContext354 */
void initMenuContext354(u8 arg0) {
    if (arg0) {
        gMenuContext->m354_subMenu = allocateHeap((s32)sizeof(struct MenuContext_354), 0);
        bzero(gMenuContext->m354_subMenu, (s32)sizeof(struct MenuContext_354));
    } else {
        freeHeap(gMenuContext->m354_subMenu);
    }
}

/* Noah initMenuContext330 */
void initMenuContext330(u8 arg0) {
    if (arg0) {
        gMenuContext->m330_itemData = allocateHeap((s32)sizeof(struct MenuContext_330), 0);
        bzero(gMenuContext->m330_itemData, (s32)sizeof(struct MenuContext_330));
    } else {
        freeHeap(gMenuContext->m330_itemData);
    }
}

/* Noah initMenuContext348 */
void initMenuContext348(u8 arg0) {
    if (arg0) {
        gMenuContext->m348_cursor = allocateHeap(sizeof(MenuContext_348), 0);
        bzero(gMenuContext->m348_cursor, sizeof(MenuContext_348));
    } else {
        freeHeap(gMenuContext->m348_cursor);
    }
}

void initMenuContext1E20(u8 arg0) {
    if (arg0) {
        gMenuContext->m1E20_nameEntry = allocateHeap(sizeof(MenuContext_1E20), 0);
        bzero(gMenuContext->m1E20_nameEntry, sizeof(MenuContext_1E20));
    } else {
        freeHeap(gMenuContext->m1E20_nameEntry);
    }
}

void initMenuContext450Shop(u8 arg0) {
    if (arg0) {
        gMenuContext->m450_shop = allocateHeap(sizeof(MenuShop), 0);
        bzero(gMenuContext->m450_shop, sizeof(MenuShop));
    } else {
        freeHeap(gMenuContext->m450_shop);
    }
}

void initMenuContext454GearShop(u8 arg0) {
    if (arg0) {
        gMenuContext->m454_gearShop = allocateHeap(sizeof(MenuGearShop), 0);
        bzero(gMenuContext->m454_gearShop, sizeof(MenuGearShop));
    } else {
        freeHeap(gMenuContext->m454_gearShop);
    }
}

char* strcpy(char* dst, const char* src);
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
extern void* menuSharedResources;
extern void* fieldSeq;

/* gear shop variant of Noah loadMenuSharedResources (main menu func_801C65F4) */
void loadMenuSharedResources(void) {
    TIM_IMAGE tim;
    s32 border[18];
    uptr* bundle;
    u8* data;
    s32 i;

    bundle = menuSharedResources;
    bundle = (uptr*)pcRelocTable((u32*)bundle);
    data = mallocAndDecompress((void*)bundle[1], 1);
    OpenTIM((u32*)data);
    ReadTIM(&gMenuContext->m32C_memoryCardContext->mB80_iconTim);
    strcpy(gMenuContext->m32C_memoryCardContext->m4FCE_saveName, "BISLPS-00800");
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m0_magic[0] = 'S';
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m0_magic[1] = 'C';
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m2_iconDisplayFlag = 0x11;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m3_blockNumber = 1;
    bzero(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title, 0x5C);
    memmove(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m60_iconsPalette,
            gMenuContext->m32C_memoryCardContext->mB80_iconTim.caddr, 0x20);
    memmove(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m80_icon,
            gMenuContext->m32C_memoryCardContext->mB80_iconTim.paddr, 0x80);
    freeHeap(data);
    data = mallocAndDecompress((void*)bundle[2], 1);
    loadMenuImageBundle((u32*)data);
    freeHeap(data);
    gMenuContext->m2DC_font = mallocAndDecompress((void*)bundle[3], 0);
    gMenuContext->m2E0_textBundle = mallocAndDecompress((void*)bundle[4], 0);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14B, &border[0], &border[1], &border[2], &border[3],
                  &border[4], &border[5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14C, &border[6], &border[7], &border[8], &border[9],
                  &border[10], &border[11]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14D, &border[12], &border[13], &border[14], &border[15],
                  &border[16], &border[17]);
    border[10] += 0xC;
    data = mallocAndDecompress((void*)bundle[5], 1);
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            OpenTIM((u32*)(data + gMenuContext->m33C_drawFlags->m30_partyMemberId[i] * 0xB20));
            ReadTIM(&tim);
            tim.crect->x = border[i * 6 + 2];
            tim.crect->y = border[i * 6 + 3];
            tim.prect->x = border[i * 6 + 4];
            tim.prect->y = border[i * 6 + 5];
            LoadImage(tim.crect, tim.caddr);
            LoadImage(tim.prect, tim.paddr);
        }
    }
    DrawSync(0);
    freeHeap(data);
    if (useDebugMenuList) {
        setCurrentDirectory(0x10, 2);
        fieldSeq = allocateHeap(getFileSizeAligned(5), 0);
        readFile(5, fieldSeq, 0, 0x80);
        waitReadCompletion(0);
        setCurrentDirectory(0x10, 0);
        loadSequence(fieldSeq);
    }
    gMenuContext->m2E4_musicSequence = fieldSeq;
    gMenuContext->m1E90_shopRecords = mallocAndDecompress((void*)bundle[8], 1);
    freeHeap(menuSharedResources);
    freeHeap(bundle);
}

u16 menu2CheckBitmask(u16 mask, u8 bit);
void loadMenuSharedResources(void);

void initMenuGlobals(void) {
    s32 i;
    u16 mask;
    s32 id;

    gMenuContext->m336_menuSelectedEntry = 4;
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m326_cardPollTimer = 0x3C;
    gMenuContext->m334_cardOperation = 0;
    gMenuContext->m335 = 0;
    mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x77F;
    for (i = 0; i < 16; i++) {
        if (menu2CheckBitmask(mask, i) && gameState.m26C_party[i].mA0_gearNum != 0xFF) {
            gMenuContext->m30C_characterAvailable[i] = 1;
        } else {
            gMenuContext->m30C_characterAvailable[i] = 0;
        }
    }
    for (i = 0; i < 3; i++) {
        id = gameState.m1D34_currentParty[i];
        if (id != 0xFF && gMenuContext->m30C_characterAvailable[id] != 0) {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[i] = id;
        } else {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[i] = 0xFF;
        }
    }
    loadMenuSharedResources();
}

/* Noah resetMenuOddOrEven */
void resetMenuOddOrEven(void) {
    gMenuContext->m308_oddOrEven = 0;
}

/* Noah initMenuContext_1DE0: set up a label's two POLY_FT4s (texture page,
 * UVs, CLUT) for text rendered at index/startY (mode 0) or for a mode-given
 * texture (mode & 0x7F: colour mode + 1; bit 7 clear: semi-transparent) */
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode) {
    s32 i;
    POLY_FT4* p;
    u8 semiTrans;

    for (i = 0; i < 2; i++) {
        p = &label->m0_polys[i];
        semiTrans = 0;
        SetPolyFT4(p);
        SetSemiTrans(p, 0);
        SetShadeTex(p, 0);
        p->r0 = 0x80;
        p->g0 = 0x80;
        p->b0 = 0x80;
        if (mode == 0) {
            label->m7C_colorMode = index & 1;
            p->tpage = GetTPage(0, 0, 0x140, 0);
            setUVWH(p, ((index / 2) & 1) << 7, ((index + startY) / 4) * 13, label->m7E_stringWidth, 13);
        } else {
            if (!(mode & 0x80)) {
                semiTrans = 0x20;
                SetSemiTrans(p, 1);
                p->r0 = semiTrans;
                p->g0 = semiTrans;
                p->b0 = semiTrans;
            }
            label->m7C_colorMode = (mode & 0x7F) - 1;
            p->tpage = semiTrans | GetTPage(0, 0, 0x180, 0x80);
            setUVWH(p, (index & 1) * 0x60, (index / 2) * 13 + startY, label->m7E_stringWidth, 13);
        }
        label->m0_polys[i].clut = label->m7C_colorMode ? textSpriteMode1 : textSpriteMode0;
    }
    label->m7F_transformEnabled = 0;
}

/* Noah setupMenuContext4E0: render pairs of menu labels to VRAM and set their quads */
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count) {
    RECT* rect;
    s32 i;

    for (i = 0; i < count; i += 2) {
        labels[i].m7E_stringWidth = renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, config[i]),
                                                  gMenuContext->m4E0_labels[0].m78_imageData, 0x18, 0);
        rect = &labels[i].m70_rect;
        labels[i + 1].m7E_stringWidth = renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, config[i + 1]),
                                                      gMenuContext->m4E0_labels[0].m78_imageData, 0x18, 1);
        labels[i].m70_rect.x = ((i / 2) & 1) * 0x20 + 0x140;
        labels[i].m70_rect.y = ((i + startY) / 4) * 13;
        labels[i].m70_rect.w = 0x1C;
        labels[i].m70_rect.h = 0xD;
        labels[i + 1].m70_rect = labels[i].m70_rect;
        initMenuContext_1DE0(&labels[i], i, startY, 0);
        initMenuContext_1DE0(&labels[i + 1], i + 1, startY, 0);
        LoadImage(rect, gMenuContext->m4E0_labels[0].m78_imageData);
        DrawSync(0);
    }
}

/* Noah loadBlankImage (unused RECT reserves stack) */
void loadBlankImage(void) {
    RECT rect;
    RECT unused; /* only reserves stack */
    u16* buf;

    buf = allocateHeap(0x20, 0);
    bzero(buf, 0x20);
    buf[1] = 0x7FFF;
    rect.y = 0x1C0;
    rect.w = 0x10;
    rect.x = 0;
    rect.h = 1;
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
}

/* Noah initMenuContext4E0 */
void initMenuContext4E0(void) {
    initFontPalettes(0, 0x1D1);
    gMenuContext->m4E0_labels[0].m78_imageData = ALLOC_LABEL_IMAGE(0x38E);
    setupMenuContext4E0(&gMenuContext->m4E0_labels[0], gearShopInitMenuLabelConfig, 0, 4);
    loadBlankImage();
}

/* Noah computeMenuBorders (unused s32[10] reserves stack) */
void computeMenuBorders(void) {
    s32 unused[10]; /* only reserves stack */

    computeMenuBorder(gMenuContext->m2DC_font, 0xFE, &gMenuContext->m46C_menuBorders[0][0],
                  &gMenuContext->m46C_menuBorders[0][1], &gMenuContext->m46C_menuBorders[0][2],
                  &gMenuContext->m46C_menuBorders[0][3], &gMenuContext->m46C_menuBorders[0][4],
                  &gMenuContext->m46C_menuBorders[0][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x103, &gMenuContext->m46C_menuBorders[1][0],
                  &gMenuContext->m46C_menuBorders[1][1], &gMenuContext->m46C_menuBorders[1][2],
                  &gMenuContext->m46C_menuBorders[1][3], &gMenuContext->m46C_menuBorders[1][4],
                  &gMenuContext->m46C_menuBorders[1][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x100, &gMenuContext->m46C_menuBorders[2][0],
                  &gMenuContext->m46C_menuBorders[2][1], &gMenuContext->m46C_menuBorders[2][2],
                  &gMenuContext->m46C_menuBorders[2][3], &gMenuContext->m46C_menuBorders[2][4],
                  &gMenuContext->m46C_menuBorders[2][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x101, &gMenuContext->m46C_menuBorders[3][0],
                  &gMenuContext->m46C_menuBorders[3][1], &gMenuContext->m46C_menuBorders[3][2],
                  &gMenuContext->m46C_menuBorders[3][3], &gMenuContext->m46C_menuBorders[3][4],
                  &gMenuContext->m46C_menuBorders[3][5]);
}

/* Noah updateCursorPolysLocation */
void updateCursorPolysLocation(s32 idx, u8 draw) {
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m348_cursor, gMenuContext->m308_oddOrEven,
                  gearShopCursorPosXTable[idx], gearShopCursorPosYTable[idx], 0x1000);
    gMenuContext->m348_cursor->m158_oddOrEven = gMenuContext->m308_oddOrEven;
    if (draw) {
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x0 = gearShopCursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y0 = gearShopCursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x1 =
            gearShopCursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y1 = gearShopCursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x2 = gearShopCursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y2 = gearShopCursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x3 =
            gearShopCursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y3 = gearShopCursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x0 = gearShopCursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y0 = gearShopCursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x1 =
            gearShopCursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y1 = gearShopCursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x2 =
            gearShopCursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y2 = gearShopCursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x0 = gearShopCursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y0 = gearShopCursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x1 = gearShopCursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y1 = gearShopCursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x2 =
            gearShopCursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y2 = gearShopCursorPosYTable[idx] - 0x14;
        gMenuContext->m348_cursor->m159_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    }
}

/* Noah resetCursorState */
void resetCursorState(void) {
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
}

/* Noah setupPolyG4-like: SetPolyG4 + top colour */
void setupPolyG4(POLY_G4* p, s32 r, s32 g, s32 b) {
    SetPolyG4(p);
    p->r0 = r;
    p->g0 = g;
    p->b0 = b;
    p->r1 = r;
    p->g1 = g;
    p->b1 = b;
    p->r2 = 0;
    p->g2 = 0;
    p->b2 = 0;
    p->r3 = 0;
    p->g3 = 0;
    p->b3 = 0;
}

/* Noah initPrimitives_348_cursor */
void initPrimitives_348_cursor(void) {
    RECT rect;
    s32 i;

    rect.y = 0;
    rect.x = 0;
    rect.h = 0x100;
    rect.w = 0x100;
    resetCursorState();
    for (i = 0; i < 2; i++) {
        setupPolyG4(&gMenuContext->m348_cursor->m50_polyG4[i], 0x80, 0x80, 0);
        SetSemiTrans(&gMenuContext->m348_cursor->m50_polyG4[i], 1);
        SetLineF3(&gMenuContext->m348_cursor->mC8_lines[i]);
        (gMenuContext->m348_cursor->mC8_lines + i)->r0 = 0;
        (gMenuContext->m348_cursor->mC8_lines + i)->g0 = 0x40;
        (gMenuContext->m348_cursor->mC8_lines + i)->b0 = 0;
        SetLineF3(&gMenuContext->m348_cursor->mF8_lines[i]);
        (gMenuContext->m348_cursor->mF8_lines + i)->r0 = 0;
        (gMenuContext->m348_cursor->mF8_lines + i)->g0 = 0x40;
        (gMenuContext->m348_cursor->mF8_lines + i)->b0 = 0;
        SetPolyF4(&gMenuContext->m348_cursor->m98_poly[i]);
        (gMenuContext->m348_cursor->m98_poly + i)->x0 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->y0 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->x1 = 0x140;
        (gMenuContext->m348_cursor->m98_poly + i)->y1 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->x2 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->y2 = 0xE0;
        (gMenuContext->m348_cursor->m98_poly + i)->x3 = 0x140;
        (gMenuContext->m348_cursor->m98_poly + i)->y3 = 0xE0;
        (gMenuContext->m348_cursor->m98_poly + i)->r0 = 0x80;
        (gMenuContext->m348_cursor->m98_poly + i)->g0 = 0x80;
        (gMenuContext->m348_cursor->m98_poly + i)->b0 = 0x80;
        SetSemiTrans(&gMenuContext->m348_cursor->m98_poly[i], 1);
        SetDrawMode(&gMenuContext->m348_cursor->m128_drawMode[i], 0, 0, GetTPage(0, 0, 0x140, 0x80), &rect);
        SetDrawMode(&gMenuContext->m348_cursor->m140_backgroundDrawMode[i], 0, 0, GetTPage(0, 2, 0x180, 0), &rect);
    }
}

/* gear shop resources: load (0) / free (16) from the item data file (dir 0x10 file 2) */
void loadGearShopData(u8 arg0) {
    uptr* bundle;
    void* raw;

    if (arg0 < 16) {
        bundle = allocateHeap(getFileSizeAligned(2), 1);
        readFile(2, bundle, 0, 0x80);
        waitReadCompletion(0);
        raw = bundle;
        bundle = (uptr*)pcRelocTable((u32*)raw);
    }
    switch (arg0) {
    case 0:
        gMenuContext->m330_itemData->m8_gearFrames = mallocAndDecompress((void*)bundle[17], 0);
        gMenuContext->m330_itemData->mC_gearEngines = mallocAndDecompress((void*)bundle[19], 0);
        gMenuContext->m330_itemData->m10_gearArmors = mallocAndDecompress((void*)bundle[18], 0);
        gMenuContext->m330_itemData->m14_gearAccessories = mallocAndDecompress((void*)bundle[20], 0);
        gMenuContext->m330_itemData->m18_gearWeapons = mallocAndDecompress((void*)bundle[43], 0);
        gMenuContext->m450_shop->m4630_textBundles[1] = mallocAndDecompress((void*)bundle[51], 0);
        gMenuContext->m450_shop->m4630_textBundles[2] = mallocAndDecompress((void*)bundle[52], 0);
        gMenuContext->m450_shop->m4630_textBundles[3] = mallocAndDecompress((void*)bundle[48], 0);
        gMenuContext->m450_shop->m4630_textBundles[4] = mallocAndDecompress((void*)bundle[49], 0);
        gMenuContext->m450_shop->m4630_textBundles[5] = mallocAndDecompress((void*)bundle[50], 0);
        gMenuContext->m450_shop->m4630_textBundles[6] = mallocAndDecompress((void*)bundle[45], 0);
        gMenuContext->m450_shop->m4630_textBundles[7] = mallocAndDecompress((void*)bundle[46], 0);
        gMenuContext->m450_shop->m4630_textBundles[8] = mallocAndDecompress((void*)bundle[47], 0);
        break;
    case 16:
        freeHeap(gMenuContext->m330_itemData->m8_gearFrames);
        freeHeap(gMenuContext->m330_itemData->mC_gearEngines);
        freeHeap(gMenuContext->m330_itemData->m10_gearArmors);
        freeHeap(gMenuContext->m330_itemData->m14_gearAccessories);
        freeHeap(gMenuContext->m330_itemData->m18_gearWeapons);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[1]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[2]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[3]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[4]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[5]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[6]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[7]);
        freeHeap(gMenuContext->m450_shop->m4630_textBundles[8]);
        break;
    }
    if (arg0 < 16) {
        freeHeap(raw);
        freeHeap(bundle);
    }
}

extern s32 gearShopMemberColumnX[];
extern s32 gearShopStockCounts[5];

/* gear shop setup: shop stock from the shop record, list outlines, title strip */
void setupGearShopStock(void) {
    u8* rec;
    s32 i;
    s32 j;

    rec = gMenuContext->m1E90_shopRecords + g_menuOpenArg * 100;
    for (i = 0, gearShopStockCounts[0] = 0; i < 20; i++) {
        gMenuContext->m450_shop->m4720_stock[0 * 20 + i] = (rec + i)[0x14];
        if (gMenuContext->m450_shop->m4720_stock[0 * 20 + i] != 0) {
            gearShopStockCounts[0]++;
        }
    }
    for (i = 0, gearShopStockCounts[1] = 0; i < 20; i++) {
        gMenuContext->m450_shop->m4720_stock[1 * 20 + i] = rec[i];
        if (gMenuContext->m450_shop->m4720_stock[1 * 20 + i] != 0) {
            gearShopStockCounts[1]++;
        }
    }
    for (i = 0, gearShopStockCounts[2] = 0; i < 20; i++) {
        gMenuContext->m450_shop->m4720_stock[2 * 20 + i] = (rec + i)[0x28];
        if (gMenuContext->m450_shop->m4720_stock[2 * 20 + i] != 0) {
            gearShopStockCounts[2]++;
        }
    }
    for (i = 0, gearShopStockCounts[3] = 0; i < 20; i++) {
        gMenuContext->m450_shop->m4720_stock[3 * 20 + i] = (rec + i)[0x50];
        if (gMenuContext->m450_shop->m4720_stock[3 * 20 + i] != 0) {
            gearShopStockCounts[3]++;
        }
    }
    for (i = 0, gearShopStockCounts[4] = 0; i < 20; i++) {
        gMenuContext->m450_shop->m4720_stock[4 * 20 + i] = (rec + i)[0x3C];
        if (gMenuContext->m450_shop->m4720_stock[4 * 20 + i] != 0) {
            gearShopStockCounts[4]++;
        }
    }
    freeHeap(gMenuContext->m1E90_shopRecords);
    loadGearShopData(0);
    for (j = 0; j < 9; j++) {
        for (i = 0; i < 2; i++) {
            SetLineF3(&gMenuContext->m450_shop->m3890_lines[0][j * 2 + i]);
            setRGB0(&gMenuContext->m450_shop->m3890_lines[0][j * 2 + i], 0xFF, 0, 0);
            SetLineF3(&gMenuContext->m450_shop->m3A40_lines[0][j * 2 + i]);
            setRGB0(&gMenuContext->m450_shop->m3A40_lines[0][j * 2 + i], 0xFF, 0, 0);
            setXY3(&gMenuContext->m450_shop->m3890_lines[0][j * 2 + i], gearShopMemberColumnX[j], 0xA6, gearShopMemberColumnX[j] + 0x18, 0xA6,
                   gearShopMemberColumnX[j] + 0x18, 0xBC);
            setXY3(&gMenuContext->m450_shop->m3A40_lines[0][j * 2 + i], gearShopMemberColumnX[j], 0xA6, gearShopMemberColumnX[j], 0xBC,
                   gearShopMemberColumnX[j] + 0x18, 0xBC);
        }
        gMenuContext->m450_shop->m469C_lineEnabled[j] = 0;
    }
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x166, gMenuContext->m454_gearShop->m80_polys, gMenuContext->m308_oddOrEven, 0x108,
                  0x18, 0x1000);
    for (j = 0; j < 2; j++) {
        SetPolyFT4(&gMenuContext->m454_gearShop->m0_poly[j]);
        SetSemiTrans(&gMenuContext->m454_gearShop->m0_poly[j], 0);
        SetShadeTex(&gMenuContext->m454_gearShop->m0_poly[j], 0);
        setRGB0(&gMenuContext->m454_gearShop->m0_poly[j], 0x80, 0x80, 0x80);
        gMenuContext->m454_gearShop->m0_poly[j].tpage = GetTPage(0, 0, 0x180, 0);
        gMenuContext->m454_gearShop->m0_poly[j].clut = textSpriteMode0;
        setUVWH(&gMenuContext->m454_gearShop->m0_poly[j], 0, 0x48, 0x60, 0xD);
        setXYWH(&gMenuContext->m454_gearShop->m0_poly[j], 0x10, 0x20, 0x60, 0xD);
    }
}

/* Noah initMemoryCardPolyVerts */
void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h) {
    v[0].vx = x - 0xA0;
    v[0].vy = y - 0x70;
    v[0].vz = 0;
    v[1].vx = x + w - 0xA0;
    v[1].vy = y - 0x70;
    v[1].vz = 0;
    v[2].vx = x - 0xA0;
    v[2].vy = y + h - 0x70;
    v[2].vz = 0;
    v[3].vx = x + w - 0xA0;
    v[3].vy = y + h - 0x70;
    v[3].vz = 0;
}

/* Noah initMenuTiles1Sub */
void initMenuTiles1Sub(POLY_FT4* p) {
    SetSemiTrans(p, 1);
    SetShadeTex(p, 0);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
}

void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h);

void setupScrollBarThumb(s32 x, s32 y, s32 h, s32 count, s32 pos) {
    s32 offset = 0;

    if (!gMenuContext->m33C_drawFlags->m49_draw43C) {
        gMenuContext->m43C_scrollBarThumb = allocateHeap((s32)sizeof(struct MenuContext_43C), 0);
        bzero(gMenuContext->m43C_scrollBarThumb, (s32)sizeof(struct MenuContext_43C));
    }
    if (count < 9) {
        h = 100;
    } else {
        offset = pos * 100 / (count - 8);
        offset = offset * 4000 / 10000;
    }
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x107, gMenuContext->m43C_scrollBarThumb, gMenuContext->m308_oddOrEven, x, y, 0x1000);
    initMemoryCardPolyVerts(gMenuContext->m43C_scrollBarThumb->m50_vertex, x, y + offset, 8, h);
    gMenuContext->m43C_scrollBarThumb->m70_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
}

/* free m43C */
void freeScrollBarThumb(void) {
    gMenuContext->m33C_drawFlags->m49_draw43C = 0;
    freeHeap(gMenuContext->m43C_scrollBarThumb);
}

/* allocate the pointer glyph m444[slot] */
void allocMenuPointerGlyph(u8 arg0) {
    gMenuContext->m444_pointerGlyphs[arg0] = allocateHeap((s32)sizeof(struct MenuContext_444), 0);
    bzero(gMenuContext->m444_pointerGlyphs[arg0], (s32)sizeof(struct MenuContext_444));
    gMenuContext->m444_pointerGlyphs[arg0]->m70_frame = 4;
    gMenuContext->m444_pointerGlyphs[arg0]->m74_timer = 0;
}

void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h);

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
        x = 0x1C;
        y = index * 13 + 0x32;
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

/* Noah setupMenuContext364: init a menu box's frame primitives */
void setupMenuContext364(u8 boxId) {
    MenuContext_364* box;
    RECT tw;
    u8 i;

    box = gMenuContext->m364_menuBoxes[boxId];
    tw.y = 0;
    tw.x = 0;
    tw.h = 0x100;
    tw.w = 0x100;
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[boxId] = 0;
    gMenuContext->m33C_drawFlags->m27_menuBoxAnimating[boxId] = 0;
    for (i = 0; i < 2; i++) {
        SetPolyG4(&box->m4B0_background[i]);
        (&box->m4B0_background[i])->r0 = 0x68;
        (&box->m4B0_background[i])->g0 = 0x68;
        (&box->m4B0_background[i])->b0 = 0x68;
        (&box->m4B0_background[i])->r1 = 0x68;
        (&box->m4B0_background[i])->g1 = 0x68;
        (&box->m4B0_background[i])->b1 = 0x68;
        (&box->m4B0_background[i])->r2 = 0x68;
        (&box->m4B0_background[i])->g2 = 0x68;
        (&box->m4B0_background[i])->b2 = 0x68;
        (&box->m4B0_background[i])->r3 = 0x68;
        (&box->m4B0_background[i])->g3 = 0x68;
        (&box->m4B0_background[i])->b3 = 0x68;
        SetSemiTrans(&box->m4B0_background[i], 1);
        SetDrawMode(&box->m4F8_drawMode[i], 0, 0, GetTPage(0, 0, gMenuContext->m46C_menuBorders[0][4], gMenuContext->m46C_menuBorders[0][5]), &tw);
    }
    for (i = 0; i < 4; i++) {
        SetPolyFT4(&box->m140_topEdge[0][i]);
        SetShadeTex(&box->m140_topEdge[0][i], 1);
        (&box->m140_topEdge[0][i])->r0 = 0xFF;
        (&box->m140_topEdge[0][i])->g0 = 0xFF;
        (&box->m140_topEdge[0][i])->b0 = 0xFF;
        (&box->m140_topEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[0][1], 0, gMenuContext->m46C_menuBorders[0][4], gMenuContext->m46C_menuBorders[0][5]);
        (&box->m140_topEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[0][2], gMenuContext->m46C_menuBorders[0][3]);
        SetPolyFT4(&box->m1E0_bottomEdge[0][i]);
        SetShadeTex(&box->m1E0_bottomEdge[0][i], 1);
        (&box->m1E0_bottomEdge[0][i])->r0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->g0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->b0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[1][1], 0, gMenuContext->m46C_menuBorders[1][4], gMenuContext->m46C_menuBorders[1][5]);
        (&box->m1E0_bottomEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[1][2], gMenuContext->m46C_menuBorders[1][3]);
        SetPolyFT4(&box->m280_leftEdge[0][i]);
        SetShadeTex(&box->m280_leftEdge[0][i], 1);
        (&box->m280_leftEdge[0][i])->r0 = 0xFF;
        (&box->m280_leftEdge[0][i])->g0 = 0xFF;
        (&box->m280_leftEdge[0][i])->b0 = 0xFF;
        (&box->m280_leftEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[2][1], 0, gMenuContext->m46C_menuBorders[2][4], gMenuContext->m46C_menuBorders[2][5]);
        (&box->m280_leftEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[2][2], gMenuContext->m46C_menuBorders[2][3]);
        SetPolyFT4(&box->m320_rightEdge[0][i]);
        SetShadeTex(&box->m320_rightEdge[0][i], 1);
        (&box->m320_rightEdge[0][i])->r0 = 0xFF;
        (&box->m320_rightEdge[0][i])->g0 = 0xFF;
        (&box->m320_rightEdge[0][i])->b0 = 0xFF;
        (&box->m320_rightEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[3][1], 0, gMenuContext->m46C_menuBorders[3][4], gMenuContext->m46C_menuBorders[3][5]);
        (&box->m320_rightEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[3][2], gMenuContext->m46C_menuBorders[3][3]);
    }
}

/* Noah initMenuTiles6 */
void initMenuTiles6(u8 index, u16 x, u16 y, s32 width, s32 height) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];
    u16 h = height;

    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x105, &box->m410_innerCorners[0], gMenuContext->m308_oddOrEven, x, y, 0x1000);
    initMenuTiles6Sub(gMenuContext->m2DC_font, 0x105, &box->m410_innerCorners[1], gMenuContext->m308_oddOrEven, x,
                      y + h - 8, 0x1000, 0, 1);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x106, &box->m3C0_innerEdge, gMenuContext->m308_oddOrEven, x, y + 8, 0x1000);
    initMemoryCardPolyVerts(box->m6D0_innerCornerVerts[0], x, y, 8, 8);
    initMemoryCardPolyVerts(box->m6D0_innerCornerVerts[1], x, y + h, 8, -8);
    initMemoryCardPolyVerts(box->m6B0_innerEdgeVerts, x, y + 8, 8, h - 8);
}

/* Noah initMenuTiles1 */
void initMenuTiles1(u8 index, u16 x, u16 y, u16 width, s32 height) {
    MenuContext* ctx;
    MenuContext_364* box;
    s32 i;
    u16 x1;
    u16 y1;

    ctx = gMenuContext;
    box = ctx->m364_menuBoxes[index];
    box->m710_cornerCount = 0;
    box->m710_cornerCount += setupStringInPolyFT4(ctx->m2DC_font, 0xFD, &box->m0_corners[box->m710_cornerCount], ctx->m308_oddOrEven, 0, 0, 0x1000);
    box->m710_cornerCount += setupStringInPolyFT4(gMenuContext->m2DC_font, 0xFF, &box->m0_corners[box->m710_cornerCount], gMenuContext->m308_oddOrEven, 0, 0,
                               0x1000);
    box->m710_cornerCount += setupStringInPolyFT4(gMenuContext->m2DC_font, 0x102, &box->m0_corners[box->m710_cornerCount], gMenuContext->m308_oddOrEven, 0,
                               0, 0x1000);
    box->m710_cornerCount += setupStringInPolyFT4(gMenuContext->m2DC_font, 0x104, &box->m0_corners[box->m710_cornerCount], gMenuContext->m308_oddOrEven, 0,
                               0, 0x1000);
    initMemoryCardPolyVerts(box->m510_cornerVerts[0], x - 8, y + 8, 0x10, -0x10);
    x1 = x + width + 8;
    initMemoryCardPolyVerts(box->m510_cornerVerts[1], x1, y + 8, -0x10, -0x10);
    y1 = y + height - 8;
    initMemoryCardPolyVerts(box->m510_cornerVerts[2], x - 8, y1, 0x10, 0x10);
    initMemoryCardPolyVerts(box->m510_cornerVerts[3], x1, y1, -0x10, 0x10);
    for (i = 0; i < 4; i++) {
        initMenuTiles1Sub(&box->m0_corners[0][i * 2 + gMenuContext->m308_oddOrEven]);
    }
}

/* Noah initMenuTilesTopFrame */
void initMenuTilesTopFrame(u8 index, u16 x, u16 y, u16 width) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];
    s32 half;
    s32 i;

    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->u0 = 0;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->u1 = 7;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->u2 = 0;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->v2 = 0x94;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->u3 = 7;
    (&box->m140_topEdge[0][gMenuContext->m308_oddOrEven])->v3 = 0x94;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->u0 = 0;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->u1 = 7;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->u2 = 0;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->v2 = 0x94;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->u3 = 7;
    (&box->m140_topEdge[1][gMenuContext->m308_oddOrEven])->v3 = 0x94;
    half = (width - 0x10) / 2;
    initMemoryCardPolyVerts(box->m590_topEdgeLeftVerts, x + 8, y - 8, half, 0x10);
    initMemoryCardPolyVerts(box->m5B0_topEdgeRightVerts, x + (half + 8), y - 8, half, 0x10);
    for (i = 0; i < 2; i++) {
        initMenuTiles1Sub(&box->m140_topEdge[0][i * 2 + gMenuContext->m308_oddOrEven]);
    }
}

/* Noah initMenuTilesBottomFrame */
void initMenuTilesBottomFrame(u8 index, u16 x, u16 y, u16 width, s32 height) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];
    s32 half;
    u16 y1;
    s32 i;

    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->u0 = 8;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->u1 = 0xF;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->u2 = 8;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->v2 = 0x94;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->u3 = 0xF;
    (&box->m1E0_bottomEdge[0][gMenuContext->m308_oddOrEven])->v3 = 0x94;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->u0 = 8;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->u1 = 0xF;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->u2 = 8;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->v2 = 0x94;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->u3 = 0xF;
    (&box->m1E0_bottomEdge[1][gMenuContext->m308_oddOrEven])->v3 = 0x94;
    y1 = y + height - 8;
    half = (width - 0x10) / 2;
    initMemoryCardPolyVerts(box->m5D0_bottomEdgeLeftVerts, x + 8, y1, half, 0x10);
    initMemoryCardPolyVerts(box->m5F0_bottomEdgeRightVerts, x + (half + 8), y1, half, 0x10);
    for (i = 0; i < 2; i++) {
        initMenuTiles1Sub(&box->m1E0_bottomEdge[0][i * 2 + gMenuContext->m308_oddOrEven]);
    }
}

/* Noah initMenuTiles4 */
void initMenuTiles4(u8 index, u16 x, u16 y, u16 height) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];
    s32 half;
    s32 i;

    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->u0 = 0x10;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->u1 = 0x20;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->u2 = 0x10;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->v2 = 0x8B;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->u3 = 0x20;
    (&box->m280_leftEdge[0][gMenuContext->m308_oddOrEven])->v3 = 0x8B;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->u0 = 0x10;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->v0 = 0x84;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->u1 = 0x20;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->v1 = 0x84;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->u2 = 0x10;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->v2 = 0x8B;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->u3 = 0x20;
    (&box->m280_leftEdge[1][gMenuContext->m308_oddOrEven])->v3 = 0x8B;
    half = (height - 0x10) / 2;
    initMemoryCardPolyVerts(box->m610_leftEdgeTopVerts, x - 8, y + 8, 0x10, half);
    initMemoryCardPolyVerts(box->m630_leftEdgeBottomVerts, x - 8, y + (half + 8), 0x10, half);
    for (i = 0; i < 2; i++) {
        initMenuTiles1Sub(&box->m280_leftEdge[0][i * 2 + gMenuContext->m308_oddOrEven]);
    }
}

/* Noah initMenuTiles5 */
void initMenuTiles5(u8 index, u16 x, u16 y, u16 width, u16 height) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];
    s32 half;
    u16 x1;
    s32 i;

    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->u0 = 0x10;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->v0 = 0x8C;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->u1 = 0x20;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->v1 = 0x8C;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->u2 = 0x10;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->v2 = 0x93;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->u3 = 0x20;
    (&box->m320_rightEdge[0][gMenuContext->m308_oddOrEven])->v3 = 0x93;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->u0 = 0x10;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->v0 = 0x8C;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->u1 = 0x20;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->v1 = 0x8C;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->u2 = 0x10;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->v2 = 0x93;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->u3 = 0x20;
    (&box->m320_rightEdge[1][gMenuContext->m308_oddOrEven])->v3 = 0x93;
    x1 = x + width - 8;
    half = (height - 0x10) / 2;
    initMemoryCardPolyVerts(box->m650_rightEdgeTopVerts, x1, y + 8, 0x10, half);
    initMemoryCardPolyVerts(box->m670_rightEdgeBottomVerts, x1, y + (half + 8), 0x10, half);
    for (i = 0; i < 2; i++) {
        initMenuTiles1Sub(&box->m320_rightEdge[0][i * 2 + gMenuContext->m308_oddOrEven]);
    }
}

/* Noah draw380 */
void draw380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 transformDisabled, s32 arg6, u8 arg7) {
    MenuContext_364* box = gMenuContext->m364_menuBoxes[index];

    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[index] = 0;
    initMemoryCardPolyVerts(box->m690_backgroundVerts, x, y, width, height);
    initMenuTiles1(index, x, y, width, height);
    initMenuTilesTopFrame(index, x, y, width);
    initMenuTilesBottomFrame(index, x, y, width, height);
    initMenuTiles4(index, x, y, height);
    initMenuTiles5(index, x, y, width, height);
    if (arg7 != 0) {
        initMenuTiles6(index, x, y, width, height);
    }
    box->m71D_drawInner = arg7;
    box->m714_transformDisabled = transformDisabled;
    box->m718_otIndex = arg6;
    box->m71C_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[index] = 1;
}

/* free a menu box (inverse of iniMenuContext364And380) */
void processLoadSaveMenuSub7Sub0(u8 index) {
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[index] = 0;
    gMenuContext->m33C_drawFlags->m27_menuBoxAnimating[index] = 0;
    freeHeap(gMenuContext->m364_menuBoxes[index]);
    freeHeap(gMenuContext->m380_menuBoxAnims[index]);
}

/* Noah iniMenuContext364And380 */
void iniMenuContext364And380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 animate, u8 transformDisabled, s32 arg7,
                   u8 arg8) {
    MenuContext_380* anim;

    if (index >= 2) {
        gMenuContext->m364_menuBoxes[index] = allocateHeap(sizeof(MenuContext_364), 0);
        bzero(gMenuContext->m364_menuBoxes[index], sizeof(MenuContext_364));
        gMenuContext->m380_menuBoxAnims[index] = allocateHeap(sizeof(MenuContext_380), 0);
        bzero(gMenuContext->m380_menuBoxAnims[index], sizeof(MenuContext_380));
        setupMenuContext364(index);
    }
    anim = gMenuContext->m380_menuBoxAnims[index];
    if (animate != 0) {
        anim->m10_boxIndex = index;
        anim->m11_done = 0;
        anim->m0_x = x;
        anim->m2_y = y;
        anim->m4_width = width;
        anim->m6_height = height;
        anim->m8_currentWidth = 0;
        anim->mA_currentHeight = 0;
        gMenuContext->m33C_drawFlags->m27_menuBoxAnimating[index] = 1;
        anim->m12_transformDisabled = transformDisabled;
        anim->mC_otIndex = arg7;
    } else {
        draw380(index, x, y, width, height, transformDisabled, arg7, arg8);
    }
}

/* Noah drawMenu_380: menu box open animation step (calls draw380) */
void drawMenu_380(void) {
    s32 i;
    MenuContext_380* anim;
    u8 done;
    u16 v;

    for (i = 0; i < 7; i++) {
        anim = gMenuContext->m380_menuBoxAnims[i];
        if (gMenuContext->m33C_drawFlags->m27_menuBoxAnimating[i] != 0 && anim->m11_done == 0) {
            done = 0;
            if (anim->m8_currentWidth + 0x20 >= anim->m4_width) {
                v = anim->m4_width;
                done = 1;
            } else {
                v = anim->m8_currentWidth + 0x20;
            }
            anim->m8_currentWidth = v;
            if (anim->mA_currentHeight + 0x20 >= anim->m6_height) {
                v = anim->m6_height;
                done++;
            } else {
                v = anim->mA_currentHeight + 0x20;
            }
            anim->mA_currentHeight = v;
            if (done == 2) {
                anim->m11_done = 1;
            }
            draw380(anim->m10_boxIndex, anim->m0_x + (anim->m4_width >> 1) - (anim->m8_currentWidth >> 1),
                          anim->m2_y + (anim->m6_height >> 1) - (anim->mA_currentHeight >> 1), anim->m8_currentWidth,
                          anim->mA_currentHeight, anim->m12_transformDisabled, anim->mC_otIndex, anim->m13_drawInner);
        }
    }
}

/* Noah transformAndAddPrim */
void transformAndAddPrim(s32 count, SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven) {
    s32 i;
    s32 depth;
    s32 flag;

    for (i = 0; i < count; i++) {
        RotTransPers4(&verts[i * 4 + 0], &verts[i * 4 + 1], &verts[i * 4 + 2], &verts[i * 4 + 3],
                      (s32*)&polys[i * 2 + oddOrEven].x0, (s32*)&polys[i * 2 + oddOrEven].x1,
                      (s32*)&polys[i * 2 + oddOrEven].x2, (s32*)&polys[i * 2 + oddOrEven].x3, &depth, &flag);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &polys[i * 2 + oddOrEven]);
    }
}

/* Noah drawMultiPolys */
void drawMultiPolys(s32 count, POLY_FT4* polys, s32 oddOrEven) {
    s32 i;

    for (i = 0; i < count; i++) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &polys[i * 2 + oddOrEven]);
    }
}

/* Noah FUN_Menu2__801d1464 (m43C) */
void drawScrollBarThumb(void) {
    if (gMenuContext->m33C_drawFlags->m49_draw43C != 0) {
        transformAndAddPrim(1, gMenuContext->m43C_scrollBarThumb->m50_vertex, gMenuContext->m43C_scrollBarThumb->m0_polys, gMenuContext->m43C_scrollBarThumb->m70_oddOrEven);
    }
}

/* Noah drawMenu_348_cursor; JP also draws the selection box (lines + gradient) when m33C->m3 is set */
void drawMenu_348_cursor(void) {
#if VERSION_JP
    if (gMenuContext->m33C_drawFlags->m3_drawSelectionBox != 0) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->mC8_lines[gMenuContext->m348_cursor->m159_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->mF8_lines[gMenuContext->m348_cursor->m159_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m50_polyG4[gMenuContext->m348_cursor->m159_oddOrEven]);
    }
#endif
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m128_drawMode[gMenuContext->m348_cursor->m159_oddOrEven]);
    if (gMenuContext->m33C_drawFlags->m4_drawCursor != 0) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m0_cursor[gMenuContext->m348_cursor->m158_oddOrEven]);
    }
}

extern s32 gearShopAvailableCount;
void transformAndAddPrim(s32 count, SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven);

void drawCharacterSwitchArrows(void) {
    if (gMenuContext->m33C_drawFlags->m53_draw440 != 0 && gearShopAvailableCount >= 2) {
        transformAndAddPrim(4, gMenuContext->m440_arrows->m140_vertex[0], gMenuContext->m440_arrows->m0_polys,
                      gMenuContext->m440_arrows->m1C0_oddOrEven);
    }
}

void drawMenuBoxTopFrame(s32 box) {
    MenuContext_364* b;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    RotTransPers4(&b->m590_topEdgeLeftVerts[0], &b->m590_topEdgeLeftVerts[1], &b->m590_topEdgeLeftVerts[2], &b->m590_topEdgeLeftVerts[3], (s32*)&b->m140_topEdge[0][b->m71C_oddOrEven].x0,
                  (s32*)&b->m140_topEdge[0][b->m71C_oddOrEven].x1, (s32*)&b->m140_topEdge[0][b->m71C_oddOrEven].x2,
                  (s32*)&b->m140_topEdge[0][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m140_topEdge[0][b->m71C_oddOrEven]);
    RotTransPers4(&b->m5B0_topEdgeRightVerts[0], &b->m5B0_topEdgeRightVerts[1], &b->m5B0_topEdgeRightVerts[2], &b->m5B0_topEdgeRightVerts[3], (s32*)&b->m140_topEdge[1][b->m71C_oddOrEven].x0,
                  (s32*)&b->m140_topEdge[1][b->m71C_oddOrEven].x1, (s32*)&b->m140_topEdge[1][b->m71C_oddOrEven].x2,
                  (s32*)&b->m140_topEdge[1][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m140_topEdge[1][b->m71C_oddOrEven]);
}

void drawMenuBoxBottomFrame(s32 box) {
    MenuContext_364* b;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    RotTransPers4(&b->m5D0_bottomEdgeLeftVerts[0], &b->m5D0_bottomEdgeLeftVerts[1], &b->m5D0_bottomEdgeLeftVerts[2], &b->m5D0_bottomEdgeLeftVerts[3], (s32*)&b->m1E0_bottomEdge[0][b->m71C_oddOrEven].x0,
                  (s32*)&b->m1E0_bottomEdge[0][b->m71C_oddOrEven].x1, (s32*)&b->m1E0_bottomEdge[0][b->m71C_oddOrEven].x2,
                  (s32*)&b->m1E0_bottomEdge[0][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m1E0_bottomEdge[0][b->m71C_oddOrEven]);
    RotTransPers4(&b->m5F0_bottomEdgeRightVerts[0], &b->m5F0_bottomEdgeRightVerts[1], &b->m5F0_bottomEdgeRightVerts[2], &b->m5F0_bottomEdgeRightVerts[3], (s32*)&b->m1E0_bottomEdge[1][b->m71C_oddOrEven].x0,
                  (s32*)&b->m1E0_bottomEdge[1][b->m71C_oddOrEven].x1, (s32*)&b->m1E0_bottomEdge[1][b->m71C_oddOrEven].x2,
                  (s32*)&b->m1E0_bottomEdge[1][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m1E0_bottomEdge[1][b->m71C_oddOrEven]);
}

void drawMenuBoxLeftFrame(s32 box) {
    MenuContext_364* b;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    RotTransPers4(&b->m610_leftEdgeTopVerts[0], &b->m610_leftEdgeTopVerts[1], &b->m610_leftEdgeTopVerts[2], &b->m610_leftEdgeTopVerts[3], (s32*)&b->m280_leftEdge[0][b->m71C_oddOrEven].x0,
                  (s32*)&b->m280_leftEdge[0][b->m71C_oddOrEven].x1, (s32*)&b->m280_leftEdge[0][b->m71C_oddOrEven].x2,
                  (s32*)&b->m280_leftEdge[0][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m280_leftEdge[0][b->m71C_oddOrEven]);
    RotTransPers4(&b->m630_leftEdgeBottomVerts[0], &b->m630_leftEdgeBottomVerts[1], &b->m630_leftEdgeBottomVerts[2], &b->m630_leftEdgeBottomVerts[3], (s32*)&b->m280_leftEdge[1][b->m71C_oddOrEven].x0,
                  (s32*)&b->m280_leftEdge[1][b->m71C_oddOrEven].x1, (s32*)&b->m280_leftEdge[1][b->m71C_oddOrEven].x2,
                  (s32*)&b->m280_leftEdge[1][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m280_leftEdge[1][b->m71C_oddOrEven]);
}

void drawMenuBoxRightFrame(s32 box) {
    MenuContext_364* b;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    RotTransPers4(&b->m650_rightEdgeTopVerts[0], &b->m650_rightEdgeTopVerts[1], &b->m650_rightEdgeTopVerts[2], &b->m650_rightEdgeTopVerts[3], (s32*)&b->m320_rightEdge[0][b->m71C_oddOrEven].x0,
                  (s32*)&b->m320_rightEdge[0][b->m71C_oddOrEven].x1, (s32*)&b->m320_rightEdge[0][b->m71C_oddOrEven].x2,
                  (s32*)&b->m320_rightEdge[0][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m320_rightEdge[0][b->m71C_oddOrEven]);
    RotTransPers4(&b->m670_rightEdgeBottomVerts[0], &b->m670_rightEdgeBottomVerts[1], &b->m670_rightEdgeBottomVerts[2], &b->m670_rightEdgeBottomVerts[3], (s32*)&b->m320_rightEdge[1][b->m71C_oddOrEven].x0,
                  (s32*)&b->m320_rightEdge[1][b->m71C_oddOrEven].x1, (s32*)&b->m320_rightEdge[1][b->m71C_oddOrEven].x2,
                  (s32*)&b->m320_rightEdge[1][b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m320_rightEdge[1][b->m71C_oddOrEven]);
}

void drawMenuBoxBackground(s32 box) {
    MenuContext_364* b;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    RotTransPers4(&b->m690_backgroundVerts[0], &b->m690_backgroundVerts[1], &b->m690_backgroundVerts[2], &b->m690_backgroundVerts[3], (s32*)&b->m4B0_background[b->m71C_oddOrEven].x0,
                  (s32*)&b->m4B0_background[b->m71C_oddOrEven].x1, (s32*)&b->m4B0_background[b->m71C_oddOrEven].x2,
                  (s32*)&b->m4B0_background[b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m4B0_background[b->m71C_oddOrEven]);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m4F8_drawMode[b->m71C_oddOrEven]);
}

void drawMenuBoxCorners(s32 box) {
    MenuContext_364* b;
    s32 i;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    for (i = 0; i < 4; i++) {
        RotTransPers4(&b->m510_cornerVerts[0][i * 4 + 0], &b->m510_cornerVerts[0][i * 4 + 1], &b->m510_cornerVerts[0][i * 4 + 2], &b->m510_cornerVerts[0][i * 4 + 3],
                      (s32*)&b->m0_corners[0][i * 2 + b->m71C_oddOrEven].x0, (s32*)&b->m0_corners[0][i * 2 + b->m71C_oddOrEven].x1,
                      (s32*)&b->m0_corners[0][i * 2 + b->m71C_oddOrEven].x2, (s32*)&b->m0_corners[0][i * 2 + b->m71C_oddOrEven].x3,
                      &depth, &flag);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m0_corners[0][i * 2 + b->m71C_oddOrEven]);
    }
}

void drawMenuBoxInnerFrame(s32 box) {
    MenuContext_364* b;
    s32 i;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    for (i = 0; i < 2; i++) {
        RotTransPers4(&b->m6D0_innerCornerVerts[0][i * 4 + 0], &b->m6D0_innerCornerVerts[0][i * 4 + 1], &b->m6D0_innerCornerVerts[0][i * 4 + 2], &b->m6D0_innerCornerVerts[0][i * 4 + 3],
                      (s32*)&b->m410_innerCorners[0][i * 2 + b->m71C_oddOrEven].x0, (s32*)&b->m410_innerCorners[0][i * 2 + b->m71C_oddOrEven].x1,
                      (s32*)&b->m410_innerCorners[0][i * 2 + b->m71C_oddOrEven].x2, (s32*)&b->m410_innerCorners[0][i * 2 + b->m71C_oddOrEven].x3,
                      &depth, &flag);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m410_innerCorners[0][i * 2 + b->m71C_oddOrEven]);
    }
    RotTransPers4(&b->m6B0_innerEdgeVerts[0], &b->m6B0_innerEdgeVerts[1], &b->m6B0_innerEdgeVerts[2], &b->m6B0_innerEdgeVerts[3], (s32*)&b->m3C0_innerEdge[b->m71C_oddOrEven].x0,
                  (s32*)&b->m3C0_innerEdge[b->m71C_oddOrEven].x1, (s32*)&b->m3C0_innerEdge[b->m71C_oddOrEven].x2,
                  (s32*)&b->m3C0_innerEdge[b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m3C0_innerEdge[b->m71C_oddOrEven]);
}

void drawMenuBoxCorners(s32 box);
void drawMenuBoxInnerFrame(s32 box);
void drawMenuBoxTopFrame(s32 box);
void drawMenuBoxBottomFrame(s32 box);
void drawMenuBoxLeftFrame(s32 box);
void drawMenuBoxRightFrame(s32 box);
void drawMenuBoxBackground(s32 box);

void drawMenuBoxes(void) {
    s32 i;
    MenuContext_364* b;
    SVECTOR rot;
    VECTOR trans;
    MATRIX m;
    SVECTOR unused;

    for (i = 0; i < 7; i++) {
        if (gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[i] != 0) {
            b = gMenuContext->m364_menuBoxes[i];
            if (b->m714_transformDisabled == 0) {
                PushMatrix();
                rot.vz = 0;
                rot.vy = 0;
                rot.vx = 0;
                trans.vy = 0;
                trans.vx = 0;
                trans.vz = 0x200;
                createRotationMatrix(&rot, &m);
                TransMatrix(&m, &trans);
                SetRotMatrix(&m);
                SetTransMatrix(&m);
                drawMenuBoxCorners(i);
                if (b->m71D_drawInner != 0) {
                    drawMenuBoxInnerFrame(i);
                }
                drawMenuBoxTopFrame(i);
                drawMenuBoxBottomFrame(i);
                drawMenuBoxLeftFrame(i);
                drawMenuBoxRightFrame(i);
                drawMenuBoxBackground(i);
                PopMatrix();
            } else {
                drawMenuBoxCorners(i);
                if (b->m71D_drawInner != 0) {
                    drawMenuBoxInnerFrame(i);
                }
                drawMenuBoxTopFrame(i);
                drawMenuBoxBottomFrame(i);
                drawMenuBoxLeftFrame(i);
                drawMenuBoxRightFrame(i);
                drawMenuBoxBackground(i);
            }
        }
    }
}

extern s32 gearShopSaveCursorToSlot[];
extern s32 gearShopMemoryCardTilesPositionX[];
extern s32 gearShopMemoryCardTilesPositionY[];

void drawMenu_428(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0) {
        for (i = 0; i < 4; i++) {
            if (gMenuContext->m428_cardCursors->m140_enabled[i] != 0) {
                if (gMenuContext->m428_cardCursors->m144_followTile[i] != 0) {
                    setXY4(&gMenuContext->m428_cardCursors->m0_polys[0][i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]],
                           gearShopMemoryCardTilesPositionX[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           gearShopMemoryCardTilesPositionY[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           gearShopMemoryCardTilesPositionX[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           gearShopMemoryCardTilesPositionY[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           gearShopMemoryCardTilesPositionX[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           gearShopMemoryCardTilesPositionY[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10,
                           gearShopMemoryCardTilesPositionX[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           gearShopMemoryCardTilesPositionY[gearShopSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10);
                }
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m428_cardCursors->m0_polys[0][i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]]);
            }
        }
    }
}

/* Noah drawMenu2_sub_sub0 */
void drawMenu2_sub_sub0(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gMenuContext->m33C_drawFlags->m34_drawLabels[i] != 0) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m4E0_labels[i].m0_polys[gMenuContext->m4E0_labels[i].m7D_oddOrEven]);
        }
    }
}

void drawMenu2_sub_sub1(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[i] != 0) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m6E0_mainMenuLabels[i].m0_polys[gMenuContext->m6E0_mainMenuLabels[i].m7D_oddOrEven]);
        }
    }
}

void drawMenu2_sub_sub2(void) {
    s32 i;
    s32 depth;
    s32 flag;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[i] != 0) {
            if (gMenuContext->mAE0_subMenuLabels[i].m7F_transformEnabled != 0) {
                RotTransPers4(&gMenuContext->mAE0_subMenuLabels[i].m50_vertex[0], &gMenuContext->mAE0_subMenuLabels[i].m50_vertex[1],
                              &gMenuContext->mAE0_subMenuLabels[i].m50_vertex[2], &gMenuContext->mAE0_subMenuLabels[i].m50_vertex[3],
                              (s32*)&gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven].x0,
                              (s32*)&gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven].x1,
                              (s32*)&gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven].x2,
                              (s32*)&gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven].x3, &depth, &flag);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven]);
            } else {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven]);
            }
        }
    }
}

void drawMenu2_sub_sub3(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels[i] != 0) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->mDE0_saveMenuLabels[i].m0_polys[gMenuContext->mDE0_saveMenuLabels[i].m7D_oddOrEven]);
        }
    }
}

void drawMenu2_drawCheckingMemoryCard(void) {
    s32 i;
    MenuContext_4E0* p;
    s32 depth;
    s32 flag;

    if (gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText != 0) {
        for (i = 0; i < 3; i++) {
            p = gMenuContext->m1DE0_messageLines[i];
            if (p->m7F_transformEnabled != 0) {
                RotTransPers4(&p->m50_vertex[0], &p->m50_vertex[1], &p->m50_vertex[2], &p->m50_vertex[3],
                              (s32*)&p->m0_polys[p->m7D_oddOrEven].x0, (s32*)&p->m0_polys[p->m7D_oddOrEven].x1,
                              (s32*)&p->m0_polys[p->m7D_oddOrEven].x2, (s32*)&p->m0_polys[p->m7D_oddOrEven].x3, &depth, &flag);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m0_polys[p->m7D_oddOrEven]);
            } else {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m0_polys[p->m7D_oddOrEven]);
            }
        }
    }
}

void drawMenuBackground(void) {
}

void drawMenu2_sub(void) {
    drawMenu2_sub_sub0();
    drawMenu2_sub_sub1();
    drawMenu2_sub_sub2();
    drawMenu2_sub_sub3();
    drawMenu2_drawCheckingMemoryCard();
}

/* Noah drawMainMenu */
void drawMainMenu(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m9_drawMainMenu != 0) {
        if (gMenuContext->m350_mainMenu->m1192_dimmed != gMenuContext->m350_mainMenu->m1193_appliedDimmed) {
            if (gMenuContext->m350_mainMenu->m1192_dimmed != 0) {
                for (i = 0; i < gMenuContext->m350_mainMenu->m118C_numEntries; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 1);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0x20, 0x20, 0x20);
                }
                for (i = 0; i < gMenuContext->m350_mainMenu->m1188_numEntries2; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 1);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0x20, 0x20, 0x20);
                }
            } else {
                for (i = 0; i < gMenuContext->m350_mainMenu->m118C_numEntries; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0x80, 0x80, 0x80);
                }
                for (i = 0; i < gMenuContext->m350_mainMenu->m1188_numEntries2; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0x80, 0x80, 0x80);
                }
            }
            gMenuContext->m350_mainMenu->m1193_appliedDimmed = gMenuContext->m350_mainMenu->m1192_dimmed;
        }
        drawMultiPolys(gMenuContext->m350_mainMenu->m118C_numEntries, gMenuContext->m350_mainMenu->m8C0_polys[0], gMenuContext->m350_mainMenu->m1191_8C0OddOrEven);
        drawMultiPolys(gMenuContext->m350_mainMenu->m1188_numEntries2, gMenuContext->m350_mainMenu->m0_polys[0], gMenuContext->m350_mainMenu->m1190_0OddOrEven);
    }
}

/* Noah drawMenuContext354 */
void drawMenuContext354(void) {
    if (gMenuContext->m33C_drawFlags->mA_354Enabled != 0) {
        drawMultiPolys(gMenuContext->m354_subMenu->m1404_500Length, gMenuContext->m354_subMenu->m500_polys[0], gMenuContext->m354_subMenu->m1409_500OddOrEven);
        drawMultiPolys(gMenuContext->m354_subMenu->m1400_0Length, gMenuContext->m354_subMenu->m0_polys[0], gMenuContext->m354_subMenu->m1408_0OddOrEven);
    }
}

/* Noah FUN_Menu2__801d1aac (m444[2]) */
void drawMenuPointerGlyphs(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (gMenuContext->m33C_drawFlags->m50_draw444[i] != 0) {
            transformAndAddPrim(1, gMenuContext->m444_pointerGlyphs[i]->m50_vertex, gMenuContext->m444_pointerGlyphs[i]->m0_polys, gMenuContext->m444_pointerGlyphs[i]->m75_oddOrEven);
        }
    }
}

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
void drawMultiPolys(s32 count, POLY_FT4* polys, s32 oddOrEven);
void drawGearShopMechas(void);
void drawMenuBackground(void);

void drawMenuSpecificElements(void) {
    if (gMenuContext->m327_drawEnabled != 0) {
        drawMenu_380();
        updateGearShopEffects();
        drawMenu_428();
        drawMenu2_sub();
        drawMenu_348_cursor();
        drawMenuPointerGlyphs();
        drawShopMenu();
        drawCharacterSwitchArrows();
        drawScrollBarThumb();
        drawMainMenu();
        drawMenuContext354();
        drawMenuBoxes();
        drawGearShopPolys();
        drawMultiPolys(1, gMenuContext->m454_gearShop->m80_polys, gMenuContext->m454_gearShop->m1EE1_80OddOrEven);
        drawGearShopMechas();
    }
    drawMenuBackground();
}

/* Noah playMenuSoundEffect */
void playMenuSoundEffect(u8 sfx) {
    if (gMenuContext->m32A_soundEnabled) {
        playSoundEffect((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | sfx);
    }
}

extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField;
s32 isControllerConnected(s32 port);
void resumeMusic(void);
void pauseMusic(void);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void playMenuSoundEffect(u8 sfx);

void updateMenuInput(void) {
    u8 paused;
    u8 retry;
    s32 time;
    u8 sel;

    sel = 8;
#if !VERSION_JP
    retry = 1;
    paused = 0;
    do {
        if (isControllerConnected(0) == 0) {
            if (paused == 0) {
                pauseMusic();
                paused++;
                time = g_playTimeInVsync;
            }
        } else {
            if (paused != 0) {
                resumeMusic();
                g_playTimeInVsync = time;
            }
            retry--;
        }
    } while (retry);
#endif
    if (getInputOverflowed()) {
        resetInputs();
    } else {
        while (loadInputFromVSyncBuffer()) {
            if (newPadButtonForField & 0x2000) {
                sel = 0;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x4000) {
                sel = 1;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x8000) {
                sel = 2;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x1000) {
                sel = 3;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForDialogs & 0x20) {
                sel = 4;
                break;
            }
            if (newPadButtonForDialogs & 0x40) {
                sel = 5;
                playMenuSoundEffect(3);
                break;
            }
            if (newPadButtonForDialogs & 0x80) {
                sel = 6;
                break;
            }
            if (newPadButtonForDialogs & 0x10) {
                sel = 7;
                break;
            }
            if (newPadButtonForField & 4) {
                sel = 10;
                break;
            }
            if (newPadButtonForField & 8) {
                sel = 9;
                break;
            }
        }
    }
    gMenuContext->m325_menuButton = sel;
}


void initGearShopCameraTween(void) {
    u8 cont;
    u8 n0;
    u8 n1;
    s32 i;

    n1 = 0;
    n0 = 0;
    gearShopCameraTween.m24_offset[0] = gearShopCameraTween.m24_offset[1] = gearShopCameraTween.m24_offset[2] = 0;
    if (gearShopCameraTween.mC_to[0] >= gearShopCameraTween.m0_from[0]) {
        gearShopCameraTween.m18_step[0] = gearShopCameraTween.mC_to[0] - gearShopCameraTween.m0_from[0];
        gearShopCameraTween.m30_reversed[0] = 0;
    } else {
        gearShopCameraTween.m18_step[0] = gearShopCameraTween.mC_to[0] - gearShopCameraTween.m0_from[0];
        gearShopCameraTween.m30_reversed[0] = 1;
    }
    if (gearShopCameraTween.mC_to[1] >= gearShopCameraTween.m0_from[1]) {
        gearShopCameraTween.m18_step[1] = gearShopCameraTween.mC_to[1] - gearShopCameraTween.m0_from[1];
        gearShopCameraTween.m30_reversed[1] = 0;
    } else {
        gearShopCameraTween.m18_step[1] = gearShopCameraTween.mC_to[1] - gearShopCameraTween.m0_from[1];
        gearShopCameraTween.m30_reversed[1] = 1;
    }
    if (gearShopCameraTween.m18_step[0] < 0) {
        gearShopCameraTween.m18_step[0] = ~gearShopCameraTween.m18_step[0] + 1;
    }
    if (gearShopCameraTween.m18_step[1] < 0) {
        gearShopCameraTween.m18_step[1] = ~gearShopCameraTween.m18_step[1] + 1;
    }
    if (gearShopCameraTween.m18_step[0] >= gearShopCameraTween.m18_step[1]) {
        gearShopCameraTween.m18_step[1] = R3000_DIV(gearShopCameraTween.m18_step[1] << 16, gearShopCameraTween.m18_step[0]);
        gearShopCameraTween.m18_step[0] = 0x10000;
    } else {
        gearShopCameraTween.m18_step[0] = (gearShopCameraTween.m18_step[0] << 16) / gearShopCameraTween.m18_step[1];
        gearShopCameraTween.m18_step[1] = 0x10000;
    }
    cont = 1;
    do {
        for (i = 0; i < gearShopCameraTween.m33_stepsPerFrame; i++) {
            gearShopCameraTween.m24_offset[0] += gearShopCameraTween.m18_step[0];
        }
        if (gearShopCameraTween.m30_reversed[0] == 0) {
            if (gearShopCameraTween.m24_offset[0] / 0x10000 + gearShopCameraTween.m0_from[0] >= gearShopCameraTween.mC_to[0]) {
                cont = 0;
            } else {
                n0++;
            }
        } else {
            if (gearShopCameraTween.mC_to[0] >= gearShopCameraTween.m0_from[0] - gearShopCameraTween.m24_offset[0] / 0x10000) {
                cont = 0;
            } else {
                n0++;
            }
        }
    } while (cont);
    cont = 1;
    do {
        for (i = 0; i < gearShopCameraTween.m33_stepsPerFrame; i++) {
            gearShopCameraTween.m24_offset[1] += gearShopCameraTween.m18_step[1];
        }
        if (gearShopCameraTween.m30_reversed[1] == 0) {
            if (gearShopCameraTween.m24_offset[1] / 0x10000 + gearShopCameraTween.m0_from[1] >= gearShopCameraTween.mC_to[1]) {
                cont = 0;
            } else {
                n1++;
            }
        } else {
            if (gearShopCameraTween.mC_to[1] >= gearShopCameraTween.m0_from[1] - gearShopCameraTween.m24_offset[1] / 0x10000) {
                cont = 0;
            } else {
                n1++;
            }
        }
    } while (cont);
    if (n0 < n1) {
        n0 = n1;
    }
    if (gearShopCameraTween.mC_to[2] >= gearShopCameraTween.m0_from[2]) {
        gearShopCameraTween.m18_step[2] = gearShopCameraTween.mC_to[2] - gearShopCameraTween.m0_from[2];
        gearShopCameraTween.m30_reversed[2] = 0;
    } else {
        gearShopCameraTween.m18_step[2] = gearShopCameraTween.mC_to[2] - gearShopCameraTween.m0_from[2];
        gearShopCameraTween.m30_reversed[2] = 1;
    }
    if (gearShopCameraTween.m18_step[2] < 0) {
        gearShopCameraTween.m18_step[2] = ~gearShopCameraTween.m18_step[2] + 1;
    }
    gearShopCameraTween.m18_step[2] = R3000_DIV(gearShopCameraTween.m18_step[2] << 16, n0);
    gearShopCameraTween.m24_offset[2] = 0;
    gearShopCameraTween.m24_offset[1] = 0;
    gearShopCameraTween.m24_offset[0] = 0;
}

/* One-shot statement groups retain retail compiler allocation; t/d
 * carry the accumulator and the zero-extended halfword used by the store. */
void updateGearShopCameraTween(void) {
    MenuContext* ctx;
    sMechaBone* bones;
    s32 i;
    u8* p;
    s32 a;
    s32 v;
    s32 t;
    s32 d;

    if (gMenuContext->m329_animationState & 1) {
        for (i = 0; i < gearShopCameraTween.m33_stepsPerFrame; i++) {
            gearShopCameraTween.m24_offset[0] += gearShopCameraTween.m18_step[0];
        }
        if (gearShopCameraTween.m30_reversed[0] == 0) {
            a = gearShopCameraTween.m24_offset[0];
            v = a / 0x10000 + gearShopCameraTween.m0_from[0];
            t = gearShopCameraTween.mC_to[0];
            if (v >= t) {
                gMenuContext->m220_menuTranslation2.vz = t;
                gMenuContext->m329_animationState &= 6;
            } else {
                gMenuContext->m220_menuTranslation2.vz = v;
            }
        } else {
            a = gearShopCameraTween.m24_offset[0];
            v = gearShopCameraTween.m0_from[0] - a / 0x10000;
            t = gearShopCameraTween.mC_to[0];
            if (t >= v) {
                gMenuContext->m220_menuTranslation2.vz = t;
                gMenuContext->m329_animationState &= 6;
            } else {
                gMenuContext->m220_menuTranslation2.vz = v;
            }
        }
    }
    if (gMenuContext->m329_animationState & 2) {
        for (i = 0; i < gearShopCameraTween.m33_stepsPerFrame; i++) {
            gearShopCameraTween.m24_offset[1] += gearShopCameraTween.m18_step[1];
        }
        if (gearShopCameraTween.m30_reversed[1] == 0) {
            a = gearShopCameraTween.m24_offset[1];
            v = a / 0x10000 + gearShopCameraTween.m0_from[1];
            t = gearShopCameraTween.mC_to[1];
            if (v >= t) {
                gMenuContext->m220_menuTranslation2.vy = t;
                gMenuContext->m329_animationState &= 5;
            } else {
                gMenuContext->m220_menuTranslation2.vy = v;
            }
        } else {
            a = gearShopCameraTween.m24_offset[1];
            v = gearShopCameraTween.m0_from[1] - a / 0x10000;
            t = gearShopCameraTween.mC_to[1];
            if (t >= v) {
                gMenuContext->m220_menuTranslation2.vy = t;
                gMenuContext->m329_animationState &= 5;
            } else {
                gMenuContext->m220_menuTranslation2.vy = v;
            }
        }
    }
    do { do { ctx = gMenuContext; } while (0); } while (0);
    if (ctx->m329_animationState & 4) {
        do {
            p = (u8*)&gearShopCameraTween + 0x2C;
        } while (0);
        t = *(s32*)p + ((GearShopRotation*)(p - 0x2C))->m18_step[2];
        *(s32*)p = t;
        if (((u8*)p)[6] == 0) {
            a = t / 0x10000;
            if (a + gearShopCameraTween.m0_from[2] >= ((GearShopRotation*)(p - 0x2C))->mC_to[2]) {
                loadedMechas[1]->m4_bones->m54_rotationAngles.vy = ((GearShopRotation*)(p - 0x2C))->mC_to[2];
                ctx->m329_animationState &= 3;
            } else {
                do { bones = loadedMechas[1]->m4_bones; } while (0);
                t = (u16)gearShopCameraTween.m0_from[2];
                d = (u16)t;
                bones->m54_rotationAngles.vy = a + d;
            }
        } else {
            a = t / 0x10000;
            if (((GearShopRotation*)(p - 0x2C))->mC_to[2] >= gearShopCameraTween.m0_from[2] - a) {
                loadedMechas[1]->m4_bones->m54_rotationAngles.vy = ((GearShopRotation*)(p - 0x2C))->mC_to[2];
                ctx->m329_animationState &= 3;
            } else {
                do { bones = loadedMechas[1]->m4_bones; } while (0);
                t = (u16)gearShopCameraTween.m0_from[2];
                d = (u16)t;
                bones->m54_rotationAngles.vy = d - a;
            }
        }
    }
}

void updateGearShopCameraTween(void);

void updateMenuTransforms(void) {
    updateGearShopCameraTween();
    createRotationMatrix(&gMenuContext->m218_menuRotation2, &gMenuContext->m230_gearMatrix);
    TransMatrix(&gMenuContext->m230_gearMatrix, &gMenuContext->m220_menuTranslation2);
    SetRotMatrix(&gMenuContext->m230_gearMatrix);
    SetTransMatrix(&gMenuContext->m230_gearMatrix);
    createRotationMatrix(&gMenuContext->m1D8_menuRotation, &gMenuContext->m1F0_menuMatrix);
    TransMatrix(&gMenuContext->m1F0_menuMatrix, &gMenuContext->m1E0_menuTranslation);
    SetRotMatrix(&gMenuContext->m1F0_menuMatrix);
    SetTransMatrix(&gMenuContext->m1F0_menuMatrix);
}

extern u8 gearShopDebugValuesEnabled;
extern POLY_FT4 gearShopDebugPolys[][2];
extern s32 gearShopDebugPolyCount;

void drawGearShopDebugValues(void) {
    s32 values[6];
    s32 i;
    s32 j;
    s32 count;
    s32 value;

    if (gearShopDebugValuesEnabled != 0 && gMenuContext->m458_files[1]->m12_loaded != 0) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x21, gearShopDebugPolys[0], gMenuContext->m308_oddOrEven, 0x10, 0x10, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x22, gearShopDebugPolys[1], gMenuContext->m308_oddOrEven, 0x10, 0x20, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x23, gearShopDebugPolys[2], gMenuContext->m308_oddOrEven, 0x10, 0x30, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0xA, gearShopDebugPolys[3], gMenuContext->m308_oddOrEven, 0x10, 0x40, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x10, gearShopDebugPolys[4], gMenuContext->m308_oddOrEven, 0x10, 0x50, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x1C, gearShopDebugPolys[5], gMenuContext->m308_oddOrEven, 0x10, 0x60, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE3, gearShopDebugPolys[6], gMenuContext->m308_oddOrEven, 0xA0, 0x64, 0x1000);
        values[0] = gMenuContext->m220_menuTranslation2.vx;
        values[1] = gMenuContext->m220_menuTranslation2.vy;
        values[2] = gMenuContext->m220_menuTranslation2.vz;
        values[3] = loadedMechas[1]->m4_bones->m54_rotationAngles.vy;
        values[4] = loadedMechas[1]->m60_groundY;
        values[5] = loadedMechas[1]->m1C_scale;
        gearShopDebugPolyCount = 7;
        for (i = 0; i < 6; i++) {
            count = 0;
            value = values[i];
            if (value < 0) {
                gearShopDebugPolyCount += setupStringInPolyFT4(gMenuContext->m2DC_font, 0xE5, gearShopDebugPolys[gearShopDebugPolyCount],
                                            gMenuContext->m308_oddOrEven, 0x30, i * 16 + 16, 0x1000);
                count = 1;
                value = ~values[i] + 1;
            }
            fillStringDisplayBuffer(value);
            for (j = 0; j < 9; j++) {
                if (gMenuContext->m31C_stringDisplayBuffer[j] != 0xFF) {
                    gearShopDebugPolyCount += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[j],
                                                gearShopDebugPolys[gearShopDebugPolyCount], gMenuContext->m308_oddOrEven, count * 8 + 0x30,
                                                i * 16 + 16, 0x1000);
                    count++;
                }
            }
        }
        drawMultiPolys(gearShopDebugPolyCount, gearShopDebugPolys[0], gMenuContext->m308_oddOrEven);
    }
}

void updateMenuInput(void);
void checkSoftReboot(void);
void updateMenuTransforms(void);
void drawGearShopDebugValues(void);
void drawMenuSpecificElements(void);
void AddPrims(void* ot, void* p0, void* p1);
extern s32* pRunningOnDTL;

void menuDraw(void) {
    MenuContext* ctx;

    if (*pRunningOnDTL != -1) {
        POLLHOST();
    }
    updateMenuInput();
    checkSoftReboot();
    ctx = gMenuContext;
    ctx->m1D4_currentDrawContext = (ctx->m1D4_currentDrawContext == &ctx->m6C_drawContexts[0])
                                       ? &ctx->m6C_drawContexts[1]
                                       : &ctx->m6C_drawContexts[0];
    ctx->m308_oddOrEven = ctx->m308_oddOrEven == 0;
    ClearOTagR(ctx->m1D4_currentDrawContext->m70_ot, 0x10);
    ClearOTagR((OTag*)gMenuContext->m1D4_currentDrawContext->mB0, 0x400);
    updateMenuTransforms();
    drawGearShopDebugValues();
    drawMenuSpecificElements();
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&gMenuContext->m1D4_currentDrawContext->m0_drawEnv);
    PutDispEnv(&gMenuContext->m1D4_currentDrawContext->m5C_dispEnv);
    ClearImage(&gMenuContext->m1D4_currentDrawContext->m0_drawEnv.clip, 0, 0, 0);
    AddPrims((OTag*)gMenuContext->m1D4_currentDrawContext->mB0, &gMenuContext->m1D4_currentDrawContext->m70_ot[15],
             gMenuContext->m1D4_currentDrawContext->m70_ot);
    menuWideLayout(&((OTag*)gMenuContext->m1D4_currentDrawContext->mB0)[0x3FF]);
    DrawOTag(&((OTag*)gMenuContext->m1D4_currentDrawContext->mB0)[0x3FF]);
}

extern s32 gearShopCardCursorStartX[];
extern s32 gearShopCardCursorStartY[];

void setupDraw428(u8 mode) {
    s32 i;

    gMenuContext->m428_cardCursors = allocateHeap((s32)sizeof(struct MenuContext_428), 0);
    bzero(gMenuContext->m428_cardCursors, (s32)sizeof(struct MenuContext_428));
    switch (mode) {
    case 0:
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
        gMenuContext->m428_cardCursors->m144_followTile[1] = 1;
    case 2:
        for (i = 0; i < 4; i++) {
            setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys + i,
                          gMenuContext->m308_oddOrEven, gearShopCardCursorStartX[i], gearShopCardCursorStartY[i], 0x800);
            gMenuContext->m428_cardCursors->m148_oddOrEven[i] = gMenuContext->m308_oddOrEven;
        }
        break;
    case 3:
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys, gMenuContext->m308_oddOrEven,
                      0, 0, 0x800);
        gMenuContext->m428_cardCursors->m148_oddOrEven[0] = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        break;
    case 1:
        break;
    }
}

void menuDraw(void);

void disableDraw428() {
    gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
    menuDraw();
    freeHeap(gMenuContext->m428_cardCursors);
}

void processLoadSaveMenuSub1(void) {
}

void processLoadSaveMenuSub1Alt2(void) {
}

void menuDraw(void);

/* gear shop variant of Noah processLoadSaveMenuSub5 (main func_801D2F4C): open
 * menu box 4 and show text lines msg..msg+2 */
void processLoadSaveMenuSub5(u8 msg) {
    MenuContext_380* box;
    MenuContext_4E0* p;
    s32 i;
    s32 x;

    iniMenuContext364And380(4, 0x42, 0x46, 0xC0, 0x40, 1, 1, 4, 0);
    box = gMenuContext->m380_menuBoxAnims[4];
    x = 0x50;
    while (!box->m11_done) {
        menuDraw();
    }
    for (i = 0; i < 4; i++) {
        gMenuContext->m1DE0_messageLines[i] = allocateHeap((s32)sizeof(MenuContext_4E0), 0);
        bzero(gMenuContext->m1DE0_messageLines[i], (s32)sizeof(MenuContext_4E0));
        if ((i & 1) == 0) {
            gMenuContext->m1DE0_messageLines[i]->m78_imageData = ALLOC_TEXT_IMAGE(0x5CA);
            gMenuContext->m1DE0_messageLines[i]->m70_rect.x = 0x140;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.y = (i / 2) * 0xD + 0x4E;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.w = 0x3A;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.h = 0xD;
        } else {
            gMenuContext->m1DE0_messageLines[i]->m78_imageData = gMenuContext->m1DE0_messageLines[i - 1]->m78_imageData;
        }
    }
    for (i = 0; i < 3; i++) {
        p = gMenuContext->m1DE0_messageLines[i];
        p->m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, msg + i), p->m78_imageData, 0x36, i - (i / 2) * 2);
        initMenuContext_1DE0(p, i, 0, 0);
        initMemoryCardPolyVerts(p->m50_vertex, x, i * 16 + 0x50, p->m7E_stringWidth, 0xD);
        setUVWH(&p->m0_polys[gMenuContext->m308_oddOrEven], 0, (i / 2) * 0xD + 0x4E, p->m7E_stringWidth, 0xD);
        p->m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        p->m7F_transformEnabled = 1;
    }
    LoadImage(&gMenuContext->m1DE0_messageLines[0]->m70_rect, gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    LoadImage(&gMenuContext->m1DE0_messageLines[2]->m70_rect, gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    DrawSync(0);
    gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 1;
    freeHeap(gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    freeHeap(gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    if (gMenuContext->m33C_drawFlags->m5A_drawShop[1] == 2) {
        gMenuContext->m33C_drawFlags->m5A_drawShop[1] = 1;
    }
    menuDraw();
    menuDraw();
}

void processLoadSaveMenuSub7Sub0(u8 index);
void menuDraw(void);

void processLoadSaveMenuSub7(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[4] != 0) {
        processLoadSaveMenuSub7Sub0(4);
        gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 0;
        for (i = 0; i < 4; i++) {
            freeHeap(gMenuContext->m1DE0_messageLines[i]);
        }
    }
    gMenuContext->m33C_drawFlags->m5A_drawShop[1] = 0;
    menuDraw();
}

/* yes/no prompt: m428 cursors 2/3; returns 1 for yes */
u8 runYesNoPrompt(u8 wait, u8 enabled) {
    u8 loop;
    u8 ret;
    u8 timer;

    loop = 1;
    ret = 0;
    timer = 0x3C;
    do {
        if (!wait) {
            gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
            gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
            if (gMenuContext->m325_menuButton != 8 || --timer == 0) {
                break;
            }
        }
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 4:
            playMenuSoundEffect(2);
            loop = 0;
            break;
        case 5:
            ret = 0;
            loop = 0;
            break;
        case 2:
            if (enabled) {
                gMenuContext->m428_cardCursors->m140_enabled[2] = 1;
                ret = 1;
                gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
            }
            break;
        case 0:
            if (enabled) {
                gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
                ret = 0;
                gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
            }
            break;
        }
    } while (loop);
    gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
    gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
    return ret;
}

u8 askYesNoQuestion(u8 a, u8 b, u8 c) {
    u8 flag;
    u8 ret;

    flag = 1;
    processLoadSaveMenuSub5(a);
    if (b == 0xFF) {
        gMenuContext->m428_cardCursors->m140_enabled[3] = flag;
    } else {
        gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
        flag = 0;
        gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
    }
    ret = runYesNoPrompt(c, flag);
    processLoadSaveMenuSub7();
    if (b != 0xFF) {
        gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
        processLoadSaveMenuSub5(b);
        gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
        ret = runYesNoPrompt(c, 1);
        processLoadSaveMenuSub7();
    }
    return ret;
}

/* gear shop teardown (cf. menu func_801C5FE4) */
void shutdownGearShopMenu(void) {
    menuDraw();
    menuDraw();
    gMenuContext->m327_drawEnabled = 0;
    menuDraw();
    do {
        menuDraw();
    } while (gMenuContext->m308_oddOrEven != 0);
    initMenuContext32C(0);
    initMenuContext33C(0);
    initMenuContext350MainMenu(0);
    initMenuContext354(0);
    initMenuContext330(0);
    initMenuContext348(0);
    loadGearShopData(0x10);
    initMenuContext450Shop(0);
    initMenuContext454GearShop(0);
    freeHeap(gMenuContext->m2DC_font);
    freeHeap(gMenuContext->m2E0_textBundle);
    freeHeap(gMenuContext->m4E0_labels[0].m78_imageData);
    if (useDebugMenuList) {
        stopSequence(gMenuContext->m2E4_musicSequence);
        menuDraw();
        unloadSequence(gMenuContext->m2E4_musicSequence);
        menuDraw();
        freeHeap(gMenuContext->m2E4_musicSequence);
    }
    initMenuContext1E20(0);
    freeHeap(gMenuContext);
}

/* j_setupMenuContext4E0 with startY 2 (= menu func_801E8018) */
void j_setupMenuContext4E0(u8 count, MenuContext_4E0* p, void* config, u8* flags) {
    setupMenuContext4E0(p, config, 2, count);
}

/* Noah countField33C_C (clear a flag array) */
void countField33C_C(u8 count, u8* arr) {
    s32 i;

    for (i = 0; i < count; i++) {
        arr[i] = 0;
    }
}

/* Noah updateMenuSelection2 (menu func_801E8070 with modes 0 and 1 only) */
void updateMenuSelection2(s32 count, MenuContext_4E0* labels, void* unused, s32* xOffsets, u8* flags, u8 sel, u8 base,
                   u8 mode) {
    switch (mode) {
    case 0:
        countField33C_C(count, flags);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x0 = gearShopCursorPosXTable[base + sel] + (s16)(xOffsets[sel] + 0x16);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y0 = gearShopCursorPosYTable[base + sel] - 0x22;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x1 = labels[sel].m7E_stringWidth + (gearShopCursorPosXTable[base + sel] + 0x16 + xOffsets[sel]);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y1 = gearShopCursorPosYTable[base + sel] - 0x22;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x2 = gearShopCursorPosXTable[base + sel] + (s16)(xOffsets[sel] + 0x16);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y2 = gearShopCursorPosYTable[base + sel] - 0x15;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x3 = labels[sel].m7E_stringWidth + (gearShopCursorPosXTable[base + sel] + 0x16 + xOffsets[sel]);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y3 = gearShopCursorPosYTable[base + sel] - 0x15;
        break;
    case 1:
        if (sel) {
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0xCE;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y0 = 0x7E;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x1 = labels->m7E_stringWidth + 0xCE;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y1 = 0x7E;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0xCE;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y2 = 0x8B;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x3 = labels->m7E_stringWidth + 0xCE;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y3 = 0x8B;
        } else {
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0xEC;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y0 = 0x7E;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x1 = labels->m7E_stringWidth + 0xEC;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y1 = 0x7E;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0xEC;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y2 = 0x8B;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x3 = labels->m7E_stringWidth + 0xEC;
            (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y3 = 0x8B;
        }
        break;
    }
    labels[sel].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    flags[sel] = 1;
}

/* Noah setupMainMenuWithConfig */
void setupMainMenuWithConfig(s32 count, u32 (*config)[2]) {
    s32 i;
    s32 k;

    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 0;
    gMenuContext->m33C_drawFlags->m9_drawMainMenu = 1;
    for (i = 1; i <= count; i++) {
        if (i != count) {
            gMenuContext->m350_mainMenu->m1188_numEntries2 = 0;
            for (k = 0; k < i; k++) {
                gMenuContext->m350_mainMenu->m1188_numEntries2 += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, config[k][0],
                    &gMenuContext->m350_mainMenu->m0_polys[gMenuContext->m350_mainMenu->m1188_numEntries2],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            gMenuContext->m350_mainMenu->m1190_0OddOrEven = gMenuContext->m308_oddOrEven;
        }
        gMenuContext->m350_mainMenu->m118C_numEntries = 0;
        if (i != 1) {
            for (k = 0; k < i - 1; k++) {
                gMenuContext->m350_mainMenu->m118C_numEntries += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, config[k][1],
                    &gMenuContext->m350_mainMenu->m8C0_polys[gMenuContext->m350_mainMenu->m118C_numEntries],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            gMenuContext->m350_mainMenu->m1191_8C0OddOrEven = gMenuContext->m308_oddOrEven;
        }
        for (k = 0; k < 2; k++) {
            menuDraw();
        }
    }
}

/* open the submenu: draw its entries one more per step (cf. menu func_801E86C8) */
void processLoadSaveMenuSub3(u8 row) {
    MenuContext* ctx;
    s32 draw;
    s32 n;
    s32 i;

    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    draw = 1;
    n = 1;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 0;
    ctx = gMenuContext;
    gMenuContext->m354_subMenu->m1400_0Length = 0;
    ctx->m354_subMenu->m1404_500Length = 0;
    ctx->m33C_drawFlags->mA_354Enabled = 1;
    do {
        ctx = gMenuContext;
        gMenuContext->m354_subMenu->m1400_0Length = 0;
        ctx->m33A_numMenuEntries = 0;
        for (i = 0; i < n; i++) {
            if (gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] != 0xFFFF) {
                gMenuContext->m354_subMenu->m1400_0Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2],
                    &gMenuContext->m354_subMenu->m0_polys[gMenuContext->m354_subMenu->m1400_0Length], gMenuContext->m308_oddOrEven,
                    0xA0, 0x96, 0x1000);
                gMenuContext->m33A_numMenuEntries++;
            } else {
                draw = 0;
            }
        }
        gMenuContext->m354_subMenu->m1408_0OddOrEven = gMenuContext->m308_oddOrEven;
        if (draw) {
            for (i = 0; i < 2; i++) {
                menuDraw();
            }
        }
        gMenuContext->m354_subMenu->m1404_500Length = 0;
        i = 0;
        if (i < n) {
        loop2:
            if (gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] != 0xFFFF) {
                gMenuContext->m354_subMenu->m1404_500Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2 + 1],
                    &gMenuContext->m354_subMenu->m500_polys[gMenuContext->m354_subMenu->m1404_500Length],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            i++;
            if (i < n) goto loop2;
        }
        gMenuContext->m354_subMenu->m1409_500OddOrEven = gMenuContext->m308_oddOrEven;
        if (draw) {
            for (i = 0; i < 2; i++) {
                menuDraw();
            }
        }
        n++;
    } while (n < 5);
}

/* Noah updateMenuSelection */
void updateMenuSelection(u8 count, u8 sel, u32 (*config)[2]) {
    s32 i;
    s32 glyph;

    gMenuContext->m350_mainMenu->m1188_numEntries2 = 0;
    gMenuContext->m350_mainMenu->m118C_numEntries = 0;
    for (i = 0; i < count; i++) {
        if (i == sel) {
            glyph = config[i][0] + 0xD;
        } else {
            glyph = config[i][0];
        }
        gMenuContext->m350_mainMenu->m1188_numEntries2 += setupStringInPolyFT4(
            gMenuContext->m2DC_font, glyph,
            &gMenuContext->m350_mainMenu->m0_polys[gMenuContext->m350_mainMenu->m1188_numEntries2],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
        gMenuContext->m350_mainMenu->m118C_numEntries += setupStringInPolyFT4(
            gMenuContext->m2DC_font, config[i][1],
            &gMenuContext->m350_mainMenu->m8C0_polys[gMenuContext->m350_mainMenu->m118C_numEntries],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
    }
    gMenuContext->m350_mainMenu->m1190_0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m350_mainMenu->m1191_8C0OddOrEven = gMenuContext->m308_oddOrEven;
    updateCursorPolysLocation(sel, 1);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
}

/* redraw the submenu entries with the selected one highlighted (cf. menu func_801E8B4C) */
void processLoadSaveMenuSub8(u8 row) {
    s32 i;
    s32 glyph;

    gMenuContext->m354_subMenu->m1400_0Length = 0;
    gMenuContext->m354_subMenu->m1404_500Length = 0;
    for (i = 0; i < gMenuContext->m33A_numMenuEntries; i++) {
        if (i == gMenuContext->m338_currentSelectedSubMenuEntry) {
            glyph = gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] + 0xD;
        } else {
            glyph = gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2];
        }
        gMenuContext->m354_subMenu->m1400_0Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, glyph, &gMenuContext->m354_subMenu->m0_polys[gMenuContext->m354_subMenu->m1400_0Length],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
        gMenuContext->m354_subMenu->m1404_500Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, gearShopSubMenuConfig[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2 + 1],
            &gMenuContext->m354_subMenu->m500_polys[gMenuContext->m354_subMenu->m1404_500Length], gMenuContext->m308_oddOrEven,
            0xA0, 0x96, 0x1000);
    }
    gMenuContext->m354_subMenu->m1408_0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m354_subMenu->m1409_500OddOrEven = gMenuContext->m308_oddOrEven;
    updateCursorPolysLocation(gMenuContext->m338_currentSelectedSubMenuEntry + 4, 1);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
}
