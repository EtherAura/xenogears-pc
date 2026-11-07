/* The menu_name_entry (disc 10/08) menu overlay, one provisional file for now. It links the
 * main menu's library (same code, own addresses: MENU_FAMILY_OVERLAY keeps
 * menu/menu.h's main-menu data and prototypes out). gcc-2.6.3-psx,
 * ASPSX 2.34, -G0. */
#include "common.h"
#include "pc_convert.h"
#define MENU_FAMILY_OVERLAY
#include "menu/menu.h"
#include "menu/shop.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pc_gameplay.h"
#include "pollhost.h"
#include "main/game_state.h"
#include "main/dialog.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "menu/shop.h"

/* --- MF5 additions --- */
#include "menu/shop.h"
/* menu_name_entry functions (twins of main-menu functions; main names in comments). */
void drawMenuBoxes(void);
void drawMenu_428(void);
void drawNameEntryScreen(void);
void drawMenu2_sub(void);

#include "main/filesystem.h"

#include "main/memory.h"
#include "pollhost.h"
void playMenuSoundEffect(u8 sfx);
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
extern void* menuSharedResources;
extern void* fieldSeq;
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
char* strcpy(char* dst, const char* src);
/* --- end MF5 additions --- */
/* MF6 declarations */
void stopSequence(void*);
void unloadSequence(void*);
void initMenuContext32C(u8 arg0);
void initMenuContext33C(u8 arg0);
void initMenuContext350MainMenu(u8 arg0);
void initMenuContext354(u8 arg0);
void initMenuContext330(u8 arg0);
void initMenuContext348(u8 arg0);
void initMenuContext1E20(u8 arg0);
void initMenuGlobals(void);
void resetMenuOddOrEven(void);
void initMenuContext4E0(void);
void computeMenuBorders(void);
void initPrimitives_348_cursor(void);
void menuDraw(void);
void shutdownNameEntryMenu(void);
void runNameEntryMenu(void);
void processLoadSaveMenuSub5(u8 msg);
void setupNameEntryGrid(void);
extern u8 nameEntryCharacterGrid[];
void initNameEntryScreen(void);
void renderNameEntryText(u8* text);
void initNameEntryBuffers(u8* dst, u8* name);
u8 getNameEntryLength(u8* p);
void setupDraw428(u8 mode);
void disableDraw428(void);
void processLoadSaveMenuSub1(void);
void processLoadSaveMenuSub1Alt2(void);
void processLoadSaveMenuSub7Sub0(u8 index);
void processLoadSaveMenuSub7(void);
#ifndef setXY3
#define setXY3(p, _x0, _y0, _x1, _y1, _x2, _y2) \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), (p)->x2 = (_x2), (p)->y2 = (_y2))
#endif
/* name entry grid (runNameEntryMenu cursor): character cell width, origin, rows */
#if VERSION_JP
#define NAME_GRID_CW 12
#define NAME_GRID_X 0x14
#define NAME_GRID_Y 0xE
#define NAME_GRID_ROWS 9
#else
#define NAME_GRID_CW 8
#define NAME_GRID_X 0x44
#define NAME_GRID_Y 0x2E
#define NAME_GRID_ROWS 5
#endif
/* width in pixels of one character of the typed name (JP glyphs are wider) */
#if VERSION_JP
#define NAME_GLYPH_WIDTH 12
#else
#define NAME_GLYPH_WIDTH 8
#endif

extern u16 nameEntryMenu2Bitmask[16]; /* 1 << bit */
extern MenuContext* gMenuContext;
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode);
u8* getDialogParamPointer(u8* buf, s32 idx);
extern u8 nameEntryInitMenuLabelConfig[];
void loadBlankImage(void);
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count);
void computeMenuBorder(void* font, s32 glyph, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpageX, s32* tpageY);
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
void playSoundEffect(s32);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void playMenuSoundEffect(u8 sfx);

