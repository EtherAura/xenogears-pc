/*
 * Movie pack pictures (xg-htn.35.10, moviepack.c): a 24-bit display showing a
 * "movie frame" loaded the way the player loads it, the player's hooks
 * called by hand, and the present pass's picture read back (XG_DUMP_PRESENT
 * into the scratch directory): the pack's picture shows for a frame that has
 * one, frame for frame; the original shows for a frame without one, with an
 * unusable one, once something is drawn over the frame, with the option off;
 * and a shader preset runs on the pack's picture. Uncompressed PNGs
 * (pngfast.c) read back exactly; an AV1 WebM (test/movie/av1.webm: our own,
 * six solid colours encoded by aomenc, keyframes at 0 and 3) is indexed and
 * decoded in and out of order, and shown frame for frame; a 15-bit frame's
 * picture lands in the hi-res copy.
 *
 *   moviepacktest <scratch dir>
 *   moviepacktest <scratch dir> bench <png folder | webm>   reading time a picture
 *   moviepacktest <scratch dir> playbench <pack root> <frames> [prime ms]
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/av1video.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"
#include "gpu/moviepack.h"
#include "gpu/pngfast.h"
#include "gpu/shaderchain.h"
#include "gpu/webm.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);

static int checks, failures;
#include "test/check.h"

static char dumps[1100], frames[3500];
static uint32_t vblanks;

static SDL_EnumerationResult SDLCALL removeDump(void* user, const char* dirname, const char* name) {
    (void)user;
    if (strncmp(name, "present_", 8) == 0) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", dirname, name);
        SDL_RemovePath(path);
    }
    return SDL_ENUM_CONTINUE;
}

/* One vblank presented; the colour at (x, y) of the newest dump. */
static int present(int x, int y, uint8_t rgb[3]) {
    gpuVBlank();
    vblanks++;
    for (uint32_t v = vblanks; v > 0; v--) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/present_%06u.png", dumps, v);
        SDL_Surface* s = SDL_LoadPNG(path);
        if (s == NULL) {
            continue;
        }
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface(s);
        if (c == NULL || x >= c->w || y >= c->h) {
            SDL_DestroySurface(c);
            return 0;
        }
        memcpy(rgb, (const uint8_t*)c->pixels + (size_t)y * c->pitch + (size_t)x * 3, 3);
        SDL_DestroySurface(c);
        return 1;
    }
    return 0;
}

static int near3(const uint8_t a[3], int r, int g, int b) {
    return abs(a[0] - r) <= 2 && abs(a[1] - g) <= 2 && abs(a[2] - b) <= 2;
}

/* Present until (x, y) shows r, g, b (the reader thread is asynchronous):
 * 1 if it did within a few hundred vblanks. */
static int waitFor(int x, int y, int r, int g, int b) {
    uint8_t c[3];
    for (int i = 0; i < 400; i++) {
        if (present(x, y, c) && near3(c, r, g, b)) {
            return 1;
        }
        SDL_Delay(2);
    }
    fprintf(stderr, "moviepacktest: (%d, %d) is %d %d %d, not %d %d %d\n", x, y, c[0], c[1], c[2], r, g, b);
    return 0;
}

/* A w x h picture, left half one colour, right half another, as the pack's PNG of frame n. */
static int packFrame(int n, int w, int h, const uint8_t left[3], const uint8_t right[3]) {
    uint8_t* px = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* c = x < w / 2 ? left : right;
            uint8_t* p = px + ((size_t)y * w + x) * 4;
            p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = 255;
        }
    }
    char path[3600];
    snprintf(path, sizeof(path), "%s/%05d.png", frames, n);
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    int ok = s != NULL && SDL_SavePNG(s, path);
    SDL_DestroySurface(s);
    free(px);
    return ok;
}

/* The player's frame: 320 x 224 24-bit pixels of grey level `level` into
 * VRAM (0, 0), then its hook. */
