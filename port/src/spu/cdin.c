/*
 * The SPU's CD audio input: what the CD controller sends it. XA-ADPCM audio
 * arrives decoded (port/src/cd/xa.c) at 37.8 or 18.9 kHz, a sector at a time,
 * and leaves at 44.1 kHz through the controller's zigzag interpolation
 * (psx-spx "CDROM XA Audio ADPCM Compression", "25-point Zigzag
 * Interpolation"): each output sample is the sum of the last 29 input samples
 * weighed by one of seven tables, each table a step of 6/7 of an input sample
 * along the input. The CdMix volumes come last (psx-spx "ATV0..ATV3").
 * CD-DA (44.1 kHz) passes through.
 *
 * psx-spx's tables are used exactly as printed. How they are stepped was fitted
 * to black-box impulse responses of Beetle PSX at both rates (port/src/spu/test
 * testCdImpulse, and work/clean-room/measure/spu/cdfit):
 *   - the output time advances by 6/7 of an input sample at 37.8 kHz and by
 *     3/7 at 18.9 kHz: 18.9 kHz audio is the same filter on its own sample
 *     grid, its response spread over twice as many outputs ("about fifty
 *     44100Hz samples", psx-spx). At 37.8 kHz this is psx-spx's six inputs to
 *     seven outputs with the tables in order 1 to 7;
 *   - each output takes the table of its fractional position and the input
 *     samples up to that position (the ring index below);
 *   - products are summed, the sum shifted right by 15 and saturated;
 *   - the history carries across sectors; there is no start delay.
 * This reproduces 286 of the 290 measured values exactly; the other four are
 * one level closer to zero than this gives (psx-spx: the tables give "nearly
 * correct results, but with small rounding errors in some cases").
 *
 * Sectors come in bursts (one every 8 sectors read at double speed, 2016
 * samples each for 37.8 kHz stereo) exactly as fast as they play. When the
 * next input sample has not arrived yet the input waits, giving silence, and
 * goes on where it stopped when it comes.
 */

#include "spu_internal.h"
#include "../system/savestate.h"

#define QUEUE_FRAMES 16384 /* power of two; about 0.4 s */
#define RING 64            /* power of two, the 29 taps plus the 18.9 kHz back steps */

static int16_t queue[QUEUE_FRAMES][2];
static uint8_t queueStep[QUEUE_FRAMES]; /* output step in 1/7 input samples: 6 (37.8 kHz), 3 (18.9 kHz), 0 (CD-DA) */
static uint32_t queueHead; /* oldest */
static uint32_t queueCount;

static int16_t ring[2][RING];
static uint32_t taken;  /* input samples moved into the ring */
static int32_t lead;    /* whole input samples of the output time, minus `taken` */
static uint8_t frac;    /* sevenths of an input sample of the output time */

/* Per fraction of the output time (fitted, see above): the table, and the
 * input samples in the ring beyond the whole part of the output time. The
 * zero state (fraction 0) is the start of the measured responses. */
static const uint8_t fracTable[7] = {5, 4, 3, 2, 1, 0, 6};
static const int8_t fracInputs[7] = {-1, 0, 1, 2, 3, 4, -1};

void spuCdReset(void) {
    queueHead = 0;
    queueCount = 0;
    taken = 0;
    lead = 0;
    frac = 0;
    for (int i = 0; i < RING; i++) {
        ring[0][i] = ring[1][i] = 0;
    }
}

void spuCdQueue(const int16_t* lr, int frames, int rate) {
    uint8_t step = rate == 18900 ? 3 : rate == 37800 ? 6 : 0;

    for (int i = 0; i < frames; i++) {
        if (queueCount == QUEUE_FRAMES) {
            queueHead = (queueHead + 1) & (QUEUE_FRAMES - 1); /* overrun: drop the oldest */
            queueCount--;
        }
        uint32_t at = (queueHead + queueCount) & (QUEUE_FRAMES - 1);
        queue[at][0] = lr[2 * i];
        queue[at][1] = lr[2 * i + 1];
        queueStep[at] = step;
        queueCount++;
    }
}

