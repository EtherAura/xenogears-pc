#ifndef MAIN_FILESYSTEM_H
#define MAIN_FILESYSTEM_H

/* Disc file loading (Noah: kernel/filesystem.cpp). */

#include "common.h"

/* One slot of the sector cache used by streamed file/image loads. */
typedef struct {
    /* 0x0 */ u16 m0_state;
    /* 0x2 */ u16 m2_sequence;
    /* 0x4 */ u16 m4_count;
    /* 0x6 */ u16 m6_size;
} SectorSlot;
extern SectorSlot* streamSectorSlots;

/* One entry of a batched file load (Noah: sLoadingBatchCommands); a list
 * ends with m0_fileIndex == 0. */
typedef struct {
    /* 0x0 */ u16 m0_fileIndex; /* loaded with lhu */
    /* 0x2 */ s16 m2;
    /* 0x4 */ void* m4_loadPtr;
} LoadingBatchCommand;

void initCDAndFileSystem(u8* fileTable, u16* directoryTable, s32 mode);
void shutdownCDAndFileSystem(void);
s32 setCurrentDirectory(s32 directory, s32 offset);
s32 getCurrentDirectory(s32* directory, s32* offset);
s32 isCDBusy(void);
s32 getFileSize(s32 fileIndex);
s32 getFileSizeAligned(s32 fileIndex);
s16 getNegativeFileSize(s32 fileIndex);
s32 getFileStartSector(s32 fileIndex);
s32 waitReadCompletion(s32 mode);
s32 readRawSectorsFromDisk(s32 sector, void* dest, s32 size, s32 a3, s32 a4);
s32 readFile(s32 fileIndex, void* dest, s32 a2, s32 a3);
s32 batchStartLoadingFiles(LoadingBatchCommand*, s32, s32);

/* Kernel movie request (filesystem.c; Noah: movieType, movieNumber,
 * movieReturnMode, movieFadeParam): kernel.c and battle_event fill it, the
 * movie overlay plays it. Retail orders the loads around these stores as
 * it does for a struct. */
typedef struct {
    /* 0x0 */ u8 m0_type;
    /* 0x1 */ u8 m1_number;
    /* 0x2 */ u8 m2_returnMode;
    /* 0x3 */ u8 m3_fadeParam;
} MovieRequest;
extern MovieRequest movieType;

/* A VRAM image move run over several frames (setupImageMoveCommand;
 * Noah: sBattleImageMoveCommand). */
typedef struct {
    /* 0x00 */ u16 m0_x;
    /* 0x02 */ u16 m2_y;
    /* 0x04 */ u16 m4_width;
    /* 0x06 */ s16 m6_totalHeight;
    /* 0x08 */ u16 m8_height;
    /* 0x0A */ u16 mA_count;
    /* 0x0C */ u16 mC_destX;
    /* 0x0E */ u16 mE_destY;
    /* 0x10 */ u8* m10_speeds;
    /* 0x14 */ u16* m14_offsets;
} ImageMoveCommand; /* 0x18 */

#endif /* MAIN_FILESYSTEM_H */
