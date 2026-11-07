/*
 * XA-ADPCM sectors the drive sends to the SPU's CD input (Setmode bit 6; the
 * drive filters them, see drive.c). Each is decoded here as the CD controller
 * does (psx-spx "CDROM XA Audio ADPCM Compression") and queued for the SPU,
 * which resamples it to 44.1 kHz (port/src/spu/cdin.c).
 *
 * A Form 2 audio sector holds 18 sound groups of 128 bytes: 16 header bytes
 * (the shift/filter byte of each sound unit) and 28 data words, word j holding
 * sample j of every unit. 4-bit audio has 8 units of 28 samples per group
 * (mono: one after the other; stereo: left and right alternating), 8-bit audio
 * 4 units. The decoder history carries over from sector to sector, per
 * channel.
 */

#include "cd_internal.h"
#include "spu/spu.h"
#include "../system/savestate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XA_GROUPS 18
#define XA_MAX_FRAMES (XA_GROUPS * 8 * 28) /* 4-bit mono */

static const int8_t filterPos[4] = {0, 60, 115, 98};
static const int8_t filterNeg[4] = {0, 0, -52, -55};

static int16_t history[2][2]; /* per channel: the last two samples */
static unsigned long xaSectors;
static int logLevel = -1;

/* Decode one sound unit's 28 samples (every `step`-th slot of out): psx-spx
 * "CDROM XA Audio ADPCM Compression" decode_28_nibbles, the prediction as
 * SPU_MEASURED_ARITHMETIC (spu.h) selects: by default the measured one, each
 * product shifted on its own; psx-spx's single rounded shift with 0. */
static void decodeUnit(const uint8_t* group, int unit, int eightBit, int16_t* hist, int16_t* out, int step) {
    uint8_t header = group[4 + unit];
    int shift = header & 0xF;
    int filter = (header >> 4) & 3;

    if (shift > 12) {
        shift = 9; /* reserved values act as 9 */
    }
    for (int j = 0; j < 28; j++) {
        const uint8_t* word = group + 16 + j * 4;
        int32_t s;
        if (eightBit) {
            s = (int16_t)(word[unit] << 8) >> shift;
        } else {
            s = (int16_t)(((word[unit >> 1] >> ((unit & 1) * 4)) & 0xF) << 12) >> shift;
        }
#if SPU_MEASURED_ARITHMETIC
        s += ((hist[0] * filterPos[filter]) >> 6) + ((hist[1] * filterNeg[filter]) >> 6);
#else
        s += (hist[0] * filterPos[filter] + hist[1] * filterNeg[filter] + 32) >> 6;
#endif
        s = s < -0x8000 ? -0x8000 : s > 0x7FFF ? 0x7FFF : s;
        hist[1] = hist[0];
        hist[0] = (int16_t)s;
        out[j * step] = (int16_t)s;
    }
}

/* Decode a raw 2352-byte XA audio sector into interleaved stereo frames (mono
 * is copied to both sides), with the decoder history `hist` (per channel, the
 * last two samples). Returns the number of frames and the sample rate. */
static int xaDecodeSectorWith(const uint8_t* raw, int16_t* lr, int* rate, int16_t hist[2][2]) {
    const uint8_t coding = raw[16 + 3];
    int stereo = (coding & 3) == 1;
    int eightBit = ((coding >> 4) & 3) == 1;
    int units = eightBit ? 4 : 8;
    int frames = 0;

    *rate = (coding & 4) ? 18900 : 37800;
    for (int g = 0; g < XA_GROUPS; g++) {
        const uint8_t* group = raw + 24 + g * 128;
        if (stereo) {
            for (int u = 0; u < units; u += 2) {
                decodeUnit(group, u, eightBit, hist[0], lr + 2 * frames, 2);
                decodeUnit(group, u + 1, eightBit, hist[1], lr + 2 * frames + 1, 2);
                frames += 28;
            }
        } else {
            for (int u = 0; u < units; u++) {
                decodeUnit(group, u, eightBit, hist[0], lr + 2 * frames, 2);
                for (int j = 0; j < 28; j++) {
                    lr[2 * (frames + j) + 1] = lr[2 * (frames + j)];
                }
                frames += 28;
            }
        }
    }
    return frames;
}

/* The same with the CD controller's own history (the sectors it plays). */
int xaDecodeSector(const uint8_t* raw, int16_t* lr, int* rate) {
    return xaDecodeSectorWith(raw, lr, rate, history);
}

void xaSubmitSector(const uint8_t* raw) {
    static int16_t lr[XA_MAX_FRAMES * 2];
    int rate;
    int frames = xaDecodeSector(raw, lr, &rate);

    if (logLevel < 0) {
        const char* env = getenv("XG_CD_LOG");
        logLevel = env != NULL ? atoi(env) : 0;
    }
    if (xaSectors++ == 0 || logLevel >= 2) {
        fprintf(stderr, "cd: XA audio, file %d channel %d, %d Hz %s %d-bit\n", raw[16], raw[17], rate,
                (raw[19] & 3) == 1 ? "stereo" : "mono", ((raw[19] >> 4) & 3) == 1 ? 8 : 4);
    }
    spuCdSubmit(lr, frames, rate);
}

/* ---- a world of its own (cd.h cdWorldCreate) ---- */

/* What a world has of this file: the decoder's history (its sectors go to
 * the world's SPU: spuCdSubmit's input is the world's, system/world.h). */
typedef struct {
    int16_t history[2][2];
    unsigned long xaSectors;
} XaPart;

size_t xaPartSize(void) {
    return sizeof(XaPart);
}

void xaPartInit(void* blob) {
    XaPart* p = blob;
    memset(p, 0, sizeof(*p));
    p->xaSectors = 1; /* the first sector's log line is the game's */
}

void xaPartSwap(void* blob) {
    XaPart* p = blob;
    CD_SWAP(history, p->history);
    CD_SWAP(xaSectors, p->xaSectors);
}

/* Save states (system/savestate.h): the ADPCM decoder's history. */
void xaStateIo(XgState* s) {
    XG_STATE_VAR(s, history);
}