/* Noah menu2CheckBitmask family: mask & nameEntryMenu2Bitmask[bit] */
u16 menu2CheckBitmask(u16 arg0, u8 arg1) {
    return nameEntryMenu2Bitmask[arg1] & arg0;
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

/* name length limits (mDE9_nameLength) for the two kinds of name (JP names are shorter) */
#if VERSION_JP
#define NAME_LEN_CHARACTER 6
#define NAME_LEN_OTHER 8
#else
#define NAME_LEN_CHARACTER 9
#define NAME_LEN_OTHER 10
#endif

/* menu_name_entry's loadMenuSharedResources (Noah name of the main-menu version,
 * func_801C65F4): no 0xE0 border glyph, save name "BISLPS-00800", party
 * member from g_menuOpenArg and the table bundle[6] */
void loadMenuSharedResources(void) {
    TIM_IMAGE tim;
    s32 border[18];
    uptr* bundle;
    u8* data;
    s32 i;
    u8 flag;

    flag = 1;
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
    gMenuContext->m1E24_nameSlotTable = mallocAndDecompress((void*)bundle[6], 1);
    if (g_menuOpenArg < 11) {
        gMenuContext->m1E20_nameEntry->mDE9_nameLength = NAME_LEN_CHARACTER;
        gMenuContext->m33C_drawFlags->m30_partyMemberId[0] = g_menuOpenArg;
    } else {
        gMenuContext->m1E20_nameEntry->mDE9_nameLength = NAME_LEN_OTHER;
        if (g_menuOpenArg - 11 == 9 && (gameState.m1D30_partyMemberBitField & 0x400)) {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[0] = 10;
            flag = 0;
        }
        if (flag) {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[0] = gMenuContext->m1E24_nameSlotTable[g_menuOpenArg - 11];
        }
    }
    gMenuContext->m1E28_nameSlots[0] = gMenuContext->m1E24_nameSlotTable[g_menuOpenArg * 3 + 0x20];
    gMenuContext->m1E28_nameSlots[1] = gMenuContext->m1E24_nameSlotTable[g_menuOpenArg * 3 + 0x21];
    gMenuContext->m1E28_nameSlots[2] = gMenuContext->m1E24_nameSlotTable[g_menuOpenArg * 3 + 0x22];
    freeHeap(gMenuContext->m1E24_nameSlotTable);
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
    freeHeap(menuSharedResources);
    freeHeap(bundle);
}

/* menu globals and the party (like menu func_801C6AA0, Noah initMenuGlobals) */
void initMenuGlobals(void) {
    s32 i;
    u16 mask;
    s32 id;

    gMenuContext->m336_menuSelectedEntry = 4;
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m326_cardPollTimer = 0x3C;
    gMenuContext->m334_cardOperation = 0;
    gMenuContext->m335 = 0;
    mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x7FF;
    for (i = 0; i < 16; i++) {
        if (menu2CheckBitmask(mask, i)) {
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

/* Noah initMenuContext4E0 */
void initMenuContext4E0(void) {
    initFontPalettes(0, 0x1D1);
    gMenuContext->m4E0_labels[0].m78_imageData = ALLOC_LABEL_IMAGE(0x38E);
    setupMenuContext4E0(&gMenuContext->m4E0_labels[0], nameEntryInitMenuLabelConfig, 0, 4);
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

extern s32 nameEntrySaveCursorToSlot[];
extern s32 nameEntryMemoryCardTilesPositionX[];
extern s32 nameEntryMemoryCardTilesPositionY[];

void drawMenu_428(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0) {
        for (i = 0; i < 4; i++) {
            if (gMenuContext->m428_cardCursors->m140_enabled[i] != 0) {
                if (gMenuContext->m428_cardCursors->m144_followTile[i] != 0) {
                    setXY4(&gMenuContext->m428_cardCursors->m0_polys[0][i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]],
                           nameEntryMemoryCardTilesPositionX[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           nameEntryMemoryCardTilesPositionY[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           nameEntryMemoryCardTilesPositionX[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           nameEntryMemoryCardTilesPositionY[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           nameEntryMemoryCardTilesPositionX[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           nameEntryMemoryCardTilesPositionY[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10,
                           nameEntryMemoryCardTilesPositionX[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           nameEntryMemoryCardTilesPositionY[nameEntrySaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10);
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

/* Noah drawMenuBackground */
void drawMenuBackground(void) {
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m98_poly[gMenuContext->m308_oddOrEven]);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m140_backgroundDrawMode[gMenuContext->m308_oddOrEven]);
}

/* draw m1E20 (menu_name_entry's name-entry screen?) */
void drawNameEntryScreen(void) {
    s32 i;
    s32 p;
    s32 flag;

    if (gMenuContext->m33C_drawFlags->m47_drawNameEntry) {
        if (gMenuContext->m1E20_nameEntry->mDE5_drawQuads) {
            RotTransPers4(&gMenuContext->m1E20_nameEntry->mDA0_portraitVerts[0], &gMenuContext->m1E20_nameEntry->mDA0_portraitVerts[1], &gMenuContext->m1E20_nameEntry->mDA0_portraitVerts[2],
                          &gMenuContext->m1E20_nameEntry->mDA0_portraitVerts[3],
                          (s32*)&gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m1E20_nameEntry->mDE1_oddOrEven].x0,
                          (s32*)&gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m1E20_nameEntry->mDE1_oddOrEven].x1,
                          (s32*)&gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m1E20_nameEntry->mDE1_oddOrEven].x2,
                          (s32*)&gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m1E20_nameEntry->mDE1_oddOrEven].x3, &p, &flag);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m1E20_nameEntry->mDE1_oddOrEven]);
            RotTransPers4(&gMenuContext->m1E20_nameEntry->mDC0_typedNameVerts[0], &gMenuContext->m1E20_nameEntry->mDC0_typedNameVerts[1], &gMenuContext->m1E20_nameEntry->mDC0_typedNameVerts[2],
                          &gMenuContext->m1E20_nameEntry->mDC0_typedNameVerts[3],
                          (s32*)&gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m1E20_nameEntry->mDE3_oddOrEven].x0,
                          (s32*)&gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m1E20_nameEntry->mDE3_oddOrEven].x1,
                          (s32*)&gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m1E20_nameEntry->mDE3_oddOrEven].x2,
                          (s32*)&gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m1E20_nameEntry->mDE3_oddOrEven].x3, &p, &flag);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m1E20_nameEntry->mDE3_oddOrEven]);
        }
        if (gMenuContext->m1E20_nameEntry->mDE6_draw) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->m0_buttonIcon1[gMenuContext->m1E20_nameEntry->mDE0_oddOrEven]);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->m50_buttonIcon2[gMenuContext->m1E20_nameEntry->mDE0_oddOrEven]);
            for (i = 0; i < gMenuContext->m1E20_nameEntry->mDEA_numA0; i++) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m1E20_nameEntry->mA0_label[i * 2 + gMenuContext->m1E20_nameEntry->mDE0_oddOrEven]);
            }
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->mD20_gridCursorTopRight[gMenuContext->m1E20_nameEntry->mDE4_oddOrEven]);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m1E20_nameEntry->mD50_gridCursorLeftBottom[gMenuContext->m1E20_nameEntry->mDE4_oddOrEven]);
            if (++gMenuContext->m1E20_nameEntry->mDE7_blinkTimer >= 0x3D) {
                gMenuContext->m1E20_nameEntry->mDE7_blinkTimer = 0;
            }
            if (gMenuContext->m1E20_nameEntry->mDE7_blinkTimer < 0x1E) {
                /* written index + array: the address adds come out (index << 4) + base */
                (gMenuContext->m308_oddOrEven + gMenuContext->m1E20_nameEntry->mD80_cursor)->x0 =
                    gMenuContext->m1E20_nameEntry->mDE8_cursorPos * NAME_GLYPH_WIDTH + 0x50;
                (gMenuContext->m308_oddOrEven + gMenuContext->m1E20_nameEntry->mD80_cursor)->y0 = 0xC6;
                (gMenuContext->m308_oddOrEven + gMenuContext->m1E20_nameEntry->mD80_cursor)->x1 =
                    gMenuContext->m1E20_nameEntry->mDE8_cursorPos * NAME_GLYPH_WIDTH + 0x50 + NAME_GLYPH_WIDTH;
                (gMenuContext->m308_oddOrEven + gMenuContext->m1E20_nameEntry->mD80_cursor)->y1 = 0xC6;
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m1E20_nameEntry->mD80_cursor[gMenuContext->m308_oddOrEven]);
            }
            for (i = 0; i < 36; i++) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m1E20_nameEntry->m190_grid[i * 2 + gMenuContext->m1E20_nameEntry->mDE2_oddOrEven]);
            }
        }
    }
}

