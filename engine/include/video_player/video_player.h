#ifndef VIDEO_PLAYER_VIDEO_PLAYER_H
#define VIDEO_PLAYER_VIDEO_PLAYER_H

/* STR (MDEC) movie streaming player, disc 18/01, loaded at 0x801D3000. */

#include "common.h"
#include "psyq.h"

/* Header of a streamed STR video sector (Noah: sStrSectorHeader). */
typedef struct {
    /* 0x00 */ u16 m0_magic;
    /* 0x02 */ u16 m2_chunkType;
    /* 0x04 */ u16 m4_sectorNumber;
    /* 0x06 */ u16 m6_numSectors;
    /* 0x08 */ s32 m8_frameNumber;
    /* 0x0C */ s32 mC_demuxedSize;
    /* 0x10 */ u16 m10_width;
    /* 0x12 */ u16 m12_height;
} StrSectorHeader;

/* MDEC decode state (strDecEnv): double-buffered VLC and image buffers,
 * the two VRAM destination rects and the slice being transferred. */
typedef struct {
    /* 0x00 */ s32 m0_vlcId;
    /* 0x04 */ u32* m4_vlcBuf[2];
    /* 0x0C */ s32 mC_imgId;
    /* 0x10 */ u16* m10_imgBuf[2];
    /* 0x18 */ s32 m18_rectId;
    /* 0x1C */ RECT m1C_rect[2];   /* w/h hold the end x/y */
    /* 0x2C */ s32 m2C_sliceId;
    /* 0x30 */ RECT m30_slice[2];
} StrDecEnv; /* 0x40 */

/* PSY-Q LIBPRESS */
void DecDCTReset(s32 mode);
void DecDCTin(u32* buf, s32 mode);
void DecDCTout(void* buf, s32 size);
void* DecDCToutCallback(void (*func)());
s32 DecDCTvlc(u32* bs, u32* buf);
s32 DecDCTvlcSize(s32 size);

/* PSY-Q LIBCD streaming */
void StSetRing(u32* ring_addr, u32 ring_size);
void StUnSetRing(void);
void StSetStream(u32 mode, u32 start_frame, u32 end_frame, void (*func1)(), void (*func2)());
u32 StGetNext(u32** addr, u32** header);
u32 StFreeRing(u32* base);
s32 StGetBackloc(CdlLOC* loc);
void StCdInterrupt(void);
s32 CdRead2(s32 mode);

#endif /* VIDEO_PLAYER_VIDEO_PLAYER_H */
