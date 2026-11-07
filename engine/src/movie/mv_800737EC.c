/* Movie overlay entry (Noah movieEntryPoint) and its debug menu: movie test
 * settings, frame search, sound test. gcc-2.6.3-psx, ASPSX 2.34, divide
 * checks kept. Its own "\n" (0x800701C8) and the linker fill after its last
 * string make it a separate object. */
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
void initGradientBackground(POLY_G4* p0, POLY_G4* p1, s32 x, s32 y, s32 w, s32 h);
void initGradientBorder(POLY_G4* p0, POLY_G4* p1, s32 x, s32 y, s32 w, s32 h);
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

void portResetModule(const char* name);
void pcMovieStartOption(void);

void movieEntryPoint(void) {
    char unused[8] = "trouble";
    s32 action;
    s32 delta;
    s32 step;
    void* tmp;
    void* data;
    s32 i;
    s32 end;
    s32 cursor;

    resetMemoryAllocStats(4, 0);
    setCurrentDirectory(0x18, 0);
    setCdVolume(0, 0);
    DrawSync(0);
    VSync(0);
    SetDispMask(0);
    tmp = allocateHeap(PC_HEAP_BYTES(4), 1);
    /* The video player is linked in, so no padding to load it at its link
     * address: a token block keeps the frees balanced. */
    data = allocateHeap(PC_HEAP_SIZED(4, pcHeapReservation(tmp, 0x801D3000u)), 1);
    freeHeap(tmp);
    if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
        readFile(1, portOverlayScratch(0, getFileSizeAligned(1)), 0, 0);
        waitReadCompletion(0);
    }
    portResetModule("video_player"); /* its data as a fresh load leaves it */
    initStrPlayer(0x140, 0x100, 0x80, 0x10, 0x20, 0x800, 3);
    movieCdromFnd = getCdromFnd();
    movieShowHeapDump = 0;
    D_800773B0 = 0;
    movieRgb24 = 1;
    movieScreenDrawLines = -1;
    moviePlaybackType = 1;
    moviePlaybackNumber = 0;
    movieStartFrame = 1;
    movieEndFrame = 0xC80;
    movieChannel = 1;
    movieStartFrameStatus = 2;
    movieEndFrameStatus = 0;
    movieStartSector = 0;
    movieRewind = 0;
    SetDefDrawEnv(&movieDisplayBuffers[0].m0_drawEnv, 0, 0, 0x140, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[0].m5C_dispEnv, 0, 0xF0, 0x140, 0xF0);
    SetDefDrawEnv(&movieDisplayBuffers[1].m0_drawEnv, 0, 0xF0, 0x140, 0xF0);
    SetDefDispEnv(&movieDisplayBuffers[1].m5C_dispEnv, 0, 0, 0x140, 0xF0);
    movieDisplayBuffers[0].m0_drawEnv.dtd = 1;
    movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[1].m0_drawEnv.dtd = 1;
    movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
    movieDisplayBuffers[0].m0_drawEnv.r0 = 0;
    movieDisplayBuffers[0].m0_drawEnv.g0 = 0;
    movieDisplayBuffers[0].m0_drawEnv.b0 = 0;
    movieDisplayBuffers[0].m5C_dispEnv.isinter = 0;
    movieDisplayBuffers[1].m0_drawEnv.r0 = 0;
    movieDisplayBuffers[1].m0_drawEnv.g0 = 0;
    movieDisplayBuffers[1].m0_drawEnv.b0 = 0;
    movieDisplayBuffers[1].m5C_dispEnv.isinter = 0;
    movieDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[0].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[0].m5C_dispEnv.screen.h = 0xD8;
    movieDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
    movieDisplayBuffers[1].m5C_dispEnv.screen.y = 0xA;
    movieDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
    movieDisplayBuffers[1].m5C_dispEnv.screen.h = 0xD8;
    movieCurrentDisplayBuffer = &movieDisplayBuffers[0];
    movieOddOrEven = 0;
    PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
    PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
    if (movieCdromFnd != 0) {
        VSync(2);
        moviePadButtons = readPadButtons(0);
    } else {
        moviePadButtons = 0;
    }
    pcMovieStartOption(); /* XG_MOVIE (port/src/game/movie/sweep.c) */
    if (movieType.m0_type != 0xFF && !(moviePadButtons & 0x100)) {
        movieDebugMenuActive = 0;
        movieChannel = 1;
        movieStartFrame = 1;
        moviePlaybackType = movieType.m0_type & 0x7F;
        moviePlaybackNumber = movieType.m1_number;
        if (movieType.m0_type & 0x80) {
            movieEndFrame = movieRequestEndFrame;
        } else {
            movieEndFrame = 0xE9;
        }
        movieStartPlaybackFromScript(movieType.m3_fadeParam);
        freeStrPlayer();
        freeHeap(data);
        setGameMode(movieType.m2_returnMode);
        bootGame(0);
    }
    initGradientBackground(&movieDisplayBuffers[0].mF0_background, &movieDisplayBuffers[1].mF0_background, 0, 0, 0, 0);
    initGradientBorder(&movieDisplayBuffers[0].m114_background2, &movieDisplayBuffers[1].m114_background2, 0, 0, 0, 0);
    initDebugFont(0x10, 0x10, 0x280, 0xF0, 0x400, 0, 0x280, 0, 0x280, 0x100, 0);
    movieDebugMenuActive = 1;
    SetDispMask(1);
    while (1) {
        movieCurrentDisplayBuffer = (movieCurrentDisplayBuffer == &movieDisplayBuffers[0]) ? &movieDisplayBuffers[1] : &movieDisplayBuffers[0];
        movieOddOrEven = 1 - movieOddOrEven;
        ClearOTagR(movieCurrentDisplayBuffer->m70_ot, 0x20);
        if (movieCdromFnd == 0) {
            logToScreen("  [ MOVIE CD-ROM MODE1 DISK %1d ]  \n\n", getCurrentDiscNumber());
        } else if (movieCdromFnd == -1) {
            logToScreen("  [ MOVIE CD-ROM MODE2 DISK %1d ]  \n\n", getCurrentDiscNumber());
        } else {
            logToScreen("  [ MOVIE PC HDD MODE  DISK %1d ]  \n\n", getCurrentDiscNumber());
        }
        logToScreen("    ERROR %2d Sect %2d:%2d FM%3d\n", cdErrorCount, cdStopRecoveryOkCount, cdStopRecoveryErrorCount, streamFrameNumber);
        step = 1;
        logToScreen("    LesMem%2d NoMem%2d Skp%3d\n", D_8005A49C, cdStopRecoveryCount, strSkippedFrames, movieRequestEndFrame);
        delta = updateMovieDebugMenuInput(0, 0xD, &action);
        if (moviePadButtons & 0x10) {
            step = 0x20;
        }
        if (moviePadButtons & 0x80) {
            step *= 0x80;
        }
        if (movieDebugMenuCursor == 0 && delta != 0) {
            moviePlaybackType += delta;
            if (moviePlaybackType < 0) {
                moviePlaybackType = 2;
            }
            if (moviePlaybackType >= 3) {
                moviePlaybackType = 0;
            }
            movieStartFrame = 1;
            movieStartSector = 0;
            movieEndFrameStatus = 0;
            movieStartFrameStatus = 2;
        }
        if (movieDebugMenuCursor == 1 && delta != 0) {
            moviePlaybackNumber += delta;
            if (moviePlaybackNumber < 0) {
                moviePlaybackNumber = 0x3F;
            }
            if (moviePlaybackNumber >= 0x40) {
                moviePlaybackNumber = 0;
            }
            movieStartFrame = 1;
            movieStartSector = 0;
            movieEndFrameStatus = 0;
            movieStartFrameStatus = 2;
        }
        if (movieDebugMenuCursor == 2 && delta != 0) {
            movieStartFrame += delta * step;
            if (movieStartFrame <= 0) {
                movieStartFrame = 1;
            }
            if (movieStartFrame >= 0x2000) {
                movieStartFrame = 0x1FFF;
            }
            if (movieStartFrame > movieEndFrame) {
                movieStartFrame = movieEndFrame;
            }
            movieStartFrameStatus = 1;
        }
        if (movieDebugMenuCursor == 2 && action == 2 && movieStartFrameStatus == 1) {
            movieStartSector = findMovieFrameSector(movieStartFrame);
            if (movieStartFrame >= movieEndFrame) {
                movieEndFrame = movieStartFrame;
                movieEndFrameStatus = 0;
            }
            movieStartFrameStatus = 2;
        }
        if (movieDebugMenuCursor == 3 && delta != 0) {
            movieEndFrame += delta * step;
            if (movieEndFrame <= 0) {
                movieEndFrame = 1;
            }
            if (movieEndFrame >= 0x2000) {
                movieEndFrame = 0x1FFF;
            }
            if (movieEndFrame < movieStartFrame) {
                movieEndFrame = movieStartFrame;
            }
            movieEndFrameStatus = 0;
        }
        if (movieDebugMenuCursor == 3 && action == 2 && movieEndFrameStatus == 0) {
            end = getMovieLastFrameNumber();
            if (end >= 0) {
                movieEndFrame = end;
                movieEndFrameStatus = 1;
                if (movieStartFrame >= end) {
                    movieStartFrame = end;
                    movieStartFrameStatus = 1;
                }
            } else {
                movieEndFrameStatus = 2;
            }
        }
        if (movieDebugMenuCursor == 4 && delta != 0) {
            movieChannel += delta;
            if (movieChannel < 0) {
                movieChannel = 7;
            }
            if (movieChannel >= 8) {
                movieChannel = 0;
            }
        }
        if (movieDebugMenuCursor == 5 && delta != 0) {
            movieRgb24 = 1 - movieRgb24;
        }
        if (movieDebugMenuCursor == 6) {
            if (delta != 0) {
                movieScreenDrawLines = (movieScreenDrawLines + delta * step) & 0xFF;
            }
            if (action == 2) {
                movieScreenDrawLines = -1;
            }
        }
        if (movieDebugMenuCursor == 7 && delta != 0) {
            movieRewind = 1 - movieRewind;
        }
        for (i = 0; i < 14; i++) {
            logToScreen(movieDebugMenuCursor == i ? "  >" : "   ");
            switch (i) {
            case 0:
                logToScreen(" MOVIE TYPE   ");
                if (moviePlaybackType == 0) {
                    logToScreen("PICTURE ONLY\n");
                } else if (moviePlaybackType == 1) {
                    logToScreen("PICTURE+ADPCM\n");
                } else if (moviePlaybackType == 2) {
                    logToScreen("ADPCM ONLY\n");
                }
                break;
            case 1:
                logToScreen(" MOVIE NUMBER %4d\n\n", moviePlaybackNumber);
                break;
            case 2:
                logToScreen(" START FRAME  %4d ", movieStartFrame);
                if (movieStartFrameStatus == 1) {
                    logToScreen("SET");
                }
                if (movieStartFrameStatus == 2) {
                    if (movieStartSector < 0) {
                        logToScreen("EOF");
                    } else {
                        logToScreen("+%4dSECT", movieStartSector);
                    }
                }
                logToScreen("\n");
                break;
            case 3:
                logToScreen(" END   FRAME  %4d ", movieEndFrame);
                if (movieEndFrameStatus == 0) {
                    logToScreen("SET");
                }
                if (movieEndFrameStatus == 2) {
                    logToScreen("???");
                }
                logToScreen("\n");
                break;
            case 4:
                logToScreen(" MOVIE CHANNEL %3d\n", movieChannel);
                break;
            case 5:
                logToScreen(" SCREEN MODE  ");
                logToScreen(movieRgb24 ? "24 BIT COLOR" : "16 BIT COLOR");
                logToScreen("\n");
                break;
            case 6:
                logToScreen(" SCREEN DRAW  ");
                if (movieScreenDrawLines < 0) {
                    logToScreen("ALL");
                } else {
                    logToScreen("%3d", movieScreenDrawLines);
                }
                logToScreen("\n");
                break;
            case 7:
                logToScreen(" REWIND       ");
                logToScreen(movieRewind ? "ON" : "OFF");
                logToScreen("\n\n");
                break;
            case 8:
                logToScreen(" MOVIE START.\n\n");
                break;
            case 9:
                logToScreen(" CD-ROM MONITOR.\n\n");
                break;
            case 10:
                logToScreen(" CD-ROM CHECK.\n");
                break;
            case 11:
                logToScreen(" FAT CHECK.\n\n");
                break;
            case 12:
                logToScreen(" [DISC CHANGE.]\n");
                break;
            case 13:
                logToScreen(" [RETURN TO KERNEL.]\n");
                break;
            }
        }
        if (movieShowHeapDump > 0) {
            dumpHeapBlocks(1, 0, 6, 0x808D);
        }
        printDebugTextOnScreen(movieCurrentDisplayBuffer->m70_ot);
        drawGradientBackground(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->mF0_background, 0x14, 0xC, 0x11C, 0xC6);
        drawGradientBorder(movieCurrentDisplayBuffer->m70_ot, &movieCurrentDisplayBuffer->m114_background2, 0x13, 0xB, 0x11E, 0xC8);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&movieCurrentDisplayBuffer->m0_drawEnv);
        PutDispEnv(&movieCurrentDisplayBuffer->m5C_dispEnv);
        DrawOTag(&movieCurrentDisplayBuffer->m70_ot[31]);
        cursor = movieDebugMenuCursor;
        if ((cursor == 0 || cursor == 1 || cursor == 4 || cursor == 5 || cursor == 7 || cursor == 8) && action == 2) {
            D_8005A49C = 0;
            cdStopRecoveryCount = 0;
            cdStopRecoveryOkCount = 0;
            cdStopRecoveryErrorCount = 0;
            playMovieFromDebugMenu();
            moviePadButtons = -1;
        }
        if (movieDebugMenuCursor == 9 && action == 2) {
            runCdromMonitorScreen();
        }
        if (movieDebugMenuCursor == 10 && action == 2) {
            runCdromCheckScreen();
        }
        if (movieDebugMenuCursor == 11 && action == 2) {
            runFatCheckScreen();
        }
        if (movieDebugMenuCursor == 12 && action == 2) {
            runDiscChangeScreen();
        }
        if (movieDebugMenuCursor == 13 && action == 2) {
            freeHeap(data);
            bootGame(0);
        }
        movieDebugMenuCursor = cursor;
        checkSoftReboot();
    }
}

