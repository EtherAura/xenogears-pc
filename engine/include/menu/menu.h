#ifndef MENU_MENU_H
#define MENU_MENU_H

/* Main menu overlay (disc 01/11; Noah: menus/menuHandler.cpp, "Menu2"). */

#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#define AddPrim pcTextureHudPrim
#define MENU_TEXT_PRIM pcTextureTextPrim

#include "main/menu.h"
#include "main/game_state.h"

/* libgpu macros missing from psyq.h (official PSY-Q names and expansions). */
#ifndef setUV4
#define setUV4(p, _u0, _v0, _u1, _v1, _u2, _v2, _u3, _v3)                         \
    ((p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u1), (p)->v1 = (_v1), (p)->u2 = (_u2), \
     (p)->v2 = (_v2), (p)->u3 = (_u3), (p)->v3 = (_v3))
#endif

#ifndef setXY4
#define setXY4(p, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3)                         \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), (p)->x2 = (_x2), \
     (p)->y2 = (_y2), (p)->x3 = (_x3), (p)->y3 = (_y3))
#endif

/* libgpu functions psyq.h does not declare yet */
void SetLineF3(void* p);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);

/*
 * Text images for LoadImage: renderString draws 13 rows of w halfwords, but
 * the menus allocate (w - 1) * 26 bytes for a w x 13 rect (0x3F6 for 0x28,
 * 0x5CA for 0x3A, 0x2BE for 0x1C) and upload all of it, so the last 26 bytes
 * (row 12, the last 13 halfwords) come from past the block: on the PS1 the
 * slack, the next block's header and its first bytes, which change with the
 * heap's state (PCSX-Redux). No poly shows them (every menu tour, US and JP,
 * gives the same frames with zero and 0xFF there). On the PC host heap bytes
 * there made the uploads depend on the build's layout, so the PC allocates
 * the whole image and zeroes those 26 bytes (xg-htn.27).
 *
 * Most sites draw into the new block without clearing it, and box open and
 * close animations sample some of the texels no glyph covers, so on the PC
 * a new image (and the menus' label pair image, ALLOC_LABEL_IMAGE) starts as
 * the PS1's would where that is known: the images uploaded from that memory
 * before, else zero (port/src/game/main/text_image.c, xg-htn.31).
 */
void* pcAllocTextImage(s32 size);
void* pcAllocLabelImage(s32 size);
#define ALLOC_TEXT_IMAGE(size) pcAllocTextImage(size)
#define ALLOC_LABEL_IMAGE(size) pcAllocLabelImage(size)

/* Noah: sMenuContext_32C_5C, one save slot's summary; size 0x5C */
typedef struct {
    /* 0x00 */ s32 m0_iconV[6]; /* icon animation frame rows (Noah m0..m14) */
    /* 0x18 */ char m18_name[0x40];
    /* 0x58 */ u8 m58_loaded; /* Noah: s8 */
    /* 0x59 */ u8 m59[3];
} MenuContext_32C_5C;

/* Character names block (gameState.m0_names[0..10]); s32 so that struct
 * copies use lw/sw like retail. */
typedef struct {
    /* 0x00 */ u8 m0_name[0x14];
} MenuName;

typedef union {
    /* 0x00 */ MenuName m0_names[11];
    /* 0x00 */ s32 m0_words[0xDC / 4]; /* word alignment for the block copies */
} MenuSaveCharacterNames; /* 0xDC; gameState.m0_names[0..10] */

/* Game summary kept in the second half of a save file's first block; it is
 * also the start of the save game data (MenuSaveGame). */
typedef struct {
    /* 0x00 */ s32 m0_playTime;
    /* 0x04 */ u16 m4_hp[3];
    /* 0x0A */ u16 mA_maxHp[3];
    /* 0x10 */ u8 m10_mp[3];
    /* 0x13 */ u8 m13_maxMp[3];
    /* 0x16 */ u8 m16_level[3];
    /* 0x19 */ u8 m19_level2[3];
    /* 0x1C */ u8 m1C_partyMemberId[3];
    /* 0x1F */ u8 m1F;
    /* 0x20 */ u8 m20[3];
    /* 0x23 */ u8 m23_slot;
    /* 0x24 */ MenuSaveCharacterNames m24_characterNames;
} MenuSaveSummary; /* 0x100 */

/* Noah: sMenuContext_32C_B94, a save's first block (memory card header:
 * "SC", icon flag, block count, title, icon palette and icon); size 0x200 */
typedef struct {
    /* 0x000 */ u8 m0_magic[2];
    /* 0x002 */ u8 m2_iconDisplayFlag;
    /* 0x003 */ u8 m3_blockNumber;
    /* 0x004 */ u8 m4_title[0x5C];
    /* 0x060 */ u16 m60_iconsPalette[16];
    /* 0x080 */ u8 m80_icon[128];                /* Noah: m80_icons[3][128] */
    /* 0x100 */ MenuSaveSummary m100_summary;    /* over icon frames 2-3 */
} MenuSaveHeader;

/* Memory card slot label ("MEMORY CARD" + slot number) drawn by
 * drawMenu_4D8_text; size 0xF0 */
typedef struct {
    /* 0x00 */ POLY_FT4 m0_label[2];
    /* 0x50 */ POLY_FT4 m50_number[2 * 2];
} MenuCardLabel;

/* Noah: sMenuContext_32C (memory card context); size 0x5034 */
typedef struct MenuContext_32C {
    /* 0x0000 */ MenuContext_32C_5C m0_files[32];
    /* 0x0B80 */ TIM_IMAGE mB80_iconTim;
    /* 0x0B94 */ MenuSaveHeader mB94_fileHeaders[32];
    /* 0x4B94 */ MenuSaveHeader m4B94_saveHeader;
    /* 0x4D94 */ MenuCardLabel m4D94_cardLabels[2];
    /* 0x4F74 */ s32 m4F74_cardStatus[2];
    /* 0x4F7C */ s32 m4F7C_cursor;
    /* 0x4F80 */ s32 m4F80_previousCursor;
    /* 0x4F84 */ s32 m4F84_tileCount;
    /* 0x4F88 */ u8 m4F88_dirScanned[2];
    /* 0x4F8A */ u8 m4F8A_fileCount[2];
    /* 0x4F8C */ u8 m4F8C_loadedFileCount[2];
    /* 0x4F8E */ u8 m4F8E_isGameSave[32];
    /* 0x4FAE */ u8 m4FAE_tileFiles[32];
    /* 0x4FCE */ char m4FCE_saveName[22];
    /* 0x4FE4 */ u8 m4FE4_cardPresent[4];
    /* 0x4FE8 */ u8 m4FE8_prevCardPresent[4];
    /* 0x4FEC */ u32 m4FEC_events[4];
    /* 0x4FFC */ char m4FFC_saveLocationName[0x1E];
    /* 0x501A */ u8 m501A_saveLocationNameEnd;
    /* 0x501B */ u8 m501B_saveLocationNameEnd2;
    /* 0x501C */ char m501C_saveName2[0x5034 - 0x501C];
} MenuContext_32C;

/* Records of the item data file (dir 0x10 file 2) that gMenuContext->m330
 * points into; 0x10 bytes each (Noah: sWeaponStats, sAccessoryStats). */
typedef struct {
    /* 0x00 */ u16 m0_equipMask;  /* party member bits (shop: who can equip, MF5) */
    /* 0x02 */ u8 m2[2];
    /* 0x04 */ u16 m4_price;      /* the shop sells for half (MF5) */
    /* 0x06 */ u8 m6_slotType[2];
    /* 0x08 */ u16 m8_statusMask;
    /* 0x0A */ u8 mA_statusChance;
    /* 0x0B */ u8 mB_statusType;
    /* 0x0C */ u8 mC_power;
    /* 0x0D */ u8 mD[3];
} MenuWeaponStats;

