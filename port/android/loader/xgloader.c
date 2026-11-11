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
 *   - takes the disc images the activity opened in the player's disc folder
 *     ("--image FD:NAME", "--bios FD:NAME": Storage Access Framework file
 *     descriptors, read in place through /proc/self/fd/FD), reads each
 *     image's SYSTEM.CNF for its release and disc, and sets XG_DISC,
 *     XG_DISC2 and XG_BIOS (unless the options set them already), and the
 *     app's version, "--app-version NAME" (XG_APP_VERSION: save states
 *     record it, port/src/system/statefile.c);
 *   - picks the game library of the region (libxenogears-us.so or -jp.so:
 *     "--region jp", XG_REGION=jp, else the region of the discs found, US
 *     first), loads it at a fixed reserved address
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

/* ---- disc images from file descriptors ----------------------------------------- */

#define SECTOR 2352

/* The user data of sector lba of a raw image (Mode 1 or Mode 2 Form 1), 2048 bytes. */
static int readUser(int fd, long lba, unsigned char* out) {
    unsigned char raw[SECTOR];
    if (pread(fd, raw, SECTOR, (off_t)lba * SECTOR) != SECTOR) return 0;
    memcpy(out, raw + (raw[15] == 2 ? 24 : 16), 2048);
    return 1;
}

/* The boot file SYSTEM.CNF names (cdrom:\SLUS_006.64;1 -> SLUS_006.64), from the ISO 9660
 * root directory. */
static int bootName(int fd, char* out, size_t size) {
    unsigned char s[2048];
    if (!readUser(fd, 16, s) || memcmp(s + 1, "CD001", 5) != 0) return 0;
    uint32_t root = s[156 + 2] | s[156 + 3] << 8 | s[156 + 4] << 16 | (uint32_t)s[156 + 5] << 24;
    uint32_t rootSize = s[156 + 10] | s[156 + 11] << 8 | s[156 + 12] << 16 | (uint32_t)s[156 + 13] << 24;
    for (uint32_t at = 0; at < rootSize && at < 16 * 2048; at += 2048) {
        if (!readUser(fd, root + at / 2048, s)) return 0;
        for (int i = 0; i < 2048 && s[i] != 0; i += s[i]) {
            int len = s[i + 32];
            if (len >= 10 && memcmp(s + i + 33, "SYSTEM.CNF", 10) == 0) {
                uint32_t lba = s[i + 2] | s[i + 3] << 8 | s[i + 4] << 16 | (uint32_t)s[i + 5] << 24;
                unsigned char cnf[2049];
                if (!readUser(fd, lba, cnf)) return 0;
                cnf[2048] = 0;
                const char* p = strstr((const char*)cnf, "cdrom:");
                if (p == NULL) return 0;
                p += 6;
                while (*p == '\\' || *p == '/') p++;
                size_t n = strcspn(p, ";\r\n ");
                if (n == 0 || n >= size) return 0;
                memcpy(out, p, n);
                out[n] = 0;
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    char path[64];
    int region; /* 0 US, 1 JP */
    int disc;   /* 1, 2 */
} Image;

static Image images[8];
static int imageCount;
static char biosPath[64];

/* "--image FD:NAME": the release and disc by the boot file. */
static void addImage(const char* arg) {
    int fd = atoi(arg);
    const char* name = strchr(arg, ':');
    char boot[32];
    if (fd <= 0 || !bootName(fd, boot, sizeof boot)) {
        fprintf(stderr, "loader: %s: not a disc image of this game\n", name ? name + 1 : arg);
        return;
    }
    static const struct { const char* boot; int region, disc; } known[] = {
        {"SLUS_006.64", 0, 1}, {"SLUS_006.69", 0, 2}, {"SLPS_011.60", 1, 1}, {"SLPS_011.61", 1, 2}};
    for (size_t k = 0; k < sizeof known / sizeof known[0]; k++) {
        if (strcmp(boot, known[k].boot) == 0 && imageCount < 8) {
            Image* im = &images[imageCount++];
            snprintf(im->path, sizeof im->path, "/proc/self/fd/%d", fd);
            im->region = known[k].region;
            im->disc = known[k].disc;
            fprintf(stderr, "loader: %s: %s Disc %d (%s)\n", name ? name + 1 : arg, im->region ? "JP" : "US",
                    im->disc, boot);
            return;
        }
    }
    fprintf(stderr, "loader: %s: boot file %s is not Xenogears\n", name ? name + 1 : arg, boot);
}

static const char* imageFor(int region, int disc) {
    for (int i = 0; i < imageCount; i++) {
        if (images[i].region == region && images[i].disc == disc) return images[i].path;
    }
    return NULL;
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
        } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            addImage(argv[++i]);
        } else if (strcmp(argv[i], "--app-version") == 0 && i + 1 < argc) {
            i++;
            if (getenv("XG_APP_VERSION") == NULL) setenv("XG_APP_VERSION", argv[i], 1);
        } else if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            snprintf(biosPath, sizeof biosPath, "/proc/self/fd/%d", atoi(argv[++i]));
        } else {
            argv[out++] = argv[i];
        }
    }
    argv[out] = NULL;
    argc = out;
    /* The region: asked for, else the discs' (US first), else US. */
    int jp = region != NULL ? SDL_strcasecmp(region, "jp") == 0 : imageFor(0, 1) == NULL && imageFor(1, 1) != NULL;
    const char* disc1 = imageFor(jp, 1);
    const char* disc2 = imageFor(jp, 2);
    if (disc1 != NULL && getenv("XG_DISC") == NULL) setenv("XG_DISC", disc1, 1);
    if (disc2 != NULL && getenv("XG_DISC2") == NULL) setenv("XG_DISC2", disc2, 1);
    if (getenv("XG_BIOS") == NULL) setenv("XG_BIOS", biosPath[0] ? biosPath : "none", 1);
    char name[64];
    snprintf(name, sizeof name, "libxenogears-%s.so", jp ? "jp" : "us");
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