s32 updateMovieDebugMenuInput(s32 min, s32 max, s32* action) {
    moviePadPreviousButtons = moviePadButtons;
    moviePadButtons = readPadButtons(0);
    if (moviePadPreviousButtons == moviePadButtons && moviePadPreviousButtons != 0) {
        if (++movieMenuRepeatTimer > movieMenuRepeatDelay) {
            moviePadPreviousButtons = 0;
            movieMenuRepeatDelay = 1;
            movieMenuRepeatTimer = 0;
        }
    } else {
        movieMenuRepeatDelay = 0x10;
        movieMenuRepeatTimer = 0;
    }
    if (!(moviePadPreviousButtons & 0x1000) && (moviePadButtons & 0x1000)) {
        if (--movieDebugMenuCursor < min) {
            movieDebugMenuCursor = max;
        }
    }
    if (!(moviePadPreviousButtons & 0x4000) && (moviePadButtons & 0x4000)) {
        if (++movieDebugMenuCursor > max) {
            movieDebugMenuCursor = min;
        }
    }
    *action = 0;
    if (!(moviePadPreviousButtons & 0x10) && (moviePadButtons & 0x10)) {
        *action = 1;
    }
    if (!(moviePadPreviousButtons & 0x20) && (moviePadButtons & 0x20)) {
        *action = 2;
    }
    if (!(moviePadPreviousButtons & 0x40) && (moviePadButtons & 0x40)) {
        *action = 3;
    }
    if (!(moviePadPreviousButtons & 0x80) && (moviePadButtons & 0x80)) {
        *action = 4;
    }
    if (!(moviePadPreviousButtons & 0x100) && (moviePadButtons & 0x100)) {
        D_800773B0 = 1 - D_800773B0;
    }
    if (!(moviePadPreviousButtons & 0x800) && (moviePadButtons & 0x800)) {
        movieShowHeapDump = 1 - movieShowHeapDump;
    }
    if (!(moviePadPreviousButtons & 0x2000) && (moviePadButtons & 0x2000)) {
        return 1;
    }
    if (!(moviePadPreviousButtons & 0x8000) && (moviePadButtons & 0x8000)) {
        return -1;
    }
    return 0;
}

