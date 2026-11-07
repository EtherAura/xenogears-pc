/*
 * Dialog text (Noah: dialog windows): window line sprites, font and text
 * bundle setup, character-code decoding/encoding, string getters, and the
 * text printer with its control codes (updateDialogTextImage).
 *
 * File 0x80032F54-0x8003569C (main executable): the hand-written LZSS
 * decoder ends at the start, the text printer's jump table starts this
 * object's .rodata at 0x80018A7C, and the controller code (pad.c, -G0)
 * follows. Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_wide.h"
#include "pc_glyph.h"
#include "pc_observe.h"
#include "pc_gametext.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"
#include "pc_convert.h"

/* file-local in retail: .sbss (ASPSX -G8, declared after use so never $gp) */
static s32 dialogFontVar0;
static s32 dialogFontVar4;
static s32 dialogFontVar2;
static s32 dialogFontVar3;
static u8* dialogFontVarPtr2;
static u8** printDialogTextVar;
static s32 dialogFontVar1;
static u32* dialogTextBundle;
static u8* dialogFontVarPtr;
/* .bss */
static DialogWindow18 renderStringWindow;
static DialogWindow18Line renderStringLine;
static u16 dialogNumberCharCodes[14];  /* 12 used + 4 unreferenced bytes before dialogSpecialCase9AndA_2Data */
static u8 dialogSpecialCase9AndA_2Data[0x18];
/* .data (0x80050190; JP 0x8004F834) */
/* Noah: fontPalettes. The two 16-colour text CLUTs initFontPalettes uploads. */
u16 fontPalettes[32] = {
    0x0000, 0xF7BD, 0xC086, 0xF7BD, 0x0000, 0xF7BD, 0xC086, 0xF7BD,
    0x0000, 0xF7BD, 0xC086, 0xF7BD, 0x0000, 0xF7BD, 0xC086, 0xF7BD,
    0x0000, 0x0000, 0x0000, 0x0000, 0xF7BD, 0xF7BD, 0xF7BD, 0xF7BD,
    0xC086, 0xC086, 0xC086, 0xC086, 0xF7BD, 0xF7BD, 0xF7BD, 0xF7BD,
};
/* Glyph for character 0xFF 0xFF: 11 rows of 16 one-bit pixels, then a zero row. */
u16 crossedBoxGlyph[12] = {
    0xC07F, 0x60C0, 0xA0A0, 0x2091, 0x208A, 0x2084,
    0x208A, 0x2091, 0xA0A0, 0x60C0, 0xC07F, 0x0000,
};

#define getcode(p) (u8)(((P_TAG*)(p))->code)
#define setShadeTex(p, tge) ((tge) ? setcode(p, getcode(p) | 1) : setcode(p, getcode(p) & ~1))
/* Word-sized stores of packed primitive fields, as retail does them. */
#define setRGBC0W(p, v) (*(u32*)&(p)->r0 = (v))
#define setXY0W(p, _x, _y) (*(s32*)&(p)->x0 = (_x) | ((_y) << 16))
#define setWHW(p, v) (*(s32*)&(p)->w = (v))
#define setUV0W(p, v) (*(u16*)&(p)->u0 = (v))

void addSPRT(void* ot, void* p);
void traceNextAlloc(s32);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
extern u8 dpadButtonMapping[]; /* button-name index table for the 0x0F 0x0F escape (pad.c) */
#include "pc_pad.h"
#include "pc_gameplay.h"
s32 pcPadHeldRemapped(void); /* pad.c: pad 0 held now, remapped (0x20 confirms a dialogue) */
/* The glyph a text source brought, from updateDialogTextImage to the two
 * glyph functions below (pc_gametext.h). */
static const u16* pcTextGlyph;
static u16 pcTextGlyphWidth;

/* Noah: setupWindowSize2. Set up a dialog text area: line sprites for a
 * width x height text box at (x, y), its line images at VRAM (x1, y1).
 * Retail: 0x80032F54, inside splat's decompress (see status). */
