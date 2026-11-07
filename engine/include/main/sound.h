/* Types the sound driver's files share (main executable, GCC 2.6.0). */
#ifndef MAIN_SOUND_H
#define MAIN_SOUND_H

#include "common.h"

typedef struct {
    s16 left;
    s16 right;
} SndVolume;

/* Linear audio parameter ramp (Noah: sInterpolatableAudioParam), 0xC. */
typedef struct AudioParam {
    /* 0x0 */ s32 m0_value;
    /* 0x4 */ s32 m4_step;
    /* 0x8 */ s16 m8_counter;
    /* 0xA */ s16 mA_target;
} AudioParam;

/* g_spuVolumes: libspu SpuCommonAttr followed by the driver's master and CD
 * volumes and their fades (Noah: sSpuVolumes). */
typedef struct SpuVolumes {
    /* 0x00 */ s32 m0_mask;
    /* 0x04 */ SndVolume m4_mvol;
    /* 0x08 */ SndVolume m8_mvolmode;
    /* 0x0C */ SndVolume mC_mvolx;
    /* 0x10 */ SndVolume m10_cdVolume;
    /* 0x14 */ s32 m14_cdReverb;
    /* 0x18 */ s32 m18_cdMix;
    /* 0x1C */ SndVolume m1C_extVolume;
    /* 0x20 */ s32 m20_extReverb;
    /* 0x24 */ s32 m24_extMix;
    /* 0x28 */ s16 m28_masterVolume;
    /* 0x2A */ s16 m2A_cdVolume;
    /* 0x2C */ s16 m2C_reverbDepth;
    /* 0x2E */ s16 m2E_cdMixVolume;
    /* 0x30 */ AudioParam m30_master;
    /* 0x3C */ AudioParam m3C_cd;
} SpuVolumes;

extern SpuVolumes g_spuVolumes;

/* Shadow of one SPU voice's state (Noah: sSoundInstanceEvent30), 0x2C. */
typedef struct SoundVoice {
    /* 0x00 */ s16 m0_voice;
    /* 0x02 */ u16 m2_flags;
    /* 0x04 */ s16 m4_priority;
    /* 0x06 */ u16 m6_dirty;
    /* 0x08 */ u16 m8_volumeLeft;
    /* 0x0A */ u16 mA_volumeRight;
    /* 0x0C */ s16 mC;
    /* 0x0E */ s16 mE;
    /* 0x10 */ u8 m10[4];
    /* 0x14 */ u16 m14_pitch;
    /* 0x16 */ u8 m16[6];
    /* 0x1C */ u32 m1C_startAddress;
    /* 0x20 */ u32 m20_repeatAddress;
    /* 0x24 */ u8 m24_attackMode;
    /* 0x25 */ u8 m25_sustainMode;
    /* 0x26 */ u8 m26_releaseMode;
    /* 0x27 */ u8 m27_attack;
    /* 0x28 */ u8 m28_decay;
    /* 0x29 */ u8 m29_sustainRate;
    /* 0x2A */ u8 m2A_release;
    /* 0x2B */ u8 m2B_sustainLevel;  /* ADSR1 bits 0-3 */
} SoundVoice;

/* Low-frequency oscillator (Noah sSoundInstanceEventD8), 0x20. */
typedef struct SoundLfo {
    /* 0x00 */ s32 (*m0_func)(struct SoundLfo*);
    /* 0x04 */ s32 m4_value;
    /* 0x08 */ s32 m8_step;
    /* 0x0C */ s32 mC_depth;
    /* 0x10 */ u16 m10_counter;
    /* 0x12 */ u16 m12_period;
    /* 0x14 */ u16 m14_delay;
    /* 0x16 */ u16 m16_delayReload;
    /* 0x18 */ s16 m18_fade;
    /* 0x1A */ u16 m1A_fadeStep;
    /* 0x1C */ u8 m1C_target;
    /* 0x1D */ u8 m1D_waveform;
    /* 0x1E */ u16 m1E_flags;
} SoundLfo;

