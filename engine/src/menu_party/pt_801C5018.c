/* The party (disc 10/06) menu overlay, one provisional file for now. It links the
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

#include "main/memory.h"
#include "pollhost.h"
void loadMenuSharedResources(void);

/* --- MF5 additions --- */
/* menu_party functions (twins of main-menu functions; main names in comments). */
void drawMenu_428(void);
void drawMenuBoxes(void);

/* --- end MF5 additions --- */
/* MF6 declarations */
void playSoundEffect(s32);
void checkSoftReboot(void);
extern s32 partyMenuInfocardNameU[];
extern s32 partyMenuInfocardNameV[];
extern s32 partyMenuCardLabelGlyphs[];
void drawMenu2_sub_sub0(void);
void drawMenu2_sub_sub1(void);
void drawMenu2_sub_sub2(void);
void drawPartyCard(MenuContext_1E08* p, u8 arg1);
void drawMenuSpecificElements(void);
void updateMenuInput(void);
void renderCharacterNamePairInVram(u8 slot, u8 row);
void setupPartyCardLabels(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep);
void setupPartyCardLevel(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep);
void setupPartyCardHpMp(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep);
void setupPartyCard(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep, u8 unused);
s32 isControllerConnected(s32 port);
void resumeMusic(void);
void pauseMusic(void);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField;
void initMenuContext32C(u8 arg0);
void initMenuContext33C(u8 arg0);
void initMenuContext350MainMenu(u8 arg0);
void initMenuContext354(u8 arg0);
void initMenuContext330(u8 arg0);
void initMenuContext348(u8 arg0);
void initPartyChangeCards(u8 arg0);
void initMenuGlobals(void);
void resetMenuOddOrEven(void);
void initMenuContext4E0(void);
void computeMenuBorders(void);
void initPrimitives_348_cursor(void);
void applyPartyChangeAndExit(void);
void runPartyChangeMenu(void);
u8 swapPartyMember(u8 side, s32 row, s32 scroll, u8 prevSide, s32 prevRow, s32 prevScroll);
void setupDraw428(u8 mode);
void disableDraw428(void);
void stopSequence(void*);
void unloadSequence(void*);
void menuDraw(void);
extern s32 partyChangeCursorX[];
extern s32 partyChangeCursorY[];
extern s32 partyReserveCardLayoutX[];
extern s32 partyActiveCardLayoutX[];
extern s32 partyReserveCardLayoutY[];
extern s32 partyActiveCardLayoutY[];
extern void* menuSharedResources;
extern void* fieldSeq;
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
char* strcpy(char* dst, const char* src);
void fillStringDisplayBuffer(u32 value);

extern u16 partyMenu2Bitmask[16]; /* 1 << bit */
extern MenuContext* gMenuContext;
s32 renderString(u8* text, u16* image, s16 width, s32 odd);
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode);
u8* getDialogParamPointer(u8* buf, s32 idx);
extern u8 partyMenuInitLabelConfig[];
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

