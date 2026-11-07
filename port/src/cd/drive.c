/*
 * The CD-ROM drive and its controller, at the level of commands and
 * interrupts (psx-spx "CDROM Drive"): the controller acknowledges each command
 * with INT3, sends INT2 when a long command finishes, INT1 per sector read and
 * INT5 on errors. Each response is an event on the drive's own timeline,
 * delivered at the time the PS1 would raise it: through hwScheduleAt at the
 * game's safe points, or polled directly while LIBCD waits inside a callback
 * (as LIBCD polls the controller when interrupts cannot reach it).
 *
 * Timing. Sectors pass at exactly 75 or 150 per second (psx-spx "INT1 Rate");
 * GetID, Stop and the motor-off acknowledge are psx-spx's "Response Timings".
 * psx-spx leaves seek times unknown; they, the first responses, Pause and Init
 * here are the port's own fit to measurements of Beetle PSX 0.9.44 as a black
 * box (work/clean-room/measure/cd: a probe program on the emulated console
 * timing every command and interrupt with root counter 2; fits in fit.py,
 * notes in work/agents/CD_TIMING/NOTES.md). In CPU cycles, P the sector
 * period, d the distance in sectors from the head:
 *
 *   first response   22300 + 1650 per parameter byte (spread +-1500)
 *   seek, d < 2250   59000 + 6.465 P after a Pause (0.96 P while reading or
 *                    after a seek) + 104.5 per sector past the first 192
 *   seek, d >= 2250  10194133 + P + 104.5 d
 *   first sector     2 P after the seek (Setloc + ReadN at double speed:
 *                    1.97 M cycles, 3.48 vblanks, across up to 192 sectors)
 *   Pause            P (4.98 + lba / 23853) after the acknowledge while
 *                    reading or seeking (lba: the head or the seek target),
 *                    5000 cycles when already paused
 *   Init             1135700 after the acknowledge
 *
 * A seek is position and direction independent; the time between a Pause or
 * Setloc and the read does not matter. Beetle's times spread by about +-14000
 * cycles around these (its controller's response jitter); the model is
 * deterministic and takes the fitted means.
 */

#include "cd_internal.h"
/* Weak: CD tests may link without the regional layer. */
extern void regionalMovieSector(int lba, unsigned char raw[2352]) __attribute__((weak));
#include "system/hw.h"
#include "../system/savestate.h"
#include "../system/speedup.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CYCLES(n) ((double)(n) / HW_CPU_HZ)

/* Measured on Beetle PSX as a black box (see the top of the file). */
#define ACK_TIME CYCLES(22300)          /* a command without parameters */
#define ACK_PER_PARAM CYCLES(1650)      /* per parameter byte */
#define PAUSE_IDLE_TIME CYCLES(5000)    /* Pause when paused, after its acknowledge */
#define PAUSE_PERIODS 4.98              /* Pause while reading: P * (this + lba / ...) */
#define PAUSE_LBA_PERIOD 23853.0
#define INIT_TIME CYCLES(1135700)       /* Init's second response, motor on */
#define SEEK_BASE CYCLES(59000)
#define SEEK_PAUSED_PERIODS 6.465       /* a short seek after a Pause or Init */
#define SEEK_HOLDING_PERIODS 0.96       /* a short seek while reading or after a seek */
#define SEEK_SHORT_LIMIT 2250           /* sectors: from here a long seek */
#define SEEK_FREE 192                   /* sectors a short seek covers in its base time */
#define SEEK_LONG CYCLES(10194133)
#define SEEK_PER_SECTOR CYCLES(104.5)
#define READ_START_PERIODS 2.0          /* seek end to the first sector */
/* psx-spx "Response Timings". */
#define ACK_TIME_STOPPED CYCLES(0x5CF4)
#define STOP_TIME_SINGLE CYCLES(0xD38ACA)
#define STOP_TIME_DOUBLE CYCLES(0x18A6076)
#define STOP_IDLE_TIME CYCLES(0x1D7B)
#define GETID_TIME CYCLES(0x4A00)
#define READTOC_TIME 1.0   /* "about 1 second" */
/* The port's own (not measured: the game never stops the motor). */
#define SPINUP_TIME 1.0    /* motor off to reading speed */
#define SHELL_OPEN_TIME 0.5

/* ---- the drive's timeline ---- */

enum {
    EV_INT,     /* a prepared interrupt (acknowledge, error) */
    EV_SECOND,  /* second response of the command in `com` */
    EV_SEEKED,  /* a seek reached its target */
    EV_SECTOR,  /* the next sector passes the head */
    EV_SHELL,   /* the shell closes after a disc change */
};

typedef struct {
    double at;
    double due; /* EV_SECTOR: its time on the drive's clock (a replay hold delays `at` only) */
    uint32_t seq;
    uint8_t kind;
    uint8_t type;
    uint8_t com;
    uint8_t count;
    uint8_t response[8];
} Event;

#define MAX_EVENTS 64
static Event events[MAX_EVENTS];
static int eventCount;
static uint32_t eventSeq;

/* ---- drive state ---- */

enum { IDLE, SEEKING, READING, PLAYING };
enum { AFTER_SEEK_READ, AFTER_SEEK_PLAY, AFTER_SEEK_STOP };