typedef struct {
    /* 0x00 */ u16 m0_equipMask;  /* party member bits (shop: who can equip, MF5) */
    /* 0x02 */ u16 m2_price;      /* the shop sells for half (MF5) */
    /* 0x04 */ u8 m4[4];
    /* 0x08 */ u8 m8_defence;
    /* 0x09 */ u8 m9_type;
    /* 0x0A */ s16 mA_value;   /* ORed into a resistance mask, or a u8 bonus */
    /* 0x0C */ s16 mC_flags;   /* 0x8000..0x100: which stat gets (u8)mC_flags */
    /* 0x0E */ u16 mE_exclusiveGroup;         /* shop: accessories with the same nonzero mE replace each other (MF4) */
} MenuAccessoryStats;

/* Usable item (m330->m1C_usableItems); 0x10 bytes */
typedef struct {
    /* 0x00 */ u8 m0[2];
    /* 0x02 */ u16 m2_price;      /* the shop sells for half (MF5) */
    /* 0x04 */ u16 m4_target;  /* 0x4000, 0x1000, low 2 bits: default target entries */
    /* 0x06 */ u8 m6_flags;    /* 0x80 greyed out; 0x20 greyed out unless g_menuOpenArg; 0xC0 usable */
    /* 0x07 */ u8 m7;
    /* 0x08 */ u8 m8_amount;
    /* 0x09 */ u8 m9;
    /* 0x0A */ s16 mA_flags;   /* 0x8000 HP, 0x4000 MP, 4 stat boost, 2 status, 1 special */
    /* 0x0C */ s16 mC_flags;
    /* 0x0E */ u8 mE[2];
} MenuUsableItem;

/* Per-character ether (magic) records (m330->m20_perCharacter); 0x28 bytes */
typedef struct {
    /* 0x00 */ u8 m0[0x11];
    /* 0x11 */ u8 m11_power;
    /* 0x12 */ u8 m12[5];
    /* 0x17 */ u8 m17_glyph;   /* learned-skill glyph (setupSkillMenuPage reads entries 7..13) */
    /* 0x18 */ u8 m18[0x28 - 0x18];
} MenuEtherData;

/* Gear part tables of the item data file (m330->m8/mC/m10) */
typedef struct {
    /* 0x00 */ u32 m0_gearMask;  /* gears that can equip it (func_801C5260 bits; gear shop) (MF3) */
    /* 0x04 */ s32 m4_hp;
    /* 0x08 */ u16 m8_weight;           /* gear shop copies it to gear m68 (func_801D61B8) (MF4) */
    /* 0x0A */ u16 mA_price;     /* gear shop price (MF3) */
    /* 0x0C */ u8 mC[0x14 - 0x0C];
    /* 0x14 */ u8 m14_agility;
    /* 0x15 */ u8 m15_etherAmp;
    /* 0x16 */ u8 m16;
    /* 0x17 */ u8 m17_response;
} MenuGearFrame; /* 0x18 */

typedef struct {
    /* 0x00 */ u32 m0_gearMask;  /* gears that can equip it (gear shop) (MF3) */
    /* 0x04 */ u8 m4[2];
    /* 0x06 */ u16 m6_fuel;
    /* 0x08 */ u8 m8[2];
    /* 0x0A */ u16 mA_price;     /* gear shop price (MF3) */
    /* 0x0C */ u8 mC_output;
    /* 0x0D */ u8 mD;
    /* 0x0E */ u8 mE_attackMultiplier;
    /* 0x0F */ u8 mF;
} MenuGearEngine; /* 0x10 */

typedef struct {
    /* 0x00 */ u32 m0_gearMask;  /* gears that can equip it (gear shop) (MF3) */
    /* 0x04 */ u16 m4_price;     /* gear shop price (MF3) */
    /* 0x06 */ u8 m6[2];
    /* 0x08 */ u16 m8_defence;
    /* 0x0A */ u16 mA_etherDefence;
    /* 0x0C */ u8 mC[0x14 - 0x0C];
} MenuGearArmor; /* 0x14 */

/* Gear weapon table (m330->m18); 0x14 bytes */
typedef struct {
    /* 0x00 */ u8 m0_extraFileIndex;
    /* 0x01 */ u8 m1_weaponAnimOffset0;
    /* 0x02 */ u8 m2_weaponAnimOffset1;
    /* 0x03 */ u8 m3_weaponAnimOffset2;
    /* 0x04 */ u32 m4_gearMask;  /* gears that can equip it (gear shop) (MF3) */
    /* 0x08 */ u16 m8_price;     /* gear shop price (sold back for half) (MF3) */
    /* 0x0A */ u8 mA[0xE - 0xA];
    /* 0x0E */ u8 mE_power;
    /* 0x0F */ u8 mF_slotType;
    /* 0x10 */ u8 m10_statusChance;
    /* 0x11 */ u8 m11_statusType;
    /* 0x12 */ u16 m12_statusMask;
} MenuGearWeapon;

/* Gear accessory table (m330->m14); 0x1C bytes */
typedef struct {
    /* 0x00 */ u32 m0_gearMask;  /* gears that can equip it (gear shop) (MF3) */
    /* 0x04 */ u16 m4_price;     /* gear shop price (MF3) */
    /* 0x06 */ u16 m6_weight;
    /* 0x08 */ u16 m8_exclusiveGroup;           /* accessory group: a gear keeps one per group (gear shop func_801D3558) (MF3) */
    /* 0x0A */ u8 mA[0xD - 0xA];
    /* 0x0D */ u8 mD_defence;            /* priority: the lowest is replaced (gear shop func_801D3558) (MF3) */
    /* 0x0E */ u8 mE_etherDefence;
    /* 0x0F */ u8 mF;
    /* 0x10 */ u8 m10_terrainAgility[4];
    /* 0x14 */ u8 m14_response;
    /* 0x15 */ u8 m15_type;
    /* 0x16 */ u16 m16_value;
    /* 0x18 */ u8 m18;
    /* 0x19 */ u8 m19;
    /* 0x1A */ u8 m1A_resistanceAmount;
    /* 0x1B */ u8 m1B_etherAmp;
} MenuGearAccessory;

/* Per-gear data file (m330->m4C_perGear[gear]); only what the menu reads */
typedef struct {
    /* 0x00 */ u8 m0[0x24];
    /* 0x24 */ u16 m24_fuelCost;
} MenuGearDataSub;

typedef struct {
    /* 0x000 */ u8 m0[0x5C8];
    /* 0x5C8 */ MenuGearDataSub m5C8_firstSkill;
} MenuGearData;

/* A gear's status-screen numbers (computeGearStatusStats); 0x1C bytes */
typedef struct {
    /* 0x00 */ s32 m0_hp;
    /* 0x04 */ s32 m4_maxHp;
    /* 0x08 */ u16 m8_defence;
    /* 0x0A */ u16 mA_etherDefence;
    /* 0x0C */ u16 mC_totalWeight;
    /* 0x0E */ u16 mE;
    /* 0x10 */ u16 m10_fuel;
    /* 0x12 */ u16 m12_maxFuel;
    /* 0x14 */ u16 m14_attack;
    /* 0x16 */ u8 m16_response;
    /* 0x17 */ u8 m17_agility;
    /* 0x18 */ u8 m18_etherAmp;
    /* 0x19 */ u8 m19;
    /* 0x1A */ u8 m1A;
    /* 0x1B */ u8 m1B;
} MenuGearStats;

/* Save file game data built by buildSaveGameData; blocks copied from gameState
 * with struct assignments (word aligned, so GCC copies them with lw/sw). */