void setupWindowSize2(DialogWindow18* p, s16 x1, s16 y1, s16 x, s16 y, s16 width, s16 height) {
    s32 i;
    s32 half;
    s32 odd;

    p->m4_x = x;
    p->m10_flags = 0;
    p->m84_delay = 0;
    p->m8C_queue = NULL;
    p->m82_queueCount = 0;
    p->m14_lineHeight = 14;
    p->m68_charsPerStep = 1;
    p->m69_charsThisStep = 1;
    p->mA_width1 = width;
    p->m6C_autoClose = 0;
    p->m6A_savedCharsPerStep = 0;
    p->m6D = 0;
    p->m6B_stopReason = 0;
    p->m6E_selectedLineForMultiChoice = 0xFF;
    p->mE_vramY = y1;
    p->m6_y = y;
    p->mC_height = height;
    p->mA_width1 |= 1;
    p->m8_widthTimes4 = p->mA_width1 << 2;
    p->m12_widthPadded = p->mA_width1 + 3;
    traceNextAlloc(0x29);
    /* A window of no lines (field 725, Cross at its XG_FIELD start): the text
     * code still uses line 0 (its m50_rect for the LoadImage, the fields it
     * resets). On the PS1 that line is the heap after the 0-byte block: the
     * next block's header and the text image, zero there (Beetle: LoadImage
     * of 0,256 0x0, nothing uploaded). The PC's larger lines reach its text
     * image's glyph markers instead (a 4028x24752 upload over VRAM): it gets
     * one zeroed line of its own. */
    if (p->mC_height <= 0) {
        u32* z;

        p->m28_perLineBuffer = allocateHeap(PC_HEAP_SIZED(sizeof(DialogWindow18Line), 0x60), 2);
        for (z = (u32*)p->m28_perLineBuffer; z < (u32*)(p->m28_perLineBuffer + 1); z++) {
            *z = 0;
        }
    } else
    p->m28_perLineBuffer = allocateHeap(PC_HEAP_SIZED(p->mC_height * sizeof(DialogWindow18Line), p->mC_height * 0x60), 2);
    traceNextAlloc(0x28);
    p->m2C_inRamDialogTextImage = allocateHeap(PC_HEAP_SIZED(p->m12_widthPadded * 14 * sizeof(u16), p->m12_widthPadded * 14 * 0x2), 2);
    pcGlyphClearRAM(p->m2C_inRamDialogTextImage, p->m12_widthPadded * 14 * sizeof(u16), 3);
    setlen(&p->m48_textTile[0], 3);
    setRGBC0W(&p->m48_textTile[0], 0x60000000);
    setXY0W(&p->m48_textTile[0], p->m4_x - 7, p->m6_y - 5);
    setWHW(&p->m48_textTile[0], (p->mA_width1 * 4 + 13) | ((p->mC_height * p->m14_lineHeight + 10) << 16));
    SetSemiTrans(&p->m48_textTile[0], 1);
    p->m48_textTile[1] = p->m48_textTile[0];

    for (i = 0; i < p->mC_height; i++) {
        half = i / 2;
        odd = i & 1;
        setXY0W(&p->m28_perLineBuffer[i].m0_sprt[0][0], p->m4_x, p->m6_y + p->m14_lineHeight * i);
        setXY0W(&p->m28_perLineBuffer[i].m0_sprt[0][1], p->m4_x + 0x100, p->m6_y + p->m14_lineHeight * i);
        setWHW(&p->m28_perLineBuffer[i].m0_sprt[0][0], p->m8_widthTimes4 < 0x101 ? p->m8_widthTimes4 | (13 << 16) : 0x100 | (13 << 16));
        setWHW(&p->m28_perLineBuffer[i].m0_sprt[0][1], p->m8_widthTimes4 < 0x101 ? 13 << 16 : (p->m8_widthTimes4 - 0xF0) | (13 << 16));
        setUV0W(&p->m28_perLineBuffer[i].m0_sprt[0][0], ((x1 & 0x3F) << 2) | (((y1 + half * 13) & 0xFF) << 8));
        setUV0W(&p->m28_perLineBuffer[i].m0_sprt[0][1], ((x1 & 0x3F) << 2) | (((y1 + half * 13) & 0xFF) << 8));
        setlen(&p->m28_perLineBuffer[i].m0_sprt[0][0], 4);
        setcode(&p->m28_perLineBuffer[i].m0_sprt[0][0], 0x64);
        setlen(&p->m28_perLineBuffer[i].m0_sprt[0][1], 4);
        setcode(&p->m28_perLineBuffer[i].m0_sprt[0][1], 0x64);
        p->m28_perLineBuffer[i].m0_sprt[0][0].code |= 1;
        p->m28_perLineBuffer[i].m0_sprt[0][1].code |= 1;
        p->m28_perLineBuffer[i].m0_sprt[1][0] = p->m28_perLineBuffer[i].m0_sprt[0][0];
        p->m28_perLineBuffer[i].m0_sprt[1][1] = p->m28_perLineBuffer[i].m0_sprt[0][1];
        p->m28_perLineBuffer[i].m50_rect.x = x1;
        p->m28_perLineBuffer[i].m50_rect.y = y1 + half * 13;
        p->m28_perLineBuffer[i].m50_rect.w = p->m12_widthPadded;
        p->m28_perLineBuffer[i].m50_rect.h = 13;
        p->m28_perLineBuffer[i].m58_widthDiv4 = 0;
        p->m28_perLineBuffer[i].m5E_clut = odd == 0 ? textSpriteMode0 : textSpriteMode1;
        p->m28_perLineBuffer[i].m5C_vramY = y1 + half * 13;
        p->m28_perLineBuffer[i].m5A_oddLine = odd;
        p->m28_perLineBuffer[i].m5B_line = i;
    }
    SetDrawMode(&p->m30_textTileDrawMode[0], 0, 0, GetTPage(0, 0, x1, y1), NULL);
    SetDrawMode(&p->m30_textTileDrawMode[1], 0, 0, GetTPage(0, 0, x1 + 0x40, y1), NULL);
}

/* Noah: doPointerRelocation. Turn a count-prefixed table of offsets into pointers. */
u32 doPointerRelocation(u32* data) {
    u32 i = 1;
    u8* base = (u8*)data;

    PORT_TODO("relocate disc offsets into host pointers");
    return data[0];
}

void relocateOffsetTable(u32* data) {
    u32 i;
    s32 base;

    i = 1;
    PORT_TODO("relocate disc offsets into host pointers");
}

u8* getFontData(void) {
    return dialogFontVarPtr;
}

u32* getTextBundle(void) {
    return dialogTextBundle;
}

void freeFontData(void) {
    unflagAllocation(dialogFontVarPtr);
    freeHeap(dialogFontVarPtr);
    dialogFontVarPtr = NULL;
}

void freeTextBundle(void) {
    unflagAllocation(printDialogTextVar - 1);
    freeHeap(printDialogTextVar - 1);
    unflagAllocation(dialogTextBundle);
    freeHeap(dialogTextBundle);
    dialogTextBundle = NULL;
}

/* Noah: initFont. Install the font file: header fields and glyph data. */
void initFont(u8* font) {
    s32 glyphOffset;

    if (font == NULL) {
        traceNextAlloc(0x20);
    } else {
        flagAllocation(font);
        dialogFontVarPtr = font;
        dialogFontVarPtr2 = font;
        glyphOffset = *(u16*)(font + 2);
        dialogFontVar0 = *(u16*)(font + 4);
        dialogFontVar4 = *(u16*)(font + 6);
        dialogFontVar2 = *(u16*)(font + 8);
        dialogFontVar3 = *(u16*)(font + 10);
        dialogFontVar1 = *(u16*)(font + 12);
        dialogFontVarPtr2 += glyphOffset;
    }
}

/* Noah: setupPrintDialogTextVar. Install the relocated text bundle. */
void setupPrintDialogTextVar(void* bundle) {
    if (bundle == NULL) {
        traceNextAlloc(0x20);
    } else {
        flagAllocation(bundle);
        dialogTextBundle = bundle;
        printDialogTextVar = (u8**)pcRelocTable(bundle);
        flagAllocation(printDialogTextVar);
        printDialogTextVar++;
    }
    pcGameTextInit(); /* the player's text sources, read once the text is in (system/gametext.h) */
}

void initFontAndTextBundle(u8* font, u32* bundle) {
    initFont(font);
    setupPrintDialogTextVar(bundle);
}

/* Noah: initFontPalettes. Upload the text CLUTs and remember them. */
void initFontPalettes(s16 x, s16 y) {
    RECT rect;

    rect.x = x;
    rect.y = y;
    rect.w = 0x20;
    rect.h = 1;
    LoadImage(&rect, fontPalettes);
    textSpriteMode0 = GetClut(x, y);
    textSpriteMode1 = GetClut(x + 0x10, y);
}

