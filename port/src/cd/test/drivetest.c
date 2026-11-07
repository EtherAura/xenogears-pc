/*
 * The CD drive's timing model (port/src/cd/drive.c) on a synthetic disc and a
 * stepped clock. Expected times come from psx-spx "CDROM - Response Timings"
 * (GetID, Stop, the motor-off acknowledge, the INT1 rate) and "CDROM - Read
 * Commands" (Setloc, Read, Pause), and from measurements of Beetle PSX as a
 * black box (first responses, seeks, Pause, Init: work/clean-room/measure/cd),
 * as each check says: the model's own formulas exactly, and the measured
 * values within their spread.
 *
 *   cmake --build <build> --target cd_drivetest && <build>/cd_drivetest
 *
 * The disc image (drivetest.bin/.cue) is written to the current directory.
 */

#include "cd/cd_internal.h"
#include "system/hw.h"
#include "system/speedup.h"
#include "test/env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#include "test/check.h"

#define CYCLES(n) ((double)(n) / HW_CPU_HZ)
#define SECTOR_1X (1.0 / 75.0)  /* psx-spx: SystemClock * 930h / 4 / 44100 cycles */
#define SECTOR_2X (1.0 / 150.0) /* half that at double speed */
#define CLOSE(a, b) (fabs((a) - (b)) < 1e-9)

/* ---- a stepped clock and its event queue (system/hw.h) ---- */

static double clockNow;
static struct {
    double at;
    HwEvent fn;
    void* arg;
} queue[256];
static int queued;

double hwNow(void) {
    return clockNow;
}

void hwScheduleAt(double at, HwEvent fn, void* arg) {
    int i = queued++;
    while (i > 0 && queue[i - 1].at > at) {
        queue[i] = queue[i - 1];
        i--;
    }
    queue[i].at = at;
    queue[i].fn = fn;
    queue[i].arg = arg;
}

/* Run the clock to `t`, delivering every event at its own time. */
static void advanceTo(double t) {
    while (queued > 0 && queue[0].at <= t) {
        HwEvent fn = queue[0].fn;
        void* arg = queue[0].arg;
        if (queue[0].at > clockNow) {
            clockNow = queue[0].at;
        }
        queued--;
        memmove(&queue[0], &queue[1], sizeof queue[0] * (size_t)queued);
        fn(arg);
    }
    if (t > clockNow) {
        clockNow = t;
    }
}

/* ---- what the drive calls ---- */

static struct {
    double at;
    int type;
    uint8_t r[8];
    int lba; /* the sector in the FIFO, for INT1 */
    uint32_t dataHash;
} ints[512];
static int intCount;
static int xaCount;
static int xaLast[64];
static double xaAt[64];

void cdInterrupt(int type, const uint8_t* response, int count) {
    if (intCount < 512) {
        ints[intCount].at = clockNow;
        ints[intCount].type = type;
        memset(ints[intCount].r, 0, 8);
        memcpy(ints[intCount].r, response, (size_t)(count < 8 ? count : 8));
        const uint8_t* s = driveCurrentSector();
        int m = (s[12] >> 4) * 10 + (s[12] & 15), sec = (s[13] >> 4) * 10 + (s[13] & 15);
        int f = (s[14] >> 4) * 10 + (s[14] & 15);
        ints[intCount].lba = (m * 60 + sec) * 75 + f - 150;
        if (type == CDI_DATAREADY) {
            uint8_t data[2048];
            driveReadFifo(data, sizeof(data));
            uint32_t hash = 2166136261u;
            for (int i = 0; i < 2048; i++) hash = (hash ^ data[i]) * 16777619u;
            ints[intCount].dataHash = hash;
        }
        intCount++;
    }
}

static int readyCallback; /* a ready callback is set: the replay may hold sectors */
int cdReadyCallbackSet(void) {
    return readyCallback;
}

void xaSubmitSector(const uint8_t* raw) {
    int m = (raw[12] >> 4) * 10 + (raw[12] & 15), s = (raw[13] >> 4) * 10 + (raw[13] & 15);
    int f = (raw[14] >> 4) * 10 + (raw[14] & 15);
    if (xaCount < 64) {
        xaLast[xaCount] = (m * 60 + s) * 75 + f - 150;
        xaAt[xaCount] = clockNow;
    }
    xaCount++;
}

