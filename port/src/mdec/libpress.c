/*
 * LIBPRESS (PSY-Q 4.0 LIBPRESS.OBJ) for the PC port: DecDCTReset, DecDCTin,
 * DecDCTout and the rest, with the SDK's semantics, on the MDEC model
 * (mdec.c). The bitstream decoder (DecDCTvlc) is vlc.c.
 *
 * The SDK feeds the MDEC by DMA0 (a command word, then its parameters) and
 * reads pixels by DMA1. Here a transfer's data lands when it completes: 512
 * CPU cycles per 8x8 block decoded and 4/3 more per AC term in it (90 us per
 * colour macroblock without terms, 1.27 ms per 16x224 slice of the game's
 * movies), and a word per cycle for the table uploads (psx-spx "DMA
 * Channels": 1 clk/word for DMA0 and DMA1; it calls the decompression time
 * unknown). The block time is measured with the Beetle PSX core as a black
 * box (work/clean-room/measure/mdec, suite "timing": root counter 2 from the
 * command to DMA1's end): 512 cycles a block without AC terms, 596 for blocks
 * of 63 terms, 512 to 532 on retail frames (US disc 1, movie 0, frames 1, 39
 * and 76). PCSX-Redux, measured the same way, takes 8 cycles an output word
 * whatever the content (256 a block in 24bpp). Completions are interrupts
 * (system/hw.h): DecDCTinCallback and DecDCToutCallback functions run at a
 * safe point, as on the PS1 (where LIBETC's DMA handler calls them), and so
 * does what they start.
 *
 * On the PS1 a callback runs some time after its DMA completes (the
 * interrupt's way through the BIOS and LIBETC, then the callback's own work:
 * the movie player loads the slice into VRAM before its DecDCTout starts the
 * next one), and the MDEC waits that long between slices. Here the callbacks
 * run CALLBACK_DELAY after the completion. Measured with Beetle PSX as a
 * black box (tools/beetle.py; work/agents/NPFIX/movie/NOTES.md): the game's
 * movies (320x224, 24bpp, 20 slices) take about 1.72 vblanks from DecDCTin
 * to the last slice's callback where the block times alone give 1.55; the
 * vblank the player's frame counter changes in bounds it frame by frame
 * (224 logo and 1723 attract movie frames), which leaves 4900 to 5300
 * cycles a slice. The pictures of the US logo and attract movies (10455
 * vblanks, slowdown replay) match Beetle's best at 4500: 102 a vblank early
 * or late (5000: 150; none: 435, nearly all early). What is left is mostly
 * the PS1's own time after each vblank interrupt, before VSync returns,
 * which the port does not take. The field's 15bpp movie backgrounds
 * (OPX_67: the whole 320x224 frame in one DMA1, one callback a frame) need
 * nothing else: field 665's (US Disc 2, movie 6 frames 1111-1247 looping)
 * matches Beetle at all 2091 vblanks of a slowdown replay with any delay
 * from 0 to 30000 cycles, and only from about 100000 do pictures come late
 * (xg-htn.45; Beetle's block time is the same for 15bpp: 196617 cycles for
 * 384 DC-only blocks, 196640 in 24bpp).
 *
 * DMA1 waits for the decoder, which runs at most a macroblock ahead of it:
 * a DecDCTout decodes the macroblocks it covers when it starts and delivers
 * them at its completion time; the rest of a macroblock it ends inside is
 * kept for the next one. The decode command ends (status bit 29, which
 * DecDCTinSync and the next DecDCTin wait for) when its last parameter word
 * has been read, after the last macroblock and any FE00h padding.
 */

#include "common.h"
#include "psyq.h"
#include "video_player/video_player.h"

#include "mdec/mdec.h"
#include "mdec/vlc.h"
#include "system/hw.h"
#include "../system/savestate.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_TIME (512.0 / HW_CPU_HZ) /* one 8x8 block decoded, DC only (above) */
#define TERM_TIME (84.0 / 63 / HW_CPU_HZ) /* and each AC term in it (596 cycles for 63) */
#define WORD_TIME (1.0 / HW_CPU_HZ)    /* one word moved by DMA */
#define CALLBACK_DELAY (4500.0 / HW_CPU_HZ) /* a DMA's completion to its callback's work (above) */
#define SYNC_TIMEOUT 1.0               /* the SDK gives up after 0x100000 status polls (about a second) */

void ResetCallback(void);