static CdDisc* disc;
static int sectorCount;
static int japaneseImage;
extern int discIsJapanese(CdDisc* disc) __attribute__((weak));
int cdDiscIsJapanese(void) { return japaneseImage; }
static double speedFactor = 1.0;
int cdLogLevel;

static int motorOn = 1;
static int paused = 1;     /* stopped by Pause or Init: a short seek takes longer */
static int shellOpen;      /* the shell is open now */
static int shellLatch;     /* stat bit 4: open since the last Nop */
static int activity = IDLE;
static int afterSeek;
static int streaming;      /* ReadS rather than ReadN */
static uint8_t mode;
static uint8_t sizeBits;   /* mode bit 5 of the last Setmode without bit 4 */
static uint8_t filterFile;
static uint8_t filterChannel;
static int muted;

static int setlocTarget;
static int setlocPending;
static int headLba;        /* the next sector to pass under the head */
static int seekTarget;
static int lastReadLba = -1;
static uint8_t lastHeader[8];
static int haveLastHeader;

static uint8_t sector[CD_RAW_SECTOR]; /* sector of the last INT1 */
static int fifoStart;
static int fifoSize;
static int fifoPos;

/* A drive of a world of its own (cd.h cdWorldCreate): none of the game's
 * hooks (the automatic disc swap, the slowdown replay) see its commands. */
static int inWorld;

void cdLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "cd %9.4f: ", hwNow());
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ---- timeline ---- */

static void deliverUpTo(double limit);

static void hwEvent(void* arg) {
    uint32_t seq = (uint32_t)(uintptr_t)arg;
    for (int i = 0; i < eventCount; i++) {
        if (events[i].seq == seq) {
            deliverUpTo(events[i].at);
            return;
        }
    }
}

/* The controller raises one interrupt at a time: nothing follows a command
 * before its acknowledge. */
static double lastAckAt;

static Event* addEvent(double at, int kind) {
    if (kind != EV_INT && at < lastAckAt) {
        at = lastAckAt;
    }
    if (eventCount >= MAX_EVENTS) {
        fprintf(stderr, "cd: event queue full\n");
        return NULL;
    }
    int i = eventCount;
    while (i > 0 && events[i - 1].at > at) {
        events[i] = events[i - 1];
        i--;
    }
    eventCount++;
    Event* e = &events[i];
    memset(e, 0, sizeof *e);
    e->at = at;
    e->seq = ++eventSeq;
    e->kind = (uint8_t)kind;
    hwScheduleAt(at, hwEvent, (void*)(uintptr_t)e->seq);
    return e;
}

/* The next sector, due at `due` on the drive's clock, read no sooner than
 * `notBefore`. */
static void queueSector(double due, double notBefore) {
    Event* e = addEvent(due > notBefore ? due : notBefore, EV_SECTOR);
    if (e != NULL) {
        e->due = due;
    }
}

static void cancelEvents(int kind) {
    int j = 0;
    for (int i = 0; i < eventCount; i++) {
        if (events[i].kind != kind) {
            events[j++] = events[i];
        }
    }
    eventCount = j;
}

static void raiseAt(double at, int type, const uint8_t* response, int count) {
    Event* e = addEvent(at, EV_INT);
    if (type == CDI_ACKNOWLEDGE || type == CDI_DISKERROR) {
        lastAckAt = at;
    }
    if (e != NULL) {
        e->type = (uint8_t)type;
        e->count = (uint8_t)count;
        memcpy(e->response, response, (size_t)count);
    }
}

/* The slowdown replay (XG_SLOWDOWN): when the PS1 completed the command being
 * written with the response that follows its acknowledge, or -1. */
static double replayDone = -1;
/* And when the PS1 delivered the first sector of the read being written. */
static double replayFirst = -1;

static void secondAt(double at, uint8_t com) {
    if (replayDone >= 0) {
        at = replayDone;
        replayDone = -1;
    }
    Event* e = addEvent(at, EV_SECOND);
    if (e != NULL) {
        e->com = com;
    }
}

/* ---- status and timing ---- */

static uint8_t driveStat(void) {
    uint8_t s = 0;
    if (motorOn) {
        s |= CDS_MOTOR;
    }
    if (shellLatch || shellOpen) {
        s |= CDS_SHELLOPEN;
    }
    switch (activity) {
    case SEEKING:
        s |= CDS_SEEK;
        break;
    case READING:
        s |= CDS_READ;
        break;
    case PLAYING:
        s |= CDS_PLAY;
        break;
    }
    return s;
}

/* Only ordinary data loads use the player's Speedup option. ReadS, XA and
 * CD-DA retain their clock; already queued events retain their deadlines.
 * Keep the developer multiplier's original behaviour when Speedup is off. */
static double factor(int dataLoad) {
    if (streaming || (mode & 0x40) || activity == PLAYING) {
        return 1.0;
    }
    if (dataLoad && speedupEnabled() && speedFactor < 8.0) {
        return 8.0;
    }
    return speedFactor;
}

static double sectorPeriod(void) {
    return (mode & 0x80) ? 1.0 / 150.0 : 1.0 / 75.0;
}

