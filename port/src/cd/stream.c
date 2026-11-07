/*
 * LIBCD streaming (St*, CdRead2) as video_player uses it for STR movies.
 *
 * The ring holds `size` sector slots: first a 32-byte header per slot (the
 * sector's STR header; its first halfword becomes the slot state), then 2016
 * data bytes per slot, so a frame's sectors are contiguous. The ready
 * callback files each sector of a ReadS stream into the ring; a frame is
 * handed out (StGetNext) once its last sector is in, and given back with
 * StFreeRing. A frame never wraps: when it would not fit before the end, the
 * slot is marked (state 1) and the frame starts again at slot 0.
 *
 * Slot states: 0 free, 1 wrap marker, 2 frame ready, 3 sector filled,
 * 4 frame handed out.
 */

/* The game's headers first (see libcd.c). */
#include "common.h"
#include "psyq.h"
#include "video_player/video_player.h"

#include <string.h>

#include "cd_internal.h"
#include "system/hw.h"
#include "../system/savestate.h"

s32 CdReady(s32 mode, u8* result);

#define ST_HEADER 32
#define ST_DATA 2016
#define STR_MAGIC 0x0160

typedef struct {
    /* 0x00 */ u16 m0_status; /* STR magic on arrival, then the slot state */
    /* 0x02 */ u16 m2_type;
    /* 0x04 */ u16 m4_secCount;
    /* 0x06 */ u16 m6_nSectors;
    /* 0x08 */ u32 m8_frameCount;
    /* 0x0C */ u32 mC_frameSize;
    /* 0x10 */ u16 m10_width;
    /* 0x12 */ u16 m12_height;
    /* 0x14 */ u32 m14;
    /* 0x18 */ u32 m18;
    /* 0x1C */ CdlLOC m1C_loc;
} StSlot; /* 0x20 */

_Static_assert(sizeof(StSlot) == ST_HEADER, "StSlot is a 32-byte STR header");

static StSlot* ring;
static u32 ringSize;
static int writeSlot;     /* where the next sector goes */
static int frameStart;    /* first slot of the frame being filled */
static int readSlot;      /* next frame StGetNext hands out */
static int expectSector;  /* sector number expected within the frame */
static u32 frameNumber;   /* frame being filled, 0 between frames */
static u32 startFrame;
static u32 endFrame;
static int masked;        /* skipping until startFrame comes by */
static int typeBits;
static void (*frameCallback)(void);
static void (*endCallback)(void);
static int headerless;    /* 800h-byte sectors: no CD header in front */
static CdlLOC lastLoc;
static u32 lastFrame;

static u8* slotData(int i) {
    return (u8*)(ring + ringSize) + (size_t)i * ST_DATA;
}

static void clearSlots(int start, int count) {
    for (int i = 0; i < count; i++) {
        ring[start + i].m0_status = 0;
    }
}

/* Drop the frame being filled. */
static void dropFrame(void) {
    frameNumber = 0;
    expectSector = 0;
    clearSlots(frameStart, writeSlot - frameStart);
    writeSlot = frameStart;
}

static void frameReady(void) {
    StSlot* first = &ring[frameStart];
    first->m0_status = 2;
    lastLoc = first->m1C_loc;
    lastFrame = first->m8_frameCount;
    frameStart = writeSlot;
    if (frameCallback != NULL) {
        frameCallback();
    }
}

