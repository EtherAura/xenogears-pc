
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "field/field.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
extern FieldBss_800AF880 fieldSceneState;

/* 0x800AFC68: 33 sprites with their draw modes, double-buffered (0x840 bytes) */
typedef struct {
    /* 0x000 */ DR_MODE m0_drawModes[0x21][2];
    /* 0x318 */ SPRT m318_sprites[0x21][2];
} FieldSpriteList; /* 0x840 */

/* 0x800B1DF0: 16 double-buffered sprites with draw modes (4 used), 0x400 bytes */
typedef struct {
    /* 0x000 */ DR_MODE m0_drawModes[16][2];
    /* 0x180 */ SPRT m180_sprites[16][2];
} FieldSpriteList16; /* 0x400 */

/* 0x800C3A3C: textured quads with draw modes and texture windows (3 used), 0x2F8 bytes */
typedef struct {
    /* 0x000 */ DR_MODE m0_drawModes[5][2];
    /* 0x078 */ RECT m78_texWindows[5][2];
    /* 0x0C8 */ POLY_FT4 mC8_polys[7][2];
} FieldPolyList; /* 0x2F8 */

/* 0x800B0188: 5 double-buffered sprites with draw modes (static) */
typedef struct {
    /* 0x00 */ DR_MODE m0_drawModes[5][2];
    /* 0x78 */ SPRT m78_sprites[5][2];
} FieldSpriteList5; /* 0x140 */

/* 0x800AF770: 16 text lines of 4 sprites (128x16 each), double-buffered */
typedef struct {
    /* 0x00 */ DR_MODE m0_drawModes[2][4];
    /* 0x60 */ SPRT m60_sprites[2][4];
} FieldTextLine; /* 0x100 */

extern FieldSpriteList* scriptMarkerSprites;
extern FieldSpriteList16* fieldMapScreenMarkerSprites;
extern FieldSpriteList5 wideImageSprites;
extern s16 wideImageSpritesVisible;
extern FieldPolyList* fieldMapScreenPolys;
extern s32 g_frameOddOrEven;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern GameState* pKernelGameState;
extern FieldTextLine* scrollingTextLines;
extern POLY_GT4 scrollingTextFadeGradients[2][2];       /* fade gradients above/below the text */
extern MATRIX occlusionTestMatrix;
extern s32 OPX8E_param0;
extern s32 OPX8E_param1;
extern s32 g_playerControlledActor;
extern s32 fieldMapScreenScaleX;
extern s32 fieldMapScreenScaleZ;
extern s32 fieldMapScreenOffsetX;
extern s32 fieldMapScreenOffsetY;
extern s16 fieldMapPatchRects[20];               /* 5 image rects (x, y, w, h) in file 0x802, read as fieldMapPatchRects[i * 4 + k] */
/* 0x800AF47C: field map screens, 8 words each: field id (0xFFFF ends the
 * table), x scale, z scale, file (0x7FB + n, directory 4), item needed,
 * x offset, y offset, 1: draw the markers (loadFieldMapPatches). Read as a flat
 * s32 array (fieldMapScreenEntries[i * 8 + k]), which is what keeps &fieldMapScreenEntries in a
 * register. */
extern s32 fieldMapScreenEntries[];
extern s32 fieldMapNumber;
extern u16 g_padButtonForField;
extern s32 movieScrollingTextEnabled;
extern u8* scrollingTextData;
extern u8* scrollingTextCursor;
extern s32 scrollingTextLineIndex;
extern s32 scrollingTextFrameCounter;
extern s32 scrollingTextBytesLeft;
extern u32* scrollingTextGlyphTim;
/* LIBGPU setRECT (libgpu.h macro) */

/* LIBGPU setRGB0 (libgpu.h macro) */

void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
void loadTimToVram(u32* addr, s16 px, s16 py, s16 cx, s16 cy, s16 cw, s16 ch);
s32 isFieldMapPatchFlagSet(s32 index);
sptr getScrollingTextGlyph(u8* str, s32* isCustom);
void expandTextGlyph(u8* dst, u16* src);
s32 isInventory0ItemOwned(s32 item);
void loadFieldMapPatches(void);
void initFieldMapScreenPrims(void);
void renderFieldMapScreen(s32 color);
void pcTransition60MapScreen(s32 color); /* Enhanced (f_800A55B8.c) */
void clearFieldOrderingTable(void);
void renderScreenTransition(void);
void updateFieldInputs(void);
u16* func_800405C4(s32 sjis); /* BIOS B(51h) Krom2RawAdd */
u8* drawScrollingTextLineToVram(u8* arg0, s32 arg1, s32 arg2);
void loadScrollingTextFiles(void);
void initScrollingTextSprites(void);

