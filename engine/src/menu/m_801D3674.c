/*
 * Menu boxes (Noah initMenuTiles*, draw380), character info cards, gold and play time,
 * status screen (Noah statusMenu.cpp). Provisional file 0x801D3674-0x801D9808. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/game_state.h"

/* main: font.c (the menu calls them with int coordinates) */
void computeMenuBorder(void* font, s32 glyph, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY, s32* tpageX,
                   s32* tpageY); /* Noah: computeMenuBorder */

/* this file */
void initMenuTiles6(u8 index, u16 x, u16 y, s32 width, s32 height);
void initMenuTiles1(u8 index, u16 x, u16 y, u16 width, s32 height);
void initMenuTilesTopFrame(u8 index, u16 x, u16 y, u16 width);
void initMenuTilesBottomFrame(u8 index, u16 x, u16 y, u16 width, s32 height);
void initMenuTiles4(u8 index, u16 x, u16 y, u16 height);
void initMenuTiles5(u8 index, u16 x, u16 y, u16 width, u16 height);
void draw380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 transformDisabled, s32 arg6, u8 arg7);

/* menu overlay data */
extern s32 characterNameLabelX[]; /* status label x */
extern s32 characterNameLabelY[]; /* status label y */
extern s32 updateCharacterFixedStringsX[]; /* info card fixed strings x */
extern s32 updateCharacterFixedStringsY[]; /* info card fixed strings y */
extern s32 characterInfocardLevelPositionX;   /* info card level x */
extern s32 characterInfocardLevelPositionY;   /* info card level y */
extern s32 characterInfocardLevel2PositionX;   /* info card level2 x */
extern s32 characterInfocardLevel2PositionY;   /* info card level2 y */
extern s32 characterInfocardHPPositionX;   /* info card HP x */
extern s32 characterInfocardHPPositionY;   /* info card HP y */
extern s32 characterInfocardMaxHPPositionX;   /* info card max HP x */
extern s32 characterInfocardMaxHPPositionY;   /* info card max HP y */
extern s32 characterInfocardMPPositionX;   /* info card MP x */
extern s32 characterInfocardMPPositionY;   /* info card MP y */
extern s32 characterInfocardMaxMPPositionX;   /* info card max MP x */
extern s32 characterInfocardMaxMPPositionY;   /* info card max MP y */
extern s32 characterInfocardExpToNextPositionX;   /* info card XP x */
extern s32 characterInfocardExpToNextPositionY;   /* info card XP y */
extern s32 characterInfocardExpToNext2PositionX;   /* info card XP2 x */
extern s32 characterInfocardExpToNext2PositionY;   /* info card XP2 y */
extern s32 infocardNameX;
extern s32 infocardNameY;
extern s32 updateCharacterFixedStringsTable[]; /* Noah: updateCharacterFixedStringsTable (0xFFFF: none) */
void updateCharacterPortraitAndName(u8 slot, u8 characterId, s32 x, s32 y);
void updateCharacterInfoCardFixedStrings(u8 slot, s32 x, s32 y);
void updateCharacterHP(u8 slot, u8 characterId, s32 x, s32 y);
void updateCharacterMP(u8 slot, u8 characterId, s32 x, s32 y);
void updateCharacterXP(u8 slot, u8 characterId, s32 x, s32 y);
void updateCharacterLevel(u8 slot, u8 characterId, s32 x, s32 y);
extern s32 statusScreenNamePositionX;   /* status screen name verts x */
extern s32 statusScreenNamePositionY;   /* status screen name verts y */
extern s32 statusScreenLabelPositionX[];     /* status screen label x (character, gear) */
extern s32 statusScreenLabelPositionY[];     /* status screen label y */
extern s32 statusScreenLabelGlyphs[];     /* status screen label glyphs (0xFFFF: none) */
extern s32 statusScreenLevelPositionX;   /* status screen level x */
extern s32 statusScreenLevelPositionY;   /* status screen level y */
extern s32 statusScreenLevel2PositionX;   /* status screen level2 x */
extern s32 statusScreenLevel2PositionY;   /* status screen level2 y */
extern s32 statusScreenHpPositionX;   /* status screen HP x */
extern s32 statusScreenHpPositionY;   /* status screen HP y */
extern s32 statusScreenMaxHpPositionX;   /* status screen max HP x */
extern s32 statusScreenMaxHpPositionY;   /* status screen max HP y */
extern s32 statusScreenMpPositionX;   /* status screen MP x */
extern s32 statusScreenMpPositionY;   /* status screen MP y */
extern s32 statusScreenMaxMpPositionX;   /* status screen max MP x */
extern s32 statusScreenMaxMpPositionY;   /* status screen max MP y */
extern s32 statusScreenExpPositionX;   /* status screen CD0 x */
extern s32 statusScreenExpPositionY;   /* status screen CD0 y */
extern s32 statusScreenExp2PositionX;   /* status screen F50 x */
extern s32 statusScreenExp2PositionY;   /* status screen F50 y */
extern s32 statusScreenExpToNextPositionX;   /* status screen XP x */
extern s32 statusScreenExpToNextPositionY;   /* status screen XP y */
extern s32 statusScreenExpToNext2PositionX;   /* status screen XP2 x */
extern s32 statusScreenExpToNext2PositionY;   /* status screen XP2 y */
extern s32 statusScreenWeightPositionX;   /* status screen 1C70 x */
extern s32 statusScreenWeightPositionY;   /* status screen 1C70 y */
void setupStatusMenuForCharacter3Sub0Sub0(u8 slot, u8 isGear);
void setupStatusScreenLabels(u8 isGear);
void setupStatusScreenHp(u8 slot, u8 isGear);
void setupStatusScreenMp(u8 slot, u8 isGear);
void setupStatusScreenLevel(u8 slot, u8 isGear);
void setupStatusScreenExperience(u8 slot, u8 isGear);
void setupStatusScreenXp(u8 slot, u8 isGear);
void setupStatusScreenWeight(u8 slot, u8 isGear);
extern s32 isOnGearCharacters[]; /* Noah: isOnGearCharacters {0x159, 0x15A} */
extern s32 statusMenuStatLineX[]; /* Noah: setupStatusMenuForCharacter3Sub2Sub0Pos[0] (x) */
extern s32 statusMenuStatLineY[]; /* Noah: setupStatusMenuForCharacter3Sub2Sub0Pos[1] (y) */
extern s32 setupStatusMenuForCharacter3Sub2Sub0String[]; /* Noah: setupStatusMenuForCharacter3Sub2Sub0String [2][7] */
extern s32 scaledStatsVar0;
extern s32 scaledStatsVar1;
extern s32 scaledStatsVar2;
extern s32 scaledStatsVar3;
extern s32 scaledStatsVar4;
extern u8 scaledStatsVar5;
extern u8 scaledStatsVar6;
extern s32 statusStatGaugePositionX;   /* status stat bar x */
extern s32 statusStatGaugePositionY;   /* status stat bar y */
extern s32 statusStatValuePositionX;   /* status stat value x */
extern s32 statusStatValuePositionY;   /* status stat value y */
void setupStatsPolyG4(POLY_G4* polys, u8 mode);
void setStatDigitsColor(POLY_FT4* polys, u8 mode, u8 count, u8 odd);
void scaleStatsForMax(u16 value, u16 value2, s32 max);
void setupStatusMenuForCharacter3Sub2Sub0(s32 x, s32 y, u8 hidden);
void setupStatusMenuForCharacter3Sub2Sub1(s32 max, s32 x, s32 y, u8 compare, u8 hidden);
extern s32 statusEquipNamesY[]; /* status equipment name y */

