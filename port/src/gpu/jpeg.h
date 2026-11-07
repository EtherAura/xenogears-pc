/*
 * JPEG pictures (jpeg.c): our own decoder, written from ITU-T T.81 (ISO/IEC
 * 10918-1) and the JFIF colour conventions, for the shader presets' LUT
 * pictures (libretro's bezel art). Huffman-coded baseline, extended
 * sequential (8-bit) and progressive frames; one component (grey) or three
 * (YCbCr, or RGB when an Adobe marker says so); any sampling factors 1 to 4
 * (4:4:4, 4:2:2, 4:2:0, ...); restart intervals. Not taken: arithmetic
 * coding, lossless and hierarchical frames, 12-bit samples, four components
 * (CMYK). Every read is bounds-checked: damaged or hostile data gives an
 * error (or, for a cut-short progressive file, the picture so far), never a
 * crash.
 */
#ifndef PORT_GPU_JPEG_H
#define PORT_GPU_JPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the data starts like a JPEG file (SOI then a marker). */
int jpegIs(const uint8_t* data, size_t size);
/* The picture as RGBA bytes (alpha 255; caller frees), *w x *h; NULL and err
 * when it cannot. Sizes above maxSide on a side or maxPixels in all are
 * refused before anything large is allocated. */
uint8_t* jpegDecode(const uint8_t* data, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels, char* err,
                    size_t errCap);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPU_JPEG_H */
