/*
 * The Android app's native entry (port/android, xg-mrj.3.6). SDL's activity
 * loads libSDL3.so and this library and calls its SDL_main on the SDL thread.
 * It:
 *   - sends stdout and stderr to logcat (tag "xenogears"): an app's are
 *     /dev/null otherwise, and the port reports on stderr;
 *   - applies the start options: the lines KEY=VALUE of xenogears.env in the
 *     app's external files folder (Android/data/<package>/files), which adb
 *     can write, then the activity's arguments "--env KEY=VALUE" (am start
 *     --es env "KEY=VALUE;KEY=VALUE"): an app has no environment of its own,
 *     and the port reads its XG_* options there (port/src/system/options.c);
 *   - picks the game library of the region (libxenogears-us.so or -jp.so:
 *     "--region jp", or XG_REGION=jp), loads it at a fixed reserved address
 *     (android_dlopen_ext into a reservation of our own, as Linux PIE builds
 *     run at fixed addresses: save states hold addresses and load only where
 *     they were made; port/src/system/statefile.c), or anywhere if that
 *     address is taken (states then stay within the run);
 *   - runs the library's xenogearsMain (port/src/main.c) with the remaining
 *     arguments on a thread of its own, whose stack lies BELOW the library
 *     in the same reservation. The game switches to stacks of its own
 *     (port/src/system/gamestack.c; Battling's coroutines in its BSS), and
 *     what it calls there may call Java (SDL's joystick polling, window
 *     style, clipboard...). ART throws StackOverflowError when a stack
 *     pointer is below the thread's stack's end; the library's stacks are
 *     above it, which ART accepts. The thread is attached to ART on its own
 *     stack before the game starts: attaching protects the stack's lowest
 *     pages and drops (MADV_DONTNEED) everything from there up to the stack
 *     pointer, which, done on a game stack, would take the library's own
 *     data with it (a dropped page of a library reads back as the file:
 *     its relocations undone).
 * The reservation: 1 MB of padding (ART's protected pages), the thread's
 * 8 MB stack, the game library from 16 MB on.
 */

#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define TAG "xenogears"

/* Where the game library goes: below the executables (app_process at about
 * two thirds of the address space) and far below the top-down mappings, in
 * a 39-bit (arm64) and a 47-bit (x86-64) address space alike. XG_LOAD_ADDRESS
 * (hex) picks another, 0 none. The reservation is address space only. */
#if defined(__aarch64__)
#define LOAD_ADDRESS 0x5800000000ull
#else
#define LOAD_ADDRESS 0x200000000000ull
#endif
#define LOAD_RESERVE (1ull << 30)
#define THREAD_STACK_AT (1ull << 20)
#define THREAD_STACK (8ull << 20)
#define LIBRARY_AT (16ull << 20)

/* ---- stdout and stderr to logcat ----------------------------------------------- */

static int logPipe[2];

static void* logPump(void* arg) {
    (void)arg;
    char buffer[4096];
    size_t have = 0;
    for (;;) {
        ssize_t n = read(logPipe[0], buffer + have, sizeof buffer - 1 - have);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        have += (size_t)n;
        buffer[have] = '\0';
        char* start = buffer;
        char* nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, TAG, start);
            start = nl + 1;
        }
        have = strlen(start);
        if (have == sizeof buffer - 1) { /* a line longer than the buffer: as it is */
            __android_log_write(ANDROID_LOG_INFO, TAG, start);
            have = 0;
        } else {
            memmove(buffer, start, have);
        }
    }
    return NULL;
}

static void logToLogcat(void) {
    if (pipe(logPipe) != 0) return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    dup2(logPipe[1], STDOUT_FILENO);
    dup2(logPipe[1], STDERR_FILENO);
    pthread_t t;
    if (pthread_create(&t, NULL, logPump, NULL) == 0) pthread_detach(t);
}

/* ---- start options ---------------------------------------------------------- */

static void setOption(const char* line) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0') return;
    const char* eq = strchr(line, '=');
    if (eq == NULL || eq == line) return;
    char key[128];
    size_t k = (size_t)(eq - line);
    if (k >= sizeof key) return;
    memcpy(key, line, k);
    key[k] = '\0';
    while (k > 0 && (key[k - 1] == ' ' || key[k - 1] == '\t')) key[--k] = '\0';
    setenv(key, eq + 1, 1);
    fprintf(stderr, "loader: %s set\n", key);
}

static void readEnvFile(const char* folder) {
    char path[1024];
    snprintf(path, sizeof path, "%s/xenogears.env", folder);
    FILE* f = fopen(path, "r");
    if (f == NULL) return;
    char line[2048];
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        setOption(line);
    }
    fclose(f);
    fprintf(stderr, "loader: options from %s\n", path);
}

