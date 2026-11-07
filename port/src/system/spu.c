/*
 * LIBSPU and the SPU's place in the virtual hardware. The SPU itself is
 * port/src/spu (spu.h); this file connects it to the clock and implements
 * every LIBSPU call the game links.
 *
 * The register block the sound driver writes directly is portSpuRegisters
 * (the PS1's 0x1F801C00-0x1F801DFF; sound_data.c points spuRegisters here).
 * spuSync, called at every safe point and after every interrupt (hw.c),
 * runs the SPU up to the virtual time and then latches the register writes
 * made since, so they act from that sample on: the sound driver's 240 Hz tick
 * writes at its own virtual time. The samples go to the host's audio output
 * (port/src/spu/output.c).
 *
 * Transfers copy at once and complete through a scheduled interrupt that runs
 * the transfer callback (the driver chains its queue from it) or, without one,
 * delivers the SPU event.
 *
 * XG_SPU_LOG=1 logs transfers, key on/off and reverb changes to stderr; 2 adds
 * the playing voices once a second.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq/libspu.h"
#include "spu/spu.h"
#include "system.h"
#include "cputime.h"
#include "savestate.h"

s32 OpenEvent(u32 cls, s32 spec, s32 mode, void* func);
s32 CloseEvent(u32 ev);
s32 EnableEvent(u32 ev);
void ResetCallback(void);

#define SPU_RAM_MASK (SPU_RAM_SIZE - 1)
#define MAX_ADVANCE (2 * SPU_RATE) /* samples run at most per sync (after a long stall) */
#define CHUNK 1024                 /* frames per output push */
#define DMA_LATENCY 20e-6          /* transfer timing: an estimate, the game only waits for it */
#define DMA_BYTES_PER_SECOND 16e6

/* The register block; sound_data.c points spuRegisters here. */
_Alignas(16) SpuRegs portSpuRegisters;
#define REGS (&portSpuRegisters)

static int powered;
static int64_t lastSample = -1;

static int started;
static s32 spuEvent;
/* Where the samples go: the audio device (the game's), or a world's sink
 * (world.h), or nowhere; runSpu 0: a world that only keeps time. */
static int toDevice = 1;
static void (*sink)(void* user, const int16_t* frames, uint32_t count);
static void* sinkUser;
static int runSpu = 1;
static u32 transferAddr = 0x1000;
static s32 transferMode;
static SpuCallbackFunc transferCallback;
static SpuCallbackFunc irqCallback;
static SpuReverbAttr reverb;
static SpuMallocRec* mallocTable;
static int logLevel = -1;

static int logging(void) {
    if (logLevel < 0) {
        const char* env = getenv("XG_SPU_LOG");
        logLevel = env != NULL ? atoi(env) : 0;
    }
    return logLevel > 0;
}

/* ---- the clock ---- */

static int64_t lastReport = -1;

/* XG_SPU_LOG=2: once a second, the voices with an envelope. */
static void logVoices(double now) {
    fprintf(stderr, "spu %9.4f: main %04x %04x cnt %04x rev %04x %04x on %06x esa %04x cd %04x %04x\n", now,
            REGS->m180_mainVolL, REGS->m182_mainVolR, REGS->m1AA_control, REGS->m184_revVolL, REGS->m186_revVolR,
            REGS->m198_reverbOn[0] | (u32)REGS->m198_reverbOn[1] << 16, REGS->m1A2_revWorkAddr, REGS->m1B0_cdVolL,
            REGS->m1B2_cdVolR);
    for (int i = 0; i < 24; i++) {
        const SpuVoiceRegs* v = &REGS->m0_voice[i];
        if (v->mC_envx != 0) {
            fprintf(stderr, "  v%-2d env %04x vol %04x %04x pitch %04x addr %05x loop %05x adsr %04x %04x\n", i,
                    v->mC_envx, v->m0_volL, v->m2_volR, v->m4_pitch, v->m6_addr << 3, v->mE_loopAddr << 3,
                    v->m8_adsr1, v->mA_adsr2);
        }
    }
}

static void raiseIrq(void* arg) {
    (void)arg;
    if (irqCallback != NULL) {
        irqCallback();
    }
}

