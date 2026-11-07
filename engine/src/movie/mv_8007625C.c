/* Movie playback: play by script or debug request, the playback loop driving
 * the video player overlay, skip input, GTE setup. gcc-2.6.3-psx, ASPSX 2.34,
 * divide checks kept. */
#include "common.h"
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
void moviePlaybackPollSkipInput(void);
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


void playMovieFromDebugMenu(void) {
    SVECTOR unused[3];
    RECT rect = {0, 0, 0x280, 0x200};

    movieSkipDisabled = 0;
    moviePadButtons = -1;
    if (moviePlaybackType == 0) {
        setCurrentDirectory(0x18, 0);
        if (moviePlaybackNumber < getNegativeFileSize(2)) {
            SetDispMask(0);
            goto play;
        }
    } else if (moviePlaybackType == 1) {
        setCurrentDirectory(0x18, 1);
        if (moviePlaybackNumber < getNegativeFileSize(1)) {
            SetDispMask(0);
            goto play;
        }
    } else if (moviePlaybackType != 2) {
    play:
        movieDisplayBuffers[0].m0_drawEnv.isbg = 0;
        movieDisplayBuffers[1].m0_drawEnv.isbg = 0;
        if (movieRgb24 != 0) {
            movieDisplayBuffers[0].m5C_dispEnv.isrgb24 = 1;
            movieDisplayBuffers[1].m5C_dispEnv.isrgb24 = 1;
        }
        moviePlaybackLoop();
        VSync(0);
        ClearImage(&rect, 0, 0, 0);
        DrawSync(0);
        VSync(0);
        movieDisplayBuffers[0].m0_drawEnv.isbg = 1;
        movieDisplayBuffers[1].m0_drawEnv.isbg = 1;
        if (movieRgb24 != 0) {
            movieDisplayBuffers[0].m5C_dispEnv.isrgb24 = 0;
            movieDisplayBuffers[1].m5C_dispEnv.isrgb24 = 0;
        }
    }
}

s32 movieStartPlaybackFromScript(u8 arg0) {
    SVECTOR unused[3];

    movieSkipDisabled = arg0;
    moviePadButtons = -1;
    if (moviePlaybackType == 0) {
        setCurrentDirectory(0x18, 0);
        if (moviePlaybackNumber >= getNegativeFileSize(2)) {
            IMPLICIT_RETURN(0);
        }
    } else if (moviePlaybackType == 1) {
        setCurrentDirectory(0x18, 1);
        if (moviePlaybackNumber >= getNegativeFileSize(1)) {
            IMPLICIT_RETURN(0);
        }
    } else if (moviePlaybackType == 2) {
        IMPLICIT_RETURN(0);
    }
    movieDisplayBuffers[0].m0_drawEnv.isbg = 0;
    movieDisplayBuffers[1].m0_drawEnv.isbg = 0;
    movieDisplayBuffers[0].m5C_dispEnv.isrgb24 = 1;
    movieDisplayBuffers[1].m5C_dispEnv.isrgb24 = 1;
    return moviePlaybackLoop();
}

