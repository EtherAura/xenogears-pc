/* Packing owns its delta while the next capture changes its spare buffer.
 * Compare exact encoded bytes, restored bytes and the history budget. */
#include "../rewind.c"
static int checks;
#include "test/check.h"
/* UI settings are isolated; these fixtures exercise history transitions. */
void settingsDescribe(const char* key, const char* comment) { (void)key; (void)comment; }
int settingsInt(const char* key, int def) { (void)key; return def; }
int settingsBool(const char* key, int def) { (void)key; return def; }
void settingsSetInt(const char* key, int value) { (void)key; (void)value; }
int settingsSave(void) { return 1; }
void stateLateFinish(void) {}
/* rewind.c's other callees (capture, restore, the rewind screen, the input
 * harness): the packing paths under test never reach them. Defined here, not
 * left to --gc-sections, because MinGW's ld resolves references from the
 * sections it collects too (port/CMakeLists.txt). */
static void notReached(const char* name) {
    fprintf(stderr, "rewindpacktest: %s reached\n", name);
    abort();
}
int cpuTimingOn;
void cpuTimeBegin(int kind) { (void)kind; notReached("cpuTimeBegin"); }
void cpuTimeEnd(int kind) { (void)kind; notReached("cpuTimeEnd"); }
int cdBootDiscNumber(void) { notReached("cdBootDiscNumber"); return 0; }
uint64_t spuOutputDumpFrames(void) { notReached("spuOutputDumpFrames"); return 0; }
void spuOutputPause(int on) { (void)on; notReached("spuOutputPause"); }
uint32_t hwVSyncCount(void) { notReached("hwVSyncCount"); return 0; }
void hwPausedFrame(void) { notReached("hwPausedFrame"); }
void gpuPresentMenu(const char* shot) { (void)shot; notReached("gpuPresentMenu"); }
void presentShowNote(const char* text) { (void)text; notReached("presentShowNote"); }
SDL_Scancode padKey(int button, int slot) { (void)button; (void)slot; notReached("padKey"); return SDL_SCANCODE_UNKNOWN; }
void padPumpEvents(void) { notReached("padPumpEvents"); }
int stateAvailable(const char** why) { (void)why; notReached("stateAvailable"); return 0; }
int stateListChunks(const StateChunkRef** chunks, int* count, int late) {
    (void)chunks; (void)count; (void)late;
    notReached("stateListChunks");
    return 0;
}
int stateRestoreChunks(const StateChunkRef* chunks, int count, int disc, uint64_t dumpFrames, char* why,
                       size_t whySize) {
    (void)chunks; (void)count; (void)disc; (void)dumpFrames; (void)why; (void)whySize;
    notReached("stateRestoreChunks");
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "sync")) SDL_setenv_unsafe("XG_REWIND_PACK_SYNC", "1", 1);
    CHECK(SDL_Init(0));
    cfg.loaded = 1; cfg.seconds = 5; cfg.interval = 2; cfg.budget = 32;
    for (unsigned k = 0; k < 400; k++) {
        buf.size = 0;
        for (unsigned i = 0; i < 20000 + k * 10; i++) put32(i * 31 + k);
        size_t n = buf.size;
        uint8_t* expected = malloc(statePackBound(n));
        uint8_t* original = malloc(n);
        CHECK(expected && original);
        memcpy(original, buf.raw, n);
        size_t size = statePack(original, n, expected);
        img.bytes = k % 2 ? (32u << 20) : 0;
        packStart((Meta){k, k * 735, 1});
        /* A later image size must not change the previous push's budget. */
        img.bytes ^= 32u << 20;
        for (unsigned i = 0; i < 10000; i++) put32(i + k);
        packFinish();
        CHECK(ring.count == (k % 2 ? 0 : 1));
        if (!(k % 2)) {
            Step step;
            CHECK(popNewest(&step));
            CHECK(step.meta.vblank == k && step.meta.dumpFrames == k * 735);
            CHECK(step.rawSize == n && step.packedSize == size && !memcmp(step.packed, expected, size));
            CHECK(stateUnpack(step.packed, step.packedSize, expected, n));
            CHECK(!memcmp(original, expected, n));
            free(step.packed);
        }
        free(original); free(expected);
    }
    rewindReset(); img.bytes = 0;
    for (int k = 0; k < maxSteps(); k++) {
        buf.size = 0; put32((uint32_t)k); packStart((Meta){k, 0, 1}); packFinish();
    }
    CHECK(ring.count == 151);
    buf.size = 0; put32(151); packStart((Meta){151, 0, 1});
    /* Expanding the window cannot resurrect a step that the old limit
     * already dropped at this capture's logical completion point. */
    rewindSetSeconds(10);
    RewindStats stats; rewindStats(&stats);
    CHECK(ring.count == 151 && stats.kept == 151 * 2 / 60.0);
    rewindReset(); img.bytes = 32u << 20;
    buf.size = 0; put32(1); packStart((Meta){1, 0, 1});
    rewindSetBudget(64); rewindStats(&stats);
    CHECK(!ring.count); /* it belonged to the earlier 32-MiB budget */
    img.bytes = 0; buf.size = 0; put32(2); packStart((Meta){2, 0, 1});
    rewindStats(&stats); CHECK(ring.count == 1 && stats.kept == 2 / 60.0);
    buf.size = 0; put32(3); packStart((Meta){3, 0, 1});
    rewindReset(); rewindStats(&stats); CHECK(!ring.count && !packing.active);
    /* A capture that finds the worker still packing waits for the next park:
     * it lists nothing (stateListChunks would abort here), settles nothing,
     * leaves the history and the capture clock alone, and never blocks. The
     * worker's delta goes into the history once it is done. */
    if (packing.thread) {
        packing.step = (Step){xrealloc(NULL, 16), 16, 16, {7, 0, 1}};
        packing.imageBytes = 0;
        packing.active = packing.hasDelta = 1; /* as packKick leaves it while the worker runs */
        run.lastCapture = 5; run.haveCapture = 1;
        uint64_t captures = run.captures;
        capture();
        CHECK(packing.active && !ring.count && !pend.active);
        CHECK(run.captures == captures && run.lastCapture == 5);
        CHECK(packBusy() && packing.active);
        SDL_SignalSemaphore(packing.done); /* the worker's end of that job */
        CHECK(!packBusy() && !packing.active && ring.count == 1);
        Step step;
        CHECK(popNewest(&step) && step.meta.vblank == 7 && step.packedSize == 16);
        free(step.packed);
    } else {
        packStart((Meta){8, 0, 1});
        CHECK(!packBusy() && ring.count == 1); /* synchronous: never busy */
    }
    rewindReset();
    /* Shutdown may encounter a just-started job and is idempotent before
     * SDL_Quit; the registered exit handler will run once more afterward. */
    packStart((Meta){401, 0, 1});
    rewindShutdown(); rewindShutdown();
    SDL_Quit();
    printf("Rewind packing: %d checks passed\n", checks);
    return 0;
}
