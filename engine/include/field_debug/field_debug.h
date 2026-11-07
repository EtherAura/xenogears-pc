#ifndef FIELD_DEBUG_FIELD_DEBUG_H
#define FIELD_DEBUG_FIELD_DEBUG_H

#include "common.h"
#include "psyq.h"
#include "main/sound.h"

void SetLineG2(void* p);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);

/* Debug 3D line object (16 at 0x80285B48): a transform built from a
 * rotation and a position, and a gouraud line between two model-space
 * points, double-buffered. */
typedef struct {
    /* 0x00 */ MATRIX m0_matrix;
    /* 0x20 */ SVECTOR m20_rotation;
    /* 0x28 */ SVECTOR m28_position;
    /* 0x30 */ SVECTOR m30_start;
    /* 0x38 */ SVECTOR m38_end;
    /* 0x40 */ LINE_G2 m40_line[2];
} FieldDebugLine; /* size 0x68 */

/* Per-event timing record (0x802859AC). */
typedef struct {
    /* 0x0 */ s32 m0_time;
    /* 0x4 */ const char* m4_name;
    /* 0x8 */ s32 m8;
} FieldDebugTimeEntry; /* size 0xC */

/* Partial views of main's sound lists (include/main/sound.h Wds/Seq). */
#endif /* FIELD_DEBUG_FIELD_DEBUG_H */