/* free m440 (inverse of Noah initStatusMenuSub1's allocation) */
void freeCharacterSwitchArrows(void) {
    MenuContext_33C* p = gMenuContext->m33C_drawFlags;

    if (p->m67_arrowsAllocated != 0) {
        p->m53_draw440 = 0;
        gMenuContext->m33C_drawFlags->m67_arrowsAllocated = 0;
        freeHeap(gMenuContext->m440_arrows);
    }
}

/* status screen name label for a character/gear (a MenuContext_4E0 variant of setupStatusMenuForCharacter3Sub0Sub0) */
void setupCharacterNameLabel(MenuContext_4E0* label, u8 slot, u8 isGear, u8 pos) {
    s32 x;
    s32 w;
    POLY_FT4* p;

    initMemoryCardTransparentPoly(&label->m0_polys[gMenuContext->m308_oddOrEven]);
    label->m0_polys[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
    if (isGear == 0) {
        w = 0x48;
        p = &label->m0_polys[gMenuContext->m308_oddOrEven];
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] & 1) {
            p->clut = textSpriteMode1;
        } else {
            p->clut = textSpriteMode0;
        }
        x = characterNameLabelX[pos] - 0x24;
        setupMenuPolyFT4(&label->m0_polys[gMenuContext->m308_oddOrEven], x, characterNameLabelY[pos], infocardNameU[slot] * 4,
                      infocardNameV[slot], w, 0xD);
    } else {
        w = 0x60;
        p = &label->m0_polys[gMenuContext->m308_oddOrEven];
        if ((gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum + 0xB) & 1) {
            p->clut = textSpriteMode1;
        } else {
            p->clut = textSpriteMode0;
        }
        x = characterNameLabelX[pos] - 0x30;
        setupMenuPolyFT4(&label->m0_polys[gMenuContext->m308_oddOrEven], x, characterNameLabelY[pos], infocardNameU[slot + 3] * 4,
                      infocardNameV[slot + 3], w, 0xD);
    }
    initMemoryCardPolyVerts(label->m50_vertex, x, characterNameLabelY[pos], w, 0xD);
    label->m7D_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* Noah iniMenuContext364And380 */
void iniMenuContext364And380(u8 index, u16 x, u16 y, u16 width, u16 height, u8 animate, u8 transformDisabled, s32 arg7,
                   u8 arg8) {
    MenuContext_380* anim;

    if (index >= 2) {
        gMenuContext->m364_menuBoxes[index] = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_364), 0x720), 0);
        bzero(gMenuContext->m364_menuBoxes[index], sizeof(MenuContext_364));
        gMenuContext->m380_menuBoxAnims[index] = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_380), 0x18), 0);
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

/* Noah updateCharacterPortraitAndName */
void updateCharacterPortraitAndName(u8 slot, u8 characterId, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];

    setupStringInPolyFT4(gMenuContext->m2DC_font, slot + 0x14B, &card->m0_portraitPoly, gMenuContext->m308_oddOrEven, x, y,
                  0x1000);
    initMemoryCardTransparentPoly(&card->m50_NamePoly[gMenuContext->m308_oddOrEven]);
    card->m50_NamePoly[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
    card->m50_NamePoly[gMenuContext->m308_oddOrEven].clut =
        (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] & 1) ? textSpriteMode1 : textSpriteMode0;
    setupMenuPolyFT4(&card->m50_NamePoly[gMenuContext->m308_oddOrEven], infocardNameX + x, infocardNameY + y,
                  infocardNameU[slot] * 4, infocardNameV[slot], 0x48, 0xD);
}

/* Noah updateCharacterInfoCardFixedStrings */
void updateCharacterInfoCardFixedStrings(u8 slot, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];
    s32 i;

    card->m1279_FixedStringLength = 0;
    for (i = 0; i < 20; i++) {
        if (updateCharacterFixedStringsTable[i] != 0xFFFF) {
            card->m1279_FixedStringLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, updateCharacterFixedStringsTable[i], &card->mA0_FixedStrings[card->m1279_FixedStringLength],
                              gMenuContext->m308_oddOrEven, x + updateCharacterFixedStringsX[i], y + updateCharacterFixedStringsY[i], 0x1000);
        }
    }
}

/* Noah updateCharacterHP */
void updateCharacterHP(u8 slot, u8 characterId, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];
    s32 i;
    s32 j;

    fillStringDisplayBuffer(gameState.m26C_party[characterId].m4C_HP);
    card->m1273_HPLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            card->m1273_HPLength += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                                                  &card->mAF0_PolyHP[card->m1273_HPLength], gMenuContext->m308_oddOrEven,
                                                  x + characterInfocardHPPositionX + i * 8, y + characterInfocardHPPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[characterId].m4E_MaxHP);
    card->m1274_MaxHPLength = 0;
    for (i = 0, j = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            card->m1274_MaxHPLength += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                &card->mBE0_PolyMaxHP[card->m1274_MaxHPLength], gMenuContext->m308_oddOrEven, x + characterInfocardMaxHPPositionX + j * 8,
                y + characterInfocardMaxHPPositionY, 0x1000);
            j++;
        }
    }
}