void StCdInterrupt(void) {
    hwSafePoint();
    if (ring == NULL) {
        return;
    }
    u8 result[8];
    if (CdReady(1, result) == 5 || (result[0] & CDS_SEEKERROR)) {
        return;
    }
    StSlot* slot = &ring[writeSlot];
    if (slot->m0_status != 0) {
        return; /* the ring is full: the sector is lost */
    }
    CdlLOC loc = { 0, 0, 0, 0 };
    if (!headerless) {
        u8 skip[8];
        driveReadFifo(&loc, 4);
        driveReadFifo(skip, 8);
    }
    driveReadFifo(slot, ST_HEADER);
    slot->m1C_loc = loc;

    if (masked && startFrame != 0) {
        if (startFrame != (u16)slot->m8_frameCount) {
            slot->m0_status = 0;
            return;
        }
        masked = 0;
    }
    if (slot->m0_status != STR_MAGIC || ((slot->m2_type >> 10) & 0x1F) != typeBits) {
        slot->m0_status = 0;
        return; /* not a video sector */
    }
    if (expectSector != slot->m4_secCount || (frameNumber != 0 && frameNumber != (u16)slot->m8_frameCount)) {
        /* A sector was missed: start over with the next frame. */
        dropFrame();
        slot->m0_status = 0;
        return;
    }
    if (slot->m4_secCount == 0) {
        expectSector = 0;
        frameNumber = (u16)slot->m8_frameCount;
        if (endFrame != 0 && frameNumber >= endFrame) {
            dropFrame();
            slot->m0_status = 0;
            masked = 1;
            if (endCallback != NULL) {
                endCallback();
            }
            return;
        }
        if ((int)ringSize - writeSlot - 1 < slot->m6_nSectors) {
            if (endFrame == 0) {
                slot->m0_status = 1;
                masked = 1;
                if (endCallback != NULL) {
                    endCallback();
                }
                return;
            }
            if (ring[0].m0_status != 0) {
                slot->m0_status = 0;
                return;
            }
            /* Wrap: mark this slot and start the frame at slot 0. */
            ring[0] = *slot;
            slot->m0_status = 1;
            writeSlot = 0;
            slot = &ring[0];
        }
        frameStart = writeSlot;
    }
    expectSector++;
    driveReadFifo(slotData(writeSlot), ST_DATA);
    int last = slot->m6_nSectors - 1 == slot->m4_secCount;
    slot->m0_status = 3;
    writeSlot++;
    if (last) {
        expectSector = 0;
        frameNumber = 0;
        frameReady();
    }
}

/* The ready callback CdRead2 installs. */
static void streamReady(u8 intr, u8* result) {
    (void)intr;
    (void)result;
    StCdInterrupt();
}

void StClearRing(void) {
    hwSafePoint();
    readSlot = 0;
    frameStart = 0;
    writeSlot = 0;
    if (ring != NULL) {
        clearSlots(0, (int)ringSize);
    }
    expectSector = 0;
    frameNumber = 0;
}

void StSetRing(u32* ring_addr, u32 ring_size) {
    hwSafePoint();
    ring = (StSlot*)ring_addr;
    ringSize = ring_size;
    StClearRing();
}

void StUnSetRing(void) {
    hwSafePoint();
    CdDataCallback(NULL);
    CdReadyCallback(NULL);
}

void StSetMask(s32 mask, s32 start, s32 end) {
    masked = mask;
    startFrame = (u32)start;
    endFrame = (u32)end;
}

void StSetStream(u32 mode, u32 start_frame, u32 end_frame, void (*func1)(), void (*func2)()) {
    hwSafePoint();
    (void)mode; /* 24-bit mode: the MDEC never holds up the PC's copies */
    StSetMask(1, (s32)start_frame, (s32)end_frame);
    frameCallback = func1;
    endCallback = func2;
    typeBits = 0;
    expectSector = 0;
    frameNumber = 0;
}

u32 StGetNext(u32** addr, u32** header) {
    hwSafePoint();
    if (ring == NULL) {
        return 1;
    }
    StSlot* slot = &ring[readSlot];
    if (slot->m0_status == 1) {
        readSlot = 0;
        if (endFrame != 0) {
            slot->m0_status = 0;
        }
        slot = &ring[0];
    }
    if (slot->m0_status != 2) {
        return 1;
    }
    slot->m0_status = 4;
    *addr = (u32*)slotData(readSlot);
    *header = (u32*)slot;
    return 0;
}

