/*
 * Primitive tests for the GPU layer without the game: LIBGPU calls and GP0
 * packets with known results, checked through StoreImage.
 *
 *   cmake --build build/pc/gpu --target gputest
 *   SDL_VIDEO_DRIVER=offscreen XG_HEADLESS=1 build/pc/gpu/gputest [outdir] [--frames N]
 *
 * Writes <outdir>/gputest_vram.png (default: the current directory) and
 * exits non-zero if a check fails. XG_VK_NO_INTERLOCK=1 tests the fallback.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"
#include "gpu/interpolation.h"
#include "pc_interpolation.h"
#include "system/hw.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetDrawArea(void* p, RECT* r);
void SetDrawOffset(void* p, u16* ofs);
void SetTexWindow(void* p, RECT* tw);
void SetDrawMove(void* p, RECT* rect, s32 x, s32 y);
void SetLineF2(void* p);
void SetLineG2(void* p);
void SetLineF3(void* p);
void SetTile(void* p);
void SetPolyG3(void* p);
void SetPolyFT3(void* p);
void AddPrims(void* ot, void* p0, void* p1);
OTag* ClearOTag(OTag* ot, s32 n);
void SetPriority(void* p, s32 pbc, s32 pbw);
s32 MargePrim(void* p0, void* p1);
u16 LoadClut2(u32* clut, s32 x, s32 y);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);

/* ---- checks ----------------------------------------------------------------------- */

static int failures;
static int checks;

static u16 pixel(int x, int y) {
    RECT r = {(s16)x, (s16)y, 1, 1};
    u32 word = 0;
    StoreImage(&r, &word);
    return (u16)word;
}

static void expect(const char* what, int x, int y, u16 want) {
    u16 got = pixel(x, y);
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: (%d,%d) = %04x, want %04x\n", what, x, y, got, want);
    }
}