void cdSwapBytes(void* a, void* b, size_t n) {
    unsigned char* x = a;
    unsigned char* y = b;
    for (size_t i = 0; i < n; i++) {
        unsigned char t = x[i];
        x[i] = y[i];
        y[i] = t;
    }
}
void cdSwapInit(const char* firstDisc) {
    (void)firstDisc;
}
void cdSwapNoteCommand(uint8_t com) {
    (void)com;
}
double slowdownCdCommand(int command) {
    (void)command;
    return -1;
}
void slowdownCdRead(int command, int32_t lba) {
    (void)command;
    (void)lba;
}
double slowdownCdFirst(void) {
    return -1;
}
/* The replay's hold (testReplayHold): sector holdLba no sooner than holdUntil. */
static int holdLba = -1;
static double holdUntil;
double slowdownCdHold(double at, int32_t lba) {
    return lba == holdLba && holdUntil > at ? holdUntil : at;
}

/* ---- the disc: 6000 mode 2 sectors; 200..263 XA audio (file 1, channel 0 on
 * even sectors, channel 1 on odd ones), the rest data. Only the sectors the
 * tests read are written (0..399 and two places far out for long seeks); the
 * file is sparse elsewhere. ---- */

#define DISC_SECTORS 6000
#define FAR_A 3000 /* long seeks land here */
#define FAR_B 5800

static void writeSector(FILE* f, int lba) {
    static uint8_t raw[CD_RAW_SECTOR];
    memset(raw, 0, sizeof raw);
    discWriteHeader(raw, lba, 2);
    uint8_t* sub = raw + 16;
    if (lba >= 200 && lba < 264) {
        sub[0] = sub[4] = 1;
        sub[1] = sub[5] = (uint8_t)(lba & 1);
        sub[2] = sub[6] = 0x64; /* realtime, form 2, audio */
        sub[3] = sub[7] = 0x01; /* stereo, 37.8 kHz, 4-bit */
    } else {
        sub[2] = sub[6] = 0x08; /* data */
        for (int i = 0; i < 2048; i++) raw[24 + i] = (uint8_t)(lba * 13 + i * 17 + i / 7);
    }
    fseek(f, (long)lba * CD_RAW_SECTOR, SEEK_SET);
    fwrite(raw, sizeof raw, 1, f);
}

