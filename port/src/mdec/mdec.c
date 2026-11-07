/*
 * The MDEC's decoder (mdec.h): commands, status, table uploads, the parameter
 * feed, and the decoding itself: run-length codes to coefficients, the IDCT,
 * and the colour or monochrome output. No timing here (libpress.c).
 *
 * The commands and status bits, the table uploads, the run-length format
 * (FE00h padding and end codes, the DC term, runs), the block order of a
 * macroblock (Cr, Cb, Y1-Y4, or one Y in monochrome), the colour equations
 * and the output formats follow psx-spx "Macroblock Decoder (MDEC)" ("MDEC
 * Commands", "MDEC Decompression", "MDEC Data Format").
 *
 * psx-spx leaves the arithmetic open ("or so?"). The fixed point here was
 * measured with the Beetle PSX libretro core as a black box: a test program
 * (work/clean-room/measure/mdec, probe.c and cases.py) fed it synthetic
 * blocks (single coefficients and pairs at every position, flat planes,
 * scale tables of our own, random blocks, every output depth, signed and
 * unsigned) and 162 retail movie frames (US and JP, both discs, 24 and 15
 * bpp), and this decoder reproduces every byte it wrote. The published
 * console figures quoted in the clean-room spec (flat DC levels 153.00,
 * 153.25, 153.75 and 165.50 giving 153, 153, 154 and 165; Cb planes of +50,
 * -50 and +63 adding 89, -89 and 112 to blue; overruns wrapping at 9 bits)
 * come out the same. PCSX-Redux, run the same way, agrees with those figures
 * but differs from this decoder on retail frames by up to 17 levels a
 * channel in 24bpp (1 in 15bpp) and sends no 4 or 8bpp output by DMA1, so it
 * is not the reference.
 *
 * One deliberate departure from the measured core: with q_scale 0 the terms
 * are doubled and stored in natural order, the DC included, as psx-spx
 * describes (the core uses zigzag order and leaves the DC alone). No movie
 * of this game uses q_scale 0 (its frames have scales 1 to 6).
 */

#include "mdec/mdec.h"
#include "../system/savestate.h"

#include <string.h>

enum { CMD_NONE, CMD_DECODE, CMD_IQ, CMD_SCALE, CMD_NOFUNC };

/* psx-spx's zigzag[]: for each position (row-major) its index in the
 * run-length order. Inverted on first use. */
static const uint8_t zigzag[64] = {
    0,  1,  5,  6,  14, 15, 27, 28,
    2,  4,  7,  13, 16, 26, 29, 42,
    3,  8,  12, 17, 25, 30, 41, 43,
    9,  11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63,
};
static uint8_t zagzig[64]; /* run-length index -> position */
static int zagzigReady;

static uint32_t control;
static uint32_t command;
static int kind;
static uint32_t paramsLeft;  /* words the command still expects */
static uint16_t counter;     /* status bits 15-0 */
static const uint32_t* feed; /* fed words not read yet */
static uint32_t feedLeft;
static uint32_t word;        /* the word being read */
static int highNext;         /* its high halfword is still to come */

static uint8_t iq[2][64];    /* [0] Y1-Y4, [1] Cr and Cb */
static int16_t scaleTable[64];
static uint32_t tableIndex;

static int depth;            /* 0 = 4bpp, 1 = 8bpp, 2 = 24bpp, 3 = 15bpp */
static int colour;
static int coefIndex;        /* 0: the next halfword starts a block; else one past the last index */
static int blockNo;          /* colour: 0 Cr, 1 Cb, 2-5 Y1-Y4 */
static int qscale;           /* the block's q_scale (its first halfword's bits 15-10) */
static int32_t coef[64];     /* the block's coefficients, natural order, 1/16 units */
static int8_t planes[6][64]; /* the macroblock's finished blocks, in block order */

static void makeZagzig(void) {
    for (int i = 0; i < 64; i++) {
        zagzig[zigzag[i]] = (uint8_t)i;
    }
    zagzigReady = 1;
}

