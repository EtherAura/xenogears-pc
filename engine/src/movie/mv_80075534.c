/* Movie overlay: the CD-ROM sector monitor and FAT check screens.
 * gcc-2.6.3-psx -fno-strength-reduce (runFatCheckScreen needs it), ASPSX 2.34,
 * divide checks kept. Linker fill before and after its .rodata and its own
 * "\n" make it a separate object. */
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
extern u8 discChangeCdResult[];
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


void runCdromMonitorScreen(void) {
    CdlLOC loc;
    s32 action;
    u8* buf;
    s32 row;
    s32 col;
    u8* p;
    u8* q;
    s32 c;
    s32 sector;
    s32 cursor;

    clearMovieVram();
    buf = allocateHeap(PC_HEAP_BYTES(0x800), 0);
    if (buf == 0) {
        return;
    }
    SetDefDrawEnv(&movieDisplayBuffers[0].m0_drawEnv, 0, 0, 0x280, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[0].m5C_dispEnv, 0, 0xF0, 0x280, 0xF0);
    SetDefDrawEnv(&movieDisplayBuffers[1].m0_drawEnv, 0, 0xF0, 0x280, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[1].m5C_dispEnv, 0, 0, 0x280, 0xF0);
    movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[0].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[0].m5C_dispEnv.screen.h = 0xD8;
    movieDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[1].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[1].m5C_dispEnv.screen.h = 0xD8;
    readRawSectorsFromDisk(cdMonitorSector, buf, 0x800, 0, 0);
    waitReadCompletion(0);
    while (1) {
        movieCurrentDisplayBuffer = (movieCurrentDisplayBuffer == &movieDisplayBuffers[0]) ? &movieDisplayBuffers[1] : &movieDisplayBuffers[0];
        movieOddOrEven = 1 - movieOddOrEven;
        ClearOTagR(movieCurrentDisplayBuffer->m70_ot, 0x20);
        logToScreen("\n[ MONITOR ]\n");
        updateMovieDebugMenuInput(0, 0, &action);
        sector = cdMonitorSector;
        if (!(moviePadPreviousButtons & 0x1000) && (moviePadButtons & 0x1000) && cdMonitorRow > 0) {
            cdMonitorRow--;
        }
        if (!(moviePadPreviousButtons & 0x10) && (moviePadButtons & 0x10)) {
            cdMonitorRow -= 12;
            if (cdMonitorRow < 0) {
                cdMonitorRow = 0;
            }
        }
        if (!(moviePadPreviousButtons & 0x4000) && (moviePadButtons & 0x4000) && cdMonitorRow < 0x74) {
            cdMonitorRow++;
        }
        if (!(moviePadPreviousButtons & 0x40) && (moviePadButtons & 0x40)) {
            cdMonitorRow += 12;
            if (cdMonitorRow >= 0x74) {
                cdMonitorRow = 0x74;
            }
        }
        if (!(moviePadPreviousButtons & 0x8000) && (moviePadButtons & 0x8000) && cdMonitorSector > 0) {
            cdMonitorSector--;
        }
        if (!(moviePadPreviousButtons & 0x2000) && (moviePadButtons & 0x2000)) {
            cdMonitorSector++;
        }
        if (!(moviePadPreviousButtons & 4) && (moviePadButtons & 4)) {
            cdMonitorSector -= 75;
            if (cdMonitorSector < 0) {
                cdMonitorSector = 0;
            }
        }
        if (!(moviePadPreviousButtons & 8) && (moviePadButtons & 8)) {
            cdMonitorSector += 75;
        }
        if (!(moviePadPreviousButtons & 1) && (moviePadButtons & 1)) {
            cdMonitorSector -= 4500;
            if (cdMonitorSector < 0) {
                cdMonitorSector = 0;
            }
        }
        if (!(moviePadPreviousButtons & 2) && (moviePadButtons & 2)) {
            cdMonitorSector += 4500;
        }
        if (sector != cdMonitorSector) {
            readRawSectorsFromDisk(cdMonitorSector, buf, 0x800, 0, 0);
            waitReadCompletion(0);
        }
        CdIntToPos(cdMonitorSector, &loc);
        logToScreen("ABSPOS %8d POS %04x\nMINUTE %02x SECOND %02x SECTOR %02x\n\n", cdMonitorSector, cdMonitorRow << 4,
                    loc.minute, loc.second, loc.sector);
        p = q = buf + (cdMonitorRow << 4);
        for (row = 0; row < 12; row++) {
            logToScreen("%03x:", (row + cdMonitorRow) << 4);
            for (col = 0; col < 15; col++) {
                logToScreen("%02x ", *p);
                p++;
            }
            logToScreen("%02x", *p);
            p++;
            logToScreen(":");
            for (col = 0; col < 16; col++) {
                c = *q;
                if (c >= 0x20 && c < 0x7E) {
                    logToScreen("%c", c);
                } else {
                    logToScreen(" ");
                }
                q++;
            }
            logToScreen("\n");
        }
        logToScreen("\nPUSH CIRCLE BUTTON TO MENU.");
        if (movieShowHeapDump > 0) {
            dumpHeapBlocks(1, 0, 6, 0x808D);
        }
        printDebugTextOnScreen(movieCurrentDisplayBuffer->m70_ot);
        drawGradientBackground(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->mF0_background, 8, 0x14, 0x270, 0xA0);
        drawGradientBorder(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->m114_background2, 7, 0x13, 0x272, 0xA2);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
        PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
        DrawOTag(&movieCurrentDisplayBuffer->m70_ot[31]);
        cursor = movieDebugMenuCursor;
        if (action == 2) {
            break;
        }
        movieDebugMenuCursor = cursor;
    }
    freeHeap(buf);
    clearMovieVram();
    SetDefDrawEnv(&movieDisplayBuffers[0].m0_drawEnv, 0, 0, 0x140, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[0].m5C_dispEnv, 0, 0xF0, 0x140, 0xF0);
    SetDefDrawEnv(&movieDisplayBuffers[1].m0_drawEnv, 0, 0xF0, 0x140, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[1].m5C_dispEnv, 0, 0, 0x140, 0xF0);
    movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[0].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[0].m5C_dispEnv.screen.h = 0xD8;
    movieDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[1].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[1].m5C_dispEnv.screen.h = 0xD8;
}

