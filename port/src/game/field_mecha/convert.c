/*
 * The gear files (dir 4, 0x6BA + 2n and 0x6BB + 2n) as native tables
 * (include/pc_convert.h). Both are trees of [count][offsets] tables, each
 * table's offsets counted from its own start, that mechaInitNewMecha
 * relocates in place on the PS1.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/memory.h"
#include "field_mecha/mecha.h"
#include "pc_convert.h"

#define SLOTS(t) ((t)[0] + 1)
#define AT(t, i) ((u32*)((u8*)(t) + (t)[i]))

/* File 1: textures, model blocks, bones, gear info (itself a table). */
sMechaDataTable1* pcConvertMechaData1(void* data) {
    u32* t1 = data;
    u32* gear = AT(t1, 4);
    void** out = allocateHeap(PC_HEAP_NATIVE((s32)((SLOTS(t1) + SLOTS(gear)) * sizeof(void*))), 1);

    pcRelocTableInto(out, t1);
    out[4] = pcRelocTableInto(out + SLOTS(t1), gear);
    return (sMechaDataTable1*)out;
}

/* File 2: animation (keyframe table, then bytecode programs) and sound. The
 * block starts with the file's address, so that freeing the tables frees the
 * file too (freeMecha frees mAC_animData). */
sMechaDataTable2* pcConvertMechaData2(void* data) {
    u32* t2 = data;
    u32* anim = AT(t2, 1);
    u32* sound = AT(t2, 2);
    u32* keyframes = AT(anim, 1);
    void** block = allocateHeap(PC_HEAP_NATIVE((s32)((1 + SLOTS(t2) + SLOTS(anim) + SLOTS(sound) + SLOTS(keyframes)) * sizeof(void*))), 1);
    void** out2 = block + 1;
    void** outAnim = out2 + SLOTS(t2);
    void** outSound = outAnim + SLOTS(anim);
    void** outKeyframes = outSound + SLOTS(sound);

    block[0] = data;
    pcRelocTableInto(out2, t2);
    out2[1] = pcRelocTableInto(outAnim, anim);
    out2[2] = pcRelocTableInto(outSound, sound);
    outAnim[1] = pcRelocTableInto(outKeyframes, keyframes);
    return (sMechaDataTable2*)out2;
}

void pcFreeMechaData2(sMechaDataTable2* table) {
    void** block = (void**)table - 1;

    freeHeap(block[0]);
    freeHeap(block);
}
