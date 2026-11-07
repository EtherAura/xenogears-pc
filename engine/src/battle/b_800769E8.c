/*
 * Battle text polys, monster names and battle code execution (Noah
 * battle.cpp: battleSetupStringInPolyFT4*, setupStringsForMonsterNames,
 * executeBattleCode_*). 0x800769E8-0x80079674; gcc-2.6.3-psx, ASPSX 2.34.
 */
#include "common.h"
#include "pc_gameplay.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "battle/battle.h"

/* main */
s32 setupStringInPolyFT4(void* font, s32 glyphIndex, POLY_FT4 (*polys)[2], s32 oddOrEven, s16 x, s16 y, u16 scale);
extern s32 dialogWindowBlendMode;   /* dialogBattleWindowAbr */
extern u16 textSpriteMode1;   /* textSpriteMode1 */
extern u16 textSpriteMode0;   /* textSpriteMode0 */
extern u8 dialogWindowColor[3]; /* dialogWindowColor */
extern u8 battleTickMain_var0;                     /* battleTickMain_var0 */
extern void* battleFont;                  /* battleFont */
extern u16 bBattleTickMode0;
extern u8 battleForceOnGear;
extern s32* pRunningOnDTL;
void performAttackSub2(void);
void computeMenuBorder(void* font, s32 glyphIndex, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpX, s32* tpY);
extern u8* currentBattleSpecificStrings;                    /* battle strings */
extern u8 isEntityReadyForBattle[11];                 /* isEntityReadyForBattle */
extern u8 battleMonsterMapping[11];                 /* battleMonsterMapping */
extern u16 entitiesHitInCurrentAttackBF[];                  /* entitiesHitInCurrentAttackBF (retail keeps its address in a register: an array) */
void battleRenderDebugAndMain(void);
void* allocateBattleHeap(s32 size, s32 flags);
void* allocateTextureRamForText(s32 size);
void executeBattleCode_showMonsterName(u8 idx, u8 entity);
void performAttackSub6(u8 a);
void initJumpData(u8 a, u8 b);
void moveEntityToOtherEntity(u8 a, u8 b);
void moveEntityToShootEntity(u8 a, u8 b);
u16 bitmaskCharacterCheck(u16 mask, u8 i);

void loadImageSync(RECT* rect, void* data) {
    LoadImage(rect, data);
    DrawSync(0);
}

s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y) {
    return setupStringInPolyFT4(battleFont, character, polys, D_800CCB04.m30_battleOddOrEven, x, y, 0x1000);
}

