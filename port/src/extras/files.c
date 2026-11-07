/*
 * Kinds of files and the formats the asset browser opens (files.h). The
 * decoders that are game code (LZSS, the bundle relocation) run as the game's
 * own, in a sandbox; the checks and the TIM reader are ours.
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/memory.h"
#include "pc_convert.h"
#include "sandbox.h"

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

const char* xgKindName(XgKind k) {
    static const char* const names[XG_KIND_COUNT] = {
        "unknown", "empty",        "TIM picture",         "LZSS stream",          "offset bundle",
        "packed VRAM uploads",     "model archive",       "WDS sample bank",      "SMDS music score",
        "SEDS sound effects",      "PS-X EXE",            "TIM pictures",
    };
    return k >= 0 && k < XG_KIND_COUNT ? names[k] : "?";
}

XgKind xgIdentify(const uint8_t* d, int size) {
    if (size <= 0) {
        return XG_KIND_EMPTY;
    }
    XgTim tim;
    int used;
    XgBundle b;
    if (size >= 8 && memcmp(d, "PS-X EXE", 8) == 0) {
        return XG_KIND_EXE;
    }
    if (size >= 0x30 && memcmp(d, "wds ", 4) == 0) {
        return XG_KIND_WDS;
    }
    if (size >= 0x20 && memcmp(d, "smds", 4) == 0) {
        return XG_KIND_SMDS;
    }
    if (size >= 0x20 && memcmp(d, "seds", 4) == 0) {
        return XG_KIND_SEDS;
    }
    if (xgTimParse(d, size, &tim) && tim.size >= size - 2048) {
        return XG_KIND_TIM;
    }
    if (xgTimRun(d, size, NULL, 0) >= 2) {
        return XG_KIND_TIMS;
    }
    if (xgModelCheck(d, size)) {
        return XG_KIND_MODEL;
    }
    if (xgTexturesCheck(d, size)) {
        return XG_KIND_TEXTURES;
    }
    if (xgBundleParse(d, size, &b)) {
        return XG_KIND_BUNDLE;
    }
    if (xgLzssCheck(d, size, &used) > 0) {
        return XG_KIND_LZSS;
    }
    return XG_KIND_UNKNOWN;
}

/* ---- the file map (docs/ASSETS.md) ---------------------------------------------------------- */