static u16 rgb15(int r, int g, int b) {
    return (u16)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

/* Constant shading also has to agree in the upscaled PS1-color copy. */
static void expectShading(const char* what, int x, int y, u16 want) {
    expect(what, x, y, want);
    uint32_t got = 0, color = 0;
    for (int k = 0; k < 3; k++) {
        uint32_t v = (want >> (k * 5)) & 31;
        color |= (v << 3 | v >> 2) << (k * 8);
    }
    if (want & 0x8000) color |= 0x1000000;
    checks++;
    if (!gvkHiRead(0, &got, x * 2 + 1, y * 2 + 1, 1, 1) || (got & 0x1ffffff) != color) {
        failures++;
        printf("FAIL upscaled %s: (%d,%d) = %08x, want %08x\n", what, x, y, got, color);
    }
}

/* The upscaled copy at `scale` (0: none) for the checks that follow, its
 * settings set too: a vertical blank (testclock.c runs them by the wall
 * clock, at any LIBGPU call) applies the settings' scale, which would
 * otherwise switch the copy off half way through a test. */
static int savedScale = -1, savedColor, savedPrecision;

static void useScale(int scale) {
    if (savedScale < 0) {
        savedScale = hiresUpscale();
        savedColor = hiresColor();
        savedPrecision = hiresPrecision();
    }
    hiresSetUpscale(scale ? scale : 1);
    hiresSetColor(HIRES_COLOR_DITHER_1X);
    hiresSetPrecision(0);
    gvkHiConfigure(scale, HIRES_COLOR_DITHER_1X);
}

static void restoreScale(void) {
    gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
    if (savedScale >= 0) {
        hiresSetUpscale(savedScale);
        hiresSetColor(savedColor);
        hiresSetPrecision(savedPrecision);
        savedScale = -1;
    }
}

static void fill(int x, int y, int w, int h, u16 c) {
    u16* buf = malloc((size_t)w * h * 2);
    for (int i = 0; i < w * h; i++) {
        buf[i] = c;
    }
    RECT r = {(s16)x, (s16)y, (s16)w, (s16)h};
    LoadImage(&r, buf);
    free(buf);
}

/* Full-VRAM drawing environment: no offset, clip everything. */
static void fullEnv(void) {
    DRAWENV env;
    SetDefDrawEnv(&env, 0, 0, 1024, 512);
    env.dtd = 0;
    env.dfe = 1;
    PutDrawEnv(&env);
}

/* ---- tests ---------------------------------------------------------------------- */

static void testTransfers(void) {
    /* LoadImage / StoreImage round trip, wrapping at the right edge. */
    enum { W = 40, H = 7 };
    u16 src[W * H], back[W * H + 2];
    for (int i = 0; i < W * H; i++) {
        src[i] = (u16)(i * 2654435761u >> 7);
    }
    RECT r = {1000, 300, W, H};
    LoadImage(&r, src);
    memset(back, 0, sizeof(back));
    StoreImage(&r, (u32*)back);
    checks++;
    if (memcmp(src, back, sizeof(src)) != 0) {
        failures++;
        printf("FAIL LoadImage/StoreImage round trip\n");
    }
    expect("LoadImage wraps", 0, 300, src[24]);

    /* ClearImage: 24-bit colour truncated to 15 bits, no mask bit. */
    RECT c = {200, 400, 30, 10};
    ClearImage(&c, 0xFF, 0x80, 0x10);
    expect("ClearImage", 215, 405, rgb15(0xFF, 0x80, 0x10));
    expect("ClearImage edge", 230, 405, 0);

    /* MoveImage of a clean area (done on the CPU copy) and of a drawn one. */
    RECT m = {200, 400, 8, 8};
    MoveImage(&m, 300, 400);
    expect("MoveImage clean", 303, 403, rgb15(0xFF, 0x80, 0x10));
    fullEnv();
    TILE t;
    SetTile(&t);
    setRGB0(&t, 0, 0xF8, 0);
    t.x0 = 400;
    t.y0 = 400;
    t.w = 8;
    t.h = 8;
    DrawPrim(&t);
    RECT m2 = {400, 400, 8, 8};
    MoveImage(&m2, 500, 400);
    expect("MoveImage drawn", 507, 407, rgb15(0, 0xF8, 0));
    /* Overlapping move: a snapshot of the source. */
    fill(600, 400, 4, 1, 0x1111);
    fill(604, 400, 4, 1, 0x2222);
    RECT m3 = {600, 400, 8, 1};
    MoveImage(&m3, 602, 400);
    expect("MoveImage overlap", 605, 400, 0x1111);
    expect("MoveImage overlap", 606, 400, 0x2222);
}

static void testPolygons(void) {
    fullEnv();
    POLY_F4 f;
    SetPolyF4(&f);
    setRGB0(&f, 0xFF, 0, 0);
    setXYWH(&f, 10, 10, 10, 10);
    DrawPrim(&f);
    expect("POLY_F4 inside", 10, 10, rgb15(0xFF, 0, 0));
    expect("POLY_F4 inside", 19, 19, rgb15(0xFF, 0, 0));
    expect("POLY_F4 right edge excluded", 20, 15, 0);
    expect("POLY_F4 bottom edge excluded", 15, 20, 0);

    /* Gouraud with dithering: a flat-coloured gouraud triangle shows the
     * dither pattern. */
    DRAWENV env;
    SetDefDrawEnv(&env, 0, 0, 1024, 512);
    env.dtd = 1;
    PutDrawEnv(&env);
    POLY_G3 g;
    SetPolyG3(&g);
    setRGB0(&g, 100, 100, 100);
    setRGB1(&g, 100, 100, 100);
    setRGB2(&g, 100, 100, 100);
    g.x0 = 40;
    g.y0 = 40;
    g.x1 = 80;
    g.y1 = 40;
    g.x2 = 40;
    g.y2 = 80;
    DrawPrim(&g);
    static const int dither[16] = {-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2};
    for (int y = 44; y < 48; y++) {
        for (int x = 44; x < 48; x++) {
            int v = (100 + dither[(y & 3) * 4 + (x & 3)]) >> 3;
            expect("POLY_G3 dither", x, y, (u16)(v | v << 5 | v << 10));
        }
    }
    fullEnv();
}

static void testTextures(void) {
    fullEnv();
    /* 4-bit texture at (640, 0): texel u,v has index (u + v) & 15. */
    u16 tex4[4 * 16];
    for (int v = 0; v < 16; v++) {
        for (int w = 0; w < 4; w++) {
            u16 word = 0;
            for (int k = 0; k < 4; k++) {
                word |= (u16)(((w * 4 + k + v) & 15) << (k * 4));
            }
            tex4[v * 4 + w] = word;
        }
    }
    RECT r = {640, 0, 4, 16};
    LoadImage(&r, tex4);
    u16 clut[16];
    for (int i = 0; i < 16; i++) {
        clut[i] = i == 0 ? 0 : (u16)(i | (i * 2) << 5 | (31 - i) << 10 | (i == 15 ? 0x8000 : 0));
    }
    LoadClut2((u32*)clut, 0, 480);
    fill(100, 100, 16, 16, 0x7C1F);
    SPRT s;
    SetSprt(&s);
    setRGB0(&s, 0x80, 0x80, 0x80);
    s.x0 = 100;
    s.y0 = 100;
    s.u0 = 0;
    s.v0 = 0;
    s.w = 16;
    s.h = 16;
    s.clut = GetClut(0, 480);
    DR_TPAGE tp;
    SetDrawTPage(&tp, 1, 0, GetTPage(0, 0, 640, 0));
    DrawPrim(&tp);
    DrawPrim(&s);
    expect("SPRT 4-bit", 101, 102, clut[3]);
    expect("SPRT 4-bit", 115, 115, clut[(15 + 15) & 15]);
    expect("SPRT 4-bit transparent", 100, 100, 0x7C1F);

    /* Modulation: colour 0x40 halves the texel. */
    setRGB0(&s, 0x40, 0x40, 0x40);
    s.x0 = 140;
    DrawPrim(&s);
    {
        u16 c = clut[5];
        u16 want = (u16)(((c & 31) >> 1) | (((c >> 5) & 31) >> 1) << 5 | (((c >> 10) & 31) >> 1) << 10 | (c & 0x8000));
        expect("SPRT modulated", 141, 104, want);
    }

    /* 15-bit texture through a polygon at (704, 0). */
    u16 tex15[8 * 8];
    for (int i = 0; i < 64; i++) {
        tex15[i] = (u16)(0x0400 + i);
    }
    RECT r15 = {704, 0, 8, 8};
    LoadImage(&r15, tex15);
    POLY_FT4 p;
    SetPolyFT4(&p);
    SetShadeTex(&p, 1);
    setRGB0(&p, 0x80, 0x80, 0x80);
    setXYWH(&p, 200, 100, 8, 8);
    setUVWH(&p, 0, 0, 8, 8);
    p.tpage = GetTPage(2, 0, 704, 0);
    p.clut = 0;
    DrawPrim(&p);
    expect("POLY_FT4 15-bit", 203, 105, tex15[5 * 8 + 3]);
    expect("POLY_FT4 15-bit", 207, 107, tex15[7 * 8 + 7]);

    /* 8-bit texture at (768, 0) with a 256-entry CLUT at (0, 481). */
    u16 tex8[8 * 16];
    for (int v = 0; v < 16; v++) {
        for (int w = 0; w < 8; w++) {
            tex8[v * 8 + w] = (u16)(((w * 2 + v * 16) & 0xFF) | ((w * 2 + 1 + v * 16) & 0xFF) << 8);
        }
    }
    RECT r8 = {768, 0, 8, 16};
    LoadImage(&r8, tex8);
    u16 clut8[256];
    for (int i = 0; i < 256; i++) {
        clut8[i] = (u16)(i * 129 + 1);
    }
    RECT rc = {0, 481, 256, 1};
    LoadImage(&rc, clut8);
    s.x0 = 100;
    s.y0 = 140;
    s.w = 16;
    s.h = 16;
    s.clut = GetClut(0, 481);
    setRGB0(&s, 0x80, 0x80, 0x80);
    SetShadeTex(&s, 1);
    SetDrawTPage(&tp, 1, 0, GetTPage(1, 0, 768, 0));
    DrawPrim(&tp);
    DrawPrim(&s);
    expect("SPRT 8-bit raw", 105, 143, clut8[(5 + 3 * 16) & 0xFF]);

    /* Texture window: mask 8 texels, offset 8 (in 8-texel units: 1): u wraps
     * every 8 texels into 8..15. */
    DR_MODE dm;
    RECT tw = {8, 0, 8, 0};
    SetDrawMode(&dm, 1, 0, GetTPage(2, 0, 704, 0), &tw);
    DrawPrim(&dm);
    u16 tex15b[16 * 4];
    for (int i = 0; i < 64; i++) {
        tex15b[i] = (u16)(0x1000 + i);
    }
    RECT r15b = {704, 8, 16, 4};
    LoadImage(&r15b, tex15b);
    SPRT s2;
    SetSprt(&s2);
    SetShadeTex(&s2, 1);
    s2.x0 = 300;
    s2.y0 = 100;
    s2.u0 = 0;
    s2.v0 = 8;
    s2.w = 16;
    s2.h = 1;
    s2.clut = 0;
    DrawPrim(&s2);
    expect("texture window", 300, 100, tex15b[8]);
    expect("texture window", 303, 100, tex15b[11]);
    expect("texture window", 309, 100, tex15b[9]);
    SetDrawMode(&dm, 1, 0, GetTPage(2, 0, 704, 0), NULL);
    DrawPrim(&dm);
}

static void testBlending(void) {
    fullEnv();
    static const u16 back = (u16)(20 | 10 << 5 | 31 << 10);
    for (int m = 0; m < 4; m++) {
        fill(400 + m * 10, 200, 8, 8, back);
        DR_TPAGE tp;
        SetDrawTPage(&tp, 1, 0, GetTPage(0, m, 0, 0));
        DrawPrim(&tp);
        TILE t;
        SetTile(&t);
        SetSemiTrans(&t, 1);
        setRGB0(&t, 8 * 8, 8 * 30, 8 * 4);
        t.x0 = (s16)(400 + m * 10);
        t.y0 = 200;
        t.w = 8;
        t.h = 8;
        DrawPrim(&t);
    }
    expect("blend 0 (B+F)/2", 401, 201, (u16)((20 + 8) / 2 | ((10 + 30) / 2) << 5 | ((31 + 4) / 2) << 10));
    expect("blend 1 B+F", 411, 201, (u16)(28 | 31 << 5 | 31 << 10));
    expect("blend 2 B-F", 421, 201, (u16)(12 | 0 << 5 | 27 << 10));
    expect("blend 3 B+F/4", 431, 201, (u16)(22 | 17 << 5 | 31 << 10));

    /* Many overlapping additive tiles in one ordering table: order matters
     * per pixel only through the read-modify-write. */
    fill(500, 200, 16, 16, 0);
    static TILE tiles[20];
    OTag ot[4];
    ClearOTagR(ot, 4);
    DR_TPAGE tp;
    SetDrawTPage(&tp, 1, 0, GetTPage(0, 1, 0, 0));
    for (int i = 0; i < 20; i++) {
        SetTile(&tiles[i]);
        SetSemiTrans(&tiles[i], 1);
        setRGB0(&tiles[i], 8, 16, 8);
        tiles[i].x0 = (s16)(500 + (i & 3));
        tiles[i].y0 = 200;
        tiles[i].w = 8;
        tiles[i].h = 8;
        AddPrim(&ot[1], &tiles[i]);
    }
    AddPrim(&ot[2], &tp);
    DrawOTag(&ot[3]);
    expect("20 additive tiles", 504, 204, (u16)(20 | 31 << 5 | 20 << 10));
    expect("10 additive tiles", 501, 204, (u16)(10 | 20 << 5 | 10 << 10));
}

static void testMask(void) {
    fullEnv();
    fill(600, 200, 8, 8, 0);
    struct {
        OTag tag;
        u32 code[2];
    } prio;
    SetPriority(&prio, 0, 1); /* set mask bit while drawing */
    DrawPrim(&prio);
    TILE t;
    SetTile(&t);
    setRGB0(&t, 0xF8, 0, 0);
    t.x0 = 600;
    t.y0 = 200;
    t.w = 4;
    t.h = 4;
    DrawPrim(&t);
    expect("set mask", 601, 201, (u16)(0x8000 | 31));
    SetPriority(&prio, 1, 0); /* check mask */
    DrawPrim(&prio);
    setRGB0(&t, 0, 0xF8, 0);
    t.w = 8;
    DrawPrim(&t);
    expect("check mask keeps", 601, 201, (u16)(0x8000 | 31));
    expect("check mask draws", 605, 201, (u16)(31 << 5));
    SetPriority(&prio, 0, 0);
    DrawPrim(&prio);
}

/* Textured and Gouraud triangles. Expected values: black-box outputs of a
 * reference emulator (Beetle PSX 4815633, software renderer, 1x) for the
 * same packets. */
static void testNativeAttributes(void) {
    fullEnv();
    u16* pixels = malloc(256 * 256 * sizeof(*pixels));
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) {
        pixels[y * 256 + x] = (u16)(((x * 7 + y * 3) % 31 + 1) |
            ((x * 13 + y * 5) % 32) << 5 | ((x * 19 + y * 11) % 32) << 10 |
            ((x * 3 + y * 7) % 256 % 3 != 0 ? 0x8000 : 0));
    }
    RECT texture = {768, 256, 256, 256};
    LoadImage(&texture, (u32*)pixels); free(pixels);
    {
        u32 packet[] = {0x302791e1, 0x004d00b6, 0x008716d4, 0x005000aa, 0x0078bf43, 0x004b00b8};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 50", 179, 77, 0x39f1);
    }
    {
        u32 packet[] = {0x308f6db7, 0x005500d3, 0x00a816d0, 0x004900dd, 0x002e5696, 0x004300db};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 51", 219, 74, 0x44b8);
    }
    {
        u32 packet[] = {0x24d2dbfa, 0x0061005c, 0x0020dcff, 0x00670051, 0x011caed3, 0x0061005e, 0x0000a529};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 62", 93, 97, 0x7cc1);
    }
    {
        u32 packet[] = {0x24d8b74d, 0x006900fd, 0x0020abef, 0x006300e2, 0x011c8452, 0x006500fe, 0x0000050f};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 67", 249, 102, 0x7fec);
    }
    {
        u32 packet[] = {0x24d3fc9e, 0x0059018d, 0x00205dd1, 0x005f0181, 0x011cb110, 0x00620199, 0x00001bc3};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 72", 391, 92, 0xffee);
        expect("triangle attribute case 72", 403, 96, 0x7fff);
    }
    {
        u32 packet[] = {0x242e079f, 0x006201cb, 0x00205646, 0x005d01c1, 0x011c6700, 0x005f01d5, 0x0000d7a1};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 74", 461, 95, 0x8036);
        expect("triangle attribute case 74", 463, 96, 0x2c0d);
    }
    {
        u32 packet[] = {0x24ca08c0, 0x00790133, 0x00201c4b, 0x007d0125, 0x011c4954, 0x006f012d, 0x0000a379};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 84", 297, 118, 0x1836);
    }
    {
        u32 packet[] = {0x3439523f, 0x00890083, 0x0020e9b9, 0x009bc885, 0x00890087, 0x011c28a2, 0x00cf587a, 0x0090008f, 0x0000c82b};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 94", 133, 138, 0xba4b);
        expect("triangle attribute case 94", 137, 139, 0x5a21);
        expect("triangle attribute case 94", 137, 140, 0xf483);
    }
    {
        u32 packet[] = {0x34f3d9d6, 0x00a40121, 0x00202371, 0x005e84cd, 0x009e0125, 0x011c466c, 0x00409fc3, 0x00ae012c, 0x0000ab41};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("triangle attribute case 114", 293, 159, 0x1f08);
        expect("triangle attribute case 114", 290, 163, 0xccd7);
        expect("triangle attribute case 114", 291, 163, 0x47bf);
        expect("triangle attribute case 114", 292, 163, 0xbcab);
        expect("triangle attribute case 114", 293, 163, 0x333e);
        expect("triangle attribute case 114", 294, 163, 0xdb7f);
        expect("triangle attribute case 114", 295, 163, 0xc4e7);
        expect("triangle attribute case 114", 295, 169, 0x122b);
        expect("triangle attribute case 114", 296, 169, 0x8ffd);
        expect("triangle attribute case 114", 297, 169, 0x89ff);
        expect("triangle attribute case 114", 297, 171, 0xab7d);
        expect("triangle attribute case 114", 298, 171, 0x20bf);
        expect("triangle attribute case 114", 299, 173, 0xb3ff);
    }
    /* The same quad with its vertices in mirrored orders. */
    {
        u32 packet[] = {0x2d351509, 0x00c8007d, 0x00200503, 0x00c80062, 0x011c051e,
                        0x00da007d, 0x00001603, 0x00da0062, 0x0000161e};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("reflected quad sampling phase", 98, 209, 0xa4e2);
        expect("reflected quad adjacent texel", 99, 209, 0xdb5a);
        expect("reflected quad third texel", 100, 209, 0x8db3);
    }
    {
        u32 packet[] = {0x2d351509, 0x00d9017d, 0x00200503, 0x00d90162, 0x011c051e,
                        0x00c7017d, 0x00001603, 0x00c70162, 0x0000161e};
        gp0Write(packet, (int)(sizeof(packet) / sizeof(*packet)));
        expect("reversed quad sampling phase", 354, 208, 0xa4e2);
        expect("reversed quad adjacent texel", 355, 208, 0xdb5a);
        expect("reversed quad third texel", 356, 208, 0x8db3);
    }
    fullEnv();
}

/* Adjacent Gouraud pixels; saturating and fractional foregrounds with each
 * blend mode; dither. Expected values: black-box outputs of a reference
 * emulator (Beetle PSX 4815633, software renderer, 1x) for the same
 * primitives. */