/* Noah: getDialogParamPointer. Entry idx of a u32-count, u16-offset table. */
u8* getDialogParamPointer(u8* buf, s32 idx) {
    /* The text from another source when the player chose one (system/gametext.h). */
    if (pcGameTextOn) {
        u8* s = pcGameTextString(buf, idx);
        if (s != NULL) {
            return s;
        }
    }
    return buf + *(u16*)(buf + idx * 2 + 4);
}

/* Byte pairs after a u16-count header and table. */
u8 getDialogParam0(u16* p, s32 i) {
    if (pcGameTextOn) {
        s32 v = pcGameTextWindow((u8*)p, i, 0); /* the source's window width with its text */
        if (v >= 0) {
            return (u8)v;
        }
    }
    u8* q = (u8*)p + (p[0] * 2 + 6);
    q += i * 2;
    return q[0];
}

u8 getDialogParam1(u8* p, s32 i) {
    if (pcGameTextOn) {
        s32 v = pcGameTextWindow(p, i, 1);
        if (v >= 0) {
            return (u8)v;
        }
    }
    p += *(u16*)p * 2 + 6;
    p += i * 2;
    return p[1];
}

/* Noah: getDialogParamPointer on one of the text bundle's string banks. */
u8* getDeathblowName(s32 bank, s32 idx) {
    return getDialogParamPointer(printDialogTextVar[bank], idx);
}

u8* getTextBank16String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x40 / 4], idx); }

u8* getAccessoryName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x44 / 4], idx); }

u8* getUsableItemName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x58 / 4], idx); }

u8* getWeaponName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x5C / 4], idx); }

u8* getTextBank24String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x60 / 4], idx); }

u8* getTextBank25String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x64 / 4], idx); }

u8* getItemLabelString(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x48 / 4], idx); }

u8* drawCircleMenuChi_updateSub0Sub1(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x50 / 4], idx); }

u8* getTextBank19String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x4C / 4], idx); }

u8* getTextBank21String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x54 / 4], idx); }

u8* getTextBank27String(s32 idx) { return getDialogParamPointer(printDialogTextVar[0x6C / 4], idx); }

u8* getGearDeathblowName(s32 bank, s32 idx) { return getDialogParamPointer(printDialogTextVar[0x70 / 4 + bank], idx); }

u8* drawCircleMenuChi_updateSub0Sub2(s32 idx) { return getDialogParamPointer(printDialogTextVar[0xC0 / 4], idx); }

u8* getGearAccessoryName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0xC8 / 4], idx); }

u8* getGearWeaponName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0xCC / 4], idx); }

u8* getGearFuelAbilityName(s32 idx) { return getDialogParamPointer(printDialogTextVar[0xD0 / 4], idx); }

/* Noah: dialogSpecialCase9AndASub0. Character codes (0xFFFF-terminated) to
 * a byte string in dialogSpecialCase9AndA_2Data, through the code table in text bank 27. */
void dialogSpecialCase9AndASub0(u16* codes) {
    u8* out = dialogSpecialCase9AndA_2Data;
    u16 c;
    u8* table = printDialogTextVar[27];

    while ((c = *codes) != 0xFFFF) {
        u8* p = (u8*)(c * 2 + (sptr)table);
        codes++;
        if (*p != 0) {
            *out = *p;
            out++;
            *out = p[1];
        } else {
            *out = p[1];
        }
        out++;
    }
    *out = 0;
}

/* The same for n codes into a caller buffer. */
void decodeCharacterCodes(u16* codes, u8* out, u32 n) {
    u8* table = printDialogTextVar[27];

    while (n--) {
        u8* p = (u8*)(*codes * 2 + (sptr)table);
        codes++;
        if (*p != 0) {
            *out = *p;
            out++;
            *out = p[1];
        } else {
            *out = p[1];
        }
        out++;
    }
    *out = 0;
}

/* Code of the character (a, b) in text bank 27, 0x8000 if missing. */
s32 findCharacterCode(u8 a, u8 b) {
    s16 i;
    u8* table = printDialogTextVar[27];

    for (i = 0; i < 0x144; i++) {
        if (table[i * 2] == a && table[i * 2 + 1] == b) {
            return i;
        }
    }
    return 0x8000;
}

/* Encode a byte string as character codes; -1 on an unknown character. */
s32 encodeStringToCharCodes(u8* s, u16* out) {
    u8 c;

    while ((c = *s++) != 0) {
        u8 a, b;
        if (c < dialogFontVar0) {
            a = 0;
            b = c;
        } else {
            a = c;
            b = *s++;
        }
        *out = findCharacterCode(a, b);
        if (*out++ == 0x8000) {
            return -1;
        }
    }
    return 0;
}

u8 addDialogWindowsToOTSub0(DialogWindow18* p) {
    if (p->m10_flags & 8) {
        return p->m6B_stopReason;
    }
    return 0;
}

/* Noah: dialogSpecialCase9AndA. Format a number (base = glyph page) into
 * dialogNumberCharCodes as character codes, then decode it into dialogSpecialCase9AndA_2Data. */
void dialogSpecialCase9AndA(s32 value, s32 base, s32 sign) {
    u32 div;
    u32 v;
    s32 i;
    u16* p;

    div = 1000000000;
    base <<= 4;
    v = value;
    if (sign != 0) {
        sign = 11;
        if (value < 0) {
            v = -value;
            sign = 10;
        }
    }
    for (i = 0; i < 10; i++) {
        dialogNumberCharCodes[i + 1] = v / div + base;
        v %= div;
        div /= 10;
    }
    dialogNumberCharCodes[11] = 0xFFFF;
    p = dialogNumberCharCodes;
    *p = base;
    while (*p == base && p != &dialogNumberCharCodes[10]) {
        p++;
    }
    if (sign != 0) {
        p--;
        *p = sign + base;
    }
    dialogSpecialCase9AndASub0(p);
}

/* Noah: dialogSpecialCase9AndA_2. Print a sub-string, then resume. */
void dialogSpecialCase9AndA_2(DialogWindow18* p, u8* str) {
    p->m20_savedString = p->m1C_currentStringToPrint;
    p->m1C_currentStringToPrint = str;
    p->m10_flags |= 0x80;
}

