/*
 * SPU voices: ADPCM decoding, the pitch counter and the 4-point Gaussian
 * interpolation (psx-spx "SPU ADPCM Samples", "SPU ADPCM Pitch"), noise
 * (psx-spx "SPU Noise Generator"), key on and off, loops and ENDX (psx-spx
 * "SPU Voice Flags"), the voices' SPU RAM reads and the interrupt (psx-spx
 * "SPU Interrupt"), the ADSR envelope and the volume sweeps (psx-spx "SPU
 * Volume and ADSR Generator").
 *
 * A voice plays one 16-byte block at a time: a shift/filter byte, a flag byte
 * and 28 4-bit samples. Its pitch counter steps by the pitch each 44.1 kHz
 * sample; bits 12 and up select the newest sample of the block, bits 4-11 the
 * interpolation phase. When the counter passes the block's last sample the
 * voice moves on to the next block, or, at a loop end flag, to the repeat
 * address. Like the hardware (psx-spx "SPU Internal State Machine"), every
 * voice keeps reading SPU RAM each sample, playing or not: the block's header
 * word and the data word it is in.
 */

#include "spu_internal.h"

/* ---- envelope and sweep rates ---- */

/* One envelope/sweep step at `level` (psx-spx "Envelope Operation"): the step
 * added and the counter increment. `rate` is shift << 2 | step (7 bits);
 * `invert` negates the step (decreasing, or a sweep's negative phase);
 * `noStep` is the rate whose all-ones bits never step (0x7F, or 0x7C for the
 * decay and release rates, which have no step bits). */
static void rateStep(int rate, int invert, int decrease, int exponential, int32_t level, int noStep,
                     int32_t* step, uint32_t* increment) {
    int shift = rate >> 2;
    int32_t s = 7 - (rate & 3);
    uint32_t inc = 0x8000;

    if (invert) {
        s = ~s; /* +7..+4 -> -8..-5 */
    }
    if (shift < 11) {
        s *= 1 << (11 - shift);
    } else {
        inc = shift - 11 >= 16 ? 0 : inc >> (shift - 11);
    }
    if (exponential) {
        if (decrease) {
            s = (s * level) >> 15;
        } else if (level > 0x6000) {
            /* "Exponential" increase: a slower linear rate near the top. */
            if (shift < 10) {
                s >>= 2;
            } else if (shift >= 11) {
                inc >>= 2;
            } else {
                s >>= 1;
                inc >>= 1;
            }
        }
    }
    if (inc == 0 && rate != noStep) {
        inc = 1;
    }
    *step = s;
    *increment = inc;
}

void spuEnvelopeClock(SpuVoice* v, const SpuVoiceRegs* r) {
    uint16_t a1 = r->m8_adsr1;
    uint16_t a2 = r->mA_adsr2;
    int rate;
    int decrease;
    int exponential;
    int noStep = 0x7F;
    int32_t step;
    uint32_t inc;

    if (v->phase == ENV_ATTACK && v->level >= 0x7FFF) {
        v->phase = ENV_DECAY;
    }
    switch (v->phase) {
    case ENV_ATTACK:
        rate = (a1 >> 8) & 0x7F;
        decrease = 0;
        exponential = a1 >> 15;
        break;
    case ENV_DECAY:
        rate = ((a1 >> 4) & 0xF) << 2;
        decrease = 1;
        exponential = 1;
        noStep = 0x7C;
        break;
    case ENV_SUSTAIN:
        rate = (a2 >> 6) & 0x7F;
        decrease = (a2 >> 14) & 1;
        exponential = a2 >> 15;
        break;
    default:
        rate = (a2 & 0x1F) << 2;
        decrease = 1;
        exponential = (a2 >> 5) & 1;
        noStep = 0x7C;
        break;
    }
    rateStep(rate, decrease, decrease, exponential, v->level, noStep, &step, &inc);
    v->divider += inc;
    if (!(v->divider & 0x8000)) {
        return;
    }
    v->divider = 0;
    v->level += step;
    if (v->level > 0x7FFF) {
        v->level = 0x7FFF;
    } else if (v->level < 0) {
        v->level = 0;
    }
    if (v->phase == ENV_DECAY && v->level < (((a1 & 0xF) + 1) << 11)) {
        v->phase = ENV_SUSTAIN;
    }
}

