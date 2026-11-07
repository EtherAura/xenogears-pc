/* Edge-length reuse and batched attribute equivalence: exact floats and fresh
 * per-draw metadata, thin/degenerate faces, winding and perspective depth. */
#include "../gp0.c"
#include <time.h>
static int checks;
#include "test/check.h"

static uint32_t randomWord(uint32_t* seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

static void randomFace(HiVertex v[3], uint32_t* seed, unsigned n) {
    for (int k = 0; k < 3; k++) {
        uint32_t* words = (uint32_t*)&v[k];
        for (size_t i = 0; i < sizeof(v[k]) / sizeof(*words); i++) words[i] = randomWord(seed);
        v[k].x = ((int)(randomWord(seed) % 131073) - 65536) / 16.0f;
        v[k].y = ((int)(randomWord(seed) % 65537) - 32768) / 32.0f;
        v[k].u = (float)((int)(randomWord(seed) % 65536) - 32768);
        v[k].v = (float)((int)(randomWord(seed) % 65536) - 32768);
        v[k].r = (float)(randomWord(seed) & 255);
        v[k].g = (float)(randomWord(seed) & 255);
        v[k].b = (float)(randomWord(seed) & 255);
        v[k].w = ldexpf(1 + (randomWord(seed) & 1023) / 1024.0f, (int)(n % 41) - 20);
    }
    if (n % 17 == 0) { v[2].x = v[0].x; v[2].y = v[0].y; }
    if (n % 19 == 0) v[2].y = v[0].y + 0.0001f;
    if (n % 23 == 0) v[n % 3].w = 0;
    if (n % 29 == 0) v[n % 3].w = -v[n % 3].w;
}

static void face(HiVertex v[3], unsigned n) {
    memset(v, 0, 3 * sizeof(*v));
    for (int i = 0; i < 3; i++) {
        v[i].x = (float)(n % 97) / 16 + (i == 1 ? 80.125f : i == 2 ? 17.5f : 0);
        v[i].y = (float)(n / 97) / 32 + (i == 2 ? (n % 13 ? 61.75f : .001f) : 0);
        v[i].u = i * 31; v[i].v = i == 2 ? 63 : 0;
        v[i].r = n % 255; v[i].g = i * 70; v[i].b = 248;
        v[i].w = 1 + (n % 11) / 8.0f + i / 4.0f;
        v[i].mode = DRAW_PRECISE | DRAW_SURFACE;
    }
    if (n % 29 == 0) v[2].y = v[0].y;
    if (n & 1) { HiVertex swap=v[1]; v[1]=v[2]; v[2]=swap; }
}
int main(void) {
    HiVertex v[3], actual[21], expected[21];
    for (unsigned n = 0; n < 12000; n++) {
        face(v, n);
        for (unsigned pass = 0; pass < 2; pass++) {
            for (int i = 0; i < 3; i++) {
                v[i].tex = n + pass * 17;
                v[i].clipMin = pass * 300; v[i].clipMax = n * 9;
                v[i].mode |= pass ? DRAW_DITHER : 0;
                v[i].sample[0] = n + pass * 10000;
            }
            int count = hiSeal(v, actual, n & 7, 1);
            int reference = hiSeal(v, expected, n & 7, 0);
            CHECK(count == reference);
            CHECK(!memcmp(actual, expected, (size_t)count * sizeof(*actual)));
            count = hiSeal(v, actual, n & 7, 2);
            CHECK(count == reference);
            CHECK(!memcmp(actual, expected, (size_t)count * sizeof(*actual)));
        }
    }
    uint32_t seed = 0x35A114;
    for (unsigned n = 0; n < 100000; n++) {
        randomFace(v, &seed, n);
        int count = hiSeal(v, actual, n & 7, 2);
        int reference = hiSeal(v, expected, n & 7, 1);
        CHECK(count == reference);
        CHECK(!memcmp(actual, expected, (size_t)count * sizeof(*actual)));
    }
    clock_t start = clock();
    for (unsigned n = 0; n < 100000; n++) { face(v, n % 128); hiSeal(v, expected, 7, 1); }
    double direct = (double)(clock() - start) / CLOCKS_PER_SEC;
    start = clock();
    for (unsigned n = 0; n < 100000; n++) { face(v, n % 128); hiSeal(v, actual, 7, 2); }
    double cached = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("Seam reuse: %d checks; repeated faces scalar %.3f ms, batched %.3f ms\n", checks, direct * 1000, cached * 1000);
    return 0;
}
