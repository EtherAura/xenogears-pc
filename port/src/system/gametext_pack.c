/*
 * Text packs (gametext.h): the game's text as UTF-8 files a person can read
 * and edit, written from the player's own discs and read back as a source.
 * Nothing of the game is in this program: a pack is made on the player's
 * machine, into their save folder, never shipped.
 *
 * <save folder>/packs/<id>/
 *   pack.ini      name=, language= (BCP 47), release= (US or JP: whose
 *                 tables the keys are), font= (us or jp: the font of <g:N>);
 *                 opt-ins, the game's own look without them:
 *                 print=proportional  its texts print with each glyph's own
 *                   width (spacing= pixels between glyphs, 1 to 3, default 2;
 *                   space= a blank's pixels, 2 to 12, default 4; a line's
 *                   leading blanks keep their width: a choice's cursor room)
 *                 windows=pack  the field windows take the entries' w= h=
 *                   (else the disc's sizes, or for a pack of the other release
 *                   its own); windows=grow  at least what the text needs
 *                   (each line's width, the most lines between waits); both
 *                   within the screen and the window's text memory (55 x 7;
 *                   a portrait's window stays 4 lines, the field's rule)
 *                 windows=source  an exported text image's sizes, with the
 *                   game's scripted sizes still taking precedence
 *   field.txt, kernel.txt, menu.txt, battle.txt, world.txt
 *
 * An entry is a line "@@ <key>" (window sizes after it for a field: w=W h=H)
 * and its text on the lines after, up to the next "@@ " line (a text line
 * that itself begins with "@@" is written "<at>@..."):
 *   field/<disc>/<field>/<i>    a field's dialogue (section 7)
 *   kernel/<disc>/<member>/<i>         01/07 (member = the game's bank + 1)
 *   menu/<disc>/<file>/<member>/<i>    10/0<file>
 *   battle/common/<disc>/<member>/<i>  0C/03
 *   battle/set/<disc>/<file>/<i>       0D/<file>
 *   world/<disc>/<file>/<i>            24/<file>
 * (disc 2's only where its table differs from disc 1's: a reader takes disc
 * 1's otherwise)
 * Text: a new line is the printer's 01; tags for its other codes:
 *   <wait> 02 (a new line right after it: 02 01), <close> 03, <box> FF FF,
 *   <delay d> <speed n> <auto d> <str b i> <ref> <name c> <weapon i> <b24 i>
 *   <b25 i> <num v> <num1 v> <x6d x> <snum v> <closeafter d> <slow d>
 *   <button b> (the 0F escapes 00..0F), <g:N> glyph N of the pack's font
 *   (no character known for it), <lt> <gt> <at> for < > @.
 * Reading it back, a text checks out or keeps the disc's: every tag known,
 * every character in the running font (or the other release's, brought along).
 */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gametext.h"
#include "gametext_fonts.h"

static const char* const escapeTags[16] = {"delay", "speed", "auto", "str", "ref", "name", "weapon", "b24",
                                           "b25", "num", "num1", "x6d", "snum", "closeafter", "slow", "button"};

/* ---- writing ------------------------------------------------------------------------------ */

typedef struct {
    char* data;
    size_t size, cap;
    int failed;
} Buf;

static void put(Buf* b, const char* s, size_t n) {
    if (b->failed) return;
    if (b->size + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 65536;
        while (b->size + n + 1 > cap) cap *= 2;
        char* grown = realloc(b->data, cap);
        if (grown == NULL) {
            b->failed = 1;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->size, s, n);
    b->size += n;
    b->data[b->size] = '\0';
}
static void puts_(Buf* b, const char* s) { put(b, s, strlen(s)); }
static void printf_(Buf* b, const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) put(b, tmp, (size_t)(n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1));
}

static void putUtf8(Buf* b, unsigned cp) {
    char s[4];
    int n;
    if (cp < 0x80) s[0] = (char)cp, n = 1;
    else if (cp < 0x800) s[0] = (char)(0xC0 | cp >> 6), s[1] = (char)(0x80 | (cp & 63)), n = 2;
    else s[0] = (char)(0xE0 | cp >> 12), s[1] = (char)(0x80 | ((cp >> 6) & 63)), s[2] = (char)(0x80 | (cp & 63)), n = 3;
    put(b, s, (size_t)n);
}

static const uint16_t* unicodeOf(const char* region, int* count) {
    if (!strcmp(region, "JP")) {
        *count = (int)(sizeof(gtJpUnicode) / sizeof(gtJpUnicode[0]));
        return gtJpUnicode;
    }
    *count = (int)(sizeof(gtUsUnicode) / sizeof(gtUsUnicode[0]));
    return gtUsUnicode;
}

/* A text of `font` (decoded with `uni`) as pack text; 0 when it does not
 * check out (then the entry is not written). */
