/*
 * Pictures with text (gametext.h): the word pictures the menus and the
 * battle draw ("Status", "Equip", "Attack"...), from the category's source
 * when the player opts in (text_menus_pictures, text_battle_pictures; off by
 * default):
 *
 *   menus   10/01 (menuSharedResources): member 2 its image bundle (TIMs;
 *           command words are TIM 16, card wording TIM 29), member 3 the sprite table that cuts them
 *           (m_801C65F4.c: bundle[2], bundle[3])
 *   battle  0C/03: member 1 its sprite table, member 2 its atlas (a texture
 *           bundle: a CLUT and a 4-bit image; bld_801E4048.c reloc[1], [2])
 *
 * Measured on the retail images: these are the members whose pictures differ
 * between US and JP (the rest of 10/01 and 0C/03 holds text, portraits and
 * tables; the event slides 04/7FB-852 and the logos 04/AC are the same in
 * both releases; 0C/04 is the battle loader's code).
 *
 * The game asks when it decompresses a member (mallocAndDecompress,
 * include/pc_gametext.h); a member is known by its bytes, the running
 * release's own from its image.
 *   other release, text image  that image's member, the pictures and the
 *                              sprite table together (they fit each other)
 *   pack                       pictures/<key>/<i>.png drawn over the disc's
 *                              pictures: an indexed PNG in the picture's
 *                              own size and palette, as the pack export
 *                              writes them from the player's discs; keys as
 *                              the text's: menu/<disc>/1/2/<i>,
 *                              battle/common/<disc>/2/<i> (a disc 2 picture
 *                              falls back to disc 1's). Optional <sprite-key>.layout
 *                              files move pieces without changing their texture
 *                              coordinates, sizes, palettes or gameplay data.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/memory.h"
#include "pc_gametext.h"

#include "gametext.h"

enum { PIC_TIMS = GT_PICTURE_TIMS, PIC_TEXTURES = GT_PICTURE_TEXTURES, PIC_SPRITES = GT_PICTURE_SPRITES };

typedef struct {
    int category, dir, file, member, kind, sprites; /* sprites: its sprite table's index here */
    const char* key;
} PicMember;

static const PicMember pics[] = {
    {GT_MENUS, 0x10, 1, 2, PIC_TIMS, 1, "menu/%d/1/2"},
    {GT_MENUS, 0x10, 1, 3, PIC_SPRITES, -1, "menu/%d/1/3"},
    {GT_BATTLE, 0x0C, 3, 1, PIC_SPRITES, -1, "battle/common/%d/1"},
    {GT_BATTLE, 0x0C, 3, 2, PIC_TEXTURES, 2, "battle/common/%d/2"},
};
#define PICS ((int)(sizeof(pics) / sizeof(pics[0])))

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---- the pictures of a member ------------------------------------------------------------ */

typedef struct {
    int index;        /* its place in the member */
    int bpp;          /* 4, 8 */
    int w, h;         /* in pixels */
    uint8_t* pixels;  /* in the member: w * bpp / 8 bytes a row */
    uint16_t pal[256];
    int colors;
} Picture;

/* The palette a texture bundle's image is drawn with: the CLUT most of the
 * sprite table's pieces on it use (the battle atlas: 4-bit). */