static void powerOn(void) {
    if (!powered) {
        powered = 1;
        spuReset(REGS);
    }
}

void spuSync(double now) {
    int64_t sample = (int64_t)floor(now * SPU_RATE);
    int16_t buffer[CHUNK * 2];
    int irq = 0;

    powerOn();
    if (lastSample < 0) {
        lastSample = sample;
    }
    if (sample > lastSample) {
        int64_t n = sample - lastSample;
        if (n > MAX_ADVANCE) {
            n = MAX_ADVANCE; /* a long stall: the rest is skipped */
        }
        while (n > 0 && runSpu) {
            uint32_t frames = n > CHUNK ? CHUNK : (uint32_t)n;
            CPU_TIME_BEGIN(CPU_SPU);
            irq |= spuRun(buffer, frames);
            CPU_TIME_END(CPU_SPU);
            if (sink != NULL) {
                sink(sinkUser, buffer, frames);
            } else if (toDevice) {
                spuOutputPush(buffer, frames);
            }
            n -= frames;
        }
        lastSample = sample;
    }
    if (logging() && (REGS->m188_keyOn[0] | REGS->m188_keyOn[1] | REGS->m18C_keyOff[0] | REGS->m18C_keyOff[1])) {
        fprintf(stderr, "spu %9.4f: key on %06x off %06x\n", now,
                REGS->m188_keyOn[0] | (u32)REGS->m188_keyOn[1] << 16, REGS->m18C_keyOff[0] | (u32)REGS->m18C_keyOff[1] << 16);
    }
    spuLatchDriver(REGS);
    if (irq) {
        hwScheduleAt(hwNow(), raiseIrq, NULL);
    }
    if (logLevel >= 2 && sample / SPU_RATE != lastReport) {
        lastReport = sample / SPU_RATE;
        logVoices(now);
    }
}

void spuCdSubmit(const int16_t* lr, int frames, int rate) {
    spuSync(hwNow());
    spuCdQueue(lr, frames, rate);
}

/* ---- transfers ---- */

static void transferDone(void* arg) {
    (void)arg;
    if (logging()) {
        fprintf(stderr, "spu %9.4f: transfer done%s\n", hwNow(), transferCallback != NULL ? "" : " (event)");
    }
    REGS->m1AA_control &= ~0x30;
    if (transferCallback != NULL) {
        transferCallback();
    } else {
        DeliverEvent(EV_CLASS_SPU, EV_SPEC_COMP);
    }
}

/* A DMA transfer completes later, through the transfer interrupt; a manual
 * (I/O) write is done when the call returns and calls nothing. An access to
 * the IRQ address raises the SPU interrupt. */
static void completeTransfer(u32 size, int dma, int irq) {
    if (logging()) {
        fprintf(stderr, "spu %9.4f: %s %#x bytes at %#x\n", hwNow(),
                REGS->m1AA_control & 0x10 ? "read" : dma ? "write" : "write (I/O)", size, transferAddr);
    }
    if (irq) {
        hwScheduleAt(hwNow(), raiseIrq, NULL);
    }
    if (dma) {
        hwScheduleAt(hwNow() + DMA_LATENCY + size / DMA_BYTES_PER_SECOND, transferDone, NULL);
    }
}

u32 SpuSetTransferStartAddr(u32 addr) {
    hwSafePoint();
    if (addr < 0x1010 || addr > 0x7FFF8) {
        return 0;
    }
    transferAddr = (addr + 7) & ~7u;
    REGS->m1A6_transferAddr = (u16)(transferAddr >> 3);
    return transferAddr;
}

s32 SpuSetTransferMode(s32 mode) {
    hwSafePoint();
    transferMode = mode == 1 ? 1 : 0;
    return transferMode;
}

SpuCallbackFunc SpuSetTransferCallback(SpuCallbackFunc func) {
    hwSafePoint();
    SpuCallbackFunc old = transferCallback;
    transferCallback = func;
    return old;
}

u32 SpuWrite(u8* addr, u32 size) {
    hwSafePoint();
    powerOn();
    if (size > 0x7EFF0) {
        size = 0x7EFF0;
    }
    int irq = spuRamWrite(transferAddr, addr, size);
    if (transferMode == 0) {
        REGS->m1AA_control = (REGS->m1AA_control & ~0x30) | 0x20;
    }
    completeTransfer(size, transferMode == 0, irq);
    return size;
}

