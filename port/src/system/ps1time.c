/*
 * The PS1's time for the game's work (xg-wv9t; ps1time.h says why).
 *
 * The PS1 frame rate (setting ps1_frame_rate, off by default; the Original
 * engine only, not online, with Speedup or a battle or field speed, nor while
 * the slowdown replay is lined up with its log, but after it: a validation
 * run replays a Beetle PSX log into a scene and the model takes over): each stretch of the game's work between two VSync calls, and
 * up to each DrawSync and each GPU job, costs what ps1time_model.h says (a
 * cost per GTE command by the function that ran it, per platform call, per
 * stretch by game mode), and the clock moves on by that much (hwBusy: the
 * vblanks that come due meanwhile are delivered, as they broke into the PS1's
 * work). The GPU draws each job from when it is queued for its modelled time,
 * and DrawSync(0) waits for it. Heavy scenes then take the PS1's vblanks a
 * picture, and the game's own catch-up (battle's frame rate) keeps its speed.
 *
 * XG_PS1TIME_LOG=file: a line per event the work falls between, with the
 * work counters since the event before, for the model's fit
 * (tools/ps1time_fit.py). Under the slowdown replay (XG_SLOWDOWN) the clock
 * these lines carry is the Beetle PSX log's, so each stretch of counters
 * comes with the time the PS1 took for it.
 *
 *   V <arg> <vblank> <safe points> <gte> <lined up> <prims>   VSync(arg) called; <lined up> is
 *                                          1 while the slowdown replay is lined up with its log and
 *                                          this call found its line (its clock the PS1's);
 *                                          <prims>: addPrim calls
 *   R <arg> <vblank>                       ... returned
 *   D <vblank> <safe points> <gte> <prims>          DrawSync called
 *   Q <what> <vblank> <safe points> <gte> <gpu> <prims>   a GPU job queued (ps1time.h's letters)
 *   W <vblank> <vblank> <calls>            a VSync(-1) busy wait: first and last call, calls
 *   M <mode> <vblank>                      bootGame starts a game mode
 *
 * <vblank> is hwVBlankNow(): vblanks with the fraction of the frame. <gte> is
 * "-" or comma-separated "command:count" (hex command numbers), <gpu> "-" or
 * comma-separated "kind:count:pixels" (ps1time.h's PT_ order), then
 * "s:<semi-transparent pixels>" and "m:<shaded textured pixels>".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "ps1time.h"
#include "ps1time_model.h"
#include "settings.h"
#include "speedup.h"
#include "netplay.h"
#include "engine.h"

const char* portOption(const char* name); /* options.c */
extern uint32_t hwGameSafePoints; /* hw.c: the game's own safe points, outside interrupt callbacks */

static int ready;
static FILE* logFile;
static uint32_t lastHeartbeat;
static Ps1timeGpu gpuSeen; /* ps1timeGpu at the last queued job */
/* A run of VSync(-1) calls with no work between them (a busy wait), not yet written. */
static int pollRun, pollCalls;
static double pollFirst, pollLast;

/* XG_PS1TIME_SITES=file: how often each platform function's safe point ran (its
 * address in hwSafePoint's caller), written at exit: which calls the work counts. */
extern int ps1timeSitesOn; /* hw.c */
#define SITE_SLOTS 4096
static struct {
    const void* site;
    uint64_t calls;
} sites[SITE_SLOTS];
static FILE* sitesFile;

void ps1timeSite(const void* site) {
    uintptr_t h = ((uintptr_t)site >> 2) * 2654435761u;
    for (int k = 0; k < SITE_SLOTS; k++) {
        int i = (int)((h + (uintptr_t)k) % SITE_SLOTS);
        if (sites[i].site == site || sites[i].site == NULL) {
            sites[i].site = site;
            sites[i].calls++;
            return;
        }
    }
}

static void writeSites(void) {
    for (int i = 0; i < SITE_SLOTS; i++) {
        if (sites[i].site != NULL) {
            fprintf(sitesFile, "%p %llu\n", sites[i].site, (unsigned long long)sites[i].calls);
        }
    }
    fclose(sitesFile);
}