static int texturePalette(const uint8_t* bundle, int size, const uint8_t* sprites, int spriteSize, int vx, int vy,
                          int vw, int vh, Picture* p) {
    int bestX = -1, bestY = -1, best = 0, depth = 0;
    if (sprites != NULL && spriteSize >= 8) {
        uint32_t n = rd32(sprites);
        static int clutX[512], clutY[512], uses[512], depths[512];
        int kinds = 0;
        for (uint32_t i = 0; i < n && i < 1024 && 4 + 2 * i + 2 <= (uint32_t)spriteSize; i++) {
            uint32_t at = rd16(sprites + 4 + 2 * i);
            if (at + 4 > (uint32_t)spriteSize) continue;
            uint32_t polys = rd16(sprites + at);
            for (uint32_t k = 0; k < polys && k < 64 && at + 4 + 0x1C * (k + 1) <= (uint32_t)spriteSize; k++) {
                const uint8_t* q = sprites + at + 4 + 0x1C * k;
                int d = rd16(q + 0x10), cx = rd16(q + 0x12), cy = rd16(q + 0x14);
                int tx = rd16(q + 0x16) & 0xFFC0, ty = rd16(q + 0x18) & 0xFF00;
                int u = q[0], v = q[2];
                int x = tx + (d ? u / 2 : u / 4), y = ty + v; /* the piece's first word in VRAM */
                if (x < vx || x >= vx + vw || y < vy || y >= vy + vh) continue;
                int j = 0;
                while (j < kinds && (clutX[j] != cx || clutY[j] != cy || depths[j] != d)) j++;
                if (j == kinds && kinds < 512) {
                    clutX[j] = cx, clutY[j] = cy, depths[j] = d, uses[j] = 0;
                    kinds++;
                }
                if (j < kinds && ++uses[j] > best) {
                    best = uses[j], bestX = cx, bestY = cy, depth = d;
                }
            }
        }
    }
    if (bestX < 0) return 0;
    p->bpp = depth ? 8 : 4;
    p->colors = depth ? 256 : 16;
    /* the CLUT entries of the bundle hold it */
    uint32_t n = rd32(bundle);
    for (uint32_t i = 0; i < n && 4 + 4 * i + 4 <= (uint32_t)size; i++) {
        uint32_t at = rd32(bundle + 4 + 4 * i);
        if (at + 16 > (uint32_t)size || rd32(bundle + at) != 0x1101) continue;
        int x = rd16(bundle + at + 4), y = rd16(bundle + at + 6), w = rd16(bundle + at + 12), h = rd16(bundle + at + 14);
        if (bestX < x || bestX + p->colors > x + w || bestY < y || bestY >= y + h) continue;
        const uint8_t* c = bundle + at + 16 + ((bestY - y) * w + (bestX - x)) * 2;
        if (c + p->colors * 2 > bundle + size) return 0;
        for (int k = 0; k < p->colors; k++) p->pal[k] = rd16(c + 2 * k);
        return 1;
    }
    return 0;
}

/* The member's pictures (pointing into `data`); returns their number. */
static int memberPictures(int kind, uint8_t* data, int size, const uint8_t* sprites, int spriteSize, Picture* out,
                          int max) {
    int count = 0;
    if (size < 8) return 0;
    uint32_t n = rd32(data);
    if (n == 0 || n > 256 || 4 + 4 * n > (uint32_t)size) return 0;
    for (uint32_t i = 0; i < n && count < max; i++) {
        uint32_t at = rd32(data + 4 + 4 * i);
        Picture* p = &out[count];
        memset(p, 0, sizeof(*p));
        if (kind == PIC_TIMS) {
            if (at + 8 > (uint32_t)size || rd32(data + at) != 0x10) continue;
            uint32_t flags = rd32(data + at + 4), q = at + 8;
            int bpp = (flags & 3) == 0 ? 4 : (flags & 3) == 1 ? 8 : 16;
            if (flags & 8) {
                if (q + 12 > (uint32_t)size) continue;
                uint32_t len = rd32(data + q);
                int cw = rd16(data + q + 8);
                p->colors = bpp == 4 ? 16 : 256;
                if (cw < p->colors || q + 12 + p->colors * 2 > (uint32_t)size) continue;
                for (int k = 0; k < p->colors; k++) p->pal[k] = rd16(data + q + 12 + 2 * k);
                q += len;
            }
            if (bpp == 16 || !(flags & 8) || q + 12 > (uint32_t)size) continue; /* no palette: left alone */
            int w = rd16(data + q + 8), h = rd16(data + q + 10);
            p->bpp = bpp;
            p->w = w * (bpp == 4 ? 4 : 2);
            p->h = h;
            p->pixels = data + q + 12;
            p->index = (int)i;
            if (q + 12 + (uint32_t)(w * 2 * h) > (uint32_t)size) continue;
            count++;
        } else if (kind == PIC_TEXTURES) {
            if (at + 16 > (uint32_t)size || rd32(data + at) != 0x1100) continue;
            int x = rd16(data + at + 4), y = rd16(data + at + 6), w = rd16(data + at + 12), h = rd16(data + at + 14);
            if (at + 16 + (uint32_t)(w * 2 * h) > (uint32_t)size) continue;
            if (!texturePalette(data, size, sprites, spriteSize, x, y, w, h, p)) continue;
            p->w = w * (p->bpp == 4 ? 4 : 2);
            p->h = h;
            p->pixels = data + at + 16;
            p->index = (int)i;
            count++;
        }
    }
    return count;
}

