/* The game's identity (gameid.c): a fresh game has none; its first save
 * gives it one, written into the save data's free tail; loading reads it
 * back; a tail without one is no id. The record keeper's hook sees each
 * change. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pc_gameid.h"
#include "system/gameid.h"

static int changes, assigned;
static unsigned lastId;
static int lastFresh;
void pcBestiaryGameChanged(unsigned id, int fresh, int isAssigned) {
    changes++;
    assigned += isAssigned;
    lastId = id;
    lastFresh = fresh;
}

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
    static u8 data[0x1F00];

    pcGameIdNewGame();
    CHECK(portGameId() == 0 && portGameFresh() == 1);
    CHECK(changes == 1 && lastId == 0 && lastFresh == 1 && assigned == 0);

    memset(data, 0, sizeof(data));
    pcGameIdSaveTail(data);
    unsigned id = portGameId();
    CHECK(id != 0 && portGameFresh() == 0);
    CHECK(assigned == 1 && lastId == id && lastFresh == 1); /* assigned while still fresh */
    CHECK(memcmp(data + 0x1E00, "XGID", 4) == 0);
    CHECK(data[0x1E04] == (u8)id && data[0x1E07] == (u8)(id >> 24));
    /* only the tail is touched */
    int clean = 1;
    for (int i = 0; i < 0x1E00; i++) clean &= data[i] == 0;
    for (int i = 0x1E08; i < 0x1F00; i++) clean &= data[i] == 0;
    CHECK(clean);

    pcGameIdSaveTail(data); /* a second save keeps the id */
    CHECK(portGameId() == id && assigned == 1);

    pcGameIdNewGame();
    CHECK(portGameId() == 0 && portGameFresh() == 1);
    pcGameIdLoadTail(data);
    CHECK(portGameId() == id && portGameFresh() == 0 && lastId == id && lastFresh == 0);

    memset(data, 0, sizeof(data));
    pcGameIdLoadTail(data); /* a save written without an id */
    CHECK(portGameId() == 0 && portGameFresh() == 0);
    pcGameIdSaveTail(data); /* gets one now, and the hook sees it was not fresh */
    CHECK(portGameId() != 0 && portGameId() != id && assigned == 2 && lastFresh == 0);

    printf(failures ? "gameidtest: %d failure(s)\n" : "gameidtest: ok\n", failures);
    return failures != 0;
}