static double sectorTime(void) {
    return sectorPeriod() / factor(1);
}

/* From the command to the head on `to` (the top of the file). */
static double seekTime(int from, int to, int dataLoad) {
    int d = abs(to - from);
    double t;
    if (d >= SEEK_SHORT_LIMIT) {
        t = SEEK_LONG + sectorPeriod() + SEEK_PER_SECTOR * d;
    } else {
        t = SEEK_BASE + (paused ? SEEK_PAUSED_PERIODS : SEEK_HOLDING_PERIODS) * sectorPeriod();
        if (d > SEEK_FREE) {
            t += SEEK_PER_SECTOR * (d - SEEK_FREE);
        }
    }
    if (!motorOn) {
        t += SPINUP_TIME;
    }
    return t / factor(dataLoad);
}

/* When Pause completes after its acknowledge. */
static double pauseTime(void) {
    if (activity == IDLE) {
        return PAUSE_IDLE_TIME;
    }
    int lba = activity == SEEKING ? seekTarget : headLba;
    return sectorTime() * (PAUSE_PERIODS + lba / PAUSE_LBA_PERIOD);
}

static double ackTime(int paramCount) {
    return motorOn ? ACK_TIME + ACK_PER_PARAM * paramCount : ACK_TIME_STOPPED;
}

static uint8_t bcd(int v) {
    return (uint8_t)((v / 10) * 16 + v % 10);
}

static int validBcd(uint8_t v) {
    return (v & 0x0F) < 10 && (v >> 4) < 10;
}