static int pixelAt(const Picture* p, int x, int y) {
    const uint8_t* row = p->pixels + y * (p->w * p->bpp / 8);
    return p->bpp == 4 ? (row[x / 2] >> ((x & 1) * 4)) & 15 : row[x];
}
static void setPixel(Picture* p, int x, int y, int v) {
    uint8_t* row = p->pixels + y * (p->w * p->bpp / 8);
    if (p->bpp == 4) row[x / 2] = (uint8_t)((row[x / 2] & ~(15 << ((x & 1) * 4))) | (v & 15) << ((x & 1) * 4));
    else row[x] = (uint8_t)v;
}
static void rgbOf(uint16_t c, uint8_t out[4]) {
    int r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    out[0] = (uint8_t)(r << 3 | r >> 2);
    out[1] = (uint8_t)(g << 3 | g >> 2);
    out[2] = (uint8_t)(b << 3 | b >> 2);
    out[3] = c == 0 ? 0 : 255; /* 0: transparent, as the GPU draws it */
}

/* The PNG palette's colour k: the PS1 colour, made unique in its lowest bits
 * (invisible: the 5-bit colour stays the same) so an editor that saves the
 * picture as RGB still gives back each index. */
static void pngColor(const Picture* p, int k, uint8_t out[4]) {
    rgbOf(p->pal[k], out);
    int dup = 0;
    for (int j = 0; j < k; j++) {
        uint8_t q[4];
        rgbOf(p->pal[j], q); /* the same picture colour (the semi-transparency bit is not one) */
        dup += !memcmp(q, out, 4);
    }
    out[0] ^= (uint8_t)(dup & 3);
    out[1] ^= (uint8_t)(dup >> 2 & 3);
    out[2] ^= (uint8_t)(dup >> 4 & 3);
}

/* The picture as an indexed PNG with its palette. */
static int writePng(const char* path, const Picture* p) {
    uint8_t* indices = malloc((size_t)p->w * p->h);
    uint8_t palette[256][4];
    if (indices == NULL) return 0;
    for (int y = 0; y < p->h; y++) {
        for (int x = 0; x < p->w; x++) indices[(size_t)y * p->w + x] = (uint8_t)pixelAt(p, x, y);
    }
    for (int k = 0; k < p->colors; k++) pngColor(p, k, palette[k]);
    int ok = gtPngWrite(path, p->w, p->h, indices, (const uint8_t(*)[4])palette, p->colors);
    free(indices);
    return ok;
}


/* A pack's PNG drawn over the picture: its indices where it is indexed with
 * no more colours than the picture's palette, else each pixel's nearest
 * palette colour (an editor that saved it as RGB). 1: drawn. */
static int drawPng(const char* path, Picture* p) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_Surface* s = SDL_LoadPNG(path);
    if (s == NULL) return 0;
    int ok = s->w == p->w && s->h == p->h;
    int indexed = 0;
    if (ok && s->format == SDL_PIXELFORMAT_INDEX8 && SDL_GetSurfacePalette(s) != NULL) {
        /* the indices themselves (SDL gives a 256-colour palette): when they fit the picture's */
        indexed = 1;
        for (int y = 0; y < p->h && indexed; y++) {
            const uint8_t* row = (const uint8_t*)s->pixels + y * s->pitch;
            for (int x = 0; x < p->w && indexed; x++) indexed = row[x] < p->colors;
        }
    }
    if (!ok) {
        fprintf(stderr, "gametext: %s is %dx%d, the picture %dx%d: not used\n", path, s->w, s->h, p->w, p->h);
    } else if (indexed) {
        for (int y = 0; y < p->h; y++) {
            const uint8_t* row = (const uint8_t*)s->pixels + y * s->pitch;
            for (int x = 0; x < p->w; x++) setPixel(p, x, y, row[x]);
        }
    } else {
        SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        ok = rgba != NULL;
        for (int y = 0; ok && y < p->h; y++) {
            const uint8_t* row = (const uint8_t*)rgba->pixels + y * rgba->pitch;
            for (int x = 0; x < p->w; x++) {
                const uint8_t* c = row + 4 * x;
                int best = 0, bestD = 1 << 30;
                for (int k = 0; k < p->colors; k++) {
                    uint8_t q[4];
                    pngColor(p, k, q);
                    int d = (c[3] < 128) != (q[3] == 0) ? 1 << 29
                                                       : (c[0] - q[0]) * (c[0] - q[0]) + (c[1] - q[1]) * (c[1] - q[1]) +
                                                             (c[2] - q[2]) * (c[2] - q[2]);
                    if (d < bestD) bestD = d, best = k;
                }
                setPixel(p, x, y, best);
            }
        }
        SDL_DestroySurface(rgba);
    }
    SDL_DestroySurface(s);
    return ok;