/* The glyph printDialogCharacter draws for (type, id), for the proportional
 * print (pcGameTextPrint: a pack's opt-in). */
static const u16* pcDialogGlyph(u16 type, u16 id) {
    if (type == 0xFF && id >= 0xC0 && pcTextGlyph != NULL) return pcTextGlyph;
    if (type == 0) return (u16*)(dialogFontVarPtr2 + (id - dialogFontVar1) * 22);
    if (type == 0xFF && id == 0xFF) return crossedBoxGlyph;
    return (u16*)(dialogFontVarPtr2 + id * 22 + dialogFontVar4 + (type - dialogFontVar0) * 0x1600);
}

/* A button symbol the text prints as a plain character (the tutorials' "x30", "press O")
 * shows the button the player presses for it, as the 0x0F escape's does (pcPadDialogSlot,
 * through the game's own button configuration dpadButtonMapping): the characters of the
 * game's button strings (bank 0xC4, slots 0-7: circle, cross, triangle, square, L1, L2, R1,
 * R2) are recognised and replaced by the string of the button that stands for it. */
static void pcDialogDecode(const u8* s, u16* c, s32* id) {
    if (s[0] < dialogFontVar0) { *c = 0; *id = s[0]; } else { *c = s[0]; *id = s[1]; }
}
static void pcDialogButtonChar(u16* c, s32* id) {
    u8* bank;
    u16 sc;
    s32 sid, slot, to;

    if (printDialogTextVar == NULL || (bank = printDialogTextVar[0xC4 / 4]) == NULL || *c == 0xFF) return;
    for (slot = 0; slot < 8; slot++) {
        pcDialogDecode(getDialogParamPointer(bank, slot), &sc, &sid);
        if (sc != *c || sid != *id) continue;
        to = pcPadDialogSlot(dpadButtonMapping[slot]);
        if (to == slot) return;
        if (to < 0) {
            const unsigned char* t = pcPadDialogText(dpadButtonMapping[slot]);
            *c = t[0]; *id = t[1];
        } else {
            pcDialogDecode(getDialogParamPointer(bank, to), c, id);
        }
        return;
    }
}

/* Noah: updateDialogTextImage. Advance to the next line when the current one
 * is full, then print up to m69_charsThisStep characters of the string,
 * handling control codes (0 end, 1 newline, 2 wait for button, 3 close,
 * 0x0F escapes). */
