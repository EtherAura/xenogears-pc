/* The dialogue settings: text speed, hold for instant text, auto-advance,
 * which text keeps its own pace, and the auto-advance timer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "system/settings.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

static int timed(const char* bytes, int n, int lead) {
    unsigned char text[64];
    memset(text, 0, sizeof(text));
    memcpy(text, bytes, (size_t)n);
    return pcGameplayTimedText(text, lead);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/dialogue.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        pcGameplaySetTextSpeed(0);
        pcGameplaySetTextHold(1);
        pcGameplaySetTextAutoAdvance(3);
        CHECK(pcGameplayTextSpeed() == 0 && pcGameplayTextHold() && pcGameplayTextAutoAdvance() == 3);
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayTextSpeed() == 0 && pcGameplayTextHold() && pcGameplayTextAutoAdvance() == 3);
        CHECK(settingsGet("text_speed") && !strcmp(settingsGet("text_speed"), "instant"));
        pcGameplaySetTextSpeed(4);
        CHECK(pcGameplayTextSpeed() == 4);
    } else {
        /* Defaults: the original pace, no hold, no auto-advance. */
        CHECK(pcGameplayTextSpeed() == 1 && !pcGameplayTextHold() && pcGameplayTextAutoAdvance() == 0);
        testSetEnv("XG_TEXT_SPEED", "3"); CHECK(pcGameplayTextSpeed() == 3);
        testSetEnv("XG_TEXT_SPEED", "99"); CHECK(pcGameplayTextSpeed() == 8);
        testSetEnv("XG_TEXT_SPEED", "-2"); CHECK(pcGameplayTextSpeed() == 1);
        testSetEnv("XG_TEXT_SPEED", "fast"); CHECK(pcGameplayTextSpeed() == 1);
        testSetEnv("XG_TEXT_SPEED", "instant"); CHECK(pcGameplayTextSpeed() == 0);
        testSetEnv("XG_TEXT_AUTO_ADVANCE", "30"); CHECK(pcGameplayTextAutoAdvance() == 10);
        testSetEnv("XG_TEXT_AUTO_ADVANCE", "-1"); CHECK(pcGameplayTextAutoAdvance() == 0);

        /* Text that paces itself: 0x0F with 0x01, 0x02, 0x0D or 0x0E. A
         * pause (0x0F 0x00) does not; escape arguments and the second byte
         * of a two-byte character are never read as escapes. */
        CHECK(!timed("\x20\x21\x02\x22", 4, 0x80));
        CHECK(!timed("\x20\x0F\x00\x1E\x21", 5, 0x80));
        CHECK(timed("\x20\x0F\x02\x3C", 4, 0x80));
        CHECK(timed("\x20\x0F\x0D\x3C", 4, 0x80));
        CHECK(timed("\x20\x0F\x0E\x04\x21", 5, 0x80));
        CHECK(timed("\x0F\x01\x02\x21", 4, 0x80));
        CHECK(!timed("\x0F\x05\x0F\x02\x21", 5, 0x80));      /* a name: its argument is 0x0F */
        CHECK(!timed("\x0F\x03\x01\x0F\x02", 5, 0x80));      /* 0x0F 0x03 has two arguments */
        CHECK(timed("\x0F\x04\x0F\x02\x10", 5, 0x80));       /* 0x0F 0x04 has none */
        CHECK(!timed("\x85\x0F\x02\x21", 4, 0x80));          /* a two-byte character, then a wait */
        CHECK(timed("\x85\x0F\x02\x21", 4, 0x90));           /* below the lead: single bytes */
        CHECK(pcGameplayTimedText(NULL, 0x80));
        unsigned char endless[9000];
        memset(endless, 0x21, sizeof(endless));
        CHECK(pcGameplayTimedText(endless, 0x80));           /* unterminated: original pace */

        /* Extra printer steps per frame. */
        static const unsigned char plain[] = {0x20, 0x21, 0x02, 0x00};
        static const unsigned char slow[] = {0x0F, 0x0E, 0x04, 0x21, 0x00};
        int windowA, windowB, windowC;
        testSetEnv("XG_TEXT_SPEED", "1");
        pcGameplayDialogueText(&windowA, plain, 0x80);
        pcGameplayDialogueText(&windowB, slow, 0x80);
        CHECK(pcGameplayDialogueSteps(&windowA, 0) == 0 && pcGameplayDialogueSteps(&windowA, 1) == 0);
        testSetEnv("XG_TEXT_HOLD_FAST", "1");
        CHECK(pcGameplayDialogueSteps(&windowA, 0) == 0 && pcGameplayDialogueSteps(&windowA, 1) == 1000);
        CHECK(pcGameplayDialogueSteps(&windowB, 1) == 0);
        CHECK(pcGameplayDialogueSteps(&windowC, 1) == 0); /* never seen starting */
        testSetEnv("XG_TEXT_SPEED", "3");
        CHECK(pcGameplayDialogueSteps(&windowA, 0) == 2 && pcGameplayDialogueSteps(&windowB, 0) == 0);
        testSetEnv("XG_TEXT_SPEED", "instant");
        CHECK(pcGameplayDialogueSteps(&windowA, 0) == 1000);
        pcGameplayDialogueText(&windowA, slow, 0x80); /* the window's next string */
        CHECK(pcGameplayDialogueSteps(&windowA, 1) == 0);
        int many[20];
        for (int i = 0; i < 20; i++) pcGameplayDialogueText(&many[i], plain, 0x80); /* table recycles */
        CHECK(pcGameplayDialogueSteps(&many[19], 0) == 1000);

        /* Auto-advance: off, then one second (60 play-time vsyncs) of a
         * page waiting, per context; anything else restarts the wait. */
        testSetEnv("XG_TEXT_AUTO_ADVANCE", "0");
        CHECK(!pcGameplayDialogueAuto(0, 1, 100) && !pcGameplayDialogueAuto(0, 1, 1000));
        testSetEnv("XG_TEXT_AUTO_ADVANCE", "1");
        CHECK(!pcGameplayDialogueAuto(0, 1, 100));
        CHECK(!pcGameplayDialogueAuto(0, 1, 159));
        CHECK(!pcGameplayDialogueAuto(1, 1, 150)); /* battle's own timer starts */
        CHECK(pcGameplayDialogueAuto(0, 1, 160));
        CHECK(!pcGameplayDialogueAuto(0, 1, 161)); /* pressed: a new wait */
        CHECK(!pcGameplayDialogueAuto(0, 0, 200));
        CHECK(!pcGameplayDialogueAuto(0, 1, 205) && !pcGameplayDialogueAuto(0, 1, 264));
        CHECK(pcGameplayDialogueAuto(0, 1, 265));
        CHECK(pcGameplayDialogueAuto(1, 1, 210));
        CHECK(!pcGameplayDialogueAuto(1, 1, 0xFFFFFFF0u));
        CHECK(!pcGameplayDialogueAuto(1, 1, 0xFFFFFFF0u + 30));
        CHECK(pcGameplayDialogueAuto(1, 1, 0xFFFFFFF0u + 60)); /* across the counter's wrap */
    }
    printf("dialogue %s: %d checks passed\n", argv[2], checks);
    return 0;
}
