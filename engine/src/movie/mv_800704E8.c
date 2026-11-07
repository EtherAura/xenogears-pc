/* Movie overlay: the CD-ROM check and disc change test screens and the
 * gradient helpers. gcc-2.6.3-psx, ASPSX 2.34, divide checks kept. */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sound.h"
#include "main/filesystem.h"
#include "main/debug_font.h"
#include "movie/movie.h"

void startMusicInstance(void* inst, s32 vol, s32 arg2);
s32 getCdromFnd(void);
void setCdMode(s32);
void stopCdRead(s32);
s32 PCclose(s32 fd);
void computeMovieLookAtMatrix(void);
void* loadPcHostFile(char* name, s32* sizeOut);
void* createMusicInstance(void* seq);
s32 moviePlaybackLoop(void);
s32 readPadButtons(s32);
void setCdVolume(s32, s32);
s32 SquareRoot0(s32 a);
s32 ratan2(s32 y, s32 x);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void SetGeomScreen(s32 h);
char* getFilenameFromPC(s32 fileIndex); /* the file's PC path */
void checkSoftReboot(void);
void printDebugTextOnScreen(OTag*);
s32 getCurrentDiscNumber(void); /* current disc number */
void setGameMode(s32 mode);
void bootGame(s32);
s32 findMovieFrameSector(s32 frame);
s32 getMovieLastFrameNumber(void);
void playMovieFromDebugMenu(void);
s32 movieStartPlaybackFromScript(u8 arg0);
void runCdromMonitorScreen(void);
void runFatCheckScreen(void);
void runCdromCheckScreen(void);
void runDiscChangeScreen(void);
s32 getRemainingReadSize(void);
void dumpHeapBlocks(s32, s32, s32, s32);
s32 isCDBusy(void);
s32 getFatEntrySize(s32 index);
void handleCdromCheckInput(void);
void verifyCdromCheckRead(void);
s32 processDiscChange(s32 disc, s32 state, s32* status, s32* result);
void stopCdForDiscChange(void);
void drawGradientBackground(OTag* ot, POLY_G4* p, s32 x, s32 y, s32 w, s32 h);
void drawGradientBorder(OTag* ot, POLY_G4* p, s32 x, s32 y, s32 w, s32 h);
void clearMovieVram(void);
s32 getMovieDebugRandom(void);
s32 updateMovieDebugMenuInput(s32 min, s32 max, s32* action);
u8* getNextStreamedSector(void);
s32 getNextStreamedPacket(u8** outData, void** outSector);
s32 releaseSectorSlot(u8* sector);
s32 releaseStreamedPacket(void* sector);
void* setSectorBuffer(void* buffer);
s32 loadImageFileToVram(s32 fileIndex, s32* buf, s32 channel, s32 flags, u16 arg4, u16 arg5, u16 arg6, u16 arg7,
                  u16 arg8, u16 arg9);
s32* allocateBufferForVramUpload(s32 count, s32 flags);
void freeLoadingBatchBuffers(LoadingBatchCommand* cmds);
LoadingBatchCommand* allocateFileGroupBatch(s32 fileIndex, LoadingBatchCommand* cmds);
void readCdromCheckRawSectors(void);
void startCdromCheckCommand(s32 command);
s32 initStrPlayer(u16 width, u16 height, u16 depth, u16 sliceWidth, u16 ringSize, u16 vlcMode, u16 mode);
/* maxHeight (s16) and the callback's x (s32) differ from the player's u16: the PS1 code converts them so. */
void startStrPlayback(s32 fileIndex, s32 offset, u16 startFrame, u16 endFrame, u16 channel, s32 flags, u16 audio,
                      u16 x0, u16 y0, u16 x1, u16 y1, s16 maxHeight, void (*callback)(u16, s32, u16));
void updateStrPlayer(void);
void stopStrPlayback(void);
void freeStrPlayer(void);
void handleMovieFrame(u16 frame, s32 arg1, u16 arg2);