static int32_t signed10(uint16_t v) {
    return (int32_t)(v & 0x3FF) - ((v & 0x200) << 1);
}

/* A term's value: raw is the 10-bit value times its quant table entry (times
 * the q_scale for AC terms, times 8 for the DC). psx-spx "rl_decode_block"
 * gives (raw+4)/8 saturated to signed 11 bits; measured (Beetle as a black
 * box, cases.py suites dequant2, count, count2, pairs, grid, mrand), the
 * value keeps a fraction: raw/8 rounded down, minus a half towards zero, in
 * 1/16 units, saturated to -1024 .. +1023 15/16. */
static int32_t termValue(int32_t raw) {
    int32_t c;

    if (raw == 0) {
        return 0;
    }
    c = (raw >> 3) * 16 + (raw > 0 ? -8 : 8);
    return c < -16384 ? -16384 : (c > 16383 ? 16383 : c);
}

/* Wraps at 9 bits, then saturates to 8 (psx-spx "y_to_mono"). */
static int clamp8(int32_t v) {
    v = ((v & 0x1FF) ^ 0x100) - 0x100;
    return v < -128 ? -128 : (v > 127 ? 127 : v);
}

/* The two IDCT passes over the scale table from MDEC(3) (psx-spx
 * "real_idct_core": first down each column, then along each row), with the
 * table's upper 13 bits only (psx-spx "set_scale_table"). Measured (Beetle as
 * a black box, suites impulse, dc, pass1, scaletab, flat, place): the first
 * pass keeps 2 fractional bits and the second none, each rounded half up.
 * The result wraps at 9 bits and saturates to 8 (psx-spx "y_to_mono"; the
 * colour planes too, measured with suite crand). Shifts of negative values
 * are arithmetic, as on every compiler the port supports. */
static void idct(int8_t* out) {
    int32_t t[64];

    for (int col = 0; col < 8; col++) {
        for (int x = 0; x < 8; x++) {
            int32_t sum = 0;
            for (int z = 0; z < 8; z++) {
                sum += coef[z * 8 + col] * (scaleTable[z * 8 + x] >> 3);
            }
            t[col * 8 + x] = (sum + 0x4000) >> 15;
        }
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int32_t sum = 0;
            for (int z = 0; z < 8; z++) {
                sum += t[z * 8 + y] * (scaleTable[z * 8 + x] >> 3);
            }
            out[y * 8 + x] = (int8_t)clamp8((sum + 0x4000) >> 15);
        }
    }
}

/* The quant table of the current block: [1] for Cr and Cb, else [0]. */
static const uint8_t* blockTable(void) {
    return iq[colour && blockNo < 2 ? 1 : 0];
}

static void startBlock(uint16_t v) {
    memset(coef, 0, sizeof(coef));
    qscale = v >> 10;
    if (qscale == 0) {
        coef[0] = signed10(v) * 32; /* psx-spx: doubled, no table */
    } else {
        coef[0] = termValue(signed10(v) * blockTable()[0] * 8);
    }
}

static void putTerm(int k, uint16_t v) {
    if (qscale == 0) {
        coef[k] = signed10(v) * 32; /* psx-spx: doubled, natural order */
    } else {
        coef[zagzig[k]] = termValue(signed10(v) * blockTable()[k] * qscale);
    }
}

/* The block is complete: transform it. Returns 1 when that completes the
 * macroblock. */
static int endBlock(void) {
    coefIndex = 0;
    idct(planes[blockNo]);
    if (colour && ++blockNo < 6) {
        return 0;
    }
    blockNo = 0;
    return 1;
}

/* One halfword of run-length data (psx-spx "MDEC Data Format",
 * "rl_decode_block"): FE00h where a block would start is padding; a block's
 * first halfword holds its q_scale and DC term, and each later one either
 * ends the block (FE00h) or moves on by its run plus one, the block ending
 * with the 64th coefficient or when a run passes it. A colour macroblock is
 * six blocks (Cr, Cb, Y1-Y4), a monochrome one a single Y block. Returns 1
 * when a macroblock is done. */