u32 SpuRead(u8* addr, u32 size) {
    hwSafePoint();
    powerOn();
    if (size > 0x7EFF0) {
        size = 0x7EFF0;
    }
    int irq = spuRamRead(transferAddr, addr, size);
    REGS->m1AA_control |= 0x30;
    completeTransfer(size, 1, irq); /* reads are always DMA */
    return size;
}

/* The capture buffers at the start of SPU RAM (CD left/right, voices 1 and
 * 3). Returns which half is being written. */
s32 SpuReadDecodedData(u8* data, s32 flag) {
    hwSafePoint();
    powerOn();
    REGS->m1AA_control |= 0x30;
    switch (flag) {
    case 5: /* SPU_CDONLY */
        completeTransfer(0x800, 1, spuRamRead(0, data, 0x800));
        break;
    case 6: /* SPU_VOICEONLY */
        completeTransfer(0x800, 1, spuRamRead(0x800, data + 0x800, 0x800));
        break;
    default:
        completeTransfer(0x1000, 1, spuRamRead(0, data, 0x1000));
        break;
    }
    return (REGS->m1AE_status & 0x800) != 0;
}

/* ---- interrupt ---- */

s32 SpuSetIRQ(s32 onOff) {
    hwSafePoint();
    if (onOff == 0 || onOff == 3) {
        REGS->m1AA_control &= ~0x40; /* off, and acknowledged */
    }
    if (onOff == 1 || onOff == 3) {
        if (onOff == 3) {
            spuSync(hwNow()); /* the acknowledge reaches the SPU first */
        }
        REGS->m1AA_control |= 0x40;
    }
    return onOff;
}

u32 SpuSetIRQAddr(u32 addr) {
    hwSafePoint();
    REGS->m1A4_irqAddr = (u16)((addr & SPU_RAM_MASK) >> 3);
    return (u32)REGS->m1A4_irqAddr << 3;
}

SpuCallbackFunc SpuSetIRQCallback(SpuCallbackFunc func) {
    hwSafePoint();
    SpuCallbackFunc old = irqCallback;
    irqCallback = func;
    return old;
}

/* ---- init ---- */

/* SpuInit without ResetCallback (LIBETC's, the game's): a world's SPU. */
void spuWorldPowerOn(void) {
    static const u8 silentLoop[16] = {0x00, 0x07}; /* end + repeat + loop start, all nibbles 0 */

    powerOn();
    spuSync(hwNow());
    spuReset(REGS);
    REGS->m1AC_transferCtrl = 4;
    for (int i = 0; i < 24; i++) {
        REGS->m0_voice[i].m4_pitch = 0x3FFF;
        REGS->m0_voice[i].m6_addr = 0x200;
    }
    /* LIBSPU parks every voice on a silent looping block at 0x1000. */
    spuRamWrite(0x1000, silentLoop, sizeof(silentLoop));
    transferAddr = 0x1000;
    REGS->m1A6_transferAddr = 0x200;
    transferMode = 0;
    transferCallback = NULL;
    irqCallback = NULL;
    memset(&reverb, 0, sizeof(reverb));
    REGS->m1A2_revWorkAddr = 0xFFFE; /* reverb off: a 16-byte work area at the top */
    REGS->m1AA_control = 0xC000;     /* SPU on, unmuted */
    REGS->m188_keyOn[0] = 0xFFFF;    /* every voice on the silent block */
    REGS->m188_keyOn[1] = 0xFF;
    spuLatch(REGS);
    REGS->m18C_keyOff[0] = 0xFFFF;
    REGS->m18C_keyOff[1] = 0xFF;
    spuLatch(REGS);
    if (!started) {
        started = 1;
        spuEvent = OpenEvent(EV_CLASS_SPU, EV_SPEC_COMP, 0x2000, NULL);
        EnableEvent((u32)spuEvent);
    }
}

void SpuInit(void) {
    hwSafePoint();
    ResetCallback();
    spuWorldPowerOn();
}