/* A loaded sequence file (Noah: sSeqFile): header, then the drum kit and
 * track data. Before init 0x14 holds the track and percussion counts and
 * 0x1C two bytes and an offset; once loaded, 0x14 is the id and 0x1C links
 * the loaded list (pLoadedSequencesLinkedList). The data is used where it was
 * loaded, so the PC (8-byte pointers) keeps the link outside it: read and
 * write it with SEQ_NEXT / SEQ_SET_NEXT. */
typedef struct SeqFile {
    /* 0x00 */ u32 m0_magic;
    /* 0x04 */ u32 m4;
    /* 0x08 */ u32 m8_size;
    /* 0x0C */ u32 mC_checksum;
    /* 0x10 */ u16 m10_id;
    /* 0x12 */ u16 m12;
    /* 0x14 */ union {
        u16 m14_id;
        struct {
            u8 m14_trackCount;
            u8 m15_percussionCount;
        } s;
    } u14;
    /* 0x16 */ u16 m16_sampleBank;
    /* 0x18 */ u16 m18;
    /* 0x1A */ u8 m1A_reverbMode;
    /* 0x1B */ u8 m1B_reverbDepth;
    /* 0x1C */ union {
        struct {
            u8 m1C;
            u8 m1D;
            u16 m1E;
        } s;
    } u1C;
    /* 0x20 */ u16 m20_drumKitOffset;
    /* 0x22 */ u16 m22_trackOffsets[1];
} SeqFile;

extern SeqFile* pLoadedSequencesLinkedList;

SeqFile* pcSeqNext(SeqFile* seq);                /* port/src/game/main/sound_pc.c */
void pcSeqSetNext(SeqFile* seq, SeqFile* next);
#define SEQ_NEXT(seq) pcSeqNext(seq)
#define SEQ_SET_NEXT(seq, next) pcSeqSetNext(seq, next)

/* One WDS instrument (sample) entry, 0x10 bytes, from WDS+0x30. */
typedef struct {
    /* 0x0 */ u32 m0_start;      /* in 8-byte SPU units */
    /* 0x4 */ u16 m4_loopOffset; /* from the start, in 8-byte units */
    /* 0x6 */ u16 m6_transpose;
    /* 0x8 */ u32 m8_adsr;
    /* 0xC */ u16 mC_adsrModes;
} WdsInstrument;

/* A loaded WDS sample bank (Noah: sWdsFile): header, then the instruments.
 * On the disc (m10_headerSize1 bytes) the link is a 4-byte slot and the
 * instruments start at 0x30; the driver keeps a copy of the header in its
 * arena, which on the PC is this native layout (pcWdsConvert). */
typedef struct Wds {
    /* 0x00 */ u8 m0[0x10];
    /* 0x10 */ s32 m10_headerSize1;
    /* 0x14 */ s32 m14_adpcmSize;
    /* 0x18 */ s32 m18_headerSize2;
    /* 0x1C */ u16 m1C;
    /* 0x1E */ u16 m1E_spuBlockTag;
    /* 0x20 */ u16 m20_bankNumber;
    /* 0x22 */ u8 m22[6];
    /* 0x28 */ s32 m28_adpcmAddress;
    /* 0x2C */ struct Wds* m2C_next;
    /* 0x30 */ WdsInstrument m30_instruments[1];
} Wds;

extern Wds* pLoadedWdsLinkedList;

/* The native copy of a WDS header: its size, and the conversion
 * (port/src/game/main/sound_pc.c). */
s32 pcWdsSize(Wds* disc);
void pcWdsConvert(Wds* copy, Wds* disc);

/* Sequencer loop stack entry (Noah: sSoundInstanceEventCallstack), 0xC. */
typedef struct SoundLoop {
    /* 0x0 */ u8 m0_count;
    /* 0x1 */ u8 m1;
    /* 0x2 */ u8 m2_octave;
    /* 0x3 */ u8 m3_octave;
    /* 0x4 */ u8* m4_start;
    /* 0x8 */ u8* m8_end;
} SoundLoop;