/* The library's tables as in its data: MDEC(2) with both quant tables
 * (the MPEG-1 default intra matrix in zigzag order, DC entry 2), MDEC(3)
 * with the standard IDCT scale matrix (psx-spx "set_scale_table"). */
static u32 iqCommand[1 + 32];
static u32 dctCommand[1 + 32];
static int tablesReady;

static const u8 defaultIq[64] = {
    2,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83,
};

static const u16 defaultScale[64] = {
    0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82,
    0x7D8A, 0x6A6D, 0x471C, 0x18F8, 0xE707, 0xB8E3, 0x9592, 0x8275,
    0x7641, 0x30FB, 0xCF04, 0x89BE, 0x89BE, 0xCF04, 0x30FB, 0x7641,
    0x6A6D, 0xE707, 0x8275, 0xB8E3, 0x471C, 0x7D8A, 0x18F8, 0x9592,
    0x5A82, 0xA57D, 0xA57D, 0x5A82, 0x5A82, 0xA57D, 0xA57D, 0x5A82,
    0x471C, 0x8275, 0x18F8, 0x6A6D, 0x9592, 0xE707, 0x7D8A, 0xB8E3,
    0x30FB, 0x89BE, 0x7641, 0xCF04, 0xCF04, 0x7641, 0x89BE, 0x30FB,
    0x18F8, 0xB8E3, 0x6A6D, 0x8275, 0x7D8A, 0x9592, 0x471C, 0xE707,
};

typedef struct {
    u8 iq_y[64];
    u8 iq_c[64];
    s16 dct[64];
} DECDCTENV;

static void (*inCallback)(void);
static void (*outCallback)(void);

/* A transfer finishes at a time, whether or not its interrupt has been
 * delivered yet: the SDK's sync functions poll the hardware, not the
 * callbacks. advance() applies what has finished by now. */
static int cmdActive;     /* MDEC status bit 29 */
static double cmdEndAt = INFINITY; /* when its last parameter word is read (DMA0 done) */
static int outBusy;       /* DMA1 running */
static double outEndAt = INFINITY;
static u8* outDst;
static u32 outBytes;
static u8* stage;         /* pixels for the running DMA1 */
static u32 stageFill;
static u32 stageCap;
static u8 spill[16 * 16 * 3]; /* the part of a macroblock past the last DMA1's end */
static u32 spillLen;
static u32 spillPos;
static double decodeAt;   /* when the decoder has produced what is staged */
static uint32_t generation; /* bumped by a reset: queued interrupts are dropped */
static const u32* inAddr; /* for the timeout report */
static u8* outAddr;
static int inWorld;       /* a world of its own (mdec.h): LIBETC is the game's */

static void makeTables(void) {
    iqCommand[0] = 0x40000001;
    memcpy((u8*)&iqCommand[1], defaultIq, 64);
    memcpy((u8*)&iqCommand[1] + 64, defaultIq, 64);
    dctCommand[0] = 0x60000000;
    memcpy(&dctCommand[1], defaultScale, sizeof(defaultScale));
    tablesReady = 1;
}

static void advance(void) {
    double now = hwNow();

    if (outBusy && now >= outEndAt) {
        memcpy(outDst, stage, outBytes);
        outBusy = 0;
        outEndAt = INFINITY;
        stageFill = 0;
    }
    if (cmdActive && now >= cmdEndAt) {
        cmdActive = mdecBusy(); /* a decode short of words stays busy */
        cmdEndAt = INFINITY;
    }
}

static void enter(void) {
    hwSafePoint();
    advance();
}

/* ---- interrupts ---- */

enum { EV_IN = 1, EV_OUT = 2 };

static void interrupt(void* arg) {
    uintptr_t a = (uintptr_t)arg;

    if ((uint32_t)(a >> 2) != generation) {
        return; /* the channels were stopped by a reset */
    }
    advance();
    if ((a & EV_IN) && inCallback != NULL) {
        inCallback();
    }
    if ((a & EV_OUT) && outCallback != NULL) {
        outCallback();
    }
}

static void completed(void* arg) {
    (void)arg;
    advance();
}

/* A transfer completes at `at`: what polls the hardware sees it then, and its
 * callback runs CALLBACK_DELAY later. */
static void schedule(double at, int what) {
    hwScheduleAt(at, completed, NULL);
    hwScheduleAt(at + CALLBACK_DELAY, interrupt, (void*)(((uintptr_t)generation << 2) | (uintptr_t)what));
}

/* The decode's input is all read by `at`: DMA0 completes then. */
static int inEnds(double at) {
    if (!cmdActive || cmdEndAt != INFINITY || mdecFedLeft() != 0) {
        return 0;
    }
    cmdEndAt = at;
    return EV_IN;
}