const char* xgKnownLocation(int disc, int dir, int f, char* out, int n) {
    static const char* const overlays[] = {"battling", "field", "world map", "battle", "menu", "movie"};
    static const char* const world[] = {"",        "archive",    "TIM bundle",   "TIM bundle",    "WDS sample bank",
                                        "main song", "SEDS sound effects", "flight song", "battle song", "terrain",
                                        "terrain, transposed"};
    out[0] = '\0';
    switch (dir) {
    case 0x01:
        if (f >= 2 && f <= 5) {
            snprintf(out, n, "resident WDS sample bank %d", f - 2);
        } else if (f == 6) {
            snprintf(out, n, "dialogue font");
        } else if (f == 7) {
            snprintf(out, n, "item and name strings");
        } else if (f >= 0x0D && f <= 0x12) {
            snprintf(out, n, "%s overlay", overlays[f - 0x0D]);
        }
        break;
    case 0x04:
        if (f >= 0x46 && f <= 0xA0) {
            snprintf(out, n, "dialogue portrait %d (TIM)", f - 0x46);
        } else if (f == 0xA8) {
            snprintf(out, n, "field sound effects (SEDS)");
        } else if (f == 0xAC) {
            snprintf(out, n, "logos");
        } else if (f >= 0xB8 && f < 0xB8 + 2 * 730) {
            snprintf(out, n, (f - 0xB8) % 2 ? "field %d textures (sector-aligned uploads)" : "field %d archive",
                     (f - 0xB8) / 2);
        } else if (f >= 0x6BA && f < 0x6BA + 2 * 72) {
            snprintf(out, n, (f - 0x6BA) % 2 ? "Gear %d model bundle" : "Gear %d animation", (f - 0x6BA) / 2);
        } else if (f >= 0x7FB && f <= 0x852) {
            snprintf(out, n, "event slide %d (TIM)", f - 0x7FB);
        } else if (f >= 0x05 && f < 0x46) {
            snprintf(out, n, "field sprite bundle, party character %d?", f - 5);
        }
        break;
    case 0x0C:
        if (f == 3) {
            snprintf(out, n, "battle common archive");
        }
        break;
    case 0x0D:
        if (f >= 2 && f < 2 + 2 * 76) {
            snprintf(out, n, (f - 2) % 2 ? "enemy set %d visuals" : "enemy set %d definition", (f - 2) / 2);
        }
        break;
    case 0x0E:
        snprintf(out, n, "battle effect sprites");
        break;
    case 0x0F:
        if (f >= 6 && f < 6 + 2 * 75) {
            snprintf(out, n, (f - 6) % 2 ? "arena %d terrain" : "arena %d model bundle", (f - 6) / 2);
        }
        break;
    case 0x10:
        if (f == 1) {
            snprintf(out, n, "menu resources");
        }
        break;
    case 0x18:
        if (f == 1) {
            snprintf(out, n, "movie library overlay");
        }
        break;
    case 0x19:
        snprintf(out, n, "movie streams (STR, XA)");
        break;
    case 0x1C:
        if (f >= 2 && f <= 8) {
            snprintf(out, n, "extra WDS sample bank");
        } else if (f >= 0x13 && f < 0x114) {
            snprintf(out, n, (f - 0x13) % 2 ? "music %d score (SMDS)" : "music %d samples (WDS)", (f - 0x13) / 2);
        } else if (f >= 0x114 && f <= 0x11E) {
            snprintf(out, n, "SEDS sound effect bank");
        }
        break;
    case 0x24:
        if (f >= 0x2C && f < 0x2B + 0x0B * 17) {
            int k = (f - 0x2B) % 0x0B;
            if (k >= 1) {
                snprintf(out, n, "world map %d: %s", (f - 0x2B) / 0x0B, world[k]);
            }
        }
        break;
    case 0x29:
        snprintf(out, n, "battle Gear (animation in the next file)");
        break;
    case 0x2A:
        snprintf(out, n, "battle action sprites");
        break;
    case 0x2C:
    case 0x2D:
        snprintf(out, n, "battle sprite animations and sounds");
        break;
    case 0x30:
        if (f == 1 || (f >= 4 && f <= 6)) {
            static const char* const b[] = {"", "Battling song", "", "", "Battling heightfield", "Battling TIMs",
                                            "Battling portraits"};
            snprintf(out, n, "%s", b[f]);
        }
        break;
    case 0x31:
        if (f >= 2 && f <= 0x32) {
            snprintf(out, n, "Battling fighter %d", f - 2);
        }
        break;
    default:
        break;
    }
    if (out[0] != '\0' && disc == 2 && dir != 0x19) {
        size_t len = strlen(out);
        snprintf(out + len, (size_t)n - len, " (disc 1's map)");
    }
    return out;
}

/* ---- LZSS ----------------------------------------------------------------------------------- */

#define LZSS_MAX (32 << 20)
#define LZSS_SLACK 16  /* bytes the walk may read past the data (its padding) */
#define LZSS_OVERRUN 160 /* output past the size: the decoder ends groups of eight */

int xgLzssCheck(const uint8_t* d, int size, int* used) {
    if (size < 6) {
        return -1;
    }
    uint32_t total = rd32(d);
    if (total < 4 || total > LZSS_MAX || (uint64_t)total * 9 + 64 < (uint64_t)(size - 4) * 8) {
        return -1;
    }
    /* The game's walk (port/src/game/main/lzss.c): whole groups of eight
     * items, while the output is short of the size. */
    uint32_t o = 0;
    int i = 4;
    while (o < total) {
        if (i >= size + LZSS_SLACK) {
            return -1;
        }
        int flags = d[i++];
        for (int bit = 0; bit < 8; bit++, flags >>= 1) {
            if (!(flags & 1)) {
                i++;
                o++;
                continue;
            }
            if (i + 1 >= size + LZSS_SLACK) {
                return -1;
            }
            uint32_t back = d[i] | (d[i + 1] & 0xF) << 8;
            int n = (d[i + 1] >> 4) + 3;
            i += 2;
            if (back == 0 || back > o) {
                return -1;
            }
            o += (uint32_t)n;
        }
        if (i > size + LZSS_SLACK) {
            return -1;
        }
    }
    /* The stream fills the data (a whole file or bundle member), give or
     * take its last group and alignment. */
    if (i < size - 64) {
        return -1;
    }
    *used = i;
    return (int)total;
}

