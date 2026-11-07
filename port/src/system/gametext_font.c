/*
 * The dialogue fonts and the text between them (gametext.h): a font's codes
 * and glyphs, the glyphs a source brings (codes FF C0..FE xx, which no disc
 * uses: pcGameTextGlyph), and a text in one font's codes as another's.
 * Encoded glyph snapshots are game state; source fonts remain host-owned.
 * Unit-tested on its own (system/test/gametexttest.c).
 */

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pc_gametext.h"

#include "gametext.h"
#include "savestate.h"

/* ---- fonts ------------------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

int gtFontParse(const uint8_t* data, int size, GtFont* f) {
    memset(f, 0, sizeof(*f));
    if (data == NULL || size < 14) return 0;
    int at = rd16(data + 2);
    f->lead = rd16(data + 4);
    f->two = rd16(data + 6) / 22;
    f->narrow = rd16(data + 8);
    f->narrow2 = rd16(data + 10);
    f->first = rd16(data + 12);
    if (at < 14 || at >= size || f->first < 0x10 || f->lead <= f->first || f->lead > 0xFF ||
        f->two != f->lead - f->first) {
        return 0;
    }
    f->data = data;
    f->size = size;
    f->bits = data + at;
    f->count = (size - at) / 22;
    return f->count > f->two;
}

int gtFontDecode(const GtFont* f, const uint8_t* s, int* length) {
    int c = s[0];
    *length = 1;
    if (c < f->first) return -1;
    if (c < f->lead) return c - f->first;
    *length = 2;
    if (c == 0xFF && s[1] == 0xFF) return -1; /* the crossed box */
    int i = f->two + (c - f->lead) * 256 + s[1];
    return i < f->count ? i : -1;
}

int gtFontEncode(const GtFont* f, int i, uint8_t out[2]) {
    if (i < 0 || i >= f->count) return 0;
    if (i < f->two) {
        out[0] = (uint8_t)(f->first + i);
        return 1;
    }
    int lead = f->lead + (i - f->two) / 256;
    if (lead > 0xFE) return 0; /* FF is no page of glyphs */
    out[0] = (uint8_t)lead;
    out[1] = (uint8_t)((i - f->two) % 256);
    return 2;
}

int gtFontWide(const GtFont* f, int i) {
    if (i < f->two) return i >= f->narrow;
    return !(i - f->two < 256 && i - f->two < f->narrow2);
}

/* ---- the bank of glyphs sources bring ---------------------------------------------------- */

#define BANK_GLYPHS 2048 /* a font's slots; FF C0..FE xx code 63 x 240 */
static GtFont bank[GT_BANK_FONTS];

/* A queued string keeps its codes after the player changes its source.
 * Copy each distinct glyph and width once; encoded codes name these immutable
 * pixels, never a mutable font bank. The unused FF C0..FE xx space bounds it. */
#define SNAPSHOT_GLYPHS (63 * 240)
#define SNAPSHOT_HASH 32768
typedef struct {
    uint16_t rows[11];
    uint16_t width;
} GlyphSnapshot;
static GlyphSnapshot glyphSnapshots[SNAPSHOT_GLYPHS];
static uint32_t glyphSnapshotCount;
static uint16_t glyphSnapshotHash[SNAPSHOT_HASH]; /* index + 1, rebuilt on load */

static uint32_t snapshotHash(const GlyphSnapshot* g) {
    uint32_t h = UINT32_C(2166136261);
    for (int i = 0; i < 11; i++) {
        h = (h ^ (g->rows[i] & 255)) * UINT32_C(16777619);
        h = (h ^ (g->rows[i] >> 8)) * UINT32_C(16777619);
    }
    return (h ^ g->width) * UINT32_C(16777619);
}

static uint32_t snapshotBucket(const GlyphSnapshot* g) {
    uint32_t at = snapshotHash(g) & (SNAPSHOT_HASH - 1);
    while (glyphSnapshotHash[at] != 0 &&
           memcmp(&glyphSnapshots[glyphSnapshotHash[at] - 1], g, sizeof(*g)))
        at = (at + 1) & (SNAPSHOT_HASH - 1);
    return at;
}

void gtBankSetFont(int slot, const GtFont* font) {
    if (slot < 0 || slot >= GT_BANK_FONTS) return;
    if (font != NULL) bank[slot] = *font;
    else memset(&bank[slot], 0, sizeof(bank[slot]));
}

