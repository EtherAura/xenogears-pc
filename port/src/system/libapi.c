/*
 * LIBAPI and the BIOS kernel services it wraps: events, root counters,
 * critical sections, FlushCache.
 *
 * Events (BIOS OpenEvent and friends): an event is a class (the source: a
 * root counter, the SPU, the memory card...), a spec (what happened) and a
 * mode. DeliverEvent(class, spec) runs the handler of each enabled
 * EvMdINTR event, or marks an enabled EvMdNOINTR event delivered for
 * TestEvent. One PC difference: an EvMdINTR event delivered while disabled
 * runs as soon as it is enabled again instead of being lost. The sound driver
 * brackets its state edits with DisableEvent/EnableEvent on its 240 Hz root
 * counter tick; on the PS1 a tick rarely lands in such a window, on the PC
 * one can whenever the window contains a safe point.
 *
 * Root counters: counts are computed from the clock; an interrupt-enabled
 * counter in target mode raises its event at each target (counter 2 at
 * 33.8688 MHz / 8 / 0x44E8 = 240 Hz for the sound driver).
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "system.h"
#include "savestate.h"

#ifdef _WIN32
/* kernel32 has an EnterCriticalSection of its own, which the C runtime links
 * in: the PsyQ one is named apart on Windows, here and in the game C
 * (port/CMakeLists.txt). */
#define EnterCriticalSection psxEnterCriticalSection
#endif

#define MAX_EVENTS 32
#define EV_DESC 0xF1000000u
#define EVST_UNUSED 0
#define EVST_WAIT 0x1000
#define EVST_ACTIVE 0x2000
#define EVST_ALREADY 0x4000
#define EVMD_INTR 0x1000
#define EVMD_NOINTR 0x2000

typedef struct {
    uint32_t cls;
    s32 spec;
    s32 mode;
    s32 status;
    void (*func)(void);
    int pending; /* EvMdINTR, delivered while disabled */
} Event;

static Event events[MAX_EVENTS];
static int testMisses;

static Event* eventOf(u32 ev) {
    u32 i = ev & 0xFFFF;
    if ((ev & 0xFFFF0000u) != EV_DESC || i >= MAX_EVENTS || events[i].status == EVST_UNUSED) {
        return NULL;
    }
    return &events[i];
}

static void runHandler(Event* e) {
    if (e->func != NULL) {
        e->func();
    }
}

void DeliverEvent(uint32_t cls, int32_t spec) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        Event* e = &events[i];
        if (e->status == EVST_UNUSED || e->cls != cls || e->spec != spec) {
            continue;
        }
        if (e->mode == EVMD_INTR) {
            if (e->status == EVST_ACTIVE) {
                runHandler(e);
            } else if (e->status == EVST_WAIT) {
                e->pending = 1;
            }
        } else if (e->status == EVST_ACTIVE) {
            e->status = EVST_ALREADY;
        }
    }
}

/* An EvMdINTR event delivered while disabled, now enabled: run it as an interrupt. */
static void deliverPending(void* arg) {
    Event* e = arg;
    if (e->pending && e->status == EVST_ACTIVE) {
        e->pending = 0;
        runHandler(e);
    }
}

s32 OpenEvent(u32 cls, s32 spec, s32 mode, void* func) {
    hwSafePoint();
    for (int i = 0; i < MAX_EVENTS; i++) {
        Event* e = &events[i];
        if (e->status == EVST_UNUSED) {
            e->cls = cls;
            e->spec = spec;
            e->mode = mode;
            e->func = (void (*)(void))func;
            e->status = EVST_WAIT;
            e->pending = 0;
            return (s32)(EV_DESC | (u32)i);
        }
    }
    fprintf(stderr, "OpenEvent: no free event\n");
    return -1;
}

s32 CloseEvent(u32 ev) {
    hwSafePoint();
    Event* e = eventOf(ev);
    if (e == NULL) {
        return 0;
    }
    e->status = EVST_UNUSED;
    e->pending = 0;
    return 1;
}

