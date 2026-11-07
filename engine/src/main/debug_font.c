/*
 * On-screen debug text (Noah: debugFont): font state setters/getters,
 * CLUT, cursor, text prims, logToScreen, plus a comparator selection sort.
 *
 * File 0x80036CD8-0x800379B4 (main executable), provisional start; a
 * hand-written block follows. Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/debug_font.h"

/* The host's varargs: x86-64 passes them in registers. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)

/* Debug font state (Noah: sDebugText), 0xD4 bytes. */
typedef struct DebugFont {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u16 m2_tpage;
    /* 0x04 */ SPRT_8* m4_buffer[2];
    /* 0x0C */ s16 mC_x;
    /* 0x0E */ s16 mE_y;
    /* 0x10 */ s16 m10_w;
    /* 0x12 */ s16 m12_h;
    /* 0x14 */ s16 m14_charW;
    /* 0x16 */ s16 m16_charH;
    /* 0x18 */ u8 m18_r;
    /* 0x19 */ u8 m19_g;
    /* 0x1A */ u8 m1A_b;
    /* 0x1B */ u8 m1B_code;
    /* 0x1C */ DR_TPAGE m1C_tpage[2];
    /* 0x2C */ s16 m2C_maxPrims;
    /* 0x2E */ u16 m2E_state;
    /* 0x30 */ s16 m30_cursorX;
    /* 0x32 */ s16 m32_cursorY;
    /* 0x34 */ s16 m34_numPrims;
    /* 0x36 */ s16 m36_lineX;
    /* 0x38 */ SPRT_8* m38_prim;
    /* 0x3C */ u16 m3C_cluts[4];
    /* 0x44 */ TILE m44_tiles[2];
    /* 0x64 */ u8 m64_widths[0x60];
    /* 0xC4 */ OTag mC4_ot[2];
    /* 0xCC */ s16 mCC_savedX;
    /* 0xCE */ s16 mCE_savedY;
    /* 0xD0 */ s16 mD0_savedLineX;
    /* 0xD2 */ u8 mD2_tpageY;
    /* 0xD3 */ u8 pad;
} DebugFont;

extern u8 compressedDebugFont[];
void traceNextAlloc(s32);
static DebugFont* debugFontRam;
void addDebugTextPrim(s32 c);
/* .data from 0x80050594 (JP 0x8004FC34); the compressed font image in front
 * of it, compressedDebugFont, stays an assembly data subsegment (game data). */
void (*pAddDebugTextPrim)(s32) = addDebugTextPrim;  /* printf's character sink (printf.c) */
/* Debug font CLUT image: initDebugFontClut(0x7FFF, 0x0000)'s output. */
u16 debugTextPalette[0x40] = {
    0x0000, 0x7FFF, 0x0000, 0x7FFF, 0x0000, 0x7FFF, 0x0000, 0x7FFF,
    0x0000, 0x7FFF, 0x0000, 0x7FFF, 0x0000, 0x7FFF, 0x0000, 0x7FFF,
    0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x7FFF, 0x7FFF,
    0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x7FFF, 0x7FFF,
    0x0000, 0x0000, 0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x0000, 0x0000, 0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
};
static RECT debugFontClutRect;    /* debug font CLUT rect */
s32 logToScreenProcessString(s32 unused, const char* fmt, va_list ap);
static sptr debugFontExternalBuffer;    /* nonzero: font memory is not ours to free */
extern void (*debugPrintSink)(const char*, ...);  /* debug print sink (heap.c) */
void addDebugTextPrim2(void* ot, void* prim);
void addTile(void* ot, void* prim);
void resetDebugPrint(void);

void setDebugFontFlags(s32 flags) {
    debugFontRam->m0_flags |= flags;
}

void clearDebugFontFlags(s32 flags) {
    debugFontRam->m0_flags &= ~flags;
}

s16 getDebugFontFlags(void) {
    return debugFontRam->m0_flags;
}

void setDebugFontStateFlags(s32 flags) {
    debugFontRam->m2E_state |= flags;
}

void clearDebugFontStateFlags(s32 flags) {
    debugFontRam->m2E_state &= ~flags;
}

s16 getDebugFontStateFlags(void) {
    return debugFontRam->m2E_state;
}