void updateDialogTextImage(DialogWindow18* p) {
    s32 count = p->m69_charsThisStep;
    u8* s;
    u8 ch;
    u16 c;
    s32 id;
    u16 w;
    s16 len;
    s32 n;
    s32 line;
    u32 member;   /* party slot / character index */
    s32 pcWidth;

    if (p->m0_cursorX > p->mA_width1) {
        p->m0_cursorX = 0;
        p->m2_cursorLine++;
        p->m18_lineCount++;
        if (p->m2_cursorLine >= p->mC_height) {
            p->m2_cursorLine = 0;
            p->m10_flags |= 1;
        }
        if (p->m10_flags & 1) {
            p->m28_perLineBuffer[p->m16_firstLine].m58_widthDiv4 = 0;
            p->m16_firstLine++;
            if (p->m16_firstLine >= p->mC_height) {
                p->m16_firstLine = 0;
            }
        }
        n = p->m18_lineCount % (p->mC_height + 1);
        line = p->m2_cursorLine;
        p->m28_perLineBuffer[line].m5C_vramY = n / 2 * 13 + (u8)p->mE_vramY;
        p->m28_perLineBuffer[line].m5E_clut = (n & 1) == 0 ? textSpriteMode0 : textSpriteMode1;
        p->m28_perLineBuffer[line].m5A_oddLine = n & 1;
        p->m28_perLineBuffer[line].m5B_line = n;
        p->m28_perLineBuffer[line].m50_rect.y = p->mE_vramY + n / 2 * 13;
    }
    count--;
    if (p->m6C_autoClose != 0) {
        p->m6C_autoClose = 0;
        p->m10_flags &= ~4;
        return;
    }
    while (count != -1) {
        s = p->m1C_currentStringToPrint;
        ch = s[0];
        c = ch;
        if (c == 0) {
            if (p->m10_flags & 0x80) {
                p->m10_flags &= ~0x80;
                p->m1C_currentStringToPrint = p->m20_savedString + 1;
            } else {
                p->m10_flags |= 8;
                p->m6B_stopReason = 1;
                p->m6C_autoClose = 1;
                return;
            }
        } else if (c == 3) {
            p->m6B_stopReason = 3;
            p->m10_flags |= 8;
            p->m1C_currentStringToPrint++;
            return;
        } else if (c == 0xF) {
            switch (s[1]) {
            case 0:
                p->m84_delay = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 3;
                return;
            case 1:
                c = p->m1C_currentStringToPrint[2];
                if (c != 0) {
                    count += c;
                    p->m6A_savedCharsPerStep = p->m68_charsPerStep;
                    p->m68_charsPerStep = c;
                    p->m69_charsThisStep = c;
                } else {
                    p->m68_charsPerStep = p->m6A_savedCharsPerStep;
                    p->m69_charsThisStep = p->m6A_savedCharsPerStep;
                    p->m6A_savedCharsPerStep = 0;
                }
                p->m1C_currentStringToPrint += 3;
                break;
            case 2:
                p->m84_delay = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 3;
                p->m6C_autoClose = 1;
                return;
            case 3:
                c = p->m1C_currentStringToPrint[2];
                id = p->m1C_currentStringToPrint[3];
                p->m1C_currentStringToPrint += 3;
                count++;
                dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[c], id));
                break;
            case 4:
                p->m1C_currentStringToPrint++;
                switch (p->m80_stringRef & 0xFF00) {
                case 0:
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x58 / 4], p->m80_stringRef & 0xFF));
                    break;
                case 0x100:
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x5C / 4], p->m80_stringRef & 0xFF));
                    break;
                case 0x200:
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x44 / 4], p->m80_stringRef & 0xFF));
                    break;
                case 0x300:
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0xCC / 4], p->m80_stringRef & 0xFF));
                    break;
                case 0x400:
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0xC8 / 4], p->m80_stringRef & 0xFF));
                    break;
                }
                break;
            case 5:
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                member = c;
                if (member >= 0x80 && (member = gameState.m1D34_currentParty[member - 0x80]) == 0xFF) {
                    dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x68 / 4], 0));
                } else {
                    dialogSpecialCase9AndA_2(p, gameState.m0_names[member]);
                }
                count++;
                break;
            case 6:
                count++;
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x5C / 4], c));
                break;
            case 7:
                count++;
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x60 / 4], c));
                break;
            case 8:
                count++;
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA_2(p, getDialogParamPointer(printDialogTextVar[0x64 / 4], c));
                break;
            case 9:
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA(p->m70_vars[c], 0, 0);
                dialogSpecialCase9AndA_2(p, dialogSpecialCase9AndA_2Data);
                count++;
                break;
            case 0xA:
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA(p->m70_vars[c], 1, 0);
                dialogSpecialCase9AndA_2(p, dialogSpecialCase9AndA_2Data);
                count++;
                break;
            case 0xB:
                p->m6D = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                break;
            case 0xC:
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                dialogSpecialCase9AndA(p->m70_vars[c], 1, 1);
                dialogSpecialCase9AndA_2(p, dialogSpecialCase9AndA_2Data);
                count++;
                break;
            case 0xD:
                p->m84_delay = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 3;
                p->m6C_autoClose = 1;
                p->m10_flags |= 0x200;
                return;
            case 0xE:
                p->m6A_savedCharsPerStep = p->m68_charsPerStep;
                p->m68_charsPerStep = 1;
                p->m69_charsThisStep = 1;
                p->m86_currentDelayForNextCharacter = p->m88_delayBetweenCharacters = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 3;
                return;
            case 0xF:
                c = p->m1C_currentStringToPrint[2];
                p->m1C_currentStringToPrint += 2;
                count++;
                {
                    int slot = pcPadDialogSlot(dpadButtonMapping[c]);
                    dialogSpecialCase9AndA_2(p, slot < 0 ? pcPadDialogText(dpadButtonMapping[c]) :
                        getDialogParamPointer(printDialogTextVar[0xC4 / 4], slot));
                }
                break;
            }
        } else if (c == 2) {
            p->m6B_stopReason = 2;
            p->m10_flags |= 0x48;
            p->m1C_currentStringToPrint++;
            if (*p->m1C_currentStringToPrint != 1) {
                return;
            }
            p->m1C_currentStringToPrint++;
            return;
        } else if (c == 1) {
            p->m0_cursorX = 100;
            p->m1C_currentStringToPrint++;
            return;
        } else {
            len = 1;
            if (c < dialogFontVar0) {
                c = 0;
                id = ch;
            } else {
                id = s[1];
                len = 2;
                /* FF C0..FE xx: a glyph another text source brought (pc_gametext.h) */
                pcTextGlyph = c == 0xFF ? pcGameTextGlyph(s, &pcTextGlyphWidth) : NULL;
                if (pcTextGlyph != NULL) {
                    len = 3;
                }
            }
            pcDialogButtonChar(&c, &id);
            if (pcGameTextWrapOn && pcGameTextWrap(p, &p->m1C_currentStringToPrint, &p->m0_cursorX, p->mA_width1)) {
                return;
            }
            w = getDialogFontCharacterWidth(c, id);
            pcWidth = pcGameTextWrapOn ? pcGameTextPrintWidth(p, p->mA_width1) : p->mA_width1;
            if (pcGameTextProportionalOn) {
                /* a pack's text printing proportionally (its opt-in; off: never asked) */
                s16 was = p->m0_cursorX;
                s32 r = pcGameTextPrint(p, pcPadIsGlyph(c, id) ? NULL : pcDialogGlyph(c, id), w,
                                        p->m2C_inRamDialogTextImage, p->m12_widthPadded,
                                        p->m28_perLineBuffer[p->m2_cursorLine].m5A_oddLine, &p->m0_cursorX,
                                        pcWidth);
                if (r < 0) {
                    p->m0_cursorX = p->mA_width1 + 1;
                    return;
                }
                if (r > 0) {
                    if (pcObserveLine && p->m0_cursorX > was) pcObserveLine(p, p->m2C_inRamDialogTextImage,
                        p->m12_widthPadded, was, p->m0_cursorX - was, p->m2_cursorLine,
                        p->m28_perLineBuffer[p->m2_cursorLine].m5A_oddLine);
                    p->m1C_currentStringToPrint += len;
                    p->m28_perLineBuffer[p->m2_cursorLine].m58_widthDiv4 = p->m0_cursorX;
                    count--;
                    continue;
                }
            }
            if (pcWidth < p->mA_width1 && pcWidth < p->m0_cursorX + w) {
                p->m0_cursorX = p->mA_width1 + 1;
                return;
            }
            if (p->mA_width1 < p->m0_cursorX + w) {
                p->m0_cursorX += w;
                return;
            }
            printDialogCharacter(c, id, &p->m2C_inRamDialogTextImage[p->m0_cursorX], p->m12_widthPadded,
                          p->m28_perLineBuffer[p->m2_cursorLine].m5A_oddLine);
            pcGlyphDialog(&p->m2C_inRamDialogTextImage[p->m0_cursorX], p->m12_widthPadded, w * 4,
                          p->m28_perLineBuffer[p->m2_cursorLine].m5A_oddLine);
            if (pcObserveLine) pcObserveLine(p, p->m2C_inRamDialogTextImage, p->m12_widthPadded,
                                            p->m0_cursorX, w, p->m2_cursorLine,
                                            p->m28_perLineBuffer[p->m2_cursorLine].m5A_oddLine);
            p->m1C_currentStringToPrint += len;
            p->m0_cursorX += w;
            p->m28_perLineBuffer[p->m2_cursorLine].m58_widthDiv4 = p->m0_cursorX;
        }
        count--;
    }
}

void addDialogWindowsToOTSub1(DialogWindow18* w) {
    w->m10_flags &= ~8;
    if (w->m10_flags & 0x200) {
        w->m84_delay = 0;
        w->m6C_autoClose = 0;
        w->m10_flags &= ~0x200;
    }
}

void resetDialogWindow18(DialogWindow18* w) {
    if (w->m84_delay == 0) {
        w->m6C_autoClose = 0;
        w->m10_flags &= 2;
    }
}

void dealocateDialogWindow18MemorySubSub(DialogWindow18* w) {
    DialogQueue* p;
    DialogQueue* next;

    if (w->m84_delay == 0) {
        next = w->m8C_queue;
        while (next != NULL) {
            p = next;
            next = p->m0_pNext;
            freeHeap(p);
        }
        w->m8C_queue = NULL;
        w->m82_queueCount = 0;
    }
}