extern u8* fatFileTableBuffer;
extern u16* fatDirectoryTableBuffer;
extern sptr cdromFnd;
extern s32 cdCheckActiveCommand;
extern s32 cdCheckErrorCount;
extern LoadingBatchCommand* cdCheckBatch1;
extern u32* cdCheckStreamWritePtr;
extern LoadingBatchCommand* cdCheckBatch2;
extern u32* cdCheckBuffer1;
extern u32* cdCheckBuffer2;
extern s32 cdCheckErrorAddress;
extern s32 cdCheckErrorSize;
extern s32 cdCheckErrorReadFileCount;
extern s32 cdCheckErrorBatchCount;
extern s32 cdCheckErrorStreamCount;
extern s32 readFileErrorCount;
extern s32 batchReadErrorCount;
extern s32 streamReadErrorCount;
extern s32 cdCommandState;
extern u16 streamNextReadSequence;
extern u16 streamNextDataSequence;
extern s32 cdReadSetlocCount;
extern s32 cdReadCommandOkCount;
extern s32 cdReadCommandErrorCount;
extern s32 cdReadRecoveryCount;
extern s32 cdReadRecoveryErrorCount;
extern s32 D_8005A49C;
extern s32 cdStopRecoveryCount;
extern s32 cdStopRecoveryOkCount;
extern s32 cdStopRecoveryErrorCount;
extern s32 cdErrorCount;
extern s16 streamFrameNumber;
extern u16 movieRequestEndFrame;
extern s32 strSkippedFrames;
extern s32 movieEndFrameStatus;
extern s32 movieDebugMenuActive;
extern s32 movieStartFrameStatus;
extern s32 movieCdromFnd;
extern s32 cdCheckCommandTotal;
extern s32 cdMonitorSector; /* monitor: sector */
extern s32 cdMonitorRow; /* monitor: first row */
extern s32 cdCheckSeconds;
extern s32 cdCheckBufferSize;
extern s32* cdCheckSectorBuffer;
extern s32 cdCheckPhase;
extern s32 cdCheckStreamRest;
extern u8* cdCheckStreamSector;
extern s32 cdCheckCancelCount;
extern s32 cdCheckBatchIndex;
extern s32 cdCheckStreamMode;
extern s32 cdCheckRandomMode;
extern u8* cdCheckPacketData;
extern void* cdCheckPacketSector;
extern s32 cdCheckVSyncCount;
extern sptr cdCheckRawBuffer;
extern s32 cdCheckPreviousCommand;
extern s32 cdCheckLastCommand;
extern s32 cdCheckSeconds;
extern s32 cdCheckFrameCounter;
extern s32 movieDebugRandomSeed1;
extern s32 movieDebugRandomSeed2;
extern s32 cdCheckCommandCounts[16]; /* per-command counters (CT%1x) */
extern u8 discChangeCdResult[8]; /* CD result bytes */
extern CVECTOR gradientBackgroundFrom[4];
extern CVECTOR gradientBackgroundTo[4];
extern s32 gradientBackgroundCounter[4];
extern s32 gradientBackgroundPeriod[4];
extern CVECTOR gradientBorderFrom[4];
extern CVECTOR gradientBorderTo[4];
extern s32 gradientBorderCounter[4];
extern s32 gradientBorderPeriod[4];
extern MovieDisplayBuffer movieDisplayBuffers[2];
extern MovieDisplayBuffer* movieCurrentDisplayBuffer; /* current display buffer */
extern s32 movieOddOrEven;
extern void* debugBattleMusic;
extern MATRIX movieViewMatrix;
extern MATRIX movieColorMatrix;
extern MATRIX movieLightMatrix;
extern MATRIX movieTranslationMatrix;
extern MATRIX D_800770F8;
extern VECTOR movieViewPoint; /* view point */
extern VECTOR movieReferencePoint; /* reference point */
extern s32 movieViewRoll;    /* view roll */
extern SVECTOR movieViewAngles;
extern MATRIX movieLookAtMatrix;
extern VECTOR D_80076F2C;
extern s32 movieCurrentFrame;
extern s32 movieStopCountdown;
extern s32 movieDrawBufferIndex;
extern s32 movieDisplayBufferIndex;
extern s32 movieImageY;
extern s32 movieSkipDisabled;
extern s32 moviePlaybackNumber;
extern s32 movieEndFrame;
extern s32 moviePadButtons;
extern s32 movieRewind;
extern s32 moviePlaybackType;
extern s32 strSingleSliceMode;
extern s32 D_80077020;
extern s32 movieChannel;
extern s32 movieScreenDrawLines;
extern s32 movieStartFrame;
extern s32 movieStartSector;
extern s32 movieUpdateTimes[32];
extern s32 movieRgb24;
extern s32 moviePadRepeatTimer;
extern s32 moviePadRepeatDelay;
extern s32 moviePadPreviousButtons;
extern s32 movieMenuRepeatTimer;
extern s32 movieMenuRepeatDelay;
extern s32 movieDebugMenuCursor;
extern s32 movieShowHeapDump;
extern s32 D_800773B0;


