/* Streaming movie (STR) player: streams sectors from CD or the PC link, runs
 * the VLC decode, feeds the MDEC and loads decoded slices into VRAM.
 * gcc-2.6.3-psx, ASPSX 2.34, divide checks kept. */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
void regionalMovieBegin(int lba);
void regionalMovieEnd(void);
void regionalMovieFrame(int frame);
/* port/src/gpu/moviepack.h: a texture pack's pictures for the movies' frames */
void moviePackBegin(int disc, int stream, int first);
void moviePackFrame(int frame, int x0, int y0, int x1, int y1, int rgb24);
void moviePackSlice(int frame, int x0, int y0, int x1, int y1, int fx0, int fy0, int fx1, int fy1, int rgb24);
void moviePackEnd(void);
s32 getCurrentDiscNumber(void);
#include "video_player/video_player.h"

void setCdVolume(s32 vol, s32 time);
s32 clearSectorBuffer(void);
void setCdMode(s32 mode);
void stopCdRead(s32 arg0);
s32* allocateBufferForVramUpload(s32 count, s32 flags);
s32 getCdromFnd(void);

extern s32 pcStreamFd;

extern StrDecEnv strDecEnv;
extern s8 strPlayerState;
extern u8 strFromPcHost;
extern void* strRingBuffer;

extern s32 strSingleSliceMode;
extern s32 strColorMode;
extern u8 strFrameUploaded;
extern s16 strMaxSliceHeight;
extern s32 strDecodingFrame;
extern s32 strShownFrame;
extern void (*strFrameCallback)(u16 frame, u16 x, u16 y);
extern s32 strFrameUploadEnabled;
extern u16 strSliceColumnWidth;
extern s32 stDeferredInterrupt;
extern s32 strNextUploadEnabled;

/* DecDCTout callback: sends the decoded slice to VRAM and starts the next. */
void uploadStrSliceToVram(void) {
    RECT rect;
    u16 h;
    s32 i;
    s32 off;
    s16 lim;

    if (strFromPcHost == 0 && (strColorMode & 1) && stDeferredInterrupt != 0) {
        StCdInterrupt();
        stDeferredInterrupt = 0;
    }
    h = strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h;
    if (strMaxSliceHeight >= 0 && strMaxSliceHeight < (s16)h) {
        strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h = strMaxSliceHeight;
    }
    if (strSingleSliceMode != 0) {
        rect.y = strDecEnv.m30_slice[strDecEnv.m2C_sliceId].y;
        rect.w = strSliceColumnWidth;
        rect.h = strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h;
        for (i = 0; i < strDecEnv.m30_slice[strDecEnv.m2C_sliceId].w / (s16)strSliceColumnWidth; i++) {
            rect.x = strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x;
            strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x += strSliceColumnWidth;
            if (strFrameUploadEnabled != 0) {
                LoadImage(&rect, strDecEnv.m10_imgBuf[strDecEnv.mC_imgId] + i * (s16)h * (s16)strSliceColumnWidth);
                moviePackSlice(strDecodingFrame, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h,
                               strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].x, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].y,
                               strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].w, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].h,
                               strColorMode & 1);
            }
        }
    } else {
        if (strFrameUploadEnabled != 0) {
            LoadImage(&strDecEnv.m30_slice[strDecEnv.m2C_sliceId], strDecEnv.m10_imgBuf[strDecEnv.mC_imgId]);
            moviePackSlice(strDecodingFrame, strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x,
                           strDecEnv.m30_slice[strDecEnv.m2C_sliceId].y,
                           strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x + strDecEnv.m30_slice[strDecEnv.m2C_sliceId].w,
                           strDecEnv.m30_slice[strDecEnv.m2C_sliceId].y + strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h,
                           strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].x, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].y,
                           strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].w, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].h,
                           strColorMode & 1);
        }
        strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x += strDecEnv.m30_slice[strDecEnv.m2C_sliceId].w;
    }
    strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h = h;
    strDecEnv.mC_imgId = 1 - strDecEnv.mC_imgId;
    if (strDecEnv.m30_slice[strDecEnv.m2C_sliceId].x >= strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].w) {
        regionalMovieFrame(strDecodingFrame);
        moviePackFrame(strDecodingFrame, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].x,
                       strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].y, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].w,
                       strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].h, strColorMode & 1);
        if (strFrameCallback != NULL) {
            if (strColorMode & 1) {
                strFrameCallback(strDecodingFrame, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].x * 2 / 3,
                           strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].y);
            } else {
                strFrameCallback(strDecodingFrame, strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].x,
                           strDecEnv.m1C_rect[strDecEnv.m2C_sliceId].y);
            }
        }
        strFrameUploaded = 1;
        strFrameUploadEnabled = strNextUploadEnabled;
        strShownFrame = strDecodingFrame;
        strDecEnv.m2C_sliceId = 1 - strDecEnv.m2C_sliceId;
    } else {
        DecDCTout(strDecEnv.m10_imgBuf[strDecEnv.mC_imgId],
                  strDecEnv.m30_slice[strDecEnv.m2C_sliceId].w * strDecEnv.m30_slice[strDecEnv.m2C_sliceId].h / 2);
    }
}

