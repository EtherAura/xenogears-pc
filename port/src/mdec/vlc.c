/*
 * LIBPRESS's bitstream decoder (PSY-Q 4.0 VLC.OBJ, VLC_C.OBJ, BUILD.OBJ):
 * DecDCTvlc turns an STR frame (a BS v1/v2/v3 bitstream) into the MDEC's
 * run-length codes.
 *
 * The SDK's decoder is hand-written assembly
 * (asm/us/video_player/nonmatchings/psyq/libpress/vlc*). This is the same
 * algorithm in C: a 32-bit window over the stream, refilled a halfword at a
 * time, and a lookup table that reads the next 13 bits and yields up to three
 * run-length codes at once (codes that start with eight zeros take a second
 * lookup on the 9 bits after them). DecDCTvlc and DecDCTvlc2 behave the same
 * on any input, malformed streams included:
 * - the frame header picks the DC coding: version 3 and up Huffman-codes the
 *   DC as a difference to the previous Cr, Cb or Y block, older versions
 *   store it raw;
 * - the end-of-frame code in place of a DC (1FFh as a raw DC, ten ones in
 *   v3) ends the frame: 65 FE00h halfwords follow;
 * - the size limit (DecDCTvlcSize) is checked after each DC only; when it is
 *   reached the call returns 1 and DecDCTvlc(NULL, NULL) carries on.
 *
 * The SDK keeps the table in its data (DecDCTvlc) or expands a packed copy
 * (DecDCTvlcBuild, for DecDCTvlc2). Here vlcMakeTable computes it from the
 * BS Huffman codes as psx-spx lists them; work/agents/PORT_MOVIES/mdec checks
 * it against the disc's table byte for byte.
 *
 * The PS1 takes its time over it: DecDCTvlc's calls cost the PS1's CPU time
 * (hwBusy), so the movie player starts the MDEC as late in the frame as the
 * PS1 does. The SDK's decoder spends about 40 cycles a table lookup and 5 an
 * output halfword: measured with Beetle PSX as a black box (tools/beetle.py),
 * the movie mode's own timer readings (VSync(1) before and after each
 * updateStrPlayer call, movieUpdateTimes) against the work of the same calls
 * here (US logo movie, 936 calls), and the attract movie's VSync(0) times
 * (7212 vblanks): both give 40 a lookup and 4-5 a halfword, interrupts in
 * the movie (CD sectors, the MDEC's slices) included.
 */

#include "common.h"
#include "video_player/video_player.h"

#include "mdec/vlc.h"
#include "system/hw.h"
#include "../system/savestate.h"

#include <string.h>

/* Every SDK entry point is a safe point; weak so the tests link without it. */
#pragma weak hwSafePoint
#pragma weak hwBusy
static inline void safePoint(void) {
    if (hwSafePoint) {
        hwSafePoint();
    }
}

#define LOOKUP_CYCLES 40.0 /* the PS1's decoder, per AC table lookup (above) */
#define HALF_CYCLES 5.0    /* and per run-length halfword written */

static void cpuTime(const VlcState* s, const u16* from) {
    if (hwBusy) {
        hwBusy((s->lookups * LOOKUP_CYCLES + (double)(s->dst - from) * HALF_CYCLES) / HW_CPU_HZ);
    }
}

/* Table layout, in halfwords (the SDK's, 0x11000 bytes):
 * DC for Y1-Y4 and for Cr/Cb, indexed by the next 8 bits: {length, size};
 * AC, by the next 13 bits: {length | code1 << 16, code2 | code3 << 16} as
 * two little-endian words, all zero when the bits start with eight zeros;
 * AC after those eight zeros, by the next 9 bits: {length | code << 16}. */
#define TAB_DC_Y 0x0000
#define TAB_DC_C 0x0200
#define TAB_AC 0x0400
#define TAB_AC_LONG 0x8400

#define CODE_EOB 0xFE00
#define CODE_ESCAPE 0x7C1F /* run 31, level 31: no Huffman code means it */

/* ---- the Huffman codes (psx-spx "BS Compression AC/DC Values") ---- */

typedef struct {
    uint8_t prefixLen;
    uint16_t prefix;
    uint8_t indexBits;  /* bits after the prefix that pick the value; a sign bit follows */
    uint16_t values[16];
} AcGroup;

