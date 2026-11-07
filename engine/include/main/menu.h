#ifndef MAIN_MENU_H
#define MAIN_MENU_H

/* Menu drawing context (Noah: menuHandler.cpp). */

#include "common.h"
#include "psyq.h"
#include "main/sound.h"

/* Noah: sMenuContext_1D4 (one display buffer), size 0xB4 */
typedef struct {
    DRAWENV m0_drawEnv;
    DISPENV m5C_dispEnv;
    OTag m70_ot[16];
    OTag* mB0;
} MenuDrawContext;

/* Sub-structures owned by the menu overlay (include/menu/menu.h). */
struct MenuContext_32C;
struct MenuContext_330;
struct MenuContext_33C;
struct MenuContext_340;
struct MenuContext_344;
struct MenuContext_348;
struct MenuContext_34C;
struct MenuContext_350;
struct MenuContext_354;
struct MenuContext_358;
struct MenuContext_35C;
struct MenuContext_360;
struct MenuContext_364;
struct MenuContext_380;
struct MenuContext_39C;
struct MenuContext_3A8;
struct MenuContext_428;
struct MenuContext_42C;
struct MenuContext_430;
struct MenuContext_434;
struct MenuContext_438;
struct MenuContext_43C;
struct MenuContext_440;
struct MenuContext_444;
struct MenuContext_44C;
struct MenuContext_1E08;
struct MenuShop;
struct MenuGearShop;
struct MenuShopFiles;
struct MenuShopList;
struct MenuContext_1E20; /* shop, gear shop, menu_name_entry (menu/shop.h) */

/* Noah: sMenuContextSub0, an info card slide animation; size 0x24 */
typedef struct {
    /* 0x00 */ s32 m0_startX;  /* Noah: m0 */
    /* 0x04 */ s32 m4_endX;    /* Noah: m4 */
    /* 0x08 */ s32 m8_startY;  /* Noah: m8 */
    /* 0x0C */ s32 mC_endY;    /* Noah: mC */
    /* 0x10 */ s32 m10_XStep;
    /* 0x14 */ s32 m14_YStep;
    /* 0x18 */ s32 m18_XOffset;
    /* 0x1C */ s32 m1C_YOffset;
    /* 0x20 */ u8 m20_XReversed;
    /* 0x21 */ u8 m21_YReversed;
    /* 0x22 */ u8 m22_numSteps;
    /* 0x23 */ u8 m23_isAnimationDone;
} MenuContextSub0;

/* Noah: sMenuContext_4E0, a text label rendered to VRAM; size 0x80 */
typedef struct {
    /* 0x00 */ POLY_FT4 m0_polys[2];
    /* 0x50 */ SVECTOR m50_vertex[4];
    /* 0x70 */ RECT m70_rect;
    /* 0x78 */ u16* m78_imageData;
    /* 0x7C */ u8 m7C_colorMode;
    /* 0x7D */ u8 m7D_oddOrEven;
    /* 0x7E */ u8 m7E_stringWidth;
    /* 0x7F */ u8 m7F_transformEnabled;
} MenuContext_4E0;