void setDebugFontCharWidth(s16 w) {
    debugFontRam->m14_charW = w;
}

void setDebugFontCharHeight(s16 h) {
    debugFontRam->m16_charH = h;
}

void setDebugFontCursorX(s16 x) {
    debugFontRam->m30_cursorX = x;
}

void setDebugFontCursorY(s16 y) {
    debugFontRam->m32_cursorY = y;
}

void setDebugFontColor(s32 r, s32 g, s32 b) {
    debugFontRam->m18_r = r;
    debugFontRam->m19_g = g;
    debugFontRam->m1A_b = b;
    if (r < 0x80 || g < 0x80 || b < 0x80) {
        debugFontRam->m1B_code &= ~1;
    } else {
        debugFontRam->m1B_code |= 1;
    }
}

/* Noah: initDebugFontClut. */
void initDebugFontClut(u16 fg, u16 bg) {
    u16* p = debugTextPalette;
    s32 i;
    s32 j;

    for (i = 0; i < 8; i++) {
        *p++ = bg;
        *p++ = fg;
    }
    for (; i < 12; i++) {
        *p++ = bg;
        *p++ = bg;
        *p++ = fg;
        *p++ = fg;
    }
    while (i < 14) {
        for (j = 0; j < 4; j++) {
            *p++ = bg;
        }
        for (j = 0; j < 4; j++) {
            *p++ = fg;
        }
        i++;
    }
    for (j = 0; j < 8; j++) {
        *p++ = bg;
    }
    for (j = 8; j < 16; j++) {
        *p++ = fg;
    }
    LoadImage(&debugFontClutRect, debugTextPalette);
}

s16 getDebugFontCharWidth(void) {
    return debugFontRam->m14_charW;
}

s16 getDebugFontCharHeight(void) {
    return debugFontRam->m16_charH;
}

s16 getDebugFontCursorX(void) {
    return debugFontRam->m30_cursorX;
}

s16 getDebugFontCursorY(void) {
    return debugFontRam->m32_cursorY;
}

s16 getDebugFontNumPrims(void) {
    return debugFontRam->m34_numPrims;
}

void saveDebugFontCursor(void) {
    DebugFont* f = debugFontRam;
    f->mCC_savedX = f->m30_cursorX;
    f->mCE_savedY = f->m32_cursorY;
    f->mD0_savedLineX = f->m36_lineX;
}

void restoreDebugFontCursor(void) {
    DebugFont* f = debugFontRam;
    f->m30_cursorX = f->mCC_savedX;
    f->m32_cursorY = f->mCE_savedY;
    f->m36_lineX = f->mD0_savedLineX;
}

/* Noah: logToScreen. */
void logToScreen(const char* fmt, ...) {
    va_list ap;

    if (debugFontRam != NULL) {
        va_start(ap, fmt);
        logToScreenProcessString(0, fmt, ap);
    }
}

void setDebugFontCursorPosition(s32 x, s32 y) {
    DebugFont* f = debugFontRam;
    if (f != NULL) {
        f->m30_cursorX = f->mC_x + x;
        f->m32_cursorY = f->mE_y + y;
    }
}

void setDebugFontLineStart(s32 x, s32 y) {
    DebugFont* f = debugFontRam;
    if (f != NULL) {
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        f->m36_lineX = f->m30_cursorX = f->mC_x + x;
        f->m32_cursorY = f->mE_y + y;
    }
}

