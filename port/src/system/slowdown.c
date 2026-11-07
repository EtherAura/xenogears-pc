/*
 * Slowdown replay, an option for validation runs (XG_SLOWDOWN=file), and the
 * log it replays (XG_SLOWDOWN_LOG=file writes the port's own).
 *
 * The port runs at full rate: on the stepped clock game code takes no time,
 * while the PS1 is CPU-bound in heavy scenes and its frames take extra
 * vblanks (the opening battle drops about 21), and its loads take as long as
 * the CD and the decompression do. The replay makes the PC take the time the
 * PS1 took, as far as the game can see it:
 *
 *   - its VSync() calls: VSync(-1)'s count and where a VSync(n) wait starts
 *     pace its frames and its catch-up logic (battle: m8DAC_frameRate);
 *   - its CD sectors: a load is done when its last sector reaches the ready
 *     callback.
 *
 * Then the PC's frames line up one for one with a PCSX-Redux capture of the
 * same input, at a constant offset.
 *
 * The log (tools/redux_capture.py --vsync-log writes it from Redux):
 *
 *   b <vblank.fraction>                kernelMain installs its vsync callback
 *                                      (logged from power-on, whatever the
 *                                      capture's first vblank)
 *   m <mode> <vblank>                  bootGame starts game mode <mode>
 *   v <arg> <vblank> [<last> <count>]  the game called VSync(<arg>) when
 *                                      <vblank> vblanks had passed (with the
 *                                      fraction of the frame); calls in a row
 *                                      with the same argument -1 or 1 (a busy
 *                                      wait) are one line: the first's
 *                                      vblank, the last's, how many (older
 *                                      logs merge waiting calls too)
 *   c <command> <lba> <vblank.fraction>  a CD read (ReadN 6, ReadS 27) from
 *                                      sector <lba> (-1: no Setloc before it),
 *                                      that far into the frame
 *   r <status> <vblank.fraction>       the CD interrupt called the ready
 *                                      callback (status 1: a data sector of
 *                                      the last read)
 *   k <command> <vblank.fraction>      LIBCD wrote a command to the drive
 *   s <status> <vblank.fraction>       the CD interrupt completed the last
 *                                      command (its acknowledge, or the
 *                                      second response of Pause, SeekL...)
 *   p <vblank>                         the BIOS pad/card handler did not
 *                                      serve that vblank (a CD interrupt was
 *                                      still being handled: Beetle PSX logs;
 *                                      the pad buffer and the card driver's
 *                                      turn stay as they were)
 *
 * Vblanks count from power-on in Redux and from startup here: only
 * differences matter. Calls the PsyQ SDK makes itself (DrawSync's timeouts
 * poll VSync(-1)) are not in it: the port's libraries make none. '#' starts a
 * comment.
 *
 * Replay: at a mode start the PC lines its clock up with the log's (the first
 * one sets the offset between the two; later ones wait out the PS1's longer
 * loads). With a b record the first mode start also waits out the PS1's
 * longer boot, so it comes as many vblanks after the vsync callback started
 * as on the PS1: its counters (play time, g_playTimeInVsync) then agree with
 * the log's. (VSync(-1) counts from ResetCallback, before the card driver's
 * start, which takes 147 vblanks on the PS1 and none here.)
 *
 * Then each run of VSync calls with one argument is matched with the log's
 * next run with that argument (within the mode, a few lines ahead at most),
 * and each waiting call (VSync(0), VSync(n)) with its own line; if the PC
 * reaches it sooner than the PS1 did, the PC lets the missing vblanks pass
 * first, delivering their interrupts as the PS1 did while it computed (a PC
 * later than the log moves the offset, but not at VSync(1), which only reads
 * the time and can come from a callback). The call itself then comes at its
 * time within the frame (the log's fraction): what the PS1 delivered before
 * it, the PC delivers before it too, so the code after the call sees it (US
 * field 287: a Pause done at x.023, between the frame's VSync(1) at .008 and
 * its VSync(-1) at .042, which the scripts after the latter saw). A vblank
 * the PS1 took in the middle of a computation passes at the same share of it
 * on the PC (see planMidFrame), but not in the field. In battles the whole span of
 * work from a call's return to the log's next call takes the PS1's time (see
 * spreadSafePoint), so a CD completion or a vblank inside it comes at its share.
 *
 * Each CD read is matched with the log's next read from the same sector in
 * the mode (reads the PC does not make are passed over; while the replay is
 * lined up the PC makes the PS1's overlay reads too, unless XG_OVERLAY_CD=0:
 * overlay_cd.c), and the drive holds each of its data sectors back until the
 * time the PS1 delivered the same one; a read's first sector comes when the
 * PS1's did even if the PC wrote the read later (slowdownCdFirst: the drive's
 * seeks take as long as the PS1's, so a PC that issues a read a little late
 * would deliver all of it late). A PC that is later than the log within a
 * read moves the offset instead. Which sector a log line is follows from its
 * time: the sectors pass the head at a steady rate, so the gaps between a
 * read's lines, in sector periods, count the sectors between them. A place
 * without a line is an XA sector, or a sector the PS1 delivered but the log
 * missed: Beetle PSX logs (tools/beetle.py) lose a ready callback that the
 * hooked copy of a frame runs past the frame's end (most at .97 of a frame),
 * and Beetle still shows every movie frame of those streams; the PC delivers
 * it at its own time. (Skipping such sectors as lost dropped movie frames:
 * JP's logo movie lost frame 176, flipped its double-buffer parity and made
 * the PC wait two VSync(0)s more at the movie's end.)
 *
 * Each command takes as long as the PS1's (its k to s): it is matched with
 * the log's next command of that code (from the matched read on), and the
 * drive completes it that long after it was written. PCSX-Redux answers
 * commands much sooner than the drive model (Pause in 7000 cycles where
 * psx-spx measured five sectors), and a load whose Pause completes after the
 * next frame starts makes the game wait one frame more. The PC does a span of
 * work between two VSync calls at once, where the PS1 takes CPU time: a
 * completion the PS1 saw inside such a span, the PC completes at the span's
 * start (seenDone has the cases the log shows). Every mode is marked, the
 * kernel menu's and the menu's too (kernel.c's PC bootGame).
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

#define LOOKAHEAD 8   /* log runs skipped at most to find the PC's argument */
#define MAX_HOLD 10.0 /* seconds: a sector held longer means the two runs went apart */
#define COMMAND_LOOKAHEAD 16 /* log commands skipped at most to find the PC's */
/* vblanks: a longer completion is not taken from the log. Beetle PSX logs
 * (tools/beetle.py) have Pause while reading at 4-6.7 vblanks and SeekL at
 * about 41; PCSX-Redux answered within 2. */
