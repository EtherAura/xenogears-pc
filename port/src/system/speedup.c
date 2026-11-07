#include "speedup.h"
#include "settings.h"

/* GPU-less card/drive tools do not link the presentation layer. */
extern void gpuHostReset(void) __attribute__((weak));

/* The setting's value, as of settingsRevision() == cachedRevision. */
static int cached, cachedValid;
static unsigned cachedRevision;

int speedupEnabled(void) {
    if (!cachedValid || cachedRevision != settingsRevision()) {
        cached = settingsBool("speedup", 0);
        cachedRevision = settingsRevision(); /* after the read: the first one loads the file */
        cachedValid = 1;
    }
    return cached;
}

void speedupRefresh(void) {
    cachedValid = 0;
}

void speedupSetEnabled(int enabled) {
    settingsDescribe("speedup", "speedup: 1 is the single enhancement switch: instant card access, 8x ordinary disc loads, battle/field pacing catch-up and Smooth motion (fields, world map) on by default (one frame of added latency); gameplay and streaming clocks never change. 0 (default): original waits, frame cadence and latency");
    settingsSetInt("speedup", enabled != 0);
    if (gpuHostReset) gpuHostReset();
    settingsSave();
}
