/*
 * PC start options that the game code asks for.
 *
 *   XG_BOOT_MODE=n   start in game mode n after the boot logo instead of the
 *                    opening movie (src/main/kernel.c gameModes): 0 is the
 *                    kernel's debug boot menu, 1 field, 2 battle, 3 worldmap,
 *                    4 battling, 5 menu, 6 movie
 *
 * A development start (XG_BOOT_MODE, or XG_FIELD alone) can also preset the
 * game state. The kernel applies these to gameState (0x8006D634 on the US
 * PS1; its layout is the PS1's) after initGameState and the boot logo, just
 * before the first mode starts (portStartState), in this order:
 *
 *   XG_PRESET=a,b    named presets (below)
 *   XG_PARTY=a,b,c   the party: character ids in m1D34_currentParty (255 =
 *                    empty slot), also added to the m1D30 member bits
 *   XG_GAMESTATE=... any fields: "OFF[:SIZE]=VALUE" items separated by
 *                    commas; OFF is the byte offset in gameState, SIZE 1, 2
 *                    (default) or 4 bytes, numbers decimal or 0x hex. Field
 *                    script variable n is at 0x1930 + 2n.
 *   XG_FIELD=n       start the field mode in field n (m231A_fieldID); on its
 *                    own it implies XG_BOOT_MODE=1
 *
 * Presets:
 *   disc2            field variable 82 (the current disc, 0 or 1) = 1, as the
 *                    game sets it from the inserted disc when a save loads;
 *                    start the run on disc 2 too (XG_DISC=<disc 2 .cue>)
 *
 * PCSX-Redux gets the same state with tools/start_state.py, which reads the
 * same variables and writes the same fields there.
 *
 * The extras' cutscene replay (port/src/extras/gallery.cpp) asks for such a
 * start from inside the program (portRequestStart): a field, its entrance
 * (gameState m2320, which the field takes as its variable 2), the story
 * progress (field variable 0) and the disc. It is applied once, by the next
 * kernelMain: the first one after `xenogears --extras`, or the soft reset
 * the game does for it at its next checkSoftReboot (kernel.c), as for
 * Start+Select+L1+R1; the disc it wants is put in first (portStartPrepare).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/game_state.h"
#include "start.h"
#include "savestate.h"

/* Texture-dump context only; the renderer does not depend on game structs. */
int gpuTextureField(void) { return gameState.m231A_fieldID; }

static int envSet(const char* name) {
    const char* s = getenv(name);
    return s != NULL && s[0] != '\0';
}

/* The text of a start option, or NULL when it is unset or empty (for game-side
 * option readers compiled without libc, e.g. port/src/game/worldmap/start.c). */
const char* portOption(const char* name) {
    const char* s = getenv(name);
    return s != NULL && s[0] != '\0' ? s : NULL;
}

/* The extras' start request (see above). */
static struct {
    int pending; /* asked for: the next kernelMain applies it */
    int active;  /* this boot applied it: field mode until the next reset */
    PortStartRequest request;
} start;

/* system/netplay.c (weak: tests link this file without it). */
int netplayRefuses(const char* what) __attribute__((weak)); /* system/netplay.c: not in an online session */

int portRequestStart(const PortStartRequest* request) {
    if (netplayRefuses != NULL && netplayRefuses("a start from the extras")) return 0; /* one side alone: apart */
    if (request == NULL || request->field < 0 || request->field > 729 ||
        request->entry < 0 || request->entry > 255 || request->progress < 0 || request->progress > 0x7fff ||
        request->disc < 1 || request->disc > 2 || request->variableCount < 0 ||
        request->variableCount > PORT_START_VARIABLES) return 0;
    /* OP_INIT_ENTITY_PC assigns the player/camera actor only to party slot
     * zero. An empty leader leaves that setup missing, even with followers. */
    if (request->party[0] < 0 || request->party[0] > 10) return 0;
    int members = 0;
    for (int i = 0; i < 3; i++) {
        int id = request->party[i];
        if (id == 255) continue;
        if (id < 0 || id > 10 || (members & (1 << id))) return 0;
        members |= 1 << id;
    }
    for (int i = 0; i < request->variableCount; i++) {
        const PortStartVariable* v = &request->variables[i];
        if (v->index < 2 || v->index >= 512 || v->index == 82 || v->value < -32768 || v->value > 65535) return 0;
        for (int k = 0; k < i; k++) if (request->variables[k].index == v->index) return 0;
    }
    start.request = *request;
    start.pending = 1;
    fprintf(stderr, "options: start field %d, entrance %d, story %d, disc %d, party %d,%d,%d, %d variables requested\n",
            request->field, request->entry, request->progress, request->disc,
            request->party[0], request->party[1], request->party[2], request->variableCount);
    return 1;
}

int portStartPending(void) {
    return start.pending;
}