/* Noah: addDebugTextPrim. Draw one character at the cursor. */
void addDebugTextPrim(s32 c) {
    DebugFont* f = debugFontRam;
    s32 w;
    s32 u;
    s32 v;

    if (f == NULL) {
        return;
    }
    if (f->m32_cursorY + f->m16_charH > f->mE_y + f->m12_h) {
        return;
    }
    if (f->m34_numPrims > f->m2C_maxPrims) {
        return;
    }
    if (c < 0x20) {
        if (c == '\n') {
            f->m30_cursorX = f->m36_lineX;
            f->m32_cursorY += f->m16_charH;
        }
        return;
    }
    if ((f->m2E_state & 4) && c >= 0x60) {
        c -= 0x20;
    }
    c -= 0x20;
    if (f->m2E_state & 8) {
        w = f->m64_widths[c];
    } else {
        w = f->m14_charW;
    }
    if (f->m30_cursorX + w >= f->mC_x + f->m10_w) {
        if (f->m0_flags & 8) {
            return;
        }
        f->m30_cursorX = f->m36_lineX;
        f->m32_cursorY += f->m16_charH;
    }
    if (c != 0) {
        *(u32*)&f->m38_prim->r0 = *(u32*)&f->m18_r;
        *(u32*)&f->m38_prim->x0 = f->m30_cursorX | (f->m32_cursorY << 16);
        if (f->m2E_state & 2) {
            f->m38_prim->clut = f->m3C_cluts[(c & 0x18) >> 3];
            *(u16*)&f->m38_prim->u0 = ((c & 7) << 4) | ((f->mD2_tpageY + ((c & 0x60) >> 1)) << 8);
        } else {
            f->m38_prim->clut = f->m3C_cluts[(c & 0x30) >> 4];
            *(u16*)&f->m38_prim->u0 = ((c & 0xF) << 3) | ((f->mD2_tpageY + ((c & 0xC0) >> 3)) << 8);
        }
        f->m38_prim++;
        f->m34_numPrims++;
    }
    f->m30_cursorX += w;
}

/* Noah: resetDebugPrint. */
void resetDebugPrint(void) {
    DebugFont* f = debugFontRam;
    s16 y = f->mE_y;
    u8 code = f->m1B_code & 0xFE;
    SPRT* prim = f->m4_buffer[f->m2E_state & 1];
    s16 x = f->mC_x;

    f->m34_numPrims = 0;
    f->m32_cursorY = y;
    f->mCE_savedY = y;
    f->m1B_code = code;
    f->m30_cursorX = x;
    f->mCC_savedX = x;
    f->m36_lineX = x;
    f->mD0_savedLineX = x;
    f->m38_prim = prim;
}

// s16 s16
/* Noah: printDebugTextOnScreen. Link this frame's glyphs into ot (or into
 * the font's own OT and draw it when ot is NULL or -1). */
void printDebugTextOnScreen(OTag* ot) {
    DebugFont* f = debugFontRam;
    s16 draw;
    s16 idx;
    u16 state;
    u16 old;
    SPRT_8* prim;
    s32 n;

    if (f == NULL) {
        return;
    }
    draw = 0;
    if (f->m0_flags & 1) {
        idx = 0;
        state = f->m2E_state & 0xFFFE;
    } else {
        old = f->m2E_state;
        idx = old & 1;
        if (idx) {
            state = old & 0xFFFE;
        } else {
            state = old | 1;
        }
    }
    f->m2E_state = state;
    if (ot == NULL || ot == (OTag*)-1) {
        DrawSync(0);
        ot = &f->mC4_ot[idx];
        draw = 1;
        TermPrim(ot);
    }
    prim = f->m4_buffer[idx];
    for (n = f->m34_numPrims; n != 0; n--) {
        addDebugTextPrim2(ot, prim++);
    }
    AddPrim(ot, &f->m1C_tpage[idx]);
    if (f->m0_flags & 0x10) {
        addTile(ot, &f->m44_tiles[idx]);
    }
    resetDebugPrint();
    if (draw) {
        DrawOTag(ot);
    }
}

void setDebugFontBuffer(sptr v) {
    debugFontExternalBuffer = v;
}

/* Noah: deinitDebugFont. */
void deinitDebugFont(void) {
    if (debugFontRam != NULL) {
        debugPrintSink = logToScreen;
        if (debugFontExternalBuffer == 0) {
            freeHeap(debugFontRam);
        }
        debugFontRam = NULL;
    }
    debugFontExternalBuffer = 0;
}

