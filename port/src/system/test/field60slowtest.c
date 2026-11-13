/* The field60slow setting (the Enhanced engine's 20 and 15 fps rooms) as the
 * options and presets read it: the stored value, in a build of the Original
 * engine too, where the engine itself never runs those rooms at 60 Hz
 * (xg-dzg: the Enhanced 60 Hz preset could not see a stored 0 from the
 * Original and left it in place). */
#include <stdio.h>
#include <string.h>
#include "system/gameplay.h"
#include "system/settings.h"
#include "test/env.h"

#if XG_ENGINE_ENHANCED
#error "field60slowtest checks the Original engine's build"
#endif

static int checks;
#include "test/check.h"

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/field60slow.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        settingsSetInt("field60slow", 0);
        CHECK(pcGameplayField60SlowSetting() == 0);
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayField60SlowSetting() == 0 && pcGameplayField60Slow() == 0);
        settingsSetInt("field60slow", 1);
        CHECK(pcGameplayField60SlowSetting() == 1);
    } else {
        /* Unset: on, as the Enhanced engine defaults it; the Original's
         * engine still never asks for it. */
        CHECK(pcGameplayField60SlowSetting() == 1);
        CHECK(pcGameplayField60Slow() == 0);
        /* A stored 0 reads back 0 although the engine's value stays 0 either
         * way: a getter that masked it in the Original would answer 1. */
        settingsSetInt("field60slow", 0);
        CHECK(pcGameplayField60SlowSetting() == 0);
        settingsSetInt("field60slow", 1);
        CHECK(pcGameplayField60SlowSetting() == 1);
        CHECK(pcGameplayField60Slow() == 0);
    }
    printf("field60slow %s: %d checks passed\n", argv[2], checks);
    return 0;
}
