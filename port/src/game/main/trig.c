/*
 * Sine/cosine table readers (hand-written assembly at 0x8003F738 in the PS1
 * build; config/handwritten.yaml). PC versions of the same arithmetic.
 *
 * The table is libgte's rcossin_tbl in main's data (game data from the disc):
 * 4096 entries of {s16 sin, s16 cos} at 4.12 fixed point, one per 1/4096 turn.
 */
#include "common.h"
#include "psyq.h"

/* The whole table. Main's data also labels its second halfword D_800523F2
 * (getAngleSin's reference); declared at full size, the table is one object on
 * the PC and D_800523F2 an alias into it (tools/pc_link.py). */
extern s16 rcossin_tbl[0x1000 * 2];

static s32 tableHalf(u32 i) {
    return rcossin_tbl[i];
}

#define TBL_SIN(a) tableHalf(((a) & 0xFFF) * 2)
#define TBL_COS(a) tableHalf(((a) & 0xFFF) * 2 + 1)

/* Noah: createRotationMatrix. m = Rz * Ry * Rx for the angles in r (vx about
 * X, vy about Y, vz about Z), each product rounded down (>> 12) in the order
 * the PS1 code computes it. Returns m. */
MATRIX* createRotationMatrix(SVECTOR* r, MATRIX* m) {
    s32 sx = TBL_SIN(r->vx);
    s32 cx = TBL_COS(r->vx);
    s32 sy = TBL_SIN(r->vy);
    s32 cy = TBL_COS(r->vy);
    s32 sz = TBL_SIN(r->vz);
    s32 cz = TBL_COS(r->vz);
    s32 a = (cz * -sy) >> 12;
    s32 b = (sz * -sy) >> 12;

    m->m[0][0] = (cz * cy) >> 12;
    m->m[0][1] = -(sz * cy) >> 12;
    m->m[0][2] = sy;
    m->m[1][0] = ((sz * cx) >> 12) - ((a * sx) >> 12);
    m->m[1][1] = ((cz * cx) >> 12) + ((b * sx) >> 12);
    m->m[1][2] = -(cy * sx) >> 12;
    m->m[2][0] = ((a * cx) >> 12) + ((sz * sx) >> 12);
    m->m[2][1] = ((cz * sx) >> 12) - ((b * cx) >> 12);
    m->m[2][2] = (cy * cx) >> 12;
    return m;
}

/* The first half of the table entry (rcossin_tbl's sine, despite the name). */
s32 getAngleCos(s32 angle) {
    return TBL_SIN(angle);
}

/* The second half of the table entry (rcossin_tbl's cosine). */
s32 getAngleSin(s32 angle) {
    return TBL_COS(angle);
}
