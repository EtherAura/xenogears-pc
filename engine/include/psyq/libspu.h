/*
 * LIBSPU (PSY-Q 4.0) types and globals shared by src/main/psyq/libspu.
 * Written from the disassembly; layouts follow the public libspu API
 * (SpuCommonAttr, SpuReverbAttr) and the SPU register map.
 * Globals keep their splat names; suggested library names in comments.
 */
#ifndef PSYQ_LIBSPU_H
#define PSYQ_LIBSPU_H

#include "common.h"

/* One voice's registers (0x10 bytes). */
typedef struct {
    /* 0x0 */ u16 m0_volL;
    /* 0x2 */ u16 m2_volR;
    /* 0x4 */ u16 m4_pitch;
    /* 0x6 */ u16 m6_addr;
    /* 0x8 */ u16 m8_adsr1;
    /* 0xA */ u16 mA_adsr2;
    /* 0xC */ u16 mC_envx;
    /* 0xE */ u16 mE_loopAddr;
} SpuVoiceRegs;

/* SPU registers (0x1F801C00), reached through _spu_RXX (_spu_RXX). */
typedef struct {
    /* 0x000 */ SpuVoiceRegs m0_voice[24];
    /* 0x180 */ u16 m180_mainVolL;
    /* 0x182 */ u16 m182_mainVolR;
    /* 0x184 */ u16 m184_revVolL;
    /* 0x186 */ u16 m186_revVolR;
    /* 0x188 */ u16 m188_keyOn[2];
    /* 0x18C */ u16 m18C_keyOff[2];
    /* 0x190 */ u16 m190_fm[2];
    /* 0x194 */ u16 m194_noise[2];
    /* 0x198 */ u16 m198_reverbOn[2];
    /* 0x19C */ u16 m19C_endx[2];
    /* 0x1A0 */ u16 m1A0;
    /* 0x1A2 */ u16 m1A2_revWorkAddr;
    /* 0x1A4 */ u16 m1A4_irqAddr;
    /* 0x1A6 */ u16 m1A6_transferAddr;
    /* 0x1A8 */ u16 m1A8_transferData;
    /* 0x1AA */ u16 m1AA_control;
    /* 0x1AC */ u16 m1AC_transferCtrl;
    /* 0x1AE */ u16 m1AE_status;
    /* 0x1B0 */ u16 m1B0_cdVolL;
    /* 0x1B2 */ u16 m1B2_cdVolR;
    /* 0x1B4 */ u16 m1B4_extVolL;
    /* 0x1B6 */ u16 m1B6_extVolR;
    /* 0x1B8 */ u16 m1B8_curVol[4];
    /* 0x1C0 */ u16 m1C0_reverb[0x20];
} SpuRegs;

typedef struct {
    /* 0x0 */ s16 m0_left;
    /* 0x2 */ s16 m2_right;
} SpuVolume;

typedef struct {
    /* 0x0 */ SpuVolume m0_volume;
    /* 0x4 */ s32 m4_reverb;
    /* 0x8 */ s32 m8_mix;
} SpuExtAttr;

typedef struct {
    /* 0x00 */ u32 m0_mask;
    /* 0x04 */ SpuVolume m4_mvol;
    /* 0x08 */ SpuVolume m8_mvolmode;
    /* 0x0C */ SpuVolume mC_mvolx;
    /* 0x10 */ SpuExtAttr m10_cd;
    /* 0x1C */ SpuExtAttr m1C_ext;
} SpuCommonAttr;

typedef struct {
    /* 0x00 */ u32 m0_mask;
    /* 0x04 */ s32 m4_mode;
    /* 0x08 */ SpuVolume m8_depth;
    /* 0x0C */ s32 mC_delay;
    /* 0x10 */ s32 m10_feedback;
} SpuReverbAttr;

/* Reverb register block: mask + the 32 reverb registers (0x1C0-0x1FE). */
typedef struct {
    /* 0x00 */ u32 m0_mask;
    /* 0x04 */ u16 m4_regs[0x20];
} SpuReverbParam;

/* Memory-management record (SpuInitMalloc table entry). */
typedef struct {
    /* 0x0 */ u32 m0_addr;
    /* 0x4 */ u32 m4_size;
} SpuMallocRec;

typedef void (*SpuCallbackFunc)(void);

s32 SpuSetNoiseClock(s32 n_clock);

#endif
