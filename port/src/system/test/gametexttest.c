/*
 * The game's text from another source (system/gametext.h) without the game:
 * fonts, codes, the glyphs a source brings, text between two fonts, tables,
 * fingerprints, pack text (tags) and packs on disk; given the retail images
 * (bin/, as tools/port_tests.py passes them), a pack of the US discs' text
 * written and read back must give every text's bytes exactly.
 *
 *   gametexttest DIR [US-DISC1 [JP-DISC1]]
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_gametext.h"
#include "system/gametext.h"

/* What cd/verify.c and cd/swap.c reach for (the drive is not here). */
int cdChangeDisc(const char* p) { (void)p; return 0; }
int cdInsertDisc(const char* p) { (void)p; return 0; }
double hwNow(void) { return 0; }
void hwScheduleAt(double when, void (*callback)(void*), void* arg) { (void)when; (void)callback; (void)arg; }

/* What gametext_picture.c reaches for in gametext.c: the running release's
 * images (set by pictureExport), no source, no game heap. */
static GtImage* retailImages[2];
GtImage* gtRetailImage(int d) { return d == 1 || d == 2 ? retailImages[d - 1] : NULL; }
int gtCurrentDisc(void) { return 1; }
unsigned gtGeneration(void) { return 0; }
int gameTextPictures(int category) { (void)category; return 0; }
GtImage* gtCategoryImage(int category, int disc) { (void)category; (void)disc; return NULL; }
GtPack* gtCategoryPack(int category) { (void)category; return NULL; }
void* allocateHeap(int size, int flags) { (void)flags; return malloc((size_t)size); }

