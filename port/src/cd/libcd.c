/*
 * LIBCD as the game uses it, on top of the drive (drive.c).
 *
 * The behaviour follows the library the retail game links (its C in
 * src/main/psyq/libcd): each command waits for the previous one to complete;
 * CdControl waits for the first response, CdControlB for completion and
 * CdControlF for nothing. A command completes with its acknowledge (INT3),
 * or with INT2 for the commands that have a second response. The sync
 * callback gets CdlComplete/CdlDiskError when a command completes, the ready
 * callback CdlDataReady per sector (and CdlDataEnd, CdlDiskError). CdGetSector
 * reads the current sector's data FIFO; the data callback (DMA channel 3)
 * runs once after a ready callback that read data, as the DMA interrupt does.
 */

/* The game's headers first: they declare libc functions that the host's
 * headers may define as macros. */
#include "common.h"
#include "psyq.h"

#include <stdio.h>
#include <string.h>

#include "cd_internal.h"
#include "system/hw.h"
#include "../system/savestate.h"

/* libcd's CdlATV: CD audio to SPU mix (L->L, L->R, R->R, R->L). */
typedef struct {
    u8 val0;
    u8 val1;
    u8 val2;
    u8 val3;
} CdAudioMix;

/* CdlIntrResult values. */
enum { CDL_NOINTR = 0, CDL_DATAREADY = 1, CDL_COMPLETE = 2, CDL_ACKNOWLEDGE = 3, CDL_DATAEND = 4, CDL_DISKERROR = 5 };

/* libcd's waits give up after 960 vertical blanks. */
#define CD_TIMEOUT 16.0

/* Per command, from the library's tables: the acknowledge does not complete
 * it (a second response follows); it clears the ready state; its
 * acknowledge carries the status; its parameter bytes; and whether a
 * position parameter is sent with Setloc first. */
static const uint8_t ackOnly[32] = {
    [CDC_MOTORON] = 1, [CDC_STOP] = 1, [CDC_PAUSE] = 1, [CDC_INIT] = 1,
    [CDC_SETSESSION] = 1, [CDC_SEEKL] = 1, [CDC_SEEKP] = 1, [CDC_GETID] = 1,
};
static const uint8_t clearsReady[32] = {
    [CDC_PLAY] = 1, [CDC_FORWARD] = 1, [CDC_BACKWARD] = 1, [CDC_READN] = 1,
    [CDC_SEEKL] = 1, [CDC_SEEKP] = 1, [CDC_READS] = 1,
};
static const uint8_t paramCount[32] = {
    [CDC_SETLOC] = 3, [CDC_SETFILTER] = 2, [CDC_SETMODE] = 1, [CDC_SETSESSION] = 1, [CDC_GETTD] = 1,
};
static const uint8_t setlocFirst[32] = {
    [CDC_PLAY] = 1, [CDC_READN] = 1, [CDC_SEEKL] = 1, [CDC_SEEKP] = 1, [CDC_READS] = 1,
};

static const char* const comNames[32] = {
    "CdlSync", "CdlNop", "CdlSetloc", "CdlPlay", "CdlForward", "CdlBackward", "CdlReadN", "CdlStandby",
    "CdlStop", "CdlPause", "CdlReset", "CdlMute", "CdlDemute", "CdlSetfilter", "CdlSetmode", "?",
    "CdlGetlocL", "CdlGetlocP", "?", "CdlGetTN", "CdlGetTD", "CdlSeekL", "CdlSeekP", "?",
    "?", "?", "?", "CdlReadS", "?", "?", "?", "?",
};
static const char* const intrNames[8] = {
    "NoIntr", "DataReady", "Complete", "Acknowledge", "DataEnd", "DiskError", "?", "?",
};

static CdlCB cbSync;
static CdlCB cbReady;
static CdlCB cbRead;
static void (*cbData)(void);
static s32 debugLevel;
static uint8_t status;   /* last stat byte */
static uint8_t status1;  /* the byte after it (error code) */
static CdlLOC lastPos = { 2, 0, 0, 0 };
static uint8_t lastMode;
static uint8_t lastCom;

/* Last interrupt per kind: sync (command), ready (data), data end. */
static volatile uint8_t syncIntr = CDL_COMPLETE;
static volatile uint8_t readyIntr;
static volatile uint8_t dataEndIntr;
static uint8_t syncResult[8];
static uint8_t readyResult[8];
static uint8_t dataEndResult[8];