/* Bounding sphere of a model block: centre (m18) and diameter (m20) */
void setupObjectRenderModesSub0(sFieldEntitySub0* p) {
    ModelBlock* m = p->m4_pModelBlock;
    s32 x0, y0, z0;
    s32 dx, dy, dz;
    s32 max;

    dx = max = m->m20_boundingBox[4] - (x0 = m->m20_boundingBox[0]);
    dy = m->m20_boundingBox[5] - (y0 = m->m20_boundingBox[1]);
    dz = m->m20_boundingBox[6] - (z0 = m->m20_boundingBox[2]);
    if (max < dy) {
        max = dy;
    }
    if (max < dz) {
        max = dz;
    }
    p->m18_boundingCenter.vx = dx / 2 + x0;
    p->m18_boundingCenter.vy = dy / 2 + y0;
    p->m18_boundingCenter.vz = dz / 2 + z0;
    p->m20_boundingRadius = max * 2 + 1;
}

/* Bounding sphere on screen? 0: visible, -1: off screen */
s32 isObjectOcluded(sFieldEntitySub0* p) {
    VECTOR v;
    SVECTOR sv;
    s32 flag;
    s32 sxy;
    s32 z;
    s32 r;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;

    RotTrans(&p->m18_boundingCenter, &v, &flag);
    occlusionTestMatrix.t[0] = v.vx;
    occlusionTestMatrix.t[1] = v.vy;
    occlusionTestMatrix.t[2] = v.vz;
    SetRotMatrix(&occlusionTestMatrix);
    SetTransMatrix(&occlusionTestMatrix);
    r = p->m20_boundingRadius;
    sv.vx = -r;
    sv.vy = -r;
    sv.vz = 0;
    RotTransPers(&sv, &sxy, &z, &flag);
    y0 = sxy >> 16;
    x0 = (s16)sxy;
    sv.vx = r;
    sv.vy = r;
    sv.vz = 0;
    RotTransPers(&sv, &sxy, &z, &flag);
    y1 = sxy >> 16;
    x1 = (s16)sxy;
    if (y0 < OPX8E_param1 + 0xE0 && -OPX8E_param1 < y1 && x0 < OPX8E_param0 + 0x140 && -OPX8E_param0 < x1) {
        return 0;
    }
    return -1;
}

void freeScriptMarkerSprites(void) {
    freeHeap(scriptMarkerSprites);
    DrawSync(0);
}

/* Allocate and set up the 33 double-buffered sprites (8x8, first one 16x16) */
void initScriptMarkerSprites(void) {
    RECT tw;
    s32 i;
    SPRT* s;
    SPRT* s2;

    scriptMarkerSprites = allocateHeap(PC_HEAP_SIZED(sizeof(FieldSpriteList), 0x840), 0);
    tw.x = 0;
    tw.y = 0;
    tw.w = 0xFF;
    tw.h = 0xFF;
    for (i = 0; i < 0x21; i++) {
        SetDrawMode(&scriptMarkerSprites->m0_drawModes[i][0], 0, 0, GetTPage(0, 0, 0x3C0, 0x100), &tw);
        SetDrawMode(&scriptMarkerSprites->m0_drawModes[i][1], 0, 0, GetTPage(0, 0, 0x3C0, 0x140), &tw);
        s = &scriptMarkerSprites->m318_sprites[i][0];
        SetSprt(s);
        s2 = s + 1;
        s->r0 = 0x80;
        s->g0 = 0x80;
        s->b0 = 0x80;
        if (i == 0) {
            s->u0 = 0xE0;
            s->v0 = 0x70;
            s->w = 0x10;
            s->h = 0x10;
        } else {
            s->u0 = 0xE0;
            s->v0 = 0x60;
            s->w = 8;
            s->h = 8;
        }
        s->x0 = 0xA0;
        s->y0 = 0x70;
        s->clut = GetClut(0x100, 0xF7);
        *s2 = *s;
    }
}

/* Set the colour of sprite <index> (both frames) */
void setScriptMarkerColor(s32 index, u8 r, u8 g, u8 b) {
    setRGB0(&scriptMarkerSprites->m318_sprites[index][0], r, g, b);
    setRGB0(&scriptMarkerSprites->m318_sprites[index][1], r, g, b);
}