void dealocateDialogWindow18MemorySub(DialogWindow18* w) {
    w->m6C_autoClose = 0;
    w->m84_delay = 0;
    w->m10_flags &= 2;
    dealocateDialogWindow18MemorySubSub(w);
}

void dealocateDialogWindow18Memory(DialogWindow18* w) {
    dealocateDialogWindow18MemorySub(w);
    freeHeap(w->m28_perLineBuffer);
    freeHeap(w->m2C_inRamDialogTextImage);
}

s32 addDialogWindowsToOTSub2(DialogWindow18* w, u8* text) {
    DialogQueue* p;
    DialogQueue* node;

    p = w->m8C_queue;
    w->m82_queueCount++;
    traceNextAlloc(0x2A);
    node = allocateHeap(PC_HEAP_SIZED(sizeof(DialogQueue), 0x8), 2);
    node->m4_dialogPointer = text;
    node->m0_pNext = NULL;
    if (p == NULL) {
        w->m8C_queue = node;
        return w->m82_queueCount;
    }
    while (p->m0_pNext != NULL) {
        p = p->m0_pNext;
    }
    p->m0_pNext = node;
    return w->m82_queueCount;
}

s32 getWindowSubStructX(DialogWindow18* w) {
    return w->m4_x + w->m0_cursorX * 4;
}

s32 getWindowSubStructY(DialogWindow18* w) {
    s32 line;

    line = w->m2_cursorLine - w->m16_firstLine;
    if (line < 0) {
        line = w->mC_height - 1;
    }
    return w->m6_y + line * w->m14_lineHeight;
}

void setupMultiChoiceColor(DialogWindow18* w, u8 r, u8 g, u8 b) {
    s32 i;
    DialogWindow18Line* l;

    for (i = 0; i < w->mC_height; i++) {
        l = &w->m28_perLineBuffer[i];
        l->m0_sprt[1][1].r0 = r;
        l->m0_sprt[1][0].r0 = r;
        l->m0_sprt[0][1].r0 = r;
        l->m0_sprt[0][0].r0 = r;
        l->m0_sprt[1][1].g0 = g;
        l->m0_sprt[1][0].g0 = g;
        l->m0_sprt[0][1].g0 = g;
        l->m0_sprt[0][0].g0 = g;
        l->m0_sprt[1][1].b0 = b;
        l->m0_sprt[1][0].b0 = b;
        l->m0_sprt[0][1].b0 = b;
        l->m0_sprt[0][0].b0 = b;
    }
}

void setSelectedLineForMultiChoice(DialogWindow18* w, u8 line) {
    w->m6E_selectedLineForMultiChoice = line;
}

void clearSelectedLineForMultiChoice(DialogWindow18* w) {
    w->m6E_selectedLineForMultiChoice = 0xFF;
}