u32 StFreeRing(u32* base) {
    hwSafePoint();
    if (ring == NULL) {
        return 1;
    }
    int index = (int)(((u8*)base - slotData(0)) / ST_DATA);
    StSlot* slot = &ring[index];
    if (slot->m0_status != 4) {
        return 1;
    }
    int n = slot->m6_nSectors;
    clearSlots(index, n);
    readSlot = index + n;
    return 0;
}

s32 StGetBackloc(CdlLOC* loc) {
    hwSafePoint();
    if (headerless) {
        return -1;
    }
    CdIntToPos(CdPosToInt(&lastLoc) + 1, loc);
    return (s32)lastFrame;
}

/* Setmode, then ReadS; with mode bit 8 the sectors go into the ring. */
s32 CdRead2(s32 mode) {
    hwSafePoint();
    u8 param[4] = { (u8)mode, 0, 0, 0 };
    CdControl(CDC_SETMODE, param, NULL);
    if (mode & 0x100) {
        headerless = !(mode & 0x20);
        CdDataCallback(NULL);
        CdReadyCallback(streamReady);
    }
    return CdControl(CDC_READS, NULL, NULL);
}

/* ---- a world of its own (cd.h cdWorldCreate) ---- */

/* What a world has of this file: its ring and the frame being filled. */
typedef struct {
    StSlot* ring;
    u32 ringSize;
    int writeSlot, frameStart, readSlot, expectSector;
    u32 frameNumber, startFrame, endFrame;
    int masked, typeBits;
    void (*frameCallback)(void);
    void (*endCallback)(void);
    int headerless;
    CdlLOC lastLoc;
    u32 lastFrame;
} StreamPart;

size_t streamPartSize(void) {
    return sizeof(StreamPart);
}

void streamPartInit(void* blob) {
    memset(blob, 0, sizeof(StreamPart)); /* no ring */
}

void streamPartSwap(void* blob) {
    StreamPart* p = blob;
    CD_SWAP(ring, p->ring);
    CD_SWAP(ringSize, p->ringSize);
    CD_SWAP(writeSlot, p->writeSlot);
    CD_SWAP(frameStart, p->frameStart);
    CD_SWAP(readSlot, p->readSlot);
    CD_SWAP(expectSector, p->expectSector);
    CD_SWAP(frameNumber, p->frameNumber);
    CD_SWAP(startFrame, p->startFrame);
    CD_SWAP(endFrame, p->endFrame);
    CD_SWAP(masked, p->masked);
    CD_SWAP(typeBits, p->typeBits);
    CD_SWAP(frameCallback, p->frameCallback);
    CD_SWAP(endCallback, p->endCallback);
    CD_SWAP(headerless, p->headerless);
    CD_SWAP(lastLoc, p->lastLoc);
    CD_SWAP(lastFrame, p->lastFrame);
}

/* ---- save states (system/savestate.h) ---- */

/* The ring's place in the game's heap and where the stream stands in it. */
void streamStateIo(XgState* s) {
    XG_STATE_VAR(s, ring);
    XG_STATE_VAR(s, ringSize);
    XG_STATE_VAR(s, writeSlot);
    XG_STATE_VAR(s, frameStart);
    XG_STATE_VAR(s, readSlot);
    XG_STATE_VAR(s, expectSector);
    XG_STATE_VAR(s, frameNumber);
    XG_STATE_VAR(s, startFrame);
    XG_STATE_VAR(s, endFrame);
    XG_STATE_VAR(s, masked);
    XG_STATE_VAR(s, typeBits);
    XG_STATE_VAR(s, frameCallback);
    XG_STATE_VAR(s, endCallback);
    XG_STATE_VAR(s, headerless);
    XG_STATE_VAR(s, lastLoc);
    XG_STATE_VAR(s, lastFrame);
}