#define MAX_COMMAND 60.0

typedef struct {
    char kind;     /* 'm' or 'v' */
    int32_t arg;   /* mode, or VSync's argument */
    int64_t at;    /* vblank of the first call */
    double atFrac; /* with the fraction of the frame (logs without one: .0) */
    int32_t calls; /* calls in the run */
    int64_t last;  /* vblank of the last call */
    double ret;    /* when the main thread went on after it (a wait: the vblank it returned at) */
} Record;

typedef struct {
    int32_t lba;
    int after;   /* records (m, v) before it in the log: where it is in the mode */
    int first;   /* its sectors: sectorTimes[first .. first + count) */
    int count;
    int command; /* its k record in commands[], or -1 */
    int stream;  /* ReadS (27): a stream the game takes as it comes; ReadN (6): data */
} Read;

typedef struct {
    int32_t com;
    int after;     /* records (m, v) before it */
    double issued; /* vblank.fraction of the k record */
    double done;   /* of the s record that completed it, or -1 */
    int doneAfter; /* records (m, v) before that s record */
} Command;

static Record* recs;
static int recCount;
static double* sectorTimes; /* each data sector's vblank, with the fraction of the frame */
static int* sectorPos;      /* and its place in its read: sectors after the read's first */
static int sectorCount;
static Read* reads;
static int readCount;
static Command* commands;
static int commandCount;
static int pos;          /* next record to match */
static int readPos;      /* next read to match */
static int commandPos;   /* next command to match */
static int curRead = -1; /* the log's read the drive's current one is matched with */
static int curSector;    /* data sectors of it delivered */
static int sectorMatched; /* the sector being delivered has a log line */
static int active;       /* a mode start has been matched */
static int64_t offset;   /* log vblank - PC vblank */
static double logBoot = -1; /* the log's b record */
static int64_t pcBoot = -1; /* the vblank the game installed its vsync callback here */
static int inRun;        /* the PC's current run of VSync calls */
static int32_t runArg;
static int runRec = -1;  /* the log record it is matched with */
static int runCalls;     /* its calls so far */
static long stalled, late, late1, unmatched, matched, held, sectorsDone, commandsTimed;

/* XG_SLOWDOWN_LOG */
static FILE* out;
static int outRun;
static int32_t outArg;
static uint32_t outFirst, outLast, outCount;
static double outFirstFrac;

/* Vblanks that came in the middle of a computation on the PS1: after a VSync
 * call that returns at once (VSync(-1), VSync(1)) the log's next call is
 * vblanks later. The PS1 ran the vsync callback (pads, play time) there, and
 * code after it in the frame saw it (battleRender reads the pad after its
 * sprite lists). The PC lets each one pass at the same share of the safe
 * points it makes between the two calls (counted the last time it made the
 * same pair of calls), instead of at the next call. */
#define MAX_MID 4
static int midCount;            /* vblanks to let pass in this computation */
static int midNext;
static int64_t midDue[MAX_MID]; /* each one's vblank number here */
static long midAt[MAX_MID];     /* at this many safe points since the call */
static long midSince = -1;      /* safe points since a VSync(-1)/VSync(1) call, or -1 */
static int32_t midArg;          /* that call's argument */
static long midEstimate[16];    /* safe points between two calls, per argument pair (0: not seen) */
static int inMid;
static long midPassed;

static int curMode = -1; /* the mode the PC is in (slowdownModeEntry) */