s32 EnableEvent(u32 ev) {
    hwSafePoint();
    Event* e = eventOf(ev);
    if (e == NULL) {
        return 0;
    }
    e->status = EVST_ACTIVE;
    if (e->pending) {
        hwScheduleAt(hwNow(), deliverPending, e);
        hwSafePoint();
    }
    return 1;
}

s32 DisableEvent(u32 ev) {
    hwSafePoint();
    Event* e = eventOf(ev);
    if (e == NULL) {
        return 0;
    }
    e->status = EVST_WAIT;
    return 1;
}

s32 TestEvent(u32 ev) {
    hwSafePoint();
    Event* e = eventOf(ev);
    if (e != NULL && e->status == EVST_ALREADY) {
        e->status = EVST_ACTIVE;
        testMisses = 0;
        return 1;
    }
    /* The game polls TestEvent in loops without a safe point of their own
     * (waitMemoryCardEvent): after a while, let the clock run. */
    if (++testMisses >= 64) {
        testMisses = 0;
        hwYield();
    }
    return 0;
}

s32 UnDeliverEvent(u32 cls, s32 spec) {
    hwSafePoint();
    for (int i = 0; i < MAX_EVENTS; i++) {
        Event* e = &events[i];
        if (e->status == EVST_ALREADY && e->cls == cls && e->spec == spec) {
            e->status = EVST_ACTIVE;
        }
    }
    return 1;
}

/* BIOS B(0Bh) TestEvent and B(20h) UnDeliverEvent, called through the game's
 * own BIOS stubs (asm/us/main/30C54.s). */
s32 func_80040494(u32 ev) {
    return TestEvent(ev);
}

void func_800404C4(u32 cls, s32 spec) {
    UnDeliverEvent(cls, spec);
}

/* ---- root counters ---- */

typedef struct {
    u16 mode;       /* the counter's mode register, as SetRCnt writes it */
    u16 target;
    int irqEnabled; /* StartRCnt */
    double base;    /* when the count was 0 */
    double nextIrq;
    uint32_t generation;
} RootCounter;

static RootCounter counters[3] = {
    {0, 0, 0, 0, 0, 0},
    {0x100, 0, 0, 0, 0, 0}, /* counter 1 counts horizontal lines (LIBETC sets it up so) */
    {0, 0, 0, 0, 0, 0},
};

static double counterRate(int n) {
    const RootCounter* c = &counters[n];
    switch (n) {
    case 0:
        return (c->mode & 0x100) ? hwDotClockRate() : HW_CPU_HZ;
    case 1:
        return (c->mode & 0x100) ? hwHBlankRate() : HW_CPU_HZ;
    default:
        return (c->mode & 0x200) ? HW_CPU_HZ / 8 : HW_CPU_HZ;
    }
}

static u32 counterPeriod(int n) {
    const RootCounter* c = &counters[n];
    if ((c->mode & 0x8) && c->target != 0) {
        return c->target;
    }
    return 0x10000;
}

static void counterInterrupt(void* arg) {
    uptr v = (uptr)arg;
    int n = (int)(v & 3);
    RootCounter* c = &counters[n];

    if ((uint32_t)(v >> 2) != c->generation || !c->irqEnabled) {
        return; /* stopped or reprogrammed since */
    }
    double period = counterPeriod(n) / counterRate(n);
    double now = hwNow();
    c->nextIrq += period;
    if (c->nextIrq < now - 0.25) {
        c->nextIrq = now + period; /* a long stall: skip the backlog */
    }
    hwScheduleAt(c->nextIrq, counterInterrupt, arg);
    DeliverEvent(EV_CLASS_RCNT(n), EV_SPEC_INT);
}

static void reprogram(int n) {
    RootCounter* c = &counters[n];

    c->generation++;
    if (c->irqEnabled && (c->mode & 0x10)) {
        c->nextIrq = c->base + counterPeriod(n) / counterRate(n);
        hwScheduleAt(c->nextIrq, counterInterrupt, (void*)(((uptr)c->generation << 2) | (uptr)n));
    }
}