void drawMenu2_sub(void) {
    drawMenu2_sub_sub0();
    drawMenu2_sub_sub1();
    drawMenu2_sub_sub2();
    drawMenu2_sub_sub3();
    drawMenu2_drawCheckingMemoryCard();
}

/* menu_name_entry's drawMenuSpecificElements (Noah name of the main-menu version) */
void drawMenuSpecificElements(void) {
    if (gMenuContext->m327_drawEnabled != 0) {
        drawMenu_380();
        drawMenu_428();
        drawMenu2_sub();
        drawNameEntryScreen();
        drawMenuBoxes();
    }
    drawMenuBackground();
}

/* Noah playMenuSoundEffect */
void playMenuSoundEffect(u8 sfx) {
    if (gMenuContext->m32A_soundEnabled) {
        playSoundEffect((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | sfx);
    }
}

/* read the pad into m325_menuButton (like menu func_801C7D78; adds 0x800 -> 11
 * and the MCB log toggle/page buttons) */
void updateMenuInput(void) {
#if !VERSION_JP
    u8 paused;
    u8 retry;
    s32 time;
#endif
    u8 sel;

    sel = 8;
#if !VERSION_JP /* JP has no controller-disconnect pause */
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
            if (newPadButtonForDialogs & 4) {
                sel = 10;
                break;
            }
            if (newPadButtonForDialogs & 8) {
                sel = 9;
                break;
            }
            if (newPadButtonForDialogs & 0x800) {
                sel = 11;
                break;
            }
            if (newPadButtonForDialogs & 0x100) {
                sel = 12;
                gMenuContext->m1E94_showMcbLog = gMenuContext->m1E94_showMcbLog == 0;
                break;
            }
            if (newPadButtonForDialogs & 1) {
                gMenuContext->m1E95_mcbLogPage++;
                break;
            }
        }
    }
    gMenuContext->m325_menuButton = sel;
}