s32 getMovieDebugRandom(void) {
    movieDebugRandomSeed1 = movieDebugRandomSeed1 * 5 + 1;
    movieDebugRandomSeed2 = movieDebugRandomSeed2 * 7 + 3;
    movieDebugRandomSeed1 = (movieDebugRandomSeed1 ^ movieDebugRandomSeed2) + 1;
    if (movieDebugRandomSeed1 < 0) {
        movieDebugRandomSeed1 = -movieDebugRandomSeed1;
    }
    return movieDebugRandomSeed1;
}

void clearMovieVram(void) {
    RECT rect;

    rect.w = 0x280;
    rect.x = 0;
    rect.y = 0;
    rect.h = 0x1E0;
    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
}

s32 findMovieFrameSector(s32 frame) {
    u8 buf[0x1000];
    char* name;
    s32 fileIndex;
    s32 sectorSize;
    s32 readSize;
    s32 offset;
    s32 best;
    s32 fd;
    s32 n;
    s32 sector;
    s32 next;
    s32 nSect;
    s32 total;
    StrSectorHeader* hdr;

    /* The CD path's direct hit (sector number 0) tests next unset: retail's
     * $s2, which movieEntryPoint, the only caller, holds at 2. */
    next = 2;
    best = 0;
    if (moviePlaybackType == 0) {
        setCurrentDirectory(0x18, 0);
        if (moviePlaybackNumber >= getNegativeFileSize(2)) {
            IMPLICIT_RETURN(0);
        }
        fileIndex = moviePlaybackNumber + 3;
        name = getFilenameFromPC(fileIndex);
        sectorSize = 0x800;
        readSize = 0x20;
        offset = 0;
    } else if (moviePlaybackType == 1) {
        setCurrentDirectory(0x18, 1);
        if (moviePlaybackNumber >= getNegativeFileSize(1)) {
            IMPLICIT_RETURN(0);
        }
        fileIndex = moviePlaybackNumber + 2;
        name = getFilenameFromPC(fileIndex);
        sectorSize = 0x920;
        readSize = 0x28;
        offset = 8;
    } else if (moviePlaybackType == 2) {
        return -1;
    }
    if (frame < 2) {
        return 0;
    }
    if (getCdromFnd() != 0) {
        fd = PCopen(name, 0, 0);
        PClseek(fd, 0, 2);
        PClseek(fd, 0, 0);
        PCread(fd, buf, readSize);
        hdr = (StrSectorHeader*)(buf + offset);
        nSect = hdr->m6_numSectors;
        sector = (frame - 1) * nSect - (frame - 1) / 4;
        PClseek(fd, sector * sectorSize, 0);
        n = PCread(fd, buf, readSize);
        if (hdr->m8_frameNumber == frame && n != 0) {
            if (hdr->m4_sectorNumber == 0) {
                goto close;
            }
            sector -= hdr->m4_sectorNumber;
            next = sector - 2;
            goto scan;
        }
        if (hdr->m8_frameNumber < frame && best < sector && n != 0) {
            best = sector;
        }
        sector = (frame - 1) * nSect - (frame - 1) / 4;
        sector += sector / 7;
        PClseek(fd, sector * sectorSize, 0);
        n = PCread(fd, buf, readSize);
        hdr = (StrSectorHeader*)(buf + offset);
        if (hdr->m8_frameNumber == frame && n != 0) {
            if (hdr->m4_sectorNumber == 0) {
                goto close;
            }
            sector -= hdr->m4_sectorNumber;
            next = sector - 2;
            goto scan;
        }
        if (hdr->m8_frameNumber < frame && best < sector && n != 0) {
            best = sector;
        }
        next = (nSect - 1) * (frame - 1);
        if (best > 0) {
            next = best;
        }
    scan:
        do {
            sector = next;
            PClseek(fd, next * sectorSize, 0);
            n = PCread(fd, buf, readSize);
            hdr = (StrSectorHeader*)(buf + offset);
            if (hdr->m0_magic == 0x160) {
                next = sector + (hdr->m6_numSectors - hdr->m4_sectorNumber);
            } else {
                next = sector + 1;
            }
        } while (hdr->m8_frameNumber != frame && n > 0);
        if (n == 0) {
            sector = -1;
        }
    close:
        PCclose(fd);
        return sector;
    }
    readRawSectorsFromDisk(getFileStartSector(fileIndex), buf, 0x800, 0, 0);
    waitReadCompletion(0);
    hdr = (StrSectorHeader*)buf;
    nSect = hdr->m6_numSectors;
    total = (getFileSizeAligned(fileIndex) + sectorSize - 1) / sectorSize;
    sector = (frame - 1) * nSect - (frame - 1) / 4;
    readRawSectorsFromDisk(getFileStartSector(fileIndex) + sector, buf, 0x800, 0, 0);
    waitReadCompletion(0);
    if (hdr->m8_frameNumber == frame && sector < total) {
        if (hdr->m4_sectorNumber == 0) {
            goto done;
        }
        sector -= hdr->m4_sectorNumber;
        next = sector - 2;
        goto scan2;
    }
    if (hdr->m8_frameNumber < frame && best < sector && sector < total) {
        best = sector;
    }
    sector = (frame - 1) * nSect - (frame - 1) / 4;
    sector += sector / 7;
    readRawSectorsFromDisk(getFileStartSector(fileIndex) + sector, buf, 0x800, 0, 0);
    waitReadCompletion(0);
    if (hdr->m8_frameNumber == frame && sector < total) {
        if (hdr->m4_sectorNumber == 0) {
            goto done;
        }
        sector -= hdr->m4_sectorNumber;
        next = sector - 2;
        goto scan2;
    }
    if (hdr->m8_frameNumber < frame && best < sector && sector < total) {
        best = sector;
    }
    next = (nSect - 1) * (frame - 1);
    if (best > 0) {
        next = best;
    }
scan2:
    do {
        sector = next;
        readRawSectorsFromDisk(getFileStartSector(fileIndex) + sector, buf, 0x800, 0, 0);
        waitReadCompletion(0);
        hdr = (StrSectorHeader*)buf;
        if (hdr->m0_magic == 0x160) {
            next = sector + (hdr->m6_numSectors - hdr->m4_sectorNumber);
        } else {
            next = sector + 1;
        }
    } while (hdr->m8_frameNumber != frame && next < total);
done:
    if (next >= total) {
        sector = -1;
    }
    return sector;
}