static int fromBcd(uint8_t v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

static void ack(double at, const uint8_t* response, int count) {
    raiseAt(at, CDI_ACKNOWLEDGE, response, count);
}

static void ackStat(double at) {
    uint8_t s = driveStat();
    ack(at, &s, 1);
}

static void fail(double at, uint8_t code) {
    uint8_t r[2] = { (uint8_t)(driveStat() | CDS_ERROR), code };
    raiseAt(at, CDI_DISKERROR, r, 2);
}

/* ---- reading and seeking ---- */

static void stopActivity(void) {
    cancelEvents(EV_SEEKED);
    cancelEvents(EV_SECTOR);
    if (activity == SEEKING) {
        headLba = seekTarget;
    }
    activity = IDLE;
    paused = 1;
}

/* Move the head to `target`, then read, play or stop there. */
static void startSeek(double now, int target, int then) {
    cancelEvents(EV_SEEKED);
    cancelEvents(EV_SECTOR);
    afterSeek = then;
    /* Even the next sector is a seek (Beetle: 3.48 vblanks from Setloc + ReadN
     * after a Pause, as for 192 sectors away). */
    double t = seekTime(activity == SEEKING ? seekTarget : headLba, target, then != AFTER_SEEK_PLAY);
    if (then == AFTER_SEEK_STOP && replayDone >= 0) {
        t = replayDone - now; /* SeekL/SeekP: completes when the PS1's did */
        replayDone = -1;
    }
    if (then == AFTER_SEEK_READ && replayFirst >= 0) {
        /* The first sector no later than the PS1's (sooner ones are held),
         * unless the PC is far behind (the replay has come apart). */
        double latest = replayFirst - now - READ_START_PERIODS * sectorTime();
        if (latest < t && t - latest < 1.0) {
            t = latest > 0 ? latest : 0;
        }
    }
    replayFirst = -1;
    activity = SEEKING;
    seekTarget = target;
    motorOn = 1;
    addEvent(now + t, EV_SEEKED);
}

static void startRead(double now, int stream) {
    streaming = stream;
    if (setlocPending) {
        setlocPending = 0;
        startSeek(now, setlocTarget, AFTER_SEEK_READ);
    } else if (activity == READING) {
        /* Already reading: it just continues. */
    } else if (activity == SEEKING) {
        afterSeek = AFTER_SEEK_READ;
    } else {
        /* Paused: resume at the most recently received sector. */
        startSeek(now, lastReadLba >= 0 ? lastReadLba : headLba, AFTER_SEEK_READ);
    }
}

static void seeked(const Event* e) {
    headLba = seekTarget;
    lastReadLba = -1;
    paused = 0; /* the drive holds the place now (psx-spx "SeekL") */
    switch (afterSeek) {
    case AFTER_SEEK_READ:
        activity = READING;
        addEvent(e->at + READ_START_PERIODS * sectorTime(), EV_SECTOR);
        break;
    case AFTER_SEEK_PLAY:
        activity = PLAYING;
        addEvent(e->at + 1.0 / ((mode & 0x80) ? 150.0 : 75.0), EV_SECTOR);
        break;
    default: {
        activity = IDLE;
        uint8_t s = driveStat();
        cdInterrupt(CDI_COMPLETE, &s, 1);
        break;
    }
    }
}

/* Where the data FIFO of a sector starts and how long it is, for the mode. */
static void setFifo(const uint8_t* raw, int audio) {
    uint8_t size = (mode & 0x10) ? sizeBits : (mode & 0x20);
    if (audio) {
        fifoStart = 0;
        fifoSize = CD_RAW_SECTOR;
    } else if (size & 0x20) {
        fifoStart = 12;
        fifoSize = 0x924;
    } else {
        fifoStart = raw[15] == 1 ? 16 : 24;
        fifoSize = 0x800;
    }
    fifoPos = 0;
}

static void readSector(const Event* e) {
    int lba = headLba;
    if (lba >= sectorCount || lba < 0) {
        /* Past the end of the disc. */
        activity = IDLE;
        uint8_t s = driveStat();
        cdInterrupt(CDI_DATAEND, &s, 1);
        return;
    }
    uint8_t raw[CD_RAW_SECTOR];
    if (!discRead(disc, lba, raw)) {
        activity = IDLE;
        uint8_t r[2] = { (uint8_t)(driveStat() | CDS_SEEKERROR), 0x04 };
        cdInterrupt(CDI_DISKERROR, r, 2);
        return;
    }
    /* A sector the replay held back keeps its place on the drive's clock, up
     * to half a period before its delivery (the PS1's own lateness: Beetle
     * PSX logs deliver a sector up to about 0.15 vblank late). A drive more
     * ahead than that began the read sooner than the PS1's (the PC wrote it
     * sooner): its next sector would already be due, and one the PS1 never
     * delivered would come at once (the worldmap stream's re-seek from its
     * ready callback: the sector after the Setloc, before the ReadS). */
    double due = e->due > 0 ? e->due : e->at;
    if (due < e->at - sectorTime() / 2) {
        due = e->at - sectorTime() / 2;
    }
    headLba = lba + 1;
    /* Queue the next sector first: the callback may stop the drive. */
    queueSector(due + sectorTime(), e->at);

    int audio = discTrackIsAudio(disc, discTrackAt(disc, lba));
    if (audio && !(mode & 0x01)) {
        return; /* CD-DA sectors are read only with Setmode bit 0 */
    }
    lastReadLba = lba;
    if (!audio) {
        memcpy(lastHeader, raw + 12, 8);
        haveLastHeader = 1;
    }
    if (!audio && raw[15] == 2) {
        /* XA: realtime audio sectors go to the ADPCM decoder, not the CPU
         * (psx-spx "Data/ADPCM Sector Filtering/Delivery"). */
        const uint8_t* sub = raw + 16;
        int adpcm = (sub[2] & 0x44) == 0x44;
        int match = sub[0] == filterFile && sub[1] == filterChannel;
        if (adpcm && (mode & 0x40) && (!(mode & 0x08) || match)) {
            if (!muted) {
                if (regionalMovieSector) regionalMovieSector(lba, raw);
                xaSubmitSector(raw);
            }
            return;
        }
        if (adpcm && (mode & 0x08)) {
            return;
        }
    }
    /* The slowdown replay (XG_SLOWDOWN): no sooner than the PS1 delivered the
     * same one; the drive reads it again then. Only its delivery waits (the
     * PS1's CPU took it late): the sectors after it pass at the drive's own
     * times, so a late callback on the PS1 does not make the rest late. */
    if (cdReadyCallbackSet() && !inWorld) {
        double hold = slowdownCdHold(e->at, lba);
        if (hold > e->at) {
            headLba = lba;
            cancelEvents(EV_SECTOR);
            queueSector(due, hold);
            return;
        }
    }
    if (cdLogLevel >= 2) {
        cdLog("INT1 sector %d", lba);
    }
    memcpy(sector, raw, sizeof sector);
    setFifo(raw, audio);
    uint8_t s = driveStat();
    cdInterrupt(CDI_DATAREADY, &s, 1);
}

static void playSector(const Event* e) {
    /* CD-DA play: the head moves on; there is no CD-DA audio output yet. */
    int track = discTrackAt(disc, headLba);
    headLba++;
    int next = track + 1 < discTrackCount(disc) ? discTrackStart(disc, track + 1) : sectorCount;
    if ((mode & 0x02) && headLba >= next) {
        activity = IDLE;
        uint8_t s = driveStat();
        cdInterrupt(CDI_DATAEND, &s, 1);
        return;
    }
    if (headLba >= sectorCount) {
        activity = IDLE;
        uint8_t s = driveStat();
        cdInterrupt(CDI_DATAEND, &s, 1);
        return;
    }
    addEvent(e->at + 1.0 / ((mode & 0x80) ? 150.0 : 75.0), EV_SECTOR);
}

static void secondResponse(const Event* e) {
    uint8_t r[8] = { 0 };
    int count = 1;
    switch (e->com) {
    case CDC_STOP:
        motorOn = 0;
        headLba = 0;
        break;
    case CDC_MOTORON:
    case CDC_INIT:
        motorOn = 1;
        break;
    case CDC_GETID:
        /* A licensed mode 2 disc. */
        count = 8;
        r[1] = 0x00;
        r[2] = 0x20;
        r[3] = 0x00;
#ifdef VERSION_JP
        memcpy(r + 4, "SCEI", 4);
#else
        memcpy(r + 4, "SCEA", 4);
#endif
        break;
    default:
        break;
    }
    r[0] = driveStat();
    if (cdLogLevel) {
        cdLog("INT2 com %02X stat %02X", e->com, r[0]);
    }
    cdInterrupt(CDI_COMPLETE, r, count);
}

static void dispatch(const Event* e) {
    switch (e->kind) {
    case EV_INT:
        if (cdLogLevel) {
            cdLog("INT%d %02X %02X", e->type, e->response[0], e->count > 1 ? e->response[1] : 0);
        }
        cdInterrupt(e->type, e->response, e->count);
        break;
    case EV_SECOND:
        secondResponse(e);
        break;
    case EV_SEEKED:
        seeked(e);
        break;
    case EV_SECTOR:
        if (activity == PLAYING) {
            playSector(e);
        } else if (activity == READING) {
            readSector(e);
        }
        break;
    case EV_SHELL:
        shellOpen = 0;
        motorOn = 1;
        break;
    }
}

static void deliverUpTo(double limit) {
    while (eventCount > 0 && events[0].at <= limit) {
        Event e = events[0];
        eventCount--;
        memmove(&events[0], &events[1], sizeof events[0] * (size_t)eventCount);
        dispatch(&e);
    }
}

int drivePoll(void) {
    double now = hwNow();
    int delivered = 0;
    while (eventCount > 0 && events[0].at <= now) {
        Event e = events[0];
        eventCount--;
        memmove(&events[0], &events[1], sizeof events[0] * (size_t)eventCount);
        dispatch(&e);
        delivered++;
    }
    return delivered;
}

int driveOverdue(double slack) {
    return eventCount > 0 && events[0].at + slack <= hwNow();
}

void driveFlush(void) {
    double now = hwNow();
    int j = 0;
    for (int i = 0; i < eventCount; i++) {
        if (!(events[i].kind == EV_INT && events[i].at <= now)) {
            events[j++] = events[i];
        }
    }
    eventCount = j;
}

/* ---- commands ---- */

static int needsDisc(uint8_t com) {
    switch (com) {
    case CDC_NOP:
    case CDC_SETMODE:
    case CDC_SETFILTER:
    case CDC_GETPARAM:
    case CDC_TEST:
    case CDC_RESET:
    case CDC_INIT:
        return 0;
    default:
        return 1;
    }
}

static int expectedParams(uint8_t com) {
    switch (com) {
    case CDC_SETLOC:
        return 3;
    case CDC_SETFILTER:
        return 2;
    case CDC_SETMODE:
    case CDC_GETTD:
    case CDC_SETSESSION:
        return 1;
    case CDC_PLAY:
    case CDC_TEST:
    case CDC_RESET:
        return -1; /* optional or variable */
    default:
        return 0;
    }
}

/* Commands that complete with a response after their acknowledge. */
static int hasSecondResponse(uint8_t com) {
    switch (com) {
    case CDC_MOTORON:
    case CDC_STOP:
    case CDC_PAUSE:
    case CDC_INIT:
    case CDC_SETSESSION:
    case CDC_SEEKL:
    case CDC_SEEKP:
    case CDC_GETID:
    case CDC_READTOC:
        return 1;
    default:
        return 0;
    }
}

void driveCommand(uint8_t com, const uint8_t* param, int paramCount) {
    double now = hwNow();
    double at = now + ackTime(paramCount);
    uint8_t r[8];

    if (cdLogLevel) {
        cdLog("command %02X (%d params: %02X %02X %02X) stat %02X", com, paramCount, paramCount > 0 ? param[0] : 0,
              paramCount > 1 ? param[1] : 0, paramCount > 2 ? param[2] : 0, driveStat());
    }
    /* The slowdown replay (XG_SLOWDOWN): the command completes as long after
     * it was written as it did on the PS1 (PCSX-Redux answers much sooner
     * than this drive), with its acknowledge or with the response after it. */
    double took = inWorld ? -1 : slowdownCdCommand(com);
    replayDone = -1;
    if (took >= 0 && hasSecondResponse(com)) {
        replayDone = now + took;
        if (at > replayDone) {
            at = replayDone; /* the acknowledge still comes first */
        }
    } else if (took >= 0) {
        at = now + took;
    }
    if (!inWorld) {
        cdSwapNoteCommand(com);
    }
    int want = expectedParams(com);
    if (want >= 0 && paramCount != want) {
        fail(at, 0x20);
        return;
    }
    if ((disc == NULL || shellOpen) && needsDisc(com)) {
        fail(at, 0x80);
        return;
    }

    switch (com) {
    case CDC_NOP:
        ackStat(at);
        if (!shellOpen) {
            shellLatch = 0;
        }
        break;
    case CDC_SETLOC:
        if (!validBcd(param[0]) || !validBcd(param[1]) || !validBcd(param[2]) || param[1] >= 0x60 ||
            param[2] >= 0x75) {
            fail(at, 0x10);
            break;
        }
        setlocTarget = (fromBcd(param[0]) * 60 + fromBcd(param[1])) * 75 + fromBcd(param[2]) - 150;
        setlocPending = 1;
        ackStat(at);
        break;
    case CDC_READN:
    case CDC_READS:
        ackStat(at);
        cancelEvents(EV_SECOND);
        if (!inWorld) {
            slowdownCdRead(com, setlocPending ? setlocTarget : -1);
            replayFirst = slowdownCdFirst();
        }
        startRead(now, com == CDC_READS);
        replayFirst = -1;
        break;
    case CDC_PLAY: {
        ackStat(at);
        cancelEvents(EV_SECOND);
        int target = setlocPending ? setlocTarget : headLba;
        if (paramCount > 0 && param[0] != 0) {
            int track = fromBcd(param[0]);
            for (int i = 0; i < discTrackCount(disc); i++) {
                if (discTrackNumber(disc, i) == track) {
                    target = discTrackStart(disc, i);
                }
            }
        }
        setlocPending = 0;
        streaming = 0;
        startSeek(now, target, AFTER_SEEK_PLAY);
        break;
    }
    case CDC_FORWARD:
    case CDC_BACKWARD:
        if (activity != PLAYING) {
            fail(at, 0x80);
            break;
        }
        ackStat(at);
        break;
    case CDC_MOTORON:
        if (motorOn) {
            /* "works ONLY if the motor was off" */
            fail(at, 0x20);
            break;
        }
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + SPINUP_TIME / speedFactor, com);
        break;
    case CDC_STOP: {
        double t = !motorOn ? STOP_IDLE_TIME : (mode & 0x80) ? STOP_TIME_DOUBLE : STOP_TIME_SINGLE;
        stopActivity();
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + t / speedFactor, com);
        break;
    }
    case CDC_PAUSE: {
        double t = pauseTime();
        ackStat(at);
        stopActivity();
        cancelEvents(EV_SECOND);
        secondAt(at + t, com);
        break;
    }
    case CDC_INIT:
        mode = 0x20;
        sizeBits = 0x20;
        muted = 0;
        stopActivity();
        setlocPending = 0;
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + (motorOn ? INIT_TIME : SPINUP_TIME / speedFactor), com);
        break;
    case CDC_RESET:
        mode = 0x20;
        sizeBits = 0x20;
        muted = 0;
        stopActivity();
        setlocPending = 0;
        ackStat(at);
        break;
    case CDC_MUTE:
        muted = 1;
        ackStat(at);
        break;
    case CDC_DEMUTE:
        muted = 0;
        ackStat(at);
        break;
    case CDC_SETFILTER:
        filterFile = param[0];
        filterChannel = param[1];
        ackStat(at);
        break;
    case CDC_SETMODE:
        mode = param[0];
        if (!(mode & 0x10)) {
            sizeBits = mode & 0x20;
        }
        ackStat(at);
        break;
    case CDC_GETPARAM:
        r[0] = driveStat();
        r[1] = mode;
        r[2] = 0;
        r[3] = filterFile;
        r[4] = filterChannel;
        ack(at, r, 5);
        break;
    case CDC_GETLOCL:
        if (!haveLastHeader || activity == SEEKING) {
            fail(at, 0x80);
            break;
        }
        ack(at, lastHeader, 8);
        break;
    case CDC_GETLOCP: {
        int lba = activity == SEEKING ? seekTarget : (lastReadLba >= 0 ? lastReadLba : headLba);
        int track = discTrackAt(disc, lba);
        int rel = lba - discTrackStart(disc, track);
        int absolute = lba + 150;
        r[0] = bcd(discTrackNumber(disc, track));
        r[1] = rel < 0 ? 0 : 1;
        if (rel < 0) {
            rel = -rel;
        }
        r[2] = bcd(rel / 4500);
        r[3] = bcd(rel / 75 % 60);
        r[4] = bcd(rel % 75);
        r[5] = bcd(absolute / 4500);
        r[6] = bcd(absolute / 75 % 60);
        r[7] = bcd(absolute % 75);
        ack(at, r, 8);
        break;
    }
    case CDC_SETSESSION:
        if (param[0] == 0) {
            fail(at, 0x10);
            break;
        }
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + seekTime(headLba, 0, 0), com);
        break;
    case CDC_GETTN:
        r[0] = driveStat();
        r[1] = bcd(discTrackNumber(disc, 0));
        r[2] = bcd(discTrackNumber(disc, discTrackCount(disc) - 1));
        ack(at, r, 3);
        break;
    case CDC_GETTD: {
        int lba = -1;
        if (!validBcd(param[0])) {
            fail(at, 0x10);
            break;
        }
        int track = fromBcd(param[0]);
        if (track == 0) {
            lba = sectorCount;
        }
        for (int i = 0; i < discTrackCount(disc); i++) {
            if (discTrackNumber(disc, i) == track) {
                lba = discTrackStart(disc, i);
            }
        }
        if (lba < 0) {
            fail(at, 0x10);
            break;
        }
        lba += 150;
        r[0] = driveStat();
        r[1] = bcd(lba / 4500);
        r[2] = bcd(lba / 75 % 60);
        ack(at, r, 3);
        break;
    }
    case CDC_SEEKL:
    case CDC_SEEKP: {
        ackStat(at);
        cancelEvents(EV_SECOND);
        int target = setlocPending ? setlocTarget : headLba;
        setlocPending = 0;
        streaming = 0;
        startSeek(now, target, AFTER_SEEK_STOP);
        break;
    }
    case CDC_TEST:
        if (paramCount >= 1 && param[0] == 0x20) {
            /* Controller version: 19 Sep 1994, vC0 (SCPH-1001). */
            r[0] = 0x94;
            r[1] = 0x09;
            r[2] = 0x19;
            r[3] = 0xC0;
            ack(at, r, 4);
        } else {
            fail(at, 0x10);
        }
        break;
    case CDC_GETID:
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + GETID_TIME, com);
        break;
    case CDC_READTOC:
        ackStat(at);
        cancelEvents(EV_SECOND);
        secondAt(at + READTOC_TIME, com);
        break;
    default:
        fail(at, 0x40);
        break;
    }
}

