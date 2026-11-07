#ifndef PORT_EXTRAS_JUKEBOX_H
#define PORT_EXTRAS_JUKEBOX_H

/*
 * The jukebox's engine (jukebox.c): the game's own sound driver
 * (src/main/sound.c and sequencer.c, main 0x80037B88-0x8003F6B0) playing
 * music and sound effects from the player's disc, on our SPU, while the game
 * is paused and without touching anything of it.
 *
 * An engine is the driver in a code sandbox of its own (sandbox.h: main's
 * data and BSS, which hold all of the driver's state, and a heap) and a world
 * of its own (port/src/system/world.h: the BIOS event and root counter its
 * initSoundSystem sets up, LIBSPU's transfers and callbacks, a whole SPU).
 * It is set up as the kernel sets the driver up (SpuInit, initSoundSystem(0),
 * then the resident sample banks, xgJbLoadBank), and its 240 Hz tick runs on
 * the world's root counter as the game's does on the PS1's. xgJbRender lets
 * the world's time pass and returns what the SPU played meanwhile.
 *
 * Music starts as the field starts it (createMusicInstance, then
 * startMusicInstance at volume 0x7F): the score's own reverb, tempo and loop
 * (op 0x91 marks the loop start, op 0x90 goes back to it). Effects play as
 * the menus play theirs (loadSequence, playSoundEffect). Channels are muted
 * with the driver's own track mute (setMusicInstanceTrackMuteMask) and the
 * music fades with its own volume ramp (playBattleMusic).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XG_JB_RATE 44100
#define XG_JB_TRACKS 32

typedef struct XgJbEngine XgJbEngine;

/* A driver set up as the kernel leaves it, no sample bank loaded. NULL if
 * out of memory. */
XgJbEngine* xgJbCreate(void);
void xgJbDestroy(XgJbEngine* e);

/* Load a WDS sample bank as the game does (loadWds; the samples go to its
 * SPU RAM address). A loaded bank its samples would overlap is unloaded
 * first, as the field unloads its resident bank for the large music banks; a
 * bank already loaded with the same number and size is kept. 1 on success. */
int xgJbLoadBank(XgJbEngine* e, const uint8_t* wds, int size);
/* Whether this bank (number, SPU address, sample bytes) is loaded. */
int xgJbHasBank(XgJbEngine* e, int bank, uint32_t addr, uint32_t bytes);
/* The bank numbers loaded, in the driver's order; returns how many. */
int xgJbBanks(XgJbEngine* e, int* banks, int max);

/* Start a score (SMDS) as the field does; the one playing stops first. Its
 * banks must be loaded (a missing one plays from the first bank, as in the
 * game). 1 on success. */
int xgJbPlay(XgJbEngine* e, const uint8_t* smds, int size);
/* Music and effects stop at once (their voices released with the driver's
 * short release). */
void xgJbStop(XgJbEngine* e);
/* The music's volume to 0 over `seconds` (the driver's ramp, 240 steps a
 * second). */
void xgJbFade(XgJbEngine* e, double seconds);
/* The music's tracks with bit n set are muted (track 0 is the conductor). */
void xgJbMute(XgJbEngine* e, uint32_t mask);

/* Play effect `effect` of a SEDS bank (loaded once; a loaded bank with the
 * same id is unloaded first). Its sample banks must be loaded. 1 on success. */
int xgJbEffect(XgJbEngine* e, const uint8_t* seds, int size, int effect);
/* Only load a SEDS bank (as the field loads its own at its start). */
int xgJbLoadEffects(XgJbEngine* e, const uint8_t* seds, int size);

/* Let up to `frames` of the world's time pass (44.1 kHz) a driver tick at a
 * time, writing what the SPU played into out (interleaved stereo). It stops
 * early after the tick where the score's loop count reaches untilLoops (if
 * > 0) or where the score ends (if untilEnd). Returns the frames written. */
int xgJbRender(XgJbEngine* e, int16_t* out, int frames, int untilLoops, int untilEnd);

/* Timing a score: the driver alone (no SPU mixed, fast) for up to `seconds`
 * more, a tick at a time, noting when every track has gone back to its loop
 * start once and twice, and when the score ends. Seconds since it started,
 * -1 not yet. Returns 1 once it knows (two loops, or the end). */
typedef struct {
    double loop[2];
    double end;
} XgJbMarks;
int xgJbMeasure(XgJbEngine* e, double seconds, XgJbMarks* m);

typedef struct {
    int music;      /* a score is playing (the instance runs) */
    int effects;    /* effect tracks are playing */
    double time;    /* seconds since the score started */
    int loops;      /* times every track has gone back to its loop start */
    int measure, beat;
    int tempo;      /* the score's tempo byte (op 0xA0), and its */
    double bpm;     /*   quarter notes a minute */
    int volume;     /* the instance's volume, 0-127 */
    int tracks;
    int error;      /* the driver has reported an error (setSoundError) */
    struct {
        int active;
        int muted;
        int voice;  /* SPU voice, -1 none (the conductor) */
        int bank, instrument;
        int key;    /* the last note (semitones) */
        int level;  /* the voice's envelope, 0-0x7FFF */
        int heard;  /* playing, not muted, its voice sounding */
    } track[XG_JB_TRACKS];
    int voiceLevel[24]; /* every SPU voice's envelope */
} XgJbStatus;

void xgJbStatus(XgJbEngine* e, XgJbStatus* s);

/* CD audio: raw 2352-byte XA audio sectors (a movie stream's), decoded by
 * our XA decoder (cd/xa.c, with a state of the engine's own) into the
 * world's SPU CD input, as the drive sends them on the PS1; the SPU mixes
 * them at the CD volume the driver set (initSoundSystem: 0x7FFF). Feed as
 * they play: the input holds about 0.4 s (xgJbCdQueued: input frames). */
void xgJbXa(XgJbEngine* e, const uint8_t* raw, int count);
int xgJbCdQueued(XgJbEngine* e);

/* The sample banks a score or effect bank uses: its default bank and those
 * its tracks switch to (ops 0xFC, 0xFE), each once. Returns how many. */
int xgJbScoreBanks(const uint8_t* data, int size, int* banks, int max);
/* The same for one effect of a SEDS bank. */
int xgJbEffectBanks(const uint8_t* data, int size, int effect, int* banks, int max);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_JUKEBOX_H */
