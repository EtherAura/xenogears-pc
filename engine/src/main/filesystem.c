/*
 * CD file system (Noah: kernel/filesystem.cpp): directories, file sizes and
 * sectors, sector-buffer streaming, file reads, the CD read-error screen.
 * Also, at the start, a heap-free wrapper and VRAM scroll-command helpers,
 * which may be a separate source file (nothing in the code decides it).
 *
 * File 0x80027D40-0x80029690 (main executable), provisional. Built with GCC
 * 2.6.0 and -G0; see config/build.yaml.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/debug_font.h"

/* Stack locals of the fatal CD error screen (reportReadError). */
typedef struct {
    DRAWENV draw;
    DISPENV disp;
    OTag ot[16];
} ErrorScreenEnv;

/* Sector cache used while streaming files (streamSectorBuffer): a slot table
 * followed by 0x800-byte sectors. */
typedef struct {
    /* 0x0 */ s32 m0_count;
    /* 0x4 */ SectorSlot m4_slots[1];
} SectorBuffer;

/* Start of a streamed sector (first 0x20 bytes are a header). */
typedef struct {
    /* 0x00 */ u8 m0[6];
    /* 0x06 */ u16 m6_sectorCount;
    /* 0x08 */ u16 m8_frameNumber;
} SectorHeader;

extern s32 streamReadErrorCount;
s32 clearSectorBuffer(void);
void cdCommandSyncCallback(u8 status, u8* result);
void batchLoadReadyCallback(u8 status, u8* result);
void batchLoadDataCallback();
void streamDataCallback();
void imageFileDataCallback();
void imageFileReadyCallback(u8 status, u8* result);
void readImageFileSectorFromPc(s32, s32);
void processImageFileSectorPc(s32, s32);
/* file-local in retail (.bss: -G0 for cc1 and ASPSX), in retail order */
static u8 discardSectorBuffer[0x800];
static u32 readSectorHeader[3];
static s32 pcImageFileFd;
static CdlCB savedCdReadyCallback;
static s32 currentReadFileIndex;
static CdlLOC cdReadLocation;
static u8 cdFilterParam[2];
int pcRegionalFile(int index, const unsigned char* table, const unsigned short* dirs, void* copy);
int pcRegionalBatch(void* cmds, int first, const unsigned char* table, const unsigned short* dirs);

/* The CdlSetmode parameter: setCdMode and startFileRead clear 4 bytes here
 * (on the PS1 the next variable is 4 bytes on), so the PC gives it the room.
 * An array or struct on the PS1 changes getNextStreamedPacket's code. */
static u8 D_80059F18_block[4];
#define cdModeParam (D_80059F18_block[0])
static u8 cdControlResult[8];
static s16 imageFilePixelPosMode;
static u16 imageFilePixelX;
static u16 imageFilePixelY;
static s16 imageFileClutPosMode;
static u16 imageFileClutX;
static u16 imageFileClutY;
static s32 imageFileUnitsRemaining;
static s16 imageFileUploadX;
static s16 imageFileUploadY;
static s16 imageFileUploadWidth;
static u16* imageFileRowsPerSector;
static s32 imageFileSectorsRemaining;
static SectorHeader* pcStreamPacketHeader;
static u8* pcStreamPacketData;
static s16 pcStreamPacketSectorCount;
static s16 pcStreamPacketSectorIndex;
void readFileReadyCallback(u8 status, u8* result);
void streamReadyCallback(u8 status, u8* result);
void seekToFileUnchecked(s32 fileIndex);
/* .data (0x8004FDE0-0x8004FE50; JP 0x8004F484), in retail order. */
u32 batchMaxSkipSectors = 0x10;
s32 readFileErrorCount = 0;
s32 batchReadErrorCount = 0;
s32 streamReadErrorCount = 0;
u8* fatFileTableBuffer = NULL;
u16* fatDirectoryTableBuffer = NULL;
s32 remainingReadSize = 0;
s32 readInProgress = 0;
s32 batchFilesRemaining = 0;
s32 readCurrentSector = 0;
u8* readDestination = NULL;
LoadingBatchCommand* loadingBatchCommands = NULL;
s32 readCursorIndex = 0;
s32 startOfDirectoryFileIndex = 0;
s32 readDirectoryFileIndex = 0;
s32 cdCommandState = 0;
s32 cdErrorRecoveryAction = 0;
u16 streamNextConsumeSequence = 0;
u16 streamNextReadSequence = 0;
u16 streamNextDataSequence = 0;
SectorSlot* streamSectorSlots = NULL;
SectorBuffer* streamSectorBuffer = NULL;
s32 readStopRequested = 0;
s32 readNextSeekFile = 0;
s32 batchSkippingGap = 0;
s32 streamSectorSlotCount = 0;
/* Movie request for the kernel's movie mode: not used here (kernel.c and the
 * movie and battle_event overlays use them), but they sit between this
 * file's variables. */
MovieRequest movieType = { 0xFF, 0, 0, 0 };
sptr cdromFnd = 0;
s32 pcStreamFd = 0;
extern s32 cdErrorCount;
extern s32 cdReadSetlocCount;
extern s32 cdReadCommandOkCount;
extern s32 cdReadCommandErrorCount;
extern s32 cdReadRecoveryCount;
extern s32 cdReadRecoveryErrorCount;
extern s32 D_8005A49C;
extern s32 cdStopRecoveryCount;
extern s32 cdStopRecoveryOkCount;
extern s32 cdStopRecoveryErrorCount;
extern s16 streamFrameNumber;
void PCinit(void);
s32 PCclose(s32);
void setCdMode(s32);
void stopCdRead(s32);
/* Merges a free slot run with the free run that follows it. */
void mergeFreeSectorSlots(s32);
/* Returns 1 unless count slots from index are all in the given state. */
s32 hasSectorSlotNotInState(s32, s32, s32);
/* PC (DTL) build: host file name of a file, NULL on CD. */
char* getFilenameFromPC(s32);
s32 startFileRead(s32 fileIndex, u8* dest, s32 channel, s32 flags);
/* File size rounded up to 4, in the directory of the file being loaded. */
s32 getLoadingFileSizeAligned(s32);
/* CD/PC read error feedback: draws a coloured bar per retry; on the fourth
 * failure resets the GPU and loops forever printing the file index and name. */
void reportReadError(s32, s32, s32, s32);

/* Frees ptr if it is not NULL. */
void freeHeapIfNotNull(void* ptr) {
    if (ptr != NULL) {
        freeHeap(ptr);
    }
}

/* Sets up a VRAM scroll command and allocates its per-row offsets. */
ImageMoveCommand* setupImageMoveCommand(ImageMoveCommand* cmd, s16 x, s16 y, s16 width, s16 a4, s16 count, u16 a6,
                                u16 a7, u8* speeds) {
    s32 i;

    resetMemoryAllocStats(4, 0);
    cmd->m0_x = x;
    cmd->m2_y = y;
    cmd->m4_width = width;
    cmd->m6_totalHeight = a4;
    cmd->m8_height = a4 / count;
    cmd->mA_count = count;
    cmd->mC_destX = a6;
    cmd->mE_destY = a7;
    cmd->m10_speeds = speeds;
    cmd->m14_offsets = allocateHeap(PC_HEAP_BYTES(count * 2), 0);
    if (cmd->m14_offsets == NULL) {
        cmd = NULL;
    } else {
        for (i = 0; i < count; i++) {
            cmd->m14_offsets[i] = 0;
        }
    }
    return cmd;
}

/* Noah: executeBattleMoveImageCommands. Scrolls each row of the area by its
 * speed, wrapping horizontally with two MoveImage calls. */
void executeBattleMoveImageCommands(ImageMoveCommand* cmd) {
    RECT rect;
    s32 i;
    u16 x;
    u16 w;
    u16 y;

    if (cmd->m14_offsets == NULL) {
        return;
    }
    rect.y = cmd->m2_y;
    rect.h = cmd->m8_height;
    y = cmd->mE_destY;
    for (i = 0; i < cmd->mA_count; i++) {
        cmd->m14_offsets[i] += (s8)cmd->m10_speeds[i];
        x = (u16)((s16)cmd->m14_offsets[i] >> 4) % cmd->m4_width;
        w = cmd->m4_width - x;
        rect.x = cmd->m0_x;
        rect.w = x;
        MoveImage(&rect, cmd->mC_destX + w, y);
        rect.x = x + cmd->m0_x;
        rect.w = w;
        MoveImage(&rect, cmd->mC_destX, y);
        y += cmd->m8_height;
        rect.y += cmd->m8_height;
    }
}