void runCdromCheckScreen(void) {
    s32 unused0[2];
    s8 unused[3] = {0, 2, 2};
    s32 unused1[6];
    s32 action;
    s32 i;
    s32 rest;
    s32 cursor;

    setSectorBuffer(0);
    clearMovieVram();
    cdCheckRawBuffer = 0;
    cdCheckStreamMode = 1;
    for (i = 15; i >= 0; i--) {
        cdCheckCommandCounts[i] = 0;
    }
    SetDefDrawEnv(&movieDisplayBuffers[0].m0_drawEnv, 0, 0, 0x280, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[0].m5C_dispEnv, 0, 0xF0, 0x280, 0xF0);
    SetDefDrawEnv(&movieDisplayBuffers[1].m0_drawEnv, 0, 0xF0, 0x280, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[1].m5C_dispEnv, 0, 0, 0x280, 0xF0);
    movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
    setCurrentDirectory(0xC, 3);
    while (1) {
        movieCurrentDisplayBuffer = (movieCurrentDisplayBuffer == &movieDisplayBuffers[0]) ? &movieDisplayBuffers[1] : &movieDisplayBuffers[0];
        movieOddOrEven = 1 - movieOddOrEven;
        ClearOTagR(movieCurrentDisplayBuffer->m70_ot, 0x20);
        verifyCdromCheckRead();
        updateMovieDebugMenuInput(0, 0, &action);
        handleCdromCheckInput();
        if (cdCheckRandomMode != 0) {
            logToScreen("Random Mode\n");
        }
        if (cdCheckStreamMode == 0) {
            logToScreen("Stream Pause\n");
        }
        logToScreen("Read %3d Error %3d VSync %8d EC %2d ST %2d ", cdCheckActiveCommand, cdCheckErrorCount, cdCheckVSyncCount, cdErrorCount,
                    cdCommandState);
        switch (cdCheckPhase) {
        case 0:
            logToScreen("Waiting\n");
            break;
        case 1:
            logToScreen("Reading\n");
            break;
        case 2:
            logToScreen("Verifing\n");
            break;
        }
        logToScreen("C1 %3d C2 %3d C3 %3d C4 %3d C5 %3d C6 %3d C7 %3d C8 %3d C9 %3d\n", cdReadSetlocCount, cdReadCommandOkCount,
                    cdReadCommandErrorCount, cdReadRecoveryCount, cdReadRecoveryErrorCount, D_8005A49C, cdStopRecoveryCount, cdStopRecoveryOkCount, cdStopRecoveryErrorCount);
        rest = isCDBusy();
        logToScreen("RestFile %7d RestSize %7d N1 %4d N2 %4d N3 %4d R%3d D%3d\n", rest, getRemainingReadSize(), readFileErrorCount,
                    batchReadErrorCount, streamReadErrorCount, streamNextReadSequence, streamNextDataSequence);
        logToScreen("ErrorAddress %8x ErrorSize %8x N%3d N%3d N%3d\n", cdCheckErrorAddress, cdCheckErrorSize, cdCheckErrorReadFileCount,
                    cdCheckErrorBatchCount, cdCheckErrorStreamCount);
        logToScreen("FrdPtr1 %8x FrdPtr2 %8x Buf1 %8x Buf2 %8x\n", cdCheckBatch1, cdCheckBatch2, cdCheckBuffer1, cdCheckBuffer2);
        if (cdCheckActiveCommand >= 0xB || cdCheckStreamRest > 0) {
            logToScreen("S %8x Adrs %8x Write %8x Rest %8x\n", cdCheckSectorBuffer, cdCheckStreamSector, cdCheckStreamWritePtr, cdCheckStreamRest);
            for (i = 0; i < 7; i++) {
                logToScreen("%08x ", cdCheckSectorBuffer[i]);
            }
            logToScreen("\n");
            for (i = 0; i < 7; i++) {
                logToScreen("%08x ", cdCheckSectorBuffer[i + 7]);
            }
            logToScreen("\n");
            for (i = 0; i < 7; i++) {
                logToScreen("%08x ", cdCheckSectorBuffer[i + 14]);
            }
            logToScreen("\n");
            for (i = 0; i < 7; i++) {
                logToScreen("%08x ", cdCheckSectorBuffer[i + 21]);
            }
            logToScreen("\n");
            if (cdCheckBatch1 != 0) {
                logToScreen("%08x %08x %08x %08x %08x\n", cdCheckBatch1[0].m4_loadPtr, cdCheckBatch1[1].m4_loadPtr,
                            cdCheckBatch1[2].m4_loadPtr, cdCheckBatch1[3].m4_loadPtr);
            }
        }
        cdCheckCommandTotal = 0;
        logToScreen("Cancel%8d\n", cdCheckCancelCount);
        for (i = 0; i < 13; i++) {
            logToScreen("CT%1x   %8d", i, cdCheckCommandCounts[i]);
            cdCheckCommandTotal += cdCheckCommandCounts[i];
            if (cdCheckLastCommand == i) {
                logToScreen(" NOW");
            }
            if (cdCheckPreviousCommand == i) {
                logToScreen(" BEFORE");
            }
            logToScreen("\n");
        }
        logToScreen("\nTOTAL %8d : Time %3d:%02d:%02d\n", cdCheckCommandTotal, cdCheckSeconds / 3600, cdCheckSeconds / 60 % 60,
                    cdCheckSeconds % 60);
        logToScreen("\nPUSH START BUTTON TO MENU.");
        if (movieShowHeapDump > 0) {
            dumpHeapBlocks(1, 0, 6, 0x808D);
        }
        printDebugTextOnScreen(movieCurrentDisplayBuffer->m70_ot);
        drawGradientBackground(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->mF0_background, 8, 0xC, 0x270, 0xD8);
        drawGradientBorder(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->m114_background2, 7, 0xB, 0x272, 0xDA);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
        PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
        DrawOTag(&movieCurrentDisplayBuffer->m70_ot[31]);
        cursor = movieDebugMenuCursor;
        if (!(moviePadPreviousButtons & 0x800) && (moviePadButtons & 0x800) && cdCheckActiveCommand == 0) {
            movieShowHeapDump = 0;
            stopCdRead(0);
            waitReadCompletion(0);
            if (cdCheckRawBuffer != 0) {
                freeHeap((void*)cdCheckRawBuffer);
            }
            cdCheckRawBuffer = 0;
            if (cdCheckSectorBuffer != 0) {
                freeHeap(cdCheckSectorBuffer);
            }
            cdCheckSectorBuffer = 0;
            clearMovieVram();
            SetDefDrawEnv(&movieDisplayBuffers[0].m0_drawEnv, 0, 0, 0x140, 0xF0);
            SetDefDispEnv(&movieDisplayBuffers[0].m5C_dispEnv, 0, 0xF0, 0x140, 0xF0);
            SetDefDrawEnv(&movieDisplayBuffers[1].m0_drawEnv, 0, 0xF0, 0x140, 0xF0);
            SetDefDispEnv(&movieDisplayBuffers[1].m5C_dispEnv, 0, 0, 0x140, 0xF0);
            movieDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
            movieDisplayBuffers[0].m5C_dispEnv.screen.y = 0xA;
            movieDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
            movieDisplayBuffers[0].m5C_dispEnv.screen.h = 0xD8;
            movieDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
            movieDisplayBuffers[1].m5C_dispEnv.screen.y = 0xA;
            movieDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
            movieDisplayBuffers[1].m5C_dispEnv.screen.h = 0xD8;
            movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
            movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
            break;
        }
        movieDebugMenuCursor = cursor;
    }
}