/* Noah updateCharacterMP */
void updateCharacterMP(u8 slot, u8 characterId, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];
    s32 i;
    s32 j;

    fillStringDisplayBuffer(gameState.m26C_party[characterId].m50_MP);
    card->m1275_MPLength = 0;
    for (i = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 7] != 0xFF) {
            card->m1275_MPLength += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 7],
                                                  &card->mCD0_PolyMP[card->m1275_MPLength], gMenuContext->m308_oddOrEven,
                                                  x + characterInfocardMPPositionX + i * 8, y + characterInfocardMPPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[characterId].m52_MaxMP);
    card->m1276_MaxMPLength = 0;
    for (i = 0, j = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 7] != 0xFF) {
            card->m1276_MaxMPLength += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 7],
                &card->mD70_PolyMaxMP[card->m1276_MaxMPLength], gMenuContext->m308_oddOrEven, x + characterInfocardMaxMPPositionX + j * 8,
                y + characterInfocardMaxMPPositionY, 0x1000);
            j++;
        }
    }
}

/* Noah updateCharacterXP */
void updateCharacterXP(u8 slot, u8 characterId, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[characterId].m44_expToNextLevel);
    card->m1277_E10Length = 0;
    for (i = 0; i < 7; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 2] != 0xFF) {
            card->m1277_E10Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 2],
                                                   &card->mE10_polys[card->m1277_E10Length], gMenuContext->m308_oddOrEven,
                                                   x + characterInfocardExpToNextPositionX + i * 8, y + characterInfocardExpToNextPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[characterId].m48_expToNextLevel2);
    card->m1278_E10Length2 = 0;
    for (i = 0; i < 7; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 2] != 0xFF) {
            card->m1278_E10Length2 += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 2],
                                                    &card->m1040_polys[card->m1278_E10Length2],
                                                    gMenuContext->m308_oddOrEven, x + characterInfocardExpToNext2PositionX + i * 8,
                                                    y + characterInfocardExpToNext2PositionY, 0x1000);
        }
    }
}

/* Noah updateCharacterLevel */
void updateCharacterLevel(u8 slot, u8 characterId, s32 x, s32 y) {
    MenuContext_39C* card = gMenuContext->m39C_infoCards[slot];
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[characterId].m62_Level);
    card->m1271_LevelLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            card->m1271_LevelLength += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                                                     &card->m910_LevelPolys[card->m1271_LevelLength],
                                                     gMenuContext->m308_oddOrEven, x + characterInfocardLevelPositionX + i * 8,
                                                     y + characterInfocardLevelPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[characterId].m63_Level2);
    card->m1272_Level2Length = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            card->m1272_Level2Length += setupStringInPolyFT4(
                gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                &card->mA00_Level2Polys[card->m1272_Level2Length], gMenuContext->m308_oddOrEven, x + characterInfocardLevel2PositionX + i * 8,
                y + characterInfocardLevel2PositionY, 0x1000);
        }
    }
    for (i = 0; i < card->m1272_Level2Length; i++) {
        SetShadeTex(&card->mA00_Level2Polys[0][i * 2 + gMenuContext->m308_oddOrEven], 0);
        (&card->mA00_Level2Polys[0][i * 2 + gMenuContext->m308_oddOrEven])->r0 = 0;
        (&card->mA00_Level2Polys[0][i * 2 + gMenuContext->m308_oddOrEven])->g0 = 0x80;
        (&card->mA00_Level2Polys[0][i * 2 + gMenuContext->m308_oddOrEven])->b0 = 0;
    }
}

/* Noah updateCharacterInfoCard */
void updateCharacterInfoCard(u8 slot, u8 characterId) {
    MenuContextSub0* anim;
    MenuContext* ctx;
    MenuContext_39C* card;
    s32 x;
    s32 y;

    if (slot != 0xFF) {
        ctx = gMenuContext;
        anim = &ctx->m0_infoCardAnimations[slot];
        card = ctx->m39C_infoCards[slot];
        x = anim->m18_XOffset / 256 + anim->m0_startX;
        y = anim->m1C_YOffset / 256 + anim->m8_startY;
        updateCharacterPortraitAndName(slot, characterId, x, y);
        updateCharacterInfoCardFixedStrings(slot, x, y);
        updateCharacterHP(slot, characterId, x, y);
        updateCharacterMP(slot, characterId, x, y);
        updateCharacterXP(slot, characterId, x, y);
        updateCharacterLevel(slot, characterId, x, y);
        gMenuContext->m33C_drawFlags->m0_isInfoCardEnabled[slot] = 1;
        card->m1270_oddOrEven = gMenuContext->m308_oddOrEven;
    }
}

/* Noah updateGoldPolys (menuGold.cpp) */
void updateGoldPolys(s32 x, s32 y) {
    s32 i;
    s32 curX;
    u8 c;

    fillStringDisplayBuffer(gameState.m1924_gold);
    i = 0;
    gMenuContext->m340_gold->m320_goldPolyLength = 0;
    curX = x;
    do {
        c = gMenuContext->m31C_stringDisplayBuffer[i];
        i++;
        if (c != 0xFF) {
            gMenuContext->m340_gold->m320_goldPolyLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, c,
                              &gMenuContext->m340_gold->m0_goldPolys[gMenuContext->m340_gold->m320_goldPolyLength],
                              gMenuContext->m308_oddOrEven, curX, y, 0x1000);
        }
        curX += 8;
    } while (i < 9);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x10, &gMenuContext->m340_gold->m2D0_goldUnit, gMenuContext->m308_oddOrEven, x + 0x50,
                  y, 0x1000);
    gMenuContext->m33C_drawFlags->m5_drawGold = 1;
    gMenuContext->m340_gold->m324_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* Noah initMenuContext33C_playTime */
void initMenuContext33C_playTime(s32 x, s32 y) {
    s32 i;

    for (i = 0; i < 3; i++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m2EC_playTime[i],
                      &gMenuContext->m344_playTime->m0_playTimePolys[i], gMenuContext->m308_oddOrEven, x + i * 8, y, 0x1000);
    }

    for (i = 3; i < 5; i++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m2EC_playTime[i],
                      &gMenuContext->m344_playTime->m0_playTimePolys[i], gMenuContext->m308_oddOrEven, x + i * 8 + 8, y,
                      0x1000);
    }

    for (i = 5; i < 7; i++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m2EC_playTime[i],
                      &gMenuContext->m344_playTime->m0_playTimePolys[i], gMenuContext->m308_oddOrEven, x + i * 8 + 0x10, y,
                      0x1000);
    }

    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xEE, &gMenuContext->m344_playTime->m230_separators[0], gMenuContext->m308_oddOrEven,
                  x + 0x18, y, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xEE, &gMenuContext->m344_playTime->m230_separators[2], gMenuContext->m308_oddOrEven,
                  x + 0x30, y, 0x1000);
    gMenuContext->m344_playTime->m370_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* Noah setupStatusMenuForCharacter3Sub0Sub0 */