static int writeDisc(void) {
    FILE* f = fopen("drivetest.bin", "wb");
    if (f == NULL) {
        return 0;
    }
    for (int lba = 0; lba < 400; lba++) {
        writeSector(f, lba);
    }
    for (int lba = 0; lba < 40; lba++) {
        writeSector(f, FAR_A + lba);
        writeSector(f, FAR_B + lba);
    }
    writeSector(f, DISC_SECTORS - 1);
    fclose(f);
    f = fopen("drivetest.cue", "w");
    if (f == NULL) {
        return 0;
    }
    fprintf(f, "FILE \"drivetest.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    fclose(f);
    return 1;
}

/* ---- helpers ---- */

static double cmd(uint8_t com, const uint8_t* p, int n) {
    double t = clockNow;
    driveCommand(com, p, n);
    return t;
}

static double setloc(int lba) {
    int a = lba + 150;
    uint8_t p[3] = {(uint8_t)((a / 4500 / 10) << 4 | a / 4500 % 10), (uint8_t)((a / 75 % 60 / 10) << 4 | a / 75 % 60 % 10),
                    (uint8_t)((a % 75 / 10) << 4 | a % 75 % 10)};
    return cmd(CDC_SETLOC, p, 3);
}

static void setmode(uint8_t m) {
    cmd(CDC_SETMODE, &m, 1);
    advanceTo(clockNow + 0.01);
}

/* The first interrupt of `type` at or after `from`, or -1. */
static int findInt(int type, double from) {
    for (int i = 0; i < intCount; i++) {
        if (ints[i].type == type && ints[i].at >= from - 1e-12) {
            return i;
        }
    }
    return -1;
}

/* The last sector delivered so far. */
static int lastDelivered(void) {
    for (int k = intCount - 1; k >= 0; k--) {
        if (ints[k].type == CDI_DATAREADY) {
            return ints[k].lba;
        }
    }
    return -1;
}

/* ---- the expected times ----
 *
 * Measured with Beetle PSX 0.9.44 as a black box (work/clean-room/measure/cd:
 * probe.c on the emulated console, the workloads of experiments.py, fits in
 * fit.py, work/agents/CD_TIMING/NOTES.md), CPU cycles from the command write,
 * double speed. The drive model (drive.c) is the port's own fit to them;
 * these checks hold it to the formulas exactly and to the measured Beetle
 * values within their spread. */

#define P2 SECTOR_2X
#define ACK0 CYCLES(22300)                       /* Nop: Beetle 21760..23536 */
#define ACK_PARAM CYCLES(1650)                   /* Setloc (3 bytes): Beetle 26432..28216 */
#define SEEK_BASE CYCLES(59000)
#define SEEK_LONG CYCLES(10194133)
#define PER_SECTOR CYCLES(104.5)

/* Setloc + ReadN to the first INT1, from a Pause (paused) or while reading. */
static double readLatency(int distance, int paused) {
    double seek;
    if (distance >= 2250) {
        seek = SEEK_LONG + P2 + PER_SECTOR * distance;
    } else {
        seek = SEEK_BASE + (paused ? 6.465 : 0.96) * P2 + (distance > 192 ? PER_SECTOR * (distance - 192) : 0);
    }
    return seek + 2 * P2;
}

/* A measured Beetle time (cycles), within `spread` cycles. */
static int nearBeetle(double t, double cycles, double spread) {
    return fabs(t - CYCLES(cycles)) <= CYCLES(spread);
}

/* ---- tests ---- */

/* First responses: 22300 cycles + 1650 per parameter byte (Beetle: Nop
 * ~23000, Setmode ~24350, Setloc ~27250; spread +-1500); with the motor off
 * psx-spx's 0x5CF4. Init: its second response 1135700 cycles after the
 * acknowledge (Beetle: INT2 1158700 after the write, idle or reading). */
static void testAcknowledge(void) {
    intCount = 0;
    double t = cmd(CDC_NOP, NULL, 0);
    advanceTo(t + 0.01);
    int i = findInt(CDI_ACKNOWLEDGE, t);
    EXPECTF(i >= 0 && CLOSE(ints[i].at - t, ACK0), "Nop: %f", i >= 0 ? ints[i].at - t : -1);
    EXPECTF(i >= 0 && nearBeetle(ints[i].at - t, 23000, 1500), "Nop vs Beetle");

    t = setloc(100);
    advanceTo(t + 0.01);
    i = findInt(CDI_ACKNOWLEDGE, t);
    EXPECTF(i >= 0 && CLOSE(ints[i].at - t, ACK0 + 3 * ACK_PARAM), "Setloc acknowledge");
    EXPECTF(i >= 0 && nearBeetle(ints[i].at - t, 27250, 1500), "Setloc vs Beetle");

    t = cmd(CDC_INIT, NULL, 0);
    advanceTo(t + 0.5);
    i = findInt(CDI_ACKNOWLEDGE, t);
    EXPECTF(i >= 0 && CLOSE(ints[i].at - t, ACK0), "Init acknowledge");
    int j = findInt(CDI_COMPLETE, t);
    EXPECTF(j >= 0 && CLOSE(ints[j].at - ints[i].at, CYCLES(1135700)), "Init complete");
    EXPECTF(j >= 0 && nearBeetle(ints[j].at - t, 1158700, 2000), "Init vs Beetle");
}

/* psx-spx "Second Response": GetID 0x4A00 after its acknowledge; Stop
 * 0x18A6076 at double speed, 0x1D7B when already stopped; a Nop while stopped
 * acknowledges after 0x5CF4. */
static void testSecondResponses(void) {
    intCount = 0;
    double t = cmd(CDC_GETID, NULL, 0);
    advanceTo(t + 0.1);
    int a = findInt(CDI_ACKNOWLEDGE, t), c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && c >= 0 && CLOSE(ints[c].at - ints[a].at, CYCLES(0x4A00)), "GetID");
#ifdef VERSION_JP
    EXPECTF(c >= 0 && memcmp(ints[c].r + 4, "SCEI", 4) == 0, "GetID region");
#else
    EXPECTF(c >= 0 && memcmp(ints[c].r + 4, "SCEA", 4) == 0, "GetID region");
#endif

    setmode(0x80);
    t = cmd(CDC_STOP, NULL, 0);
    advanceTo(t + 2.0);
    a = findInt(CDI_ACKNOWLEDGE, t);
    c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && c >= 0 && CLOSE(ints[c].at - ints[a].at, CYCLES(0x18A6076)), "Stop at double speed");
    t = cmd(CDC_STOP, NULL, 0);
    advanceTo(t + 0.1);
    a = findInt(CDI_ACKNOWLEDGE, t);
    c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && CLOSE(ints[a].at - t, CYCLES(0x5CF4)), "acknowledge while stopped");
    EXPECTF(c >= 0 && CLOSE(ints[c].at - ints[a].at, CYCLES(0x1D7B)), "Stop when stopped");

    /* Bring the motor back: MotorOn, a spin-up (the port's own 1 s). */
    t = cmd(CDC_MOTORON, NULL, 0);
    advanceTo(t + 1.5);
    c = findInt(CDI_COMPLETE, t);
    EXPECTF(c >= 0, "MotorOn completes");
}