void handleCdromCheckInput(void) {
    s32 i;
    s32 cmd;

    if (!(moviePadPreviousButtons & 0x20) && (moviePadButtons & 0x20)) {
        startCdromCheckCommand(1);
    }
    if (!(moviePadPreviousButtons & 0x10) && (moviePadButtons & 0x10)) {
        startCdromCheckCommand(2);
    }
    if (!(moviePadPreviousButtons & 0x80) && (moviePadButtons & 0x80)) {
        startCdromCheckCommand(3);
    }
    if (!(moviePadPreviousButtons & 0x40) && (moviePadButtons & 0x40)) {
        cdCheckCancelCount++;
        stopCdRead(0);
    }
    if (!(moviePadPreviousButtons & 0x1000) && (moviePadButtons & 0x1000)) {
        startCdromCheckCommand(7);
    }
    if (!(moviePadPreviousButtons & 0x4000) && (moviePadButtons & 0x4000)) {
        startCdromCheckCommand(9);
    }
    if (!(moviePadPreviousButtons & 8) && (moviePadButtons & 8)) {
        cdCheckStreamMode = 1 - cdCheckStreamMode;
    }
    if (!(moviePadPreviousButtons & 4) && (moviePadButtons & 4) && cdCheckActiveCommand < 0xB) {
        readCdromCheckRawSectors();
    }
    if (cdCheckStreamMode == 1) {
        cdCheckStreamSector = getNextStreamedSector();
        if (cdCheckStreamSector != 0) {
            if (cdCheckStreamRest > 0x800) {
                for (i = 0; i < 0x200; i++) {
                    *cdCheckStreamWritePtr++ = ((u32*)cdCheckStreamSector)[i];
                }
            } else {
                for (i = 0; i < cdCheckStreamRest / 4; i++) {
                    *cdCheckStreamWritePtr++ = ((u32*)cdCheckStreamSector)[i];
                }
            }
            cdCheckStreamRest -= 0x800;
            if (cdCheckStreamRest <= 0 && cdCheckActiveCommand == 0xC) {
                cdCheckBatchIndex++;
                i = cdCheckBatch1[cdCheckBatchIndex].m0_fileIndex;
                if (i != 0) {
                    cdCheckStreamRest = getFileSizeAligned(i);
                }
                cdCheckStreamWritePtr = cdCheckBatch1[cdCheckBatchIndex].m4_loadPtr;
            }
            releaseSectorSlot(cdCheckStreamSector);
        }
    }
    if (cdCheckStreamMode == 2 && getNextStreamedPacket(&cdCheckPacketData, &cdCheckPacketSector) == 0) {
        releaseStreamedPacket(cdCheckPacketSector);
    }
    if (!(moviePadPreviousButtons & 0x2000) && (moviePadButtons & 0x2000)) {
        startCdromCheckCommand(0xB);
    }
    if (!(moviePadPreviousButtons & 0x8000) && (moviePadButtons & 0x8000)) {
        startCdromCheckCommand(0xD);
    }
    if (cdCheckRandomMode != 0) {
        cmd = getMovieDebugRandom() & 0xFF;
        if (cmd == 0 && cdCheckActiveCommand < 0xB) {
            readCdromCheckRawSectors();
        }
        if (cmd > 0 && cmd < 0xD) {
            startCdromCheckCommand(cmd);
        }
    }
    if (!(moviePadPreviousButtons & 0x100) && (moviePadButtons & 0x100)) {
        cdCheckRandomMode = 1 - cdCheckRandomMode;
    }
}

/* fill a buffer of cdCheckBufferSize bytes with -1 */
#define FILL(ptr)                               \
    {                                           \
        u32* q = (u32*)(ptr);                   \
        for (i = 0; i < cdCheckBufferSize / 4; i++) {  \
            q[i] = -1;                          \
        }                                       \
    }

void verifyCdromCheckRead(void) {
    s32 i;
    s32 k;
    s32 fileIndex;
    u32* p;
    u32* p2;
    u32* q;

    if (cdCheckPhase == 0) {
        return;
    }
    if (!isCDBusy() && cdCheckPhase == 1) {
        cdCheckPhase = 2;
        switch (cdCheckActiveCommand) {
        case 1:
            cdCheckBufferSize = 0x2000;
            cdCheckBuffer2 = allocateHeap(PC_HEAP_BYTES(0x2000), 0);
            FILL(cdCheckBuffer2);
            readRawSectorsFromDisk(0x40, cdCheckBuffer2, cdCheckBufferSize, 0, 0);
            break;
        case 2:
            cdCheckBufferSize = getFileSizeAligned(7);
            cdCheckBuffer2 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
            FILL(cdCheckBuffer2);
            readFile(7, cdCheckBuffer2, 0, 0);
            break;
        case 3:
        case 12:
            cdCheckBatch2 = allocateFileGroupBatch(2, 0);
            if (cdCheckBatch2 == 0) {
                goto fail;
            }
            fileIndex = cdCheckBatch2[0].m0_fileIndex;
            for (k = 0; fileIndex > 0;) {
                p2 = cdCheckBatch2[k].m4_loadPtr;
                cdCheckBufferSize = getFileSizeAligned(fileIndex);
                FILL(p2);
                k++;
                fileIndex = cdCheckBatch2[k].m0_fileIndex;
            }
            batchStartLoadingFiles(cdCheckBatch2, 0, 0);
            break;
        case 4:
            cdCheckBufferSize = 0x2000;
            cdCheckBuffer2 = allocateHeap(PC_HEAP_BYTES(0x2000), 0);
            FILL(cdCheckBuffer2);
            readRawSectorsFromDisk(0x40, cdCheckBuffer2, cdCheckBufferSize, 0, 0);
            break;
        case 5:
            cdCheckBufferSize = getFileSizeAligned(7);
            cdCheckBuffer2 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
            FILL(cdCheckBuffer2);
            readFile(7, cdCheckBuffer2, 1, 0);
            break;
        case 6:
            cdCheckBatch2 = allocateFileGroupBatch(2, 0);
            if (cdCheckBatch2 == 0) {
                goto fail;
            }
            fileIndex = cdCheckBatch2[0].m0_fileIndex;
            for (k = 0; fileIndex > 0;) {
                p2 = cdCheckBatch2[k].m4_loadPtr;
                cdCheckBufferSize = getFileSizeAligned(fileIndex);
                FILL(p2);
                k++;
                fileIndex = cdCheckBatch2[k].m0_fileIndex;
            }
            batchStartLoadingFiles(cdCheckBatch2, 1, 0);
            break;
        case 7:
        case 8:
        fail:
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        case 11:
            cdCheckBufferSize = getFileSizeAligned(6);
            cdCheckBuffer2 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
            FILL(cdCheckBuffer2);
            readFile(6, cdCheckBuffer2, 0, 0);
            break;
        }
    }
    if (!isCDBusy() && cdCheckPhase == 2) {
        cdCheckPhase = 0;
        switch (cdCheckActiveCommand) {
        case 1:
        case 4:
            for (i = 0; i < 0x800; i++) {
                if (cdCheckBuffer1[i] != cdCheckBuffer2[i]) {
                    if (cdCheckErrorCount == 0) {
                        cdCheckErrorAddress = i * 4;
                        cdCheckErrorSize = 0x2000;
                        cdCheckErrorReadFileCount = readFileErrorCount;
                        cdCheckErrorBatchCount = batchReadErrorCount;
                        cdCheckErrorStreamCount = streamReadErrorCount;
                    }
                    cdCheckErrorCount++;
                    break;
                }
            }
            freeHeap(cdCheckBuffer1);
            freeHeap(cdCheckBuffer2);
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        case 2:
        case 5:
            cdCheckBufferSize = getFileSizeAligned(7);
            for (i = 0; i < cdCheckBufferSize / 4; i++) {
                if (cdCheckBuffer1[i] != cdCheckBuffer2[i]) {
                    if (cdCheckErrorCount == 0) {
                        cdCheckErrorAddress = i * 4;
                        cdCheckErrorSize = getFileSizeAligned(7);
                        cdCheckErrorReadFileCount = readFileErrorCount;
                        cdCheckErrorBatchCount = batchReadErrorCount;
                        cdCheckErrorStreamCount = streamReadErrorCount;
                    }
                    cdCheckErrorCount++;
                    break;
                }
            }
            freeHeap(cdCheckBuffer1);
            freeHeap(cdCheckBuffer2);
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        case 3:
        case 6:
        case 12:
            fileIndex = cdCheckBatch1[0].m0_fileIndex;
            for (k = 0; fileIndex > 0;) {
                p2 = cdCheckBatch2[k].m4_loadPtr;
                p = cdCheckBatch1[k].m4_loadPtr;
                cdCheckBufferSize = getFileSizeAligned(fileIndex);
                for (i = 0; i < cdCheckBufferSize / 4; i++) {
                    if (p[i] != p2[i]) {
                        if (cdCheckErrorCount == 0) {
                            cdCheckErrorAddress = i * 4;
                            cdCheckErrorSize = getFileSizeAligned(fileIndex);
                            cdCheckErrorReadFileCount = readFileErrorCount;
                            cdCheckErrorBatchCount = batchReadErrorCount;
                            cdCheckErrorStreamCount = streamReadErrorCount;
                        }
                        cdCheckErrorCount++;
                        break;
                    }
                }
                k++;
                fileIndex = cdCheckBatch1[k].m0_fileIndex;
            }
            freeLoadingBatchBuffers(cdCheckBatch1);
            freeLoadingBatchBuffers(cdCheckBatch2);
            freeHeap(cdCheckBatch1);
            freeHeap(cdCheckBatch2);
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        case 7:
        case 8:
        case 9:
        case 10:
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        case 11:
            if (cdCheckStreamRest > 0) {
                cdCheckPhase = 2;
            } else {
                cdCheckBufferSize = getFileSizeAligned(6);
                for (i = 0; i < cdCheckBufferSize / 4; i++) {
                    if (cdCheckBuffer1[i] != cdCheckBuffer2[i]) {
                        if (cdCheckErrorCount == 0) {
                            cdCheckErrorAddress = i * 4;
                            cdCheckErrorSize = getFileSizeAligned(6);
                            cdCheckErrorReadFileCount = readFileErrorCount;
                            cdCheckErrorBatchCount = batchReadErrorCount;
                            cdCheckErrorStreamCount = streamReadErrorCount;
                        }
                        cdCheckErrorCount++;
                        break;
                    }
                }
                freeHeap(cdCheckBuffer1);
                freeHeap(cdCheckBuffer2);
            }
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        default:
            cdCheckActiveCommand = 0;
            break;
        }
    }
}