static int bankCode(int slot, int index, uint8_t out[3]) {
    if (slot < 0 || slot >= GT_BANK_FONTS || index < 0 || index >= BANK_GLYPHS) return 0;
    const GtFont* f = &bank[slot];
    if (f->data == NULL || index >= f->count) return 0;
    GlyphSnapshot glyph;
    memcpy(glyph.rows, f->bits + index * 22, sizeof(glyph.rows));
    glyph.width = (uint16_t)(gtFontWide(f, index) ? 3 : 2);
    uint32_t at = snapshotBucket(&glyph);
    int n = glyphSnapshotHash[at] - 1;
    if (n < 0) {
        if (glyphSnapshotCount >= SNAPSHOT_GLYPHS) return 0; /* caller keeps the disc's text */
        n = (int)glyphSnapshotCount++;
        glyphSnapshots[n] = glyph;
        glyphSnapshotHash[at] = (uint16_t)(n + 1);
    }
    out[0] = 0xFF;
    out[1] = (uint8_t)(0xC0 + n / 240);
    out[2] = (uint8_t)(0x10 + n % 240);
    return 3;
}

int gtBankEncodeGlyph(int slot, const GtFont* font, int index, uint8_t out[3]) {
    gtBankSetFont(slot, font);
    return bankCode(slot, index, out);
}

const u16* pcGameTextGlyph(const u8* s, u16* width) {
    if (s[0] != 0xFF || s[1] < 0xC0 || s[1] == 0xFF || s[2] < 0x10) return NULL;
    int n = (s[1] - 0xC0) * 240 + (s[2] - 0x10);
    if ((uint32_t)n >= glyphSnapshotCount) return NULL;
    *width = glyphSnapshots[n].width;
    return glyphSnapshots[n].rows;
}

void gametextFontStateIo(XgState* s) {
    size_t count = xgStateLen(s, "count", glyphSnapshotCount);
    if (count > SNAPSHOT_GLYPHS) count = 0;
    xgStateIo(s, "glyphs", glyphSnapshots, count * sizeof(glyphSnapshots[0]));
    if (xgStateLoading(s)) {
        glyphSnapshotCount = (uint32_t)count;
        memset(glyphSnapshotHash, 0, sizeof(glyphSnapshotHash));
        for (uint32_t i = 0; i < glyphSnapshotCount; i++)
            glyphSnapshotHash[snapshotBucket(&glyphSnapshots[i])] = (uint16_t)(i + 1);
    }
}

int16_t* gtFontEquivalents(const GtFont* from, const GtFont* to) {
    int16_t* eq = malloc(sizeof(int16_t) * (size_t)(from->count > 0 ? from->count : 1));
    if (eq == NULL) return NULL;
    for (int i = 0; i < from->count; i++) {
        eq[i] = -1;
        const uint8_t* g = from->bits + i * 22;
        int blank = 1;
        for (int k = 0; k < 22 && blank; k++) blank = g[k] == 0;
        /* a space: the first space of the same width (the fonts' spaces are blank glyphs) */
        for (int j = 0; j < to->count && blank; j++) {
            const uint8_t* h = to->bits + j * 22;
            int empty = 1;
            for (int k = 0; k < 22 && empty; k++) empty = h[k] == 0;
            if (empty && gtFontWide(from, i) == gtFontWide(to, j)) {
                eq[i] = (int16_t)j;
                break;
            }
        }
        for (int j = 0; j < to->count && !blank; j++) {
            if (!memcmp(g, to->bits + j * 22, 22) && gtFontWide(from, i) == gtFontWide(to, j)) {
                uint8_t code[2];
                if (gtFontEncode(to, j, code)) {
                    eq[i] = (int16_t)j;
                    break;
                }
            }
        }
    }
    return eq;
}

