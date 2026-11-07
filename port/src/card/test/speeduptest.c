/* Real BIOS card operations on private images and a synthetic event clock.
 * Speedup must preserve pending events, results and bytes, not just return
 * sooner. Original card timing remains the unset/default path. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "card/card.h"
#include "system/system.h"
#include "system/settings.h"
#include "system/speedup.h"
#include "test/env.h"

void InitCARD(s32 padEnable);
void StartCARD(void);
void cardVBlank(void);
s32 _card_info(s32 chan);
s32 func_80040534(char* path, s32 mode);
s32 func_80040544(s32 fd, void* buf, s32 n);
s32 func_80040554(s32 fd, void* buf, s32 n);
s32 func_80040564(s32 fd);

static struct { double at; HwEvent fn; void* arg; } queue[32];
static int queued, yields, events, lastSpec, checks;
static uint32_t vblank;
static double now, nextVblank = 1.0 / 60.0;

void hwSafePoint(void) {}
int slowdownBiosMissed(void) { return 0; }
double hwNow(void) { return now; }
double hwVSyncPeriod(void) { return 1.0 / 60.0; }
uint32_t hwVSyncCount(void) { return vblank; }
void hwScheduleAt(double at, HwEvent fn, void* arg) {
    if (queued == 32) exit(3);
    queue[queued].at = at; queue[queued].fn = fn; queue[queued++].arg = arg;
}
void DeliverEvent(uint32_t ev, int32_t spec) { events++; lastSpec = spec; }
void hwYield(void) {
    if (++yields > 10000) exit(3);
    double at = nextVblank;
    for (int i = 0; i < queued; i++) if (queue[i].at < at) at = queue[i].at;
    now = at;
    if (now >= nextVblank - 1e-12) {
        vblank++; nextVblank += hwVSyncPeriod(); cardVBlank();
    }
    for (;;) {
        int first = -1;
        for (int i = 0; i < queued; i++)
            if (queue[i].at <= now + 1e-12 && (first < 0 || queue[i].at < queue[first].at)) first = i;
        if (first < 0) break;
        HwEvent fn = queue[first].fn; void* arg = queue[first].arg;
        memmove(queue + first, queue + first + 1, (size_t)(--queued - first) * sizeof(queue[0]));
        fn(arg);
    }
}

#include "test/check.h"

static int finishInfo(int previous) {
    for (int i = 0; i < 20 && events == previous; i++) hwYield();
    CHECK(events == previous + 1 && lastSpec == EV_SPEC_IOE);
    CHECK(!cardBusy(0));
    return 0;
}
static int pending(void) {
    /* Enabling while queued must not strand an existing native request. */
    int previous = events; double began = now;
    CHECK(_card_info(0) == 1 && cardBusy(0) && events == previous);
    speedupSetEnabled(1); CHECK(speedupEnabled());
    CHECK(!finishInfo(previous) && now > began);
    began = now; previous = events;
    CHECK(_card_info(0) == 1 && events == previous + 1 && now == began && !cardBusy(0));

    /* Do the same after the slot has been served and its timed completion
     * event is in flight. A new request still waits behind that one. */
    speedupSetEnabled(0); previous = events;
    CHECK(_card_info(0) == 1);
    while (!queued) hwYield();
    CHECK(cardBusy(0) && events == previous);
    speedupSetEnabled(1); began = now;
    CHECK(!finishInfo(previous) && now > began);
    previous = events; began = now;
    CHECK(_card_info(0) == 1 && events == previous + 1 && now == began);

    /* Turning it off affects subsequent requests immediately. */
    speedupSetEnabled(0); previous = events; began = now;
    CHECK(_card_info(0) == 1 && events == previous);
    CHECK(!finishInfo(previous) && now > began);

    /* A missing card still reports TIMEOUT, without a fake successful read. */
    cards[0].present = 0; speedupSetEnabled(1); previous = events;
    CHECK(_card_info(0) == 1 && events == previous + 1 && lastSpec == EV_SPEC_TIMEOUT);
    CHECK(!cardBusy(0)); cards[0].present = 1;
    return 0;
}
static int io(const char* directory) {
    static uint8_t native[CARD_SIZE];
    uint8_t written[512], read[512];
    for (int i = 0; i < 512; i++) written[i] = (uint8_t)(i * 17 + i / 7);
    for (int fast = 0; fast < 2; fast++) {
        Card* c = &cards[0]; cardFormatImage(c->data); c->flagNew = c->dirty = 0;
        snprintf(c->path, sizeof(c->path), "%s/%s.mcd", directory, fast ? "fast" : "native");
        int changed[CARD_BLOCKS];
        CHECK(cardCreateFile(c->data, "TEST", 1, changed) == 1);
        CHECK(cardSaveImage(c->path, c->data));
        speedupSetEnabled(fast); double began = now;
        int fd = func_80040534("bu00:TEST", 0); CHECK(fd >= 2);
        CHECK(func_80040554(fd, written, sizeof(written)) == sizeof(written));
        CHECK(func_80040564(fd) == fd);
        fd = func_80040534("bu00:TEST", 0); CHECK(fd >= 2);
        CHECK(func_80040544(fd, read, sizeof(read)) == sizeof(read));
        CHECK(func_80040564(fd) == fd && !memcmp(read, written, sizeof(read)));
        CHECK(!cardBusy(0) && !c->dirty);
        static uint8_t disk[CARD_SIZE]; CHECK(cardLoadFile(c->path, disk, sizeof(disk)));
        CHECK(!memcmp(disk, c->data, CARD_SIZE));
        if (!fast) { CHECK(now - began > 8 * hwVSyncPeriod()); memcpy(native, c->data, CARD_SIZE); }
        else { CHECK(now == began); CHECK(!memcmp(native, c->data, CARD_SIZE)); }
    }
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/settings.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path); testSetEnv("XG_CARD_FAST", "0");
    testSetEnv("XG_CARD1", "none"); testSetEnv("XG_CARD2", "none");
    if (!strcmp(argv[2], "write")) {
        CHECK(!speedupEnabled());
        InitCARD(0); StartCARD();
        for (int i = 0; i < 2; i++) {
            cards[i].present = 1; cards[i].flagNew = 0; cardFormatImage(cards[i].data);
        }
        CHECK(!pending()); CHECK(!io(argv[1]));
        speedupSetEnabled(1); CHECK(speedupEnabled());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(speedupEnabled()); speedupSetEnabled(0); CHECK(!speedupEnabled());
    } else if (!strcmp(argv[2], "off")) CHECK(!speedupEnabled());
    else return 2;
    CHECK(settingsOverride("speedup") == NULL);
    CHECK(settingsSave());
    printf("Speedup card %s: %d checks passed\n", argv[2], checks);
    return 0;
}