extern u16 strBufferWidth;
extern u16 strBufferHeight;
extern s32 strVlcSize;

s32 initStrPlayer(u16 width, u16 height, u16 depth, u16 sliceWidth, u16 ringSize, u16 vlcMode, u16 mode) {
    s32 size;
    s32 tmp;

    if (getCdromFnd() != 0) {
        strFromPcHost = 1;
    } else {
        strFromPcHost = 0;
    }
    strPlayerState = 0;
    if (strSingleSliceMode != 0) {
        sliceWidth = width;
    }
    DecDCTReset(0);
    tmp = depth * 2;
    size = width * height * tmp;
    strColorMode = mode & 3;
    strVlcSize = vlcMode;
    strDecEnv.m4_vlcBuf[0] = allocateHeap(PC_HEAP_BYTES(size / 256), 0);
    strDecEnv.m4_vlcBuf[1] = allocateHeap(PC_HEAP_BYTES(size / 256), 0);
    if (strColorMode & 1) {
        sliceWidth = sliceWidth * 3 / 2;
        width = width * 3 / 2;
    }
    strBufferWidth = width;
    strBufferHeight = height;
    strDecEnv.m10_imgBuf[0] = allocateHeap(PC_HEAP_BYTES(sliceWidth * height * 2), 0);
    strDecEnv.m10_imgBuf[1] = allocateHeap(PC_HEAP_BYTES(sliceWidth * height * 2), 0);
    strDecEnv.m1C_rect[0].x = 0;
    strDecEnv.m1C_rect[0].y = 0;
    strDecEnv.m1C_rect[0].w = width;
    strDecEnv.m1C_rect[0].h = height;
    strDecEnv.m1C_rect[1].x = 0;
    strDecEnv.m1C_rect[1].y = 0;
    strDecEnv.m1C_rect[1].w = width;
    strDecEnv.m1C_rect[1].h = height;
    strDecEnv.m30_slice[0].x = 0;
    strDecEnv.m30_slice[0].y = 0;
    strDecEnv.m30_slice[0].w = sliceWidth;
    strDecEnv.m30_slice[0].h = height;
    strDecEnv.m30_slice[1].x = 0;
    strDecEnv.m30_slice[1].y = 0;
    strDecEnv.m30_slice[1].w = sliceWidth;
    strDecEnv.m30_slice[1].h = height;
    if (strFromPcHost != 0) {
        strRingBuffer = allocateBufferForVramUpload(ringSize, 0);
    } else {
        strRingBuffer = allocateHeap(PC_HEAP_BYTES(ringSize << 11), 0);
        StSetRing((u32*)strRingBuffer, ringSize);
    }
    if (strRingBuffer == 0) {
        return -1;
    }
    strPlayerState = 1;
    return 0;
}