static uint8_t frameData[224][960];
static void playerFrame(int n, int level) {
    memset(frameData, level, sizeof(frameData));
    RECT r = {0, 0, 480, 224};
    LoadImage(&r, frameData);
    DrawSync(0);
    moviePackFrame(n, 0, 0, 480, 224, 1);
}

/* Reading time a picture. Enumerate all numbered PNGs: gaps in optional
 * packs must not hide later damaged frames. Count successful decodes only. */
typedef struct { int* frame; int count; } BenchFiles;
static SDL_EnumerationResult SDLCALL benchFile(void* user, const char* dir, const char* name) {
    (void)dir;
    if (strlen(name) != 9 || strcmp(name + 5, ".png") != 0) return SDL_ENUM_CONTINUE;
    for (int i = 0; i < 5; i++) if (name[i] < '0' || name[i] > '9') return SDL_ENUM_CONTINUE;
    BenchFiles* f = user;
    int* frames = realloc(f->frame, (size_t)(f->count + 1) * sizeof(*frames));
    if (frames == NULL) return SDL_ENUM_FAILURE;
    f->frame = frames;
    f->frame[f->count++] = atoi(name);
    return SDL_ENUM_CONTINUE;
}
static int benchOrder(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

static int bench(const char* what) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(what, &info)) {
        fprintf(stderr, "moviepacktest: no %s\n", what);
        return 1;
    }
    double total = 0, worst = 0;
    int n = 0, stored = 0, w = 0, h = 0;
    const char* format = info.type == SDL_PATHTYPE_DIRECTORY ? "PNG" : "AV1";
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        BenchFiles files = {0};
        if (!SDL_EnumerateDirectory(what, benchFile, &files) || files.count == 0) {
            fprintf(stderr, "moviepacktest: no readable numbered PNGs in %s\n", what);
            free(files.frame);
            return 1;
        }
        qsort(files.frame, (size_t)files.count, sizeof(*files.frame), benchOrder);
        for (int i = 0; i < files.count; i++) {
            char path[3500];
            if (snprintf(path, sizeof(path), "%s/%05d.png", what, files.frame[i]) >= (int)sizeof(path)) {
                free(files.frame);
                return 1;
            }
            uint64_t t0 = SDL_GetTicksNS();
            size_t size = 0;
            uint8_t* file = SDL_LoadFile(path, &size);
            int other = 0, fw = 0, fh = 0;
            uint8_t* px = file != NULL ? pngDecodeStored(file, size, &fw, &fh, 4096, 16u << 20, &other) : NULL;
            if (px != NULL) {
                stored++;
            } else if (other) {
                SDL_Surface* source = SDL_LoadPNG_IO(SDL_IOFromConstMem(file, size), true);
                SDL_Surface* rgba = source != NULL ? SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32) : NULL;
                SDL_DestroySurface(source);
                if (rgba != NULL && rgba->w <= 4096 && rgba->h <= 4096 &&
                    (uint64_t)rgba->w * rgba->h <= (16u << 20)) {
                    fw = rgba->w, fh = rgba->h;
                    px = malloc((size_t)fw * fh * 4);
                    if (px != NULL) {
                        /* Match the production reader's owned, packed RGBA copy. */
                        for (int y = 0; y < fh; y++)
                            memcpy(px + (size_t)y * fw * 4, (uint8_t*)rgba->pixels + (size_t)y * rgba->pitch,
                                   (size_t)fw * 4);
                    }
                }
                SDL_DestroySurface(rgba);
            }
            SDL_free(file);
            if (px == NULL || (n > 0 && (fw != w || fh != h))) {
                fprintf(stderr, "moviepacktest: benchmark frame %s failed or changed size (decoded %d)\n", path, n);
                free(px);
                free(files.frame);
                return 1;
            }
            free(px);
            double ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
            w = fw, h = fh;
            total += ms;
            worst = ms > worst ? ms : worst;
            n++;
        }
        free(files.frame);
    } else {
        char err[256];
        MovieVideo* v = movieVideoOpen(what, err, sizeof(err));
        if (v == NULL) {
            fprintf(stderr, "moviepacktest: %s\n", err);
            return 1;
        }
        for (int i = 0; i < movieVideoFrames(v); i++) {
            uint64_t t0 = SDL_GetTicksNS();
            int fw = 0, fh = 0;
            uint8_t* px = movieVideoDecode(v, i, &fw, &fh, err, sizeof(err));
            if (px == NULL || (n > 0 && (fw != w || fh != h))) {
                fprintf(stderr, "moviepacktest: benchmark AV1 frame %d failed or changed size (decoded %d): %s\n",
                        i, n, px ? "size" : err);
                free(px);
                movieVideoClose(v);
                return 1;
            }
            free(px);
            double ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
            w = fw, h = fh;
            total += ms;
            worst = ms > worst ? ms : worst;
            n++;
        }
        movieVideoClose(v);
    }
    if (n == 0) return 1;
    printf("moviepackbench: {\"format\":\"%s\",\"frames\":%d,\"stored\":%d,\"width\":%d,\"height\":%d,"
           "\"mean_ms\":%.4f,\"worst_ms\":%.4f}\n", format, n, stored, w, h, total / n, worst);
    return 0;
}

