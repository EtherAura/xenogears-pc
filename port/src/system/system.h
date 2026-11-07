#ifndef PORT_SYSTEM_H
#define PORT_SYSTEM_H

/*
 * Internals shared by the files of port/src/system (the clock and interrupts,
 * LIBETC, LIBAPI, the pads, the SPU). Other subsystems use hw.h; the BIOS
 * event calls below (DeliverEvent and friends) are the PS1's own API, so the
 * CD and GPU layers may call them too.
 */

#include <stdint.h>

#include "hw.h"

/* PS1 clocks: the CPU's and the GPU's are in hw.h. */
#define HW_SPU_HZ (HW_CPU_HZ / 768)

/* hw.c */
double hwHBlankRate(void);    /* horizontal lines per second (timer 1's hblank clock) */
double hwDotClockRate(void);  /* pixels per second of the display mode (timer 0's dot clock) */
int hwInInterrupt(void);      /* a callback is being delivered */
double hwVBlankNow(void);     /* vblanks so far, with the fraction of the current frame */
double hwVBlankToTime(double vblank); /* hwNow() when that fractional vblank count is reached */
void hwSetVideoStandard(int pal);

/* libetc.c: the vblank interrupt's LIBETC part (counter and callbacks). */
void etcVBlank(void);
void etcSoftReset(void);      /* LIBETC's state as the PS1's cleared BSS leaves it */

/* reset.c: the soft reset (startKernel unwinds to main(), which calls this). */
void portSoftReset(void);

/* quit.c: window close, SIGINT and SIGTERM end the game at its next safe point. */
void quitInit(void);          /* signal handlers and the watchdog */
void quitRequest(void);       /* ask for a quit (any thread) */
int quitRequested(void);
void quitPoll(int pump);      /* quit now if asked; pump: also look for the window closing */
void quitNow(void);           /* shut down and exit (game thread) */

/* stall.c: notices a game thread that reaches no safe point (a game loop
 * that waits for an interrupt and calls nothing) and reports it. */
void stallInit(void);         /* on the game thread, from hwInit */
uint32_t hwHeartbeat(void);   /* hw.c: safe points so far (any thread) */

/* slowdown.c: the PS1's frame timing replayed from a Redux log (XG_SLOWDOWN). */
void slowdownVSync(int32_t arg);     /* each VSync call, before it does anything */
void slowdownVSyncReturn(const void* site); /* each VSync call, as it returns (site: its caller) */
void slowdownModeEntry(int mode);    /* bootGame starts a game mode (memory.c) */
int slowdownLogVBlank(int64_t* vblank); /* lined up: this vblank on the log's clock */
int slowdownBiosMissed(void);        /* lined up: the PS1's BIOS pad/card handler skipped this vblank */
void slowdownSafePoint(void);        /* each safe point that can deliver interrupts */
void slowdownBoot(void);             /* the game installs its vsync callback (libetc.c) */

/* krom.c: the BIOS image the kanji glyphs come from (the settings menu). */
const char* kromPath(void);          /* NULL: none read */
int kromSetBios(const char* path);   /* read another ("none": no glyphs); 0 if it is no BIOS image */

/* bios.c: the SN debugger's host files (PCopen, PClseek, PCread, PCclose)
 * served by someone, or NULL (the default: they fail, as without a
 * debugger). The extras' sandbox serves them from a disc image while game
 * code runs inside it (port/src/extras/hostfiles.c). */
typedef struct PcHostFiles {
    void* user;
    int32_t (*open)(void* user, const char* name);                 /* fd, -1 */
    int32_t (*seek)(void* user, int32_t fd, int32_t offset, int32_t mode); /* new position, -1 */
    int32_t (*read)(void* user, int32_t fd, void* buf, uint32_t len);      /* bytes read, -1 */
    int32_t (*close)(void* user, int32_t fd);                      /* 0, -1 */
} PcHostFiles;
void biosSetHostFiles(const PcHostFiles* files);

/* pad.c */
void padInit(void);
void padVBlank(void);         /* poll SDL, fill the InitPAD buffers, rumble */
void padPumpEvents(void);     /* the window's events (the settings menu's loop pumps them too) */

/* spu.c */
void spuSync(double now);     /* run the SPU to `now`, then take the register writes since the last sync */

/* libapi.c: BIOS events (classes and specs as in the PsyQ headers). */
#define EV_CLASS_SPU 0xF0000009u
#define EV_CLASS_RCNT(n) (0xF2000000u | (uint32_t)(n))
#define EV_CLASS_SWCARD 0xF4000001u
#define EV_SPEC_INT 0x0002
#define EV_SPEC_IOE 0x0004
#define EV_SPEC_COMP 0x0020
#define EV_SPEC_TIMEOUT 0x0100
#define EV_SPEC_NEW 0x2000
#define EV_SPEC_ERROR 0x8000

void DeliverEvent(uint32_t ev, int32_t spec);
void rcntVBlank(void);

/* world.c's parts (world.h): what each file keeps for a world of its own, in
 * a blob of *Size bytes: *Init makes a fresh one, *Swap exchanges it with the
 * file's state (entering and leaving a world are the same swap). */
#include <stddef.h>
size_t hwWorldPartSize(void);   /* hw.c: the event queue, the clock, masking */
void hwWorldPartInit(void* blob);
void hwWorldPartFree(void* blob);
void hwWorldPartSwap(void* blob);
size_t apiWorldPartSize(void);  /* libapi.c: BIOS events, root counters, critical section */
void apiWorldPartInit(void* blob);
void apiWorldPartSwap(void* blob);
size_t spuWorldPartSize(void);  /* spu.c: LIBSPU's state, the SPU's sync clock, its output */
void spuWorldPartInit(void* blob);
void spuWorldPartSwap(void* blob);
void spuWorldPowerOn(void);     /* inside: what SpuInit leaves (without ResetCallback) */
void spuWorldSetSink(void (*fn)(void* user, const int16_t* frames, uint32_t count), void* user);
void spuWorldSetRun(int on);    /* inside: 0 keeps time without running the SPU */

#endif /* PORT_SYSTEM_H */
