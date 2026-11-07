/* Authored word-picture placement. Original geometry is read from the
 * player's disc; no sprite table or translated artwork is built in.
 * See src/main/font.c: a 4-byte record header, then 0x1c-byte pieces whose
 * signed draw offsets are +8/+10, separate from UVs, size and palette. */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gametext.h"

enum { MAX_SPRITES = 4096, MAX_PIECES = 64, MAX_ROWS = 1024, TABLE_BYTES = 65535 };
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t* p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }

uint64_t gtSpriteFingerprint(const uint8_t* table, int size) {
    uint64_t h = UINT64_C(14695981039346656037);
    const char* domain = "xgspr1";
    while (*domain) h = (h ^ (uint8_t)*domain++) * UINT64_C(1099511628211);
    for (int i = 0; i < size; i++) h = (h ^ table[i]) * UINT64_C(1099511628211);
    return h;
}

static int tableCount(const uint8_t* table, int size) {
    if (!table || size < 8 || size > TABLE_BYTES) return 0;
    uint32_t n = rd32(table);
    if (!n || n > MAX_SPRITES || 4 + 2 * n > (unsigned)size) return 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned at = rd16(table + 4 + 2 * i);
        if (at < 4 + 2 * n || (at & 1) || at + 4 > (unsigned)size) return 0;
        int polys = (int16_t)rd16(table + at);
        if (polys < 0 || polys > MAX_PIECES || at + 4 + 28 * (unsigned)polys > (unsigned)size) return 0;
    }
    return (int)n;
}

static char* spaces(char* p) { while (*p == ' ' || *p == '\t' || *p == '\r') p++; return p; }
static int integer(char** p, int* out) {
    char* start = spaces(*p), *end;
    errno = 0;
    long value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || value < INT_MIN || value > INT_MAX ||
        (*end && *end != ' ' && *end != '\t' && *end != '\r')) return 0;
    *p = end; *out = (int)value;
    return 1;
}
static int header(char* p, uint64_t expected) {
    const char magic[] = "xg-sprites-1";
    size_t n = sizeof(magic) - 1;
    if (strncmp(p, magic, n) || (p[n] != ' ' && p[n] != '\t')) return 0;
    p = spaces(p + n);
    uint64_t h = 0;
    for (int i = 0; i < 16; i++) {
        unsigned c = (unsigned char)*p++, digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        h = h << 4 | digit;
    }
    return !*spaces(p) && h == expected;
}