/* Reading at double speed. After Setloc + ReadN from a Pause (here a Stop:
 * the head at 0) the first sector comes 45000 cycles + 6.465 sector periods
 * (a seek of up to 192 sectors) + 2 periods later (Beetle: 1970600 +-15000,
 * the same for 0..192 sectors), then one every 1/150 s exactly (psx-spx "INT1
 * Rate": SystemClock * 930h / 4 / 44100 / 2 cycles). Pause while reading
 * completes P * (4.98 + lba / 23853) after its acknowledge (Beetle: 1146958 +
 * 9.4659 * lba cycles after the write), 5000 cycles when already paused
 * (Beetle: ~27000 after the write). A Read without a new Setloc resumes at the
 * most recently received sector (psx-spx "Setloc, Read, Pause"; Beetle
 * delivers the one before it, the game never does this). */
static void testRead(void) {
    setmode(0x80);
    intCount = 0;
    double t = setloc(50);
    advanceTo(t + 0.01);
    t = cmd(CDC_READN, NULL, 0);
    double first = t + readLatency(50, 1);
    advanceTo(first + 9.5 * P2);
    int i = findInt(CDI_DATAREADY, t);
    EXPECTF(i >= 0 && CLOSE(ints[i].at, first) && ints[i].lba == 50, "first INT1: %f (want %f) sector %d",
          i >= 0 ? ints[i].at : -1, first, i >= 0 ? ints[i].lba : -1);
    EXPECTF(i >= 0 && nearBeetle(ints[i].at - t, 1970600, 15000), "first INT1 vs Beetle: %f",
          i >= 0 ? (ints[i].at - t) * HW_CPU_HZ : -1);
    int n = 0;
    for (int k = i; k >= 0 && k < intCount; k++) {
        if (ints[k].type == CDI_DATAREADY) {
            EXPECTF(CLOSE(ints[k].at, first + n * P2) && ints[k].lba == 50 + n, "INT1 %d at %f sector %d", n,
                  ints[k].at, ints[k].lba);
            n++;
        }
    }
    EXPECTF(n == 10, "ten sectors (%d)", n);

    /* GetlocP while reading: the last delivered sector (absolute 59 + 150). */
    t = cmd(CDC_GETLOCP, NULL, 0);
    advanceTo(t + ACK0 + 1e-6);
    int g = findInt(CDI_ACKNOWLEDGE, t);
    EXPECTF(g >= 0 && ints[g].r[7] == 0x59 && ints[g].r[6] == 0x02, "GetlocP %02x:%02x", g >= 0 ? ints[g].r[6] : 0,
          g >= 0 ? ints[g].r[7] : 0);

    int lastSector = lastDelivered();
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.2);
    int a = findInt(CDI_ACKNOWLEDGE, t), c = findInt(CDI_COMPLETE, t);
    double pause = P2 * (4.98 + (lastSector + 1) / 23853.0);
    EXPECTF(a >= 0 && c >= 0 && CLOSE(ints[c].at - ints[a].at, pause), "Pause while reading");
    EXPECTF(c >= 0 && nearBeetle(ints[c].at - t, 1146958 + 9.4659 * lastSector, 3000), "Pause vs Beetle");
    EXPECTF(findInt(CDI_DATAREADY, t + ACK0) < 0, "no sector after the Pause");
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.1);
    a = findInt(CDI_ACKNOWLEDGE, t);
    c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && c >= 0 && CLOSE(ints[c].at - ints[a].at, CYCLES(5000)), "Pause when paused");
    EXPECTF(c >= 0 && nearBeetle(ints[c].at - t, 27000, 2000), "Pause when paused vs Beetle");

    t = cmd(CDC_READN, NULL, 0);
    advanceTo(t + 0.2);
    i = findInt(CDI_DATAREADY, t);
    EXPECTF(i >= 0 && ints[i].lba == lastSector, "resumed at the last received sector (%d, want %d)",
          i >= 0 ? ints[i].lba : -1, lastSector);
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.2);
}