void SpuQuit(void) {
    hwSafePoint();
    if (started) {
        started = 0;
        transferCallback = NULL;
        irqCallback = NULL;
        CloseEvent((u32)spuEvent);
    }
}

s32 SpuInitMalloc(s32 num, SpuMallocRec* top) {
    hwSafePoint();
    if (num <= 0) {
        return 0;
    }
    /* One free block (the RAM above the capture buffers) and the terminator
     * flag, as LIBSPU writes it; the game manages SPU RAM itself. */
    top->m0_addr = 0x40001010;
    top->m4_size = SPU_RAM_SIZE - 0x1010;
    mallocTable = top;
    return num;
}

/* ---- common attributes ---- */

static u16 masterVolume(s16 volume, s16 mode) {
    static const u16 sweep[8] = {0, 0x8000, 0x9000, 0xA000, 0xB000, 0xC000, 0xD000, 0xE000};

    if (mode <= 0 || mode >= 8) {
        return (u16)volume & 0x7FFF;
    }
    /* Sweep modes take a 0..0x7F rate. */
    s16 rate = volume >= 0x80 ? 0x7F : volume < 0 ? 0 : volume;
    return (u16)(rate | sweep[mode]);
}

static void setControl(u16 bit, int on) {
    if (on) {
        REGS->m1AA_control |= bit;
    } else {
        REGS->m1AA_control &= ~bit;
    }
}

void SpuSetCommonAttr(SpuCommonAttr* attr) {
    hwSafePoint();
    u32 mask = attr->m0_mask;
    int all = mask == 0;

    if (all || (mask & 1)) {
        REGS->m180_mainVolL = masterVolume(attr->m4_mvol.m0_left, (all || (mask & 4)) ? attr->m8_mvolmode.m0_left : 0);
    }
    if (all || (mask & 2)) {
        REGS->m182_mainVolR = masterVolume(attr->m4_mvol.m2_right, (all || (mask & 8)) ? attr->m8_mvolmode.m2_right : 0);
    }
    if (all || (mask & 0x40)) {
        REGS->m1B0_cdVolL = (u16)attr->m10_cd.m0_volume.m0_left;
    }
    if (all || (mask & 0x80)) {
        REGS->m1B2_cdVolR = (u16)attr->m10_cd.m0_volume.m2_right;
    }
    if (all || (mask & 0x400)) {
        REGS->m1B4_extVolL = (u16)attr->m1C_ext.m0_volume.m0_left;
    }
    if (all || (mask & 0x800)) {
        REGS->m1B6_extVolR = (u16)attr->m1C_ext.m0_volume.m2_right;
    }
    if (all || (mask & 0x100)) {
        setControl(0x4, attr->m10_cd.m4_reverb != 0);
    }
    if (all || (mask & 0x200)) {
        setControl(0x1, attr->m10_cd.m8_mix != 0);
    }
    if (all || (mask & 0x1000)) {
        setControl(0x8, attr->m1C_ext.m4_reverb != 0);
    }
    if (all || (mask & 0x2000)) {
        setControl(0x2, attr->m1C_ext.m8_mix != 0);
    }
}

s32 SpuSetNoiseClock(s32 clock) {
    hwSafePoint();
    clock = clock < 0 ? 0 : clock > 0x3F ? 0x3F : clock;
    REGS->m1AA_control = (u16)((REGS->m1AA_control & 0xC0FF) | (clock << 8));
    return clock;
}

/* The voice's envelope level, and its key state as LIBSPU tracks it: from
 * SpuSetKey only, which this game never calls (it writes the key registers
 * itself), so the state is "off" with or without envelope. */
s32 SpuGetVoiceEnvelopeAttr(s32 voice, s32* keyStat, s16* envx) {
    hwSafePoint();
    s16 env = (s16)((const u16*)REGS)[((voice & 0x1F) << 3) | 6]; /* ENVX, as LIBSPU indexes it */

    *envx = env;
    *keyStat = env > 0 ? 2 : 0; /* SPU_OFF_ENV_ON : SPU_OFF */
    return 0;
}

/* ---- reverb ---- */