static void testAccuracyShading(void) {
    fullEnv();
    int oldScale = hiresUpscale(), oldColor = hiresColor(), oldPrecision = hiresPrecision();
    hiresSetUpscale(2); hiresSetColor(HIRES_COLOR_DITHER_1X); hiresSetPrecision(0);
    checks++;
    if (gvkHiConfigure(2, HIRES_COLOR_DITHER_1X) != 2) {
        failures++;
        printf("FAIL configure upscaled shading copy\n");
        hiresSetUpscale(oldScale); hiresSetColor(oldColor); hiresSetPrecision(oldPrecision);
        return;
    }
    u16 clut[256], tex[64 * 23];
    for (int i = 0; i < 256; i++) {
        clut[i] = (u16)(((i * 7) % 31 + 1) | ((i * 13) % 32) << 5 |
                        ((i * 19) % 32) << 10 | (i % 3 != 0 ? 0x8000 : 0));
    }
    for (int y = 0; y < 23; y++) for (int x = 0; x < 64; x++) {
        u16 word = 0;
        for (int k = 0; k < 4; k++) word |= (u16)(((4 * x * 3 + k * 3 + y * 7) % 16) << (k * 4));
        tex[y * 64 + x] = word;
    }
    RECT palette = {512, 0, 256, 1}, texture = {512, 256, 64, 23};
    LoadImage(&palette, (u32*)clut); LoadImage(&texture, (u32*)tex);
    POLY_GT4 g;
    SetPolyGT4(&g);
    setRGB0(&g, 9, 21, 53); setRGB1(&g, 250, 170, 115);
    setRGB2(&g, 171, 67, 253); setRGB3(&g, 45, 250, 81);
    g.x0 = g.x2 = 162; g.x1 = g.x3 = 189;
    g.y0 = g.y1 = 24; g.y2 = g.y3 = 41;
    g.u0 = g.u2 = 3; g.u1 = g.u3 = 30;
    g.v0 = g.v1 = 5; g.v2 = g.v3 = 22;
    g.clut = GetClut(512, 0); g.tpage = GetTPage(0, 0, 512, 256);
    DrawPrim(&g);
    static const u16 adjacent[] = {0x0481, 0x3001, 0x88c3, 0xb801, 0xac49, 0xa0a6, 0x9522, 0xa8c4};
    for (int i = 0; i < 8; i++) expect("Gouraud adjacent colors", 162 + i, 24, adjacent[i]);

    fill(768, 100, 8, 8, 0xffff);
    POLY_FT4 p;
    SetPolyFT4(&p); SetSemiTrans(&p, 1);
    setRGB0(&p, 255, 255, 255);
    p.u0 = p.u2 = 0; p.u1 = p.u3 = 8;
    p.v0 = p.v1 = 100; p.v2 = p.v3 = 108; p.clut = 0;
    static const int bright[] = {20, 31, 0, 17};
    for (int m = 0; m < 4; m++) {
        int x = 600 + m * 16;
        fill(x, 120, 8, 8, 10 | 10 << 5 | 10 << 10);
        p.x0 = p.x2 = x; p.x1 = p.x3 = x + 8;
        p.y0 = p.y1 = 120; p.y2 = p.y3 = 128;
        p.tpage = GetTPage(2, m, 768, 0);
        DrawPrim(&p);
        expectShading("saturated foreground blend", x + 1, 121,
               (u16)(0x8000 | bright[m] | bright[m] << 5 | bright[m] << 10));
    }
    fill(768, 100, 8, 8, 0x8c63); /* three in every channel */
    fill(680, 120, 5, 8, 10 | 10 << 5 | 10 << 10);
    setRGB0(&p, 50, 50, 50); p.tpage = GetTPage(2, 2, 768, 0);
    p.x0 = p.x2 = 680; p.x1 = p.x3 = 685;
    DrawPrim(&p);
    expectShading("subtraction quantizes all channels", 684, 121, (u16)(0x8000 | 9 | 9 << 5 | 9 << 10));

    fill(768, 100, 8, 8, 0xffff);
    DRAWENV env;
    SetDefDrawEnv(&env, 0, 0, 1024, 512); env.dtd = 1; PutDrawEnv(&env);
    p.x0 = p.x2 = 600; p.x1 = p.x3 = 608;
    p.y0 = p.y1 = 140; p.y2 = p.y3 = 148;
    p.tpage = GetTPage(2, 0, 768, 0); setRGB0(&p, 52, 52, 52);
    fill(600, 140, 8, 8, 10 | 10 << 5 | 10 << 10);
    DrawPrim(&p);
    for (int y = 140; y < 144; y++) for (int x = 600; x < 604; x++)
        expectShading("dither precedes average blend", x, y, (u16)(0x8000 | 11 | 11 << 5 | 11 << 10));
    SetSemiTrans(&p, 0); setRGB0(&p, 51, 51, 51);
    DrawPrim(&p);
    expectShading("flat modulated polygon dithers", 600, 140, (u16)(0x8000 | 11 | 11 << 5 | 11 << 10));
    expectShading("flat modulated polygon dither phase", 601, 140, (u16)(0x8000 | 12 | 12 << 5 | 12 << 10));
    SetShadeTex(&g, 1); g.tpage = GetTPage(2, 0, 768, 0);
    g.u0 = g.u2 = 0; g.u1 = g.u3 = 8;
    g.v0 = g.v1 = 100; g.v2 = g.v3 = 108;
    g.x0 = g.x2 = 600; g.x1 = g.x3 = 608;
    g.y0 = g.y1 = 160; g.y2 = g.y3 = 168;
    DrawPrim(&g);
    expectShading("raw Gouraud texture ignores dither", 600, 160, 0xffff);
    gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
    hiresSetUpscale(oldScale); hiresSetColor(oldColor); hiresSetPrecision(oldPrecision);
    fullEnv();
}

/* Lines, and rectangles with texture flips. Expected values: black-box
 * outputs of a reference emulator (Beetle PSX 4815633, software renderer,
 * 1x) for the same packets. */