typedef struct {
    /* 0x00 */ s32 m0[0x190 / 4];
} MenuSaveGearNames; /* 0x190; gameState.m0_names[11..30] */

typedef struct {
    /* 0x00 */ s32 m0[4];
} MenuSaveGearEquip; /* 0x10; GameStateGear 0x00-0x0F (part indices) */

typedef struct {
    /* 0x00 */ s32 m0[6];
} MenuSaveGearWeapons; /* 0x18; GameStateGear 0x10-0x27 (weapons) */

typedef struct {
    /* 0x00 */ MenuSaveGearEquip m0_equip;
    /* 0x10 */ MenuSaveGearWeapons m10_weapons;
    /* 0x28 */ s32 m28_weaponStats; /* gear m5C..m5F */
    /* 0x2C */ s32 m2C_hp;
    /* 0x30 */ u8 m30[4];
    /* 0x34 */ u16 m34_fuel;
    /* 0x36 */ u8 m36[2];
    /* 0x38 */ u8 m38_defenceDown;
    /* 0x39 */ u8 m39_baseAttackMultiplier;
    /* 0x3A */ u8 m3A_weightAllowance;
    /* 0x3B */ u8 m3B;
} MenuSaveGear; /* 0x3C */

typedef struct {
    /* 0x00 */ s32 m0[0x78 / 4];
} MenuSaveBlock1648; /* gameState 0x1648 */

typedef struct {
    /* 0x000 */ s32 m0[0x160 / 4];
} MenuSaveBlock16C0; /* gameState.m16C0 */

typedef struct {
    /* 0x000 */ s32 m0[0x100 / 4];
} MenuSaveBlock1820; /* gameState 0x1820 */

typedef struct {
    /* 0x0000 */ MenuSaveSummary m0_summary;
    /* 0x0100 */ MenuSaveGearNames m100_gearNames;
    /* 0x0290 */ GameStateCharacter m290_party[11];
    /* 0x099C */ MenuSaveGear m99C_gears[20];
    /* 0x0E4C */ MenuSaveBlock1648 mE4C;
    /* 0x0EC4 */ MenuSaveBlock16C0 mEC4_abilities;
    /* 0x1024 */ MenuSaveBlock1820 m1024_worldmapPosition;
    /* 0x1124 */ u8 m1124_gameStateTail[0xA38];
} MenuSaveGame; /* 0x1B5C */

/* Noah: sMenuContext_330 (item/equipment data loaded from dir 0x10 file 2);
 * size 0xCC */
typedef struct MenuContext_330 {
    /* 0x00 */ MenuWeaponStats* m0_weaponStats;
    /* 0x04 */ MenuAccessoryStats* m4_accessoryData;
    /* 0x08 */ MenuGearFrame* m8_gearFrames;
    /* 0x0C */ MenuGearEngine* mC_gearEngines;
    /* 0x10 */ MenuGearArmor* m10_gearArmors;
    /* 0x14 */ MenuGearAccessory* m14_gearAccessories;
    /* 0x18 */ MenuGearWeapon* m18_gearWeapons;
    /* 0x1C */ MenuUsableItem* m1C_usableItems;
    /* 0x20 */ MenuEtherData* m20_perCharacter[11];
    /* 0x4C */ MenuGearData* m4C_perGear[20];
    /* 0x9C */ MenuGearStats m9C_gearStats;
    /* 0xB8 */ u16 mB8_computedStats[9]; /* Attack, Hit, Defense, Evade, Ether, EtherDef, Agility, ...;
                                          * the equip menu copies 9 entries */
    /* 0xCA */ u8 mCA[2];
} MenuContext_330;

/* Noah: sMenuContext_33C (what to draw); size 0x6C. The label arrays of
 * MenuContext.m4E0..m1BE0 are drawn per entry of mC/m14/m1A/m34/m38/m40/m54/m5C. */
typedef struct MenuContext_33C {
    /* 0x00 */ u8 m0_isInfoCardEnabled[3];
    /* 0x03 */ u8 m3_drawSelectionBox;
    /* 0x04 */ u8 m4_drawCursor;
    /* 0x05 */ u8 m5_drawGold;
    /* 0x06 */ u8 m6_drawPlayTime;
    /* 0x07 */ u8 m7_drawCurrentSelectedCharacterInfoCard;
    /* 0x08 */ u8 m8_drawCurrentSelectedCharacterStats;
    /* 0x09 */ u8 m9_drawMainMenu;
    /* 0x0A */ u8 mA_354Enabled;
    /* 0x0B */ u8 mB_drawSaveScreen;
    /* 0x0C */ u8 mC_drawMainMenuLabels[8];
    /* 0x14 */ u8 m14_drawSubMenuLabels[6];
    /* 0x1A */ u8 m1A_drawSaveMenuLabels[6];
    /* 0x20 */ u8 m20_menuBoxEnabled[7];
    /* 0x27 */ u8 m27_menuBoxAnimating[7];
    /* 0x2E */ u8 m2E_drawCheckingMemorycardText;
    /* 0x2F */ u8 m2F_draw428;
    /* 0x30 */ u8 m30_partyMemberId[3];
    /* 0x33 */ u8 m33_cardMessageShown;
    /* 0x34 */ u8 m34_drawLabels[4];
    /* 0x38 */ u8 m38_drawListMenuLabels[8];
    /* 0x40 */ u8 m40_drawEquipMenuLabels[6];
    /* 0x46 */ u8 m46_draw1E08;   /* party info cards */
    /* 0x47 */ u8 m47_drawNameEntry;
    /* 0x48 */ u8 m48_draw42C;    /* item menu */
    /* 0x49 */ u8 m49_draw43C;
    /* 0x4A */ u8 m4A_draw430;
    /* 0x4B */ u8 m4B_draw360;
    /* 0x4C */ u8 m4C_draw434;
    /* 0x4D */ u8 m4D_draw438;
    /* 0x4E */ u8 m4E_drawSkillPageLabels[2];
    /* 0x50 */ u8 m50_draw444[2];
    /* 0x52 */ u8 m52_draw44C;    /* progress box */
    /* 0x53 */ u8 m53_draw440;
    /* 0x54 */ u8 m54_drawGearMenuLabels[6];
    /* 0x5A */ u8 m5A_drawShop[2];
    /* 0x5C */ u8 m5C_drawSoundMenuLabels[4];
    /* 0x60 */ u8 m60_characterAvailable[3]; /* findNextPartySlot skips 0 entries */
    /* 0x63 */ u8 m63_drawGearShop[4];
    /* 0x67 */ u8 m67_arrowsAllocated;
    /* 0x68 */ u8 m68_drawCardLabels;
    /* 0x69 */ u8 m69[3];
} MenuContext_33C;

/* Noah: sMenuContext_340_goldOwned */
typedef struct MenuContext_340 {
    /* 0x000 */ POLY_FT4 m0_goldPolys[9][2];
    /* 0x2D0 */ POLY_FT4 m2D0_goldUnit[2];
    /* 0x320 */ s32 m320_goldPolyLength;
    /* 0x324 */ u8 m324_oddOrEven;
} MenuContext_340;

/* Noah: sMenuContext_344_playTime */
typedef struct MenuContext_344 {
    /* 0x000 */ POLY_FT4 m0_playTimePolys[7][2];
    /* 0x230 */ POLY_FT4 m230_separators[4][2];
    /* 0x370 */ u8 m370_oddOrEven;
} MenuContext_344;

