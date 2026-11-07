/*
 * The add-to-OT stubs (hand-written assembly at 0x800315A0 in the PS1 build;
 * config/handwritten.yaml): link a primitive at the head of an ordering-table
 * entry and set its word count, one stub per primitive type.
 *
 * Exactly what the PS1 code does, on the PC's 64-bit tags (include/psyq.h):
 * the entry gets the primitive's link with a zero count, and the primitive
 * gets the entry's previous tag with the stub's count OR'ed into the count
 * byte (the entry's own count byte is normally 0).
 */
#include "common.h"
#include "psyq.h"

static void addPrimWithLength(void* ot, void* prim, u32 len) {
    OTag next = *(OTag*)ot;

    *(OTag*)ot = OT_LINK(prim);
    *(OTag*)prim = next | OT_LEN(len);
}

#define ADD_PRIM_STUB(name, len)              \
    void name(void* ot, void* prim) {         \
        addPrimWithLength(ot, prim, len);     \
    }

ADD_PRIM_STUB(addPolyF3, 4)
ADD_PRIM_STUB(addPolyFT3, 7)
ADD_PRIM_STUB(addPolyG3, 6)
ADD_PRIM_STUB(addPolyGT3, 9)
ADD_PRIM_STUB(addPolyF4, 5)
ADD_PRIM_STUB(addPolyFT4, 9)
ADD_PRIM_STUB(addPolyG4, 8)
ADD_PRIM_STUB(addPolyGT4, 11) /* sic: a POLY_GT4 is 12 words (unused by the game) */
ADD_PRIM_STUB(addLineF2, 3)
ADD_PRIM_STUB(addLineG2, 4)
ADD_PRIM_STUB(addLineF3, 5)
ADD_PRIM_STUB(addLineG3, 7)
ADD_PRIM_STUB(addLineF4, 6)
ADD_PRIM_STUB(addLineG4, 9)
ADD_PRIM_STUB(addSPRT, 4)
ADD_PRIM_STUB(addSprt16, 3)
ADD_PRIM_STUB(addDebugTextPrim2, 3)
ADD_PRIM_STUB(addTile, 3)
ADD_PRIM_STUB(addTile16, 2)
ADD_PRIM_STUB(addTile8, 2)
ADD_PRIM_STUB(addTile1, 2)
