/*
 * The game thread's watchdog: a game loop that waits for an interrupt and
 * calls nothing.
 *
 * Interrupts reach the game only at safe points (hw.h). On the PS1 a loop
 * that spins until the vsync interrupt changes something (the battle pause
 * waiting for Start) is broken by the interrupt; on the PC, with no safe
 * point in it, it spins for good: no vblank, so no new picture, no pad, no
 * sound, no window events. Such a loop needs PORT_YIELD() (common.h).
 *
 * A thread here watches hw.c's count of safe points. When the game thread
 * has reached none for XG_STALL_MS (default 2000) while the game runs, it
 * says so once per stall, with the GPU layer's view (gpuDescribe) and, on
 * Linux, the game thread's backtrace, and says again when the game moves on.
 * While a stall lasts it serves SIGUSR1's VRAM dump itself
 * (gpuServeStalledDump), since no vblank comes to do it. XG_STALL_MS=0: off.
 *
 * A short XG_STALL_MS (under a second) finds the host's hitches instead: a
 * game thread blocked for longer than the sound's queue (60 for the 60 ms
 * audio_latency) is a gap in the sound, and the backtrace names where it was
 * (xg-2fy); XG_CPU_TIMING=summary gives each slow vblank's length. The watch
 * polls three times per XG_STALL_MS then, at most every 10 ms. Diagnostics
 * only: the backtrace's signal can end a plain sleep early (blocking calls
 * resume, SA_RESTART).
 */

#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

#if defined(__linux__) && defined(__GLIBC__)
#define STALL_BACKTRACE 1
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <execinfo.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

/* port/src/gpu (weak: the GPU layer may not be in the build). */
extern void gpuDescribe(char* out, size_t size) __attribute__((weak));
extern int gpuServeStalledDump(void) __attribute__((weak));

#define POLL_MS 100
#define DUMP_MS 500 /* SIGUSR1 is served from here after half a second without a safe point */

static int pollMs = POLL_MS;
static int reportPolls;

#ifdef STALL_BACKTRACE

/* ---- the game thread's backtrace: SIGUSR2 sent to it, backtrace() there ---- */

#define MAX_FRAMES 48

static pthread_t gameThread;
static void* frames[MAX_FRAMES];
static volatile sig_atomic_t frameCount; /* -1: asked, not taken yet */

static void onBacktraceSignal(int sig) {
    int saved = errno;
    (void)sig;
    frameCount = backtrace(frames, MAX_FRAMES);
    errno = saved;
}