/* Noah: sMenuContext_348 (cursor); size 0x15C */
typedef struct MenuContext_348 {
    /* 0x000 */ POLY_FT4 m0_cursor[2];
    /* 0x050 */ POLY_G4 m50_polyG4[2];
    /* 0x098 */ POLY_F4 m98_poly[2];
    /* 0x0C8 */ LINE_F3 mC8_lines[2];
    /* 0x0F8 */ LINE_F3 mF8_lines[2];
    /* 0x128 */ DR_MODE m128_drawMode[2];
    /* 0x140 */ DR_MODE m140_backgroundDrawMode[2];
    /* 0x158 */ u8 m158_oddOrEven;
    /* 0x159 */ u8 m159_oddOrEven;
    /* 0x15A */ u8 m15A;
    /* 0x15B */ u8 m15B_selectionWidth;
} MenuContext_348;

/* Noah: sMenuContext_350 (main menu); size 0x1194 */
typedef struct MenuContext_350 {
    /* 0x0000 */ POLY_FT4 m0_polys[28][2];
    /* 0x08C0 */ POLY_FT4 m8C0_polys[28][2];
    /* 0x1180 */ RECT m1180_backgroundRect;
    /* 0x1188 */ s32 m1188_numEntries2;
    /* 0x118C */ s32 m118C_numEntries;
    /* 0x1190 */ u8 m1190_0OddOrEven;
    /* 0x1191 */ u8 m1191_8C0OddOrEven;
    /* 0x1192 */ u8 m1192_dimmed;
    /* 0x1193 */ u8 m1193_appliedDimmed;
} MenuContext_350;

/* Noah: sMenuContext_354 */
typedef struct MenuContext_354 {
    /* 0x0000 */ POLY_FT4 m0_polys[16][2];
    /* 0x0500 */ POLY_FT4 m500_polys[48][2];
    /* 0x1400 */ s32 m1400_0Length;
    /* 0x1404 */ s32 m1404_500Length;
    /* 0x1408 */ u8 m1408_0OddOrEven;
    /* 0x1409 */ u8 m1409_500OddOrEven;
} MenuContext_354;

/* Noah: sMenuContext_39C (character info card) */
typedef struct MenuContext_39C {
    /* 0x0000 */ POLY_FT4 m0_portraitPoly[2];
    /* 0x0050 */ POLY_FT4 m50_NamePoly[2];
    /* 0x00A0 */ POLY_FT4 mA0_FixedStrings[27][2]; /* Noah: 20; 27 fits the 0x910 offset */
    /* 0x0910 */ POLY_FT4 m910_LevelPolys[3][2];
    /* 0x0A00 */ POLY_FT4 mA00_Level2Polys[3][2];
    /* 0x0AF0 */ POLY_FT4 mAF0_PolyHP[3][2];
    /* 0x0BE0 */ POLY_FT4 mBE0_PolyMaxHP[3][2];
    /* 0x0CD0 */ POLY_FT4 mCD0_PolyMP[2][2];
    /* 0x0D70 */ POLY_FT4 mD70_PolyMaxMP[2][2];
    /* 0x0E10 */ POLY_FT4 mE10_polys[7][2];
    /* 0x1040 */ POLY_FT4 m1040_polys[7][2];
    /* 0x1270 */ u8 m1270_oddOrEven;
    /* 0x1271 */ u8 m1271_LevelLength;
    /* 0x1272 */ u8 m1272_Level2Length;
    /* 0x1273 */ u8 m1273_HPLength;
    /* 0x1274 */ u8 m1274_MaxHPLength;
    /* 0x1275 */ u8 m1275_MPLength;
    /* 0x1276 */ u8 m1276_MaxMPLength;
    /* 0x1277 */ u8 m1277_E10Length;
    /* 0x1278 */ u8 m1278_E10Length2;
    /* 0x1279 */ u8 m1279_FixedStringLength;
} MenuContext_39C;

/* Noah: sMenuContext_428 (memory card screen cursors) */
typedef struct MenuContext_428 {
    /* 0x000 */ POLY_FT4 m0_polys[4][2];
    /* 0x140 */ u8 m140_enabled[4];
    /* 0x144 */ u8 m144_followTile[4];
    /* 0x148 */ u8 m148_oddOrEven[4];
} MenuContext_428;

/* Noah: sMenuContext_358 (status screen: selected character/gear detail;
 * drawn when m33C->m7_drawCurrentSelectedCharacterInfoCard). Polys (two
 * buffers each) then their quads; size 0x2AF0 (JP 0x2930) */
typedef struct MenuContext_358 {
    /* 0x0000 */ POLY_FT4 m0_portrait[2];
    /* 0x0050 */ POLY_FT4 m50_name[2];
    /* An intentional PC divergence, with m1EE0_labelVerts and the u16
     * m2AEC_labelLength: with pictures from another source
     * (port/src/system/gametext_picture.c) each of the 24 labels
     * (setupStatusScreenLabels) can be a word sprite of up to 64 pieces.
     * Retail sizes the arrays for its own glyphs, and its u8 count would wrap
     * at 255 pieces; the PC keeps and draws them all. Both allocations
     * (m_801E0F78.c, m_801E23CC.c) take sizeof (tools/heapsizes.py). */
    POLY_FT4 mA0_labels[24 * 64][2];
    /* 0x0AF0 */ POLY_FT4 mAF0_level[3][2];
    /* 0x0BE0 */ POLY_FT4 mBE0_level2[3][2];
    /* 0x0CD0 */ POLY_FT4 mCD0_experience[8][2];
    /* 0x0F50 */ POLY_FT4 mF50_experience2[8][2];
    /* 0x11D0 */ POLY_FT4 m11D0_xp[7][2];
    /* 0x1400 */ POLY_FT4 m1400_xp2[7][2];
    /* 0x1630 */ POLY_FT4 m1630_hp[5][2];
    /* 0x17C0 */ POLY_FT4 m17C0_maxHp[5][2];
    /* 0x1950 */ POLY_FT4 m1950_mp[5][2];
    /* 0x1AE0 */ POLY_FT4 m1AE0_maxMp[5][2];
    /* 0x1C70 */ POLY_FT4 m1C70_weight[5][2];
    /* 0x1E00 */ POLY_FT4 m1E00_polys[2][2];
    /* 0x1EA0 */ SVECTOR m1EA0_portraitVerts[4]; /* Noah: m1EAC */
    /* 0x1EC0 */ SVECTOR m1EC0_nameVerts[4];
    SVECTOR m1EE0_labelVerts[24 * 64][4];
    /* 0x2300 */ SVECTOR m2300_levelVerts[3][4];
    /* 0x2360 */ SVECTOR m2360_level2Verts[3][4];
    /* 0x23C0 */ SVECTOR m23C0_experienceVerts[8][4];
    /* 0x24C0 */ SVECTOR m24C0_experience2Verts[8][4];
    /* 0x25C0 */ SVECTOR m25C0_xpVerts[7][4];
    /* 0x26A0 */ SVECTOR m26A0_xp2Verts[7][4];
    /* 0x2780 */ SVECTOR m2780_hpVerts[5][4];
    /* 0x2820 */ SVECTOR m2820_maxHpVerts[5][4];
    /* 0x28C0 */ SVECTOR m28C0_mpVerts[5][4];
    /* 0x2960 */ SVECTOR m2960_maxMpVerts[5][4];
    /* 0x2A00 */ SVECTOR m2A00_weightVerts[5][4];
    /* 0x2AA0 */ SVECTOR m2AA0_onGearIconVerts[2][4];
    /* 0x2AE0 */ u8 m2AE0_oddOrEven;
    /* 0x2AE1 */ u8 m2AE1_levelLength;
    /* 0x2AE2 */ u8 m2AE2_level2Length;
    /* 0x2AE3 */ u8 m2AE3_CD0Length;
    /* 0x2AE4 */ u8 m2AE4_F50Length;
    /* 0x2AE5 */ u8 m2AE5_xpLength;
    /* 0x2AE6 */ u8 m2AE6_xp2Length;
    /* 0x2AE7 */ u8 m2AE7_hpLength;
    /* 0x2AE8 */ u8 m2AE8_maxHpLength;
    /* 0x2AE9 */ u8 m2AE9_mpLength;
    /* 0x2AEA */ u8 m2AEA_maxMpLength;
    /* 0x2AEB */ u8 m2AEB_1C70Length;
    u16 m2AEC_labelLength;
    /* 0x2AED */ u8 m2AED_1E00Length;
    /* 0x2AEE */ u8 m2AEE_1E00OddOrEven;
    /* 0x2AEF */ u8 m2AEF;
} MenuContext_358;

