/*
 * Shader presets (xg-htn.35.28): the preset reader on the test presets in
 * test/slang (our own), then the chain on the present pass, headless: a
 * known picture, presented with and without each preset (XG_DUMP_PRESENT
 * into the scratch directory, read back), compared with the plain picture.
 *
 *   slangtest <scratch dir>          (built with glslang: XG_GLSLANG)
 *   slangtest <scratch dir> <preset>...   try presets: each one's load result,
 *                                         its picture as <scratch>/<name>.png
 *                                         (not with XG_SLANG_TRY_NOSAVE set)
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu.h"
#include "gpu/shaderchain.h"
#include "system/settings.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);

static int checks, failures;
#include "test/check.h"

static char dir[1024], fixtures[1024];
static uint32_t vblanks;
static uint16_t image[240][320];

static const char* fixture(const char* name) {
    static char path[2][1100];
    static int k;
    k ^= 1;
    snprintf(path[k], sizeof(path[k]), "%s/%s", fixtures, name);
    return path[k];
}

static void picture(int variant) {
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            int b = ((x + y) >> 2) & 31;
            image[y][x] = (uint16_t)(((x + variant * 7) & 31) | (y & 31) << 5 | b << 10);
        }
    }
    RECT r = {0, 0, 320, 240};
    LoadImage(&r, image);
    DrawSync(0);
}

/* One vblank presented; its picture (the dump of it, or of the last that
 * changed), w x h RGB24 into *out (caller frees). */
static uint8_t* present(int* w, int* h) {
    gpuVBlank();
    vblanks++;
    for (uint32_t v = vblanks; v > 0; v--) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/present_%06u.png", dir, v);
        SDL_Surface* s = SDL_LoadPNG(path);
        if (s == NULL) {
            continue;
        }
        SDL_Surface* rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface(s);
        *w = rgb->w;
        *h = rgb->h;
        uint8_t* out = malloc((size_t)rgb->w * rgb->h * 3);
        for (int y = 0; y < rgb->h; y++) {
            memcpy(out + (size_t)y * rgb->w * 3, (uint8_t*)rgb->pixels + (size_t)y * rgb->pitch, (size_t)rgb->w * 3);
        }
        SDL_DestroySurface(rgb);
        return out;
    }
    return NULL;
}

/* Sample points well inside the picture: the window's middle half. */
#define POINTS 64
static void points(int w, int h, int* px, int* py) {
    for (int i = 0; i < POINTS; i++) {
        px[i] = w / 4 + (i * 37) % (w / 2);
        py[i] = h / 4 + (i * 53) % (h / 2);
    }
}

/* A dump left by an earlier run must not stand in for one this run skipped
 * (the dumps skip a picture equal to the last one). */
static SDL_EnumerationResult SDLCALL removeDump(void* user, const char* dirname, const char* name) {
    (void)user;
    if (strncmp(name, "present_", 8) == 0) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", dirname, name);
        SDL_RemovePath(path);
    }
    return SDL_ENUM_CONTINUE;
}