/* Draw sprite <index> at (x, y); mode 0/1 offset it to centre the 16x16 / 8x8 sprite */
void drawScriptMarker(s32 index, s32 x, s32 y, s32 mode) {
    switch (mode) {
    case 0:
        y -= 0xC;
        x -= 4;
        break;
    case 1:
        y -= 4;
        x -= 4;
        break;
    }
    scriptMarkerSprites->m318_sprites[index][g_frameOddOrEven].x0 = x;
    scriptMarkerSprites->m318_sprites[index][g_frameOddOrEven].y0 = y;
    addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &scriptMarkerSprites->m318_sprites[index][g_frameOddOrEven]);
    addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &scriptMarkerSprites->m0_drawModes[index][g_frameOddOrEven]);
}

/* Allocate and set up 4 sprites (fieldMapScreenMarkerSprites) and 3 semi-transparent 128x224 quads (fieldMapScreenPolys) */
void initFieldMapScreenPrims(void) {
    RECT tw;
    s32 i;
    SPRT* s;
    SPRT* s2;
    POLY_FT4* p;
    POLY_FT4* p2;

    fieldMapScreenPolys = allocateHeap(PC_HEAP_SIZED(sizeof(FieldPolyList), 0x2f8), 0);
    fieldMapScreenMarkerSprites = allocateHeap(PC_HEAP_SIZED(sizeof(FieldSpriteList16), 0x400), 0);
    tw.x = 0;
    tw.y = 0;
    tw.w = 0xFF;
    tw.h = 0xFF;
    for (i = 0; i < 4; i++) {
        SetDrawMode(&fieldMapScreenMarkerSprites->m0_drawModes[i][0], 0, 0, GetTPage(0, 0, 0x3C0, 0x140), &tw);
        SetDrawMode(&fieldMapScreenMarkerSprites->m0_drawModes[i][1], 0, 0, GetTPage(0, 0, 0x3C0, 0x140), &tw);
        s = &fieldMapScreenMarkerSprites->m180_sprites[i][0];
        SetSprt(s);
        s2 = s + 1;
        s->r0 = 0x80;
        s->g0 = 0x80;
        s->b0 = 0x80;
        s->x0 = 0xA0;
        s->y0 = 0x70;
        if (i == 0) {
            s->u0 = 0xE0;
            s->v0 = 0x70;
            s->w = 0x10;
            s->h = 0x10;
        } else {
            s->u0 = 0xE0;
            s->v0 = 0x60;
            s->w = 8;
            s->h = 8;
        }
        s->clut = GetClut(0x100, 0xF7);
        *s2 = *s;
    }
    for (i = 0; i < 3; i++) {
        p = &fieldMapScreenPolys->mC8_polys[i][0];
        SetPolyFT4(p);
        p->x0 = i << 7;
        p->y0 = 0;
        p->x1 = (i << 7) + 0x80;
        p->y1 = 0;
        p->x2 = i << 7;
        p->y2 = 0xDF;
        p->x3 = (i << 7) + 0x80;
        p->y3 = 0xDF;
        setRECT(&fieldMapScreenPolys->m78_texWindows[i][0], 0, 0, 0xFF, 0xFF);
        setRECT(&fieldMapScreenPolys->m78_texWindows[i][1], 0, 0, 0xFF, 0xFF);
        SetDrawMode(&fieldMapScreenPolys->m0_drawModes[i][0], 0, 0, GetTPage(1, 0, (i << 6) + 0x300, 0x100), &fieldMapScreenPolys->m78_texWindows[i][0]);
        SetDrawMode(&fieldMapScreenPolys->m0_drawModes[i][1], 0, 0, GetTPage(1, 0, (i << 6) + 0x300, 0x100), &fieldMapScreenPolys->m78_texWindows[i][1]);
        p->r0 = 0x80;
        p->g0 = 0x80;
        p->b0 = 0x80;
        SetSemiTrans(p, 1);
        p->u0 = 0;
        p->v0 = 0;
        p->u1 = 0x80;
        p->v1 = 0;
        p->u2 = 0;
        p->v2 = 0xDF;
        p->u3 = 0x80;
        p->v3 = 0xDF;
        p2 = p + 1;
        p->tpage = GetTPage(1, 0, (i << 6) + 0x300, 0x100);
        p->clut = GetClut(0, 0xF6);
        *p2 = *p;
    }
}

