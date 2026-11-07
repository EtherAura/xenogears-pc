/*
 * The LZSS decoder (hand-written assembly at 0x80032E88 in the PS1 build;
 * config/handwritten.yaml). PC version of the same logic.
 *
 * Stream: a u32 decompressed size, then groups of a flag byte and eight
 * items, low flag bit first. A clear bit is one literal byte; a set bit is a
 * two-byte back reference: offset = b0 | (b1 & 0xF) << 8 (1..4095 bytes back
 * from the output), length = (b1 >> 4) + 3, copied byte by byte so it may
 * overlap its own output.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/memory.h"
#include "pc_gametext.h"

/* Noah: decompress. The PS1 checks for the end only before each flag byte
 * (all eight items of a group are decoded), so a stream must end exactly on a
 * group boundary: the PS1 loops for ever if it overshoots. The PC stops
 * instead (`<` for `!=`); every stream that terminates on the PS1 decodes
 * identically. */
void decompress(void* src, void* dst) {
    u8* in = (u8*)src;
    u8* out = (u8*)dst;
    u8* end = out + *(u32*)in;
    u8* from;
    u32 flags;
    u32 hi;
    s32 n;
    s32 i;

    in += 4;
    while (out < end) {
        flags = *in++;
        for (i = 8; i != 0; i--, flags >>= 1) {
            if (!(flags & 1)) {
                *out++ = *in++;
                continue;
            }
            hi = in[1];
            from = out - (in[0] | ((hi & 0xF) << 8));
            in += 2;
            for (n = (hi >> 4) + 3; n != 0; n--) {
                *out++ = *from++;
            }
        }
    }
}

/* Noah: mallocAndDecompress. Allocates the decompressed size (the stream's
 * first word) with the caller's heap flags and decodes into it; NULL when the
 * allocation fails. */
u8* mallocAndDecompress(void* src, s32 flags) {
    u8* dst;

    /* a picture with text from the player's text source (system/gametext.h; opt-in) */
    if (pcGameTextOn && (dst = pcGameTextMember(src, flags)) != NULL) {
        return dst;
    }
    dst = allocateHeap(PC_HEAP_BYTES(*(s32*)src), flags);

    if (dst != NULL) {
        decompress(src, dst);
    }
    return dst;
}