static void testNativeLinesFlips(void) {
    fullEnv();
    {
        static const u32 words[] = {0xe1000400, 0xe2000000, 0xe3000421, 0xe400503e, 0xe5000000, 0xe6000000, 0x50f9f9d8, 0x0013003d, 0x0018772b, 0x00020022};
        fill(32, 0, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 1", 34, 2, 0x0dc5);
        expect("native line 1", 35, 3, 0x11e6);
        expect("native line 1", 36, 3, 0x1607);
        expect("native line 1", 36, 4, 0x5eb3);
    }
    {
        static const u32 words[] = {0xe1000600, 0xe2000000, 0xe3000060, 0xe400547f, 0xe5000000, 0xe6000000, 0x502ab8d0, 0x0002007d, 0x00a53266, 0x00130062};
        fill(96, 0, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 3", 124, 2, 0x5eb3);
        expect("native line 3", 125, 2, 0x16fa);
        expect("native line 3", 123, 3, 0x1ab8);
        expect("native line 3", 124, 3, 0x1ad9);
    }
    {
        static const u32 words[] = {0xe1000620, 0xe2000000, 0xe300b421, 0xe401003e, 0xe5000000, 0xe6000000, 0x526a6838, 0x00370026, 0x000d89f4, 0x00310031};
        fill(32, 44, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 31", 48, 49, 0x5eb3);
        expect("native line 31", 46, 50, 0x5eb3);
        expect("native line 31", 48, 50, 0x67ff);
        expect("native line 31", 44, 51, 0x5eb3);
    }
    {
        static const u32 words[] = {0xe1000460, 0xe2000000, 0xe30214e1, 0xe40260fe, 0xe5000000, 0xe6000000, 0x527d5c1b, 0x008800e1, 0x009344d6, 0x009900f5};
        fill(224, 132, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 97", 229, 139, 0x6ef5);
        expect("native line 97", 229, 140, 0x5eb3);
        expect("native line 97", 230, 140, 0x6ef5);
        expect("native line 97", 230, 141, 0x5eb3);
    }
    {
        static const u32 words[] = {0xe1000640, 0xe2000000, 0xe30378a2, 0xe403bcbd, 0xe5000000, 0xe6000000, 0x52860240, 0x00e200a8, 0x00b322aa, 0x00e700a2};
        fill(160, 220, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 155", 167, 226, 0x5eb3);
        expect("native line 155", 168, 226, 0x1eac);
        expect("native line 155", 166, 227, 0x5eb3);
        expect("native line 155", 167, 227, 0x1aa9);
    }
    {
        static const u32 words[] = {0xe1000660, 0xe2000000, 0xe3037902, 0xe403bd1d, 0xe5000000, 0xe6000000, 0x42a75e30, 0x00df010e, 0x00dd0108};
        fill(256, 220, 32, 22, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 158", 270, 223, 0x7314);
    }
    {
        static const u32 words[] = {0xe1000400, 0xe2000000, 0xe30655f9, 0xe40655f9, 0xe5000000, 0xe6000000, 0x4000f800, 0x019101f5, 0x019901fd};
        fill(505, 405, 1, 1, 0x5eb3);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 258", 505, 405, 0x03e0);
    }
    {
        static const u32 words[] = {0xe1000600, 0xe2000000, 0xe3000000, 0xe407ffff, 0xe5000000, 0xe6000000, 0x5a034ba3, 0x015e01ea, 0x00f18020, 0x016801f4, 0x005010a0, 0x015e01fe, 0x55555555};
        fill(490, 350, 21, 11, 0);
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        expect("native line 259", 490, 350, 0x0089);
        expect("native line 259", 491, 351, 0x0489);
        expect("native line 259", 492, 352, 0x08a8);
        expect("native line 259", 494, 354, 0x14a6);
    }
    fullEnv();
    int oldScale = hiresUpscale(), oldColor = hiresColor(), oldPrecision = hiresPrecision();
    hiresSetUpscale(2); hiresSetColor(HIRES_COLOR_DITHER_1X); hiresSetPrecision(0);
    gvkHiConfigure(2, HIRES_COLOR_DITHER_1X);
    u16 texels[8*8];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        texels[y*8+x] = (u16)(0x8000 | (x+1) | (y+1)<<5 | (x+y+1)<<10);
    RECT tr = {768, 256, 8, 8}; LoadImage(&tr, (u32*)texels);
    for (int fy = 0; fy < 2; fy++) for (int fx = 0; fx < 2; fx++) {
        fill(500, 200, 4, 4, 0);
        u32 words[] = {0xe100051c | (fx<<12) | (fy<<13), 0x65808080, 500 | 200<<16, 4 | 4<<8, 4 | 4<<16};
        gp0Write(words, sizeof(words)/sizeof(words[0]));
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            int u = fx ? 5-x : 4+x, v = fy ? 4-y : 4+y;
            expectShading("sprite texture direction", 500+x, 200+y, texels[v*8+u]);
        }
    }
    gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
    hiresSetUpscale(oldScale); hiresSetColor(oldColor); hiresSetPrecision(oldPrecision);
    fullEnv();
}

static void testAreaOffsetLines(void) {
    fullEnv();
    fill(0, 250, 64, 64, 0);
    struct {
        OTag tag;
        u32 code[2];
    } area, ofs;
    RECT clip = {10, 260, 20, 20};
    SetDrawArea(&area, &clip);
    u16 off[2] = {10, 260};
    SetDrawOffset(&ofs, off);
    DrawPrim(&area);
    DrawPrim(&ofs);
    TILE t;
    SetTile(&t);
    setRGB0(&t, 0, 0, 0xF8);
    t.x0 = -5;
    t.y0 = -5;
    t.w = 40;
    t.h = 40;
    DrawPrim(&t);
    expect("draw area inside", 10, 260, (u16)(31 << 10));
    expect("draw area inside", 29, 279, (u16)(31 << 10));
    expect("draw area outside", 9, 260, 0);
    expect("draw area outside", 30, 279, 0);
    fullEnv();

    fill(100, 250, 32, 32, 0);
    LINE_F2 l;
    SetLineF2(&l);
    setRGB0(&l, 0xF8, 0xF8, 0xF8);
    l.x0 = 100;
    l.y0 = 250;
    l.x1 = 110;
    l.y1 = 255;
    DrawPrim(&l);
    expect("line start", 100, 250, 0x7FFF);
    expect("line end", 110, 255, 0x7FFF);
    expect("line middle", 105, 253, 0x7FFF);

    LINE_F3 l3;
    SetLineF3(&l3);
    setRGB0(&l3, 0xF8, 0, 0);
    l3.x0 = 120;
    l3.y0 = 260;
    l3.x1 = 125;
    l3.y1 = 260;
    l3.x2 = 125;
    l3.y2 = 270;
    DrawPrim(&l3);
    expect("polyline", 122, 260, 31);
    expect("polyline corner", 125, 260, 31);
    expect("polyline end", 125, 270, 31);
    expect("polyline past end", 125, 271, 0);
}

static void testFeedback(void) {
    /* Draw into a 15-bit texture page, then sample it in the same ordering
     * table: the sprite must see the tile. */
    fullEnv();
    fill(832, 64, 16, 16, 0x0001);
    TILE t;
    SetTile(&t);
    setRGB0(&t, 0xF8, 0xF8, 0);
    t.x0 = 832;
    t.y0 = 64;
    t.w = 16;
    t.h = 16;
    SPRT s;
    SetSprt(&s);
    SetShadeTex(&s, 1);
    s.x0 = 700;
    s.y0 = 300;
    s.u0 = 0;
    s.v0 = 64;
    s.w = 16;
    s.h = 16;
    s.clut = 0;
    DR_TPAGE tp;
    SetDrawTPage(&tp, 1, 0, GetTPage(2, 0, 832, 0));
    OTag ot[4];
    ClearOTagR(ot, 4);
    AddPrim(&ot[3], &t);
    AddPrim(&ot[2], &tp);
    AddPrim(&ot[1], &s);
    DrawOTag(&ot[3]);
    expect("render to texture, then sample", 705, 305, (u16)(31 | 31 << 5));

    /* DR_MOVE inside an ordering table, after a draw. */
    struct {
        OTag tag;
        u32 code[5];
    } mv;
    RECT from = {700, 300, 16, 16};
    SetDrawMove(&mv, &from, 750, 300);
    ClearOTagR(ot, 4);
    AddPrim(&ot[0], &mv);
    DrawOTag(&ot[3]);
    expect("DR_MOVE", 755, 305, (u16)(31 | 31 << 5));
}

/* A primitive that samples pixels it draws itself sees, as on the PS1, what
 * its earlier pixels (rows top to bottom, each left to right) wrote, and the
 * old texel where a later pixel writes. */
static void testSelfFeedback(void) {
    fullEnv();
    DR_TPAGE tp;
    SetDrawTPage(&tp, 1, 0, GetTPage(2, 0, 896, 256));
    DrawPrim(&tp);
    u16 row[32];
    for (int i = 0; i < 32; i++) {
        row[i] = (u16)(0x0400 + i * 33);
    }
    /* Each pixel reads the one on its left. Expected values: black-box
     * output of a reference emulator (Beetle PSX 4815633, software renderer,
     * 1x), also read back by an MMIO workload: within each aligned group of
     * four texels only the first sees the preceding write. */
    for (int y = 400; y < 404; y++) {
        RECT r = {900, (s16)y, 32, 1};
        LoadImage(&r, row);
    }
    SPRT s;
    SetSprt(&s);
    SetShadeTex(&s, 1);
    s.x0 = 901;
    s.y0 = 400;
    s.u0 = 900 - 896;
    s.v0 = 400 - 256;
    s.w = 31;
    s.h = 4;
    s.clut = 0;
    DrawPrim(&s);
    for (int x = 901; x < 932; x += 5) {
        int i = x - 901;
        expect("SPRT cached left feedback", x, 402, row[i - (i > 0 && (i & 3) == 0)]);
    }
    /* Each pixel reads the one on its right before that is drawn. */
    for (int y = 410; y < 414; y++) {
        RECT r = {900, (s16)y, 32, 1};
        LoadImage(&r, row);
    }
    s.x0 = 900;
    s.y0 = 410;
    s.u0 = 901 - 896;
    s.v0 = 410 - 256;
    DrawPrim(&s);
    for (int i = 0; i < 31; i += 5) {
        expect("SPRT reading its right pixel", 900 + i, 411, row[i + 1]);
    }
    /* A triangle reading the pixel above: each column smears the pixel
     * above the triangle's top edge down. */
    for (int y = 419; y < 436; y++) {
        RECT r = {900, (s16)y, 32, 1};
        if (y == 419) {
            LoadImage(&r, row);
        } else {
            fill(900, y, 32, 1, 0x7FFF);
        }
    }
    POLY_FT3 p;
    SetPolyFT3(&p);
    SetShadeTex(&p, 1);
    setRGB0(&p, 0x80, 0x80, 0x80);
    p.x0 = 900;
    p.y0 = 420;
    p.x1 = 916;
    p.y1 = 420;
    p.x2 = 900;
    p.y2 = 436;
    p.u0 = p.u2 = 900 - 896;
    p.u1 = 916 - 896;
    p.v0 = p.v1 = 419 - 256;
    p.v2 = 435 - 256;
    p.tpage = GetTPage(2, 0, 896, 256);
    p.clut = 0;
    DrawPrim(&p);
    for (int y = 421; y < 435; y += 3) {
        expect("POLY_FT3 reading the pixel above", 901, y, row[1]);
    }
    expect("POLY_FT3 reading the pixel above", 905, 430, row[5]);
    expect("POLY_FT3 reading the pixel above", 910, 424, row[10]);
}

/* Texels and palettes sampled after VRAM changed under them: by fills,
 * by uploads, by LIBGPU transfers and after GP0(01h). Expected values:
 * black-box outputs of a reference emulator (Beetle PSX 4815633, software
 * renderer, 1x) for the same command sequences, also read back by MMIO
 * workloads. */
static void testNativeCache(void) {
    for (int scale = 0; scale <= 2; scale += 2) {
        useScale(scale);
        fullEnv();
        fill(768, 288, 16, 1, 0x801f);
        uint32_t warm[] = {0x01000000, 0xe100051c, 0x65808080, 0x00280028, 0x00002000, 0x00010010};
        gp0Write(warm, 6);
        uint32_t change[] = {0x0200f800, 0x01200300, 0x00010010,
                             0x65808080, 0x0028003c, 0x00002000, 0x00010010};
        gp0Write(change, 7);
        if (scale) expectShading("resident texture after fill", 62, 40, 0x801f);
        else expect("resident texture after fill", 62, 40, 0x801f);
        uint32_t fresh[] = {0x01000000, 0x65808080, 0x00280050, 0x00002000, 0x00010010};
        gp0Write(fresh, 5);
        expect("texture after explicit flush", 82, 40, 0x03e0);
        fill(768, 288, 16, 1, 0x7c00);
        fresh[2] = 0x00280064;
        gp0Write(fresh + 1, 4);
        expect("texture upload clears lines", 102, 40, 0x7c00);

        fill(512, 288, 4, 1, 0x1111);
        fill(512, 0, 16, 1, 0x801f);
        warm[1] = 0xe1000418; warm[4] = 0x00202000;
        gp0Write(warm, 6);
        uint16_t palette[16];
        for (int i = 0; i < 16; i++) palette[i] = 0x03e0;
        gpuLoad(512, 0, 16, 1, palette); /* raw A0: no LIBGPU cache flush */
        warm[3] = 0x0028003c;
        gp0Write(warm + 2, 4);
        if (scale) expectShading("resident CLUT after upload", 62, 40, 0x801f);
        else expect("resident CLUT after upload", 62, 40, 0x801f);
        warm[3] = 0x00280050;
        gp0Write(warm, 6);
        expect("CLUT after explicit flush", 82, 40, 0x03e0);
        /* LIBGPU _dws/_drs insert GP0(01h), unlike raw A0/C0. */
        gp0Write(warm + 2, 4);
        fill(512, 0, 16, 1, 0x7c00);
        warm[3] = 0x00280064;
        gp0Write(warm + 2, 4);
        expect("LIBGPU upload clears CLUT", 102, 40, 0x7c00);
        gp0Write(warm + 2, 4);
        gpuLoad(512, 0, 16, 1, palette);
        (void)pixel(0, 0); /* StoreImage's explicit cache clear */
        warm[3] = 0x00280078;
        gp0Write(warm + 2, 4);
        expect("LIBGPU read clears CLUT", 122, 40, 0x03e0);
    }
    restoreScale();
}

/* The same textured triangle through the ordinary device path and the
 * authored native-wave path, without an active display capture group.
 * Cover fresh/stale cache lines, transparency, gouraud/raw colours, all
 * blends, dither and masks; compare both native and enhanced pixels. */
static void testNativeWave(void) {
    static uint16_t image[VRAM_W * VRAM_H];
    uint16_t source[256], background[256], expected[256];
    uint32_t enhanced[1024], expectedHi[1024];
    const uint16_t colors[] = {0,0x801f,0x03e0,0xfc00,0x4210,0xffff,1,0x8000};
    int saved = interpolationEnabled(), owner;
    interpolationSetEnabled(1); useScale(2);
    for (int i = 0; i < 256; i++) {
        source[i] = colors[(i * 5 + i / 16) & 7];
        background[i] = (uint16_t)((i * 137) & 32767) | (i & 1 ? 32768 : 0);
    }
    for (int depth = 0; depth < 3; depth++)
    for (int family = 0; family < 5; family++)
    for (unsigned mode = 0; mode < 128; mode++) for (int stale = 0; stale < (family == 2 || family == 4 ? 1 : 2); stale++) {
        for (int wave = 0; wave < 2; wave++) {
            interpolationReset(); fullEnv();
            uint32_t maskOff = 0xe6000000; gp0Write(&maskOff,1);
            RECT src = {768,288,16,16}, dst = {40,40,16,16};
            /* Start both paths with the same enhanced backdrop too. A
             * mostly unchanged masked draw can otherwise make LoadImage
             * retain the preceding iteration's subpixel detail. */
            gpuFill(40,40,16,16,0);
            LoadImage(&src,source); LoadImage(&dst,background);
            uint16_t palette[256]; for(int i=0;i<256;i++) palette[i]=colors[i&7];
            RECT pal={512,400,256,1}; LoadImage(&pal,palette);
            unsigned clut=(400u<<6)|(512u>>4), basePage=0x41c | (unsigned)depth<<7;
            uint32_t warm[] = {0x01000000,0xe100051c,0x65808080,0x012c0008,0x00002000,0x00100010};
            warm[1]=0xe1000000|basePage; warm[4]|=clut<<16;
            gp0Write(warm,6);
            if (stale) {
                uint32_t change[] = {0x02f80000,0x01200300,0x00100010}; gp0Write(change,3);
            }
            uint32_t page = basePage | (mode & 3) << 5 | (mode & 16 ? 0x200 : 0);
            uint32_t setup[] = {0xe1000000 | page,0xe6000000 | (mode & 32 ? 1 : 0) | (mode & 64 ? 2 : 0)};
            gp0Write(setup,2);
            POLY_GT3 p = {0}; setlen(&p,9);
            p.code = 0x34 | (mode & 4 ? 2 : 0) | (mode & 8 ? 1 : 0);
            p.r0=240;p.g0=144;p.b0=48; p.r1=16;p.g1=80;p.b1=224;p.r2=128;p.g2=192;p.b2=96;
            p.x0=40;p.y0=40;p.x1=56;p.y1=40;p.x2=40;p.y2=56;
            p.u0=0;p.v0=32;p.u1=15;p.v1=32;p.u2=0;p.v2=47;p.tpage=(u16)page; p.clut=(u16)clut;
            POLY_G3 solid = {0};
            setlen(&solid,6); solid.code=0x30 | (mode & 4 ? 2 : 0);
            solid.r0=p.r0; solid.g0=p.g0; solid.b0=p.b0;
            solid.r1=p.r1; solid.g1=p.g1; solid.b1=p.b1;
            solid.r2=p.r2; solid.g2=p.g2; solid.b2=p.b2;
            solid.x0=p.x0; solid.y0=p.y0; solid.x1=p.x1; solid.y1=p.y1; solid.x2=p.x2; solid.y2=p.y2;
            SPRT sprite={0}; setlen(&sprite,4); sprite.code=0x64 | (mode&4?2:0) | (mode&8?1:0);
            sprite.r0=p.r0; sprite.g0=p.g0; sprite.b0=p.b0;
            sprite.x0=40; sprite.y0=40; sprite.w=16; sprite.h=16; sprite.u0=0; sprite.v0=32; sprite.clut=(u16)clut;
            TILE tile={0}; setlen(&tile,3); tile.code=0x60 | (mode&4?2:0);
            tile.r0=p.r0; tile.g0=p.g0; tile.b0=p.b0; tile.x0=40; tile.y0=40; tile.w=16; tile.h=16;
            void* primitive=family==2?(void*)&solid:family==3?(void*)&sprite:family==4?(void*)&tile:(void*)&p;
            if (wave) {
                pcInterpolationFrameBegin(2,0,0,320,224);
                if (family) {
                    /* A regular scene triangle precedes the later wave.
                     * The metadata already describes the whole OT. */
                    pcInterpolationKeyType(&owner,&owner,1,&source,PC_INTERP_WAVE);
                    pcInterpolationKeyType(&owner,&owner,2,primitive,PC_INTERP_MODEL);
                } else {
                    pcInterpolationKeyType(&owner,&owner,1,primitive,PC_INTERP_WAVE);
                    pcInterpolationCapturePrim(primitive,PC_INTERP_CAPTURE_BODY,0,0);
                }
            }
            DrawPrim(primitive);
            if (wave) {
                checks++;
                if (vramIsDrawn((VRect){40,40,55,55})) { failures++; puts("FAIL native wave requires a device download"); }
            }
            gpuReadVram(image);
            checks++;
            if (!gvkHiRead(0,enhanced,80,80,32,32)) { failures++; puts("FAIL native wave high-resolution read"); }
            for (int y=0;y<16;y++) for (int x=0;x<16;x++) {
                int i=y*16+x;uint16_t got=image[(y+40)*VRAM_W+x+40];
                if (!wave) expected[i]=got;
                else { checks++; if (got!=expected[i]) { failures++; printf("FAIL native wave family %d mode %u stale %d (%d,%d)\n",family,mode,stale,x,y); } }
            }
            if (!wave) memcpy(expectedHi,enhanced,sizeof(enhanced));
            else { checks++; if (memcmp(expectedHi,enhanced,sizeof(enhanced))) { failures++; printf("FAIL enhanced native wave family %d mode %u stale %d\n",family,mode,stale); } }
        }
    }
    interpolationReset(); interpolationSetEnabled(saved); restoreScale();
}

static void testCpuOverwrite(void) {
    interpolationReset(); fullEnv(); useScale(0);
    vramOwnedReset(); /* the rectangle below is the device's (earlier tests read these tiles) */
    uint32_t rect[]={0x600000f8,0x00100010,0x00100010};
    uint16_t words[256]; for(int i=0;i<256;i++) words[i]=0x03e0;
    gp0Write(rect,3);
    checks++; if(!vramIsDrawn((VRect){16,16,31,31})) failures++;
    gpuLoad(16,16,16,16,words);
    checks++; if(vramIsDrawn((VRect){16,16,31,31})) { failures++; puts("FAIL upload retains obsolete device pixels"); }
    expect("complete CPU upload",20,20,0x03e0);
    gp0Write(rect,3); gpuLoad(17,17,14,14,words);
    expect("partial CPU upload neighbours",16,16,31);
    expect("partial CPU upload interior",20,20,0x03e0);
    gp0Write(rect,3); gpuFill(16,16,16,16,0x00f800);
    checks++; if(vramIsDrawn((VRect){16,16,31,31})) { failures++; puts("FAIL clear retains obsolete device pixels"); }
    expect("complete CPU clear",20,20,0x03e0);
    rect[1]=0x00200020; gp0Write(rect,3); gpuMove(16,16,32,32,16,16);
    checks++; if(vramIsDrawn((VRect){32,32,47,47})) { failures++; puts("FAIL CPU copy retains obsolete device pixels"); }
    expect("complete CPU copy",36,36,0x03e0);
    restoreScale();
}

/* A tile the CPU read after the frame drew it is drawn by the CPU while the
 * frames that follow draw it again (vram.c vramCpuOwned): the same pixels,
 * and a read needs no device download. */
static void testCpuOwned(void) {
    interpolationReset(); fullEnv(); useScale(0);
    vramOwnedReset();
    uint32_t rect[]={0x600000f8,0x00500050,0x00080008}; /* (80,80) 8x8, red */
    gp0Write(rect,3);
    checks++; if(!vramIsDrawn((VRect){80,80,87,87})) { failures++; puts("FAIL owned: the first draw is the device's"); }
    expect("owned: device draw",82,82,0x001f); /* read back: the tile is now read after a draw */
    rect[0]=0x60f80000; /* blue, the same place */
    gp0Write(rect,3);
    checks++; if(vramIsDrawn((VRect){80,80,87,87})) { failures++; puts("FAIL owned: the redraw needs a device download"); }
    expect("owned: CPU draw",82,82,0x7c00);
    expect("owned: CPU draw keeps the neighbours",88,82,pixel(88,82));
    restoreScale();
}

/* Resolved native words can wait while independent wide work is queued.
 * Overlapping odd-sized boxes must own their inputs, preserve holes, replace
 * opaque black and mask bits, and precede a later enhanced blend. */
static void testResolvedBoxes(void) {
    useScale(2);fullEnv();
    for(int color=0;color<4;color++) {
        hiresSetColor(color);gvkHiConfigure(2,color);
        gpuFill(32,64,48,16,0xf80000);
        uint16_t expected[16][48],words[20];
        for(int y=0;y<16;y++)for(int x=0;x<48;x++)expected[y][x]=0x7c00;
        const VRect boxes[]={{35,67,39,69},{37,68,41,71},{67,67,69,69}};
        for(unsigned k=0;k<sizeof(boxes)/sizeof(boxes[0]);k++) {
            VRect r=boxes[k];int w=r.x1-r.x0+1,h=r.y1-r.y0+1;
            for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
                uint16_t word=(uint16_t)(((x+y*w)*177)&32767)|((x+y)&1?32768:0);
                words[y*w+x]=word;expected[r.y0+y-64][r.x0+x-32]=word;
            }
            gvkHiResolveCpu(r,words,w);memset(words,0xff,sizeof(words));
        }
        gvkWideTouch(127);
        HiVertex wide[3]={0};
        for(int i=0;i<3;i++) {
            wide[i].x=i==1?4:0;wide[i].y=i==2?4:0;wide[i].w=1;
            wide[i].g=248;wide[i].clipMax=127u|127u<<16;
        }
        gvkHiDrawWide(wide,3);
        uint32_t got[96*32];
        checks++;if(!gvkHiRead(0,got,64,128,96,32)) { failures++;continue; }
        for(int y=0;y<32;y++)for(int x=0;x<96;x++) {
            uint16_t word=expected[y/2][x/2];uint32_t want=(word&32768)?0x1000000:0;
            for(int c=0;c<3;c++) { unsigned v=word>>(c*5)&31;want|=(v<<3|v>>2)<<(c*8); }
            checks++;if((got[y*96+x]&0x1ffffff)!=want) { failures++;puts("FAIL queued resolved box union/ownership"); }
        }
        for(int i=0;i<15;i++)words[i]=0x801f;
        gvkHiResolveCpu((VRect){35,67,39,69},words,5);
        HiVertex read[6]={0};const int rx[]={0,10,0,10,0,10},ry[]={32,32,37,32,37,37};
        for(int i=0;i<6;i++) {
            read[i].x=rx[i];read[i].y=ry[i];read[i].w=1;
            read[i].u=rx[i]+31;read[i].v=ry[i]+34;
            read[i].mode=DRAW_TEXTURED|DRAW_RAW;read[i].tex=0x100;
            read[i].uvMin=31u|66u<<16;read[i].uvMax=40u|70u<<16;
            read[i].clipMax=127u|127u<<16;
        }
        gvkHiDrawWide(read,6);gvkBarrier();
        for(int i=0;i<15;i++)words[i]=0x03e0;
        gvkHiResolveCpu((VRect){35,67,39,69},words,5);
        checks++;if(!gvkHiRead(1,got,9,67,1,1) || (got[0]&0x1ffffff)!=0x10000ff) {
            failures++;puts("FAIL wide read owns pending native words");
        }
        checks++;if(!gvkHiRead(1,got,3,67,1,1) || (got[0]&0x1ffffff)!=0xff0000) {
            failures++;puts("FAIL wide read preserves enhanced texels outside pending words");
        }
        for(int i=0;i<15;i++)words[i]=0x801f;
        gvkHiResolveCpu((VRect){35,67,39,69},words,5);
        HiVertex blend[6]={0};const int xs[]={35,40,35,40,35,40},ys[]={67,67,70,67,70,70};
        for(int i=0;i<6;i++) {
            blend[i].x=xs[i];blend[i].y=ys[i];blend[i].w=1;blend[i].b=248;
            blend[i].mode=DRAW_SEMI;blend[i].clipMax=1023u|511u<<16;
        }
        gvkHiDraw(blend,6);
        uint32_t blendWant=color==0?0x7f007f:color==3?0x7c007f:0x7b007b;
        checks++;if(!gvkHiRead(0,got,72,136,1,1) || (got[0]&0x1ffffff)!=blendWant) {
            failures++;puts("FAIL enhanced blend reads queued resolved pixels");
        }
    }
    restoreScale();
}

static void testQuadHalves(void) {
    fullEnv();
    fill(20, 20, 81, 81, 0);
    uint32_t first[] = {0x2810a070, 0x00140014, 0x00140064, 0x00640014, 0x03ff03ff};
    gp0Write(first, 5);
    expect("quad keeps valid first triangle", 30, 30, rgb15(0x70, 0xa0, 0x10));
    fill(20, 20, 81, 81, 0);
    uint32_t second[] = {0x2810a070, 0xfc00fc00, 0x00140064, 0x00640014, 0x00640064};
    gp0Write(second, 5);
    expect("quad keeps valid second triangle", 90, 90, rgb15(0x70, 0xa0, 0x10));
}

/* Overlapping VRAM-to-VRAM copies go row by row, each row in pieces of 128
 * halfwords that read their whole source before they write, on the CPU
 * copy, on the device (a source the GPU drew) and in the upscaled copy.
 * Expected values: black-box outputs of a reference emulator (Beetle PSX
 * 4815633, software renderer, 1x) for the same transfers
 * (work/clean-room/measure/gpu, M-G9). */
/* Residency changes after a state load; one logical move must advance
 * provenance once whether its pixels are copied on the CPU or device. */
static void copyWithProvenanceCheck(RECT* r, int x, int y) {
    uint64_t latest = 0;
    for (int ty = 0; ty < TILES_Y; ty++) for (int tx = 0; tx < TILES_X; tx++) {
        uint64_t g = vramTileGeneration(tx, ty);
        if (g > latest) latest = g;
    }
    MoveImage(r, x, y);
    checks++;
    if (vramTileGeneration(x / 16, y / 16) != latest + 1) {
        failures++; puts("FAIL copy provenance depends on source residency");
    }
}

static void testCopyOrdering(void) {
    static const int right[][2] = {{163, 1}, {290, 128}, {291, 66}, {292, 67}, {353, 128},
                                   {354, 192}, {418, 256}, {419, 194}, {420, 195}, {462, 237}};
    static const int down[4][6] = {{257, 259, 260, 261, 267, 0}, {513, 515, 257, 258, 264, 276},
                                   {769, 771, 513, 514, 261, 273}, {1025, 1027, 769, 770, 258, 270}};
    static const int downX[6] = {100, 102, 103, 104, 110, 122};
    static const int left[][2] = {{600, 41}, {727, 168}, {728, 169}, {849, 290}, {850, 251}, {851, 252}};
    for (int scale = 0; scale <= 2; scale += 2) {
        for (int drawn = 0; drawn < 2; drawn++) {
            useScale(scale);
            fullEnv();
            fill(0, 470, 1024, 30, 0);
            u16 row[400];
            for (int i = 0; i < 400; i++) row[i] = (u16)(i + 1);
            RECT r = {100, 470, 400, 1};
            LoadImage(&r, row);
            for (int k = 0; k < 4; k++) {
                u16 rr[20];
                for (int i = 0; i < 20; i++) rr[i] = (u16)((k + 1) * 0x100 + i + 1);
                RECT rk = {100, (s16)(480 + k), 20, 1};
                LoadImage(&rk, rr);
            }
            RECT rl = {600, 490, 300, 1};
            LoadImage(&rl, row);
            if (drawn) {
                /* The same pixels drawn again (B + 0): the copies' sources
                 * are then the GPU's, copied on the device. */
                DR_TPAGE tp;
                SetDrawTPage(&tp, 1, 0, GetTPage(0, 1, 0, 0));
                DrawPrim(&tp);
                TILE t;
                SetTile(&t);
                SetSemiTrans(&t, 1);
                setRGB0(&t, 0, 0, 0);
                for (int x = 0; x < 1024; x += 512) {
                    t.x0 = (s16)x; t.y0 = 470; t.w = 512; t.h = 30;
                    DrawPrim(&t);
                }
                SetDrawTPage(&tp, 1, 0, GetTPage(0, 0, 0, 0));
                DrawPrim(&tp);
            }
            RECT m1 = {100, 470, 300, 1}, m2 = {100, 480, 20, 3}, m3 = {640, 490, 250, 1};
            copyWithProvenanceCheck(&m1, 163, 470);
            copyWithProvenanceCheck(&m2, 103, 481);
            copyWithProvenanceCheck(&m3, 600, 490);
            const char* what = drawn ? "drawn source copy order" : "copy order";
            for (size_t i = 0; i < sizeof(right) / sizeof(right[0]); i++) {
                if (scale) expectShading(what, right[i][0], 470, (u16)right[i][1]);
                else expect(what, right[i][0], 470, (u16)right[i][1]);
            }
            for (int k = 0; k < 4; k++) {
                for (int i = 0; i < 6; i++) {
                    if (scale) expectShading(what, downX[i], 480 + k, (u16)down[k][i]);
                    else expect(what, downX[i], 480 + k, (u16)down[k][i]);
                }
            }
            for (size_t i = 0; i < sizeof(left) / sizeof(left[0]); i++) {
                if (scale) expectShading(what, left[i][0], 490, (u16)left[i][1]);
                else expect(what, left[i][0], 490, (u16)left[i][1]);
            }
        }
    }
    restoreScale();
    fullEnv();
}

/* A triangle's rows are drawn from its leftmost vertex's row down, then the
 * rows above it upward; the texture cache's blocks are 32 halfwords wide for
 * 8-bit textures and 16 for 4-bit ones; a primitive that draws nothing still
 * loads its palette into the CLUT cache. Expected values: black-box outputs
 * of a reference emulator (Beetle PSX 4815633, software renderer, 1x) for
 * the same words (work/clean-room/measure/gpu/gputest_vectors.py). */
static void testMeasuredOrder(void) {
    fullEnv();
    /* A triangle reading the row below each pixel. */
    u16 pat[48 * 32];
    for (int y = 0; y < 32; y++) for (int x = 0; x < 48; x++) pat[y * 48 + x] = (u16)(0x0400 + x + y * 41);
    RECT pr = {896, 300, 48, 32};
    LoadImage(&pr, pat);
    u32 tri[] = {0x25808080, 930 | 302u << 16, (u32)(47 << 8 | 34), 902 | 316u << 16,
                 (u32)((14 | 1 << 4 | 2 << 7) << 16 | 61 << 8 | 6), 925 | 329u << 16, (u32)(74 << 8 | 29)};
    gp0Write(tri, 7);
    static const int rows[][4] = {{303, 0x498, 0x48e, 0x484}, {306, 0x6d6, 0x509, 0x4ff}, {310, 0x6d6, 0x6cc, 0x5a3},
                                  {314, 0x6d6, 0x6cc, 0x647}, {315, 0x6d6, 0x6cc, 0x6c2}, {316, 0x6d6, 0x6cc, 0x6c2},
                                  {317, 0x6ff, 0x6f5, 0x6eb}, {320, 0x77a, 0x770, 0x73d}, {325, 0x847, 0x814, 0x80a}};
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        expect("triangle row order", 925, rows[i][0], (u16)rows[i][1]);
        expect("triangle row order", 915, rows[i][0], (u16)rows[i][2]);
        expect("triangle row order", 905, rows[i][0], (u16)rows[i][3]);
    }
    /* Warm texel (768, 5), read 16 or 32 halfwords on, draw over it, read it again. */
    u16 clut[256], page[64 * 8];
    for (int i = 0; i < 256; i++) clut[i] = (u16)(0x8000 | ((i * 37 + 5) & 0x7FFF));
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 64; x++) page[y * 64 + x] = (u16)((((x * 7 + y * 13) * 2654435761u) >> 9 & 0x7FFF) | 1);
    RECT cr = {0, 480, 256, 1}, gr = {768, 0, 64, 8};
    LoadImage(&cr, clut);
    LoadImage(&gr, page);
    static const u16 probes[4] = {0xa324, 0x98bc, 0x819c, 0x819c}; /* 8-bit +16 kept, +32 evicted; 4-bit both */
    for (int k = 0; k < 4; k++) {
        int depth = k < 2 ? 1 : 0, step = (k & 1) ? 32 : 16, shift = depth ? 1 : 2;
        u16 old = page[5 * 64], now = (u16)(((old ^ 0x5A5A) & 0x7FFF) | 2);
        u32 w[] = {0x68000000u | (u32)((old >> 10) & 31) << 19 | (u32)((old >> 5) & 31) << 11 | (u32)(old & 31) << 3,
                   768 | 5u << 16, 0x01000000, 0xE1000400u | 12 | (u32)depth << 7,
                   0x6D000000, (u32)(200 + k) | 450u << 16, 480u << 6 << 16 | 5 << 8,
                   0x6D000000, (u32)(210 + k) | 450u << 16, 480u << 6 << 16 | 5 << 8 | (u32)(step << shift),
                   0x68000000u | (u32)((now >> 10) & 31) << 19 | (u32)((now >> 5) & 31) << 11 | (u32)(now & 31) << 3,
                   768 | 5u << 16, 0x6D000000, (u32)(220 + k) | 450u << 16, 480u << 6 << 16 | 5 << 8};
        gp0Write(w, (int)(sizeof(w) / sizeof(w[0])));
        expect("texture cache blocks", 220 + k, 450, probes[k]);
    }
    /* A palette-B sprite outside the drawing area reloads the CLUT cache, so
     * palette A's changed entry shows; without it the old entry stays. */
    u16 clut2[16];
    for (int i = 0; i < 16; i++) clut2[i] = (u16)(0x8000 | ((i * 91 + 3) & 0x7FFF));
    RECT c2 = {256, 480, 16, 1};
    LoadImage(&c2, clut2);
    u32 idx = page[6 * 64] & 15;
    u32 c[] = {0x01000000, 0xE1000400u | 12, 0x6D000000, 230 | 450u << 16, 480u << 6 << 16 | 6 << 8,
               0xE3000000u | 460 << 10, 0xE4000000u | 470 << 10 | 10,
               0x6D000000, 300 | 300u << 16, (480u << 6 | 16) << 16 | 6 << 8,
               0xE3000000, 0xE4000000u | 511 << 10 | 1023,
               0xE6000001, 0x68000000u | 0x9CE7u >> 10 << 19 | (0x9CE7u >> 5 & 31) << 11 | (0x9CE7u & 31) << 3,
               idx | 480u << 16, 0xE6000000, 0x6D000000, 231 | 450u << 16, 480u << 6 << 16 | 6 << 8,
               0xE6000001, 0x68000000u | 31 << 3, idx | 480u << 16, 0xE6000000,
               0x6D000000, 232 | 450u << 16, 480u << 6 << 16 | 6 << 8};
    gp0Write(c, (int)(sizeof(c) / sizeof(c[0])));
    expect("CLUT reloaded by a clipped sprite", 231, 450, 0x9ce7);
    expect("CLUT kept without a palette change", 232, 450, 0x9ce7);
    fullEnv();
}

static void testLibgpuClear(void) {
    fullEnv();
    fill(640, 256, 16, 8, 0x7fff);
    RECT small = {640, 256, 8, 8};
    ClearImage(&small, 0, 0, 0);
    expect("unaligned clear exact last column", 647, 271 - 8, 0);
    expect("unaligned clear preserves next column", 648, 263, 0x7fff);
    fill(1000, 90, 40, 4, 0x7fff);
    RECT edge = {1000, 90, 40, 4};
    ClearImage(&edge, 0, 0, 0);
    expect("unaligned clear reaches right edge", 1023, 91, 0);
    expect("unaligned clear clips at right edge", 0, 91, 0x7fff);
    fill(200, 510, 9, 4, 0x7fff);
    RECT bottom = {200, 510, 9, 4};
    ClearImage(&bottom, 0, 0, 0);
    expect("unaligned clear reaches bottom edge", 204, 511, 0);
    expect("unaligned clear clips at bottom edge", 204, 0, 0x7fff);
    fill(960, 510, 64, 4, 0x7fff);
    RECT aligned = {960, 510, 64, 4};
    ClearImage(&aligned, 0, 0, 0);
    expect("aligned clear fill wraps vertically", 964, 0, 0);
}

static void testOffsetOverflow(void) {
    DRAWENV env;
    POLY_F4 q;
    LINE_F2 line;
    TILE tile;
    SetPolyF4(&q);
    SetLineF2(&line);
    SetTile(&tile);
    setRGB0(&q, 0xF8, 0, 0);
    setRGB0(&line, 0, 0xF8, 0);
    setRGB0(&tile, 0, 0, 0xF8);
    SetDefDrawEnv(&env, 0, 0, 1024, 512);
    env.dfe = 1;

    /* Scene 54 dust: a back-buffer offset takes the lower vertices past
     * +1023. Wrapping that sum stretches an off-screen quad over the frame. */
    fill(0, 256, 320, 224, 0);
    env.ofs[1] = 256;
    PutDrawEnv(&env);
    setXYWH(&q, 10, 700, 20, 100);
    DrawPrim(&q);
    expect("polygon offset past +1023 stays off-screen", 20, 300, 0);

    /* Past -1024 a polygon moves by +2048 as a whole, lines wrap at pixel
     * output, rectangles wrap their origin before traversal. Expected
     * values: black-box outputs of a reference emulator (Beetle PSX
     * 4815633, software renderer, 1x) for the same primitives
     * (work/clean-room/measure/gpu, mgI_wrap.py: the polygon is drawn at
     * x 424..433). */
    fill(400, 0, 64, 80, 0);
    env.ofs[0] = -1024;
    env.ofs[1] = 0;
    PutDrawEnv(&env);
    setXYWH(&q, -600, 10, 10, 10);
    DrawPrim(&q);
    expect("polygon X offset below -1024 moves by +2048", 425, 15, rgb15(0xF8, 0, 0));
    expect("polygon X offset below -1024 moves by +2048", 424, 19, rgb15(0xF8, 0, 0));
    expect("polygon X offset below -1024 keeps its size", 434, 15, 0);
    line.x0 = -600; line.y0 = 30;
    line.x1 = -590; line.y1 = 40;
    DrawPrim(&line);
    expect("line X output wraps below -1024", 429, 35, 0x03c0);
    tile.x0 = -600; tile.y0 = 50; tile.w = tile.h = 10;
    DrawPrim(&tile);
    expect("rectangle X offset still wraps", 425, 55, rgb15(0, 0, 0xF8));

    fill(600, 400, 80, 64, 0);
    env.ofs[0] = 0;
    env.ofs[1] = -1024;
    PutDrawEnv(&env);
    setXYWH(&q, 600, -600, 10, 10);
    DrawPrim(&q);
    expect("polygon Y offset below -1024 moves by +2048", 605, 429, rgb15(0xF8, 0, 0));
    expect("polygon Y offset below -1024 moves by +2048", 600, 424, rgb15(0xF8, 0, 0));
    expect("polygon Y offset below -1024 keeps its size", 605, 434, 0);
    line.x0 = 630; line.y0 = -600;
    line.x1 = 640; line.y1 = -590;
    DrawPrim(&line);
    expect("line Y output wraps below -1024", 635, 429, 0x03c0);
    tile.x0 = 650; tile.y0 = -600;
    DrawPrim(&tile);
    expect("rectangle Y offset still wraps", 655, 429, rgb15(0, 0, 0xF8));
    fullEnv();
}

static int callbackCount;
/* A rewind stores every native backing column, including hidden captures.
 * Restoring it must retain the selected scale and only allocate columns the
 * game used. Hidden data must survive both reuse and later image growth. */
static void testWideRestore(void) {
    enum { H = 4, Y = 23, WIDTH = 320 };
    uint16_t src[WIDE_W * H], back[WIDE_W * H];
    for (int i = 0; i < WIDE_W * H; i++) src[i] = (uint16_t)(i * 40503u + 17u);
    useScale(2);
    gvkUploadWide((VRect){0, Y, WIDTH - 1, Y + H - 1}, src, WIDE_W);
    int allocated = gvkHiWideWidth();
    gvkRestoreWideWidth(WIDTH);
    gvkRestoreWide((VRect){0, Y, WIDE_W - 1, Y + H - 1}, src, WIDE_W);
    checks++;
    if (gvkHiScale() != 2 || gvkHiWideWidth() != allocated || gvkWideUsedWidth() != WIDTH) {
        failures++;
        printf("FAIL wide restore expanded the allocation or changed scale\n");
    }
    gvkReadWide(back, 0, Y, WIDE_W, H);
    for (int i = 0; i < WIDE_W * H; i++) {
        checks++;
        if (back[i] != src[i]) failures++;
    }
    /* Grow well past the restored extent; native hidden data initializes
     * the new columns. Then restore a narrower timeline into that allocation. */
    gvkWideTouch(1503);
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            for (int i = 0; i < WIDE_W * H; i++) src[i] ^= 0xA55A;
            allocated = gvkHiWideWidth();
            gvkRestoreWideWidth(WIDTH);
            gvkRestoreWide((VRect){0, Y, WIDE_W - 1, Y + H - 1}, src, WIDE_W);
            checks++;
            if (gvkHiScale() != 2 || gvkHiWideWidth() != allocated || gvkWideUsedWidth() != WIDTH) failures++;
            gvkWideTouch(1503);
        }
        uint32_t hi[8] = {0};
        checks++;
        if (!gvkHiRead(1, hi, 1500 * 2, Y * 2, 8, 1)) failures++;
        for (int x = 0; x < 8; x++) {
            uint16_t native = src[1500 + x / 2];
            uint32_t color = (uint32_t)(native >> 15) << 24;
            for (int c = 0; c < 3; c++) {
                uint32_t v = (native >> (c * 5)) & 31;
                color |= (v << 3 | v >> 2) << (c * 8);
            }
            checks++;
            if ((hi[x] & 0x1ffffff) != color) {
                failures++;
                printf("FAIL wide hidden restore/grow pass %d pixel %d\n", pass, x);
            }
        }
    }
    restoreScale();
}