s32 isInventory0ItemOwned(s32 item) {
    s32 i;

    for (i = 0; i < 0x96; i++) {
        if (pKernelGameState->m2026_inventory0Ids[i] == item && pKernelGameState->m1F90_inventory0Counts[i] != 0) {
            return 0;
        }
    }
    return -1;
}

/* Draw the player marker sprite and the 3 quads with brightness <color> (map screen?) */
void renderFieldMapScreen(s32 color) {
    sFieldScriptEntity* e;
    s32 i;
    s32 x;
    s32 y;

    pcTransition60MapScreen(color); /* Enhanced, 60 Hz (f_800A55B8.c renderScreenTransition) */
    e = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    x = ((e->m20_position.vx >> 16) * fieldMapScreenScaleX) >> 16;
    y = -((e->m20_position.vz >> 16) * fieldMapScreenScaleZ) >> 16;
    for (i = 0; i < 1; i++) {
        if (i == 0) {
            y -= 0xC;
            x -= 4;
        }
        fieldMapScreenMarkerSprites->m180_sprites[i][g_frameOddOrEven].x0 = x + fieldMapScreenOffsetX;
        fieldMapScreenMarkerSprites->m180_sprites[i][g_frameOddOrEven].y0 = y + fieldMapScreenOffsetY;
        setRGB0(&fieldMapScreenMarkerSprites->m180_sprites[i][g_frameOddOrEven & 1], color, color, color);
        addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &fieldMapScreenMarkerSprites->m180_sprites[i][g_frameOddOrEven]);
        addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &fieldMapScreenMarkerSprites->m0_drawModes[i][g_frameOddOrEven]);
    }
    for (i = 0; i < 3; i++) {
        setRGB0(&fieldMapScreenPolys->mC8_polys[i][g_frameOddOrEven & 1], color, color, color);
        addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &fieldMapScreenPolys->mC8_polys[i][g_frameOddOrEven]);
        addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &fieldMapScreenPolys->m0_drawModes[i][g_frameOddOrEven]);
    }
}

/* Is story flag 3+index set (gameState field var 0x73 bits 3..7; bit 7 inverted)? 0: yes, -1: no */
s32 isFieldMapPatchFlagSet(s32 index) {
    switch (index) {
    case 0:
        if (pKernelGameState->m1930_fieldVarsBackup[0x73] & 8) {
            return 0;
        }
        break;
    case 1:
        if (pKernelGameState->m1930_fieldVarsBackup[0x73] & 0x10) {
            return 0;
        }
        break;
    case 2:
        if (pKernelGameState->m1930_fieldVarsBackup[0x73] & 0x20) {
            return 0;
        }
        break;
    case 3:
        if (pKernelGameState->m1930_fieldVarsBackup[0x73] & 0x40) {
            return 0;
        }
        break;
    case 4:
        if (!(pKernelGameState->m1930_fieldVarsBackup[0x73] & 0x80)) {
            return 0;
        }
        break;
    }
    return -1;
}

/* Load file 0x802 (a TIM) and upload the parts of it for the story flags that are not set (map markers) */
void loadFieldMapPatches(void) {
    TIM_IMAGE tim;
    u32* file;
    u32* buf;
    s32 i;
    s32 j;
    u32* dst;
    s32 xo;

    file = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0x802)), 0);
    readFile(0x802, file, 0, 0x80);
    waitReadCompletion(0);
    buf = allocateHeap(PC_HEAP_BYTES(0xF20), 0);
    OpenTIM(file);
    if (ReadTIM(&tim) != 0) {
        for (i = 0; i < 5; i++) {
            if (isFieldMapPatchFlagSet(i) == -1 && tim.paddr != 0) {
                dst = buf;
                for (j = 0; j < fieldMapPatchRects[i * 4 + 3]; j++) {
                    xo = fieldMapPatchRects[i * 4] / 4;
                    memcpy(dst, (u32*)((((fieldMapPatchRects[i * 4 + 1] + j) * 80 + xo) << 2) + (sptr)tim.paddr), fieldMapPatchRects[i * 4 + 2]);
                    dst += fieldMapPatchRects[i * 4 + 2] / 4;
                }
                tim.prect->x = fieldMapPatchRects[i * 4] / 2 + 0x300;
                tim.prect->y = fieldMapPatchRects[i * 4 + 1] + 0x100;
                tim.prect->w = fieldMapPatchRects[i * 4 + 2] / 2;
                tim.prect->h = fieldMapPatchRects[i * 4 + 3];
                LoadImage(tim.prect, buf);
                DrawSync(0);
            }
        }
    }
    freeHeap(file);
    freeHeap(buf);
}