/* Decode for the running DMA1 what the input allows, and queue its
 * completion once it is whole. When the decode's input runs out first, DMA0
 * completes and DMA1 waits for the next command's data. */
static void service(void) {
    u8 mb[16 * 16 * 3];
    uint32_t blocks = 0;
    uint32_t terms = 0;
    double now = hwNow();

    if (!outBusy || outEndAt != INFINITY) {
        return;
    }
    if (stageCap < outBytes) {
        stage = realloc(stage, outBytes);
        if (stage == NULL) {
            fprintf(stderr, "MDEC: out of memory\n");
            abort();
        }
        stageCap = outBytes;
    }
    while (stageFill < outBytes) {
        if (spillPos < spillLen) {
            u32 n = spillLen - spillPos;
            if (n > outBytes - stageFill) {
                n = outBytes - stageFill;
            }
            memcpy(stage + stageFill, spill + spillPos, n);
            stageFill += n;
            spillPos += n;
            continue;
        }
        if (!mdecDecoding()) {
            break;
        }
        u32 left = mdecFedLeft();
        u32 size = mdecDecode(mb);
        if (size == 0) {
            break;
        }
        /* Its blocks, and its AC terms: the halfwords read but each block's
         * DC and end code. */
        u32 n = size >= 512 ? 6 : 1;
        u32 halves = (left - mdecFedLeft()) * 2;
        blocks += n;
        terms += halves > 2 * n ? halves - 2 * n : 0;
        u32 fit = size < outBytes - stageFill ? size : outBytes - stageFill;
        memcpy(stage + stageFill, mb, fit);
        stageFill += fit;
        memcpy(spill, mb + fit, size - fit);
        spillLen = size - fit;
        spillPos = 0;
    }
    if (decodeAt < now) {
        decodeAt = now;
    }
    decodeAt += blocks * BLOCK_TIME + terms * TERM_TIME;
    if (stageFill == outBytes) {
        double moved = now + (outBytes / 4) * WORD_TIME;
        double end = decodeAt > moved ? decodeAt : moved;
        mdecSkipPadding();
        outEndAt = end;
        schedule(end, EV_OUT | inEnds(end));
    } else if (inEnds(decodeAt)) {
        schedule(decodeAt, EV_IN);
    }
}

/* ---- the SDK's MDEC layer (MDEC_reset, MDEC_in, ...) ---- */

static void resetMdec(int mode);

static uint32_t readStatus(void) {
    uint32_t st = mdecStatus();

    /* The command counts as running until its last word is read. */
    st &= ~(MDEC_ST_BUSY | MDEC_ST_DREQ0);
    if (cmdActive) {
        st |= MDEC_ST_BUSY;
        if (mdecControlBits() & MDEC_CTL_DMA0) {
            st |= MDEC_ST_DREQ0;
        }
    }
    if (spillPos < spillLen || (outBusy && stageFill > 0)) {
        st &= ~MDEC_ST_OUT_EMPTY;
        if (mdecControlBits() & MDEC_CTL_DMA1) {
            st |= MDEC_ST_DREQ1;
        }
    }
    return st;
}

static void printStatus(void) {
    u32 status = readStatus();

    fprintf(stderr, "\t DMA=(%d,%d), ADDR=(%p->%p)\n", cmdActive, outBusy, (const void*)inAddr, (void*)outAddr);
    fprintf(stderr, "\t FIFO=(%d,%d),BUSY=%d,DREQ=(%d,%d),RGB24=%d,STP=%d\n", (~status >> 31) & 1, (status >> 30) & 1,
            (status >> 29) & 1, (status >> 28) & 1, (status >> 27) & 1, (status >> 25) & 1, (status >> 23) & 1);
}

static int inSyncBusy(void) {
    advance();
    return cmdActive;
}

static int outSyncBusy(void) {
    advance();
    return outBusy;
}

/* The SDK polls 0x100000 times, then reports and resets the MDEC. */
static s32 waitWhile(int (*busy)(void), const char* name) {
    double start = hwNow();

    while (busy()) {
        if (hwNow() - start > SYNC_TIMEOUT) {
            fprintf(stderr, "%s timeout:\n", name);
            printStatus();
            resetMdec(1);
            return -1;
        }
        hwYield();
    }
    return 0;
}

/* A command word and `size` parameter words (DMA0 moves whole 32-word
 * blocks, as the SDK programs it). */