void readCdromCheckRawSectors(void) {
    if (cdCheckRawBuffer == 0) {
        cdCheckRawBuffer = (sptr)allocateHeap(PC_HEAP_BYTES(0x2000), 0);
    }
    if (cdCheckRawBuffer != 0) {
        readRawSectorsFromDisk(0x40, (void*)cdCheckRawBuffer, 0x2000, 0, 0);
    }
    cdCheckCommandCounts[0]++;
    if (cdCheckErrorCount == 0) {
        cdCheckPreviousCommand = cdCheckLastCommand;
        cdCheckLastCommand = 0;
    }
}

/* zero a freshly allocated/loaded buffer of cdCheckBufferSize bytes */
#define CLEAR(ptr)                              \
    {                                           \
        u32* q = (u32*)(ptr);                   \
        for (i = 0; i < cdCheckBufferSize / 4; i++) {  \
            q[i] = 0;                           \
        }                                       \
    }

void startCdromCheckCommand(s32 command) {
    s32 i;
    s32 k;
    s32 fileIndex;
    u32* p;

    if (cdCheckActiveCommand != 0 || cdCheckPhase != 0) {
        return;
    }
    setCurrentDirectory(0xC, 3);
    cdCheckPhase = 1;
    cdCheckActiveCommand = command;
    switch (command) {
    case 1:
        cdCheckCommandCounts[1]++;
        cdCheckBufferSize = 0x2000;
        cdCheckBuffer1 = allocateHeap(PC_HEAP_BYTES(0x2000), 0);
        CLEAR(cdCheckBuffer1);
        readRawSectorsFromDisk(0x40, cdCheckBuffer1, cdCheckBufferSize, 0, 0);
        break;
    case 2:
        cdCheckCommandCounts[2]++;
        cdCheckBufferSize = getFileSizeAligned(7);
        cdCheckBuffer1 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
        CLEAR(cdCheckBuffer1);
        readFile(7, cdCheckBuffer1, 0, 0);
        break;
    case 3:
        cdCheckCommandCounts[3]++;
        cdCheckBatch1 = allocateFileGroupBatch(2, 0);
        if (cdCheckBatch1 == 0) {
            goto fail;
        }
        fileIndex = cdCheckBatch1[0].m0_fileIndex;
        for (k = 0; fileIndex > 0;) {
            p = cdCheckBatch1[k].m4_loadPtr;
            cdCheckBufferSize = getFileSizeAligned(fileIndex);
            CLEAR(p);
            k++;
            fileIndex = cdCheckBatch1[k].m0_fileIndex;
        }
        batchStartLoadingFiles(cdCheckBatch1, 0, 0);
        break;
    case 4:
        cdCheckCommandCounts[4]++;
        cdCheckBufferSize = 0x2000;
        cdCheckBuffer1 = allocateHeap(PC_HEAP_BYTES(0x2000), 0);
        CLEAR(cdCheckBuffer1);
        readRawSectorsFromDisk(0x40, cdCheckBuffer1, cdCheckBufferSize, 0, 0);
        break;
    case 5:
        cdCheckCommandCounts[5]++;
        cdCheckBufferSize = getFileSizeAligned(7);
        cdCheckBuffer1 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
        CLEAR(cdCheckBuffer1);
        readFile(7, cdCheckBuffer1, 1, 0);
        break;
    case 6:
        cdCheckCommandCounts[6]++;
        cdCheckBatch1 = allocateFileGroupBatch(2, 0);
        if (cdCheckBatch1 == 0) {
            goto fail;
        }
        fileIndex = cdCheckBatch1[0].m0_fileIndex;
        for (k = 0; fileIndex > 0;) {
            p = cdCheckBatch1[k].m4_loadPtr;
            cdCheckBufferSize = getFileSizeAligned(fileIndex);
            CLEAR(p);
            k++;
            fileIndex = cdCheckBatch1[k].m0_fileIndex;
        }
        batchStartLoadingFiles(cdCheckBatch1, 1, 0);
        break;
    case 7:
        cdCheckCommandCounts[7]++;
        if (cdCheckSectorBuffer == 0) {
            cdCheckSectorBuffer = allocateBufferForVramUpload(4, 0);
        }
        loadImageFileToVram(1, cdCheckSectorBuffer, 0, 0, 1, 0, 0, 0, 0, 0);
        break;
    case 8:
        cdCheckCommandCounts[8]++;
        if (cdCheckSectorBuffer == 0) {
            cdCheckSectorBuffer = allocateBufferForVramUpload(4, 0);
        }
        loadImageFileToVram(1, cdCheckSectorBuffer, 1, 0, 1, 0, 0, 0, 0, 0);
        break;
    case 11:
        if (cdCheckStreamMode == 2) {
            cdCheckStreamMode = 1;
        }
        cdCheckCommandCounts[11]++;
        if (cdCheckSectorBuffer == 0) {
            cdCheckSectorBuffer = allocateBufferForVramUpload(4, 0);
        }
        cdCheckStreamRest = cdCheckBufferSize = getFileSizeAligned(6);
        cdCheckStreamWritePtr = cdCheckBuffer1 = allocateHeap(PC_HEAP_BYTES(cdCheckBufferSize), 0);
        CLEAR(cdCheckBuffer1);
        readFile(6, cdCheckSectorBuffer, 1, 0x100);
        break;
    case 12:
        if (cdCheckStreamMode == 2) {
            cdCheckStreamMode = 1;
        }
        cdCheckCommandCounts[12]++;
        if (cdCheckSectorBuffer == 0) {
            cdCheckSectorBuffer = allocateBufferForVramUpload(4, 0);
        }
        cdCheckBatch1 = allocateFileGroupBatch(2, 0);
        if (cdCheckBatch1 == 0) {
        fail:
            cdCheckPhase = 0;
            cdCheckActiveCommand = 0;
            break;
        }
        fileIndex = cdCheckBatch1[0].m0_fileIndex;
        cdCheckStreamRest = getFileSizeAligned(fileIndex);
        cdCheckBatchIndex = 0;
        cdCheckStreamWritePtr = cdCheckBatch1[0].m4_loadPtr;
        for (k = 0; fileIndex > 0;) {
            p = cdCheckBatch1[k].m4_loadPtr;
            cdCheckBufferSize = getFileSizeAligned(fileIndex);
            CLEAR(p);
            k++;
            fileIndex = cdCheckBatch1[k].m0_fileIndex;
        }
        setSectorBuffer(cdCheckSectorBuffer);
        batchStartLoadingFiles(cdCheckBatch1, 1, 0x100);
        break;
    case 13:
        cdCheckStreamMode = 2;
        cdCheckCommandCounts[13]++;
        if (cdCheckSectorBuffer != 0) {
            freeHeap(cdCheckSectorBuffer);
        }
        cdCheckSectorBuffer = allocateBufferForVramUpload(0x40, 0);
        setCurrentDirectory(0x18, 0);
        readFile(3, cdCheckSectorBuffer, 0, 0x200);
        break;
    }
    if (cdCheckErrorCount == 0) {
        cdCheckPreviousCommand = cdCheckLastCommand;
        cdCheckLastCommand = cdCheckActiveCommand;
    }
}