uint8_t* gtSpriteLayoutApply(const uint8_t* table, int size, const char* text, size_t length,
                            int* outSize, char* why, size_t whySize) {
    if (outSize) *outSize = 0;
    if (whySize) why[0] = 0;
    int count = tableCount(table, size);
    const char* error = NULL;
    if (!count) error = "invalid source sprite table";
    else if (!text || !length || length > GT_SPRITE_LAYOUT_MAX || memchr(text, 0, length))
        error = "invalid layout size or embedded NUL";
    if (error) { if (whySize) snprintf(why, whySize, "%s", error); return NULL; }
    char* copy = malloc(length + 1);
    uint8_t* out = malloc(TABLE_BYTES);
    if (!copy || !out) { free(copy); free(out); if (whySize) snprintf(why, whySize, "out of memory"); return NULL; }
    memcpy(copy, text, length); copy[length] = 0;
    memcpy(out, table, (size_t)size);
    uint16_t clones[MAX_SPRITES] = {0};
    uint64_t seen[MAX_SPRITES] = {0};
    int used = size, rows = 0, haveHeader = 0, lineNumber = 0;
    char* p = copy;
    if (length >= 3 && !memcmp(p, "\xEF\xBB\xBF", 3)) p += 3;
    while (*p) {
        char* line = p;
        p = strchr(p, '\n');
        if (p) *p++ = 0; else p = line + strlen(line);
        lineNumber++;
        char* comment = strchr(line, '#');
        if (comment) *comment = 0;
        line = spaces(line);
        if (!*line) continue;
        if (!haveHeader) {
            if (!header(line, gtSpriteFingerprint(table, size))) { error = "header or source fingerprint mismatch"; break; }
            haveHeader = 1; continue;
        }
        int sprite, piece, x, y;
        if (!integer(&line, &sprite) || !integer(&line, &piece) || !integer(&line, &x) || !integer(&line, &y) ||
            *spaces(line)) { error = "expected sprite piece x y (decimal integers)"; break; }
        if (++rows > MAX_ROWS) { error = "more than 1024 placement rows"; break; }
        if (sprite < 0 || sprite >= count) { error = "sprite index out of range"; break; }
        int at = rd16(table + 4 + 2 * sprite), polys = rd16(table + at);
        if (piece < 0 || piece >= polys) { error = "piece index out of range"; break; }
        if (seen[sprite] & (UINT64_C(1) << piece)) { error = "duplicate sprite/piece"; break; }
        const uint8_t* q = table + at + 4 + 28 * piece;
        int w = (int16_t)rd16(q + 4), h = (int16_t)rd16(q + 6);
        if (w < 1 || w > 256 || h < 1 || h > 256 || x < -1024 || y < -1024 || x > 1023 || y > 1023 ||
            x + w > 1024 || y + h > 1024) { error = "piece size or resulting coordinates out of range"; break; }
        seen[sprite] |= UINT64_C(1) << piece;
        if (!clones[sprite]) {
            int bytes = 4 + 28 * polys;
            /* Decompressed members can have an odd trailing byte. Native
             * FontGlyph/FontGlyphPoly halfwords still require aligned records. */
            int aligned = (used + 1) & ~1;
            if (aligned + bytes > TABLE_BYTES) { error = "expanded sprite table exceeds 65535 bytes"; break; }
            if (aligned != used) out[used] = 0;
            clones[sprite] = (uint16_t)aligned;
            memcpy(out + aligned, table + at, (size_t)bytes);
            wr16(out + 4 + 2 * sprite, aligned);
            used = aligned + bytes;
        }
        uint8_t* moved = out + clones[sprite] + 4 + 28 * piece;
        wr16(moved + 8, x); wr16(moved + 10, y);
    }
    if (!haveHeader && !error) error = "missing layout header";
    free(copy);
    if (error || !rows) {
        free(out);
        if (error && whySize) snprintf(why, whySize, "line %d: %s", lineNumber, error);
        return NULL;
    }
    if (outSize) *outSize = used;
    return out;
}

char* gtSpriteLayoutTemplate(const uint8_t* table, int size, int* length) {
    if (length) *length = 0;
    int count = tableCount(table, size);
    if (!count) return NULL;
    char* text = malloc(GT_SPRITE_LAYOUT_MAX + 1);
    if (!text) return NULL;
    size_t used = (size_t)snprintf(text, GT_SPRITE_LAYOUT_MAX,
        "xg-sprites-1 %016" PRIx64 "\n# Uncomment only the pieces you want to move.\n"
        "# sprite piece x y (decimal native-pixel draw offsets; UVs, size and palette stay unchanged)\n",
        gtSpriteFingerprint(table, size));
    for (int i = 0; i < count; i++) {
        int at = rd16(table + 4 + 2 * i), polys = rd16(table + at);
        for (int k = 0; k < polys; k++) {
            const uint8_t* q = table + at + 4 + 28 * k;
            if (used + 128 > GT_SPRITE_LAYOUT_MAX) {
                used += (size_t)snprintf(text + used, GT_SPRITE_LAYOUT_MAX + 1 - used, "# Remaining pieces omitted: template size limit.\n");
                goto done;
            }
            used += (size_t)snprintf(text + used, GT_SPRITE_LAYOUT_MAX + 1 - used,
                "# %d %d %d %d # %dx%d pixels\n", i, k, (int16_t)rd16(q + 8), (int16_t)rd16(q + 10),
                (int16_t)rd16(q + 4), (int16_t)rd16(q + 6));
        }
    }
done:
    if (length) *length = (int)used;
    return text;
}