s32 battleSetupStringInPolyFT4Small(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
s32 battleSetupStringInPolyFT4Small(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y) {
    return setupStringInPolyFT4(battleFont, character, polys, D_800CCB04.m30_battleOddOrEven, x, y, 0x800);
}

void battleSetupTextPolySub(POLY_FT4* p) {
    SetSemiTrans(p, 1);
    SetShadeTex(p, 0);
}

void drawBattleDialogWindow_sub0Sub(POLY_FT4* p) {
    battleSetupTextPolySub(p);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
    if (dialogWindowBlendMode) {
        p->tpage |= 0x40;
    } else {
        p->tpage &= ~0x40;
    }
}

void battleSetupTextPolyAdditive(POLY_FT4* p) {
    battleSetupTextPolySub(p);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
    p->tpage |= 0x20;
}

void battleSetupTextPolyAdditiveDim(POLY_FT4* p) {
    battleSetupTextPolySub(p);
    p->r0 = 0x40;
    p->g0 = 0x40;
    p->b0 = 0x40;
    p->tpage |= 0x20;
}

void battleSetupTextPolySubtractive(POLY_FT4* p) {
    battleSetupTextPolySub(p);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
    p->tpage |= 0x40;
}

void battleSetupTextPoly(POLY_FT4* p) {
    battleSetupTextPolySub(p);
    p->r0 = 0x40;
    p->g0 = 0x40;
    p->b0 = 0x40;
    p->tpage |= 0x40;
}

void repositionTextRenderingPlanePrim(POLY_FT4* p, s16 x, s16 y, u8 u, u8 v, u8 width) {
    p->x0 = x;
    p->y0 = y;
    p->y1 = y;
    p->x2 = x;
    p->y2 = y + 13;
    p->y3 = y + 13;
    p->u0 = u;
    p->u2 = u;
    p->x1 = x + width;
    p->x3 = x + width;
    p->v0 = v;
    p->u1 = u + width;
    p->v1 = v;
    p->v2 = v + 13;
    p->u3 = u + width;
    p->v3 = v + 13;
}

void setupMenuPolyFT4(POLY_FT4* p, s16 x, s16 y, u8 u, u8 v, s32 w, s32 h) {
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

void battleInitPoly4(POLY_FT4* p, u8 odd, u8 mode);
/* Noah: battleInitPoly4. The clut store goes through a pointer taken
 * before the choice and a u16 temporary: loop keeps that pointer as a copy
 * of the walk register (retail's move v1,s0) and the store sits after the
 * join (reorg copies it into the j delay slot). */
void battleInitPoly4(POLY_FT4* polys, u8 flag, u8 mode) {
    POLY_FT4* p;
    u16 clut;
    s32 i;

    for (i = 0; i < 2; i++) {
        SetPolyFT4(&polys[i]);
        polys[i].r0 = 0x80;
        polys[i].g0 = 0x80;
        polys[i].b0 = 0x80;
        SetSemiTrans(&polys[i], 0);
        SetShadeTex(&polys[i], 1);
        switch (mode) {
        case 0:
            polys[i].tpage = GetTPage(0, 0, 0x380, 0);
            break;
        case 1:
            polys[i].tpage = GetTPage(0, 0, 0x380, 0x100);
            break;
        case 2:
            polys[i].tpage = GetTPage(0, 0, 0x3C0, 0x100);
            break;
        case 3:
            polys[i].tpage = GetTPage(0, 0, 0x3C0, 0);
            break;
        }
        p = &polys[i];
        clut = flag ? textSpriteMode1 : textSpriteMode0;
        p->clut = clut;
    }
}

/* Renders the 11 target names (normal and odd-line CLUT copies) into VRAM
 * at (0x3DE, 0x10D + 13i) and fills their glyph rects, then uploads the ten
 * item label images (Noah: part of batteLoaderPhase3 setup). Retail addresses
 * the local table, like itemLabelsIds, as an array of sBattleTextImage
 * (symbol+index addressing, base-first adds); a plain u16* array does not
 * come out that way. */
void loadItemTargetsLabel(void) {
    sBattleTextImage images[11];
    RECT rect;
    s32 i;

    for (i = 0; i < 11; i++) {
        images[i].m0_image = allocateTextureRamForText(0x1B);
        bzero(images[i].m0_image, 0x30C);
        battleLabelGlyphRects[i].m0_width = renderString(getItemLabelString(i + 10), images[i].m0_image, 0x1B, 0);
        battleLabelGlyphRects[i + 11].m0_width = renderString(getItemLabelString(i + 0x15), images[i].m0_image, 0x1B, 1);
        battleLabelGlyphRects[i].m2_u = battleLabelGlyphRects[i + 11].m2_u = 0x78;
        battleLabelGlyphRects[i].m3_v = battleLabelGlyphRects[i + 11].m3_v = i * 13 + 13;
        battleLabelGlyphRects[i].m1_clut = 0;
        battleLabelGlyphRects[i + 11].m1_clut = 1;
        rect.x = 0x3DE;
        rect.y = i * 13 + 0x10D;
        rect.w = 0x1E;
        rect.h = 0xD;
        loadImageSync(&rect, images[i].m0_image);
    }
    for (i = 0; i < 11; i++) {
        freeHeap(images[i].m0_image);
    }
    for (i = 0; i < 10; i++) {
        rect.x = i * 2 + 0x3DE;
        rect.y = 0x100;
        rect.w = 6;
        rect.h = 0xD;
        loadImageSync(&rect, itemLabelsIds[i].m0_image);
    }
}

void initBattleVar0_A230_content(void);
void initBattleVar0_A230_content(void) {
    battleVar0->mA230_menuPanel->m669_drawEPCost = 0;
    battleVar0->mA230_menuPanel->m66B_drawSelection = 0;
    battleVar0->mA230_menuPanel->m66D_drawDescription = 0;
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[0], 0, 1);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[1], 0, 1);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[2], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[3], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[4], 0, 1);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[5], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[6], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[7], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[8], 0, 3);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[9], 1, 3);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[10], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[11], 1, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[12], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[13], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[14], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[15], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[16], 0, 2);
    battleInitPoly4(battleVar0->mA230_menuPanel->m0_polys[17], 0, 2);
}