s32 getMovieLastFrameNumber(void) {
    u8 buf[0x1000];
    s32 frame;
    char* name;
    s32 fileIndex;
    s32 sectorSize;
    s32 readSize;
    s32 offset;
    s32 fd;
    s32 size;
    u16* hdr;

    frame = -1;
    if (moviePlaybackType == 0) {
        setCurrentDirectory(0x18, 0);
        if (moviePlaybackNumber >= getNegativeFileSize(2)) {
            IMPLICIT_RETURN(0);
        }
        fileIndex = moviePlaybackNumber + 3;
        name = getFilenameFromPC(fileIndex);
        sectorSize = 0x800;
        readSize = 0x20;
        offset = 0;
    } else if (moviePlaybackType == 1) {
        setCurrentDirectory(0x18, 1);
        if (moviePlaybackNumber >= getNegativeFileSize(1)) {
            IMPLICIT_RETURN(0);
        }
        fileIndex = moviePlaybackNumber + 2;
        name = getFilenameFromPC(fileIndex);
        sectorSize = 0x920;
        readSize = 0x28;
        offset = 8;
    } else if (moviePlaybackType == 2) {
        return -1;
    }
    if (getCdromFnd() != 0) {
        fd = PCopen(name, 0, 0);
        size = PClseek(fd, 0, 2);
        PClseek(fd, 0, 0);
        PClseek(fd, size - sectorSize, 0);
        PCread(fd, buf, readSize);
        hdr = (u16*)(buf + offset);
        if (hdr[0] == 0x160) {
            frame = *(s32*)(hdr + 4);
        }
        PCclose(fd);
    } else {
        size = (getFileSizeAligned(fileIndex) + sectorSize - 1) / sectorSize;
        readRawSectorsFromDisk(getFileStartSector(fileIndex) + size - 1, buf, 0x800, 0, 0);
        waitReadCompletion(0);
        if (*(u16*)buf == 0x160) {
            frame = *(s32*)(buf + 8);
        }
    }
    return frame;
}