s32 moviePlaybackLoop(void) {
    u8 unused[0x90];
    RECT clearRect = {0, 0, 0x280, 0x200};
    u8 unused2[0x200];
    RECT moveRect;
    s32 fileIndex;
    s32 audio;
    s32 budget;
    s32 i;
    s32 t0;
    s32 t1;

    movieStopCountdown = 0;
    D_80077020 = 0;
    if (moviePlaybackType == 0) {
        audio = 0;
        fileIndex = moviePlaybackNumber + 3;
    } else {
        audio = 1;
        fileIndex = moviePlaybackNumber + 2;
    }
    if (getFileSize(fileIndex) == 0x18) {
        SetDispMask(1);
        movieType.m2_returnMode = 0;
        IMPLICIT_RETURN(0);
    }
    VSync(0);
    ClearImage(&clearRect, 0, 0, 0);
    DrawSync(0);
    VSync(0);
    movieDrawBufferIndex = 0;
    movieDisplayBufferIndex = 0;
    freeStrPlayer();
    if (movieScreenDrawLines > 0) {
        movieImageY = (0xF0 - movieScreenDrawLines) / 2;
    } else {
        movieImageY = 0;
    }
    initStrPlayer(0x140, 0xF0, 0x80, 0x10, 0x20, 0x800, movieRgb24);
    strSingleSliceMode = 0;
    if (movieRewind != 0) {
        startStrPlayback(fileIndex, movieStartSector, movieStartFrame, movieEndFrame, movieChannel, audio, 1, 0, movieImageY, 0,
                      movieImageY + 0xF0, movieScreenDrawLines, handleMovieFrame);
    } else {
        startStrPlayback(fileIndex, movieStartSector, movieStartFrame, movieEndFrame, movieChannel, audio, 0, 0, movieImageY, 0,
                      movieImageY + 0xF0, movieScreenDrawLines, handleMovieFrame);
    }
    budget = 30;
    PutDrawEnv(&movieDisplayBuffers[movieDrawBufferIndex].m0_drawEnv);
    PutDispEnv(&movieDisplayBuffers[movieDrawBufferIndex].m5C_dispEnv);
    SetDispMask(1);
    while (1) {
        if (D_80077020 == 0) {
            for (i = 0; i < budget / 10; i++) {
                t0 = VSync(1);
                updateStrPlayer();
                t1 = VSync(1);
                if (i * 2 + 1 < 32) {
                    movieUpdateTimes[i * 2] = t0;
                    movieUpdateTimes[i * 2 + 1] = t1;
                }
            }
            budget = budget % 10;
        }
        budget += 30;
        moviePlaybackPollSkipInput();
        checkSoftReboot();
        VSync(0);
        PutDispEnv(&movieDisplayBuffers[movieDisplayBufferIndex].m5C_dispEnv);
        movieDisplayBufferIndex = movieDrawBufferIndex;
        if (movieStopCountdown == 1) {
            break;
        }
        if (movieStopCountdown >= 2) {
            movieStopCountdown--;
        }
    }
    stopStrPlayback();
    if (movieDisplayBufferIndex == 0) {
        DrawSync(0);
        VSync(0);
        moveRect.x = 0;
        moveRect.y = 0xF0;
        moveRect.w = 0x1E0;
        moveRect.h = 0xF0;
        MoveImage(&moveRect, 0, 0);
        DrawSync(0);
        VSync(0);
        PutDispEnv(&movieDisplayBuffers[1].m5C_dispEnv);
    }
    return 0;
}

void portMovieSweepFrame(s32 frame);

void handleMovieFrame(u16 frame, s32 arg1, u16 arg2) {
    movieCurrentFrame = frame;
    portMovieSweepFrame(frame); /* XG_SWEEP_LOG (port/src/game/movie/sweep.c) */
    if (strSingleSliceMode == 1) {
        if (arg2 != movieImageY) {
            movieDrawBufferIndex = 0;
        } else {
            movieDrawBufferIndex = 1;
        }
    } else if (arg2 != movieImageY) {
        movieDrawBufferIndex = 0;
        movieDisplayBufferIndex = 0;
    } else {
        movieDrawBufferIndex = 1;
        movieDisplayBufferIndex = 1;
    }
    if (frame >= movieEndFrame && movieRewind == 0) {
        movieStopCountdown = 1;
    }
}