void setupBattleDialogWindow_sub0(POLY_FT4* polys, u8 idx);
void setupBattleDialogWindow_sub0(POLY_FT4* polys, u8 idx) {
    s32 i;

    for (i = 0; i < 4; i++) {
        POLY_FT4* p = &polys[i];
        SetPolyFT4(p);
        SetShadeTex(p, 1);
        p->r0 = 0xFF;
        p->g0 = 0xFF;
        p->b0 = 0xFF;
        p->tpage = GetTPage(battleVar0->mA234_glyphTextures[idx].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[idx].m10_tpage_X, battleVar0->mA234_glyphTextures[idx].m14_tpage_Y);
        p->clut = GetClut(battleVar0->mA234_glyphTextures[idx].m8_clutX, battleVar0->mA234_glyphTextures[idx].mC_clutY);
    }
}

void setupBattleDialogWindow(u8 idx);
void setupBattleDialogWindow(u8 idx) {
    sBattleDialogWindow* w = battleDialogWindows[idx];
    u8 i;

    for (i = 0; i < 2; i++) {
        SetPolyG4(&w->m3C0_polyG4[i]);
        setRGB0(&w->m3C0_polyG4[i], dialogWindowColor[0], dialogWindowColor[1], dialogWindowColor[2]);
        setRGB1(&w->m3C0_polyG4[i], dialogWindowColor[0], dialogWindowColor[1], dialogWindowColor[2]);
        setRGB2(&w->m3C0_polyG4[i], dialogWindowColor[0], dialogWindowColor[1], dialogWindowColor[2]);
        setRGB3(&w->m3C0_polyG4[i], dialogWindowColor[0], dialogWindowColor[1], dialogWindowColor[2]);
        SetSemiTrans(&w->m3C0_polyG4[i], 1);
        SetDrawMode(&w->m408_drawMode[i], 0, 0, GetTPage(0, dialogWindowBlendMode, battleVar0->mA234_glyphTextures[1].m10_tpage_X, battleVar0->mA234_glyphTextures[1].m14_tpage_Y), &battleVar1->m0_textureWindow);
    }
    setupBattleDialogWindow_sub0(w->m140_topEdge[0], 1);
    setupBattleDialogWindow_sub0(w->m1E0_bottomEdge[0], 2);
    setupBattleDialogWindow_sub0(w->m280_leftEdge[0], 3);
    setupBattleDialogWindow_sub0(w->m320_rightEdge[0], 4);
}

void initBattleVar0_A230(void);
void initBattleVar0_A230(void) {
    sBattleVar0_a230* p = allocateBattleHeap((s32)sizeof(sBattleVar0_a230), 0);
    battleVar0->mA230_menuPanel = p;
    bzero(p, (s32)sizeof(sBattleVar0_a230));
    initBattleVar0_A230_content();
}

void freeBattleVar0_A230(void);
void freeBattleVar0_A230(void) {
    battleRenderDebugAndMain();
    freeHeap(battleVar0->mA230_menuPanel);
}

void initBattleG3Array(void);
void initBattleG3Array(void) {
    s32 i, j;

    bzero(battleG3, sizeof(sBattle800c3e24));
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            SetPolyG3(&((POLY_G3*)battleG3->m0_poly)[i * 2 + j]);
            switch (i) {
            case 0:
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x0 = 0xC0;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y0 = 0x70;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x1 = 0xB0;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y1 = 0x78;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x2 = 0xB0;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y2 = 0x68;
                break;
            case 1:
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x0 = 0xA0;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y0 = 0x90;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x1 = 0x98;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y1 = 0x80;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x2 = 0xA8;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y2 = 0x80;
                break;
            case 2:
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x0 = 0x80;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y0 = 0x70;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x1 = 0x90;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y1 = 0x68;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x2 = 0x90;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y2 = 0x78;
                break;
            case 3:
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x0 = 0xA0;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y0 = 0x50;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x1 = 0x98;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y1 = 0x60;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].x2 = 0xA8;
                ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].y2 = 0x60;
                break;
            }
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].r0 = 0xFF;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].g0 = 0;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].b0 = 0;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].r1 = 0x40;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].g1 = 0;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].b1 = 0;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].r2 = 0x40;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].g2 = 0;
            ((POLY_G3*)battleG3->m0_poly)[i * 2 + j].b2 = 0;
        }
    }
    /* not a plain field store: retail keeps it ahead of the battleG3 reload */
    battleG3->mE0_fadeValue = 0xFF; /* at another offset on the PC */
    battleG3->mE5_fadeDirection = 1;
    battleG3->mE4_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    battleVar1->mC6_isTargetSelectionCursorVisible = 1;
}

