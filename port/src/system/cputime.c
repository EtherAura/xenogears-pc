/*
 * XG_CPU_TIMING (xg-htn.41.1): where the game thread's time goes, vblank by
 * vblank. Instrumentation only: nothing the game sees changes.
 *
 *   XG_CPU_TIMING=1        a line per vblank on stderr, and the summary at exit
 *   XG_CPU_TIMING=summary  the summary, and the line of each slow vblank: over
 *                          two frames, or with an audio gap (dry, cut, starved)
 *
 * A line: "cpu: vb <n> wall <ms> cpu <ms> | game <ms> gp0 .. spans .. cache ..
 * spu .. present .. gpuwait .. state .. spin .. idle .. | late <ms> dry <n>", the times since
 * the last vblank:
 *   wall     the time between this vblank and the last (the host's clock)
 *   cpu      the game thread's CPU time (the operating system's count)
 *   gp0 ...  the parts the hooks time (cputime.h), each exclusive
 *   game     cpu less the parts that run on the CPU (gp0, spans, cache, spu,
 *            present, state): the game's own code, LIBGPU/LIBCD/the GTE, the rest
 *   state    save-state and rewind captures (savestate.c, rewind.c)
 *   spin     vsync_wait=spin: the busy wait for the next event (hw.c)
 *   bkeep .. the 60 Hz Battling's picture between (between.c): its VRAM
 *            keep, memory records, sandbox render and draw, VRAM put back,
 *            and the bytes its memory records (bmemb) and VRAM keep and put
 *            back (vramb) copied
 *   idle     wall less cpu less gpuwait: sleeping for the next vblank (the
 *            frame was ready early), or descheduled
 *   late     how long after its due time the vblank was delivered (hw.c's
 *            clock; real time: a game thread that reached no safe point)
 *   dry      audio pushes since the last vblank that found the device's queue
 *            empty (port/src/spu/output.c: an audible gap, real time only)
 * The summary at exit: the mean and the worst of each, the vblanks delivered
 * more than 1 ms and more than a frame late, and every dry push.
 *
 * Without the variable the settings menu's performance overlay and log
 * (port/src/ui/diagnostics.cpp) run the same timing (cpuTimingRequest) and
 * take each vblank's numbers (cpuTimingSetListener): nothing on stderr then.
 */

#define _GNU_SOURCE 1 /* pread, syscall, O_CLOEXEC (a thread's run-queue wait) */
#include "cputime.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __linux__
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

/* Weak: the GPU layer's test programs have neither the system clock (their
 * test clock) nor the audio output. */
extern double hwNow(void) __attribute__((weak));
extern double hwVBlankToTime(double vblank) __attribute__((weak));
extern uint32_t hwVSyncCount(void) __attribute__((weak));
extern int spuOutputDryPushes(void) __attribute__((weak));
extern int spuOutputCuts(void) __attribute__((weak));
extern int spuOutputStarved(void) __attribute__((weak));

int cpuTimingOn;
static int requests;                                  /* cpuTimingRequest's users */
static void (*listener)(const CpuTimingFrame* frame); /* cpuTimingSetListener */

static const char* const names[CPU_KINDS] = {"gp0",   "spans", "cache", "spu",  "present", "gpuwait", "state",
                                             "spin",  "load",  "bkeep", "bmem", "bdraw",   "bput"};

static struct {
    int checked, lines, env;
    uint64_t acc[CPU_KINDS]; /* ns since the last vblank */
    uint64_t bytes[2];       /* the between's memory and VRAM bytes copied since the last vblank */
    int stack[16], depth;
    uint64_t since;          /* when the innermost running part last started or resumed */
    uint64_t lastWall, lastCpu, lastRunq;
    int lastDry, lastCut, lastStarved;
    /* the run */
    uint32_t vblanks;
    double sum[CPU_KINDS + 4], worst[CPU_KINDS + 4]; /* the kinds, then wall, cpu, game, idle */
    uint32_t late1ms, lateFrame;
    double lateWorst;
    int dry, cut, starved;
} t;

static uint64_t now(void) {
    return SDL_GetTicksNS();
}

/* The calling thread's CPU time, in ns. */
/* Nanoseconds the game thread has waited runnable for a CPU (Linux schedstat: its second
 * field), so 'idle' splits into waiting to run (other processes held the cores: runq) and
 * blocked (disk, locks, the device). 0 elsewhere. */