/* Battles: the work between a VSync call's return and the log's next call takes
 * the time the PS1 took, spread over the PC's safe points in it (as many as the
 * last time after the same call site with the same next argument), up to
 * SPREAD_MARGIN of a vblank before that call. So a CD completion the PS1 delivered
 * inside its frame's work reaches the tasks that polled after it on the PS1 (US
 * battle c_v2s0: the battle loader's intro task polls isCDBusy() once a frame, the
 * Pause done at 2297.217 inside the PS1's work .030-.722; the PC did all of its
 * work at .000-.021 and saw it a frame late), and a vblank comes at its share.
 * The margin stands for the end of the span the PS1 spent waiting for its GPU
 * (DrawSync before the next call), which has no safe points on the PC. A span
 * seen for the first time has no estimate and is not spread. On the replay
 * scenario set (129 scenarios against Beetle) a margin of .1: 8 battles line up
 * better (c_v2s0 1723 -> 2650 equal pictures, g_v15s11 1700 -> 3216), 2 by 3 and
 * 25 pictures worse, both after their first unexplained divergence; none, .01,
 * .05, .2 and .3 do worse, and so does spreading in every mode (fields: their
 * safe points are mostly rendering's GTE and sprite calls, planMidFrame's note). */
#define SPREAD_SLOTS 1024
#define SPREAD_MARGIN 0.1
static struct {
    const void* site;
    int32_t nextArg;
    long count; /* safe points from the call's return to the next call, last time */
} spreadTab[SPREAD_SLOTS];
static int spreadSlot = -1;
static long spreadSince = -1; /* safe points since the span began, or -1 */
static long spreadEstimate;
static double spreadStart, spreadEnd; /* the span, on the PC's clock */
static int inSpread;
static long spreadMoves;

static int spreads(void) {
    return curMode == 2;
}

/* The next VSync call ends the span: remember its length. */
static void spreadDone(void) {
    if (spreadSince >= 0 && spreadSlot >= 0) {
        spreadTab[spreadSlot].count = spreadSince;
    }
    spreadSince = -1;
    spreadSlot = -1;
}

static void spreadSafePoint(void) {
    if (spreadSince < 0 || inSpread) {
        return;
    }
    spreadSince++;
    if (spreadEstimate <= 0) {
        return;
    }
    double share = (double)spreadSince / (double)spreadEstimate;
    double target = spreadStart + (spreadEnd - spreadStart) * (share < 1 ? share : 1);
    double now = hwNow();
    if (target > now) {
        inSpread = 1;
        spreadMoves++;
        hwBusy(target - now);
        inSpread = 0;
    }
}

static int64_t* biosMissed; /* p records: vblanks the BIOS pad/card handler did not serve */
static int biosMissedCount, biosMissedPos;
static long biosSkipped;
static long movedDone;   /* CD completions moved to the span where the PS1 saw them */

static void flushRun(void) {
    if (!outRun) {
        return;
    }
    if (outCount == 1) {
        fprintf(out, "v %d %.3f\n", outArg, outFirstFrac);
    } else {
        fprintf(out, "v %d %.3f %u %u\n", outArg, outFirstFrac, outLast, outCount);
    }
    outRun = 0;
}

static void* grow(void* p, int* cap, int count, size_t size) {
    if (count < *cap) {
        return p;
    }
    *cap = *cap ? *cap * 2 : 4096;
    return realloc(p, (size_t)*cap * size);
}

static int compareVblanks(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return x < y ? -1 : x > y;
}