/* Noah: sMenuContext_35C (status screen, drawn when
 * m33C->m8_drawCurrentSelectedCharacterStats): seven stat rows, each with
 * two gauges (POLY_G4) and two number strings; size 0x32F4 (JP 0x2954) */
typedef struct MenuContext_35C {
    /* Seven translated word sprites can each contain up to 64 pieces
     * (setupStatusMenuForCharacter3Sub2Sub0). An intentional PC divergence,
     * with m2420_vertex and the u16 m32F3_0Length (retail's u8 would wrap at
     * 255 pieces), as MenuContext_358's labels; allocated by sizeof. */
    POLY_FT4 m0_polys[7 * 64][2];
    /* 0x0EB0 */ POLY_FT4 mEB0_polys[7][4][2];
    /* 0x1770 */ POLY_FT4 m1770_polys[7][4][2];
    /* 0x2030 */ POLY_G4 m2030_gauge[7][2];
    /* 0x2228 */ POLY_G4 m2228_gauge[7][2];
    SVECTOR m2420_vertex[7 * 64][4];
    /* 0x2A00 */ SVECTOR m2A00_vertex[7][4][4];
    /* 0x2D80 */ SVECTOR m2D80_vertex[7][4][4];
    /* 0x3100 */ SVECTOR m3100_gaugeVertex[7][4];
    /* 0x31E0 */ SVECTOR m31E0_gaugeVertex[7][4];
    /* 0x32C0 */ u8 m32C0_EB0Length[7];
    /* 0x32C7 */ u8 m32C7_EB0OddOrEven[7];
    /* 0x32CE */ u8 m32CE_1770Length[7];
    /* 0x32D5 */ u8 m32D5_1770OddOrEven[7];
    /* 0x32DC */ u8 m32DC_2030OddOrEven[7];
    /* 0x32E3 */ u8 m32E3_2228OddOrEven[7];
    /* 0x32EA */ u8 m32EA_rowEnabled[7];
    /* 0x32F1 */ u8 m32F1_oddOrEven;
    /* 0x32F2 */ u8 m32F2_drawSecondGauge;
    u16 m32F3_0Length;
} MenuContext_35C;

/* Noah: sMenuContext_364 (a framed menu box: corners, edges, background);
 * size 0x720 */
typedef struct MenuContext_364 {
    /* 0x000 */ POLY_FT4 m0_corners[4][2];
    /* 0x140 */ POLY_FT4 m140_topEdge[2][2];
    /* 0x1E0 */ POLY_FT4 m1E0_bottomEdge[2][2];
    /* 0x280 */ POLY_FT4 m280_leftEdge[2][2];
    /* 0x320 */ POLY_FT4 m320_rightEdge[2][2];
    /* 0x3C0 */ POLY_FT4 m3C0_innerEdge[2];
    /* 0x410 */ POLY_FT4 m410_innerCorners[2][2];
    /* 0x4B0 */ POLY_G4 m4B0_background[2];
    /* 0x4F8 */ DR_MODE m4F8_drawMode[2];
    /* 0x510 */ SVECTOR m510_cornerVerts[4][4];
    /* 0x590 */ SVECTOR m590_topEdgeLeftVerts[4];
    /* 0x5B0 */ SVECTOR m5B0_topEdgeRightVerts[4];
    /* 0x5D0 */ SVECTOR m5D0_bottomEdgeLeftVerts[4];
    /* 0x5F0 */ SVECTOR m5F0_bottomEdgeRightVerts[4];
    /* 0x610 */ SVECTOR m610_leftEdgeTopVerts[4];
    /* 0x630 */ SVECTOR m630_leftEdgeBottomVerts[4];
    /* 0x650 */ SVECTOR m650_rightEdgeTopVerts[4];
    /* 0x670 */ SVECTOR m670_rightEdgeBottomVerts[4];
    /* 0x690 */ SVECTOR m690_backgroundVerts[4];
    /* 0x6B0 */ SVECTOR m6B0_innerEdgeVerts[4];
    /* 0x6D0 */ SVECTOR m6D0_innerCornerVerts[2][4];
    /* 0x710 */ s32 m710_cornerCount;
    /* 0x714 */ s32 m714_transformDisabled;
    /* 0x718 */ s32 m718_otIndex;
    /* 0x71C */ u8 m71C_oddOrEven;
    /* 0x71D */ u8 m71D_drawInner;
    /* 0x71E */ u8 m71E[2];
} MenuContext_364;

/* One save slot line of the memory card screen (setupSaveSlotLabels..
 * setupSaveSlotMp); 0x87C */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_name[2];         /* leader's name sprite */
    /* 0x050 */ POLY_FT4 m50_label[9][2];    /* "Lv HP EP" style labels */
    /* 0x320 */ POLY_FT4 m320_level[3][2];
    /* 0x410 */ POLY_FT4 m410_level2[3][2];
    /* 0x500 */ POLY_FT4 m500_hp[3][2];
    /* 0x5F0 */ POLY_FT4 m5F0_maxHp[3][2];
    /* 0x6E0 */ POLY_FT4 m6E0_mp[2][2];
    /* 0x780 */ POLY_FT4 m780_maxMp[2][2];
    /* 0x820 */ POLY_FT4 m820_nameImage[2];            /* the slot's name image (infocardNameU/V[6 + slot]) */
    /* 0x870 */ u8 m870_levelLength;
    /* 0x871 */ u8 m871_level2Length;
    /* 0x872 */ u8 m872_hpLength;
    /* 0x873 */ u8 m873_maxHpLength;
    /* 0x874 */ u8 m874_mpLength;
    /* 0x875 */ u8 m875_maxMpLength;
    /* 0x876 */ u8 m876_oddOrEven;
    /* 0x877 */ u8 m877_oddOrEven;
    /* 0x878 */ u8 m878_enabled;
    /* 0x879 */ u8 m879_oddOrEven;
    /* 0x87A */ u8 m87A_labelLength;
    /* 0x87B */ u8 m87B;
} MenuSaveSlotLine;

/* Noah: sMenuContext_34C (save/load screen: save slot icons, the file list
 * and the selected save's summary; drawn when m33C->mB) */
typedef struct MenuContext_34C {
    /* 0x000 */ POLY_FT4 m0_polys[32][2];
    /* 0xA00 */ POLY_FT4 mA00_icon[2];
    /* 0xA50 */ POLY_G4 mA50_polyG4[2];
    /* 0xA98 */ MenuSaveSlotLine mA98_slots[3];
    /* 0x240C */ POLY_FT4 m240C_separator1[2][2];     /* separators of the play time */
    /* 0x24AC */ POLY_FT4 m24AC_separator2[2][2];
    /* 0x254C */ POLY_FT4 m254C_playTime[7][2];
    /* 0x277C */ POLY_FT4 m277C_titleGlyphs[16][2];
#if !VERSION_JP /* JP shows no save slot number: m2DBC_showSummary is at 0x2C7C */
    /* 0x2C7C */ POLY_FT4 m2C7C_slotLabel1[2];
    /* 0x2CCC */ POLY_FT4 m2CCC_slotLabel2[2];
    /* 0x2D1C */ POLY_FT4 m2D1C_slotTens[2];
    /* 0x2D6C */ POLY_FT4 m2D6C_slotOnes[2];
#endif
    /* 0x2DBC */ u8 m2DBC_showSummary;
    /* 0x2DBD */ u8 m2DBD[3];
} MenuContext_34C; /* 0x2DC0 (JP 0x2C80) */