/* Noah updateMenuTransforms */
void updateMenuTransforms(void) {
    switch (gMenuContext->m329_animationState) {
    case 4:
        gMenuContext->m1E0_menuTranslation.vz = 0x200;
        gMenuContext->m1D8_menuRotation.vz = 0;
        gMenuContext->m1D8_menuRotation.vy = 0;
        gMenuContext->m1D8_menuRotation.vx = 0;
        gMenuContext->m1E0_menuTranslation.vy = 0;
        gMenuContext->m1E0_menuTranslation.vx = 0;
        gMenuContext->m329_animationState = 2;
        break;
    case 3:
        gMenuContext->m1E0_menuTranslation.vz = 0x800;
        gMenuContext->m1D8_menuRotation.vz = 0;
        gMenuContext->m1D8_menuRotation.vy = 0;
        gMenuContext->m1D8_menuRotation.vx = 0;
        gMenuContext->m1E0_menuTranslation.vy = 0;
        gMenuContext->m1E0_menuTranslation.vx = 0;
        gMenuContext->m329_animationState = 1;
        break;
    case 2:
        gMenuContext->m1D8_menuRotation.vy -= 0x60;
        gMenuContext->m1E0_menuTranslation.vz += 0x40;
        if (gMenuContext->m1E0_menuTranslation.vz >= 0xE00) {
            gMenuContext->m329_animationState = 0;
        }
        break;
    case 1:
        gMenuContext->m1D8_menuRotation.vx += 0x7C;
        gMenuContext->m1E0_menuTranslation.vz -= 0x30;
        if (gMenuContext->m1E0_menuTranslation.vz < 0x200) {
            gMenuContext->m1E0_menuTranslation.vz = 0x200;
            gMenuContext->m1D8_menuRotation.vz = 0;
            gMenuContext->m1D8_menuRotation.vx = 0;
            gMenuContext->m1D8_menuRotation.vy = 0;
            gMenuContext->m329_animationState = 0;
        }
        break;
    }
    createRotationMatrix(&gMenuContext->m1D8_menuRotation, &gMenuContext->m1F0_menuMatrix);
    TransMatrix(&gMenuContext->m1F0_menuMatrix, &gMenuContext->m1E0_menuTranslation);
    SetRotMatrix(&gMenuContext->m1F0_menuMatrix);
    SetTransMatrix(&gMenuContext->m1F0_menuMatrix);
}

/* Noah menuDraw (the shop's; like menu func_801C7BF4) */
void menuDraw(void) {
    MenuContext* ctx;
    s32 odd;

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
    updateMenuTransforms();
    drawMenuSpecificElements();
    odd = gMenuContext->m308_oddOrEven == 0;
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&gMenuContext->m1D4_currentDrawContext->m0_drawEnv);
    PutDispEnv(&gMenuContext->m1D4_currentDrawContext->m5C_dispEnv);
    /* The captured field or worldmap behind the menu: the menu is wide even
     * when that screen was 4:3 (port/src/game/menu/wide.c). */
    pcWideNarrowBackdrop(1);
    MoveImage(&gMenuContext->m350_mainMenu->m1180_backgroundRect, 0, odd * 0xE0);
    pcWideNarrowBackdrop(0);
    menuWideLayout(&gMenuContext->m1D4_currentDrawContext->m70_ot[15]);
    DrawOTag(&gMenuContext->m1D4_currentDrawContext->m70_ot[15]);
}

extern s32 nameEntryCardCursorStartX[];
extern s32 nameEntryCardCursorStartY[];

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
                          gMenuContext->m308_oddOrEven, nameEntryCardCursorStartX[i], nameEntryCardCursorStartY[i], 0x800);
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

