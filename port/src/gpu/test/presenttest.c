/*
 * The present pass on known pictures: every display mode the PS1 can show
 * (256/320/368/512/640 wide, 240 and 480 lines, 15- and 24-bit, the game's
 * 216-line range, pictures off centre, past the TV's frame, PAL) at several
 * window sizes, under the display options of the environment (XG_ASPECT,
 * XG_SCALE, XG_PRESENT_FILTER), written with XG_DUMP_PRESENT.
 *
 *   cmake --build build/pc/<you> --target presenttest
 *   SDL_VIDEO_DRIVER=offscreen XG_HEADLESS=1 XG_SETTINGS=none XG_ASPECT=4:3 XG_SCALE=fit \
 *       XG_DUMP_PRESENT=<dir> build/pc/<you>/presenttest
 *
 * VRAM holds a pattern whose colour gives each pixel's coordinates (15-bit:
 * R = x & 31, G = y & 31, B from x >> 5 and y >> 5; 24-bit: R = x & 255,
 * G = y & 255, B from x >> 8 and y >> 8). <dir>/cases.txt lists each case:
 * "vblank dispw disph rgb24 shownw shownh winw winh tvclock tvline clocks
 * rows framelines" (GpuDisplay's TV geometry). A checker compares the
 * images with the layout rules of present.c and the pattern; after the
 * cases, three hotkeys show their notes (the last vblanks).
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
s32 SetVideoMode(s32 mode);

static uint16_t vramImage[512][1024];

static void fillPattern(int rgb24) {
    for (int y = 0; y < 512; y++) {
        if (!rgb24) {
            for (int x = 0; x < 1024; x++) {
                int b = ((x >> 5) * 7 + (y >> 5) * 3) & 31;
                vramImage[y][x] = (uint16_t)((x & 31) | (y & 31) << 5 | b << 10);
            }
            continue;
        }
        uint8_t* bytes = (uint8_t*)vramImage[y];
        for (int px = 0; px < 682; px++) {
            bytes[px * 3] = (uint8_t)px;
            bytes[px * 3 + 1] = (uint8_t)y;
            bytes[px * 3 + 2] = (uint8_t)((px >> 8) * 85 + (y >> 8) * 170 + 30);
        }
    }
    RECT r = {0, 0, 1024, 512};
    LoadImage(&r, vramImage);
}

int main(int argc, char** argv) {
    static const struct {
        int w, h, rgb24;
        int sx, sy, sw, sh; /* DISPENV.screen: the display range (0, 0, 0, 0: LIBGPU's default) */
        int pal;
    } modes[] = {
        {256, 240, 0, 0, 0, 0, 0, 0}, {320, 240, 0, 0, 0, 0, 0, 0}, {368, 240, 0, 0, 0, 0, 0, 0},
        {512, 240, 0, 0, 0, 0, 0, 0}, {640, 240, 0, 0, 0, 0, 0, 0}, {256, 480, 0, 0, 0, 0, 0, 0},
        {320, 480, 0, 0, 0, 0, 0, 0}, {368, 480, 0, 0, 0, 0, 0, 0}, {512, 480, 0, 0, 0, 0, 0, 0},
        {640, 480, 0, 0, 0, 0, 0, 0}, {320, 240, 1, 0, 0, 0, 0, 0}, {640, 480, 1, 0, 0, 0, 0, 0},
        /* The game's ranges: 216 lines from line 10 (fields, worldmap, movies). */
        {320, 224, 0, 0, 10, 256, 216, 0}, {320, 224, 1, 0, 10, 256, 216, 0}, {640, 224, 0, 0, 10, 256, 216, 0},
        /* Battling's interlaced range, an off-centre picture, one past the
         * frame's bottom, one left of it, and PAL. */
        {640, 480, 0, 0, 16, 256, 212, 0}, {320, 240, 0, 16, 20, 200, 180, 0}, {320, 240, 0, 0, 0, 0, 256, 0},
        {320, 240, 0, -12, 0, 0, 0, 0}, {320, 288, 0, 0, 0, 0, 288, 1}, {320, 256, 0, 0, 8, 256, 256, 1},
    };
    static const int windows[][2] = {
        {960, 720}, {1920, 1080}, {1280, 1024}, {2560, 1080}, {641, 479}, {333, 251}, {200, 150}, {3840, 2160},
    };
    const char* dir = getenv("XG_DUMP_PRESENT");
    if (dir == NULL || dir[0] == '\0') {
        fprintf(stderr, "presenttest: set XG_DUMP_PRESENT\n");
        return 1;
    }
    (void)argc;
    (void)argv;
    /* Vblanks 1-199 settle; cases from 200. */
    static char range[] = "XG_DUMP_RANGE=200-1000000";
    putenv(range); /* the C library's environment, which dump.c reads (also on Windows) */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("presenttest", 960, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window == NULL || !gpuInit(window)) {
        return 1;
    }
    ResetGraph(0);
    SetDispMask(1);
    for (int i = 0; i < 199; i++) {
        gpuVBlank();
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/cases.txt", dir);
    FILE* cases = fopen(path, "w");
    uint32_t vblank = 200;
    int lastRgb24 = -1;
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        if (modes[m].rgb24 != lastRgb24) {
            fillPattern(modes[m].rgb24);
            lastRgb24 = modes[m].rgb24;
        }
        DISPENV env;
        SetDefDispEnv(&env, 0, 0, modes[m].w, modes[m].h);
        env.isrgb24 = (u8)modes[m].rgb24;
        env.isinter = modes[m].h > (modes[m].pal ? 288 : 256);
        env.screen.x = (s16)modes[m].sx;
        env.screen.y = (s16)modes[m].sy;
        env.screen.w = (s16)modes[m].sw;
        env.screen.h = (s16)modes[m].sh;
        SetVideoMode(modes[m].pal);
        PutDispEnv(&env);
        const GpuDisplay* d = gpuDisplay();
        for (size_t s = 0; s < sizeof(windows) / sizeof(windows[0]); s++) {
            SDL_SetWindowSize(window, windows[s][0], windows[s][1]);
            SDL_SyncWindow(window);
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            gpuVBlank();
            fprintf(cases, "%u %d %d %d %d %d %d %d %d %d %d %d %d\n", vblank, modes[m].w, modes[m].h, modes[m].rgb24,
                    d->w, d->h, pw, ph, d->tvClock, d->tvLine, d->clocks, d->rows, d->frameLines);
            vblank++;
        }
    }
    /* The notes: one vblank each, 960x720, the last picture. */
    SDL_SetWindowSize(window, 960, 720);
    SDL_SyncWindow(window);
    presentCycleScale();
    gpuVBlank();
    presentCycleFilter();
    gpuVBlank();
    presentCycleAspect();
    gpuVBlank();
    fprintf(cases, "# notes at %u-%u\n", vblank, vblank + 2);
    fclose(cases);
    gpuShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