static XgSandbox* decoder;

static XgSandbox* decoderSandbox(void) {
    if (decoder == NULL) {
        decoder = xgSandboxCreate(NULL, 64u << 20);
    } else {
        xgSandboxReset(decoder, 0);
    }
    return decoder;
}

uint8_t* xgLzssDecompress(const uint8_t* data, int size, int* outSize) {
    int used;
    int total = xgLzssCheck(data, size, &used);
    XgSandbox* sb = total > 0 ? decoderSandbox() : NULL;
    if (sb == NULL) {
        return NULL;
    }
    uint8_t* out = calloc(1, (size_t)total + LZSS_OVERRUN);
    xgSandboxEnter(sb);
    uint8_t* src = xgSandboxCopy(data, (size_t)size + LZSS_SLACK);
    uint8_t* dst = xgSandboxAlloc((size_t)total + LZSS_OVERRUN);
    if (out != NULL && src != NULL && dst != NULL) {
        memset(src + size, 0, LZSS_SLACK);
        decompress(src, dst); /* the game's decoder */
        memcpy(out, dst, (size_t)total);
    } else {
        free(out);
        out = NULL;
    }
    xgSandboxLeave(sb);
    *outSize = total;
    return out;
}

/* ---- offset bundles ------------------------------------------------------------------------- */

int xgBundleParse(const uint8_t* d, int size, XgBundle* b) {
    if (size < 8) {
        return 0;
    }
    uint32_t count = rd32(d);
    if (count == 0 || count > 4096 || 4 + 4 * (uint64_t)count > (uint64_t)size) {
        return 0;
    }
    uint32_t first = rd32(d + 4);
    int hasEnd = first == 4 * count + 8;
    if (first != 4 * count + 4 && !hasEnd) {
        return 0;
    }
    uint32_t prev = first;
    for (uint32_t i = 1; i < count; i++) {
        uint32_t o = rd32(d + 4 + 4 * i);
        if (o < prev || o > (uint32_t)size) {
            return 0;
        }
        prev = o;
    }
    int end = size;
    if (hasEnd) {
        uint32_t e = rd32(d + 4 + 4 * count);
        if (e < prev || e > (uint32_t)size + 2048) {
            return 0;
        }
        end = (int)e < size ? (int)e : size;
    }
    b->count = (int)count;
    b->hasEnd = hasEnd;
    b->end = end;
    return 1;
}

int xgBundleMember(const uint8_t* d, int size, const XgBundle* b, int i, int* memberSize) {
    if (i < 0 || i >= b->count) {
        return -1;
    }
    int start = (int)rd32(d + 4 + 4 * i);
    int end = i + 1 < b->count ? (int)rd32(d + 8 + 4 * i) : b->end;
    end = end > size ? size : end;
    *memberSize = end > start ? end - start : 0;
    return start;
}

int xgBundleCheckRelocation(const uint8_t* data, int size) {
    XgBundle b;
    XgSandbox* sb = xgBundleParse(data, size, &b) ? decoderSandbox() : NULL;
    if (sb == NULL) {
        return 0;
    }
    int ok = 0;
    xgSandboxEnter(sb);
    u8* copy = xgSandboxCopy(data, (size_t)size);
    if (copy != NULL) {
        void** table = pcRelocTable((u32*)copy); /* the PC's doPointerRelocation */
        ok = table != NULL;
        for (int i = 1; ok && i <= b.count; i++) {
            ok = (u8*)table[i] == copy + rd32(data + 4 * i);
        }
    }
    xgSandboxLeave(sb);
    return ok;
}

/* ---- TIM ------------------------------------------------------------------------------------ */

