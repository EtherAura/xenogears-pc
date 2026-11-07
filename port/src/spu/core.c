/*
 * The SPU core (spu.h): register latching, the per-sample mix, the capture
 * buffers, the noise generator, the IRQ and SPU RAM access.
 */

#include <stddef.h>
#include <string.h>

#include "spu_internal.h"
#include "../system/savestate.h"

/* The sound driver writes key on at the start of its tick and key off at
 * the end, which the PC runs in one instant: key offs written through the
 * driver path act this many samples after they are seen, the tick's length
 * on the PS1 (about 0.23 ms during field music, measured in PCSX-Redux), so a
 * note keyed on and off in the same tick still sounds briefly. A port
 * approximation, not a property of the SPU. */
#define DRIVER_KEY_OFF_DELAY 10

SpuState spu;

static uint32_t reg32(const uint16_t r[2]) {
    return (uint32_t)r[0] | (uint32_t)r[1] << 16;
}

/* ---- IRQ ---- */

/* spuCheckIrq with the interrupt enabled (SPUCNT bit 6): an access to the
 * 8-byte unit at the IRQ address sets the flag (SPUSTAT bit 6) until the game
 * acknowledges it by clearing SPUCNT bit 6 (psx-spx "SPU Interrupt", "ATTR"). */
void spuCheckIrqSlow(uint32_t halfword) {
    if (!spu.irq && ((halfword >> 2) & 0xFFFF) == spu.regs.m1A4_irqAddr) {
        spu.irq = 1;
        spu.irqRaised = 1;
    }
}

static int checkIrqRange(uint32_t addr, uint32_t size) {
    uint32_t irq = ((uint32_t)spu.regs.m1A4_irqAddr << 3) & (SPU_RAM_SIZE - 1);

    if (!(spu.regs.m1AA_control & 0x40) || spu.irq) {
        return 0;
    }
    if (((irq - addr) & (SPU_RAM_SIZE - 1)) < size) {
        spu.irq = 1;
        return 1;
    }
    return 0;
}

/* ---- SPU RAM ---- */

int spuRamWrite(uint32_t addr, const void* src, uint32_t size) {
    uint8_t* ram = (uint8_t*)spu.ram;
    const uint8_t* s = src;

    addr &= SPU_RAM_SIZE - 1;
    for (uint32_t done = 0; done < size;) {
        uint32_t a = (addr + done) & (SPU_RAM_SIZE - 1);
        uint32_t n = SPU_RAM_SIZE - a;
        if (n > size - done) {
            n = size - done;
        }
        memcpy(&ram[a], s + done, n);
        done += n;
    }
    return checkIrqRange(addr, size);
}

int spuRamRead(uint32_t addr, void* dst, uint32_t size) {
    const uint8_t* ram = (const uint8_t*)spu.ram;
    uint8_t* d = dst;

    addr &= SPU_RAM_SIZE - 1;
    for (uint32_t done = 0; done < size;) {
        uint32_t a = (addr + done) & (SPU_RAM_SIZE - 1);
        uint32_t n = SPU_RAM_SIZE - a;
        if (n > size - done) {
            n = size - done;
        }
        memcpy(d + done, &ram[a], n);
        done += n;
    }
    return checkIrqRange(addr, size);
}

int spuRamFill(uint32_t addr, uint8_t value, uint32_t size) {
    uint8_t* ram = (uint8_t*)spu.ram;

    addr &= SPU_RAM_SIZE - 1;
    for (uint32_t done = 0; done < size;) {
        uint32_t a = (addr + done) & (SPU_RAM_SIZE - 1);
        uint32_t n = SPU_RAM_SIZE - a;
        if (n > size - done) {
            n = size - done;
        }
        memset(&ram[a], value, n);
        done += n;
    }
    return checkIrqRange(addr, size);
}

/* ---- registers ---- */

void spuReset(SpuRegs* regs) {
    /* Everything but SPU RAM, which keeps its contents (zero at startup). */
    memset(&spu.regs, 0, sizeof(spu) - offsetof(SpuState, regs));
    memset(regs, 0, sizeof(*regs));
    for (int i = 0; i < 24; i++) {
        spu.voices[i].phase = ENV_RELEASE;
        spu.voices[i].keyOffIn = -1;
    }
    spuReverbReset();
}

/* Write back what the SPU updates itself, into both the live and the latched
 * block, so the next latch sees only the game's writes. */
static void writeBack(SpuRegs* regs) {
    for (int i = 0; i < 24; i++) {
        uint16_t envx = (uint16_t)spu.voices[i].level;
        uint16_t loop = (uint16_t)(spu.voices[i].loopAddr >> 2);
        regs->m0_voice[i].mC_envx = spu.regs.m0_voice[i].mC_envx = envx;
        regs->m0_voice[i].mE_loopAddr = spu.regs.m0_voice[i].mE_loopAddr = loop;
    }
    regs->m19C_endx[0] = spu.regs.m19C_endx[0] = (uint16_t)spu.endx;
    regs->m19C_endx[1] = spu.regs.m19C_endx[1] = (uint16_t)(spu.endx >> 16);
    uint16_t stat = (uint16_t)((spu.regs.m1AA_control & 0x3F) | (spu.irq ? 0x40 : 0) |
                               ((spu.captureIndex & 0x100) ? 0x800 : 0));
    regs->m1AE_status = spu.regs.m1AE_status = stat;
    for (int lr = 0; lr < 2; lr++) {
        regs->m1B8_curVol[lr] = spu.regs.m1B8_curVol[lr] = (uint16_t)spu.mainVolume[lr].level;
    }
}

