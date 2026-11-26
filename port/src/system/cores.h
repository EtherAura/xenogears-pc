#ifndef PORT_CORES_H
#define PORT_CORES_H

/*
 * The game thread on a hybrid CPU's fast cores (cores.c, xg-hyd0).
 *
 * A scheduler that sees every core as equal (Intel's P- and E-cores report
 * the same cpu_capacity to Linux; sched_ext schedulers such as scx_lavd use
 * it) moves the game thread onto a slow core for seconds at a time, where a
 * busy room's drawing takes about 1.4 times as long and vblanks come late
 * (the picture stutters). coresPinGameThread keeps the calling thread on the
 * fast cores; threads started with hostThreadCreate begin on every core the
 * process had, so the pin never crowds the background work onto the fast
 * cores too, and those started with hostThreadCreateFast (helpers the game
 * thread waits for: its work split in parts) on the game thread's cores.
 * Linux desktops only; elsewhere all are plain.
 */

#include <SDL3/SDL.h>

/* main.c, on the game thread before the game starts. The setting fast_cores
 * (auto, the default, or off; XG_FAST_CORES for one run). */
void coresPinGameThread(void);

#if defined(__linux__) && !defined(__ANDROID__)
/* The calling thread on the game thread's cores (fast) or on every core the
 * process had before the pin; nothing before it (weak: the programs that link
 * a worker's file without cores.c start it as is). */
void coresPlace(int fast) __attribute__((weak));

typedef struct {
    SDL_ThreadFunction fn;
    void* data;
    int fast;
} CoresThreadStart;

static inline int coresThreadStart(void* p) {
    CoresThreadStart s = *(CoresThreadStart*)p;
    SDL_free(p);
    coresPlace(s.fast);
    return s.fn(s.data);
}

static inline SDL_Thread* coresThreadCreate(SDL_ThreadFunction fn, const char* name, void* data, int fast) {
    CoresThreadStart* s;
    SDL_Thread* t;
    if (coresPlace == NULL || (s = (CoresThreadStart*)SDL_malloc(sizeof(*s))) == NULL) {
        return SDL_CreateThread(fn, name, data);
    }
    s->fn = fn;
    s->data = data;
    s->fast = fast;
    t = SDL_CreateThread(coresThreadStart, name, s);
    if (t == NULL) {
        SDL_free(s);
    }
    return t;
}

/* SDL_CreateThread for the port's own threads: the new thread on every core
 * the process had, whichever thread starts it. */
static inline SDL_Thread* hostThreadCreate(SDL_ThreadFunction fn, const char* name, void* data) {
    return coresThreadCreate(fn, name, data, 0);
}
/* The same for a helper the game thread waits for: on the game thread's cores. */
static inline SDL_Thread* hostThreadCreateFast(SDL_ThreadFunction fn, const char* name, void* data) {
    return coresThreadCreate(fn, name, data, 1);
}
/* Around a call on the game thread that makes threads of SDL's own (the sound device's): 1
 * before, the calling thread on every core so they start there too; 0 after, back on the fast
 * cores. A sound thread kept to busy fast cores ran the device dry (the 4x battle's real-clock
 * test, xg-hyd0). */
static inline void hostLibraryThreadsAnywhere(int begin) {
    if (coresPlace != NULL) coresPlace(begin ? 0 : 1);
}
#else
#define hostThreadCreate(fn, name, data) SDL_CreateThread(fn, name, data)
#define hostThreadCreateFast(fn, name, data) SDL_CreateThread(fn, name, data)
#define hostLibraryThreadsAnywhere(begin) ((void)0)
#endif

#endif