static const AcGroup acGroups[] = {
    { 2, 0x3, 0, { 0x0001 } },                                      /* 11s */
    { 3, 0x3, 0, { 0x0401 } },                                      /* 011s */
    { 3, 0x2, 1, { 0x0002, 0x0801 } },                              /* 010xs */
    { 4, 0x3, 1, { 0x1001, 0x0C01 } },                              /* 0011xs */
    { 5, 0x5, 0, { 0x0003 } },                                      /* 00101s */
    { 5, 0x4, 3, { 0x3401, 0x0006, 0x3001, 0x2C01, 0x0C02, 0x0403, 0x0005, 0x2801 } },
    { 4, 0x1, 2, { 0x1C01, 0x1801, 0x0402, 0x1401 } },              /* 0001xxs */
    { 5, 0x1, 2, { 0x0802, 0x2401, 0x0004, 0x2001 } },              /* 00001xxs */
    { 7, 0x1, 3, { 0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3C01, 0x3801, 0x1002 } },
    { 8, 0x1, 4, { 0x000B, 0x2002, 0x1003, 0x000A, 0x0804, 0x1C02, 0x5401, 0x5001,
                   0x0009, 0x4C01, 0x4801, 0x0405, 0x0C03, 0x0008, 0x1802, 0x4401 } },
    { 9, 0x1, 4, { 0x2802, 0x2402, 0x1403, 0x0C04, 0x0805, 0x0407, 0x0406, 0x000F,
                   0x000E, 0x000D, 0x000C, 0x6801, 0x6401, 0x6001, 0x5C01, 0x5801 } },
    { 10, 0x1, 4, { 0x001F, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A, 0x0019, 0x0018,
                    0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010 } },
    { 11, 0x1, 4, { 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
                    0x0020, 0x040E, 0x040D, 0x040C, 0x040B, 0x040A, 0x0409, 0x0408 } },
    { 12, 0x1, 4, { 0x0412, 0x0411, 0x0410, 0x040F, 0x1803, 0x4002, 0x3C02, 0x3802,
                    0x3402, 0x3002, 0x2C02, 0x7C01, 0x7801, 0x7401, 0x7001, 0x6C01 } },
};

typedef struct {
    uint8_t len;
    uint8_t code;
    uint8_t size;   /* bits of the DC difference that follow */
} DcCode;

static const DcCode dcLuma[] = {
    { 3, 0x4, 0 }, { 2, 0x0, 1 }, { 2, 0x1, 2 }, { 3, 0x5, 3 }, { 3, 0x6, 4 },
    { 4, 0xE, 5 }, { 5, 0x1E, 6 }, { 6, 0x3E, 7 }, { 7, 0x7E, 8 },
};
static const DcCode dcChroma[] = {
    { 2, 0x0, 0 }, { 2, 0x1, 1 }, { 2, 0x2, 2 }, { 3, 0x6, 3 }, { 4, 0xE, 4 },
    { 5, 0x1E, 5 }, { 6, 0x3E, 6 }, { 7, 0x7E, 7 }, { 8, 0xFE, 8 },
};

/* The AC code at the top of `bits` if all of it lies within the first
 * `avail` bits: its MDEC value, with its length in *len. */
static int acCode(uint32_t bits, int avail, uint16_t* value, int* len) {
    if ((bits >> 30) == 2) {
        *value = CODE_EOB;
        *len = 2;
        return avail >= 2;
    }
    if ((bits >> 26) == 1) {
        *value = CODE_ESCAPE;
        *len = 6;
        return avail >= 6;
    }
    for (size_t i = 0; i < sizeof(acGroups) / sizeof(acGroups[0]); i++) {
        const AcGroup* g = &acGroups[i];
        if ((bits >> (32 - g->prefixLen)) != g->prefix) {
            continue;
        }
        int n = g->prefixLen + g->indexBits + 1;
        if (n > avail) {
            return 0;
        }
        uint32_t index = g->indexBits ? (bits << g->prefixLen) >> (32 - g->indexBits) : 0;
        uint16_t v = g->values[index];
        if ((bits >> (32 - n)) & 1) {
            v = (uint16_t)((v & 0xFC00) | (-(v & 0x3FF) & 0x3FF)); /* sign bit: negative level */
        }
        *value = v;
        *len = n;
        return 1;
    }
    return 0; /* twelve or more zeros: no code */
}

static void putWord(u16* table, int at, uint32_t w) {
    table[at] = (u16)w;
    table[at + 1] = (u16)(w >> 16);
}

static void makeDc(u16* out, const DcCode* codes, int count) {
    for (int idx = 0; idx < 256; idx++) {
        out[idx * 2] = 0;
        out[idx * 2 + 1] = 0;
        for (int i = 0; i < count; i++) {
            if ((idx >> (8 - codes[i].len)) == codes[i].code) {
                out[idx * 2] = codes[i].len;
                out[idx * 2 + 1] = codes[i].size;
                break;
            }
        }
    }
}