/* Noah menu2CheckBitmask family: mask & partyMenu2Bitmask[bit] */
u16 menu2CheckBitmask(u16 arg0, u8 arg1) {
    return partyMenu2Bitmask[arg1] & arg0;
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

void initPartyChangeCards(u8 arg0) {
    s32 i;

    if (arg0) {
        for (i = 0; i < 6; i++) {
            gMenuContext->m1DF0_reserveCards[i] = allocateHeap((s32)sizeof(struct MenuContext_1E08), 0);
            bzero(gMenuContext->m1DF0_reserveCards[i], (s32)sizeof(struct MenuContext_1E08));
        }
        for (i = 0; i < 3; i++) {
            gMenuContext->m1E08_partyCards[i] = allocateHeap((s32)sizeof(struct MenuContext_1E08), 0);
            bzero(gMenuContext->m1E08_partyCards[i], (s32)sizeof(struct MenuContext_1E08));
        }
    } else {
        for (i = 0; i < 6; i++) {
            freeHeap(gMenuContext->m1DF0_reserveCards[i]);
        }
        for (i = 0; i < 3; i++) {
            freeHeap(gMenuContext->m1E08_partyCards[i]);
        }
    }
}

void loadMenuSharedResources(void) {
    TIM_IMAGE tim;
    s32 border[18];
    uptr* bundle;
    u8* data;

    bundle = menuSharedResources;
    bundle = (uptr*)pcRelocTable((u32*)bundle);
    data = mallocAndDecompress((void*)bundle[1], 1);
    OpenTIM((u32*)data);
    ReadTIM(&gMenuContext->m32C_memoryCardContext->mB80_iconTim);
#if VERSION_JP
    strcpy(gMenuContext->m32C_memoryCardContext->m4FCE_saveName, "BISLPS-00800");
#endif
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
    setupMenuContext4E0(&gMenuContext->m4E0_labels[0], partyMenuInitLabelConfig, 0, 4);
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

extern s32 partyMenuSaveCursorToSlot[];
extern s32 partyMenuMemoryCardTilesPositionX[];
extern s32 partyMenuMemoryCardTilesPositionY[];

void drawMenu_428(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0) {
        for (i = 0; i < 4; i++) {
            if (gMenuContext->m428_cardCursors->m140_enabled[i] != 0) {
                if (gMenuContext->m428_cardCursors->m144_followTile[i] != 0) {
                    setXY4(&gMenuContext->m428_cardCursors->m0_polys[0][i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]],
                           partyMenuMemoryCardTilesPositionX[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           partyMenuMemoryCardTilesPositionY[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           partyMenuMemoryCardTilesPositionX[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           partyMenuMemoryCardTilesPositionY[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                           partyMenuMemoryCardTilesPositionX[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                           partyMenuMemoryCardTilesPositionY[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10,
                           partyMenuMemoryCardTilesPositionX[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                           partyMenuMemoryCardTilesPositionY[partyMenuSaveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10);
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

/* Noah drawMenuBackground */
void drawMenuBackground(void) {
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m98_poly[gMenuContext->m308_oddOrEven]);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m140_backgroundDrawMode[gMenuContext->m308_oddOrEven]);
}

void drawPartyCard(MenuContext_1E08* p, u8 drawExtra) {
    s32 i;

    if (p->mBE7_enabled) {
        pcTextureClass(&p->m460_poly[p->mBE6_oddOrEven], PC_TEX_MENU_PORTRAIT);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m460_poly[p->mBE6_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m4B0_poly[p->mBE6_oddOrEven]);
        for (i = 0; i < p->mBE8_0Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m0_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        for (i = 0; i < p->mBE0_500Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m500_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        for (i = 0; i < p->mBE2_6E0Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m6E0_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        for (i = 0; i < p->mBE3_870Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m870_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        for (i = 0; i < p->mBE4_A00Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->mA00_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        for (i = 0; i < p->mBE5_AF0Length; i++) {
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->mAF0_polys[i * 2 + p->mBE6_oddOrEven]);
        }
        if (drawExtra) {
            for (i = 0; i < p->mBE9_2D0Length; i++) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m2D0_polys[i * 2 + p->mBE6_oddOrEven]);
            }
        }
    }
}

void drawMenu_1E08(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m46_draw1E08) {
        for (i = 0; i < 6; i++) {
            drawPartyCard(gMenuContext->m1DF0_reserveCards[i], 0);
        }
        for (i = 0; i < 3; i++) {
            drawPartyCard(gMenuContext->m1E08_partyCards[i], 1);
        }
    }
}

void drawMenu2_sub(void) {
    drawMenu2_sub_sub0();
    drawMenu2_sub_sub1();
    drawMenu2_sub_sub2();
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

/* menu_party's drawMenuSpecificElements (Noah name of the main-menu version) */
void drawMenuSpecificElements(void) {
    if (gMenuContext->m327_drawEnabled != 0) {
        drawMenu_380();
        drawMenu_428();
        drawMenu2_sub();
        drawMenu_1E08();
        drawMenuBoxes();
    }
    drawMenuBackground();
}

void playMenuSoundEffect(u8 sfx) {
    playSoundEffect((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | sfx);
}

void updateMenuInput(void) {
#if !VERSION_JP
    u8 paused;
    u8 retry;
    s32 time;
#endif
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
                break;
            }
            if (newPadButtonForField & 0x4000) {
                sel = 1;
                break;
            }
            if (newPadButtonForField & 0x8000) {
                sel = 2;
                break;
            }
            if (newPadButtonForField & 0x1000) {
                sel = 3;
                break;
            }
            if (newPadButtonForDialogs & 0x20) {
                sel = 4;
                break;
            }
            if (newPadButtonForDialogs & 0x40) {
                sel = 5;
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

void menuDraw(void) {
    MenuContext* ctx;
    s32 odd;

    updateMenuInput();
    checkSoftReboot();
    ctx = gMenuContext;
    ctx->m1D4_currentDrawContext = (ctx->m1D4_currentDrawContext == &ctx->m6C_drawContexts[0])
                                       ? &ctx->m6C_drawContexts[1]
                                       : &ctx->m6C_drawContexts[0];
    ctx->m308_oddOrEven = ctx->m308_oddOrEven == 0;
    ClearOTagR(ctx->m1D4_currentDrawContext->m70_ot, 0x10);
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

void renderCharacterNamePairInVram(u8 slot, u8 row) {
    RECT rect;
    u16* buf;

    buf = ALLOC_TEXT_IMAGE(0x3F6);
    bzero(buf, 0x3F6);
    renderString(gameState.m0_names[slot / 2 * 2], buf, 0x24, 0);
    renderString(gameState.m0_names[slot / 2 * 2 + 1], buf, 0x24, 1);
    rect.x = partyMenuInfocardNameU[row / 2] + 0x180;
    rect.y = partyMenuInfocardNameV[row / 2];
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
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

void applyPartyChangeAndExit(void) {
    s32 i;
    s32 n;

    for (i = 0, n = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            gameState.m1D34_currentParty[n] = gMenuContext->m33C_drawFlags->m30_partyMemberId[i];
            n++;
        }
    }
    while (n < 3) {
        gameState.m1D34_currentParty[n] = 0xFF;
        n++;
    }
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
        unloadSequence(gMenuContext->m2E4_musicSequence);
        freeHeap(gMenuContext->m2E4_musicSequence);
    }
    initPartyChangeCards(0);
    freeHeap(gMenuContext);
}

void setupReserveMembers(void) {
    s32 i;
    s32 j;
    s32 n;
    u8 reserve;

    for (i = 0, n = 0; i < 11; i++) {
        if (gMenuContext->m30C_characterAvailable[i]) {
            reserve = 1;
            for (j = 0; j < 3; j++) {
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[j] == i) {
                    reserve = 0;
                    break;
                }
            }
            if (reserve) {
                gMenuContext->m1E14_reserve[n++] = i;
            }
        }
    }
    for (; n < 11; n++) {
        gMenuContext->m1E14_reserve[n] = 0xFF;
    }
    for (i = 0; i < 11; i += 2) {
        renderCharacterNamePairInVram(i, i);
    }
}

/* The labels spell LV, HP/ and MP/ here (glyph 0x16 'M', both versions);
 * every other menu's copy of this table (menu partyCardLabelGlyphs) has 0x0E
 * 'E': EP. Original-game bug fixes: EP. */
#define PARTY_CARD_GLYPH(i) (((i) == 6 && pcGameplayOriginalBugFixes()) ? 0x0E : partyMenuCardLabelGlyphs[i])

void setupPartyCardLabels(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep) {
    s32 i;

    p->mBE8_0Length = 0;
    for (i = 0; i < 9; i++) {
        if (partyMenuCardLabelGlyphs[i] != 0xFFFF) {
            p->mBE8_0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, PARTY_CARD_GLYPH(i), &p->m0_polys[p->mBE8_0Length * 2],
                                             gMenuContext->m308_oddOrEven, xTable[i], rowStep * row + yTable[i], 0x1000);
        }
    }
    setupStringInPolyFT4(gMenuContext->m2DC_font, slot + 0x14E, p->m460_poly, gMenuContext->m308_oddOrEven, xTable[9],
                  rowStep * row + yTable[9], 0x1000);
    SetPolyFT4(&p->m4B0_poly[gMenuContext->m308_oddOrEven]);
    setRGB0(&p->m4B0_poly[gMenuContext->m308_oddOrEven], 0x80, 0x80, 0x80);
    SetSemiTrans(&p->m4B0_poly[gMenuContext->m308_oddOrEven], 0);
    p->m4B0_poly[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
    p->m4B0_poly[gMenuContext->m308_oddOrEven].clut = (slot % 2) ? textSpriteMode1 : textSpriteMode0;
    setXYWH(&p->m4B0_poly[gMenuContext->m308_oddOrEven], xTable[16], yTable[16] + rowStep * row, 0x48, 0xD);
    setUVWH(&p->m4B0_poly[gMenuContext->m308_oddOrEven], partyMenuInfocardNameU[slot / 2] * 4, partyMenuInfocardNameV[slot / 2], 0x48, 0xD);
}

void setupPartyCardLevel(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep) {
    s32 i;
    s32 k;

    fillStringDisplayBuffer(gameState.m26C_party[slot].m62_Level);
    p->mBE0_500Length = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[6 + i] != 0xFF) {
            p->mBE0_500Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[6 + i],
                                               &p->m500_polys[p->mBE0_500Length * 2], gMenuContext->m308_oddOrEven,
                                               i * 8 + xTable[10], rowStep * row + yTable[10], 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[slot].m63_Level2);
    p->mBE1_5F0Length = 0;
    for (i = 0, k = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[6 + i] != 0xFF) {
            p->mBE1_5F0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[6 + i],
                                               &p->m5F0_polys[p->mBE1_5F0Length * 2], gMenuContext->m308_oddOrEven,
                                               k * 8 + xTable[11], rowStep * row + yTable[11], 0x1000);
            k++;
        }
    }
    for (i = 0; i < p->mBE1_5F0Length; i++) {
        SetShadeTex(&p->m5F0_polys[i * 2 + gMenuContext->m308_oddOrEven], 0);
        setRGB0(&p->m5F0_polys[i * 2 + gMenuContext->m308_oddOrEven], 0, 0x80, 0);
    }
}

void setupPartyCardHpMp(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep) {
    s32 i;
    s32 k;

    fillStringDisplayBuffer(gameState.m26C_party[slot].m4C_HP);
    p->mBE2_6E0Length = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[6 + i] != 0xFF) {
            p->mBE2_6E0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[6 + i],
                                               &p->m6E0_polys[p->mBE2_6E0Length * 2], gMenuContext->m308_oddOrEven,
                                               i * 8 + xTable[12], rowStep * row + yTable[12], 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[slot].m4E_MaxHP);
    p->mBE3_870Length = 0;
    for (i = 0, k = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[6 + i] != 0xFF) {
            p->mBE3_870Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[6 + i],
                                               &p->m870_polys[p->mBE3_870Length * 2], gMenuContext->m308_oddOrEven,
                                               k * 8 + xTable[13], rowStep * row + yTable[13], 0x1000);
            k++;
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[slot].m50_MP);
    p->mBE4_A00Length = 0;
    for (i = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[7 + i] != 0xFF) {
            p->mBE4_A00Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[7 + i],
                                               &p->mA00_polys[p->mBE4_A00Length * 2], gMenuContext->m308_oddOrEven,
                                               i * 8 + xTable[14], rowStep * row + yTable[14], 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[slot].m52_MaxMP);
    p->mBE5_AF0Length = 0;
    for (i = 0, k = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[7 + i] != 0xFF) {
            p->mBE5_AF0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[7 + i],
                                               &p->mAF0_polys[p->mBE5_AF0Length * 2], gMenuContext->m308_oddOrEven,
                                               k * 8 + xTable[15], rowStep * row + yTable[15], 0x1000);
            k++;
        }
    }
}

void setupPartyCard(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep, u8 unused) {
    setupPartyCardLabels(p, slot, row, xTable, yTable, rowStep);
    setupPartyCardLevel(p, slot, row, xTable, yTable, rowStep);
    setupPartyCardHpMp(p, slot, row, xTable, yTable, rowStep);
    p->mBE9_2D0Length = 0;
    p->mBE7_enabled = 1;
    p->mBE6_oddOrEven = gMenuContext->m308_oddOrEven;
}

void setupPartyChangeCards(u8 first) {
    s32 i;
    s32 idx;
    MenuContext_1E08* card;
    s32* xTable;
    s32* yTable;

    xTable = partyActiveCardLayoutX;
    yTable = partyActiveCardLayoutY;
    for (i = 0; i < 3; i++) {
        card = gMenuContext->m1E08_partyCards[i];
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            setupPartyCard(card, gMenuContext->m33C_drawFlags->m30_partyMemberId[i], i, xTable, yTable, 0x38, 1);
        } else {
            card->mBE7_enabled = 0;
        }
    }
    xTable = partyReserveCardLayoutX;
    yTable = partyReserveCardLayoutY;
    gMenuContext->m33C_drawFlags->m46_draw1E08 = 1;
    for (i = 0; i < 6; i++) {
        idx = first + i;
        if (idx >= 11) {
            break;
        }
        card = gMenuContext->m1DF0_reserveCards[i];
        if (gMenuContext->m1E14_reserve[idx] != 0xFF) {
            menuDraw();
            setupPartyCard(card, gMenuContext->m1E14_reserve[idx], i, xTable, yTable, 0x20, 0);
        } else {
            card->mBE7_enabled = 0;
        }
    }
}

void setPartyChangeCursor(u8 side, s32 row, u8 slot) {
    s32 base;

    base = side ? 3 : 0;
    if (slot == 0) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys[0], gMenuContext->m308_oddOrEven,
                      partyChangeCursorX[base + row], partyChangeCursorY[base + row], 0x800);
    } else {
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys[1], gMenuContext->m308_oddOrEven,
                      partyChangeCursorX[base + row], partyChangeCursorY[base + row], 0x800);
    }
    gMenuContext->m428_cardCursors->m140_enabled[slot] = 1;
    gMenuContext->m428_cardCursors->m148_oddOrEven[slot] = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
}

extern s32 partyMenuCardCursorStartX[];
extern s32 partyMenuCardCursorStartY[];

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
                          gMenuContext->m308_oddOrEven, partyMenuCardCursorStartX[i], partyMenuCardCursorStartY[i], 0x800);
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

u8 swapPartyMember(u8 side, s32 row, s32 scroll, u8 prevSide, s32 prevRow, s32 prevScroll) {
    u8 partyOk;
    u8 reserveOk;
    u8 result;
    u8 member;
    u8 reserve;
    u8 tmp;
    u8 count;
    s32 i;

    result = 0;
    partyOk = 1;
    reserveOk = 1;
    if (side == 0) {
        member = row;
        reserve = prevRow + prevScroll;
    } else {
        member = prevRow;
        reserve = row + scroll;
    }
    if (gMenuContext->m33C_drawFlags->m30_partyMemberId[member] == 0xFF) {
        partyOk = 0;
    } else if (menu2CheckBitmask(gameState.m2318_partyFrameLock, gMenuContext->m33C_drawFlags->m30_partyMemberId[member])) {
        partyOk = 0;
    }
    if (gMenuContext->m1E14_reserve[reserve] == 0xFF) {
        reserveOk = 0;
    } else if (menu2CheckBitmask(gameState.m2318_partyFrameLock, gMenuContext->m1E14_reserve[reserve])) {
        reserveOk = 0;
    }
    if (partyOk && reserveOk) {
        tmp = gMenuContext->m33C_drawFlags->m30_partyMemberId[member];
        gMenuContext->m33C_drawFlags->m30_partyMemberId[member] = gMenuContext->m1E14_reserve[reserve];
        gMenuContext->m1E14_reserve[reserve] = tmp;
        for (i = 0, count = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                count++;
            }
        }
        result = 1;
        if (count == 0) {
            tmp = gMenuContext->m33C_drawFlags->m30_partyMemberId[member];
            gMenuContext->m33C_drawFlags->m30_partyMemberId[member] = gMenuContext->m1E14_reserve[reserve];
            gMenuContext->m1E14_reserve[reserve] = tmp;
            result = 0;
        }
    }
    return result;
}

void runPartyChangeMenu(void) {
    u8 loop;
    s32 row;
    s32 scroll;
    u8 side;
    u8 selected;
    s32 shownScroll;
    s32 prevRow;
    u8 prevSide;
    s32 prevScroll;
    s32 i;

    loop = 1;
    row = 0;
    scroll = 0;
    setupReserveMembers();
    iniMenuContext364And380(2, 0x78, 6, 0xB8, 0xC8, 1, 0, 4, 0);
    setupDraw428(1);
    shownScroll = 0xFF;
    side = 0;
    selected = 0;
    while (gMenuContext->m380_menuBoxAnims[2]->m11_done == 0) {
        menuDraw();
    }
    while (loop) {
        menuDraw();
        if (scroll != shownScroll) {
            setupPartyChangeCards(scroll);
            shownScroll = scroll;
        }
        setPartyChangeCursor(side, row, 0);
        switch (gMenuContext->m325_menuButton) {
        case 5:
            playMenuSoundEffect(3);
            if (selected) {
                selected = 0;
                gMenuContext->m428_cardCursors->m140_enabled[loop] = 0; /* loop == 1 here: retail indexes with it */
            } else {
                loop = 0;
            }
            break;
        case 4:
            if (!selected) {
                selected = 1;
                setPartyChangeCursor(side, row, 1);
                prevRow = row;
                row = 0;
                prevSide = side;
                side = !side;
                prevScroll = scroll;
                playMenuSoundEffect(2);
            } else if (swapPartyMember(side, row, scroll, prevSide, prevRow, prevScroll)) {
                selected = 0;
                shownScroll = 0xFF;
                gMenuContext->m428_cardCursors->m140_enabled[loop] = 0; /* loop == 1 here: retail indexes with it */
                playMenuSoundEffect(2);
            } else {
                playMenuSoundEffect(4);
            }
            break;
        case 1:
            if (side == 0) {
                playMenuSoundEffect(1);
                row++;
                if (row >= 3) {
                    row = 0;
                }
            } else {
                row++;
                if (row >= 6) {
                    row = 5;
                }
                playMenuSoundEffect(1);
            }
            break;
        case 3:
            if (side == 0) {
                playMenuSoundEffect(1);
                row--;
                if (row < 0) {
                    row = 2;
                }
            } else {
                row--;
                if (row < 0) {
                    row = 0;
                    scroll--;
                    if (scroll < 0) {
                        scroll = 0;
                        break;
                    }
                }
                playMenuSoundEffect(1);
            }
            break;
        case 2:
            if (!selected && side != 0) {
                playMenuSoundEffect(1);
                side = 0;
                row = 0;
            }
            break;
        case 0:
            if (!selected && side == 0) {
                playMenuSoundEffect(1);
                side = 1;
                row = 0;
            }
            break;
        }
    }
    disableDraw428();
    processLoadSaveMenuSub7Sub0(2);
    for (i = 0; i < 3; i++) {
        gameState.m1D34_currentParty[i] = gMenuContext->m33C_drawFlags->m30_partyMemberId[i];
    }
}

void partyChangeMenuEntryPoint(void) {
    initMenuContext32C(1);
    initMenuContext33C(1);
    initMenuContext350MainMenu(1);
    initMenuContext354(1);
    initMenuContext330(1);
    initMenuContext348(1);
    initPartyChangeCards(1);
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
    runPartyChangeMenu();
    applyPartyChangeAndExit();
}
