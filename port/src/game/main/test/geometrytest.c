/* Actual model loops: fractional-only faces cannot alter native GTE state,
 * counters, instance cursors, or the following polygon. No game data/GPU. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "gte_pc.h"
#include "pc_geometry.h"

SVECTOR* currentModelBlockVertices;
SVECTOR* currentModelBlockNormals;
u8* currentModelInstanceDrawPrims;
u8* currentModeBlock18;
OTag* currentOTEntry;
s32 fieldPolyCount, fieldPolyCount2, screenClippingX, screenClippingY, gDepthDivider;
s32 objectClippingMask;
u8* g_currentModelBlockSubBlocks;
CVECTOR fogNearColor;
int pcWideView, pcWideCull, pcVerticalCull, pcWideOnlyScope;
static int treatment, tagged, wideTagged, checks;
int pcGeometryActive(void) { return gtePrecisionEnabled(); }
void pcGeometryPrim(void* p, int t) { (void)p; treatment = t; tagged++; }
void pcFadePrim(void* p) { (void)p; }
void pcInterpolationPrim(void* p) { (void)p; }
void pcWidePrim(void* p, int dx, unsigned flags) { (void)p; (void)dx; if (flags & PC_WIDE_ONLY) wideTagged++; }
s32 isObjectClippedWide(void* b, u8 mask) { (void)b; (void)mask; return 1; }
s32 submitModelForRendering(void* b, u8* p, OTag* ot, s32 mode) {
    (void)b; (void)p; (void)ot; (void)mode; return 0;
}

#define DECL(name) s32 name(u8*, s32)
DECL(prim4_0); DECL(primC_0); DECL(prim4_2); DECL(primC_2);
DECL(prim0_1); DECL(FT3_FAKE); DECL(GT3_FAKE); DECL(prim2_1);
DECL(prim8_1); DECL(primD_1); DECL(prim5_4); DECL(prim5_5);
DECL(primD_4); DECL(prim10_0);
#define CHECK_CONTEXT fprintf(stderr, "loop %d face %d ", loop, face)
#include "../../../test/check.h" /* no port/src on this test's include path */

static SVECTOR vertices[8], normals[8];
static OTag ot[4096];
static _Alignas(16) u8 prims[256];
static u32 instance[128];
static u16 indices[12] = {0,1,2,3, 4,5,6,7, 0,1,2,3};
static void setup(int face, int precision, int margin) {
    GteState s = {0}; gteLoad(&s); gtePrecisionEnable(precision);
    gteCtc2(0, 4096); gteCtc2(2, 4096); gteCtc2(4, 4096);
    gteCtc2(24, (u32)(margin ? -10 : 160) << 16); gteCtc2(25, 100 << 16); gteCtc2(26, 256);
    gteCtc2(29, 1365); gteCtc2(30, 1024);
    vertices[0] = (SVECTOR){0,0,1000,0};
    vertices[1] = (SVECTOR){face ? 5 : 1,face ? 3 : 0,1000,0};
    vertices[2] = (SVECTOR){face ? 12 : 0,face ? 7 : 1,1000,0};
    vertices[3] = (SVECTOR){face ? 17 : 1,face ? 10 : 1,1000,0};
    vertices[4] = (SVECTOR){-100,-100,1000,0};
    vertices[5] = (SVECTOR){100,-100,1000,0};
    vertices[6] = (SVECTOR){-100,100,1000,0};
    vertices[7] = (SVECTOR){100,100,1000,0};
    memset(prims, 0, sizeof(prims)); memset(instance, 0, sizeof(instance));
    for (int i = 0; i < 4096; i++) ot[i] = OT_LINK_MASK;
    currentModelBlockVertices = vertices; currentModelBlockNormals = normals;
    currentModelInstanceDrawPrims = prims; currentModeBlock18 = (u8*)instance;
    currentOTEntry = ot; fieldPolyCount = 0;
    screenClippingX = 320; screenClippingY = 215 << 16; gDepthDivider = 4;
    treatment = tagged = wideTagged = pcVerticalCull = 0;
    pcWideCull = margin ? 64 : 0;
}