static int irqDepth;    /* inside cdInterrupt (the CD interrupt handler) */
static int dmaPending;  /* CdGetSector ran in the handler: the DMA interrupt follows */
static uint8_t mix[4] = { 0x80, 0, 0x80, 0 };
static int inWorld;     /* a world of its own (cd.h): the slowdown replay does not see it */

/* ---- the interrupt handler ---- */

void cdInterrupt(int type, const uint8_t* response, int count) {
    uint8_t result[8] = { 0 };
    memcpy(result, response, (size_t)(count < 8 ? count : 8));
    irqDepth++;

    int err = 0;
    if (type != CDI_ACKNOWLEDGE || (lastCom != CDC_GETLOCL && lastCom != CDC_GETLOCP)) {
        status = result[0];
        status1 = result[1];
        err = status & (CDS_ERROR | CDS_SEEKERROR | CDS_IDERROR | CDS_SHELLOPEN);
    }
    if (type == CDI_DISKERROR && debugLevel > 0) {
        fprintf(stderr, "DiskError: com=%s,code=(%02x:%02x)\n", comNames[lastCom & 31], status, status1);
    }
    int ready = 0;
    int sync = 0;
    switch (type) {
    case CDI_ACKNOWLEDGE:
        memcpy(syncResult, result, 8);
        if (err) {
            syncIntr = CDL_DISKERROR;
            sync = 1;
        } else if (ackOnly[lastCom & 31]) {
            syncIntr = CDL_ACKNOWLEDGE;
        } else {
            syncIntr = CDL_COMPLETE;
            sync = 1;
        }
        break;
    case CDI_COMPLETE:
        syncIntr = err ? CDL_DISKERROR : CDL_COMPLETE;
        memcpy(syncResult, result, 8);
        sync = 1;
        break;
    case CDI_DATAREADY:
        /* A one-byte response never reports an error. */
        if (err && count == 1) {
            err = 0;
        }
        readyIntr = err ? CDL_DISKERROR : CDL_DATAREADY;
        memcpy(readyResult, result, 8);
        ready = 1;
        break;
    case CDI_DATAEND:
        readyIntr = dataEndIntr = CDL_DATAEND;
        memcpy(dataEndResult, result, 8);
        memcpy(readyResult, result, 8);
        ready = 1;
        break;
    case CDI_DISKERROR:
        syncIntr = readyIntr = CDL_DISKERROR;
        memcpy(syncResult, result, 8);
        memcpy(readyResult, result, 8);
        ready = sync = 1;
        break;
    }
    if (ready && cbReady != NULL) {
        if (readyIntr == CDL_DATAREADY && !inWorld) {
            slowdownCdDelivered(); /* port/src/system/slowdown.c */
        }
        cbReady(readyIntr, readyResult);
    }
    if (sync && !inWorld) {
        slowdownCdComplete(syncIntr);
    }
    if (sync && cbSync != NULL) {
        cbSync(syncIntr, syncResult);
    }
    if (--irqDepth == 0 && dmaPending) {
        dmaPending = 0;
        if (cbData != NULL) {
            cbData();
        }
    }
}

/* The DMA interrupt of a CdGetSector outside the CD interrupt handler. */
static void dmaEvent(void* arg) {
    (void)arg;
    if (cbData != NULL) {
        cbData();
    }
}

/* One step of a busy-wait: let interrupts in (hwYield delivers them, or lets
 * time pass where it cannot). Inside an interrupt callback, or with interrupts
 * masked, nothing is delivered, so the drive is polled directly, as libcd
 * polls the controller when called from a callback. */
static void waitStep(void) {
    hwYield();
    if (irqDepth > 0 || driveOverdue(0.002)) {
        drivePoll();
    }
}

static void timeout(const char* name) {
    fprintf(stderr, "CD timeout: %s:(%s) Sync=%s, Ready=%s\n", name, comNames[lastCom & 31], intrNames[syncIntr & 7],
            intrNames[readyIntr & 7]);
    CdFlush();
}

static void copyResult(uint8_t* dst, const uint8_t* src) {
    if (dst != NULL) {
        memcpy(dst, src, 8);
    }
}

/* Wait for the current command to complete (mode 0) or check (mode 1). */
static s32 syncWait(s32 mode, uint8_t* result) {
    double deadline = hwNow() + CD_TIMEOUT;
    for (;;) {
        uint8_t intr = syncIntr;
        if (intr == CDL_COMPLETE || intr == CDL_DISKERROR) {
            syncIntr = CDL_COMPLETE;
            copyResult(result, syncResult);
            return intr;
        }
        if (mode != 0) {
            return CDL_NOINTR;
        }
        if (hwNow() > deadline) {
            timeout("CD_sync");
            return -1;
        }
        waitStep();
    }
}