static void backtraceInit(void) {
    void* warm[1];
    struct sigaction sa;
    gameThread = pthread_self();
    backtrace(warm, 1); /* loads the unwinder now, not inside the handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onBacktraceSignal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);
}

/* The executable's own symbols (.symtab: the game's functions are not
 * exported, so dladdr cannot name them), read once from /proc/self/exe. */
typedef struct {
    uintptr_t addr;
    uint64_t size;
    uint32_t name;
} Sym;

static Sym* syms;
static size_t symCount;
static char* strtab;
static uintptr_t loadBias;

static int firstObject(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    *(uintptr_t*)data = info->dlpi_addr; /* the first one is the executable */
    return 1;
}

static void loadSymbols(void) {
    static int done;
    if (done) {
        return;
    }
    done = 1;
    dl_iterate_phdr(firstObject, &loadBias);
    FILE* f = fopen("/proc/self/exe", "rb");
    if (f == NULL) {
        return;
    }
    Elf64_Ehdr eh;
    Elf64_Shdr* sh = NULL;
    Elf64_Sym* raw = NULL;
    if (fread(&eh, sizeof(eh), 1, f) != 1 || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_shentsize != sizeof(Elf64_Shdr)) {
        goto out;
    }
    sh = calloc(eh.e_shnum, sizeof(*sh));
    if (sh == NULL || fseek(f, (long)eh.e_shoff, SEEK_SET) != 0 || fread(sh, sizeof(*sh), eh.e_shnum, f) != eh.e_shnum) {
        goto out;
    }
    for (int pass = 0; pass < 2 && syms == NULL; pass++) {
        for (unsigned i = 0; i < eh.e_shnum; i++) {
            if (sh[i].sh_type != (pass == 0 ? SHT_SYMTAB : SHT_DYNSYM) || sh[i].sh_link >= eh.e_shnum) {
                continue;
            }
            const Elf64_Shdr* st = &sh[sh[i].sh_link];
            size_t n = sh[i].sh_size / sizeof(Elf64_Sym);
            raw = malloc(sh[i].sh_size);
            strtab = malloc(st->sh_size + 1);
            syms = malloc(n * sizeof(Sym));
            if (raw == NULL || strtab == NULL || syms == NULL || fseek(f, (long)sh[i].sh_offset, SEEK_SET) != 0 ||
                fread(raw, sizeof(Elf64_Sym), n, f) != n || fseek(f, (long)st->sh_offset, SEEK_SET) != 0 ||
                fread(strtab, 1, st->sh_size, f) != st->sh_size) {
                free(raw);
                free(strtab);
                free(syms);
                raw = NULL;
                strtab = NULL;
                syms = NULL;
                break;
            }
            strtab[st->sh_size] = '\0';
            for (size_t k = 0; k < n; k++) {
                if (ELF64_ST_TYPE(raw[k].st_info) == STT_FUNC && raw[k].st_value != 0 && raw[k].st_name < st->sh_size) {
                    syms[symCount].addr = (uintptr_t)raw[k].st_value + loadBias;
                    syms[symCount].size = raw[k].st_size;
                    syms[symCount].name = raw[k].st_name;
                    symCount++;
                }
            }
            break;
        }
    }
out:
    free(raw);
    free(sh);
    fclose(f);
}

/* "name+0xoff" for an address in the executable, else what dladdr knows. */
static void nameOf(void* p, char* out, size_t size) {
    uintptr_t a = (uintptr_t)p;
    const Sym* best = NULL;
    for (size_t i = 0; i < symCount; i++) {
        if (syms[i].addr <= a && (best == NULL || syms[i].addr > best->addr) &&
            (syms[i].size == 0 || a < syms[i].addr + syms[i].size)) {
            best = &syms[i];
        }
    }
    if (best != NULL && (best->size != 0 || a - best->addr < 0x10000)) {
        snprintf(out, size, "%s+0x%lx", strtab + best->name, (unsigned long)(a - best->addr));
        return;
    }
    Dl_info info;
    if (dladdr(p, &info) && info.dli_fname != NULL) {
        const char* base = strrchr(info.dli_fname, '/');
        base = base != NULL ? base + 1 : info.dli_fname;
        if (info.dli_sname != NULL) {
            snprintf(out, size, "%s+0x%lx (%s)", info.dli_sname, (unsigned long)(a - (uintptr_t)info.dli_saddr), base);
        } else {
            snprintf(out, size, "%s+0x%lx", base, (unsigned long)(a - (uintptr_t)info.dli_fbase));
        }
        return;
    }
    snprintf(out, size, "?");
}

static void printBacktrace(void) {
    frameCount = -1;
    if (pthread_kill(gameThread, SIGUSR2) != 0) {
        return;
    }
    for (int i = 0; i < 50 && frameCount < 0; i++) {
        SDL_Delay(10);
    }
    if (frameCount <= 0) {
        fprintf(stderr, "xenogears: no backtrace from the game thread\n");
        return;
    }
    loadSymbols();
    fprintf(stderr, "xenogears: the game thread is in (innermost first):\n");
    /* Frame 0 is the handler, frame 1 the kernel's signal return. */
    for (int i = 2; i < frameCount; i++) {
        char name[256];
        nameOf(frames[i], name, sizeof(name));
        fprintf(stderr, "    #%-2d %#014lx  %s\n", i - 2, (unsigned long)(uintptr_t)frames[i], name);
    }
}

#endif /* STALL_BACKTRACE */

/* ---- the watch ---- */

static void report(int polls, int dumped) {
    if (polls * pollMs < 1000) {
        fprintf(stderr, "xenogears: the game thread has reached no safe point for %d ms (vblank %u)\n", polls * pollMs,
                (unsigned)hwVSyncCount());
#ifdef STALL_BACKTRACE
        printBacktrace();
#endif
        fflush(stderr);
        return;
    }
    char gpu[512] = "";
    if (gpuDescribe != NULL) {
        gpuDescribe(gpu, sizeof(gpu));
    }
    fprintf(stderr,
            "xenogears: the game thread has reached no safe point for %.1f s (vblank %u): a game loop waiting for an "
            "interrupt without calling the SDK? (it needs PORT_YIELD); no vblank comes until it does: no new picture, "
            "pads, sound or window events\n",
            polls * pollMs / 1000.0, (unsigned)hwVSyncCount());
    if (gpu[0] != '\0') {
        fprintf(stderr, "xenogears: gpu: %s\n", gpu);
    }
#ifdef STALL_BACKTRACE
    printBacktrace();
#endif
    if (!dumped && gpuServeStalledDump != NULL) {
#ifdef _WIN32
        fprintf(stderr, "xenogears: Ctrl+Break in the console writes VRAM while it lasts\n");
#else
        fprintf(stderr, "xenogears: kill -USR1 %ld writes VRAM while it lasts\n", (long)getpid());
#endif
    }
    fflush(stderr);
}

static int watch(void* arg) {
    uint32_t last = hwHeartbeat();
    int still = 0;
    int reported = 0;

    (void)arg;
    for (;;) {
        SDL_Delay(pollMs);
        uint32_t now = hwHeartbeat();
        if (now != last) {
            /* A short one's length is XG_CPU_TIMING's (the polls pause while
             * the backtrace is taken). */
            if (reported && still * pollMs < 1000) {
                fprintf(stderr, "xenogears: the game thread reached a safe point again (vblank %u)\n",
                        (unsigned)hwVSyncCount());
            } else if (reported) {
                fprintf(stderr, "xenogears: the game thread reached a safe point again after %.1f s (vblank %u)\n",
                        still * pollMs / 1000.0, (unsigned)hwVSyncCount());
            }
            last = now;
            still = 0;
            reported = 0;
            continue;
        }
        if (hwVSyncCount() == 0 || quitRequested()) {
            continue; /* not running yet, or on its way out (quit.c's watchdog) */
        }
        /* Counted in polls, not by the clock: a debugger stopping the whole
         * process is no stall. */
        still++;
        int dumped = still * pollMs >= DUMP_MS && gpuServeStalledDump != NULL && gpuServeStalledDump();
        if (!reported && (still >= reportPolls || dumped)) {
            report(still, dumped);
            reported = 1;
        } else if (dumped) {
#ifdef STALL_BACKTRACE
            printBacktrace();
#endif
        }
    }
    return 0;
}

void stallInit(void) {
    static int done;
    const char* ms = getenv("XG_STALL_MS");

    if (done) {
        return;
    }
    done = 1;
    int limit = ms != NULL && ms[0] != '\0' ? atoi(ms) : 2000;
    if (limit > 0 && limit < 3 * POLL_MS) {
        pollMs = limit / 3 < 10 ? 10 : limit / 3;
    }
    reportPolls = limit / pollMs;
    if (reportPolls <= 0) {
        return;
    }
#ifdef STALL_BACKTRACE
    backtraceInit();
#endif
    SDL_Thread* t = SDL_CreateThread(watch, "stall watch", NULL);
    if (t != NULL) {
        SDL_DetachThread(t);
    }
}