/* A volume register's level for this sample, then one sweep clock. */
void spuSweepClock(SpuSweep* s, uint16_t reg) {
    int32_t step;
    uint32_t inc;

    if (!(reg & 0x8000)) {
        s->level = (int16_t)(reg << 1); /* direct: volume / 2, 15-bit signed */
        return;
    }
    int exponential = (reg >> 14) & 1;
    int decrease = (reg >> 13) & 1;
    int negative = (reg >> 12) & 1;

    /* A negative phase inverts the step (psx-spx). */
    rateStep(reg & 0x7F, decrease ^ negative, decrease, exponential, s->level, 0x7F, &step, &inc);
    s->divider += inc;
    if (!(s->divider & 0x8000)) {
        return;
    }
    s->divider = 0;
    s->level += step;
    if (!decrease) {
        s->level = spuClamp16(s->level);
    } else if (negative) {
        s->level = s->level > 0 ? 0 : s->level < -0x8000 ? -0x8000 : s->level;
    } else if (s->level < 0) {
        s->level = 0;
    }
}

/* The envelope to release (psx-spx "KOFF"). */
static void release(SpuVoice* v) {
    if (v->phase != ENV_RELEASE) {
        v->phase = ENV_RELEASE;
        v->divider = 0;
    }
}

/* ---- ADPCM ---- */

/* psx-spx "SPU ADPCM Samples" ("reportedly same as for CD-XA") and "CDROM XA
 * Audio ADPCM Compression" ("Pos/neg Tables"): filters 0-4. */
static const int8_t filterPos[16] = {0, 60, 115, 98, 122};
static const int8_t filterNeg[16] = {0, 0, -52, -55, -60};

/* Decode this block's samples up to and including sample `n` (the hardware
 * reads one data word, four samples, at a time). psx-spx "CDROM XA Audio
 * ADPCM Compression" decode_28_nibbles: the nibble shifted to the top of 16
 * bits, shifted right by the shift (13-15 act as 9, "XA-ADPCM Header Bytes"),
 * plus the prediction from the last two samples, saturated. The prediction
 * is by default the measured one, each product shifted on its own (both
 * PCSX-Redux and Beetle PSX, workloads E1/E1R); psx-spx's single rounded
 * shift with SPU_MEASURED_ARITHMETIC 0 (spu.h). */
static void decode(SpuVoice* v, int n) {
    int shift = v->header & 0xF;
    int filter = v->header >> 4;
    int32_t f0 = filterPos[filter];
    int32_t f1 = filterNeg[filter];

    if (shift > 12) {
        shift = 9;
    }
    while (v->decoded <= n) {
        uint16_t word = spu.ram[(v->addr + 1 + (v->decoded >> 2)) & SPU_ADDR_MASK];
        int nibble = (word >> ((v->decoded & 3) * 4)) & 0xF;
        int32_t s = (int16_t)(nibble << 12) >> shift;
#if SPU_MEASURED_ARITHMETIC
        s += ((v->history[0] * f0) >> 6) + ((v->history[1] * f1) >> 6);
#else
        s += (v->history[0] * f0 + v->history[1] * f1 + 32) >> 6;
#endif
        s = spuClamp16(s);
        v->history[1] = v->history[0];
        v->history[0] = (int16_t)s;
        v->window[3 + v->decoded] = (int16_t)s;
        v->decoded++;
    }
}

