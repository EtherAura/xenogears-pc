/*
 * The MDEC decoder (port/src/mdec/mdec.c) against measured outputs.
 *
 *   mdectest                                   synthetic cases
 *   mdectest US1.cue US2.cue JP1.cue JP2.cue   and eight retail movie frames
 *
 * Unless a comment says psx-spx, the expected values were measured with the
 * Beetle PSX libretro core as a black box: a test program fed it these very
 * inputs (work/clean-room/measure/mdec, cases.py suites "explicit", "unit"
 * and "retail") and its DMA1 output was read back from RAM. The retail
 * frames come from the player's discs (bin/, never committed); without them
 * that part is skipped.
 */

#include "common.h"
#include "mdec/mdec.h"
#include "mdec/vlc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

#include "test/check.h"

/* psx-spx "MDEC Commands" set_scale_table: the standard matrix. */
static const uint16_t stdScale[64] = {
    0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82,
    0x7D8A, 0x6A6D, 0x471C, 0x18F8, 0xE707, 0xB8E3, 0x9592, 0x8275,
    0x7641, 0x30FB, 0xCF04, 0x89BE, 0x89BE, 0xCF04, 0x30FB, 0x7641,
    0x6A6D, 0xE707, 0x8275, 0xB8E3, 0x471C, 0x7D8A, 0x18F8, 0x9592,
    0x5A82, 0xA57D, 0xA57D, 0x5A82, 0x5A82, 0xA57D, 0xA57D, 0x5A82,
    0x471C, 0x8275, 0x18F8, 0x6A6D, 0x9592, 0xE707, 0x7D8A, 0xB8E3,
    0x30FB, 0x89BE, 0x7641, 0xCF04, 0xCF04, 0x7641, 0x89BE, 0x30FB,
    0x18F8, 0xB8E3, 0x6A6D, 0x8275, 0x7D8A, 0x9592, 0x471C, 0xE707,
};

/* LIBPRESS's quant table (both halves of its MDEC(2) upload, libpress.c). */
static const uint8_t libpressIq[64] = {
    2,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83,
};

/* psx-spx zigzag[]: run-length index of each position (row-major). */
static const uint8_t zigzag[64] = {
    0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42,
    3,  8,  12, 17, 25, 30, 41, 43, 9,  11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63,
};

/* ---- building input ---- */

static uint16_t halves[1 << 18];
static int nHalves;

static void put(uint16_t h) {
    halves[nHalves++] = h;
}

/* A block: q_scale, DC and terms at run-length indices (ascending, k >= 1),
 * then the FE00h end code (psx-spx "MDEC Data Format"). */
static void block(int q, int dc, const int* terms, int n) {
    int k = 0;
    put((uint16_t)(q << 10 | (dc & 0x3FF)));
    for (int i = 0; i < n; i++) {
        put((uint16_t)((terms[2 * i] - k - 1) << 10 | (terms[2 * i + 1] & 0x3FF)));
        k = terms[2 * i];
    }
    put(0xFE00);
}

static void flatBlock(int q, int dc) {
    block(q, dc, NULL, 0);
}

/* One term at a natural position (row * 8 + column). */
static void natBlock(int q, int dc, int pos, int value, int pos2, int value2) {
    int t[4];
    int n = 0;
    int k1 = pos < 0 ? 99 : zigzag[pos];
    int k2 = pos2 < 0 ? 99 : zigzag[pos2];
    if (k2 < k1) {
        int s = k1;
        k1 = k2;
        k2 = s;
        s = value;
        value = value2;
        value2 = s;
    }
    if (k1 != 99) {
        t[n++] = k1;
        t[n++] = value;
    }
    if (k2 != 99) {
        t[n++] = k2;
        t[n++] = value2;
    }
    block(q, dc, t, n / 2);
}

static void colourFlat(int q, int cr, int cb, int y) {
    flatBlock(q, cr);
    flatBlock(q, cb);
    for (int i = 0; i < 4; i++) {
        flatBlock(q, y);
    }
}

/* ---- running the decoder ---- */

static uint32_t words[1 << 17];
static uint8_t out[1 << 20];

static void tables(const uint8_t* iqY, const uint8_t* iqC, const uint16_t* scale) {
    uint8_t iq[128];
    memcpy(iq, iqY, 64);
    memcpy(iq + 64, iqC, 64);
    memcpy(words, iq, 128);
    mdecCommand(0x40000001);
    mdecFeed(words, 32);
    memcpy(words, scale, 128);
    mdecCommand(0x60000000);
    mdecFeed(words, 32);
}

