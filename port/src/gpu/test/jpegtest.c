/*
 * The JPEG decoder (jpeg.c) on known pictures: test/jpeg holds our own 45x37
 * test picture encoded by libjpeg-turbo's cjpeg in each kind the decoder
 * takes (baseline 4:4:4, 4:2:2, 4:2:0, 4:1:1, grey, progressive 4:2:0 and
 * 4:4:4, restart markers sequential and progressive, one scan per
 * component), with djpeg's decodes beside them (-nosmooth -dct float: the
 * nearest chroma sample and a float inverse DCT, as ours); every pixel must
 * be within 3 of djpeg's. An arithmetic-coded file is refused. Then damaged
 * data: every fixture cut at every length, bytes changed at random, absurd
 * sizes: the decoder must return (a picture or an error), never crash (an
 * address-sanitizer build makes this a memory check too).
 *
 *   jpegtest                          (JPEGTEST_ROUNDS: random changes a file, 3000)
 *   jpegtest <file.jpg> <file.ppm>... a JPEG against a reference decode, and its time
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/jpeg.h"

static int checks, failures;
#include "test/check.h"

static uint8_t* readFile(const char* name, size_t* n) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/port/src/gpu/test/jpeg/%s", XG_ROOT, name);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size > 0 ? (size_t)size : 1);
    *n = data != NULL ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    return data;
}

/* A binary PPM (P6) or PGM (P5) as RGB. */
static uint8_t* readPnm(const char* name, int* w, int* h) {
    size_t n = 0;
    uint8_t* f = readFile(name, &n);
    if (f == NULL) {
        return NULL;
    }
    int maxv = 0, used = 0;
    char kind = 0;
    if (sscanf((const char*)f, "P%c %d %d %d%n", &kind, w, h, &maxv, &used) != 4 || maxv != 255) {
        free(f);
        return NULL;
    }
    const uint8_t* px = f + used + 1;
    int ch = kind == '6' ? 3 : 1;
    if ((size_t)(used + 1) + (size_t)*w * *h * ch > n) {
        free(f);
        return NULL;
    }
    uint8_t* rgb = malloc((size_t)*w * *h * 3);
    for (int i = 0; i < *w * *h; i++) {
        for (int c = 0; c < 3; c++) {
            rgb[i * 3 + c] = px[i * ch + (ch == 3 ? c : 0)];
        }
    }
    free(f);
    return rgb;
}

static void known(const char* jpg, const char* ref) {
    size_t n = 0;
    uint8_t* data = readFile(jpg, &n);
    int rw = 0, rh = 0, w = 0, h = 0;
    uint8_t* want = readPnm(ref, &rw, &rh);
    char err[256];
    uint8_t* got = data != NULL ? jpegDecode(data, n, &w, &h, 8192, 1u << 26, err, sizeof(err)) : NULL;
    EXPECTF(got != NULL && want != NULL && w == rw && h == rh, "%s: %s", jpg, got ? "size" : err);
    if (got != NULL && want != NULL && w == rw && h == rh) {
        int worst = 0;
        long sum = 0;
        for (int i = 0; i < w * h; i++) {
            for (int c = 0; c < 3; c++) {
                int dd = abs(got[i * 4 + c] - want[i * 3 + c]);
                worst = dd > worst ? dd : worst;
                sum += dd;
            }
            EXPECT(got[i * 4 + 3] == 255);
        }
        EXPECTF(worst <= 3, "%s: off by up to %d from djpeg (mean %.2f)", jpg, worst, (double)sum / (w * h * 3));
        printf("jpegtest: %-14s %dx%d, within %d of djpeg (mean %.3f)\n", jpg, w, h, worst,
               (double)sum / (w * h * 3));
    }
    free(got);
    free(want);
    free(data);
}