static int near(int a, int b, int tol) {
    return abs(a - b) <= tol;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: slangtest <scratch dir>\n");
        return 2;
    }
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    snprintf(fixtures, sizeof(fixtures), "%s/port/src/gpu/test/slang", XG_ROOT);
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("XG_DUMP_PRESENT", dir);
    SDL_CreateDirectory(dir);
    SDL_EnumerateDirectory(dir, removeDump, NULL);

    /* ---- the reader alone ---- */
    static SlangPreset p;
    EXPECT(slangLoad(&p, fixture("twopass.slangp")));
    EXPECT(p.passes == 2 && p.textures == 1 && p.params == 2);
    EXPECT(strcmp(p.pass[0].alias, "Doubled") == 0);
    EXPECT(p.pass[0].scaleSet && p.pass[0].scaleType[0] == SLANG_SCALE_SOURCE && p.pass[0].scale[0] == 2.0f);
    EXPECT(!p.pass[1].scaleSet && p.pass[0].filter == 0);
    EXPECT(strcmp(p.param[0].name, "TINT_STRENGTH") == 0 && p.param[0].value == 1.0f && p.param[0].step == 0.25f);
    EXPECT(strcmp(p.param[1].name, "ROW_DARK") == 0 && p.param[1].value == 0.0f); /* the preset's value */
    EXPECT(strstr(p.pass[1].fragment, "TINT_STRENGTH") != NULL && strstr(p.pass[1].fragment, "#pragma") == NULL);
    EXPECT(strstr(p.pass[1].vertex, "gl_Position") != NULL && strstr(p.pass[1].fragment, "gl_Position") == NULL);
    slangApplyParams(&p, "TINT_STRENGTH=0.5;NOPE=3;ROW_DARK=9");
    EXPECT(p.param[0].value == 0.5f && p.param[1].value == 1.0f); /* clamped to its maximum */
    char text[256];
    slangParamsText(&p, text, sizeof(text));
    EXPECT(strcmp(text, "TINT_STRENGTH=0.5;ROW_DARK=1") == 0);
    slangFree(&p);
    EXPECT(!slangLoad(&p, fixture("missing.slangp")) && strstr(p.error, "missing.slangp") != NULL);
    EXPECT(!slangLoad(&p, fixture("common.inc")) && strstr(p.error, "not a .slangp") != NULL);
    /* the port's built-in movie preset: three passes, two parameters */
    EXPECT(slangLoad(&p, "builtin:movie-smooth.slangp") && p.passes == 3 && p.params == 2);
    EXPECT(strstr(p.pass[0].fragment, "DIRECTION 0") != NULL && strstr(p.pass[1].fragment, "DIRECTION 1") != NULL);
    slangFree(&p);
    EXPECT(!slangLoad(&p, "builtin:nothing.slangp"));
    /* the Slang shading language (no #version): named, refused */
    EXPECT(!slangLoad(&p, fixture("slanglang.slang")) && strstr(p.error, "Slang-language") != NULL);

    /* ---- the chain on the present pass ---- */
    if (argc > 2) {
        checks = failures = 0; /* trying presets, not testing */
    }
    if (!SDL_Init(SDL_INIT_VIDEO) || !gpuInit(NULL)) {
        fprintf(stderr, "slangtest: no GPU\n");
        return 1;
    }
    if (!shaderChainAvailable()) {
        printf("slangtest: %d reader checks passed; this build has no glslang, no chain checks\n", checks);
        gpuShutdown();
        return failures != 0;
    }
    ResetGraph(0);
    SetDispMask(1);
    DISPENV env;
    SetDefDispEnv(&env, 0, 0, 320, 240);
    PutDispEnv(&env);
    picture(0);
    int w, h, px[POINTS], py[POINTS];
    uint8_t* plain = present(&w, &h);
    EXPECT(plain != NULL);
    if (plain == NULL) {
        return 1;
    }
    points(w, h, px, py);

    if (argc > 2) {
        /* try the presets named: load, a few vblanks, the picture kept */
        int bad = 0;
        for (int i = 2; i < argc; i++) {
            uint64_t t0 = SDL_GetTicksNS();
            int ok = shaderChainSetPath(argv[i]);
            uint64_t t1 = SDL_GetTicksNS();
            uint8_t* out = NULL;
            for (int k = 0; ok && k < 3; k++) {
                free(out);
                out = present(&w, &h);
            }
            const char* base = strrchr(argv[i], '/');
            base = base != NULL ? base + 1 : argv[i];
            if (ok && out != NULL) {
                if (SDL_getenv("XG_SLANG_TRY_NOSAVE") == NULL) {
                    char path[1300];
                    snprintf(path, sizeof(path), "%s/%s.png", dir, base);
                    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, out, w * 3);
                    SDL_SavePNG(s, path);
                    SDL_DestroySurface(s);
                }
                printf("slangtest: %s: %d passes, %d parameters, loaded in %.0f ms\n", base, shaderChainPasses(),
                       shaderChainParamCount(), (double)(t1 - t0) / 1e6);
            } else {
                printf("slangtest: %s: FAILED: %s\n", base, shaderChainError());
                bad++;
            }
            free(out);
        }
        shaderChainSetPath("");
        free(plain);
        gpuShutdown();
        SDL_Quit();
        return bad != 0;
    }

    /* invert: every picture pixel 255 - v; outside the picture still black */
    EXPECT(shaderChainSetPath(fixture("invert.slang")));
    EXPECT(shaderChainPasses() == 1);
    uint8_t* inv = present(&w, &h);
    int ok = inv != NULL;
    for (int i = 0; ok && i < POINTS; i++) {
        const uint8_t* a = plain + ((size_t)py[i] * w + px[i]) * 3;
        const uint8_t* b = inv + ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            ok &= b[c] == 255 - a[c];
        }
    }
    EXPECT(ok);
    free(inv);

    /* two passes, a named pass, a LUT (128, 255, 64), a parameter */
    EXPECT(shaderChainSetPath(fixture("twopass.slangp")));
    EXPECT(shaderChainPasses() == 2 && shaderChainParamCount() == 2);
    uint8_t* tint = present(&w, &h);
    static const int lut[3] = {128, 255, 64};
    ok = tint != NULL;
    for (int i = 0; ok && i < POINTS; i++) {
        const uint8_t* a = plain + ((size_t)py[i] * w + px[i]) * 3;
        const uint8_t* b = tint + ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            ok &= near(b[c], (int)lroundf(a[c] * lut[c] / 255.0f), 1);
        }
    }
    EXPECT(ok);
    free(tint);
    shaderChainSetParam(0, 0.0f); /* TINT_STRENGTH: the picture as it is */
    EXPECT(strstr(settingsGet("present_shader_params"), "TINT_STRENGTH=0") != NULL);
    uint8_t* untinted = present(&w, &h);
    ok = untinted != NULL;
    for (int i = 0; ok && i < POINTS; i++) {
        const uint8_t* a = plain + ((size_t)py[i] * w + px[i]) * 3;
        const uint8_t* b = untinted + ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            ok &= near(b[c], a[c], 1);
        }
    }
    EXPECT(ok);
    free(untinted);
    shaderChainResetParams();
    EXPECT(shaderChainParam(0)->value == 1.0f);

    /* feedback: half this picture, half the last output: 1/2, 3/4 */
    EXPECT(shaderChainSetPath(fixture("feedback.slangp")));
    uint8_t* f1 = present(&w, &h);
    uint8_t* f2 = present(&w, &h);
    ok = f1 != NULL && f2 != NULL;
    for (int i = 0; ok && i < POINTS; i++) {
        size_t k = ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            ok &= near(f1[k + c], (int)lroundf(plain[k + c] * 0.5f), 1) &&
                  near(f2[k + c], (int)lroundf(plain[k + c] * 0.75f), 2);
        }
    }
    EXPECT(ok);
    free(f1);
    free(f2);
    /* OriginalHistory1: the last frame's picture, after the picture changes */
    shaderChainSetParam(0, 1.0f);
    picture(1);
    uint8_t* hist = present(&w, &h); /* shows picture 0, the last frame's */
    ok = hist != NULL;
    for (int i = 0; ok && i < POINTS; i++) {
        size_t k = ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            ok &= hist[k + c] == plain[k + c];
        }
    }
    EXPECT(ok);
    free(hist);
    picture(0);

    /* a shader that does not compile: an error naming it, the plain picture */
    EXPECT(!shaderChainSetPath(fixture("broken.slang")));
    EXPECT(strstr(shaderChainError(), "broken.slang:8:") != NULL && strstr(shaderChainError(), "undefinedThing") != NULL);
    EXPECT(shaderChainPasses() == 0);
    uint8_t* fallback = present(&w, &h);
    EXPECT(fallback != NULL && memcmp(fallback, plain, (size_t)w * h * 3) == 0);
    free(fallback);

    /* the built-in movie preset compiles and draws: close to the picture */
    EXPECT(shaderChainSetPath("builtin:movie-smooth.slangp") && shaderChainPasses() == 3);
    uint8_t* smooth = present(&w, &h);
    ok = smooth != NULL;
    long diff = 0;
    for (int i = 0; ok && i < POINTS; i++) {
        size_t k = ((size_t)py[i] * w + px[i]) * 3;
        for (int c = 0; c < 3; c++) {
            diff += abs(smooth[k + c] - plain[k + c]);
        }
    }
    EXPECT(ok && diff < POINTS * 3 * 24);
    free(smooth);

    /* off again: the very same picture as before any preset */
    EXPECT(shaderChainSetPath(fixture("sharpen.slang")));
    free(present(&w, &h));
    shaderChainSetPath("");
    EXPECT(strcmp(settingsGet("present_shader"), "") == 0);
    uint8_t* again = present(&w, &h);
    EXPECT(again != NULL && memcmp(again, plain, (size_t)w * h * 3) == 0);
    free(again);
    free(plain);

    printf("slangtest: %d checks, %d failed\n", checks, failures);
    gpuShutdown();
    SDL_Quit();
    return failures != 0;
}