static int decodeText(Buf* b, const uint8_t* s, int length, const GtFont* font, const uint16_t* uni, int uniCount) {
    int i = 0, lineStart = 1;
    while (i < length) {
        int c = s[i];
        if (c == 0) return 1;
        if (lineStart && c == '@' && i + 1 < length && s[i + 1] == '@') puts_(b, "<at>");
        lineStart = 0;
        if (c == 1) {
            puts_(b, "\n");
            lineStart = 1;
            i++;
        } else if (c == 2) {
            puts_(b, "<wait>");
            i++;
            if (i < length && s[i] == 1) {
                puts_(b, "\n");
                lineStart = 1;
                i++;
            }
        } else if (c == 3) {
            puts_(b, "<close>");
            i++;
        } else if (c == 0x0F) {
            if (i + 1 >= length || s[i + 1] > 15) return 0;
            int k = s[i + 1];
            if (k == 4) {
                puts_(b, "<ref>");
                i += 2;
            } else if (k == 3) {
                if (i + 3 >= length) return 0;
                printf_(b, "<str %d %d>", s[i + 2], s[i + 3]);
                i += 4;
            } else {
                if (i + 2 >= length) return 0;
                printf_(b, "<%s %d>", escapeTags[k], s[i + 2]);
                i += 3;
            }
        } else if (c < 0x10) {
            return 0;
        } else if (c == 0xFF && i + 1 < length && s[i + 1] == 0xFF) {
            puts_(b, "<box>");
            i += 2;
        } else {
            int n, g = gtFontDecode(font, s + i, &n);
            if (g < 0) return 0;
            unsigned cp = g < uniCount ? uni[g] : 0;
            if (cp == '<') puts_(b, "<lt>");
            else if (cp == '>') puts_(b, "<gt>");
            else if (cp >= 0x20) putUtf8(b, cp);
            else printf_(b, "<g:%d>", g);
            i += n;
        }
    }
    return 1;
}

typedef struct {
    Buf* file[GT_KINDS];
    const GtFont* font;
    const uint16_t* uni;
    int uniCount, entries;
    int disc;
    /* disc 1's tables (key, fingerprint): disc 2 writes only those that differ */
    struct {
        GtKey key;
        uint64_t fp;
    } * seen;
    int seenCount, seenCap;
} Export;

static const char* const kindFiles[GT_KINDS] = {"kernel.txt", "menu.txt", "battle.txt", "battle.txt", "world.txt"};

void gtKeyName(const GtKey* k, int disc, char* out, size_t size) {
    switch (k->kind) {
    case GT_KIND_KERNEL: snprintf(out, size, "kernel/%d/%d", disc, k->member); break;
    case GT_KIND_MENU: snprintf(out, size, "menu/%d/%d/%d", disc, k->file, k->member); break;
    case GT_KIND_BATTLE_COMMON: snprintf(out, size, "battle/common/%d/%d", disc, k->member); break;
    case GT_KIND_BATTLE_SET: snprintf(out, size, "battle/set/%d/%d", disc, k->file); break;
    default: snprintf(out, size, "world/%d/%d", disc, k->file); break;
    }
}

static void exportTable(void* user, const GtKey* key, const uint8_t* table, int size) {
    Export* e = user;
    GtTable t;
    if (gtKeyCategory(key) < 0 || !gtTableParse(table, size, &t)) return;
    uint64_t fp = gtFingerprint(table, size);
    if (e->disc == 1) {
        if (e->seenCount == e->seenCap) {
            int cap = e->seenCap ? e->seenCap * 2 : 128;
            void* grown = realloc(e->seen, sizeof(*e->seen) * (size_t)cap);
            if (grown == NULL) return;
            e->seen = grown;
            e->seenCap = cap;
        }
        e->seen[e->seenCount].key = *key;
        e->seen[e->seenCount++].fp = fp;
    } else {
        for (int i = 0; i < e->seenCount; i++) {
            if (!memcmp(&e->seen[i].key, key, sizeof(*key)) && e->seen[i].fp == fp) return;
        }
    }
    char prefix[64];
    gtKeyName(key, e->disc, prefix, sizeof(prefix));
    Buf* b = e->file[key->kind];
    for (int i = 0; i < t.count; i++) {
        int length;
        const uint8_t* s = gtTableEntry(&t, i, &length);
        size_t mark = b->size;
        printf_(b, "@@ %s/%d\n", prefix, i);
        if (!decodeText(b, s, length, e->font, e->uni, e->uniCount)) {
            b->size = mark; /* a text that does not check out is left out */
            if (b->data) b->data[mark] = '\0';
            continue;
        }
        puts_(b, "\n");
        e->entries++;
    }
}

static int writeFile(const char* dir, const char* name, const Buf* b) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (f == NULL) return 0;
    size_t n = b->size ? fwrite(b->data, 1, b->size, f) : 0;
    int ok = n == b->size;
    return fclose(f) == 0 && ok;
}

/* The texts of the images (one release's two discs) as a pack in `dir`,
 * decoded with `font` and its characters `uni`; pack.ini names `fontName`
 * as the font of its <g:N>. */