static void mdecIn(const u32* addr, s32 size) {
    u32 words = ((u32)size >> 5 & 0xFFFF) * 32;

    waitWhile(inSyncBusy, "MDEC_in_sync");
    inAddr = addr;
    mdecCommand(addr[0]);
    mdecFeed(addr + 1, words);
    cmdActive = 1;
    cmdEndAt = INFINITY;
    if (!mdecDecoding()) {
        /* A table upload: the MDEC takes the words as fast as DMA moves them. */
        double end = hwNow() + words * WORD_TIME;
        schedule(end, inEnds(end));
        return;
    }
    service();
}

static void mdecOut(u32* addr, s32 size) {
    waitWhile(outSyncBusy, "MDEC_out_sync");
    outAddr = (u8*)addr;
    outDst = (u8*)addr;
    outBytes = ((u32)size >> 5 & 0xFFFF) * 32 * 4;
    stageFill = 0;
    outBusy = 1;
    outEndAt = INFINITY;
    service();
}

static void resetMdec(int mode) {
    if (!tablesReady) {
        makeTables();
    }
    switch (mode) {
    case 0:
    case 1:
        mdecControl(MDEC_CTL_RESET);
        /* Both DMA channels stopped: their transfers and interrupts are gone. */
        generation++;
        cmdActive = 0;
        cmdEndAt = INFINITY;
        outBusy = 0;
        outEndAt = INFINITY;
        stageFill = 0;
        spillLen = spillPos = 0;
        decodeAt = hwNow();
        mdecControl(MDEC_CTL_DMA0 | MDEC_CTL_DMA1);
        if (mode == 0) {
            mdecIn(iqCommand, 32);
            mdecIn(dctCommand, 32);
        }
        break;
    default:
        fprintf(stderr, "MDEC_rest:bad option(%d)\n", mode);
        break;
    }
}

/* ---- LIBPRESS ---- */

void DecDCTReset(s32 mode) {
    enter();
    if (mode == 0 && !inWorld) {
        ResetCallback();
    }
    resetMdec(mode);
}

DECDCTENV* DecDCTGetEnv(DECDCTENV* env) {
    enter();
    if (!tablesReady) {
        makeTables();
    }
    memcpy(env->iq_y, (u8*)&iqCommand[1], 64);
    memcpy(env->iq_c, (u8*)&iqCommand[1] + 64, 64);
    memcpy(env->dct, &dctCommand[1], 128);
    return env;
}

/* Takes the quant tables (not the IDCT one: the SDK uploads its own). */
DECDCTENV* DecDCTPutEnv(DECDCTENV* env) {
    enter();
    if (!tablesReady) {
        makeTables();
    }
    memcpy((u8*)&iqCommand[1], env->iq_y, 64);
    memcpy((u8*)&iqCommand[1] + 64, env->iq_c, 64);
    mdecIn(iqCommand, 32);
    mdecIn(dctCommand, 32);
    return env;
}

s32 DecDCTBufSize(u32* bs) {
    enter();
    return *(u16*)bs;
}

/* mode bit 0: 24bpp (else 15bpp); bit 1: set bit 15 of 15bpp pixels. */
void DecDCTin(u32* buf, s32 mode) {
    enter();
    if (mode & 1) {
        *buf &= ~0x08000000u;
    } else {
        *buf |= 0x08000000u;
    }
    if (mode & 2) {
        *buf |= 0x02000000u;
    } else {
        *buf &= ~0x02000000u;
    }
    mdecIn(buf, *(u16*)buf);
}

void DecDCTout(void* buf, s32 size) {
    enter();
    mdecOut(buf, size);
}

s32 DecDCTinSync(s32 mode) {
    enter();
    if (mode != 0) {
        return (readStatus() >> 29) & 1;
    }
    return waitWhile(inSyncBusy, "MDEC_in_sync");
}

/* mode != 0 reads status bit 24 like the SDK (the signed-output flag on
 * hardware, so it is 0 for LIBPRESS's commands). */
s32 DecDCToutSync(s32 mode) {
    enter();
    if (mode != 0) {
        return (readStatus() >> 24) & 1;
    }
    return waitWhile(outSyncBusy, "MDEC_out_sync");
}

void* DecDCTinCallback(void (*func)(void)) {
    enter();
    void (*old)(void) = inCallback;
    inCallback = func;
    return (void*)old;
}

/* Returns the previous callback, like the SDK. */
void* DecDCToutCallback(void (*func)()) {
    enter();
    void (*old)(void) = outCallback;
    outCallback = (void (*)(void))func;
    return (void*)old;
}