/* One sequence track (Noah: sSoundInstanceEvent), 0x158. */
typedef struct SoundTrack {
    /* 0x000 */ u16 m0_flags;
    /* 0x002 */ u16 m2_update;
    /* 0x004 */ u16 m4_flags;
    /* 0x006 */ u8 m6_voiceIndex;
    /* 0x007 */ u8 m7_priority;
    /* 0x008 */ union {
        s32 m8_id; /* (sequence id << 16) | track */
        struct {
            s16 m8_track;
            s16 mA_seqId;
        } s;
    } m8;
    /* 0x00C */ s32 mC_time;
    /* 0x010 */ u8* m10_ptr;
    /* 0x014 */ u8* m14_pc;
    /* 0x018 */ u8* m18_loopStart;
    /* 0x01C */ s32 m1C;
    /* 0x020 */ u16 m20_loopCount;
    /* 0x022 */ u8 m22;
    /* 0x023 */ u8 m23_loopOctave;
    /* 0x024 */ u8 m24;
    /* 0x025 */ u8 m25_bank;
    /* 0x026 */ u8 m26_instrument;
    /* 0x027 */ u8 m27_voice;
    /* 0x028 */ u8 m28_releaseBackup;
    /* 0x029 */ u8 m29[3];
    /* 0x02C */ Wds* m2C_wds;
    /* 0x030 */ SoundVoice m30_voice;
    /* 0x05C */ u16 m5C_deltaTime;
    /* 0x05E */ u16 m5E_gate;
    /* 0x060 */ u8 m60_lengthAdjust;
    /* 0x061 */ u8 m61;
    /* 0x062 */ u16 m62_gateRatio;
    /* 0x064 */ u8 m64_key;
    /* 0x065 */ u8 m65_key;
    /* 0x066 */ s16 m66_octave;
    /* 0x068 */ s32 m68_pitch;
    /* 0x06C */ s16 m6C_transpose;
    /* 0x06E */ s16 m6E_pitchBend;
    /* 0x070 */ u16 m70_portamento;
    /* 0x072 */ u16 m72_loopDepth;
    /* 0x074 */ s16 m74_pan;
    /* 0x076 */ s16 m76_volume;
    /* 0x078 */ s32 m78_volume;
    /* 0x07C */ s32 m7C_noteVolumeDelta;
    /* 0x080 */ u16 m80_noteVolumeSlideLength;
    /* 0x082 */ u16 m82_noteVolumeStart;
    /* 0x084 */ s32 m84_pitchDelta;
    /* 0x088 */ s32 m88_volumeDelta;
    /* 0x08C */ s16 m8C_volumeStep;
    /* 0x08E */ s16 m8E_volumeTarget;
    /* 0x090 */ s16 m90_panStep;
    /* 0x092 */ s16 m92_panTarget;
    /* 0x094 */ u16 m94_pitchSlide;
    /* 0x096 */ u16 m96_volumeSlide;
    /* 0x098 */ u16 m98_panSlide;
    /* 0x09A */ u16 m9A_volumeFade;
    /* 0x09C */ SoundLoop m9C_loops[4];
    /* 0x0CC */ u16 mCC_lfoIndex;
    /* 0x0CE */ u16 mCE_lfoActive;
    /* 0x0D0 */ s16 mD0_pitchMod;
    /* 0x0D2 */ s16 mD2_volumeMod;
    /* 0x0D4 */ s16 mD4_panMod;
    /* 0x0D6 */ s16 mD6;
    /* 0x0D8 */ SoundLfo mD8_lfo[4];
} SoundTrack;