#else
    (void)path;
    (void)p;
    return 0;
#endif
}

/* ---- the running release's members, and the sources' ------------------------------------ */

static struct {
    int tried[2];
    uint8_t* stored[2][PICS]; /* as the disc stores it (LZSS) */
    int size[2][PICS];
    unsigned generation;
    uint8_t* given[2][PICS]; /* from the source (decompressed); NULL: the disc's */
    int givenSize[2][PICS], givenTried[2][PICS];
} known;

static void loadKnown(int d) {
    if (known.tried[d - 1]) return;
    known.tried[d - 1] = 1;
    GtImage* im = gtRetailImage(d);
    for (int i = 0; i < PICS && im != NULL; i++) {
        known.stored[d - 1][i] = gtImageMember(im, pics[i].dir, pics[i].file, pics[i].member, &known.size[d - 1][i]);
    }
}

static uint8_t* decompressed(const uint8_t* stored, int size, int* out) {
    return stored != NULL ? gtLzss(stored, size + GT_MEMBER_SLACK, out) : NULL;
}

static uint8_t* packMember(GtPack* pack, int d, int i, const uint8_t* base, int baseSize,
                           int* outSize, int* present) {
    *present = 0;
    const char* dir = gtPackDir();
    if (!dir || !pack) return NULL;
    for (int disc = d; disc >= 1; disc--) {
        char key[64], path[4096];
        snprintf(key, sizeof(key), pics[i].key, disc);
        snprintf(path, sizeof(path), "%s/%s/pictures/%s.member", dir, gtPackId(pack), key);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) continue;
        *present = 1;
        SDL_IOStream* io = SDL_IOFromFile(path, "rb");
        Sint64 length = io ? SDL_GetIOSize(io) : -1;
        uint8_t* file = length >= 40 && length <= GT_PICTURE_MEMBER_MAX + 32 ? malloc((size_t)length) : NULL;
        int read = file && SDL_ReadIO(io, file, (size_t)length) == (size_t)length;
        if (io && !SDL_CloseIO(io)) read = 0;
        uint8_t* out = read ? gtPictureRestore(base, baseSize, file, (int)length, pics[i].kind, outSize) : NULL;
        free(file);
        fprintf(stderr, "gametext: picture member %s: %s\n", key,
                out ? "source geometry and palette" : "ignored: invalid data or source fingerprint");
        return out;
    }
    return NULL;
}

/* Read only a bounded placement file. A present but invalid Disc 2 file
 * retains the disc's table, rather than silently trying another layout. */
static uint8_t* packSprites(GtPack* pack, int d, int i, const uint8_t* data, int size, int* outSize) {
    const char* dir = gtPackDir();
    if (!dir || !pack) return NULL;
    for (int disc = d; disc >= 1; disc--) {
        char key[64], path[4096], why[160];
        snprintf(key, sizeof(key), pics[i].key, disc);
        snprintf(path, sizeof(path), "%s/%s/pictures/%s.layout", dir, gtPackId(pack), key);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) continue;
        SDL_IOStream* io = SDL_IOFromFile(path, "rb");
        Sint64 length = io ? SDL_GetIOSize(io) : -1;
        char* text = length > 0 && length <= GT_SPRITE_LAYOUT_MAX ? malloc((size_t)length) : NULL;
        int read = text && SDL_ReadIO(io, text, (size_t)length) == (size_t)length;
        if (io && !SDL_CloseIO(io)) read = 0;
        uint8_t* out = NULL;
        if (read) out = gtSpriteLayoutApply(data, size, text, (size_t)length, outSize, why, sizeof(why));
        else snprintf(why, sizeof(why), "cannot read layout (1..%u bytes)", GT_SPRITE_LAYOUT_MAX);
        free(text);
        if (why[0]) fprintf(stderr, "gametext: placement %s: ignored: %s\n", key, why);
        else if (out) fprintf(stderr, "gametext: placement %s: authored draw offsets\n", key);
        return out;
    }
    return NULL;
}