static uint32_t rng = 12345;
static uint32_t next(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* Damaged data: decoded or refused, never a crash; any picture has its size. */
static void damaged(const char* jpg) {
    size_t n = 0;
    uint8_t* data = readFile(jpg, &n);
    EXPECT(data != NULL);
    if (data == NULL) {
        return;
    }
    char err[256];
    int w, h, pictures = 0, errors = 0;
    for (size_t cut = 0; cut <= n; cut++) {
        uint8_t* copy = malloc(cut ? cut : 1);
        memcpy(copy, data, cut);
        uint8_t* px = jpegDecode(copy, cut, &w, &h, 8192, 1u << 26, err, sizeof(err));
        pictures += px != NULL;
        errors += px == NULL && err[0] != '\0';
        free(px);
        free(copy);
    }
    uint8_t* copy = malloc(n);
    const char* r = getenv("JPEGTEST_ROUNDS"); /* more, by hand (an address-sanitizer build) */
    int rounds = r != NULL ? atoi(r) : 3000;
    for (int round = 0; round < rounds; round++) {
        memcpy(copy, data, n);
        int changes = 1 + (int)(next() % 8);
        for (int k = 0; k < changes; k++) {
            copy[next() % n] = (uint8_t)next();
        }
        uint8_t* px = jpegDecode(copy, n, &w, &h, 8192, 1u << 26, err, sizeof(err));
        if (px != NULL) {
            EXPECT(w > 0 && h > 0 && w <= 8192 && h <= 8192);
            pictures++;
        } else {
            errors += err[0] != '\0';
        }
        free(px);
    }
    free(copy);
    free(data);
    EXPECT(errors > 0);
    printf("jpegtest: %-14s damaged: %d pictures, %d refused, no crash\n", jpg, pictures, errors);
}

#include <time.h>

int main(int argc, char** argv) {
    if (argc > 2) {
        /* pairs given by hand: decode time, worst difference */
        for (int i = 1; i + 1 < argc; i += 2) {
            FILE* f = fopen(argv[i], "rb");
            if (f == NULL) continue;
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            uint8_t* data = malloc((size_t)size);
            size_t n = fread(data, 1, (size_t)size, f);
            fclose(f);
            char err[256];
            int w = 0, h = 0;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            uint8_t* px = jpegDecode(data, n, &w, &h, 16384, 1u << 28, err, sizeof(err));
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
            int worst = -1;
            double mean = 0;
            FILE* r = fopen(argv[i + 1], "rb");
            int rw = 0, rh = 0, maxv = 0;
            if (px != NULL && r != NULL && fscanf(r, "P6 %d %d %d", &rw, &rh, &maxv) == 3 && rw == w && rh == h) {
                fgetc(r);
                uint8_t* ref = malloc((size_t)w * h * 3);
                if (fread(ref, 1, (size_t)w * h * 3, r) == (size_t)w * h * 3) {
                    worst = 0;
                    for (long k = 0; k < (long)w * h; k++) {
                        for (int c = 0; c < 3; c++) {
                            int dd = abs(px[k * 4 + c] - ref[k * 3 + c]);
                            worst = dd > worst ? dd : worst;
                            mean += dd;
                        }
                    }
                    mean /= (double)w * h * 3;
                }
                free(ref);
            }
            if (r != NULL) fclose(r);
            printf("%s: %s %dx%d %.1f ms, worst %d mean %.4f\n", argv[i], px ? "ok" : err, w, h, ms, worst, mean);
            free(px);
            free(data);
        }
        return 0;
    }
    static const char* const kinds[][2] = {
        {"base444.jpg", "base444.ppm"}, {"base422.jpg", "base422.ppm"},   {"base420.jpg", "base420.ppm"},
        {"base411.jpg", "base411.ppm"}, {"gray.jpg", "gray.pgm"},         {"prog420.jpg", "prog420.ppm"},
        {"prog444.jpg", "prog444.ppm"}, {"rst420.jpg", "rst420.ppm"},     {"progrst.jpg", "progrst.ppm"},
        {"seqscans.jpg", "seqscans.ppm"},
    };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        known(kinds[i][0], kinds[i][1]);
    }
    /* refused, with a reason */
    char err[256];
    int w, h;
    size_t n = 0;
    uint8_t* arith = readFile("arith.jpg", &n);
    EXPECT(arith != NULL && jpegDecode(arith, n, &w, &h, 8192, 1u << 26, err, sizeof(err)) == NULL &&
           strstr(err, "arithmetic") != NULL);
    free(arith);
    EXPECT(jpegDecode((const uint8_t*)"", 0, &w, &h, 8192, 1u << 26, err, sizeof(err)) == NULL && err[0] != '\0');
    EXPECT(jpegDecode((const uint8_t*)"\x89PNG", 4, &w, &h, 8192, 1u << 26, err, sizeof(err)) == NULL);
    /* a frame header claiming 65535 x 65535: refused before anything large is made */
    static const uint8_t huge[] = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 8,    0xFF, 0xFF, 0xFF, 0xFF, 3, 1,
                                   0x11, 0,    2,    0x11, 0,    3,    0x11, 0,    0xFF, 0xD9};
    EXPECT(jpegDecode(huge, sizeof(huge), &w, &h, 8192, 1u << 26, err, sizeof(err)) == NULL &&
           strstr(err, "too large") != NULL);
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        damaged(kinds[i][0]);
    }
    printf("jpegtest: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