static int exportPack(GtImage* images[2], const char* dir, const char* name, const char* language,
                      int (*fieldOk)(int disc, int field, void* user), void* user, const GtFont* fontIn,
                      const uint16_t* uni, int uniCount, const char* fontName, char* why, size_t size) {
    GtImage* any = images[0] ? images[0] : images[1];
    const char* region = gtImageRegion(any);
    GtFont font = *fontIn;
    if (!SDL_CreateDirectory(dir)) {
        snprintf(why, size, "Cannot make %s.", dir);
        return 0;
    }
    Buf files[GT_KINDS + 1];
    memset(files, 0, sizeof(files));
    Export e;
    memset(&e, 0, sizeof(e));
    e.font = &font;
    e.uni = uni;
    e.uniCount = uniCount;
    for (int k = 0; k < GT_KINDS; k++) e.file[k] = &files[k];
    e.file[GT_KIND_BATTLE_SET] = &files[GT_KIND_BATTLE_COMMON]; /* one battle.txt */
    Buf* field = &files[GT_KINDS];
    for (int d = 0; d < 2; d++) {
        if (images[d] == NULL) continue;
        e.disc = gtImageDisc(images[d]);
        gtImageTables(images[d], exportTable, &e);
        for (int n = 0; n < 730; n++) {
            int bytes;
            if (fieldOk != NULL && !fieldOk(e.disc, n, user)) continue;
            uint8_t* blob = gtFieldSection(images[d], n, 7, &bytes);
            GtTable t;
            if (blob == NULL || !gtTableParse(blob, bytes, &t)) {
                free(blob);
                continue;
            }
            const uint8_t* wh = blob + 4 + 2 * t.count;
            for (int i = 0; i < t.count; i++) {
                int length;
                const uint8_t* s = gtTableEntry(&t, i, &length);
                size_t mark = field->size;
                printf_(field, "@@ field/%d/%d/%d w=%d h=%d\n", gtImageDisc(images[d]), n, i, wh[2 * i], wh[2 * i + 1]);
                if (!decodeText(field, s, length, &font, e.uni, e.uniCount)) {
                    field->size = mark;
                    if (field->data) field->data[mark] = '\0';
                    continue;
                }
                puts_(field, "\n");
                e.entries++;
            }
            free(blob);
        }
    }
    Buf ini = {0};
    printf_(&ini, "# A text pack made by the Xenogears PC port from your own discs: for your own use.\n");
    printf_(&ini, "name=%s\nlanguage=%s\nrelease=%s\nfont=%s\n", name, language, region, fontName);
    /* A text image's pack must retain its windows as well as its font.
     * Authored pack/grow modes separately opt out of script precedence. */
    if (!strcmp(fontName, "pack")) printf_(&ini, "windows=source\n");
    printf_(&ini, "# Opt-ins (without them the text prints as the game does; remove a # to use one):\n"
                  "#print=proportional\n#spacing=2\n#space=4\n#windows=pack\n#windows=grow\n");
    int ok = writeFile(dir, "pack.ini", &ini) && writeFile(dir, "field.txt", field);
    for (int k = 0; k < GT_KINDS && ok; k++) {
        if (k == GT_KIND_BATTLE_SET) continue;
        ok = writeFile(dir, kindFiles[k], &files[k]);
    }
    int failed = ini.failed;
    for (int k = 0; k <= GT_KINDS; k++) failed |= files[k].failed;
    for (int k = 0; k <= GT_KINDS; k++) free(files[k].data);
    free(ini.data);
    free(e.seen);
    if (!ok || failed) {
        snprintf(why, size, "Cannot write the pack in %s.", dir);
        return 0;
    }
    snprintf(why, size, "%d texts written to %s.", e.entries, dir);
    return 1;
}

int gameTextExport(GtImage* disc1, GtImage* disc2, const char* dir, const char* name,
                   int (*fieldOk)(int disc, int field, void* user), void* user, char* why, size_t size) {
    GtImage* images[2] = {disc1, disc2};
    GtImage* any = disc1 ? disc1 : disc2;
    if (any == NULL) {
        snprintf(why, size, "No disc image to read the text from.");
        return 0;
    }
    const char* region = gtImageRegion(any);
    int fontSize, uniCount;
    uint8_t* fontData = gtImageFont(any, &fontSize);
    GtFont font;
    if (fontData == NULL || !gtFontParse(fontData, fontSize, &font)) {
        free(fontData);
        snprintf(why, size, "Cannot read the font of %s.", gtImagePath(any));
        return 0;
    }
    const uint16_t* uni = unicodeOf(region, &uniCount);
    int jp = !strcmp(region, "JP");
    int ok = exportPack(images, dir, name, jp ? "ja" : "en", fieldOk, user, &font, uni, uniCount, jp ? "jp" : "us",
                        why, size);
    free(fontData);
    return ok;
}

/* ---- indexed PNG (pictures, a pack font's sheet; read back with SDL's PNG reader) ---------- */