static void take(int n) {
    for (; n > 0; n--) {
        ring[0][taken & (RING - 1)] = queue[queueHead][0];
        ring[1][taken & (RING - 1)] = queue[queueHead][1];
        taken++;
        lead--;
        queueHead = (queueHead + 1) & (QUEUE_FRAMES - 1);
        queueCount--;
    }
}

/* The next 44.1 kHz frame before the CdMix volumes; 0 while starved. */
static void resample(int32_t out[2]) {
    out[0] = out[1] = 0;
    if (queueCount > 0 && queueStep[queueHead] == 0) {
        /* CD-DA: no interpolation. */
        out[0] = queue[queueHead][0];
        out[1] = queue[queueHead][1];
        queueHead = (queueHead + 1) & (QUEUE_FRAMES - 1);
        queueCount--;
        return;
    }
    int32_t need = lead + fracInputs[frac];
    if (need > 0) {
        if ((uint32_t)need > queueCount) {
            return; /* starved */
        }
        take(need);
    }
    /* The output time steps at the rate of the input that comes next. */
    uint8_t step = queueCount > 0 ? queueStep[queueHead] : queueStep[(queueHead - 1) & (QUEUE_FRAMES - 1)];
    uint32_t p = taken + (uint32_t)(lead + fracInputs[frac]);
    const int16_t* t = spuZigzag[fracTable[frac]];
    for (int c = 0; c < 2; c++) {
        int32_t sum = 0;
        for (int i = 1; i <= 29; i++) {
            sum += ring[c][(p - (uint32_t)i) & (RING - 1)] * t[i - 1];
        }
        out[c] = spuClamp16(sum >> 15);
    }
    frac += step != 0 ? step : 6;
    lead += frac / 7;
    frac %= 7;
}

void spuCdNext(int32_t out[2]) {
    int32_t s[2];
    uint8_t atv[4];

    resample(s);
    cdGetAudioMix(atv);
    out[0] = spuClamp16(((s[0] * atv[0]) >> 7) + ((s[1] * atv[3]) >> 7));
    out[1] = spuClamp16(((s[0] * atv[1]) >> 7) + ((s[1] * atv[2]) >> 7));
}

/* ---- a world of its own (port/src/system/world.h) ---- */

/* The input's state, for a world with an SPU of its own: its CD input stays
 * empty, and the paused game's queued audio is not drawn on. All zero is an
 * empty input. */
typedef struct {
    int16_t queue[QUEUE_FRAMES][2];
    uint8_t queueStep[QUEUE_FRAMES];
    uint32_t queueHead, queueCount;
    int16_t ring[2][RING];
    uint32_t taken;
    int32_t lead;
    uint8_t frac;
} CdInState;

uint32_t spuCdQueued(void) {
    return queueCount;
}

size_t spuCdStateSize(void) {
    return sizeof(CdInState);
}

#define SWAP_FIELD(a, b)                                                                                              \
    do {                                                                                                              \
        unsigned char* x_ = (unsigned char*)&(a);                                                                     \
        unsigned char* y_ = (unsigned char*)&(b);                                                                     \
        for (size_t i_ = 0; i_ < sizeof(a); i_++) {                                                                   \
            unsigned char t_ = x_[i_];                                                                                \
            x_[i_] = y_[i_];                                                                                          \
            y_[i_] = t_;                                                                                              \
        }                                                                                                             \
    } while (0)

void spuCdStateSwap(void* other) {
    CdInState* s = other;
    SWAP_FIELD(queue, s->queue);
    SWAP_FIELD(queueStep, s->queueStep);
    SWAP_FIELD(queueHead, s->queueHead);
    SWAP_FIELD(queueCount, s->queueCount);
    SWAP_FIELD(ring, s->ring);
    SWAP_FIELD(taken, s->taken);
    SWAP_FIELD(lead, s->lead);
    SWAP_FIELD(frac, s->frac);
}

/* Save states (system/savestate.h): the CD audio waiting and the
 * resampler's history and phase. */
void spuCdStateIo(XgState* s) {
    XG_STATE_VAR(s, queue);
    XG_STATE_VAR(s, queueStep);
    XG_STATE_VAR(s, queueHead);
    XG_STATE_VAR(s, queueCount);
    XG_STATE_VAR(s, ring);
    XG_STATE_VAR(s, taken);
    XG_STATE_VAR(s, lead);
    XG_STATE_VAR(s, frac);
}