/* Noah: sMenuContext_3A8, one memory card block tile (memoryCardTiles.h);
 * size 0x158 */
typedef struct MenuContext_3A8 {
    /* 0x000 */ POLY_FT4 m0_memoryCardTransparentPolys[2];
    /* 0x050 */ LINE_F3 m50_topRightLine[2];
    /* 0x080 */ LINE_F3 m80_leftBottomLine[2];
    /* 0x0B0 */ POLY_F4 mB0_memoryCardOpaquePolys[2];
    /* 0x0E0 */ SVECTOR mE0_vertices[4];
    /* 0x100 */ SVECTOR m100_topRightVerts[4];
    /* 0x120 */ SVECTOR m120_leftBottomVerts[4];
    /* 0x140 */ DR_MODE m140_drawMode[2];
} MenuContext_3A8;

/* Noah: sMenuContext_380 (a menu box growing open); size 0x18 */
typedef struct MenuContext_380 {
    /* 0x00 */ u16 m0_x;
    /* 0x02 */ u16 m2_y;
    /* 0x04 */ u16 m4_width;
    /* 0x06 */ u16 m6_height;
    /* 0x08 */ u16 m8_currentWidth;
    /* 0x0A */ u16 mA_currentHeight;
    /* 0x0C */ s32 mC_otIndex;
    /* 0x10 */ u8 m10_boxIndex;
    /* 0x11 */ u8 m11_done;
    /* 0x12 */ u8 m12_transformDisabled;
    /* 0x13 */ u8 m13_drawInner;
    /* 0x14 */ u8 m14[4];
} MenuContext_380;

/* Noah: sMenuContext_360 (equipment names and the equip preview; drawn when
 * m33C->m4B_draw360); size 0x2AC */
typedef struct MenuContext_360 {
    /* 0x000 */ MenuContext_4E0 m0_names[5];   /* equipment name labels */
    /* 0x280 */ u16 m280_stats[9];             /* copy of m330->mB8_computedStats (equip preview) */
    /* 0x292 */ u8 m292[0x294 - 0x292];
    /* 0x294 */ u8 m294_nameEnabled[5];
    /* 0x299 */ u8 m299_oddOrEven;
    /* 0x29A */ u8 m29A[2];
    /* 0x29C */ u8 m29C_backup6A[5];  /* character m6A..m6E, or gear mC..mF */
    /* 0x2A1 */ u8 m2A1_backup6F[5];  /* character m6F..m73, or gear m4..m7 */
    /* 0x2A6 */ u8 m2A6_backup74[5];  /* character m74..m76 (accessories), or gear m9..mB */
    /* 0x2AB */ u8 m2AB;
} MenuContext_360;

/* Item menu page (initItemMenu allocates it; drawn when m33C->m48): 16 item
 * names and their counts, two columns of eight; size 0x1198 */
typedef struct MenuContext_42C {
    /* 0x0000 */ MenuContext_4E0 m0_names[16];
    /* 0x0800 */ MenuContext_4E0 m800_counts[16];
    /* 0x1000 */ MenuContext_4E0 m1000_selectedName;  /* copy of m0_names[sel] */
    /* 0x1080 */ MenuContext_4E0 m1080_selectedCount; /* copy of m800_counts[sel] */
    /* 0x1100 */ MenuContext_4E0 m1100_description;
    /* 0x1180 */ u8* m1180_descriptions; /* item description text bundle */
    /* 0x1184 */ u8 m1184_enabled[16];
    /* 0x1194 */ u8 m1194_showSelected;
    /* 0x1195 */ u8 m1195[3];
} MenuContext_42C;

/* Two-column list page used by initEtherListMenu (modes 0-2; drawn when
 * m33C->m4A); size 0x1094 */
typedef struct MenuContext_430 {
    /* 0x0000 */ MenuContext_4E0 m0_names[14];
    /* 0x0700 */ MenuContext_4E0 m700_values[14];
    /* 0x0E00 */ MenuContext_4E0 mE00_selectedName;
    /* 0x0E80 */ MenuContext_4E0 mE80_selectedValue;
    /* 0x0F00 */ MenuContext_4E0 mF00_description[2];
    /* 0x1000 */ MenuContext_4E0 m1000_characterName;
    /* 0x1080 */ void* m1080_descriptions;
    /* 0x1084 */ u8 m1084_flags[14]; /* per entry: 0x80 selectable, | 1; nonzero: drawn */
    /* 0x1092 */ u8 m1092_showSelected;
    /* 0x1093 */ u8 m1093;
} MenuContext_430;

/* Equip menu lists (drawn when m33C->m4C); size 0xA1C */
typedef struct MenuContext_434 {
    /* 0x000 */ MenuContext_4E0 m0_names[8];
    /* 0x400 */ MenuContext_4E0 m400_counts[8];
    /* 0x800 */ MenuContext_4E0 m800_characterName;
    /* 0x880 */ MenuContext_4E0 m880_description[3]; /* item description lines */
    /* 0xA00 */ u8* mA00_textBundles[4];       /* item names/descriptions: char weapon, char accessory,
                                                * gear part, gear accessory */
    /* 0xA10 */ u8 mA10_enabled[8];
    /* 0xA18 */ u8 mA18_drawDescription;
    /* 0xA19 */ u8 mA19[3];
} MenuContext_434;

/* Per-character reference values compared with GameStateCharacter.m90 by
 * computeSkillLearnPercent (percentage average); size 0x110 */
typedef struct MenuStatTable {
    /* 0x000 */ u16 m0_requirements[19][7];
    /* 0x10A */ u8 m10A[6];
} MenuStatTable;

/* initSkillMenu allocates it (drawn when m33C->m4D); size 0x25C0 */
typedef struct MenuContext_438 {
    /* 0x0000 */ MenuContext_4E0 m0_names[13];
    /* 0x0680 */ MenuContext_4E0 m680_values[13];
    /* 0x0D00 */ MenuContext_4E0 mD00_characterName;             /* setupCharacterNameLabel fills it */
    /* 0x0D80 */ POLY_FT4 mD80_glyphs[13][5][2];   /* learned-entry glyphs (setupSkillEntryProgress) */
    /* 0x21D0 */ POLY_G4 m21D0_bars[13][2];        /* green-to-black gradient bars */
    /* 0x2578 */ MenuStatTable* m2578_skillRequirements;             /* [character] */
    /* 0x257C */ u8 m257C_glyphCount[13];
    /* 0x2589 */ u8 m2589_glyphOddOrEven[13];
    /* 0x2596 */ u8 m2596_enabled[13];
    /* 0x25A3 */ u8 m25A3_showBar[13];
    /* 0x25B0 */ u8 m25B0_barOddOrEven[13];
    /* 0x25BD */ u8 m25BD[3];
} MenuContext_438;

/* A single glyph cursor (glyph 0x107) with its 3D quad (drawn when
 * m33C->m49); size 0x74 */
typedef struct MenuContext_43C {
    /* 0x00 */ POLY_FT4 m0_polys[2];
    /* 0x50 */ SVECTOR m50_vertex[4];
    /* 0x70 */ u8 m70_oddOrEven;
    /* 0x71 */ u8 m71[3];
} MenuContext_43C;

/* Noah: sMenuContext_440, two-glyph arrows (glyphs 0x164/0x165) with their
 * 3D quads (drawn when m33C->m53); size 0x1C4 */