/* Seeks (Beetle, Setloc + ReadN from a Pause to the first INT1): position and
 * direction independent; up to 2250 sectors 1970600 + 104.0 * (d - 192),
 * from 2250 on 10873259 + 104.52 * d (a step of ~9 M cycles at 30 s of
 * disc); while reading, short ones lose the Pause's ~1.24 M (Beetle ~730000
 * to the first INT1); SeekL completes 2 periods sooner than ReadN's first
 * sector; Pause while seeking takes the target's place. */
static void testSeeks(void) {
    setmode(0x80);
    struct {
        int from, to;
        double beetle; /* measured cycles to the first INT1 */
    } cases[] = {
        {100, 101, 1970600},                                     /* the next sector */
        {30, 330, 1970600 + 104.0 * (330 - 31 - 192)},           /* 299 sectors */
        {330, FAR_A, 10873259 + 104.52 * (FAR_A - 331)},         /* long */
        {FAR_A + 10, FAR_B, 10873259 + 104.52 * (FAR_B - FAR_A - 11)},
        {FAR_B + 10, 20, 10873259 + 104.52 * (FAR_B + 11 - 20)}, /* backwards */
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        double t = setloc(cases[c].from);
        advanceTo(t + 0.01);
        t = cmd(CDC_READN, NULL, 0);
        while (lastDelivered() != cases[c].from && clockNow < t + 2.0) {
            advanceTo(clockNow + 0.001); /* the first sector, then pause */
        }
        t = cmd(CDC_PAUSE, NULL, 0);
        advanceTo(t + 0.5);
        int head = lastDelivered() + 1;
        intCount = 0;
        t = setloc(cases[c].to);
        advanceTo(t + 0.01);
        t = cmd(CDC_READN, NULL, 0);
        advanceTo(t + 2.0);
        int i = findInt(CDI_DATAREADY, t);
        int d = abs(cases[c].to - head);
        EXPECTF(i >= 0 && CLOSE(ints[i].at - t, readLatency(d, 1)) && ints[i].lba == cases[c].to,
              "seek %d -> %d: %f (want %f) sector %d", head, cases[c].to, i >= 0 ? ints[i].at - t : -1,
              readLatency(d, 1), i >= 0 ? ints[i].lba : -1);
        EXPECTF(i >= 0 && nearBeetle(ints[i].at - t, cases[c].beetle, 16000), "seek %d -> %d vs Beetle: %f cycles",
              head, cases[c].to, i >= 0 ? (ints[i].at - t) * HW_CPU_HZ : -1);
        t = cmd(CDC_PAUSE, NULL, 0);
        advanceTo(t + 0.5);
    }

    /* While reading (no Pause): Setloc + ReadN 10 sectors ahead (Beetle 726512..739064). */
    double t = setloc(100);
    advanceTo(t + 0.01);
    t = cmd(CDC_READN, NULL, 0);
    advanceTo(t + 0.1);
    int head = lastDelivered() + 1;
    intCount = 0;
    t = setloc(head + 10);
    advanceTo(t + ACK0 + 3 * ACK_PARAM);
    t = cmd(CDC_READN, NULL, 0);
    advanceTo(t + 0.1);
    int i = findInt(CDI_DATAREADY, t);
    EXPECTF(i >= 0 && ints[i].lba == head + 10 && nearBeetle(ints[i].at - t, 733300, 16000),
          "seek while reading: %f cycles, sector %d", i >= 0 ? (ints[i].at - t) * HW_CPU_HZ : -1,
          i >= 0 ? ints[i].lba : -1);
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.5);

    /* SeekL from a Pause: INT2 two periods before a read would deliver. */
    head = lastDelivered() + 1;
    intCount = 0;
    t = setloc(FAR_A);
    advanceTo(t + 0.01);
    t = cmd(CDC_SEEKL, NULL, 0);
    advanceTo(t + 1.0);
    int c = findInt(CDI_COMPLETE, t);
    int d = abs(FAR_A - head);
    EXPECTF(c >= 0 && CLOSE(ints[c].at - t, readLatency(d, 1) - 2 * P2), "SeekL: %f", c >= 0 ? ints[c].at - t : -1);
    EXPECTF(c >= 0 && nearBeetle(ints[c].at - t, 10419925 + 104.54 * d, 16000), "SeekL vs Beetle");

    /* Pause while seeking: as at the seek target. */
    t = setloc(FAR_B);
    advanceTo(t + 0.01);
    t = cmd(CDC_READN, NULL, 0);
    advanceTo(t + 0.05);
    intCount = 0;
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.5);
    int a = findInt(CDI_ACKNOWLEDGE, t);
    c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && (ints[a].r[0] & CDS_SEEK) && c >= 0 && CLOSE(ints[c].at - ints[a].at, P2 * (4.98 + FAR_B / 23853.0)),
          "Pause while seeking");
    EXPECTF(findInt(CDI_DATAREADY, t) < 0, "no sector after a Pause while seeking");
}