/* Send one command: 0 on success, -1 on an error response, -2 without its
 * parameter. */
static s32 sendCommand(uint8_t com, const uint8_t* param, uint8_t* result, int async) {
    int c = com & 31; /* the library's tables cover commands 0-31 */
    if (debugLevel >= 2) {
        fprintf(stderr, "%s...\n", comNames[c]);
    }
    if (paramCount[c] != 0 && param == NULL) {
        if (debugLevel > 0) {
            fprintf(stderr, "%s: no param\n", comNames[c]);
        }
        return -2;
    }
    syncWait(0, NULL);
    if (com == CDC_SETLOC) {
        memcpy(&lastPos, param, sizeof lastPos);
    }
    if (com == CDC_SETMODE) {
        lastMode = param[0];
    }
    syncIntr = CDL_NOINTR;
    if (clearsReady[c]) {
        readyIntr = CDL_NOINTR;
    }
    lastCom = com;
    driveCommand(com, param, paramCount[c]);
    if (async) {
        return 0;
    }
    double deadline = hwNow() + CD_TIMEOUT;
    while (syncIntr == CDL_NOINTR) {
        if (hwNow() > deadline) {
            timeout("CD_cw");
            return -1;
        }
        waitStep();
    }
    copyResult(result, syncResult);
    return syncIntr == CDL_DISKERROR ? -1 : 0;
}

/* Up to four tries; a Nop first while the shell reports open, and Setloc
 * first for commands given a position. The caller's sync callback sees only
 * the command itself. */
static s32 command(uint8_t com, uint8_t* param, uint8_t* result, int async) {
    CdlCB old = cbSync;
    for (int tries = 0; tries < 4; tries++) {
        cbSync = NULL;
        if (com != CDC_NOP && (status & CDS_SHELLOPEN)) {
            sendCommand(CDC_NOP, NULL, NULL, 0);
        }
        if (param == NULL || !setlocFirst[com & 31] || sendCommand(CDC_SETLOC, param, result, 0) == 0) {
            cbSync = old;
            if (sendCommand(com, param, result, async) == 0) {
                return 0;
            }
        }
    }
    cbSync = old;
    return -1;
}

/* ---- initialisation ---- */

static void defaultSync(u8 intr, u8* result) {
    (void)intr;
    (void)result;
}

static void defaultReady(u8 intr, u8* result) {
    (void)intr;
    (void)result;
}

static void defaultRead(u8 intr, u8* result) {
    (void)intr;
    (void)result;
}

/* The SPU's CD input, as libcd sets it up: main volume if it is still off,
 * CD volume, and SPUCNT with the CD audio enabled. */
extern unsigned char portSpuRegisters[0x200];

static uint16_t spuRead(int offset) {
    uint16_t v;
    memcpy(&v, &portSpuRegisters[offset], 2);
    return v;
}

static void spuWrite(int offset, uint16_t v) {
    memcpy(&portSpuRegisters[offset], &v, 2);
}

/* The SPU's CD input as CdInit sets it up. */
static void spuCdInputOn(void) {
    if (spuRead(0x1B8) == 0 && spuRead(0x1BA) == 0) {
        spuWrite(0x180, 0x3FFF);
        spuWrite(0x182, 0x3FFF);
    }
    spuWrite(0x1B0, 0x3FFF);
    spuWrite(0x1B2, 0x3FFF);
    spuWrite(0x1AA, 0xC001);
}

static int resetDrive(void) {
    lastCom = 0;
    lastMode = 0;
    cbReady = NULL;
    cbSync = NULL;
    status = 0;
    status1 = 0;
    CdFlush();
    sendCommand(CDC_NOP, NULL, NULL, 0);
    if (status & CDS_SHELLOPEN) {
        sendCommand(CDC_NOP, NULL, NULL, 0);
    }
    if (sendCommand(CDC_INIT, NULL, NULL, 0) != 0) {
        return 0;
    }
    if (sendCommand(CDC_DEMUTE, NULL, NULL, 0) != 0) {
        return 0;
    }
    if (syncWait(0, NULL) != CDL_COMPLETE) {
        return 0;
    }
    spuCdInputOn();
    static const uint8_t full[4] = { 0x80, 0, 0x80, 0 };
    memcpy(mix, full, sizeof mix);
    return 1;
}

/* ---- the API ---- */