void removeTargetSelectionCursors(void) {
    battleVar1->mC6_isTargetSelectionCursorVisible = 0;
}

void init8920(void);
void init8920(void) {
    u16* p0;
    u16* p1;
    u16* p2;
    u16* p3;

    setRECT(&battleVar0->m8950_clutRects[0], 1, battleVar0->mA234_glyphTextures[0].mC_clutY - 1, 0xC6, 1);
    setRECT(&battleVar0->m8950_clutRects[1], 1, battleVar0->mA234_glyphTextures[0].mC_clutY, 0xC6, 1);
    setRECT(&battleVar0->m8950_clutRects[2], 1, battleVar0->mA234_glyphTextures[0].mC_clutY - 2, 0xC6, 1);
    setRECT(&battleVar0->m8950_clutRects[3], 1, battleVar0->mA234_glyphTextures[0].mC_clutY - 3, 0xC6, 1);
    p0 = battleVar0->m8970_cluts[0][0];
    p1 = battleVar0->m8970_cluts[1][0];
    p2 = battleVar0->m8970_cluts[2][0];
    p3 = battleVar0->m8970_cluts[3][0];
    StoreImage(&battleVar0->m8950_clutRects[0], p0);
    StoreImage(&battleVar0->m8950_clutRects[1], p1);
    StoreImage(&battleVar0->m8950_clutRects[2], p2);
    StoreImage(&battleVar0->m8950_clutRects[3], p3);
    StoreImage(&battleVar0->m8950_clutRects[0], p0 + 0xC6);
    StoreImage(&battleVar0->m8950_clutRects[1], p1 + 0xC6);
    StoreImage(&battleVar0->m8950_clutRects[2], p2 + 0xC6);
    StoreImage(&battleVar0->m8950_clutRects[3], p3 + 0xC6);
    StoreImage(&battleVar0->m8950_clutRects[0], p0 + 0xC6 * 2);
    StoreImage(&battleVar0->m8950_clutRects[1], p1 + 0xC6 * 2);
    StoreImage(&battleVar0->m8950_clutRects[2], p2 + 0xC6 * 2);
    StoreImage(&battleVar0->m8950_clutRects[3], p3 + 0xC6 * 2);
    StoreImage(&battleVar0->m8950_clutRects[0], p0 + 0xC6 * 3);
    StoreImage(&battleVar0->m8950_clutRects[1], p1 + 0xC6 * 3);
    StoreImage(&battleVar0->m8950_clutRects[2], p2 + 0xC6 * 3);
    StoreImage(&battleVar0->m8950_clutRects[3], p3 + 0xC6 * 3);
    computeMenuBorder(battleFont, 0x4B, &battleVar0->mA234_glyphTextures[1].m0_polyCount, &battleVar0->mA234_glyphTextures[1].m4_tpage_tp, &battleVar0->mA234_glyphTextures[1].m8_clutX, &battleVar0->mA234_glyphTextures[1].mC_clutY, &battleVar0->mA234_glyphTextures[1].m10_tpage_X, &battleVar0->mA234_glyphTextures[1].m14_tpage_Y);
    computeMenuBorder(battleFont, 0x50, &battleVar0->mA234_glyphTextures[2].m0_polyCount, &battleVar0->mA234_glyphTextures[2].m4_tpage_tp, &battleVar0->mA234_glyphTextures[2].m8_clutX, &battleVar0->mA234_glyphTextures[2].mC_clutY, &battleVar0->mA234_glyphTextures[2].m10_tpage_X, &battleVar0->mA234_glyphTextures[2].m14_tpage_Y);
    computeMenuBorder(battleFont, 0x4D, &battleVar0->mA234_glyphTextures[3].m0_polyCount, &battleVar0->mA234_glyphTextures[3].m4_tpage_tp, &battleVar0->mA234_glyphTextures[3].m8_clutX, &battleVar0->mA234_glyphTextures[3].mC_clutY, &battleVar0->mA234_glyphTextures[3].m10_tpage_X, &battleVar0->mA234_glyphTextures[3].m14_tpage_Y);
    computeMenuBorder(battleFont, 0x4E, &battleVar0->mA234_glyphTextures[4].m0_polyCount, &battleVar0->mA234_glyphTextures[4].m4_tpage_tp, &battleVar0->mA234_glyphTextures[4].m8_clutX, &battleVar0->mA234_glyphTextures[4].mC_clutY, &battleVar0->mA234_glyphTextures[4].m10_tpage_X, &battleVar0->mA234_glyphTextures[4].m14_tpage_Y);
    battleVar1->m0_textureWindow.y = 0;
    battleVar1->m0_textureWindow.x = 0;
    battleVar1->m0_textureWindow.h = 0x100;
    battleVar1->m0_textureWindow.w = 0x100;
    battleVar0->mA234_glyphTextures[1].m10_tpage_X = 0x3C0;
    battleVar0->mA234_glyphTextures[2].m10_tpage_X = 0x3C8;
    battleVar0->mA234_glyphTextures[1].m14_tpage_Y = 0x34;
    battleVar0->mA234_glyphTextures[2].m14_tpage_Y = 0x34;
    setRECT(&battleVar1->m8_glyphTexture0Window, ((u16)battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) * 2, battleVar0->mA234_glyphTextures[0].m14_tpage_Y, 0x100, 0x100);
    setRECT(&battleVar1->m10_glyphTexture1Window, ((u16)battleVar0->mA234_glyphTextures[1].m10_tpage_X & 0x3F) * 2, battleVar0->mA234_glyphTextures[1].m14_tpage_Y, 8, 0x10);
    setRECT(&battleVar1->m18_glyphTexture2Window, ((u16)battleVar0->mA234_glyphTextures[2].m10_tpage_X & 0x3F) * 2, battleVar0->mA234_glyphTextures[2].m14_tpage_Y, 8, 0x10);
    setRECT(&battleVar1->m20_glyphTexture3Window, ((u16)battleVar0->mA234_glyphTextures[3].m10_tpage_X & 0x3F) * 2 + 0xE, battleVar0->mA234_glyphTextures[3].m14_tpage_Y, 0x10, 8);
    setRECT(&battleVar1->m28_glyphTexture4Window, ((u16)battleVar0->mA234_glyphTextures[4].m10_tpage_X & 0x3F) * 2 + 0xE, battleVar0->mA234_glyphTextures[4].m14_tpage_Y, 0x10, 8);
    SetDrawMode(&battleVar0->m8908_drMode[0], 0, 0, GetTPage(battleVar0->mA234_glyphTextures[0].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y), &battleVar1->m8_glyphTexture0Window);
    SetDrawMode(&battleVar0->m8908_drMode[1], 0, 0, GetTPage(battleVar0->mA234_glyphTextures[0].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y), &battleVar1->m8_glyphTexture0Window);
    SetDrawMode(&battleVar0->m8920_fullWindowDrMode[0], 0, 0, GetTPage(battleVar0->mA234_glyphTextures[0].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y), &battleVar1->m0_textureWindow);
    SetDrawMode(&battleVar0->m8920_fullWindowDrMode[1], 0, 0, GetTPage(battleVar0->mA234_glyphTextures[0].m4_tpage_tp, 0, battleVar0->mA234_glyphTextures[0].m10_tpage_X, battleVar0->mA234_glyphTextures[0].m14_tpage_Y), &battleVar1->m0_textureWindow);
}