/* Start playing the block at v->addr: its header, and a loop start flag
 * copies its address to the repeat address (psx-spx "LSAX"), unless the game
 * wrote the repeat address since the key on: then that address stays.
 * Measured with PCSX-Redux and Beetle PSX as black boxes (workloads E3 and
 * E3R: a repeat address written after key on survives later loop start
 * flags; one written before key on gives way to them; one written before key
 * on with no loop start flag in the sample is where the sample loops, as
 * psx-spx "LSAX" describes, on Beetle). */
static void enterBlock(SpuVoice* v) {
    uint16_t h = spu.ram[v->addr & SPU_ADDR_MASK];

    v->header = (uint8_t)h;
    v->decoded = 0;
    if ((h & 0x400) && !v->loopWritten) {
        v->loopAddr = v->addr;
    }
}

/* The counter passed the block's last sample: the next block, or at a loop
 * end flag the repeat address, setting ENDX; "End+Mute" (code 1) also
 * releases the voice with ENVX 0 (psx-spx "Flag Bits"). */
static void leaveBlock(int i) {
    SpuVoice* v = &spu.voices[i];
    uint16_t flags = spu.ram[v->addr & SPU_ADDR_MASK] >> 8;

    decode(v, SPU_BLOCK_SAMPLES - 1);
    for (int k = 0; k < 3; k++) {
        v->window[k] = v->window[SPU_BLOCK_SAMPLES + k];
    }
    if (flags & 1) {
        spu.endx |= 1u << i;
        v->addr = v->loopAddr;
        if (!(flags & 2)) {
            release(v);
            v->level = 0;
        }
    } else {
        v->addr = (v->addr + 8) & SPU_ADDR_MASK;
    }
    enterBlock(v);
}

/* ---- per sample ---- */

/* psx-spx "4-Point Gaussian Interpolation": the four most recent samples
 * weighed by the table at the counter's phase. By default the products are
 * summed and shifted once, as both instruments measure; psx-spx shifts each
 * product (SPU_MEASURED_ARITHMETIC 0, spu.h). */
static int32_t interpolate(const SpuVoice* v) {
    int n = (int)(v->counter >> 12);
    int g = (v->counter >> 4) & 0xFF;
    const int16_t* w = &v->window[n]; /* oldest, older, old, new */

#if SPU_MEASURED_ARITHMETIC
    return (spuGauss[0x0FF - g] * w[0] + spuGauss[0x1FF - g] * w[1] + spuGauss[0x100 + g] * w[2] +
            spuGauss[g] * w[3]) >> 15;
#else
    return ((spuGauss[0x0FF - g] * w[0]) >> 15) + ((spuGauss[0x1FF - g] * w[1]) >> 15) +
           ((spuGauss[0x100 + g] * w[2]) >> 15) + ((spuGauss[g] * w[3]) >> 15);
#endif
}

int32_t spuVoiceSample(int i) {
    SpuVoice* v = &spu.voices[i];
    int n = (int)(v->counter >> 12);
    int32_t x;

    /* This sample's reads: the block's header word and the data word of
     * the newest sample (psx-spx "SPU Internal State Machine", "Voice
     * Interrupt"). */
    spuCheckIrq(v->addr);
    spuCheckIrq((v->addr + 1 + (n >> 2)) & SPU_ADDR_MASK);
    decode(v, n);
    if ((spu.regs.m194_noise[i >> 4] >> (i & 15)) & 1) {
        x = (int16_t)spu.noise;
    } else {
        x = interpolate(v);
    }
    v->out = (x * v->level) >> 15;
    return v->out;
}

static void start(int i);