void countCdromCheckVSyncs(void) {
    cdCheckVSyncCount++;
    cdCheckFrameCounter++;
    if (cdCheckFrameCounter >= 60) {
        cdCheckFrameCounter = 0;
        cdCheckSeconds++;
    }
}

void runDiscChangeScreen(void) {
    s32 action;
    s32 status;
    s32 result;
    s32 i;
    s32 state;
    s32 count;

    count = 0;
    state = 0;
    status = 0;
    result = 1;
    while (1) {
        movieCurrentDisplayBuffer = (movieCurrentDisplayBuffer == &movieDisplayBuffers[0]) ? &movieDisplayBuffers[1] : &movieDisplayBuffers[0];
        movieOddOrEven = 1 - movieOddOrEven;
        ClearOTagR(movieCurrentDisplayBuffer->m70_ot, 0x20);
        logToScreen("\n[ DISC CHANGE TEST NOW DISC %2d ]\n\n", getCurrentDiscNumber());
        updateMovieDebugMenuInput(0, 0, &action);
        logToScreen("  STATUS ");
        if (status == 1) {
            logToScreen("[ IT IS NOT PLAY STATION DISC ]\n");
        } else if (status == 2) {
            logToScreen("[ NOT XENOGEARS DISC ]\n");
        } else if (status == 3) {
            logToScreen("[ NO CHANGE DISC ]\n");
        } else if (status == 4) {
            logToScreen("[ RETRY SET DISC ]\n");
        } else {
            logToScreen("[ NOP ]\n");
        }
        logToScreen("\n");
        for (i = 0; i < 9; i++) {
            if (i < state) {
                switch (i) {
                case 0:
                    logToScreen("  1 : NORMAL SPEED\n");
                    break;
                case 1:
                    logToScreen("  2 : CD STOPED\n");
                    break;
                case 2:
                    logToScreen("  3 : CD OPENED\n");
                    break;
                case 3:
                    logToScreen("  4 : CD CLOSED\n");
                    break;
                case 4:
                    logToScreen("  5 : SPINDLE OK\n");
                    break;
                case 5:
                    logToScreen("  6 : TOC OK\n");
                    break;
                case 6:
                    logToScreen("  7 : SET LOCATION OK\n");
                    break;
                case 7:
                    logToScreen("  8 : PLAY STATION DISC OK\n");
                    break;
                case 8:
                    logToScreen("  9 : XENOGEARS %2d DISC OK\n", getCurrentDiscNumber());
                    break;
                }
            } else {
                logToScreen(" %2d :\n", i + 1);
            }
        }
        if (result != 0) {
            logToScreen("\n MODE %1d : NO ERROR  COUNT %6d\n", state, count);
        } else {
            logToScreen("\n MODE %1d : %2d ERROR COUNT %6d\n", state, 0, count);
        }
        logToScreen(" RESULT %02x %02x %02x %02x %02x %02x %02x %02x\n", discChangeCdResult[0], discChangeCdResult[1],
                    discChangeCdResult[2], discChangeCdResult[3], discChangeCdResult[4], discChangeCdResult[5], discChangeCdResult[6], discChangeCdResult[7]);
        logToScreen("\n\n PUSH START TO TEST.\n");
        logToScreen(" PUSH CIRCLE BUTTON TO MENU.\n");
        if (state > 0) {
            state = processDiscChange(3 - getCurrentDiscNumber(), state, &status, &result);
        }
        if ((moviePadButtons & 0x40) && !(moviePadPreviousButtons & 0x40) && state > 0 && state < 8) {
            state++;
        }
        if ((moviePadButtons & 0x800) && !(moviePadPreviousButtons & 0x800) && (state == 0 || state == 9 || status != 0)) {
            status = 0;
            state = 2;
            stopCdForDiscChange();
        }
        if (movieShowHeapDump > 0) {
            movieShowHeapDump = 0;
        }
        printDebugTextOnScreen(movieCurrentDisplayBuffer->m70_ot);
        drawGradientBackground(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->mF0_background, 8, 0x14, 0x130, 0xC0);
        drawGradientBorder(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->m114_background2, 7, 0x13, 0x132, 0xC2);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
        PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
        DrawOTag(&movieCurrentDisplayBuffer->m70_ot[31]);
        if (action == 2) {
            break;
        }
        count++;
    }
}