/* Frees a VRAM scroll command's row offsets. */
void freeImageMoveCommand(ImageMoveCommand* cmd) {
    if (cmd->m14_offsets != NULL) {
        freeHeap(cmd->m14_offsets);
        cmd->m14_offsets = NULL;
    }
}

void reportReadError(s32 retry, s32 r, s32 g, s32 b) {
    RECT rect;
    ErrorScreenEnv env;

    rect.x = retry * 10 + 10;
    rect.y = 0;
    rect.w = 8;
    rect.h = 0x1C0;
    if (retry + 1 >= 4) {
        r = 0xFF;
        g = 0xFF;
        b = 0xFF;
    }
    ClearImage(&rect, (u8)r, (u8)g, (u8)b);
    DrawSync(0);
    if (retry + 1 >= 4) {
        ResetGraph(0);
        InitGeom();
        SetDefDrawEnv(&env.draw, 0, 0, 0x140, 0x100);
        SetDefDispEnv(&env.disp, 0, 0, 0x140, 0xF0);
        env.draw.dtd = 0;
        env.draw.isbg = 0;
        env.draw.dfe = 1;
        env.disp.isinter = 0;
        PutDrawEnv(&env.draw);
        PutDispEnv(&env.disp);
        initDebugFont(0x10, 0x10, 0x280, 0xF0, 0x400, 0, 0x280, 0, 0x280, 0x100, 0);
        SetDispMask(1);
        rect.x = 0;
        rect.y = 0;
        rect.w = 0x280;
        rect.h = 0x30;
        ClearImage(&rect, 0, 0, 0);
        while (1) {
            ClearOTagR(env.ot, 8);
            logToScreen("\n%d", startOfDirectoryFileIndex + currentReadFileIndex - 1);
            logToScreen("\n%s", getFilenameFromPC(currentReadFileIndex));
            printDebugTextOnScreen(env.ot);
            DrawOTag(&env.ot[7]);
            DrawSync(0);
        }
    }
}

/* Noah: initCDAndFileSystem. mode 0 = CD, -1 = CD with no PC file names,
 * otherwise a pointer to the PC (DTL) file name table (64 bytes per file). */
void initCDAndFileSystem(u8* fileTable, u16* directoryTable, s32 mode) {
    cdReadSetlocCount = 0;
    cdReadCommandOkCount = 0;
    cdReadCommandErrorCount = 0;
    cdReadRecoveryCount = 0;
    cdReadRecoveryErrorCount = 0;
    D_8005A49C = 0;
    cdStopRecoveryCount = 0;
    cdStopRecoveryOkCount = 0;
    cdStopRecoveryErrorCount = 0;
    if (mode == 0) {
        goto cd;
    }
    if (mode == -1) {
    cd:
        while (CdInit() == 0) {
        }
        CdSetDebug(0);
        CdDataCallback(0);
        CdSyncCallback(0);
        CdReadyCallback(0);
        CdControl(7, 0, cdControlResult);
        setCdMode(0xA0);
        waitReadCompletion(0);
        VSync(3);
    } else {
        PCinit();
    }
    if (mode != -1) {
        cdromFnd = mode;
    } else {
        cdromFnd = 0;
    }
    fatFileTableBuffer = fileTable;
    fatDirectoryTableBuffer = directoryTable;
    startOfDirectoryFileIndex = 0;
    readInProgress = 0;
    remainingReadSize = 0;
    cdCommandState = 0;
    pcStreamFd = -1;
    if (mode == 0) {
        readRawSectorsFromDisk(0x18, fileTable, 0x8000, 0, 0);
        waitReadCompletion(0);
        readRawSectorsFromDisk(0x28, fatDirectoryTableBuffer, 0x7A, 0, 0);
        waitReadCompletion(0);
    }
}

/* Stops streaming and pauses the drive (softReboot). */
void shutdownCDAndFileSystem(void) {
    stopCdRead(0);
    waitReadCompletion(0);
    if (cdromFnd == 0) {
        while (CdControlB(9, 0, cdControlResult) == 0) {
        }
        setCdMode(0xA0);
        waitReadCompletion(0);
        VSync(3);
    }
    CdDataCallback(0);
    CdSyncCallback(0);
    CdReadyCallback(0);
    readInProgress = 0;
    remainingReadSize = 0;
    cdCommandState = 0;
}

/* Noah: setCurrentDirectory. */
s32 setCurrentDirectory(s32 directory, s32 offset) {
    startOfDirectoryFileIndex = fatDirectoryTableBuffer[directory + offset] - 1;
    if (startOfDirectoryFileIndex < 0) {
        startOfDirectoryFileIndex = 0;
        return -1;
    }
    return startOfDirectoryFileIndex;
}

/* Noah: getCurrentDirectory. */
s32 getCurrentDirectory(s32* directory, s32* offset) {
    s32 i;
    u16* table;

    for (table = fatDirectoryTableBuffer, i = 0; i < 0x40; i++, table++) {
        if (*table == startOfDirectoryFileIndex + 1) {
            *directory = (i / 4) * 4;
            *offset = i - (i / 4) * 4;
            break;
        }
    }
    if (i == 0x40) {
        *directory = 0;
        *offset = 0;
    }
    return startOfDirectoryFileIndex;
}

/* Directory table entry 0x3C (kernel.c compares it with 1: disc number). */
s32 getCurrentDiscNumber(void) {
    return fatDirectoryTableBuffer[0x3C];
}

/* First file index of a directory, relative to the current directory. */
s32 getDirectoryFileOffset(s32 directory, s32 offset) {
    return fatDirectoryTableBuffer[directory + offset] - startOfDirectoryFileIndex;
}

/* PC (DTL) build: loads a whole host file into a new heap block. */
void* loadPcHostFile(char* name, s32* sizeOut) {
    s32 i;
    s32 fd;
    s32 size;
    void* buf;
    s32 ret;

    for (i = 0; i < 4; i++) {
        fd = PCopen(name, 0, 0);
        if (fd != -1) {
            break;
        }
    }
    if (fd == -1) {
        goto fail;
    }
    size = PClseek(fd, 0, 2);
    if (sizeOut != NULL) {
        *sizeOut = size;
    }
    PClseek(fd, 0, 0);
    buf = allocateHeap(size, 0);
    ret = 0;
    if (buf != NULL) {
        for (i = 0; i < 4; i++) {
            ret = PCread(fd, buf, size);
            if (ret != 0) {
                break;
            }
        }
    }
    if (ret == 0) {
        if (buf != NULL) {
            freeHeap(buf);
        }
        buf = NULL;
    }
    for (i = 0; i < 4; i++) {
        if (PCclose(fd) == 0) {
            goto done;
        }
    }
    if (buf != NULL) {
        freeHeap(buf);
    }
fail:
    buf = NULL;
done:
    return buf;
}

/* Bytes left to read in the current transfer. */
s32 getRemainingReadSize(void) {
    return remainingReadSize;
}

/* Noah: isCDBusy. */
s32 isCDBusy(void) {
    s32 busy = readInProgress;

    if (busy == 0) {
        if (cdromFnd == 0 && CdDataSync(1) != 0) {
            return 1;
        }
        if (cdCommandState != 0) {
            return 1;
        }
    }
    return busy;
}

/* Noah: getFileSize. */
s32 getFileSize(s32 fileIndex) {
    s32 fd;
    s32 size;
    u8* entry;

    if (cdromFnd == 0) {
        size = pcRegionalFile(fileIndex + startOfDirectoryFileIndex - 1, fatFileTableBuffer, fatDirectoryTableBuffer, NULL);
        if (size > 0) return size;
    }
    if (cdromFnd != 0) {
        fd = PCopen(getFilenameFromPC(fileIndex), 0, 0);
        size = PClseek(fd, 0, 2);
        PCclose(fd);
        if (size > 0) {
            goto end;
        }
    }
    entry = &fatFileTableBuffer[(fileIndex + startOfDirectoryFileIndex - 1) * 7];
    size = (entry[6] << 24) + (entry[5] << 16) + (entry[4] << 8) + entry[3];
end:
    return size;
}

