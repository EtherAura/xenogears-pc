/*
 * Replay a LIBGPU call trace recorded in PCSX-Redux through the GPU layer and
 * compare VRAM with Redux's at the frame markers that have a snapshot.
 *
 *   gpureplay TRACEDIR [OUTDIR] [--no-resync] [--images N] [--verbose N]
 *
 * TRACEDIR holds trace.txt and vram_<marker>.bin (the recorder is
 * tools/redux_gputrace.py + redux_gputrace.lua; the line format is
 * described there). At each snapshot the mismatching pixels are counted; the
 * first N mismatching markers (default 5) are written to OUTDIR as
 * <marker>_ours.png, <marker>_redux.png and <marker>_diff.png. Unless
 * --no-resync, VRAM is then set to Redux's so each interval is judged alone.
 * With shots.txt (redux_gputrace.py --shots), the display area at every marker is
 * compared with Redux's screenshot (shot_<marker>.bin) too; the first N
 * mismatching ones are written as <marker>_screen_ours.png / _redux.png.
 * Exits non-zero if any snapshot or screenshot differs.
 */

#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu_internal.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);

static uint16_t* readVram(const char* dir, int marker) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/vram_%05d.bin", dir, marker);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    uint16_t* v = malloc(VRAM_W * VRAM_H * 2);
    size_t n = fread(v, 2, VRAM_W * VRAM_H, f);
    fclose(f);
    if (n != VRAM_W * VRAM_H) {
        free(v);
        return NULL;
    }
    return v;
}

static void toRgb(const uint16_t* v, uint8_t* rgb) {
    for (int i = 0; i < VRAM_W * VRAM_H; i++) {
        for (int k = 0; k < 3; k++) {
            uint32_t c = (v[i] >> (5 * k)) & 31;
            rgb[i * 3 + k] = (uint8_t)(c << 3 | c >> 2);
        }
    }
}

typedef struct {
    int w, h, bpp;
} Shot;

static Shot* shots;
static int verbose;
static int shotCount;

static void loadShots(const char* dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/shots.txt", dir);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    int m, w, h, bpp;
    while (fscanf(f, "%d %d %d %d", &m, &w, &h, &bpp) == 4) {
        if (m >= shotCount) {
            int n = m + 1024;
            shots = realloc(shots, sizeof(*shots) * (size_t)n);
            memset(shots + shotCount, 0, sizeof(*shots) * (size_t)(n - shotCount));
            shotCount = n;
        }
        shots[m] = (Shot){w, h, bpp};
    }
    fclose(f);
}

/* Compare the display area with Redux's screenshot; returns differing pixels. */
static int compareShot(const char* dir, const char* out, int marker, int* written, int images) {
    if (marker >= shotCount || shots[marker].w == 0) {
        return -1;
    }
    Shot s = shots[marker];
    char path[1024];
    snprintf(path, sizeof(path), "%s/shot_%05d.bin", dir, marker);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t bytes = (size_t)s.w * s.h * (s.bpp == 24 ? 3 : 2);
    uint8_t* ref = malloc(bytes);
    size_t got = fread(ref, 1, bytes, f);
    fclose(f);
    if (got != bytes) {
        free(ref);
        return -1;
    }
    GpuDisplay d = *gpuDisplay();
    d.w = s.w;
    d.h = s.h;
    d.enabled = 1;
    uint8_t* ours = malloc((size_t)s.w * s.h * 3);
    uint8_t* theirs = malloc((size_t)s.w * s.h * 3);
    gpuReadVram(NULL);
    displayToRgb(&d, ours);
    for (int i = 0; i < s.w * s.h; i++) {
        if (s.bpp == 24) {
            memcpy(theirs + i * 3, ref + i * 3, 3);
        } else {
            uint16_t c = (uint16_t)(ref[i * 2] | ref[i * 2 + 1] << 8);
            for (int k = 0; k < 3; k++) {
                uint32_t v = (c >> (5 * k)) & 31;
                theirs[i * 3 + k] = (uint8_t)(v << 3 | v >> 2);
            }
        }
    }
    int diff = 0;
    for (int i = 0; i < s.w * s.h; i++) {
        if (memcmp(ours + i * 3, theirs + i * 3, 3) != 0) {
            if (diff < verbose) {
                printf("  (%d,%d) ours %02x%02x%02x redux %02x%02x%02x\n", i % s.w, i / s.w, ours[i * 3],
                       ours[i * 3 + 1], ours[i * 3 + 2], theirs[i * 3], theirs[i * 3 + 1], theirs[i * 3 + 2]);
            }
            diff++;
        }
    }
    if (diff && (*written)++ < images) {
        snprintf(path, sizeof(path), "%s/%05d_screen_ours.png", out, marker);
        pngWrite(path, s.w, s.h, ours);
        snprintf(path, sizeof(path), "%s/%05d_screen_redux.png", out, marker);
        pngWrite(path, s.w, s.h, theirs);
    }
    free(ref);
    free(ours);
    free(theirs);
    return diff;
}