typedef struct MenuContext_440 {
    /* 0x000 */ POLY_FT4 m0_polys[8]; /* [quad * 2 + oddOrEven]; each glyph fills two quads */
    /* 0x140 */ SVECTOR m140_vertex[4][4];
    /* 0x1C0 */ u8 m1C0_oddOrEven;
    /* 0x1C1 */ u8 m1C1[3];
} MenuContext_440;

/* Animated pointer glyph (font characters 0x15B..0x15F) drawn next to a list
 * entry; allocMenuPointerGlyph allocates one per slot in gMenuContext->m444[slot],
 * updateMenuPointerGlyph animates and places it (drawn when m33C->m50[i]); size 0x78 */
typedef struct MenuContext_444 {
    /* 0x00 */ POLY_FT4 m0_polys[2];
    /* 0x50 */ SVECTOR m50_vertex[4];
    /* 0x70 */ s32 m70_frame;   /* counts down 4..0 */
    /* 0x74 */ u8 m74_timer;    /* frame advances every 6 ticks */
    /* 0x75 */ u8 m75_oddOrEven;
    /* 0x76 */ u8 m76[2];
} MenuContext_444;

/* Memory card progress box (drawn when m33C->m52): a bar (POLY_F4 whose
 * width grows by m7B4 per frame while m52 == 1) with two lines of text;
 * size 0x7BC */
typedef struct MenuContext_44C {
    /* 0x000 */ POLY_F4 m0_bar[2];
    /* 0x030 */ POLY_FT4 m30_line1[12][2];
    /* 0x3F0 */ POLY_FT4 m3F0_line2[12][2];
    /* 0x7B0 */ s32 m7B0_width;
    /* 0x7B4 */ s32 m7B4_speed;
    /* 0x7B8 */ u8 m7B8_oddOrEven;
    /* 0x7B9 */ u8 m7B9[3];
} MenuContext_44C;

/* One ether/skill entry of a per-character/per-gear table (0x28 bytes; character entries
 * at +0x370, gear entries at +0x348, gear skills at +0x5C8; setupEtherListSelection reads m0 too) */
typedef struct {
    /* 0x00 */ u16 m0_flags;     /* 1: all targets; 0x4000/0x1000: label selection */
    /* 0x02 */ u8 m2[0x13 - 0x2];
    /* 0x13 */ u8 m13_cost;      /* MP */
    /* 0x14 */ u8 m14[0x24 - 0x14];
    /* 0x24 */ u16 m24_fuelCost; /* gear fuel */
    /* 0x26 */ u8 m26[2];
} MenuEtherEntry;

/* Party info card (gMenuContext->m1E08[3], drawn when m33C->m46) */
typedef struct MenuContext_1E08 {
    /* 0x000 */ POLY_FT4 m0_polys[9 * 2];
    /* 0x2D0 */ POLY_FT4 m2D0_polys[5 * 2];
    /* 0x460 */ POLY_FT4 m460_poly[2];
    /* 0x4B0 */ POLY_FT4 m4B0_poly[2];
    /* 0x500 */ POLY_FT4 m500_polys[3 * 2];
    /* 0x5F0 */ POLY_FT4 m5F0_polys[3 * 2];
    /* 0x6E0 */ POLY_FT4 m6E0_polys[5 * 2];
    /* 0x870 */ POLY_FT4 m870_polys[5 * 2];
    /* 0xA00 */ POLY_FT4 mA00_polys[3 * 2];
    /* 0xAF0 */ POLY_FT4 mAF0_polys[3 * 2];
    /* 0xBE0 */ u8 mBE0_500Length;
    /* 0xBE1 */ u8 mBE1_5F0Length;
    /* 0xBE2 */ u8 mBE2_6E0Length;
    /* 0xBE3 */ u8 mBE3_870Length;
    /* 0xBE4 */ u8 mBE4_A00Length;
    /* 0xBE5 */ u8 mBE5_AF0Length;
    /* 0xBE6 */ u8 mBE6_oddOrEven;
    /* 0xBE7 */ u8 mBE7_enabled;
    /* 0xBE8 */ u8 mBE8_0Length;
    /* 0xBE9 */ u8 mBE9_2D0Length;  /* party menu: m2D0_polys count, cleared by func_801CA5C0 (MF6) */
} MenuContext_1E08;

/* Noah: sMenuContextMenuTile, a font glyph's texture info (computeMenuBorder) */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ s32 m4_tp;
    /* 0x08 */ s32 m8_clutX;
    /* 0x0C */ s32 mC_clutY;
    /* 0x10 */ s32 m10_tpageX;
    /* 0x14 */ s32 m14_tpageY;
} MenuBorderTile;

extern MenuContext* gMenuContext;

/* Main-executable globals the menu uses in several files. */
extern u8 g_menuOpenArg;
extern u8 useDebugMenuList;
extern u8 menuToEnter;
extern s32 g_playTimeInVsync;
extern u8 savedMenuSelectedEntry;

/* The shop/gear shop/menu_name_entry/party overlays share the types above but have
 * their own data and function addresses: they define MENU_FAMILY_OVERLAY. */
#ifndef MENU_FAMILY_OVERLAY
/* Menu data and bss used by several files. */
extern u8 discChangeSaveActive;
extern u8 needProcessLoadSaveMenuSub7;
extern u8 processLoadSaveMenuSub6Var1;
extern u8 menuVarUnk1;
extern u8 menuVarUnk0;
extern s32 memoryCardTilesPositionX[]; /* Noah: memoryCardTilesPositionX (save slot cursor x) */
extern s32 memoryCardTilesPositionY[];
extern s32 cursorPosXTable[11] PORT_ALIAS; /* Noah: cursorPosXTable */
extern s32 cursorPosYTable[11]; /* Noah: cursorPosYTable */
extern s32 saveGameMainMenuConfig5[]; /* label x offsets (updateMenuSelection2) */
extern u32 mainMenuConfig[][2]; /* main menu entries, two glyph ids each (setupMainMenuWithConfig, updateMenuSelection) */
extern u8 mainMenuConfig2[];
extern u8 menuConfig2[];
extern u8 listMenuLabelConfig[2][8];
extern u8 gearMenuLabelConfig[12]; /* [6..11]: the on-gear set */
extern s32 infocardNameU[19] PORT_ALIAS; /* Noah: infocardNameU (name images' VRAM u / 4; save slots use [6 + slot]) */
extern s32 infocardNameV[19] PORT_ALIAS; /* Noah: infocardNameV (name images' VRAM v) */
extern u16 itemListScrollThumbHeight;   /* item list scroll bar */
extern s32 itemListMaxPage;
extern u16 itemListScrollStep;
extern u8 yesNoPromptCancelled;
extern s32 processLoadSaveMenuSub6Var2[2]; /* Noah: processLoadSaveMenuSub6Var2 (blocks used per card; [1] is D_801EA904) */

#endif /* !MENU_FAMILY_OVERLAY */

/* Main's font functions (src/main/font.c). Every menu caller passes ints for
 * the s16/u16/u8 parameters (unmasked), so the PS1 build keeps int views; the
 * PC gets font.c's parameter types (polys stays void*: callers pass single
 * polys and arrays of pairs alike). */
struct Font;
s32 setupStringInPolyFT4(struct Font* font, s32 glyphIndex, void* polys, s32 oddOrEven, s16 x, s16 y, u16 scale);
s32 initMenuTiles6Sub(struct Font* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, u16 scale,
                      u8 flipX, u8 flipY);

/* The wide picture's layout of the frame about to be drawn from head
 * (port/src/game/menu/wide.c; every menu overlay's menuDraw calls it just
 * before its DrawOTag). */