/* Field map screen: if this field has a map entry and its item is owned, show the map (fade in, wait for the button 0x100, fade out) and restore VRAM */
void showFieldMapScreen(void) {
    RECT rect;
    s32 i;
    s32 k;
    s32 file;
    u32* saved;
    u32* buf;

    i = 0;
    while (1) {
        if (fieldMapScreenEntries[i * 8] == 0xFFFF) {
            return;
        }
        if ((fieldMapNumber & 0x3FFF) == fieldMapScreenEntries[i * 8]) {
            break;
        }
        i++;
    }
    k = i * 8;
    if (isInventory0ItemOwned(fieldMapScreenEntries[k + 4]) == -1) {
        return;
    }
    fieldMapScreenScaleX = fieldMapScreenEntries[k + 1];
    fieldMapScreenScaleZ = fieldMapScreenEntries[k + 2];
    file = fieldMapScreenEntries[k + 3];
    fieldMapScreenOffsetX = fieldMapScreenEntries[k + 5];
    fieldMapScreenOffsetY = fieldMapScreenEntries[k + 6];
    rect.x = 0x300;
    rect.y = 0x100;
    rect.w = 0xA0;
    rect.h = 0x100;
    saved = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
    StoreImage(&rect, saved);
    DrawSync(0);
    waitReadCompletion(0);
    setCurrentDirectory(4, 0);
    buf = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(file + 0x7FB)), 0);
    readFile(file + 0x7FB, buf, 0, 0x80);
    waitReadCompletion(0);
    loadTimToVram(buf, 0x300, 0x100, 0, 0xF6, 0, 0);
    freeHeap(buf);
    if (fieldMapScreenEntries[k + 7] == 1) {
        loadFieldMapPatches();
    }
    initFieldMapScreenPrims();
    for (i = 0; i < 0x10; i++) {
        clearFieldOrderingTable();
        renderFieldMapScreen(i << 3);
        renderScreenTransition();
    }
    do {
        clearFieldOrderingTable();
        renderFieldMapScreen(0x80);
        renderScreenTransition();
        updateFieldInputs();
    } while (!(g_padButtonForField & 0x100));
    for (i = 0x10; i > 0; i--) {
        clearFieldOrderingTable();
        renderFieldMapScreen(i << 3);
        renderScreenTransition();
    }
    DrawSync(0);
    freeHeap(fieldMapScreenPolys);
    freeHeap(fieldMapScreenMarkerSprites);
    LoadImage(&rect, saved);
    DrawSync(0);
    freeHeap(saved);
}

/* Set up the 5 static 128x224 semi-transparent sprites of wideImageSprites */
void initWideImageSprites(void) {
    RECT tw;
    s32 i;
    SPRT* s;

    tw.x = 0;
    tw.y = 0;
    tw.w = 0xFF;
    tw.h = 0xFF;
    for (i = 0; i < 5; i++) {
        SetDrawMode(&wideImageSprites.m0_drawModes[i][0], 0, 0, GetTPage(1, 0, (i << 6) + 0x280, 0), &tw);
        SetDrawMode(&wideImageSprites.m0_drawModes[i][1], 0, 0, GetTPage(1, 0, (i << 6) + 0x280, 0), &tw);
        s = &wideImageSprites.m78_sprites[i][0];
        SetSprt(s);
        s->x0 = i << 7;
        s->r0 = 0x80;
        s->g0 = 0x80;
        s->b0 = 0x80;
        s->y0 = 0;
        s->u0 = 0;
        s->v0 = 0;
        s->w = 0x80;
        s->h = 0xE0;
        SetSemiTrans(s, 0);
        s->clut = GetClut(0, 0xE8);
        wideImageSprites.m78_sprites[i][1] = *s;
    }
}

/* Draw the 5 static sprites of wideImageSprites when wideImageSpritesVisible is set */
void drawWideImageSprites(void) {
    s32 i;

    if (wideImageSpritesVisible != 0) {
        for (i = 0; i < 5; i++) {
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &wideImageSprites.m78_sprites[i][g_frameOddOrEven]);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &wideImageSprites.m0_drawModes[i][g_frameOddOrEven]);
        }
    }
}