s32 SetRCnt(u32 spec, u16 target, s32 mode) {
    hwSafePoint();
    int n = spec & 0xFFFF;
    u16 m = 0x48; /* reset at target, repeat */

    if (n >= 3) {
        return 0;
    }
    if (n < 2) {
        if (mode & 0x10) {
            m = 0x49;
        }
        if (!(mode & 1)) {
            m |= 0x100;
        }
    } else if (!(mode & 1)) {
        m = 0x248;
    }
    if (mode & 0x1000) {
        m |= 0x10; /* interrupt at target */
    }
    counters[n].mode = m;
    counters[n].target = target;
    counters[n].base = hwNow(); /* writing the mode resets the count */
    reprogram(n);
    return 1;
}

s32 GetRCnt(u32 spec) {
    hwSafePoint();
    int n = spec & 0xFFFF;

    if (n >= 3) {
        return 0;
    }
    double ticks = (hwNow() - counters[n].base) * counterRate(n);
    return (s32)((uint64_t)ticks % counterPeriod(n));
}

s32 StartRCnt(u32 spec) {
    hwSafePoint();
    int n = spec & 0xFFFF;

    if (n >= 3) {
        return 0;
    }
    if (!counters[n].irqEnabled) {
        counters[n].irqEnabled = 1;
        reprogram(n);
    }
    return 1;
}

s32 StopRCnt(u32 spec) {
    hwSafePoint();
    int n = spec & 0xFFFF;

    if (n < 3) {
        counters[n].irqEnabled = 0;
        counters[n].generation++;
    }
    return 1;
}

s32 ResetRCnt(u32 spec) {
    hwSafePoint();
    int n = spec & 0xFFFF;

    if (n >= 3) {
        return 0;
    }
    counters[n].base = hwNow();
    reprogram(n);
    return 1;
}

/* ---- critical sections ---- */

/* The PS1's interrupt enable is one flag, not a nesting count: a second
 * Enter does nothing and one Exit re-enables. */
static int critical;

void EnterCriticalSection(void) {
    hwSafePoint();
    if (!critical) {
        critical = 1;
        hwMaskInterrupts();
    }
}

void ExitCriticalSection(void) {
    if (critical) {
        critical = 0;
        hwUnmaskInterrupts();
    }
    hwSafePoint();
}

void SwEnterCriticalSection(void) {
    EnterCriticalSection();
}

void SwExitCriticalSection(void) {
    ExitCriticalSection();
}

/* Overlay code is linked in, so there is no instruction cache to flush. */
void FlushCache(void) {
    hwSafePoint();
}

/* ---- a world of its own (world.h) ---- */

/* What a world has of this file: its events, root counters and critical
 * section, so the game code it runs (the sound driver's OpenEvent, SetRCnt,
 * StartRCnt) sets up its own and never the paused game's. */
typedef struct {
    Event events[MAX_EVENTS];
    int testMisses;
    RootCounter counters[3];
    int critical;
} ApiPart;

size_t apiWorldPartSize(void) {
    return sizeof(ApiPart);
}

void apiWorldPartInit(void* blob) {
    ApiPart* a = blob;
    memset(a, 0, sizeof(*a));
    a->counters[1].mode = 0x100; /* as at power-on (counters[]'s initialiser) */
}

void apiWorldPartSwap(void* blob) {
    ApiPart* a = blob;
    ApiPart mine;
    memcpy(mine.events, events, sizeof(events));
    mine.testMisses = testMisses;
    memcpy(mine.counters, counters, sizeof(counters));
    mine.critical = critical;
    memcpy(events, a->events, sizeof(events));
    testMisses = a->testMisses;
    memcpy(counters, a->counters, sizeof(counters));
    critical = a->critical;
    *a = mine;
}

/* ---- save states (savestate.h) ---- */

/* The BIOS events (handlers are game code), the root counters (their times
 * are the virtual clock's; the generation matches the queued interrupts'
 * arguments) and the critical section. */
void apiStateIo(XgState* s) {
    XG_STATE_VAR(s, events);
    XG_STATE_VAR(s, testMisses);
    XG_STATE_VAR(s, counters);
    XG_STATE_VAR(s, critical);
}