static void flatTables(int dcEntry, const uint16_t* scale) {
    uint8_t iq[64];
    memset(iq, 1, sizeof(iq));
    iq[0] = (uint8_t)dcEntry;
    tables(iq, iq, scale);
}

/* Decodes the built halfwords with MDEC(1) as LIBPRESS sends them (padded with
 * FE00h to whole 32-word DMA blocks) and returns the bytes written. */
static uint32_t decode(int depth, int sign, int bit15, uint32_t bytes) {
    while (nHalves % 64) {
        put(0xFE00);
    }
    uint32_t n = (uint32_t)nHalves / 2;
    for (uint32_t i = 0; i < n; i++) {
        words[i] = halves[2 * i] | (uint32_t)halves[2 * i + 1] << 16;
    }
    mdecCommand(0x20000000u | (uint32_t)depth << 27 | (uint32_t)sign << 26 | (uint32_t)bit15 << 25 | n);
    mdecFeed(words, n);
    uint32_t got = 0;
    while (got < bytes) {
        uint32_t m = mdecDecode(out + got);
        if (m == 0) {
            break;
        }
        got += m;
        mdecSkipPadding();
    }
    nHalves = 0;
    return got;
}

static int uniform(const uint8_t* p, int n, int stride) {
    for (int i = stride; i < n; i += stride) {
        if (memcmp(p, p + i, (size_t)stride) != 0) {
            return 0;
        }
    }
    return 1;
}

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

/* ---- explicit cases ---- */

static void testConsoleFigures(void) {
    /* psx-spx/clean-room spec, published console figures: flat levels of
     * 153.00, 153.25, 153.75 and 165.50 come out as 153, 153, 154, 165; Cb
     * planes of +50, -50 and +63 add 89, -89 and 112 to blue. A level L is a
     * DC of 8*(L-128) with a table entry of 1. */
    static const int dc[4] = { 200, 202, 206, 300 };
    static const uint8_t level[4] = { 153, 153, 154, 165 };
    flatTables(1, stdScale);
    for (int i = 0; i < 4; i++) {
        colourFlat(1, 0, 0, dc[i]);
    }
    colourFlat(1, 0, 400, 0);
    colourFlat(1, 0, -400, 0);
    colourFlat(1, 0, 504, 0);
    EXPECT(decode(2, 0, 0, 7 * 768) == 7 * 768);
    for (int i = 0; i < 4; i++) {
        EXPECT(out[i * 768] == level[i] && out[i * 768 + 1] == level[i] && out[i * 768 + 2] == level[i]);
        EXPECT(uniform(out + i * 768, 768, 3));
    }
    /* Blue as published; green measured. */
    EXPECT(out[4 * 768] == 128 && out[4 * 768 + 1] == 111 && out[4 * 768 + 2] == 128 + 89);
    EXPECT(out[5 * 768] == 128 && out[5 * 768 + 1] == 145 && out[5 * 768 + 2] == 128 - 89);
    EXPECT(out[6 * 768] == 128 && out[6 * 768 + 1] == 106 && out[6 * 768 + 2] == 128 + 112);
    for (int i = 4; i < 7; i++) {
        EXPECT(uniform(out + i * 768, 768, 3));
    }
}

static void testTermValues(void) {
    /* A scale table of 4000h (1/4 a pass): every pixel is the sum of the
     * terms / 16. 63 terms of 1 at q_scale 1 are worth -1/2 each, at q_scale
     * 8 +1/2 each. A term of 24 alone at (1,1) gives 1.5 -> 2; 8 and 16 in
     * one column give 1 (the first pass keeps quarters), in one row 2; a DC
     * of -9 gives 0. */
    static uint16_t quarter[64];
    int t[126];
    for (int i = 0; i < 64; i++) {
        quarter[i] = 0x4000;
    }
    flatTables(1, quarter);
    for (int k = 1; k < 64; k++) {
        t[2 * (k - 1)] = k;
        t[2 * (k - 1) + 1] = 1;
    }
    block(1, 0, t, 63);
    block(8, 0, t, 63);
    natBlock(8, 0, 9, 24, -1, 0);
    natBlock(8, 0, 9, 8, 17, 16);
    natBlock(8, 0, 9, 8, 10, 16);
    flatBlock(8, -9);
    EXPECT(decode(1, 1, 0, 6 * 64) == 6 * 64);
    static const int8_t expect[6] = { -2, 2, 2, 1, 2, 0 };
    for (int i = 0; i < 6; i++) {
        EXPECT(uniform(out + i * 64, 64, 1) && (int8_t)out[i * 64] == expect[i]);
    }
}