void menuWideLayout(OTag* head);

#ifndef MENU_FAMILY_OVERLAY
/* Menu functions called from other files (definition views). */
/* m_801C531C */
void loadSaveLocationName(void);

/* m_801C65F4 */
void loadSharedMenuData2(u8 arg0);
void setupMainMenu(void);
void menuDraw(void);
void updatePlayTimeString(u32 t);
void fillStringDisplayBuffer(u32 value);
void setupInfoCardAnimation(s32 startX, s32 startY, s32 endX, s32 endY, u8 numSteps, u8 slot);
void infocardAnimationStep(u8 slot);
void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h);
void playMenuSoundEffect(u8 sfx);
u16 menu2CheckBitmaskMsbFirst(u16 arg0, u8 arg1); /* Noah: menu2CheckBitmask */
u16 menu2CheckBitmask(u16 mask, u8 bit);
void checkCurrentDiscNumber(u8 disc);
void closeMemoryCardsEvents(void);
void makeSaveBlockDisplay(u8 chan);

/* m_801C8D78 */
void updateMemoryCardDirectoryScans(void);
u8 processLoadSaveMenuSub6(void);
u8 askYesNoQuestion(u8 arg0, u8 arg1, u8 arg2);
u8 updateSaveState(u8 arg0);
void setupPartyCard(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant);
void drawMenuSpecificElements(void);
void updateMenuTransforms(void);
void processLoadSaveMenuSub1(void);

/* m_801D1EB0 */
void processLoadSaveMenuSub1Alt2(void);
void updateCursorPolysLocation(s32 idx, u8 draw);
void resetCursorState(void);
void setupDraw428(u8 mode);
void disableDraw428(void);
void initStatusMenuSub0(u8 show);
void hideSubMenuLabels(void);
void setupStatusMenuForCharacter4(void);
void updateGoldDisplay(void);
void initMenuForPlaytime(void);
void drawMenuPlayTime(void);

/* m_801D29A8 */
void processLoadSaveMenuSub2(u8 show, u8 hideGold);
void setupMenu0(void);
void setupStatusMenuForCharacter3(u8 a, u8 b);
void processLoadSaveMenuSub5(u8 msg);
void processLoadSaveMenuSub7(void);
void setupScrollBarThumb(s32 x, s32 y, s32 h);
void freeScrollBarThumb(void);
void initStatusMenuSub1(u8 which, u8 mode);

/* m_801D3674 */
void freeCharacterSwitchArrows(void); /* frees what Noah initStatusMenuSub1 allocates */
void setupCharacterNameLabel(MenuContext_4E0* label, u8 slot, u8 isGear, u8 pos);
void iniMenuContext364And380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 animate, u8 transformDisabled, s32 arg7, u8 arg8);
void drawMenu_380(void);
void processLoadSaveMenuSub7Sub0(u8 index);
void updateCharacterInfoCard(u8 slot, u8 characterId);
void updateGoldPolys(s32 x, s32 y);
void initMenuContext33C_playTime(s32 x, s32 y);
void setupStatusMenuForCharacter3Sub0(u8 slot, u8 isGear);
void setupStatusMenuForCharacter3Sub1(u8 slot, u8 show, u8 onGear);
void setupStatusMenuForCharacter3Sub2(u8 slot, u8 alt, u8 compare, u8 hidden);
void setupStatusScreenEquipNames(u8 slot, u8 mode, u8 compare, u8 isGear);
s32 computeStatsMax(s32 unused, u16* stats1, u16* stats2);
s32 findNextPartySlot(s32 cur, u8 dir, u8 mode);

/* m_801D9808 */
u8 processSoundMenu(void);
void startMemoryCard(void);
u8 processLoadSaveMenuSub4(void);
void endMemoryCardSession(void);
void processLoadSaveMenuSub0(void);

/* m_801D9F98 */
u8 processLoadSaveMenu(u8 arg0, u8 arg1);
void initItemMenu(void);
void freeItemMenu(void);
void setupItemMenuPage(s32 page);
void setupItemMenuSelection(s32 sel, s32 page);
void allocMenuPointerGlyph(u8 arg0);
void updateMenuPointerGlyph(s32 index, s32 page, u8 mode, u8 slot);
void freeMenuPointerGlyph(u8 arg0);
u8 useItemOnParty(s32 page, s32 sel);
void swapInventorySlots(s32 a, s32 b);
void computeItemListScrollBar(); /* m_801DBE54 calls it with no arguments: the parameters are uninitialised locals */

/* m_801DBE54 */
u8 openItemMenu(void);
void freeEtherListMenu(u8 mode);
void runEtherListMenu(u8 slot, u8 flag, u8 mode);

/* m_801DE29C */
u8 openEtherMenu(u8 slot, u8 arg1);
void freeEquipMenu(void);
void freeStatusStatsAndEquipNames(void);
void computeGearMenuStats(u8 slot);
void runEquipMenu(u8 character, u8 arg1, u8 arg2);

/* m_801E0F78 */
u8 openEquipMenu(u8 character, u8 arg1);
u8 runSkillMenu(u8 slot);
u8 initGearStatusMenu(void); /* like Noah initStatusMenu */
void setupGearMenuLabels(u8 index);
void freeGearStatusMenu(void);

/* m_801E23CC */
u8 openGearMenu(void);
void initStatusMenu(void);
void freeStatusMenu(void);

/* m_801E2BE4 */
u8 openStatusMenu(void);

/* m_801E3088 */
void cleanupMainMenuSelection(u8 arg0);
s32 applyItemToCharacter(MenuContext_330* p, u8 character, u8 item);
void setupStatusMenuForCharacter1(MenuContext_330* ctx, u8 charId);
void setupStatusMenuForCharacter2(MenuContext_330* ctx, u8 charId);
void computeGearStatusStats(MenuContext_330* ctx, u8 gearId);
void refreshGearEquipment(MenuContext_330* ctx, u8 gearId);
void computeGearHealFuelCost(MenuContext_330* ctx, u8 gearId);
void buildSaveGameData(MenuSaveGame* save);
void restoreSaveGameData(MenuSaveGame* save, MenuContext_330* ctx);
void setupMenuContext364(u8 boxId);
void initMenuContext3A8_memoryCardDisplay(void);
void freeMemoryCardTiles(void);
void initMenuContext34C(void);
void freeMenuContext34C(void);
void clearSaveTitleImage(void);
void showMemoryCardSave(s32 cardSlot, u8 withSummary);
void loadMemoryCardIconToVram(s32 slot);
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode);
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count);

/* m_801E8018 */
void j_setupMenuContext4E0(u8 count, MenuContext_4E0* p, void* config, u8* flags);
void countField33C_C(u8 count, u8* arr);
void updateMenuSelection2(s32 count, MenuContext_4E0* labels, void* unused, s32* xOffsets, u8* flags, u8 sel, u8 base, u8 mode);
void setupMainMenuWithConfig(s32 count, u32 (*config)[2]);
void processLoadSaveMenuSub3(u8 row);
void updateMenuSelection(u8 count, u8 sel, u32 (*config)[2]);
void processLoadSaveMenuSub8(u8 row);
void renderCharacterNameInVram(u8 slot, u8 pos);
void setMenuPolyShadeMode(POLY_FT4* p, u8 mode);
void setMenuBoxShadeMode(u8 box, u8 mode);
void initMenuTiles1Sub(POLY_FT4* p);
void setupMenuPolyFT4(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u16 w, u16 h);
void initMemoryCardTransparentPoly(POLY_FT4* p);
void stopCdDrive(void);
s32 checkInsertedDisc(s32 disc);

#endif /* !MENU_FAMILY_OVERLAY */

#endif /* MENU_MENU_H */
