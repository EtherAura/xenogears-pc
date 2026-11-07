/*
 * The present pass's filters (shaders/filters.glsl, present_filter=) on
 * pixel-art pictures of known shape: a 256 x 256 picture, square pixels
 * (aspect 1:1, scale fit), in square windows 4 x (1024), 3.52 x (900) and
 * 0.78 x (200) its size, read back from XG_DUMP_PRESENT's images.
 *
 *   - a flat colour stays that colour under every filter at every size;
 *   - nearest at a whole multiple gives each picture pixel its block, and
 *     sharp is exactly nearest there;
 *   - xbr: straight lines one pixel wide stay exactly as nearest has them
 *     (the corners of larger shapes are kept); a 45° and a 2:1 staircase
 *     become straight edges: the area wrong against the true half plane
 *     (each window pixel's covered share, 4 x 4 samples) is a small part of
 *     nearest's; turning the picture by 90° turns xbr's result the same way;
 *     a window smaller than the picture gets sharp's area average exactly;
 *   - the hi-res copy shown smaller than it is still honors nearest, with
 *     no new edge colors; Sharp explicitly selects coverage averaging.
 *
 *   presentfiltertest <scratch dir>
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);

static int checks, failures;
#include "test/check.h"

#define SIZE 256

static SDL_Window* window;
static char dir[600];
static uint32_t vblank = 1;
static uint16_t picture[SIZE][SIZE];

enum { SOLID, CROSS, DIAGONAL, SHALLOW, DISKS, DISKS_TURNED, SHAPES };

/* Fixture pixel (x, y): 1 white, 0 black (SOLID: one colour). */
static int on(int shape, int x, int y) {
    switch (shape) {
    case CROSS:
        return x % 32 == 16 || y % 32 == 16;
    case DIAGONAL:
        return x + y < 256;
    case SHALLOW:
        return x + 2 * y < 384;
    case DISKS: {
        int a = 2 * (x % 32) - 31, b = 2 * (y % 32) - 37; /* off centre: no symmetry of its own across x = y */
        return a * a + b * b < 300;
    }
    case DISKS_TURNED: /* DISKS turned 90° clockwise: (x, y) shows DISKS at (y, SIZE - 1 - x) */
        return on(DISKS, y, SIZE - 1 - x);
    default:
        return 1;
    }
}

static void load(int shape) {
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            picture[y][x] = shape == SOLID ? (uint16_t)(6 | 19 << 5 | 27 << 10) : on(shape, x, y) ? 0x7FFF : 0;
        }
    }
    RECT r = {0, 0, SIZE, SIZE};
    LoadImage(&r, picture);
}

/* The window's picture (RGB, side x side) with this filter, or NULL. The
 * dump writes only changed pictures: an unchanged one is the last read. */
static uint8_t* last;
static int lastSide;

static uint8_t* shot(int filter, int side) {
    presentSetFilter(filter);
    SDL_SetWindowSize(window, side, side);
    SDL_SyncWindow(window);
    GpuDisplay d = {0, 0, SIZE, SIZE, 0, 1, 0, 0, 0, 0, 0};
    gvkPresent(&d, vblank++); /* the swapchain at the new size */
    gvkPresent(&d, vblank++);
    for (uint32_t v = vblank - 1; v >= vblank - 2; v--) {
        char path[700];
        snprintf(path, sizeof(path), "%s/present_%06u.png", dir, (unsigned)v);
        SDL_Surface* s = SDL_LoadPNG(path);
        if (s == NULL) {
            continue;
        }
        SDL_Surface* rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface(s);
        if (rgb == NULL || rgb->w != side || rgb->h != side) {
            SDL_DestroySurface(rgb);
            return NULL;
        }
        free(last);
        last = malloc((size_t)side * side * 3);
        for (int y = 0; y < side; y++) {
            memcpy(last + (size_t)y * side * 3, (uint8_t*)rgb->pixels + (size_t)y * rgb->pitch, (size_t)side * 3);
        }
        lastSide = side;
        SDL_DestroySurface(rgb);
        break;
    }
    if (last == NULL || lastSide != side) {
        return NULL;
    }
    uint8_t* copy = malloc((size_t)side * side * 3);
    memcpy(copy, last, (size_t)side * side * 3);
    return copy;
}

/* Largest difference of two pictures, any channel. */
static int maxDiff(const uint8_t* a, const uint8_t* b, int side) {
    int worst = 0;
    for (size_t i = 0; i < (size_t)side * side * 3; i++) {
        int d = abs(a[i] - b[i]);
        worst = d > worst ? d : worst;
    }
    return worst;
}

