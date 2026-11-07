/* Display-copy coverage, whole-quad fallback, perspective and shared edges. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "psyq.h"
#include "gte_pc.h"
#include "pc_wide.h"
#include "pc_geometry.h"
#include "gpu/gpu_internal.h"
#include "gpu/gpu.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks;
static void captured(void* user, const uint32_t* words, int count) {
    (void)words; *(int*)user += count;
}
#include "test/check.h"
static void tag(void* addr, double dx, double dy, double depth) {
    u32 word; memcpy(&word, addr, 4);
    GtePrecise v = {(s16)word + dx, (s16)(word >> 16) + dy, depth, word, 1};
    gtePrecisionRecord(addr, &v);
}
static uint32_t hiPixel(int x, int y) {
    uint32_t p = 0;
    if (!gvkHiRead(0, &p, x, y, 1, 1)) return 0;
    return p & 0xffffff;
}
static void clear(void) {
    RECT r = {0, 0, 128, 128};
    ClearImage(&r, 248, 0, 248);
}
static void quad(POLY_F4* p, int x0, int y0, int x1, int y1, double dx, double dy) {
    SetPolyF4(p); setRGB0(p, 248, 248, 248);
    p->x0 = p->x2 = x0; p->x1 = p->x3 = x1;
    p->y0 = p->y1 = y0; p->y2 = p->y3 = y1;
    tag(&p->x0, dx, dy, 1000); tag(&p->x1, dx, dy, 1000);
    tag(&p->x2, dx, dy, 1000); tag(&p->x3, dx, dy, 1000);
}
int main(void) {
    testSetEnv("XG_SETTINGS", "none");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(gpuInit(NULL));
    /* The test drives the vblanks itself. The GPU layer's own would come
     * from the wall clock (testclock.c), wherever a slow or busy host puts
     * them, and each ages the precision tags: three retire a tag
     * (port/src/gte/precision.c MAX_AGE), and a quad drawn again long after
     * its tagging then falls back to its integer vertices (xg-htn.34.2.5:
     * the perspective redraw below failed so on a loaded Intel GPU). After
     * one external vblank the layer schedules none of its own. */
    gpuVBlank();
    DRAWENV env;
    SetDefDrawEnv(&env, 0, 0, 128, 128); PutDrawEnv(&env);

    CHECK(!hiresPrecision() && hiresSeamGuard() && hiresPerspective() && !hiresSlivers());
    hiresSetUpscale(4); CHECK(hiresPrecision());
    hiresSetUpscale(1); CHECK(!hiresPrecision());
    hiresSetPrecision(0); hiresSetUpscale(4); CHECK(!hiresPrecision());
    hiresSetUpscale(4); hiresSetPrecision(1); hiresSetPerspective(0); hiresSetSeamGuard(0);
    hiresSetColor(HIRES_COLOR_DITHER_1X);
    CHECK(gvkHiConfigure(4, HIRES_COLOR_DITHER_1X) == 4);
    POLY_F4 p; quad(&p, 20, 20, 100, 100, .25, .25);
    RECT r = {0, 0, 128, 128};
    static uint16_t exact[128 * 128], again[128 * 128];
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)exact);
    CHECK(hiPixel(80, 120) == 0xff00ff && hiPixel(81, 120) == 0xffffff);
    CHECK(exact[30 * 128 + 20] == 0x7fff); /* game still sees the integer edge */
    gtePrecisionForget(&p.x3);
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)again);
    CHECK(hiPixel(80, 120) == 0xffffff); /* entire quad falls back */
    CHECK(memcmp(exact, again, sizeof(exact)) == 0);

    uint16_t pixels[32 * 32];
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) pixels[y * 32 + x] = (uint16_t)(x | 1 << 10);
    RECT tex = {640, 0, 32, 32}; LoadImage(&tex, (u32*)pixels);
    POLY_FT4 t; SetPolyFT4(&t); SetShadeTex(&t, 1);
    t.x0 = t.x2 = 20; t.x1 = t.x3 = 100;
    t.y0 = t.y1 = 20; t.y2 = t.y3 = 100;
    t.u0 = t.u2 = 0; t.u1 = t.u3 = 31;
    t.v0 = t.v1 = 0; t.v2 = t.v3 = 31;
    t.tpage = GetTPage(2, 0, 640, 0); t.clut = 0;
    tag(&t.x0, 0, 0, 1000); tag(&t.x1, 0, 0, 4000);
    tag(&t.x2, 0, 0, 1000); tag(&t.x3, 0, 0, 4000);
    clear(); DrawPrim(&t); DrawSync(0); StoreImage(&r, (u32*)exact);
    int affine = hiPixel(241, 200) & 255;
    hiresSetPerspective(1);
    clear(); DrawPrim(&t); DrawSync(0); StoreImage(&r, (u32*)again);
    int perspective = hiPixel(241, 200) & 255;
    /* At x=60.25: affine u=15.596875 rounds to16; projective u=6.26 to6. */
    CHECK(affine == 132 && perspective == 49);
    CHECK(memcmp(exact, again, sizeof(exact)) == 0);

    /* Sprites, HUD and text keep their native texel block alignment.
     * An incomplete projection also retains the integer sampling path. */
    hiresSetPerspective(0);
    for (int kind = PC_TEX_SPRITE; kind <= PC_TEX_TEXT; kind++) {
        pcTextureClass(&t, kind);
        clear(); DrawPrim(&t); DrawSync(0); StoreImage(&r, (u32*)again);
        CHECK((int)(hiPixel(241, 200) & 255) == 123);
        CHECK(memcmp(exact, again, sizeof(exact)) == 0);
    }
    pcTextureClass(&t, PC_TEX_3D); gtePrecisionForget(&t.x3);
    clear(); DrawPrim(&t); DrawSync(0); StoreImage(&r, (u32*)again);
    CHECK((int)(hiPixel(241, 200) & 255) == 123);
    CHECK(memcmp(exact, again, sizeof(exact)) == 0);
    hiresSetPerspective(1);

    /* A clipped affine walker starts at u=15 here. Projective sampling at
     * that same edge needs u=6, inside the vertex patch but outside the
     * clipped walker's range. It must not be clamped to the affine edge. */
    SetDefDrawEnv(&env, 60, 20, 40, 80); env.ofs[0] = env.ofs[1] = 0; PutDrawEnv(&env);
    tag(&t.x0, 0, 0, 1000); tag(&t.x1, 0, 0, 4000);
    tag(&t.x2, 0, 0, 1000); tag(&t.x3, 0, 0, 4000);
    clear(); DrawPrim(&t); DrawSync(0);
    int clipped = hiPixel(245, 200) & 255;
    /* At x=61.25 the projective coordinate rounds to texel7. */
    CHECK(clipped == 57);
    CHECK(hiPixel(239, 200) == 0xff00ff);
    SetDefDrawEnv(&env, 0, 0, 128, 128); PutDrawEnv(&env);

    /* Retail quads can collapse two corners to the same integer word.
     * Their fractional wedge must still meet the neighbouring surface. */
    quad(&p, 20, 20, 100, 100, 0, 0);
    p.x3 = p.x2; p.y3 = p.y2;
    tag(&p.x3, .75, 0, 1000);
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)exact);
    CHECK(hiPixel(83, 398) == 0xffffff);
    CHECK(exact[99 * 128 + 21] == (31 | 31 << 10));

    /* Separately emitted neighbours must meet through fractional motion. */
    for (int seal = 0; seal < 2; seal++) {
        hiresSetSeamGuard(seal);
        for (int frame = 0; frame < 8; frame++) {
            clear();
            POLY_F4 mesh[16];
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                POLY_F4* q = &mesh[y * 4 + x];
                quad(q, 20 + 20 * x, 20 + 20 * y, 40 + 20 * x, 40 + 20 * y, frame / 8.0, frame / 8.0);
                double delta = frame / 8.0;
                tag(&q->x0, delta, delta, 1000 + 600 * x + 200 * y);
                tag(&q->x1, delta, delta, 1600 + 600 * x + 200 * y);
                tag(&q->x2, delta, delta, 1200 + 600 * x + 200 * y);
                tag(&q->x3, delta, delta, 1800 + 600 * x + 200 * y);
                DrawPrim(q);
            }
            uint32_t area[312 * 312];
            CHECK(gvkHiRead(0, area, 84, 84, 312, 312));
            int holes = 0;
            for (int i = 0; i < 312 * 312; i++) holes += (area[i] & 0xffffff) != 0xffffff;
            CHECK(holes == 0);
        }
    }
    hiresSetSeamGuard(0);
    /* A retail-style T-junction: both surfaces round to x=60, but their
     * actual edges are .65 native pixels apart at different depths. */
    POLY_F4 neighbours[2];
    quad(&neighbours[0], 20, 20, 60, 100, .25, .25);
    quad(&neighbours[1], 60, 40, 100, 80, .9, .25);
    tag(&neighbours[1].x0, .9, .25, 1100); tag(&neighbours[1].x1, .9, .25, 1100);
    tag(&neighbours[1].x2, .9, .25, 1100); tag(&neighbours[1].x3, .9, .25, 1100);
    clear(); DrawPrim(&neighbours[0]); DrawPrim(&neighbours[1]); DrawSync(0);
    StoreImage(&r, (u32*)exact);
    CHECK(hiPixel(242, 200) == 0xff00ff);
    hiresSetSeamGuard(1);
    clear(); DrawPrim(&neighbours[0]); DrawPrim(&neighbours[1]); DrawSync(0);
    StoreImage(&r, (u32*)again);
    CHECK(hiPixel(242, 200) == 0xffffff);
    CHECK(memcmp(exact, again, sizeof(exact)) == 0);
    /* The overlap still moves through fractional positions. */
    CHECK(hiPixel(76, 200) == 0xff00ff && hiPixel(78, 200) == 0xffffff);
    quad(&p, 20, 20, 100, 100, .75, .25);
    clear(); DrawPrim(&p); DrawSync(0);
    CHECK(hiPixel(78, 200) == 0xff00ff && hiPixel(80, 200) == 0xffffff);
    /* Projective planes stay fixed in the interior when edges expand. */
    clear(); DrawPrim(&t); DrawSync(0);
    CHECK((int)(hiPixel(241, 200) & 255) == perspective);
    /* Untracked screen-space packets keep their exact integer silhouette. */
    quad(&p, 20, 20, 100, 100, .25, .25); gtePrecisionForget(&p.x3);
    clear(); DrawPrim(&p); DrawSync(0);
    CHECK(hiPixel(79, 200) == 0xff00ff && hiPixel(80, 200) == 0xffffff);
    /* Semi-transparent polygons cannot acquire an extra blended fringe. */
    quad(&p, 20, 20, 100, 100, .25, .25); SetSemiTrans(&p, 1);
    clear(); DrawPrim(&p); DrawSync(0);
    uint32_t semiEdge = hiPixel(81, 200), semiCentre = hiPixel(240, 200);
    CHECK(hiPixel(80, 200) == 0xff00ff);
    /* A vblank reapplies the configured colour mode: the repeated blend
     * must come out the same after one. */
    gpuVBlank();
    hiresSetSeamGuard(0);
    clear(); DrawPrim(&p); DrawSync(0);
    uint32_t repeatedEdge = hiPixel(81, 200), repeatedCentre = hiPixel(240, 200);
    if (repeatedEdge != semiEdge || repeatedCentre != semiCentre) {
        fprintf(stderr, "semi repeat: edge %08x -> %08x, centre %08x -> %08x\n",
                semiEdge, repeatedEdge, semiCentre, repeatedCentre);
    }
    CHECK(repeatedEdge == semiEdge && repeatedCentre == semiCentre);

    /* A grazing face is thinner than a native pixel. A full-pixel fringe
     * would dominate it, then jump to the other side when winding changes.
     * Both triangle orientations must keep the overlap close to the face. */
    hiresSetSeamGuard(1);
    for (int winding = 0; winding < 2; winding++) {
        for (int frame = 0; frame < 16; frame++) {
            double thickness = .125 + frame / 32.0;
            quad(&p, 20, 30, 100, 30, .25, .25);
            double top = winding ? .25 + thickness : .25;
            double bottom = winding ? .25 : .25 + thickness;
            tag(&p.x0, .25, top, 1000); tag(&p.x1, .25, top, 1000);
            tag(&p.x2, .25, bottom, 1000); tag(&p.x3, .25, bottom, 1000);
            clear(); DrawPrim(&p); DrawSync(0);
            CHECK(hiPixel(240, 120) == 0xff00ff);
            CHECK(hiPixel(240, 125) == 0xff00ff);
            if (frame >= 8) CHECK(hiPixel(240, 122) == 0xffffff);
        }
    }

    /* The same centre texel must remain on a rotating projected face.
     * Recentring its samples by half a native pixel moves them outside a
     * narrow face and selects its border colours as the angle changes. */
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
        pixels[y * 32 + x] = y >= 12 && y <= 20 ? 31 : 31 << 10;
    tex.w = tex.h = 32; LoadImage(&tex, (u32*)pixels);
    t.u0 = t.u2 = 0; t.u1 = t.u3 = 31;
    t.v0 = t.v1 = 0; t.v2 = t.v3 = 31;
    hiresSetSeamGuard(0);
    for (int frame = 0; frame < 32; frame++) {
        double angle = frame * 6.283185307179586 / 32;
        double cs = cos(angle), sn = sin(angle);
        short* xy[4] = {&t.x0, &t.x1, &t.x2, &t.x3};
        for (int i = 0; i < 4; i++) {
            double along = (i & 1) ? 30 : -30;
            double across = (i & 2) ? .5 : -.5;
            double x = 60 + along * cs - across * sn;
            double y = 60 + along * sn + across * cs;
            xy[i][0] = (short)floor(x); xy[i][1] = (short)floor(y);
            tag(xy[i], x - xy[i][0], y - xy[i][1], 1000);
        }
        clear(); DrawPrim(&t); DrawSync(0);
        CHECK(hiPixel(240, 240) == 255);
    }

    /* Neighbouring faces own their original interiors even as native
     * depth sorting changes their draw order. A seam fringe must only
     * fill the gap, not flash the other face's colour over covered pixels. */
    hiresSetSeamGuard(1);
    for (int order = 0; order < 2; order++) {
        for (int frame = 0; frame < 8; frame++) {
            double delta = .25 + frame / 8.0;
            quad(&neighbours[0], 20, 20, 60, 100, delta, .25);
            quad(&neighbours[1], 60, 20, 100, 100, delta, .25);
            setRGB0(&neighbours[0], 248, 0, 0);
            setRGB0(&neighbours[1], 0, 0, 248);
            clear(); DrawPrim(&neighbours[order]); DrawPrim(&neighbours[1 - order]); DrawSync(0);
            int edge = (int)floor((60 + delta) * 4);
            CHECK(hiPixel(edge - 1, 200) == 255);
            CHECK(hiPixel(edge + 2, 200) == 0xff0000);
        }
    }

    /* Mask-only CPU round trips keep the projected surface covered. */
    quad(&neighbours[0], 20, 20, 60, 100, .25, .25);
    quad(&neighbours[1], 60, 20, 100, 100, .25, .25);
    setRGB0(&neighbours[0], 248, 0, 0); setRGB0(&neighbours[1], 0, 0, 248);
    clear(); DrawPrim(&neighbours[0]); DrawSync(0); StoreImage(&r, (u32*)exact);
    for (int i = 0; i < 128 * 128; i++) exact[i] |= 0x8000;
    LoadImage(&r, (u32*)exact); DrawPrim(&neighbours[1]); DrawSync(0);
    CHECK(hiPixel(240, 200) == 255);
    CHECK(hiPixel(243, 200) == 0xff0000);

    /* Projected menu text has adjacent packed lines in its atlas. An
     * opaque surface fringe must not reach that next line or widen glyphs. */
    uint16_t lines[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) lines[i] = 0x7fff;
    for (int x = 0; x < 16; ++x) lines[13 * 16 + x] = 31;
    tex.w = tex.h = 16; LoadImage(&tex, (u32*)lines);
    t.x0 = t.x2 = 20; t.x1 = t.x3 = 32;
    t.y0 = t.y1 = 20; t.y2 = t.y3 = 33;
    t.u0 = t.u2 = 0; t.u1 = t.u3 = 12;
    t.v0 = t.v1 = 0; t.v2 = t.v3 = 13;
    tag(&t.x0, .25, .25, 512); tag(&t.x1, .25, .25, 512);
    tag(&t.x2, .25, .25, 512); tag(&t.x3, .25, .25, 512);
    pcTextureClass(&t, PC_TEX_TEXT); hiresSetSeamGuard(1);
    clear(); DrawPrim(&t); DrawSync(0);
    CHECK(hiPixel(100, 132) == 0xffffff); /* the glyph's last row survives */
    CHECK(hiPixel(100, 134) == 0xff00ff); /* no row from the next line */
    CHECK(hiPixel(130, 100) == 0xff00ff); /* no enlarged right edge */
    /* Projected sprite and HUD quads are flat pixel art as well: the memory
     * card screen draws each save icon's outline (LINE_F3s, which mark no
     * surface coverage) before the icon's GTE-projected quad. A fringe
     * would repeat the icon's border texels a native pixel outward, over
     * that outline and the background. */
    for (int i = 0; i < 16 * 16; ++i) lines[i] = 0x7fff;
    LoadImage(&tex, (u32*)lines);
    for (int kind = PC_TEX_SPRITE; kind <= PC_TEX_HUD; kind++) {
        LINE_F2 outline; setlen(&outline, 3); setcode(&outline, 0x40); setRGB0(&outline, 248, 0, 0);
        outline.x0 = outline.x1 = 32; outline.y0 = 20; outline.y1 = 32;
        tag(&t.x0, 0, 0, 512); tag(&t.x1, 0, 0, 512);
        tag(&t.x2, 0, 0, 512); tag(&t.x3, 0, 0, 512);
        pcTextureClass(&t, kind);
        clear(); DrawPrim(&outline); DrawPrim(&t); DrawSync(0);
        CHECK(hiPixel(100, 100) == 0xffffff); /* the quad itself */
        CHECK(hiPixel(129, 100) == 255);      /* the outline beside its right edge survives */
        CHECK(hiPixel(78, 100) == 0xff00ff);  /* nothing left of its left edge */
        CHECK(hiPixel(100, 78) == 0xff00ff);  /* nothing above its top edge */
        CHECK(hiPixel(100, 133) == 0xff00ff); /* nothing below its bottom edge */
    }
    /* A fractional-only face never writes native VRAM or changes GP0's
     * texture page. An integer-only back face keeps native VRAM intact. */
    widePacketsDone(); hiresSetSeamGuard(0);
    quad(&p, 20, 20, 100, 100, .25, .25);
    pcGeometryPrim(&p, PC_GEOMETRY_ONLY);
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)exact);
    CHECK(hiPixel(240, 200) == 0xffffff);
    CHECK(exact[50 * 128 + 60] == 0x7c1f);
    pcGeometryPrim(&p, PC_GEOMETRY_HIDE);
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)again);
    CHECK(hiPixel(240, 200) == 0xff00ff);
    CHECK(again[50 * 128 + 60] == 0x7fff);
    hiresSetPrecision(0);
    clear(); DrawPrim(&p); DrawSync(0);
    CHECK(hiPixel(240, 200) == 0xffffff); /* precision off keeps the integer face */
    hiresSetPrecision(1);
    pcGeometryPrim(&p, PC_GEOMETRY_ONLY); gtePrecisionForget(&p.x3);
    clear(); DrawPrim(&p); DrawSync(0); StoreImage(&r, (u32*)again);
    CHECK(hiPixel(240, 200) == 0xff00ff && again[50 * 128 + 60] == 0x7c1f);
    widePacketsDone(); pcGeometryPrim(&t, PC_GEOMETRY_ONLY);
    uint16_t red[16 * 16];
    for (int i = 0; i < 16 * 16; i++) red[i] = 31;
    RECT pageTex = {704, 0, 16, 16}; LoadImage(&pageTex, (u32*)red);
    DR_TPAGE page; SetDrawTPage(&page, 0, 0, GetTPage(2, 0, 704, 0)); DrawPrim(&page);
    clear(); DrawPrim(&t); DrawSync(0);
    widePacketsDone(); CHECK(widePacket(&t) == NULL);
    SPRT sprite; SetSprt(&sprite); SetShadeTex(&sprite, 1); sprite.w = sprite.h = 8;
    sprite.x0 = sprite.y0 = 108; sprite.u0 = sprite.v0 = 0; sprite.clut = 0;
    DrawPrim(&sprite); DrawSync(0); StoreImage(&r, (u32*)again);
    CHECK(again[110 * 128 + 110] == 31 && hiPixel(440, 440) == 255);
    int capturedWords = 0;
    GpuCapture capture = {.user = &capturedWords, .gp0 = captured};
    CHECK(pcGeometryActive()); gpuSetCapture(&capture); CHECK(!pcGeometryActive());
    pcGeometryPrim(&p, PC_GEOMETRY_ONLY); DrawPrim(&p); CHECK(capturedWords == 0);
    OTag table; ClearOTagR(&table, 1); AddPrim(&table, &p); DrawOTag(&table);
    CHECK(capturedWords == 0);
    pcGeometryPrim(&p, PC_GEOMETRY_HIDE); DrawPrim(&p); CHECK(capturedWords == 5);
    gpuSetCapture(NULL); CHECK(pcGeometryActive()); widePacketsDone();
    /* Original aspect also needs bounded, display-only packet storage. */
    CHECK(pcGeometryAlloc(&p, sizeof(p)) == NULL);
    hiresSetSlivers(1);
    hiresSetPrecision(0); CHECK(pcGeometryAlloc(&p, sizeof(p)) == NULL);
    hiresSetPrecision(1);
    gpuSetCapture(&capture); CHECK(pcGeometryAlloc(&p, sizeof(p)) == NULL);
    gpuSetCapture(NULL);
    CHECK(pcGeometryAlloc(&p, 0) == NULL && pcGeometryAlloc(&p, -1) == NULL);
    CHECK(pcGeometryAlloc(&p, 2*1024*1024+1) == NULL);
    for (int abr=0;abr<4;abr++) {
        widePacketsDone();
        /* A normal reference establishes the exact blending at this scale.
         * The private face must use that mode while the next native packet
         * still inherits mode zero. */
        quad(&p,20,20,100,100,0,0); setRGB0(&p,0,248,0); SetSemiTrans(&p,1);
        SetDrawTPage(&page,0,0,abr<<5); DrawPrim(&page);
        clear(); DrawPrim(&p); DrawSync(0);
        uint32_t expected=hiPixel(240,200);
        SetDrawTPage(&page,0,0,0); DrawPrim(&page);
        clear(); DrawPrim(&p); DrawSync(0);
        uint32_t inherited=hiPixel(240,200);
        StoreImage(&r,(u32*)exact);
        POLY_F4* thin=pcGeometryAlloc(&p,sizeof(p)); CHECK(thin && thin!=&p);
        pcGeometryBlend(thin,abr);
        thin->x0=thin->x1=thin->x2=thin->x3=40;
        tag(&thin->x0,.1,.2,1000); tag(&thin->x1,.8,.2,1000);
        tag(&thin->x2,.1,.2,1000); tag(&thin->x3,.8,.2,1000);
        clear(); DrawPrim(thin); DrawSync(0); StoreImage(&r,(u32*)again);
        CHECK(hiPixel(161,200)==expected);
        for(int k=0;k<128*128;k++) CHECK(again[k]==0x7c1f);
        DrawPrim(&p); DrawSync(0); StoreImage(&r,(u32*)again);
        CHECK(hiPixel(240,200)==inherited && !memcmp(exact,again,sizeof(exact)));
    }
    widePacketsDone(); pcTextureClass(&p,PC_TEX_HUD);
    POLY_F4* classified=pcGeometryAlloc(&p,sizeof(p));
    CHECK(classified && widePacket(classified)->classified && widePacket(classified)->texClass==PC_TEX_HUD);
    /* No game buffer is read past its template on exhaustion or bad sizes. */
    widePacketsDone(); int capacity=0;
    while(pcGeometryAlloc(&p,sizeof(p))) { capacity++; CHECK(capacity<60000); }
    CHECK(capacity>0 && pcGeometryAlloc(&p,sizeof(p))==NULL);
    widePacketsDone(); CHECK(pcGeometryAlloc(&p,sizeof(p))!=NULL);
    printf("GPU geometry: %d checks passed (affine %d, perspective %d)\n", checks, affine, perspective);
    gpuShutdown(); SDL_Quit();
    return 0;
}