s32 getFatEntrySize(s32 index) {
    u8* entry = &fatFileTableBuffer[index * 7];

    return (entry[6] << 24) + (entry[5] << 16) + (entry[4] << 8) | entry[3];
}

void runFatCheckScreen(void) {
    s32 dir;
    s32 offset;
    s32 action;
    s32 top;
    s32 hex;
    s32 detail;
    s32 max;
    s32 i;
    char* name;
    char* p;

    clearMovieVram();
    top = 0;
    getCurrentDirectory(&dir, &offset);
    setCurrentDirectory(0, 0);
    hex = 0;
    detail = 0;
    do {
        max = 0x1249;
        movieCurrentDisplayBuffer = (movieCurrentDisplayBuffer == &movieDisplayBuffers[0]) ? &movieDisplayBuffers[1] : &movieDisplayBuffers[0];
        movieOddOrEven = 1 - movieOddOrEven;
        ClearOTagR(movieCurrentDisplayBuffer->m70_ot, 0x20);
        logToScreen("\n[ FAT CHECK MODE ");
        logToScreen(hex ? "HEX ]\n" : "DEC ]\n");
        updateMovieDebugMenuInput(0, 0, &action);
        if (!(moviePadPreviousButtons & 0x1000) && (moviePadButtons & 0x1000) && top > 0) {
            top--;
        }
        if (!(moviePadPreviousButtons & 0x10) && (moviePadButtons & 0x10)) {
            top -= 20;
            if (top < 0) {
                top = 0;
            }
        }
        if (!(moviePadPreviousButtons & 0x4000) && (moviePadButtons & 0x4000) && top < max - 20) {
            top++;
        }
        if (!(moviePadPreviousButtons & 0x40) && (moviePadButtons & 0x40)) {
            top += 20;
            if (top > max - 20) {
                top = max - 20;
            }
        }
        if (!(moviePadPreviousButtons & 8) && (moviePadButtons & 8)) {
            hex = 1 - hex;
        }
        if (!(moviePadPreviousButtons & 4) && (moviePadButtons & 4)) {
            detail = 1 - detail;
        }
        i = top;
        do {
            if (getFatEntrySize(i) == 0) {
                if (hex) {
                    logToScreen("No %4x NullFile\n", i);
                } else {
                    logToScreen("No %4d NullFile\n", i);
                }
            } else {
                if (hex) {
                    logToScreen("No %4x Sect%6x ", i, getFileStartSector(i + 1));
                } else {
                    logToScreen("No %4d Sect%6d ", i, getFileStartSector(i + 1));
                }
                if (detail) {
                    if (getFatEntrySize(i) < 0) {
                        logToScreen("[P%3d]\n", -getFatEntrySize(i));
                    } else {
                        name = getFilenameFromPC(i + 1);
                        if (name != 0) {
                            for (p = name; *p != 0; p++) {
                                if (*p == '\\') {
                                    name = p + 1;
                                }
                            }
                            logToScreen("%s\n", name);
                        } else {
                            logToScreen("\n");
                        }
                    }
                } else {
                    if (hex) {
                        logToScreen("Size%9x\n", getFatEntrySize(i));
                    } else {
                        logToScreen("Size%9d\n", getFatEntrySize(i));
                    }
                }
            }
            i++;
        } while (i < top + 20);
        logToScreen("\nPUSH CIRCLE BUTTON TO MENU.");
        if (movieShowHeapDump > 0) {
            dumpHeapBlocks(1, 0, 6, 0x808D);
        }
        printDebugTextOnScreen(movieCurrentDisplayBuffer->m70_ot);
        drawGradientBackground(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->mF0_background, 8, 0x14, 0x130, 0xC0);
        drawGradientBorder(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->m114_background2, 7, 0x13, 0x132, 0xC2);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
        PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
        DrawOTag(&movieCurrentDisplayBuffer->m70_ot[31]);
    } while (action != 2);
    setCurrentDirectory(dir, offset);
}