static uint32_t crcTable[256];
static uint32_t crc32Of(uint32_t crc, const uint8_t* p, size_t n) {
    if (crcTable[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
    }
    crc = ~crc;
    while (n--) crc = crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
static void be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static int chunk(SDL_IOStream* io, const char* type, const uint8_t* data, uint32_t n) {
    uint8_t head[8], tail[4];
    be32(head, n);
    memcpy(head + 4, type, 4);
    uint32_t crc = crc32Of(crc32Of(0, head + 4, 4), data, n);
    be32(tail, crc);
    return SDL_WriteIO(io, head, 8) == 8 && (n == 0 || SDL_WriteIO(io, data, n) == n) && SDL_WriteIO(io, tail, 4) == 4;
}

/* An 8-bit indexed PNG: w x h indices, `colors` RGBA palette entries (stored
 * deflate blocks: small pictures, no compressor needed). */
int gtPngWrite(const char* path, int w, int h, const uint8_t* indices, const uint8_t (*palette)[4], int colors) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
    size_t raw = (size_t)(w + 1) * h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zsize = 2 + raw + blocks * 5 + 4;
    uint8_t* z = malloc(zsize);
    uint8_t* line = malloc(raw > 0 ? raw : 1);
    if (z == NULL || line == NULL || colors < 1 || colors > 256) {
        free(z);
        free(line);
        return 0;
    }
    for (int y = 0; y < h; y++) {
        line[(size_t)y * (w + 1)] = 0; /* filter: none */
        memcpy(line + (size_t)y * (w + 1) + 1, indices + (size_t)y * w, (size_t)w);
    }
    size_t o = 0;
    z[o++] = 0x78, z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw; i++) {
        a = (a + line[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw || at == 0; at += 65535) {
        size_t n = raw - at < 65535 ? raw - at : 65535;
        z[o++] = at + n >= raw ? 1 : 0;
        z[o++] = (uint8_t)n, z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n, z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, line + at, n);
        o += n;
        if (raw == 0) break;
    }
    be32(z + o, b << 16 | a);
    o += 4;
    uint8_t ihdr[13], plte[768], trns[256];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    for (int k = 0; k < colors; k++) {
        plte[3 * k] = palette[k][0], plte[3 * k + 1] = palette[k][1], plte[3 * k + 2] = palette[k][2];
        trns[k] = palette[k][3];
    }
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    int ok = io != NULL && SDL_WriteIO(io, "\x89PNG\r\n\x1a\n", 8) == 8 && chunk(io, "IHDR", ihdr, 13) &&
             chunk(io, "PLTE", plte, (uint32_t)colors * 3) && chunk(io, "tRNS", trns, (uint32_t)colors) &&
             chunk(io, "IDAT", z, (uint32_t)o) && chunk(io, "IEND", NULL, 0);
    if (io != NULL && !SDL_CloseIO(io)) ok = 0;
    free(z);
    free(line);
    return ok;
}

/* A font's glyphs, 16 to a row in cells of 14 x 13 (the glyph at 1, 1; grid
 * lines between): a pack's font.png, to see which glyph is which. */
int gtFontSheet(const GtFont* font, const char* dir, const char* name) {
    int rows = (font->count + 15) / 16, w = 16 * 14 + 1, h = rows * 13 + 1;
    uint8_t* px = calloc((size_t)w * h, 1);
    if (px == NULL) return 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x % 14 == 0 || y % 13 == 0) px[(size_t)y * w + x] = (y / 13) % 4 == 0 && y % 13 == 0 ? 3 : 2;
        }
    }
    for (int g = 0; g < font->count; g++) {
        const uint8_t* bits = font->bits + g * 22;
        int ox = (g % 16) * 14 + 1, oy = (g / 16) * 13 + 1;
        for (int r = 0; r < 11; r++) {
            int v = (int)(bits[2 * r] | bits[2 * r + 1] << 8);
            for (int c = 0; c < 12; c++) {
                int on = c < 8 ? v >> (7 - c) & 1 : v >> (15 - (c - 8)) & 1;
                if (on) px[(size_t)(oy + r + 1) * w + ox + c + 1] = 1;
            }
        }
    }
    static const uint8_t palette[4][4] = {{24, 24, 40, 255}, {240, 240, 240, 255}, {64, 64, 96, 255}, {160, 64, 64, 255}};
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int ok = gtPngWrite(path, w, h, px, palette, 4);
    free(px);
    return ok;
}

/* ---- a text image's pack: its own font ------------------------------------------------------ */

static int glyphPixels(const uint8_t* g, int* ink) {
    int n = 0;
    for (int k = 0; k < 22; k++) n += __builtin_popcount(g[k]);
    *ink = n;
    return n;
}

/* A redrawn glyph: most of its pixels those of the glyph it replaces. */
static int similarGlyph(const uint8_t* a, const uint8_t* b) {
    int diff = 0, both = 0, inkA, inkB;
    glyphPixels(a, &inkA);
    glyphPixels(b, &inkB);
    for (int k = 0; k < 22; k++) {
        diff += __builtin_popcount(a[k] ^ b[k]);
        both += __builtin_popcount(a[k] | b[k]);
    }
    return both > 0 && inkA > 0 && inkB > 0 && diff * 3 <= both;
}

uint16_t* gtDerivedUnicode(const GtFont* font, const GtFont* base, const uint16_t* baseUni, int baseUniCount) {
    uint16_t* uni = calloc((size_t)(font->count > 0 ? font->count : 1), sizeof(uint16_t));
    int16_t* eq = gtFontEquivalents(font, base);
    if (uni == NULL || eq == NULL) {
        free(uni);
        free(eq);
        return NULL;
    }
    for (int g = 0; g < font->count; g++) {
        int b = eq[g];
        if (b >= 0 && b < baseUniCount && baseUni[b]) {
            uni[g] = baseUni[b]; /* the same picture */
        } else if (g < base->count && g < baseUniCount && baseUni[g] && gtFontWide(font, g) == gtFontWide(base, g) &&
                   similarGlyph(font->bits + g * 22, base->bits + g * 22)) {
            uni[g] = baseUni[g]; /* redrawn in its place */
        }
    }
    free(eq);
    return uni;
}

/* font.map: each glyph's character ("N A", "N U+0020"; unknown: "# N"). */
static int writeFontMap(const char* dir, const char* from, const GtFont* font, const uint16_t* uni) {
    Buf b = {0};
    printf_(&b, "# The pack's font (font.bin, from %s): a glyph's number and its character.\n", from);
    puts_(&b, "# A glyph listed as \"# N\" has no character known: give it one (\"N U+00E1\", or the\n"
              "# character itself after the number) to write that character in the texts instead of <g:N>.\n"
              "# font.png shows the glyphs, 16 to a row: glyph N in column N % 16, row N / 16.\n");
    for (int g = 0; g < font->count; g++) {
        if (uni[g] == 0) {
            printf_(&b, "# %d\n", g);
        } else if (uni[g] <= 0x20 || uni[g] == 0x3000 || uni[g] == '#') {
            printf_(&b, "%d U+%04X\n", g, uni[g]);
        } else {
            printf_(&b, "%d ", g);
            putUtf8(&b, uni[g]);
            puts_(&b, "\n");
        }
    }
    int ok = !b.failed && writeFile(dir, "font.map", &b);
    free(b.data);
    return ok;
}