sptr getScrollingTextGlyph(u8* str, s32* isCustom) {
    u16 code = (str[0] << 8) | str[1];

    if ((u16)(code - 0x8540) < 0x340) {
        *isCustom = 1;
        return code - 0x8540;
    }
    *isCustom = 0;
    return (sptr)func_800405C4(code);
}

/* Expand a 16x15 1bpp glyph (u16 rows, bytes swapped) to one byte per pixel, 18 bytes per row; src -1: blank */
void expandTextGlyph(u8* dst, u16* src) {
    s32 i;
    s32 j;

    if (src == (u16*)-1) {
        for (i = 0; i < 0x120; i++) {
            *dst++ = 0xFF;
        }
        return;
    }
    for (i = 0; i < 15; i++) {
        for (j = 7; j >= 0; j--) {
            *dst++ = (*src >> j) & 1 ? 0xFF : 0;
        }
        for (j = 15; j >= 8; j--) {
            *dst++ = (*src >> j) & 1 ? 0xFF : 0;
        }
        *dst++ = 0;
        *dst++ = 0;
        src++;
    }
}

/* Draw one line of SJIS text (up to 28 chars, 0x0D ends it) into VRAM row <row> at x; custom glyphs (0x8540..) come from VRAM 0x380, others from the BIOS font; returns the next text position */
u8* drawScrollingTextLineToVram(u8* text, s32 x, s32 row) {
    u8 glyph[0x120];
    RECT rect;
    RECT src;
    u8 line[0x40];
    s32 isCustom;
    s32 pos;
    s32 i;
    sptr code;
    s32 j;

    rect.w = 9;
    rect.h = 16;
    rect.y = row << 4;
    for (i = 0; i < 0x40; i++) {
        line[i] = text[i];
    }
    for (j = 0x11F; j >= 0; j--) {
        glyph[j] = 0;
    }
    pos = 0;
    if (scrollingTextBytesLeft <= 0) {
        i = 0;
    } else {
        for (i = 0; i < 0x1C; i++) {
            if (line[pos] == 0x0D) {
                pos++;
                break;
            }
            code = getScrollingTextGlyph(&line[pos], &isCustom);
            pos += 2;
            if (isCustom == 1) {
                src.x = (code % 7) * 9 + 0x380;
                src.y = (code / 7) * 16 + 0x100;
                src.w = 9;
                src.h = 16;
                MoveImage(&src, x + i * 9, row << 4);
            } else {
                expandTextGlyph(glyph, (u16*)code);
                rect.x = x + i * 9;
                LoadImage(&rect, glyph);
            }
            DrawSync(0);
        }
    }
    for (j = 0x11F; j >= 0; j--) {
        glyph[j] = 0;
    }
    for (; i < 0x1C; i++) {
        rect.x = x + i * 9;
        LoadImage(&rect, glyph);
        DrawSync(0);
    }
    scrollingTextBytesLeft -= pos;
    return text + pos;
}

void loadScrollingTextFiles(void) {
    setCurrentDirectory(4, 0);
    scrollingTextBytesLeft = getFileSize(0xAB);
    scrollingTextData = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xAB)), 1);
    readFile(0xAB, scrollingTextData, 0, 0x80);
    waitReadCompletion(0);
    scrollingTextGlyphTim = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xAC)), 1);
    readFile(0xAC, scrollingTextGlyphTim, 0, 0x80);
    waitReadCompletion(0);
}