/* GetlocP while seeking reports the seek target (the port's model; psx-spx
 * "GetlocP" works during seeks). */
static void testGetlocPSeeking(void) {
    intCount = 0;
    double t = setloc(300);
    advanceTo(t + 0.01);
    t = cmd(CDC_SEEKL, NULL, 0);
    advanceTo(t + 0.002);
    double g = cmd(CDC_GETLOCP, NULL, 0);
    advanceTo(g + 0.01);
    int i = findInt(CDI_ACKNOWLEDGE, g);
    int abs = 300 + 150; /* 00:06:00 */
    EXPECTF(i >= 0 && ints[i].r[5] == 0 && ints[i].r[6] == 0x06 && ints[i].r[7] == 0x00, "GetlocP %d -> %02x:%02x:%02x",
          abs, i >= 0 ? ints[i].r[5] : 0, i >= 0 ? ints[i].r[6] : 0, i >= 0 ? ints[i].r[7] : 0);
    advanceTo(t + 1.0);
    int c = findInt(CDI_COMPLETE, t);
    EXPECTF(c >= 0, "SeekL completes");
}

/* psx-spx "Data/ADPCM Sector Filtering/Delivery": with Setmode bit 6 (XA-ADPCM)
 * and bit 3 (filter), realtime audio sectors of the filtered file and channel
 * go to the ADPCM decoder, others are skipped, neither raises INT1; data
 * sectors still do. */
static void testXaRouting(void) {
    setmode(0xC8);
    uint8_t filt[2] = {1, 0};
    cmd(CDC_SETFILTER, filt, 2);
    advanceTo(clockNow + 0.01);
    intCount = 0;
    xaCount = 0;
    double t = setloc(196);
    advanceTo(t + 0.01);
    t = cmd(CDC_READS, NULL, 0);
    advanceTo(t + 1.0);
    int data = 0, xaInts = 0;
    for (int k = 0; k < intCount; k++) {
        if (ints[k].type == CDI_DATAREADY) {
            if (ints[k].lba >= 200 && ints[k].lba < 264) {
                xaInts++;
            } else {
                data++;
            }
        }
    }
    EXPECTF(xaInts == 0 && data >= 4, "no INT1 for XA audio (%d), data sectors do (%d)", xaInts, data);
    EXPECTF(xaCount == 32 && xaLast[0] == 200 && xaLast[1] == 202, "channel 0 sectors to the decoder: %d (%d %d)",
          xaCount, xaLast[0], xaLast[1]);
    for (int i = 1; i < xaCount && i < 64; i++)
        EXPECTF(CLOSE(xaAt[i] - xaAt[i - 1], 2 * P2), "XA audio retains its sector clock");
    cmd(CDC_PAUSE, NULL, 0);
    advanceTo(clockNow + 0.2);
}