extern s16 streamFrameNumber;
extern s16 movieRequestEndFrame;
extern u16 strFrameWidth;
extern u16 strFrameHeight;
extern s32 strVlcRemaining;
extern s32 strEndFrame;
extern u8 strFrameMissing;
extern s8 D_801E8960;
extern s32 strStartOffset;
extern s32 strReadMode;
extern u16 strFileIndex;
extern u16 strChannel;
extern s32 strStartFrame;
extern s32 strDirectory;
extern s32 strDirectoryOffset;
extern s32 strSkippedFrames;
extern s32 strStallCount;
void startStrStreamRead(s32 fileIndex, s32 offset, s32 count, s32 mode, CdlLOC* loc);

void startStrPlayback(s32 fileIndex, s32 offset, u16 startFrame, u16 endFrame, u16 channel, s32 flags, u16 audio,
                   u16 x0, u16 y0, u16 x1, u16 y1, u16 maxHeight, void (*callback)(u16, u16, u16)) {
    u8 unused[8];
    u8 param[8];

    if (strPlayerState != 0) {
        getCurrentDirectory(&strDirectory, &strDirectoryOffset);
        DecDCToutCallback(uploadStrSliceToVram);
        strPlayerState = (audio != 0) ? 2 : 1;
        D_801E8960 = 0;
        strMaxSliceHeight = maxHeight;
        strFileIndex = fileIndex;
        strStartOffset = offset;
        if (strFromPcHost != 0) {
            if (flags & 1) {
                strChannel = channel;
                strReadMode = 0x248;
            } else {
                strChannel = 1;
                strReadMode = 0x200;
            }
            clearSectorBuffer();
            streamFrameNumber = 0;
            movieRequestEndFrame = 0;
        } else {
            if (flags & 1) {
                strChannel = channel;
                strReadMode = 0x148;
                param[0] = 1;
                param[1] = strChannel;
                while (CdControlB(0xD, param, 0) == 0) {
                }
            } else {
                strReadMode = 0x100;
            }
            StSetStream(strColorMode & 1, startFrame, -1, 0, 0);
        }
        if (flags & 2) {
            strReadMode &= ~0x40;
        }
        strFrameCallback = callback;
        if (strColorMode & 1) {
            strDecEnv.m1C_rect[0].x = x0 * 3 / 2;
            strDecEnv.m1C_rect[0].y = y0;
            strDecEnv.m1C_rect[1].x = x1 * 3 / 2;
            strDecEnv.m1C_rect[1].y = y1;
            strSliceColumnWidth = 24;
        } else {
            strDecEnv.m1C_rect[0].x = x0;
            strDecEnv.m1C_rect[0].y = y0;
            strDecEnv.m1C_rect[1].x = x1;
            strDecEnv.m1C_rect[1].y = y1;
            strSliceColumnWidth = 16;
        }
        strFrameUploaded = 1;
        strFrameMissing = 1;
        strShownFrame = -1;
        strNextUploadEnabled = 1;
        strFrameUploadEnabled = 1;
        strStartFrame = startFrame;
        strDecEnv.m0_vlcId = 0;
        strDecEnv.mC_imgId = 0;
        strDecEnv.m18_rectId = 0;
        strDecEnv.m2C_sliceId = 0;
        strFrameWidth = 0;
        strFrameHeight = 0;
        strStallCount = 0;
        strVlcRemaining = 0;
        strSkippedFrames = 0;
        strEndFrame = endFrame;
        startStrStreamRead(fileIndex, offset, strChannel, strReadMode, 0);
    }
}

extern s32 strFetchedFrame;
extern s32 strPreviousFetchedFrame;
s32 getNextStreamedPacket(u8** outData, StrSectorHeader** outSector);