static uint32_t* words;
static size_t wordCap;

/* Hex words from s; returns the count. */
static size_t parseWords(char* s) {
    size_t n = 0;
    while (*s != '\0') {
        while (*s == ' ') {
            s++;
        }
        if (*s == '\0' || *s == '\n') {
            break;
        }
        char* end;
        unsigned long v = strtoul(s, &end, 16);
        if (end == s) {
            break;
        }
        if (n == wordCap) {
            wordCap = wordCap ? wordCap * 2 : 4096;
            words = realloc(words, wordCap * sizeof(*words));
        }
        words[n++] = (uint32_t)v;
        s = end;
    }
    return n;
}

static char* parseDrawEnv(char* s, DRAWENV* env) {
    int v[17];
    int used = 0;
    for (int i = 0; i < 17; i++) {
        int n = 0;
        sscanf(s + used, "%d%n", &v[i], &n);
        used += n;
    }
    memset(env, 0, sizeof(*env));
    env->clip.x = (s16)v[0];
    env->clip.y = (s16)v[1];
    env->clip.w = (s16)v[2];
    env->clip.h = (s16)v[3];
    env->ofs[0] = (s16)v[4];
    env->ofs[1] = (s16)v[5];
    env->tw.x = (s16)v[6];
    env->tw.y = (s16)v[7];
    env->tw.w = (s16)v[8];
    env->tw.h = (s16)v[9];
    env->tpage = (u16)v[10];
    env->dtd = (u8)v[11];
    env->dfe = (u8)v[12];
    env->isbg = (u8)v[13];
    env->r0 = (u8)v[14];
    env->g0 = (u8)v[15];
    env->b0 = (u8)v[16];
    return s + used;
}

