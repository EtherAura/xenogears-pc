#ifndef PORT_EXTRAS_RASTER_H
#define PORT_EXTRAS_RASTER_H
#include "vgpu.h"
#ifdef __cplusplus
extern "C" {
#endif
void xgVgpuRaster(XgVgpu *, int x, int y, int w, int h, double scale, uint32_t background, uint32_t *rgba);
#ifdef __cplusplus
}
#endif
#endif