s32 getLoadingFileSizeAligned(s32 fileIndex) {
    s32 fd;
    s32 size;
    u8* entry;

    if (cdromFnd != 0) {
        fd = PCopen(getFilenameFromPC(fileIndex), 0, 0);
        size = PClseek(fd, 0, 2);
        PCclose(fd);
        if (size > 0) {
            goto end;
        }
    }
    entry = &fatFileTableBuffer[(fileIndex + readDirectoryFileIndex - 1) * 7];
    size = (entry[6] << 24) + (entry[5] << 16) + (entry[4] << 8) + entry[3];
end:
    return (size + 3) / 4 * 4;
}

/* Noah: getFileSizeAligned. */
s32 getFileSizeAligned(s32 fileIndex) {
    s32 size = getFileSize(fileIndex);
    return (size + 3) / 4 * 4;
}

/* Noah: getNegativeFileSize. */
s16 getNegativeFileSize(s32 fileIndex) {
    u8* entry = &fatFileTableBuffer[(fileIndex + startOfDirectoryFileIndex - 1) * 7];
    s32 size = (entry[6] << 24) + (entry[5] << 16) + (entry[4] << 8) + entry[3];

    if (size < 0) {
        return (s16)-size;
    }
    return 0;
}

char* getFilenameFromPC(s32 fileIndex) {
    char* name = NULL;

    if (cdromFnd != 0) {
        name = (char*)cdromFnd + (fileIndex + startOfDirectoryFileIndex - 1) * 64;
    }
    return name;
}

s32 getFileStartSector(s32 fileIndex) {
    u8* entry = &fatFileTableBuffer[(fileIndex + startOfDirectoryFileIndex - 1) * 7];
    return ((entry[2] << 16) + (entry[1] << 8)) | entry[0];
}

/* Start sector of a file in the directory of the file being loaded. */
s32 getLoadingFileStartSector(s32 fileIndex) {
    u8* entry = &fatFileTableBuffer[(fileIndex + readDirectoryFileIndex - 1) * 7];
    return ((entry[2] << 16) + (entry[1] << 8)) | entry[0];
}

/* Noah: waitReadCompletion. */
s32 waitReadCompletion(s32 mode) {
    if (mode == 0) {
        while (isCDBusy() > 0) {
            PORT_YIELD();
        }
    }
    return isCDBusy();
}

/* Installs the sector buffer used for streaming; returns the previous one. */
SectorBuffer* setSectorBuffer(SectorBuffer* buffer) {
    SectorBuffer* old = streamSectorBuffer;
    streamSectorBuffer = buffer;
    return old;
}

/* Clears every slot of the sector buffer.  The unused local only reserves the
 * 8-byte stack frame retail has. */
s32 clearSectorBuffer(void) {
    SectorBuffer* buf = streamSectorBuffer;
    SectorSlot* slots;
    s32 unused[1];
    s32 count;
    s32 i;

    if (buf == NULL) {
        return -1;
    }
    count = buf->m0_count;
    slots = buf->m4_slots;
    for (i = 0; i < count; i++) {
        slots[i].m0_state = 0;
        slots[i].m2_sequence = 0;
        slots[i].m4_count = 0;
        slots[i].m6_size = 0;
    }
    slots->m4_count = count;
    return count;
}

/* Returns the next streamed 0x800-byte sector, or NULL if none is ready. */
u8* getNextStreamedSector(void) {
    u8* data;
    SectorSlot* slot;
    LoadingBatchCommand* cmd;
    char* name;
    s32 count;
    s32 index;
    s32 fileIndex;
    s32 i;

    if (streamSectorBuffer == NULL) {
        return NULL;
    }
    data = (u8*)streamSectorBuffer;
    slot = ((SectorBuffer*)data)->m4_slots;
    count = ((SectorBuffer*)data)->m0_count;
    data = data + count * 8 + 0x24;
    if (cdromFnd != 0) {
        if (pcStreamFd == -1) {
            return NULL;
        }
        if (remainingReadSize <= 0) {
            return NULL;
        }
        for (i = 0; i < streamSectorSlotCount; i++) {
            slot = &streamSectorSlots[readCursorIndex];
            index = readCursorIndex;
            readCursorIndex = index + 1;
            if (readCursorIndex >= streamSectorSlotCount) {
                readCursorIndex = 0;
            }
            if (slot->m0_state == 0) {
                break;
            }
        }
        if (slot->m0_state != 0) {
            return NULL;
        }
        slot->m0_state = 3;
        data += index << 11;
        for (i = 0; i < 4; i++) {
            if (PCread(pcStreamFd, data, 0x800) != 0) {
                goto read_ok;
            }
            reportReadError(i, 0, 0xFF, 0);
        }
        return NULL;
    read_ok:
        remainingReadSize -= 0x800;
        if (remainingReadSize > 0) {
            return data;
        }
        remainingReadSize = 0;
        for (i = 0; i < 4; i++) {
            if (PCclose(pcStreamFd) == 0) {
                break;
            }
            reportReadError(i, 0, 0, 0xFF);
        }
        pcStreamFd = -1;
        if (loadingBatchCommands != NULL) {
            readCursorIndex++;
            cmd = &loadingBatchCommands[readCursorIndex];
            fileIndex = cmd->m0_fileIndex;
            currentReadFileIndex = fileIndex;
            if (fileIndex > 0 && cmd->m4_loadPtr != NULL) {
                name = getFilenameFromPC(fileIndex);
                for (i = 0; i < 4; i++) {
                    pcStreamFd = PCopen(name, 0, 0);
                    if (pcStreamFd != -1) {
                        break;
                    }
                    reportReadError(i, 0xFF, 0, 0);
                }
                remainingReadSize = getLoadingFileSizeAligned(fileIndex);
                readInProgress--;
                return data;
            }
            remainingReadSize = 0;
        }
        readInProgress = 0;
        return data;
    }
    for (i = 0; i < count; i++, slot++) {
        if (slot->m0_state == 3 && slot->m2_sequence == streamNextConsumeSequence) {
            break;
        }
    }
    if (i == streamSectorSlotCount) {
        return NULL;
    }
    streamNextConsumeSequence++;
    return data + (i << 11);
}

s32 hasSectorSlotNotInState(s32 index, s32 count, s32 state) {
    s32 unused[1];
    s32 i;

    for (i = 0; i < count; i++) {
        if (streamSectorSlots[index].m0_state != state) {
            return 1;
        }
        index++;
        if (index > streamSectorSlotCount) {
            return 1;
        }
    }
    return 0;
}

void mergeFreeSectorSlots(s32 index) {
    s32 unused[1];
    s16 count = streamSectorSlots[index].m4_count;
    s32 next = index + count;

    if (next < streamSectorSlotCount) {
        if (streamSectorSlots[next].m0_state == 0) {
            streamSectorSlots[index].m4_count = count + streamSectorSlots[next].m4_count;
        }
    }
}

/* Returns 0 and the next complete streamed packet (header + payload), or 1.
 * The state stores go through a u16 pointer: a plain (non-struct) store makes
 * CSE forget pcStreamPacketSectorCount and streamNextReadSequence, as retail's reloads show. */