/* Unmaterialized hidden rows are native replicas. Growing rows after a
 * restore must initialize them from that timeline while retaining every
 * enhanced bit already written, even beyond the restored column extent. */
static void testWideRows(void) {
    enum { W=128,N=2 };
    uint16_t* native=malloc((size_t)W*WIDE_H*2);
    uint32_t* before=malloc((size_t)W*VRAM_H*N*N*4);
    uint32_t* after=malloc((size_t)W*VRAM_H*N*N*4);
    if (!native || !before || !after) abort();
    for (int i=0;i<W*WIDE_H;i++) native[i]=(uint16_t)(i*17713u+331u);
    gvkHiConfigure(0,HIRES_COLOR_DITHER_1X);
    gvkRestoreWideWidth(W);
    gvkRestoreWide((VRect){0,0,W-1,WIDE_H-1},native,W);
    useScale(N);
    checks++; if (gvkHiWideHeight()!=VRAM_H) { failures++; puts("FAIL rows were eagerly allocated"); }
    HiVertex v[3]={0};
    for (int i=0;i<3;i++) v[i]=(HiVertex){.x=31.25f+(i==1?8:0),.y=21.25f+(i==2?8:0),
        .r=214,.g=42,.b=86,.w=1,.mode=DRAW_PRECISE|DRAW_SURFACE|DRAW_SET_MASK,.clipMax=(W-1)|((WIDE_H-1)<<16)};
    gvkHiDrawWide(v,3);
    checks++; if (!gvkHiRead(1,before,0,0,W*N,VRAM_H*N)) failures++;
    /* Restore native-only hidden rows without allocating them yet. */
    for (int i=VRAM_H*W;i<W*WIDE_H;i++) native[i]^=0xA55A;
    gvkRestoreWide((VRect){0,VRAM_H,W-1,WIDE_H-1},native+VRAM_H*W,W);
    checks++; if (gvkHiWideHeight()!=VRAM_H) { failures++; puts("FAIL native hidden restore grew enhanced rows"); }
    gvkRestoreWideWidth(64);
    gvkCopy(1,20,800,1,20,1400,4,4);
    checks++; if (gvkHiWideWidth()!=W || gvkHiWideHeight()<1404 || gvkHiWideHeight()==WIDE_H) { failures++; puts("FAIL bounded row growth after narrower restore"); }
    checks++; if (!gvkHiRead(1,after,0,0,W*N,VRAM_H*N) || memcmp(before,after,(size_t)W*VRAM_H*N*N*4)) { failures++; puts("FAIL row growth lost enhanced detail"); }
    uint32_t copied[4*N*4*N];
    checks++; if (!gvkHiRead(1,copied,20*N,1400*N,4*N,4*N)) failures++;
    int bad=0;
    for (int y=0;y<4*N;y++) for (int x=0;x<4*N;x++) {
        uint16_t c=native[(800+y/N)*W+20+x/N]; uint32_t want=(uint32_t)(c>>15)<<24;
        for (int k=0;k<3;k++) { uint32_t a=(c>>(k*5))&31;want|=(a<<3|a>>2)<<(k*8); }
        if (copied[y*4*N+x]!=want) bad=1;
    }
    checks++; if (bad) { failures++; puts("FAIL row growth used stale native hidden rows"); }
    /* A direct enhanced draw near the final row grows before it writes. */
    for (int i=0;i<3;i++) v[i].y=2042.25f+(i==2?4:0);
    gvkHiDrawWide(v,3);
    uint32_t hidden[12*12];
    checks++; if (!gvkHiRead(1,hidden,30*N,2042*N,12,12) || gvkHiWideHeight()!=WIDE_H) failures++;
    int detailed=0;for(int i=0;i<144;i++)if(hidden[i]&(1u<<25))detailed=1;
    checks++; if (!detailed) { failures++; puts("FAIL hidden row draw lost enhanced coverage"); }
    gvkHiTrimWide(VRAM_H);
    checks++; if (gvkHiWideHeight()!=VRAM_H) { failures++; puts("FAIL inactive rows not cached"); }
    /* Column growth while rows are cold must retain the hidden enhancement. */
    gvkWideTouch(191);
    uint32_t recovered[144];
    checks++; if (!gvkHiRead(1,recovered,30*N,2042*N,12,12) || memcmp(hidden,recovered,sizeof(hidden))) { failures++; puts("FAIL cold hidden rows changed after column growth"); }
    for (int pass=0;pass<2;pass++) {
        gvkHiTrimWide(VRAM_H+pass*256);
        checks++; if (gvkHiWideHeight()!=VRAM_H+pass*256) failures++;
        checks++; if (!gvkHiRead(1,recovered,30*N,2042*N,12,12) || memcmp(hidden,recovered,sizeof(hidden))) { failures++; puts("FAIL repeated cold row eviction lost detail"); }
    }
    gvkHiTrimWide(VRAM_H);
    gvkRestoreWide((VRect){0,0,W-1,WIDE_H-1},native,W);
    checks++; if (!gvkHiRead(1,recovered,30*N,2042*N,12,12)) failures++;
    detailed=0;for(int i=0;i<144;i++)if(recovered[i]&(1u<<25))detailed=1;
    checks++; if (detailed) { failures++; puts("FAIL native restore retained abandoned cold enhancement"); }
    free(native);free(before);free(after);restoreScale();
}