static int writeBytes(const char* dir, const char* name, const uint8_t* data, int size) {
    Buf b = {0};
    if (size <= 0) return 0;
    put(&b, (const char*)data, (size_t)size);
    int ok = !b.failed && writeFile(dir, name, &b);
    free(b.data);
    return ok;
}

int gameTextExportImage(GtImage* disc1, GtImage* disc2, const GtFont* base, const uint16_t* baseUni, int baseUniCount,
                        const char* dir, const char* name, int (*fieldOk)(int disc, int field, void* user), void* user,
                        char* why, size_t size) {
    GtImage* images[2] = {disc1, disc2};
    GtImage* any = disc1 ? disc1 : disc2;
    if (any == NULL) {
        snprintf(why, size, "No text image to read.");
        return 0;
    }
    int fontSize;
    uint8_t* fontData = gtImageFont(any, &fontSize);
    GtFont font;
    if (fontData == NULL || !gtFontParse(fontData, fontSize, &font)) {
        free(fontData);
        snprintf(why, size, "Cannot read the font of %s.", gtImagePath(any));
        return 0;
    }
    uint16_t* uni = gtDerivedUnicode(&font, base, baseUni, baseUniCount);
    int ok = uni != NULL && SDL_CreateDirectory(dir);
    const char* from = strrchr(gtImagePath(any), '/');
    from = from ? from + 1 : gtImagePath(any);
    ok = ok && writeBytes(dir, "font.bin", fontData, fontSize) && writeFontMap(dir, from, &font, uni) &&
         gtFontSheet(&font, dir, "font.png");
    if (ok) {
        ok = exportPack(images, dir, name, "und", fieldOk, user, &font, uni, font.count, "pack", why, size);
    } else {
        snprintf(why, size, "Cannot write the pack's font in %s.", dir);
    }
    int known = 0;
    for (int g = 0; ok && g < font.count; g++) known += uni[g] != 0;
    if (ok) {
        size_t n = strlen(why);
        if (n > 0 && why[n - 1] == '.') why[n - 1] = '\0';
        snprintf(why + strlen(why), size - strlen(why), "; its font: %d glyphs, %d with a character known.",
                 font.count, known);
    }
    free(uni);
    free(fontData);
    return ok;
}

/* ---- reading -------------------------------------------------------------------------------- */

typedef struct {
    char* key;
    char* text;
    int w, h;
} PackEntry;

struct GtPack {
    char id[64], name[128], language[16], region[4], font[8];
    GtLayout layout;
    /* font=pack: its own font (font.bin) and the glyphs' characters (font.map) */
    uint8_t* fontData;
    GtFont ownFont;
    uint16_t* fontUni;
    PackEntry* entries;
    int count, cap;
};

const char* gtPackDir(void) {
    static char dir[4096];
    const char* env = SDL_getenv("XG_PACK_DIR");
    if (env != NULL && env[0] != '\0') {
        if (!strcmp(env, "none")) return NULL;
        snprintf(dir, sizeof(dir), "%s", env);
        return dir;
    }
    if (dir[0] == '\0') {
        char* pref = SDL_GetPrefPath(NULL, "xenogears");
        if (pref == NULL) return NULL;
        snprintf(dir, sizeof(dir), "%spacks", pref);
        SDL_free(pref);
    }
    return dir;
}

static char* readAll(const char* path, size_t* size) {
    SDL_IOStream* f = SDL_IOFromFile(path, "rb");
    if (f == NULL) return NULL;
    Sint64 n = SDL_GetIOSize(f);
    char* data = n >= 0 && n < (64 << 20) ? malloc((size_t)n + 1) : NULL;
    size_t got = data ? SDL_ReadIO(f, data, (size_t)n) : 0;
    SDL_CloseIO(f);
    if (data == NULL || got != (size_t)n) {
        free(data);
        return NULL;
    }
    data[n] = '\0';
    *size = (size_t)n;
    return data;
}