s32 getNextStreamedPacket(u8** outData, SectorHeader** outSector) {
    SectorSlot* slots;
    u16* slot;
    SectorHeader* sector;
    u8* data;
    s32 count;
    s32 i;
    s32 remaining;
    s32 unused[1];

    if (streamSectorBuffer == NULL) {
        return 1;
    }
    data = (u8*)streamSectorBuffer;
    slots = ((SectorBuffer*)data)->m4_slots;
    count = ((SectorBuffer*)data)->m0_count;
    data = data + count * 8 + 0x24;
    if (cdromFnd != 0 && pcStreamFd != -1 && remainingReadSize > 0) {
        if (pcStreamPacketSectorIndex == 0) {
            for (i = 0; i < streamSectorSlotCount; i += slot[2]) {
                slot = (u16*)&streamSectorSlots[i];
                if (slot[0] == 0) {
                    break;
                }
            }
            if (i < streamSectorSlotCount) {
                if (cdModeParam & 8) {
                    PCread(pcStreamFd, discardSectorBuffer, 8);
                    if (discardSectorBuffer[0] == 1) {
                        goto skip;
                    }
                }
                sector = pcStreamPacketHeader = (SectorHeader*)((u8*)readDestination + (i << 11));
                PCread(pcStreamFd, sector, 0x20);
                pcStreamPacketSectorCount = sector->m6_sectorCount;
                streamFrameNumber = sector->m8_frameNumber;
                if (slot[2] < pcStreamPacketSectorCount) {
                    if (cdModeParam & 8) {
                        PClseek(pcStreamFd, -0x28, 1);
                    } else {
                        PClseek(pcStreamFd, -0x20, 1);
                    }
                } else {
                    slot[1] = streamNextReadSequence;
                    slot[0] = 3;
                    remaining = slot[2] - pcStreamPacketSectorCount;
                    if (remaining >= 3) {
                        slot[2] = pcStreamPacketSectorCount + 1;
                        slot[(pcStreamPacketSectorCount + 1) * 4 + 2] = remaining - 1;
                        slot[(pcStreamPacketSectorCount + 1) * 4] = 0;
                        mergeFreeSectorSlots(pcStreamPacketSectorCount + 1);
                    }
                    streamNextReadSequence++;
                    pcStreamPacketData = (u8*)readDestination + (i << 11) + pcStreamPacketSectorCount * 32;
                    PCread(pcStreamFd, pcStreamPacketData, 0x7E0);
                    if (cdModeParam & 8) {
                        PClseek(pcStreamFd, 0x118, 1);
                    }
                    readCursorIndex = i;
                    remainingReadSize -= 0x800;
                    pcStreamPacketSectorIndex++;
                }
            }
        } else {
            if (cdModeParam & 8) {
                PCread(pcStreamFd, discardSectorBuffer, 8);
                if (discardSectorBuffer[0] == 1) {
                skip:
                    PClseek(pcStreamFd, 0x918, 1);
                    goto end;
                }
            }
            readCursorIndex++;
            slot = (u16*)&streamSectorSlots[readCursorIndex];
            slot[1] = streamNextReadSequence;
            slot[0] = 3;
            streamNextReadSequence++;
            PCread(pcStreamFd, (u8*)pcStreamPacketHeader + pcStreamPacketSectorIndex * 32, 0x20);
            PCread(pcStreamFd, pcStreamPacketData + pcStreamPacketSectorIndex * 0x7E0, 0x7E0);
            if (cdModeParam & 8) {
                PClseek(pcStreamFd, 0x118, 1);
            }
            remainingReadSize -= 0x800;
            pcStreamPacketSectorIndex++;
            if (pcStreamPacketSectorIndex >= pcStreamPacketSectorCount) {
                pcStreamPacketSectorIndex = 0;
            }
        }
    }
end:
    *outSector = NULL;
    for (i = 0; i < count; i++, slots++) {
        if (slots->m0_state == 3 && slots->m2_sequence == streamNextConsumeSequence) {
            break;
        }
    }
    if (i == streamSectorSlotCount) {
        return 1;
    }
    sector = (SectorHeader*)(data + (i << 11));
    *outSector = sector;
    *outData = (u8*)sector + sector->m6_sectorCount * 32;
    if (hasSectorSlotNotInState(i, sector->m6_sectorCount, 3) != 0) {
        return 1;
    }
    streamNextConsumeSequence += sector->m6_sectorCount;
    return 0;
}

/* Releases one sector slot; returns its previous state. */
s32 releaseSectorSlot(u8* sector) {
    SectorBuffer* buf = streamSectorBuffer;
    SectorSlot* slots;
    u8* data;
    u32 index;
    u16 state;

    if (buf == NULL) {
        return 0xFFFF;
    }
    if (sector == NULL) {
        return 0;
    }
    slots = buf->m4_slots;
    data = (u8*)buf + buf->m0_count * 8 + 0x24;
    index = (u32)(sector - data) >> 11;
    state = slots[index].m0_state;
    slots[index].m0_state = 0;
    return state;
}

/* Releases all slots of a streamed packet and merges the free runs. */
s32 releaseStreamedPacket(SectorHeader* sector) {
    SectorBuffer* buf = streamSectorBuffer;
    SectorSlot* slots;
    u8* data;
    u32 index;
    s32 n;
    u16 state;

    if (buf == NULL) {
        return 0xFFFF;
    }
    if (sector == NULL) {
        return 0;
    }
    slots = buf->m4_slots;
    n = sector->m6_sectorCount;
    data = (u8*)buf + buf->m0_count * 8 + 0x24;
    index = (u32)((u8*)sector - data) >> 11;
    state = slots[index].m0_state;
    while (n > 0) {
        slots[index + n - 1].m0_state = 0;
        n--;
    }
    mergeFreeSectorSlots(index);
    return state;
}

s32 readRawSectorsFromDisk(s32 sector, void* dest, s32 size, s32 a3, s32 a4) {
    if (cdromFnd != 0) {
        return -1;
    }
    waitReadCompletion(0);
    readCurrentSector = sector;
    remainingReadSize = size;
    return startFileRead(0, dest, a3, a4);
}

/* Noah: readFile. */
s32 readFile(s32 fileIndex, void* dest, s32 a2, s32 a3) {
    if (fileIndex <= 0 || getFileSize(fileIndex) <= 0 || dest == NULL) {
        return -3;
    }
    waitReadCompletion(0);
    if (cdromFnd == 0 && !(a3 & 0x300) &&
        pcRegionalFile(fileIndex + startOfDirectoryFileIndex - 1, fatFileTableBuffer, fatDirectoryTableBuffer, dest) > 0) return 0;
    readDirectoryFileIndex = startOfDirectoryFileIndex;
    readCurrentSector = getFileStartSector(fileIndex);
    remainingReadSize = getFileSizeAligned(fileIndex);
    return startFileRead(fileIndex, dest, a2, a3);
}

s32 startFileRead(s32 fileIndex, u8* dest, s32 channel, s32 flags) {
    s32 i;
    u32* q;
    u8* p;
    CdlLOC* pos;
    s32* buf;
    s32 n;
    char* name;
    s32 fd;
    CdlCB cb;

    currentReadFileIndex = fileIndex;
    for (i = 2, q = &readSectorHeader[2]; i >= 0; i--) {
        *q-- = 0;
    }
    pos = &cdReadLocation;
    readInProgress = 1;
    readDestination = dest;
    readNextSeekFile = (u16)channel;
    readCursorIndex = 0;
    loadingBatchCommands = 0;
    readStopRequested = 0;
    cdErrorCount = 0;
    CdIntToPos(readCurrentSector, pos);
    if (flags & 0x100) {
        setSectorBuffer(dest);
        buf = streamSectorBuffer;
        n = buf[0];
        if (n == 0) {
            return -4;
        }
        readDestination = (u8*)buf + n * 8 + 0x24;
        streamSectorSlots = (u16*)(buf + 1);
        streamSectorSlotCount = n;
        streamNextReadSequence = 0;
        streamNextDataSequence = 0;
        streamNextConsumeSequence = 0;
        clearSectorBuffer();
        if (cdromFnd != 0) {
            name = getFilenameFromPC(fileIndex);
            for (i = 0; i < 4; i++) {
                pcStreamFd = PCopen(name, 0, 0);
                if (pcStreamFd != -1) {
                    break;
                }
                reportReadError(i, 0xFF, 0, 0);
            }
            return (pcStreamFd == -1) ? -3 : 0;
        }
        cdCommandState = 1;
        CdDataCallback(streamDataCallback);
        CdSyncCallback(cdCommandSyncCallback);
        CdReadyCallback(streamReadyCallback);
        cdReadSetlocCount++;
        CdControlF(2, (u8*)pos);
        return 0;
    } else if (flags & 0x200) {
        setSectorBuffer(dest);
        buf = streamSectorBuffer;
        n = buf[0];
        if (n == 0) {
            return -4;
        }
        readDestination = (u8*)buf + n * 8 + 0x24;
        streamSectorSlots = (u16*)(buf + 1);
        streamSectorSlotCount = n;
        pcStreamPacketSectorIndex = 0;
        streamNextReadSequence = 0;
        streamNextDataSequence = 0;
        streamNextConsumeSequence = 0;
        clearSectorBuffer();
        for (i = 3, p = &cdModeParam + 3; i >= 0; i--) {
            *p-- = 0;
        }
        *&cdModeParam = flags | 0xA0;
        if (cdromFnd == 0) {
            return 0;
        }
        name = getFilenameFromPC(fileIndex);
        for (i = 0; i < 4; i++) {
            pcStreamFd = PCopen(name, 0, 0);
            if (pcStreamFd != -1) {
                break;
            }
            reportReadError(i, 0xFF, 0, 0);
        }
        return (pcStreamFd == -1) ? -3 : 0;
    } else {
        if (cdromFnd != 0) {
            char* path = getFilenameFromPC(fileIndex);
            for (i = 0; i < 4; i++) {
                fd = PCopen(path, 0, 0);
                if (fd != -1) {
                    break;
                }
                reportReadError(i, 0xFF, 0, 0);
            }
            if (fd == -1) {
                return -4;
            }
            if (dest != NULL) {
                for (i = 0; i < 4; i++) {
                    if (PCread(fd, dest, remainingReadSize) != 0) {
                        break;
                    }
                    reportReadError(i, 0, 0xFF, 0);
                }
            }
            for (i = 0; i < 4; i++) {
                if (PCclose(fd) == 0) {
                    remainingReadSize = 0;
                    readInProgress = 0;
                    return 0;
                }
                reportReadError(i, 0, 0, 0xFF);
            }
            return -6;
        }
        cdCommandState = 1;
        CdDataCallback(0);
        CdSyncCallback(cdCommandSyncCallback);
        CdReadyCallback(readFileReadyCallback);
        cdReadSetlocCount++;
        CdControlF(2, (u8*)pos);
        return 0;
    }
}