/* A playing sequence (Noah: sSoundInstance): header state, then its tracks. */
typedef struct SoundInstance {
    /* 0x00 */ struct SoundInstance* m0_next;
    /* 0x04 */ void* m4_snapshot;
    /* 0x08 */ SeqFile* m8_seq;
    /* 0x0C */ u8* mC_percussion;
    /* 0x10 */ u16 m10_flags;
    /* 0x12 */ s16 m12_seqId;
    /* 0x14 */ u8 m14_trackCount;
    /* 0x15 */ u8 m15;
    /* 0x16 */ s16 m16_sampleBank;
    /* 0x18 */ u16 m18;
    /* 0x1A */ u8 m1A_instanceVar;
    /* 0x1B */ u8 m1B_loopSelector;
    /* 0x1C */ u16 m1C_noiseClock;
    /* 0x1E */ u16 m1E_seekMeasure;
    /* 0x20 */ s32 m20_updateCount;
    /* 0x24 */ u32 m24_tickCount;
    /* 0x28 */ s32 m28_elapsedTime;
    /* 0x2C */ u32 m2C_loopEndTick;
    /* 0x30 */ s16 m30_minLoopCount;
    /* 0x32 */ u16 m32_measure;
    /* 0x34 */ u16 m34_beat;
    /* 0x36 */ u16 m36_tick;
    /* 0x38 */ u16 m38_beatsPerMeasure;
    /* 0x3A */ u16 m3A_ticksPerBeat;
    /* 0x3C */ u16 m3C_timeSigUnit;
    /* 0x3E */ u16 m3E_timeSigBeats;
    /* 0x40 */ u8 m40;
    /* 0x41 */ s8 m41_reverbMode;
    /* 0x42 */ u8 m42_reverbDelay;
    /* 0x43 */ u8 m43_reverbFeedback;
    /* 0x44 */ s16 m44_reverbDepth;
    /* 0x46 */ u16 m46;
    /* 0x48 */ u32 m48_activeVoices;
    /* 0x4C */ s32 m4C_muteMask;
    /* 0x50 */ s32 m50_tickAccum;
    /* 0x54 */ s32 m54_tickRate;
    /* 0x58 */ union {
        s32 m58_tempo;
        struct {
            u16 m58_lo;
            s16 m5A_hi;
        } s;
    } m58;
    /* 0x5C */ s32 m5C_tempoDelta;
    /* 0x60 */ u16 m60_tempoSlide;
    /* 0x62 */ u16 m62_tempoTarget;
    /* 0x64 */ AudioParam m64_tempo;
    /* 0x70 */ AudioParam m70_volume;
    /* 0x7C */ AudioParam m7C_pitch;
    /* 0x88 */ AudioParam m88_pan;
    /* 0x94 */ SoundTrack m94_tracks[1];
} SoundInstance;

/* Driver state the game modes share (main). */
extern SoundInstance* pMusic;                 /* the current music */
extern SoundInstance* savedMusicInstance;     /* music held while another plays */
extern SeqFile* gBattleCurrentSoundEffectSeq; /* the loaded sound-effect sequence */
extern Wds* worldmapWdsResult;                /* the world map's sample bank */
extern Wds* wdsFile3;                       /* sample banks (kernelMain, the field) */
extern Wds* wdsFile5;
extern Wds* fieldWdsSlots[4];                    /* sample banks by slot (field opcode EX 0xB0) */
extern Wds* fieldWdsSlot1; /* fieldWdsSlots[1], addressed on its own (retail codegen) */
#define fieldWdsSlot1 (fieldWdsSlots[1])
/* Slots of fieldWdsSlots (pointers: their PC offsets differ from the PS1 ones). */

/* sound_data.c: errorSoundSed's body (errorSoundSed + 0x14). The PC defines
 * the whole SED as one array: its compiler aligns separate arrays apart. */
extern u8 errorSoundSed[];
#define errorSoundSedBody ((u16*)(errorSoundSed + 0x14))

/* sound.c */
void initSoundSystem(u16 mode);
Wds* loadWds(Wds* wds, s32 addr);
Wds* playMusic2(Wds* wds, s32 size, s32 addr);
void unloadWds(Wds* wds);
void freeSoundInstance(SoundInstance* inst);
void startMusicInstanceSub0(SoundInstance* inst);

/* sequencer.c */
void applyFlagToAllVoices(u16 flag, SoundInstance* inst);
void setSoundError(s16 code);
void playBattleMusic(SoundInstance* inst, s32 vol, s32 dur);
s32 waitForMusic(s32 wait);

/* Functions some PS1 callers declare their own way (they pass or expect
 * other widths): the PC sees the definitions' prototypes here, the callers
 * keep their views under #ifndef PLATFORM_PC. */
void PlaySoundEffect2(s32 id, s32 ch, s32 vol, s32 pan);   /* sound.c */
void playSoundEffectSub(u32 ch, s32 id, u32 vol, u16 pan); /* sequencer.c */

#endif