void moviePlaybackPollSkipInput(void) {
    moviePadPreviousButtons = moviePadButtons;
    moviePadButtons = readPadButtons(0);
    if (moviePadPreviousButtons == moviePadButtons && moviePadPreviousButtons != 0) {
        if (++moviePadRepeatTimer > moviePadRepeatDelay) {
            moviePadPreviousButtons = 0;
            moviePadRepeatDelay = 1;
            moviePadRepeatTimer = 0;
        }
    } else {
        moviePadRepeatDelay = 0x5A;
        moviePadRepeatTimer = 0;
    }
#if VERSION_JP
    if (!(moviePadPreviousButtons & 0x20) && (moviePadButtons & 0x20) && movieSkipDisabled == 0) { /* circle */
#else
    if (!(moviePadPreviousButtons & 0x40) && (moviePadButtons & 0x40) && movieSkipDisabled == 0) { /* cross */
#endif
        setCdVolume(0, 0xA);
        movieStopCountdown = 5;
    }
    if (!(moviePadPreviousButtons & 0x800) && (moviePadButtons & 0x800) && movieSkipDisabled == 0) {
        setCdVolume(0, 0xA);
        movieStopCountdown = 5;
    }
}

void setupMovieGte(void) {
    SetGeomScreen(0x200);
    SetGeomOffset(0xA0, 0x78);
    movieTranslationMatrix.m[0][0] = 0x1000;
    movieTranslationMatrix.m[1][1] = 0x1000;
    movieTranslationMatrix.m[2][2] = 0x1000;
    D_800770F8.m[0][0] = 0x1000;
    D_800770F8.m[1][1] = 0x1000;
    D_800770F8.m[2][2] = 0x1000;
    movieLightMatrix.m[0][0] = 0x93D;
    movieLightMatrix.m[0][1] = -0x93D;
    movieLightMatrix.m[0][2] = -0x93D;
    movieTranslationMatrix.m[0][1] = 0;
    movieTranslationMatrix.m[0][2] = 0;
    movieTranslationMatrix.m[1][0] = 0;
    movieTranslationMatrix.m[1][2] = 0;
    movieTranslationMatrix.m[2][0] = 0;
    movieTranslationMatrix.m[2][1] = 0;
    D_800770F8.m[0][1] = 0;
    D_800770F8.m[0][2] = 0;
    D_800770F8.m[1][0] = 0;
    D_800770F8.m[1][2] = 0;
    D_800770F8.m[2][0] = 0;
    D_800770F8.m[2][1] = 0;
    movieLightMatrix.m[1][0] = 0;
    movieLightMatrix.m[1][1] = 0;
    movieLightMatrix.m[1][2] = 0;
    movieLightMatrix.m[2][0] = 0;
    movieLightMatrix.m[2][1] = 0;
    movieLightMatrix.m[2][2] = 0;
    movieColorMatrix.m[0][0] = 0x969;
    movieColorMatrix.m[0][1] = 0;
    movieColorMatrix.m[0][2] = 0;
    movieColorMatrix.m[1][0] = 0x969;
    movieColorMatrix.m[1][1] = 0;
    movieColorMatrix.m[1][2] = 0;
    movieColorMatrix.m[2][0] = 0x969;
    movieColorMatrix.m[2][1] = 0;
    movieColorMatrix.m[2][2] = 0;
    SetColorMatrix(&movieColorMatrix);
    SetBackColor(0x60, 0x60, 0x60);
}

void applyMovieLookAtMatrix(void) {
    SVECTOR unused[3];

    computeMovieLookAtMatrix();
    SetRotMatrix(&movieViewMatrix);
    SetTransMatrix(&movieViewMatrix);
}

void computeMovieLookAtMatrix(void) {
    s32 dx;
    s32 dy;
    s32 dz;
    s32 xx;
    s32 zz;

    dx = movieReferencePoint.vx - movieViewPoint.vx;
    dy = movieReferencePoint.vy - movieViewPoint.vy;
    dz = movieReferencePoint.vz - movieViewPoint.vz;
    zz = dz * dz;
    xx = dx * dx;
    movieViewAngles.vx = ratan2(dy, SquareRoot0(zz + xx));
    movieViewAngles.vy = -ratan2(dx, dz);
    createRotationMatrix(&movieViewAngles, &movieLookAtMatrix);
    RotMatrixZ(movieViewRoll, &movieLookAtMatrix);
    MulMatrix2(&D_800770F8, &movieLookAtMatrix);
    movieLookAtMatrix.t[0] = 0;
    movieLookAtMatrix.t[1] = 0;
    movieLookAtMatrix.t[2] = SquareRoot0(xx + dy * dy + zz);
    movieTranslationMatrix.t[0] = D_80076F2C.vx - movieReferencePoint.vx;
    movieTranslationMatrix.t[1] = D_80076F2C.vy - movieReferencePoint.vy;
    movieTranslationMatrix.t[2] = D_80076F2C.vz - movieReferencePoint.vz;
    CompMatrix(&movieLookAtMatrix, &movieTranslationMatrix, &movieViewMatrix);
}