int main(void) {
    s32 (*loops[])(u8*,s32) = {prim4_0,primC_0,prim4_2,primC_2,prim0_1,
        FT3_FAKE,GT3_FAKE,prim2_1,prim8_1,primD_1,prim5_4,
        prim5_5,primD_4,prim10_0};
    for (int loop = 0; loop < (int)(sizeof(loops)/sizeof(loops[0])); loop++) {
        for (int face = 0; face < 2; face++) for (int margin = 0; margin < 2; margin++) {
            GteState off, on;
            setup(face, 0, margin); loops[loop]((u8*)indices, 2); gteSave(&off);
            int count = fieldPolyCount;
            u8* cursor = currentModelInstanceDrawPrims;
            u8* inst = currentModeBlock18;
            CHECK(tagged == 0 && count == (margin ? 1 : face + 1));
            setup(face, 1, margin); loops[loop]((u8*)indices, 2); gteSave(&on);
            CHECK(tagged == 1 && treatment == (face ? PC_GEOMETRY_HIDE : PC_GEOMETRY_ONLY));
            CHECK(fieldPolyCount == count && currentModelInstanceDrawPrims == cursor && currentModeBlock18 == inst);
            CHECK(memcmp(off.data, on.data, sizeof(off.data)) == 0 && memcmp(off.ctrl, on.ctrl, sizeof(off.ctrl)) == 0);
        }
    }
    /* Entire faces above/below native framing are display-only. Extra
     * projection/lighting must leave counters, cursors and GTE unchanged. */
    for (int loop = 0; loop < (int)(sizeof(loops)/sizeof(loops[0])); loop++) {
        int face = 1;
        for (int edge = 0; edge < 2; edge++) {
            GteState off, on;
            int y = edge ? 224 : -10;
            setup(face, 0, 0); gteCtc2(25, (u32)y << 16);
            loops[loop]((u8*)indices, 2); gteSave(&off);
            int count = fieldPolyCount;
            u8* cursor = currentModelInstanceDrawPrims;
            u8* inst = currentModeBlock18;
            CHECK(wideTagged == 0);
            setup(face, 0, 0); gteCtc2(25, (u32)y << 16); pcVerticalCull = 32;
            loops[loop]((u8*)indices, 2); gteSave(&on);
            CHECK(wideTagged == 1);
            CHECK(fieldPolyCount == count && currentModelInstanceDrawPrims == cursor && currentModeBlock18 == inst);
            CHECK(memcmp(off.data, on.data, sizeof(off.data)) == 0 && memcmp(off.ctrl, on.ctrl, sizeof(off.ctrl)) == 0);
        }
    }
    /* Move a narrow quad across a pixel: its rounded test switches between
     * visible and collapsed, but it must remain linked in the display copy. */
    int loop = 1, face = 0, roundedVisible = 0, roundedCulled = 0;
    for (int phase = 0; phase < 32; phase++) {
        GteState off, on;
        setup(0, 0, 0);
        gteCtc2(24, (160 << 16) + phase * 2048);
        gteCtc2(25, (100 << 16) + phase * 2048);
        primC_0((u8*)indices, 2); gteSave(&off);
        int count = fieldPolyCount;
        if (count == 2) roundedVisible++; else roundedCulled++;
        setup(0, 1, 0);
        gteCtc2(24, (160 << 16) + phase * 2048);
        gteCtc2(25, (100 << 16) + phase * 2048);
        primC_0((u8*)indices, 2); gteSave(&on);
        CHECK((*(OTag*)prims >> 56) == 5); /* fractional face never disappears */
        CHECK(fieldPolyCount == count);
        CHECK(memcmp(off.data, on.data, sizeof(off.data)) == 0 && memcmp(off.ctrl, on.ctrl, sizeof(off.ctrl)) == 0);
    }
    CHECK(roundedVisible && roundedCulled);
    printf("Model geometry: %d checks passed across 14 render loops\n", checks);
    return 0;
}