void updateAndRenderTextForDialogWindow(DialogWindow18* w, void* ot, s32 buf) {
    s32 i;
    s32 line;
    DialogQueue* q;
    u16 flags;

    if (!(w->m10_flags & 4)) {
        if (w->m82_queueCount == 0 || pcGameplayDialogStepSkip()) {
            /* Enhanced (xg-hbc.11): starting the queued page is a step too:
             * not on the 60 Hz field's off-tick frame (field/f_8007DCF8.c) */
            return;
        }
        q = w->m8C_queue;
        w->m1C_currentStringToPrint = q->m4_dialogPointer;
        if (pcObserveText) pcObserveText(w);
        pcGameplayDialogueText(w, w->m1C_currentStringToPrint, dialogFontVar0); /* timed or not */
        pcGameTextBegin(w, w->m1C_currentStringToPrint);
        w->m8C_queue = w->m8C_queue->m0_pNext;
        freeHeap(q);
        w->m82_queueCount--;
        w->m10_flags = (w->m10_flags & 2) | 0x24;
        if (w->m6A_savedCharsPerStep != 0) {
            w->m68_charsPerStep = w->m6A_savedCharsPerStep;
            w->m69_charsThisStep = w->m6A_savedCharsPerStep;
            w->m6A_savedCharsPerStep = 0;
        }
        w->m88_delayBetweenCharacters = 0;
        w->m86_currentDelayForNextCharacter = 0;
        w->m69_charsThisStep = w->m68_charsPerStep;
    }
    if (w->m10_flags & 0x100) {
        w->m69_charsThisStep = w->m68_charsPerStep * 3;
    } else {
        w->m69_charsThisStep = w->m68_charsPerStep;
    }
    flags = w->m10_flags;
    if (flags & 0x40) {
        if (!(flags & 8)) {
            w->m10_flags = (flags & ~0x40) | 0x20;
        }
    }
    if (w->m10_flags & 0x20) {
        w->m16_firstLine = 0;
        w->m18_lineCount = 0;
        w->m2_cursorLine = 0;
        w->m0_cursorX = 0;
        w->m28_perLineBuffer->m5C_vramY = w->mE_vramY;
        w->m28_perLineBuffer->m5E_clut = textSpriteMode0;
        w->m28_perLineBuffer->m5A_oddLine = 0;
        w->m28_perLineBuffer->m50_rect.y = w->mE_vramY;
        for (line = 0; line < w->mC_height; line++) {
            w->m28_perLineBuffer[line].m58_widthDiv4 = 0;
        }
        w->m10_flags &= ~0x21;
    }

    line = w->m16_firstLine;
    for (i = 0; i < w->mC_height; i++) {
        if (line >= w->mC_height) {
            line = 0;
        }
        setShadeTex(&w->m28_perLineBuffer[line].m0_sprt[buf][1], w->m6E_selectedLineForMultiChoice != i);
        if (w->m28_perLineBuffer[line].m58_widthDiv4 > 0x40 && !pcGameplayDialogHideLines()) {
            w->m28_perLineBuffer[line].m0_sprt[buf][1].v0 = w->m28_perLineBuffer[line].m5C_vramY;
            w->m28_perLineBuffer[line].m0_sprt[buf][1].clut = w->m28_perLineBuffer[line].m5E_clut;
            w->m28_perLineBuffer[line].m0_sprt[buf][1].y0 = w->m6_y + w->m14_lineHeight * i;
            w->m28_perLineBuffer[line].m0_sprt[buf][1].w = (w->m28_perLineBuffer[line].m58_widthDiv4 - 0x40) * 4;
            pcTextureClass(&w->m28_perLineBuffer[line].m0_sprt[buf][1], PC_TEX_TEXT);
            addSPRT(ot, &w->m28_perLineBuffer[line].m0_sprt[buf][1]);
        }
        line++;
    }
    AddPrim(ot, &w->m30_textTileDrawMode[1]);

    line = w->m16_firstLine;
    for (i = 0; i < w->mC_height; i++) {
        if (line >= w->mC_height) {
            line = 0;
        }
        setShadeTex(&w->m28_perLineBuffer[line].m0_sprt[buf][0], w->m6E_selectedLineForMultiChoice != i);
        if (w->m28_perLineBuffer[line].m58_widthDiv4 != 0 && !pcGameplayDialogHideLines()) {
            /* (Enhanced, xg-hbc.19.5: not on a 60 Hz field frame between whose next tick clears
             * the lines for the next page; pcGameplaySetDialogHideLines, field/f_8007DCF8.c) */
            w->m28_perLineBuffer[line].m0_sprt[buf][0].v0 = w->m28_perLineBuffer[line].m5C_vramY;
            w->m28_perLineBuffer[line].m0_sprt[buf][0].clut = w->m28_perLineBuffer[line].m5E_clut;
            w->m28_perLineBuffer[line].m0_sprt[buf][0].y0 = w->m6_y + w->m14_lineHeight * i;
            if (w->m28_perLineBuffer[line].m58_widthDiv4 > 0x40) {
                w->m28_perLineBuffer[line].m0_sprt[buf][0].w = 0x100;
            } else {
                w->m28_perLineBuffer[line].m0_sprt[buf][0].w = w->m28_perLineBuffer[line].m58_widthDiv4 * 4;
            }
            pcTextureClass(&w->m28_perLineBuffer[line].m0_sprt[buf][0], PC_TEX_TEXT);
            addSPRT(ot, &w->m28_perLineBuffer[line].m0_sprt[buf][0]);
        }
        line++;
    }

    if (pcGameplayDialogStepSkip()) {
        /* Enhanced (xg-hbc.11): the 60 Hz field's off-tick frame: drawn above,
         * not stepped, so dialogue prints and waits at the authored cadence */
    } else if (w->m84_delay != 0) {
        w->m84_delay--;
    } else if (w->m86_currentDelayForNextCharacter != 0) {
        w->m86_currentDelayForNextCharacter--;
    } else {
        w->m86_currentDelayForNextCharacter = w->m88_delayBetweenCharacters;
        if (!(w->m10_flags & 0x58)) {
            updateDialogTextImage(w);
            LoadImage(&w->m28_perLineBuffer[w->m2_cursorLine].m50_rect, w->m2C_inRamDialogTextImage);
            {
                /* The text speed settings (off by default): more of the same
                 * steps this frame, until the text stops, pauses or closes;
                 * never for timed or slow text (pcGameplayDialogueSteps). */
                s32 extra = pcGameplayDialogueSteps(w, pcPadHeldRemapped() & 0x20);
                while (extra-- > 0 && w->m84_delay == 0 && w->m88_delayBetweenCharacters == 0 &&
                       (w->m10_flags & 0x5C) == 4) {
                    updateDialogTextImage(w);
                    LoadImage(&w->m28_perLineBuffer[w->m2_cursorLine].m50_rect, w->m2C_inRamDialogTextImage);
                }
            }
        }
    }
    if (w->m84_delay != 0 && !pcGameplayDialogStepSkip()) {
        /* (Enhanced, xg-hbc.19.3: the timed wait counts native frames too; on the 60 Hz field's
         * frames between it ran down twice as fast and timed dialogue closed early) */
        if (--w->m84_delay == -1) {
            w->m10_flags &= ~0x10;
        }
    }
    if (!(w->m10_flags & 2)) {
        setXY0W(w->m48_textTile + buf, w->m4_x - 7, w->m6_y - 5);
        setWHW(w->m48_textTile + buf, ((s16)(w->mA_width1 | 1) * 4 + 13) | ((w->mC_height * w->m14_lineHeight + 10) << 16));
        addPrim(ot, w->m48_textTile + buf);
    }
    w->m10_flags &= ~0x100;
    AddPrim(ot, &w->m30_textTileDrawMode[0]);
}

s32 renderString(u8* text, u16* image, s16 width, s32 odd) {
    DialogWindow18* w = &renderStringWindow;

    w->mA_width1 = width;
    w->mC_height = 1;
    w->mA_width1 = w->mA_width1 | 1;
    w->m1C_currentStringToPrint = text;
    w->m8_widthTimes4 = w->mA_width1 * 4;
    w->m12_widthPadded = w->mA_width1 + 3;
    w->m68_charsPerStep = 1;
    w->m84_delay = 0;
    w->m6C_autoClose = 0;
    w->m6A_savedCharsPerStep = 0;
    w->m2C_inRamDialogTextImage = image;
    pcGlyphClearRAM(image, w->m12_widthPadded * 13 * sizeof(u16), 1u << (odd & 1));
    pcGameTextBegin(w, text);
    w->m10_flags = 0;
    w->m2_cursorLine = 0;
    w->m0_cursorX = 0;
    w->m69_charsThisStep = 100;
    w->m28_perLineBuffer = &renderStringLine;
    renderStringLine.m58_widthDiv4 = 0;
    renderStringLine.m5A_oddLine = odd & 1;
    updateDialogTextImage(w);
    return w->m28_perLineBuffer->m58_widthDiv4 * 4;
}

u16 getDialogFontCharacterWidth(u16 type, u16 id) {
    if (pcPadIsGlyph(type,id)) return 3;
    if (type == 0xFF && id >= 0xC0 && pcTextGlyph != NULL) return pcTextGlyphWidth;
    if (type == 0) {
        if ((s32)(id - dialogFontVar1) < dialogFontVar2) {
            return 2;
        } else {
            return 3;
        }
    } else if (type == dialogFontVar0 && id < dialogFontVar3) {
        return 2;
    } else {
        return 3;
    }
}