int main(int argc, char** argv) {
    const char* dir = NULL;
    const char* out = ".";
    int resync = 1;
    int images = 5;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-resync") == 0) {
            resync = 0;
        } else if (strcmp(argv[i], "--verbose") == 0 && i + 1 < argc) {
            verbose = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--images") == 0 && i + 1 < argc) {
            images = atoi(argv[++i]);
        } else if (dir == NULL) {
            dir = argv[i];
        } else {
            out = argv[i];
        }
    }
    if (dir == NULL) {
        fprintf(stderr, "usage: gpureplay TRACEDIR [OUTDIR] [--no-resync] [--images N]\n");
        return 2;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/trace.txt", dir);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 2;
    }
    SDL_Init(SDL_INIT_VIDEO);
    if (!gpuInit(NULL)) {
        return 1;
    }
    SDL_CreateDirectory(out);
    /* Traces start with the game running: its display is on. */
    SetDispMask(1);
    static uint16_t ours[VRAM_H][VRAM_W];
    static uint8_t rgbA[VRAM_W * VRAM_H * 3], rgbB[VRAM_W * VRAM_H * 3];
    char* line = NULL;
    size_t cap = 0;
    int started = 0, snapshots = 0, bad = 0;
    int shotsCompared = 0, shotsBad = 0, shotImages = 0;
    loadShots(dir);
    long total = 0;
    while (getline(&line, &cap, f) > 0) {
        char kind = line[0];
        char* rest = line + 1;
        if (kind == 'V') {
            int marker = atoi(rest);
            gpuVBlank();
            if (getenv("XG_GP0_PROBE") != NULL) {
                fprintf(stderr, "V %d display (%d,%d)\n", marker, gpuDisplay()->x, gpuDisplay()->y);
            }
            if (started) {
                int d = compareShot(dir, out, marker, &shotImages, images);
                if (d >= 0) {
                    shotsCompared++;
                    if (d > 0) {
                        shotsBad++;
                        if (shotsBad <= 40) {
                            printf("marker %5d: screen %d pixels differ\n", marker, d);
                        }
                    }
                }
            }
            uint16_t* ref = readVram(dir, marker);
            if (ref == NULL) {
                continue;
            }
            if (started) {
                gpuReadVram(&ours[0][0]);
                int diff = 0, x0 = VRAM_W, y0 = VRAM_H, x1 = -1, y1 = -1;
                for (int y = 0; y < VRAM_H; y++) {
                    for (int x = 0; x < VRAM_W; x++) {
                        if (ours[y][x] != ref[y * VRAM_W + x]) {
                            diff++;
                            x0 = x < x0 ? x : x0;
                            y0 = y < y0 ? y : y0;
                            x1 = x > x1 ? x : x1;
                            y1 = y > y1 ? y : y1;
                        }
                    }
                }
                snapshots++;
                total += diff;
                if (diff) {
                    printf("marker %5d: %7d pixels differ in (%d,%d)-(%d,%d)\n", marker, diff, x0, y0, x1, y1);
                    if (bad++ < images) {
                        toRgb(&ours[0][0], rgbA);
                        toRgb(ref, rgbB);
                        snprintf(path, sizeof(path), "%s/%05d_ours.png", out, marker);
                        pngWrite(path, VRAM_W, VRAM_H, rgbA);
                        snprintf(path, sizeof(path), "%s/%05d_redux.png", out, marker);
                        pngWrite(path, VRAM_W, VRAM_H, rgbB);
                        for (int i = 0; i < VRAM_W * VRAM_H; i++) {
                            int d = (&ours[0][0])[i] != ref[i];
                            rgbA[i * 3] = d ? 255 : rgbB[i * 3] / 4;
                            rgbA[i * 3 + 1] = d ? 0 : rgbB[i * 3 + 1] / 4;
                            rgbA[i * 3 + 2] = d ? 255 : rgbB[i * 3 + 2] / 4;
                        }
                        snprintf(path, sizeof(path), "%s/%05d_diff.png", out, marker);
                        pngWrite(path, VRAM_W, VRAM_H, rgbA);
                    }
                }
            }
            if (!started || resync) {
                /* Start from (or return to) Redux's VRAM, without touching
                 * the drawing state the trace has set up. */
                uint32_t e6 = (uint32_t)gp0SetMask() | (uint32_t)gp0CheckMask() << 1;
                gp0SetMaskBits(0);
                gpuLoad(0, 0, VRAM_W, VRAM_H, ref);
                gp0SetMaskBits(e6);
                started = 1;
            }
            free(ref);
        } else if (kind == 'O' || kind == 'P') {
            size_t n = parseWords(rest);
            gp0Write(words, (int)n);
        } else if (kind == 'E') {
            DRAWENV env;
            char* p = parseDrawEnv(rest, &env);
            PutDrawEnv(&env);
            p = strchr(p, '|');
            if (p != NULL) {
                size_t n = parseWords(p + 1);
                gp0Write(words, (int)n);
            }
        } else if (kind == 'L') {
            int x, y, w, h, used = 0;
            sscanf(rest, "%d %d %d %d%n", &x, &y, &w, &h, &used);
            size_t n = parseWords(rest + used);
            RECT r = {(s16)x, (s16)y, (s16)w, (s16)h};
            if (n > 0) {
                LoadImage(&r, words);
            }
        } else if (kind == 'M') {
            int x, y, w, h, dx, dy;
            sscanf(rest, "%d %d %d %d %d %d", &x, &y, &w, &h, &dx, &dy);
            RECT r = {(s16)x, (s16)y, (s16)w, (s16)h};
            MoveImage(&r, dx, dy);
        } else if (kind == 'C') {
            int x, y, w, h, r_, g, b;
            sscanf(rest, "%d %d %d %d %d %d %d", &x, &y, &w, &h, &r_, &g, &b);
            RECT r = {(s16)x, (s16)y, (s16)w, (s16)h};
            ClearImage(&r, r_, g, b);
        } else if (kind == 'D') {
            DRAWENV env;
            parseDrawEnv(rest, &env);
            PutDrawEnv(&env);
        } else if (kind == 'S') {
            int v[10] = {0};
            sscanf(rest, "%d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                   &v[8], &v[9]);
            DISPENV d;
            memset(&d, 0, sizeof(d));
            d.disp.x = (s16)v[0];
            d.disp.y = (s16)v[1];
            d.disp.w = (s16)v[2];
            d.disp.h = (s16)v[3];
            d.screen.x = (s16)v[4];
            d.screen.y = (s16)v[5];
            d.screen.w = (s16)v[6];
            d.screen.h = (s16)v[7];
            d.isinter = (u8)v[8];
            d.isrgb24 = (u8)v[9];
            PutDispEnv(&d);
        } else if (kind == 'K') {
            SetDispMask(atoi(rest));
        } else if (kind == 'R') {
            ResetGraph(atoi(rest));
        }
    }
    free(line);
    fclose(f);
    printf("%d VRAM snapshots compared, %d differ, %ld pixels in all\n", snapshots, bad, total);
    printf("%d screens compared, %d differ\n", shotsCompared, shotsBad);
    gpuShutdown();
    SDL_Quit();
    return bad || shotsBad ? 1 : 0;
}