s32 batchStartLoadingFiles(LoadingBatchCommand* cmds, s32 channel, s32 flags) {
    s32 count;
    s32 i;
    s32 j;
    s32 minIdx;
    u16 min;
    void* tmp;
    LoadingBatchCommand* p;
    u32* q;
    u16 fileIndex;
    char* name;
    s32 fd;
    CdlLOC* pos;

    if (cmds == NULL) {
        return -3;
    }
    count = 0;
    if (cmds->m0_fileIndex != 0) {
        p = cmds;
        do {
            p++;
            count++;
        } while (p->m0_fileIndex != 0);
    }
    if (count == 0) {
        return -3;
    }
    for (i = 0; i < count - 1; i++) {
        min = cmds[i].m0_fileIndex;
        minIdx = i;
        for (j = i + 1; j < count; j++) {
            if (cmds[j].m0_fileIndex < min) {
                minIdx = j;
                min = cmds[j].m0_fileIndex;
            }
        }
        min = cmds[i].m0_fileIndex;
        tmp = cmds[i].m4_loadPtr;
        cmds[i].m0_fileIndex = cmds[minIdx].m0_fileIndex;
        cmds[i].m4_loadPtr = cmds[minIdx].m4_loadPtr;
        cmds[minIdx].m0_fileIndex = min;
        cmds[minIdx].m4_loadPtr = tmp;
    }
    waitReadCompletion(0);
    if (cdromFnd == 0 && !(flags & 0x300)) {
        s32 regional = pcRegionalBatch(cmds, startOfDirectoryFileIndex, fatFileTableBuffer, fatDirectoryTableBuffer);
        if (regional != 0) return regional > 0 ? 0 : -3;
    }
    readDirectoryFileIndex = startOfDirectoryFileIndex;
    for (i = 2, q = &readSectorHeader[2]; i >= 0; i--) {
        *q-- = 0;
    }
    readCursorIndex = 0;
    loadingBatchCommands = cmds;
    readInProgress = count;
    batchFilesRemaining = count;
    readDestination = cmds->m4_loadPtr;
    fileIndex = cmds->m0_fileIndex;
    if (fileIndex == 0 || readDestination == NULL) {
        seekToFileUnchecked(channel);
        goto done;
    }
    currentReadFileIndex = fileIndex;
    readCurrentSector = getFileStartSector(fileIndex);
    remainingReadSize = getLoadingFileSizeAligned(fileIndex);
    pos = &cdReadLocation;
    readNextSeekFile = (u16)channel;
    batchSkippingGap = 0;
    readStopRequested = 0;
    cdErrorCount = 0;
    CdIntToPos(readCurrentSector, pos);
    if (cdromFnd != 0) {
        for (i = 0; i < count; i++) {
            min = cmds[i].m0_fileIndex;
            currentReadFileIndex = min;
            name = getFilenameFromPC(min);
            for (j = 0; j < 4; j++) {
                fd = PCopen(name, 0, 0);
                if (fd != -1) {
                    goto opened;
                }
                reportReadError(j, 0xFF, 0, 0);
            }
            goto close;
        opened:
            if (cmds[i].m4_loadPtr != NULL) {
                for (j = 0; j < 4; j++) {
                    if (PCread(fd, cmds[i].m4_loadPtr, getLoadingFileSizeAligned(min)) != 0) {
                        break;
                    }
                    reportReadError(j, 0, 0xFF, 0);
                }
            }
        close:
            for (j = 0; j < 4; j++) {
                if (PCclose(fd) == 0) {
                    break;
                }
                reportReadError(j, 0, 0, 0xFF);
            }
        }
    done:
        remainingReadSize = 0;
        readInProgress = 0;
        return 0;
    }
    cdCommandState = 1;
    CdDataCallback(batchLoadDataCallback);
    CdSyncCallback(cdCommandSyncCallback);
    CdReadyCallback(batchLoadReadyCallback);
    cdReadSetlocCount++;
    CdControlF(2, (u8*)pos);
    return 0;
}

s32 loadImageFileToVram(s32 fileIndex, s32* buf, s32 channel, s32 flags, u16 arg4, u16 arg5, u16 arg6, u16 arg7, u16 arg8, u16 arg9) {
    s16 i;
    u32 n;
    char* name;
    CdlLOC* pos;

    if (buf == NULL || (n = buf[0]) < 2) {
        return -4;
    }
    if (fileIndex <= 0) {
        return -3;
    }
    if (getFileSize(fileIndex) <= 0) {
        return -3;
    }
    waitReadCompletion(0);
    readDirectoryFileIndex = startOfDirectoryFileIndex;
    for (i = 0; i < 3; i++) {
        readSectorHeader[i] = 0;
    }
    setSectorBuffer(buf);
    currentReadFileIndex = fileIndex;
    readCurrentSector = getFileStartSector(fileIndex);
    remainingReadSize = getFileSizeAligned(fileIndex);
    readInProgress = 1;
    readDestination = (u8*)buf + n * 8 + 0x24;
    streamSectorSlots = (u16*)(buf + 1);
    readNextSeekFile = (u16)channel;
    readCursorIndex = 0;
    streamSectorSlotCount = n;
    streamNextReadSequence = 0;
    streamNextDataSequence = 0;
    loadingBatchCommands = 0;
    readStopRequested = 0;
    cdErrorCount = 0;
    imageFilePixelPosMode = arg4;
    imageFilePixelX = arg5;
    imageFilePixelY = arg6;
    imageFileClutPosMode = arg7;
    imageFileClutX = arg8;
    imageFileUnitsRemaining = 0;
    imageFileUploadX = 0;
    imageFileUploadY = 0;
    imageFileUploadWidth = 0;
    imageFileRowsPerSector = 0;
    imageFileSectorsRemaining = 0;
    imageFileClutY = arg9;
    clearSectorBuffer();
    pos = &cdReadLocation;
    CdIntToPos(readCurrentSector, pos);
    if (cdromFnd != 0) {
        name = getFilenameFromPC(fileIndex);
        for (i = 0; i < 4; i++) {
            pcImageFileFd = PCopen(name, 0, 0);
            if (pcImageFileFd != -1) {
                break;
            }
            reportReadError(i, 0xFF, 0, 0);
        }
        do {
            readImageFileSectorFromPc(0, 0);
            processImageFileSectorPc(0, 0);
        } while (readInProgress > 0);
        for (i = 0; i < 4; i++) {
            i = PCclose(pcImageFileFd);
            if (i == 0) {
                break;
            }
            reportReadError(i, 0, 0, 0xFF);
        }
        if (i != 0) {
            return -6;
        }
        readInProgress = 0;
        remainingReadSize = 0;
        return 0;
    } else {
        cdCommandState = 1;
        CdDataCallback(imageFileDataCallback);
        CdSyncCallback(cdCommandSyncCallback);
        CdReadyCallback(imageFileReadyCallback);
        cdReadSetlocCount++;
        CdControlF(2, (u8*)pos);
        return 0;
    }
}

s32* allocateBufferForVramUpload(s32 count, s32 flags) {
    s32* buf;

    if (count > 0) {
        buf = allocateHeap(PC_HEAP_BYTES(count * 0x808 + 0x24), flags);
        if (buf != NULL) {
            *buf = count;
            setSectorBuffer(buf);
            clearSectorBuffer();
            return buf;
        }
        return NULL;
    }
    return NULL;
}

