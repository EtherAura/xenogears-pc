/* Battling overlay, provisional file 3 of 4 (0x8007E528-0x80083DCC): font
 * and text, the menus and settings, gear select, the arena map, backdrop and
 * camera. */

#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "main/dialog.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "battling/battling.h"
#include "main/game_state.h"

VECTOR* ApplyRotMatrixLV(VECTOR* v0, VECTOR* v1);
void SetFarColor(s32 r, s32 g, s32 b);
void SetFogNearFar(s32 a, s32 b, s32 h);
void computeFaceNormal(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* out);
void stopAllSounds(void);
void setFogNearColor(u8 r, u8 g, u8 b);
void freeHeapDelayed(void* p, s32 delay);
s32 isControllerConnected(s32 port);
int pcRumbleOffered(int port);
void mergeQueuedInputs(void);
s32 getAngleCos(s32 angle); /* sin */
s32 getAngleSin(s32 angle); /* cos */
void initBattlingTutorial(void);
void startBattlingVictoryScreen(BattlingGear* gear);
void blurBattlingImage(void* arg0, void* arg1);
void copyBattlingWords(void* dst, void* src, s32 size); /* word copy */
void startBattlingReplay(s32 arg0);
void startBattlingRoundResult(void);
void setBattlingTextPos(s32 arg0, s32 arg1);
s32 drawBattlingChar(s32 c);
s32 getBattlingTextWidth(u8* str);
void printBattlingText(u8* str);
void printBattlingTextOffset(u8* str, s32 arg1);
void setBattlingTextBlinkYellow(s32 arg0);
void setBattlingTextBlinkBlue(s32 arg0);
void drawBattlingGearSelect(void* ot, s32 arg1);
void hideBattlingHelpLabels(void);
void closeBattlingPauseMenu(void);
void highlightBattlingMenuItem(BattlingMenu* menu, s32 index);
u8* getBattlingComLevelName(void);
void drawBattlingGearSelectMenu(void);
s32 adjustBattlingMenuValue(s32 value, s32 max, s32 flags);
void openBattlingMenu(s32 menu);
void freeBattlingScreenCopy(s32 arg0);
void blurBattlingScreenCopy(void);
void captureBattlingScreen(void);
void openBattlingPauseMenu(s32 mode);
void initBattlingHelpLabels(void);
void showBattlingHelpLabel(s32 idx, s32 which);
void drawBattlingHelpLabels(OTag* ot);
s32 updateBattlingMenuInput(BattlingMenu* menu, s32 pad);
void updateBattlingGearSelect(void);
s32 getBattlingGroundHeight(VECTOR* pos, s32 flag);
void updateBattlingOrbitCamera(s32 smooth);
void setBattlingSceneMode(s32 mode);
void loadBattlingGearFile(s32 arg0, s32 arg1);
void freeBattlingGearFile(s32 arg0);
void initBattlingHud(void);
void addBattlingGroundSpanEdge(s32 x0, s32 y0, s32 x1, s32 y1);
void initBattlingGroundPrims(void);
s32 getBattlingVectorLengthXZ(VECTOR* v);            /* length (x, z) */
s32 getBattlingDistance(VECTOR* a, VECTOR* b); /* distance */
s32 isBattlingGearDefeated(s32 id);
void unlockBattlingBonusGear(void);
void* loadBattlingFile(s32 arg0);
void playBattlingSfx(s32 arg0);
extern u16 textSpriteMode1;
extern u8 pad0LeftStickX;  /* analog sticks */
extern u8 pad1LeftStickX;
extern u8 pad0LeftStickY;
extern u8 pad1LeftStickY;
extern volatile s32 g_playTimeInVsync; /* main vsync counter, bumped by the vsync interrupt callback: volatile (retail reads it twice in battlingMain and setBattlingTextBlinkBlue; playBattlingSfx load order) */
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForDialogs2; /* pad 1 newly pressed */
extern u16 newPadButtonForField; /* main: pad 0 auto-repeat */
extern u16 newPadButtonForField2; /* pad 1 auto-repeat */
extern u16 newPadButtonForScripts; /* pad 0 buttons */
extern u16 newPadButtonForScripts2; /* pad 1 buttons */
extern CVECTOR fogNearColor;
extern u16 textSpriteMode0;
extern BattlingGlyph battlingFontGlyphs[];
extern s32 battlingTextScale;
extern s16 battlingPortraitNeighborOffsets[2][4];
extern s32 battlingDrawScreenCopyOnce;
extern u8* battlingComLevelNames[];
extern u8 battlingFrameRates[];
extern s32 battlingMotionSpeeds[];
extern u8* battlingComAiNames[];
extern s32 battlingMenuPad;
/* The item lists of menus 5 and 6; the code sets their first items' help
 * labels. */
extern BattlingMenuItem battlingGear1MenuItems[2];
extern BattlingMenuItem battlingGear2MenuItems[2];
extern BattlingMenu battlingMenus[8]; /* [5] 0x800916D8, [6] 0x80091714, [7] 0x80091750 */
extern BattlingStageColors battlingStageColorSets[3];
extern BattlingGearEntry battlingGearEntries[49];
extern POLY_FT4* battlingGlyphPolys[2];
extern s32 battlingGlyphCount;
extern u16 battlingFontTPage;
extern u16 battlingFontClut;
extern s16 battlingTextX; /* text cursor x */
extern s16 battlingTextY; /* text cursor y */
extern u8 battlingTextR;
extern u8 battlingTextG;
extern u8 battlingTextB;
extern u8 battlingScreenBlurCount;
extern s32 battlingSelectedGear1;
extern s32 battlingSelectedGear2;
extern u8 battlingAnalogWarningTimer;
extern BattlingGearPortrait* battlingPortraitSlots;
extern s32 battlingGearSelectDone; /* s32: updateBattlingGearSelect switches on it (drawBattlingGearSelectMenu shifts it as u32) */
extern s32 battlingPrevSelectedGear1;
extern s32 battlingGear1ListScroll;
extern s32 battlingGear1PortraitScroll;
extern s32 battlingPrevSelectedGear2;
extern s32 battlingGear2ListScroll;
extern s32 battlingGear2PortraitScroll;
extern s32 battlingGear1NameBlink;
extern s32 battlingGear2NameBlink;
extern BattlingMenu* battlingCurrentMenu; /* current menu */
extern BattlingMenu* battlingPreviousMenu; /* previous menu */
extern u8 battlingTopHelpLabel;
extern u8 battlingBottomHelpLabel;
extern s32 battlingNextHelpLabel;
extern s32 battlingMenuPadHeld;
extern s32 battlingMenuPadRepeat; /* auto-repeat */
extern s32 battlingMenuPadPressed; /* newly pressed */
extern s32 battlingGear2SelectActive;
extern u8 battlingPauseMenuShown;
extern u8 battlingBlurScreenCopyPending;
extern u8* battlingScreenCopy;
extern u8 battlingMenuStickWarned;
extern SVECTOR battlingGroundNormal; /* ground normal under the last position (getBattlingGroundHeight) */
extern s32 battlingCameraDistance;
extern s32 battlingCameraHeight;
extern s32 battlingCameraAngle; /* camera angle */
extern POLY_FT4* battlingRingWallPolys[2]; /* ring wall: 64 polys per display buffer */
extern s32 battlingSceneMode;
extern s32 battlingNextSceneMode;
extern u16 battlingRingWallClut; /* clut */
extern u16 battlingRingWallTPage; /* tpage */
extern u16 battlingRingWallV; /* v of the texture */
extern s32 battlingOrbitCameraAngle;
extern s32 battlingOrbitCameraSpeed;
extern s16 battlingScreenHeight; /* screen height */
extern BattlingGearDef* battlingGearDefs;
extern u8 battlingCameraLag;
extern u8* battlingDialogTexts;
extern u8 battlingRubberBandMode;
extern s32 battlingGearListCount;
extern BattlingStageColors* battlingStageColors;
extern s32 battlingRoundWinner; /* winner side */
extern u8 battlingBufferIndex;
extern s32 battlingResultTimer; /* KO camera timer */
extern u8 battlingStageColorIndex;
extern s32 battlingGameMode;
extern u8* battlingPortraitFile;
extern BattlingMapCell* battlingHeightMap; /* 128 x 128 arena height map */
extern s32 battlingFrameCounter;
extern BattlingGearEntry** battlingGearList;
extern u8 battlingCameraSide;
extern u8 battlingActivePad;
extern s32 battlingBackColorB;
extern s32 battlingCameraSideAngle;
extern s32 battlingBackColorG;
extern s32 battlingBackColorR;
extern s32 battlingStartRequested;
extern s32 battlingSpeedScale;
extern OTag* battlingCurrentOt; /* current OT */
extern s32 battlingPortraitsLoaded;
extern s32 battlingFightTime;
extern s32 battlingRoundFrames;
extern s32 battlingRoundNumber;
extern DR_MOVE battlingScreenCopyMoves[2];
extern DR_TPAGE battlingMenuTileTPages[2];
extern BattlingSprtTPage battlingAnalogWarningSprites[2];
extern SPRT battlingAnalogWarningSprt; /* battlingAnalogWarningSprites[0].m8_sprt, addressed on its own (retail codegen) */
#define battlingAnalogWarningSprt (battlingAnalogWarningSprites[0].m8_sprt)
extern BattlingLabel battlingHelpLabels[2]; /* help labels */
/* Splat names inside the arrays above: their PC layouts differ. */
extern DR_TPAGE battlingHelpLabelTPages[2];
extern POLY_G4 battlingBackdropGradient[2]; /* backdrop gradient */
extern DR_TPAGE battlingDemoBlurTPages[4];
extern SPRT battlingDemoBlurSprites[6];
extern VECTOR battlingViewOrigin; /* camera focus offset (just before gear 0; zeroed by startBattlingVictoryScreen) */
extern BattlingGear battlingGear2;
extern VECTOR battlingCameraTarget; /* camera target */
extern VECTOR battlingCameraPos; /* camera position */
extern BattlingGear battlingGear1;
extern BattlingSettings battlingSettings;
extern POLY_FT4 battlingPortraitPolys[2][10];
extern BattlingDisplayBuffer battlingDisplayBuffers[2];

void showBattlingAnalogWarning(s32 arg0) {
    if (battlingAnalogWarningTimer == 0 && arg0 == 10) {
        playBattlingSfx(0x24);
    }
    battlingAnalogWarningTimer = arg0;
}

void drawBattlingAnalogWarning(void* ot) {
    if (battlingAnalogWarningTimer != 0) {
        if (battlingFrameCounter & 2) {
            AddPrim(ot, &battlingAnalogWarningSprites[battlingBufferIndex].m8_sprt);
            AddPrim(ot, &battlingAnalogWarningSprites[battlingBufferIndex].m0_tpage);
        }
        battlingAnalogWarningTimer--;
    }
}

s32 getBattlingGlyphCount(void) {
    return battlingGlyphCount;
}