/* ---- the data FIFO ---- */

void driveReadFifo(void* dst, int bytes) {
    uint8_t* out = dst;
    int n = fifoSize - fifoPos;
    if (n > bytes) {
        n = bytes;
    }
    if (n > 0) {
        memcpy(out, sector + fifoStart + fifoPos, (size_t)n);
        fifoPos += n;
    } else {
        n = 0;
    }
    if (bytes > n) {
        memset(out + n, 0, (size_t)(bytes - n));
    }
}

const uint8_t* driveCurrentSector(void) {
    return sector;
}

uint8_t driveMode(void) {
    return mode;
}

/* ---- the disc ---- */

static void readSettings(void) {
    const char* speed = getenv("XG_CD_SPEED");
    if (speed != NULL && speed[0] != '\0') {
        double f = atof(speed);
        if (f >= 1.0 && f <= 1000.0) {
            speedFactor = f;
        } else {
            fprintf(stderr, "cd: XG_CD_SPEED=%s ignored (1..1000)\n", speed);
        }
    }
    const char* log = getenv("XG_CD_LOG");
    if (log != NULL) {
        cdLogLevel = atoi(log);
    }
}

int cdOpenDisc(const char* cuePath) {
    readSettings();
    CdDisc* d = discOpen(cuePath);
    if (d == NULL) {
        fprintf(stderr, "cd: cannot use the disc image %s\n", cuePath);
        return 0;
    }
    discClose(disc);
    disc = d;
    discWarm(d);
    sectorCount = discSectorCount(d);
    japaneseImage = discIsJapanese && discIsJapanese(d);
    fprintf(stderr, "cd: %s (%d sectors, %d track%s)%s\n", cuePath, sectorCount, discTrackCount(d),
            discTrackCount(d) == 1 ? "" : "s", speedFactor != 1.0 ? " (XG_CD_SPEED)" : "");
    cdSwapInit(cuePath);
    return 1;
}