/* The slowdown replay holds a sector back to the PS1's time. Held by less
 * than half a period (the PS1 delivered it late), the next one keeps its
 * place on the drive's clock; held longer (the PC wrote the read sooner than
 * the PS1), the next one comes no sooner than half a period after it, not at
 * once (the worldmap stream's re-seek from its ready callback got a sector
 * the PS1 never delivered). */
static void testReplayHold(void) {
    setmode(0x80);
    readyCallback = 1;
    intCount = 0;
    double t = setloc(60);
    advanceTo(t + 0.01);
    t = cmd(CDC_READN, NULL, 0);
    double first = t + readLatency(60, 1);
    holdLba = 60;
    holdUntil = first + 10 * P2;
    advanceTo(holdUntil + 0.5 * P2 + 1e-6);
    int i = findInt(CDI_DATAREADY, t);
    EXPECTF(i >= 0 && ints[i].lba == 60 && CLOSE(ints[i].at, holdUntil), "held first sector at %f (want %f)",
          i >= 0 ? ints[i].at : -1, holdUntil);
    int j = findInt(CDI_DATAREADY, holdUntil + 1e-9);
    EXPECTF(j >= 0 && ints[j].lba == 61 && CLOSE(ints[j].at, holdUntil + 0.5 * P2), "next sector at %f (want %f)",
          j >= 0 ? ints[j].at : -1, holdUntil + 0.5 * P2);
    /* Then a hold of a third of a period: the one after it keeps its place. */
    double due = holdUntil + 1.5 * P2; /* sector 62 */
    holdLba = 62;
    holdUntil = due + P2 / 3;
    advanceTo(due + 2.5 * P2);
    i = findInt(CDI_DATAREADY, due);
    EXPECTF(i >= 0 && ints[i].lba == 62 && CLOSE(ints[i].at, holdUntil), "short hold at %f", i >= 0 ? ints[i].at : -1);
    j = findInt(CDI_DATAREADY, holdUntil + 1e-9);
    EXPECTF(j >= 0 && ints[j].lba == 63 && CLOSE(ints[j].at, due + P2), "after a short hold at %f (want %f)",
          j >= 0 ? ints[j].at : -1, due + P2);
    holdLba = -1;
    readyCallback = 0;
    t = cmd(CDC_PAUSE, NULL, 0);
    advanceTo(t + 0.2);
}

/* Compare the same real FIFO payloads, event ordering and stream clocks,
 * not merely a helper returning a larger multiplier. Reset to the same
 * stopped head before each load so seek latency is comparable. */
typedef struct { double first, period, pause; int count; uint32_t hashes[16]; } Load;

static void loadSample(int fast, uint8_t m, uint8_t command, Load* out) {
    speedupSetEnabled(0);
    cmd(CDC_STOP, NULL, 0); advanceTo(clockNow + 2);
    cmd(CDC_MOTORON, NULL, 0); advanceTo(clockNow + 1.5);
    setmode(m); setloc(FAR_A); advanceTo(clockNow + 0.01);
    intCount = 0; speedupSetEnabled(fast);
    double t = cmd(command, NULL, 0), first = -1, previous = -1;
    memset(out, 0, sizeof(*out));
    int seen = 0;
    while (out->count < 16 && clockNow < t + 2) {
        advanceTo(clockNow + 1e-5);
        while (seen < intCount) {
            int i = seen++;
            if (ints[i].type != CDI_DATAREADY) continue;
            int n = out->count++;
            EXPECTF(ints[i].lba == FAR_A + n, "Speedup keeps sector ordering");
            if (n < 16) out->hashes[n] = ints[i].dataHash;
            if (n == 0) first = ints[i].at;
            if (n == 1) out->period = ints[i].at - previous;
            previous = ints[i].at;
        }
    }
    EXPECTF(out->count == 16, "load completes with all sectors");
    out->first = first - t;
    t = cmd(CDC_PAUSE, NULL, 0); advanceTo(t + 0.5);
    int a = findInt(CDI_ACKNOWLEDGE, t), c = findInt(CDI_COMPLETE, t);
    EXPECTF(a >= 0 && c >= 0, "Pause completes after load");
    if (a >= 0 && c >= 0) out->pause = ints[c].at - ints[a].at;
}

