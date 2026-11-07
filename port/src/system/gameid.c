/* The running game's identity (include/pc_gameid.h, gameid.h). */

#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "gameid.h"
#include "pc_gameid.h"
#include "savestate.h"

#define TAIL_OFFSET 0x1E00 /* in the save's data (block +0x1F00) */
static const char MAGIC[4] = {'X', 'G', 'I', 'D'};

static unsigned gameId;    /* 0: not saved yet, or a save without one */
static int gameFresh = 1;  /* the kernel's new game, nothing loaded */

static void changed(int assigned) {
    if (pcBestiaryGameChanged != NULL) pcBestiaryGameChanged(gameId, gameFresh, assigned);
}

static unsigned newId(void) {
    static unsigned serial;
    uint64_t a = (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull;
    a ^= SDL_GetPerformanceCounter() * 0xC2B2AE3D27D4EB4Full;
    a ^= (uint64_t)++serial << 48;
    a ^= a >> 29;
    a *= 0xBF58476D1CE4E5B9ull;
    a ^= a >> 32;
    unsigned id = (unsigned)a;
    return id != 0 ? id : 1;
}

unsigned portGameId(void) { return gameId; }
int portGameFresh(void) { return gameFresh; }

void pcGameIdNewGame(void) {
    gameId = 0;
    gameFresh = 1;
    changed(0);
}

void pcGameIdLoadTail(const u8* data) {
    const u8* t = data + TAIL_OFFSET;
    gameId = memcmp(t, MAGIC, 4) == 0 ? (unsigned)t[4] | (unsigned)t[5] << 8 | (unsigned)t[6] << 16 | (unsigned)t[7] << 24 : 0;
    gameFresh = 0;
    changed(0);
}

void pcGameIdSaveTail(u8* data) {
    u8* t = data + TAIL_OFFSET;
    if (gameId == 0) {
        gameId = newId();
        changed(1);
        gameFresh = 0;
    }
    memcpy(t, MAGIC, 4);
    t[4] = (u8)gameId;
    t[5] = (u8)(gameId >> 8);
    t[6] = (u8)(gameId >> 16);
    t[7] = (u8)(gameId >> 24);
}

void gameIdStateIo(XgState* s) {
    XG_STATE_VAR(s, gameId);
    XG_STATE_VAR(s, gameFresh);
    if (xgStateLoading(s)) changed(0);
}