void setupStatusMenuForCharacter3Sub0Sub0(u8 slot, u8 isGear) {
    s32 offset = -(isGear != 0) & 0x18;
    u16 w;
    s32 idx;

    setupStringInPolyFT4(gMenuContext->m2DC_font, slot + 0x14B, gMenuContext->m358_selectedInfoCard->m0_portrait, gMenuContext->m308_oddOrEven,
                  0x18 - offset, 0xE, 0x1000);
    initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m1EA0_portraitVerts,
                  gMenuContext->m358_selectedInfoCard->m0_portrait[gMenuContext->m308_oddOrEven].x0,
                  gMenuContext->m358_selectedInfoCard->m0_portrait[gMenuContext->m308_oddOrEven].y0, 0x30, 0x30);
    initMemoryCardTransparentPoly(&gMenuContext->m358_selectedInfoCard->m50_name[gMenuContext->m308_oddOrEven]);
    gMenuContext->m358_selectedInfoCard->m50_name[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
    if (isGear == 0) {
        gMenuContext->m358_selectedInfoCard->m50_name[gMenuContext->m308_oddOrEven].clut =
            (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] & 1) ? textSpriteMode1 : textSpriteMode0;
    } else {
        gMenuContext->m358_selectedInfoCard->m50_name[gMenuContext->m308_oddOrEven].clut =
            ((gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum + 0xB) & 1) ? textSpriteMode1
                                                                                                    : textSpriteMode0;
    }
    w = isGear * 0x18 + 0x48;
    idx = isGear * 3 + slot;
    setupMenuPolyFT4(&gMenuContext->m358_selectedInfoCard->m50_name[gMenuContext->m308_oddOrEven], 0, 0, infocardNameU[idx] * 4, infocardNameV[idx],
                  w, 0xD);
    initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m1EC0_nameVerts, statusScreenNamePositionX - offset, statusScreenNamePositionY, w, 0xD);
}

