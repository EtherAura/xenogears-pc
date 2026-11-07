/* Actual hw.c waits/interrupts, Vulkan snapshots/presents, and the real SPU
 * core. Only the game's input and timer writers are controlled fixtures. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "psyq.h"
#include "pc_interpolation.h"
#include "gpu/gpu_internal.h"
#include "gpu/interpolation.h"
#include "system/speedup.h" /* speedupRefresh after XG_SPEEDUP changes */
#include "gpu/shaderchain.h"
#include "system/hw.h"
#include "system/system.h"
#include "system/savestate.h"
#include "system/settings.h"
#include "spu/spu.h"
#include "test/env.h"
int gpuInit(SDL_Window* window);
int hwInit(void);
void gpuShutdown(void);
static int checks;
#include "test/check.h"

static SpuRegs regs;
static unsigned pads, callbacks, ticks;
static uint64_t events = UINT64_C(1469598103934665603);
static uint64_t samples, limit;
static int16_t* pcm;
static double endTime;
static unsigned nativeLimit=90;
static unsigned timerLimit;
static uint64_t nativeState;
static int forceFifo;
static unsigned fallbacks;
static unsigned quietFrames, maxQuietFrames;
static unsigned capturePoints;
void stateVBlankPoint(void) { capturePoints++; }
static uint64_t lastHostFrame;
int displayLockFifo(void) { return forceFifo; }
static void draw(int x);
static void event(unsigned kind, unsigned value) {
    events = (events ^ kind) * UINT64_C(1099511628211);
    events = (events ^ value) * UINT64_C(1099511628211);
}
void quitInit(void) {}
void stallInit(void) {}
void invariantVBlank(void) {}
void quitPoll(int pump) { (void)pump; }
void slowdownSafePoint(void) {}
void padInit(void) {}
void padVBlank(void) { SDL_PumpEvents(); if (hwVSyncCount()<=nativeLimit) { pads++; event(1, pads % 7); } }
void etcVBlank(void) {
    GvkPresentStats shown; gvkPresentStats(&shown); if (!shown.hostActive) fallbacks++;
    if (shown.hostActive && shown.hostFrames == lastHostFrame) {
        if (++quietFrames > maxQuietFrames) maxQuietFrames = quietFrames;
    } else quietFrames = 0;
    lastHostFrame = shown.hostFrames;
    if (hwVSyncCount()>nativeLimit) return;
    callbacks++; event(2, callbacks);
    if (callbacks==nativeLimit) {
        nativeState=UINT64_C(1469598103934665603);
        for (size_t i=0;i<sizeof(regs);i++) nativeState=(nativeState^((uint8_t*)&regs)[i])*UINT64_C(1099511628211);
    }
    /* A controlled native writer removes startup CPU/driver scheduling
     * jitter from the pose sequence. hw.c must keep host polls outside it. */
    if (callbacks<nativeLimit && !(callbacks%2)) draw(40+(int)(callbacks%40));
}
void pcTextImageUploaded(const void* p, int bytes) { (void)p; (void)bytes; }
void cdGetAudioMix(uint8_t atv[4]) { memset(atv, 0, 4); }
void cdSwapBytes(void* a, void* b, size_t n) {
    uint8_t* x = a; uint8_t* y = b;
    for (size_t i = 0; i < n; i++) { uint8_t t = x[i]; x[i] = y[i]; y[i] = t; }
}
void spuSync(double now) {
    uint64_t until = (uint64_t)(fmin(now, endTime) * SPU_RATE + 1e-7);
    if (until > limit) until = limit;
    if (until > samples) { spuRun(pcm + samples * 2, (uint32_t)(until - samples)); samples = until; }
    spuLatch(&regs);
}
static void timer(void* arg) {
    (void)arg;
    ticks++; if (ticks<=timerLimit) event(3, ticks);
    regs.m0_voice[0].m4_pitch = (uint16_t)(0x800 + (ticks % 16) * 0x80);
    hwScheduleAt((ticks + 1) / 240.0, timer, NULL);
}
static POLY_F4 quad;
static int owner;
static void draw(int x) {
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT rect = {0,0,320,224}; ClearImage(&rect,0,0,0);
    SetPolyF4(&quad); setRGB0(&quad,248,0,0);
    quad.x0 = quad.x2 = x; quad.x1 = quad.x3 = x + 20;
    quad.y0 = quad.y1 = 40; quad.y2 = quad.y3 = 60;
    pcInterpolationObject(&owner,&quad); pcInterpolationModel(&quad); pcInterpolationPrim(&quad);
    DrawPrim(&quad); pcInterpolationObject(NULL,NULL);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static unsigned heldBlank;
static int presentNew(int menu) {
    GvkPresentStats old, next; gvkPresentStats(&old);
    for (int attempt=0;attempt<60;attempt++) {
        gvkPresentStats(&next);
        if(next.shown>old.shown)return next.shown==old.shown+1;
        /* An asynchronous picture is already queued. Poll its completion
         * without submitting duplicate menu/shader pictures while waiting. */
        if(!next.pending) {
            if (menu) gvkPresentMenu(gpuDisplay(),++heldBlank,NULL);
            else gvkPresent(gpuDisplay(),++heldBlank);
        }
        SDL_Delay(5);SDL_PumpEvents();
    }
    return 0;
}

int main(int argc, char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);
    if (argc==2 && !strcmp(argv[1],"--features")) {
#ifdef XG_HAVE_GLSLANG
        puts("shader=1");
#else
        puts("shader=0");
#endif
        return 0;
    }
    CHECK(argc >= 3);
    if (argc>3 && (!strcmp(argv[3],"write") || !strcmp(argv[3],"read"))) {
        char config[1200]; snprintf(config,sizeof(config),"%s/settings.ini",argv[2]);
        testSetEnv("XG_SETTINGS",config);
        if (!strcmp(argv[3],"write")) { presentationSetRate(999); CHECK(presentationRate()==0); presentationSetRate(144); CHECK(settingsSave()); }
        else CHECK(presentationRate()==144);
        puts("high-refresh settings: pass"); return 0;
    }
    int hz = atoi(argv[1]);
    int step = argc > 3 && (!strcmp(argv[3],"step") || !strcmp(argv[3],"pacing-step"));
    int pacing = argc > 3 && (!strcmp(argv[3],"pacing") || !strcmp(argv[3],"pacing-step"));
    int shader = argc > 3 && !strcmp(argv[3],"shader");
    int hold = argc > 3 && !strcmp(argv[3],"hold");
    int visible = hold || (argc > 3 && (!strcmp(argv[3],"window") || !strcmp(argv[3],"fullscreen")));
    int poll = argc>3 && !strcmp(argv[3],"poll");
    int noIdle = (argc > 3 && !strcmp(argv[3],"no-idle")) ||
                 (argc > 4 && !strcmp(argv[4],"no-idle"));
    forceFifo = argc > 4 && !strcmp(argv[4],"fifo");
    testSetEnv("XG_SETTINGS","none"); testSetEnv("XG_CLOCK",step ? "step" : "real");
    testSetEnv("XG_INTERPOLATION","1"); testSetEnv("XG_DUMP_HOST","1");
    testSetEnv("XG_DUMP_PRESENT",argv[2]);
    if (visible) { testSetEnv("XG_DUMP_HOST","0"); testSetEnv("XG_DUMP_PRESENT",""); }
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow("High refresh validation",320,240,
        visible ? SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN);
    CHECK(window);
    if(visible)CHECK(SDL_SyncWindow(window));
    if (visible && !strcmp(argv[3],"fullscreen")) { CHECK(SDL_SetWindowFullscreen(window,true)); CHECK(SDL_SyncWindow(window)); }
    SDL_PumpEvents();
    CHECK(hwInit()); hwPause(1); CHECK(gpuInit(window)); presentationSetRate(hz);
    if (shader) {
        char preset[1200]; snprintf(preset,sizeof(preset),"%s/port/src/gpu/test/slang/feedback.slangp",XG_ROOT);
        shaderChainEdit(0); CHECK(shaderChainSetPath(preset));
    }
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    DISPENV disp; SetDefDispEnv(&disp,0,0,320,224); disp.screen.h=224; PutDispEnv(&disp); SetDispMask(1);
    if (argc>3 && !strcmp(argv[3],"authentic")) {
        /* Stored smoothing/high-refresh preferences must not change the
         * original experience while Speedup is off, or bridge a toggle. */
        SDL_setenv_unsafe("XG_SPEEDUP","0",1);speedupRefresh();
        interpolationSetEnabled(1);presentationSetRate(120);gpuHostReset();
        CHECK(!interpolationEnabled());
        draw(40);gvkPresent(gpuDisplay(),100);draw(50);gvkPresent(gpuDisplay(),102);
        gvkPresent(gpuDisplay(),103);
        int before,after;double alpha;
        CHECK(!interpolationPicture(gpuDisplay(),103,&before,&after,&alpha));
        CHECK(gpuHostDeadline()==UINT64_MAX);
        GvkPresentStats original;gvkPresentStats(&original);CHECK(original.hostFrames==0 && !original.hostActive);
        interpolationSetEnabled(0);gvkPresent(gpuDisplay(),104);
        char a[1200],b[1200];snprintf(a,sizeof(a),"%s/present_000102.png",argv[2]);snprintf(b,sizeof(b),"%s/present_000104.png",argv[2]);
        SDL_Surface* x=SDL_LoadPNG(a);SDL_Surface* y=SDL_LoadPNG(b);if(!y)y=SDL_LoadPNG(a);CHECK(x && y);
        CHECK(x->w==y->w && x->h==y->h && x->pitch==y->pitch);
        CHECK(!memcmp(x->pixels,y->pixels,(size_t)x->pitch*x->h));SDL_DestroySurface(x);SDL_DestroySurface(y);
        SDL_setenv_unsafe("XG_SPEEDUP","1",1);speedupRefresh();gpuHostReset();interpolationSetEnabled(1);
        CHECK(interpolationEnabled());draw(40);gvkPresent(gpuDisplay(),108);draw(50);gvkPresent(gpuDisplay(),110);
        CHECK(interpolationPicture(gpuDisplay(),111,&before,&after,&alpha));CHECK(alpha==.5);
        SDL_setenv_unsafe("XG_SPEEDUP","0",1);speedupRefresh();gpuHostReset();
        CHECK(!interpolationEnabled() && gpuHostDeadline()==UINT64_MAX);
        CHECK(!interpolationPicture(gpuDisplay(),111,&before,&after,&alpha));
        gpuShutdown();SDL_DestroyWindow(window);SDL_Quit();
        printf("Authentic cadence: %d checks passed\n",checks);return 0;
    }
    if (hold) {
        interpolationSetEnabled(0); presentationSetRate(0); nativeLimit=0;
        gpuFill(0,0,320,224,0x0000f8);
        /* Let initial compositor placement/DPI changes settle, then drain
         * those startup pictures before counting individual invalidations. */
        for(int i=0;i<20;i++) { gvkPresent(gpuDisplay(),++heldBlank);SDL_Delay(5);SDL_PumpEvents(); }
        gpuHostReset();
        GvkPresentStats first, next;
        CHECK(presentNew(0)); gvkPresentStats(&first);
        CHECK(first.haveSwapchain && first.shown>0);
        for(unsigned i=2;i<=8;i++) gvkPresent(gpuDisplay(),++heldBlank);
        gvkPresentStats(&next);
        CHECK(next.shown==first.shown && next.nativeFrames==first.nativeFrames+7);
        uint16_t pixel=0x03e0; gpuLoad(16,16,1,1,&pixel);
        CHECK(presentNew(0)); gvkPresentStats(&first); CHECK(first.shown==next.shown+1);
        gvkPresent(gpuDisplay(),++heldBlank); gvkPresentStats(&next); CHECK(next.shown==first.shown);
        gpuFill(0,0,16,16,0xf80000);
        CHECK(presentNew(0)); gvkPresentStats(&first); CHECK(first.shown==next.shown+1);
        gpuMove(0,0,32,32,16,16);
        CHECK(presentNew(0)); gvkPresentStats(&next); CHECK(next.shown==first.shown+1);
        hiresSetUpscale(2); hiresSetColor(HIRES_COLOR_DITHER_1X);
        CHECK(gvkHiConfigure(2,HIRES_COLOR_DITHER_1X)==2);
        CHECK(presentNew(0)); gvkPresentStats(&first); CHECK(first.shown==next.shown+1);
        gvkPresent(gpuDisplay(),++heldBlank); gvkPresentStats(&next); CHECK(next.shown==first.shown);
        gpuHostReset(); CHECK(presentNew(0)); gvkPresentStats(&first); CHECK(first.shown==next.shown+1);
        CHECK(presentNew(1)); gvkPresentStats(&next); CHECK(next.shown==first.shown+1);
        CHECK(presentNew(1)); gvkPresentStats(&first); CHECK(first.shown==next.shown+1);
        CHECK(SDL_SetWindowSize(window,480,360)); CHECK(SDL_SyncWindow(window)); SDL_PumpEvents();
        CHECK(presentNew(0)); gvkPresentStats(&next);
        int actualW,actualH;SDL_GetWindowSizeInPixels(window,&actualW,&actualH);
        CHECK(next.shown==first.shown+1 && next.width==actualW && next.height==actualH);
        CHECK(next.width!=first.width || next.height!=first.height);
        /* Drain buffered images when Speedup changes. Stored smoothing and
         * rate preferences cannot reactivate it in original mode. */
        interpolationSetEnabled(1);presentationSetRate(120);
        SDL_setenv_unsafe("XG_SPEEDUP","0",1);speedupRefresh();gpuHostReset();
        CHECK(!interpolationEnabled());CHECK(presentNew(0));gvkPresentStats(&first);
        CHECK(!first.hostActive && first.hostFrames==next.hostFrames);
        SDL_setenv_unsafe("XG_SPEEDUP","1",1);speedupRefresh();gpuHostReset();
        CHECK(interpolationEnabled());CHECK(presentNew(0));gvkPresentStats(&next);
        CHECK(next.shown==first.shown+1);
#ifdef XG_HAVE_GLSLANG
        char preset[1200]; snprintf(preset,sizeof(preset),"%s/port/src/gpu/test/slang/feedback.slangp",XG_ROOT);
        shaderChainEdit(0); CHECK(shaderChainSetPath(preset));
        CHECK(presentNew(0)); CHECK(presentNew(0)); gvkPresentStats(&first);
        CHECK(first.shown==next.shown+2);
        /* A native display-mode change can resize shader resources with an
         * older final picture still queued on the presentation worker. */
        gvkPresentMenu(gpuDisplay(),++heldBlank,NULL);
        DISPENV smaller;SetDefDispEnv(&smaller,0,0,256,224);smaller.screen.h=224;PutDispEnv(&smaller);
        gvkPresent(gpuDisplay(),++heldBlank);
        gpuHostReset();CHECK(presentNew(0));
        gvkPresentMenu(gpuDisplay(),++heldBlank,NULL);
        shaderChainSetPath("");CHECK(shaderChainPasses()==0);
        gvkPresent(gpuDisplay(),++heldBlank);
        gpuHostReset();CHECK(presentNew(0));
#endif
        gpuShutdown(); SDL_DestroyWindow(window); SDL_Quit();
        printf("Held presentation: %d checks passed\n",checks); return 0;
    }
    /* Non-silent looping voice with timer-controlled pitch changes. */
    spuReset(&regs);
    if (pacing) {
        nativeLimit=0; hwPause(0);
        uint32_t previous=hwWaitVSync();
        if (step) hwBusy(hwVSyncPeriod()*1.25);
        else SDL_Delay(25);
        /* Settle elapsed interrupts on a fixed real-clock instant. Otherwise
         * a naturally due vblank between the count read and the handoff can
         * fail exact equality even when no extra wait was introduced. A
         * forced wait still cannot pass: this clock has no future vblank. */
        if (!step) hwPause(1);
        hwSafePoint(); uint32_t elapsed=hwVSyncCount();
        CHECK((int32_t)(elapsed-previous)>0);
        unsigned points=capturePoints;
        uint32_t resumed=hwWaitVSyncSince(previous);
        CHECK(step ? (int32_t)(resumed-elapsed)>0 : resumed==elapsed);
        CHECK(capturePoints==points+1);
        if (!step) hwPause(0);
        points=capturePoints;
        uint32_t next=hwWaitVSyncSince(resumed);
        CHECK((int32_t)(next-resumed)>0); /* no second handoff in one vblank */
        CHECK(capturePoints==points+1);
        gpuShutdown(); SDL_DestroyWindow(window); SDL_Quit();
        printf("Hardware battle pacing %s: %d checks passed\n",step?"step":"real",checks);
        return 0;
    }
    uint8_t block[16] = {8,7,0x78,0x12,0x34,0x56,0x78,0x12,0x34,0x56,0x78,0x12,0x34,0x56,0x78,0x12};
    spuRamWrite(0x2000,block,sizeof(block));
    regs.m1AA_control=0xC000; regs.m1AC_transferCtrl=4;
    regs.m180_mainVolL=regs.m182_mainVolR=0x3FFF;
    regs.m0_voice[0].m0_volL=regs.m0_voice[0].m2_volR=0x3FFF;
    regs.m0_voice[0].m6_addr=0x2000>>3; regs.m0_voice[0].m4_pitch=0x1000;
    regs.m0_voice[0].m8_adsr1=0x00FF; regs.m0_voice[0].mA_adsr2=0x7F<<6;
    regs.m188_keyOn[0]=1; spuLatch(&regs);
    unsigned n=nativeLimit;
    endTime=n*hwVSyncPeriod(); limit=(uint64_t)(endTime*SPU_RATE+1e-7);
    timerLimit=(unsigned)floor(endTime*240.0);
    pcm=calloc((size_t)limit*2,sizeof(*pcm)); CHECK(pcm);
    hwScheduleAt(1.0/240.0,timer,NULL);
    draw(40); gvkPresent(gpuDisplay(),100); draw(50); gvkPresent(gpuDisplay(),102);
    gvkPresent(gpuDisplay(),103); /* warm the actual intermediate GPU path */
    GvkPresentStats warm; gvkPresentStats(&warm);
    hwPause(0); draw(40);
    uint64_t began=SDL_GetTicksNS();
    while (hwVSyncCount()<n) {
        /* A busy game reaches native safe points without an idle host poll.
         * Exercise mixed active/native fallback with zero host samples. */
        if (noIdle) hwSafePoint(); else if(poll) { hwSafePoint(); hwPresentPoll(); } else hwWaitVSync();
    }
    spuSync(endTime);
    CHECK(pads==n && callbacks==n); CHECK(ticks>=timerLimit); CHECK(samples==limit);
    GvkPresentStats s; gvkPresentStats(&s);
    CHECK(s.nativeFrames-warm.nativeFrames>=n);
    CHECK(s.nativeFrames-warm.nativeFrames==s.autoFrames-warm.autoFrames);
    CHECK(!step || s.hostFrames==0);
    /* A visible display may leave no idle rendering time. The scheduler
     * allows two active native intervals without a host sample before it
     * falls back, so a valid run can mix active and fallback intervals with
     * zero host samples. Check that bounded fallback, rather than requiring
     * every interval to have been inactive. Offscreen runs still require
     * actual high-rate samples. */
    /* Busy GPU contexts may hold the last picture while idle polls retry.
     * Only absence of idle opportunities requires the two-interval fallback. */
    CHECK(!noIdle || maxQuietFrames <= 2);
    CHECK(!hz || step || shader || s.hostFrames>warm.hostFrames ||
          ((visible || noIdle) && fallbacks>0));
    CHECK(!noIdle || (s.hostFrames==warm.hostFrames && maxQuietFrames==2 &&
                     fallbacks>0 && fallbacks<s.nativeFrames-warm.nativeFrames));
    CHECK(!shader || s.hostFrames==0);
    CHECK(s.shown-warm.shown<=s.hostFrames-warm.hostFrames+fallbacks); /* no native+host double presents */
    CHECK(step || s.hostFrames<=(uint64_t)ceil((double)hz*endTime)+2);
    int nonzero=0; for (uint64_t i=0;i<limit*2;i++) nonzero|=pcm[i]; CHECK(nonzero);
    char path[1200]; snprintf(path,sizeof(path),"%s/audio.pcm",argv[2]);
    FILE* out=fopen(path,"wb"); CHECK(out); CHECK(fwrite(pcm,4,(size_t)limit,out)==limit); fclose(out);
    uint16_t* vram=malloc(VRAM_W*VRAM_H*2); CHECK(vram);
    gvkReadback((VRect){0,0,VRAM_W-1,VRAM_H-1},vram,VRAM_W);
    snprintf(path,sizeof(path),"%s/vram.bin",argv[2]); out=fopen(path,"wb"); CHECK(out);
    CHECK(fwrite(vram,2,VRAM_W*VRAM_H,out)==VRAM_W*VRAM_H); fclose(out); free(vram);
    char status[384]; presentationRateDescribe(status,sizeof(status)); puts(status);
    printf("high-refresh: hz=%d step=%d native=%u pads=%u callbacks=%u timers=%u samples=%llu events=%016llx state=%016llx host=%llu shown=%u missed=%llu auto=%llu actualnative=%llu heartbeat=%u seconds=%.6f fallbacks=%u quietmax=%u\n",
        hz,step,n,pads,callbacks,timerLimit,(unsigned long long)limit,(unsigned long long)events,(unsigned long long)nativeState,
        (unsigned long long)(s.hostFrames-warm.hostFrames),s.shown-warm.shown,(unsigned long long)s.missedHostSlots,
        (unsigned long long)(s.autoFrames-warm.autoFrames),(unsigned long long)(s.nativeFrames-warm.nativeFrames),hwHeartbeat(),(SDL_GetTicksNS()-began)*1e-9,fallbacks,maxQuietFrames);
    if (!shader && !visible) {
        /* Separate lifetime controls after the measured native interval. */
        hwPause(1); gpuHostClock(1); presentationSetRate(120);
        draw(40); gvkPresent(gpuDisplay(),100);
        draw(50); gvkPresent(gpuDisplay(),102);
        CHECK(gpuHostDeadline()!=UINT64_MAX);
        int before,after,oldBefore,oldAfter; double alpha;
        CHECK(interpolationSample(gpuDisplay(),102.5,&oldBefore,&oldAfter,&alpha)); CHECK(alpha==0.25);
        draw(60); /* a completed back buffer must wait for native activation */
        CHECK(interpolationSample(gpuDisplay(),102.5,&before,&after,&alpha));
        CHECK(before==oldBefore && after==oldAfter && alpha==0.25);
        gvkPresentStats(&s); uint64_t host=s.hostFrames, missed=s.missedHostSlots;
        double paused=hwNow(); uint32_t vb=hwVSyncCount();
        SDL_Delay(30); hwSafePoint();
        CHECK(hwNow()==paused && hwVSyncCount()==vb);
        gvkPresentStats(&s); CHECK(s.hostFrames==host); /* paused hw never polls */
        uint64_t now=SDL_GetTicksNS(); gpuHostPoll(now,102.5,0);
        gvkPresentStats(&s); CHECK(s.hostFrames==host && gpuHostDeadline()>now);
        CHECK(gpuHostDeadline()<=now+10000); /* retry after the event, don't lose a near-native slot */
        SDL_Delay(10); now=SDL_GetTicksNS(); gpuHostPoll(now,102.5,UINT64_MAX);
        gvkPresentStats(&s); CHECK(s.hostFrames==host+1 && s.missedHostSlots>missed);
        gpuHostPoll(now,102.5,UINT64_MAX); gvkPresentStats(&s); CHECK(s.hostFrames==host+1); /* skip, no burst */
        gpuHostPoll(0,102.5,UINT64_MAX); CHECK(gpuHostDeadline()==UINT64_MAX); /* backward clock */
        gpuStateLoaded(); CHECK(gpuHostDeadline()==UINT64_MAX);
        CHECK(!interpolationSample(gpuDisplay(),103,&before,&after,&alpha));
        hwPause(0); CHECK(gpuHostDeadline()==UINT64_MAX);
        hwMaskInterrupts();
        draw(40); gvkPresent(gpuDisplay(),104); draw(50); gvkPresent(gpuDisplay(),106);
        CHECK(gpuHostDeadline()!=UINT64_MAX);
        gvkPresentStats(&s); host=s.hostFrames;
        SDL_Delay(15); hwSafePoint(); gvkPresentStats(&s); CHECK(s.hostFrames==host);
        hwUnmaskInterrupts(); gpuHostReset(); CHECK(gpuHostDeadline()==UINT64_MAX);
        /* Keep disruptive lifetime tests outside the common native interval:
         * an overloaded host can deliver several vblanks before a wait
         * returns, so resetting at "count >= 45" would compare different
         * reset frames. The actual real-clock gap path still runs here. */
        draw(40); gvkPresent(gpuDisplay(),108); draw(50); gvkPresent(gpuDisplay(),110);
        CHECK(gpuHostDeadline()!=UINT64_MAX);
        if (step) gpuHostReset(); else SDL_Delay(300);
        hwSafePoint(); CHECK(gpuHostDeadline()==UINT64_MAX);
        draw(40); gvkPresent(gpuDisplay(),114); draw(50); gvkPresent(gpuDisplay(),116);
        CHECK(gpuHostDeadline()!=UINT64_MAX);
        gvkPresentStats(&s); host=s.hostFrames;
        gvkPresent(gpuDisplay(),117); CHECK(gpuHostDeadline()!=UINT64_MAX);
        gvkPresent(gpuDisplay(),118); CHECK(gpuHostDeadline()==UINT64_MAX);
        gvkPresentStats(&s); CHECK(s.hostFrames==host && !s.hostActive); /* no idle time: native fallback */
    }
    gpuHostReset(); CHECK(gpuHostDeadline()==UINT64_MAX);
    /* The menu's switch takes effect at once although the setting is
     * cached: its reset already sees the new value. (It replaces the
     * XG_SPEEDUP override, so this runs last.) */
    speedupSetEnabled(0); CHECK(!interpolationEnabled() && !strcmp(interpolationStatus(),"Original presentation"));
    speedupSetEnabled(1); CHECK(interpolationEnabled() && strcmp(interpolationStatus(),"Original presentation"));
    gpuShutdown(); SDL_DestroyWindow(window); SDL_Quit(); free(pcm);
    printf("highrefreshtest: %d checks pass\n",checks); return 0;
}