void seekToFile(s32 fileIndex) {
    if (cdromFnd == 0 && isCDBusy() == 0) {
        readDirectoryFileIndex = startOfDirectoryFileIndex;
        if (fileIndex > 0) {
            CdlLOC* pos = &cdReadLocation;
            CdIntToPos(getFileStartSector(fileIndex), pos);
            cdCommandState = 3;
            CdSyncCallback(cdCommandSyncCallback);
            CdControlF(2, (u8*)pos);
        } else {
            cdCommandState = 5;
            CdSyncCallback(cdCommandSyncCallback);
            CdControlF(9, 0);
        }
    }
}

void seekToFileUnchecked(s32 fileIndex) {
    if (fileIndex > 0) {
        CdlLOC* pos = &cdReadLocation;
        CdIntToPos(getFileStartSector(fileIndex), pos);
        cdCommandState = 3;
        CdSyncCallback(cdCommandSyncCallback);
        CdControlF(2, (u8*)pos);
    } else {
        cdCommandState = 5;
        CdSyncCallback(cdCommandSyncCallback);
        CdControlF(9, 0);
    }
}

void setCdMode(s32 mode) {
    s32 i;
    u8* p;

    cdCommandState = 9;
    CdSyncCallback(cdCommandSyncCallback);
    for (i = 3, p = &cdModeParam + 3; i >= 0; i--) {
        *p-- = 0;
    }
    {
        u8* param = &cdModeParam;

        *param = mode;
        CdControlF(0xE, param);
    }
}

void stopCdRead(s32 arg0) {
    s32 i;

    readStopRequested = 1;
    readNextSeekFile = arg0;
    if (cdromFnd != 0) {
        remainingReadSize = 0;
        readInProgress = 0;
        if (pcStreamFd != -1) {
            for (i = 0; i < 4; i++) {
                if ((i = PCclose(pcStreamFd)) == 0) {
                    break;
                }
            }
            pcStreamFd = -1;
        }
    }
}

void freeLoadingBatchBuffers(LoadingBatchCommand* cmds) {
    LoadingBatchCommand* p;

    if (cmds->m0_fileIndex != 0) {
        p = cmds;
        do {
            void* ptr = p->m4_loadPtr;
            p++;
            if (ptr != NULL) {
                freeHeap(ptr);
            }
        } while (p->m0_fileIndex != 0);
    }
}

LoadingBatchCommand* allocateFileGroupBatch(s32 fileIndex, LoadingBatchCommand* cmds) {
    s32 allocated;
    s32 count;
    s32 i;

    allocated = 0;
    count = getNegativeFileSize(fileIndex);
    if (count > 0) {
        if (cmds == NULL) {
            cmds = allocateHeap(PC_HEAP_SIZED((count + 1) * (s32)sizeof(LoadingBatchCommand), (count + 1) * (s32)0xc), 0);
            allocated = 1;
            if (cmds == NULL) {
                return NULL;
            }
        }
        for (i = 0; i < count; i++) {
            cmds[i].m0_fileIndex = fileIndex + i + 1;
            cmds[i].m4_loadPtr = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(fileIndex + i + 1)), 0);
            if (cmds[i].m4_loadPtr == NULL) {
                freeLoadingBatchBuffers(cmds);
                if (allocated > 0) {
                    freeHeap(cmds);
                }
                cmds = NULL;
                return cmds;
            }
        }
        cmds[count].m0_fileIndex = 0;
        cmds[count].m4_loadPtr = NULL;
    } else {
        cmds = NULL;
    }
    return cmds;
}