static uint64_t threadRunq(void) {
#ifdef __linux__
    static int fd = -2;
    if (fd == -2) {
        char path[64];
        snprintf(path, sizeof path, "/proc/self/task/%ld/schedstat", (long)syscall(SYS_gettid));
        fd = open(path, O_RDONLY | O_CLOEXEC);
    }
    char buf[96];
    ssize_t n = fd >= 0 ? pread(fd, buf, sizeof buf - 1, 0) : -1;
    if (n <= 0) return 0;
    buf[n] = 0;
    unsigned long long run = 0, wait = 0;
    return sscanf(buf, "%llu %llu", &run, &wait) == 2 ? wait : 0;
#else
    return 0;
#endif
}

static uint64_t threadCpu(void) {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) {
        return 0;
    }
    uint64_t kernel = (uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime;
    uint64_t user = (uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime;
    return (kernel + user) * 100;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

void cpuTimeBegin(int kind) {
    uint64_t at = now();
    if (t.depth > 0) {
        t.acc[t.stack[t.depth - 1]] += at - t.since; /* the outer part pauses */
    }
    if (t.depth < (int)(sizeof(t.stack) / sizeof(t.stack[0]))) {
        t.stack[t.depth] = kind;
    }
    t.depth++;
    t.since = at;
}

void cpuTimeEnd(int kind) {
    (void)kind;
    uint64_t at = now();
    if (t.depth > 0) {
        t.depth--;
        if (t.depth < (int)(sizeof(t.stack) / sizeof(t.stack[0]))) {
            t.acc[t.stack[t.depth]] += at - t.since;
        }
    }
    t.since = at; /* the outer part resumes */
}

void cpuTimeBetween(int part, int on) {
    if (!cpuTimingOn || part < 0 || part > CPU_BPUT - CPU_BKEEP) return;
    if (on) cpuTimeBegin(CPU_BKEEP + part);
    else cpuTimeEnd(CPU_BKEEP + part);
}

void cpuTimeBetweenBytes(int which, unsigned long long n) {
    if (cpuTimingOn && (which == 0 || which == 1)) t.bytes[which] += n;
}

static void note(int i, double ms) {
    t.sum[i] += ms;
    if (ms > t.worst[i]) {
        t.worst[i] = ms;
    }
}

static void summary(void) {
    if (t.vblanks == 0) {
        return;
    }
    double n = t.vblanks;
    static const char* const extra[4] = {"wall", "cpu", "game", "idle"};
    fprintf(stderr, "cpu: %u vblanks, mean/worst ms per vblank:", t.vblanks);
    for (int i = 0; i < 4; i++) {
        fprintf(stderr, " %s %.3f/%.3f", extra[i], t.sum[CPU_KINDS + i] / n, t.worst[CPU_KINDS + i]);
    }
    for (int k = 0; k < CPU_KINDS; k++) {
        fprintf(stderr, " %s %.3f/%.3f", names[k], t.sum[k] / n, t.worst[k]);
    }
    fprintf(stderr, "\ncpu: vblanks delivered late: %u by more than 1 ms, %u by more than a frame (worst %.3f ms); "
                    "%d dry audio pushes, %d audio cuts, %d device underruns\n",
            t.late1ms, t.lateFrame, t.lateWorst, t.dry, t.cut, t.starved);
}

/* The timing starts: the counts from now. */
static void start(void) {
    t.lastWall = now();
    t.lastCpu = threadCpu();
    t.lastRunq = threadRunq();
    t.lastDry = spuOutputDryPushes != NULL ? spuOutputDryPushes() : 0;
    t.lastCut = spuOutputCuts != NULL ? spuOutputCuts() : 0;
    t.lastStarved = spuOutputStarved != NULL ? spuOutputStarved() : 0;
    t.since = t.lastWall;
    t.depth = 0; /* a part stopped mid-way while the timing was off ended unseen */
    memset(t.acc, 0, sizeof(t.acc));
    memset(t.bytes, 0, sizeof(t.bytes));
    cpuTimingOn = 1;
}

void cpuTimingRequest(int on) {
    requests += on ? 1 : -1;
    if (requests < 0) requests = 0;
}

int cpuTimingFromEnv(void) {
    return t.env;
}

const char* cpuTimingKindName(int kind) {
    return kind >= 0 && kind < CPU_KINDS ? names[kind] : "?";
}

void cpuTimingSetListener(void (*fn)(const CpuTimingFrame* frame)) {
    listener = fn;
}

void cpuTimingVBlank(uint32_t vblank) {
    if (!t.checked) {
        t.checked = 1;
        const char* v = getenv("XG_CPU_TIMING");
        if (v != NULL && v[0] != '\0' && strcmp(v, "0") != 0) {
            t.env = 1;
            t.lines = strcmp(v, "summary") != 0;
            atexit(summary);
        }
    }
    int want = t.env || requests > 0;
    if (want != cpuTimingOn) {
        /* On and off at a vblank: a frame's parts are all counted or none. */
        if (want) start();
        else cpuTimingOn = 0;
        return;
    }
    if (!cpuTimingOn) {
        return;
    }
    uint64_t wall = now(), cpu = threadCpu(), runq = threadRunq();
    if (t.depth > 0) {
        /* A part still running (a vblank inside it): its time so far is this
         * frame's, the rest the next's. */
        t.acc[t.stack[t.depth - 1]] += wall - t.since;
        t.since = wall;
    }
    double ms[CPU_KINDS], wallMs = (double)(wall - t.lastWall) * 1e-6, cpuMs = (double)(cpu - t.lastCpu) * 1e-6;
    double onCpu = 0;
    for (int k = 0; k < CPU_KINDS; k++) {
        ms[k] = (double)t.acc[k] * 1e-6;
        onCpu += k != CPU_GPUWAIT ? ms[k] : 0;
        note(k, ms[k]);
    }
    double runqMs = (double)(runq - t.lastRunq) * 1e-6;
    double game = cpuMs - onCpu, idle = wallMs - cpuMs - ms[CPU_GPUWAIT] - runqMs;
    game = game < 0 ? 0 : game;
    idle = idle < 0 ? 0 : idle;
    note(CPU_KINDS, wallMs);
    note(CPU_KINDS + 1, cpuMs);
    note(CPU_KINDS + 2, game);
    note(CPU_KINDS + 3, idle);
    double late = 0;
    if (hwNow != NULL && hwVBlankToTime != NULL && hwVSyncCount != NULL) {
        late = (hwNow() - hwVBlankToTime((double)hwVSyncCount())) * 1e3;
        late = late < 0 ? 0 : late;
        t.late1ms += late > 1.0;
        t.lateFrame += late > 1000.0 / 59.94;
        t.lateWorst = late > t.lateWorst ? late : t.lateWorst;
    }
    int dryNow = spuOutputDryPushes != NULL ? spuOutputDryPushes() : 0, dry = dryNow - t.lastDry;
    t.dry += dry;
    int cutNow = spuOutputCuts != NULL ? spuOutputCuts() : 0, cut = cutNow - t.lastCut;
    t.cut += cut;
    int starvedNow = spuOutputStarved != NULL ? spuOutputStarved() : 0, starved = starvedNow - t.lastStarved;
    t.starved += starved;
    t.vblanks++;
    if (listener != NULL) {
        CpuTimingFrame f = {vblank, wallMs, cpuMs, game, idle, runqMs, late, {0}, {t.bytes[0], t.bytes[1]}, dry, cut,
                            starved};
        memcpy(f.ms, ms, sizeof(f.ms));
        listener(&f);
    }
    if (t.env && (t.lines || wallMs > 2000.0 / 59.94 || dry || cut || starved)) {
        fprintf(stderr,
                "cpu: vb %u wall %.3f cpu %.3f | game %.3f gp0 %.3f spans %.3f cache %.3f spu %.3f present %.3f "
                "gpuwait %.3f state %.3f spin %.3f load %.3f bkeep %.3f bmem %.3f bdraw %.3f bput %.3f bmemb %llu "
                "vramb %llu runq %.3f idle %.3f | late %.3f dry %d cut %d starved %d\n",
                vblank, wallMs, cpuMs, game, ms[CPU_GP0], ms[CPU_SPANS], ms[CPU_CACHE], ms[CPU_SPU], ms[CPU_PRESENT],
                ms[CPU_GPUWAIT], ms[CPU_STATE], ms[CPU_SPIN], ms[CPU_LOAD], ms[CPU_BKEEP], ms[CPU_BMEM], ms[CPU_BDRAW],
                ms[CPU_BPUT], (unsigned long long)t.bytes[0], (unsigned long long)t.bytes[1], runqMs, idle, late, dry,
                cut, starved);
    }
    memset(t.acc, 0, sizeof(t.acc));
    memset(t.bytes, 0, sizeof(t.bytes));
    t.lastWall = wall;
    t.lastCpu = cpu;
    t.lastRunq = runq;
    t.lastDry = dryNow;
    t.lastCut = cutNow;
    t.lastStarved = starvedNow;
}