/* The real reader's 15 Hz schedule, without GPU uploads or MDEC work.
 * A fixed optional prime interval is reported separately; deadlines never
 * wait for the replacement. The player's original would show on a miss. */
static int playbench(const char* root, int count, int primeMs) {
    if (count < 1 || count > 10000 || primeMs < 0 || primeMs > 10000) return 2;
    texturePackSetRoot(root);
    moviePackSetEnabled(1);
    moviePackBegin(1, 5, 1);
    MoviePackStats stats;
    moviePackStats(&stats);
    if (!stats.active) {
        fprintf(stderr, "moviepacktest: no movie pack at %s\n", root);
        moviePackShutdown();
        return 1;
    }
    SDL_Delay((uint32_t)primeMs);
    uint64_t t0 = SDL_GetTicksNS(), maxBytes = 0;
    unsigned maxFrames = 0, maxLoading = 0;
    int current = 0, repeated = 0, fallback = 0;
    int width = 0, height = 0;
    for (int f = 1; f <= count; f++) {
        uint64_t at = t0 + (uint64_t)(f - 1) * 1000000000 / 15;
        uint64_t now = SDL_GetTicksNS();
        if (now < at) SDL_DelayNS(at - now);
        /* No change of VRAM is needed: the hook marks its current contents. */
        moviePackFrame(f, 0, 0, 480, 224, 1);
        MoviePicture p;
        if (moviePackPicture(0, 0, 320, 224, &p)) {
            current += (uint32_t)p.id == (uint32_t)f;
            repeated += (uint32_t)p.id != (uint32_t)f;
            width = p.w, height = p.h;
        } else {
            fallback++;
        }
        moviePackStats(&stats);
        if (stats.cacheBytes > maxBytes) maxBytes = stats.cacheBytes;
        if (stats.cacheFrames > maxFrames) maxFrames = stats.cacheFrames;
        if (stats.loadingFrames > maxLoading) maxLoading = stats.loadingFrames;
    }
    moviePackStats(&stats);
    printf("moviepackplay: {\"frames\":%d,\"current\":%d,\"repeated\":%d,\"fallback\":%d,"
           "\"prime_ms\":%d,\"width\":%d,\"height\":%d,\"mean_ms\":%.4f,\"worst_ms\":%.4f,"
           "\"late\":%llu,\"invalid\":%llu,\"peak_observed_rgba_bytes\":%llu,"
           "\"peak_observed_cached_frames\":%u,\"peak_observed_loading_frames\":%u}\n",
           count, current, repeated, fallback, primeMs, width, height, stats.decodeMs, stats.decodeMsMax,
           (unsigned long long)stats.late, (unsigned long long)stats.invalid,
           (unsigned long long)maxBytes, maxFrames, maxLoading);
    moviePackShutdown();
    return current + repeated + fallback == count && stats.invalid == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: moviepacktest <scratch dir>\n");
        return 2;
    }
    testSetEnv("XG_SETTINGS", "none");
    if (argc > 3 && strcmp(argv[2], "bench") == 0) return bench(argv[3]);
    if (argc > 4 && strcmp(argv[2], "playbench") == 0)
        return playbench(argv[3], atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 0);
    char root[1100];
    snprintf(dumps, sizeof(dumps), "%s/present", argv[1]);
    snprintf(root, sizeof(root), "%s/pack", argv[1]);
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("XG_DUMP_PRESENT", dumps);
    SDL_CreateDirectory(dumps);
    SDL_EnumerateDirectory(dumps, removeDump, NULL);
    snprintf(frames, sizeof(frames), "%s/%s/movies/d1s05", root, texturePackVersion());
    SDL_RemovePath(frames); /* only when empty: the PNGs are written again below */
    SDL_CreateDirectory(frames);
    static const uint8_t red[3] = {200, 40, 10}, green[3] = {10, 200, 40}, blue[3] = {40, 10, 200};
    EXPECT(packFrame(1, 640, 448, red, red));
    EXPECT(packFrame(2, 640, 448, green, blue));
    EXPECT(packFrame(4, 320, 224, blue, blue)); /* another size: shown all the same */
    {
        char bad[3700];
        snprintf(bad, sizeof(bad), "%s/00006.png", frames);
        EXPECT(SDL_SaveFile(bad, "\x89PNG\r\n\x1a\nnot really", 18)); /* unusable */
    }
    char stale[3600];
    snprintf(stale, sizeof(stale), "%s/00003.png", frames);
    SDL_RemovePath(stale); /* frame 3 has none */

    /* uncompressed PNGs: written stored, read back exactly without inflating;
     * a compressed one is left to the general reader */
    {
        static uint8_t rgb[37 * 23 * 3];
        for (size_t i = 0; i < sizeof(rgb); i++) {
            rgb[i] = (uint8_t)(i * 7 + i / 5);
        }
        char path[1200];
        snprintf(path, sizeof(path), "%s/stored.png", argv[1]);
        EXPECT(pngWriteStored(path, 37, 23, 3, rgb, 37 * 3));
        int w = 0, h = 0, other = 0;
        uint8_t* px = pngReadStored(path, &w, &h, 4096, 1u << 24, &other);
        int same = px != NULL && w == 37 && h == 23;
        for (int i = 0; same && i < 37 * 23; i++) {
            same = memcmp(px + i * 4, rgb + i * 3, 3) == 0 && px[i * 4 + 3] == 255;
        }
        EXPECT(same);
        free(px);
        SDL_Surface* sdl = SDL_LoadPNG(path); /* a valid PNG for others too */
        EXPECT(sdl != NULL && sdl->w == 37);
        snprintf(path, sizeof(path), "%s/compressed.png", argv[1]);
        EXPECT(sdl != NULL && SDL_SavePNG(sdl, path));
        SDL_DestroySurface(sdl);
        px = pngReadStored(path, &w, &h, 4096, 1u << 24, &other);
        EXPECT(px == NULL && other == 1);
    }
    /* the AV1 WebM: its index, its pictures */
    char webmPath[1200];
    snprintf(webmPath, sizeof(webmPath), "%s/port/src/gpu/test/movie/av1.webm", XG_ROOT);
    {
        Webm wm;
        char err[256];
        EXPECT(webmOpen(&wm, webmPath, err, sizeof(err)));
        EXPECT(strcmp(wm.codec, "V_AV1") == 0 && wm.width == 64 && wm.height == 48 && wm.frames == 6);
        EXPECT(wm.frames == 6 && wm.frame[0].key && !wm.frame[1].key && wm.frame[3].key && wm.frame[3].timeNs == 200000000);
        if (movieVideoAvailable()) {
            size_t size = 0;
            uint8_t* bad = SDL_LoadFile(webmPath, &size);
            char path[1200];
            snprintf(path, sizeof(path), "%s/bad-av1.webm", argv[1]);
            EXPECT(bad != NULL);
            if (bad != NULL) {
                for (int i = 0; i < wm.frames; i++) {
                    if (wm.frame[i].offset + wm.frame[i].size <= size)
                        memset(bad + wm.frame[i].offset, 0xFF, wm.frame[i].size);
                }
                EXPECT(SDL_SaveFile(path, bad, size));
                EXPECT(bench(path) != 0); /* valid container, failed AV1 decode */
                SDL_free(bad);
            }
        }
        webmClose(&wm);
        if (movieVideoAvailable()) {
            static const int colours[6][3] = {{200, 40, 10}, {10, 200, 40}, {40, 10, 200},
                                              {240, 240, 240}, {128, 64, 32}, {16, 128, 200}};
            MovieVideo* v = movieVideoOpen(webmPath, err, sizeof(err));
            EXPECT(v != NULL);
            static const int order[9] = {0, 1, 2, 3, 4, 5, 4, 1, 5}; /* in order, then back and forth */
            for (int k = 0; v != NULL && k < 9; k++) {
                int w = 0, h = 0, i = order[k];
                uint8_t* px = movieVideoDecode(v, i, &w, &h, err, sizeof(err));
                int ok = px != NULL && w == 64 && h == 48;
                for (int c = 0; ok && c < 3; c++) {
                    ok = abs(px[(24 * 64 + 32) * 4 + c] - colours[i][c]) <= 12;
                }
                if (!ok) fprintf(stderr, "moviepacktest: AV1 frame %d: %s\n", i, px ? "colour" : err);
                EXPECT(ok);
                free(px);
            }
            EXPECT(v != NULL && movieVideoFrameAt(v, 67000000) == 1 && movieVideoFrameAt(v, 0) == 0);
            movieVideoClose(v);
        }
    }

    {
        char dir[1200], path[1300];
        snprintf(dir, sizeof(dir), "%s/bench-empty", argv[1]);
        SDL_CreateDirectory(dir);
        EXPECT(bench(dir) != 0);
        snprintf(dir, sizeof(dir), "%s/bench-invalid", argv[1]);
        SDL_CreateDirectory(dir);
        snprintf(path, sizeof(path), "%s/00003.png", dir);
        EXPECT(SDL_SaveFile(path, "\x89PNG\r\n\x1a\nnot really", 18));
        EXPECT(bench(dir) != 0); /* a numbered file after a gap is still checked */
    }

    if (!SDL_Init(SDL_INIT_VIDEO) || !gpuInit(NULL)) {
        fprintf(stderr, "moviepacktest: no GPU\n");
        return 1;
    }
    texturePackSetRoot(root);
    ResetGraph(0);
    SetDispMask(1);
    DISPENV env;
    SetDefDispEnv(&env, 0, 0, 320, 224);
    env.isrgb24 = 1;
    PutDispEnv(&env);
    /* the window's middle, its left and right quarters: inside the picture */
    const int mx = 480, my = 360, lx = 240, rx = 720;
    uint8_t c[3];

    /* frame 1: the pack's picture, not the grey frame */
    moviePackBegin(1, 5, 1);
    playerFrame(1, 100);
    EXPECT(waitFor(mx, my, 200, 40, 10));
    MoviePicture picture;
    EXPECT(moviePackPicture(0, 0, 320, 224, &picture) && picture.w == 640 && picture.h == 448);
    /* frame 2: its picture, both halves where they belong */
    playerFrame(2, 110);
    EXPECT(waitFor(lx, my, 10, 200, 40));
    EXPECT(present(rx, my, c) && near3(c, 40, 10, 200));
    /* drawn over (a fill in the frame's corner): the original again */
    RECT corner = {0, 0, 48, 16};
    ClearImage(&corner, 255, 255, 255);
    DrawSync(0);
    EXPECT(present(mx, my, c) && near3(c, 110, 110, 110));
    /* frame 3 has no picture: the original */
    playerFrame(3, 120);
    for (int i = 0; i < 20; i++) {
        present(mx, my, c);
        SDL_Delay(2);
    }
    EXPECT(near3(c, 120, 120, 120));
    /* frame 4's different-size picture still covers the whole movie frame */
    playerFrame(4, 130);
    EXPECT(waitFor(mx, my, 40, 10, 200));
    EXPECT(moviePackPicture(0, 0, 320, 224, &picture) && picture.w == 320 && picture.h == 224);
    /* frame 6 is corrupt: fall back to the player's original picture */
    playerFrame(6, 135);
    EXPECT(waitFor(mx, my, 135, 135, 135));
    /* a loop of the same stream: frame 1's picture again */
    moviePackBegin(1, 5, 1);
    playerFrame(1, 100);
    EXPECT(waitFor(mx, my, 200, 40, 10));
    /* a shader preset over the pack's picture (invert) */
    if (shaderChainAvailable()) {
        char invert[1200];
        snprintf(invert, sizeof(invert), "%s/port/src/gpu/test/slang/invert.slang", XG_ROOT);
        EXPECT(shaderChainSetPath(invert));
        EXPECT(waitFor(mx, my, 55, 215, 245));
        /* the movies' chain (present_movie_shader): none, though the
         * picture has a preset; their own with the picture's off; the
         * picture's again */
        shaderChainEdit(1);
        shaderChainSetPath("none");
        EXPECT(shaderChainMovieMode() == 1);
        EXPECT(waitFor(mx, my, 200, 40, 10));
        EXPECT(shaderChainSetPath(invert) && shaderChainMovieMode() == 2);
        shaderChainEdit(0);
        shaderChainSetPath("");
        EXPECT(waitFor(mx, my, 55, 215, 245));
        shaderChainEdit(1);
        shaderChainSetPath("same");
        EXPECT(shaderChainMovieMode() == 0);
        EXPECT(waitFor(mx, my, 200, 40, 10));
        shaderChainEdit(0);
    }
    MoviePackStats st;
    moviePackStats(&st);
    EXPECT(st.active && st.disc == 1 && st.stream == 5);
    /* Stats describe the last decoded picture, which read-ahead can finish
     * in either order. Displayed dimensions were checked on each frame. */
    EXPECT((st.w == 640 && st.h == 448) || (st.w == 320 && st.h == 224));
    EXPECT(st.shown > 0 && st.invalid >= 1);
    EXPECT(st.cacheFrames == 3 && st.loadingFrames <= 2);
    EXPECT(st.cacheBytes == (2u * 640 * 448 + 320 * 224) * 4);
    /* off: the original, the pack's pictures not read */
    moviePackEnd();
    moviePackSetEnabled(0);
    moviePackBegin(1, 5, 1);
    playerFrame(1, 140);
    for (int i = 0; i < 20; i++) {
        present(mx, my, c);
        SDL_Delay(2);
    }
    EXPECT(near3(c, 140, 140, 140));
    moviePackStats(&st);
    EXPECT(!st.active);
    moviePackEnd();
    /* a 15-bit movie (the field's backgrounds): no pictures read for it */
    moviePackSetEnabled(1);

    /* an AV1 video for stream 6 (frames 1-6 the six colours), frame for frame */
    if (movieVideoAvailable()) {
        char dst[3600];
        snprintf(dst, sizeof(dst), "%s/%s/movies/d1s06.webm", root, texturePackVersion());
        size_t n = 0;
        void* data = SDL_LoadFile(webmPath, &n);
        EXPECT(data != NULL && SDL_SaveFile(dst, data, n));
        SDL_free(data);
        moviePackBegin(1, 6, 1);
        playerFrame(1, 100);
        EXPECT(waitFor(mx, my, 200, 40, 10) || waitFor(mx, my, 199, 40, 10));
        playerFrame(2, 100);
        EXPECT(waitFor(mx, my, 10, 200, 40) || waitFor(mx, my, 11, 199, 41));
        playerFrame(5, 100); /* a jump: from the keyframe at 3 */
        EXPECT(waitFor(mx, my, 128, 64, 32) || waitFor(mx, my, 127, 64, 32));
        moviePackStats(&st);
        EXPECT(strstr(st.format, "AV1") != NULL && st.w == 64);
        moviePackEnd();
    }

    /* uncompressed PNGs for stream 8: shown, counted as such */
    {
        snprintf(frames, sizeof(frames), "%s/%s/movies/d1s08", root, texturePackVersion());
        SDL_CreateDirectory(frames);
        static uint8_t px[640 * 448 * 3];
        for (size_t i = 0; i < sizeof(px); i += 3) {
            px[i] = 30, px[i + 1] = 90, px[i + 2] = 150;
        }
        char path[3700];
        snprintf(path, sizeof(path), "%s/00001.png", frames);
        EXPECT(pngWriteStored(path, 640, 448, 3, px, 640 * 3));
        moviePackBegin(1, 8, 1);
        playerFrame(1, 100);
        EXPECT(waitFor(mx, my, 30, 90, 150));
        moviePackStats(&st);
        EXPECT(st.storedPngs >= 1 && strstr(st.format, "uncompressed") != NULL);
        moviePackEnd();
    }

    /* a 15-bit movie frame (the field's background, loaded slice by slice
     * into one buffer): each slice's part of the picture goes into the
     * hi-res copy right after the slice's upload */
    hiresSetUpscale(2);
    hiresSetPrecision(0);
    hiresSetSeamGuard(0);
    hiresSetColor(HIRES_COLOR_RAW);
    if (gvkHiConfigure(2, HIRES_COLOR_RAW) == 2) {
        moviePackBegin(1, 5, 2); /* frame 2: 640x448, left half (10, 200, 40), right (40, 10, 200) */
        static uint16_t grey[224][160];
        for (int i = 0; i < 224 * 160; i++) {
            ((uint16_t*)grey)[i] = 12 | 12 << 5 | 12 << 10;
        }
        for (int tries = 0; tries < 200; tries++) {
            for (int k = 0; k < 2; k++) { /* two slices of 160 columns, as the player loads them */
                RECT r = {(short)(320 + 160 * k), 0, 160, 224};
                LoadImage(&r, grey);
                moviePackSlice(2, r.x, 0, r.x + 160, 224, 320, 0, 640, 224, 0);
            }
            DrawSync(0);
            moviePackStats(&st);
            if (st.shown > 0) {
                break;
            }
            SDL_Delay(5);
        }
        moviePackFrame(2, 320, 0, 640, 224, 0);
        uint32_t left = 0, right = 0;
        EXPECT(gvkHiRead(0, &left, 2 * (320 + 40), 2 * 100, 1, 1));
        EXPECT(gvkHiRead(0, &right, 2 * (320 + 280), 2 * 100, 1, 1));
        EXPECTF((left & 0x1FFFFFF) == (10u | 200u << 8 | 40u << 16), "%08x", left);
        EXPECTF((right & 0x1FFFFFF) == (40u | 10u << 8 | 200u << 16), "%08x", right);
        moviePackEnd();
    } else {
        printf("moviepacktest: no hi-res copy here, its 15-bit check skipped\n");
    }

    printf("moviepacktest: %d checks, %d failed\n", checks, failures);
    gpuShutdown();
    SDL_Quit();
    return failures != 0;
}