/* Noah processLoadSaveMenuSub1 (twin of func_801D1EB0 = Sub1Alt2) */
void processLoadSaveMenuSub1(void) {
    gMenuContext->m329_animationState = 3;
    playMenuSoundEffect(0x5B);
}

/* Noah processLoadSaveMenuSub1Alt2 (twin of func_801D1E80 = Sub1) */
void processLoadSaveMenuSub1Alt2(void) {
    gMenuContext->m329_animationState = 4;
    playMenuSoundEffect(0x5C);
}

/* 3-line message from the text bundle (like main-menu func_801D2F4C, Noah
 * processLoadSaveMenuSub5, without the box; UVs written directly) */
void processLoadSaveMenuSub5(u8 msg) {
    MenuContext_4E0* p;
    s32 i;
    s32 x;

    x = 0x48;
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
        initMemoryCardPolyVerts(p->m50_vertex, x, i * 16 + 0xA0, p->m7E_stringWidth, 0xD);
        setUV4(&p->m0_polys[gMenuContext->m308_oddOrEven], 0, (i / 2) * 0xD + 0x4E, p->m7E_stringWidth,
               (i / 2) * 0xD + 0x4E, 0, (i / 2) * 0xD + 0x4E + 0xD, p->m7E_stringWidth, (i / 2) * 0xD + 0x4E + 0xD);
        p->m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        p->m7F_transformEnabled = 1;
    }
    LoadImage(&gMenuContext->m1DE0_messageLines[0]->m70_rect, gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    LoadImage(&gMenuContext->m1DE0_messageLines[2]->m70_rect, gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    DrawSync(0);
    gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 1;
    freeHeap(gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    freeHeap(gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    menuDraw();
    menuDraw();
}

void processLoadSaveMenuSub7(void) {
    s32 i;

    gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 0;
    for (i = 0; i < 4; i++) {
        freeHeap(gMenuContext->m1DE0_messageLines[i]);
    }
    menuDraw();
}

void shutdownNameEntryMenu(void) {
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

void setupNameEntryGrid(void) {
    u16 codes[8];
    u8 text[16];
    RECT rect;
    u16* buf;
    s32 i;
    s32 j;

    buf = ALLOC_TEXT_IMAGE(0x2BE);
    for (i = 0; i < 74; i++) {
        SetPolyFT4(&gMenuContext->m1E20_nameEntry->m190_grid[i]);
        setRGB0(&gMenuContext->m1E20_nameEntry->m190_grid[i], 0x80, 0x80, 0x80);
        SetSemiTrans(&gMenuContext->m1E20_nameEntry->m190_grid[i], 0);
        SetShadeTex(&gMenuContext->m1E20_nameEntry->m190_grid[i], 1);
        gMenuContext->m1E20_nameEntry->m190_grid[i].clut = textSpriteMode0;
        gMenuContext->m1E20_nameEntry->m190_grid[i].tpage = GetTPage(0, 0, 0x180, 0);
    }
    for (i = 0; i < 36; i++) {
        for (j = 0; j < 5; j++) {
            codes[j] = nameEntryCharacterGrid[i * 6 + j];
        }
        decodeCharacterCodes(codes, text, 5);
        bzero(buf, 0x2BE);
        renderString(text, buf, 0x18, 0);
        rect.x = (i % 2) * 32 + 0x180;
        rect.y = (i / 2) * 13;
        rect.w = 0x1C;
        rect.h = 0xD;
        LoadImage(&rect, buf);
        setUVWH(&gMenuContext->m1E20_nameEntry->m190_grid[i * 2 + gMenuContext->m308_oddOrEven], (i % 2) * 128, (i / 2) * 13, 0x3C, 0xD);
#if VERSION_JP
        setXYWH(&gMenuContext->m1E20_nameEntry->m190_grid[i * 2 + gMenuContext->m308_oddOrEven], (i / 9) * 72 + 0x14, (i % 9) * 16 + 0xE,
                0x3C, 0xD);
#else
        setXYWH(&gMenuContext->m1E20_nameEntry->m190_grid[i * 2 + gMenuContext->m308_oddOrEven], (i / 9) * 48 + 0x44, (i % 9) * 16 + 0x2E,
                0x3C, 0xD);
#endif
        DrawSync(0);
    }
    freeHeap(buf);
    gMenuContext->m1E20_nameEntry->mDE2_oddOrEven = gMenuContext->m308_oddOrEven;
    setUVWH(&gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m308_oddOrEven], 0, 0xEA,
            gMenuContext->m1E20_nameEntry->mDE9_nameLength * NAME_GLYPH_WIDTH, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m1E20_nameEntry->mDC0_typedNameVerts, gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m308_oddOrEven].x0 + 0x50,
                  gMenuContext->m1E20_nameEntry->mCD0_typedName[gMenuContext->m308_oddOrEven].y0 + 0xB6,
                  gMenuContext->m1E20_nameEntry->mDE9_nameLength * NAME_GLYPH_WIDTH, 0xD);
    gMenuContext->m1E20_nameEntry->mDE3_oddOrEven = gMenuContext->m308_oddOrEven;
    for (i = 0; i < 2; i++) {
        SetLineF3(&gMenuContext->m1E20_nameEntry->mD20_gridCursorTopRight[i]);
        setRGB0(&gMenuContext->m1E20_nameEntry->mD20_gridCursorTopRight[i], 0, 0xFF, 0);
        SetLineF3(&gMenuContext->m1E20_nameEntry->mD50_gridCursorLeftBottom[i]);
        setRGB0(&gMenuContext->m1E20_nameEntry->mD50_gridCursorLeftBottom[i], 0, 0xFF, 0);
        SetLineF2(&gMenuContext->m1E20_nameEntry->mD80_cursor[i]);
        setRGB0(&gMenuContext->m1E20_nameEntry->mD80_cursor[i], 0, 0x80, 0);
    }
}

void initNameEntryScreen(void) {
    iniMenuContext364And380(3, 0x10, 0x9A, 0xC0, 0x3C, 1, 1, 4, 0);
    processLoadSaveMenuSub5(0x1D);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x14B, gMenuContext->m1E20_nameEntry->m140_portrait, gMenuContext->m308_oddOrEven, 0, 0, 0x1000);
    initMemoryCardPolyVerts(gMenuContext->m1E20_nameEntry->mDA0_portraitVerts, gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].x0 + 0x18,
                  gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].y0 + 0x9E,
                  gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].x1 - gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].x0,
                  gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].y3 - gMenuContext->m1E20_nameEntry->m140_portrait[gMenuContext->m308_oddOrEven].y0);
    gMenuContext->m1E20_nameEntry->mDE1_oddOrEven = gMenuContext->m308_oddOrEven;
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xF9, gMenuContext->m1E20_nameEntry->m0_buttonIcon1, gMenuContext->m308_oddOrEven, 0xE8, 0xB6, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xFC, gMenuContext->m1E20_nameEntry->m50_buttonIcon2, gMenuContext->m308_oddOrEven, 0xE0, 0xC6, 0x1000);
#if VERSION_JP
    gMenuContext->m1E20_nameEntry->mDEA_numA0 =
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0xF0, gMenuContext->m1E20_nameEntry->mA0_label, gMenuContext->m308_oddOrEven, 0x11C, 0x8E, 0x1000);
#else
    gMenuContext->m1E20_nameEntry->mDEA_numA0 =
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0xF0, gMenuContext->m1E20_nameEntry->mA0_label, gMenuContext->m308_oddOrEven, 0xF4, 0x6E, 0x1000);
#endif
    gMenuContext->m1E20_nameEntry->mDE0_oddOrEven = gMenuContext->m308_oddOrEven;
    setXYWH(&gMenuContext->m4E0_labels[0].m0_polys[gMenuContext->m308_oddOrEven], 0xF8, 0xB6, gMenuContext->m4E0_labels[0].m7E_stringWidth, 0xD);
    setXYWH(&gMenuContext->m4E0_labels[3].m0_polys[gMenuContext->m308_oddOrEven], 0xF0, 0xC6, gMenuContext->m4E0_labels[3].m7E_stringWidth, 0xD);
    gMenuContext->m4E0_labels[3].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    setupNameEntryGrid();
    gMenuContext->m1E20_nameEntry->mDE5_drawQuads = 1;
    gMenuContext->m33C_drawFlags->m47_drawNameEntry = 1;
}