/* Set up the two fade gradients (scrollingTextFadeGradients) and the 16 text-line sprite rows (scrollingTextLines) */
void initScrollingTextSprites(void) {
    s32 i;
    s32 j;
    SPRT* s;
    FieldTextLine* line;

    SetPolyGT4(&scrollingTextFadeGradients[0][0]);
    SetPolyGT4(&scrollingTextFadeGradients[1][0]);
    scrollingTextFadeGradients[0][0].b0 = 0xFF;
    scrollingTextFadeGradients[0][0].g0 = 0xFF;
    scrollingTextFadeGradients[0][0].r0 = 0xFF;
    scrollingTextFadeGradients[0][0].b1 = 0xFF;
    scrollingTextFadeGradients[0][0].g1 = 0xFF;
    scrollingTextFadeGradients[0][0].r1 = 0xFF;
    scrollingTextFadeGradients[0][0].b2 = 0;
    scrollingTextFadeGradients[0][0].g2 = 0;
    scrollingTextFadeGradients[0][0].r2 = 0;
    scrollingTextFadeGradients[0][0].b3 = 0;
    scrollingTextFadeGradients[0][0].g3 = 0;
    scrollingTextFadeGradients[0][0].r3 = 0;
    scrollingTextFadeGradients[1][0].b0 = 0;
    scrollingTextFadeGradients[1][0].g0 = 0;
    scrollingTextFadeGradients[1][0].r0 = 0;
    scrollingTextFadeGradients[1][0].b1 = 0;
    scrollingTextFadeGradients[1][0].g1 = 0;
    scrollingTextFadeGradients[1][0].r1 = 0;
    scrollingTextFadeGradients[1][0].b2 = 0xFF;
    scrollingTextFadeGradients[1][0].g2 = 0xFF;
    scrollingTextFadeGradients[1][0].r2 = 0xFF;
    scrollingTextFadeGradients[1][0].b3 = 0xFF;
    scrollingTextFadeGradients[1][0].g3 = 0xFF;
    scrollingTextFadeGradients[1][0].r3 = 0xFF;
    scrollingTextFadeGradients[0][0].x0 = 0;
    scrollingTextFadeGradients[0][0].y0 = 0;
    scrollingTextFadeGradients[0][0].x1 = 0x280;
    scrollingTextFadeGradients[0][0].y1 = 0;
    scrollingTextFadeGradients[0][0].x2 = 0;
    scrollingTextFadeGradients[0][0].y2 = 0x18;
    scrollingTextFadeGradients[0][0].x3 = 0x280;
    scrollingTextFadeGradients[0][0].y3 = 0x18;
    scrollingTextFadeGradients[1][0].x0 = 0;
    scrollingTextFadeGradients[1][0].y0 = 0xC8;
    scrollingTextFadeGradients[1][0].x1 = 0x280;
    scrollingTextFadeGradients[1][0].y1 = 0xC8;
    scrollingTextFadeGradients[1][0].x2 = 0;
    scrollingTextFadeGradients[1][0].y2 = 0xE0;
    scrollingTextFadeGradients[1][0].x3 = 0x280;
    scrollingTextFadeGradients[1][0].y3 = 0xE0;
    scrollingTextFadeGradients[0][0].u0 = 0;
    scrollingTextFadeGradients[0][0].v0 = 0;
    scrollingTextFadeGradients[0][0].u1 = 2;
    scrollingTextFadeGradients[0][0].v1 = 0;
    scrollingTextFadeGradients[0][0].u2 = 0;
    scrollingTextFadeGradients[0][0].v2 = 2;
    scrollingTextFadeGradients[0][0].u3 = 2;
    scrollingTextFadeGradients[0][0].v3 = 2;
    scrollingTextFadeGradients[1][0].u0 = 0;
    scrollingTextFadeGradients[1][0].v0 = 0;
    scrollingTextFadeGradients[1][0].u1 = 2;
    scrollingTextFadeGradients[1][0].v1 = 0;
    scrollingTextFadeGradients[1][0].u2 = 0;
    scrollingTextFadeGradients[1][0].v2 = 2;
    scrollingTextFadeGradients[1][0].u3 = 2;
    scrollingTextFadeGradients[1][0].v3 = 2;
    scrollingTextFadeGradients[0][0].tpage = GetTPage(1, 2, 0x3C0, 0x100);
    scrollingTextFadeGradients[1][0].tpage = GetTPage(1, 2, 0x3C0, 0x100);
    scrollingTextFadeGradients[0][0].clut = GetClut(0, 0x1FF);
    scrollingTextFadeGradients[1][0].clut = GetClut(0, 0x1FF);
    SetSemiTrans(&scrollingTextFadeGradients[0][0], 1);
    SetSemiTrans(&scrollingTextFadeGradients[1][0], 1);
    scrollingTextFadeGradients[0][1] = scrollingTextFadeGradients[0][0];
    scrollingTextFadeGradients[1][1] = scrollingTextFadeGradients[1][0];
    scrollingTextLines = allocateHeap(PC_HEAP_SIZED(16 * (s32)sizeof(FieldTextLine), 16 * (s32)0x100), 1);
    for (i = 0; i < 16; i++) {
        line = &scrollingTextLines[i];
        s = &line->m60_sprites[0][0];
        SetSprt(s);
        s->r0 = 0x80;
        s->g0 = 0x80;
        s->b0 = 0x80;
        SetSemiTrans(s, 0);
        s->clut = GetClut(0, 0x1FF);
        s->u0 = 0;
        s->v0 = i << 4;
        s->w = 0x80;
        s->h = 0x10;
        s->x0 = 0x40;
        s->y0 = i << 4;
        line->m60_sprites[1][0] = *s;
        line->m60_sprites[0][1] = *s;
        line->m60_sprites[1][1] = *s;
        line->m60_sprites[0][2] = *s;
        line->m60_sprites[1][2] = *s;
        line->m60_sprites[0][3] = *s;
        line->m60_sprites[1][3] = *s;
        for (j = 0; j < 4; j++) {
            scrollingTextLines[i].m60_sprites[0][j].x0 = j * 0x80 + 0x40;
            scrollingTextLines[i].m60_sprites[1][j].x0 = j * 0x80 + 0x40;
            SetDrawMode(&scrollingTextLines[i].m0_drawModes[0][j], 0, 0, GetTPage(1, 0, j * 0x40 + 0x300, 0), NULL);
            SetDrawMode(&scrollingTextLines[i].m0_drawModes[1][j], 0, 0, GetTPage(1, 0, j * 0x40 + 0x300, 0), NULL);
        }
    }
}

