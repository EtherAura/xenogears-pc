#ifndef PC_GEOMETRY_H
#define PC_GEOMETRY_H

/* Projected faces whose integer and fractional back-face tests disagree.
 * These treatments affect only the enhanced display copy, at any aspect. */
#define PC_GEOMETRY_ONLY 1 /* integer test culled a visible fractional face */
#define PC_GEOMETRY_HIDE 2 /* integer test kept a fractional back face */
int pcGeometryActive(void); /* enhanced geometry enabled and drawing the live display */
void pcGeometryPrim(void* prim, int treatment);
/* Additional terrain/shadow/custom-polygon coverage is opt-in. Query the
 * projected FIFO without changing the native NCLIP result. Private packets
 * never consume the game's polygon buffers or cursors. */
#include "gte_pc.h"
int pcGeometrySlivers(void);
static inline int pcGeometryRescue(void) {
    double area;
    return pcGeometryActive() && pcGeometrySlivers() && gtePrecisionNclip(&area) && area > 1e-6;
}
void* pcGeometryAlloc(const void* packet, int bytes);
void pcGeometryBlend(void* packet, int abr);

#endif