static void testSpeedup(void) {
    Load original, fast, restored, stream, fastStream, xa, fastXa;
    loadSample(0, 0x80, CDC_READN, &original);
    loadSample(1, 0x80, CDC_READN, &fast);
    EXPECTF(CLOSE(original.first, fast.first * 8), "ordinary data seek/start 8x faster");
    EXPECTF(CLOSE(original.period, fast.period * 8), "ordinary data sectors 8x faster");
    EXPECTF(CLOSE(original.pause, fast.pause * 8), "ordinary data Pause 8x faster");
    EXPECTF(!memcmp(original.hashes, fast.hashes, sizeof(original.hashes)), "identical data bytes at both speeds");
    loadSample(0, 0x80, CDC_READN, &restored);
    EXPECTF(CLOSE(original.first, restored.first) && CLOSE(original.period, restored.period), "disabled restores native timing");
    loadSample(0, 0x80, CDC_READS, &stream);
    loadSample(1, 0x80, CDC_READS, &fastStream);
    EXPECTF(CLOSE(stream.first, fastStream.first) && CLOSE(stream.period, fastStream.period) &&
            CLOSE(stream.pause, fastStream.pause), "ReadS terrain/movie stream timing unchanged");
    EXPECTF(!memcmp(stream.hashes, fastStream.hashes, sizeof(stream.hashes)), "ReadS bytes unchanged");
    loadSample(0, 0xC0, CDC_READN, &xa);
    loadSample(1, 0xC0, CDC_READN, &fastXa);
    EXPECTF(CLOSE(xa.first, fastXa.first) && CLOSE(xa.period, fastXa.period) && CLOSE(xa.pause, fastXa.pause),
            "XA-enabled ReadN timing unchanged");
    testXaRouting(); /* also route actual audio with Speedup enabled */

    /* An in-flight sector keeps its deadline. Switching at either speed
     * affects the next interval without dropping/duplicating sectors. */
    for (int startFast = 0; startFast < 2; startFast++) {
        loadSample(startFast, 0x80, CDC_READN, &fast);
        setloc(100); advanceTo(clockNow + 0.01);
        intCount = 0; double t = cmd(CDC_READN, NULL, 0);
        while (findInt(CDI_DATAREADY, t) < 0 && clockNow < t + 2) advanceTo(clockNow + 1e-5);
        int i = findInt(CDI_DATAREADY, t);
        EXPECTF(i >= 0 && ints[i].lba == 100, "live toggle preserves queued read");
        if (i >= 0) {
            speedupSetEnabled(!startFast);
            double next = ints[i].at + P2 / (startFast ? 8 : 1);
            advanceTo(next + 1e-9);
            int j = findInt(CDI_DATAREADY, ints[i].at + 1e-9);
            EXPECTF(j >= 0 && ints[j].lba == 101 && CLOSE(ints[j].at, next), "queued sector keeps its deadline");
            if (j >= 0) {
                next += P2 / (startFast ? 1 : 8);
                advanceTo(next + 1e-9);
                int k = findInt(CDI_DATAREADY, ints[j].at + 1e-9);
                EXPECTF(k >= 0 && ints[k].lba == 102 && CLOSE(ints[k].at, next), "following sector uses selected timing");
            }
        }
        cmd(CDC_PAUSE, NULL, 0); advanceTo(clockNow + 0.5);
    }
    speedupSetEnabled(0);
}

int main(void) {
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("XG_CD_SPEED", "1");
    testSetEnv("XG_SPEEDUP", "0");
    if (!writeDisc() || !cdOpenDisc("drivetest.cue")) {
        printf("cannot write the test disc\n");
        return 1;
    }
    testAcknowledge();
    testSecondResponses();
    testRead();
    testSeeks();
    testGetlocPSeeking();
    testXaRouting();
    testReplayHold();
    testSpeedup();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