void loadDebugSoundEffectBanks(void) {
    void* wds;

    wds = loadPcHostFile("c:\\work\\cdrom\\sound\\wave\\main_se.wd", 0);
    loadWds(wds, 0);
    while ((s16)waitForMusic(0) != 0) {
    }
    freeHeap(wds);
    wds = loadPcHostFile("c:\\work\\cdrom\\sound\\wave\\bat_se.wd", 0);
    loadWds(wds, 0);
    while ((s16)waitForMusic(0) != 0) {
    }
    freeHeap(wds);
    wds = loadPcHostFile("c:\\work\\cdrom\\sound\\wave\\gear_se.wd", 0);
    loadWds(wds, 0);
    while ((s16)waitForMusic(0) != 0) {
    }
    freeHeap(wds);
}

void loadDebugBattleMusic(void) {
    void* wds;

    wds = loadPcHostFile("c:\\work\\cdrom\\sound\\wave\\battle2.wd", 0);
    loadWds(wds, 0);
    while ((s16)waitForMusic(0) != 0) {
    }
    freeHeap(wds);
    debugBattleMusic = createMusicInstance(loadPcHostFile("c:\\work\\cdrom\\sound\\music\\battle2.smd", 0));
}


void playDebugBattleMusic(void) {
    startMusicInstance(debugBattleMusic, 0x7F, 0);
}