void printDialogCharacter(u16 type, u16 id, u16* out, s16 stride, s32 odd) {
    u16* glyph;
    s32 i;
    s32 ma;
    s32 mb;
    s32 mb2;
    s32 mc;
    s32 md;
    s32 me;
    s32 mf;
    s32 ta;
    s32 tb;
    u32 v;

    if (pcPadIsGlyph(type,id)) {
        pcPadPrintGlyph(out,stride,odd,id-0x80);
        return;
    }

    if ((u16)type == 0xFF && id >= 0xC0 && pcTextGlyph != NULL) {
        glyph = (u16*)pcTextGlyph; /* updateDialogTextImage found it */
    } else
    if ((u16)type == 0) {
        glyph = (u16*)(dialogFontVarPtr2 + (id - dialogFontVar1) * 22);
    } else if ((u16)type == 0xFF && id == 0xFF) {
        glyph = crossedBoxGlyph;
    } else {
        glyph = (u16*)(dialogFontVarPtr2 + id * 22 + dialogFontVar4 + ((u16)type - dialogFontVar0) * 0x1600);
    }

    i = 0;
    if (odd == 0) {
        out[0] &= 0xCCCC;
        out[1] &= 0xCCCC;
        out[2] &= 0xCCCC;
        out += stride;
        out[0] &= 0xCCCC;
        out[1] &= 0xCCCC;
        out[2] &= 0xCCCC;
        do {
            out[stride + 0] &= 0xCCCC;
            out[stride + 1] &= 0xCCCC;
            out[stride + 2] &= 0xCCCC;
            type = *glyph++;

            ma = (type & 0x80) ? 0x222 : 0;
            if (type & 0x40) ma |= 0x2220;
            if (type & 0x20) ma |= 0x2200;
            if (!(type & 0x10)) {
                v = ma;
            } else {
                v = ma | 0x2000;
            }
            out[-stride] |= v;
            mb = (type & 0x80) ? 0x212 : 0;
            out[stride] |= v;
            if (type & 0x40) mb |= 0x2120;
            mb2 = (type & 0x20) ? mb | 0x1200 : mb;
            v = out[0];
            if (type & 0x10) {
                out[0] = (u16)(v | 0x2000) | mb2;
            } else {
                out[0] = v | mb2;
            }

            ta = (type & 8) ? 0x222 : ((type & 0x10) ? 0x22 : ((type >> 4) & 2));
            mc = (type & 4) ? ta | 0x2220 : ta;
            if (type & 2) mc |= 0x2200;
            if (!(type & 1)) {
                v = mc;
            } else {
                v = mc | 0x2000;
            }
            out[-stride + 1] |= v;
            out[stride + 1] |= v;
            md = ((type >> 4) & 2);
            if (type & 0x10) md |= 0x21;
            if (type & 8) md |= 0x212;
            if (type & 4) md |= 0x2120;
            if (type & 2) md |= 0x1200;
            v = out[1];
            if (type & 1) {
                out[1] = (u16)(v | 0x2000) | md;
            } else {
                out[1] = v | md;
            }

            tb = (type & 0x8000) ? 0x222 : ((type & 1) ? 0x22 : (type & 2));
            me = (type & 0x4000) ? tb | 0x2220 : tb;
            if (type & 0x2000) me |= 0x2200;
            if (!(type & 0x1000)) {
                v = me;
            } else {
                v = me | 0x2000;
            }
            out[-stride + 2] |= v;
            out[stride + 2] |= v;
            mf = (type & 2);
            if (type & 1) mf |= 0x21;
            if (type & 0x8000) mf |= 0x212;
            if (type & 0x4000) mf |= 0x2120;
            if (type & 0x2000) mf |= 0x1200;
            v = out[2];
            if (type & 0x1000) {
                out[2] = (u16)(v | 0x2000) | mf;
            } else {
                out[2] = v | mf;
            }

            out += stride;
            i++;
        } while (i < 11);
    } else {
        out[0] &= 0x3333;
        out[1] &= 0x3333;
        out[2] &= 0x3333;
        out += stride;
        out[0] &= 0x3333;
        out[1] &= 0x3333;
        out[2] &= 0x3333;
        do {
            out[stride + 0] &= 0x3333;
            out[stride + 1] &= 0x3333;
            out[stride + 2] &= 0x3333;
            type = *glyph++;

            ma = (type & 0x80) ? 0x888 : 0;
            if (type & 0x40) ma |= 0x8880;
            if (type & 0x20) ma |= 0x8800;
            if (!(type & 0x10)) {
                v = ma;
            } else {
                v = ma | 0x8000;
            }
            out[-stride] |= v;
            mb = (type & 0x80) ? 0x848 : 0;
            out[stride] |= v;
            if (type & 0x40) mb |= 0x8480;
            mb2 = (type & 0x20) ? mb | 0x4800 : mb;
            v = out[0];
            if (type & 0x10) {
                out[0] = (u16)(v | 0x8000) | mb2;
            } else {
                out[0] = v | mb2;
            }

            ta = (type & 8) ? 0x888 : ((type & 0x10) ? 0x88 : ((type >> 2) & 8));
            mc = (type & 4) ? ta | 0x8880 : ta;
            if (type & 2) mc |= 0x8800;
            if (!(type & 1)) {
                v = mc;
            } else {
                v = mc | 0x8000;
            }
            out[-stride + 1] |= v;
            out[stride + 1] |= v;
            md = ((type >> 2) & 8);
            if (type & 0x10) md |= 0x84;
            if (type & 8) md |= 0x848;
            if (type & 4) md |= 0x8480;
            if (type & 2) md |= 0x4800;
            v = out[1];
            if (type & 1) {
                out[1] = (u16)(v | 0x8000) | md;
            } else {
                out[1] = v | md;
            }

            tb = (type & 0x8000) ? 0x888 : ((type & 1) ? 0x88 : ((type << 2) & 8));
            me = (type & 0x4000) ? tb | 0x8880 : tb;
            if (type & 0x2000) me |= 0x8800;
            if (!(type & 0x1000)) {
                v = me;
            } else {
                v = me | 0x8000;
            }
            out[-stride + 2] |= v;
            out[stride + 2] |= v;
            mf = ((type << 2) & 8);
            if (type & 1) mf |= 0x84;
            if (type & 0x8000) mf |= 0x848;
            if (type & 0x4000) mf |= 0x8480;
            if (type & 0x2000) mf |= 0x4800;
            v = out[2];
            if (type & 0x1000) {
                out[2] = (u16)(v | 0x8000) | mf;
            } else {
                out[2] = v | mf;
            }

            out += stride;
            i++;
        } while (i < 11);
    }
}