/* Bytes each escape (0F nn) takes, as updateDialogTextImage steps over them. */
static int escapeLength(int k) {
    static const int8_t length[16] = {3, 3, 3, 4, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    return k >= 0 && k < 16 ? length[k] : 0;
}

int gtTranscode(const uint8_t* src, int length, const GtFont* from, const int16_t* equiv, int bankSlot,
                const GtFont* to, uint8_t* out, int cap) {
    int i = 0, o = 0;
    while (i < length) {
        int c = src[i];
        if (o + 4 > cap) return 0;
        if (c == 0) {
            out[o++] = 0;
            return o;
        }
        if (c == 1 || c == 3) {
            out[o++] = (uint8_t)c;
            i++;
        } else if (c == 2) {
            out[o++] = 2;
            i++;
            if (i < length && src[i] == 1) out[o++] = src[i++];
        } else if (c == 0x0F) {
            int n = i + 1 < length ? escapeLength(src[i + 1]) : 0;
            if (n == 0 || i + n > length || o + n > cap) return 0;
            memcpy(out + o, src + i, (size_t)n);
            o += n;
            i += n;
        } else if (c < 0x10) {
            return 0; /* no code the printer knows */
        } else if (c == 0xFF && i + 1 < length && src[i + 1] == 0xFF) {
            out[o++] = 0xFF;
            out[o++] = 0xFF;
            i += 2;
        } else {
            int n, g = gtFontDecode(from, src + i, &n);
            if (g < 0 || i + n > length) return 0;
            i += n;
            int t = equiv != NULL ? equiv[g] : (from == to ? g : -1);
            if (t >= 0) {
                o += gtFontEncode(to, t, out + o);
            } else {
                int k = bankCode(bankSlot, g, out + o);
                if (k == 0) return 0;
                o += k;
            }
        }
    }
    if (o + 1 > cap) return 0;
    out[o++] = 0; /* a text cut by its table's end */
    return o;
}


/* ---- the proportional print (a pack's opt-in: print=proportional) ------------------------- */

/* A glyph's columns: 11 rows, bits 7..0 the columns 0..7, bits 15..12 the
 * columns 8..11 (the printer's order: printDialogCharacter). */
static int glyphColumns(const uint8_t* g, int row) {
    int v = rd16(g + row * 2);
    return (v >> 7 & 1) | (v >> 5 & 2) | (v >> 3 & 4) | (v >> 1 & 8) | (v << 1 & 16) | (v << 3 & 32) |
           (v << 5 & 64) | (v << 7 & 128) | (v >> 7 & 256) | (v >> 5 & 512) | (v >> 3 & 1024) | (v >> 1 & 2048);
}

int gtGlyphInk(const uint16_t* glyph, int* first, int* last) {
    const uint8_t* g = (const uint8_t*)glyph;
    int mask = 0;
    for (int r = 0; r < 11; r++) mask |= glyphColumns(g, r);
    if (mask == 0) return 0;
    int a = 0, b = 11;
    while (!(mask >> a & 1)) a++;
    while (!(mask >> b & 1)) b--;
    *first = a;
    *last = b;
    return 1;
}

int gtPenGlyph(GtPen* pen, const uint16_t* glyph, int units, int spacing, int space, int* first, int* last) {
    if (glyph == NULL || !gtGlyphInk(glyph, first, last)) {
        /* a blank: a line's leading ones keep their width (a choice's cursor room) */
        pen->px += pen->ink ? (units >= 3 ? space * 3 / 2 : space) : units * 4;
        if (pen->end < pen->px) pen->end = pen->px;
        return -1;
    }
    int x = pen->px;
    int right = x + (*last - *first) + 2; /* the right outline */
    pen->end = right + 1;
    pen->px = right + spacing - 1; /* spacing 1: the outlines overlap */
    pen->ink = 1;
    return x;
}

static void orPixel(uint16_t* image, int stride, int odd, int x, int y, int v) {
    if (x < 0 || x >= stride * 4 || y < 0 || y > 12) return;
    image[y * stride + x / 4] |= (uint16_t)(v << ((x & 3) * 4 + odd * 2));
}

void gtClearColumns(uint16_t* image, int stride, int odd, int from, int to) {
    if (from < 0) from = 0;
    if (to >= stride * 4) to = stride * 4 - 1;
    for (int x = from; x <= to; x++) {
        uint16_t keep = (uint16_t)~(3 << ((x & 3) * 4 + odd * 2));
        for (int y = 0; y < 13; y++) image[y * stride + x / 4] &= keep;
    }
}

void gtDrawGlyph(uint16_t* image, int stride, int odd, int x, const uint16_t* glyph, int first, int last,
                 uint8_t* indices) {
    const uint8_t* g = (const uint8_t*)glyph;
    uint8_t local[14 * 13];
    int width = last - first + 3;
    if (first < 0 || last > 11 || first > last) return;
    if (!indices) indices = local;
    memset(indices, 0, (size_t)width * 13);
    odd &= 1;
    for (int r = 0; r < 11; r++) {
        int cols = glyphColumns(g, r);
        for (int c = first; c <= last; c++) {
            if (!(cols >> c & 1)) continue;
            int px = 1 + (c - first), py = r + 1;
            /* the printer's look: the body 1, its eight neighbours 2, or'ed (3 shows as the body) */
            indices[py * width + px] |= 1;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx || dy) indices[(py + dy) * width + px + dx] |= 2;
                }
            }
        }
    }
    for (int y = 0; y < 13; y++) for (int col = 0; col < width; col++)
        orPixel(image, stride, odd, x + col, y, indices[y * width + col]);
}