void initPolyMonsterName(sMonsterTurnSub* m, s32 index) {
    s32 i;

    for (i = 0; i < 2; i++) {
        SetPolyFT4(&m->m0_polys[i]);
        setRGB0(&m->m0_polys[i], 0x80, 0x80, 0x80);
        SetSemiTrans(&m->m0_polys[i], 0);
        SetShadeTex(&m->m0_polys[i], 1);
        m->m5C_useOddClut = index & 1;
        m->m0_polys[i].clut = m->m5C_useOddClut ? textSpriteMode1 : textSpriteMode0;
        m->m0_polys[i].tpage = GetTPage(0, 0, 0x3C0, (index / 2) * 13);
    }
    m->m5D_monsterNameIsDisplayed = 0;
}

void setupStringsForMonsterNames(void);
void setupStringsForMonsterNames(void) {
    s32 i;

#if VERSION_JP
    allocateAndSetupBattleDialogWindow(5, 8, 0x2A, 0x64, 0x12, 0, 0); /* JP monster-name window is narrower */
#else
    allocateAndSetupBattleDialogWindow(5, 8, 0x2A, 0x70, 0x12, 0, 0);
#endif
    battleVar1->mB0_isDialogWindowInitialized[5] = 0;
    allocateAndSetupBattleDialogWindow(4, 0x20, 0xC8, 0xF4, 0x12, 0, 0);
    battleVar1->mB0_isDialogWindowInitialized[4] = 0;
    for (i = 0; i < 8; i += 2) {
        previousEntityTurn.m8_nameLabels[i].m58_ramBufferFromMonsterName = allocateTextureRamForText(0x39);
        previousEntityTurn.m8_nameLabels[i + 1].m58_ramBufferFromMonsterName = previousEntityTurn.m8_nameLabels[i].m58_ramBufferFromMonsterName;
        setRECT(&previousEntityTurn.m8_nameLabels[i].m50_monsterNameVRamRect, 0x3C0, (i / 2) * 13, 0x3C, 13);
        previousEntityTurn.m8_nameLabels[i + 1].m50_monsterNameVRamRect = previousEntityTurn.m8_nameLabels[i].m50_monsterNameVRamRect;
        initPolyMonsterName(&previousEntityTurn.m8_nameLabels[i], i);
        initPolyMonsterName(&previousEntityTurn.m8_nameLabels[i + 1], i + 1);
    }
}