/* A pack's pictures over the disc's member (decompressed, owned by the caller). */
static int packPictures(GtPack* pack, int d, int i, uint8_t* data, int size) {
    const char* dir = gtPackDir();
    if (dir == NULL || pack == NULL || pics[i].kind == PIC_SPRITES) return 0;
    uint8_t* sprites = NULL;
    int spriteSize = 0;
    if (pics[i].kind == PIC_TEXTURES) {
        int k = 0;
        while (k < PICS && !(pics[k].kind == PIC_SPRITES && pics[k].dir == pics[i].dir && pics[k].file == pics[i].file))
            k++;
        if (k < PICS) {
            sprites = decompressed(known.stored[d - 1][k], known.size[d - 1][k], &spriteSize);
            if (sprites) {
                int present = 0, carriedSize = 0;
                uint8_t* carried = packMember(pack, d, k, sprites, spriteSize, &carriedSize, &present);
                if (carried) { free(sprites); sprites = carried; spriteSize = carriedSize; }
            }
        }
    }
    static Picture list[64];
    int n = memberPictures(pics[i].kind, data, size, sprites, spriteSize, list, 64), drawn = 0;
    for (int k = 0; k < n; k++) {
        for (int disc = d; disc >= 1; disc--) {
            char key[64], path[4096];
            snprintf(key, sizeof(key), pics[i].key, disc);
            snprintf(path, sizeof(path), "%s/%s/pictures/%s/%d.png", dir, gtPackId(pack), key, list[k].index);
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
                drawn += drawPng(path, &list[k]);
                break;
            }
        }
    }
    free(sprites);
    return drawn;
}

/* ---- a source's sprite table, fitted to the running code -------------------------------- */

/* The VRAM words an image member uploads. */
typedef struct {
    int x, y, w, h;
    const uint8_t* words;
} WordRect;

static int imageRects(int kind, const uint8_t* data, int size, WordRect* out, int max) {
    int n = 0;
    uint32_t count = size >= 8 ? rd32(data) : 0;
    if (count > 256 || 4 + 4 * count > (uint32_t)size) return 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        uint32_t at = rd32(data + 4 + 4 * i), q;
        if (kind == PIC_TIMS) {
            if (at + 8 > (uint32_t)size || rd32(data + at) != 0x10) continue;
            q = at + 8;
            if (rd32(data + at + 4) & 8) q += rd32(data + q);
            if (q + 12 > (uint32_t)size) continue;
            q += 4;
        } else {
            if (at + 16 > (uint32_t)size || rd32(data + at) != 0x1100) continue;
            q = at + 4;
        }
        WordRect* r = &out[n];
        r->x = rd16(data + q), r->y = rd16(data + q + 2);
        r->w = rd16(data + q + (kind == PIC_TIMS ? 4 : 8)), r->h = rd16(data + q + (kind == PIC_TIMS ? 6 : 10));
        r->words = data + q + (kind == PIC_TIMS ? 8 : 12);
        if (r->words + (size_t)r->w * r->h * 2 > data + size) continue;
        n++;
    }
    return n;
}

static int wordAt(const WordRect* r, int n, int x, int y) {
    for (int i = 0; i < n; i++) {
        if (x >= r[i].x && x < r[i].x + r[i].w && y >= r[i].y && y < r[i].y + r[i].h) {
            return rd16(r[i].words + ((y - r[i].y) * r[i].w + (x - r[i].x)) * 2);
        }
    }
    return -1;
}

/* A sprite record's pieces show the same pixels in both releases' images. */
static int sameRegions(const uint8_t* rec, int length, const WordRect* a, int na, const WordRect* b, int nb) {
    int polys = (int16_t)rd16(rec);
    for (int k = 0; k < polys && 4 + 0x1C * (k + 1) <= length; k++) {
        const uint8_t* q = rec + 4 + 0x1C * k;
        int u = rd16(q), v = (int16_t)rd16(q + 2), w = (int16_t)rd16(q + 4), h = (int16_t)rd16(q + 6);
        int per = rd16(q + 0x10) ? 2 : 4;
        int tx = rd16(q + 0x16) & 0xFFC0, ty = rd16(q + 0x18) & 0xFF00;
        for (int y = ty + v; y < ty + v + h && y < 512; y++) {
            for (int x = tx + u / per; x <= tx + (u + w - 1) / per && x < 1024; x++) {
                if (wordAt(a, na, x, y) != wordAt(b, nb, x, y)) return 0;
            }
        }
    }
    return 1;
}