static int putHalf(uint16_t v) {
    int k;

    if (coefIndex == 0) {
        if (v != 0xFE00) {
            startBlock(v);
            coefIndex = 1;
        }
        return 0;
    }
    k = coefIndex + (v >> 10);
    if (v == 0xFE00 || k > 63) {
        return endBlock();
    }
    putTerm(k, v);
    if (k == 63) {
        return endBlock();
    }
    coefIndex = k + 1;
    return 0;
}

/* ---- output (psx-spx "MDEC Decompression", "MDEC Data Format") ---- */

/* An 8-bit channel to 5 bits: measured (Beetle as a black box, suites
 * cflat and crand) as (v+4)>>3 saturated, not a plain truncation. */
static int to5(int v, int sign) {
    v = (v + 4) >> 3;
    v = v < -16 ? -16 : (v > 15 ? 15 : v);
    return sign ? v & 31 : v + 16;
}

/* A pixel to 4 bits, the same way (suite mono4). */
static int to4(int v, int sign) {
    v = (v + 8) >> 4;
    v = v < -8 ? -8 : (v > 7 ? 7 : v);
    return sign ? v & 15 : v + 8;
}

/* psx-spx "yuv_to_rgb": R = Y + 1.402 Cr, G = Y - 0.3437 Cb - 0.7143 Cr,
 * B = Y + 1.772 Cb, each saturated to 8 bits, 80h added if unsigned; each
 * 8x8 Cr/Cb sample covers 2x2 pixels, the Y blocks are the four quarters.
 * Measured (Beetle as a black box, suites cflat, crand, layout): the factors
 * are 359/256, 183/256, 88/256 and 454/256, each product is rounded down to
 * 1/8 and the sum rounded half up, and a channel wraps at 9 bits before it
 * saturates (psx-spx: "probably also some 9bit limit"). */
static void emitColour(uint8_t* out) {
    int sign = (command >> 26) & 1;
    uint16_t stp = (command & 0x02000000) ? 0x8000 : 0;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int c = (y >> 1) * 8 + (x >> 1);
            int cr = planes[0][c];
            int cb = planes[1][c];
            int l = planes[2 + (y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)];
            int r = clamp8(l + ((((359 * cr) >> 5) + 4) >> 3));
            int g = clamp8(l + ((((-183 * cr) >> 5) + ((-88 * cb) >> 5) + 4) >> 3));
            int b = clamp8(l + ((((454 * cb) >> 5) + 4) >> 3));
            if (depth == 2) {
                int add = sign ? 0 : 128;
                *out++ = (uint8_t)(r + add);
                *out++ = (uint8_t)(g + add);
                *out++ = (uint8_t)(b + add);
            } else {
                uint16_t px = (uint16_t)(to5(r, sign) | to5(g, sign) << 5 | to5(b, sign) << 10 | stp);
                *out++ = (uint8_t)px;
                *out++ = (uint8_t)(px >> 8);
            }
        }
    }
}

/* psx-spx "y_to_mono": the pixels as they are (80h added if unsigned), one
 * a byte in 8bpp, two a byte in 4bpp, the first in the low nibble
 * (measured, suite mono4). */
static void emitMono(uint8_t* out) {
    int sign = (command >> 26) & 1;
    const int8_t* p = planes[0];

    if (depth == 1) {
        for (int i = 0; i < 64; i++) {
            out[i] = (uint8_t)(sign ? p[i] : p[i] + 128);
        }
        return;
    }
    for (int i = 0; i < 64; i += 2) {
        out[i / 2] = (uint8_t)(to4(p[i], sign) | to4(p[i + 1], sign) << 4);
    }
}

static int nextWord(uint32_t* w) {
    if (paramsLeft == 0 || feedLeft == 0) {
        return 0;
    }
    *w = *feed++;
    feedLeft--;
    paramsLeft--;
    counter--;
    return 1;
}

static void endCommand(void) {
    if (paramsLeft == 0 && !highNext) {
        kind = CMD_NONE;
    }
}