void vlcMakeTable(u16* table) {
    makeDc(table + TAB_DC_Y, dcLuma, sizeof(dcLuma) / sizeof(dcLuma[0]));
    makeDc(table + TAB_DC_C, dcChroma, sizeof(dcChroma) / sizeof(dcChroma[0]));

    /* Up to three whole codes from 13 bits; nothing after an end of block
     * (a DC follows) or an escape (16 raw bits follow). */
    for (uint32_t idx = 0; idx < 0x2000; idx++) {
        uint16_t codes[3] = { 0, 0, 0 };
        int used = 0;
        if ((idx >> 5) != 0) {
            for (int c = 0; c < 3; c++) {
                uint16_t v;
                int len;
                if (!acCode((idx << 19) << used, 13 - used, &v, &len)) {
                    break;
                }
                codes[c] = v;
                used += len;
                if (v == CODE_EOB || v == CODE_ESCAPE) {
                    break;
                }
            }
        }
        putWord(table, TAB_AC + idx * 4, used == 0 ? 0 : (uint32_t)used | (uint32_t)codes[0] << 16);
        putWord(table, TAB_AC + idx * 4 + 2, (uint32_t)codes[1] | (uint32_t)codes[2] << 16);
    }

    /* After eight zeros: one code from the next 9 bits. */
    for (uint32_t idx = 0; idx < 0x200; idx++) {
        uint16_t v;
        int len;
        uint32_t w = 0;
        if (acCode(idx << 15, 17, &v, &len)) {
            w = (uint32_t)(len - 8) | (uint32_t)v << 16;
        }
        putWord(table, TAB_AC_LONG + idx * 2, w);
    }
}

/* ---- the decoder ---- */

static u16 builtin[VLC_TABLE_HALVES];
static int builtinReady;

static VlcState state1, state2;
static u32 limit1 = 0xFFFFFF, limit2 = 0xFFFFFF; /* halfwords per call */

static uint32_t tableWord(const u16* t, uint32_t at) {
    return t[at] | (uint32_t)t[at + 1] << 16;
}

/* Shift out n bits (at most 16) and refill the window to 16 or more. */
static inline void consume(VlcState* s, uint32_t n) {
    s->bits <<= n & 31;
    s->used += n;
    if (s->used & 16) {
        s->used &= 15;
        s->bits |= (uint32_t)*s->src++ << s->used;
    }
}

s32 vlcDecode(VlcState* s, u32 limit, u32* bs, u32* buf, const u16* table) {
    u16* dst;
    u16* base;
    uint32_t w0, w1;
    u16 code;

    s->lookups = 0;
    if (bs != NULL) {
        const u16* in = (const u16*)bs;
        dst = (u16*)buf;
        dst[0] = in[0]; /* MDEC(1) command: size, 3800h */
        dst[1] = in[1];
        s->quant = (uint32_t)in[2] << 10;
        s->block = in[3] >= 3 ? 1 : 0;
        s->dc[0] = s->dc[1] = s->dc[2] = 0;
        s->bits = (uint32_t)in[4] << 16 | in[5];
        s->used = 0;
        s->src = in + 6;
        base = dst;
        dst += 2;
        goto block;
    }
    dst = s->dst;
    base = dst;
    goto ac;

block:
    /* A block's DC, or the end of the frame. */
    if (s->block == 0) {
        uint32_t dc = s->bits >> 22;
        if (dc == 0x1FF) {
            goto end;
        }
        consume(s, 10);
        *dst = (u16)(s->quant | dc);
    } else {
        if ((s->bits >> 22) == 0x3FF) {
            goto end;
        }
        const u16* dct = table + (s->block < 3 ? TAB_DC_C : TAB_DC_Y);
        uint32_t idx = s->bits >> 24;
        uint32_t len = dct[idx * 2];
        uint32_t size = dct[idx * 2 + 1];
        s32 diff = 0;
        s->bits <<= len & 31;
        if (size != 0) {
            diff = (s32)(s->bits >> ((32 - size) & 31));
            if ((s32)s->bits >= 0) {
                diff -= (s32)(0xFFFFFFFFu >> ((32 - size) & 31)); /* leading 0: negative */
            }
            s->bits <<= size & 31;
            s->used += size;
        }
        s->used += len;
        if (s->used & 16) {
            s->used &= 15;
            s->bits |= (uint32_t)*s->src++ << s->used;
        }
        s32* pred = &s->dc[s->block < 3 ? s->block - 1 : 2];
        *pred += diff;
        *dst = (u16)(((uint32_t)*pred << 2 & 0x3FF) | s->quant);
        if (++s->block == 7) {
            s->block = 1;
        }
    }
    if ((u32)(dst - base) >= limit) {
        s->dst = dst + 1;
        return 1;
    }
    dst++;

ac:
    /* AC codes up to the end of the block. */
    for (;;) {
        uint32_t idx = s->bits >> 19;
        s->lookups++;
        w0 = tableWord(table, TAB_AC + idx * 4);
        if (w0 != 0) {
            w1 = tableWord(table, TAB_AC + idx * 4 + 2);
        } else {
            consume(s, 8);
            w0 = tableWord(table, TAB_AC_LONG + (s->bits >> 23) * 2);
            w1 = 0;
        }
        consume(s, w0 & 0xFF);

        code = (u16)(w0 >> 16);
        if (code == CODE_ESCAPE) {
            goto escape;
        }
        *dst = code;
        if (code == CODE_EOB) {
            dst++;
            goto block;
        }
        dst++;
        if (w1 == 0) {
            continue;
        }
        code = (u16)w1;
        if (code == CODE_ESCAPE) {
            goto escape;
        }
        *dst = code;
        if (code == CODE_EOB) {
            dst++;
            goto block;
        }
        dst++;
        code = (u16)(w1 >> 16);
        if (code == 0) {
            continue;
        }
        if (code == CODE_ESCAPE) {
            goto escape;
        }
        *dst = code;
        if (code == CODE_EOB) {
            dst++;
            goto block;
        }
        dst++;
        continue;

    escape:
        /* 16 raw bits: run and level as the MDEC takes them. */
        *dst++ = (u16)(s->bits >> 16);
        s->bits = s->bits << 16 | (uint32_t)*s->src++ << s->used;
    }

end:
    for (int i = 0; i <= 64; i++) {
        *dst++ = CODE_EOB;
    }
    s->dst = dst;
    return 0;
}