void cdCommandSyncCallback(u8 status, u8* result) {
    u8* param;

    switch (cdCommandState) {
    case 0:
        return;
    case 1:
        if (status == 2) {
            cdReadCommandOkCount++;
            cdCommandState++;
            CdControlF(6, 0);
        } else {
            cdReadCommandErrorCount++;
            savedCdReadyCallback = CdReadyCallback(0);
            cdErrorRecoveryAction = 3;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 2:
        if (status == 2) {
            cdReadCommandOkCount++;
            CdSyncCallback(0);
            cdCommandState = 0;
            return;
        } else {
            cdReadCommandErrorCount++;
            savedCdReadyCallback = CdReadyCallback(0);
            cdErrorRecoveryAction = 3;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 3:
        if (status == 2) {
            cdCommandState++;
            CdControlF(0x15, 0);
        } else {
            cdErrorRecoveryAction = 1;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 4:
        if (status == 2) {
            CdSyncCallback(0);
            cdCommandState = 0;
            return;
        } else {
            cdErrorRecoveryAction = 1;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 5:
        if (status == 2) {
            CdSyncCallback(0);
            cdCommandState = 0;
            return;
        } else {
            cdErrorRecoveryAction = 2;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 6:
        if (status == 2) {
            cdCommandState = 1;
            cdReadSetlocCount++;
            cdReadRecoveryCount++;
            CdReadyCallback(savedCdReadyCallback);
            CdControlF(2, (u8*)&cdReadLocation);
        } else {
            cdErrorRecoveryAction = 3;
            cdCommandState = 10;
            cdReadRecoveryErrorCount++;
            CdControlF(1, 0);
        }
        break;
    case 7:
        if (status == 2) {
            cdCommandState = 6;
            cdStopRecoveryOkCount++;
            CdControlF(9, 0);
        } else {
            cdErrorRecoveryAction = 4;
            cdCommandState = 10;
            cdStopRecoveryErrorCount++;
            CdControlF(1, 0);
        }
        break;
    case 12:
        if (status == 2) {
            cdCommandState = 8;
            param = cdFilterParam;
            param[0] = 1;
            cdFilterParam[1] = readNextSeekFile;
            CdControlF(0xD, param);
        } else {
            cdErrorRecoveryAction = 5;
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 8:
        if (status == 2) {
            cdCommandState = 1;
            cdReadSetlocCount++;
            cdReadRecoveryCount++;
            CdReadyCallback(savedCdReadyCallback);
            CdControlF(2, (u8*)&cdReadLocation);
        } else {
            cdErrorRecoveryAction = 5;
            cdCommandState = 10;
            cdReadRecoveryErrorCount++;
            CdControlF(1, 0);
        }
        break;
    case 9:
        if (status == 2) {
            cdCommandState = 5;
            CdControlF(9, 0);
        } else {
            cdErrorRecoveryAction = 6;
            cdCommandState = 10;
            cdStopRecoveryErrorCount++;
            CdControlF(1, 0);
        }
        break;
    case 10:
        if (status == 2 && !(result[0] & 0x10)) {
            cdCommandState = 11;
            CdControlF(0x13, 0);
        } else {
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    case 11:
        if (status == 2) {
            switch (cdErrorRecoveryAction) {
            case 1:
                cdCommandState = 3;
                CdControlF(2, (u8*)&cdReadLocation);
                break;
            case 2:
                cdCommandState = 5;
                CdControlF(9, 0);
                break;
            case 3:
                cdCommandState = 6;
                CdControlF(9, 0);
                break;
            case 4:
                cdCommandState = 7;
                CdControlF(8, 0);
                break;
            case 5:
                cdCommandState = 12;
                CdControlF(0xE, &cdModeParam);
                break;
            case 6:
                cdCommandState = 9;
                CdControlF(0xE, &cdModeParam);
                break;
            default:
                return;
            }
        } else {
            cdCommandState = 10;
            CdControlF(1, 0);
        }
        break;
    default:
        return;
    }
}

void batchLoadReadyCallback(u8 status, u8* result) {
    s32 i;
    s32 j;
    u32 sector;
    u16 fileIndex;
    LoadingBatchCommand* cmd;

    if (status == 1) {
        if (readStopRequested > 0) {
            CdReadyCallback(0);
            CdDataCallback(0);
            remainingReadSize = 0;
            seekToFileUnchecked(readNextSeekFile);
            batchFilesRemaining = 0;
            readInProgress = 0;
            return;
        }
        if (remainingReadSize >= 0x800) {
            if (batchSkippingGap == 0) {
                CdGetSector(readSectorHeader, 3);
                CdGetSector(readDestination, 0x200);
            }
        } else if (remainingReadSize > 0) {
            if (batchSkippingGap == 0) {
                CdGetSector(readSectorHeader, 3);
                CdGetSector(readDestination, (remainingReadSize + 3) / 4);
                CdGetSector(discardSectorBuffer, 0x200 - (remainingReadSize + 3) / 4);
            }
        }
        if (CdPosToInt((CdlLOC*)readSectorHeader) != readCurrentSector && batchSkippingGap == 0) {
            batchReadErrorCount++;
            goto error;
        }
        readDestination += 0x800;
        remainingReadSize -= 0x800;
        readCurrentSector++;
        if (remainingReadSize > 0) {
            return;
        }
        cmd = &loadingBatchCommands[++readCursorIndex];
        fileIndex = cmd->m0_fileIndex;
        readDestination = cmd->m4_loadPtr;
        if (fileIndex != 0 && readDestination != NULL) {
            sector = getLoadingFileStartSector(fileIndex);
            remainingReadSize = getLoadingFileSizeAligned(fileIndex);
            if (readCurrentSector < sector && readCurrentSector + batchMaxSkipSectors >= sector) {
                batchSkippingGap = 1;
                remainingReadSize = (readCurrentSector - sector) * 0x800;
                readCursorIndex--;
                return;
            }
            if (sector == readCurrentSector) {
                batchSkippingGap = 0;
                batchFilesRemaining--;
            } else {
                batchSkippingGap = 0;
                readCurrentSector = sector;
                savedCdReadyCallback = CdReadyCallback(0);
                CdIntToPos(readCurrentSector, &cdReadLocation);
                cdCommandState = 6;
                CdSyncCallback(cdCommandSyncCallback);
                CdControlF(9, 0);
                batchFilesRemaining--;
            }
            return;
        }
        remainingReadSize = 0;
        CdReadyCallback(0);
        seekToFileUnchecked(readNextSeekFile);
        batchFilesRemaining = 0;
        readInProgress = 0;
        return;
    }
error:
    cdErrorCount++;
    savedCdReadyCallback = CdReadyCallback(0);
    CdIntToPos(readCurrentSector, &cdReadLocation);
    if (cdErrorCount < 3) {
        cdErrorRecoveryAction = 3;
    } else {
        for (i = 9999; i >= 0; i--) {
            for (j = 1999; j >= 0; j--) {
            }
        }
        cdErrorCount = 0;
        cdErrorRecoveryAction = 4;
        cdStopRecoveryCount++;
    }
    cdCommandState = 10;
    CdSyncCallback(cdCommandSyncCallback);
    CdControlF(1, 0);
}

void readFileReadyCallback(u8 status, u8* result) {
    s32 i;
    s32 j;

    if (status == 1) {
        if (readStopRequested <= 0) {
            if (remainingReadSize >= 0x800) {
                CdGetSector(readSectorHeader, 3);
                CdGetSector(readDestination, 0x200);
            } else if (remainingReadSize > 0) {
                CdGetSector(readSectorHeader, 3);
                CdGetSector(readDestination, (remainingReadSize + 3) / 4);
                CdGetSector(discardSectorBuffer, 0x200 - (remainingReadSize + 3) / 4);
            }
            if (CdPosToInt((CdlLOC*)readSectorHeader) != readCurrentSector) {
                readFileErrorCount++;
                goto error;
            }
            readCurrentSector++;
            readDestination += 0x800;
            remainingReadSize -= 0x800;
            if (remainingReadSize > 0) {
                return;
            }
        }
        CdReadyCallback(0);
        remainingReadSize = 0;
        seekToFileUnchecked(readNextSeekFile);
        readInProgress = 0;
        return;
    }
error:
    cdErrorCount++;
    savedCdReadyCallback = CdReadyCallback(0);
    CdIntToPos(readCurrentSector, &cdReadLocation);
    if (cdErrorCount < 3) {
        cdErrorRecoveryAction = 3;
    } else {
        for (i = 9999; i >= 0; i--) {
            for (j = 1999; j >= 0; j--) {
            }
        }
        cdErrorCount = 0;
        cdErrorRecoveryAction = 4;
        cdStopRecoveryCount++;
    }
    cdCommandState = 10;
    CdSyncCallback(cdCommandSyncCallback);
    CdControlF(1, 0);
}

void streamReadyCallback(u8 status, u8* result) {
    s32 i;
    s32 j;
    s32 idx;
    u16* slot;

    if (status == 1) {
        if (readStopRequested > 0) {
            CdReadyCallback(0);
            CdDataCallback(0);
            remainingReadSize = 0;
            seekToFileUnchecked(readNextSeekFile);
            readInProgress = 0;
            return;
        }
        if (remainingReadSize > 0) {
            for (i = 0; i < streamSectorSlotCount; i++) {
                slot = (u16*)&streamSectorSlots[readCursorIndex];
                idx = readCursorIndex;
                readCursorIndex = idx + 1;
                if (readCursorIndex >= streamSectorSlotCount) {
                    readCursorIndex = 0;
                }
                if (slot[0] == 0) {
                    break;
                }
            }
            if (slot[0] != 0) {
                goto retry;
            }
            CdGetSector(readSectorHeader, 3);
            if (CdPosToInt((CdlLOC*)readSectorHeader) != readCurrentSector) {
                streamReadErrorCount++;
                CdGetSector(discardSectorBuffer, 0x200);
                goto error;
            }
            slot[1] = streamNextReadSequence;
            slot[0] = 1;
            streamNextReadSequence++;
            CdGetSector(readDestination + idx * 0x800, 0x200);
            remainingReadSize -= 0x800;
            readCurrentSector++;
            if (remainingReadSize > 0) {
                return;
            }
        }
        CdReadyCallback(0);
        remainingReadSize = 0;
        return;
    }
error:
    cdErrorCount++;
retry:
    savedCdReadyCallback = CdReadyCallback(0);
    CdIntToPos(readCurrentSector, &cdReadLocation);
    if (cdErrorCount < 3) {
        cdErrorRecoveryAction = 3;
    } else {
        s32 k, l;
        for (k = 9999; k >= 0; k--) {
            for (l = 1999; l >= 0; l--) {
            }
        }
        cdErrorCount = 0;
        cdErrorRecoveryAction = 4;
        cdStopRecoveryCount++;
    }
    cdCommandState = 10;
    CdSyncCallback(cdCommandSyncCallback);
    CdControlF(1, 0);
}

void imageFileReadyCallback(u8 status, u8* result) {
    s32 i;
    s32 j;
    s32 idx;
    u16* slot;

    if (status == 1) {
        if (readStopRequested > 0) {
            CdReadyCallback(0);
            CdDataCallback(0);
            remainingReadSize = 0;
            seekToFileUnchecked(readNextSeekFile);
            readInProgress = 0;
            return;
        }
        if (remainingReadSize > 0) {
            for (i = 0; i < streamSectorSlotCount; i++) {
                slot = (u16*)&streamSectorSlots[readCursorIndex];
                idx = readCursorIndex;
                readCursorIndex = idx + 1;
                if (readCursorIndex >= streamSectorSlotCount) {
                    readCursorIndex = 0;
                }
                if (slot[0] == 0) {
                    break;
                }
            }
            if (slot[0] != 0) {
                goto retry;
            }
            CdGetSector(readSectorHeader, 3);
            if (CdPosToInt((CdlLOC*)readSectorHeader) != readCurrentSector) {
                streamReadErrorCount++;
                CdGetSector(discardSectorBuffer, 0x200);
                goto error;
            }
            slot[1] = streamNextReadSequence;
            slot[0] = 1;
            streamNextReadSequence++;
            CdGetSector(readDestination + idx * 0x800, 0x200);
            remainingReadSize -= 0x800;
            readCurrentSector++;
            if (remainingReadSize > 0) {
                return;
            }
        }
        CdReadyCallback(0);
        remainingReadSize = 0;
        return;
    }
error:
    cdErrorCount++;
retry:
    savedCdReadyCallback = CdReadyCallback(0);
    CdIntToPos(readCurrentSector, &cdReadLocation);
    if (cdErrorCount < 3) {
        cdErrorRecoveryAction = 3;
    } else {
        s32 k, l;
        for (k = 9999; k >= 0; k--) {
            for (l = 1999; l >= 0; l--) {
            }
        }
        cdErrorCount = 0;
        cdErrorRecoveryAction = 4;
        cdStopRecoveryCount++;
    }
    cdCommandState = 10;
    CdSyncCallback(cdCommandSyncCallback);
    CdControlF(1, 0);
}

void readImageFileSectorFromPc(s32 arg0, s32 arg1) {
    s16 i;
    s32 slot;
    u16* s;

    if (remainingReadSize > 0) {
        for (i = 0; i < streamSectorSlotCount; i++) {
            s = (u16*)((readCursorIndex << 3) + (sptr)streamSectorSlots);
            slot = readCursorIndex;
            readCursorIndex = slot + 1;
            if (readCursorIndex >= streamSectorSlotCount) {
                readCursorIndex = 0;
            }
            if (s[0] == 0) {
                break;
            }
        }
        if (s[0] == 0) {
            s[0] = 1;
            s[1] = streamNextReadSequence;
            streamNextReadSequence++;
            for (i = 0; i < 4; i++) {
                if (PCread(pcImageFileFd, (u8*)((slot << 11) + (sptr)readDestination), 0x800)) {
                    break;
                }
                reportReadError(i, 0, 0xFF, 0);
            }
            remainingReadSize -= 0x800;
            readCurrentSector++;
        } else {
            remainingReadSize = 0;
        }
        if (remainingReadSize <= 0) {
            remainingReadSize = 0;
        }
    } else {
        remainingReadSize = 0;
    }
}

void batchLoadDataCallback(void) {
    readInProgress = batchFilesRemaining;
}

void streamDataCallback(void) {
    s16 i;
    SectorSlot* s = streamSectorSlots;

    for (i = 0; i < streamSectorSlotCount; i++, s++) {
        if (s->m0_state == 1 && s->m2_sequence == streamNextDataSequence) {
            break;
        }
    }
    if (i != streamSectorSlotCount) {
        s->m0_state = 3;
        streamNextDataSequence++;
        if (remainingReadSize <= 0 && readInProgress < 2) {
            remainingReadSize = 0;
            CdDataCallback(0);
            seekToFileUnchecked(readNextSeekFile);
            readInProgress = 0;
        }
    }
}

void imageFileDataCallback(void) {
    s16 i;
    u16* s = (u16*)streamSectorSlots;
    u8* data;
    s32 type;
    u16* hdr;
    s32 t;
    RECT rect;

    for (i = 0; i < streamSectorSlotCount; i++, s += 4) {
        if (s[0] == 1 && s[1] == streamNextDataSequence) {
            break;
        }
    }
    if (i == streamSectorSlotCount) {
        return;
    }
    s[0] = 2;
    data = (u8*)((i << 11) + (sptr)readDestination);
    if (imageFileSectorsRemaining == 0) {
        type = *(s32*)data;
        data += 4;
        hdr = (u16*)data;
        if (type != 0x1200 && type != 0x1201) {
            goto end;
        }
        if (type == 0x1200) {
            switch (imageFilePixelPosMode) {
            case 1:
                imageFileUploadX = imageFilePixelX + hdr[2];
                imageFileUploadY = imageFilePixelY + hdr[3];
                break;
            case 2:
                t = imageFilePixelX + hdr[0] + hdr[2];
                imageFileUploadX = t;
                t = imageFilePixelY + hdr[1] + hdr[3];
                imageFileUploadY = t;
                break;
            default:
                imageFileUploadX = hdr[0] + hdr[2];
                imageFileUploadY = hdr[1] + hdr[3];
                break;
            }
        }
        if (type == 0x1201) {
            switch (imageFileClutPosMode) {
            case 1:
                imageFileUploadX = imageFileClutX + hdr[2];
                imageFileUploadY = imageFileClutY + hdr[3];
                break;
            case 2:
                t = imageFileClutX + hdr[0] + hdr[2];
                imageFileUploadX = t;
                t = imageFileClutY + hdr[1] + hdr[3];
                imageFileUploadY = t;
                break;
            default:
                imageFileUploadX = hdr[0] + hdr[2];
                imageFileUploadY = hdr[1] + hdr[3];
                break;
            }
        }
        data += 8;
        imageFileUploadWidth = *(u16*)data;
        data += 8;
        if (imageFileUnitsRemaining == 0) {
            imageFileUnitsRemaining = *(s32*)data;
        }
        data += 4;
        imageFileSectorsRemaining = *(s32*)data;
        data += 4;
        imageFileRowsPerSector = (u16*)data;
    } else {
        rect.x = imageFileUploadX;
        rect.y = imageFileUploadY;
        rect.w = imageFileUploadWidth;
        rect.h = *imageFileRowsPerSector;
        LoadImage(&rect, data);
        imageFileUploadY += *imageFileRowsPerSector++;
        if (--imageFileSectorsRemaining <= 0) {
            imageFileSectorsRemaining = 0;
            imageFileUnitsRemaining--;
            for (i = 0; i < streamSectorSlotCount; i++) {
                streamSectorSlots[i].m0_state = 0;
                streamSectorSlots[i].m2_sequence = 0;
            }
            if (imageFileUnitsRemaining <= 0) {
            end:
                remainingReadSize = 0;
                CdDataCallback(0);
                seekToFileUnchecked(readNextSeekFile);
                readInProgress = 0;
                return;
            }
        }
        s[0] = 0;
    }
    streamNextDataSequence++;
}

void processImageFileSectorPc(s32 arg0, s32 arg1) {
    s16 i;
    u16* s = (u16*)streamSectorSlots;
    u8* data;
    s32 type;
    u16* hdr;
    s32 t;
    RECT rect;

    for (i = 0; i < streamSectorSlotCount; i++, s += 4) {
        if (s[0] == 1 && s[1] == streamNextDataSequence) {
            break;
        }
    }
    if (i == streamSectorSlotCount) {
        return;
    }
    s[0] = 2;
    data = (u8*)((i << 11) + (sptr)readDestination);
    if (imageFileSectorsRemaining == 0) {
        type = *(s32*)data;
        data += 4;
        hdr = (u16*)data;
        if (type != 0x1200 && type != 0x1201) {
            return;
        }
        if (type == 0x1200) {
            switch (imageFilePixelPosMode) {
            case 1:
                imageFileUploadX = imageFilePixelX + hdr[2];
                imageFileUploadY = imageFilePixelY + hdr[3];
                break;
            case 2:
                t = imageFilePixelX + hdr[0] + hdr[2];
                imageFileUploadX = t;
                t = imageFilePixelY + hdr[1] + hdr[3];
                imageFileUploadY = t;
                break;
            default:
                imageFileUploadX = hdr[0] + hdr[2];
                imageFileUploadY = hdr[1] + hdr[3];
                break;
            }
        }
        if (type == 0x1201) {
            switch (imageFileClutPosMode) {
            case 1:
                imageFileUploadX = imageFileClutX + hdr[2];
                imageFileUploadY = imageFileClutY + hdr[3];
                break;
            case 2:
                t = imageFileClutX + hdr[0] + hdr[2];
                imageFileUploadX = t;
                t = imageFileClutY + hdr[1] + hdr[3];
                imageFileUploadY = t;
                break;
            default:
                imageFileUploadX = hdr[0] + hdr[2];
                imageFileUploadY = hdr[1] + hdr[3];
                break;
            }
        }
        data += 8;
        imageFileUploadWidth = *(u16*)data;
        data += 8;
        if (imageFileUnitsRemaining == 0) {
            imageFileUnitsRemaining = *(s32*)data;
        }
        data += 4;
        imageFileSectorsRemaining = *(s32*)data;
        data += 4;
        imageFileRowsPerSector = (u16*)data;
    } else {
        rect.x = imageFileUploadX;
        rect.y = imageFileUploadY;
        rect.w = imageFileUploadWidth;
        rect.h = *imageFileRowsPerSector;
        LoadImage(&rect, data);
        DrawSync(0);
        imageFileUploadY += *imageFileRowsPerSector++;
        if (--imageFileSectorsRemaining <= 0) {
            imageFileSectorsRemaining = 0;
            imageFileUnitsRemaining--;
            for (i = 0; i < streamSectorSlotCount; i++) {
                streamSectorSlots[i].m0_state = 0;
                streamSectorSlots[i].m2_sequence = 0;
            }
            if (imageFileUnitsRemaining <= 0) {
                remainingReadSize = 0;
                readInProgress = 0;
                return;
            }
        }
        s[0] = 0;
    }
    streamNextDataSequence++;
}

void printImageFileLoadState(void) {
    sprintf_screen("F%8x A%8x S%8x\n", fatFileTableBuffer, readDestination, streamSectorSlots);
    sprintf_screen("%d %d %d %d\n", streamSectorSlotCount, readCursorIndex, imageFileUnitsRemaining, imageFileSectorsRemaining);
    sprintf_screen("%d %d %d %8x\n", imageFileUploadX, imageFileUploadY, imageFileUploadWidth, imageFileRowsPerSector);
    sprintf_screen("%d %d %d\n", imageFileRowsPerSector[0], imageFileRowsPerSector[1], imageFileRowsPerSector[2]);
}

s32 getCdromFnd(void) {
    return (s32)cdromFnd;
}
