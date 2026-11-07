/*
 * The reverb unit (psx-spx "SPU Reverb Registers", "SPU Reverb Formula").
 *
 * It runs at 22.05 kHz on a work area in SPU RAM (ESA up to the end), every
 * address relative to a current address that moves on one halfword per step
 * and wraps within the area, and resamples input and output with a 39-tap FIR
 * ("Reverb Buffer Resampling"). Both sides of a step take their input at the
 * same 44.1 kHz sample, and both outputs go back to 44.1 kHz through the same
 * FIR on zero-stuffed sequences of the same phase, as the formula shows them
 * ("left/right ... simultaneously at 22050Hz"). psx-spx's "Notes" say the
 * hardware spends one 44.1 kHz cycle on the left side and the next on the
 * right; that is how it uses its cycles, and the output does not show it:
 * Beetle PSX's reverb tails (workloads E10 Room, E11 Studio Large, the game's
 * mode 4, through its audio output, work/clean-room/measure/spu/raw; the
 * port's core with SPU_MEASURED_KEY_ON, so that the voice feeding the reverb
 * starts as it does there) match this to 0.26-0.39 levels RMS on both sides,
 * while a left side computed one cycle earlier, at its own input sample and
 * output phase, was off by 5.1-5.6 levels RMS, a tenth of the tail, mostly
 * near 11 kHz (xg-htn.44, work/agents/BD/AUDIO/NOTES.md).
 * Values written to memory saturate to 16 bits ("Notes"); the saturation in
 * between and the rounding of each product are the port's choice. ATTR bit 7
 * off stops the writes, not the reads ("Reverb Disable").
 *
 * The arithmetic is psx-spx's formula as written (SPEC SPU-F option a). Beetle
 * PSX, measured as a black box (work/clean-room/measure/spu workload E8),
 * rounds the reflections differently in about a quarter of the values, by one
 * level; PCSX-Redux writes nothing to the work area, so it gives no second
 * measurement. In game audio that choice is inaudible: the single rounding E8
 * fits for vIIR 4000h/8001h changes field 4's mix (reverb at -4 dB of it) by
 * 0.76 levels RMS, 77 dB below it (xg-htn.44).
 *
 * Not modelled: vIIR = -0x8000, which the hardware handles as a special case
 * (psx-spx "Bug"; on Beetle PSX it is not a plain negation either); no LIBSPU
 * preset uses it.
 */

#include <string.h>

#include "spu_internal.h"

enum {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
    mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2,
    vLIN, vRIN,
};

#define HISTORY 64 /* power of two, more than the FIR's 39 taps */

void spuReverbReset(void) {
    memset(spu.reverbIn, 0, sizeof(spu.reverbIn));
    memset(spu.reverbOut, 0, sizeof(spu.reverbOut));
    spu.reverbAddr = 0;
}

/* A work area address: `offset` halfwords from the current address, wrapped
 * into SPUESA..0x3FFFF. */
static uint32_t area(uint32_t offset) {
    uint32_t start = ((uint32_t)spu.regs.m1A2_revWorkAddr << 2) & SPU_ADDR_MASK;
    uint32_t a = spu.reverbAddr + (offset & SPU_ADDR_MASK);

    if (a > SPU_ADDR_MASK) {
        a = a - SPU_RAM_HALFWORDS + start;
    }
    return a & SPU_ADDR_MASK;
}

static int32_t rd(uint32_t offset) {
    uint32_t a = area(offset);
    spuCheckIrq(a);
    return (int16_t)spu.ram[a];
}

static void wr(uint32_t offset, int32_t value) {
    if (spu.regs.m1AA_control & 0x80) {
        uint32_t a = area(offset);
        spuCheckIrq(a);
        spu.ram[a] = (uint16_t)spuClamp16(value);
    }
}

static int32_t mul(int32_t a, int32_t v) {
    return (a * v) >> 15;
}