/* A failed overlap allocation must retain every enhanced bit, including
 * detail absent from native pixels and the hidden bottom capture rows. */
static void testWideGrowthPressure(void) {
    enum { OLD = 448, NEW = 512, N = 2 };
    size_t pixels = (size_t)OLD * WIDE_H * N * N;
    uint32_t* before = malloc(pixels * 4);
    uint32_t* after = malloc(pixels * 4);
    uint16_t* native = malloc((size_t)NEW * WIDE_H * 2);
    uint16_t* back = malloc((size_t)NEW * WIDE_H * 2);
    if (!before || !after || !native || !back) abort();
    const char* env = getenv("XG_TEST_HI_WIDE_GROW_FAIL");
    char* saved = SDL_strdup(env ? env : "0");
    for (int i = 0; i < NEW * WIDE_H; i++) native[i] = (uint16_t)(i * 40503u + 17u);
    for (int pass = 0; pass < 3; pass++) {
        testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", "0");
        gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
        gvkRestoreWideWidth(OLD);
        gvkRestoreWide((VRect){0, 0, NEW - 1, WIDE_H - 1}, native, NEW);
        useScale(N);
        for (int edge = 0; edge < 2; edge++) {
            HiVertex v[3] = {0};
            for (int k = 0; k < 3; k++) v[k] = (HiVertex){
                .x=(edge ? OLD-5.75f : 31.25f)+(k==1 ? 4 : 0),
                .y=(edge ? WIDE_H-4.75f : 23.25f)+(k==2 ? 4 : 0),
                .r=184,.g=40,.b=216,.w=1,.mode=DRAW_PRECISE|DRAW_SURFACE|DRAW_SET_MASK,
                .clipMax=(OLD-1)|((WIDE_H-1)<<16)};
            gvkHiDrawWide(v,3);
        }
        checks++;
        if (!gvkHiRead(1,before,0,0,OLD*N,WIDE_H*N)) { failures++; break; }
        int detailed = 0;
        for (size_t i = 0; i < pixels; i++) if (before[i] & (1u << 25)) { detailed = 1; break; }
        checks++;
        if (!detailed) { failures++; puts("FAIL pressure fixture has no enhanced coverage"); }
        testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", pass == 2 ? "2" : pass ? "1" : "0");
        gvkWideTouch(NEW-1);
        checks++;
        /* growing, the copy takes up to 128 more columns than used (vk.c hiWideGrowBody) */
        if (gvkHiScale()!=N || gvkHiWideWidth()<NEW || gvkHiWideWidth()>NEW+128 || gvkWideUsedWidth()!=NEW) {
            failures++; puts("FAIL pressure growth changed resolution or extent"); break;
        }
        checks++;
        if (!gvkHiRead(1,after,0,0,OLD*N,WIDE_H*N) || memcmp(before,after,pixels*4)) {
            failures++; puts("FAIL pressure growth lost enhanced pixels");
        }
        gvkReadWide(back,0,0,NEW,WIDE_H);
        checks++;
        if (memcmp(native,back,(size_t)NEW*WIDE_H*2)) { failures++; puts("FAIL pressure growth changed native pixels"); }
        checks++;
        if (!gvkHiRead(1,after,OLD*N,0,(NEW-OLD)*N,WIDE_H*N)) { failures++; break; }
        int bad = 0;
        for (int y=0;y<WIDE_H*N && !bad;y++) for (int x=0;x<(NEW-OLD)*N;x++) {
            uint16_t c=native[(y/N)*NEW+OLD+x/N];
            uint32_t want=(uint32_t)(c>>15)<<24;
            for (int k=0;k<3;k++) { uint32_t v=(c>>(k*5))&31; want|=(v<<3|v>>2)<<(k*8); }
            if (after[y*(NEW-OLD)*N+x]!=want) { bad=1; break; }
        }
        checks++;
        if (bad) { failures++; puts("FAIL pressure growth initialized new columns incorrectly"); }
        /* A narrower restored timeline reuses this allocation, then grows
         * again under pressure without losing the restored hidden pixels. */
        gvkRestoreWideWidth(OLD);
        gvkRestoreWide((VRect){0,0,NEW-1,WIDE_H-1},native,NEW);
        gvkWideTouch(575);
        checks++;
        if (gvkHiScale()!=N || gvkHiWideWidth()<576 || gvkHiWideWidth()>576+128) { failures++; puts("FAIL pressure growth after restore"); }
    }
    testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL",saved); SDL_free(saved);
    free(before); free(after); free(native); free(back);
    restoreScale();
}