void stopCdForDiscChange(void) {
    if (getCdromFnd() == 0) {
        stopCdRead(0);
        waitReadCompletion(0);
        setCdMode(0);
        waitReadCompletion(0);
        VSync(3);
        while (CdControlB(8, 0, discChangeCdResult) == 0) {
        }
    }
}

void loadFileFromPC(char* name, void* buf, s32 size) {
    s32 fd;

    fd = PCopen(name, 0, 0);
    PCread(fd, buf, size);
    PCclose(fd);
}

s32 processDiscChange(s32 disc, s32 state, s32* status, s32* result) {
    s32 buf[4] = {0, 0, 0, 0};
    CdlLOC loc;
    s32 ret;

    CdIntToPos(0, &loc);
    ret = 1;
    if (*status != 0) {
        return state;
    }
    if (getCdromFnd() != 0 && state < 9) {
        if (disc == 1) {
            loadFileFromPC("c:\\work\\cdrom.mdg", fatFileTableBuffer, 0x8000);
            loadFileFromPC("c:\\work\\cdrom.fid", fatDirectoryTableBuffer, 0x7A);
            loadFileFromPC("c:\\work\\cdrom.fnd", (void*)cdromFnd, 0x40000);
        } else {
            loadFileFromPC("c:\\work\\cdrom2.mdg", fatFileTableBuffer, 0x8000);
            loadFileFromPC("c:\\work\\cdrom2.fid", fatDirectoryTableBuffer, 0x7A);
            loadFileFromPC("c:\\work\\cdrom2.fnd", (void*)cdromFnd, 0x40000);
        }
        state = 9;
    } else {
        switch (state) {
        case 1:
            ret = CdControlB(8, 0, discChangeCdResult);
            if (ret != 0) {
                state++;
            }
            break;
        case 2:
            CdControlB(1, 0, discChangeCdResult);
            if (discChangeCdResult[0] & 0x10) {
                state++;
            }
            break;
        case 3:
            CdControlB(1, 0, discChangeCdResult);
            if (!(discChangeCdResult[0] & 0x10)) {
                state++;
            }
            break;
        case 4:
            ret = CdControlB(1, 0, discChangeCdResult);
            if ((discChangeCdResult[0] & 2) && ret != 0) {
                state++;
            }
            break;
        case 5:
            ret = CdControlB(0x13, 0, discChangeCdResult);
            if (ret != 0) {
                state++;
            }
            break;
        case 6:
            ret = CdControlB(2, (u8*)&loc, discChangeCdResult);
            if (ret != 0) {
                state++;
            }
            break;
        case 7:
            ret = CdControlB(0x15, 0, discChangeCdResult);
            if ((discChangeCdResult[0] & 1) && (discChangeCdResult[1] & 0x40) && ret == 0) {
                *status = 1;
            } else if (ret != 0) {
                state++;
            }
            if (ret == 0) {
                state = 5;
            }
            break;
        case 8:
            setCdMode(0xA0);
            waitReadCompletion(0);
            VSync(3);
            readRawSectorsFromDisk(0x17, buf, 0x10, 0, 0);
            waitReadCompletion(0);
            if (buf[1] == 0x4E45585F) {
                if (((u8*)buf)[3] == disc + 0x30) {
                    readRawSectorsFromDisk(0x18, fatFileTableBuffer, 0x8000, 0, 0);
                    waitReadCompletion(0);
                    readRawSectorsFromDisk(0x28, fatDirectoryTableBuffer, 0x7A, 0, 0);
                    waitReadCompletion(0);
                    state++;
                    *status = 0;
                } else {
                    *status = 3;
                }
            } else {
                *status = 2;
            }
            break;
        }
    }
    *result = ret;
    return state;
}