/* Loads the 3 party character portrait TIMs from data (0x460 bytes each,
 * index 0xB for slots 1-2 when battleForceOnGear) into the VRAM slots of battle font
 * glyphs glyph + i. */
void loadBattleCharacterPortraits(u8* data, u8 glyph) {
    TIM_IMAGE tim;
    s32 t[18]; /* 3 sBattleVar0Sub_tpage records, indexed flat (retail keeps one
                  induction value per field: i * 24 + field offset) */
    s32 i;

    for (i = 0; i < 3; i++) {
        u8 c;
        if (battleCharacters[i] == 0x7F) {
            continue;
        }
        c = battleCharacters[i];
        if (battleForceOnGear && (i == 1 || i == 2)) {
            c = 0xB;
        }
        OpenTIM((u32*)(data + c * 0x460));
        ReadTIM(&tim);
        computeMenuBorder(battleFont, glyph + i, &t[i * 6], &t[i * 6 + 1], &t[i * 6 + 2], &t[i * 6 + 3], &t[i * 6 + 4], &t[i * 6 + 5]);
        tim.crect->x = t[i * 6 + 2];
        tim.crect->y = t[i * 6 + 3];
        tim.prect->x = t[i * 6 + 4] + i * 6;
        tim.prect->y = t[i * 6 + 5];
        LoadImage(tim.crect, tim.caddr);
        LoadImage(tim.prect, tim.paddr);
        DrawSync(0);
    }
}

void battleGetSlotStatus(u8* slotStatus) {
    s32 i;

    for (i = 0; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i]) {
            battleSlotStatusVar0.m0_battleSlotStatusVar0[i] = battleSlotStatusVar0.m16_numTicksBeforeReady[i] = battleGetSlotStatusSub(i, 0);
        } else {
            battleSlotStatusVar0.m0_battleSlotStatusVar0[i] = battleSlotStatusVar0.m16_numTicksBeforeReady[i] = 0xFF;
        }
        isEntityReadyForBattle[i] = 0;
        battleSlotStatusVar0.m2C_battleSlotHalfSpeedToggle[i] = 0;
        slotStatus[i] = 0;
    }
}

void executeBattleCode_finalizeDamageEntry(u8 entity, u8 idx) {
    s16 v = (battleScriptContext[idx].m5_valueHigh << 8) | battleScriptContext[idx].m4_valueLow;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = v;
    battleVar2->m2DA_indexInBattleVar48++;
}

void executeBattleCode_showMonsterName(u8 idx, u8 entity) {
    if (battleTickMain_var0 < 9) {
        previousEntityTurn.m8_nameLabels[battleTickMain_var0].m5E_currentMonsterNameStringWidth = renderString(getDialogParamPointer(currentBattleSpecificStrings, battleScriptContext[idx].m3_nameIndex), previousEntityTurn.m8_nameLabels[battleTickMain_var0].m58_ramBufferFromMonsterName, 0x39, battleTickMain_var0 & 1);
        loadImageSync(&previousEntityTurn.m8_nameLabels[battleTickMain_var0].m50_monsterNameVRamRect, previousEntityTurn.m8_nameLabels[battleTickMain_var0].m58_ramBufferFromMonsterName);
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFA;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = battleTickMain_var0++;
        battleVar2->m2DA_indexInBattleVar48++;
    }
}

void executeBattleCode_statusEffect(u8 value, u8 entity) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = -9;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = value;
    battleVar2->m2DA_indexInBattleVar48++;
}

void executeBattleCode_hideMonsterName(u8 entity) {
    if (battleTickMain_var0 != 0) {
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = -8;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = battleTickMain_var0 - 1;
        battleVar2->m2DA_indexInBattleVar48++;
    }
}

