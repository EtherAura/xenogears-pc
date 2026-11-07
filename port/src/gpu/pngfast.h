/*
 * PNGs for the movie pack (pngfast.c, xg-htn.35.10): an uncompressed
 * ("stored") PNG writer, and a reader whose fast path takes stored-deflate
 * PNGs without inflating (the stored blocks copied, the rows unfiltered).
 * The PNG format from the PNG specification (W3C, third edition) and the
 * zlib/deflate formats from RFC 1950 and RFC 1951.
 */
#ifndef PORT_GPU_PNGFAST_H
#define PORT_GPU_PNGFAST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* w x h pixels of `channels` bytes (3: RGB, 4: RGBA), rows `pitch` bytes
 * apart, as an 8-bit PNG whose zlib stream is stored blocks only (no
 * compression, filter 0). 1 when written. */
int pngWriteStored(const char* path, int w, int h, int channels, const uint8_t* px, size_t pitch);

/* A PNG whose image data is stored deflate blocks (8-bit RGB or RGBA, not
 * interlaced): its pixels as RGBA bytes (caller frees), *w x *h; NULL when
 * it is not such a PNG (*other 1: a valid PNG of another kind, for a general
 * decoder) or it is damaged. Sizes above maxSide or maxPixels are refused. */
uint8_t* pngReadStored(const char* path, int* w, int* h, int maxSide, uint64_t maxPixels, int* other);

/* dump_png_uncompressed (settings): discovery's PNGs (texture dumps and
 * movie frames) written uncompressed, faster to write and to read back. */
int pngDumpStored(void);
void pngSetDumpStored(int on);

/* The same from memory (the file's bytes). */
uint8_t* pngDecodeStored(const uint8_t* file, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels,
                         int* other);

/* An 8-bit greyscale stored PNG (an index map: one byte a pixel, caller frees);
 * NULL as pngDecodeStored. */
uint8_t* pngDecodeStoredGray(const uint8_t* file, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels,
                             int* other);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPU_PNGFAST_H */