int cdChangeDisc(const char* cuePath) {
    CdDisc* d = discOpen(cuePath);
    if (d == NULL) {
        return 0;
    }
    double now = hwNow();
    /* The shell opens: everything stops and INT5 reports it. */
    stopActivity();
    cancelEvents(EV_SECOND);
    setlocPending = 0;
    haveLastHeader = 0;
    lastReadLba = -1;
    motorOn = 0;
    shellOpen = 1;
    shellLatch = 1;
    uint8_t r[2] = { CDS_SHELLOPEN | CDS_ERROR, 0x08 };
    raiseAt(now, CDI_DISKERROR, r, 2);
    discClose(disc);
    disc = d;
    discWarm(d);
    sectorCount = discSectorCount(d);
    japaneseImage = discIsJapanese && discIsJapanese(d);
    headLba = 0;
    addEvent(now + SHELL_OPEN_TIME, EV_SHELL);
    fprintf(stderr, "cd: disc changed to %s\n", cuePath);
    return 1;
}

/* A disc put in while the console is off or resetting (the lid opened and
 * closed before the reset): no shell-open event, the drive idle, its motor
 * on, as the next boot finds it. */
int cdInsertDisc(const char* cuePath) {
    CdDisc* d = discOpen(cuePath);
    if (d == NULL) {
        return 0;
    }
    stopActivity();
    cancelEvents(EV_SECOND);
    cancelEvents(EV_SHELL);
    setlocPending = 0;
    haveLastHeader = 0;
    lastReadLba = -1;
    headLba = 0;
    motorOn = 1;
    shellOpen = 0;
    shellLatch = 0;
    discClose(disc);
    disc = d;
    discWarm(d);
    sectorCount = discSectorCount(d);
    japaneseImage = discIsJapanese && discIsJapanese(d);
    fprintf(stderr, "cd: disc %s put in for the reset\n", cuePath);
    return 1;
}