static void testFormats(void) {
    flatTables(1, stdScale);
    /* 4bpp, unsigned: two pixels a byte, the first in the low nibble. */
    int t[2] = { 1, 100 };
    block(8, 0, t, 1);
    t[0] = 2;
    block(8, 0, t, 1);
    EXPECT(decode(0, 0, 0, 64) == 64);
    static const uint8_t ramp[4] = { 0x99, 0x89, 0x78, 0x77 };
    for (int r = 0; r < 8; r++) {
        EXPECT(memcmp(out + r * 4, ramp, 4) == 0);
    }
    static const uint8_t rows[8] = { 0x99, 0x99, 0x99, 0x88, 0x88, 0x77, 0x77, 0x77 };
    for (int r = 0; r < 8; r++) {
        EXPECT(out[32 + r * 4] == rows[r] && uniform(out + 32 + r * 4, 4, 1));
    }

    /* 15bpp with bit 15: (r, g, b) = (5, 22, 16) and (13, 11, 21). */
    colourFlat(1, -512, 0, 0);
    colourFlat(1, 0, 300, -200);
    EXPECT(decode(3, 0, 1, 1024) == 1024);
    EXPECT(out[0] == 0xC5 && out[1] == 0xC2 && uniform(out, 512, 2));
    EXPECT(out[512] == 0x6D && out[513] == 0xD5 && uniform(out + 512, 512, 2));

    /* A channel past the 9-bit range wraps (published console figure: a
     * channel past 255 reads black): Y and Cr of +126 give R 303 -> black,
     * -126 each give R -303 -> white. */
    flatTables(2, stdScale);
    colourFlat(1, 504, 0, 504);
    colourFlat(1, -504, 0, -504);
    EXPECT(decode(2, 0, 0, 1536) == 1536);
    EXPECT(out[0] == 0 && out[1] == 164 && out[2] == 254 && uniform(out, 768, 3));
    EXPECT(out[768] == 255 && out[769] == 92 && out[770] == 2 && uniform(out + 768, 768, 3));
}

static void testFraming(void) {
    /* psx-spx "MDEC Data Format": a block can end at index 63 without an end
     * code; a run past index 63 ends the block (its value unused); FE00h at a
     * block start is padding. */
    int t[126];
    flatTables(1, stdScale);
    for (int k = 1; k < 64; k++) {
        t[2 * (k - 1)] = k;
        t[2 * (k - 1) + 1] = 1;
    }
    block(8, 10, t, 63);
    nHalves--; /* no end code */
    flatBlock(8, 200);
    put(8 << 10 | 50);
    put(40 << 10 | 7);
    put(40 << 10 | 9);
    put(0xFE00);
    put(0xFE00);
    put(0xFE00);
    flatBlock(8, -100);
    t[0] = 5;
    t[1] = 60;
    block(8, 0, t, 1);
    t[0] = 2;
    t[1] = -20;
    block(8, 30, t, 1);
    EXPECT(decode(1, 1, 0, 384) == 384);
    static const int8_t row0[6][8] = {
        { 4, 0, 2, 1, 2, 1, 1, 1 },     { 25, 25, 25, 25, 25, 25, 25, 25 },
        { 7, 5, 8, 6, 6, 8, 5, 7 },     { -12, -12, -12, -12, -12, -12, -12, -12 },
        { 10, 4, -4, -10, -10, -4, 4, 10 }, { 0, 0, 0, 0, 0, 0, 0, 0 },
    };
    for (int b = 0; b < 6; b++) {
        EXPECT(memcmp(out + b * 64, row0[b], 8) == 0);
    }
    EXPECT((int8_t)out[5 * 64 + 63] == 7);
}

/* ---- random jobs, checked by CRC-32 ---- */

static uint32_t lcg;

static int next(void) {
    lcg = lcg * 1103515245u + 12345u;
    return (int)(lcg >> 16);
}