int xgTimParse(const uint8_t* d, int size, XgTim* t) {
    if (size < 20 || rd32(d) != 0x10) {
        return 0;
    }
    uint32_t flags = rd32(d + 4);
    int mode = (int)(flags & 7);
    if ((flags & ~0xFu) != 0 || mode > 3) {
        return 0;
    }
    memset(t, 0, sizeof(*t));
    t->bpp = mode == 0 ? 4 : mode == 1 ? 8 : mode == 2 ? 16 : 24;
    t->hasClut = (flags & 8) != 0;
    int p = 8;
    if (t->hasClut) {
        uint32_t bytes = rd32(d + p);
        t->clutX = rd16(d + p + 4);
        t->clutY = rd16(d + p + 6);
        t->clutW = rd16(d + p + 8);
        t->clutH = rd16(d + p + 10);
        if (bytes != 12u + (uint32_t)t->clutW * t->clutH * 2 || t->clutW == 0 || t->clutH == 0 ||
            p + (int64_t)bytes > size || t->clutX >= 1024 || t->clutY >= 512) {
            return 0;
        }
        t->clut = (const uint16_t*)(d + p + 12);
        p += (int)bytes;
    }
    if (p + 12 > size) {
        return 0;
    }
    uint32_t bytes = rd32(d + p);
    t->imgX = rd16(d + p + 4);
    t->imgY = rd16(d + p + 6);
    t->imgW = rd16(d + p + 8);
    t->imgH = rd16(d + p + 10);
    if (bytes != 12u + (uint32_t)t->imgW * t->imgH * 2 || t->imgW == 0 || t->imgH == 0 || p + (int64_t)bytes > size ||
        t->imgX >= 1024 || t->imgY >= 512) {
        return 0;
    }
    t->pixels = d + p + 12;
    t->width = t->bpp == 4 ? t->imgW * 4 : t->bpp == 8 ? t->imgW * 2 : t->bpp == 16 ? t->imgW : t->imgW * 2 / 3;
    t->size = p + (int)bytes;
    return 1;
}

int xgTimRun(const uint8_t* d, int size, int* at, int max) {
    int n = 0, p = 0;
    XgTim t;
    while (p < size && xgTimParse(d + p, size - p, &t)) {
        if (at != NULL && n < max) {
            at[n] = p;
        }
        n++;
        p += (t.size + 3) & ~3;
    }
    /* A pack: the pictures fill it (give or take its last sector). */
    return n >= 1 && p >= size - 2048 ? n : 0;
}

int xgTimClutCount(const XgTim* t) {
    if (!t->hasClut || t->bpp > 8) {
        return 0;
    }
    int colours = t->bpp == 4 ? 16 : 256;
    int perRow = t->clutW >= colours ? t->clutW / colours : 1;
    return perRow * t->clutH;
}

static void clutPlace(const XgTim* t, int clut, int* x, int* y) {
    int colours = t->bpp == 4 ? 16 : 256;
    int perRow = t->clutW >= colours ? t->clutW / colours : 1;
    *x = (clut % perRow) * colours;
    *y = clut / perRow;
}

uint16_t xgTimClutWord(const XgTim* t, int clut) {
    int x = 0, y = 0;
    if (xgTimClutCount(t) > 0) {
        clutPlace(t, clut, &x, &y);
    }
    return (uint16_t)(((t->clutX + x) >> 4 & 0x3F) | (t->clutY + y) << 6);
}

static uint32_t widen(uint16_t c, int zeroClear) {
    uint32_t r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    uint32_t a = zeroClear && c == 0 ? 0 : 255;
    return r | g << 8 | b << 16 | a << 24;
}