u32* getNextStrFrame(u32 endFrame, StrSectorHeader** outHeader) {
    u32* addr;
    StrSectorHeader* hdr;

    if (strFromPcHost != 0) {
        if (getNextStreamedPacket((u8**)&addr, &hdr) != 0) {
            return NULL;
        }
        strFetchedFrame = hdr->m8_frameNumber;
        if (strFetchedFrame >= endFrame) {
            stopCdRead(0);
        }
    } else {
        if (StGetNext(&addr, (u32**)&hdr) != 0) {
            strStallCount++;
            return NULL;
        }
        strStallCount = 0;
        strPreviousFetchedFrame = strFetchedFrame;
        strFetchedFrame = hdr->m8_frameNumber;
        if (strPreviousFetchedFrame + 1 < strFetchedFrame) {
            strSkippedFrames++;
        }
    }
    if (strFrameWidth != hdr->m10_width || strFrameHeight != hdr->m12_height) {
        strFrameWidth = hdr->m10_width;
        strFrameHeight = hdr->m12_height;
        if (strColorMode & 1) {
            strDecEnv.m1C_rect[0].w = strDecEnv.m1C_rect[0].x + (strFrameWidth * 3 >> 1);
            strDecEnv.m1C_rect[1].w = strDecEnv.m1C_rect[1].x + (strFrameWidth * 3 >> 1);
        } else {
            strDecEnv.m1C_rect[0].w = strDecEnv.m1C_rect[0].x + strFrameWidth;
            strDecEnv.m1C_rect[1].w = strDecEnv.m1C_rect[1].x + strFrameWidth;
        }
        strDecEnv.m1C_rect[0].h = strDecEnv.m1C_rect[0].y + strFrameHeight;
        strDecEnv.m1C_rect[1].h = strDecEnv.m1C_rect[1].y + strFrameHeight;
        if (strFrameHeight > strBufferHeight) {
            strDecEnv.m30_slice[0].h = strBufferHeight;
            strDecEnv.m30_slice[1].h = strBufferHeight;
        } else {
            strDecEnv.m30_slice[0].h = strFrameHeight;
            strDecEnv.m30_slice[1].h = strFrameHeight;
        }
    }
    *outHeader = hdr;
    return addr;
}

extern StrSectorHeader* strFrameHeader;
extern u32* strFrameData;
s32 releaseStreamedPacket(StrSectorHeader* sector);

void decodeNextStrFrame(void) {
    if (strVlcRemaining == 0) {
        if (strFrameMissing == 0) {
            strDecEnv.m30_slice[strDecEnv.m18_rectId].x = strDecEnv.m1C_rect[strDecEnv.m18_rectId].x;
            strDecEnv.m30_slice[strDecEnv.m18_rectId].y = strDecEnv.m1C_rect[strDecEnv.m18_rectId].y;
            strDecodingFrame = strFetchedFrame;
            DecDCTin(strDecEnv.m4_vlcBuf[strDecEnv.m0_vlcId], strColorMode);
            DecDCTout(strDecEnv.m10_imgBuf[strDecEnv.mC_imgId],
                      strDecEnv.m30_slice[strDecEnv.m18_rectId].w * strDecEnv.m30_slice[strDecEnv.m18_rectId].h / 2);
            strFrameUploaded = 0;
            strDecEnv.m18_rectId = 1 - strDecEnv.m18_rectId;
            strDecEnv.m0_vlcId = 1 - strDecEnv.m0_vlcId;
        }
        strFrameData = getNextStrFrame(strEndFrame, &strFrameHeader);
        if (strFrameData == NULL) {
            strFrameMissing = 1;
            return;
        }
        strFrameMissing = 0;
        DecDCTvlcSize(strVlcSize);
        strVlcRemaining = DecDCTvlc(strFrameData, strDecEnv.m4_vlcBuf[strDecEnv.m0_vlcId]);
    } else {
        strVlcRemaining = DecDCTvlc(NULL, NULL);
    }
    if (strVlcRemaining == 0) {
        if (strFromPcHost != 0) {
            releaseStreamedPacket(strFrameHeader);
        } else {
            StFreeRing(strFrameData);
        }
    }
}

extern s32 cdStopRecoveryOkCount;
extern s32 cdStopRecoveryErrorCount;
extern s32 cdErrorCount;
extern s32 strFadeInPending;
extern s32 strFadeOutPending;
void stopStrPlayback(void);