/* cases.py lcg_block. */
static void randomBlock(void) {
    static const int amps[4] = { 3, 15, 63, 511 };
    int q = 1 + next() % 63;
    int dc = next() % 1024;
    uint16_t first = (uint16_t)(q << 10 | dc);
    if (first == 0xFE00) {
        first = (uint16_t)(62 << 10 | dc);
    }
    int amp = amps[next() % 4];
    int count = next() % 64;
    int k = 0;
    put(first);
    for (int i = 0; i < count; i++) {
        int run = next() % 8;
        if (k + run + 1 > 63) {
            break;
        }
        k += run + 1;
        int v = next() % (2 * amp + 1) - amp;
        put((uint16_t)(run << 10 | (v & 0x3FF)));
    }
    put(0xFE00);
}

static void testRandom(void) {
    static const struct {
        const char* name;
        uint32_t seed;
        int depth, sign, bit15, mbs, flat;
        uint32_t crc;
    } jobs[] = {
        { "mono8s", 1, 1, 1, 0, 256, 0, 0x470B8479u }, { "mono8u", 2, 1, 0, 0, 256, 0, 0x17C689E6u },
        { "mono4s", 3, 0, 1, 0, 256, 0, 0xF78043CBu }, { "mono4u", 4, 0, 0, 0, 256, 0, 0x52AD2720u },
        { "rgb24u", 5, 2, 0, 0, 64, 0, 0x57CC1889u },  { "rgb24s", 6, 2, 1, 0, 64, 0, 0xEB64BBCAu },
        { "rgb15u", 7, 3, 0, 0, 64, 0, 0x4AF6761Cu },  { "rgb15s", 8, 3, 1, 1, 64, 0, 0xEDD761E5u },
        { "flat8s", 9, 1, 1, 0, 256, 1, 0x862A2692u }, { "flat24u", 10, 2, 0, 1, 64, 1, 0xF04DC21Au },
    };
    for (size_t j = 0; j < sizeof(jobs) / sizeof(jobs[0]); j++) {
        if (jobs[j].flat) {
            flatTables(1, stdScale);
        } else {
            tables(libpressIq, libpressIq, stdScale);
        }
        lcg = jobs[j].seed;
        int blocks = jobs[j].mbs * (jobs[j].depth >= 2 ? 6 : 1);
        for (int b = 0; b < blocks; b++) {
            randomBlock();
        }
        static const uint32_t mbBytes[4] = { 32, 64, 768, 512 };
        uint32_t bytes = mbBytes[jobs[j].depth] * (uint32_t)jobs[j].mbs;
        uint32_t got = decode(jobs[j].depth, jobs[j].sign, jobs[j].bit15, bytes);
        uint32_t crc = crc32(out, bytes);
        if (got != bytes || crc != jobs[j].crc) {
            fprintf(stderr, "%s: %u bytes, CRC %08X, expected %08X\n", jobs[j].name, got, crc, jobs[j].crc);
        }
        EXPECT(got == bytes && crc == jobs[j].crc);
    }
}

/* ---- retail frames ---- */

/* The .bin a .cue names (a Redump single-track image). */
static FILE* openImage(const char* cue) {
    char line[1024], path[4096];
    FILE* f = fopen(cue, "r");
    if (f == NULL) {
        return NULL;
    }
    path[0] = 0;
    while (fgets(line, sizeof(line), f)) {
        char* a = strchr(line, '"');
        char* b = a ? strchr(a + 1, '"') : NULL;
        if (strncmp(line, "FILE", 4) == 0 && b != NULL) {
            const char* slash = strrchr(cue, '/');
            int dir = slash ? (int)(slash - cue + 1) : 0;
            snprintf(path, sizeof(path), "%.*s%.*s", dir, cue, (int)(b - a - 1), a + 1);
            break;
        }
    }
    fclose(f);
    return path[0] ? fopen(path, "rb") : NULL;
}

/* A frame of an STR stream from its first sector (psx-spx "CDROM File Video
 * Streaming STR (Sony)": mode 2 form 1 sectors whose data starts 0160h 8001h,
 * a 20h-byte header, then 7E0h bytes of the frame; XA sectors in between). */