void xgTimDecode(const XgTim* t, int clut, int zeroClear, uint32_t* rgba) {
    int cx = 0, cy = 0;
    int haveClut = xgTimClutCount(t) > 0;
    if (haveClut) {
        clut = clut < 0 ? 0 : clut >= xgTimClutCount(t) ? xgTimClutCount(t) - 1 : clut;
        clutPlace(t, clut, &cx, &cy);
    }
    for (int y = 0; y < t->imgH; y++) {
        const uint8_t* row = t->pixels + (size_t)y * t->imgW * 2;
        for (int x = 0; x < t->width; x++) {
            uint32_t out;
            if (t->bpp == 16) {
                out = widen(rd16(row + x * 2), zeroClear);
            } else if (t->bpp == 24) {
                const uint8_t* p = row + x * 3;
                out = p[0] | p[1] << 8 | p[2] << 16 | 0xFF000000u;
            } else {
                int index = t->bpp == 4 ? (row[x / 2] >> ((x & 1) * 4)) & 15 : row[x];
                if (!haveClut) {
                    uint32_t v = (uint32_t)(t->bpp == 4 ? index * 17 : index);
                    out = v | v << 8 | v << 16 | 0xFF000000u;
                } else {
                    int ci = (cy * t->clutW) + cx + index;
                    uint16_t c = ci < t->clutW * t->clutH ? t->clut[ci] : 0;
                    out = widen(c, zeroClear);
                }
            }
            rgba[(size_t)y * t->width + x] = out;
        }
    }
}

/* ---- packed uploads, models ----------------------------------------------------------------- */

int xgTexturesCheck(const uint8_t* d, int size) {
    if (size < 24) {
        return 0;
    }
    uint32_t count = rd32(d);
    if (count == 0 || count > 1024 || 4 + 4 * (uint64_t)count + 16 > (uint64_t)size) {
        return 0;
    }
    int p = 4 + 4 * (int)count;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 16 > size) {
            return 0;
        }
        uint32_t tag = rd32(d + p);
        int w = rd16(d + p + 12), h = rd16(d + p + 14);
        if ((tag != 0x1100 && tag != 0x1101) || w == 0 || h == 0 || w > 1024 || h > 512) {
            return 0;
        }
        p += 16 + w * h * 2;
        if (p > size) {
            return 0;
        }
    }
    return (int)count;
}

int xgModelCheck(const uint8_t* d, int size) {
    if (size < 0x48) {
        return 0;
    }
    uint32_t parts = rd32(d), flags = rd32(d + 4);
    if (parts == 0 || parts > 256 || flags > 3 || rd32(d + 8) != 0 || rd32(d + 12) != 0 ||
        0x10 + parts * 0x38 > (uint32_t)size) {
        return 0;
    }
    for (uint32_t i = 0; i < parts; i++) {
        const uint8_t* b = d + 0x10 + i * 0x38;
        uint32_t lo = 0x10 + parts * 0x38;
        uint16_t vertices = rd16(b + 2), prims = rd16(b + 4), groups = rd16(b + 6);
        if (vertices == 0 || prims == 0 || groups == 0 || groups > prims) {
            return 0;
        }
        for (int k = 0; k < 4; k++) {
            uint32_t o = rd32(b + 8 + 4 * k);
            if (o < lo || o >= (uint32_t)size) {
                return 0;
            }
        }
    }
    return (int)parts;
}

/* ---- sound ---------------------------------------------------------------------------------- */

int xgWdsPresets(const uint8_t* d, int size) {
    if (size < 0x30 || memcmp(d, "wds ", 4) != 0) {
        return 0;
    }
    int n = (int)rd32(d + 0x1C) + 1;
    return n > 0 && 0x30 + n * 16 <= size ? n : 0;
}

int xgWdsBank(const uint8_t* d, int size) {
    return size >= 0x24 ? (int)rd32(d + 0x20) : -1;
}

int xgSedsEffects(const uint8_t* d, int size, int* bank) {
    if (size < 0x20 || memcmp(d, "seds", 4) != 0) {
        return 0;
    }
    *bank = rd16(d + 0x16);
    int n = rd16(d + 0x12);
    return 0x20 + n * 4 <= size ? n : 0;
}

const char* xgSmdsName(const uint8_t* d, int size, char* out, int n) {
    out[0] = '\0';
    if (size < 0x20 || memcmp(d, "smds", 4) != 0) {
        return out;
    }
    int at = rd16(d + 0x1E);
    int k = 0;
    while (at + k < size && k + 1 < n && d[at + k] >= 0x20 && d[at + k] < 0x7F) {
        out[k] = (char)d[at + k];
        k++;
    }
    out[k] = '\0';
    return out;
}