/* ---- a world of its own (mdec.h mdecWorldCreate) ---- */

/* What a world has of this file: LIBPRESS's tables and callbacks, and the
 * DMA channels' transfers (their completions wait on the world's queue). */
typedef struct {
    u32 iqCommand[1 + 32], dctCommand[1 + 32];
    int tablesReady;
    void (*inCallback)(void);
    void (*outCallback)(void);
    int cmdActive;
    double cmdEndAt;
    int outBusy;
    double outEndAt;
    u8* outDst;
    u32 outBytes;
    u8* stage;
    u32 stageFill, stageCap;
    u8 spill[16 * 16 * 3];
    u32 spillLen, spillPos;
    double decodeAt;
    uint32_t generation;
    const u32* inAddr;
    u8* outAddr;
    int inWorld;
} PressPart;

struct MdecWorld {
    PressPart press;
    void* decoder;
    void* vlc;
};

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

static void pressPartSwap(PressPart* p) {
    SWAP(iqCommand);
    SWAP(dctCommand);
    SWAP(tablesReady);
    SWAP(inCallback);
    SWAP(outCallback);
    SWAP(cmdActive);
    SWAP(cmdEndAt);
    SWAP(outBusy);
    SWAP(outEndAt);
    SWAP(outDst);
    SWAP(outBytes);
    SWAP(stage);
    SWAP(stageFill);
    SWAP(stageCap);
    SWAP(spill);
    SWAP(spillLen);
    SWAP(spillPos);
    SWAP(decodeAt);
    SWAP(generation);
    SWAP(inAddr);
    SWAP(outAddr);
    SWAP(inWorld);
}
#undef SWAP

MdecWorld* mdecWorldCreate(void) {
    MdecWorld* w = calloc(1, sizeof(MdecWorld));
    if (w == NULL) {
        return NULL;
    }
    w->decoder = malloc(mdecDecoderPartSize());
    w->vlc = malloc(vlcPartSize());
    if (w->decoder == NULL || w->vlc == NULL) {
        mdecWorldDestroy(w);
        return NULL;
    }
    mdecDecoderPartInit(w->decoder);
    vlcPartInit(w->vlc);
    w->press.cmdEndAt = INFINITY;
    w->press.outEndAt = INFINITY;
    w->press.inWorld = 1;
    return w;
}

void mdecWorldDestroy(MdecWorld* w) {
    if (w == NULL) {
        return;
    }
    free(w->press.stage);
    free(w->decoder);
    free(w->vlc);
    free(w);
}

void mdecWorldSwap(MdecWorld* w) {
    pressPartSwap(&w->press);
    mdecDecoderPartSwap(w->decoder);
    vlcPartSwap(w->vlc);
}

/* ---- save states (system/savestate.h) ---- */

/* The transfers in flight (their times are the virtual clock's; the
 * generation matches the queued interrupts'), the tables, the callbacks and
 * the pixels staged for the running DMA1 (the staging buffer is this
 * process's: its first stageFill bytes are saved). */
void pressStateIo(XgState* s) {
    XG_STATE_VAR(s, iqCommand);
    XG_STATE_VAR(s, dctCommand);
    XG_STATE_VAR(s, tablesReady);
    XG_STATE_VAR(s, inCallback);
    XG_STATE_VAR(s, outCallback);
    XG_STATE_VAR(s, cmdActive);
    XG_STATE_VAR(s, cmdEndAt);
    XG_STATE_VAR(s, outBusy);
    XG_STATE_VAR(s, outEndAt);
    XG_STATE_VAR(s, outDst);
    XG_STATE_VAR(s, outBytes);
    XG_STATE_VAR(s, spill);
    XG_STATE_VAR(s, spillLen);
    XG_STATE_VAR(s, spillPos);
    XG_STATE_VAR(s, decodeAt);
    XG_STATE_VAR(s, generation);
    XG_STATE_VAR(s, inAddr);
    XG_STATE_VAR(s, outAddr);
    u32 fill = (u32)xgStateLen(s, "stageFill", stageFill);
    if (xgStateLoading(s)) {
        u32 need = fill > outBytes ? fill : outBytes;
        if (need > stageCap) {
            stage = realloc(stage, need);
            if (stage == NULL) {
                fprintf(stderr, "MDEC: out of memory\n");
                abort();
            }
            stageCap = need;
        }
        stageFill = fill;
    }
    xgStateIo(s, "stage", stage, fill);
}