static void latch(SpuRegs* regs, int keyOffDelay) {
    SpuRegs* old = &spu.regs;
    uint32_t keyOn = reg32(regs->m188_keyOn);
    uint32_t keyOff = reg32(regs->m18C_keyOff);

    /* Called at every safe point: most of the time nothing ran and nothing
     * was written. */
    if (spu.clock == spu.latchClock && memcmp(regs, old, sizeof(*old)) == 0) {
        return;
    }
    spu.latchClock = spu.clock;

    /* Registers the SPU updates: a value that differs from the one written
     * back is the game's. */
    for (int i = 0; i < 24; i++) {
        SpuVoice* v = &spu.voices[i];
        if (regs->m0_voice[i].mC_envx != old->m0_voice[i].mC_envx) {
            v->level = (int16_t)regs->m0_voice[i].mC_envx;
        }
        if (regs->m0_voice[i].mE_loopAddr != old->m0_voice[i].mE_loopAddr) {
            v->loopAddr = ((uint32_t)regs->m0_voice[i].mE_loopAddr << 2) & SPU_ADDR_MASK;
            v->loopWritten = 1;
        }
    }
    if (regs->m1A2_revWorkAddr != old->m1A2_revWorkAddr) {
        spu.reverbAddr = ((uint32_t)regs->m1A2_revWorkAddr << 2) & SPU_ADDR_MASK;
    }
    if (reg32(regs->m19C_endx) != reg32(old->m19C_endx)) {
        spu.endx = reg32(regs->m19C_endx) & 0xFFFFFF;
    }
    if (!(regs->m1AA_control & 0x40)) {
        spu.irq = 0; /* acknowledged */
    }

    /* Everything else is read as it is. The key registers are write-only:
     * a value there is a new write, consumed here. */
    memcpy(old, regs, sizeof(*old));
    regs->m188_keyOn[0] = regs->m188_keyOn[1] = 0;
    regs->m18C_keyOff[0] = regs->m18C_keyOff[1] = 0;
    old->m188_keyOn[0] = old->m188_keyOn[1] = 0;
    old->m18C_keyOff[0] = old->m18C_keyOff[1] = 0;
    for (int i = 0; i < 24; i++) {
        uint32_t bit = 1u << i;
        if (keyOn & bit) {
            spuVoiceKeyOn(i);
        }
        if (keyOff & bit) {
            if (keyOffDelay > 0) {
                spu.voices[i].keyOffIn = keyOffDelay;
            } else {
                spuVoiceKeyOff(i);
            }
        }
    }
    writeBack(regs);
}

void spuLatch(SpuRegs* regs) {
    latch(regs, 0);
}

void spuLatchDriver(SpuRegs* regs) {
    latch(regs, DRIVER_KEY_OFF_DELAY);
}

int16_t spuVoiceEnvelope(int voice) {
    return voice >= 0 && voice < 24 ? (int16_t)spu.voices[voice].level : 0;
}

/* ---- noise ---- */

/* psx-spx "SPU Noise Generator": SPUCNT bits 8-13 (shift << 2 | step), the
 * timer formula and the parity taps as written (SPEC SPU-E option a). The
 * instruments disagree with it and with each other on the cadence (workloads
 * E5, E5R: Beetle PSX changes the level at power-of-two intervals in a
 * pattern that averages psx-spx's rate, PCSX-Redux at power-of-two intervals
 * that ignore the step); the average rate is the same. */
static void noiseClock(void) {
    int shift = (spu.regs.m1AA_control >> 10) & 0xF;
    int step = 4 + ((spu.regs.m1AA_control >> 8) & 3);
    uint16_t n = spu.noise;

    spu.noiseTimer -= step;
    if (spu.noiseTimer < 0) {
        int parity = ((n >> 15) ^ (n >> 12) ^ (n >> 11) ^ (n >> 10) ^ 1) & 1;
        spu.noise = (uint16_t)(n << 1 | parity);
        spu.noiseTimer += 0x20000 >> shift;
        if (spu.noiseTimer < 0) {
            spu.noiseTimer += 0x20000 >> shift;
        }
    }
}

/* ---- the sample clock ---- */

/* Halfword addresses of the capture buffers (psx-spx "SPU Memory layout"):
 * CD left and right before the SPU's CD volume, voices 1 and 3 after their
 * envelope, 0x200 samples each. */
enum { CAPTURE_CD_L = 0x000, CAPTURE_CD_R = 0x200, CAPTURE_VOICE1 = 0x400, CAPTURE_VOICE3 = 0x600 };