void mdecControl(uint32_t value) {
    if (value & MDEC_CTL_RESET) {
        /* Abort the command; the tables stay (psx-spx). */
        command = 0;
        kind = CMD_NONE;
        paramsLeft = 0;
        counter = 0;
        feed = NULL;
        feedLeft = 0;
        highNext = 0;
        coefIndex = 0;
        blockNo = 0;
        qscale = 0;
        colour = 0;
    }
    control = value & ~MDEC_CTL_RESET;
}

uint32_t mdecControlBits(void) {
    return control;
}

int mdecBusy(void) {
    return kind != CMD_NONE && kind != CMD_NOFUNC;
}

int mdecDecoding(void) {
    return kind == CMD_DECODE;
}

uint32_t mdecFedLeft(void) {
    return feedLeft;
}

uint32_t mdecStatus(void) {
    uint32_t st = MDEC_ST_OUT_EMPTY;
    uint32_t block;

    if (mdecBusy()) {
        st |= MDEC_ST_BUSY;
        if ((control & MDEC_CTL_DMA0) && paramsLeft != 0) {
            st |= MDEC_ST_DREQ0;
        }
    }
    st |= ((command >> 25) & 0xF) << 23;
    /* Block being read: 0-3 Y1-Y4, 4 Cr (or the mono Y), 5 Cb. */
    block = (colour && kind == CMD_DECODE) ? (uint32_t)(blockNo + 4) % 6 : 4;
    st |= block << 16;
    st |= counter;
    return st;
}

void mdecCommand(uint32_t cmd) {
    if (!zagzigReady) {
        makeZagzig();
    }
    command = cmd;
    highNext = 0;
    feed = NULL;
    feedLeft = 0;
    switch (cmd >> 29) {
    case 1:
        kind = CMD_DECODE;
        paramsLeft = cmd & 0xFFFF;
        depth = (cmd >> 27) & 3;
        colour = depth >= 2;
        coefIndex = 0;
        blockNo = 0;
        break;
    case 2:
        kind = CMD_IQ;
        paramsLeft = (cmd & 1) ? 32 : 16;
        tableIndex = 0;
        break;
    case 3:
        kind = CMD_SCALE;
        paramsLeft = 32;
        tableIndex = 0;
        break;
    default:
        /* No function: bits 15-0 show in the status, nothing is expected. */
        kind = CMD_NOFUNC;
        paramsLeft = 0;
        counter = (uint16_t)cmd;
        return;
    }
    counter = (uint16_t)(paramsLeft - 1);
    if (paramsLeft == 0) {
        kind = CMD_NONE;
    }
}

void mdecFeed(const uint32_t* words, uint32_t count) {
    uint32_t w;

    feed = words;
    feedLeft = count;
    if (kind == CMD_IQ) {
        while (nextWord(&w)) {
            for (int i = 0; i < 4; i++, w >>= 8) {
                iq[tableIndex >> 6][tableIndex & 63] = (uint8_t)w;
                tableIndex = (tableIndex + 1) & 127;
            }
        }
        endCommand();
    } else if (kind == CMD_SCALE) {
        while (nextWord(&w)) {
            for (int i = 0; i < 2; i++, w >>= 16) {
                scaleTable[tableIndex] = (int16_t)w;
                tableIndex = (tableIndex + 1) & 63;
            }
        }
        endCommand();
    }
    if (kind != CMD_DECODE) {
        /* Words past a table's end would start a new command; LIBPRESS never
         * sends any. */
        feed = NULL;
        feedLeft = 0;
    }
}

uint32_t mdecMacroblockSize(void) {
    static const uint32_t sizes[4] = { 32, 64, 16 * 16 * 3, 16 * 16 * 2 };
    return sizes[depth];
}

uint32_t mdecDecode(uint8_t* out) {
    if (kind != CMD_DECODE && !highNext) {
        return 0;
    }
    for (;;) {
        uint16_t half;
        if (highNext) {
            highNext = 0;
            half = (uint16_t)(word >> 16);
        } else {
            if (!nextWord(&word)) {
                endCommand();
                return 0;
            }
            highNext = 1;
            half = (uint16_t)word;
        }
        if (putHalf(half)) {
            if (colour) {
                emitColour(out);
            } else {
                emitMono(out);
            }
            endCommand();
            return mdecMacroblockSize();
        }
    }
}