static int recordLength(const uint8_t* table, int size, int k, const uint8_t** rec) {
    uint32_t at = rd16(table + 4 + 2 * k);
    if (at + 4 > (uint32_t)size) return 0;
    int polys = (int16_t)rd16(table + at);
    if (polys < 0 || polys > 64) return 0;
    int length = 4 + 0x1C * (polys > 0 ? polys : 0);
    if (at + (uint32_t)length > (uint32_t)size) return 0;
    *rec = table + at;
    return length;
}

/* The disc's sprite table with the source's record wherever the two differ
 * and the source's shows other pixels (a word picture: the source's own),
 * the disc's where both only point at the same pixels elsewhere (the code
 * of the two releases asks for some sprites by other numbers: the battle's
 * button icons, its arrows). Measured US with JP: battle 33 of 234 from JP,
 * 13 kept; menu 1 of 385 from JP, 29 kept. */
static uint8_t* fittedSprites(const uint8_t* disc, int discSize, const uint8_t* src, int srcSize,
                              const WordRect* a, int na, const WordRect* b, int nb, int* outSize) {
    uint32_t n = discSize >= 8 ? rd32(disc) : 0, m = srcSize >= 8 ? rd32(src) : 0;
    if (n == 0 || n > 4096 || 4 + 2 * n > (uint32_t)discSize || m > 4096 || 4 + 2 * m > (uint32_t)srcSize) return NULL;
    uint8_t* out = malloc(65536);
    if (out == NULL) return NULL;
    uint32_t o = 4 + 2 * n;
    memcpy(out, disc, 4);
    int taken = 0;
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *dr = NULL, *sr = NULL;
        int dl = recordLength(disc, discSize, (int)k, &dr);
        int sl = k < m ? recordLength(src, srcSize, (int)k, &sr) : 0;
        const uint8_t* rec = dr;
        int length = dl;
        if (sl > 0 && (dl != sl || memcmp(dr, sr, (size_t)dl)) &&
            !(sameRegions(sr, sl, a, na, b, nb) && (dl == 0 || sameRegions(dr, dl, a, na, b, nb)))) {
            rec = sr, length = sl, taken++;
        }
        if (rec == NULL || o + (uint32_t)length > 65535) {
            free(out);
            return NULL;
        }
        out[4 + 2 * k] = (uint8_t)o, out[5 + 2 * k] = (uint8_t)(o >> 8);
        memcpy(out + o, rec, (size_t)length);
        o += (uint32_t)length;
    }
    *outSize = (int)o;
    fprintf(stderr, "gametext: sprite table: %d of %u sprites from the source\n", taken, n);
    return out;
}

static int imageOf(int i) { /* the image member a sprite table cuts */
    for (int k = 0; k < PICS; k++) {
        if (pics[k].kind != PIC_SPRITES && pics[k].dir == pics[i].dir && pics[k].file == pics[i].file) return k;
    }
    return -1;
}

static uint8_t* imageMember(GtImage* im, int i, int* size) {
    int n;
    uint8_t* stored = gtImageMember(im, pics[i].dir, pics[i].file, pics[i].member, &n);
    uint8_t* out = decompressed(stored, n, size);
    free(stored);
    return out;
}