static void capture(uint32_t buffer, int32_t value) {
    uint32_t a = buffer + spu.captureIndex;

    /* psx-spx "Capture Interrupt": no capture IRQs with SPU RAM control
     * bits 2-3 both zero. */
    if (spu.regs.m1AC_transferCtrl & 0xC) {
        spuCheckIrq(a);
    }
    spu.ram[a] = (uint16_t)spuClamp16(value);
}

static int bit(const uint16_t r[2], int i) {
    return (r[i >> 4] >> (i & 15)) & 1;
}

/* One voice into the dry mix and the reverb input. */
static void voice(int i, int32_t dry[2], int32_t wet[2]) {
    int32_t x = spuVoiceSample(i);
    int32_t l = (x * spu.voices[i].volume[0].level) >> 15;
    int32_t r = (x * spu.voices[i].volume[1].level) >> 15;

    spuVoiceAdvance(i);
    dry[0] += l;
    dry[1] += r;
    if (bit(spu.regs.m198_reverbOn, i)) {
        wet[0] += l;
        wet[1] += r;
    }
}

/*
 * One 44.1 kHz sample: the voices in order (a voice's pitch modulation uses
 * the output of the voice before it in the same sample, psx-spx "Pitch
 * Counter"), the capture buffers, the reverb, the mix. psx-spx "SPU Internal
 * State Machine" gives the order of the SPU RAM accesses within a sample
 * period; within one sample here everything reads this sample's voice
 * outputs, so that order has no visible effect, and the reverb input is the
 * sum of this sample's voices. The capture buffers hold voices 1 and 3
 * counted from 0, as "SPU Memory layout" numbers them (measured on Beetle PSX
 * as a black box, workload E1: the second and fourth voices).
 *
 * Then the mix (psx-spx "SPU Reverb Formula", "ATTR", volumes): each voice's
 * output times its left/right volume to the dry mix, and to the reverb input
 * when its EON bit is set; the CD input (always captured) times the CD
 * volume, into the mix with ATTR bit 0 and into the reverb input with bit 2;
 * the reverb output times the reverb output volume; then the main volume.
 * ATTR bit 14 clear mutes the voices, not the CD input. `game` 0: no CD
 * input and no capture buffers (an SPU of the extras, port/src/extras).
 */
static void mix(int16_t* out, int game) {
    uint16_t cnt = spu.regs.m1AA_control;
    int32_t dry[2] = {0, 0};
    int32_t wet[2] = {0, 0};
    int32_t cd[2] = {0, 0};
    int32_t rev[2];

    noiseClock();
    for (int i = 0; i < 24; i++) {
        voice(i, dry, wet);
    }
    if (game) {
        spuCdNext(cd);
        capture(CAPTURE_CD_L, cd[0]);
        capture(CAPTURE_CD_R, cd[1]);
        capture(CAPTURE_VOICE1, spu.voices[1].out);
        capture(CAPTURE_VOICE3, spu.voices[3].out);
        spu.captureIndex = (spu.captureIndex + 1) & 0x1FF;
    }
    if (!(cnt & 0x4000) || !(cnt & 0x8000)) {
        dry[0] = dry[1] = wet[0] = wet[1] = 0;
    }
    for (int lr = 0; lr < 2; lr++) {
        int32_t c = (cd[lr] * (int16_t)(lr ? spu.regs.m1B2_cdVolR : spu.regs.m1B0_cdVolL)) >> 15;
        if (cnt & 1) {
            dry[lr] += c;
        }
        if (cnt & 4) {
            wet[lr] += c;
        }
        wet[lr] = spuClamp16(wet[lr]);
    }
    spuReverbClock(wet, rev);
    for (int lr = 0; lr < 2; lr++) {
        int32_t v = dry[lr] + ((rev[lr] * (int16_t)(lr ? spu.regs.m186_revVolR : spu.regs.m184_revVolL)) >> 15);
        v = (spuClamp16(v) * spu.mainVolume[lr].level) >> 15;
        spuSweepClock(&spu.mainVolume[lr], lr ? spu.regs.m182_mainVolR : spu.regs.m180_mainVolL);
        if (out != NULL) {
            out[lr] = (int16_t)spuClamp16(v);
        }
    }
    spu.clock++;
}

static void sampleClock(int16_t* out) {
    mix(out, 1);
}

void spuRunBox(int16_t* out, uint32_t frames) {
    for (uint32_t n = 0; n < frames; n++) {
        mix(out != NULL ? out + 2 * n : NULL, 0);
    }
}

int spuRun(int16_t* out, uint32_t frames) {
    int raised;

    for (uint32_t n = 0; n < frames; n++) {
        sampleClock(out != NULL ? out + 2 * n : NULL);
    }
    raised = spu.irqRaised;
    spu.irqRaised = 0;
    return raised;
}

/* Save states (system/savestate.h): RAM, voices, reverb, the clock. */
void spuCoreStateIo(XgState* s) {
    XG_STATE_VAR(s, spu);
}