/* XG_PS1TIME_GTESITES=1: the GTE commands of each segment by the game function that
 * ran them (include/gte_pc.h's GTE_RUN; "-" for LIBGTE's own), logged as a "G" line
 * after the line that ends the stretch: "function:count,...". */
#define GSITE_SLOTS 1024
static struct {
    const char* site;
    uint32_t calls;
} gsites[GSITE_SLOTS];
static int gsitesUsed;

static int siteLog; /* XG_PS1TIME_GTESITES=1: G lines */
extern uint32_t pcPrimLinkCount; /* gpu/prim.c: addPrim calls (include/psyq.h) since the last event */
extern int pcPrimLinkOn;

/* The PS1 frame rate: the stretch of work under way. */
static int on;            /* the model applies now (refresh) */
static double segStart;   /* hwNow() where it began */
static double segCycles;  /* its GTE commands' cost so far */
static double gpuEnd;     /* hwNow() when the GPU has drawn what it was given */
static int gameMode;

/* A function's cost per GTE command, by its __func__ pointer (looked up by name once). */
#define COST_SLOTS 2048
static struct {
    const char* fn;
    double cycles;
} costs[COST_SLOTS];

/* XG_PS1TIME_MODEL=file: costs read from a file instead of ps1time_model.h, for the
 * model's validation (tools/ps1time_fit.py fit --model-file): lines "safe_point <cycles>",
 * "prim <cycles>", "other <cycles>", "mode <n> <cycles>", "fn <name> <cycles>". */
static double modelSafePoint = PS1TIME_MODEL_SAFE_POINT, modelPrim = PS1TIME_MODEL_PRIM, modelOther = PS1TIME_MODEL_OTHER;
static double modelMode[8];
static struct {
    char name[64];
    double cycles;
} *fileFn;
static int fileFnCount;

static void loadModel(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "ps1time: cannot read XG_PS1TIME_MODEL %s\n", path);
        return;
    }
    char key[16], name[64];
    double v;
    int n;
    memset(modelMode, 0, sizeof(modelMode));
    modelSafePoint = modelPrim = modelOther = 0;
    fileFnCount = 0;
    while (fscanf(f, "%15s", key) == 1) {
        if (strcmp(key, "fn") == 0 && fscanf(f, "%63s %lf", name, &v) == 2) {
            void* grown = realloc(fileFn, sizeof(*fileFn) * (size_t)(fileFnCount + 1));
            if (grown == NULL) {
                break;
            }
            fileFn = grown;
            snprintf(fileFn[fileFnCount].name, sizeof(fileFn[0].name), "%s", name);
            fileFn[fileFnCount++].cycles = v;
        } else if (strcmp(key, "mode") == 0 && fscanf(f, "%d %lf", &n, &v) == 2) {
            modelMode[n & 7] = v;
        } else if (fscanf(f, "%lf", &v) == 1) {
            if (strcmp(key, "safe_point") == 0) {
                modelSafePoint = v;
            } else if (strcmp(key, "prim") == 0) {
                modelPrim = v;
            } else if (strcmp(key, "other") == 0) {
                modelOther = v;
            }
        }
    }
    fclose(f);
    fprintf(stderr, "ps1time: costs from %s (%d functions)\n", path, fileFnCount);
}