static uint8_t* sourceMember(int d, int i, const uint8_t* src, int* size) {
    int c = pics[i].category;
    GtImage* im = gtCategoryImage(c, d);
    if (im != NULL) {
        uint8_t* out = imageMember(im, i, size);
        int k = imageOf(i);
        if (out == NULL || pics[i].kind != PIC_SPRITES || k < 0) return out;
        /* a sprite table: fitted to the running code, cutting the source's images */
        int discSize = 0, discImageSize = 0, srcImageSize = 0, fitted = 0;
        uint8_t* disc = decompressed(src, known.size[d - 1][i], &discSize);
        uint8_t* discImage = decompressed(known.stored[d - 1][k], known.size[d - 1][k], &discImageSize);
        uint8_t* srcImage = imageMember(im, k, &srcImageSize);
        uint8_t* merged = NULL;
        if (disc != NULL && discImage != NULL && srcImage != NULL) {
            static WordRect a[64], b[64];
            int na = imageRects(pics[k].kind, discImage, discImageSize, a, 64);
            int nb = imageRects(pics[k].kind, srcImage, srcImageSize, b, 64);
            merged = fittedSprites(disc, discSize, out, *size, a, na, b, nb, &fitted);
        }
        free(disc);
        free(discImage);
        free(srcImage);
        free(out);
        *size = fitted;
        return merged;
    }
    GtPack* pack = gtCategoryPack(c);
    if (pack != NULL) {
        uint8_t* out = decompressed(src, known.size[d - 1][i], size);
        int carried = 0;
        if (out != NULL) {
            int present = 0, carriedSize = 0;
            uint8_t* member = packMember(pack, d, i, out, *size, &carriedSize, &present);
            int k = pics[i].kind == PIC_SPRITES ? imageOf(i) : pics[i].sprites;
            int pairSize = 0, pairPresent = 0, pairOutSize = 0;
            uint8_t* pairBase = k >= 0 ? decompressed(known.stored[d - 1][k], known.size[d - 1][k], &pairSize) : NULL;
            uint8_t* pair = pairBase ? packMember(pack, d, k, pairBase, pairSize, &pairOutSize, &pairPresent) : NULL;
            free(pairBase);
            if ((present || pairPresent) && (!member || !pair)) {
                fprintf(stderr, "gametext: picture member pair: ignored incomplete or invalid source data\n");
                free(pair); free(member); free(out); return NULL;
            }
            free(pair);
            if (member) {
                free(out); out = member; *size = carriedSize; carried = 1;
            }
        }
        if (out != NULL && pics[i].kind == PIC_SPRITES) {
            int movedSize = 0;
            uint8_t* moved = packSprites(pack, d, i, out, *size, &movedSize);
            if (moved) { free(out); *size = movedSize; return moved; }
            if (carried) return out;
            free(out); return NULL;
        }
        if (out != NULL) {
            int drawn = packPictures(pack, d, i, out, *size);
            if (carried || drawn > 0) return out;
        }
        free(out);
    }
    return NULL;
}

u8* pcGameTextMember(void* src, s32 flags) {
    if (src == NULL || !(gameTextPictures(GT_MENUS) || gameTextPictures(GT_BATTLE))) return NULL;
    int d = gtCurrentDisc();
    loadKnown(d);
    if (known.generation != gtGeneration()) {
        known.generation = gtGeneration();
        for (int k = 0; k < 2; k++) {
            for (int i = 0; i < PICS; i++) {
                free(known.given[k][i]);
                known.given[k][i] = NULL;
                known.givenTried[k][i] = 0;
            }
        }
    }
    for (int i = 0; i < PICS; i++) {
        const uint8_t* stored = known.stored[d - 1][i];
        int n = known.size[d - 1][i];
        if (stored == NULL || !gameTextPictures(pics[i].category) || memcmp(src, stored, 8) || memcmp(src, stored, n))
            continue;
        if (!known.givenTried[d - 1][i]) {
            known.givenTried[d - 1][i] = 1;
            known.given[d - 1][i] = sourceMember(d, i, stored, &known.givenSize[d - 1][i]);
            char key[64];
            snprintf(key, sizeof(key), pics[i].key, d);
            fprintf(stderr, "gametext: pictures %s: %s\n", key,
                    known.given[d - 1][i] ? "from the source" : "the disc's (the source has none)");
        }
        if (known.given[d - 1][i] == NULL) return NULL;
        u8* out = allocateHeap(known.givenSize[d - 1][i], flags);
        if (out != NULL) memcpy(out, known.given[d - 1][i], (size_t)known.givenSize[d - 1][i]);
        return out;
    }
    return NULL;
}

/* ---- export: the pictures of a pack -------------------------------------------------------- */

static int makeDirs(const char* path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            SDL_CreateDirectory(tmp);
            *p = '/';
        }
    }
    return SDL_CreateDirectory(tmp);
}