DebugFont* initDebugFont(s32 x, s32 y, s32 w, s32 h, s32 bufferSize, u32 flags,
                         s32 tpageX, s32 tpageY, s32 clutX, s32 clutY, u8* font) {
    DebugFont* f;
    SPRT_8* buf;
    u8* data;
    u8* tex;
    u8 b;
    s16 small;
    s16 big;
    s32 texH;
    s32 size;
    RECT rect;

    tex = debugFontExternalBuffer ? (u8*)debugFontExternalBuffer : (traceNextAlloc(0x32), allocateHeap(sizeof(DebugFont) + sizeof(SPRT_8) * bufferSize * ((flags & 1) ? 1 : 2), ((flags >> 2) ^ 1) & 1));
    f = (DebugFont*)tex;
    buf = (SPRT_8*)(f + 1);
    f->m4_buffer[0] = buf;
    if (flags & 1) {
        f->m4_buffer[1] = buf;
    } else {
        f->m4_buffer[1] = buf + bufferSize;
    }
    if (font == NULL) {
        font = compressedDebugFont;
    }
    data = mallocAndDecompress(font, 0);
    b = data[0];
    small = b & 1;
    big = (u8)(b & 2);
    f->m14_charW = data[2];
    f->m16_charH = data[3];
    if (flags & 2) {
        big = 0;
    }
    f->m30_cursorX = f->mC_x = x;
    f->m0_flags = flags;
    f->m32_cursorY = f->mE_y = y;
    f->m10_w = w;
    f->m12_h = h;
    f->m1A_b = 0xFF;
    f->m19_g = 0xFF;
    f->m18_r = 0xFF;
    f->m2C_maxPrims = bufferSize;
    f->m34_numPrims = 0;
    f->m2E_state = 0;
    if (small) {
        f->m1B_code = 0x7D;
    } else {
        f->m1B_code = 0x75;
    }
    f->mD2_tpageY = tpageY;
    if (big == 0) {
        f->m2E_state |= 4;
    }
    if (small) {
        f->m2E_state |= 2;
        texH = 0x20;
        if (big) {
            texH = 0x30;
        }
    } else {
        texH = 8;
        if (big) {
            texH = 0x10;
        }
    }
    rect.x = tpageX;
    rect.y = tpageY;
    rect.w = 0x20;
    rect.h = texH;
    tex = data + 4;
    if (f->m14_charW == 0) {
        f->m2E_state |= 8;
        memmove(f->m64_widths, tex, 0x60);
        tex = data + 0x64;
    }
    LoadImage(&rect, tex);
    f->m2_tpage = GetTPage(0, 0, tpageX, tpageY);
    rect.w = 0x40;
    rect.x = clutX;
    rect.y = clutY;
    rect.h = 1;
    f->m3C_cluts[0] = GetClut(clutX, clutY);
    f->m3C_cluts[1] = GetClut(clutX + 0x10, clutY);
    f->m3C_cluts[2] = GetClut(clutX + 0x20, clutY);
    f->m3C_cluts[3] = GetClut(clutX + 0x30, clutY);
    debugFontClutRect = rect;
    initDebugFontClut(0x7FFF, 0);
    SetDrawTPage(&f->m1C_tpage[0], 0, 0, (s16)f->m2_tpage);
    SetDrawTPage(&f->m1C_tpage[1], 0, 0, (s16)f->m2_tpage);
    setlen(&f->m44_tiles[0], 3);
    setcode(&f->m44_tiles[0], 0x60);
    f->m44_tiles[0].r0 = 0;
    f->m44_tiles[0].g0 = 0;
    f->m44_tiles[0].b0 = 0;
    *(s32*)&f->m44_tiles[0].x0 = x | (y << 16);
    *(s32*)&f->m44_tiles[0].w = w | (h << 16);
    f->m44_tiles[0].code |= 2;
    f->m44_tiles[1] = f->m44_tiles[0];
    debugFontRam = f;
    resetDebugPrint();
    freeHeap(data);
    return f;
}

/* Selection sort of n elements of size bytes, using cmp (qsort-like). */
void selectionSort(u8* base, s32 n, s32 size, s32 (*cmp)(void*, void*)) {
    u8* tmp;
    s32 i;
    s32 j;
    s32 min;

    tmp = allocateHeap(size, 0);
    for (i = 0; i < n; i++) {
        min = i;
        for (j = i; j < n; j++) {
            if (cmp(base + size * min, base + j * size) > 0) {
                min = j;
            }
        }
        memcpy(tmp, base + i * size, size);
        memcpy(base + i * size, base + size * min, size);
        memcpy(base + size * min, tmp, size);
    }
    freeHeap(tmp);
}