void updateStrPlayer(void) {
    CdlLOC loc;
    s32 n;
    CdlLOC* p;

    if (strPlayerState > 0) {
        if (strShownFrame > strStartFrame && strFadeInPending != 0) {
            strFadeInPending = 0;
            setCdVolume(0x7FFF, 40);
        }
        if (strShownFrame >= strEndFrame - 3 && strFadeOutPending != 0) {
            strFadeOutPending = 0;
            setCdVolume(0, 40);
        }
        if (strShownFrame >= strEndFrame) {
            if (strPlayerState == 1) {
                stopStrPlayback();
            } else {
                D_801E8960 = 0;
                strShownFrame = -1;
                startStrStreamRead(strFileIndex, strStartOffset, strChannel, strReadMode, NULL);
            }
        }
        if (strFrameUploaded != 0 || strFrameMissing != 0 || strVlcRemaining != 0) {
            decodeNextStrFrame();
        }
        if (strStallCount > 0x870) {
            strStallCount = 0;
            n = StGetBackloc(&loc);
            streamFrameNumber = n;
            cdErrorCount++;
            cdStopRecoveryOkCount = CdPosToInt(&loc);
            cdStopRecoveryErrorCount = strStartOffset;
            if (n > strEndFrame || n <= 0) {
                p = NULL;
            } else {
                p = &loc;
            }
            strShownFrame = -1;
            startStrStreamRead(strFileIndex, strStartOffset, strChannel, strReadMode, p);
        }
    }
}


void startStrStreamRead(s32 fileIndex, s32 offset, s32 count, s32 mode, CdlLOC* loc) {
    s32 dir;
    s32 dirOffset;
    CdlLOC pos;
    CdlLOC* p;

    setCdVolume(0, 0);
    stopCdRead(0);
    waitReadCompletion(0);
    getCurrentDirectory(&dir, &dirOffset);
    setCurrentDirectory(strDirectory, strDirectoryOffset);
    strFadeInPending = 1;
    strFadeOutPending = 1;
    if (strFromPcHost) {
        readFile(fileIndex, strRingBuffer, count, mode);
        if (mode & 8) {
            PClseek(pcStreamFd, offset * 0x920, 0);
        } else {
            PClseek(pcStreamFd, offset * 0x800, 0);
        }
    } else {
        mode |= 0x80;
        regionalMovieBegin(getFileStartSector(fileIndex) + offset);
        moviePackBegin(getCurrentDiscNumber(), fileIndex - 2, strStartFrame); /* stream n is file n + 2 */
        CdIntToPos(getFileStartSector(fileIndex) + offset, &pos);
        if (loc != NULL) {
            p = loc;
        } else {
            p = &pos;
        }
        while (CdControlB(2, (u8*)p, 0) == 0) {
        }
        while (CdRead2(mode) == 0) {
        }
    }
    setCurrentDirectory(dir, dirOffset);
}

void stopStrPlayback(void) {
    regionalMovieEnd();
    moviePackEnd();
    setCdVolume(0, 0);
    stopCdRead(0);
    DecDCToutCallback(0);
    DecDCTReset(0);
    strPlayerState = -1;
    if (strFromPcHost) {
        clearSectorBuffer();
    } else {
        StUnSetRing();
        while (CdControlB(9, 0, 0) == 0) {
        }
        setCdMode(0xA0);
    }
    waitReadCompletion(0);
}


void freeStrPlayer(void) {
    stopStrPlayback();
    freeHeap(strDecEnv.m4_vlcBuf[0]);
    freeHeap(strDecEnv.m4_vlcBuf[1]);
    freeHeap(strDecEnv.m10_imgBuf[0]);
    freeHeap(strDecEnv.m10_imgBuf[1]);
    freeHeap(strRingBuffer);
    strDecEnv.m4_vlcBuf[0] = 0;
    strDecEnv.m4_vlcBuf[1] = 0;
    strDecEnv.m10_imgBuf[0] = 0;
    strDecEnv.m10_imgBuf[1] = 0;
    strRingBuffer = 0;
}