static void iniValue(const char* ini, const char* key, char* out, size_t size) {
    size_t k = strlen(key);
    for (const char* p = ini; p && *p;) {
        if (!strncmp(p, key, k) && p[k] == '=') {
            const char* v = p + k + 1;
            size_t n = strcspn(v, "\r\n");
            snprintf(out, size, "%.*s", (int)n, v);
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

void gtLayoutParse(const char* ini, GtLayout* out) {
    char v[32];
    memset(out, 0, sizeof(*out));
    out->spacing = 2;
    out->space = 4;
    v[0] = '\0';
    iniValue(ini, "print", v, sizeof(v));
    out->proportional = !SDL_strcasecmp(v, "proportional");
    v[0] = '\0';
    iniValue(ini, "windows", v, sizeof(v));
    if (!SDL_strcasecmp(v, "pack")) out->windows = GT_WINDOWS_PACK;
    else if (!SDL_strcasecmp(v, "grow")) out->windows = GT_WINDOWS_GROW;
    else if (!SDL_strcasecmp(v, "source")) out->windows = GT_WINDOWS_SOURCE;
    else out->windows = GT_WINDOWS_NATIVE;
    v[0] = '\0';
    iniValue(ini, "spacing", v, sizeof(v));
    if (v[0] != '\0') out->spacing = SDL_clamp(SDL_atoi(v), 1, 3);
    v[0] = '\0';
    iniValue(ini, "space", v, sizeof(v));
    if (v[0] != '\0') out->space = SDL_clamp(SDL_atoi(v), 2, 12);
}

static int byKey(const void* a, const void* b) {
    return strcmp(((const PackEntry*)a)->key, ((const PackEntry*)b)->key);
}

static void addEntries(GtPack* p, char* data) {
    char* line = data;
    PackEntry* cur = NULL;
    while (line != NULL) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        else if (*line == '\0') break; /* after the file's last new line */
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[--n] = '\0';
        if (!strncmp(line, "@@ ", 3)) {
            if (p->count == p->cap) {
                int cap = p->cap ? p->cap * 2 : 4096;
                PackEntry* grown = realloc(p->entries, sizeof(*grown) * (size_t)cap);
                if (grown == NULL) return;
                p->entries = grown;
                p->cap = cap;
            }
            cur = &p->entries[p->count++];
            memset(cur, 0, sizeof(*cur));
            char* key = line + 3;
            char* sp = strchr(key, ' ');
            if (sp) {
                *sp = '\0';
                sscanf(sp + 1, "w=%d h=%d", &cur->w, &cur->h);
            }
            cur->key = key;
            cur->text = NULL;
        } else if (cur != NULL) {
            if (cur->text == NULL) {
                cur->text = line;
            } else {
                /* the lines of one text, joined back (they are consecutive in the file) */
                char* end = cur->text + strlen(cur->text);
                *end = '\n';
                memmove(end + 1, line, n + 1);
            }
        }
        line = next;
    }
    for (int i = 0; i < p->count; i++) {
        if (p->entries[i].text == NULL) p->entries[i].text = "";
    }
}

static unsigned nextUtf8(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = p[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    unsigned cp = n == 1 ? c : n == 2 ? c & 31 : n == 3 ? c & 15 : c & 7;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += 1;
            return 0xFFFD;
        }
        cp = cp << 6 | (p[i] & 63);
    }
    *s += n;
    return cp;
}

/* font.map's lines: "N U+XXXX" or "N <character>"; "#" starts a comment. */
static void readFontMap(GtPack* p, const char* data) {
    for (const char* line = data; line && *line;) {
        const char* end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        char text[64];
        snprintf(text, sizeof(text), "%.*s", (int)(n < 63 ? n : 63), line);
        char* cr = strchr(text, '\r');
        if (cr) *cr = '\0';
        int g = -1, used = 0;
        if (text[0] != '#' && sscanf(text, "%d %n", &g, &used) >= 1 && g >= 0 && g < p->ownFont.count && used > 0) {
            const char* v = text + used;
            unsigned cp = 0;
            if ((v[0] == 'U' || v[0] == 'u') && v[1] == '+') cp = (unsigned)strtoul(v + 2, NULL, 16);
            else if (v[0] != '\0') cp = nextUtf8(&v);
            if (cp > 0 && cp < 65536) p->fontUni[g] = (uint16_t)cp;
        }
        line = end ? end + 1 : NULL;
    }
}

static void loadOwnFont(GtPack* p, const char* dir, const char* id) {
    char path[4096];
    size_t n;
    snprintf(path, sizeof(path), "%s/%s/font.bin", dir, id);
    p->fontData = (uint8_t*)readAll(path, &n);
    if (p->fontData == NULL || !gtFontParse(p->fontData, (int)n, &p->ownFont) ||
        (p->fontUni = calloc((size_t)p->ownFont.count, sizeof(uint16_t))) == NULL) {
        fprintf(stderr, "gametext: pack %s: font=pack but no usable %s\n", id, path);
        free(p->fontData);
        p->fontData = NULL;
        memset(&p->ownFont, 0, sizeof(p->ownFont));
        return;
    }
    snprintf(path, sizeof(path), "%s/%s/font.map", dir, id);
    char* map = readAll(path, &n);
    if (map != NULL) readFontMap(p, map);
    free(map);
}

const GtFont* gtPackOwnFont(const GtPack* p, const uint16_t** uni) {
    if (p == NULL || p->fontData == NULL) return NULL;
    if (uni) *uni = p->fontUni;
    return &p->ownFont;
}

GtPack* gtPackLoad(const char* id, char* why, size_t size) {
    const char* dir = gtPackDir();
    if (dir == NULL || id == NULL || id[0] == '\0' || strchr(id, '/') || strchr(id, '\\') || strstr(id, "..")) {
        snprintf(why, size, "No pack folder.");
        return NULL;
    }
    char path[4096];
    size_t n;
    snprintf(path, sizeof(path), "%s/%s/pack.ini", dir, id);
    char* ini = readAll(path, &n);
    if (ini == NULL) {
        snprintf(why, size, "No pack %s (%s).", id, path);
        return NULL;
    }
    GtPack* p = calloc(1, sizeof(*p));
    if (p == NULL) {
        free(ini);
        return NULL;
    }
    snprintf(p->id, sizeof(p->id), "%s", id);
    iniValue(ini, "name", p->name, sizeof(p->name));
    iniValue(ini, "language", p->language, sizeof(p->language));
    iniValue(ini, "release", p->region, sizeof(p->region));
    iniValue(ini, "font", p->font, sizeof(p->font));
    gtLayoutParse(ini, &p->layout);
    free(ini);
    if (p->name[0] == '\0') snprintf(p->name, sizeof(p->name), "%s", id);
    if (!SDL_strcasecmp(p->font, "pack")) loadOwnFont(p, dir, id);
    static const char* const files[] = {"field.txt", "kernel.txt", "menu.txt", "battle.txt", "world.txt"};
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s/%s/%s", dir, id, files[i]);
        char* data = readAll(path, &n);
        if (data != NULL) addEntries(p, data); /* the data stays: entries point into it */
    }
    if (p->count > 1) qsort(p->entries, (size_t)p->count, sizeof(*p->entries), byKey);
    snprintf(why, size, "%s: %d texts", p->name, p->count);
    return p;
}