int portSceneReplayActive(void) {
    return start.active;
}

/* port/src/cd (cd.h): the disc the start wants, put in as before a reset. */
int cdBootDiscNumber(void);
int cdSwapForReset(void);
void cdBootTablesReload(void);

void portStartPrepare(void) {
    start.active = 0;
    if (!start.pending) {
        return;
    }
    if (cdBootDiscNumber() != start.request.disc && !cdSwapForReset()) {
        fprintf(stderr, "options: disc %d is not at hand; the start uses the disc in the drive\n", start.request.disc);
        start.request.disc = cdBootDiscNumber();
    }
    cdBootTablesReload(); /* kernelMain takes the file tables of the disc in the drive */
}

int portBootMode(void) {
    if (start.pending || start.active) {
        return 1;
    }
    const char* s = getenv("XG_BOOT_MODE");
    if (s == NULL || s[0] == '\0') {
        return envSet("XG_FIELD") ? 1 : -1;
    }
    int mode = atoi(s);
    return mode >= 0 && mode <= 6 ? mode : -1;
}

static void poke(unsigned long off, int size, unsigned long value) {
    u8* p = (u8*)&gameState + off;

    if (off + (unsigned long)size > sizeof(GameState)) {
        fprintf(stderr, "options: gameState offset 0x%lX is past its end\n", off);
        return;
    }
    for (int i = 0; i < size; i++) {
        p[i] = (u8)(value >> (8 * i)); /* little-endian, as on the PS1 */
    }
}

/* "OFF[:SIZE]=VALUE,..." */
static void pokeList(const char* s) {
    while (s != NULL && *s != '\0') {
        char* end;
        unsigned long off = strtoul(s, &end, 0);
        int size = 2;
        if (end == s) {
            break;
        }
        if (*end == ':') {
            size = (int)strtol(end + 1, &end, 0);
        }
        if (*end != '=' || (size != 1 && size != 2 && size != 4)) {
            fprintf(stderr, "options: XG_GAMESTATE: expected OFF[:SIZE]=VALUE at '%s'\n", s);
            return;
        }
        s = end + 1;
        unsigned long value = strtoul(s, &end, 0);
        poke(off, size, value);
        s = *end == ',' ? end + 1 : NULL;
    }
}

static void preset(const char* name, size_t len) {
    if (len == 5 && strncmp(name, "disc2", 5) == 0) {
        gameState.m1930_fieldVarsBackup[82] = 1;
    } else {
        fprintf(stderr, "options: unknown XG_PRESET '%.*s'\n", (int)len, name);
    }
}

void portStartState(void) {
    if (start.pending) {
        /* A fresh initial state plus this request only. Development env
         * overrides are deliberately excluded from gallery starts. */
        start.pending = 0;
        start.active = 1;
        const PortStartRequest* r = &start.request;
        gameState.m1930_fieldVarsBackup[0] = (u16)r->progress;
        gameState.m1930_fieldVarsBackup[82] = r->disc == 2;
        gameState.m2320_worldmapMode = (u16)r->entry;
        gameState.m231A_fieldID = (u16)r->field;
        gameState.m1D30_partyMemberBitField = 0;
        for (int i = 0; i < 3; i++) {
            gameState.m1D34_currentParty[i] = (u8)r->party[i];
            gameState.m22B1_isOnGear[i] = 0;
            if (r->party[i] != 255) gameState.m1D30_partyMemberBitField |= 1 << r->party[i];
        }
        for (int i = 0; i < r->variableCount; i++) {
            gameState.m1930_fieldVarsBackup[r->variables[i].index] = (u16)r->variables[i].value;
        }
        return;
    }
    const char* s = getenv("XG_PRESET");
    while (s != NULL && *s != '\0') {
        const char* end = strchr(s, ',');
        size_t len = end != NULL ? (size_t)(end - s) : strlen(s);
        preset(s, len);
        s = end != NULL ? end + 1 : NULL;
    }
    s = getenv("XG_PARTY");
    if (s != NULL && s[0] != '\0') {
        for (int i = 0; i < 3; i++) {
            char* end;
            long id = strtol(s, &end, 0);
            gameState.m1D34_currentParty[i] = end != s ? (u8)id : 0xFF;
            if (end != s && id >= 0 && id < 16) {
                gameState.m1D30_partyMemberBitField |= 1 << id;
            }
            s = *end == ',' ? end + 1 : end;
        }
    }
    pokeList(getenv("XG_GAMESTATE"));
    s = getenv("XG_FIELD");
    if (s != NULL && s[0] != '\0') {
        gameState.m231A_fieldID = (u16)strtol(s, NULL, 0);
    }
}

/* Save states (savestate.h): a start asked for or applied by this boot. */
void optionsStateIo(XgState* s) {
    XG_STATE_VAR(s, start);
}