int cdDiscSectorCount(void) {
    return disc != NULL ? sectorCount : 0;
}

int cdReadRawSector(int lba, uint8_t out[CD_RAW_SECTOR]) {
    return disc != NULL && discRead(disc, lba, out);
}

/* ---- a world of its own (cd.h cdWorldCreate) ---- */

/* What a world has of this file: a drive with its own disc, its timeline
 * (whose events wait on the world's queue: system/world.h) and its FIFO. */
typedef struct {
    Event events[MAX_EVENTS];
    int eventCount;
    uint32_t eventSeq;
    CdDisc* disc;
    int sectorCount;
    int japaneseImage;
    int motorOn, paused, shellOpen, shellLatch, activity, afterSeek, streaming;
    uint8_t mode, sizeBits, filterFile, filterChannel;
    int muted;
    int setlocTarget, setlocPending, headLba, seekTarget, lastReadLba;
    uint8_t lastHeader[8];
    int haveLastHeader;
    uint8_t sector[CD_RAW_SECTOR];
    int fifoStart, fifoSize, fifoPos;
    double lastAckAt, replayDone, replayFirst;
    int inWorld;
} DrivePart;

size_t drivePartSize(void) {
    return sizeof(DrivePart);
}

/* A drive as CdInit leaves it, motor on, with `d` in it (owned by the part). */
void drivePartInit(void* blob, CdDisc* d) {
    DrivePart* p = blob;
    memset(p, 0, sizeof(*p));
    p->disc = d;
    p->sectorCount = d != NULL ? discSectorCount(d) : 0;
    p->japaneseImage = discIsJapanese && discIsJapanese(d);
    p->motorOn = 1;
    p->paused = 1;
    p->activity = IDLE;
    p->mode = 0x20;
    p->sizeBits = 0x20;
    p->lastReadLba = -1;
    p->replayDone = -1;
    p->replayFirst = -1;
    p->inWorld = 1;
}

