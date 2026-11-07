/*
 * The battle's gear files as native tables (include/pc_convert.h,
 * include/battle/battle.h). The battle's copy of the gear code relocates a
 * gear file in place the first time a mecha is built from it; the mechas built
 * from the same file afterwards (monster gears sharing a model, flags 1 and 4)
 * read the relocated tables. The PC converts file 2 (animation and sound,
 * which the mecha keeps pointers into) once and hands later users the same
 * tables. The list lives in the battle's BSS, so each battle starts empty;
 * tables nobody owns stay in the heap until the mode switch resets it.
 */
#include "common.h"
#include "battle/battle.h"
#include "pc_convert.h"

#define MAX_DATA2 32

static struct {
    void* raw;
    sMechaDataTable2* table;
} data2[MAX_DATA2];

sMechaDataTable2* pcBattleMechaData2(void* raw, s32 fresh) {
    s32 i;
    s32 freeSlot = -1;

    for (i = 0; i < MAX_DATA2; i++) {
        if (data2[i].raw == raw && raw != NULL) {
            if (!fresh) {
                return data2[i].table;
            }
            freeSlot = i;
            break;
        }
        if (data2[i].raw == NULL && freeSlot < 0) {
            freeSlot = i;
        }
    }
    if (freeSlot < 0) {
        PORT_TODO("more battle gear files than the conversion list holds");
        return pcConvertMechaData2(raw);
    }
    data2[freeSlot].raw = raw;
    data2[freeSlot].table = pcConvertMechaData2(raw);
    return data2[freeSlot].table;
}

void pcBattleFreeMechaData2(sMechaDataTable2* table) {
    s32 i;

    for (i = 0; i < MAX_DATA2; i++) {
        if (data2[i].table == table) {
            data2[i].raw = NULL;
            data2[i].table = NULL;
        }
    }
    pcFreeMechaData2(table);
}
