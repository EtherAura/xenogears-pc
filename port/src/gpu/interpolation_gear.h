#ifndef XG_INTERPOLATION_GEAR_H
#define XG_INTERPOLATION_GEAR_H
#include <stddef.h>
#include <stdint.h>
#include "pc_interpolation.h"

#define INTERP_GEAR_RIGS 32
/* A Gear packet without an eligible recipe still holds its authored pose. */
#define INTERP_GEAR_HOLD (INTERP_GEAR_RIGS + 1)
typedef struct {
    uint64_t identity;
    size_t size, capacity;
    void* pose;
    InterpGearEval* evaluate;
} InterpGearRig;

typedef struct {
    int16_t local[3];
    uint16_t screen;
    float center[2];
    int perspective;
} InterpGearVertex;

int interpolationGearPacket(void);
#endif
