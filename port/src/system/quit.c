/*
 * Quitting: the window's close button, SIGINT and SIGTERM (Ctrl+C or the
 * console closing on Windows).
 *
 * The game never polls for a quit (the PS1 had nothing to poll), so a request
 * only sets a flag, and the game thread acts on it at its next safe point,
 * whatever the game is doing: in any mode, inside an interrupt callback, with
 * interrupts masked, in a movie or a load. It shuts the GPU down, quits SDL
 * (which closes the audio output) and exits with status 0.
 *
 * Where no safe point comes (the game stuck in a loop that calls nothing), a
 * watchdog thread ends the process QUIT_GRACE_MS after the request, and
 * names the step it was stuck in; the shutdown itself gets as long again once
 * it has started (a loaded machine can take a while to idle the GPU). A
 * second signal (half a second or more after the first) ends it at once.
 *
 * The watchdog keeps its own clock: SDL_Quit resets SDL's tick count, and an
 * SDL_GetTicksNS difference taken across it wrapped around and forced exit 1
 * whenever the watchdog woke between SDL_Quit and the end of the process.
 */

#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define write _write
#else
#include <unistd.h>
#endif

#include "cores.h"
#include "rumble.h"
#include "system.h"
#ifdef __ANDROID__
#include <sys/ioctl.h>

#include "android.h"

/* The exit handlers and static destructors this library registered (bionic keys
 * each atexit() to its caller's library), and only those. */
extern void __cxa_finalize(void* dso);
extern void* __dso_handle;
#endif

#define QUIT_GRACE_MS 3000 /* request to forced exit (and again for the shutdown steps) */
#define WATCH_PERIOD_MS 100
#define PUMP_PERIOD_NS 50000000ull   /* window events while vblanks are held back */
#define SECOND_SIGNAL_MS 500         /* a signal this soon after the first is the same request */

void gpuShutdown(void);
void rewindShutdown(void) __attribute__((weak));
void workersShutdown(void) __attribute__((weak)); /* workers.c: the helper threads, before SDL_Quit */
void updateShutdown(void) __attribute__((weak));  /* update/update_check.c (XG_UPDATER): its check's thread */

int engineStartAfter(const char* exe); /* engine.c */

static volatile sig_atomic_t requested; /* 0, or the signal number (-1: the window) */
static char startAfter[2048];           /* quitAndStart: the executable started once the rest has shut down */
static volatile sig_atomic_t quitting;
static uint64_t lastPump;
static const char* volatile step = "the game thread reaching a safe point"; /* for the watchdog's message */

static void say(const char* msg) {
    /* async-signal-safe: no stdio */
    size_t n = 0;
    while (msg[n] != '\0') {
        n++;
    }
    (void)!write(2, msg, (unsigned)n);
}

