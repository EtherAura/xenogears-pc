/* Analytical projections and metadata lifetime; no game data or GPU. */
#include "gte_pc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks;
#include "../../test/check.h" /* no port/src on this test's include path */
static void setup(void) {
    GteState s = {0};
    gteLoad(&s);
    gteCtc2(0, 4096); gteCtc2(2, 4096); gteCtc2(4, 4096);
    gteCtc2(24, (160 << 16) + 32768); gteCtc2(25, (100 << 16) + 32768); gteCtc2(26, 256);
}
static void vertex(int reg, int x, int y, int z) {
    gteMtc2(reg, ((u32)y << 16) | ((u32)x & 0xffff));
    gteMtc2(reg + 1, z);
}

int main(void) {
    setup(); gtePrecisionEnable(1);
    u32 xy[4], copied, fake;
    GtePrecise p;
    vertex(0, 1, 2, 1000); gte_rtps(); gte_stsxy(&xy[0]);
    CHECK(gtePrecisionLookup(&xy[0], xy[0], &p));
    CHECK(fabs(p.x - 160.756) < 1e-10 && fabs(p.y - 101.012) < 1e-10 && p.z == 1000);
    CHECK((s16)xy[0] == 160 && (s16)(xy[0] >> 16) == 101);
    fake = xy[0]; CHECK(!gtePrecisionLookup(&fake, fake, &p)); /* no value-only fallback */
    gteCopySxy(&copied, &xy[0]); CHECK(gtePrecisionLookup(&copied, copied, &p));
    copied ^= 1; CHECK(!gtePrecisionLookup(&copied, copied, &p));
    gteCopyPrecision(&copied, &xy[0]); copied = xy[0];
    CHECK(!gtePrecisionLookup(&copied, copied, &p)); /* failed copy invalidates prior metadata */
    vertex(0, 2, 4, 1000); vertex(2, 4, 8, 1000); vertex(4, 8, 16, 1000);
    gte_rtpt(); gte_stsxy3(&xy[0], &xy[1], &xy[2]);
    for (int i = 0; i < 3; i++) {
        CHECK(gtePrecisionLookup(&xy[i], xy[i], &p));
        CHECK(fabs(p.x - (160.5 + (2 << i) * .256)) < 1e-10);
    }
    GteState saved; gteSave(&saved);
    vertex(0, 16, 32, 1000); gte_rtps(); gte_stsxy(&xy[3]);
    gteLoad(&saved); gte_stsxy(&xy[2]);
    CHECK(gtePrecisionLookup(&xy[2], xy[2], &p) && fabs(p.x - 162.548) < 1e-10);
    gteMtc2(15, xy[2]); gte_stsxy(&xy[2]);
    CHECK(!gtePrecisionLookup(&xy[2], xy[2], &p));
    gteSwc2(13, &xy[1]); CHECK(gtePrecisionLookup(&xy[1], xy[1], &p)); /* FIFO pushed */
    gteMtc2(13, xy[1]); gteSwc2(13, &xy[1]); CHECK(!gtePrecisionLookup(&xy[1], xy[1], &p));
    setup(); vertex(0, 1, 2, 100); gte_rtps(); gte_stsxy(&xy[0]);
    CHECK(!gtePrecisionLookup(&xy[0], xy[0], &p)); /* divide overflow */
    vertex(0, 30000, 0, 2000); gte_rtps(); gte_stsxy(&xy[0]);
    CHECK(!gtePrecisionLookup(&xy[0], xy[0], &p)); /* screen saturation */
    vertex(0, 1, 2, 1000); gte_rtps(); gte_stsxy(&xy[0]);
    gtePrecisionNewFrame(); gtePrecisionNewFrame(); CHECK(gtePrecisionLookup(&xy[0], xy[0], &p));
    gtePrecisionNewFrame(); CHECK(!gtePrecisionLookup(&xy[0], xy[0], &p));
    /* A pending field draw owns its packet across any number of refreshes.
     * Nested callers must not release the outer draw's ownership. */
    gtePrecisionBeginDraw();
    vertex(0, 1, 2, 1000); gte_rtps(); gte_stsxy(&xy[0]);
    gtePrecisionBeginDraw();
    for (int i = 0; i < 60; ++i) gtePrecisionNewFrame();
    CHECK(gtePrecisionLookup(&xy[0], xy[0], &p));
    gtePrecisionEndDraw();
    for (int i = 0; i < 60; ++i) gtePrecisionNewFrame();
    CHECK(gtePrecisionLookup(&xy[0], xy[0], &p));
    copied = xy[0] ^ 1;
    CHECK(!gtePrecisionLookup(&xy[0], copied, &p));
    fake = xy[0]; CHECK(!gtePrecisionLookup(&fake, fake, &p));
    gtePrecisionEndDraw();
    gtePrecisionNewFrame(); gtePrecisionNewFrame();
    CHECK(gtePrecisionLookup(&xy[0], xy[0], &p));
    gtePrecisionNewFrame(); CHECK(!gtePrecisionLookup(&xy[0], xy[0], &p));
    gte_stsxy(&xy[0]); gtePrecisionEnable(0); CHECK(!gtePrecisionLookup(&xy[0], xy[0], &p));
    /* Narrow well/fence faces: rounded NCLIP can collapse or reverse the
     * actual winding. Querying the fractional area leaves all registers exact. */
    setup(); gtePrecisionEnable(1);
    GteState narrow, integer, queried;
    gteSave(&narrow);
    const double faces[2][3][2] = {
        {{207.523,164.766}, {207.384,166.153}, {207.777,160.360}},
        {{224.745,193.671}, {215.278,172.711}, {224.234,192.775}}
    };
    double area;
    for (int f = 0; f < 2; f++) {
        for (int i = 0; i < 3; i++) {
            u32 word = (u32)(int)faces[f][i][0] | (u32)(int)faces[f][i][1] << 16;
            narrow.data[12+i] = word;
            narrow.precise[i] = (GtePrecise){faces[f][i][0], faces[f][i][1], 1000, word, 1};
        }
        gteLoad(&narrow); gte_nclip(); gteSave(&integer);
        CHECK((s32)gteMfc2(24) == (f == 0 ? 0 : 9));
        CHECK(gtePrecisionNclip(&area) && (f == 0 ? area > .25 : area < -2));
        gteSave(&queried);
        CHECK(memcmp(integer.data, queried.data, sizeof(integer.data)) == 0 &&
              memcmp(integer.ctrl, queried.ctrl, sizeof(integer.ctrl)) == 0);
    }
    gteMtc2(12, narrow.data[12]); CHECK(!gtePrecisionNclip(&area));
    gteLoad(&narrow); gtePrecisionEnable(0); CHECK(!gtePrecisionNclip(&area));
    /* Turning tracking on cannot change any integer register or flags. */
    for (int x = -200; x <= 200; x += 7) {
        GteState before, off, on;
        setup(); vertex(0, x, x / 2, 600 + x); gteSave(&before);
        gte_rtps(); gteSave(&off);
        gteLoad(&before); gtePrecisionEnable(1); gte_rtps(); gteSave(&on);
        CHECK(memcmp(off.data, on.data, sizeof(on.data)) == 0 && memcmp(off.ctrl, on.ctrl, sizeof(on.ctrl)) == 0);
        gtePrecisionEnable(0);
    }
    printf("GTE precision: %d checks passed\n", checks);
    return 0;
}