void initGradientBackground(POLY_G4* p0, POLY_G4* p1, s32 x, s32 y, s32 w, s32 h) {
    s32 i;

    for (i = 0; i < 4; i++) {
        gradientBackgroundFrom[i].r = (getMovieDebugRandom() & 0xFF) / 32 + 8;
        gradientBackgroundFrom[i].g = 8;
        gradientBackgroundFrom[i].b = (getMovieDebugRandom() & 0xFF) / 3 + 0x10;
        gradientBackgroundTo[i].r = (getMovieDebugRandom() & 0xFF) / 32 + 8;
        gradientBackgroundTo[i].g = 8;
        gradientBackgroundTo[i].b = (getMovieDebugRandom() & 0xFF) / 3 + 0x10;
    }
    for (i = 0; i < 4; i++) {
        gradientBackgroundCounter[i] = 0;
        gradientBackgroundPeriod[i] = (getMovieDebugRandom() & 0xFF) + 0x20;
    }
    SetPolyG4(p0);
    SetSemiTrans(p0, 0);
    setXYWH(p0, x, y, w, h);
    SetPolyG4(p1);
    SetSemiTrans(p1, 0);
    setXYWH(p1, x, y, w, h);
}

void drawGradientBackground(OTag* ot, POLY_G4* p, s32 x, s32 y, s32 w, s32 h) {
    s32 i;
    s16 r;
    s16 g;
    s16 b;

    setXYWH(p, x, y, w, h);
    for (i = 0; i < 4; i++) {
        if (++gradientBackgroundCounter[i] > gradientBackgroundPeriod[i]) {
            gradientBackgroundCounter[i] = 0;
            gradientBackgroundPeriod[i] = (getMovieDebugRandom() & 0xFF) + 0x20;
            gradientBackgroundFrom[i].r = gradientBackgroundTo[i].r;
            gradientBackgroundFrom[i].g = gradientBackgroundTo[i].g;
            gradientBackgroundFrom[i].b = gradientBackgroundTo[i].b;
            gradientBackgroundTo[i].r = (getMovieDebugRandom() & 0xFF) / 32 + 8;
            gradientBackgroundTo[i].g = 8;
            gradientBackgroundTo[i].b = (getMovieDebugRandom() & 0xFF) / 3 + 0x10;
        }
        r = gradientBackgroundFrom[i].r + (gradientBackgroundTo[i].r - gradientBackgroundFrom[i].r) * gradientBackgroundCounter[i] / gradientBackgroundPeriod[i];
        g = gradientBackgroundFrom[i].g + (gradientBackgroundTo[i].g - gradientBackgroundFrom[i].g) * gradientBackgroundCounter[i] / gradientBackgroundPeriod[i];
        b = gradientBackgroundFrom[i].b + (gradientBackgroundTo[i].b - gradientBackgroundFrom[i].b) * gradientBackgroundCounter[i] / gradientBackgroundPeriod[i];
        switch (i) {
        case 0:
            setRGB0(p, r, g, b);
            break;
        case 1:
            setRGB1(p, r, g, b);
            break;
        case 2:
            setRGB2(p, r, g, b);
            break;
        case 3:
            setRGB3(p, r, g, b);
            break;
        }
    }
    addPrim(ot, p);
}

void initGradientBorder(POLY_G4* p0, POLY_G4* p1, s32 x, s32 y, s32 w, s32 h) {
    s32 i;

    for (i = 0; i < 4; i++) {
        gradientBorderFrom[i].r = 0xFF;
        gradientBorderFrom[i].g = 0xFF;
        gradientBorderFrom[i].b = (getMovieDebugRandom() & 0x3F) - 0x42;
        gradientBorderTo[i].r = 0xFF;
        gradientBorderTo[i].g = 0xFF;
        gradientBorderTo[i].b = (getMovieDebugRandom() & 0x3F) - 0x42;
    }
    for (i = 0; i < 4; i++) {
        gradientBorderCounter[i] = 0;
        gradientBorderPeriod[i] = (getMovieDebugRandom() & 0xFF) + 0x20;
    }
    SetPolyG4(p0);
    setXYWH(p0, x, y, w, h);
    SetPolyG4(p1);
    setXYWH(p1, x, y, w, h);
}

void drawGradientBorder(OTag* ot, POLY_G4* p, s32 x, s32 y, s32 w, s32 h) {
    s32 i;
    s16 r;
    s16 g;
    s16 b;

    setXYWH(p, x, y, w, h);
    for (i = 0; i < 4; i++) {
        if (++gradientBorderCounter[i] > gradientBorderPeriod[i]) {
            gradientBorderCounter[i] = 0;
            gradientBorderPeriod[i] = (getMovieDebugRandom() & 0xFF) + 0x20;
            gradientBorderFrom[i].r = gradientBorderTo[i].r;
            gradientBorderFrom[i].g = gradientBorderTo[i].g;
            gradientBorderFrom[i].b = gradientBorderTo[i].b;
            gradientBorderTo[i].r = 0xFF;
            gradientBorderTo[i].g = 0xFF;
            gradientBorderTo[i].b = (getMovieDebugRandom() & 0x3F) - 0x42;
        }
        r = gradientBorderFrom[i].r + (gradientBorderTo[i].r - gradientBorderFrom[i].r) * gradientBorderCounter[i] / gradientBorderPeriod[i];
        g = gradientBorderFrom[i].g + (gradientBorderTo[i].g - gradientBorderFrom[i].g) * gradientBorderCounter[i] / gradientBorderPeriod[i];
        b = gradientBorderFrom[i].b + (gradientBorderTo[i].b - gradientBorderFrom[i].b) * gradientBorderCounter[i] / gradientBorderPeriod[i];
        switch (i) {
        case 0:
            setRGB0(p, r, g, b);
            break;
        case 1:
            setRGB1(p, r, g, b);
            break;
        case 2:
            setRGB2(p, r, g, b);
            break;
        case 3:
            setRGB3(p, r, g, b);
            break;
        }
    }
    addPrim(ot, p);
}