static int checks, failed;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++checks;                                                             \
        if (!(c)) {                                                           \
            ++failed;                                                         \
            fprintf(stderr, "gametexttest: %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

/* A font: glyph i's 11 rows are pattern(i) (0: blank). */
static uint8_t* makeFont(int first, int lead, int narrow, int narrow2, int count, const int* pattern, int* size) {
    int two = lead - first;
    *size = 14 + 22 * count;
    uint8_t* f = calloc(1, (size_t)*size);
    uint16_t h[7] = {(uint16_t)count, 14, (uint16_t)lead, (uint16_t)(two * 22), (uint16_t)narrow, (uint16_t)narrow2,
                     (uint16_t)first};
    memcpy(f, h, sizeof(h));
    for (int i = 0; i < count; i++) {
        for (int r = 0; r < 11; r++) {
            uint16_t v = pattern[i] ? (uint16_t)(pattern[i] * 31 + r * 7) : 0;
            memcpy(f + 14 + i * 22 + r * 2, &v, 2);
        }
    }
    return f;
}

static void fonts(void) {
    /* A ("the running font"): 4 one-byte glyphs (2 narrow), lead 0x14, 6 two-byte. */
    const int pa[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    /* B ("the source's"): 2 one-byte (1 narrow), lead 0x12; shares A's 2 (narrow? no: wide), 5, 9, and a blank. */
    const int pb[7] = {11, 3, 0, 5, 12, 9, 13};
    int sa, sb;
    uint8_t* da = makeFont(0x10, 0x14, 2, 0, 10, pa, &sa);
    uint8_t* db = makeFont(0x10, 0x12, 1, 1, 7, pb, &sb);
    GtFont a, b;
    CHECK(gtFontParse(da, sa, &a) && a.count == 10 && a.two == 4);
    CHECK(gtFontParse(db, sb, &b) && b.count == 7 && b.two == 2);
    uint8_t code[2];
    for (int i = 0; i < a.count; i++) {
        int n = gtFontEncode(&a, i, code), len;
        CHECK(n > 0 && gtFontDecode(&a, code, &len) == i && len == n);
    }
    uint8_t box[2] = {0xFF, 0xFF};
    int len;
    CHECK(gtFontDecode(&a, box, &len) == -1);
    CHECK(gtFontWide(&a, 1) == 0 && gtFontWide(&a, 2) == 1 && gtFontWide(&b, 2) == 0 && gtFontWide(&b, 3) == 1);

    /* B's glyphs in A: 1 (pattern 3, wide) = A 2 (wide); 3 (5) = A 4; 5 (9) = A 8; the blank (a narrow
     * space) has no blank of its width in A. */
    int16_t* eq = gtFontEquivalents(&b, &a);
    CHECK(eq != NULL && eq[1] == 2 && eq[3] == 4 && eq[5] == 8 && eq[0] == -1 && eq[2] == -1 && eq[4] == -1);

    /* A text of B's codes: controls copied, glyphs mapped or brought along. */
    gtBankSetFont(GT_BANK_OTHER, &b);
    const uint8_t src[] = {0x0F, 0x05, 0x00, 0x01, 0x11, 0x12, 0x01, 0x02, 0x01, 0x10, 0x0F, 0x03, 0x16, 0x07,
                           0xFF, 0xFF, 0x12, 0x03, 0x03, 0x00, 0xEE};
    uint8_t out[64];
    int n = gtTranscode(src, (int)sizeof(src), &b, eq, GT_BANK_OTHER, &a, out, sizeof(out));
    const uint8_t want[] = {0x0F, 0x05, 0x00, 0x01, 0x12, 0x14, 0x00, 0x02, 0x01, 0xFF, 0xC0, 0x10, 0x0F, 0x03,
                            0x16, 0x07, 0xFF, 0xFF, 0x14, 0x04, 0x03, 0x00};
    CHECK(n == (int)sizeof(want) && !memcmp(out, want, sizeof(want)));
    /* the brought glyph: B's glyph 0, narrow */
    u16 width = 0;
    const u16* g = pcGameTextGlyph(out + 9, &width);
    CHECK(g != NULL && !memcmp(g, b.bits, 22) && width == 2);
    uint8_t notBank[3] = {0xFF, 0x85, 0x20};
    CHECK(pcGameTextGlyph(notBank, &width) == NULL);
    CHECK(pcGameTextGlyph(box, &width) == NULL);
    /* what the printer would not take */
    const uint8_t bad1[] = {0x05, 0x00};
    const uint8_t bad2[] = {0x0F, 0x10, 0x00, 0x00};
    CHECK(gtTranscode(bad1, 2, &b, eq, GT_BANK_OTHER, &a, out, sizeof(out)) == 0);
    CHECK(gtTranscode(bad2, 4, &b, eq, GT_BANK_OTHER, &a, out, sizeof(out)) == 0);

    /* Pack text: tags and characters (A's characters: 'x' 'y' at 0, 1; B brings 'z'). */
    uint16_t ua[10] = {'x', 'y', 0x3042, 0, 0x4E00, 0, 0, 0, 0, 0};
    uint16_t ub[7] = {'z', 0, 0, 0, 0, 0, 0};
    GtEncode enc = {&a, ua, 10, &b, ub, 7, GT_BANK_OTHER, 1};
    n = gtPackEncode("<name 0>\nxあ<wait>\ny<delay 20><str 22 7><ref><close>z<g:9><box><lt>", &enc, out, sizeof(out));
    const uint8_t want2[] = {0x0F, 0x05, 0x00, 0x01, 0x10, 0x12, 0x02, 0x01, 0x11, 0x0F, 0x00, 0x14, 0x0F,
                             0x03, 0x16, 0x07, 0x0F, 0x04, 0x03, 0xFF, 0xC0, 0x10, 0x14, 0x05, 0xFF, 0xFF};
    CHECK(n == 0); /* <lt>: A has no '<' */
    n = gtPackEncode("<name 0>\nxあ<wait>\ny<delay 20><str 22 7><ref><close>z<g:9><box>", &enc, out, sizeof(out));
    CHECK(n == (int)sizeof(want2) + 1 && !memcmp(out, want2, sizeof(want2)) && out[n - 1] == 0);
    CHECK(gtPackEncode("<nonsense 3>", &enc, out, sizeof(out)) == 0);
    CHECK(gtPackEncode("w", &enc, out, sizeof(out)) == 0); /* no font has it */
    free(eq);
    free(da);
    free(db);
    gtBankSetFont(GT_BANK_OTHER, NULL);
}

/* Encoded text may still be queued when the source bank changes or closes. */
static void fontSnapshots(void) {
    const int pa[6] = {31, 32, 33, 34, 35, 36};
    const int pb[6] = {41, 42, 43, 44, 45, 46};
    int sa, sb;
    uint8_t* da = makeFont(0x10, 0x12, 1, 0, 6, pa, &sa);
    uint8_t* db = makeFont(0x10, 0x12, 0, 0, 6, pb, &sb);
    GtFont a, b;
    CHECK(gtFontParse(da, sa, &a) && gtFontParse(db, sb, &b));
    int16_t missing[6] = {-1, -1, -1, -1, -1, -1};
    uint8_t src[] = {0x10, 0}, old[16], next[16], again[16];
    uint16_t oldPixels[11], newPixels[11], width = 0;
    memcpy(oldPixels, a.bits, sizeof(oldPixels));
    memcpy(newPixels, b.bits, sizeof(newPixels));
    int slot = GT_BANK_PACK + GT_DIALOGUE;
    gtBankSetFont(slot, &a);
    int n = gtTranscode(src, sizeof(src), &a, missing, slot, &a, old, sizeof(old));
    CHECK(n == 4);
    gtBankSetFont(slot, &b);
    CHECK(gtTranscode(src, sizeof(src), &b, missing, slot, &b, next, sizeof(next)) == n);
    CHECK(memcmp(old, next, (size_t)n) != 0);
    const u16* glyph = pcGameTextGlyph(old, &width);
    CHECK(glyph != NULL && width == 2 && !memcmp(glyph, oldPixels, sizeof(oldPixels)));
    glyph = pcGameTextGlyph(next, &width);
    CHECK(glyph != NULL && width == 3 && !memcmp(glyph, newPixels, sizeof(newPixels)));
    const uint16_t map[6] = {'x', 0, 0, 0, 0, 0};
    GtEncode enc = {&a, NULL, 0, &a, map, 6, slot, 0, 1, missing};
    CHECK(gtPackEncode("x", &enc, again, sizeof(again)) == n && !memcmp(old, again, (size_t)n));
    enc.bankFont = &b;
    CHECK(gtPackEncode("x", &enc, again, sizeof(again)) == n && !memcmp(next, again, (size_t)n));
    glyph = pcGameTextGlyph(old, &width);
    CHECK(glyph != NULL && width == 2 && !memcmp(glyph, oldPixels, sizeof(oldPixels)));
    GtFont wide = a;
    wide.narrow = 0; /* identical pixels with a different advance are distinct */
    CHECK(gtBankEncodeGlyph(slot, &wide, 0, again) == 3 && memcmp(old, again, 3));
    glyph = pcGameTextGlyph(again, &width);
    CHECK(glyph != NULL && width == 3 && !memcmp(glyph, oldPixels, sizeof(oldPixels)));
    CHECK(gtBankEncodeGlyph(GT_BANK_IMAGE, &a, 0, again) == 3 && !memcmp(old, again, 3));
    gtBankSetFont(GT_BANK_IMAGE, NULL);
    gtBankSetFont(slot, &a);
    CHECK(gtTranscode(src, sizeof(src), &a, missing, slot, &a, again, sizeof(again)) == n &&
          !memcmp(old, again, (size_t)n));
    gtBankSetFont(slot, NULL);
    free(da); free(db);
    glyph = pcGameTextGlyph(old, &width);
    CHECK(glyph != NULL && width == 2 && !memcmp(glyph, oldPixels, sizeof(oldPixels)));
    glyph = pcGameTextGlyph(next, &width);
    CHECK(glyph != NULL && width == 3 && !memcmp(glyph, newPixels, sizeof(newPixels)));
}

static void snapshotCapacity(void) {
    const int patterns[6] = {0, 0, 0, 0, 0, 0};
    int size;
    uint8_t* data = makeFont(0x10, 0x12, 1, 0, 6, patterns, &size);
    GtFont font;
    CHECK(gtFontParse(data, size, &font));
    uint16_t rows[11] = {0}, firstRows[11];
    uint8_t first[16], code[16];
    int i;
    for (i = 0; i < 16000; i++) {
        rows[0] = (uint16_t)i; rows[1] = (uint16_t)~i; rows[2] = 0x5AC0;
        memcpy(data + 14, rows, 22);
        if (!gtBankEncodeGlyph(GT_BANK_IMAGE, &font, 0, code)) break;
        if (!i) { memcpy(first, code, 3); memcpy(firstRows, rows, 22); }
    }
    CHECK(i > 10000 && i < 16000);
    const uint16_t map[6] = {'x', 0, 0, 0, 0, 0};
    GtEncode enc = {&font, NULL, 0, &font, map, 6, GT_BANK_IMAGE, 0, 1, NULL};
    CHECK(gtPackEncode("x", &enc, code, sizeof(code)) == 0);
    uint8_t src[2] = {0x10, 0};
    int16_t missing[6] = {-1, -1, -1, -1, -1, -1};
    CHECK(gtTranscode(src, sizeof(src), &font, missing, GT_BANK_IMAGE, &font, code, sizeof(code)) == 0);
    uint16_t width = 0;
    const u16* glyph = pcGameTextGlyph(first, &width);
    CHECK(glyph != NULL && width == 2 && !memcmp(glyph, firstRows, 22));
    memcpy(data + 14, firstRows, 22);
    CHECK(gtBankEncodeGlyph(GT_BANK_IMAGE, &font, 0, code) == 3 && !memcmp(code, first, 3));
    gtBankSetFont(GT_BANK_IMAGE, NULL);
    free(data);
}

static void tables(void) {
    /* count-1 = 3; offsets 12, 12 (an empty entry), 14, 17; texts */
    const uint8_t t[] = {3, 0, 0, 0, 12, 0, 12, 0, 14, 0, 17, 0, 0x20, 0, 0x21, 0x22, 0, 0x0F, 0x05, 0, 0};
    GtTable tab;
    CHECK(gtTableParse(t, (int)sizeof(t), &tab) && tab.count == 4);
    int len;
    const uint8_t* e = gtTableEntry(&tab, 2, &len);
    CHECK(e == t + 14 && len == (int)sizeof(t) - 14);
    CHECK(gtFingerprint(t, (int)sizeof(t)) == gtFingerprint(t, -1));
    const uint8_t back[] = {1, 0, 0, 0, 10, 0, 8, 0, 0, 0, 0x20, 0};
    CHECK(!gtTableParse(back, (int)sizeof(back), &tab)); /* an offset going back */
}

static void packs(const char* dir) {
    char pdir[4096], path[4096];
    snprintf(pdir, sizeof(pdir), "%s/packs", dir);
    SDL_CreateDirectory(pdir);
    snprintf(path, sizeof(path), "%s/test", pdir);
    SDL_CreateDirectory(path);
    SDL_setenv_unsafe("XG_PACK_DIR", pdir, 1);
    snprintf(path, sizeof(path), "%s/test/pack.ini", pdir);
    FILE* f = fopen(path, "wb");
    fputs("name=Test pack\nlanguage=it\nrelease=US\nfont=us\n", f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/test/field.txt", pdir);
    f = fopen(path, "wb");
    fputs("@@ field/1/14/1 w=30 h=3\nRiga uno\r\n<at>@ due<wait>\n@@ field/1/14/0 w=20 h=2\nZero\n", f);
    fclose(f);
    char why[300];
    GtPack* p = gtPackLoad("test", why, sizeof(why));
    CHECK(p != NULL && !strcmp(gtPackName(p), "Test pack") && !strcmp(gtPackRegion(p), "US"));
    int w = 0, h = 0;
    const char* text = gtPackFind(p, "field/1/14/1", &w, &h);
    CHECK(text && !strcmp(text, "Riga uno\n<at>@ due<wait>") && w == 30 && h == 3);
    CHECK(gtPackFind(p, "field/1/14/0", NULL, NULL) && !strcmp(gtPackFind(p, "field/1/14/0", NULL, NULL), "Zero"));
    CHECK(gtPackFind(p, "field/1/14/2", NULL, NULL) == NULL);
    char ids[4][64], names[4][128];
    int listed = gtPackList(ids, names, 4), found = 0;
    for (int i = 0; i < listed; i++) found |= !strcmp(ids[i], "test") && !strcmp(names[i], "Test pack");
    CHECK(found);
    CHECK(gtPackLoad("../etc", why, sizeof(why)) == NULL);
    GtLayout layout;
    gtPackLayout(p, &layout); /* no opt-in: the game's look */
    CHECK(!layout.proportional && layout.windows == GT_WINDOWS_NATIVE && layout.spacing == 2 && layout.space == 4);
    gtLayoutParse("print=proportional\nwindows=grow\nspacing=9\nspace=3\n", &layout);
    CHECK(layout.proportional && layout.windows == GT_WINDOWS_GROW && layout.spacing == 3 && layout.space == 3);
    gtLayoutParse("print=native\r\nwindows=pack\r\n", &layout);
    CHECK(!layout.proportional && layout.windows == GT_WINDOWS_PACK);
}

/* A text image's font in a pack (font=pack): characters by the base font's
 * pictures, font.map, and text through the pack's own glyphs. */
static void ownFont(const char* dir) {
    const int pa[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int sa;
    uint8_t* da = makeFont(0x10, 0x14, 2, 0, 10, pa, &sa);
    uint8_t* dp = malloc((size_t)sa);
    memcpy(dp, da, (size_t)sa);
    dp[14 + 1 * 22 + 4] ^= 0x01;                       /* glyph 1 redrawn a little */
    memset(dp + 14 + 3 * 22, 0xFF, 22);                 /* glyph 3 a new picture */
    GtFont a, p;
    CHECK(gtFontParse(da, sa, &a) && gtFontParse(dp, sa, &p));
    uint16_t ua[10] = {'a', 'b', 'c', 'd', 'e', 0, 0, 0, 0, 0};
    uint16_t* uni = gtDerivedUnicode(&p, &a, ua, 10);
    CHECK(uni != NULL && uni[0] == 'a' && uni[1] == 'b' && uni[2] == 'c' && uni[3] == 0 && uni[4] == 'e');
    free(uni);

    char pdir[4096], path[4096];
    snprintf(pdir, sizeof(pdir), "%s/packs/own", dir);
    SDL_CreateDirectory(pdir);
    snprintf(path, sizeof(path), "%s/pack.ini", pdir);
    FILE* f = fopen(path, "wb");
    fputs("name=Own font\nrelease=US\nfont=pack\n", f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/font.bin", pdir);
    f = fopen(path, "wb");
    fwrite(dp, 1, (size_t)sa, f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/font.map", pdir);
    f = fopen(path, "wb");
    fputs("# glyphs\n0 a\n1 b\n2 U+0063\r\n3 \xC3\xA9\n# 4\n", f);
    fclose(f);
    char why[300];
    GtPack* pack = gtPackLoad("own", why, sizeof(why));
    const uint16_t* map = NULL;
    const GtFont* own = pack ? gtPackOwnFont(pack, &map) : NULL;
    CHECK(own != NULL && own->count == 10 && map[0] == 'a' && map[2] == 'c' && map[3] == 0xE9 && map[4] == 0);
    int w = 23, h = 4;
    CHECK(gtPackFind(pack, "field/1/14/0", &w, &h) == NULL && w == 23 && h == 4);
    if (own != NULL) {
        int16_t* eq = gtFontEquivalents(own, &a);
        GtEncode enc = {&a, ua, 10, own, map, own->count, GT_BANK_PACK, 0, 1, eq};
        uint8_t out[64];
        int n = gtPackEncode("ab\xC3\xA9<g:4>", &enc, out, sizeof(out));
        /* a: the same picture as the running font's; b and the new glyph from the pack's font; glyph 4 the running's */
        CHECK(n == 10 && out[0] == 0x10 && out[7] == 0x14 && out[8] == 0 && out[9] == 0);
        u16 width = 0;
        const u16* glyph = pcGameTextGlyph(out + 1, &width);
        CHECK(glyph != NULL && !memcmp(glyph, own->bits + 22, 22) && width == 2);
        glyph = pcGameTextGlyph(out + 4, &width);
        CHECK(glyph != NULL && !memcmp(glyph, own->bits + 3 * 22, 22) && width == 3);
        free(eq);
    }
    free(da);
    free(dp);
}

/* The proportional print: a glyph's ink, the pen, the printer's outline. */
static unsigned pixel(const uint16_t* image, int stride, int odd, int x, int y) {
    return (unsigned)(image[y * stride + x / 4] >> ((x & 3) * 4 + odd * 2)) & 3;
}

static void proportionalPrint(void) {
    uint16_t blank[11] = {0}, dot[11] = {0}, bar[11] = {0};
    int first = -1, last = -1;
    CHECK(gtGlyphInk(blank, &first, &last) == 0);
    dot[0] = 0x40;   /* column 1, row 0 */
    bar[5] = 0x2007; /* columns 5..7 and 10 */
    CHECK(gtGlyphInk(dot, &first, &last) && first == 1 && last == 1);
    CHECK(gtGlyphInk(bar, &first, &last) && first == 5 && last == 10);

    GtPen pen = {0, 0, 0};
    CHECK(gtPenGlyph(&pen, blank, 2, 2, 4, &first, &last) == -1 && pen.px == 8); /* a leading blank: its cell */
    CHECK(gtPenGlyph(&pen, dot, 2, 2, 4, &first, &last) == 8 && pen.end == 11 && pen.px == 11);
    CHECK(gtPenGlyph(&pen, blank, 2, 2, 4, &first, &last) == -1 && pen.px == 15); /* then the pack's space */
    CHECK(gtPenGlyph(&pen, bar, 3, 1, 4, &first, &last) == 15 && pen.end == 23 && pen.px == 22);

    uint16_t image[13 * 8];
    for (int i = 0; i < 13 * 8; i++) image[i] = 0xFFFF;
    gtClearColumns(image, 8, 1, 0, 31);
    CHECK(image[0] == 0x3333 && image[13 * 8 - 1] == 0x3333); /* the even line's plane kept */
    gtClearColumns(image, 8, 0, 0, 31);
    CHECK(image[0] == 0 && image[13 * 8 - 1] == 0);
    gtDrawGlyph(image, 8, 1, 4, dot, 1, 1, NULL); /* body at (5, 1) */
    CHECK(pixel(image, 8, 1, 5, 1) == 1 && pixel(image, 8, 1, 4, 0) == 2 && pixel(image, 8, 1, 6, 2) == 2 &&
          pixel(image, 8, 1, 4, 1) == 2 && pixel(image, 8, 1, 7, 1) == 0 && pixel(image, 8, 0, 5, 1) == 0);
    gtDrawGlyph(image, 8, 1, 5, dot, 1, 1, NULL); /* a body next to it: 1 | 2 */
    CHECK(pixel(image, 8, 1, 5, 1) == 3 && pixel(image, 8, 1, 6, 1) == 3);
}

/* A pack of the US disc's text, read back: every text the same bytes. */
/* A disc 1 image's text as a pack and back: every 7th field's texts the same
 * bytes (US: the font's own characters; JP: also its kanji, all identified). */
static int roundTrip(const char* dir, const char* discPath) {
    char why[512];
    GtImage* us = gtImageOpen(discPath, why, sizeof(why));
    if (us == NULL) {
        printf("gametexttest: no image (%s): the round trip is skipped\n", why);
        return 0;
    }
    const char* region = gtImageRegion(us);
    /* the pictures' members: LZSS streams that end inside a group, read with what follows them */
    static const int members[][3] = {{0x10, 1, 2}, {0x10, 1, 3}, {0x0C, 3, 1}, {0x0C, 3, 2}};
    for (int i = 0; i < 4; i++) {
        int n = 0, out = 0;
        uint8_t* stored = gtImageMember(us, members[i][0], members[i][1], members[i][2], &n);
        uint8_t* data = stored ? gtLzss(stored, n + GT_MEMBER_SLACK, &out) : NULL;
        CHECK(stored != NULL && data != NULL && out > 1000 && (uint32_t)out == (uint32_t)(stored[0] | stored[1] << 8 | stored[2] << 16));
        free(stored);
        free(data);
    }
    char pdir[4096];
    char id[8];
    snprintf(id, sizeof(id), "%s", !strcmp(region, "JP") ? "jp" : "us");
    snprintf(pdir, sizeof(pdir), "%s/packs/%s", dir, id);
    CHECK(gameTextExport(us, NULL, pdir, region, NULL, NULL, why, sizeof(why)));
    GtPack* pack = gtPackLoad(id, why, sizeof(why));
    CHECK(pack != NULL);
    int fontSize, uniCount;
    uint8_t* fontData = gtImageFont(us, &fontSize);
    GtFont font;
    CHECK(fontData && gtFontParse(fontData, fontSize, &font));
    GtEncode enc = {&font, gtFontUnicode(region, &uniCount), 0, NULL, NULL, 0, 0, 1};
    enc.toUniCount = uniCount;
    GtLayout layout;
    gtPackLayout(pack, &layout);
    CHECK(layout.windows == GT_WINDOWS_NATIVE);
    char imageId[32];
    snprintf(imageId, sizeof(imageId), "%s-image", id);
    snprintf(pdir, sizeof(pdir), "%s/packs/%s", dir, imageId);
    CHECK(gameTextExportImage(us, NULL, &font, enc.toUni, enc.toUniCount, pdir, region,
                              NULL, NULL, why, sizeof(why)));
    GtPack* imagePack = gtPackLoad(imageId, why, sizeof(why));
    CHECK(imagePack != NULL);
    gtPackLayout(imagePack, &layout);
    CHECK(layout.windows == GT_WINDOWS_SOURCE && !layout.proportional);
    const GtFont* own = gtPackOwnFont(imagePack, NULL);
    CHECK(own != NULL && own->count == font.count &&
          !memcmp(own->bits, font.bits, (size_t)font.count * 22));
    int texts = 0, same = 0;
    for (int n = 0; n < 730 && pack != NULL; n += 7) {
        int size;
        uint8_t* blob = gtFieldSection(us, n, 7, &size);
        GtTable t;
        if (blob == NULL || !gtTableParse(blob, size, &t)) {
            free(blob);
            continue;
        }
        for (int i = 0; i < t.count; i++) {
            char key[64];
            snprintf(key, sizeof(key), "field/1/%d/%d", n, i);
            const char* text = gtPackFind(pack, key, NULL, NULL);
            int w = -1, h = -1;
            const uint8_t* wh = blob + 4 + 2 * t.count;
            CHECK(gtPackFind(imagePack, key, &w, &h) != NULL && w == wh[2 * i] && h == wh[2 * i + 1]);
            int length;
            const uint8_t* s = gtTableEntry(&t, i, &length);
            uint8_t out[4096], ref[4096];
            int a = text ? gtPackEncode(text, &enc, out, sizeof(out)) : 0;
            int b = gtTranscode(s, length, &font, NULL, 0, &font, ref, sizeof(ref)); /* the original up to its end */
            texts++;
            int ok = a > 0 && a == b && !memcmp(out, ref, (size_t)a);
            if (!ok && a > 0 && a == b) {
                /* a character the font has twice with the same picture (JP: ) - . 。) comes back as its first */
                ok = 1;
                for (int k = 0, la, lb; k < a && ok; k += la) {
                    int ga = gtFontDecode(&font, out + k, &la), gb = gtFontDecode(&font, ref + k, &lb);
                    ok = la == lb && (ga == gb ? !memcmp(out + k, ref + k, (size_t)la)
                                               : ga >= 0 && gb >= 0 && !memcmp(font.bits + ga * 22, font.bits + gb * 22, 22));
                }
            }
            same += ok;
        }
        free(blob);
    }
    printf("gametexttest: %s disc 1 round trip: %d of %d field texts the same\n", region, same, texts);
    CHECK(texts > 1000 && same == texts);
    free(fontData);
    gtImageClose(us);
    return 1;
}

static void pictureSnapshots(void) {
    uint8_t sprites[40] = {1, 0, 0, 0, 8, 0, 0, 0, 1};
    sprites[16] = 12; sprites[18] = 11;
    uint8_t changed[40];
    memcpy(changed, sprites, sizeof(changed));
    changed[20] = 7;
    int size = 0, restoredSize = 0;
    uint8_t* file = gtPictureSnapshot(sprites, sizeof(sprites), changed, sizeof(changed), GT_PICTURE_SPRITES, &size);
    CHECK(file != NULL && size == 72);
    if (!file) return;
    uint8_t* restored = gtPictureRestore(sprites, sizeof(sprites), file, size, GT_PICTURE_SPRITES, &restoredSize);
    CHECK(restored && restoredSize == 40 && !memcmp(restored, changed, 40));
    free(restored);
    CHECK(!gtPictureRestore(changed, sizeof(changed), file, size, GT_PICTURE_SPRITES, &restoredSize));
    CHECK(!gtPictureRestore(sprites, sizeof(sprites), file, size - 1, GT_PICTURE_SPRITES, &restoredSize));
    CHECK(!gtPictureRestore(sprites, sizeof(sprites), file, size, GT_PICTURE_TIMS, &restoredSize));
    file[32 + 20] ^= 1;
    CHECK(!gtPictureRestore(sprites, sizeof(sprites), file, size, GT_PICTURE_SPRITES, &restoredSize));
    file[32 + 20] ^= 1;
    file[0] ^= 1;
    CHECK(!gtPictureRestore(sprites, sizeof(sprites), file, size, GT_PICTURE_SPRITES, &restoredSize));
    free(file);
    changed[4] = 38; /* a record running beyond its member */
    CHECK(!gtPictureSnapshot(sprites, sizeof(sprites), changed, sizeof(changed), GT_PICTURE_SPRITES, &size));
    CHECK(!gtPictureSnapshot(sprites, sizeof(sprites), sprites, GT_PICTURE_MEMBER_MAX + 1, GT_PICTURE_SPRITES, &size));
    uint8_t tim[30] = {1, 0, 0, 0, 8, 0, 0, 0, 0x10, 0, 0, 0, 2, 0, 0, 0, 14};
    tim[24] = 1; tim[26] = 1; tim[28] = 0x1f;
    file = gtPictureSnapshot(tim, sizeof(tim), tim, sizeof(tim), GT_PICTURE_TIMS, &size);
    CHECK(file != NULL);
    restored = gtPictureRestore(tim, sizeof(tim), file, size, GT_PICTURE_TIMS, &restoredSize);
    CHECK(restored && restoredSize == 30 && !memcmp(restored, tim, 30));
    free(restored); free(file);
    tim[24] = 255; /* image dimensions exceed its pixel block */
    CHECK(!gtPictureSnapshot(tim, sizeof(tim), tim, sizeof(tim), GT_PICTURE_TIMS, &size));
    uint8_t texture[26] = {1, 0, 0, 0, 8, 0, 0, 0, 0, 0x11};
    texture[20] = 1; texture[22] = 1;
    file = gtPictureSnapshot(texture, sizeof(texture), texture, sizeof(texture), GT_PICTURE_TEXTURES, &size);
    CHECK(file != NULL);
    restored = gtPictureRestore(texture, sizeof(texture), file, size, GT_PICTURE_TEXTURES, &restoredSize);
    CHECK(restored && restoredSize == 26 && !memcmp(restored, texture, 26));
    free(restored); free(file);
    texture[8] = 2;
    CHECK(!gtPictureSnapshot(texture, sizeof(texture), texture, sizeof(texture), GT_PICTURE_TEXTURES, &size));
}

/* A text image's pictures are kept against the running release's own
 * members (the snapshot's fingerprints, the fitted sprite tables). Without
 * this release's Disc 2 image, Disc 2 is left out and Disc 1's pictures are
 * still exported (a Disc 2 reader falls back to them): the export succeeds. */
static void pictureExport(const char* dir, const char* discPath) {
    char why[512], out[4096], path[4096];
    GtImage* im = gtImageOpen(discPath, why, sizeof(why));
    if (im == NULL) return;
    for (int both = 1; both >= 0; both--) {
        retailImages[0] = im;
        retailImages[1] = both ? im : NULL;
        snprintf(out, sizeof(out), "%s/picture-export-%d", dir, both);
        snprintf(path, sizeof(path), "%s/pictures/menu/2/1/3.member", out);
        SDL_RemovePath(path);
        CHECK(gtPictureExport(im, im, out, 1) > 0);
        SDL_PathInfo info;
        CHECK((SDL_GetPathInfo(path, &info) ? 1 : 0) == both);
        snprintf(path, sizeof(path), "%s/pictures/menu/1/1/3.member", out);
        CHECK(SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE);
    }
    retailImages[0] = retailImages[1] = NULL;
    gtImageClose(im);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: gametexttest DIR [US-DISC1 [JP-DISC1]]\n");
        return 2;
    }
    SDL_CreateDirectory(argv[1]);
    fonts();
    fontSnapshots();
    tables();
    packs(argv[1]);
    ownFont(argv[1]);
    proportionalPrint();
    pictureSnapshots();
    for (int i = 2; i < argc; i++) roundTrip(argv[1], argv[i]); /* US disc 1, JP disc 1 */
    if (argc > 2) pictureExport(argv[1], argv[2]);
    snapshotCapacity();
    printf("gametexttest: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