/* Noah: sMenuContext (gMenuContext, pointer at gMenuContext); 0x1E98 bytes, partial. */
typedef struct {
    /* 0x0000 */ MenuContextSub0 m0_infoCardAnimations[3];
    /* 0x006C */ MenuDrawContext m6C_drawContexts[2];
    /* 0x01D4 */ MenuDrawContext* m1D4_currentDrawContext;
    /* 0x01D8 */ SVECTOR m1D8_menuRotation;
    /* 0x01E0 */ VECTOR m1E0_menuTranslation;
    /* 0x01F0 */ MATRIX m1F0_menuMatrix;
    /* 0x0210 */ u8 m210[0x218 - 0x210];
    /* 0x0218 */ SVECTOR m218_menuRotation2;
    /* 0x0220 */ VECTOR m220_menuTranslation2;
    /* 0x0230 */ MATRIX m230_gearMatrix;   /* gear shop: renderMechas matrix */
    /* 0x0250 */ u8 m250[0x298 - 0x250];
    /* 0x0298 */ MATRIX m298_lightMatrix;  /* gear shop: renderMechas second matrix (func_801D5D38 sets it) */
    /* 0x02B8 */ MATRIX m2B8_colorMatrix;  /* gear shop: 0x600 diagonal (func_801D5D38) */
    /* 0x02D8 */ u32 m2D8_frameCounter; /* compared unsigned (sltu) */
    /* 0x02DC */ void* m2DC_font;
    /* 0x02E0 */ u8* m2E0_textBundle;
    /* 0x02E4 */ SeqFile* m2E4_musicSequence;
    /* 0x02E8 */ s32 m2E8;
    /* 0x02EC */ u32 m2EC_playTime[7]; /* Noah: m302_playTimeString (100h, 10h, h, 10min, min, 10s, s) */
    /* 0x0308 */ s32 m308_oddOrEven;
    /* 0x030C */ u8 m30C_characterAvailable[16];
    /* 0x031C */ u8 m31C_stringDisplayBuffer[9];
    /* 0x0325 */ u8 m325_menuButton;
    /* 0x0326 */ u8 m326_cardPollTimer;
    /* 0x0327 */ u8 m327_drawEnabled;
    /* 0x0328 */ u8 m328;
    /* 0x0329 */ u8 m329_animationState;
    /* 0x032A */ u8 m32A_soundEnabled;
    /* 0x032B */ u8 m32B_numPartyMembers;
    /* 0x032C */ struct MenuContext_32C* m32C_memoryCardContext;
    /* 0x0330 */ struct MenuContext_330* m330_itemData;
    /* 0x0334 */ u8 m334_cardOperation;
    /* 0x0335 */ u8 m335;
    /* 0x0336 */ u8 m336_menuSelectedEntry;
    /* 0x0337 */ u8 m337_previousMenuSelectedEntry;
    /* 0x0338 */ u8 m338_currentSelectedSubMenuEntry;
    /* 0x0339 */ u8 m339_previousSelectedSubMenuEntry;
    /* 0x033A */ u8 m33A_numMenuEntries;
    /* 0x033B */ u8 m33B_numGears;
    /* 0x033C */ struct MenuContext_33C* m33C_drawFlags;
    /* 0x0340 */ struct MenuContext_340* m340_gold;
    /* 0x0344 */ struct MenuContext_344* m344_playTime;
    /* 0x0348 */ struct MenuContext_348* m348_cursor;
    /* 0x034C */ struct MenuContext_34C* m34C_saveScreen;
    /* 0x0350 */ struct MenuContext_350* m350_mainMenu;
    /* 0x0354 */ struct MenuContext_354* m354_subMenu;
    /* 0x0358 */ struct MenuContext_358* m358_selectedInfoCard;
    /* 0x035C */ struct MenuContext_35C* m35C_selectedStats;
    /* 0x0360 */ struct MenuContext_360* m360_equipment;
    /* 0x0364 */ struct MenuContext_364* m364_menuBoxes[7];
    /* 0x0380 */ struct MenuContext_380* m380_menuBoxAnims[7];
    /* 0x039C */ struct MenuContext_39C* m39C_infoCards[3];
    /* 0x03A8 */ struct MenuContext_3A8* m3A8_memoryCardTiles[0x20];
    /* 0x0428 */ struct MenuContext_428* m428_cardCursors;
    /* 0x042C */ struct MenuContext_42C* m42C_itemMenu;
    /* 0x0430 */ struct MenuContext_430* m430_etherMenu;
    /* 0x0434 */ struct MenuContext_434* m434_equipMenu;
    /* 0x0438 */ struct MenuContext_438* m438_skillMenu;
    /* 0x043C */ struct MenuContext_43C* m43C_scrollBarThumb;
    /* 0x0440 */ struct MenuContext_440* m440_arrows;
    /* 0x0444 */ struct MenuContext_444* m444_pointerGlyphs[2];
    /* 0x044C */ struct MenuContext_44C* m44C_progressBox;
    /* 0x0450 */ struct MenuShop* m450_shop;         /* shops (0x4788 bytes) */
    /* 0x0454 */ struct MenuGearShop* m454_gearShop; /* gear shop (0x1F00 bytes) */
    /* 0x0458 */ struct MenuShopFiles* m458_files[2];
    /* 0x0460 */ u8 m460[0x46C - 0x460];
    /* 0x046C */ s32 m46C_menuBorders[4][6];
    /* 0x04CC */ s32 m4CC_iconFrame;
    /* 0x04D0 */ s32 m4D0_iconFrameTimer;
    /* 0x04D4 */ s32 m4D4_fadeColor;
    /* 0x04D8 */ u8 m4D8_cardTilesMode;
    /* 0x04D9 */ u8 m4D9_fadeDirection;
    /* 0x04DA */ u8 m4DA[2];
    /* 0x04DC */ u8 m4DC_currentlySelectedCharacter;
    /* 0x04DD */ u8 m4DD[3];
    /* 0x04E0..0x1DE0: Noah m4E0[50]. Retail indexes each group as its own
     * array (the labels drawn for each MenuContext_33C flag array), so they
     * are split. */
    /* 0x04E0 */ MenuContext_4E0 m4E0_labels[4];  /* m33C->m34[i] */
    /* 0x06E0 */ MenuContext_4E0 m6E0_mainMenuLabels[8];  /* m33C->mC[i] */
    /* 0x0AE0 */ MenuContext_4E0 mAE0_subMenuLabels[6];  /* m33C->m14[i] (submenu labels) */
    /* 0x0DE0 */ MenuContext_4E0 mDE0_saveMenuLabels[6];  /* m33C->m1A[i] */
    /* 0x10E0 */ MenuContext_4E0 m10E0_listMenuLabels[8]; /* m33C->m38[i] (item menu labels) */
    /* 0x14E0 */ MenuContext_4E0 m14E0_equipMenuLabels[6]; /* m33C->m40[i] (equip menu labels) */
    /* 0x17E0 */ MenuContext_4E0 m17E0_skillPageLabels[2]; /* m33C->m4E[i] (skill page labels) */
    /* 0x18E0 */ MenuContext_4E0 m18E0_gearMenuLabels[6]; /* m33C->m54[i] */
    /* 0x1BE0 */ MenuContext_4E0 m1BE0_soundMenuLabels[4]; /* m33C->m5C[i] */
    /* 0x1DE0 */ MenuContext_4E0* m1DE0_messageLines[4];
    /* 0x1DF0 */ struct MenuContext_1E08* m1DF0_reserveCards[6]; /* party menu: reserve member cards (MF6) */
    /* 0x1E08 */ struct MenuContext_1E08* m1E08_partyCards[3]; /* party info cards (item/equip target) */
    /* 0x1E14 */ u8 m1E14_reserve[11]; /* party menu: characters not in the party, 0xFF-terminated (MF6) */
    /* 0x1E1F */ u8 m1E1F;
    /* 0x1E20 */ struct MenuContext_1E20* m1E20_nameEntry; /* shop, gear shop, menu_name_entry (0xDEC bytes; menu/shop.h) */
    /* 0x1E24 */ u8* m1E24_nameSlotTable;  /* menu_name_entry: decompressed name-slot table (bundle[6]), freed after init (func_801C5318, MF5) */
    /* 0x1E28 */ u8 m1E28_nameSlots[3]; /* menu_name_entry: the three name slots (gameState.m0_names index) the typed name is written to (MF6) */
    /* 0x1E2B */ u8 m1E2B;
    /* 0x1E2C */ struct MenuShopList* m1E2C_shopLists; /* shop: stock lists (item file bundle[7]; func_801C54B4 loads it, the teardown frees it) (MF4) */
    /* 0x1E30 */ u8 m1E30_shopItemIds[48];   /* shop: the items on sale; gear shop: per list entry, the part/item id */
    /* 0x1E60 */ u8 m1E60_shopItemKinds[48]; /* shop: 0 inventory 1, 1 inventory 2, 2 inventory 0; gear shop: 0 armor, 1 frame, 2 engine, 3 accessory (inventory 4), 4 weapon (inventory 3) */
    /* 0x1E90 */ u8* m1E90_shopRecords; /* gear shop: menu bundle entry 8, 100-byte shop records (func_801C56C8, func_801C6E74) */
    /* 0x1E94 */ u8 m1E94_showMcbLog;
    /* 0x1E95 */ u8 m1E95_mcbLogPage;
    /* 0x1E96 */ u8 m1E96[2];
} MenuContext;

/* The gear shop's two ordering tables (its draw contexts' mB0): game_init.c
 * allocates them for the debug menu list, the field points them at its own.
 * An array: the gear shop's code only matches with one symbol for both. */
extern OTag* gearShopOrderingTables[2];

#endif /* MAIN_MENU_H */