const u16* vlcBuiltinTable(void) {
    if (!builtinReady) {
        vlcMakeTable(builtin);
        builtinReady = 1;
    }
    return builtin;
}

/* Halfwords a call may write, from DecDCTvlcSize's argument in words:
 * 0 or 1 means no limit. The previous setting is returned as stored. */
static s32 setLimit(u32* limit, s32 size) {
    u32 old = *limit;
    *limit = size > 1 ? (u32)size << 1 : 0xFFFFFF;
    return (s32)old;
}

s32 DecDCTvlcSize(s32 size) {
    safePoint();
    return setLimit(&limit1, size);
}

s32 DecDCTvlc(u32* bs, u32* buf) {
    safePoint();
    const u16* from = bs != NULL ? (const u16*)buf : state1.dst;
    s32 done = vlcDecode(&state1, limit1, bs, buf, vlcBuiltinTable());
    cpuTime(&state1, from);
    return done;
}

s32 DecDCTvlcSize2(s32 size) {
    safePoint();
    return setLimit(&limit2, size);
}

s32 DecDCTvlc2(u32* bs, u32* buf, u16* table) {
    safePoint();
    return vlcDecode(&state2, limit2, bs, buf, table);
}

void DecDCTvlcBuild(u16* table) {
    safePoint();
    memcpy(table, vlcBuiltinTable(), VLC_TABLE_HALVES * sizeof(u16));
}

/* ---- a world of its own (mdec.h mdecWorldCreate) ---- */

/* What a world has of this file: DecDCTvlc's and DecDCTvlc2's decodes in
 * progress and their size limits (the tables are the same everywhere). */
typedef struct {
    VlcState state1, state2;
    u32 limit1, limit2;
} VlcPart;

size_t vlcPartSize(void) {
    return sizeof(VlcPart);
}

void vlcPartInit(void* blob) {
    VlcPart* p = blob;
    memset(p, 0, sizeof(*p));
    p->limit1 = p->limit2 = 0xFFFFFF;
}

void vlcPartSwap(void* blob) {
    VlcPart* p = blob;
    VlcPart mine = {state1, state2, limit1, limit2};
    state1 = p->state1;
    state2 = p->state2;
    limit1 = p->limit1;
    limit2 = p->limit2;
    *p = mine;
}

/* Save states (system/savestate.h): DecDCTvlc's place in a frame it decodes
 * in pieces (src and dst point into game memory). */
void vlcStateIo(XgState* s) {
    XG_STATE_VAR(s, state1);
    XG_STATE_VAR(s, state2);
    XG_STATE_VAR(s, limit1);
    XG_STATE_VAR(s, limit2);
}
