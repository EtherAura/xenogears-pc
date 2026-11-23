/*
 * Field speed (xg-mrj.6; fieldspeed.h): the field and the world map at 1x, 2x
 * or 4x.
 *
 * This file is the setting and the answer to one question, asked by
 * battlespeed.c at each vblank outside a battle: which factor does the
 * screen the game shows now ask for? The clock, the pictures withheld and the
 * rules that hold 1x whatever is asked (online, the slowdown replay, CD
 * audio streaming) are battlespeed.c's and hw.c's, the same for both speeds.
 *
 * The screen is the wide picture's kind (include/pc_wide.h): bootGame names
 * each mode as it starts (src/main/kernel.c) and the menus opened from the
 * field and the world map name themselves and put the old kind back, so
 * PC_WIDE_FIELD and PC_WIDE_WORLDMAP are those two modes without their
 * menus. A movie the field plays itself stays in the field's kind: the
 * field's player says when one starts and ends (pcFieldMovieBlock,
 * port/src/game/field/wide.c, before the wide picture's own exceptions),
 * and it is at 1x from that call to the other, sound or no sound.
 *
 * Threads: all of it runs on the game thread. fieldSpeedWanted is called
 * from the vblank handler (hw.c vblankInterrupt, delivered at the game's
 * safe points), fieldSpeedMovie by the field's code, fieldSpeedStateIo with
 * the game parked in its VSync wait (savestate.h), as every xxxStateIo.
 * Nothing else reads or writes the setting's copy or the movie flag. The
 * F1 menu and the control (fieldSpeedStep: pad.c's key events and its
 * vblank, through battlespeed.c) call fieldSpeedSetChosen on the game thread
 * too: the menu runs inside the vblank (uiVBlank).
 */

#include <stdio.h>

#include "fieldspeed.h"
#include "netplay.h"
#include "pc_wide.h"
#include "settings.h"

void presentShowNote(const char* text) __attribute__((weak)); /* gpu/present.c */

/* ---- the setting (host) ---- */

static int fieldChosen, fieldChosenValid;
static unsigned fieldChosenRevision;

static int clampChosen(int v) {
    return v <= 0 ? 0 : v == 1 ? 1 : v <= 3 ? 2 : 4;
}

int fieldSpeedChosen(void) {
    if (!fieldChosenValid || fieldChosenRevision != settingsRevision()) {
        fieldChosen = clampChosen(settingsInt("field_speed", 0));
        fieldChosenRevision = settingsRevision(); /* after the read: the first one loads the file */
        fieldChosenValid = 1;
    }
    return fieldChosen;
}

void fieldSpeedSetChosen(int speed) {
    settingsDescribe("field_speed", "field_speed: 0 off (default); 1, 2 or 4: the field and the world map at that speed "
                                    "(music at its tempo), stepped there with field_speed_key or the gamepad's Select+R2; "
                                    "menus, movies and battles keep their own");
    settingsSetInt("field_speed", clampChosen(speed));
    settingsSave();
    fieldChosenValid = 0;
}

int fieldSpeedOnScreen(void) {
    int kind = pcWideGetMode();
    return kind == PC_WIDE_FIELD || kind == PC_WIDE_WORLDMAP;
}

int fieldSpeedStep(void) {
    int chosen = fieldSpeedChosen();
    if (chosen == 0 || !fieldSpeedOnScreen()) {
        return 0;
    }
    if (netplayActive()) {
        if (presentShowNote) presentShowNote("Field speed: 1x online");
        return 1;
    }
    int next = chosen == 1 ? 2 : chosen == 2 ? 4 : 1;
    fieldSpeedSetChosen(next);
    if (presentShowNote) {
        char text[32];
        snprintf(text, sizeof text, "Field speed %dx", next);
        presentShowNote(text);
    }
    return 1;
}

/* ---- the game's side (state) ---- */

/* A field movie plays (between pcFieldMovieBlock(1) and (0)). Kept whatever
 * the setting says, so turning the speed on inside a movie finds it. */
static int fieldMovie;

void fieldSpeedMovie(int on) {
    fieldMovie = on != 0;
}

int fieldSpeedWanted(void) {
    int chosen = fieldSpeedChosen();
    if (chosen <= 1) {
        return 1;
    }
    int kind = pcWideGetMode();
    if (kind == PC_WIDE_WORLDMAP) {
        return chosen;
    }
    if (kind == PC_WIDE_FIELD && !fieldMovie) {
        return chosen;
    }
    return 1; /* a menu, the movie mode, a battle (its loading too), the kernel's screens */
}

/* ---- save states (savestate.h) ---- */

void fieldSpeedStateIo(XgState* s) {
    XG_STATE_VAR(s, fieldMovie);
}