void mdecSkipPadding(void) {
    for (;;) {
        if (coefIndex != 0) {
            return;
        }
        if (highNext) {
            if ((word >> 16) != 0xFE00) {
                return;
            }
            highNext = 0;
            continue;
        }
        if (paramsLeft == 0 || feedLeft == 0 || (feed[0] & 0xFFFF) != 0xFE00) {
            endCommand();
            return;
        }
        nextWord(&word);
        highNext = 1;
    }
}

void mdecGetTables(uint8_t outIq[2][64], int16_t scale[64]) {
    memcpy(outIq, iq, sizeof(iq));
    memcpy(scale, scaleTable, sizeof(scaleTable));
}

/* ---- a world of its own (mdec.h mdecWorldCreate) ---- */

/* What a world has of this file: the decoder's whole state (the zigzag
 * inverse is the same everywhere). */
typedef struct {
    uint32_t control, command;
    int kind;
    uint32_t paramsLeft;
    uint16_t counter;
    const uint32_t* feed;
    uint32_t feedLeft, word;
    int highNext;
    uint8_t iq[2][64];
    int16_t scaleTable[64];
    uint32_t tableIndex;
    int depth, colour;
    int coefIndex, blockNo, qscale;
    int32_t coef[64];
    int8_t planes[6][64];
} DecoderPart;

size_t mdecDecoderPartSize(void) {
    return sizeof(DecoderPart);
}

void mdecDecoderPartInit(void* blob) {
    memset(blob, 0, sizeof(DecoderPart)); /* as at power-on */
}

static void swapField(void* a, void* b, size_t n) {
    unsigned char* x = a;
    unsigned char* y = b;
    for (size_t i = 0; i < n; i++) {
        unsigned char t = x[i];
        x[i] = y[i];
        y[i] = t;
    }
}
#define SWAP(f) swapField(&(f), &(p->f), sizeof(f))

void mdecDecoderPartSwap(void* blob) {
    DecoderPart* p = blob;
    SWAP(control);
    SWAP(command);
    SWAP(kind);
    SWAP(paramsLeft);
    SWAP(counter);
    SWAP(feed);
    SWAP(feedLeft);
    SWAP(word);
    SWAP(highNext);
    SWAP(iq);
    SWAP(scaleTable);
    SWAP(tableIndex);
    SWAP(depth);
    SWAP(colour);
    SWAP(coefIndex);
    SWAP(blockNo);
    SWAP(qscale);
    SWAP(coef);
    SWAP(planes);
}
#undef SWAP

/* ---- save states (system/savestate.h) ---- */

/* A command and a macroblock in progress (feed points into game memory or
 * LIBPRESS's statics), the tables uploaded, the output format. */
void mdecStateIo(XgState* s) {
    XG_STATE_VAR(s, control);
    XG_STATE_VAR(s, command);
    XG_STATE_VAR(s, kind);
    XG_STATE_VAR(s, paramsLeft);
    XG_STATE_VAR(s, counter);
    XG_STATE_VAR(s, feed);
    XG_STATE_VAR(s, feedLeft);
    XG_STATE_VAR(s, word);
    XG_STATE_VAR(s, highNext);
    XG_STATE_VAR(s, iq);
    XG_STATE_VAR(s, scaleTable);
    XG_STATE_VAR(s, tableIndex);
    XG_STATE_VAR(s, depth);
    XG_STATE_VAR(s, colour);
    XG_STATE_VAR(s, coefIndex);
    XG_STATE_VAR(s, blockNo);
    XG_STATE_VAR(s, qscale);
    XG_STATE_VAR(s, coef);
    XG_STATE_VAR(s, planes);
    if (xgStateLoading(s) && !zagzigReady) {
        makeZagzig(); /* a block may be under way: its order table first */
    }
}
