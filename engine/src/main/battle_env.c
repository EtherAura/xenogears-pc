/*
 * Main executable 0x800379D8-0x80037B88: loadBattleMechaAndEnvironment. The only
 * function of 0x800379D8-0x8003F738 that calls game code (file system, heap);
 * the sound driver after it calls only PSY-Q and itself. GCC 2.6.0 (2.7.2 also
 * matches), ASPSX 2.34; -G0 and -G8 compile the same. Its object owns the .bss
 * item battleEnvLoadingCommands (0x8005A1DC, JP 0x80059878; 0x20 bytes up to
 * libc2 rand's), so it could become a static.
 */

#include "common.h"
#include "pc_retail_heap.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/battle.h"

static LoadingBatchCommand battleEnvLoadingCommands[4];  /* 0x20 in retail; [3] unused */


/* Load sound file pair number index from directory (0xC, 3): file
 * index*2+6 into *outA and file index*2+7+offset (kept in battleEnvMechaInitData+4). */
s32 loadBattleMechaAndEnvironment(s32 index, s32 offset, u8** outA, s32* outB, u8** outC) {
    s32 result;
    s32 dir;
    s32 dirOffset;
    u8* bufA;
    u8* bufB;
    s32 fileA;
    s32 file;

    result = 0;
    getCurrentDirectory(&dir, &dirOffset);
    setCurrentDirectory(0xC, 3);
    resetMemoryAllocStats(4, 0);
    if (index >= getNegativeFileSize(5) / 2) {
        result = -1;
        *outA = NULL;
        *outB = 0;
        *outC = NULL;
    } else {
        file = index * 2;
        bufB = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(file + 7 + offset)), 1);
        flagAllocation(bufB);
        fileA = file + 6;
        bufA = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(fileA)), 1);
        flagAllocation(bufA);
        battleEnvLoadingCommands[0].m0_fileIndex = fileA;
        battleEnvLoadingCommands[0].m4_loadPtr = bufA;
        file = file + (offset + 7);
        battleEnvLoadingCommands[1].m0_fileIndex = file;
        battleEnvLoadingCommands[1].m4_loadPtr = bufB;
        battleEnvLoadingCommands[2].m0_fileIndex = 0;
        battleEnvLoadingCommands[2].m4_loadPtr = NULL;
        batchStartLoadingFiles(battleEnvLoadingCommands, 0, 0);
        *outA = bufA;
        *outB = 0;
        *outC = (u8*)battleEnvLoadingCommands[1].m4_loadPtr + 4;
        battleEnvMechaInitData = (struct sBattleMechaInitData*)((u8*)battleEnvLoadingCommands[1].m4_loadPtr + 4);
    }
    setCurrentDirectory(dir, dirOffset);
    return result;
}