const char* gtPackFind(const GtPack* p, const char* key, int* w, int* h) {
    if (p == NULL || p->count == 0) return NULL;
    PackEntry probe = {(char*)key, NULL, 0, 0};
    const PackEntry* e = bsearch(&probe, p->entries, (size_t)p->count, sizeof(*p->entries), byKey);
    if (e == NULL) return NULL;
    if (w) *w = e->w;
    if (h) *h = e->h;
    return e->text;
}

const char* gtPackName(const GtPack* p) { return p ? p->name : ""; }
const char* gtPackId(const GtPack* p) { return p ? p->id : ""; }
const char* gtPackRegion(const GtPack* p) { return p ? p->region : ""; }
const char* gtPackFontName(const GtPack* p) { return p ? p->font : ""; }
void gtPackLayout(const GtPack* p, GtLayout* out) {
    if (p != NULL) *out = p->layout;
    else gtLayoutParse("", out);
}

int gtPackList(char ids[][64], char names[][128], int max) {
    const char* dir = gtPackDir();
    if (dir == NULL) return 0;
    int count = 0, n = 0;
    char** list = SDL_GlobDirectory(dir, "*", 0, &count);
    for (int i = 0; i < count && n < max; i++) {
        char path[4096];
        size_t size;
        snprintf(path, sizeof(path), "%s/%s/pack.ini", dir, list[i]);
        char* ini = readAll(path, &size);
        if (ini == NULL || strlen(list[i]) >= 64) {
            free(ini);
            continue;
        }
        snprintf(ids[n], 64, "%s", list[i]);
        names[n][0] = '\0';
        iniValue(ini, "name", names[n], 128);
        if (names[n][0] == '\0') snprintf(names[n], 128, "%s", list[i]);
        free(ini);
        n++;
    }
    SDL_free(list);
    return n;
}

/* ---- encoding: pack text as the running font's codes ---------------------------------------- */

static int16_t* reverseOf(const uint16_t* uni, int count) {
    int16_t* rev = malloc(sizeof(int16_t) * 65536);
    if (rev == NULL) return NULL;
    for (int i = 0; i < 65536; i++) rev[i] = -1;
    for (int i = count - 1; i >= 0; i--) { /* the first glyph of a character wins (narrow before wide) */
        if (uni[i]) rev[uni[i]] = (int16_t)i;
    }
    return rev;
}

const uint16_t* gtFontUnicode(const char* region, int* count) {
    return unicodeOf(region, count);
}

/* ---- the game's text as UTF-8, without its font (the extras' names) ----------------------- */

/* The retail fonts' code layout (01/06's header, the same on both discs of a
 * release): one-byte codes from 0x10 up to the first lead, then lead + byte
 * for the glyphs after `two`; the narrow ones the first `narrow` one-byte
 * glyphs and the first `narrow2` of the first lead's page. */
static const struct {
    const char* region;
    int lead, two, narrow, narrow2;
} layouts[] = {{"US", 0xFE, 238, 81, 0}, {"JP", 0xF8, 232, 63, 21}};

static int layoutOf(const char* region) {
    return region != NULL && !strcmp(region, "JP") ? 1 : 0;
}

int gtGlyphWide(const char* region, int g) {
    int k = layoutOf(region);
    return g < layouts[k].two ? g >= layouts[k].narrow : !(g - layouts[k].two < layouts[k].narrow2);
}

int gtUnicodeGlyph(const char* region, unsigned cp, int* wide) {
    static int16_t* rev[2];
    int k = layoutOf(region), count;
    const uint16_t* uni = unicodeOf(layouts[k].region, &count);
    if (rev[k] == NULL) rev[k] = reverseOf(uni, count);
    int g = rev[k] != NULL && cp < 65536 ? rev[k][cp] : -1;
    if (g >= 0 && wide != NULL) *wide = gtGlyphWide(region, g);
    return g;
}