void drivePartFree(void* blob) {
    DrivePart* p = blob;
    discClose(p->disc);
    p->disc = NULL;
}

void drivePartSwap(void* blob) {
    DrivePart* p = blob;
    CD_SWAP(events, p->events);
    CD_SWAP(eventCount, p->eventCount);
    CD_SWAP(eventSeq, p->eventSeq);
    CD_SWAP(disc, p->disc);
    CD_SWAP(sectorCount, p->sectorCount);
    CD_SWAP(japaneseImage, p->japaneseImage);
    CD_SWAP(motorOn, p->motorOn);
    CD_SWAP(paused, p->paused);
    CD_SWAP(shellOpen, p->shellOpen);
    CD_SWAP(shellLatch, p->shellLatch);
    CD_SWAP(activity, p->activity);
    CD_SWAP(afterSeek, p->afterSeek);
    CD_SWAP(streaming, p->streaming);
    CD_SWAP(mode, p->mode);
    CD_SWAP(sizeBits, p->sizeBits);
    CD_SWAP(filterFile, p->filterFile);
    CD_SWAP(filterChannel, p->filterChannel);
    CD_SWAP(muted, p->muted);
    CD_SWAP(setlocTarget, p->setlocTarget);
    CD_SWAP(setlocPending, p->setlocPending);
    CD_SWAP(headLba, p->headLba);
    CD_SWAP(seekTarget, p->seekTarget);
    CD_SWAP(lastReadLba, p->lastReadLba);
    CD_SWAP(lastHeader, p->lastHeader);
    CD_SWAP(haveLastHeader, p->haveLastHeader);
    CD_SWAP(sector, p->sector);
    CD_SWAP(fifoStart, p->fifoStart);
    CD_SWAP(fifoSize, p->fifoSize);
    CD_SWAP(fifoPos, p->fifoPos);
    CD_SWAP(lastAckAt, p->lastAckAt);
    CD_SWAP(replayDone, p->replayDone);
    CD_SWAP(replayFirst, p->replayFirst);
    CD_SWAP(inWorld, p->inWorld);
}

/* ---- save states (system/savestate.h) ---- */

/* The drive: its timeline (absolute virtual times; eventSeq goes on, so a
 * stale queued cookie never meets a new event), modes, motor and lid, the
 * head and the sector FIFO. The disc in it is not saved: savestate.c checks
 * the state's disc number and puts that image in first (cdSwapForReset). */
void driveStateIo(XgState* s) {
    double speed = speedFactor;
    XG_STATE_VAR(s, events);
    XG_STATE_VAR(s, eventCount);
    XG_STATE_VAR(s, eventSeq);
    XG_STATE_VAR(s, speed);
    XG_STATE_VAR(s, motorOn);
    XG_STATE_VAR(s, paused);
    XG_STATE_VAR(s, shellOpen);
    XG_STATE_VAR(s, shellLatch);
    XG_STATE_VAR(s, activity);
    XG_STATE_VAR(s, afterSeek);
    XG_STATE_VAR(s, streaming);
    XG_STATE_VAR(s, mode);
    XG_STATE_VAR(s, sizeBits);
    XG_STATE_VAR(s, filterFile);
    XG_STATE_VAR(s, filterChannel);
    XG_STATE_VAR(s, muted);
    XG_STATE_VAR(s, setlocTarget);
    XG_STATE_VAR(s, setlocPending);
    XG_STATE_VAR(s, headLba);
    XG_STATE_VAR(s, seekTarget);
    XG_STATE_VAR(s, lastReadLba);
    XG_STATE_VAR(s, lastHeader);
    XG_STATE_VAR(s, haveLastHeader);
    XG_STATE_VAR(s, sector);
    XG_STATE_VAR(s, fifoStart);
    XG_STATE_VAR(s, fifoSize);
    XG_STATE_VAR(s, fifoPos);
    XG_STATE_VAR(s, lastAckAt);
    XG_STATE_VAR(s, replayDone);
    XG_STATE_VAR(s, replayFirst);
    if (xgStateLoading(s) && speed != speedFactor) {
        fprintf(stderr, "cd: the state was saved at XG_CD_SPEED %g, this run has %g: its timing differs\n", speed,
                speedFactor);
    }
}