void initBattlingFont(void** files) {
    TIM_IMAGE tim;
    s32 i;
    s16* clut;
    u8 pad[8]; /* unused: reserves the 8 bytes of retail's frame */

    battlingGlyphPolys[0] = allocateHeap(100 * (s32)sizeof(POLY_FT4), 0);
    battlingGlyphPolys[1] = allocateHeap(100 * (s32)sizeof(POLY_FT4), 0);
    for (i = 0; i < 100; i++) {
        setlen(&battlingGlyphPolys[0][i], 0);
        setlen(&battlingGlyphPolys[1][i], 0);
    }
    OpenTIM(files[15]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[2] = 0x9063;
    clut[0] = 0;
    clut[3] = -1;
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingFontClut = GetClut(tim.crect->x, tim.crect->y);
    battlingFontTPage = GetTPage(0, 1, tim.prect->x, tim.prect->y);
    battlingGlyphCount = 0;
    OpenTIM(files[25]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    setlen(&battlingAnalogWarningSprt, 4);
    setcode(&battlingAnalogWarningSprt, 0x65);
    SetDrawTPage(&battlingAnalogWarningSprites[0].m0_tpage, 0, 0, GetTPage(0, 1, tim.prect->x, tim.prect->y));
    battlingAnalogWarningSprites[0].m8_sprt.clut = GetClut(tim.crect->x, tim.crect->y);
    battlingAnalogWarningSprites[0].m8_sprt.x0 = 0x40;
    battlingAnalogWarningSprites[0].m8_sprt.y0 = 0xBE;
    battlingAnalogWarningSprites[0].m8_sprt.w = 0xC4;
    battlingAnalogWarningSprites[0].m8_sprt.h = 0xD;
    battlingAnalogWarningSprites[0].m8_sprt.u0 = tim.prect->x * 4;
    battlingAnalogWarningSprites[0].m8_sprt.v0 = tim.prect->y;
    battlingAnalogWarningSprites[1] = battlingAnalogWarningSprites[0];
}

void setBattlingTextPos(s32 arg0, s32 arg1) {
    battlingTextX = arg0;
    battlingTextY = arg1;
}

BattlingGlyph* getBattlingGlyph(s32 c) {
    if ((u32)(c - '0') < 10) {
        c = c - '0';
    } else if ((u32)(c - 'A') < 26) {
        c = c - 'A' + 10;
    } else {
        switch (c) {
        case '!':
            c = 0x24;
            break;
        case ':':
            c = 0x25;
            break;
        case '-':
            c = 0x26;
            break;
        case '/':
            c = 0x27;
            break;
        case '#':
            c = 0x28;
            break;
        case ' ':
            c = 0x29;
            break;
        case '\'':
            c = 0x2A;
            break;
        default:
            return NULL;
        }
    }
    return &battlingFontGlyphs[c];
}

void setBattlingTextScale(s32 arg0) {
    battlingTextScale = arg0;
}

s32 drawBattlingChar(s32 c) {
    POLY_FT4* p;
    BattlingGlyph* g;
    s32 x1;
    s16 x;
    s16 y;

    if (battlingGlyphCount < 101) {
        p = battlingGlyphPolys[battlingBufferIndex];
        p += battlingGlyphCount;
        g = getBattlingGlyph(c);
        if (g != NULL) {
            if (c != 0x28) {
                x = battlingTextX;
                y = battlingTextY;
                c = g->m2_width | 3;
                *(u32*)&p->x0 = x | (y << 16);
                x1 = x + ((c * battlingTextScale) >> 8);
                *(u32*)&p->x1 = x1 | (y << 16);
                *(u32*)&p->x2 = x | ((y + g->m3_height) << 16);
                *(u32*)&p->x3 = x1 | ((y + g->m3_height) << 16);
                *(u16*)&p->u0 = g->m0_u | (g->m1_v << 8);
                *(u16*)&p->u1 = (g->m0_u + c) | (g->m1_v << 8);
                *(u16*)&p->u2 = g->m0_u | ((g->m1_v + (g->m3_height + 1)) << 8);
                *(u16*)&p->u3 = (g->m0_u + c) | ((g->m1_v + (g->m3_height + 1)) << 8);
                p->tpage = battlingFontTPage;
                p->clut = battlingFontClut;
                setlen(p, 9);
                *(u32*)&p->r0 = battlingTextR | (battlingTextG << 8) | (battlingTextB << 16) | 0x2C000000;
                battlingGlyphCount++;
            }
            battlingTextX += 2 + ((g->m2_width * battlingTextScale) >> 8);
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 getBattlingTextWidth(u8* str) {
    s32 w = 0;

    while (*str != 0) {
        w += 2 + ((getBattlingGlyph(*str++)->m2_width * battlingTextScale) >> 8);
    }
    return w;
}

void printBattlingText(u8* str) {
    s32 x = battlingTextX;

    while (*str != 0) {
        drawBattlingChar(*str++);
    }
    battlingTextX = x;
    battlingTextY += 20;
}

void printBattlingTextCentered(u8* str) {
    s32 x = battlingTextX;

    battlingTextX -= getBattlingTextWidth(str) / 2;
    while (*str != 0) {
        drawBattlingChar(*str++);
    }
    battlingTextX = x;
    battlingTextY += 20;
}

void printBattlingTextRight(u8* str) {
    s32 x = battlingTextX;

    battlingTextX -= getBattlingTextWidth(str);
    while (*str != 0) {
        drawBattlingChar(*str++);
    }
    battlingTextX = x;
    battlingTextY += 20;
}

void printBattlingTextOffset(u8* str, s32 arg1) {
    s32 x = battlingTextX;
    u8 pad[8]; /* unused: reserves the 8 bytes of retail's frame */

    battlingTextX = x - arg1;
    while (*str != 0) {
        drawBattlingChar(*str++);
    }
    battlingTextX = x;
    battlingTextY += 20;
}

void setBattlingTextBlinkYellow(s32 arg0) {
    s32 c = 0xFF;

    if (arg0 != 0) {
        battlingTextG = 0xFF;
        battlingTextB = 0;
        battlingTextR = g_playTimeInVsync * 20;
    } else {
        battlingTextR = c;
        battlingTextG = c;
        battlingTextB = c;
    }
}

void setBattlingTextBlinkBlue(s32 arg0) {
    s32 c;
    s32 t0;
    s32 t1;
    s32 r0;
    s32 r1;

    if (arg0 != 0) {
        t0 = g_playTimeInVsync;
        t1 = g_playTimeInVsync;
        battlingTextB = 0xFF;
        r0 = 0xFF - t0 * 20; /* int temporaries: a direct u8 store narrows 0xFF - x to nor */
        battlingTextR = r0;
        r1 = 0xFF - t1 * 20;
        battlingTextG = r1;
    } else {
        c = 0xFF;
        battlingTextR = c;
        battlingTextG = c;
        battlingTextB = c;
    }
}

int pcArenaAllGears(void); /* port/src/system/arena.c: the Battle arena's "every Gear" setting */
extern u8 battlingBonusGearUnlocked;
void buildBattlingGearList(s32 filter) {
    s32 i;
    s32 limit;
    BattlingGearDef* def;
    BattlingGearEntry* entry;
    BattlingGearEntry** list;

    limit = gameState.m1930_fieldVarsBackup[0];
    if (filter != 0 && pcArenaAllGears()) {
        /* every unlock level passes, Argento (999) too: already listed, so
         * unlockBattlingBonusGear never adds it a second time */
        limit = 0x7FFF;
        battlingBonusGearUnlocked = 1;
    }
    battlingGearList = allocateHeap(49 * (s32)sizeof(BattlingGearEntry*), 1);
    battlingGearListCount = 0;
    for (i = 0, list = battlingGearList, def = battlingGearDefs, entry = battlingGearEntries; i < 49; def++, i++, entry++) {
        if (filter == 0 || def->m4_unlockLevel <= limit) {
            list[battlingGearListCount++] = entry;
        }
    }
    if (filter != 0) {
        unlockBattlingBonusGear();
    }
}

void initBattlingPortraitSlots(void) {
    u8 pad[0x30]; /* unused: reserves retail's 0x30 bytes of locals */
    s32 i;
    s32 j;
    s32 n;
    s16 y;
    s16 x;
    BattlingGearPortrait* p;

    p = battlingPortraitSlots = allocateHeap(0x3D4, 0);
    n = 0x1FF;
    for (i = 0; i < 7; i++) {
        y = i * 32 + 0x1A0;
        for (j = 0; j < 7; j++) {
            x = j << 6;
            p->m0_clutRect.x = 0x200;
            p->m0_clutRect.y = n--;
            p->m0_clutRect.w = 0x80;
            p->m0_clutRect.h = 1;
            p->m8_imageRect.x = y;
            p->m8_imageRect.y = x;
            p->m8_imageRect.w = 0x1E;
            p->m8_imageRect.h = 0x40;
            p++;
        }
    }
}

void drawBattlingGearPortrait(s32 idx, POLY_FT4* p, s32 side, s32 x, s32 size) {
    BattlingGearPortrait* b;
    s32 u0;
    s32 u1;
    s32 c;
    s32 rgb;
    s32 d;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;
    s32 t;
    s32 t2;
    s32 y1s;
    s32 ys;
    s32 xy1;
    s32 xy2;
    s32 xy3;

    if (side != 0) {
        x += 0xD3;
    } else {
        x += 0x33;
    }
    if (battlingGearListCount - 1 < idx) {
        idx -= battlingGearListCount;
    }
    if (idx < 0) {
        idx += battlingGearListCount;
    }
    idx = battlingGearList[idx]->m0_id;
    b = &battlingPortraitSlots[idx];
    if (size == 0x40) {
        setlen(p, 9);
        setcode(p, 0x2D);
        *(u32*)&p->x0 = x | 0x300000;
        *(u32*)&p->x1 = (x + 0x3C) | 0x300000;
        *(u32*)&p->x2 = x | 0x700000;
        *(u32*)&p->x3 = (x + 0x3C) | 0x700000;
    } else {
        /* scaled portrait: grey level and quad grow with size (the many
         * single-use temporaries reproduce retail's instruction order) */
        setlen(p, 9);
        size += 0x40;
        c = size | (size << 8) | (size << 16);
        size -= 0x40;
        size >>= 4;
        rgb = c | 0x2C000000;
        *(u32*)&p->r0 = rgb;
        d = size - 4;
        x0 = x - d;
        y0 = 0x34 - size;
        ys = y0 << 16;
        *(u32*)&p->x0 = x0 | ys;
        t = x0 + 0x34;
        x1 = t + size * 2;
        xy1 = x1 | ys;
        t2 = y0 + 0x38;
        y1 = t2 + size * 2;
        y1s = y1 << 16;
        xy2 = x0 | y1s;
        xy3 = x1 | y1s;
        *(u32*)&p->x1 = xy1;
        *(u32*)&p->x2 = xy2;
        *(u32*)&p->x3 = xy3;
    }
    u0 = b->m8_imageRect.x * 2;
    u1 = u0 + 0x3B;
    *(u16*)&p->u0 = u0 | (b->m8_imageRect.y << 8);
    *(u16*)&p->u1 = u1 | (b->m8_imageRect.y << 8);
    *(u16*)&p->u2 = u0 | ((b->m8_imageRect.y + 0x3F) << 8);
    *(u16*)&p->u3 = u1 | ((b->m8_imageRect.y + 0x3F) << 8);
    p->clut = GetClut(b->m0_clutRect.x, b->m0_clutRect.y);
    p->tpage = GetTPage(1, 0, b->m8_imageRect.x & 0xFF80, b->m8_imageRect.y);
    AddPrim(battlingCurrentOt, p);
}

void drawBattlingGearSelect(void* ot, s32 arg1) {
    POLY_FT4* p;
    s32 d;
    s32 idx;
    u8 pad[0x20]; /* unused: reserves retail's 0x20 bytes of locals */

    p = battlingPortraitPolys[battlingBufferIndex];
    d = battlingPrevSelectedGear1 - battlingSelectedGear1;
    if (d != 0) {
        if (abs(d) >= 4) {
            d = -d;
        }
        battlingGear1ListScroll = (d > 0) ? -0x24 : 0x24;
        battlingGear1PortraitScroll = battlingGear1ListScroll = battlingGear1ListScroll;
    }
    d = battlingPrevSelectedGear2 - battlingSelectedGear2;
    if (d != 0) {
        if (abs(d) >= 4) {
            d = -d;
        }
        battlingGear2ListScroll = (d > 0) ? -0x24 : 0x24;
        battlingGear2PortraitScroll = battlingGear2ListScroll = battlingGear2ListScroll;
    }
    if (battlingGear1ListScroll != 0) {
        if (battlingGear1ListScroll > 0) {
            battlingGear1ListScroll -= 2;
        } else {
            battlingGear1ListScroll += 2;
        }
    }
    if (battlingGear1PortraitScroll != 0) {
        if (battlingGear1PortraitScroll > 0) {
            battlingGear1PortraitScroll -= 4;
        } else {
            battlingGear1PortraitScroll += 4;
        }
    }
    if (battlingGear2ListScroll != 0) {
        if (battlingGear2ListScroll > 0) {
            battlingGear2ListScroll -= 2;
        } else {
            battlingGear2ListScroll += 2;
        }
    }
    if (battlingGear2PortraitScroll != 0) {
        if (battlingGear2PortraitScroll > 0) {
            battlingGear2PortraitScroll -= 4;
        } else {
            battlingGear2PortraitScroll += 4;
        }
    }
    idx = battlingGear1ListScroll >= 0;
    battlingPrevSelectedGear1 = battlingSelectedGear1;
    battlingPrevSelectedGear2 = battlingSelectedGear2;
    drawBattlingGearPortrait(battlingSelectedGear1, p++, 0, battlingGear1PortraitScroll, ((0x24 - abs(battlingGear1PortraitScroll)) << 6) / 36);
    if (!(battlingGearSelectDone & 1)) {
        drawBattlingGearPortrait(battlingSelectedGear1 + battlingPortraitNeighborOffsets[idx][0], p++, 0, battlingPortraitNeighborOffsets[idx][3] + battlingGear1ListScroll, (abs(battlingGear1ListScroll) << 6) / 36);
        drawBattlingGearPortrait(battlingSelectedGear1 + battlingPortraitNeighborOffsets[idx][1], p++, 0, -0x24, 0);
        drawBattlingGearPortrait(battlingSelectedGear1 + battlingPortraitNeighborOffsets[idx][2], p++, 0, 0x24, 0);
    }
    idx = battlingGear2ListScroll >= 0;
    drawBattlingGearPortrait(battlingSelectedGear2, p++, 1, battlingGear2PortraitScroll, ((0x24 - abs(battlingGear2PortraitScroll)) << 6) / 36);
    if (!(battlingGearSelectDone & 2)) {
        drawBattlingGearPortrait(battlingSelectedGear2 + battlingPortraitNeighborOffsets[idx][0], p++, 1, battlingPortraitNeighborOffsets[idx][3] + battlingGear2ListScroll, (abs(battlingGear2ListScroll) << 6) / 36);
        drawBattlingGearPortrait(battlingSelectedGear2 + battlingPortraitNeighborOffsets[idx][1], p++, 1, -0x24, 0);
        drawBattlingGearPortrait(battlingSelectedGear2 + battlingPortraitNeighborOffsets[idx][2], p++, 1, 0x24, 0);
    }
    setBattlingTextPos(0xA0, 0x78);
    printBattlingTextCentered("VS");
    setBattlingTextPos(0x50, 0x78);
    if (isBattlingGearDefeated(battlingGearList[battlingSelectedGear1]->m0_id)) {
        setBattlingTextBlinkBlue(battlingGear1NameBlink);
    } else {
        setBattlingTextBlinkYellow(battlingGear1NameBlink);
    }
    printBattlingTextCentered(battlingGearList[battlingSelectedGear1]->m8_name);
    setBattlingTextPos(0xF0, 0x78);
    if (isBattlingGearDefeated(battlingGearList[battlingSelectedGear2]->m0_id)) {
        setBattlingTextBlinkBlue(battlingGear2NameBlink);
    } else {
        setBattlingTextBlinkYellow(battlingGear2NameBlink);
    }
    printBattlingTextCentered(battlingGearList[battlingSelectedGear2]->m8_name);
    setBattlingTextBlinkYellow(0);
}

/*
 * Initialized .data owned by this file: 0x800912F4-0x80091364 (the two string
 * tables must be defined here, between drawBattlingGearSelect and drawBattlingOptionsValues, so
 * that their strings land in .rodata at 0x8006FE8C-0x8006FF5B, in reverse
 * table order, as in retail).
 */
u8* battlingComLevelNames[] = { "EASY", "NORMAL", "HARD" };
u8 battlingFrameRates[] = { 30, 20, 15, 12, 10, 6, 5, 4, 3, 2, 1, 0 };
s32 battlingMotionSpeeds[] = { 0x60, 0x80, 0xBB, 0x100, 0x180, 0x200, 0x300, 0x400 };
u8* battlingComAiNames[] = {
#if VERSION_JP
    "STAND", "GUARD", "CONTROLER2", "ATACK HARD", "ATACK SLOW", "MAGIC", "JUMP MAGIC",
    "JUMP", "CHASE", "AWAY", "BACKDUSH", "BATTLE EASY", "BATTLE NORMAL", "BATTLE HARD",
#else
    "BYSTANDER", "ON GUARD", "CONTROLLER2", "UP AND AT'EM", "SLOWPOKE", "MAGIC FIRER ", "MAGIC JUMPER",
    "KANGAROO", "GIVE CHASE", "RUN AWAY", "BACK DASH", "EASY BATTLE", "NORMAL BATTLE", "HARD BATTLE",
#endif
};

void hideBattlingHelpLabels(void) {
    battlingBottomHelpLabel = 0;
    battlingTopHelpLabel = 0;
    battlingNextHelpLabel = 0;
}

void resumeBattlingFromMenu(void) {
    u8 pad[8]; /* unused: reserves the 8 bytes of retail's frame */
    s32 flags1;
    s32 flags2;

    battlingDrawScreenCopyOnce = 1;
    setBattlingSceneMode(1);
    battlingCurrentMenu = NULL;
    hideBattlingHelpLabels();
    /* read-modify-write through locals assigned twice (register order) */
    flags2 = battlingGear1.mD4_cues.flags;
    flags2 |= 0xC;
    battlingGear1.mD4_cues.flags = flags2;
    flags1 = battlingGear2.mD4_cues.flags;
    flags1 |= 0xC;
    battlingGear2.mD4_cues.flags = flags1;
}

void closeBattlingPauseMenu(void) {
    u8 pad[8]; /* unused: reserves the 8 bytes of retail's frame */

    setBattlingSceneMode(1);
    battlingCurrentMenu = NULL;
    hideBattlingHelpLabels();
}

void giveUpBattling(void) {
    s32 next;

    openBattlingPauseMenu(0);
    if (battlingSettings.m7_winsNeeded != 0 || battlingRoundNumber == 1) {
        next = 3;
    } else {
        battlingRoundNumber--;
        next = 6;
    }
    setBattlingSceneMode(next);
}

void highlightBattlingMenuItem(BattlingMenu* menu, s32 index) {
    if (menu->m12_selected == index) {
        setBattlingTextBlinkYellow(1);
    } else {
        setBattlingTextBlinkYellow(0);
    }
}

u8* getBattlingComLevelName(void) {
    return battlingComLevelNames[battlingSettings.m0_comLevel];
}

void drawBattlingOptionsValues(BattlingMenu* menu) {
    char buf[8];

    setBattlingTextPos(menu->m1C_tiles[0].x0 + menu->m1C_tiles[0].w - 10, menu->m14_y);
    setBattlingTextBlinkYellow(0);
    highlightBattlingMenuItem(menu, 0);
    printBattlingTextRight(getBattlingComLevelName());
    highlightBattlingMenuItem(menu, 1);
    sprintf(buf, "%d", battlingSettings.m9_speedIndex + 1);
    battlingSettings.mC_speed = battlingSpeedScale = battlingMotionSpeeds[battlingSettings.m9_speedIndex];
    printBattlingTextRight(buf);
    highlightBattlingMenuItem(menu, 2);
    sprintf(buf, "%dFPS", battlingFrameRates[battlingSettings.m2_frameSkip]);
    printBattlingTextRight(buf);
    highlightBattlingMenuItem(menu, 3);
    printBattlingTextRight(battlingSettings.m5_p1Com ? "COM" : "USER1");
    highlightBattlingMenuItem(menu, 4);
    printBattlingTextRight(battlingSettings.m6_p2Com ? "COM" : "USER2");
    setBattlingTextBlinkYellow(0);
}

void drawBattlingPracticeValues(BattlingMenu* menu) {
    char buf[8];

    setBattlingTextPos(menu->m1C_tiles[0].x0 + menu->m1C_tiles[0].w - 10, menu->m14_y);
    setBattlingTextBlinkYellow(0);
    printBattlingTextRight("");
    highlightBattlingMenuItem(menu, 1);
    printBattlingTextRight(battlingComAiNames[battlingSettings.mA_comAi]);
    showBattlingHelpLabel(battlingSettings.mA_comAi + 0x15, 1);
    highlightBattlingMenuItem(menu, 2);
    sprintf(buf, "%dFPS", battlingFrameRates[battlingSettings.m2_frameSkip]);
    printBattlingTextRight(buf);
    setBattlingTextBlinkYellow(0);
}

#if VERSION_JP
#define VIBRATION_ON "VIBRATE ON"
#define VIBRATION_OFF "VIBRATE OFF"
#else
#define VIBRATION_ON "VIBRATION ON"
#define VIBRATION_OFF "VIBRATION OFF"
#endif

void drawBattlingGearSelectMenu(void) {
    s32 active;
    BattlingMenu* menu;
    u8 pad[8]; /* unused: reserves the 8 bytes of retail's frame */

    setBattlingTextPos(0xA0, 0x8C);
    active = !(battlingGearSelectDone & 1);
    menu = &battlingMenus[5];
    if (active && menu->m12_selected == 0) {
        battlingGear1NameBlink = 1;
    } else {
        battlingGear1NameBlink = 0;
    }
    setBattlingTextPos(0x50, 0x8C);
    /* a pad with motors offers it as a DualShock would (vibration=, port/src/system/rumble_game.c) */
    if ((isControllerConnected(0) == 4 || pcRumbleOffered(0)) && battlingSettings.m5_p1Com == 0) {
        if (active) {
            highlightBattlingMenuItem(menu, 1);
        }
        printBattlingTextCentered(battlingSettings.m3_p1Vibration & 1 ? VIBRATION_ON : VIBRATION_OFF);
        battlingMenus[5].m4_items[1].m0_flags &= ~4;
    } else {
        battlingMenus[5].m4_items[1].m0_flags |= 4;
    }
    setBattlingTextBlinkYellow(0);
    active = !(((u32)battlingGearSelectDone >> 1) & 1);
    if (active && battlingGear2SelectActive == 0) {
        active = 0;
    }
    menu = &battlingMenus[6];
    if (active && menu->m12_selected == 0) {
        battlingGear2NameBlink = 1;
    } else {
        battlingGear2NameBlink = 0;
    }
    setBattlingTextPos(0xF0, 0x8C);
    /* a pad with motors offers it as a DualShock would (vibration=, port/src/system/rumble_game.c) */
    if ((isControllerConnected(1) == 4 || pcRumbleOffered(1)) && battlingSettings.m6_p2Com == 0) {
        if (active) {
            highlightBattlingMenuItem(menu, 1);
        }
        printBattlingTextCentered(battlingSettings.m4_p2Vibration & 1 ? VIBRATION_ON : VIBRATION_OFF);
        battlingMenus[6].m4_items[1].m0_flags &= ~4;
    } else {
        battlingMenus[6].m4_items[1].m0_flags |= 4;
    }
    setBattlingTextBlinkYellow(0);
    drawBattlingGearSelect(battlingCurrentOt, 1);
}

void drawBattlingBonusValues(BattlingMenu* menu) {
    char buf[16];

    setBattlingTextBlinkYellow(0);
    setBattlingTextPos(menu->m1C_tiles[0].x0 + menu->m1C_tiles[0].w - 10, menu->m14_y);
    printBattlingTextRight("");
    printBattlingTextRight("");
    printBattlingTextRight("");
    printBattlingTextRight("");
    highlightBattlingMenuItem(menu, 4);
    if (battlingSettings.m7_winsNeeded != 0) {
        sprintf(buf, "%d", battlingSettings.m7_winsNeeded);
        printBattlingTextRight(buf);
    } else {
        printBattlingTextRight("#");
    }
    highlightBattlingMenuItem(menu, 5);
    printBattlingTextRight(battlingComLevelNames[battlingSettings.m0_comLevel]);
    highlightBattlingMenuItem(menu, 6);
    printBattlingTextRight(battlingRubberBandMode ? "ON" : "OFF");
    setBattlingTextBlinkYellow(0);
}

s32 adjustBattlingMenuValue(s32 value, s32 max, s32 flags) {
    s32 step;
    s32 changed;
    u32 pad;

    step = 1;
    if (flags & 4) {
        step = -1;
    }
    changed = 0;
    if (flags & 2) {
        pad = battlingMenuPadRepeat;
    } else {
        pad = battlingMenuPadPressed;
    }
    if (pad & 0x2000) {
        value += step;
    }
    if (pad & 0x8000) {
        value -= step;
    }
    if (pad & 0xA000) {
        changed = 1;
    }
    if (flags & 1) {
        if (value == -1) {
            value = max;
        }
        if (max < value) {
            value = 0;
        }
    } else {
        if (value == -1) {
            changed = 0;
            value = 0;
        }
        if (max < value) {
            changed = 0;
            value = max;
        }
    }
    if (changed) {
        playBattlingSfx(0x20);
    }
    return value;
}

void adjustBattlingComLevel(void) {
    battlingSettings.m0_comLevel = adjustBattlingMenuValue(battlingSettings.m0_comLevel, 2, 0);
}

void adjustBattlingMotionSpeed(void) {
    battlingSettings.m9_speedIndex = adjustBattlingMenuValue(battlingSettings.m9_speedIndex, 7, 2);
}

void adjustBattlingFrameRate(void) {
    battlingSettings.m2_frameSkip = adjustBattlingMenuValue(battlingSettings.m2_frameSkip, 4, 2);
}

void toggleBattlingP1Vibration(void) {
    battlingSettings.m3_p1Vibration = adjustBattlingMenuValue(battlingSettings.m3_p1Vibration, 1, 1);
}

void toggleBattlingP2Vibration(void) {
    battlingSettings.m4_p2Vibration = adjustBattlingMenuValue(battlingSettings.m4_p2Vibration, 1, 1);
}

void toggleBattlingP1Com(void) {
    battlingSettings.m5_p1Com = adjustBattlingMenuValue(battlingSettings.m5_p1Com, 1, 1);
}

void toggleBattlingP2Com(void) {
    battlingSettings.m6_p2Com = adjustBattlingMenuValue(battlingSettings.m6_p2Com, 1, 1);
}

void adjustBattlingNumOfMatches(void) {
    battlingSettings.m7_winsNeeded = adjustBattlingMenuValue(battlingSettings.m7_winsNeeded, 3, 2);
}

void toggleBattlingRubberBand(void) {
    battlingRubberBandMode = adjustBattlingMenuValue(battlingRubberBandMode, 1, 1);
}

void adjustBattlingPracticeAi(void) {
    battlingSettings.mA_comAi = adjustBattlingMenuValue(battlingSettings.mA_comAi, 13, 3);
}

void selectBattlingGear1(void) {
    s32 same;

    if (battlingMenuPad == 0 && (newPadButtonForDialogs & 0x40)) {
        battlingGearSelectDone &= ~1;
        freeBattlingGearFile(0);
        playBattlingSfx(0x22);
    }
    if (!(battlingGearSelectDone & 1)) {
        same = battlingSelectedGear1 == battlingSelectedGear2;
        do {
            battlingSelectedGear1 = adjustBattlingMenuValue(battlingSelectedGear1, battlingGearListCount - 1, 3);
        } while (battlingSelectedGear1 == battlingSelectedGear2 && !(battlingMenuPadHeld & 1) && !same);
        if (battlingMenuPad == 0 && (newPadButtonForDialogs & 0x20)) {
            battlingGearSelectDone |= 1;
            playBattlingSfx(0x21);
            loadBattlingGearFile(0, battlingGearList[battlingSelectedGear1]->m0_id);
        }
    }
}

void selectBattlingGear2(void) {
    s32 same;

    if (battlingMenuPadPressed & 0x40) {
        if (battlingGameMode != 2) {
            battlingGearSelectDone = 0;
            freeBattlingGearFile(1);
            playBattlingSfx(0x22);
            return;
        }
        battlingGearSelectDone &= ~2;
    }
    if (!(battlingGearSelectDone & 2)) {
        same = battlingSelectedGear1 == battlingSelectedGear2;
        do {
            battlingSelectedGear2 = adjustBattlingMenuValue(battlingSelectedGear2, battlingGearListCount - 1, 3);
        } while (battlingSelectedGear1 == battlingSelectedGear2 && !(battlingMenuPadHeld & 1) && !same);
        if (battlingMenuPadPressed & 0x20) {
            battlingGearSelectDone |= 2;
            playBattlingSfx(0x21);
            loadBattlingGearFile(1, battlingGearList[battlingSelectedGear2]->m0_id);
        }
    }
}

void loadBattlingDemoGears(void) {
    s32 a;
    s32 b;

    a = battlingGearList[battlingSelectedGear1]->m0_id;
    b = battlingGearList[battlingSelectedGear2]->m0_id;
    switch (a) {
    case 4:
    case 7:
    case 11:
    case 30:
    case 31:
        a = b == 0;
        break;
    }
    loadBattlingGearFile(0, a);
    switch (b) {
    case 4:
    case 7:
    case 11:
    case 30:
    case 31:
        b = a != 1;
        break;
    }
    loadBattlingGearFile(1, b);
}

void loadBattlingPortraitPair(s32 gear1, s32 gear2) {
    u8* buf;
    BattlingGearPortrait* p;

    buf = allocateHeap(0x2000, 0);
    readRawSectorsFromDisk(getFileStartSector(6) + gear1 * 2, buf, 0x1000, 0, 0);
    readRawSectorsFromDisk(getFileStartSector(6) + gear2 * 2, buf + 0x1000, 0x1000, 0, 0);
    battlingSelectedGear1 = gear1;
    battlingSelectedGear2 = gear2;
    battlingPrevSelectedGear1 = gear1;
    battlingPrevSelectedGear2 = gear2;
    battlingGearSelectDone = 3;
    waitReadCompletion(0);
    p = &battlingPortraitSlots[gear1];
    LoadImage(&p->m0_clutRect, buf);
    LoadImage(&p->m8_imageRect, buf + 0x100);
    p = &battlingPortraitSlots[gear2];
    LoadImage(&p->m0_clutRect, buf + 0x1000);
    LoadImage(&p->m8_imageRect, buf + 0x1100);
    freeHeapDelayed(buf, 2);
}

void startBattlingGearSelect(s32 mode) {
    s32 i;
    BattlingGearPortrait* p;

    if (battlingPortraitsLoaded == 0) {
        waitReadCompletion(0);
        p = battlingPortraitSlots;
        for (i = 0; i < 49; i++) {
            LoadImage(&p->m0_clutRect, battlingPortraitFile + (i << 12));
            LoadImage(&p->m8_imageRect, battlingPortraitFile + (i << 12) + 0x100);
            p++;
        }
        freeHeap(battlingPortraitFile);
        battlingPortraitsLoaded = 1;
    }
    battlingGameMode = mode;
    if (mode == 4) {
        battlingMenus[5].mA_parent = 3;
    } else {
        battlingMenus[5].mA_parent = 4;
    }
    battlingMenus[6].mA_parent = 5;
    if (mode == 3) {
        battlingGear1MenuItems[0].m1_labelId = 0x27;
        battlingGear2MenuItems[0].m1_labelId = 0x28;
    } else {
        battlingGear1MenuItems[0].m1_labelId = 0x25;
        battlingGear2MenuItems[0].m1_labelId = 0x26;
    }
    setBattlingSceneMode(1);
    battlingGearSelectDone = 0;
    battlingGear2PortraitScroll = 0;
    battlingGear2ListScroll = 0;
    battlingGear1PortraitScroll = 0;
    battlingGear1ListScroll = 0;
    battlingPrevSelectedGear1 = battlingSelectedGear1;
    battlingPrevSelectedGear2 = battlingSelectedGear2;
    openBattlingMenu(5);
}

void requestBattlingStart(void) {
    battlingStartRequested = 1;
    hideBattlingHelpLabels();
}

void startBattlingTutorial(void) {
    battlingStartRequested = 1;
    initBattlingTutorial();
    loadBattlingGearFile(0, 0);
    loadBattlingGearFile(1, 1);
}

void openBattlingMenu(s32 menu) {
    if (menu == 0xFF) {
        battlingCurrentMenu = battlingPreviousMenu;
    } else {
        battlingPreviousMenu = battlingCurrentMenu;
        battlingCurrentMenu = &battlingMenus[menu];
    }
}

s32 isBattlingMainMenuOpen(void) {
    return battlingCurrentMenu == &battlingMenus[3];
}

void enterBattlingMainMenu(void) {
    stopAllSounds();
    battlingCurrentMenu = NULL;
    openBattlingMenu(3);
    battlingMenus[3].m12_selected = 0;
    battlingMenus[4].m12_selected = 0;
    battlingGameMode = 0;
    battlingPauseMenuShown = 0;
    hideBattlingHelpLabels();
    battlingStartRequested = 0;
    freeBattlingScreenCopy(0);
    battlingPortraitsLoaded = 0;
    battlingPortraitFile = loadBattlingFile(6);
}

void freeBattlingPortraitFile(void) {
    if (battlingPortraitsLoaded == 0) {
        waitReadCompletion(0);
        freeHeap(battlingPortraitFile);
        battlingPortraitsLoaded = 1;
    }
}

void freeBattlingScreenCopy(s32 arg0) {
    if (arg0 != 0) {
        battlingScreenCopy = NULL;
    }
    if (battlingScreenCopy != NULL) {
        freeHeap(battlingScreenCopy);
        battlingScreenCopy = NULL;
    }
}

void blurBattlingScreenCopy(void) {
    RECT rect;

    if (battlingScreenCopy != NULL) {
        DrawSync(0);
        rect.x = 0x140;
        rect.y = 0x100;
        rect.w = 0x140;
        rect.h = 0xDA;
        blurBattlingImage(battlingScreenCopy, battlingScreenCopy + 0x21E80);
        LoadImage(&rect, battlingScreenCopy);
        pcWideBattlingBlur((u16*)battlingScreenCopy); /* the capture's margins (wide.c) */
    }
}

void captureBattlingScreen(void) {
    RECT rect;

    if (battlingScreenCopy == NULL) {
        setHeapAllowFailure(1);
        battlingScreenCopy = allocateHeap(0x22100, 0);
        setHeapAllowFailure(0);
    }
    DrawSync(0);
    rect = battlingDisplayBuffers[(battlingBufferIndex + 1) & 1].m0_drawEnv.clip;
    MoveImage(&rect, 0x140, 0x100);
    pcWideBattlingCapture(); /* keeps the capture's margins for the blur */
    if (battlingScreenCopy != NULL) {
        StoreImage(&rect, (u32*)battlingScreenCopy);
    }
    DrawSync(0);
}

void openBattlingPauseMenu(s32 mode) {
    stopAllSounds();
    playBattlingSfx(0x1F);
    if (mode == 1) {
        battlingCurrentMenu = NULL;
        openBattlingMenu(0);
        battlingMenus[0].m12_selected = 0;
        battlingMenus[2].m12_selected = 1;
    } else if (mode == 2) {
        battlingCurrentMenu = NULL;
        openBattlingMenu(7);
        battlingMenus[7].m12_selected = 1;
    } else {
        goto other;
    }
    setBattlingSceneMode(0);
    battlingPauseMenuShown = 1;
    battlingScreenBlurCount = 0;
    battlingBlurScreenCopyPending = 1;
    captureBattlingScreen();
    return;
other:
    closeBattlingPauseMenu();
}

void clearBattlingGlyphs(void) {
    battlingGlyphCount = 0;
}

void drawBattlingTextOverlay(void* ot) {
    s32 i;
    POLY_FT4* prim;
    RECT rect;

    prim = battlingGlyphPolys[battlingBufferIndex];
    for (i = 0; i < battlingGlyphCount; i++, prim++) {
        AddPrim(ot, prim);
    }
    battlingGlyphCount = 0;
    drawBattlingHelpLabels(ot);
    if ((battlingCurrentMenu != NULL && battlingPauseMenuShown != 0) || battlingDrawScreenCopyOnce != 0) {
        if (battlingCurrentMenu != NULL) {
            AddPrim(ot, &battlingCurrentMenu->m1C_tiles[battlingBufferIndex]);
            SetDrawTPage(&battlingMenuTileTPages[battlingBufferIndex], 0, 0, GetTPage(0, 2, 0, 0));
            AddPrim(ot, &battlingMenuTileTPages[battlingBufferIndex]);
        }
        rect.x = 0x140;
        rect.y = 0x100;
        rect.w = 0x140;
        rect.h = 0xDA;
        SetDrawMove(&battlingScreenCopyMoves[battlingBufferIndex], &rect, battlingDisplayBuffers[battlingBufferIndex].m0_drawEnv.clip.x,
                    battlingDisplayBuffers[battlingBufferIndex].m0_drawEnv.clip.y);
        AddPrim(ot, &battlingScreenCopyMoves[battlingBufferIndex]);
    }
    battlingDrawScreenCopyOnce = 0;
}

void initBattlingHelpLabels(void) {
    battlingHelpLabels[0].m0_image = battlingHelpLabels[1].m0_image = allocateHeap(0x6B4, 0);
    *(u32*)&battlingHelpLabels[0].m4_sprt[0].x0 = 0xB40000;
    *(u16*)&battlingHelpLabels[0].m4_sprt[0].u0 = 0x3000;
    SetSprt(&battlingHelpLabels[0].m4_sprt[0]);
    SetShadeTex(&battlingHelpLabels[0].m4_sprt[0], 1);
    battlingHelpLabels[0].m4_sprt[0].h = 0xD;
    battlingHelpLabels[0].m4_sprt[0].clut = textSpriteMode0;
    battlingHelpLabels[0].m4_sprt[1] = battlingHelpLabels[0].m4_sprt[0];
    *(u32*)&battlingHelpLabels[1].m4_sprt[0].x0 = 0xC30000;
    *(u16*)&battlingHelpLabels[1].m4_sprt[0].u0 = 0x3000;
    SetSprt(&battlingHelpLabels[1].m4_sprt[0]);
    SetShadeTex(&battlingHelpLabels[1].m4_sprt[0], 1);
    battlingHelpLabels[1].m4_sprt[0].h = 0xD;
    battlingHelpLabels[1].m4_sprt[0].clut = textSpriteMode1;
    battlingHelpLabels[1].m4_sprt[1] = battlingHelpLabels[1].m4_sprt[0];
    SetDrawTPage(&battlingHelpLabelTPages[0], 0, 0, GetTPage(0, 0, 0x140, 0x30));
    battlingHelpLabelTPages[1] = battlingHelpLabelTPages[0];
}

/* Renders help-label text idx into the label image and centres it. */
void renderBattlingHelpLabel(BattlingLabel* label, s32 idx, s32 odd) {
    s32 w;

    w = renderString(getDialogParamPointer(battlingDialogTexts, idx), label->m0_image, 0x3F, odd);
    label->m2C_width = w;
    label->m2E_x = (0x140 - w) / 2;
}

/* Shows help label idx in slot which (0 top, 1 bottom) if it changed, and uploads the image. */
void showBattlingHelpLabel(s32 idx, s32 which) {
    RECT rect;

    if (which == 0) {
        if (idx == battlingTopHelpLabel) {
            return;
        }
        battlingTopHelpLabel = idx;
        renderBattlingHelpLabel(&battlingHelpLabels[0], idx, 0);
    } else {
        if (idx == battlingBottomHelpLabel) {
            return;
        }
        battlingBottomHelpLabel = idx;
        renderBattlingHelpLabel(&battlingHelpLabels[1], idx, 1);
    }
    rect.x = 0x140;
    rect.y = 0x30;
    rect.w = 0x42;
    rect.h = 0xD;
    LoadImage(&rect, battlingHelpLabels[0].m0_image);
}

/* Adds the visible help labels (and their draw-mode prim) to ot. */
void drawBattlingHelpLabels(OTag* ot) {
    BattlingLabel* label;

    if (battlingTopHelpLabel != 0) {
        label = &battlingHelpLabels;
        label->m4_sprt[battlingBufferIndex].w = label->m2C_width;
        label->m4_sprt[battlingBufferIndex].x0 = label->m2E_x;
        AddPrim(ot, &label->m4_sprt[battlingBufferIndex]);
    }
    if (battlingBottomHelpLabel != 0) {
        label = &battlingHelpLabels[1];
        label->m4_sprt[battlingBufferIndex].w = label->m2C_width;
        label->m4_sprt[battlingBufferIndex].x0 = label->m2E_x;
        AddPrim(ot, &label->m4_sprt[battlingBufferIndex]);
    }
    if (battlingTopHelpLabel | battlingBottomHelpLabel) {
        AddPrim(ot, &battlingHelpLabelTPages[battlingBufferIndex]);
    }
}

/* Lays out a menu: item widths, background tile, cursor reset. */
void layoutBattlingMenu(BattlingMenu* menu) {
    s32 i;
    s32 maxw;
    BattlingMenuItem* item;
    TILE* tile;

    maxw = 0;
    for (i = 0; i < menu->m8_count; i++) {
        item = &menu->m4_items[i];
        item->m12_halfWidth = getBattlingTextWidth(item->m4_text) / 2;
        maxw = (maxw < item->m12_halfWidth) ? item->m12_halfWidth : maxw;
    }
    tile = &menu->m1C_tiles[0];
    setlen(tile, 3);
    tile->w = maxw * 2 + 0x14;
    tile->x0 = 0x96 - maxw;
    menu->m12_selected = 0;
    *(u32*)&tile->r0 = 0x60102020;
    menu->m14_y = 0x6D - menu->m8_count * 10;
    tile->code |= 2;
    tile->h = menu->m8_count * 0x14 + 0x14;
    tile->y0 = menu->m14_y - 10;
    for (i = 0; i < menu->m8_count; i++) {
        item = &menu->m4_items[i];
        if (item->m0_flags & 2) {
            item->m12_halfWidth = maxw;
        }
    }
    if (menu->m0_leftMargin != 0) {
        tile->w += menu->m0_leftMargin;
        tile->x0 -= menu->m0_leftMargin >> 1;
        menu->m16_x = 0xA0 - (menu->m0_leftMargin >> 1) - maxw;
    }
    menu->m1C_tiles[1] = menu->m1C_tiles[0];
    setBattlingTextBlinkYellow(0);
}

/* Lays out the eight menus and resets the menu state. */
void initBattlingMenus(void) {
    u32 i;

    for (i = 0; i < 8; i++) {
        layoutBattlingMenu(&battlingMenus[i]);
    }
    battlingCurrentMenu = NULL;
    battlingSelectedGear1 = 0;
    battlingSelectedGear2 = 1;
    initBattlingHelpLabels();
}

/* Makes menu current and prints its items (then its draw callback). */
void printBattlingMenu(BattlingMenu* menu) {
    s32 i;

    if (menu != NULL) {
        battlingCurrentMenu = menu;
        if (menu != &battlingMenus[5]) {
            if (menu->m0_leftMargin != 0) {
                setBattlingTextPos(menu->m16_x, menu->m14_y);
            } else {
                setBattlingTextPos(0xA0, menu->m14_y);
            }
            for (i = 0; i < menu->m8_count; i++) {
                highlightBattlingMenuItem(menu, i);
                if (menu->m0_leftMargin != 0) {
                    printBattlingText(menu->m4_items[i].m4_text);
                } else {
                    printBattlingTextOffset(menu->m4_items[i].m4_text, menu->m4_items[i].m12_halfWidth);
                }
            }
            if (menu->mC_draw != NULL) {
                menu->mC_draw(menu);
            }
        }
    }
}

/* Menu input for pad: help label, analog-stick sound, item callback, back, up/down with wrap and skipped items. */
s32 updateBattlingMenuInput(BattlingMenu* menu, s32 pad) {
    BattlingMenuItem* item;
    s32 type;
    s32 dx;
    s32 dy;
    void (*cb)(s32);

    item = &menu->m4_items[menu->m12_selected];
    if (pad == 0 || menu == &battlingMenus[7]) {
        showBattlingHelpLabel(battlingNextHelpLabel, 0);
        battlingNextHelpLabel = menu->m4_items[menu->m12_selected].m1_labelId;
    }
    battlingMenuPad = pad;
    type = 0;
    if (pad == 1) {
        battlingMenuPadHeld = newPadButtonForScripts2;
        battlingMenuPadRepeat = newPadButtonForField2;
        battlingMenuPadPressed = newPadButtonForDialogs2;
        type = isControllerConnected(1);
        dx = pad1LeftStickY - 0x80;
        dy = pad1LeftStickX - 0x80;
    } else if (pad == 0) {
        battlingMenuPadHeld = newPadButtonForScripts;
        battlingMenuPadRepeat = newPadButtonForField;
        battlingMenuPadPressed = newPadButtonForDialogs;
        type = isControllerConnected(0);
        dx = pad0LeftStickY - 0x80;
        dy = pad0LeftStickX - 0x80;
    }
    if ((type == 3 || type == 4) && SquareRoot0(dx * dx + dy * dy) > 0x40) {
        if (battlingMenuStickWarned == 0) {
            playBattlingSfx(0x24);
            battlingMenuStickWarned = 1;
        }
    } else {
        battlingMenuStickWarned = 0;
    }
    cb = item->m8_callback;
    if (cb != NULL) {
        if (item->m0_flags & 1) {
            cb(item->mC_arg);
        } else if (battlingMenuPadPressed & 0x20) {
            playBattlingSfx(0x21);
            cb(item->mC_arg);
        }
    }
    if (battlingCurrentMenu != NULL) {
        if ((battlingMenuPadPressed & 0x40) && (pad != 1 || menu != &battlingMenus[6])) {
            if (battlingCurrentMenu == &battlingMenus[menu->mA_parent] && !(menu->mA_parent == 5 || menu->mA_parent == 6)) {
                playBattlingSfx(0x24);
            } else {
                openBattlingMenu(menu->mA_parent);
                playBattlingSfx(0x22);
            }
        }
        if (battlingMenuPadRepeat & 0x1000) {
            playBattlingSfx(0x1E);
            if (--menu->m12_selected < 0) {
                menu->m12_selected = menu->m8_count - 1;
            }
            if (menu->m4_items[menu->m12_selected].m0_flags & 4) {
                menu->m12_selected--;
            }
        }
        if (battlingMenuPadRepeat & 0x4000) {
            playBattlingSfx(0x1E);
            menu->m12_selected++;
            if (menu->m4_items[menu->m12_selected].m0_flags & 4) {
                menu->m12_selected++;
            }
        }
        if (menu->m12_selected < 0) {
            menu->m12_selected = menu->m8_count - 1;
        }
        if (menu->m12_selected >= menu->m8_count) {
            menu->m12_selected = 0;
        }
        if (menu->m4_items[menu->m12_selected].m0_flags & 4) {
            menu->m12_selected--;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

INCLUDE_RODATA("battling/nonmatchings/bl_8007E528", battlingMenuStrings);

/* Runs the menus of the current game mode (battlingGameMode 1..5). */
void updateBattlingGearSelect(void) {
    switch (battlingGameMode) {
    case 1:
        switch (battlingGearSelectDone) {
        case 0:
            battlingGear2SelectActive = 0;
            updateBattlingMenuInput(&battlingMenus[5], 0);
            break;
        case 1:
            battlingGear2SelectActive = 1;
            updateBattlingMenuInput(&battlingMenus[6], 0);
            break;
        case 3:
            battlingStartRequested = 1;
            break;
        }
        battlingSettings.m5_p1Com = 0;
        battlingSettings.m6_p2Com = 1;
        break;
    case 2:
        battlingGear2SelectActive = 1;
        if (battlingGearSelectDone & 1) {
            battlingMenus[5].mA_parent = 5;
        } else {
            battlingMenus[5].mA_parent = 4;
        }
        if (battlingGearSelectDone & 2) {
            battlingMenus[6].mA_parent = 5;
        } else {
            battlingMenus[6].mA_parent = 4;
        }
        updateBattlingMenuInput(&battlingMenus[5], 0);
        updateBattlingMenuInput(&battlingMenus[6], 1);
        if (battlingGearSelectDone == 3) {
            battlingStartRequested = 1;
        }
        battlingSettings.m5_p1Com = 0;
        battlingSettings.m6_p2Com = 0;
        break;
    case 3:
        switch (battlingGearSelectDone) {
        case 0:
            battlingGear2SelectActive = 0;
            updateBattlingMenuInput(&battlingMenus[5], 0);
            break;
        case 1:
            battlingGear2SelectActive = 1;
            updateBattlingMenuInput(&battlingMenus[6], 0);
            break;
        case 3:
            battlingStartRequested = 1;
            break;
        }
        battlingSettings.m5_p1Com = 1;
        battlingSettings.m6_p2Com = 1;
        break;
    case 4:
        switch (battlingGearSelectDone) {
        case 0:
            battlingGear2SelectActive = 0;
            updateBattlingMenuInput(&battlingMenus[5], 0);
            break;
        case 1:
            battlingGear2SelectActive = 1;
            updateBattlingMenuInput(&battlingMenus[6], 0);
            break;
        case 3:
            battlingStartRequested = 1;
            break;
        }
        battlingSettings.m5_p1Com = 0;
        battlingSettings.m6_p2Com = 1;
        break;
    case 5:
        switch (battlingGearSelectDone) {
        case 0:
            updateBattlingMenuInput(&battlingMenus[5], 0);
            break;
        case 1:
            if (battlingSelectedGear2 == 3) {
                battlingGearSelectDone = 3;
            } else {
                battlingSelectedGear2++;
                if (battlingSelectedGear2 > battlingGearListCount) {
                    battlingSelectedGear2 -= battlingGearListCount;
                }
                if (battlingSelectedGear2 < 0) {
                    battlingSelectedGear2 += battlingGearListCount;
                }
            }
            break;
        case 3:
            battlingStartRequested = 1;
            break;
        }
        battlingSettings.m5_p1Com = 0;
        battlingSettings.m6_p2Com = 1;
        break;
    }
    drawBattlingGearSelectMenu();
}

/* Per-frame menu update: sound, pad, current menu input and print. */
void updateBattlingMenus(void) {
    BattlingMenu* menu;
    s32 unused[2];

    if (battlingBlurScreenCopyPending != 0) {
        blurBattlingScreenCopy();
        battlingBlurScreenCopyPending = 0;
    }
    mergeQueuedInputs();
    menu = battlingCurrentMenu;
    if (menu == &battlingMenus[5]) {
        updateBattlingGearSelect();
        return;
    }
    if (menu != NULL) {
        if ((newPadButtonForDialogs & 1) && battlingScreenBlurCount < 5) {
            battlingScreenBlurCount++;
            blurBattlingScreenCopy();
        }
        updateBattlingMenuInput(menu, battlingActivePad);
    }
    printBattlingMenu(battlingCurrentMenu);
}

/* Letterbox the screen clear tiles (grey band from y 0x60). */
void setBattlingArenaClearTiles(void) {
    battlingDisplayBuffers[0].mE8_clearTile.y0 = 0x60;
    battlingDisplayBuffers[1].mE8_clearTile.y0 = 0x60;
    setRGB0(&battlingDisplayBuffers[0].mE8_clearTile, 0x7F, 0x7F, 0x7F);
    setRGB0(&battlingDisplayBuffers[1].mE8_clearTile, 0x7F, 0x7F, 0x7F);
    battlingDisplayBuffers[0].mE8_clearTile.h = battlingScreenHeight - 0x60;
    battlingDisplayBuffers[1].mE8_clearTile.h = battlingScreenHeight - 0x60;
}

/* Full-screen black clear tiles. */
void setBattlingBlackClearTiles(void) {
    battlingDisplayBuffers[0].mE8_clearTile.y0 = 0;
    battlingDisplayBuffers[1].mE8_clearTile.y0 = 0;
    setRGB0(&battlingDisplayBuffers[0].mE8_clearTile, 0, 0, 0);
    setRGB0(&battlingDisplayBuffers[1].mE8_clearTile, 0, 0, 0);
    battlingDisplayBuffers[0].mE8_clearTile.h = battlingScreenHeight;
    battlingDisplayBuffers[1].mE8_clearTile.h = battlingScreenHeight;
}

/* battlingBackdropGradient[0] as packed words */
#define G4 ((BattlingPolyG4Words*)battlingBackdropGradient)
void initBattlingArena(void) {
    s32 i;
    BattlingMapCell* cell;

    initBattlingHud();
    setlen(&battlingBackdropGradient[0], 8);
    setcode(&battlingBackdropGradient[0], 0x38);
    G4->r0 = 0x10;
    G4->g0 = 0x60;
    G4->b0 = 0x7F;
    G4->rg1 = 0x6010;
    G4->b1 = 0x7F;
    G4->rg2 = 0x7F7F;
    G4->b2 = 0x7F;
    G4->rg3 = 0x7F7F;
    G4->b3 = 0x7F;
    G4->xy0 = 0;
    G4->xy1 = 0x140;
    G4->xy2 = 0x600000;
    G4->xy3 = 0x600140;
    battlingBackdropGradient[1] = battlingBackdropGradient[0];
    SetDrawTPage(&battlingDemoBlurTPages[0], 0, 0, GetTPage(2, 2, 0, 0x100));
    SetDrawTPage(&battlingDemoBlurTPages[1], 0, 0, GetTPage(2, 2, 0, 0));
    SetDrawTPage(&battlingDemoBlurTPages[2], 0, 0, GetTPage(2, 2, 0x100, 0x100));
    SetDrawTPage(&battlingDemoBlurTPages[3], 0, 0, GetTPage(2, 2, 0x100, 0));
    ((BattlingSprtWords*)battlingDemoBlurSprites)->rgbc = 0x64707070;
    setlen(&battlingDemoBlurSprites[0], 4);
    ((BattlingSprtWords*)battlingDemoBlurSprites)->xy0 = 0;
    ((BattlingSprtWords*)battlingDemoBlurSprites)->uv0 = 0;
    ((BattlingSprtWords*)battlingDemoBlurSprites)->wh = 0xDB0080;
    battlingDemoBlurSprites[0].code = (battlingDemoBlurSprites[0].code & 0xFE) | 2;
    copyBattlingWords(&battlingDemoBlurSprites[1], &battlingDemoBlurSprites[0], 5 * (s32)sizeof(SPRT));
    battlingDemoBlurSprites[2].u0 = battlingDemoBlurSprites[3].u0 = 0x80;
    battlingDemoBlurSprites[2].x0 = battlingDemoBlurSprites[3].x0 = 0x80;
    battlingDemoBlurSprites[4].x0 = battlingDemoBlurSprites[5].x0 = 0x100;
    battlingDemoBlurSprites[4].w = battlingDemoBlurSprites[5].w = 0x40;
    cell = battlingHeightMap;
    for (i = 0; i < 0x4000; i++) {
        cell->m0_height *= 12;
        cell++;
    }
    initBattlingGroundPrims();
}
#undef G4

/* Draws a map marker (a 6-segment arc plus two spokes, lines via
 * addBattlingGroundSpanEdge) at (x >> 8, z >> 8) facing angle. */
void setBattlingGroundView(s32 x, s32 z, s32 angle) {
    s32 sx;
    s32 sz;
    s32 px;
    s32 pz;
    s32 nx;
    s32 nz;
    s32 i;
    s32 a;

    pcWideBattlingGroundCone(x, z, angle, 0); /* the margins' floor (wide.c) */
    x >>= 8;
    z >>= 8;
    sx = x + ((getAngleCos(angle + 0x280) * 10) >> 12);
    px = sx;
    sz = z + ((getAngleSin(angle + 0x280) * 10) >> 12);
    pz = sz;
    a = angle + 0x580;
    for (i = 0; i < 6; i++) {
        nx = x + ((getAngleCos(a) * 24) >> 12);
        nz = z + ((getAngleSin(a) * 24) >> 12);
        addBattlingGroundSpanEdge(px, pz, nx, nz);
        px = nx;
        pz = nz;
        a += 0x100;
    }
    nx = x + ((getAngleCos(angle - 0x280) * 10) >> 12);
    nz = z + ((getAngleSin(angle - 0x280) * 10) >> 12);
    addBattlingGroundSpanEdge(px, pz, nx, nz);
    addBattlingGroundSpanEdge(sx, sz, nx, nz);
}

/* Draws a map marker (a 3-segment arc plus two spokes) at (x >> 8, z >> 8)
 * facing angle. */
void setBattlingGroundViewNarrow(s32 x, s32 z, s32 angle) {
    s32 sx;
    s32 sz;
    s32 px;
    s32 pz;
    s32 nx;
    s32 nz;
    s32 i;
    s32 a;

    pcWideBattlingGroundCone(x, z, angle, 1);
    x >>= 8;
    z >>= 8;
    sx = x + ((getAngleCos(angle + 0x100) * 16) >> 12);
    px = sx;
    sz = z + ((getAngleSin(angle + 0x100) * 16) >> 12);
    pz = sz;
    a = angle + 0x78A;
    for (i = 0; i < 3; i++) {
        nx = x + ((getAngleCos(a) * 32) >> 12);
        nz = z + ((getAngleSin(a) * 32) >> 12);
        addBattlingGroundSpanEdge(px, pz, nx, nz);
        px = nx;
        pz = nz;
        a += 0x75;
    }
    nx = x + ((getAngleCos(angle - 0x100) * 16) >> 12);
    nz = z + ((getAngleSin(angle - 0x100) * 16) >> 12);
    addBattlingGroundSpanEdge(px, pz, nx, nz);
    addBattlingGroundSpanEdge(sx, sz, nx, nz);
}

/* Copies the last ground normal battlingGroundNormal. */
void getBattlingGroundNormal(SVECTOR* out) {
    *out = battlingGroundNormal;
}

/* Height of the arena floor under pos (the plane through the map cell's
 * triangle); the plane normal is left in battlingGroundNormal. flag: raise cells of
 * type 1/3 (bits 24-25 of the cell word) by 0x100/0x40. */
s32 getBattlingGroundHeight(VECTOR* pos, s32 flag) {
    SVECTOR unused0; /* frame layout */
    SVECTOR v[3];
    s32 tx;
    s32 ty;
    s32 tz;

    {
        SVECTOR v3;
        SVECTOR unused1; /* frame layout */
        SVECTOR tri[3];
        SVECTOR unused2; /* frame layout */
        BattlingMapCell* cell;
        s32 x;
        s32 z;
        s32 x0;
        s32 z0;

        x = pos->vx;
        z = pos->vz;
        x0 = x & ~0xFF;
        x >>= 8;
        z0 = z & ~0xFF;
        z >>= 8;
        cell = (BattlingMapCell*)((z * 128 + x) * 4 + (sptr)battlingHeightMap);
        v[0].vx = x0;
        v[0].vy = cell[0].m0_height;
        v[0].vz = z0;
        v[1].vx = x0 + 0x100;
        v[1].vy = cell[129].m0_height;
        v[1].vz = z0 + 0x100;
        v[2].vx = x0 + 0x100;
        v[2].vy = cell[1].m0_height;
        v[2].vz = z0;
        v3.vx = x0;
        v3.vy = cell[128].m0_height;
        v3.vz = z0 + 0x100;
        if (flag != 0) {
            switch (*(s32*)&battlingHeightMap[z * 128 + x] & 0x03000000) {
            case 0x01000000:
                v[0].vy += 0xC0;
            case 0x03000000:
                v[0].vy += 0x40;
            }
            switch (*(s32*)&battlingHeightMap[z * 128 + x + 129] & 0x03000000) {
            case 0x01000000:
                v[1].vy += 0xC0;
            case 0x03000000:
                v[1].vy += 0x40;
            }
            switch (*(s32*)&battlingHeightMap[z * 128 + x + 1] & 0x03000000) {
            case 0x01000000:
                v[2].vy += 0xC0;
            case 0x03000000:
                v[2].vy += 0x40;
            }
            switch (*(s32*)&battlingHeightMap[z * 128 + x + 128] & 0x03000000) {
            case 0x01000000:
                v3.vy += 0xC0;
            case 0x03000000:
                v3.vy += 0x40;
            }
        }
        if ((v[0].vz - v[1].vz) * pos->vx + (v[1].vx - v[0].vx) * pos->vz + v[0].vx * v[1].vz - v[1].vx * v[0].vz < 0) {
            tri[0] = v[0];
            tri[1] = v[1];
            tri[2] = v[2];
        } else {
            tri[0] = v[0];
            tri[1] = v3;
            tri[2] = v[1];
        }
        computeFaceNormal(&tri[0], &tri[1], &tri[2], &battlingGroundNormal);
        tx = tri[0].vx;
        ty = tri[0].vy;
        tz = tri[0].vz;
    }
    {
        VECTOR p;

        p.vx = tx;
        p.vy = ty;
        p.vz = tz;
        return pos->vy + R3000_DIV((p.vx * battlingGroundNormal.vx + p.vy * battlingGroundNormal.vy + p.vz * battlingGroundNormal.vz)
                          - (pos->vx * battlingGroundNormal.vx + pos->vy * battlingGroundNormal.vy + pos->vz * battlingGroundNormal.vz), battlingGroundNormal.vy);
    }
}

/* Height map cell (s16) under an SVECTOR position. */
s16 getBattlingMapHeight(SVECTOR* pos) {
    MATRIX unused1;
    VECTOR unused2;
    s16 x;
    s16 z;

    x = pos->vx >> 8;
    z = pos->vz >> 8;
    return battlingHeightMap[x + z * 128].m0_height;
}

/* Height map cell word under a VECTOR position. */
u32 getBattlingMapCell(VECTOR* pos) {
    s32 x;
    s32 z;

    x = pos->vx >> 8;
    z = pos->vz >> 8;
    return *(u32*)&battlingHeightMap[z * 128 + x];
}

/* Keeps pos + move inside the arena circle of the given radius (slides along the wall). */
void clampBattlingMoveToArena(VECTOR* pos, VECTOR* move, s32 radius) {
    VECTOR v;
    VECTOR v2;
    VECTOR sq;
    MATRIX m;
    MATRIX mt;
    SVECTOR n;
    s32 d;

    v.vx = pos->vx + move->vx - 0x3F80;
    v.vz = pos->vz + move->vz - 0x3F80;
    /* v.vy is never set, yet VectorNormalS normalises all three components, so
     * the wall direction n depends on it. Retail's slot mostly holds
     * renderBattlingNodeTree's saved $s1, a node pointer (PCSX-Redux, battling
     * demo at the arena wall: US 0x8015ED4C, JP 0x8015D46C in 95 of 107 calls;
     * 10 a word runBattlingGearEffectCommand's frame left, 2 others). With it
     * the camera follows Redux's exactly through the first 55 calls (was 1-2),
     * until a gear effect leaves the other word there. */
#if VERSION_JP
    v.vy = (s32)0x8015D46C;
#else
    v.vy = (s32)0x8015ED4C;
#endif
    Square0(&v, &sq);
    if (radius < SquareRoot0(sq.vx + sq.vz)) {
        VectorNormalS(&v, &n);
        m.m[0][1] = m.m[1][0] = m.m[1][2] = m.m[2][1] = 0;
        m.m[1][1] = 0x1000;
        m.m[0][0] = m.m[2][2] = n.vz;
        m.m[0][2] = -n.vx;
        m.m[2][0] = n.vx;
        ApplyMatrixLV(&m, move, &v);
        TransposeMatrix(&m, &mt);
        SetRotMatrix(&mt);
        v.vz = 0;
        while (1) {
            ApplyRotMatrixLV(&v, move);
            v2.vx = pos->vx + move->vx - 0x3F80;
            v2.vz = pos->vz + move->vz - 0x3F80;
            Square0(&v2, &sq);
            d = SquareRoot0(sq.vx + sq.vz);
            if (radius >= d) {
                break;
            }
            v.vz += 8 - (d - radius);
        }
    }
}

/* Applies stage colour set battlingStageColorIndex: backdrop gradient, fog/far colour, back colour. */
void applyBattlingStageColors(void) {
    BattlingStageColors* c;
    u8 r;
    u8 g;
    u8 b;
    u8 fr;
    u8 fg;
    u8 fb;

    c = &battlingStageColorSets[battlingStageColorIndex];
    battlingStageColors = c;
    r = c->m0_topR;
    g = c->m1_topG;
    b = c->m2_topB;
    battlingBackColorR = c->m4_backR;
    battlingBackColorG = c->m5_backG;
    battlingBackColorB = c->m6_backB;
    fr = c->m8_farR;
    fg = c->m9_farG;
    fb = c->mA_farB;
    setFogNearColor(c->mC_r, c->mD_g, c->mE_b);
    SetFarColor(fr, fg, fb);
    battlingBackdropGradient[0].g0 = g;
    battlingBackdropGradient[1].g0 = g;
    battlingBackdropGradient[0].r0 = r;
    battlingBackdropGradient[1].r0 = r;
    battlingBackdropGradient[0].b0 = b;
    battlingBackdropGradient[1].b0 = b;
    *(u16*)&battlingBackdropGradient[0].r1 = r | (g << 8);
    battlingBackdropGradient[0].b1 = b;
    *(u16*)&battlingBackdropGradient[1].r1 = r | (g << 8);
    battlingBackdropGradient[1].b1 = b;
    *(u16*)&battlingBackdropGradient[0].r2 = fr | (fg << 8);
    battlingBackdropGradient[0].b2 = fb;
    *(u16*)&battlingBackdropGradient[1].r2 = fr | (fg << 8);
    battlingBackdropGradient[1].b2 = fb;
    *(u16*)&battlingBackdropGradient[0].r3 = fr | (fg << 8);
    battlingBackdropGradient[0].b3 = fb;
    *(u16*)&battlingBackdropGradient[1].r3 = fr | (fg << 8);
    battlingBackdropGradient[1].b3 = fb;
    setRGB0(&battlingDisplayBuffers[0].mE8_clearTile, fr, fg, fb);
    setRGB0(&battlingDisplayBuffers[1].mE8_clearTile, fr, fg, fb);
    SetFogNearFar(0x800, 0x1800, 0xC0);
    fogNearColor.cd = 0x28;
    gte_ldrgb(&fogNearColor);
}

/* Loads the ring-wall texture (TIM 27 of the bundle) and builds the 64 wall polys (two buffers). */
void initBattlingRingWall(u32** bundle) {
    TIM_IMAGE tim;
    s32 i;
    s16* clut;
    POLY_FT4* p;

    OpenTIM(bundle[27]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    for (i = 0; i < 256; i++) {
        *clut++ |= 0x8000;
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingRingWallClut = GetClut(tim.crect->x, tim.crect->y);
    battlingRingWallTPage = GetTPage(1, 0, tim.prect->x, tim.prect->y);
    battlingRingWallV = (u8)tim.prect->y;
    battlingRingWallPolys[0] = allocateHeap(64 * (s32)sizeof(POLY_FT4), 0);
    battlingRingWallPolys[1] = allocateHeap(64 * (s32)sizeof(POLY_FT4), 0);
    p = battlingRingWallPolys[0];
    for (i = 0; i < 64; i += 2) {
        setlen(p, 9);
        setcode(p, 0x2C);
        setlen(p + 1, 9);
        setcode(p + 1, 0x2C);
        p->clut = battlingRingWallClut;
        p->tpage = battlingRingWallTPage;
        p->u0 = 0x7F;
        p->v0 = battlingRingWallV + 0x3F;
        p->u1 = 0x7F;
        p->v1 = battlingRingWallV;
        p->u2 = 0x3F;
        p->v2 = battlingRingWallV + 0x3F;
        p->u3 = 0x3F;
        p->v3 = battlingRingWallV;
        p++;
        p->clut = battlingRingWallClut;
        p->tpage = battlingRingWallTPage;
        p->u0 = 0x3F;
        p->v0 = battlingRingWallV + 0x3F;
        p->u1 = 0x3F;
        p->v1 = battlingRingWallV;
        p->u2 = 0;
        p->v2 = battlingRingWallV + 0x3F;
        p->u3 = 0;
        p->v3 = battlingRingWallV;
        p++;
    }
    copyBattlingWords(battlingRingWallPolys[1], battlingRingWallPolys[0], 64 * (s32)sizeof(POLY_FT4));
}

/* Draws the arena's ring wall: 32 segments of two POLY_FT4 (from the
 * current buffer of battlingRingWallPolys) around the centre, starting from the angle
 * that faces pos (the camera), depth-cued with the colour set by
 * applyBattlingStageColors. */
void drawBattlingRingWall(OTag* ot, VECTOR* pos) {
    VECTOR v;
    SVECTOR sv[6];
    s32 z[4];
    s32 maxz;
    s32 angle;
    POLY_FT4* p;
    POLY_FT4* q;
    s32 i;

    v = *pos;
    i = 0;
    p = battlingRingWallPolys[battlingBufferIndex];
    v.vx -= 0x3F80;
    v.vz -= 0x3F80;
    angle = ratan2(v.vx, v.vz) & 0xFFF0;
    angle -= 0x100;
    sv[0].vy = sv[1].vy = 0;
    sv[2].vy = sv[3].vy = -0x290;
    sv[4].vy = sv[5].vy = -0x520;
    sv[0].vx = ((getAngleCos(angle) * 0x3F80) >> 12) - (s16)(battlingViewOrigin.vx - 0x3F80);
    sv[0].vz = ((getAngleSin(angle) * 0x3F80) >> 12) - (s16)(battlingViewOrigin.vz - 0x3F80);
    angle += 0x10;
    do {
        sv[2].vx = sv[0].vx;
        sv[4].vx = sv[0].vx;
        sv[2].vz = sv[0].vz;
        sv[4].vz = sv[0].vz;
        sv[5].vx = sv[3].vx = sv[1].vx = ((getAngleCos(angle) * 0x3F80) >> 12) - (s16)(battlingViewOrigin.vx - 0x3F80);
        sv[5].vz = sv[3].vz = sv[1].vz = ((getAngleSin(angle) * 0x3F80) >> 12) - (s16)(battlingViewOrigin.vz - 0x3F80);
        gte_ldv3(&sv[0], &sv[1], &sv[2]);
        gte_rtpt();
        gte_dpcs();
        gte_stsxy3(&p[0].x0, &p[0].x1, &p[0].x2);
        gte_stsz3(&z[0], &z[1], &z[2]);
        gte_ldv3(&sv[3], &sv[4], &sv[5]);
        gte_rtpt();
        q = p + 1;
        maxz = z[0];
        if (maxz < z[1]) {
            maxz = z[1];
        }
        if (maxz <= z[2]) {
            maxz = z[2];
        }
        *(u32*)&p[1].x0 = *(u32*)&p[0].x2;
        gte_stsxy(&p[0].x3);
        gte_stsxy3(&p[0].x3, &p[1].x2, &p[1].x3);
        gte_stsz(&z[3]);
        *(u32*)&p[1].x1 = *(u32*)&p[0].x3;
        if (maxz <= z[3]) {
            maxz = z[3];
        }
        if (maxz < 0x1C00) {
            maxz >>= 4;
            gte_strgb(&p[0].r0);
            gte_strgb(&p[1].r0);
            setlen(&p[0], 9);
            setcode(&p[0], 0x2C);
            setlen(&p[1], 9);
            setcode(&p[1], 0x2C);
            AddPrim(ot + maxz, &p[0]);
            AddPrim(ot + maxz, q);
        }
        p += 2;
        angle += 0x10;
        i++;
        sv[0].vx = sv[1].vx;
        sv[0].vz = sv[1].vz;
    } while (i < 32);
}

/* Starts the intro camera: random position around the arena. */
void startBattlingIntroCamera(void) {
    s32 speed;
    s32 angle;

    speed = (rand() & 0x1FFF) + 0x800;
    angle = rand() % 1536 + 0x500;
    battlingCameraPos.vx = ((getAngleCos(angle) * speed) >> 12) + 0x4000;
    battlingCameraPos.vz = ((getAngleSin(angle) * speed) >> 12) + 0x4000;
    battlingCameraPos.vy = -((rand() & 0x7FF) + 0x400);
    battlingCameraDistance = 0x100;
    battlingCameraHeight = 0x40;
    battlingCameraLag = 0x40;
    battlingCameraSideAngle = 0x400;
}

/* Left/right (0x8000/0x2000) turn the orbit camera angle battlingOrbitCameraAngle. */
void turnBattlingOrbitCamera(s32 buttons) {
    if (buttons & 0x8000) {
        battlingOrbitCameraAngle += 0x20;
    }
    if (buttons & 0x2000) {
        battlingOrbitCameraAngle -= 0x20;
    }
}

void updateBattlingOrbitCamera(s32 smooth) {
    VECTOR a;
    VECTOR b;
    VECTOR c;
    SVECTOR unused[3];
    BattlingGear* g1;
    BattlingGear* g2;
    s32 t;

    if (battlingRoundWinner != 0) {
        g1 = &battlingGear2;
        g2 = &battlingGear1;
    } else {
        g1 = &battlingGear1;
        g2 = &battlingGear2;
    }
    ratan2(g1->m0_position.vx - g2->m0_position.vx, g1->m0_position.vz - g2->m0_position.vz);
    if (battlingResultTimer > 0xB0) {
        t = 0x100;
    } else if (battlingResultTimer > 0xA0) {
        t = (battlingResultTimer - 0xA0) * 16;
    } else {
        t = 0;
    }
    c.vx = g2->m0_position.vx;
    c.vy = g2->m0_position.vy;
    c.vz = g2->m0_position.vz;
    c.vx -= g1->m0_position.vx;
    c.vy -= g1->m0_position.vy;
    c.vz -= g1->m0_position.vz;
    c.vx *= t;
    c.vy *= t;
    c.vz *= t;
    c.vx /= 256;
    c.vy /= 256;
    c.vz /= 256;
    c.vx += g1->m0_position.vx;
    c.vy += g1->m0_position.vy;
    c.vz += g1->m0_position.vz;
    c.vy -= 0xA0;
    c.vx -= battlingCameraTarget.vx;
    c.vy -= battlingCameraTarget.vy;
    c.vz -= battlingCameraTarget.vz;
    if (smooth != 0) {
        c.vx /= 3;
        c.vy /= 3;
        c.vz /= 3;
    }
    battlingCameraDistance = 0xC00;
    battlingCameraTarget.vx += c.vx;
    battlingCameraTarget.vy += c.vy;
    battlingCameraTarget.vz += c.vz;
    t = battlingOrbitCameraAngle + battlingResultTimer * battlingOrbitCameraSpeed;
    a.vx = (getAngleCos(t) * battlingCameraDistance) >> 12;
    a.vz = (getAngleSin(t) * battlingCameraDistance) >> 12;
    a.vy = -(battlingResultTimer * 6 + 0x200);
    a.vx += battlingCameraTarget.vx;
    a.vy += battlingCameraTarget.vy;
    a.vz += battlingCameraTarget.vz;
    b.vx = a.vx - battlingCameraPos.vx;
    b.vz = a.vz - battlingCameraPos.vz;
    clampBattlingMoveToArena(&battlingCameraPos, &b, 0x3D00);
    battlingCameraPos.vx += b.vx;
    battlingCameraPos.vz += b.vz;
    t = getBattlingGroundHeight(&battlingCameraPos, 0);
    if (t < a.vy) {
        a.vy = t;
    }
    battlingCameraPos.vy = a.vy;
}

/* Starts the victory camera orbit with a random angle and speed. */
void startBattlingOrbitCamera(void) {
    battlingOrbitCameraAngle = rand();
    battlingOrbitCameraSpeed = rand() % 12 + 4;
    if (rand() & 1) {
        battlingOrbitCameraSpeed = -battlingOrbitCameraSpeed;
    }
    updateBattlingOrbitCamera(0);
}

void updateBattlingScriptFightCamera(VECTOR* g1, VECTOR* g2) {
    VECTOR a;
    VECTOR b;
    MATRIX unused;
    s32 angle;
    s32 dist;
    s32 h;
    s32 dist3;

    angle = ratan2(g1->vx - g2->vx, g1->vz - g2->vz);
    dist = getBattlingDistance(g1, g2);
    dist3 = dist * 2 / 3;
    battlingCameraTarget.vx = (g1->vx + g2->vx) / 2;
    battlingCameraTarget.vy = (g1->vy + g2->vy) / 2 - 0xA0;
    battlingCameraTarget.vz = (g1->vz + g2->vz) / 2;
    battlingCameraDistance = dist3 + 0xC0;
    a.vx = battlingCameraTarget.vx + ((getAngleCos(angle - 0x400) * battlingCameraDistance) >> 12);
    a.vz = battlingCameraTarget.vz + ((getAngleSin(angle - 0x400) * battlingCameraDistance) >> 12);
    b.vx = battlingCameraTarget.vx + ((getAngleCos(angle + 0x400) * battlingCameraDistance) >> 12);
    b.vz = battlingCameraTarget.vz + ((getAngleSin(angle + 0x400) * battlingCameraDistance) >> 12);
    a.vx -= battlingCameraPos.vx;
    a.vy -= battlingCameraPos.vy;
    a.vz -= battlingCameraPos.vz;
    b.vx -= battlingCameraPos.vx;
    b.vy -= battlingCameraPos.vy;
    b.vz -= battlingCameraPos.vz;
    h = getBattlingVectorLengthXZ(&a);
    if (h > getBattlingVectorLengthXZ(&b)) {
        battlingCameraSideAngle = 0x400;
        battlingCameraSide = 0;
    } else {
        battlingCameraSideAngle = -0x400;
        battlingCameraSide = 1;
    }
    dist /= 4;
    if (dist > 0x300) {
        dist = 0x300;
    }
    a.vy = battlingCameraTarget.vy - battlingCameraHeight - dist;
    a.vx = battlingCameraTarget.vx + ((getAngleCos(angle + battlingCameraSideAngle) * battlingCameraDistance) >> 12);
    a.vz = battlingCameraTarget.vz + ((getAngleSin(angle + battlingCameraSideAngle) * battlingCameraDistance) >> 12);
    h = getBattlingGroundHeight(&a, 0) - 0x100;
    if (h < a.vy) {
        a.vy = h;
    }
    battlingCameraAngle = angle;
    a.vx = (a.vx - battlingCameraPos.vx) / battlingCameraLag;
    a.vy = (a.vy - battlingCameraPos.vy) / battlingCameraLag;
    a.vz = (a.vz - battlingCameraPos.vz) / battlingCameraLag;
    clampBattlingMoveToArena(&battlingCameraPos, &a, 0x3D00);
    battlingCameraLag = 100;
    battlingCameraPos.vx += a.vx;
    battlingCameraPos.vy += a.vy;
    battlingCameraPos.vz += a.vz;
}

/* Returns the camera position and target. */
void getBattlingCamera(VECTOR* a, VECTOR* b) {
    *a = battlingCameraPos;
    *b = battlingCameraTarget;
}

/* Clears VRAM (320 or 640 wide). */
void clearBattlingVram(s32 wide) {
    RECT rect;

    rect.x = 0;
    rect.y = 0;
    if (wide) {
        rect.w = 0x280;
    } else {
        rect.w = 0x140;
    }
    rect.h = 0x1E0;
    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
}

/* Sets the arena/camera mode battlingNextSceneMode and runs its setup; mode 6 starts
 * the victory sequence for the gear with more wins. */
void setBattlingSceneMode(s32 mode) {
    battlingNextSceneMode = mode;
    switch (mode) {
    case 3:
        setBattlingBlackClearTiles();
        break;
    case 4:
        startBattlingReplay(battlingRoundFrames);
        break;
    case 8:
        startBattlingRoundResult();
        break;
    case 6:
        if (battlingGear1.mF2_wins < battlingGear2.mF2_wins) {
            startBattlingVictoryScreen(&battlingGear2);
        } else {
            startBattlingVictoryScreen(&battlingGear1);
        }
        break;
    }
}

/* Returns battlingSceneMode. */
s32 getBattlingSceneMode(void) {
    return battlingSceneMode;
}

/* Prints the fight timer battlingFightTime (frames at 30/s) as mm'ss''cc. */
void printBattlingFightTime(void) {
    char buf[32];
    s32 t;
    s32 x;
    s32 min;

    t = battlingFightTime;
    x = t / 1800;
    min = x;
    x = t - x * 1800;
    sprintf(buf, "%02d'%02d''%02d", min, x / 30, t % 30 * 99 / 30);
    printBattlingText(buf);
}