/* status screen labels (Noah: MissingCode in setupStatusMenuForCharacter3Sub0) */
void setupStatusScreenLabels(u8 isGear) {
    s32 i;

    gMenuContext->m358_selectedInfoCard->m2AEC_labelLength = 0;
    for (i = 0; i < 24; i++) {
        if (statusScreenLabelGlyphs[isGear * 24 + i] != 0xFFFF) {
            gMenuContext->m358_selectedInfoCard->m2AEC_labelLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, statusScreenLabelGlyphs[isGear * 24 + i],
                              &gMenuContext->m358_selectedInfoCard->mA0_labels[gMenuContext->m358_selectedInfoCard->m2AEC_labelLength],
                              gMenuContext->m308_oddOrEven, statusScreenLabelPositionX[isGear * 24 + i], statusScreenLabelPositionY[isGear * 24 + i], 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AEC_labelLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m1EE0_labelVerts[i],
                      gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->mA0_labels[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen HP / gear HP */
void setupStatusScreenHp(u8 slot, u8 isGear) {
    u8 count;
    u8 start;
    s32 x;
    s32 i;

    if (isGear == 0) {
        count = 3;
        x = statusScreenHpPositionX;
        start = 6;
        fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m4C_HP);
    } else {
        count = 5;
        start = 4;
        x = statusScreenHpPositionX + 8;
        fillStringDisplayBuffer(
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m60_hp);
    }
    gMenuContext->m358_selectedInfoCard->m2AE7_hpLength = 0;
    for (i = 0; i < count; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE7_hpLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                              &gMenuContext->m358_selectedInfoCard->m1630_hp[gMenuContext->m358_selectedInfoCard->m2AE7_hpLength],
                              gMenuContext->m308_oddOrEven, x - isGear * 0x18, statusScreenHpPositionY, 0x1000);
        }
        x += 8;
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE7_hpLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2780_hpVerts[i],
                      gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m1630_hp[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    if (isGear == 0) {
        fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m4E_MaxHP);
        x = statusScreenMaxHpPositionX;
    } else {
        fillStringDisplayBuffer(
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m64_maxHp);
        x = statusScreenMaxHpPositionX - 0x20;
    }
    gMenuContext->m358_selectedInfoCard->m2AE8_maxHpLength = 0;
    for (i = 0; i < count; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE8_maxHpLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                              &gMenuContext->m358_selectedInfoCard->m17C0_maxHp[gMenuContext->m358_selectedInfoCard->m2AE8_maxHpLength],
                              gMenuContext->m308_oddOrEven, x - isGear * 0x18, isGear * 8 + statusScreenMaxHpPositionY, 0x1000);
            x += 8;
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE8_maxHpLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2820_maxHpVerts[i],
                      gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen MP / gear fuel */
void setupStatusScreenMp(u8 slot, u8 isGear) {
    u8 count;
    u8 start;
    s32 x;
    s32 y;
    s32 i;

    if (isGear == 0) {
        count = 2;
        x = statusScreenMpPositionX;
        y = statusScreenMpPositionY;
        start = 7;
        fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m50_MP);
    } else {
        count = 4;
        start = 5;
        x = statusScreenMpPositionX;
        y = statusScreenMpPositionY + 8;
        fillStringDisplayBuffer(
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m38_fuel);
    }
    gMenuContext->m358_selectedInfoCard->m2AE9_mpLength = 0;
    for (i = 0; i < count; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE9_mpLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                              &gMenuContext->m358_selectedInfoCard->m1950_mp[gMenuContext->m358_selectedInfoCard->m2AE9_mpLength],
                              gMenuContext->m308_oddOrEven, x - isGear * 0x18, y, 0x1000);
        }
        x += 8;
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE9_mpLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m28C0_mpVerts[i],
                      gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m1950_mp[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    if (isGear == 0) {
        fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m52_MaxMP);
        x = statusScreenMaxMpPositionX;
        y = statusScreenMaxMpPositionY;
    } else {
        fillStringDisplayBuffer(gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum]
                          .m3A_maxFuel);
        x = statusScreenMaxMpPositionX - 0x20;
        y = statusScreenMaxMpPositionY + 0x10;
    }
    gMenuContext->m358_selectedInfoCard->m2AEA_maxMpLength = 0;
    for (i = 0; i < count; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AEA_maxMpLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                              &gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[gMenuContext->m358_selectedInfoCard->m2AEA_maxMpLength],
                              gMenuContext->m308_oddOrEven, x - isGear * 0x18, y, 0x1000);
            x += 8;
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AEA_maxMpLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2960_maxMpVerts[i],
                      gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen level */
void setupStatusScreenLevel(u8 slot, u8 isGear) {
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m62_Level);
    gMenuContext->m358_selectedInfoCard->m2AE1_levelLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE1_levelLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                              &gMenuContext->m358_selectedInfoCard->mAF0_level[gMenuContext->m358_selectedInfoCard->m2AE1_levelLength],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenLevelPositionX - isGear * 0x18, statusScreenLevelPositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE1_levelLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2300_levelVerts[i],
                      gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->mAF0_level[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m63_Level2);
    gMenuContext->m358_selectedInfoCard->m2AE2_level2Length = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE2_level2Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 6],
                              &gMenuContext->m358_selectedInfoCard->mBE0_level2[gMenuContext->m358_selectedInfoCard->m2AE2_level2Length],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenLevel2PositionX - isGear * 0x18, statusScreenLevel2PositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE2_level2Length; i++) {
        SetShadeTex(&gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven], 0);
        (&gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven])->r0 = 0;
        (&gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven])->g0 = 0x80;
        (&gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven])->b0 = 0;
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2360_level2Verts[i],
                      gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->mBE0_level2[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen, party m3C/m40 values */
void setupStatusScreenExperience(u8 slot, u8 isGear) {
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m3C_experience);
    gMenuContext->m358_selectedInfoCard->m2AE3_CD0Length = 0;
    for (i = 0; i < 8; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 1] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE3_CD0Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 1],
                              &gMenuContext->m358_selectedInfoCard->mCD0_experience[gMenuContext->m358_selectedInfoCard->m2AE3_CD0Length],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenExpPositionX - isGear * 0x18, statusScreenExpPositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE3_CD0Length; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m23C0_experienceVerts[i],
                      gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->mCD0_experience[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m40_experience2);
    gMenuContext->m358_selectedInfoCard->m2AE4_F50Length = 0;
    for (i = 0; i < 8; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 1] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE4_F50Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 1],
                              &gMenuContext->m358_selectedInfoCard->mF50_experience2[gMenuContext->m358_selectedInfoCard->m2AE4_F50Length],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenExp2PositionX - isGear * 0x18, statusScreenExp2PositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE4_F50Length; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m24C0_experience2Verts[i],
                      gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->mF50_experience2[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen XP m44/m48 */
void setupStatusScreenXp(u8 slot, u8 isGear) {
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m44_expToNextLevel);
    gMenuContext->m358_selectedInfoCard->m2AE5_xpLength = 0;
    for (i = 0; i < 7; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 2] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE5_xpLength +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 2],
                              &gMenuContext->m358_selectedInfoCard->m11D0_xp[gMenuContext->m358_selectedInfoCard->m2AE5_xpLength],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenExpToNextPositionX - isGear * 0x18, statusScreenExpToNextPositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE5_xpLength; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m25C0_xpVerts[i],
                      gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m11D0_xp[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
    fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m48_expToNextLevel2);
    gMenuContext->m358_selectedInfoCard->m2AE6_xp2Length = 0;
    for (i = 0; i < 7; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 2] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AE6_xp2Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[i + 2],
                              &gMenuContext->m358_selectedInfoCard->m1400_xp2[gMenuContext->m358_selectedInfoCard->m2AE6_xp2Length],
                              gMenuContext->m308_oddOrEven, i * 8 + statusScreenExpToNext2PositionX - isGear * 0x18, statusScreenExpToNext2PositionY, 0x1000);
        }
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AE6_xp2Length; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m26A0_xp2Verts[i],
                      gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m1400_xp2[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* status screen weight (US lbs N.N / JP kg) or gear m68 */
void setupStatusScreenWeight(u8 slot, u8 isGear) {
    u8 count;
    u8 start;
    s32 x;
    s32 i;
    u16 v;
    s32 rem;
    u16 q;
    u16 r;

    if (isGear == 0) {
#if VERSION_JP
        /* JP shows the weight in kg: whole part m78 + m77, tenths m79 */
        count = 3;
        start = 6;
        rem = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m79_weightTenths;
        fillStringDisplayBuffer(gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m78_weightGain +
                      gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m77_baseWeight);
#else
        /* US converts to pounds: v = weight in tenths * 2.2 */
        v = ((gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m78_weightGain +
              gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m77_baseWeight) *
                 10 +
             gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m79_weightTenths) *
            22;
        count = 3;
        start = 6;
        q = v / 100;
        rem = (s32)(v - q * 100) / 10;
        fillStringDisplayBuffer(q);
#endif
        x = statusScreenWeightPositionX;
    } else {
        count = 5;
        fillStringDisplayBuffer(
            gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m68_weight);
        x = statusScreenWeightPositionX + 8;
        start = 4;
    }
    gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length = 0;
    for (i = 0; i < count; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length +=
                setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                              &gMenuContext->m358_selectedInfoCard->m1C70_weight[gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length],
                              gMenuContext->m308_oddOrEven, x + i * 8 - isGear * 0x18, isGear * 8 + statusScreenWeightPositionY, 0x1000);
        }
    }
    if (isGear == 0) {
#if VERSION_JP
        r = rem;
#else
        /* the one-shot loops only weight rem's use (loop depth) so that it gets s4 */
        do { do { r = rem; } while (0); } while (0);
#endif
        gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length +=
            setupStringInPolyFT4(gMenuContext->m2DC_font, r, &gMenuContext->m358_selectedInfoCard->m1C70_weight[gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length],
                          gMenuContext->m308_oddOrEven, statusScreenWeightPositionX + 0x20, statusScreenWeightPositionY, 0x1000);
    }
    for (i = 0; i < gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length; i++) {
        initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2A00_weightVerts[i],
                      gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m358_selectedInfoCard->m1C70_weight[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* Noah setupStatusMenuForCharacter3Sub0 */
void setupStatusMenuForCharacter3Sub0(u8 slot, u8 isGear);
void setupStatusMenuForCharacter3Sub0(u8 slot, u8 isGear) {
    setupStatusMenuForCharacter3Sub0Sub0(slot, isGear);
    setupStatusScreenLabels(isGear);
    setupStatusScreenHp(slot, isGear);
    setupStatusScreenMp(slot, isGear);
    setupStatusScreenLevel(slot, isGear);
    setupStatusScreenXp(slot, isGear);
    setupStatusScreenWeight(slot, isGear);
    setupStatusScreenExperience(slot, isGear);
    gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 1;
    gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* Noah setupStatusMenuForCharacter3Sub1: on-gear icon */
void setupStatusMenuForCharacter3Sub1(u8 slot, u8 show, u8 onGear);
void setupStatusMenuForCharacter3Sub1(u8 slot, u8 show, u8 onGear) {
    u8 sel = (onGear == 0);
    s32 i;
    s32 x;

    gMenuContext->m358_selectedInfoCard->m2AED_1E00Length = 0;
    if (show) {
        for (i = 0; i < 2; i++) {
            setupStringInPolyFT4(gMenuContext->m2DC_font, isOnGearCharacters[i], &gMenuContext->m358_selectedInfoCard->m1E00_polys[i],
                          gMenuContext->m308_oddOrEven, 0x78 + i * 0x20, 0x5A, 0x1000);
            initMemoryCardPolyVerts(gMenuContext->m358_selectedInfoCard->m2AA0_onGearIconVerts[i],
                          gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                          gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                              gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                              gMenuContext->m358_selectedInfoCard->m1E00_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
        }
        initMenuTiles1Sub(&gMenuContext->m358_selectedInfoCard->m1E00_polys[0][sel * 2 + gMenuContext->m308_oddOrEven]);
        gMenuContext->m358_selectedInfoCard->m1E00_polys[0][sel * 2 + gMenuContext->m308_oddOrEven].tpage |= 0x20;
        (&gMenuContext->m358_selectedInfoCard->m1E00_polys[0][sel * 2 + gMenuContext->m308_oddOrEven])->r0 = 0x20;
        (&gMenuContext->m358_selectedInfoCard->m1E00_polys[0][sel * 2 + gMenuContext->m308_oddOrEven])->g0 = 0x20;
        (&gMenuContext->m358_selectedInfoCard->m1E00_polys[0][sel * 2 + gMenuContext->m308_oddOrEven])->b0 = 0x20;
        gMenuContext->m358_selectedInfoCard->m2AEE_1E00OddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m358_selectedInfoCard->m2AED_1E00Length = 2;
    }
}

/* Noah setupStatusMenuForCharacter3Sub2Sub0: stat names */
void setupStatusMenuForCharacter3Sub2Sub0(s32 x, s32 y, u8 hidden) {
    s32 numPoly;
    s32 tp;
    s32 clutX;
    s32 clutY;
    s32 tpageX;
    s32 tpageY;
    s32 i;
    s32 j;
    s32 start;

    computeMenuBorder(gMenuContext->m2DC_font, 0xE0, &numPoly, &tp, &clutX, &clutY, &tpageX, &tpageY);
    gMenuContext->m35C_selectedStats->m32F3_0Length = 0;
    for (i = 0; i < 7 - hidden; i++) {
        start = gMenuContext->m35C_selectedStats->m32F3_0Length;
        gMenuContext->m35C_selectedStats->m32F3_0Length +=
            setupStringInPolyFT4(gMenuContext->m2DC_font, setupStatusMenuForCharacter3Sub2Sub0String[hidden * 7 + i], &gMenuContext->m35C_selectedStats->m0_polys[start],
                          gMenuContext->m308_oddOrEven, x + statusMenuStatLineX[i], y + statusMenuStatLineY[i], 0x1000);
        if (i & 1) {
            for (j = start; j < gMenuContext->m35C_selectedStats->m32F3_0Length; j++) {
                SetShadeTex(&gMenuContext->m35C_selectedStats->m0_polys[0][j * 2 + gMenuContext->m308_oddOrEven], 0);
                (&gMenuContext->m35C_selectedStats->m0_polys[0][j * 2 + gMenuContext->m308_oddOrEven])->r0 = 0x40;
                (&gMenuContext->m35C_selectedStats->m0_polys[0][j * 2 + gMenuContext->m308_oddOrEven])->g0 = 0x40;
                (&gMenuContext->m35C_selectedStats->m0_polys[0][j * 2 + gMenuContext->m308_oddOrEven])->b0 = 0x40;
            }
        }
    }
    for (i = 0; i < gMenuContext->m35C_selectedStats->m32F3_0Length; i++) {
        initMemoryCardPolyVerts(gMenuContext->m35C_selectedStats->m2420_vertex[i], gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y0,
                      gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x1 -
                          gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].x0,
                      gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y3 -
                          gMenuContext->m35C_selectedStats->m0_polys[0][i * 2 + gMenuContext->m308_oddOrEven].y0);
    }
}

/* Noah setupStatsPolyG4 */
void setupStatsPolyG4(POLY_G4* polys, u8 mode) {
    u8 rgb[3];
    s32 i;

    switch (mode) {
    case 0:
        rgb[0] = 0xFF;
        rgb[1] = 0x80;
        rgb[2] = 0x80;
        break;
    case 1:
        rgb[0] = 0x80;
        rgb[1] = 0xFF;
        rgb[2] = 0x80;
        break;
    case 2:
        rgb[0] = 0xFF;
        rgb[1] = 0;
        rgb[2] = 0;
        break;
    case 3:
        rgb[0] = 0;
        rgb[1] = 0;
        rgb[2] = 0xFF;
        break;
    }
    for (i = 0; i < 2; i++) {
        SetPolyG4(&polys[i]);
        polys[i].r0 = rgb[0];
        polys[i].g0 = rgb[1];
        polys[i].b0 = rgb[2];
        polys[i].r1 = rgb[0];
        polys[i].g1 = rgb[1];
        polys[i].b1 = rgb[2];
        polys[i].r2 = 0;
        polys[i].g2 = 0;
        polys[i].b2 = 0;
        polys[i].r3 = 0;
        polys[i].g3 = 0;
        polys[i].b3 = 0;
    }
}

/* stats label shading: SetShadeTex + rgb by mode */
void setStatDigitsColor(POLY_FT4* polys, u8 mode, u8 count, u8 odd) {
    u8 rgb[3];
    s32 i;
    s32 j;

    rgb[1] = 0x40;
    switch (mode) {
    case 0:
        rgb[0] = 0x80;
        rgb[2] = 0x40;
        break;
    case 1:
        rgb[0] = 0x40;
        rgb[2] = 0x80;
        break;
    case 2:
        rgb[0] = 0x40;
        rgb[2] = 0x40;
        break;
    }
    for (i = 0; i < count; i++) {
        SetShadeTex(&polys[i * 2 + odd], 0);
        polys[i * 2 + odd].r0 = rgb[0];
        polys[i * 2 + odd].g0 = rgb[1];
        polys[i * 2 + odd].b0 = rgb[2];
    }
}

/* Noah scaleStatsForMax */
void scaleStatsForMax(u16 value, u16 value2, s32 max) {
    scaledStatsVar0 = value;
    scaledStatsVar1 = value2;
    scaledStatsVar2 = scaledStatsVar1 - scaledStatsVar0;
    scaledStatsVar3 = (R3000_DIV(scaledStatsVar0 * 100, max) * 0x1900) / 10000;
    if (scaledStatsVar2 >= 0) {
        scaledStatsVar5 = 2;
        scaledStatsVar6 = 0xE3;
    } else {
        scaledStatsVar5 = 3;
        scaledStatsVar6 = 0xE5;
        scaledStatsVar2 = scaledStatsVar0 - scaledStatsVar1;
    }
    scaledStatsVar4 = (R3000_DIV(scaledStatsVar2 * 100, max) * 0x1900) / 10000;
}

/* Noah computeStatsMax (an unused local reproduces retail's empty 0x18 frame) */
s32 computeStatsMax(s32 unused, u16* stats1, u16* stats2) {
    u16 max;
    s32 unused2[5]; /* only reserves the 0x18-byte frame retail has */
    s32 i;

    max = 0;
    i = 0;
    do {
        i++;
        if (max < *stats1) {
            max = *stats1;
        }
        stats1++;
    } while (i < 7);
    i = 0;
    do {
        i++;
        if (max < *stats2) {
            max = *stats2;
        }
        stats2++;
    } while (i < 7);
    return max;
}

/* Noah setupStatusMenuForCharacter3Sub2Sub1: stat values and gauges (first param is reused for the stats max) */
void setupStatusMenuForCharacter3Sub2Sub1(s32 max, s32 x, s32 y, u8 compare, u8 hidden) {
    u16* stats1;
    u16* stats2;
    s32 i;
    s32 j;
    s32 digits;
    s32 barX;

    if (compare == 0) {
        stats1 = stats2 = gMenuContext->m330_itemData->mB8_computedStats;
    } else {
        stats1 = gMenuContext->m360_equipment->m280_stats;
        stats2 = gMenuContext->m330_itemData->mB8_computedStats;
    }
    max = computeStatsMax(0, stats1, stats2);
    for (i = 0; i < 7 - hidden; i++) {
        gMenuContext->m35C_selectedStats->m32C0_EB0Length[i] = 0;
        gMenuContext->m35C_selectedStats->m32CE_1770Length[i] = 0;
        scaleStatsForMax(stats1[i], stats2[i], max);
        setupStatsPolyG4(gMenuContext->m35C_selectedStats->m2030_gauge[i], 0);
        initMemoryCardPolyVerts(gMenuContext->m35C_selectedStats->m3100_gaugeVertex[i], statusStatGaugePositionX + x, y + statusStatGaugePositionY + i * 8, scaledStatsVar3, 6);
        gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i] = gMenuContext->m308_oddOrEven;
        fillStringDisplayBuffer(scaledStatsVar0);
        for (j = 0; j < 4; j++) {
            if (gMenuContext->m31C_stringDisplayBuffer[j + 5] != 0xFF) {
                gMenuContext->m35C_selectedStats->m32C0_EB0Length[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[j + 5],
                                  &gMenuContext->m35C_selectedStats->mEB0_polys[i][gMenuContext->m35C_selectedStats->m32C0_EB0Length[i]],
                                  gMenuContext->m308_oddOrEven, x + statusStatValuePositionX + j * 8, y + statusStatValuePositionY + i * 8, 0x1000);
            }
        }
        for (j = 0; j < gMenuContext->m35C_selectedStats->m32C0_EB0Length[i]; j++) {
            initMemoryCardPolyVerts(gMenuContext->m35C_selectedStats->m2A00_vertex[i][j],
                          gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y0,
                          gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x1 -
                              gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y3 -
                              gMenuContext->m35C_selectedStats->mEB0_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y0);
        }
        if (i & 1) {
            setStatDigitsColor(gMenuContext->m35C_selectedStats->mEB0_polys[i][0], 2, gMenuContext->m35C_selectedStats->m32C0_EB0Length[i],
                          gMenuContext->m308_oddOrEven);
        }
        gMenuContext->m35C_selectedStats->m32C7_EB0OddOrEven[i] = gMenuContext->m308_oddOrEven;
        if (compare) {
            setupStatsPolyG4(gMenuContext->m35C_selectedStats->m2228_gauge[i], scaledStatsVar5);
            if (scaledStatsVar5 == 2) {
                barX = x + statusStatGaugePositionX + scaledStatsVar3;
            } else {
                barX = x + statusStatGaugePositionX + scaledStatsVar3 - scaledStatsVar4;
            }
            initMemoryCardPolyVerts(gMenuContext->m35C_selectedStats->m31E0_gaugeVertex[i], barX, y + statusStatGaugePositionY + i * 8, scaledStatsVar4, 6);
            gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i] = gMenuContext->m308_oddOrEven;
            if (compare && scaledStatsVar2 != 0) {
                gMenuContext->m35C_selectedStats->m32CE_1770Length[i] +=
                    setupStringInPolyFT4(gMenuContext->m2DC_font, scaledStatsVar6, gMenuContext->m35C_selectedStats->m1770_polys[i],
                                  gMenuContext->m308_oddOrEven, x + statusStatValuePositionX + 0x20, y + statusStatValuePositionY + i * 8, 0x1000);
                fillStringDisplayBuffer(scaledStatsVar2);
                for (j = 0, digits = 0; j < 3; j++) {
                    if (gMenuContext->m31C_stringDisplayBuffer[j + 6] != 0xFF) {
                        gMenuContext->m35C_selectedStats->m32CE_1770Length[i] += setupStringInPolyFT4(
                            gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[j + 6],
                            &gMenuContext->m35C_selectedStats->m1770_polys[i][gMenuContext->m35C_selectedStats->m32CE_1770Length[i]], gMenuContext->m308_oddOrEven,
                            x + statusStatValuePositionX + 0x28 + digits * 8, y + statusStatValuePositionY + i * 8, 0x1000);
                        digits++;
                    }
                }
                for (j = 0; j < gMenuContext->m35C_selectedStats->m32CE_1770Length[i]; j++) {
                    initMemoryCardPolyVerts(gMenuContext->m35C_selectedStats->m2D80_vertex[i][j],
                                  gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x0,
                                  gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y0,
                                  gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x1 -
                                      gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].x0,
                                  gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y3 -
                                      gMenuContext->m35C_selectedStats->m1770_polys[i][0][j * 2 + gMenuContext->m308_oddOrEven].y0);
                }
                setStatDigitsColor(gMenuContext->m35C_selectedStats->m1770_polys[i][0], scaledStatsVar5 - 2, gMenuContext->m35C_selectedStats->m32CE_1770Length[i],
                              gMenuContext->m308_oddOrEven);
                gMenuContext->m35C_selectedStats->m32D5_1770OddOrEven[i] = gMenuContext->m308_oddOrEven;
                gMenuContext->m35C_selectedStats->m32F2_drawSecondGauge = 1;
            }
        }
        gMenuContext->m35C_selectedStats->m32EA_rowEnabled[i] = 1;
    }
}

