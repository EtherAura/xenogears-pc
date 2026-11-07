#ifndef PORT_EXTRAS_PREVIEW_H
#define PORT_EXTRAS_PREVIEW_H

/*
 * Sound previews for the extras (preview.c): the game's samples and
 * sequences played by our SPU (port/src/spu) on an SPU of their own, heard
 * on an audio stream of their own. The game's sound is paused and never
 * touched: neither its SPU state, its register block, nor its CD audio.
 *
 * XgSpuBox is an SPU state of its own (generalising port/src/spu/menusfx.c,
 * which puts the SPU aside for one effect): inside xgSpuBoxEnter/Leave the
 * SPU core's state (RAM, voices, reverb) and the register block the game's
 * sound driver writes (portSpuRegisters) are the box's. Game code may then
 * drive it, in a code sandbox (sandbox.h) for its own state: the sound
 * driver's register writes and SpuWrite transfers land in the box, and
 * xgSpuBoxMix renders them (voices and reverb; no CD input, which belongs to
 * the paused game). A jukebox ticks the driver at 240 Hz between mixes.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XG_PREVIEW_RATE 44100

typedef struct XgSpuBox XgSpuBox;

XgSpuBox* xgSpuBoxCreate(void); /* a reset SPU: RAM clear, voices off, SPU on */
void xgSpuBoxDestroy(XgSpuBox* b);
void xgSpuBoxEnter(XgSpuBox* b);
void xgSpuBoxLeave(XgSpuBox* b);
/* Inside: bytes into its SPU RAM (byte address). */
void xgSpuBoxWrite(uint32_t addr, const void* data, uint32_t size);
/* Inside: take the register writes made since the last call, then render
 * `frames` stereo frames (44.1 kHz) of its voices and reverb. Returns 1 while
 * a voice is still sounding. */
int xgSpuBoxMix(int16_t* out, int frames);

/* One preset (instrument) of a WDS bank as a note (key 60 = the preset's own
 * pitch), held for holdFrames and then released, into out (stereo, at most
 * maxFrames). Returns the frames written (0: no such preset). */
int xgPreviewSample(const uint8_t* wds, int wdsSize, int preset, int key, int holdFrames, int16_t* out,
                    int maxFrames);

/* One effect of a SEDS bank with its WDS bank, as the game's driver plays it
 * (port/src/spu/menusfx.c). Returns the frames written (0: not followed). */
int xgPreviewEffect(const uint8_t* seds, int sedsSize, const uint8_t* wds, int wdsSize, int effect, int16_t* out,
                    int maxFrames);

/* The preview's audio stream: play (from the start, replacing what plays),
 * stop, and whether it still plays. The volume is settings.ini's volume. */
void xgPreviewPlay(const int16_t* frames, int count);
void xgPreviewQueue(const int16_t* frames, int count);
void xgPreviewStop(void);
int xgPreviewPlaying(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_PREVIEW_H */
