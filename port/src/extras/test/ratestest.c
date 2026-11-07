/* Keep viewer playback and default GLB durations on the runtime's clock.
 * Counts below are native VM steps over 600 vblanks, not rounded FPS. */
#include "extras/models.h"
#include "extras/tables.h"
#include "system/system.h"
#include <math.h>
#include <stdio.h>

static int checks;
#include "test/check.h"

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    XgRates r;
    hwSetVideoStandard(0);
    hwSetDisplayTiming(0, 8);
    double seconds = 600 * hwVSyncPeriod();
    CHECK(near(xgVsyncRate() * seconds, 600));
    CHECK(near(XG_MECHA_TICK_HZ * seconds, 300));
    for (int slow = 0; slow < 3; slow++) {
        const int frames[] = {300, 200, 150};
        CHECK(xgAnimRates(1, slow, &r));
        CHECK(r.vsyncsPerFrame == 2 + slow && !r.catchUp);
        CHECK(near(r.frameRate * seconds, frames[slow]));
        CHECK(near(r.spriteTickRate * seconds, 2 * frames[slow]));
        CHECK(near(r.gearStepRate * seconds, frames[slow]));
    }
    CHECK(xgAnimRates(2, 2, &r));
    CHECK(r.vsyncsPerFrame == 1 && r.catchUp);
    CHECK(near(r.frameRate * seconds, 600));
    CHECK(near(r.spriteTickRate * seconds, 600));
    CHECK(near(r.gearStepRate * seconds, 300));
    CHECK(xgAnimRates(3, 2, &r));
    CHECK(r.vsyncsPerFrame == 2 && !r.catchUp);
    CHECK(near(r.frameRate * seconds, 300));
    CHECK(near(r.spriteTickRate * seconds, 600));
    CHECK(near(r.gearStepRate * seconds, 300));
    CHECK(xgAnimRates(1, -1, &r) && near(r.frameRate * seconds, 300));
    CHECK(xgAnimRates(1, 3, &r) && near(r.frameRate * seconds, 300));
    CHECK(!xgAnimRates(0, 0, &r));
    CHECK(!xgAnimRates(4, 0, &r));
    CHECK(near(xgSpriteTickRate(1) * seconds, 600));
    CHECK(near(xgGearStepRate(1), XG_MECHA_TICK_HZ));
    CHECK(near(xgSpriteTickRate(2) * seconds, 600));
    CHECK(near(xgGearStepRate(2), XG_MECHA_TICK_HZ));
    CHECK(xgSpriteTickRate(0) == 0 && xgGearStepRate(0) == 0);
    /* These describe NTSC disc animation even if the caller switches the
     * runtime to another video mode; do not make exports host-dependent. */
    hwSetVideoStandard(1);
    hwSetDisplayTiming(1, 4);
    CHECK(near(xgVsyncRate() * seconds, 600));
    CHECK(near(XG_MECHA_TICK_HZ * seconds, 300));
    printf("Animation rates: %d checks passed\n", checks);
    return 0;
}
