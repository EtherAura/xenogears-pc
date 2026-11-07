#ifndef PORT_SPU_INTERNAL_H
#define PORT_SPU_INTERNAL_H

/* Internals shared by the SPU core's files (port/src/spu). */

#include "spu.h"

#define SPU_RAM_HALFWORDS (SPU_RAM_SIZE / 2)
#define SPU_ADDR_MASK (SPU_RAM_HALFWORDS - 1) /* halfword addresses */

enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

/* A volume register in sweep mode ramps its current level (psx-spx "SPU
 * Volume and ADSR Generator"); in direct mode the level is the register. */
typedef struct {
    int32_t level;
    uint32_t divider;
} SpuSweep;

/* Samples of one ADPCM block (psx-spx "Sample Data"). */
#define SPU_BLOCK_SAMPLES 28


typedef struct {
    /* Decoding and playback (voice.c). Addresses are in halfwords. */
    uint32_t addr;         /* the current ADPCM block */
    uint32_t loopAddr;     /* the repeat address register */
    uint8_t loopWritten;   /* the game wrote it since the key on: loop start flags leave it */
    uint32_t counter;      /* the pitch counter: bits 12 and up the sample in the block */
    int16_t window[3 + SPU_BLOCK_SAMPLES]; /* the previous block's last three samples, then this block's */
    int16_t history[2];    /* the ADPCM predictor's last two samples */
    uint8_t decoded;       /* samples of this block decoded so far */
    uint8_t header;        /* this block's shift/filter byte */
    int32_t out;           /* this sample's output, after the envelope */

    /* Envelope. */
    int phase;
    int32_t level;         /* ENVX */
    uint32_t divider;

    SpuSweep volume[2];
    int keyOffIn;          /* samples until a pending key off, -1: none */
    int keyOnIn;           /* samples until a written key on starts the voice, 0: none (SPU_MEASURED_KEY_ON) */
    int keyOffAtStart;     /* a key off came while the key on waited */
} SpuVoice;

typedef struct {
    uint16_t ram[SPU_RAM_HALFWORDS]; /* first: spuReset clears from regs on */
    SpuRegs regs;          /* the registers as latched */
    SpuVoice voices[24];
    uint32_t endx;
    int irq;               /* SPUSTAT bit 6 */
    int irqRaised;         /* since the last spuRun */
    SpuSweep mainVolume[2];
    uint32_t captureIndex; /* 0..0x1FF, the capture buffers' write position */
    uint64_t clock;        /* samples since reset */
    uint64_t latchClock;   /* clock at the last latch */

    uint16_t noise;        /* the noise generator's level */
    int32_t noiseTimer;

    /* Reverb: the work area's current address and the 22.05 kHz resampling. */
    uint32_t reverbAddr;
    int16_t reverbIn[2][64];  /* 44.1 kHz input history */
    int16_t reverbOut[2][64]; /* 44.1 kHz zero-stuffed output history */
} SpuState;

extern SpuState spu;

/* tables.c */
extern const int16_t spuGauss[512];
extern const int16_t spuZigzag[7][29];
extern const int16_t spuReverbFir[39];

/* core.c */
void spuCheckIrqSlow(uint32_t halfword);

/* spuRun without the CD input and the capture buffers, for an SPU of the
 * extras (port/src/extras/preview.c): theirs belong to the paused game. */
void spuRunBox(int16_t* out, uint32_t frames);

/* A voice, capture or reverb access to SPU RAM halfword `halfword` (psx-spx
 * "SPU Interrupt"): raises the interrupt when it is enabled and the access
 * hits the IRQ address. */
static inline void spuCheckIrq(uint32_t halfword) {
    if (spu.regs.m1AA_control & 0x40) {
        spuCheckIrqSlow(halfword);
    }
}

/* voice.c */
void spuVoiceKeyOn(int i);       /* a key on written (KON) */
void spuVoiceKeyOff(int i);      /* a key off written (KOFF) */
int32_t spuVoiceSample(int i);   /* the voice's output this sample */
void spuVoiceAdvance(int i);     /* the voice's state, one sample on */
void spuEnvelopeClock(SpuVoice* v, const SpuVoiceRegs* r); /* one ADSR clock */
void spuSweepClock(SpuSweep* s, uint16_t reg);

/* reverb.c */
void spuReverbReset(void);
void spuReverbClock(const int32_t in[2], int32_t out[2]);

static inline int32_t spuClamp16(int32_t v) {
    return v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v;
}

#endif /* PORT_SPU_INTERNAL_H */
