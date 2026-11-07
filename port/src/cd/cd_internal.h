#ifndef PORT_CD_INTERNAL_H
#define PORT_CD_INTERNAL_H

/* Shared by the files of port/src/cd. */

#include "cd/cd.h"

#include <stdint.h>

/* ---- disc.c: the image ---- */

typedef struct CdDisc CdDisc;

CdDisc* discOpen(const char* path);
int discIsJapanese(CdDisc* disc);
const char* discLastError(void); /* last open failure; main-thread image operations */
void discClose(CdDisc* disc);
void discWarm(const CdDisc* disc); /* its files into the system's file cache, in the background (once per path) */
int discRead(CdDisc* disc, int lba, uint8_t* out); /* 2352 bytes; 0 outside the disc */
int discSectorCount(const CdDisc* disc);
int discTrackCount(const CdDisc* disc);
int discTrackNumber(const CdDisc* disc, int i);
int discTrackStart(const CdDisc* disc, int i);   /* LBA of INDEX 01 */
int discTrackIsAudio(const CdDisc* disc, int i);
int discTrackAt(const CdDisc* disc, int lba);    /* index of the track holding lba */
void discWriteHeader(uint8_t* raw, int lba, int mode);

/* ---- drive.c: the drive and its controller ---- */

/* Controller commands (psx-spx numbering). */
enum {
    CDC_SYNC = 0x00,
    CDC_NOP = 0x01,
    CDC_SETLOC = 0x02,
    CDC_PLAY = 0x03,
    CDC_FORWARD = 0x04,
    CDC_BACKWARD = 0x05,
    CDC_READN = 0x06,
    CDC_MOTORON = 0x07,
    CDC_STOP = 0x08,
    CDC_PAUSE = 0x09,
    CDC_INIT = 0x0A,
    CDC_MUTE = 0x0B,
    CDC_DEMUTE = 0x0C,
    CDC_SETFILTER = 0x0D,
    CDC_SETMODE = 0x0E,
    CDC_GETPARAM = 0x0F,
    CDC_GETLOCL = 0x10,
    CDC_GETLOCP = 0x11,
    CDC_SETSESSION = 0x12,
    CDC_GETTN = 0x13,
    CDC_GETTD = 0x14,
    CDC_SEEKL = 0x15,
    CDC_SEEKP = 0x16,
    CDC_TEST = 0x19,
    CDC_GETID = 0x1A,
    CDC_READS = 0x1B,
    CDC_RESET = 0x1C,
    CDC_READTOC = 0x1E,
};

/* Interrupt types the controller raises (INT1..INT5). */
enum {
    CDI_DATAREADY = 1,
    CDI_COMPLETE = 2,
    CDI_ACKNOWLEDGE = 3,
    CDI_DATAEND = 4,
    CDI_DISKERROR = 5,
};

/* Stat bits. */
enum {
    CDS_ERROR = 0x01,
    CDS_MOTOR = 0x02,
    CDS_SEEKERROR = 0x04,
    CDS_IDERROR = 0x08,
    CDS_SHELLOPEN = 0x10,
    CDS_READ = 0x20,
    CDS_SEEK = 0x40,
    CDS_PLAY = 0x80,
};

/* Send a command with its parameter bytes; the responses arrive later as
 * interrupts through cdInterrupt (libcd.c). */
void driveCommand(uint8_t com, const uint8_t* param, int paramCount);

/* Deliver every drive event due by now (the interrupt line, polled). Returns
 * the number delivered. */
int drivePoll(void);

/* Whether an event has been due for longer than `slack` seconds (the safe
 * points are not delivering it). */
int driveOverdue(double slack);

/* The data FIFO of the sector of the last INT1: copy `bytes` from the read
 * position on (zeros past the end), as the CD DMA would. */
void driveReadFifo(void* dst, int bytes);

/* The last INT1's raw sector (2352 bytes), for the streaming library. */
const uint8_t* driveCurrentSector(void);

/* Acknowledge pending interrupts (CdFlush): drop due, undelivered ones. */
void driveFlush(void);

uint8_t driveMode(void);

/* ---- libcd.c: LIBCD's interrupt handler ---- */

void cdInterrupt(int type, const uint8_t* response, int count);
int cdReadyCallbackSet(void);

/* ---- xa.c: XA-ADPCM sectors the drive sends to the SPU ---- */

void xaSubmitSector(const uint8_t* raw);
/* ---- world.c: a world of its own (cd.h cdWorldCreate) ---- */

/* Each file's part of a world: *PartSize bytes, made by *PartInit, exchanged
 * with the file's state by *PartSwap (entering and leaving are the same swap). */
#include <stddef.h>
void cdSwapBytes(void* a, void* b, size_t n);
#define CD_SWAP(mine, theirs) cdSwapBytes(&(mine), &(theirs), sizeof(mine))
size_t drivePartSize(void);
void drivePartInit(void* blob, CdDisc* disc); /* the part owns the disc */
void drivePartFree(void* blob);
void drivePartSwap(void* blob);
size_t libcdPartSize(void);
void libcdPartInit(void* blob);
void libcdPartSwap(void* blob);
size_t streamPartSize(void);
void streamPartInit(void* blob);
void streamPartSwap(void* blob);
size_t regionalPartSize(void);
void regionalPartInit(void* blob);
void regionalPartSwap(void* blob);
size_t xaPartSize(void);
void xaPartInit(void* blob);
void xaPartSwap(void* blob);

/* ---- logging ---- */

extern int cdLogLevel;
void cdLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* PORT_CD_INTERNAL_H */
