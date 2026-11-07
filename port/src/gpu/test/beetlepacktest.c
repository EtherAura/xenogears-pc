/* Beetle PSX HW replacement packs without a GPU (texture_beetle.c, texture_pack.c): the
 * upload names and their ownership as measured on the core (docs/BEETLE_PACKS.md), palette
 * names, the folder's rules, samples (one image with its offset, composites with native
 * texels), precedence against the texture pack and the A/B comparison.
 *
 *   beetlepacktest <scratch dir> <settings.ini>
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test/env.h"
#include "pack_fixture.h"
#include "system/settings.h"
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"

/* gp0.c's texture cache hook (vram.c) and the device: none here. */
void gp0VramChanging(VRect r) { (void)r; }
void gvkUpload(VRect r, const uint16_t* src, int pitch) { (void)r; (void)src; (void)pitch; }
void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    for (int y = r.y0; y <= r.y1; y++) memcpy(dst + (y - r.y0) * pitch, &vramShadow[y][r.x0], (size_t)(r.x1 - r.x0 + 1) * 2);
}
void gpuLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
unsigned widePoseSerial(void) { return 0; }
uint32_t hwVSyncCount(void) { return 0; }
int gpuTextureField(void) { return -1; }

/* libgpu.c's gpuLoad / gpuFill / gpuMove, reduced to what the tracker and the texture cache see. */
static void load(int x, int y, int w, int h, const uint16_t* src) {
    for (int j = 0; j < h; ++j) memcpy(&vramShadow[y + j][x], src + (size_t)j * w, (size_t)w * 2);
    vramMarkUploaded((VRect){x, y, x + w - 1, y + h - 1}, 0);
    beetleTrackLoad(x, y, w, h, src);
}
static void fill(int x, int y, int w, int h) {
    for (int j = 0; j < h; ++j) memset(&vramShadow[y + j][x], 0, (size_t)w * 2);
    vramMarkCopied((VRect){x, y, x + w - 1, y + h - 1}, 1);
    beetleTrackFill(x, y, w, h);
}
static void move(int sx, int sy, int dx, int dy, int w, int h) {
    beetleTrackMove(sx, sy, dx, dy, w, h);
    for (int j = 0; j < h; ++j) memmove(&vramShadow[dy + j][dx], &vramShadow[sy + j][sx], (size_t)w * 2);
    vramMarkCopied((VRect){dx, dy, dx + w - 1, dy + h - 1}, 0);
}
static uint32_t seed = 12345;
static uint16_t rnd(void) {
    seed = seed * 1664525u + 1013904223u;
    return (uint16_t)(seed >> 16);
}
static void fillRandom(uint16_t* w, int n, int noZero) {
    for (int i = 0; i < n; ++i) {
        w[i] = rnd();
        if (noZero) w[i] = (uint16_t)((w[i] & 0x7FFF) | 1);
    }
}
static int owner(int x, int y, uint32_t* crc, int* ux, int* uy) {
    int w, h;
    return beetleOwner(x, y, crc, &w, &h, ux, uy);
}
/* An RGBA PNG at path (w x h, every pixel rgba, alpha per column band `alphas` when given). */
static int png(const char* path, int w, int h, const uint8_t rgba[4], const uint8_t* alphas, int bands) {
    uint8_t* px = malloc((size_t)w * h * 4);
    if (!px) return 0;
    for (int i = 0; i < w * h; ++i) {
        memcpy(px + i * 4, rgba, 4);
        if (alphas) px[i * 4 + 3] = alphas[(i % w) * bands / w];
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    int ok = s && SDL_SavePNG(s, path);
    SDL_DestroySurface(s);
    free(px);
    return ok;
}
static char dir[3000];
static int named(char* out, size_t n, const char* fmt, uint32_t a, uint32_t b) {
    char name[64];
    snprintf(name, sizeof(name), fmt, a, b);
    return snprintf(out, n, "%s/%s", dir, name) > 0;
}
static const uint8_t RED[4] = {255, 0, 0, 255}, GREEN[4] = {0, 255, 0, 255}, BLUE[4] = {0, 0, 255, 255};

int main(int argc, char** argv) {
    CHECK(argc >= 3);
    CHECK(testSetEnv("XG_SETTINGS", argv[2]) == 0);
    CHECK(testSetEnv("XG_TEXTURE_PACK", "none") == 0);
    CHECK(SDL_Init(0));
    vramReset();
    textureCacheReset();

    /* The names are zlib's CRC-32 of the halfwords, little-endian (the core's dumps: xg-mrj.1). */
    {
        const uint16_t w[4] = {0x1234, 0xABCD, 0x0000, 0xFFFF};
        CHECK(beetleCrc(w, 4) == 0x8043be1bu);
    }

    /* The pack folder (written before the tracking starts: it needs one file to be on). */
    snprintf(dir, sizeof(dir), "%s/beetle", argv[1]);
    CHECK(SDL_CreateDirectory(dir));
    char path[3400], sub[3400];
    static uint16_t T1[16 * 16], T2[16 * 16], T3[16 * 16], T4[16 * 16], T5[16 * 16], T6[32 * 16], T7[32 * 16],
        T8[16 * 16], T9[16 * 16], T10[16 * 16], A[64 * 64], P1[16], P2[16], Pw[512];
    fillRandom(T1, 256, 0); fillRandom(T2, 256, 0); fillRandom(T3, 256, 0); fillRandom(T4, 256, 0); fillRandom(T5, 256, 0);
    fillRandom(T6, 512, 1); fillRandom(T7, 512, 1); fillRandom(T8, 256, 0); fillRandom(T9, 256, 0); fillRandom(T10, 256, 0);
    fillRandom(A, 64 * 64, 0); fillRandom(P1, 16, 1); fillRandom(P2, 16, 1); fillRandom(Pw, 512, 1);
    P1[0] = 0; /* index 0 clear, as the game's palettes */
    uint32_t p1 = beetleCrc(P1, 16), p2 = beetleCrc(P2, 16);
    /* T1: 2x, alpha bands 0, 1, 127, 128, 254, 255 */
    const uint8_t alphas[6] = {0, 1, 127, 128, 254, 255};
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T1, 256), p1) && png(path, 128, 32, RED, alphas, 6));
    /* T2: upper case hex (the core's Windows builds open it; the port reads either case) */
    {
        char name[64];
        snprintf(name, sizeof(name), "%X-%X.PNG", beetleCrc(T2, 256), p1);
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        CHECK(png(path, 64, 16, GREEN, NULL, 0));
    }
    /* T3: a leading zero (the core asks without one: never applied); T4: in a subfolder (top level only) */
    CHECK(named(path, sizeof(path), "0%x-%x.png", beetleCrc(T3, 256), p1) && png(path, 64, 16, GREEN, NULL, 0));
    snprintf(sub, sizeof(sub), "%s/sub", dir);
    CHECK(SDL_CreateDirectory(sub));
    snprintf(path, sizeof(path), "%s/%x-%x.png", sub, beetleCrc(T4, 256), p1);
    CHECK(png(path, 64, 16, GREEN, NULL, 0));
    /* T5: 3x (refused), T8: 16x (taken at 8x), T9: 1x, T10: 1x */
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T5, 256), p1) && png(path, 192, 48, GREEN, NULL, 0));
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T8, 256), p1) && png(path, 1024, 256, GREEN, NULL, 0));
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T9, 256), p1) && png(path, 64, 16, BLUE, NULL, 0));
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T10, 256), p1) && png(path, 64, 16, BLUE, NULL, 0));
    /* 15-bit: '<upload>-0.png' is applied, the dump's '<upload>.png' is not */
    CHECK(named(path, sizeof(path), "%x-%x.png", beetleCrc(T6, 512), 0) && png(path, 64, 32, GREEN, NULL, 0));
    CHECK(named(path, sizeof(path), "%x.png", beetleCrc(T7, 512), 0) && png(path, 32, 16, GREEN, NULL, 0));

    settingsSet("beetle_pack", dir);
    texturePackReload();
    CHECK(texturePackBeetleEnabled() && beetleTrackEnabled() && texturePackActive());
    BeetlePackStats bs;
    texturePackBeetleStats(&bs);
    CHECK(bs.files == 7); /* the leading zero, the subfolder and '<upload>.png' are not names */

    /* Ownership: per halfword, the last upload wins; a fill lets go; a copy takes the source's places. */
    {
        uint16_t B = 0x5555;
        load(512, 256, 64, 64, A);
        load(533, 269, 1, 1, &B);
        uint32_t crc;
        int ux, uy;
        CHECK(owner(533, 269, &crc, &ux, &uy) && crc == beetleCrc(&B, 1) && ux == 0 && uy == 0);
        CHECK(owner(532, 269, &crc, &ux, &uy) && crc == beetleCrc(A, 64 * 64) && ux == 20 && uy == 13);
        CHECK(owner(534, 269, &crc, &ux, &uy) && crc == beetleCrc(A, 64 * 64) && ux == 22 && uy == 13);
        CHECK(owner(533, 268, &crc, &ux, &uy) && crc == beetleCrc(A, 64 * 64) && ux == 21 && uy == 12);
        fill(520, 276, 4, 2);
        CHECK(!owner(521, 277, &crc, &ux, &uy));
        CHECK(owner(524, 276, &crc, &ux, &uy) && ux == 12 && uy == 20);
        move(512, 256, 640, 256, 16, 16);
        CHECK(owner(645, 259, &crc, &ux, &uy) && crc == beetleCrc(A, 64 * 64) && ux == 5 && uy == 3);
        move(520, 276, 700, 300, 4, 2);
        CHECK(!owner(701, 300, &crc, &ux, &uy));
        /* a state load keeps the owners whose texels VRAM still holds */
        vramShadow[257][514] ^= 1;
        beetleTrackStateLoaded();
        CHECK(!owner(514, 257, &crc, &ux, &uy));
        CHECK(owner(515, 257, &crc, &ux, &uy) && ux == 3 && uy == 1);
    }

    /* Palettes: named by the entries their one upload sent; 'missing' otherwise. */
    {
        uint32_t crc = 0;
        load(0, 480, 16, 1, P1);
        CHECK(beetlePaletteName((uint16_t)(480 << 6 | 0), 16, &crc) && crc == p1);
        load(16, 480, 16, 1, P2);
        uint16_t one = 0x1234;
        load(19, 480, 1, 1, &one);
        CHECK(!beetlePaletteName((uint16_t)(480 << 6 | 1), 16, &crc));
        load(32, 480, 8, 1, P2);
        load(40, 480, 8, 1, P2 + 8);
        CHECK(!beetlePaletteName((uint16_t)(480 << 6 | 2), 16, &crc));
        load(48, 480, 16, 1, P2);
        vramShadow[480][51] ^= 0x7FFF; /* a drawing over an entry: the name stands */
        CHECK(beetlePaletteName((uint16_t)(480 << 6 | 3), 16, &crc) && crc == p2);
        load(0, 481, 512, 1, Pw);
        CHECK(beetlePaletteName((uint16_t)(481 << 6 | 16), 256, &crc) && crc == beetleCrc(Pw + 256, 256));
        load(0, 482, 128, 1, Pw); /* 8-bit: 128 entries owned, 128 not */
        CHECK(!beetlePaletteName((uint16_t)(482 << 6), 256, &crc));
    }

    const uint16_t clut1 = (uint16_t)(480 << 6 | 0), clut3 = (uint16_t)(480 << 6 | 3);
    TextureSample s;
    /* One image covers the request: the image itself, its origin at the upload's. 2x, alpha classes. */
    {
        load(768, 0, 16, 16, T1);
        TextureRequest r = {(uint16_t)(768 / 64), clut1, 0, 0, 0, 64, 16, 0, 0};
        const DecodedTexture* d = textureCacheGet(r);
        CHECK(d);
        TextureRequest patch = {r.tpage, r.clut, 0, 8, 4, 16, 8, 0, 0};
        CHECK(texturePackSample(d, patch, 0, &s));
        CHECK(s.scale == 2 && s.u == 0 && s.v == 0 && s.w == 64 && s.h == 16 && !s.indices);
        const uint8_t want[6] = {0, 128, 128, 128, 128, 255};
        for (int b = 0; b < 6; ++b) CHECK(s.rgba[(b * 128 / 6 + 2) * 4 + 3] == want[b] && (!b || s.rgba[(b * 128 / 6 + 2) * 4] == 255));
        /* the A/B withholds it; another palette has none */
        textureReplacementsShow(0);
        CHECK(!texturePackSample(d, patch, 0, &s));
        textureReplacementsShow(1);
        CHECK(texturePackSample(d, patch, 0, &s));
        TextureRequest other = r;
        other.clut = clut3;
        const DecodedTexture* d2 = textureCacheGet(other);
        CHECK(d2 && !texturePackSample(d2, other, 0, &s));
        /* the same data elsewhere: the same name */
        load(768, 64, 16, 16, T1);
        TextureRequest moved = {(uint16_t)(768 / 64), clut1, 0, 0, 64, 64, 16, 0, 0};
        CHECK(texturePackSample(textureCacheGet(moved), moved, 0, &s) && s.scale == 2 && s.v == 64);
    }
    /* File rules: upper case taken; leading zero, subfolder, 3x and '<upload>.png' not; 16x taken at 8x. */
    {
        struct { uint16_t* t; int ok, scale; } cases[] = {{T2, 1, 1}, {T3, 0, 0}, {T4, 0, 0}, {T5, 0, 0}, {T8, 1, 8}};
        for (int i = 0; i < 5; ++i) {
            load(832, 0, 16, 16, cases[i].t);
            TextureRequest r = {(uint16_t)(832 / 64), clut1, 0, 0, 0, 64, 16, 0, 0};
            int got = texturePackSample(textureCacheGet(r), r, 0, &s);
            CHECK(got == cases[i].ok && (!got || s.scale == cases[i].scale));
        }
        load(896, 0, 32, 16, T6);
        TextureRequest r15 = {(uint16_t)(896 / 64 | 2 << 7), 0, 0, 0, 0, 32, 16, 0, 0};
        CHECK(texturePackSample(textureCacheGet(r15), r15, 0, &s) && s.scale == 2);
        load(896, 0, 32, 16, T7);
        CHECK(!texturePackSample(textureCacheGet(r15), r15, 0, &s));
        texturePackBeetleStats(&bs);
        CHECK(bs.invalid == 1); /* the 3x file */
    }
    /* A request over two uploads: a composite, each texel from its own upload's image (1x and 8x: the
     * composite at 8x); a 1x1 upload inside one keeps its native texels. */
    {
        load(768, 128, 16, 16, T9);
        load(784, 128, 16, 16, T8);
        TextureRequest r = {(uint16_t)(768 / 64), clut1, 0, 32, 128, 64, 16, 0, 0};
        CHECK(texturePackSample(textureCacheGet(r), r, 0, &s));
        CHECK(s.scale == 8 && s.u == 32 && s.v == 128 && s.w == 64 && s.h == 16);
        CHECK(s.rgba[2] == 255 && s.rgba[1] == 0);                       /* T9: blue */
        CHECK(s.rgba[(63 * 8 + 7) * 4 + 1] == 255);                     /* T8: green */
        uint16_t one = 0x1111;                                          /* index 1 in each texel */
        load(768, 192, 16, 16, T10);
        load(770, 193, 1, 1, &one);
        TextureRequest r2 = {(uint16_t)(768 / 64), clut1, 0, 0, 192, 64, 16, 0, 0};
        CHECK(texturePackSample(textureCacheGet(r2), r2, 0, &s) && s.scale == 1);
        const uint8_t* px = s.rgba + (1 * 64 + 8) * 4;                        /* texel (8,1): the 1x1 upload's */
        int r5 = P1[1] & 31;
        CHECK(px[0] == (uint8_t)(r5 << 3 | r5 >> 2) && px[3] == (P1[1] & 0x8000 ? 128 : 255));
        CHECK(s.rgba[(1 * 64 + 12) * 4 + 2] == 255);                   /* beside it: T10's image */
    }
    /* Precedence: the user's texture pack first (beetle_pack_order=ours), or the Beetle pack. */
    {
        char root[3200];
        snprintf(root, sizeof(root), "%s/ours", argv[1]);
        CHECK(SDL_CreateDirectory(root) && packDir(root));
        TextureRequest r = {(uint16_t)(768 / 64), clut1, 0, 0, 0, 64, 16, 0, 0};
        load(768, 0, 16, 16, T1);
        const DecodedTexture* d = textureCacheGet(r);
        CHECK(d);
        uint8_t* mine = malloc(64 * 16 * 4);
        CHECK(mine);
        for (int i = 0; i < 64 * 16; ++i) memcpy(mine + i * 4, BLUE, 4);
        CHECK(packPng(root, d, 0, 64, 16, mine));
        free(mine);
        texturePackSetRoot(root);
        d = textureCacheGet(r);
        CHECK(texturePackSample(d, r, 0, &s) && s.scale == 1 && s.rgba[2] == 255); /* ours */
        texturePackBeetleSetFirst(1);
        CHECK(texturePackSample(d, r, 0, &s) && s.scale == 2 && s.rgba[30 * 4] == 255); /* the Beetle pack's red */
        texturePackBeetleSetFirst(0);
        CHECK(texturePackSample(d, r, 0, &s) && s.scale == 1);
    }
    /* Off: no tracking, no samples. */
    texturePackBeetleSetRoot("");
    CHECK(!texturePackBeetleEnabled() && !beetleTrackEnabled());
    texturePackShutdown();
    SDL_Quit();
    printf("beetle packs: %d checks passed\n", checks);
    return 0;
}
