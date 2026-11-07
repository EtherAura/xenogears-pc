/*
 * Disc swaps (cd.h). Xenogears has two discs: the game asks for the other one
 * at the disc 1 to 2 transition and when a save from the other disc is
 * loaded. It asks the way it would on a PS1 (the menu's checkInsertedDisc,
 * the movie debug menu's disc change test): it stops the drive, shows
 * "insert disc N", then polls Getstat until the shell opens, until it closes
 * again and until the motor is on, and reads the disc's ID sector ("_XEN"
 * and the disc digit at LBA 0x17). The drive models the shell
 * (cdChangeDisc), so the game's own check runs unchanged.
 *
 * The other disc: disc2 (settings.ini, XG_DISC2 overrides; when the run did
 * not start on it), else the image beside the first one with "Disc 1" and
 * "Disc 2" exchanged in its path (bin/Xenogears (USA) (Disc 2)/Xenogears
 * (USA) (Disc 2).cue).
 *
 * When the swap happens:
 *   - automatically (disc_swap=auto in settings.ini or XG_DISC_SWAP, the
 *     default) when the game waits for the shell to open: a Stop, then only
 *     Getstat polls, AUTO_POLLS of them;
 *   - with the swap key (F2, pad.c) or the settings menu (port/src/ui), but
 *     only while the game waits for a disc (cdSwapRequest). Any other time a
 *     new disc under the running game makes its next read come from the
 *     wrong disc and the game breaks, as a PS1 does when the lid is opened
 *     mid-game;
 *   - disc_swap=manual: only with the key or the menu.
 * Each swap puts in the other disc, so a wrong disc is fixed by the game's
 * next request.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd.h"
#include "cd_internal.h"
#include "verify.h"
#include "system/hw.h"
#include "system/settings.h"
#include "../system/savestate.h"

#define AUTO_POLLS 4 /* the game polls every 1 to 3 vblanks: 4 polls are 70-200 ms */
#define MAX_PATH_LEN 4096

static char paths[2][MAX_PATH_LEN]; /* [0] the disc in the drive, [1] the other one ("" if none) */
static int manualOnly;
static int afterStop;  /* the last command other than Getstat was Stop */
static int polls;      /* Getstat polls since */
static int pending;
static int warned;

static int readable(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

void cdSwapInit(const char* firstDisc) {
    settingsDescribe("disc2", "disc2: the other disc's image (empty: the Disc 2 image beside the first one)");
    settingsDescribe("disc_swap", "disc_swap: auto (swap discs when the game asks) or manual (F2 or the menu only)");
    const char* env = settingsGet("disc2");
    const char* how = settingsGet("disc_swap");

    manualOnly = how != NULL && strcmp(how, "manual") == 0;
    snprintf(paths[0], sizeof paths[0], "%s", firstDisc);
    paths[1][0] = '\0';
    if (env != NULL && env[0] != '\0' && strcmp(env, firstDisc) != 0) {
        snprintf(paths[1], sizeof paths[1], "%s", env);
    } else if (!cdImageSibling(firstDisc, paths[1], sizeof paths[1]) || !readable(paths[1])) {
        paths[1][0] = '\0';
    }
    if (paths[1][0]) {
        CdImageInfo info[2]; char why[1024];
        if (!cdVerifyPair(paths[0], paths[1], NULL, info, why, sizeof why)) {
            fprintf(stderr, "cd: other disc ignored: %s\n", why);
            paths[1][0] = 0;
        }
    }
}

const char* cdSwapPath(int which) {
    return paths[which != 0][0] != '\0' ? paths[which != 0] : NULL;
}

int cdSwapSetOther(const char* path) {
    char found[MAX_PATH_LEN];
    if (path[0] == '\0') {
        /* The default: the other disc's image beside the one in the drive. */
        if (!cdImageSibling(paths[0], found, sizeof found) || !readable(found)) {
            found[0] = '\0';
        }
    } else if (!readable(path)) {
        return 0;
    } else {
        snprintf(found, sizeof found, "%s", path);
    }
    if (found[0]) {
        CdImageInfo info[2]; char why[1024];
        if (!cdVerifyPair(paths[0], found, NULL, info, why, sizeof why)) {
            fprintf(stderr, "cd: other disc refused: %s\n", why);
            if (path[0]) return 0;
            found[0] = 0; /* automatic discovery is optional */
        }
    }
    snprintf(paths[1], sizeof paths[1], "%s", found);
    settingsSet("disc2", path);
    settingsSave();
    return 1;
}

int cdSwapAutomatic(void) {
    return !manualOnly;
}

void cdSwapSetAutomatic(int on) {
    manualOnly = !on;
    settingsSet("disc_swap", on ? "auto" : "manual");
    settingsSave();
}

int cdSwapDisc(void) {
    char next[MAX_PATH_LEN];

    if (paths[1][0] == '\0') {
        fprintf(stderr, "cd: no other disc to swap to (set XG_DISC2)\n");
        return 0;
    }
    snprintf(next, sizeof next, "%s", paths[1]);
    if (!cdChangeDisc(next)) {
        fprintf(stderr, "cd: cannot use the disc image %s\n", next);
        return 0;
    }
    snprintf(paths[1], sizeof paths[1], "%s", paths[0]);
    snprintf(paths[0], sizeof paths[0], "%s", next);
    afterStop = 0;
    return 1;
}

int cdSwapForReset(void) {
    char next[MAX_PATH_LEN];

    if (paths[1][0] == '\0') {
        return 0;
    }
    snprintf(next, sizeof next, "%s", paths[1]);
    if (!cdInsertDisc(next)) {
        return 0;
    }
    snprintf(paths[1], sizeof paths[1], "%s", paths[0]);
    snprintf(paths[0], sizeof paths[0], "%s", next);
    afterStop = 0;
    polls = 0;
    return 1;
}

int cdSwapWanted(void) {
    return afterStop && polls > 0;
}

int cdSwapRequest(void) {
    if (!cdSwapWanted()) {
        return -1;
    }
    return cdSwapDisc();
}

static void swapEvent(void* arg) {
    (void)arg;
    pending = 0;
    if (paths[1][0] == '\0') {
        if (!warned) {
            warned = 1;
            fprintf(stderr, "cd: the game waits for the other disc; set XG_DISC2 (or press F2 after setting it)\n");
        }
        return;
    }
    fprintf(stderr, "cd: the game asks for the other disc\n");
    cdSwapDisc();
}

void cdSwapNoteCommand(uint8_t com) {
    if (com == CDC_STOP) {
        afterStop = 1;
        polls = 0;
        return;
    }
    if (com != CDC_NOP) {
        afterStop = 0;
        return;
    }
    if (afterStop && ++polls == AUTO_POLLS && !manualOnly && !pending) {
        /* After this command is done: the shell opens at the next safe point. */
        pending = 1;
        hwScheduleAt(hwNow(), swapEvent, NULL);
    }
}

/* ---- save states (system/savestate.h) ---- */

/* The automatic swap's watch of the game (a swapEvent may be queued). The
 * images' paths are this process's. */
void swapStateIo(XgState* s) {
    XG_STATE_VAR(s, afterStop);
    XG_STATE_VAR(s, polls);
    XG_STATE_VAR(s, pending);
}