void executeBattleCode_showAttackName(u8 idx, u8 entity) {
    if (battleScriptContext[idx].m3_nameIndex) {
        executeBattleCode_showMonsterName(idx, entity);
        executeBattleCode_statusEffect(0x1E, entity);
        executeBattleCode_hideMonsterName(entity);
    }
}

void executeBattleCode_performAttack(u8 entity, u8 idx, u8 target) {
    s32 i;

    executeBattleCode_showAttackName(idx, entity);
    battleVar2->m2DC_attackIndex = battleScriptContext[idx].m1_operand + 1;
    performAttackSub3(entity, battleScriptContext[idx].m6_mask, battleScriptContext[idx].m2_animationId);
    performAttackSub6(battleVar2->m2DA_indexInBattleVar48);
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = battleScriptContext[idx].m2_animationId;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = entitiesHitInCurrentAttackBF[0];
    executeBattleCode_finalizeDamageEntry(entity, idx);
    if (battleScriptContext[idx].m6_mask != entitiesHitInCurrentAttackBF[0]) {
        for (i = 0; i < battleVar2->m2DA_indexInBattleVar48; i++) {
            if (battleCurrentDamages[i].m47_battleAnimationToPlay == 0xFD) {
                battleCurrentDamages[i].m16_targetBitMask = entitiesHitInCurrentAttackBF[0];
                return;
            }
        }
    }
}

void executeBattleCode_moveAndAttack(u8 entity, u8 idx, u8 target) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFD;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = 0;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = battleScriptContext[idx].m6_mask;
    initJumpData(entity, target);
    if (battleScriptContext[idx].m4_valueLow != 1) {
        if (apConfigArray[entity].m1_gearState == 0) {
            moveEntityToOtherEntity(entity, target);
        } else {
            moveEntityToShootEntity(entity, target);
        }
    }
    setCameraVisibleEntities(battleScriptContext[idx].m6_mask | characterIdToTargetBitmask(entity));
    executeBattleCode_finalizeDamageEntry(entity, idx);
}

void executeBattleCode_setIdleAnimation(u8 entity, u8 idx, u8 target) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = -4;
    executeBattleCode_finalizeDamageEntry(entity, idx);
}

void executeBattleCode_playAnimation(u8 entity, u8 idx, u8 target) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = battleScriptContext[idx].m4_valueLow;
    executeBattleCode_finalizeDamageEntry(entity, idx);
}

void executeBattleCode_setMonsterRestoreMask(u8 entity, u8 idx, u8 target) {
    bBattleTickMode0 = battleScriptContext[idx].m6_mask;
}

void executeBattleCode_removeMonster(u8 entity, u8 idx, u8 target) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF9;
    executeBattleCode_finalizeDamageEntry(entity, idx);
    isEntityReadyForBattle[entity] = 0xFF;
    unknownMonsterStatus0[entity - 3].m3_removed = 1;
    battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0] &= 0x8000;
}

void executeBattleCode_copyMonsterToSlot(u8 dest, u8 idx, u8 target) {
    s32 i;
    u8 src;

    for (i = 0; i < 11; i++) {
        if (bitmaskCharacterCheck(battleScriptContext[idx].m6_mask, i)) {
            src = i;
            break;
        }
    }
    battleMonsterMapping[dest] = battleMonsterMapping[src];
    monstersScriptsEntities[dest - 3].m0_scripts[0] = monstersScriptsEntities[src - 3].m0_scripts[0];
    monstersScriptsEntities[dest - 3].m0_scripts[1] = monstersScriptsEntities[src - 3].m0_scripts[1];
    monstersScriptsEntities[dest - 3].m0_scripts[2] = monstersScriptsEntities[src - 3].m0_scripts[2];
    monstersScriptsEntities[dest - 3].m0_scripts[3] = monstersScriptsEntities[src - 3].m0_scripts[3];
    unknownMonsterStatus0[dest - 3].m0_hasWhenAttackedScript = unknownMonsterStatus0[src - 3].m0_hasWhenAttackedScript;
    unknownMonsterStatus0[dest - 3].m1_hasEndOfTurnScript = unknownMonsterStatus0[src - 3].m1_hasEndOfTurnScript;
    memmove(&battleEntities.m0_entities[dest], &battleEntities.m0_entities[src], 0x170);
    pcGameplayEnemyCopied(dest, src);
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = src;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFB;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = dest;
    battleVar2->m2DA_indexInBattleVar48++;
}

