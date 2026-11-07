/*
 * XG_CPU_TIMING (cputime.c): where the game thread's time goes, vblank by
 * vblank. Instrumentation only; with the variable unset every hook is a test
 * of cpuTimingOn.
 */
#ifndef PORT_CPUTIME_H
#define PORT_CPUTIME_H

#include <stdint.h>

/* What the hooks time. Each is exclusive: a part timed inside another (the
 * texture cache inside GP0, a fence wait inside either) counts only once. */
enum {
    CPU_GP0,     /* GP0 commands (gp0Write) other than the parts below */
    CPU_SPANS,   /* the triangle walk (gp0.c buildSpans) */
    CPU_CACHE,   /* the texture cache emulation (gp0.c cacheRaster) */
    CPU_SPU,     /* the SPU's samples (spuRun from spuSync) */
    CPU_PRESENT, /* the vblank's picture (gvkPresent) */
    CPU_GPUWAIT, /* waiting for the device (vk.c fence waits: readbacks, frames) */
    CPU_STATE,   /* save-state and rewind captures (savestate.c, rewind.c) */
    CPU_SPIN,    /* vsync_wait=spin: the busy wait for the next event (hw.c spinUntil) */
    CPU_LOAD,    /* pack images read and decoded on the game thread (texture, sprite pose, portrait, Cel) */
    CPU_BKEEP,   /* the 60 Hz Battling's picture between (port/src/game/battling/between.c): the VRAM keep */
    CPU_BMEM,    /* ... its memory records (the sandbox's copies of the module, the heap, the scratchpad) */
    CPU_BDRAW,   /* ... the sandbox render and its draw (less the gp0 parts) */
    CPU_BPUT,    /* ... the VRAM put back */
    CPU_KINDS
};

extern int cpuTimingOn;

void cpuTimeBegin(int kind);
void cpuTimeEnd(int kind);
/* Each vblank (libgpu.c), on the game thread. */
void cpuTimingVBlank(uint32_t vblank);
/* For game C, which has no port headers: part 0..3 = CPU_BKEEP..CPU_BPUT, on
 * 1 begins and 0 ends; the bytes a part copied (which 0 memory, 1 VRAM) go on
 * the vblank's line (bmemb, vramb). */
void cpuTimeBetween(int part, int on);
void cpuTimeBetweenBytes(int which, unsigned long long n);

/* The timing without XG_CPU_TIMING (the settings menu's performance overlay
 * and log, port/src/ui/diagnostics.cpp): each user asks for it (on 1) and lets
 * it go (on 0); it starts and stops at the next vblank. Lines and the exit
 * summary stay XG_CPU_TIMING's only. */
void cpuTimingRequest(int on);
int cpuTimingFromEnv(void); /* XG_CPU_TIMING set (its lines or summary) */
const char* cpuTimingKindName(int kind);
/* A vblank's line as numbers (ms; the dry/cut/starved counts since the last
 * vblank), given to the listener at each vblank while the timing runs. */
typedef struct {
    uint32_t vblank;
    double wall, cpu, game, idle, runq, late;
    double ms[CPU_KINDS];
    unsigned long long bytes[2];
    int dry, cut, starved;
} CpuTimingFrame;
void cpuTimingSetListener(void (*listener)(const CpuTimingFrame* frame));

#define CPU_TIME_BEGIN(kind)       \
    do {                           \
        if (cpuTimingOn) {         \
            cpuTimeBegin(kind);    \
        }                          \
    } while (0)
#define CPU_TIME_END(kind)         \
    do {                           \
        if (cpuTimingOn) {         \
            cpuTimeEnd(kind);      \
        }                          \
    } while (0)

#endif /* PORT_CPUTIME_H */