static int readFrame(FILE* f, long lba, uint8_t* frame, size_t cap) {
    uint8_t sec[2352];
    int want = -1, got = 0;
    uint32_t number = 0;
    size_t size = 0;
    for (int tries = 0; tries < 400 && got != want; tries++, lba++) {
        if (fseek(f, lba * 2352L, SEEK_SET) != 0 || fread(sec, 1, sizeof(sec), f) != sizeof(sec)) {
            return 0;
        }
        const uint8_t* u = sec + 24;
        if (u[0] != 0x60 || u[1] != 0x01 || u[2] != 0x01 || u[3] != 0x80) {
            continue;
        }
        uint32_t fr = u[8] | u[9] << 8 | u[10] << 16 | (uint32_t)u[11] << 24;
        if (want < 0) {
            want = u[6] | u[7] << 8;
            number = fr;
            size = u[12] | u[13] << 8 | u[14] << 16 | (size_t)u[15] << 24;
        } else if (fr != number) {
            continue;
        }
        if ((size_t)(got + 1) * 0x7E0 > cap) {
            return 0;
        }
        memcpy(frame + got * 0x7E0, u + 0x20, 0x7E0);
        got++;
    }
    return got == want && size > 0;
}

static int testRetail(char** cues) {
    static const struct {
        int disc;
        long lba;
        uint32_t crc24, crc15; /* LIBPRESS mode 3 (24bpp, bit 15) and mode 0 (15bpp) */
        const char* name;
    } frames[] = {
        { 0, 41, 0x0804A96Au, 0xEE11BFEEu, "US disc 1, movie 0, frame 1" },
        { 0, 31241, 0xB80C7557u, 0xD9270B6Eu, "US disc 1, movie 7, frame 531" },
        { 0, 76453, 0x2B5F9F47u, 0x1C22211Du, "US disc 1, movie 13, frame 2233" },
        { 1, 98559, 0x0933E708u, 0x6F7B99CDu, "US disc 2, movie 4, frame 1900" },
        { 1, 160647, 0x3B2703FDu, 0x81A20678u, "US disc 2, movie 9, frame 228" },
        { 2, 19775, 0xC438B39Eu, 0x04F4B839u, "JP disc 1, movie 5, frame 305" },
        { 3, 33651, 0x290349B5u, 0x43C8E6E1u, "JP disc 2, movie 0, frame 3362" },
        { 3, 154195, 0x7C7A5EE7u, 0x7F78131Cu, "JP disc 2, movie 6, frame 3362" },
    };
    static uint8_t frame[0x7E0 * 64];
    static uint32_t rl[0x20000];
    FILE* img[4];
    for (int d = 0; d < 4; d++) {
        img[d] = openImage(cues[d]);
        if (img[d] == NULL) {
            fprintf(stderr, "cannot open the image of %s\n", cues[d]);
            return 0;
        }
    }
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        VlcState vs;
        memset(frame, 0, sizeof(frame));
        EXPECT(readFrame(img[frames[i].disc], frames[i].lba, frame, sizeof(frame)));
        vlcDecode(&vs, 0xFFFFFF, (u32*)frame, rl, vlcBuiltinTable());
        for (int mode = 0; mode < 2; mode++) {
            /* DecDCTin: mode bit 0 clears bit 27 (24bpp), bit 1 sets bit 25. */
            uint32_t cmd = mode == 0 ? (rl[0] & ~0x08000000u) | 0x02000000u : rl[0];
            uint32_t bytes = 280u * (mode == 0 ? 768 : 512);
            uint32_t got = 0;
            tables(libpressIq, libpressIq, stdScale);
            mdecCommand(cmd);
            mdecFeed(rl + 1, cmd & 0xFFFF);
            while (got < bytes) {
                uint32_t m = mdecDecode(out + got);
                if (m == 0) {
                    break;
                }
                got += m;
                mdecSkipPadding();
            }
            uint32_t crc = crc32(out, bytes);
            uint32_t expect = mode == 0 ? frames[i].crc24 : frames[i].crc15;
            if (got != bytes || crc != expect) {
                fprintf(stderr, "%s, %s: %u bytes, CRC %08X, expected %08X\n", frames[i].name,
                        mode == 0 ? "24bpp" : "15bpp", got, crc, expect);
            }
            EXPECT(got == bytes && crc == expect);
        }
    }
    for (int d = 0; d < 4; d++) {
        fclose(img[d]);
    }
    return 1;
}

int main(int argc, char** argv) {
    mdecControl(MDEC_CTL_RESET);
    mdecControl(MDEC_CTL_DMA0 | MDEC_CTL_DMA1);
    testConsoleFigures();
    testTermValues();
    testFormats();
    testFraming();
    testRandom();
    int retail = argc == 5 && testRetail(argv + 1);
    printf("mdectest: %d checks, %d failed%s\n", checks, failures, retail ? " (with retail frames)" : "");
    return failures != 0;
}