int gtTextToUtf8(const char* region, const uint8_t* s, int max, char* out, int size) {
    int k = layoutOf(region), count, n = 0;
    const uint16_t* uni = unicodeOf(layouts[k].region, &count);
    if (size <= 0) return 0;
    out[0] = '\0';
    for (int i = 0; i < max && s[i] != 0;) {
        int c = s[i], g;
        unsigned cp;
        if (c == 1) { /* a new line */
            cp = ' ';
            i++;
        } else if (c < 0x10) {
            return 0; /* a wait, a close, an escape (a name, a number...): not a plain text */
        } else if (c == 0xFF && i + 1 < max && s[i + 1] == 0xFF) {
            cp = 0x25A1; /* the crossed box: a white square */
            i += 2;
        } else {
            if (c < layouts[k].lead) {
                g = c - 0x10;
                i++;
            } else {
                if (i + 1 >= max) return 0;
                g = layouts[k].two + (c - layouts[k].lead) * 256 + s[i + 1];
                i += 2;
            }
            cp = g >= 0 && g < count ? uni[g] : 0;
            if (cp == 0) return 0; /* a glyph with no character known */
        }
        char utf[4];
        int len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
        if (len == 1) utf[0] = (char)cp;
        else if (len == 2) utf[0] = (char)(0xC0 | cp >> 6), utf[1] = (char)(0x80 | (cp & 63));
        else utf[0] = (char)(0xE0 | cp >> 12), utf[1] = (char)(0x80 | ((cp >> 6) & 63)), utf[2] = (char)(0x80 | (cp & 63));
        if (n + len + 1 > size) break;
        memcpy(out + n, utf, (size_t)len);
        n += len;
    }
    out[n] = '\0';
    return 1;
}


/* Glyph a of the bank font: the running font's code when it is the same
 * picture there, else the bank's code (FF C0..FE xx). */
static int bankGlyph(const GtEncode* enc, int a, uint8_t* out) {
    if (enc->bankEquiv != NULL && enc->bankEquiv[a] >= 0) {
        int n = gtFontEncode(enc->to, enc->bankEquiv[a], out);
        if (n > 0) return n;
    }
    return gtBankEncodeGlyph(enc->bankSlot, enc->bankFont, a, out);
}

int gtPackEncode(const char* text, const GtEncode* enc, uint8_t* out, int cap) {
    static const uint16_t* revFor[2];
    static int16_t* rev[2];
    const uint16_t* tables[2] = {enc->toUni, enc->bankUni};
    for (int k = 0; k < 2; k++) {
        if (revFor[k] != tables[k]) {
            free(rev[k]);
            rev[k] = tables[k] ? reverseOf(tables[k], k == 0 ? enc->toUniCount : enc->bankUniCount) : NULL;
            revFor[k] = tables[k];
        }
    }
    int o = 0;
    const char* s = text;
    while (*s) {
        if (o + 5 > cap) return 0;
        if (*s == '\n') {
            out[o++] = 1;
            s++;
        } else if (*s == '<') {
            const char* end = strchr(s, '>');
            if (end == NULL || end - s > 40) return 0;
            char tag[48];
            snprintf(tag, sizeof(tag), "%.*s", (int)(end - s - 1), s + 1);
            s = end + 1;
            int a = 0, b = 0, k;
            if (!strcmp(tag, "wait")) {
                out[o++] = 2;
                if (*s == '\n') {
                    out[o++] = 1;
                    s++;
                }
            } else if (!strcmp(tag, "close")) {
                out[o++] = 3;
            } else if (!strcmp(tag, "box")) {
                out[o++] = 0xFF;
                out[o++] = 0xFF;
            } else if (!strcmp(tag, "lt") || !strcmp(tag, "gt") || !strcmp(tag, "at")) {
                unsigned cp = tag[0] == 'l' ? '<' : tag[0] == 'g' ? '>' : '@';
                int g = rev[0] ? rev[0][cp] : -1;
                if (g < 0) return 0;
                o += gtFontEncode(enc->to, g, out + o);
            } else if (!strcmp(tag, "ref")) {
                out[o++] = 0x0F;
                out[o++] = 4;
            } else if (sscanf(tag, "g:%d", &a) == 1) {
                if (enc->gIsTo) {
                    int n = gtFontEncode(enc->to, a, out + o);
                    if (n == 0) return 0;
                    o += n;
                } else {
                    if (enc->bankFont == NULL || a < 0 || a >= enc->bankFont->count || a >= 2048) return 0;
                    int n = bankGlyph(enc, a, out + o);
                    if (n == 0) return 0;
                    o += n;
                }
            } else if (sscanf(tag, "str %d %d", &a, &b) == 2 && a >= 0 && a < 256 && b >= 0 && b < 256) {
                out[o++] = 0x0F;
                out[o++] = 3;
                out[o++] = (uint8_t)a;
                out[o++] = (uint8_t)b;
            } else {
                char word[16];
                if (sscanf(tag, "%15s %d", word, &a) != 2 || a < 0 || a > 255) return 0;
                for (k = 0; k < 16 && strcmp(word, escapeTags[k]); k++) {
                }
                if (k == 16 || k == 3 || k == 4) return 0;
                out[o++] = 0x0F;
                out[o++] = (uint8_t)k;
                out[o++] = (uint8_t)a;
            }
        } else {
            unsigned cp = nextUtf8(&s);
            int g = cp < 65536 && rev[0] ? rev[0][cp] : -1;
            int own = enc->bankFirst && cp < 65536 && rev[1] && enc->bankFont ? rev[1][cp] : -1;
            if (own >= 0 && own < 2048) {
                int n = bankGlyph(enc, own, out + o); /* the pack's own glyph for it */
                if (n == 0) return 0;
                o += n;
            } else if (g >= 0) {
                int n = gtFontEncode(enc->to, g, out + o);
                if (n == 0) return 0;
                o += n;
            } else {
                int h = cp < 65536 && rev[1] && enc->bankFont ? rev[1][cp] : -1;
                if (h < 0 || h >= 2048) return 0; /* no glyph for it: the disc's text */
                int n = bankGlyph(enc, h, out + o);
                if (n == 0) return 0;
                o += n;
            }
        }
    }
    if (o + 1 > cap) return 0;
    out[o++] = 0;
    return o;
}