void spuVoiceAdvance(int i) {
    SpuVoice* v = &spu.voices[i];
    const SpuVoiceRegs* r = &spu.regs.m0_voice[i];
    uint32_t step;

    if (v->keyOnIn > 0 && --v->keyOnIn == 0) {
        start(i);
    }
    step = r->m4_pitch;

    /* psx-spx "Pitch Counter": pitch modulation by the previous voice's
     * output, then the step clipped: to 3FFFh by default, as both instruments
     * measure; to psx-spx's 4000h with SPU_MEASURED_ARITHMETIC 0 (spu.h). */
    if (i > 0 && ((spu.regs.m190_fm[i >> 4] >> (i & 15)) & 1)) {
        int32_t factor = spu.voices[i - 1].out + 0x8000;
        step = (uint32_t)(((int32_t)(int16_t)step * factor) >> 15) & 0xFFFF;
    }
    if (step > 0x3FFF) {
        step = SPU_MEASURED_ARITHMETIC ? 0x3FFF : 0x4000;
    }
    v->counter += step;
    if (v->counter >= SPU_BLOCK_SAMPLES << 12) {
        v->counter -= SPU_BLOCK_SAMPLES << 12;
        leaveBlock(i);
    }

    spuEnvelopeClock(v, r);
    spuSweepClock(&v->volume[0], r->m0_volL);
    spuSweepClock(&v->volume[1], r->m2_volR);
    /* A key off from the driver path (core.c DRIVER_KEY_OFF_DELAY): the
     * voice plays on with its envelope through that many samples after the
     * first one after the latch, then releases, as the port did before this
     * rewrite (the settings menu's sound effects render the same). */
    if (v->keyOffIn >= 0 && v->keyOffIn-- == 0) {
        spuVoiceKeyOff(i);
    }
    /* ATTR bit 15 clear (psx-spx "SPU Enable"): the voices stop, released
     * with ENVX 0. Measured with Beetle PSX as a black box (workload E7: a
     * voice is silent and its ENVX 0 after the SPU is switched off and on
     * again); PCSX-Redux leaves the voices alone. */
    if (!(spu.regs.m1AA_control & 0x8000)) {
        release(v);
        v->level = 0;
    }
}

/* psx-spx "KON", "SSA", "ENDX": the start address becomes the current
 * address, ENDX clears, and the envelope starts its attack from 0. The
 * counter starts at 0 with an empty interpolation history, or (measured,
 * SPU_MEASURED_KEY_ON) with the block's first four samples in the window. */
static void start(int i) {
    SpuVoice* v = &spu.voices[i];

    v->addr = ((uint32_t)spu.regs.m0_voice[i].m6_addr << 2) & SPU_ADDR_MASK;
    v->loopWritten = 0;
    v->counter = SPU_MEASURED_KEY_ON ? 3 << 12 : 0;
    v->history[0] = v->history[1] = 0;
    for (int k = 0; k < 3; k++) {
        v->window[k] = 0;
    }
    enterBlock(v);
    spu.endx &= ~(1u << i);
    v->phase = ENV_ATTACK;
    v->level = 0;
    v->divider = 0;
    if (v->keyOffAtStart) {
        v->keyOffAtStart = 0;
        release(v);
    }
}

/* The delay of a written key on (SPU_MEASURED_KEY_ON): the voice starts in
 * the advance of the fifth sample after the latch. */
#define KEY_ON_DELAY 5

void spuVoiceKeyOn(int i) {
    SpuVoice* v = &spu.voices[i];

    v->keyOffIn = -1; /* a key off from an earlier batch is older */
#if SPU_MEASURED_KEY_ON
    spu.endx &= ~(1u << i);
    v->keyOnIn = KEY_ON_DELAY;
    v->keyOffAtStart = 0;
#else
    start(i);
#endif
}

/* A key off written to the voice (psx-spx "KOFF": release from any phase).
 * While a measured key on waits (SPU_MEASURED_KEY_ON): one written with the
 * key on is dropped, a later one releases the voice as it starts (Beetle
 * PSX, workload E6). */
void spuVoiceKeyOff(int i) {
    SpuVoice* v = &spu.voices[i];

    if (v->keyOnIn == KEY_ON_DELAY) {
        return; /* written with the key on (measured: dropped) */
    }
    if (v->keyOnIn > 0) {
        v->keyOffAtStart = 1;
        return;
    }
    release(v);
}