/* "KEY=VALUE;KEY=VALUE" from one argument. */
static void setOptions(const char* list) {
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", list);
    for (char* item = strtok(copy, ";"); item != NULL; item = strtok(NULL, ";")) setOption(item);
}

/* ---- the game library -------------------------------------------------------- */

static void* reservation;

/* The reservation at the fixed address (or, taken, anywhere), the library in
 * it above the padding, the thread's own stack mapped at its top. */
static void* loadGame(const char* name) {
    uint64_t want = LOAD_ADDRESS;
    const char* pick = getenv("XG_LOAD_ADDRESS");
    if (pick != NULL && pick[0] != '\0') want = strtoull(pick, NULL, 16);
    void* at = MAP_FAILED;
    if (want != 0) {
        at = mmap((void*)(uintptr_t)want, LOAD_RESERVE, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (at != MAP_FAILED && at != (void*)(uintptr_t)want) {
            munmap(at, LOAD_RESERVE); /* a kernel that took the address as a hint */
            at = MAP_FAILED;
        }
        if (at == MAP_FAILED) {
            fprintf(stderr, "loader: 0x%llx is taken: save states hold for this run only\n", (unsigned long long)want);
        }
    }
    if (at == MAP_FAILED) {
        at = mmap(NULL, LOAD_RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    }
    if (at == MAP_FAILED) {
        fprintf(stderr, "loader: no address space for the game: %s\n", strerror(errno));
        return NULL;
    }
    char* base = at;
    if (mmap(base + THREAD_STACK_AT, THREAD_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
             -1, 0) == MAP_FAILED) {
        fprintf(stderr, "loader: no stack for the game thread: %s\n", strerror(errno));
        return NULL;
    }
    android_dlextinfo info = {0};
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = base + LIBRARY_AT;
    info.reserved_size = LOAD_RESERVE - LIBRARY_AT;
    void* h = android_dlopen_ext(name, RTLD_NOW | RTLD_LOCAL, &info);
    if (h == NULL) {
        fprintf(stderr, "loader: %s: %s\n", name, dlerror());
        return NULL;
    }
    fprintf(stderr, "loader: %s at %p\n", name, (void*)(base + LIBRARY_AT));
    reservation = base;
    return h;
}

typedef struct {
    int (*entry)(int, char**);
    int argc;
    char** argv;
    int status;
} GameRun;

static void* gameThread(void* arg) {
    GameRun* run = arg;
    SDL_GetAndroidJNIEnv(); /* attached to ART here, on this thread's own stack (above) */
    run->status = run->entry(run->argc, run->argv);
    return NULL;
}

/* xenogearsMain on a thread whose stack is below the library (above). */
static int runGame(int (*entry)(int, char**), int argc, char** argv) {
    GameRun run = {entry, argc, argv, 1};
    pthread_attr_t attr;
    pthread_t t;
    if (pthread_attr_init(&attr) != 0 ||
        pthread_attr_setstack(&attr, (char*)reservation + THREAD_STACK_AT, THREAD_STACK) != 0 ||
        pthread_create(&t, &attr, gameThread, &run) != 0) {
        fprintf(stderr, "loader: no game thread: %s\n", strerror(errno));
        return 1;
    }
    pthread_attr_destroy(&attr);
    pthread_setname_np(t, "xenogears");
    pthread_join(t, NULL);
    return run.status;
}

int SDL_main(int argc, char* argv[]) {
    logToLogcat();
    const char* files = SDL_GetAndroidExternalStoragePath();
    if (files != NULL) {
        setenv("XG_ANDROID_FILES", files, 1);
        readEnvFile(files);
    }
    /* The activity's own arguments come off; the rest go to the game. */
    const char* region = getenv("XG_REGION");
    int out = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--region") == 0 && i + 1 < argc) {
            region = argv[++i];
        } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            setOptions(argv[++i]);
        } else {
            argv[out++] = argv[i];
        }
    }
    argv[out] = NULL;
    argc = out;
    char name[64];
    snprintf(name, sizeof name, "libxenogears-%s.so", region != NULL && SDL_strcasecmp(region, "jp") == 0 ? "jp" : "us");
    void* game = loadGame(name);
    if (game == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Xenogears", "The game library did not load (logcat: xenogears).",
                                 NULL);
        return 1;
    }
    int (*entry)(int, char**) = (int (*)(int, char**))dlsym(game, "xenogearsMain");
    if (entry == NULL) {
        fprintf(stderr, "loader: %s has no xenogearsMain\n", name);
        return 1;
    }
    int status = runGame(entry, argc, argv);
    fprintf(stderr, "loader: the game returned %d\n", status);
    return status;
}
