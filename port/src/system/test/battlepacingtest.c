/* LIBETC battle pacing on a controlled vblank clock. The ordinary VSync
 * contract, authored waits, replay and netplay must remain available. */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "system/settings.h"
#include "system/speedup.h"
#include "test/env.h"
int VSync(int mode);
void pcBattleVSync(void);
void pcFieldVSync(void);
static uint32_t count;
static int waits, catchups, resumes, replay, online, presents, checks;
static const void* lastSite;
#include "test/check.h"
void hwSafePoint(void) {}
uint32_t hwVSyncCount(void) { return count; }
double hwNow(void) { return count / 60.0; }
double hwHBlankRate(void) { return 60.0 * 263; }
uint32_t hwWaitVSync(void) { waits++; return ++count; }
uint32_t hwWaitVSyncSince(uint32_t previous) {
    catchups++;
    return (int32_t)(count-previous)>0 ? count : hwWaitVSync();
}
void hwVSyncResume(void) { resumes++; }
void slowdownVSync(int mode) { (void)mode; }
void slowdownVSyncReturn(const void* site) { lastSite = site; }
void hwPresentPoll(void) { presents++; }
int hwVBlankIdle(void) { return 0; }
int slowdownLogVBlank(int64_t* at) { *at=count; return replay; }
int netplayActive(void) { return online; }
void hwClockHold(int on) { (void)on; }
const char* portOption(const char* name) { (void)name; return NULL; }
/* libetc.c's other callees (ResetCallback, VSyncCallback, CheckCallback,
 * SetVideoMode): never reached here. Defined, not left to --gc-sections,
 * because MinGW's ld resolves references from the sections it collects too. */
void ExitCriticalSection(void) { abort(); }
int hwInInterrupt(void) { abort(); }
void hwSetVideoStandard(int pal) { (void)pal; abort(); }
void slowdownBoot(void) { abort(); }
/* The replay keys its spreads on the game's call site: the main flips must
 * report their caller, as VSync does (the asm keeps the calls from being
 * tail calls). */
static __attribute__((noinline)) void battleSite(void) { pcBattleVSync(); __asm__ volatile("" ::: "memory"); }
static __attribute__((noinline)) void fieldSite(void) { pcFieldVSync(); __asm__ volatile("" ::: "memory"); }
static int nearSite(void (*fn)(void)) {
    uintptr_t at = (uintptr_t)lastSite, start = (uintptr_t)fn;
    return at > start && at - start < 128;
}
int main(void) {
    testSetEnv("XG_SETTINGS","none"); testSetEnv("XG_SPEEDUP","0");
    count=10; VSync(0); CHECK(count==11 && waits==1);
    count+=2; pcBattleVSync(); CHECK(count==14 && waits==2 && !catchups);
    /* speedupEnabled keeps its value: XG_SPEEDUP is read at the first use
     * and again after speedupRefresh (a test changing it while running). */
    testSetEnv("XG_SPEEDUP","1"); CHECK(!speedupEnabled());
    speedupRefresh(); CHECK(speedupEnabled());
    testSetEnv("XG_SPEEDUP","0"); speedupRefresh(); CHECK(!speedupEnabled());
    speedupSetEnabled(1); count+=2; pcBattleVSync();
    CHECK(count==16 && waits==2 && catchups==1);
    pcBattleVSync(); CHECK(count==17 && waits==3 && catchups==2); /* cap at one/frame */
    count+=2; VSync(0); CHECK(count==20 && waits==4); /* other modes unchanged */
    VSync(2); CHECK(count==22 && waits==6); /* authored two-vblank wait */
    online=1; count+=2; pcBattleVSync(); CHECK(count==25 && waits==7 && catchups==2);
    online=0; replay=1; count+=2; pcBattleVSync(); CHECK(count==28 && waits==8 && catchups==2);
    replay=0; speedupSetEnabled(0); count+=2; pcBattleVSync();
    CHECK(count==31 && waits==9 && catchups==2);
    CHECK(resumes==9);
    /* Establish the last native wait across the uint32 vblank wrap. */
    count=UINT32_MAX-2; VSync(0); speedupSetEnabled(1); count+=3;
    pcBattleVSync(); CHECK(count==1 && waits==10 && catchups==3);
    count+=2; pcFieldVSync(); CHECK(count==3 && waits==10 && catchups==4);
    pcFieldVSync(); CHECK(count==4 && waits==11 && catchups==5);
    VSync(2); CHECK(count==6 && waits==13 && catchups==5); /* authored field waits */
    speedupSetEnabled(0); count+=2; pcFieldVSync(); CHECK(count==9 && waits==14 && catchups==5);
    speedupSetEnabled(1); online=1; count+=2; pcFieldVSync(); CHECK(count==12 && waits==15 && catchups==5);
    online=0; replay=1; count+=2; pcFieldVSync(); CHECK(count==15 && waits==16 && catchups==5);
    replay=0; count+=2; battleSite(); CHECK(nearSite(battleSite) && count==17 && catchups==6);
    fieldSite(); CHECK(nearSite(fieldSite) && count==18 && catchups==7);
    /* The host may present only in a busy wait for the vblank (the field's
     * frame cap): VSync(-1) again with the count unchanged. Not in VSync(1)
     * or a single VSync(-1) (a frame's timing), nor in the call that saw
     * the awaited vblank (it returns to the game at once). */
    CHECK(presents==0);
    VSync(1); VSync(-1); CHECK(presents==0);
    VSync(-1); VSync(-1); CHECK(presents==2);
    count++; CHECK(VSync(-1)==(int)count && presents==2);
    VSync(-1); CHECK(presents==3);
    VSync(1); VSync(-1); CHECK(presents==3);
    VSync(0); VSync(-1); CHECK(presents==3);
    pcFieldVSync(); VSync(-1); VSync(-1); CHECK(presents==4);
    VSync(1); VSync(1); CHECK(presents==4);
    /* Every settings change reaches the kept value: a netplay pin too. */
    speedupSetEnabled(0); CHECK(!speedupEnabled());
    settingsPin("speedup","1"); CHECK(speedupEnabled());
    speedupSetEnabled(0); CHECK(speedupEnabled()); /* held for the session */
    printf("Battle/field pacing: %d checks passed\n",checks);
    return 0;
}