static double costByName(const char* fn) {
    if (fileFn != NULL) {
        for (int i = 0; i < fileFnCount; i++) {
            if (strcmp(fileFn[i].name, fn) == 0) {
                return fileFn[i].cycles;
            }
        }
        return modelOther;
    }
    int lo = 0, hi = (int)(sizeof(ps1timeModelFn) / sizeof(ps1timeModelFn[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, cmp = strcmp(fn, ps1timeModelFn[mid].fn);
        if (cmp == 0) {
            return ps1timeModelFn[mid].cycles;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return modelOther;
}

static double fnCost(const char* fn) {
    uintptr_t h = ((uintptr_t)fn >> 3) * 2654435761u;
    for (int k = 0; k < COST_SLOTS; k++) {
        int i = (int)((h + (uintptr_t)k) % COST_SLOTS);
        if (costs[i].fn == fn) {
            return costs[i].cycles;
        }
        if (costs[i].fn == NULL) {
            costs[i].fn = fn;
            costs[i].cycles = costByName(fn);
            return costs[i].cycles;
        }
    }
    return modelOther;
}

void gteCountSite(const char* site) {
    if (site == NULL || site[0] == '\0') {
        site = "-"; /* LIBGTE's own commands (port/src/gte/libgte.c) */
    }
    if (on) {
        segCycles += fnCost(site);
    }
    if (!siteLog) {
        return;
    }
    uintptr_t h = ((uintptr_t)site >> 2) * 2654435761u;
    for (int k = 0; k < GSITE_SLOTS; k++) {
        int i = (int)((h + (uintptr_t)k) % GSITE_SLOTS);
        if (gsites[i].site == site || gsites[i].site == NULL) {
            if (gsites[i].site == NULL) {
                gsitesUsed++;
            }
            gsites[i].site = site;
            gsites[i].calls++;
            return;
        }
    }
}

static void writeGteSites(void) {
    if (logFile == NULL || gsitesUsed == 0) {
        return;
    }
    fprintf(logFile, "G");
    char sep = ' ';
    for (int i = 0; i < GSITE_SLOTS; i++) {
        if (gsites[i].site != NULL) {
            fprintf(logFile, "%c%s:%u", sep, gsites[i].site, gsites[i].calls);
            sep = ',';
        }
    }
    fprintf(logFile, "\n");
    memset(gsites, 0, sizeof(gsites));
    gsitesUsed = 0;
}

static int replaying; /* XG_SLOWDOWN: the log's timing while it is lined up, the model's after it */
static int replayLined;

static void init(void) {
    if (ready) {
        return;
    }
    ready = 1;
    const char* path = portOption("XG_PS1TIME_LOG");
    if (path != NULL) {
        logFile = fopen(path, "w");
        if (logFile == NULL) {
            fprintf(stderr, "ps1time: cannot write XG_PS1TIME_LOG %s\n", path);
        } else {
            fprintf(logFile, "# xenogears PS1 time log 1 (port/src/system/ps1time.c)\n");
            const char* g = portOption("XG_PS1TIME_GTESITES");
            siteLog = g != NULL && g[0] == '1';
        }
    }
    const char* sitesPath = portOption("XG_PS1TIME_SITES");
    if (sitesPath != NULL && (sitesFile = fopen(sitesPath, "w")) != NULL) {
        ps1timeSitesOn = 1;
        atexit(writeSites);
    }
    replaying = portOption("XG_SLOWDOWN") != NULL;
    memcpy(modelMode, ps1timeModelMode, sizeof(modelMode));
    const char* modelPath = portOption("XG_PS1TIME_MODEL");
    if (modelPath != NULL) {
        loadModel(modelPath);
    }
    lastHeartbeat = hwGameSafePoints;
}

/* The setting, as of settingsRevision(). */
static int wanted(void) {
    static int value, valid;
    static unsigned revision;
    if (!valid || revision != settingsRevision()) {
        value = settingsBool("ps1_frame_rate", 0);
        revision = settingsRevision();
        valid = 1;
    }
    return value;
}

int ps1timeFrameRateOn(void) {
    return wanted();
}

void ps1timeSetFrameRate(int enabled) {
    settingsDescribe("ps1_frame_rate", "ps1_frame_rate: 1 draws heavy scenes as slowly as the PS1 did (its CPU and GPU "
                                       "time for each frame's work, modelled); the game keeps its speed. Original "
                                       "engine; not with Speedup, battle or field speed, or online. 0 (default): "
                                       "every frame the game asks for");
    settingsSetInt("ps1_frame_rate", enabled != 0);
    settingsSave();
}

/* Whether the model applies now; counting follows. */
static void refresh(void) {
    if (replaying) {
        int64_t v;
        int lined = slowdownLogVBlank(&v);
        replayLined |= lined;
        if (replayLined && !lined) {
            replaying = 0; /* the log is over: the model from here (a validation run's switch-over) */
        }
    }
    int now = wanted() && !replaying && !netplayActive() && !speedupEnabled() && hwGameSpeed() == 1 &&
              engineThis() == ENGINE_ORIGINAL;
    if (now && !on) {
        segStart = gpuEnd = hwNow();
        segCycles = 0;
    }
    on = now;
    gteCountOn = on || siteLog ? 2 : logFile != NULL ? 1 : 0;
    ps1timeGpuOn = on || logFile != NULL;
    pcPrimLinkOn = on || logFile != NULL;
}

/* The safe points since the last event and whether there was work (GTE commands or
 * platform calls); when logging, the fields of a line. */
static int workSince(uint32_t* spOut, uint32_t* primsOut, char* out, size_t size) {
    uint32_t beat = hwGameSafePoints;
    uint32_t sp = beat - lastHeartbeat;
    lastHeartbeat = beat;
    *spOut = sp;
    *primsOut = pcPrimLinkCount;
    pcPrimLinkCount = 0;
    int any = *primsOut != 0;
    int n = out != NULL ? snprintf(out, size, "%u ", sp) : 0;
    int gte = 0;
    for (int i = 0; i < 64; i++) {
        if (gteCommandCount[i] != 0) {
            if (out != NULL) {
                n += snprintf(out + n, size - (size_t)n, "%s%x:%u", gte ? "," : "", (unsigned)i, gteCommandCount[i]);
            }
            gte = 1;
            gteCommandCount[i] = 0;
        }
    }
    if (!gte && out != NULL) {
        snprintf(out + n, size - (size_t)n, "-");
    }
    return any || gte || sp > 2;
}

/* The PS1's CPU time for the stretch so far: the clock moves on to its end. `ends`:
 * the stretch ends at a VSync call (its per-mode cost). */
/* The clock went back (a save state or rewind loaded): the stretch and the GPU's work
 * start again from now. Not part of a save state: a stretch is at most a frame's work. */
static void clockCheck(double now) {
    if (segStart > now) {
        segStart = now;
    }
    if (gpuEnd > now + 1.0) {
        gpuEnd = now;
    }
}

/* Platform calls and primitives counted up to an event inside an interrupt callback:
 * the stretch under way goes on, so they are charged at its next event. */
static uint32_t carrySp, carryPrims;

static void carry(uint32_t sp, uint32_t prims) {
    carrySp += sp;
    carryPrims += prims;
}

static void charge(uint32_t sp, uint32_t prims, int ends) {
    sp += carrySp;
    prims += carryPrims;
    carrySp = carryPrims = 0;
    double cycles = segCycles + sp * modelSafePoint + prims * modelPrim + (ends ? modelMode[gameMode & 7] : 0);
    segCycles = 0;
    double now = hwNow();
    clockCheck(now);
    double until = segStart + cycles / HW_CPU_HZ;
    if (until > now) {
        hwBusy(until - now);
        lastHeartbeat = hwGameSafePoints; /* the interrupts delivered meanwhile are no game work */
    }
}

/* The game reads a root counter (GetRCnt): the work so far is charged first, so the
 * count says what the PS1's did. Battling spends a time budget skipping empty OT
 * entries (skipEmptyBattlingOtEntries) and stops when the counter says so. */
void ps1timeTimeRead(void) {
    if (!on || hwInInterrupt()) {
        return; /* an interrupt callback's read (the sound driver's): the game's work goes on after it */
    }
    uint32_t sp, prims;
    workSince(&sp, &prims, NULL, 0);
    charge(sp, prims, 0);
    segStart = hwNow();
}

static void flushPoll(void) {
    if (pollRun && logFile != NULL) {
        fprintf(logFile, "W %.4f %.4f %d\n", pollFirst, pollLast, pollCalls);
    }
    pollRun = 0;
}

void ps1timeVSync(int32_t arg) {
    init();
    refresh();
    char work[1024];
    uint32_t sp, prims;
    double vb = hwVBlankNow();
    int any = workSince(&sp, &prims, logFile != NULL ? work : NULL, sizeof(work));
    if (on && hwInInterrupt()) {
        carry(sp, prims); /* a callback's VSync (battling's DrawSync callback): no stretch ends */
    } else if (on) {
        if (any) {
            charge(sp, prims, 1);
        }
        if (arg == -1 || arg == 1 || !any) {
            segStart = hwNow(); /* a wait's own stretch starts when it returns */
        }
    }
    if (logFile == NULL) {
        return;
    }
    if (arg < 0 && !any && pollRun) {
        pollLast = vb;
        pollCalls++;
        return;
    }
    flushPoll();
    int64_t logVBlank;
    fprintf(logFile, "V %d %.4f %s %d %u\n", (int)arg, vb, work, slowdownLogVBlank(&logVBlank) && slowdownMatched(),
            prims);
    writeGteSites();
    if (arg < 0) {
        pollRun = 1;
        pollCalls = 1;
        pollFirst = pollLast = vb;
    }
}

void ps1timeMode(int mode) {
    init();
    gameMode = mode;
    if (logFile == NULL) {
        return;
    }
    flushPoll();
    fprintf(logFile, "M %d %.4f\n", mode, hwVBlankNow());
}

void ps1timeVSyncDone(int32_t arg) {
    if (arg < 0 || arg == 1) {
        return; /* VSync(-1) and VSync(1) return at once */
    }
    if (on && !hwInInterrupt()) {
        segStart = hwNow();
    }
    if (logFile != NULL) {
        fprintf(logFile, "R %d %.4f\n", (int)arg, hwVBlankNow());
    }
}

void ps1timeDrawSync(void) {
    init();
    refresh();
    char work[1024];
    uint32_t sp, prims;
    double vb = hwVBlankNow();
    workSince(&sp, &prims, logFile != NULL ? work : NULL, sizeof(work));
    if (on && hwInInterrupt()) {
        carry(sp, prims);
    } else if (on) {
        charge(sp, prims, 0);
        double now = hwNow();
        if (gpuEnd > now) {
            hwBusy(gpuEnd - now); /* DrawSync(0) waits for the GPU */
            lastHeartbeat = hwGameSafePoints;
        }
        segStart = hwNow();
    }
    if (logFile == NULL) {
        return;
    }
    flushPoll();
    fprintf(logFile, "D %.4f %s %u\n", vb, work, prims);
    writeGteSites();
}

/* The GPU's time for what was queued since the last job, in CPU cycles. */
static double gpuCycles(const Ps1timeGpu* d) {
    double c = PS1TIME_MODEL_GPU_JOB + d->semiPixels * PS1TIME_MODEL_GPU_SEMI +
               d->shadedTexPixels * PS1TIME_MODEL_GPU_SHADED;
    for (int k = 0; k < PT_KINDS; k++) {
        c += d->count[k] * ps1timeModelGpuCount[k] + d->pixels[k] * ps1timeModelGpuPixel[k];
    }
    return c;
}

void ps1timeGpuQueued(char what) {
    init();
    refresh();
    char work[1024], gpu[1024];
    uint32_t sp, prims;
    double vb = hwVBlankNow();
    workSince(&sp, &prims, logFile != NULL ? work : NULL, sizeof(work));
    Ps1timeGpu d;
    for (int k = 0; k < PT_KINDS; k++) {
        d.count[k] = ps1timeGpu.count[k] - gpuSeen.count[k];
        d.pixels[k] = ps1timeGpu.pixels[k] - gpuSeen.pixels[k];
    }
    d.semiPixels = ps1timeGpu.semiPixels - gpuSeen.semiPixels;
    d.shadedTexPixels = ps1timeGpu.shadedTexPixels - gpuSeen.shadedTexPixels;
    gpuSeen = ps1timeGpu;
    if (on && hwInInterrupt()) {
        carry(sp, prims);
    } else if (on) {
        charge(sp, prims, 0);
        double now = hwNow();
        segStart = now;
        gpuEnd = (gpuEnd > now ? gpuEnd : now) + gpuCycles(&d) / HW_CPU_HZ;
    }
    if (logFile == NULL) {
        return;
    }
    int n = 0, any = 0;
    for (int k = 0; k < PT_KINDS; k++) {
        if (d.count[k] != 0) {
            n += snprintf(gpu + n, sizeof(gpu) - (size_t)n, "%s%d:%.0f:%.0f", any ? "," : "", k, d.count[k], d.pixels[k]);
            any = 1;
        }
    }
    if (any) {
        snprintf(gpu + n, sizeof(gpu) - (size_t)n, ",s:%.0f,m:%.0f", d.semiPixels, d.shadedTexPixels);
    } else {
        snprintf(gpu, sizeof(gpu), "-");
    }
    flushPoll();
    fprintf(logFile, "Q %c %.4f %s %s %u\n", what, vb, work, gpu, prims);
    writeGteSites();
}
