/*
 * The sound driver's tables: main's .data 0x80050624-0x80050BF0 (JP
 * 0x8004FCC4-0x80050290), data only. pitchTable2 (0x1800 bytes, the rest of
 * this object's .data up to the sin/cos table) stays an assembly data piece:
 * it is a generated 2^(n/3072) pitch curve, game data rather than code.
 *
 * Owner evidence (agent DD): one object owns all of these. Its users
 * interleave against link order (opcode/LFO tables: sequencer.c; reverb
 * sizes: sound.c; built-in SED/WDS and note tables: sequencer.c; SPU base:
 * both), and the 4-byte SPU base pointer spuRegisters sits in .data, so the
 * owner was compiled with cc1 -G0. sound.c and sequencer.c are cc1 -G8
 * (setCdMixVolume and waitForMusic match only with -G8), so neither can
 * define it; asm 281B4's words come just before (0x80050618) and asm
 * 2FF38's sin/cos table just after. battle_env.c (the battle loader) would
 * give the same bytes, but nothing ties these tables to it.
 * Build: cc1 -G0 (GCC 2.6.0, like the driver), ASPSX 2.34.
 */
#include "common.h"
#include "main/sound.h"
#include "psyq/libspu.h"

typedef u8* (*SeqOpcode)(u8* p, SoundInstance* inst, SoundTrack* ch);