s32 CdInit(void) {
    hwSafePoint();
    for (int i = 0; i < 5; i++) {
        if (resetDrive()) {
            cbSync = defaultSync;
            cbReady = defaultReady;
            cbRead = defaultRead;
            return 1;
        }
    }
    fprintf(stderr, "CdInit: Init failed\n");
    return 0;
}

void CdFlush(void) {
    hwSafePoint();
    driveFlush();
    readyIntr = dataEndIntr = CDL_NOINTR;
    syncIntr = CDL_COMPLETE;
}

s32 CdSetDebug(s32 level) {
    hwSafePoint();
    s32 old = debugLevel;
    debugLevel = level;
    return old;
}

CdlCB CdSyncCallback(CdlCB func) {
    hwSafePoint();
    CdlCB old = cbSync;
    cbSync = func;
    return old;
}

/* Whether a data sector reaches a ready callback (the slowdown replay counts those). */
int cdReadyCallbackSet(void) {
    return cbReady != NULL;
}

CdlCB CdReadyCallback(CdlCB func) {
    hwSafePoint();
    CdlCB old = cbReady;
    cbReady = func;
    return old;
}

/* The callback of CdRead; the game only saves and restores it. */
CdlCB CdReadCallback(CdlCB func) {
    hwSafePoint();
    CdlCB old = cbRead;
    cbRead = func;
    return old;
}

void* CdDataCallback(void* func) {
    hwSafePoint();
    void (*old)(void) = cbData;
    cbData = (void (*)(void))func;
    return (void*)old;
}

s32 CdControl(u8 com, u8* param, u8* result) {
    hwSafePoint();
    return command(com, param, result, 0) + 1;
}

s32 CdControlF(u8 com, u8* param) {
    hwSafePoint();
    return command(com, param, NULL, 1) + 1;
}

s32 CdControlB(u8 com, u8* param, u8* result) {
    hwSafePoint();
    if (command(com, param, result, 0) != 0) {
        return 0;
    }
    return syncWait(0, result) == CDL_COMPLETE;
}

s32 CdSync(s32 mode, u8* result) {
    hwSafePoint();
    return syncWait(mode, result);
}

s32 CdReady(s32 mode, u8* result) {
    hwSafePoint();
    double deadline = hwNow() + CD_TIMEOUT;
    for (;;) {
        uint8_t intr = dataEndIntr;
        if (intr != CDL_NOINTR) {
            dataEndIntr = CDL_NOINTR;
            copyResult(result, dataEndResult);
            return intr;
        }
        intr = readyIntr;
        if (intr != CDL_NOINTR) {
            readyIntr = CDL_NOINTR;
            copyResult(result, readyResult);
            return intr;
        }
        if (mode != 0) {
            return CDL_NOINTR;
        }
        if (hwNow() > deadline) {
            timeout("CD_ready");
            return -1;
        }
        waitStep();
    }
}

/* `size` is in words. The copy is immediate; the DMA interrupt (the data
 * callback) follows once the CD interrupt handler returns. */
s32 CdGetSector(void* madr, s32 size) {
    hwSafePoint();
    if (size > 0) {
        driveReadFifo(madr, size * 4);
    }
    if (irqDepth > 0) {
        dmaPending = 1;
    } else if (cbData != NULL) {
        hwScheduleAt(hwNow(), dmaEvent, NULL);
    }
    return 1;
}

s32 CdDataSync(s32 mode) {
    hwSafePoint();
    (void)mode;
    return 0; /* CdGetSector's transfers finish before it returns */
}

s32 CdMix(CdAudioMix* vol) {
    hwSafePoint();
    memcpy(mix, vol, sizeof mix);
    return 1;
}

s32 CdStatus(void) {
    hwSafePoint();
    return status;
}

s32 CdMode(void) {
    hwSafePoint();
    return lastMode;
}

s32 CdLastCom(void) {
    hwSafePoint();
    return lastCom;
}

CdlLOC* CdLastPos(void) {
    hwSafePoint();
    return &lastPos;
}

static int toBcd(int i) {
    return i / 10 * 16 + i % 10;
}

static int fromBcd(int b) {
    return b / 16 * 10 + b % 16;
}

CdlLOC* CdIntToPos(s32 i, CdlLOC* p) {
    hwSafePoint();
    i += 150;
    p->sector = (u8)toBcd(i % 75);
    p->second = (u8)toBcd(i / 75 % 60);
    p->minute = (u8)toBcd(i / 75 / 60);
    return p;
}

