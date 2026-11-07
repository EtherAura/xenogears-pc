/* Bounded ordered GPU passes. Boxes use exclusive upper edges. */
#ifndef XG_GPU_ORDERED_H
#define XG_GPU_ORDERED_H
#include <stddef.h>
#include <stdint.h>
#define ORDERED_TRIANGLES 64
#define ORDERED_CAPACITY (1u << 20)
typedef struct { int x0,y0,x1,y1; } OrderedBox;
/* Positions are the first two floats in each vertex. Non-finite input uses
 * the whole target; clipping and perspective remain the rasterizer's job. */
OrderedBox orderedBounds(const void* vertices, size_t stride, int scale, int w, int h);
uint64_t orderedEstimate(const OrderedBox* boxes, int count, OrderedBox band);
/* Largest safe next horizontal band within column. The caller limits
 * column width to capacity/count, guaranteeing at least one row fits. */
int orderedBandEnd(const OrderedBox* boxes, int count, OrderedBox column, int y, uint32_t capacity);
/* A single triangle or an axis-aligned rectangle split into two triangles
 * needs no destination ordering. Hi-res rectangle vertices must share W. */
int orderedIndependent(const void* vertices,size_t stride,int count,size_t wOffset);
#endif
