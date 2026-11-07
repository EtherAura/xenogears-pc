/* Deterministic overload/recovery scenarios plus the real asynchronous Vulkan
 * query lifecycle. GPU performance itself is measured separately in replays. */
#include "common.h"
#include "psyq.h"
#include "gpu/gpu_internal.h"
#include "gpu/autores.h"
#include "system/settings.h"
#include "test/env.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetTile(void* p);
static int checks;
#include "test/check.h"

static void samples(AutoResolution* a, int n, double ms, int frames) {
    for (int i = 0; i < frames; ++i) autoResolutionSample(a, n, 12, ms, 1);
}
static int policy(void) {
    AutoResolution a;
    autoResolutionReset(&a, 4);
    samples(&a, 4, 18, 60); CHECK(a.limit == 4); /* warm-up */
    samples(&a, 4, 18, 59); CHECK(a.limit == 4);
    samples(&a, 4, 18, 1); CHECK(a.limit == 3);
    samples(&a, 3, 18, 60); CHECK(a.limit == 3); /* new-scale warm-up */
    samples(&a, 3, 18, 30); CHECK(a.limit == 3);
    samples(&a, 3, 12, 30); CHECK(a.limit == 3); /* interrupted overload */
    samples(&a, 3, 6, 900); CHECK(a.limit == 3); /* too little next-step headroom */
    autoResolutionRestart(&a); CHECK(a.limit == 3 && a.constrained);
    samples(&a, 3, 1, 60); CHECK(a.limit == 3); /* resume discards old history */
    samples(&a, 3, 1, 299); CHECK(a.limit == 3);
    samples(&a, 3, 1, 1); CHECK(a.limit == 4);
    samples(&a, 4, 18, 60); CHECK(a.limit == 4);
    samples(&a, 4, 1, 29); samples(&a, 4, 1000, 1); CHECK(a.limit == 4);
    samples(&a, 4, 1, 30); CHECK(a.limit == 4); /* one spike never drops */

    AutoResolution before = a;
    autoResolutionSample(&a, 4, 12, NAN, 1);
    autoResolutionSample(&a, 4, 12, INFINITY, 1);
    autoResolutionSample(&a, 4, 12, -1, 1);
    autoResolutionSample(&a, 4, 12, 1, 0);
    autoResolutionSample(&a, 4, 12, 1, 121);
    autoResolutionSample(&a, 4, 0, 1, 1);
    autoResolutionSample(&a, 0, 12, 1, 1);
    CHECK(!memcmp(&a, &before, sizeof(a)));

    autoResolutionReset(&a, 4);
    for (int i = 0; i < 60; ++i) autoResolutionSample(&a, 4, 12, 36, 2);
    CHECK(a.limit == 3); /* GPU total divided by video frames, not submissions */
    autoResolutionCeiling(&a, 2); CHECK(a.limit == 2);
    autoResolutionCeiling(&a, 8); CHECK(a.limit == 2); /* resize retains GPU cap */
    autoResolutionReset(&a, 2);
    autoResolutionCeiling(&a, 8); CHECK(a.limit == 8); /* unrestricted resize */
    samples(&a, 2, 1, 1000); CHECK(a.limit == 8); /* resize pending: no recovery cap */
    autoResolutionReset(&a, 4);
    samples(&a, 4, 100, 120); CHECK(a.limit == 3);
    samples(&a, 3, 100, 120); CHECK(a.limit == 2);
    samples(&a, 2, 100, 120); CHECK(a.limit == 1);
    samples(&a, 1, 100, 1000); CHECK(a.limit == 1);
    autoResolutionCeiling(&a, 12);
    CHECK(a.limit == 1);
    samples(&a, 1, 1, 300); CHECK(a.limit == 2); /* recovers from minimum */
    samples(&a, 2, 1, 359); CHECK(a.limit == 2);
    samples(&a, 2, 1, 1); CHECK(a.limit == 3);
    return 0;
}

static void draw(void) {
    TILE t; SetTile(&t); setRGB0(&t, 248, 0, 0);
    t.x0 = t.y0 = 20; t.w = t.h = 32;
    DrawPrim(&t);
}

int main(int argc, char** argv) {
    if (argc == 3) {
        testSetEnv("XG_SETTINGS", argv[2]);
        if (!strcmp(argv[1], "write")) {
            hiresSetGpuBudget(9); hiresSetUpscale(0);
            CHECK(settingsSave());
        } else {
            CHECK(hiresGpuBudget() == 9); CHECK(hiresUpscale() == 0);
        }
        return 0;
    }
    CHECK(!policy());
    testSetEnv("XG_SETTINGS", "none");
    CHECK(hiresUpscale() == 1); CHECK(hiresGpuBudget() == 0);
    hiresSetGpuBudget(-1); hiresSetGpuBudget(51); CHECK(hiresGpuBudget() == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL));
    hiresSetPrecision(0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); PutDrawEnv(&env);
    DISPENV disp; SetDefDispEnv(&disp, 0, 0, 320, 224); PutDispEnv(&disp); SetDispMask(1);
    hiresSetGpuBudget(1); hiresSetUpscale(3);
    for (int i = 0; i < 150; ++i) { draw(); gpuVBlank(); }
    CHECK(gvkHiScale() == 3); /* fixed N ignores the budget */
    double ms; unsigned frames; int scale;
    CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    hiresSetUpscale(1); gpuVBlank(); CHECK(gvkHiScale() == 0);
    CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));

    hiresSetGpuBudget(50); hiresSetUpscale(0); gpuVBlank();
    int available = gvkGpuTimeEnable(1), measured = 0;
    double total = 0; unsigned elapsed = 0;
    uint32_t pixel;
    RECT r = {20, 20, 1, 1};
    for (int i = 0; i < 90; ++i) {
        /* Multiple submissions in one vblank, including a readback. */
        draw(); StoreImage(&r, &pixel);
        draw(); StoreImage(&r, &pixel);
        CHECK((pixel & 0xffff) == 31); /* measurement never changes native VRAM */
        if (gvkGpuTimeTake(&ms, &frames, &scale)) {
            CHECK(isfinite(ms) && ms > 0 && frames > 0 && frames <= 120);
            CHECK(scale == (gvkHiScale() > 0 ? gvkHiScale() : 1));
            total += ms; elapsed += frames; measured++;
        }
        gpuVBlank();
    }
    CHECK(!available || measured > 10);
    printf("Auto GPU timing: %d samples, %.3f ms over %u video frames\n", measured, total, elapsed);
    gvkGpuTimeReset(); CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    hiresGpuTimePause(); CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    SetDispMask(0); gpuVBlank(); CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    SetDispMask(1); disp.isrgb24 = 1; PutDispEnv(&disp); gpuVBlank();
    CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    disp.isrgb24 = 0; PutDispEnv(&disp); gpuVBlank();
    hiresStateLoaded(); CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    hiresSetGpuBudget(0); gpuVBlank(); CHECK(!gvkGpuTimeTake(&ms, &frames, &scale));
    CHECK(hiresGpuBudget() == 0 && hiresUpscale() == 0);
    gpuShutdown(); SDL_Quit();
    printf("Auto resolution: %d checks passed\n", checks);
    return 0;
}