/* No heap holds the grown wide copy, with (3) or without (4) a staging copy
 * of the old one: degrade instead of aborting. Upscaling is off for the rest
 * of that vblank with exact native pixels, the next configuration is capped
 * at about half the bytes and grows there, and only an explicit switch to the
 * exact picture lifts the cap. */
static void testWideGrowthExhausted(void) {
    enum { OLD = 448, NEW = 512, N = 4, CAP = 2 };
    uint16_t* native = malloc((size_t)NEW * WIDE_H * 2);
    uint16_t* back = malloc((size_t)NEW * WIDE_H * 2);
    if (!native || !back) abort();
    const char* env = getenv("XG_TEST_HI_WIDE_GROW_FAIL");
    char* saved = SDL_strdup(env ? env : "0");
    for (int i = 0; i < NEW * WIDE_H; i++) native[i] = (uint16_t)(i * 2654435761u >> 9);
    for (int pass = 3; pass <= 4; pass++) {
        testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", "0");
        gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
        gvkRestoreWideWidth(OLD);
        gvkRestoreWide((VRect){0, 0, NEW - 1, WIDE_H - 1}, native, NEW);
        useScale(N);
        checks++;
        if (gvkHiScale() != N || gvkHiWideWidth() != OLD) { failures++; puts("FAIL exhausted-growth fixture"); break; }
        testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", pass == 3 ? "3" : "4");
        gvkWideTouch(NEW - 1);
        testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", "0");
        checks++;
        if (gvkHiScale() != 0 || gvkWideUsedWidth() != NEW) { failures++; puts("FAIL exhausted growth did not turn upscaling off"); }
        gvkReadWide(back, 0, 0, NEW, WIDE_H);
        checks++;
        if (memcmp(native, back, (size_t)NEW * WIDE_H * 2)) { failures++; puts("FAIL exhausted growth changed native pixels"); }
        checks++;
        if (gvkHiConfigure(N, HIRES_COLOR_DITHER_1X) != CAP || gvkHiConfigure(N, HIRES_COLOR_DITHER_1X) != CAP ||
            gvkHiWideWidth() != NEW) { failures++; puts("FAIL exhausted scale not capped"); }
        /* Rows still grow at the capped scale, from the exact native rows. */
        uint32_t corner[CAP * CAP];
        checks++;
        if (!gvkHiRead(1, corner, (NEW - 1) * CAP, (WIDE_H - 1) * CAP, CAP, CAP) || gvkHiWideHeight() != WIDE_H ||
            gvkHiScale() != CAP) { failures++; puts("FAIL capped scale could not grow"); }
        uint16_t c = native[(WIDE_H - 1) * NEW + NEW - 1];
        uint32_t want = (uint32_t)(c >> 15) << 24;
        for (int k = 0; k < 3; k++) { uint32_t a = (c >> (k * 5)) & 31; want |= (a << 3 | a >> 2) << (k * 8); }
        checks++;
        if (corner[0] != want || corner[CAP * CAP - 1] != want) { failures++; puts("FAIL capped growth initialized rows incorrectly"); }
        gvkHiConfigure(0, HIRES_COLOR_DITHER_1X);
        checks++;
        if (gvkHiConfigure(N, HIRES_COLOR_DITHER_1X) != N) { failures++; puts("FAIL exact picture did not lift the cap"); }
    }
    testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL", saved); SDL_free(saved);
    free(native); free(back);
    restoreScale();
}