/* Monotonic milliseconds, async-signal-safe and independent of SDL's state. */
static uint64_t monotonicMs(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

static void onSignal(int sig) {
    static volatile uint64_t firstMs;

    if (requested != 0) {
        /* timeout(1) signals the process and then its group: a copy that
         * soon after is the same request. */
        if (monotonicMs() - firstMs >= SECOND_SIGNAL_MS) {
            say("xenogears: second signal, exiting now\n");
            _exit(128 + sig);
        }
        return;
    }
    firstMs = monotonicMs();
    requested = sig;
#ifdef _WIN32
    signal(sig, onSignal); /* the CRT resets the handler before calling it */
#endif
}

static int watchdog(void* arg) {
    uint64_t since = 0;
    int shutdownSeen = 0;

    (void)arg;
    for (;;) {
        SDL_Delay(WATCH_PERIOD_MS);
        if (requested == 0 && !quitting) {
            continue;
        }
        uint64_t now = monotonicMs();
        if (since == 0 || (quitting && !shutdownSeen)) {
            since = now; /* the request seen, or the shutdown started: its own grace */
            shutdownSeen = quitting;
        } else if (now - since > QUIT_GRACE_MS) {
            say("xenogears: quit did not finish in time (stuck in: ");
            say(step);
            say("), exiting now\n");
            _exit(requested > 0 ? 128 + requested : 1);
        }
    }
    return 0;
}

void quitInit(void) {
    static int done;

    if (done) {
        return;
    }
    done = 1;
    /* Ours replace SDL's (which turn the signals into an SDL_EVENT_QUIT that
     * only an event pump would see). */
#ifdef _WIN32
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
#else
    struct sigaction sa = {0};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
#endif
    SDL_Thread* t = hostThreadCreate(watchdog, "quit watchdog", NULL);
    if (t != NULL) {
        SDL_DetachThread(t);
    }
}

void quitRequest(void) {
    if (requested == 0) {
        requested = -1;
    }
}

int quitRequested(void) {
    return requested != 0 || quitting;
}

/* The end of the process. On a desktop, exit(): the exit handlers the port
 * registers (settings, save states, the timing summaries) run and the process
 * is gone.
 *
 * An Android app is not alone in its process (xg-mrj.3.16): the activity's own
 * threads (RenderThread, hwui's workers) are still running, and exit() would
 * also run the exit handlers and static destructors of every other library in
 * it under their feet. About one quit in four then died in
 * "FORTIFY: pthread_mutex_lock called on a destroyed mutex" (SIGABRT) after
 * the game had finished. So here only this library's handlers run
 * (__cxa_finalize of its own handle: what unloading it would do), the log's
 * pipe (the loader sends stdout and stderr to logcat through one) is given a
 * moment to empty, and the process ends without running anyone else's. */
static void processEnd(void) {
#ifdef __ANDROID__
    __cxa_finalize(&__dso_handle);
    fflush(NULL);
    for (int i = 0; i < 50; i++) { /* at most half a second */
        int unread = 0;
        if (ioctl(2, FIONREAD, &unread) != 0 || unread == 0) {
            break;
        }
        usleep(10000);
    }
    usleep(20000); /* the pump's last line on its way to logcat */
    _exit(0);
#else
    exit(0);
#endif
}

void quitNow(void) {
    if (quitting) {
        return; /* a platform call made while shutting down */
    }
    step = "rewind shutdown";
    if (rewindShutdown) rewindShutdown();
    step = "workers shutdown";
    if (workersShutdown) workersShutdown();
    step = "update check shutdown";
    if (updateShutdown) updateShutdown();
    step = "GPU shutdown";
    quitting = 1;
    fprintf(stderr, "xenogears: quit\n");
    gpuShutdown();
    rumbleStopAll(); /* no pad left vibrating */
#ifdef __ANDROID__
    /* While SDL can still reach the activity: it finishes and takes its task out of the
     * recent apps, so nothing of a game that has quit is left to come back to. */
    androidEndTask();
#endif
    step = "SDL_Quit";
    SDL_Quit();
    if (startAfter[0] != '\0') {
        step = "start after";
        fprintf(stderr, "xenogears: starting %s\n", startAfter);
        if (!engineStartAfter(startAfter)) fprintf(stderr, "xenogears: could not start %s\n", startAfter);
    }
    step = "exit handlers";
    fflush(NULL);
    processEnd();
}

void quitAndStart(const char* exe) {
    snprintf(startAfter, sizeof startAfter, "%s", exe);
    quitNow();
}

void quitPoll(int pump) {
    if (requested != 0) {
        quitNow();
    }
    if (!pump || quitting) {
        return;
    }
    /* Vblanks are held back (a callback running, interrupts masked), so
     * padVBlank's event pump does not run: look for the window closing. */
    uint64_t now = SDL_GetTicksNS();
    if (now - lastPump < PUMP_PERIOD_NS) {
        return;
    }
    lastPump = now;
    SDL_PumpEvents();
    if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) {
        quitNow();
    }
}
