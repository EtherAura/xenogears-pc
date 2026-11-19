/*
 * The stall watchdog (stall.c) and the app's lifecycle (xg-mrj.3.19): no
 * report while the app is in the background, however long, nor a recovery
 * line on the return, and a stall in the foreground still reported. SDL's
 * lifecycle events go through SDL_PushEvent to stall.c's own event watch; the
 * watch thread's polls are stallPoll calls, the game's safe points a counter.
 * No thread, no sleep, no window.
 */

#include <SDL3/SDL.h>
#include <stdio.h>

#include "system/system.h"

static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"

static uint32_t heartbeat;

uint32_t hwHeartbeat(void) {
    return heartbeat;
}

uint32_t hwVSyncCount(void) {
    return 100; /* running */
}

int quitRequested(void) {
    return 0;
}

static int app(SDL_EventType type) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    return SDL_PushEvent(&e);
}

/* n polls without a safe point; how many reported, how many recovered. */
static void polls(int n, int* reports, int* recoveries) {
    for (int i = 0; i < n; i++) {
        int r = stallPoll();
        *reports += r == STALL_REPORTED;
        *recoveries += r == STALL_RECOVERED;
    }
}

static int run(void) {
    int reports = 0, recoveries = 0;

    CHECK(SDL_Init(SDL_INIT_EVENTS));
    int limit = stallSetup("2000");
    CHECK(limit > 1);

    /* Running: a safe point each poll is no stall. */
    for (int i = 0; i < 3 * limit; i++) {
        heartbeat++;
        polls(1, &reports, &recoveries);
    }
    CHECK(reports == 0 && recoveries == 0);

    /* A stall in the foreground: one report at the limit, then recovered. */
    polls(limit - 1, &reports, &recoveries);
    CHECK(reports == 0);
    polls(1, &reports, &recoveries);
    CHECK(reports == 1);
    polls(limit, &reports, &recoveries);
    CHECK(reports == 1);
    heartbeat++;
    polls(1, &reports, &recoveries);
    CHECK(recoveries == 1);

    /* Home: SDL sends both "to the background" events and blocks the game
     * thread, for three limits. */
    reports = recoveries = 0;
    heartbeat++;
    polls(1, &reports, &recoveries);
    polls(limit / 2, &reports, &recoveries); /* stuck a while already as it goes */
    CHECK(app(SDL_EVENT_WILL_ENTER_BACKGROUND));
    CHECK(app(SDL_EVENT_DID_ENTER_BACKGROUND));
    polls(3 * limit, &reports, &recoveries);
    CHECK(reports == 0);

    /* Back: the game has not reached its first safe point yet for most of a
     * limit; then it does, and that is no recovery. */
    CHECK(app(SDL_EVENT_WILL_ENTER_FOREGROUND));
    CHECK(app(SDL_EVENT_DID_ENTER_FOREGROUND));
    polls(limit - 1, &reports, &recoveries);
    CHECK(reports == 0);
    heartbeat++;
    polls(1, &reports, &recoveries);
    CHECK(reports == 0 && recoveries == 0);

    /* Away and back between two polls. */
    polls(limit / 2, &reports, &recoveries);
    CHECK(app(SDL_EVENT_WILL_ENTER_BACKGROUND));
    CHECK(app(SDL_EVENT_DID_ENTER_FOREGROUND));
    polls(limit - 1, &reports, &recoveries);
    CHECK(reports == 0);
    heartbeat++;
    polls(1, &reports, &recoveries);
    CHECK(reports == 0 && recoveries == 0);

    /* A fresh stall after the return is still one: reported at the limit. */
    CHECK(app(SDL_EVENT_WILL_ENTER_BACKGROUND));
    polls(limit, &reports, &recoveries);
    CHECK(app(SDL_EVENT_DID_ENTER_FOREGROUND));
    polls(1, &reports, &recoveries); /* the poll that sees the return */
    polls(limit - 1, &reports, &recoveries);
    CHECK(reports == 0);
    polls(1, &reports, &recoveries);
    CHECK(reports == 1);
    heartbeat++;
    polls(1, &reports, &recoveries);
    CHECK(recoveries == 1);

    SDL_Quit();
    return 0;
}

int main(void) {
    int status = run();
    printf("stallwatchtest: %d checks, %s\n", checks, status == 0 ? "ok" : "FAILED");
    return status;
}