s32 CdPosToInt(CdlLOC* p) {
    hwSafePoint();
    return (fromBcd(p->minute) * 60 + fromBcd(p->second)) * 75 + fromBcd(p->sector) - 150;
}

void cdGetAudioMix(uint8_t atv[4]) {
    memcpy(atv, mix, sizeof mix);
}

/* ---- a world of its own (cd.h cdWorldCreate) ---- */

/* What a world has of this file: LIBCD's callbacks, its interrupt state and
 * the CD audio mix. */
typedef struct {
    CdlCB cbSync, cbReady, cbRead;
    void (*cbData)(void);
    s32 debugLevel;
    uint8_t status, status1;
    CdlLOC lastPos;
    uint8_t lastMode, lastCom;
    uint8_t syncIntr, readyIntr, dataEndIntr;
    uint8_t syncResult[8], readyResult[8], dataEndResult[8];
    int irqDepth, dmaPending;
    uint8_t mix[4];
    int inWorld;
} LibcdPart;

size_t libcdPartSize(void) {
    return sizeof(LibcdPart);
}

/* As CdInit leaves it (without its drive commands and SPU writes). */
void libcdPartInit(void* blob) {
    LibcdPart* p = blob;
    memset(p, 0, sizeof(*p));
    p->cbSync = defaultSync;
    p->cbReady = defaultReady;
    p->cbRead = defaultRead;
    p->status = CDS_MOTOR;
    p->lastPos.minute = 2;
    p->syncIntr = CDL_COMPLETE;
    p->mix[0] = p->mix[2] = 0x80;
    p->inWorld = 1;
}

/* Inside a world of its own (and its SPU's, system/world.h): the SPU's CD
 * input as CdInit sets it up, for the drive's XA audio. */
void cdWorldSpuInput(void) {
    spuCdInputOn();
}

/* volatile fields are exchanged through plain copies. */
static void swapVolatile(volatile uint8_t* mine, uint8_t* theirs) {
    uint8_t t = *mine;
    *mine = *theirs;
    *theirs = t;
}

void libcdPartSwap(void* blob) {
    LibcdPart* p = blob;
    CD_SWAP(cbSync, p->cbSync);
    CD_SWAP(cbReady, p->cbReady);
    CD_SWAP(cbRead, p->cbRead);
    CD_SWAP(cbData, p->cbData);
    CD_SWAP(debugLevel, p->debugLevel);
    CD_SWAP(status, p->status);
    CD_SWAP(status1, p->status1);
    CD_SWAP(lastPos, p->lastPos);
    CD_SWAP(lastMode, p->lastMode);
    CD_SWAP(lastCom, p->lastCom);
    swapVolatile(&syncIntr, &p->syncIntr);
    swapVolatile(&readyIntr, &p->readyIntr);
    swapVolatile(&dataEndIntr, &p->dataEndIntr);
    CD_SWAP(syncResult, p->syncResult);
    CD_SWAP(readyResult, p->readyResult);
    CD_SWAP(dataEndResult, p->dataEndResult);
    CD_SWAP(irqDepth, p->irqDepth);
    CD_SWAP(dmaPending, p->dmaPending);
    CD_SWAP(mix, p->mix);
    CD_SWAP(inWorld, p->inWorld);
}

/* ---- save states (system/savestate.h) ---- */

/* The callbacks (game code, or the stream's), results not yet delivered. */
void libcdStateIo(XgState* s) {
    XG_STATE_VAR(s, cbSync);
    XG_STATE_VAR(s, cbReady);
    XG_STATE_VAR(s, cbRead);
    XG_STATE_VAR(s, cbData);
    XG_STATE_VAR(s, debugLevel);
    XG_STATE_VAR(s, status);
    XG_STATE_VAR(s, status1);
    XG_STATE_VAR(s, lastPos);
    XG_STATE_VAR(s, lastMode);
    XG_STATE_VAR(s, lastCom);
    xgStateIo(s, "syncIntr", (void*)&syncIntr, sizeof(syncIntr));
    xgStateIo(s, "readyIntr", (void*)&readyIntr, sizeof(readyIntr));
    xgStateIo(s, "dataEndIntr", (void*)&dataEndIntr, sizeof(dataEndIntr));
    XG_STATE_VAR(s, syncResult);
    XG_STATE_VAR(s, readyResult);
    XG_STATE_VAR(s, dataEndResult);
    XG_STATE_VAR(s, irqDepth);
    XG_STATE_VAR(s, dmaPending);
    XG_STATE_VAR(s, mix);
}
