/* Exact scanline-edge stepping versus the previously validated per-row
 * signed divisions, including clipping, ties, winding and attributes. */
#include "../gp0.c"
#include <time.h>
static int checks;
#include "test/check.h"

static uint32_t seed = 0x913ba417u;
static unsigned randomWord(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static void vertices(PVertex v[3], unsigned n) {
    memset(v, 0, 3 * sizeof(*v));
    for (int k = 0; k < 3; k++) {
        v[k].x = (int)(randomWord() >> 16) % 4096 - 1024;
        v[k].y = (int)(randomWord() >> 16) % 1536 - 512;
        v[k].u = randomWord() >> 24; v[k].v = randomWord() >> 24;
        v[k].r = randomWord() >> 24; v[k].g = randomWord() >> 24; v[k].b = randomWord() >> 24;
    }
    if (!(n % 7)) v[1].y = v[0].y;
    if (!(n % 11)) v[2].x = v[1].x;
    if (!(n % 13)) v[2] = v[1];
    if (!(n % 17)) v[2].y = v[0].y;
    if (!(n % 5)) for (int k = 1; k < 3; k++) {
        v[k].r = v[0].r; v[k].g = v[0].g; v[k].b = v[0].b;
    }
    if (!(n % 29)) for (int k = 1; k < 3; k++) { v[k].u = v[0].u; v[k].v = v[0].v; }
    if (n & 1) { PVertex t = v[1]; v[1] = v[2]; v[2] = t; }
}
/* Portrait canvas trims (polygonUvBounds) equal the texels the integer
 * walker actually samples, for every vertex order of an axis-aligned face
 * and every mirror, flip and quarter turn of its UVs. The game's order
 * (vertex 1 beside vertex 0, vertex 2 below or above it) without a turn
 * also gives exactly the previous vertex-1/2 rule. */
static int notes;
void gpuLog(const char* fmt, ...) { (void)fmt; ++notes; }
static void sampledBounds(const PVertex* q, uint32_t* lo, uint32_t* hi) {
    static int32_t spans[SPAN_HEADER + SPAN_ROW * VRAM_H + 16];
    int u0 = 1 << 30, u1 = -(1 << 30), v0 = 1 << 30, v1 = -(1 << 30);
    const int tri[2][3] = {{0, 1, 2}, {1, 2, 3}};
    for (int t = 0; t < 2; t++) {
        VRect box;
        if (!buildSpans(&q[tri[t][0]], &q[tri[t][1]], &q[tri[t][2]], 1, &box, spans, (VRect){0, 0, 1023, 511})) continue;
        uint32_t a, b; spanTexels(spans, &a, &b);
        if ((int16_t)a < u0) u0 = (int16_t)a;
        if ((int16_t)(a >> 16) < v0) v0 = (int16_t)(a >> 16);
        if ((int16_t)b > u1) u1 = (int16_t)b;
        if ((int16_t)(b >> 16) > v1) v1 = (int16_t)(b >> 16);
    }
    *lo = uvWord(u0, v0); *hi = uvWord(u1, v1);
}
static int portraitTrims(void) {
    static char logging[] = "XG_TEXTURE_LOG=1";
    putenv(logging); /* read once, at the first trim */
    const int kinds[2] = {PC_TEX_PORTRAIT, PC_TEX_BATTLE_PORTRAIT};
    int gameOrders = 0;
    for (int k = 0; k < 2; k++) {
        int size = PC_TEX_PORTRAIT_SIZE(kinds[k]);
        for (int o = 0; o < 8; o++) for (int c0 = 0; c0 < 4; c0++) for (int beside = 0; beside < 2; beside++) {
            /* corners 0 top left, 1 top right, 2 bottom left, 3 bottom right;
             * a quad's vertices 0 and 3 are opposite */
            int order[4] = {c0, beside ? c0 ^ 1 : c0 ^ 2, beside ? c0 ^ 2 : c0 ^ 1, 3 - c0};
            PVertex q[4];
            memset(q, 0, sizeof(q));
            for (int i = 0; i < 4; i++) {
                int fx = order[i] & 1, fy = order[i] >> 1;
                q[i].x = 40 + fx * size; q[i].y = 20 + fy * size;
                if (o & 4) { int t = fx; fx = fy; fy = t; } /* u follows y */
                if (o & 1) fx = !fx;
                if (o & 2) fy = !fy;
                q[i].u = 64 + fx * size; q[i].v = 32 + fy * size;
                q[i].r = q[i].g = q[i].b = 128;
            }
            uint32_t lo, hi, sampledLo, sampledHi;
            polygonUvBounds(q, 4, kinds[k], &lo, &hi);
            sampledBounds(q, &sampledLo, &sampledHi);
            CHECK(lo == sampledLo && hi == sampledHi);
            if (beside && !(o & 4)) {
                int u0 = 64, u1 = 64 + size, v0 = 32, v1 = 32 + size;
                if ((q[1].x - q[0].x) * (q[1].u - q[0].u) < 0) ++u0; else --u1;
                if ((q[2].y - q[0].y) * (q[2].v - q[0].v) < 0) ++v0; else --v1;
                CHECK(lo == uvWord(u0, v0) && hi == uvWord(u1, v1));
                ++gameOrders;
            }
        }
    }
    CHECK(gameOrders == 2 * 4 * 4 && notes == 0);
    /* A rotated face has no exact trim: it keeps the vertex-1/2 rule, noted. */
    PVertex q[4] = {{.x = 40, .y = 20, .u = 0, .v = 0}, {.x = 100, .y = 40, .u = 64, .v = 0},
                    {.x = 20, .y = 80, .u = 0, .v = 64}, {.x = 80, .y = 100, .u = 64, .v = 64}};
    uint32_t lo, hi; polygonUvBounds(q, 4, PC_TEX_PORTRAIT, &lo, &hi);
    CHECK(lo == uvWord(0, 0) && hi == uvWord(63, 63) && notes == 2);
    /* A collapsed face (an opening animation) keeps the rule silently. */
    for (int i = 0; i < 4; i++) q[i].y = 20;
    polygonUvBounds(q, 4, PC_TEX_PORTRAIT, &lo, &hi);
    CHECK(notes == 2);
    return 0;
}
int main(void) {
    if (portraitTrims()) return 1;
    enum { WORDS = SPAN_HEADER + SPAN_ROW * VRAM_H + 16 };
    int32_t actual[WORDS], expected[WORDS];
    PVertex v[3]; VRect box, reference;
    for (unsigned n = 0; n < 64000; n++) {
        vertices(v, n);
        VRect clip = {(int)(randomWord() >> 24), (int)(randomWord() >> 24),
                      256 + (int)(randomWord() >> 23), 256 + (int)(randomWord() >> 24)};
        if (!(n % 3)) clip = (VRect){0, 0, 2047, 511};
        if (!(n % 19)) clip.x1 = clip.x0 - 1;
        if (!(n % 23)) clip.y1 = clip.y0 - 1;
        memset(actual, 0xa5, sizeof(actual)); memset(expected, 0xa5, sizeof(expected));
        box = reference = (VRect){-99, -99, -99, -99};
        int count = buildSpans(&v[0], &v[1], &v[2], n & 1, &box, actual, clip);
        int direct = buildSpansMode(&v[0], &v[1], &v[2], n & 1, &reference, expected, clip, 0);
        CHECK(count == direct);
        CHECK(!memcmp(&box, &reference, sizeof(box)));
        CHECK(!memcmp(actual, expected, sizeof(actual)));
    }
    /* A thin diagonal has uncovered rows between covered ones. Preserve
     * all row attributes and untouched holes exactly. */
    v[0] = (PVertex){.x=0,.y=0,.u=1,.v=200,.r=245,.g=20,.b=100};
    v[1] = (PVertex){.x=12,.y=128,.u=249,.v=7,.r=1,.g=255,.b=70};
    v[2] = (PVertex){.x=13,.y=128,.u=100,.v=20,.r=75,.g=100,.b=200};
    memset(actual, 0xa5, sizeof(actual)); memset(expected, 0xa5, sizeof(expected));
    VRect clip = {0,0,1023,511};
    int count = buildSpans(&v[0], &v[1], &v[2], 1, &box, actual, clip);
    int direct = buildSpansMode(&v[0], &v[1], &v[2], 1, &reference, expected, clip, 1);
    CHECK(count == direct && count > SPAN_HEADER);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    int gaps = 0;
    for (int y = 0; y < actual[1]; y++) {
        const int32_t* row = actual + SPAN_HEADER + y * SPAN_ROW;
        if (row[0] > row[1]) gaps++;
    }
    CHECK(gaps > 0);
    v[0] = (PVertex){.x=7,.y=0,.u=1,.v=200,.r=245,.g=20,.b=100};
    v[1] = (PVertex){.x=1017,.y=511,.u=249,.v=7,.r=1,.g=255,.b=70};
    v[2] = (PVertex){.x=30,.y=490,.u=100,.v=20,.r=75,.g=100,.b=200};
    clip = (VRect){0,0,1023,511};
    volatile uint64_t sums[3] = {0,0,0}; double ms[3];
    for (int fast=0;fast<3;fast++) {
        clock_t start=clock();
        for (int n=0;n<20000;n++) {
            int count=buildSpansMode(&v[0],&v[1],&v[2],1,&box,actual,clip,fast);
            sums[fast]+=(uint32_t)actual[17]+count;
        }
        ms[fast]=(double)(clock()-start)*1000/CLOCKS_PER_SEC;
    }
    CHECK(sums[0] == sums[1]);
    CHECK(sums[0] == sums[2]);
    printf("Span stepping: %d checks; division %.3f ms, edges %.3f ms, clipped bounds %.3f ms\n",checks,ms[0],ms[1],ms[2]);
    /* Fully clipped faces must not pay for unused attribute accumulation. */
    clip = (VRect){1500,0,1800,511};
    for (int fast = 1; fast < 3; fast++) {
        clock_t start = clock();
        for (int n = 0; n < 20000; n++) {
            int c = buildSpansMode(&v[0], &v[1], &v[2], 1, &box, actual, clip, fast);
            sums[fast] += c;
        }
        ms[fast] = (double)(clock()-start)*1000/CLOCKS_PER_SEC;
    }
    printf("Clipped spans: edges %.3f ms, bounds reject %.3f ms\n",ms[1],ms[2]);
    return 0;
}