/* The area (window pixels) where a picture differs from the half plane
 * the staircase stands for: DIAGONAL x + y < 256.5, SHALLOW x + 2y < 385
 * (picture pixels; the lines through the middles of the steps), each
 * window pixel's true share from 4 x 4 samples. */
static double edgeError(const uint8_t* img, int side, int shape) {
    double sum = 0.0, k = (double)SIZE / side;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int inside = 0;
            for (int j = 0; j < 4; j++) {
                for (int i = 0; i < 4; i++) {
                    double sx = (x + (i + 0.5) / 4.0) * k, sy = (y + (j + 0.5) / 4.0) * k;
                    inside += shape == DIAGONAL ? sx + sy < 256.5 : sx + 2.0 * sy < 385.0;
                }
            }
            sum += fabs(img[((size_t)y * side + x) * 3] / 255.0 - inside / 16.0);
        }
    }
    return sum;
}

static int run(void) {
    static const int sides[] = {1024, 900, 200};
    static const char* const names[] = {"nearest", "linear", "sharp", "bicubic", "xbr"};

    /* A flat colour: the same everywhere, every filter, every size. */
    load(SOLID);
    uint8_t* flat = shot(PRESENT_FILTER_NEAREST, 1024);
    CHECK(flat != NULL);
    for (int f = 0; f < PRESENT_FILTER_COUNT; f++) {
        for (int s = 0; s < 3; s++) {
            uint8_t* img = shot(f, sides[s]);
            CHECK(img != NULL);
            int worst = 0;
            for (size_t i = 0; i < (size_t)sides[s] * sides[s]; i++) {
                for (int c = 0; c < 3; c++) {
                    int d = abs(img[i * 3 + c] - flat[c]);
                    worst = d > worst ? d : worst;
                }
            }
            EXPECTF(worst <= 1, "%s at %d: a flat colour off by %d", names[f], sides[s], worst);
            free(img);
        }
    }
    free(flat);

    /* Whole multiples: nearest's blocks, sharp the same; xbr keeps lines. */
    load(CROSS);
    uint8_t* nearest = shot(PRESENT_FILTER_NEAREST, 1024);
    uint8_t* sharp = shot(PRESENT_FILTER_SHARP, 1024);
    uint8_t* xbr = shot(PRESENT_FILTER_XBR, 1024);
    CHECK(nearest != NULL && sharp != NULL && xbr != NULL);
    int blocks = 1;
    for (int y = 0; y < 1024 && blocks; y++) {
        for (int x = 0; x < 1024; x++) {
            if ((nearest[((size_t)y * 1024 + x) * 3] > 127) != on(CROSS, x / 4, y / 4)) {
                blocks = 0;
                break;
            }
        }
    }
    EXPECTF(blocks, "nearest at 4x: not each picture pixel's 4 x 4 block");
    EXPECTF(maxDiff(nearest, sharp, 1024) == 0, "sharp at 4x differs from nearest by %d", maxDiff(nearest, sharp, 1024));
    EXPECTF(maxDiff(nearest, xbr, 1024) == 0, "xbr changed straight lines (by %d)", maxDiff(nearest, xbr, 1024));
    free(nearest);
    free(sharp);
    free(xbr);

    /* Staircases: xbr's edges against nearest's steps. */
    for (int shape = DIAGONAL; shape <= SHALLOW; shape++) {
        load(shape);
        for (int s = 0; s < 2; s++) {
            nearest = shot(PRESENT_FILTER_NEAREST, sides[s]);
            xbr = shot(PRESENT_FILTER_XBR, sides[s]);
            CHECK(nearest != NULL && xbr != NULL);
            double en = edgeError(nearest, sides[s], shape), ex = edgeError(xbr, sides[s], shape);
            printf("presentfiltertest: %s at %d: area off the true edge: nearest %.0f, xbr %.0f window pixels\n",
                   shape == DIAGONAL ? "45-degree staircase" : "2:1 staircase", sides[s], en, ex);
            EXPECTF(en > 100.0 && ex < 0.25 * en, "%s at %d: xbr %.0f against nearest %.0f",
                    shape == DIAGONAL ? "45 degrees" : "2:1", sides[s], ex, en);
            free(nearest);
            free(xbr);
        }
    }

    /* Turned by 90°: the result turns with it. */
    load(DISKS);
    uint8_t* a = shot(PRESENT_FILTER_XBR, 1024);
    load(DISKS_TURNED);
    uint8_t* b = shot(PRESENT_FILTER_XBR, 1024);
    CHECK(a != NULL && b != NULL);
    int worst = 0, cut = 0;
    for (int y = 0; y < 1024; y++) {
        for (int x = 0; x < 1024; x++) {
            /* b(x, y) shows a at (y, 1023 - x) */
            const uint8_t* pa = a + ((size_t)(1023 - x) * 1024 + y) * 3;
            const uint8_t* pb = b + ((size_t)y * 1024 + x) * 3;
            for (int c = 0; c < 3; c++) {
                int d = abs(pa[c] - pb[c]);
                worst = d > worst ? d : worst;
            }
            cut += (pa[0] > 127) != on(DISKS, (1023 - x) / 4, y / 4);
        }
    }
    EXPECTF(worst <= 1, "xbr turned by 90 degrees differs by %d", worst);
    EXPECTF(cut > 1000, "xbr left the disks' edges as they were (%d window pixels changed)", cut);
    free(a);
    free(b);

    /* Smaller than the picture: xbr is sharp's area average. */
    for (int shape = CROSS; shape <= DISKS; shape++) {
        load(shape);
        sharp = shot(PRESENT_FILTER_SHARP, 200);
        xbr = shot(PRESENT_FILTER_XBR, 200);
        CHECK(sharp != NULL && xbr != NULL);
        EXPECTF(maxDiff(sharp, xbr, 200) == 0, "xbr below 1x is not sharp (by %d)", maxDiff(sharp, xbr, 200));
        free(sharp);
        free(xbr);
    }

    /* An 8x copy downsampled by both integer and fractional ratios must
     * keep hard edges under Nearest. Sharp retains explicit averaging. */
    hiresSetUpscale(8);
    hiresSetPrecision(0);
    CHECK(gvkHiConfigure(8, HIRES_COLOR_TRUE) == 8);
    memset(picture, 0, sizeof(picture));
    RECT all = {0, 0, SIZE, SIZE};
    LoadImage(&all, picture);
    DRAWENV env;
    SetDefDrawEnv(&env, 0, 0, 1024, 512);
    env.dfe = 1;
    PutDrawEnv(&env);
    POLY_F3 t;
    SetPolyF3(&t);
    setRGB0(&t, 255, 255, 255);
    t.x0 = 0, t.y0 = 0, t.x1 = 251, t.y1 = 7, t.x2 = 9, t.y2 = 240; /* edges at slopes no grid has */
    DrawPrim(&t);
    for(int side=1024;side>=900;side-=124) {
        nearest = shot(PRESENT_FILTER_NEAREST, side);
        sharp = shot(PRESENT_FILTER_SHARP, side);
        CHECK(nearest != NULL && sharp != NULL);
        int blended=0,between=0;
        for(size_t i=0;i<(size_t)side*side;i++) {
            blended+=nearest[i*3]!=0 && nearest[i*3]!=255;
            between+=sharp[i*3]>40 && sharp[i*3]<215;
        }
        EXPECTF(blended==0,"Nearest downsampling at %d added %d blended edge pixels",side,blended);
        EXPECTF(between>200,"Sharp downsampling at %d lost averaging (%d pixels)",side,between);
        EXPECTF(maxDiff(nearest,sharp,side)>0,"Nearest at %d silently became Sharp",side);
        free(nearest);free(sharp);
    }
    gvkHiConfigure(0, HIRES_COLOR_TRUE);
    hiresSetUpscale(1);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "presentfiltertest <scratch dir>\n");
        return 2;
    }
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    /* The C library's environment, which dump.c and the settings read. */
    static char dump[700], range[] = "XG_DUMP_RANGE=0-1000000000", aspect[] = "XG_ASPECT=1:1",
                       scale[] = "XG_SCALE=fit";
    snprintf(dump, sizeof(dump), "XG_DUMP_PRESENT=%s", dir);
    putenv(dump);
    putenv(range);
    putenv(aspect);
    putenv(scale);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("presentfiltertest", 1024, 1024, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window == NULL || !gpuInit(window)) {
        return 1;
    }
    ResetGraph(0);
    SetDispMask(1);
    gpuVBlank();
    vblank = 10;
    int failed = run();
    free(last);
    gpuShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("presentfiltertest: %d checks, %d failed\n", checks, failures + failed);
    return failures != 0 || failed;
}