void executeBattleCode_setByteStat(u8 entity, u8 idx, u8 target) {
    accessBattleEntityByteStat(entity, battleScriptContext[idx].m1_operand, battleScriptContext[idx].m4_valueLow, 0);
}

void executeBattleCode_addByteStat(u8 entity, u8 idx, u8 target) {
    u8 v = accessBattleEntityByteStat(entity, battleScriptContext[idx].m1_operand, 0, 1);
    accessBattleEntityByteStat(entity, battleScriptContext[idx].m1_operand, battleScriptContext[idx].m4_valueLow + v, 0);
}

void executeBattleCode_setWordStat(u8 entity, u8 idx, u8 target) {
    accessBattleEntityWordStat(entity, battleScriptContext[idx].m1_operand, battleScriptContext[idx].m4_valueLow | (battleScriptContext[idx].m5_valueHigh << 8), 0);
}

void executeBattleCode_addWordStat(u8 entity, u8 idx, u8 target) {
    u16 v = accessBattleEntityWordStat(entity, battleScriptContext[idx].m1_operand, 0, 1);
    accessBattleEntityWordStat(entity, battleScriptContext[idx].m1_operand, battleScriptContext[idx].m4_valueLow + ((battleScriptContext[idx].m5_valueHigh << 8) + v), 0);
}

void executeBattleCode_playAbility(u8 entity, u8 idx, u8 target) {
    executeBattleCode_showAttackName(idx, entity);
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF4;
    executeBattleCode_finalizeDamageEntry(entity, idx);
}

void executeBattleCode_specialAnimation(u8 entity, u8 idx, u8 target) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF6;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = battleScriptContext[idx].m6_mask;
    executeBattleCode_finalizeDamageEntry(entity, idx);
}

void executeBattleCode_languageError(u8 actor, u8 no) {
    s32 i, j, k;

    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
    }
    k = 0;
    j = 0;
    if (*pRunningOnDTL != -1) {
        while (1) {
            for (i = 0; i < j; i++) {
                logToScreen(" ");
            }
            k++;
            logToScreen("\n\n\n\n\n\nLanguage Error\n");
            logToScreen("\t\t\tActor%X\t\tNo%x\n\n", actor, no);
            battleRenderDebugAndMain();
            if (k >= 3) {
                k = 0;
                j++;
                if (j >= 21) {
                    j = 0;
                }
            }
        }
    }
}

void executeBattleCode(u8 entity) {
    s32 n;
    u8 continueScript;
    s32 i;
    u8 target;

    n = 0;
    continueScript = 1;
    do {
        i = 0;
        target = 0;
        while (i < 11) {
            u8 c = i;
            if (bitmaskCharacterCheck(battleScriptContext[n].m6_mask, c)) {
                target = c;
                break;
            }
            i++;
        }
        performAttackSub2();
        switch (battleScriptContext[n].m0_attackType) {
        case 0:
            continueScript = 0;
            break;
        case 1:
            executeBattleCode_performAttack(entity, n, target);
            break;
        case 2:
            executeBattleCode_moveAndAttack(entity, n, target);
            break;
        case 3:
            executeBattleCode_setIdleAnimation(entity, n, target);
            break;
        case 4:
            executeBattleCode_playAnimation(entity, n, target);
            break;
        case 5:
            executeBattleCode_setMonsterRestoreMask(entity, n, target);
            break;
        case 6:
            executeBattleCode_removeMonster(entity, n, target);
            break;
        case 7:
            executeBattleCode_copyMonsterToSlot(entity, n, target);
            break;
        case 8:
            executeBattleCode_setByteStat(entity, n, target);
            break;
        case 9:
            executeBattleCode_addByteStat(entity, n, target);
            break;
        case 10:
            executeBattleCode_setWordStat(entity, n, target);
            break;
        case 11:
            executeBattleCode_addWordStat(entity, n, target);
            break;
        case 12:
            executeBattleCode_showMonsterName(n, entity);
            break;
        case 13:
            executeBattleCode_playAbility(entity, n, target);
            break;
        case 14:
            executeBattleCode_statusEffect(battleScriptContext[n].m4_valueLow, entity);
            break;
        case 15:
            executeBattleCode_specialAnimation(entity, n, target);
            break;
        case 16:
            executeBattleCode_hideMonsterName(entity);
            break;
        default:
            executeBattleCode_languageError(entity, n);
            break;
        }
        n++;
    } while (n < 32 || continueScript);
}