/* The LIBSPU reverb presets: work area size and the 32 reverb registers
 * (0x1F801DC0..0x1F801DFE), as listed in psx-spx "SPU Reverb Examples";
 * LIBSPU's "off" preset is all zeros. Modes: off, room, studio small, studio
 * medium, studio large, hall, space echo, echo, delay, pipe (half echo). */
static const struct {
    u32 size;
    u16 regs[32];
} reverbPresets[10] = {
    {0x10, {0}},
    {0x26C0, {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6,
              0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF, 0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000,
              0x0000, 0x0000, 0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000}},
    {0x1F40, {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4,
              0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266, 0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135,
              0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000}},
    {0x4840, {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904,
              0x076B, 0x0824, 0x065F, 0x07A2, 0x0616, 0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305,
              0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000}},
    {0x6FE0, {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB,
              0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973, 0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0,
              0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000}},
    {0xADE0, {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA,
              0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1,
              0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000}},
    {0xF6C0, {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6,
              0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D,
              0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000}},
    {0x18040, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFF,
               0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000,
               0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
    {0x18040, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFF,
               0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000,
               0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
    {0x3C00, {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371,
              0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7, 0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1,
              0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000}},
};

static u16 reverbWorkStart(s32 mode) {
    return (u16)((SPU_RAM_SIZE - reverbPresets[mode].size) >> 3);
}

s32 SpuSetReverbModeType(s32 mode) {
    hwSafePoint();
    int clear = (mode & 0x100) != 0;

    mode &= ~0x100;
    if ((u32)mode >= 10) {
        return -1;
    }
    reverb.m4_mode = mode;
    reverb.mC_delay = mode == 7 || mode == 8 ? 0x7F : 0;
    reverb.m10_feedback = mode == 7 ? 0x7F : 0;
    reverb.m8_depth.m0_left = 0;
    reverb.m8_depth.m2_right = 0;
    REGS->m184_revVolL = 0;
    REGS->m186_revVolR = 0;
    memcpy(REGS->m1C0_reverb, reverbPresets[mode].regs, sizeof(REGS->m1C0_reverb));
    if (clear) {
        spuRamFill((u32)reverbWorkStart(mode) << 3, 0, reverbPresets[mode].size);
    }
    REGS->m1A2_revWorkAddr = reverbWorkStart(mode);
    if (logging()) {
        fprintf(stderr, "spu %9.4f: reverb mode %d\n", hwNow(), mode);
    }
    return 0;
}

void SpuSetReverbModeDepth(s32 left, s32 right) {
    hwSafePoint();
    REGS->m184_revVolL = (u16)left;
    REGS->m186_revVolR = (u16)right;
    reverb.m8_depth.m0_left = (s16)left;
    reverb.m8_depth.m2_right = (s16)right;
}

/* Echo and delay only: the delay (0..127) moves the same-side reflection and
 * comb/APF addresses, as LIBSPU computes them from the preset. */
void SpuSetReverbModeDelayTime(s32 delay) {
    hwSafePoint();
    const u16* p;
    u16* r = REGS->m1C0_reverb;

    if (reverb.m4_mode != 7 && reverb.m4_mode != 8) {
        return;
    }
    p = reverbPresets[reverb.m4_mode].regs;
    reverb.mC_delay = delay;
    s32 d13 = (delay << 13) / 127;
    s32 d12 = (delay << 12) / 127;
    r[10] = (u16)(d13 - p[0]);  /* mLSAME */
    r[11] = (u16)(d12 - p[1]);  /* mRSAME */
    r[12] = (u16)(p[13] + d12); /* mLCOMB1 */
    r[16] = (u16)(p[17] + d12); /* dLSAME */
    r[26] = (u16)(p[28] + d12); /* mLAPF1 */
    r[27] = (u16)(p[29] + d12); /* mRAPF1 */
}

/* Echo and delay only: the feedback (0..127) sets the wall reflection volume. */
void SpuSetReverbModeFeedback(s32 feedback) {
    hwSafePoint();
    if (reverb.m4_mode != 7 && reverb.m4_mode != 8) {
        return;
    }
    reverb.m10_feedback = feedback;
    REGS->m1C0_reverb[7] = (u16)((feedback * 0x8100) / 127); /* vWALL */
}

s32 SpuSetReverb(s32 onOff) {
    hwSafePoint();
    /* The work area is never inside a SpuMalloc block (the game does not use
     * SpuMalloc), so "on" always succeeds. */
    if (onOff == 0 || onOff == 1) {
        setControl(0x80, onOff);
    }
    return (REGS->m1AA_control & 0x80) != 0;
}

/* LIBSPU SpuGetReverbModeType, a small asm function in retail
 * (asm/us/main/3EF74.s). */
void SpuGetReverbModeType(s32* mode) {
    hwSafePoint();
    *mode = reverb.m4_mode;
}

/* ---- a world of its own (world.h) ---- */

/* What a world has of this file: LIBSPU's state and callbacks, the SPU's
 * sync clock, and where its samples go (the world's sink, never the device). */
typedef struct {
    int powered;
    int64_t lastSample;
    int started;
    s32 spuEvent;
    u32 transferAddr;
    s32 transferMode;
    SpuCallbackFunc transferCallback;
    SpuCallbackFunc irqCallback;
    SpuReverbAttr reverb;
    SpuMallocRec* mallocTable;
    int64_t lastReport;
    int toDevice;
    void (*sink)(void* user, const int16_t* frames, uint32_t count);
    void* sinkUser;
    int runSpu;
} SpuPart;

size_t spuWorldPartSize(void) {
    return sizeof(SpuPart);
}

void spuWorldPartInit(void* blob) {
    SpuPart* p = blob;
    memset(p, 0, sizeof(*p));
    p->lastSample = -1;
    p->transferAddr = 0x1000;
    p->lastReport = -1;
    p->runSpu = 1;
}

void spuWorldPartSwap(void* blob) {
    SpuPart* p = blob;
    SpuPart mine = {powered,   lastSample, started,    spuEvent, transferAddr, transferMode, transferCallback, irqCallback,
                    reverb,    mallocTable, lastReport, toDevice, sink,         sinkUser,     runSpu};
    powered = p->powered;
    lastSample = p->lastSample;
    started = p->started;
    spuEvent = p->spuEvent;
    transferAddr = p->transferAddr;
    transferMode = p->transferMode;
    transferCallback = p->transferCallback;
    irqCallback = p->irqCallback;
    reverb = p->reverb;
    mallocTable = p->mallocTable;
    lastReport = p->lastReport;
    toDevice = p->toDevice;
    sink = p->sink;
    sinkUser = p->sinkUser;
    runSpu = p->runSpu;
    *p = mine;
}

void spuWorldSetSink(void (*fn)(void* user, const int16_t* frames, uint32_t count), void* user) {
    sink = fn;
    sinkUser = user;
}

void spuWorldSetRun(int on) {
    runSpu = on != 0;
}

/* ---- save states (savestate.h) ---- */

/* The register block the driver writes (writes not yet taken too), the
 * SPU's last sync (a state is taken right after one: lastSample is the
 * restored clock's sample), LIBSPU's transfer, IRQ and reverb state. Where
 * the samples go (the sink) is this process's. */
/* The 60 Hz Battling's sandbox (port/src/game/battling/between.c): the
 * previous frame's render run again may write voice registers (volumes,
 * pitches) the live frame already set; they are kept before and put back
 * after. Under the clock hold the mixer produces nothing in between. */
static SpuRegs keptRegs;
void spuRegsKeep(void) {
    spuSync(hwNow());
    keptRegs = *REGS;
}
void spuRegsPutBack(void) {
    spuSync(hwNow());
    *REGS = keptRegs;
}

void spuStateIo(XgState* s) {
    XG_STATE_VAR(s, portSpuRegisters);
    XG_STATE_VAR(s, powered);
    XG_STATE_VAR(s, lastSample);
    XG_STATE_VAR(s, started);
    XG_STATE_VAR(s, spuEvent);
    XG_STATE_VAR(s, transferAddr);
    XG_STATE_VAR(s, transferMode);
    XG_STATE_VAR(s, transferCallback);
    XG_STATE_VAR(s, irqCallback);
    XG_STATE_VAR(s, reverb);
    XG_STATE_VAR(s, mallocTable);
}
