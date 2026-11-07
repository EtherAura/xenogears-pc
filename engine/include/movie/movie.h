#ifndef MOVIE_MOVIE_H
#define MOVIE_MOVIE_H

/* Movie overlay (disc 01/12, 0x8006FAF0): the STR movie player front end
 * and its debug menu (movie test, CD-ROM monitor/check, FAT check, disc
 * change test, sound test). Agent RR4, round 20. */

#include "common.h"
#include "psyq.h"

/* One display buffer (two at movieDisplayBuffers, 0x138 apart). */
typedef struct {
    /* 0x000 */ DRAWENV m0_drawEnv;
    /* 0x05C */ DISPENV m5C_dispEnv;
    /* 0x070 */ OTag m70_ot[32];
    /* 0x0F0 */ POLY_G4 mF0_background;  /* initGradientBackground */
    /* 0x114 */ POLY_G4 m114_background2; /* initGradientBorder */
} MovieDisplayBuffer; /* 0x138 */

/* The two animated background gradients (initGradientBackground/drawGradientBackground at
 * gradientBackgroundFrom.., initGradientBorder/drawGradientBorder at gradientBorderFrom..) are four
 * separate arrays each (CVECTOR from[4], CVECTOR to[4], s32 counter[4],
 * s32 period[4]): retail computes their addresses independently, so they
 * are declared as separate externs, not as one struct. */

/* STR sector header (Noah sStrSectorHeader). */
typedef struct {
    /* 0x00 */ u16 m0_magic; /* 0x160 */
    /* 0x02 */ u16 m2_chunkType;
    /* 0x04 */ u16 m4_sectorNumber;
    /* 0x06 */ u16 m6_numSectors;
    /* 0x08 */ u32 m8_frameNumber;
    /* 0x0C */ u32 mC_demuxedSize;
    /* 0x10 */ u16 m10_width;
    /* 0x12 */ u16 m12_height;
} StrSectorHeader;

#endif
