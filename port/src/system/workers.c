/*
 * Helper threads for work the game thread splits into equal parts
 * (workers.h). The threads are made on first use and wait on a semaphore
 * between jobs; a job is one function run on every thread at once, the
 * caller's included, and workersRun returns when the last is done. Host
 * only: nothing here is state the game sees.
 */
#include "workers.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

static struct {
    int count; /* threads in use, the caller's included; 0 before the first job */
    SDL_Thread* thread[WORKERS_MAX];
    SDL_Semaphore* go[WORKERS_MAX];
    SDL_Semaphore* done[WORKERS_MAX];
    SDL_AtomicInt stop;
    void (*fn)(void* arg, int part, int parts);
    void* arg;
} pool;

static int worker(void* arg) {
    int part = (int)(intptr_t)arg;
    for (;;) {
        SDL_WaitSemaphore(pool.go[part]);
        if (SDL_GetAtomicInt(&pool.stop)) return 0;
        pool.fn(pool.arg, part, pool.count);
        SDL_SignalSemaphore(pool.done[part]);
    }
}

/* At quit (quit.c quitNow, before SDL_Quit) and at exit: the helpers told
 * to stop and joined, once. The stop flag stays set and nothing is freed:
 * at exit SDL_Quit has run (SDL3 then treats the threads as invalid objects
 * and SDL_WaitThread returns at once), so a helper may still be waking from
 * its semaphore while the process ends. */
void workersShutdown(void) {
    if (pool.count > 1 && !SDL_GetAtomicInt(&pool.stop)) {
        SDL_SetAtomicInt(&pool.stop, 1);
        for (int i = 1; i < pool.count; i++) SDL_SignalSemaphore(pool.go[i]);
        for (int i = 1; i < pool.count; i++) SDL_WaitThread(pool.thread[i], NULL);
    }
}

static void poolStart(void) {
    if (pool.count > 0) return;
    int helpers = WORKERS_HELPERS;
    const char* e = getenv("XG_WORKERS");
    if (e != NULL && *e != '\0') helpers = atoi(e);
    if (helpers < 0) helpers = 0;
    if (helpers > WORKERS_MAX - 1) helpers = WORKERS_MAX - 1;
    pool.count = 1;
    for (int i = 1; i <= helpers; i++) {
        pool.go[i] = SDL_CreateSemaphore(0);
        pool.done[i] = SDL_CreateSemaphore(0);
        pool.thread[i] = pool.go[i] && pool.done[i] ? SDL_CreateThread(worker, "worker", (void*)(intptr_t)i) : NULL;
        if (pool.thread[i] == NULL) {
            if (pool.go[i]) SDL_DestroySemaphore(pool.go[i]);
            if (pool.done[i]) SDL_DestroySemaphore(pool.done[i]);
            pool.go[i] = pool.done[i] = NULL;
            break;
        }
        pool.count = i + 1;
    }
    atexit(workersShutdown);
}

int workersRun(void (*fn)(void* arg, int part, int parts), void* arg) {
    poolStart();
    if (SDL_GetAtomicInt(&pool.stop)) { /* after the shutdown: the caller alone */
        fn(arg, 0, 1);
        return 1;
    }
    pool.fn = fn;
    pool.arg = arg;
    for (int i = 1; i < pool.count; i++) SDL_SignalSemaphore(pool.go[i]);
    fn(arg, 0, pool.count);
    for (int i = 1; i < pool.count; i++) SDL_WaitSemaphore(pool.done[i]);
    return pool.count;
}