#define ADDR(reg) ((uint32_t)spu.regs.m1C0_reverb[reg] << 2)
#define VOL(reg) ((int32_t)(int16_t)spu.regs.m1C0_reverb[reg])

/* One reflection: (input + [src] * vWALL) * vIIR + [dst - 1] * (1 - vIIR). */
static int32_t reflection(int32_t in, int src, int dst) {
    int32_t x = spuClamp16(in + mul(rd(ADDR(src)), VOL(vWALL)));
    int32_t old = rd(ADDR(dst) - 1);
    return spuClamp16(mul(x, VOL(vIIR)) + (int32_t)(((int64_t)old * (0x8000 - VOL(vIIR))) >> 15));
}

/* One side's step at 22.05 kHz (c: 0 left, 1 right). */
static int32_t step(int c, int32_t input) {
    int32_t in = mul(input, VOL(vLIN + c));

    wr(ADDR(mLSAME + c), reflection(in, dLSAME + c, mLSAME + c));
    wr(ADDR(mLDIFF + c), reflection(in, dLDIFF + (c ^ 1), mLDIFF + c));

    int32_t comb = mul(rd(ADDR(mLCOMB1 + c)), VOL(vCOMB1)) + mul(rd(ADDR(mLCOMB2 + c)), VOL(vCOMB2)) +
                   mul(rd(ADDR(mLCOMB3 + c)), VOL(vCOMB3)) + mul(rd(ADDR(mLCOMB4 + c)), VOL(vCOMB4));
    int32_t fa = rd(ADDR(mLAPF1 + c) - ADDR(dAPF1));
    int32_t fb = rd(ADDR(mLAPF2 + c) - ADDR(dAPF2));
    int32_t apf1 = spuClamp16(comb - mul(fa, VOL(vAPF1)));
    int32_t apf2 = spuClamp16(fa + mul(apf1, VOL(vAPF1)) - mul(fb, VOL(vAPF2)));
    wr(ADDR(mLAPF1 + c), apf1);
    wr(ADDR(mLAPF2 + c), apf2);
    return spuClamp16(fb + mul(apf2, VOL(vAPF2)));
}

/* The FIR over a history ring ending at `now`: the even taps and the centre
 * (every other tap is zero). */
static int32_t fir(const int16_t* h, uint32_t now) {
    int32_t sum = spuReverbFir[19] * h[(now - 19) & (HISTORY - 1)];
    for (int k = 0; k < 39; k += 2) {
        sum += spuReverbFir[k] * h[(now - k) & (HISTORY - 1)];
    }
    return sum;
}

void spuReverbClock(const int32_t in[2], int32_t out[2]) {
    uint32_t t = (uint32_t)spu.clock;
    uint32_t now = t & (HISTORY - 1);

    spu.reverbIn[0][now] = (int16_t)in[0];
    spu.reverbIn[1][now] = (int16_t)in[1];
    if (t & 1) {
        spu.reverbOut[0][now] = spu.reverbOut[1][now] = 0;
    } else {
        /* A step on even cycles: both sides' input from this sample, left
         * then right (the order shows only if one reads what the other
         * writes in the same step). */
        int32_t down[2];
        for (int c = 0; c < 2; c++) {
            down[c] = spuClamp16(fir(spu.reverbIn[c], t) >> 15);
        }
        for (int c = 0; c < 2; c++) {
            spu.reverbOut[c][now] = (int16_t)step(c, down[c]);
        }
        uint32_t start = ((uint32_t)spu.regs.m1A2_revWorkAddr << 2) & SPU_ADDR_MASK;
        spu.reverbAddr = (spu.reverbAddr + 1) & SPU_ADDR_MASK;
        if (spu.reverbAddr < start) {
            spu.reverbAddr = start;
        }
    }
    out[0] = spuClamp16(fir(spu.reverbOut[0], t) >> 14);
    out[1] = spuClamp16(fir(spu.reverbOut[1], t) >> 14);
}