/* Noah setupStatusMenuForCharacter3Sub2 */
void setupStatusMenuForCharacter3Sub2(u8 slot, u8 alt, u8 compare, u8 hidden);
void setupStatusMenuForCharacter3Sub2(u8 slot, u8 alt, u8 compare, u8 hidden) {
    s32 x;
    s32 y;

    x = 0x98;
    y = 0x26;
    if (alt) {
        x = 0x80;
        y = 0x90;
    }
    setupStatusMenuForCharacter3Sub2Sub0(x, y, hidden);
    setupStatusMenuForCharacter3Sub2Sub1(slot, x, y, compare, hidden);
    gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 1;
    gMenuContext->m35C_selectedStats->m32F1_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* status screen equipment names: render item/gear part names to VRAM, set up the m360 labels */
void setupStatusScreenEquipNames(u8 slot, u8 mode, u8 compare, u8 isGear) {
    s32 count;
    s32 x;
    s32 yOffset;
    u8* equip;
    u8* equip2;
    u8* equip3;
    u8 load;
    u16* buf;
    u8* text;
    RECT rect;
    s32 i;

    count = 5;
    x = 0xD0;
    yOffset = 0;
    equip = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6A_weaponModifier;
    equip2 = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m6F_equipment;
    equip3 = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m74_accessorySlots;
    if (mode != 0) {
        count = 4;
        x = 0x28;
        yOffset = 9;
        if (mode == 1) {
            yOffset = 5;
        }
        gMenuContext->m360_equipment->m294_nameEnabled[4] = 0;
    }
    if (isGear != 0) {
        equip = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].mC_weapon;
        equip2 = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m4_slotItem;
        equip3 = gameState.m978_gears[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum].m9_accessorySlots;
    }
    if (compare != 0) {
        equip = gMenuContext->m360_equipment->m29C_backup6A;
        equip2 = gMenuContext->m360_equipment->m2A1_backup6F;
        equip3 = gMenuContext->m360_equipment->m2A6_backup74;
    }
    buf = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < count; i++) {
        switch (i) {
        case 0:
            if (mode != 2) {
                if (isGear == 0) {
                    gMenuContext->m360_equipment->m0_names[0].m7E_stringWidth = renderString(getWeaponName(equip[0]), buf, 0x24, 0);
                } else {
                    gMenuContext->m360_equipment->m0_names[0].m7E_stringWidth = renderString(getGearWeaponName(equip[0]), buf, 0x24, 0);
                }
            } else if (isGear == 0) {
                gMenuContext->m360_equipment->m0_names[0].m7E_stringWidth = renderString(getWeaponName(equip2[0]), buf, 0x24, 0);
            } else {
                gMenuContext->m360_equipment->m0_names[0].m7E_stringWidth = renderString(getGearWeaponName(equip2[0]), buf, 0x24, 0);
            }
            break;
        case 1:
        case 2:
        case 3:
        case 4:
            if (i == 4 && isGear == 0) {
                break;
            }
            if (mode < 2) {
                if (isGear == 0) {
                    text = getAccessoryName(equip3[i - 1]);
                } else if (mode == 0) {
                    if (i == 1) {
                        gMenuContext->m360_equipment->m0_names[1].m7E_stringWidth =
                            renderString(getGearWeaponName(equip[3]), buf, 0x24, 1);
                        break;
                    }
                    text = getGearAccessoryName(equip3[i - 2]);
                } else {
                    text = getGearAccessoryName(equip3[i - 1]);
                }
            } else if (isGear == 0) {
                text = getWeaponName(equip2[i]);
            } else {
                text = getGearWeaponName(equip2[i]);
            }
            gMenuContext->m360_equipment->m0_names[i].m7E_stringWidth = renderString(text, buf, 0x24, i % 2);
            break;
        }
        if (i & 1) {
            load = 1;
        } else if (isGear == 0) {
            load = 0;
        } else if (mode == 0 && i == 4) {
            load = 1;
        }
        if (load) {
            rect.x = ((i / 2) & 1) * 0x20 + 0x140;
            rect.y = (i / 4) * 13 + 0x27;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, buf);
            DrawSync(0);
        }
        initMenuContext_1DE0(&gMenuContext->m360_equipment->m0_names[i], i, 0xC, 0);
        if (i == 4 && isGear == 0) {
            gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
            gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven].clut =
                ((gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].mA0_gearNum + 0xB) & 1)
                    ? textSpriteMode1
                    : textSpriteMode0;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->u0 = infocardNameU[slot + 3] * 4;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->v0 = infocardNameV[slot + 3];
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->u1 = infocardNameU[slot + 3] * 4 + 0x60;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->v1 = infocardNameV[slot + 3];
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->u2 = infocardNameU[slot + 3] * 4;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->v2 = infocardNameV[slot + 3] + 0xD;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->u3 = infocardNameU[slot + 3] * 4 + 0x60;
            (&gMenuContext->m360_equipment->m0_names[i].m0_polys[gMenuContext->m308_oddOrEven])->v3 = infocardNameV[slot + 3] + 0xD;
            gMenuContext->m360_equipment->m0_names[i].m7E_stringWidth = 0x60;
        }
        initMemoryCardPolyVerts(gMenuContext->m360_equipment->m0_names[i].m50_vertex, x, statusEquipNamesY[i + yOffset],
                      gMenuContext->m360_equipment->m0_names[i].m7E_stringWidth, 0xD);
        gMenuContext->m360_equipment->m294_nameEnabled[i] = 1;
    }
    gMenuContext->m360_equipment->m299_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m4B_draw360 = 1;
    freeHeap(buf);
}

/* next/previous party slot, skipping empty slots */
s32 findNextPartySlot(s32 cur, u8 dir, u8 mode) {
    switch (dir) {
    case 0:
        while (1) {
            cur++;
            if (cur >= 3) {
                cur = 0;
            }
            if (mode == 0) {
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[cur] != 0xFF) {
                    break;
                }
            } else if (gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels[cur + 4] != 0) {
                break;
            }
        }
        break;
    case 1:
        while (1) {
            cur--;
            if (cur < 0) {
                cur = 2;
            }
            if (mode == 0) {
                if (gMenuContext->m33C_drawFlags->m30_partyMemberId[cur] != 0xFF) {
                    break;
                }
            } else if (gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels[cur + 4] != 0) {
                break;
            }
        }
        break;
    }
    return cur;
}
