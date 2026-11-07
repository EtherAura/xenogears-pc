/* Display-only actor anchors, independent of the current animation's tile
 * offsets and dimensions. Read native matrices/registers without executing
 * additional GTE commands or changing the actor. */
#include "common.h"
#include "psyq.h"
#include "gte_pc.h"
#include "main/sprite.h"
#include "pc_interpolation.h"

extern MATRIX currentRenderingMatrix;

void pcSpriteInterpolationPrim(void* owner, void* primitive, unsigned part) {
    SpriteActorCore* actor = owner;
    double position[3];
    double camera[3];
    double h;
    s32 i;
    if (!pcInterpolationBuilding() || !actor || !actor->m20_object) return;
    position[0] = (s16)(actor->m0_position.vx >> 16);
    position[1] = (s16)(actor->m0_position.vy >> 16);
    position[2] = (s16)(actor->m0_position.vz >> 16);
    for (i = 0; i < 3; i++)
        camera[i] = currentRenderingMatrix.t[i] +
            (currentRenderingMatrix.m[i][0] * position[0] + currentRenderingMatrix.m[i][1] * position[1] +
             currentRenderingMatrix.m[i][2] * position[2]) / 4096.0;
    h = (u16)gteCfc2(26);
    if (camera[2] <= h / 2.0) {
        pcInterpolationKey(actor, actor->m20_object, part, primitive);
        return;
    }
    pcInterpolationPlacement(actor, actor->m20_object, part, primitive,
        (float)((s32)gteCfc2(24) / 65536.0 + h * camera[0] / camera[2]),
        (float)((s32)gteCfc2(25) / 65536.0 + h * camera[1] / camera[2]), (float)camera[2]);
}
