#ifndef PORT_SYSTEM_STATECODEC_H
#define PORT_SYSTEM_STATECODEC_H

/*
 * The save states' compression (statecodec.c): an LZ77 code of our own on
 * bytes, in blocks packed independently.
 *
 * A packed stream is a series of blocks, each
 *   u32 raw size (at most STATE_PACK_BLOCK), u32 packed size, the bytes:
 *   stored as they are when packing would not make them smaller.
 * A packed block is a series of sequences: a control byte (bits 7-4: how many
 * literals follow, bits 3-0: the match length minus 4; 15 in either means more
 * length follows in bytes, each added, until one under 255), the literals,
 * then a 16-bit little-endian distance back (1-65535) and the match's extra
 * length bytes. The last sequence has literals only. Zero runs become long
 * matches at distance 1.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATE_PACK_BLOCK (1u << 20)

/* The most a packed stream of n bytes can take. */
size_t statePackBound(size_t n);
/* Packs n bytes into out (statePackBound(n) bytes); returns the packed size. */
size_t statePack(const uint8_t* in, size_t n, uint8_t* out);
/* Unpacks a stream of `size` bytes into out, which takes exactly rawSize;
 * 1 on success, 0 for a damaged or mismatched stream (out then undefined). */
int stateUnpack(const uint8_t* in, size_t size, uint8_t* out, size_t rawSize);

/* A 64-bit hash of n bytes (the state body's checksum, the build id). */
uint64_t stateHash(const void* data, size_t n, uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATECODEC_H */