int gtPictureExport(GtImage* disc1, GtImage* disc2, const char* dir, int sourceMembers) {
    GtImage* discs[2] = {disc1, disc2};
    int written = 0, memberFailed = 0;
    uint8_t* first[PICS] = {0};
    int firstSize[PICS] = {0};
    for (int d = 1; d <= 2; d++) {
        if (discs[d - 1] == NULL) continue;
        /* A source's pictures are kept against the running release's own
         * members (the snapshot's fingerprints, the fitted sprite table):
         * without its image of this disc, leave the disc out rather than
         * fail the export (a Disc 2 reader falls back to Disc 1's files). */
        if (sourceMembers && gtRetailImage(d) == NULL) {
            fprintf(stderr, "gametext: pictures of Disc %d not exported: no Disc %d image of the running release\n",
                    d, d);
            continue;
        }
        uint8_t* data[PICS] = {0};
        int size[PICS] = {0};
        uint8_t* base[PICS] = {0};
        int baseSize[PICS] = {0};
        for (int i = 0; i < PICS; i++) {
            int n;
            uint8_t* stored = gtImageMember(discs[d - 1], pics[i].dir, pics[i].file, pics[i].member, &n);
            data[i] = decompressed(stored, n, &size[i]);
            free(stored);
            if (sourceMembers) base[i] = imageMember(gtRetailImage(d), i, &baseSize[i]);
        }
        if (sourceMembers) {
            for (int i = 0; i < PICS; i++) {
                if (pics[i].kind != PIC_SPRITES || !data[i] || !base[i]) continue;
                int k = imageOf(i), fitted = 0;
                uint8_t* merged = NULL;
                if (k >= 0 && base[k] && data[k]) {
                    WordRect a[64], b[64];
                    int na = imageRects(pics[k].kind, base[k], baseSize[k], a, 64);
                    int nb = imageRects(pics[k].kind, data[k], size[k], b, 64);
                    merged = fittedSprites(base[i], baseSize[i], data[i], size[i], a, na, b, nb, &fitted);
                }
                free(data[i]); data[i] = merged; size[i] = fitted;
            }
        }
        for (int i = 0; i < PICS; i++) {
            if (data[i] == NULL) continue;
            if (sourceMembers) {
                int length = 0;
                uint8_t* file = gtPictureSnapshot(base[i], baseSize[i], data[i], size[i], pics[i].kind, &length);
                char key[64], path[4096];
                snprintf(key, sizeof(key), pics[i].key, d);
                snprintf(path, sizeof(path), "%s/pictures/%s.member", dir, key);
                char* slash = strrchr(path, '/');
                if (file && slash) {
                    *slash = 0; int made = makeDirs(path); *slash = '/';
                    SDL_IOStream* io = made ? SDL_IOFromFile(path, "wb") : NULL;
                    int ok = io && SDL_WriteIO(io, file, (size_t)length) == (size_t)length;
                    if (io && !SDL_CloseIO(io)) ok = 0;
                    if (!ok) fprintf(stderr, "gametext: cannot export picture member %s\n", path);
                    if (!ok) memberFailed = 1;
                } else memberFailed = 1;
                free(file);
            }
            /* disc 2's only where it differs from disc 1's (a reader falls back) */
            if (d == 2 && first[i] != NULL && firstSize[i] == size[i] && !memcmp(first[i], data[i], (size_t)size[i]))
                continue;
            if (pics[i].kind == PIC_SPRITES) {
                int length = 0;
                char* text = gtSpriteLayoutTemplate(data[i], size[i], &length);
                char key[64], path[4096];
                snprintf(key, sizeof(key), pics[i].key, d);
                snprintf(path, sizeof(path), "%s/pictures/%s.layout", dir, key);
                char* slash = strrchr(path, '/');
                if (text && slash) {
                    *slash = 0;
                    int made = makeDirs(path);
                    *slash = '/';
                    SDL_IOStream* io = made ? SDL_IOFromFile(path, "wb") : NULL;
                    int ok = io && SDL_WriteIO(io, text, (size_t)length) == (size_t)length;
                    if (io && !SDL_CloseIO(io)) ok = 0;
                    if (!ok) fprintf(stderr, "gametext: cannot export placement template %s\n", path);
                }
                free(text);
                continue;
            }
            int k = 0;
            while (k < PICS && !(pics[k].kind == PIC_SPRITES && pics[k].dir == pics[i].dir && pics[k].file == pics[i].file))
                k++;
            static Picture list[64];
            int n = memberPictures(pics[i].kind, data[i], size[i], k < PICS ? data[k] : NULL, k < PICS ? size[k] : 0,
                                   list, 64);
            char key[64], path[4096];
            snprintf(key, sizeof(key), pics[i].key, d);
            snprintf(path, sizeof(path), "%s/pictures/%s", dir, key);
            makeDirs(path);
            for (int j = 0; j < n; j++) {
                snprintf(path, sizeof(path), "%s/pictures/%s/%d.png", dir, key, list[j].index);
                written += writePng(path, &list[j]);
            }
        }
        for (int i = 0; i < PICS; i++) {
            free(base[i]);
            if (d == 1) {
                first[i] = data[i];
                firstSize[i] = size[i];
            } else {
                free(data[i]);
            }
        }
    }
    for (int i = 0; i < PICS; i++) free(first[i]);
    return memberFailed ? -1 : written;
}