static int compareDoubles(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

/* Each logged sector's place in its read. The sectors pass the head at a
 * steady rate, so the gap between two lines, in sector periods (the read's
 * typical gap), counts the places between them: about 1, 2 or more where
 * sectors went to the ADPCM decoder or were lost. Gaps stay within 0.3 of a
 * whole number; a gap under half a period (the PS1 ran the ready callback
 * twice, Beetle PSX in movie streams) keeps the place. Each gap is rounded on
 * its own, so the sector clock's drift never adds up. */
static void placeSectors(void) {
    sectorPos = realloc(sectorPos, (size_t)(sectorCount > 0 ? sectorCount : 1) * sizeof(*sectorPos));
    for (int i = 0; i < readCount; i++) {
        const Read* r = &reads[i];
        const double* t = sectorTimes + r->first;
        int* pos = sectorPos + r->first;
        if (r->count <= 0) {
            continue;
        }
        double period = 0;
        if (r->count >= 3) {
            double* gaps = malloc((size_t)(r->count - 1) * sizeof(*gaps));
            for (int k = 1; k < r->count; k++) {
                gaps[k - 1] = t[k] - t[k - 1];
            }
            qsort(gaps, (size_t)(r->count - 1), sizeof(*gaps), compareDoubles);
            period = gaps[(r->count - 1) / 2]; /* the typical gap: one sector */
            free(gaps);
        }
        pos[0] = 0;
        for (int k = 1; k < r->count; k++) {
            int step = period > 0 ? (int)floor((t[k] - t[k - 1]) / period + 0.5) : 1;
            pos[k] = pos[k - 1] + (step < 0 ? 0 : step);
        }
    }
}

static void loadLog(const char* path) {
    FILE* f = fopen(path, "r");
    char line[256];
    int cap = 0, scap = 0, rcap = 0, kcap = 0, pcap = 0;
    int64_t waitReturn = -1; /* when the log's last waiting VSync call returned */
    int dropped = 0;

    if (f == NULL) {
        fprintf(stderr, "slowdown: cannot open XG_SLOWDOWN %s\n", path);
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char kind;
        long arg;
        double at;
        long lba;
        if (sscanf(line, " p %lf", &at) == 1) {
            biosMissed = grow(biosMissed, &pcap, biosMissedCount, sizeof(*biosMissed));
            biosMissed[biosMissedCount++] = (int64_t)at;
            continue;
        }
        if (sscanf(line, " b %lf", &at) == 1) {
            if (logBoot < 0) {
                logBoot = at;
            }
            continue;
        }
        if (sscanf(line, " %c %ld %lf", &kind, &arg, &at) != 3) {
            continue;
        }
        if (kind == 'c') {
            if (sscanf(line, " %c %ld %ld %lf", &kind, &arg, &lba, &at) != 4) {
                continue;
            }
            reads = grow(reads, &rcap, readCount, sizeof(*reads));
            if (readCount > 0) {
                reads[readCount - 1].count = sectorCount - reads[readCount - 1].first;
            }
            /* Its k record comes just before it (logs without them: none). */
            int k = commandCount > 0 && commands[commandCount - 1].com == arg ? commandCount - 1 : -1;
            reads[readCount++] = (Read){(int32_t)lba, recCount, sectorCount, 0, k, arg == 27};
        } else if (kind == 'k') {
            commands = grow(commands, &kcap, commandCount, sizeof(*commands));
            commands[commandCount++] = (Command){(int32_t)arg, recCount, at, -1, -1};
        } else if (kind == 's') {
            /* The drive takes one command at a time: it completes the last. */
            if (commandCount > 0 && commands[commandCount - 1].done < 0) {
                commands[commandCount - 1].done = at;
                commands[commandCount - 1].doneAfter = recCount;
            }
        } else if (kind == 'r') {
            if (arg == 1 && readCount > 0) { /* data end, errors: not counted */
                sectorTimes = grow(sectorTimes, &scap, sectorCount, sizeof(*sectorTimes));
                sectorTimes[sectorCount++] = at;
            }
        } else if (kind == 'm' || kind == 'v') {
            double last;
            long calls = 1;
            if (kind != 'v' || sscanf(line, " %c %ld %lf %lf %ld", &kind, &arg, &at, &last, &calls) != 5) {
                calls = 1;
            }
            /* A run's last call: its vblank only; one in the run's first vblank is no
             * sooner than the first (US battle: VSync(1) twice around DrawSync at x.312). */
            double ret = calls > 1 && last > floor(at) ? last : at;
            if (kind == 'v' && (arg == 0 || arg > 1)) {
                /* LIBETC's VSync(n) waits until n - 1 vblanks after the last
                 * VSync(0)/VSync(n) returned, then for the next vblank
                 * (src/main/psyq/libetc/vsync.c): a waiting call logged
                 * before the one before it can have returned is that one
                 * again. Beetle PSX logs (tools/beetle.py) rarely have a call
                 * from the end of a frame recorded by the hooked copies of
                 * both frames (US worldmap area_c7: VSync(2) at 1925.970,
                 * which returns at 1927, and at 1926.004; area_c7_gear
                 * 2005.970 and 2006.004, where Beetle's RAM has LIBETC's
                 * last-return count move once, 2005 -> 2007). */
                if (waitReturn >= 0 && at < (double)waitReturn) {
                    dropped++;
                    continue;
                }
                int64_t vb = calls > 1 ? (int64_t)last : (int64_t)at;
                int64_t target = arg == 0 || waitReturn < 0 ? vb : waitReturn + arg - 1;
                waitReturn = (vb > target ? vb : target) + 1;
                ret = (double)waitReturn;
            }
            recs = grow(recs, &cap, recCount, sizeof(*recs));
            recs[recCount++] = (Record){kind, (int32_t)arg, (int64_t)at, at, (int32_t)calls,
                                        calls > 1 ? (int64_t)last : (int64_t)at, ret};
        }
    }
    if (readCount > 0) {
        reads[readCount - 1].count = sectorCount - reads[readCount - 1].first;
    }
    fclose(f);
    if (biosMissedCount > 0) { /* their lines come one frame late */
        qsort(biosMissed, (size_t)biosMissedCount, sizeof(*biosMissed), compareVblanks);
    }
    placeSectors();
    fprintf(stderr, "slowdown: %d records, %d CD reads (%d sectors), %d CD commands from %s\n", recCount, readCount,
            sectorCount, commandCount, path);
    if (dropped > 0) {
        fprintf(stderr, "slowdown: %d waiting VSync line(s) dropped (made while the call before still waited)\n",
                dropped);
    }
}

static void finish(void) {
    if (out != NULL) {
        flushRun();
        fclose(out);
        out = NULL;
    }
    if (active) {
        fprintf(stderr, "slowdown: %ld vblanks added, %ld runs matched, %ld late, %ld unmatched; %ld CD sectors, "
                "%ld held back, %ld commands timed; %ld vblanks mid-frame\n", stalled,
                matched, late, unmatched, sectorsDone, held, commandsTimed, midPassed);
        if (late1 > 0) {
            fprintf(stderr, "slowdown: %ld VSync(1) runs reached late (time reads: the offset stays)\n", late1);
        }
        if (biosSkipped > 0) {
            fprintf(stderr, "slowdown: %ld vblanks without the BIOS pad/card handler, as on the PS1\n", biosSkipped);
        }
        if (spreadMoves > 0) {
            fprintf(stderr, "slowdown: battle work spread over its spans (%ld clock moves)\n", spreadMoves);
        }
        if (movedDone > 0) {
            fprintf(stderr, "slowdown: %ld CD completions moved to the start of the span the PS1 saw them in\n",
                    movedDone);
        }
    }
}

static void initOnce(void) {
    static int done;

    if (done) {
        return;
    }
    done = 1;
    const char* replay = getenv("XG_SLOWDOWN");
    const char* log = getenv("XG_SLOWDOWN_LOG");
    if (replay != NULL && replay[0] != '\0') {
        loadLog(replay);
    }
    if (log != NULL && log[0] != '\0') {
        out = fopen(log, "w");
        if (out == NULL) {
            fprintf(stderr, "slowdown: cannot write XG_SLOWDOWN_LOG %s\n", log);
        } else {
            fprintf(out, "# xenogears VSync log 1 (port; vblanks from startup)\n");
        }
    }
    atexit(finish);
}

/* Closer than this to the vblank (seconds, more than a stepped safe point's
 * cost in hw.c), a wait's own safe point would deliver it and then wait for the
 * next one: a battle span's spread can end there (US field 412's encounter: a
 * span ended 0.7 us before vblank 1129 and every later call came a vblank late). */
#define STALL_NEAR 2e-6

/* Let n vblanks pass, as the PS1's CPU time would have. */
static void stall(int64_t n) {
    stalled += n;
    while (n-- > 0) {
        uint32_t before = hwVSyncCount();
        double due = hwVBlankToTime((double)before + 1) - hwNow();
        if (due > 0 && due < STALL_NEAR) {
            hwBusy(due + 1e-9);
            if (hwVSyncCount() != before) {
                continue;
            }
        }
        if (hwWaitVSync() == before) {
            return; /* inside a callback or with interrupts masked: none can come */
        }
    }
}

static int argClass(int32_t arg) {
    return arg < 0 ? 0 : arg <= 2 ? arg + 1 : 3;
}

/* A VSync call that returns at once was matched with log record j: plan the
 * vblanks the PS1 took before its next call. */
static void planMidFrame(int j) {
    midCount = midNext = 0;
    midSince = -1;
    if (spreads()) {
        return; /* the span's spread brings mid-frame vblanks at their share */
    }
    if (curMode == 1) {
        /* Not in the field: its frame reads the pad from its input ring at the start
         * (a mid-frame sample counts next frame anyway), and its safe points come
         * mostly from the rendering's GTE and sprite calls, so the share put the vblank,
         * and a CD completion due before it, ahead of scripts the PS1 had run before
         * them (fields 59, 72, 83, 131, 192, 224, 246, 247, 285, 430, 610, 656, 674,
         * 710-713, disc 2 296, 672-674 equal to Beetle without; the battle needs it:
         * battleRender reads the pad late in its work). */
        return;
    }
    const Record* a = &recs[j];
    if ((a->arg != -1 && a->arg != 1) || a->calls != 1 || j + 1 >= recCount || recs[j + 1].kind != 'v') {
        return; /* VSync(0) and VSync(n) wait for theirs; busy-wait runs poll through them */
    }
    const Record* b = &recs[j + 1];
    midSince = 0;
    midArg = a->arg;
    long estimate = midEstimate[argClass(a->arg) * 4 + argClass(b->arg)];
    double span = b->atFrac - a->atFrac;
    for (int64_t v = a->at + 1; v <= b->at && midCount < MAX_MID && estimate > 0 && span > 0; v++) {
        midDue[midCount] = v - offset;
        midAt[midCount] = (long)(((double)v - a->atFrac) / span * (double)estimate);
        midCount++;
    }
}

/* The next VSync call ends the computation: remember its length. */
static void endMidFrame(int32_t arg) {
    if (midSince >= 0) {
        midEstimate[argClass(midArg) * 4 + argClass(arg)] = midSince;
    }
    midSince = -1;
    midCount = 0;
}

void slowdownSafePoint(void) {
    spreadSafePoint();
    if (midSince < 0 || inMid) {
        return;
    }
    midSince++;
    while (midNext < midCount && midSince >= midAt[midNext]) {
        int64_t due = midDue[midNext++];
        if ((int64_t)hwVSyncCount() < due) {
            inMid = 1;
            stall(due - (int64_t)hwVSyncCount());
            inMid = 0;
            midPassed++;
        }
    }
}

void slowdownModeEntry(int mode) {
    initOnce();
    curMode = mode;
    inRun = 0;
    midSince = -1;
    midCount = 0;
    int found = 0;
    for (int j = pos; j < recCount; j++) {
        if (recs[j].kind != 'm') {
            continue;
        }
        if (recs[j].arg != mode) {
            if (!active) {
                break; /* the replay starts at the log's first mode start only */
            }
            continue;
        }
        uint32_t now = hwVSyncCount();
        if (!active && logBoot >= 0 && pcBoot >= 0) {
            /* The PS1's boot took longer: wait it out before the mode starts. */
            int64_t want = pcBoot + (recs[j].at - (int64_t)logBoot);
            if ((int64_t)now < want) {
                fprintf(stderr, "slowdown: boot lined up with the log's vsync callback start (%lld vblanks added)\n",
                        (long long)(want - (int64_t)now));
                stall(want - (int64_t)now);
                now = hwVSyncCount();
            }
        }
        if (!active) {
            offset = recs[j].at - (int64_t)now;
            active = 1;
            fprintf(stderr, "slowdown: replaying from mode %d (log vblank %lld = vblank %u here)\n", mode,
                    (long long)recs[j].at, now);
        } else {
            /* The PS1's load took longer: wait it out before the mode starts. */
            int64_t want = recs[j].at - offset;
            if ((int64_t)now < want) {
                stall(want - (int64_t)now);
            } else {
                offset = recs[j].at - (int64_t)now;
            }
        }
        pos = j + 1;
        readPos = 0;
        while (readPos < readCount && reads[readPos].after <= j) {
            readPos++;
        }
        commandPos = 0;
        while (commandPos < commandCount && commands[commandPos].after <= j) {
            commandPos++;
        }
        curRead = -1;
        found = 1;
        break;
    }
    if (active && !found) {
        fprintf(stderr, "slowdown: mode %d is not in the log any more: the replay ends\n", mode);
        finish();
        active = 0;
        recCount = readCount = commandCount = 0;
        curRead = -1;
    }
    if (out != NULL) {
        flushRun();
        fprintf(out, "m %d %u\n", mode, hwVSyncCount());
        fflush(out);
    }
}

/* VSync(0) and VSync(n > 1) wait for vblanks: each call is a step of the
 * PS1's timing. A load's loop of VSync(2) calls took 173 vblanks for 86 calls
 * in Redux where the PC's own waits took 169, so each call is lined up, not
 * only a run's first. */
static int waits(int32_t arg) {
    return arg == 0 || arg > 1;
}

void slowdownVSync(int32_t arg) {
    initOnce();
    int first = !(inRun && runArg == arg);
    /* Logs list each waiting call on its own line: the next call of the PC's
     * run is the log's next line (the line just after, or none). */
    int next = !first && waits(arg) && runRec >= 0 && runCalls >= recs[runRec].calls;
    if (first) {
        endMidFrame(arg);
        runRec = -1;
    }
    spreadDone();
    if (active && (first || next)) {
        /* Line the call up with the log's. */
        uint32_t now = hwVSyncCount();
        int j = pos, k = 0;
        while (j < recCount && recs[j].kind == 'v' && k < (next ? 0 : LOOKAHEAD) && recs[j].arg != arg) {
            j++;
            k++;
        }
        if (j < recCount && recs[j].kind == 'v' && recs[j].arg == arg) {
            int64_t want = recs[j].at - offset;
            if ((int64_t)now < want) {
                stall(want - (int64_t)now);
            } else if ((int64_t)now > want && arg == 1) {
                /* VSync(1) only reads the time. Battling calls it from its DrawSync callback
                 * (func_80088C00), which the PS1 runs when its GPU finishes, often after the
                 * frame's VSync(0) and next to the next frame's own VSync(1): reaching that
                 * run a vblank "late" says nothing about the PC's pacing, so the offset stays
                 * (moving it drifted battling 5 vblanks every 86). The next waiting or
                 * VSync(-1) call lines up as usual. */
                late1++;
            } else if ((int64_t)now > want) {
                offset = recs[j].at - (int64_t)now;
                late++;
            }
            if ((int64_t)hwVSyncCount() == recs[j].at - offset) {
                double at = hwVBlankToTime(recs[j].atFrac - (double)offset);
                if (at > hwNow()) {
                    hwBusy(at - hwNow());
                }
            }
            matched++;
            pos = j + 1;
            runRec = j;
            runCalls = 0;
            planMidFrame(j);
        } else {
            unmatched++;
            runRec = -1;
        }
    } else if (active && runRec >= 0 && waits(arg) && runCalls < recs[runRec].calls) {
        /* Older logs merge a run of waiting calls into one line: spread its
         * calls evenly from the first's vblank to the last's. */
        const Record* r = &recs[runRec];
        int64_t want = r->at + (r->last - r->at) * runCalls / (r->calls - 1) - offset;
        uint32_t now = hwVSyncCount();
        if ((int64_t)now < want) {
            stall(want - (int64_t)now);
        }
    }
    if (runCalls < INT_MAX) { /* counts calls in a run; without a log it would overflow */
        runCalls++;
    }
    inRun = 1;
    runArg = arg;
    if (out != NULL) {
        uint32_t now = hwVSyncCount();
        if (outRun && outArg == arg && !waits(arg)) {
            outLast = now;
            outCount++;
        } else {
            flushRun();
            outRun = 1;
            outArg = arg;
            outFirst = outLast = now;
            outFirstFrac = hwVBlankNow();
            outCount = 1;
        }
    }
}

/* After a matched waiting call or a single VSync(-1)/VSync(1) (a busy-wait run's
 * polls are no work), in battles: the span up to the log's next call. */
void slowdownVSyncReturn(const void* site) {
    if (!active || !spreads() || runRec < 0) {
        return;
    }
    int j = runRec;
    if ((recs[j].arg == -1 || recs[j].arg == 1) && recs[j].calls != 1) {
        return;
    }
    if (j + 1 >= recCount || recs[j + 1].kind != 'v') {
        return;
    }
    double end = hwVBlankToTime(recs[j + 1].atFrac - SPREAD_MARGIN - (double)offset);
    double now = hwNow();
    if (end <= now) {
        return;
    }
    uintptr_t h = (uintptr_t)site * 2654435761u ^ (uintptr_t)(uint32_t)recs[j + 1].arg;
    int slot = (int)(h % SPREAD_SLOTS);
    for (int k = 0; k < SPREAD_SLOTS; k++, slot = (slot + 1) % SPREAD_SLOTS) {
        if (spreadTab[slot].site == NULL) {
            spreadTab[slot].site = site;
            spreadTab[slot].nextArg = recs[j + 1].arg;
            spreadTab[slot].count = 0;
            break;
        }
        if (spreadTab[slot].site == site && spreadTab[slot].nextArg == recs[j + 1].arg) {
            break;
        }
    }
    spreadSlot = slot;
    spreadEstimate = spreadTab[slot].count;
    spreadStart = now;
    spreadEnd = end;
    spreadSince = 0;
}

void slowdownCdRead(int command, int32_t lba) {
    initOnce();
    curRead = -1;
    curSector = 0;
    if (out != NULL) {
        fprintf(out, "c %d %d %.3f\n", command, lba, hwVBlankNow());
    }
    if (!active) {
        return;
    }
    /* The next read from that sector in this mode (the log's next mode start
     * ends it); reads the PC does not make (overlay loads) are passed over. */
    int end = pos;
    while (end < recCount && recs[end].kind != 'm') {
        end++;
    }
    for (int i = readPos; i < readCount && reads[i].after <= end; i++) {
        if (reads[i].lba == lba) {
            curRead = i;
            readPos = i + 1;
            if (reads[i].command >= 0) {
                commandPos = reads[i].command + 1; /* the commands that follow it are the PC's next */
            }
            return;
        }
    }
}

/* The PS1's time for the matched read's first sector (the one at its Setloc
 * position), or -1. */
double slowdownCdFirst(void) {
    if (!active || curRead < 0 || reads[curRead].lba < 0 || reads[curRead].count <= 0 ||
        sectorPos[reads[curRead].first] != 0) {
        return -1;
    }
    return hwVBlankToTime(sectorTimes[reads[curRead].first] - (double)offset);
}

/* Records by time around a hint (older logs wrote a waiting call with the next
 * one, and a busy-wait run's line comes at its first call's time). */
static int recordBefore(double t, int hint) {
    int best = -1;
    for (int r = hint - 64 < 0 ? 0 : hint - 64; r < recCount && r < hint + 64; r++) {
        if (recs[r].atFrac <= t && (best < 0 || recs[r].atFrac >= recs[best].atFrac)) {
            best = r;
        }
    }
    return best;
}

static int recordAfter(double t, int hint) {
    int best = -1;
    for (int r = hint - 64 < 0 ? 0 : hint - 64; r < recCount && r < hint + 64; r++) {
        if (recs[r].atFrac > t && (best < 0 || recs[r].atFrac < recs[best].atFrac)) {
            best = r;
        }
    }
    return best;
}

static int recordBetween(double a, double b, int hint) {
    for (int r = hint - 64 < 0 ? 0 : hint - 64; r < recCount && r < hint + 64; r++) {
        if (recs[r].atFrac > a && recs[r].atFrac <= b) {
            return 1;
        }
    }
    return 0;
}

/* Where command i's completion belongs on the PC (the log's time of the start of
 * the span of work the PS1 saw it in), or -1: its own time. */
static double seenDone(int i) {
    const Command* c = &commands[i];
    if (c->doneAfter <= 0) {
        return -1;
    }
    int b = recordBefore(c->done, c->doneAfter);
    if (b < 0 || recs[b].kind != 'v' || recs[b].ret > c->done || floor(recs[b].ret) != floor(c->done) ||
        recs[b].ret <= c->issued) {
        return -1; /* the span began before the command, or in another frame: the PC's is the same */
    }
    /* The PS1 wrote its next command later in the same span (no VSync call between,
     * same frame, not straight from the callback): it saw the completion there
     * (battles: Pause done, then Setloc .15 later in the frame's work). */
    if (i + 1 < commandCount && commands[i + 1].issued >= c->done + 0.01 &&
        floor(commands[i + 1].issued) == floor(c->done) &&
        !recordBetween(c->done, commands[i + 1].issued, c->doneAfter)) {
        return recs[b].ret;
    }
    /* A wait loop the completion ended: it came after the loop's waiting call
     * returned, and the PS1's next call in that frame is another one (US field
     * transitions, f_80078B5C.c `wait: ...; DrawSync; VSync(2); <GPU calls>; if
     * (isCDBusy()) goto wait;`: Pause done at x.019-.030, VSync(0) at x.19; the PS1's
     * check came after its GPU calls, the PC's at once). */
    int n = recordAfter(c->done, c->doneAfter);
    if (waits(recs[b].arg) && n >= 0 && recs[n].kind == 'v' && recs[n].at == (int64_t)floor(c->done) &&
        recs[n].arg != recs[b].arg) {
        return recs[b].ret;
    }
    /* Not every completion inside a span: where the PS1 polls in it differs (the
     * battle's tasks: moving every battle completion to its frame's start made five
     * battles of 23 line up and five others go apart). */
    return -1;
}

double slowdownCdCommand(int command) {
    initOnce();
    if (out != NULL) {
        fprintf(out, "k %d %.3f\n", command, hwVBlankNow());
    }
    if (!active) {
        return -1;
    }
    int end = pos;
    while (end < recCount && recs[end].kind != 'm') {
        end++;
    }
    for (int i = commandPos, k = 0; i < commandCount && commands[i].after <= end && k < COMMAND_LOOKAHEAD; i++, k++) {
        if (commands[i].com == command) {
            commandPos = i + 1;
            double took = commands[i].done - commands[i].issued;
            if (commands[i].done < 0 || took < 0 || took > MAX_COMMAND) {
                return -1;
            }
            commandsTimed++;
            double at = seenDone(i);
            if (at >= 0) {
                double t = hwVBlankToTime(at - (double)offset) - hwNow();
                double min = 0.002 * hwVSyncPeriod();
                movedDone++;
                return t > min ? t : min;
            }
            return took * hwVSyncPeriod();
        }
    }
    return -1;
}

void slowdownCdComplete(int status) {
    if (out != NULL) {
        fprintf(out, "s %d %.3f\n", status, hwVBlankNow());
    }
}

/* The log's line for the drive's current sector: by its place in the read when
 * the read starts at a known sector, else the next one in order; -1: none. */
static int sectorLine(int32_t lba) {
    const Read* r = &reads[curRead];
    if (r->lba >= 0 && lba >= r->lba) {
        int place = lba - r->lba;
        for (int k = curSector; k < r->count; k++) {
            if (sectorPos[r->first + k] == place) {
                return r->first + k;
            }
            if (sectorPos[r->first + k] > place) {
                return -1;
            }
        }
        return -1;
    }
    return curSector < r->count ? r->first + curSector : -1;
}

double slowdownCdHold(double at, int32_t lba) {
    sectorMatched = 0;
    if (!active || curRead < 0) {
        return at;
    }
    int line = sectorLine(lba);
    if (line < 0) {
        return at;
    }
    sectorMatched = 1;
    curSector = line - reads[curRead].first;
    double t = hwVBlankToTime(sectorTimes[line] - (double)offset);
    if (t > at + 1e-6 && t - at < MAX_HOLD) {
        held++;
        return t;
    }
    return at;
}

void slowdownCdDelivered(void) {
    sectorsDone++;
    /* A sector without a line (the log missed its callback) leaves the next
     * search where it was. */
    if (sectorMatched || curRead < 0 || reads[curRead].lba < 0) {
        curSector++;
    }
    sectorMatched = 0;
    if (out != NULL) {
        fprintf(out, "r 1 %.3f\n", hwVBlankNow());
    }
}

void slowdownBoot(void) {
    initOnce();
    if (pcBoot < 0) {
        pcBoot = hwVSyncCount();
        if (out != NULL) {
            fprintf(out, "b %.3f\n", hwVBlankNow());
        }
    }
}

/* Whether the PS1's BIOS pad/card handler skipped this vblank (the log's p
 * records); the pad (pad.c) and the card driver (card/bu.c) ask at the vblank. */
int slowdownBiosMissed(void) {
    static int64_t askedAt = INT64_MIN;
    static int missed;
    int64_t now;
    if (!slowdownLogVBlank(&now)) {
        return 0;
    }
    if (now != askedAt) {
        askedAt = now;
        while (biosMissedPos < biosMissedCount && biosMissed[biosMissedPos] < now) {
            biosMissedPos++;
        }
        missed = biosMissedPos < biosMissedCount && biosMissed[biosMissedPos] == now;
        biosSkipped += missed;
    }
    return missed;
}

int slowdownLogVBlank(int64_t* vblank) {
    if (!active) {
        return 0;
    }
    *vblank = (int64_t)hwVSyncCount() + offset;
    return 1;
}
