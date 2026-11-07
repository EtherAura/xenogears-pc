#ifndef PORT_MDEC_VLC_H
#define PORT_MDEC_VLC_H

/* LIBPRESS's bitstream decoder (vlc.c). Needs common.h's types. */

#include <stdint.h>

/* The lookup table DecDCTvlc2 takes (DecDCTvlcBuild fills it). */
#define VLC_TABLE_HALVES 0x8800

/* A decode in progress (where it stopped at its size limit). */
typedef struct {
    const u16* src;   /* next stream halfword to load */
    u16* dst;         /* next run-length halfword */
    uint32_t bits;    /* the next 16 to 32 stream bits, first bit in bit 31 */
    uint32_t used;    /* bits of the last loaded halfword shifted out (0-15) */
    uint32_t quant;   /* q_scale << 10 */
    uint32_t block;   /* v3: 1-6 for Cr, Cb, Y1-Y4; 0 for a raw DC (v1/v2) */
    s32 dc[3];        /* v3 DC predictors: Cr, Cb, Y */
    uint32_t lookups; /* AC table lookups the last call made (its PS1 time, vlc.c) */
} VlcState;

/* One DecDCTvlc call: a new frame from `bs` into `buf`, or with bs NULL the
 * rest of the one `s` stopped in. At most `limit` halfwords after the start
 * (checked at each DC). Returns 1 if stopped there, 0 at the frame's end;
 * either way s->dst is where the output ends. */
s32 vlcDecode(VlcState* s, u32 limit, u32* bs, u32* buf, const u16* table);

void vlcMakeTable(u16* table);
const u16* vlcBuiltinTable(void);

/* LIBPRESS calls the game does not declare. */
s32 DecDCTvlcSize2(s32 size);
s32 DecDCTvlc2(u32* bs, u32* buf, u16* table);
void DecDCTvlcBuild(u16* table);

#endif /* PORT_MDEC_VLC_H */
