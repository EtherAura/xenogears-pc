#ifndef PORT_SPU_H
#define PORT_SPU_H

/*
 * The SPU (port/src/spu), after psx-spx "Sound Processing Unit (SPU)": 24
 * ADPCM voices with the 4-point Gaussian interpolation, ADSR envelopes and
 * volume sweeps, pitch modulation, noise, the reverb unit with its work area
 * in SPU RAM, the CD audio input, the capture buffers and the IRQ. It produces
 * 44.1 kHz stereo, one frame per SPU sample clock.
 *
 * Registers: the game writes the register block (portSpuRegisters, the PS1's
 * 0x1F801C00..0x1F801DFF) as plain memory, so nothing traps a write. The core
 * runs on a latched copy of the block: spuRun advances with the registers as
 * they were at the last spuLatch, and spuLatch then takes in the writes made
 * since, acting on them as the hardware acts on a write (key on and off, ENVX
 * and repeat address writes, the reverb work area start), and writes back what
 * the SPU updates itself (ENVX, repeat addresses, ENDX, SPUSTAT). A write thus
 * takes effect at the next latch; port/src/system/spu.c latches at every safe
 * point and around every interrupt, so the sound driver's 240 Hz tick lands on
 * the sample of its virtual time.
 *
 * The core is plain C (no SDL, no clock): port/src/spu/test runs it alone.
 */

#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "psyq/libspu.h"

#define SPU_RAM_SIZE 0x80000
#define SPU_RATE 44100

/*
 * The ADPCM and voice arithmetic (SPEC SPU-D), for the SPU's voices and the
 * CD controller's XA decoder (port/src/cd/xa.c). 1 (the default): what
 * PCSX-Redux and Beetle PSX both do, measured as black boxes
 * (work/clean-room/measure/spu, workloads E1 and E1R): each prediction product
 * shifted right by 6 on its own, without rounding; the Gaussian products
 * summed, then shifted right by 15 once; steps above 3FFFh clipped to 3FFFh.
 * XA audio decoded this way matches Beetle PSX running the disc to within one
 * level (the opening movie). 0: psx-spx's text: the ADPCM prediction rounded
 * once, (old * f0 + older * f1 + 32) >> 6 ("CDROM XA Audio ADPCM
 * Compression", which "SPU ADPCM Samples" refers to); each Gaussian product
 * shifted right by 15 before the sum ("4-Point Gaussian Interpolation"); steps
 * above 3FFFh clipped to 4000h ("Pitch Counter"). The two differ by a few
 * output levels for voices, up to about 90 for XA audio.
 */
#ifndef SPU_MEASURED_ARITHMETIC
#define SPU_MEASURED_ARITHMETIC 1
#endif

/*
 * Key on (SPEC SPU-A, SPU-B; psx-spx gives only "Unstable and Delayed I/O":
 * a written value may take one 44.1 kHz cycle to act). 1 (the default): as
 * measured on Beetle PSX as a black box (workloads E1, E2, E6): the voice
 * keeps playing what it played for five more samples, then starts with its
 * first four samples already in the interpolation window; ENDX clears at
 * once; a key off written with the key on is dropped, one written while the
 * key on waits leaves the voice silent. PCSX-Redux also delays key on (seven
 * samples) and fills the window with three samples. 0, the alternative: the
 * voice starts at the first sample after the latch that sees the key on, its
 * interpolation history zero (psx-spx's bound).
 *
 * The user's choice: 0 (xg-htn.36), 1 (xg-45y)
 * after the game's own sounds were measured against Beetle PSX running the
 * disc (work/agents/BD/AUDIO/NOTES.md): with 1 the menu's cursor and back
 * sounds come out sample for sample as Beetle's (residual 0.23 levels RMS,
 * its x0.75 output's rounding), with 0 they were 11 and 4 dB off (the two
 * voices of each sound fall out of step); field music in 100 ms windows came
 * 5.6 dB closer (median). With both switches 1, the settings menu's four
 * sound effects (menusfx.c) render byte for byte as they did before the
 * clean rewrite.
 */
#ifndef SPU_MEASURED_KEY_ON
#define SPU_MEASURED_KEY_ON 1
#endif

/* ---- core.c ---- */

/* Power-on state of the registers and voices; `regs` is the live register
 * block (cleared too). SPU RAM keeps its contents. */
void spuReset(SpuRegs* regs);

/* Take in the game's register writes since the last latch, and write back
 * the SPU's own state. */
void spuLatch(SpuRegs* regs);

/* As spuLatch, for the game's sound driver running natively: its writes
 * for a whole tick arrive in one batch, so key offs act a fixed number of
 * samples late (core.c DRIVER_KEY_OFF_DELAY). Direct register writes use
 * spuLatch. */
void spuLatchDriver(SpuRegs* regs);

/* Advance `frames` sample clocks, writing interleaved stereo to `out` (may be
 * NULL). Returns 1 if the SPU interrupt was raised meanwhile. */
int spuRun(int16_t* out, uint32_t frames);

/* SPU RAM transfers (byte address, wraps at 512 KB); an access to the IRQ
 * address raises the interrupt (returns 1). */
int spuRamWrite(uint32_t addr, const void* src, uint32_t size);
int spuRamRead(uint32_t addr, void* dst, uint32_t size);
int spuRamFill(uint32_t addr, uint8_t value, uint32_t size);

/* The voice's envelope level (ENVX), for LIBSPU. */
int16_t spuVoiceEnvelope(int voice);

/* ---- cdin.c: the CD audio input ---- */

/* Queue decoded XA-ADPCM audio: `frames` interleaved stereo samples at 37800
 * or 18900 Hz. The CD controller's zigzag resampler turns it into 44.1 kHz. */
void spuCdQueue(const int16_t* lr, int frames, int rate);

/* Drop queued CD audio and reset the resampler (a new stream). */
void spuCdReset(void);

/* The next 44.1 kHz CD input frame, after the CdMix volumes (CD controller). */
void spuCdNext(int32_t out[2]);

/* The input's whole state, for a world with an SPU of its own
 * (port/src/system/world.h): a blob of spuCdStateSize() bytes, zero for an
 * empty input, exchanged with the live state. */
size_t spuCdStateSize(void);
/* Input frames queued and not played yet (a world's player feeds by it). */
uint32_t spuCdQueued(void);
void spuCdStateSwap(void* other);

/* The CD input's mix matrix (CdMix: L->L, L->R, R->R, R->L; 0x80 = 1.0).
 * Provided by the CD layer (port/src/cd/libcd.c). */
void cdGetAudioMix(uint8_t atv[4]);

/* ---- port/src/system/spu.c: for the other subsystems ---- */

/* The SPU at the current virtual time, then queue CD audio (port/src/cd/xa.c). */
void spuCdSubmit(const int16_t* lr, int frames, int rate);

/* ---- output.c: the host audio device ---- */

/* Queue frames for playback (SDL3 audio stream, drift-corrected); also written
 * to $XG_AUDIO_DUMP (raw s16 stereo 44.1 kHz) when set. */
void spuOutputPush(const int16_t* frames, uint32_t count);

#endif /* PORT_SPU_H */