static void onDrawSync(void) {
    callbackCount++;
}

static void testSync(void) {
    DrawSyncCallback(onDrawSync);
    OTag ot[2];
    ClearOTagR(ot, 2);
    TILE t;
    SetTile(&t);
    t.x0 = 0;
    t.y0 = 500;
    t.w = 4;
    t.h = 4;
    AddPrim(&ot[1], &t);
    DrawOTag(&ot[1]);
    DrawSync(0);
    checks++;
    if (callbackCount != 1) {
        failures++;
        printf("FAIL DrawSyncCallback ran %d times, want 1\n", callbackCount);
    }
    DrawSyncCallback(NULL);
}

int main(int argc, char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);
    const char* out = argc > 1 ? argv[1] : ".";
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("gputest", 960, 720, SDL_WINDOW_VULKAN);
    if (!gpuInit(window)) {
        return 1;
    }
    ResetGraph(0);
    SetDispMask(1);
    testTransfers();
    testPolygons();
    testTextures();
    testBlending();
    testMask();
    testNativeLinesFlips();
    testAreaOffsetLines();
    testOffsetOverflow();
    testFeedback();
    testSelfFeedback();
    testNativeAttributes();
    testAccuracyShading();
    testNativeCache();
    testNativeWave();
    testCpuOverwrite();
    testCpuOwned();
    testResolvedBoxes();
    testQuadHalves();
    testCopyOrdering();
    testMeasuredOrder();
    testLibgpuClear();
    testSync();
    testWideRestore();
    testWideRows();
    testWideGrowthPressure();
    testWideGrowthExhausted();

    DISPENV disp;
    SetDefDispEnv(&disp, 0, 0, 320, 240);
    PutDispEnv(&disp);
    gpuVBlank();
    /* --frames N: keep presenting N vblanks (a window shows the test area
     * with a moving bar). */
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0) {
            int frames = atoi(argv[i + 1]);
            fullEnv();
            for (int f = 0; f < frames; f++) {
                TILE bar;
                SetTile(&bar);
                setRGB0(&bar, (u8)(f * 4), 0x80, 0xF8);
                bar.x0 = (s16)(f % 300);
                bar.y0 = 230;
                bar.w = 20;
                bar.h = 8;
                DrawPrim(&bar);
                SDL_PumpEvents();
                gpuVBlank();
                SDL_Delay(16);
            }
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/gputest_vram", out);
    gpuDumpVram(path);
    printf("%d checks, %d failures\n", checks, failures);
    gpuShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