/* Scroll the 16 text lines up by one pixel (VRAM v wraps at 256) and draw them between the two fade gradients */
void renderMovieScrollingText(void) {
    s32 i;
    s32 j;
    s32 y;
    FieldTextLine* lines;

    addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &scrollingTextFadeGradients[1][g_frameOddOrEven]);
    addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &scrollingTextFadeGradients[0][g_frameOddOrEven]);
    lines = scrollingTextLines;
    for (i = 0; i < 16; i++) {
        y = (lines[i].m60_sprites[(g_frameOddOrEven + 1) & 1][0].y0 - 1) & 0xFF;
        lines[i].m60_sprites[g_frameOddOrEven][0].y0 = y;
        lines[i].m60_sprites[g_frameOddOrEven][1].y0 = y;
        lines[i].m60_sprites[g_frameOddOrEven][2].y0 = y;
        lines[i].m60_sprites[g_frameOddOrEven][3].y0 = y;
        for (j = 0; j < 4; j++) {
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &lines[i].m60_sprites[g_frameOddOrEven][j]);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[0], &lines[i].m0_drawModes[g_frameOddOrEven][j]);
        }
    }
}

void uploadScrollingTextTextures(void) {
    RECT rect;
    s32* buf;
    s32 i;
    s32 v;

    loadTimToVram(scrollingTextGlyphTim, 0x380, 0x100, 0, 0x1FF, 0, 0);
    DrawSync(0);
    freeHeap(scrollingTextGlyphTim);
    buf = allocateHeap(PC_HEAP_BYTES(0x200), 1);
    v = -1;
    for (i = 0x7F; i >= 0; i--) {
        buf[i] = v;
    }
    rect.x = 0x3C0;
    rect.y = 0x100;
    rect.w = 0x40;
    rect.h = 4;
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
}

/* Load the name-entry font files (when enabled); the unused 8-byte local reserves retail's stack slot */
void initMovieScrollingText(void) {
    RECT unused;

    if (movieScrollingTextEnabled) {
        loadScrollingTextFiles();
        initScrollingTextSprites();
        scrollingTextFrameCounter = 0;
        scrollingTextLineIndex = 0xF;
        scrollingTextCursor = scrollingTextData;
    }
}

void freeMovieScrollingText(void) {
    if (movieScrollingTextEnabled) {
        freeHeap(scrollingTextData);
        freeHeap(scrollingTextLines);
    }
}

void updateMovieScrollingText(void) {
    if (movieScrollingTextEnabled) {
        if (!(scrollingTextFrameCounter & 0xF)) {
            scrollingTextCursor = drawScrollingTextLineToVram(scrollingTextCursor, 0x300, scrollingTextLineIndex & 0xF);
            scrollingTextLineIndex++;
        }
        scrollingTextFrameCounter++;
    }
}

s16 readS16FromScript(s32 offset) {
    u8* p = pCurrentFieldScriptFile + (pCurrentFieldScriptActor->mCC_scriptPC + offset);
    return p[0] + (p[1] << 8);
}

u16 readU16FromScript(s32 offset) {
    u8* p = pCurrentFieldScriptFile + (pCurrentFieldScriptActor->mCC_scriptPC + offset);
    return p[0] | (p[1] << 8);
}

s32 getImmediateOrVariableUnsigned(s32 offset) {
    u16 value = readU16FromScript(offset);
    if (!(value & 0x8000)) {
        return getVariable(value);
    }
    return value & 0x7FFF;
}