void renderNameEntryText(u8* text) {
    RECT rect;
    u16* buf;

    buf = ALLOC_TEXT_IMAGE(0x3F6);
    bzero(buf, 0x3F6);
    renderString(text, buf, 0x24, 0);
    rect.x = 0x180;
    rect.y = 0xEA;
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
}

void initNameEntryBuffers(u8* dst, u8* name) {
    s32 i;

    for (i = 0; i < 0x14; i += 2) {
        dst[i] = 0x1C;
        dst[i + 1] = 0;
        name[0] = gameState.m0_names[g_menuOpenArg][i];
        name[1] = (gameState.m0_names[g_menuOpenArg] + 1)[i];
        name += 2;
    }
    dst[0x12] = 0x1F;
    dst[0x13] = 0;
}

u8 getNameEntryLength(u8* p) {
    s32 i;

    for (i = 0; i < 0x14; i += 2) {
        if (p[0] == 0x1F && p[1] == 0) {
            break;
        }
        p += 2;
    }
    return i / 2;
}

void runNameEntryMenu(void) {
    u8 codes[20];
    u8 text[20];
    s32 prevCol;
    s32 prevRow;
    s32 col;
    s32 row;
    u8 dirty;
    u8 loop;
#if VERSION_JP
    s32 c; /* JP stores c itself (no table rewrite), so it stays a word */
#else
    u8 c;
#endif
    s32 idx;
    s32 i;
    s32 j;
    s32 last;

    prevCol = 0xFF;
    prevRow = 0xFF;
    col = 22;
    row = NAME_GRID_ROWS - 1;
    initNameEntryBuffers(codes, text);
    dirty = 0;
    initNameEntryScreen();
    renderNameEntryText(text);
    loop = 1;
    processLoadSaveMenuSub1();
    while (gMenuContext->m329_animationState != 0) {
        menuDraw();
    }
#if VERSION_JP
    iniMenuContext364And380(2, 8, 6, 0x130, 0x98, 1, 0, 4, 0);
#else
    iniMenuContext364And380(2, 0x38, 0x26, 0xD0, 0x60, 1, 0, 4, 0);
#endif
    while (gMenuContext->m380_menuBoxAnims[2]->m11_done == 0) {
        menuDraw();
    }
    gMenuContext->m1E20_nameEntry->mDE6_draw = 1;
    gMenuContext->m33C_drawFlags->m34_drawLabels[0] = 1;
    gMenuContext->m33C_drawFlags->m34_drawLabels[3] = 1;
    setupDraw428(1);
    gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
    while (loop) {
        if (col != prevCol || row != prevRow) {
            setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys, gMenuContext->m308_oddOrEven,
                          col * NAME_GRID_CW + NAME_GRID_X + 4, row * 16 + NAME_GRID_Y + 8, 0x800);
            setXY3(&gMenuContext->m1E20_nameEntry->mD20_gridCursorTopRight[gMenuContext->m308_oddOrEven], col * NAME_GRID_CW + NAME_GRID_X, row * 16 + NAME_GRID_Y, col * NAME_GRID_CW + NAME_GRID_X + NAME_GRID_CW,
                   row * 16 + NAME_GRID_Y, col * NAME_GRID_CW + NAME_GRID_X + NAME_GRID_CW, row * 16 + NAME_GRID_Y + 0xC);
            setXY3(&gMenuContext->m1E20_nameEntry->mD50_gridCursorLeftBottom[gMenuContext->m308_oddOrEven], col * NAME_GRID_CW + NAME_GRID_X, row * 16 + NAME_GRID_Y, col * NAME_GRID_CW + NAME_GRID_X,
                   row * 16 + NAME_GRID_Y + 0xC, col * NAME_GRID_CW + NAME_GRID_X + NAME_GRID_CW, row * 16 + NAME_GRID_Y + 0xC);
            gMenuContext->m1E20_nameEntry->mDE4_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m428_cardCursors->m148_oddOrEven[0] = gMenuContext->m308_oddOrEven;
            prevCol = col;
            prevRow = row;
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
        }
        if (dirty) {
            renderNameEntryText(text);
            dirty = 0;
        }
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 5:
            if (gMenuContext->m1E20_nameEntry->mDE8_cursorPos) {
                gMenuContext->m1E20_nameEntry->mDE8_cursorPos--;
            }
            {
                s32 blank = 0xF; /* an SImode 0xF: loop.c hoists it ahead of &codes, like retail */

                codes[gMenuContext->m1E20_nameEntry->mDE8_cursorPos * 2] = blank;
            }
            codes[gMenuContext->m1E20_nameEntry->mDE8_cursorPos * 2 + 1] = 0;
            decodeCharacterCodes((u16*)codes, text, getNameEntryLength(codes));
            dirty = 1;
            break;
        case 4:
            idx = (row + (col / 6) * 9) * 6 + col % 6;
            c = nameEntryCharacterGrid[idx];
            if (c == 0x1F) {
                if (codes[0] != 0xF) {
                    loop = 0;
                }
            } else if (gMenuContext->m1E20_nameEntry->mDE8_cursorPos < gMenuContext->m1E20_nameEntry->mDE9_nameLength) {
#if !VERSION_JP
                if (c == 0xCF || c == 0xF) {
                    nameEntryCharacterGrid[idx] = 0xC3;
                }
#endif
                codes[gMenuContext->m1E20_nameEntry->mDE8_cursorPos * 2] = nameEntryCharacterGrid[idx];
                codes[gMenuContext->m1E20_nameEntry->mDE8_cursorPos * 2 + 1] = 0;
                decodeCharacterCodes((u16*)codes, text, getNameEntryLength(codes));
                dirty = 1;
                gMenuContext->m1E20_nameEntry->mDE8_cursorPos++;
                playMenuSoundEffect(2);
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 11:
            if (codes[0] != 0xF) {
                loop = 0;
            }
            break;
        case 0:
            col++;
            idx = (row + (col / 6) * 9) * 6 + col % 6;
            c = nameEntryCharacterGrid[idx];
            if (c == 0xFF) {
                col = 0;
            } else if (c == 0xF || c == 0xCF) {
                col++;
            }
            break;
        case 2:
            col--;
            if (col < 0) {
                col = 22;
            } else {
                idx = (row + (col / 6) * 9) * 6 + col % 6;
            c = nameEntryCharacterGrid[idx];
                if (c == 0xF || c == 0xCF) {
                    col--;
                }
            }
            break;
        case 1:
            row++;
            if (row >= NAME_GRID_ROWS) {
                row = 0;
            }
            idx = (row + (col / 6) * 9) * 6 + col % 6;
            c = nameEntryCharacterGrid[idx];
            if (c == 0xCF) {
                row = 0;
            } else if (c == 0xF) {
                row++;
            }
            break;
        case 3:
            row--;
            if (row < 0) {
                row = NAME_GRID_ROWS - 1;
            }
            idx = (row + (col / 6) * 9) * 6 + col % 6;
            c = nameEntryCharacterGrid[idx];
            if (c == 0xF || c == 0xCF) {
                row--;
            }
            break;
        case 9:
            if (++gMenuContext->m1E20_nameEntry->mDE8_cursorPos > gMenuContext->m1E20_nameEntry->mDE9_nameLength) {
                gMenuContext->m1E20_nameEntry->mDE8_cursorPos--;
            }
            break;
        case 10:
            if (gMenuContext->m1E20_nameEntry->mDE8_cursorPos) {
                gMenuContext->m1E20_nameEntry->mDE8_cursorPos--;
            }
            break;
        }
    }
    for (i = 0, last = 0; i < 3; i++) {
        for (j = 0; j < 20; j++) {
            gameState.m0_names[gMenuContext->m1E28_nameSlots[i]][j] = 0;
        }
        for (j = 0; j < 18; j++) {
            gameState.m0_names[gMenuContext->m1E28_nameSlots[i]][j] = text[j];
            if (text[j] == 0) {
                break;
            }
#if !VERSION_JP
            /* Trailing blanks are trimmed. 0x4F is the JP font's blank (the
             * code table's unused-slot glyph), kept by the US version, where
             * 0x4F is 's' (its slots decode to 0 and its space is 0x10): a
             * name's final 's' was cut. Original-game bug fixes: the US blank. */
            if (text[j] != (pcGameplayOriginalBugFixes() ? 0x10 : 0x4F)) {
#else
            if (text[j] != 0x4F) {
#endif
                last = j;
            }
        }
        for (j = last + 1; j < 20; j++) {
            gameState.m0_names[gMenuContext->m1E28_nameSlots[i]][j] = 0;
        }
    }
    gMenuContext->m33C_drawFlags->m34_drawLabels[0] = 0;
    gMenuContext->m33C_drawFlags->m34_drawLabels[3] = 0;
    disableDraw428();
    gMenuContext->m1E20_nameEntry->mDE6_draw = 0;
    processLoadSaveMenuSub7Sub0(2);
    processLoadSaveMenuSub1Alt2();
    while (gMenuContext->m1E0_menuTranslation.vz < 0x600) {
        menuDraw();
    }
    gMenuContext->m33C_drawFlags->m47_drawNameEntry = 0;
    processLoadSaveMenuSub7();
    processLoadSaveMenuSub7Sub0(3);
}

void nameEntryMenuEntryPoint(void) {
    initMenuContext32C(1);
    initMenuContext33C(1);
    initMenuContext350MainMenu(1);
    initMenuContext354(1);
    initMenuContext330(1);
    initMenuContext348(1);
    initMenuContext1E20(1);
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
    gMenuContext->m327_drawEnabled = 1;
    gMenuContext->m32A_soundEnabled = 1;
    runNameEntryMenu();
    shutdownNameEntryMenu();
}