u8* seqOP_0_restAndNoteOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_1_hold(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_unused(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_A_nop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_D_selectLoopPoint(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_E_skip3(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_F_nop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_10_end(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_11_infiniteLoop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_14_setOctave(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_15_incOctave(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_16_decOctave(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_17_timeSignature(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_18_loopStart(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_19_loopEnd(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_1A_loopBreak(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_1C_playSoundEffect(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_1D_stopSoundEffect(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_1E_jumpToTrack(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_20_setTempo(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_21_adjustTempo(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_22_tempoSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_24_setInstanceVar(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_25_adjustInstanceVar(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_26_setMasterVolume(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_27_masterVolumeSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_29_setTranspose(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_2A_changeVoiceChannel(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_2C_setAdsr(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_2D_adjustNoteLength(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_2E_percussionOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_2F_percussionOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_30_legatoOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_31_legatoOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_32_pitchModOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_33_pitchModOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_34_setNoiseClock(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_35_adjustNoiseClock(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_36_noiseModeOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_37_noiseModeOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_38_setupReverb(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_3A_reverbOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_3B_reverbOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_3C_skip3(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_3D_nop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_3E_nop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_40_ADSRReset(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_41_setAdsrModes(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_42_setAttack(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_43_setDecay(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_44_setSustain(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_45_setRelease(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_46_setSustainLevel(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_47_setDecayAndSustainLevel(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_48_setAttackMode(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_49_setSustainMode(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_4A_setReleaseMode(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_50_setPitchBend(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_51_adjustPitchBend(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_52_adjustPitchBendFine(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_53_adjustPitchBend16(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_54_pitchSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_55_toggleEndlessPitchSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_56_portamento(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_57_pitchLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_58_pitchLFO(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_59_pitchLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_5A_pitchLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_5B_pitchLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_5C_pitchSlideOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_60_setVolume(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_61_adjustVolume(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_62_volumeSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_63_volumeLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_64_volumeLFO(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_65_volumeLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_66_volumeLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_67_volumeLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_68_setPan(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_69_adjustPan(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6A_panSlide(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6B_panLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6C_panLFO(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6D_panLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6E_panLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_6F_panLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_70_LFOSelect(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_71_LFOUpdate(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_72_LFODelayAndFade(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_75_nop(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_76_LFORestart(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_77_LFOOff(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_78_volumeSlideMulti(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_79_setMeasure(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_7C_changeWdsAndADSR(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_7D_setTempoScale(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_7E_changeBank(u8* p, SoundInstance* inst, SoundTrack* ch);
u8* seqOP_7F_waitRelease(u8* p, SoundInstance* inst, SoundTrack* ch);

s32 stepLfoSquare(SoundLfo* lfo);
s32 stepLfoSquareBipolar(SoundLfo* lfo);
s32 stepLfoTriangle(SoundLfo* lfo);
s32 stepLfoTriangleBipolar(SoundLfo* lfo);
s32 stepLfoSawtooth(SoundLfo* lfo);
s32 stepLfoRandom(SoundLfo* lfo);
s32 stepLfoRandomBipolar(SoundLfo* lfo);
s32 stepLfoDisabled(SoundLfo* lfo);

/* Sequence opcode handlers, opcodes 0x80-0xFF (executeSequenceEvents). */
SeqOpcode seqOpcodes[0x80] = {
    /* 0x80 */ seqOP_0_restAndNoteOff, seqOP_1_hold, seqOP_unused, seqOP_unused,
    /* 0x84 */ seqOP_unused, seqOP_unused, seqOP_unused, seqOP_unused,
    /* 0x88 */ seqOP_unused, seqOP_unused, seqOP_A_nop, seqOP_unused,
    /* 0x8C */ seqOP_unused, seqOP_D_selectLoopPoint, seqOP_E_skip3, seqOP_F_nop,
    /* 0x90 */ seqOP_10_end, seqOP_11_infiniteLoop, seqOP_unused, seqOP_unused,
    /* 0x94 */ seqOP_14_setOctave, seqOP_15_incOctave, seqOP_16_decOctave, seqOP_17_timeSignature,
    /* 0x98 */ seqOP_18_loopStart, seqOP_19_loopEnd, seqOP_1A_loopBreak, seqOP_unused,
    /* 0x9C */ seqOP_1C_playSoundEffect, seqOP_1D_stopSoundEffect, seqOP_1E_jumpToTrack, seqOP_unused,
    /* 0xA0 */ seqOP_20_setTempo, seqOP_21_adjustTempo, seqOP_22_tempoSlide, seqOP_unused,
    /* 0xA4 */ seqOP_24_setInstanceVar, seqOP_25_adjustInstanceVar, seqOP_26_setMasterVolume, seqOP_27_masterVolumeSlide,
    /* 0xA8 */ seqOP_unused, seqOP_29_setTranspose, seqOP_2A_changeVoiceChannel, seqOP_unused,
    /* 0xAC */ seqOP_2C_setAdsr, seqOP_2D_adjustNoteLength, seqOP_2E_percussionOn, seqOP_2F_percussionOff,
    /* 0xB0 */ seqOP_30_legatoOn, seqOP_31_legatoOff, seqOP_32_pitchModOn, seqOP_33_pitchModOff,
    /* 0xB4 */ seqOP_34_setNoiseClock, seqOP_35_adjustNoiseClock, seqOP_36_noiseModeOn, seqOP_37_noiseModeOff,
    /* 0xB8 */ seqOP_38_setupReverb, seqOP_unused, seqOP_3A_reverbOn, seqOP_3B_reverbOff,
    /* 0xBC */ seqOP_3C_skip3, seqOP_3D_nop, seqOP_3E_nop, seqOP_unused,
    /* 0xC0 */ seqOP_40_ADSRReset, seqOP_41_setAdsrModes, seqOP_42_setAttack, seqOP_43_setDecay,
    /* 0xC4 */ seqOP_44_setSustain, seqOP_45_setRelease, seqOP_46_setSustainLevel, seqOP_47_setDecayAndSustainLevel,
    /* 0xC8 */ seqOP_48_setAttackMode, seqOP_49_setSustainMode, seqOP_4A_setReleaseMode, seqOP_unused,
    /* 0xCC */ seqOP_unused, seqOP_unused, seqOP_unused, seqOP_unused,
    /* 0xD0 */ seqOP_50_setPitchBend, seqOP_51_adjustPitchBend, seqOP_52_adjustPitchBendFine, seqOP_53_adjustPitchBend16,
    /* 0xD4 */ seqOP_54_pitchSlide, seqOP_55_toggleEndlessPitchSlide, seqOP_56_portamento, seqOP_57_pitchLFOSpeed,
    /* 0xD8 */ seqOP_58_pitchLFO, seqOP_59_pitchLFOWaveform, seqOP_5A_pitchLFOOn, seqOP_5B_pitchLFOOff,
    /* 0xDC */ seqOP_5C_pitchSlideOff, seqOP_unused, seqOP_unused, seqOP_unused,
    /* 0xE0 */ seqOP_60_setVolume, seqOP_61_adjustVolume, seqOP_62_volumeSlide, seqOP_63_volumeLFOSpeed,
    /* 0xE4 */ seqOP_64_volumeLFO, seqOP_65_volumeLFOWaveform, seqOP_66_volumeLFOOn, seqOP_67_volumeLFOOff,
    /* 0xE8 */ seqOP_68_setPan, seqOP_69_adjustPan, seqOP_6A_panSlide, seqOP_6B_panLFOSpeed,
    /* 0xEC */ seqOP_6C_panLFO, seqOP_6D_panLFOWaveform, seqOP_6E_panLFOOn, seqOP_6F_panLFOOff,
    /* 0xF0 */ seqOP_70_LFOSelect, seqOP_71_LFOUpdate, seqOP_72_LFODelayAndFade, seqOP_unused,
    /* 0xF4 */ seqOP_unused, seqOP_75_nop, seqOP_76_LFORestart, seqOP_77_LFOOff,
    /* 0xF8 */ seqOP_78_volumeSlideMulti, seqOP_79_setMeasure, seqOP_unused, seqOP_unused,
    /* 0xFC */ seqOP_7C_changeWdsAndADSR, seqOP_7D_setTempoScale, seqOP_7E_changeBank, seqOP_7F_waitRelease,
};

/* Operand bytes per opcode 0x80-0xFF (for scanning past events). */
u8 opcodeSizes[0x80] = {
    /* 0x80 */ 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 4, 1,
    /* 0x90 */ 1, 1, 0, 0, 2, 1, 1, 3, 2, 1, 1, 0, 4, 4, 4, 0,
    /* 0xA0 */ 2, 2, 3, 0, 2, 2, 2, 3, 0, 2, 2, 0, 2, 2, 1, 1,
    /* 0xB0 */ 1, 1, 1, 1, 2, 2, 1, 1, 4, 0, 1, 1, 4, 1, 1, 0,
    /* 0xC0 */ 1, 4, 2, 2, 2, 2, 2, 3, 2, 2, 2, 0, 0, 0, 0, 0,
    /* 0xD0 */ 2, 2, 2, 3, 3, 1, 2, 2, 4, 4, 1, 1, 1, 0, 0, 0,
    /* 0xE0 */ 2, 2, 3, 2, 4, 4, 1, 1, 2, 2, 3, 2, 4, 4, 1, 1,
    /* 0xF0 */ 4, 4, 3, 0, 0, 2, 2, 2, 4, 3, 0, 0, 3, 2, 2, 1,
};

/* LFO step function per waveform. */
s32 (*lfoWaveformFuncs[16])(SoundLfo*) = {
    stepLfoSquare, stepLfoSquareBipolar, stepLfoTriangle, stepLfoTriangleBipolar,
    stepLfoSawtooth, stepLfoSawtooth, stepLfoRandom, stepLfoRandomBipolar,
    stepLfoDisabled, stepLfoDisabled, stepLfoDisabled, stepLfoDisabled,
    stepLfoDisabled, stepLfoDisabled, stepLfoDisabled, stepLfoDisabled,
};

/* The SPU registers. */
/* The port's SPU register block (the sound driver writes the voice registers directly). */
extern SpuRegs portSpuRegisters;
SpuRegs* spuRegisters = &portSpuRegisters;

/* SPU reverb work area size per reverb mode (setupReverb). */
s32 reverbWorkAreaSizes[10] = {
    0x80, 0x26C0, 0x1F40, 0x4840, 0x6FE0, 0xADE0, 0xF6C0, 0x18040, 0x18040, 0x3C00,
};

/* Built-in fallback sound, loaded by setSoundError: a 0x30-byte SED (header,
 * then the sound effect data, whose first halfword is read on its own) and a
 * 0x70-byte WDS with one ADPCM block. */
u8 errorSoundSed[0x30] __attribute__((aligned(4))) = {
    's', 'e', 'd', 's', 0x7F, 0x19, 0x00, 0x9B, 0x30, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x04, 0x7F, 0xFB, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x80, 0x80, 0x91, 0x60, 0x07, 0x80, 0x0C, 0x90,
};
u8 errorSoundWds[0x70] = {
    'w', 'd', 's', ' ', 0x68, 0x96, 0x9E, 0xED, 0x40, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0xEE, 0xEC, 0x00, 0xFF, 0x7F, 0x04, 0x31, 0x03, 0x7F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0C, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x07, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
};

/* Note data bytes 0x00-0xE3: length (noteDeltaTimeTable) and key (noteKeyTable) of
 * byte n are NOTE_LENGTHS[n % 19] and n / 19. */
#define NOTE_LENGTHS \
    0x00, 0xC0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, \
    0x12, 0x10, 0x0C, 0x09, 0x08, 0x06, 0x04, 0x03, 0x02
#define KEY19(k) k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k
u8 noteDeltaTimeTable[12 * 19] = {
    NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS,
    NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS, NOTE_LENGTHS,
};
u8 noteKeyTable[12 * 19] = {
    KEY19(0), KEY19(1), KEY19(2), KEY19(3), KEY19(4), KEY19(5),
    KEY19(6), KEY19(7), KEY19(8), KEY19(9), KEY19(10), KEY19(11),
};

/* Note number (pitch >> 8) to octave << 4 | semitone, notes 0-116. */
#define OCTAVE(o) (o << 4) | 0, (o << 4) | 1, (o << 4) | 2, (o << 4) | 3, (o << 4) | 4, \
    (o << 4) | 5, (o << 4) | 6, (o << 4) | 7, (o << 4) | 8, (o << 4) | 9, (o << 4) | 10, \
    (o << 4) | 11
u8 pitchToOctaveSemitone[117] = {
    OCTAVE(0), OCTAVE(1), OCTAVE(2), OCTAVE(3), OCTAVE(4), OCTAVE(5), OCTAVE(6), OCTAVE(7),
    OCTAVE(8), 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
};
